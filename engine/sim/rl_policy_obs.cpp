// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// In-process RL transport, Stage 1 (read_state) and Stage 2 (build_obs/
// build_mask/build_wait) -- tstl-sylvanas phase 210, plan 210-04. See
// rl_policy.hpp's own header comment for the NORMATIVE-source note: this
// file is an implementation of `scripts/rl/obs.py` and `scripts/rl/mask.py`
// (this repo), never the other way around.
//
// TWO DELIBERATE GAPS IN THIS SLICE (objective, 210-04-PLAN.md):
//   - build_obs takes the "absent" branch for every non-derived field --
//     plan 210-05 adds the `_lookup` tri-state (present/permanent) into
//     this same loop, at the marked comment below.
//   - build_mask/build_wait/cd_ready_now are REAL, not stubbed -- the mask
//     is what prevents a not-ready cast from reaching protocol_abort.

#include "sim/rl_policy.hpp"

#include "action/attack.hpp"
#include "buff/buff.hpp"
#include "player/player.hpp"
#include "sim/cooldown.hpp"
#include "sim/decision_dump.hpp"
#include "sim/event.hpp"
#include "sim/sim.hpp"

#include "fmt/format.h"

#include <algorithm>
#include <cstring>

namespace rl_policy
{
// ---------------------------------------------------------------------------
// rl_state_t lookups -- absent is a distinct state, never a zero default.
// ---------------------------------------------------------------------------

const buff_reading* rl_state_t::find_buff( const char* name ) const
{
  for ( const buff_reading& b : buffs )
  {
    if ( b.name == name )
      return &b;
  }
  return nullptr;
}

const cooldown_reading* rl_state_t::find_cooldown( const char* name ) const
{
  for ( const cooldown_reading& c : cooldowns )
  {
    if ( c.name == name )
      return &c;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Stage 1: read_state -- needs the engine, not exercised by the standalone
// test executable (plan 210-07).
// ---------------------------------------------------------------------------

rl_state_t read_state( const player_t* p, bool boundary_is_foreground )
{
  rl_state_t s;
  sim_t* sim = p->sim;

  s.t = sim->current_time().total_seconds();
  s.boundary_is_foreground = boundary_is_foreground;

  // Same formula as decision_dump.cpp:355's gcd_remains -- shared helper,
  // not a re-derived expression, so the two can never drift.
  s.gcd_remains = decision_dump::clamp_nonneg( ( p->gcd_ready - sim->current_time() ).total_seconds() );
  s.has_gcd_remains = true;

  // Same source decision_dump.cpp's swing-timer emitter reads.
  if ( p->main_hand_attack && p->main_hand_attack->execute_event )
  {
    s.swing_mh_remains = p->main_hand_attack->execute_event->remains().total_seconds();
    s.has_swing_mh_remains = true;
  }

  // Same active_enemies derivation decision_dump.cpp's emitter uses:
  // a single-target fight with no adds/pull raid events collapses to the
  // constant 1, otherwise the live sim_t::active_enemies counter.
  {
    int active_enemies = 1;
    if ( !( sim->target_list.size() == 1U && !sim->has_raid_event( "adds" ) && !sim->has_raid_event( "pull" ) ) )
      active_enemies = sim->active_enemies;
    s.active_enemies = static_cast<double>( active_enemies );
    s.has_active_enemies = true;
  }

  // Buffs -- same walk decision_dump's emitter uses, skipping
  // buff->check() <= 0 exactly as it does. `permanent` cannot currently be
  // produced by this live walk for this spec (R4 SS1.4) -- always false
  // here. Documentation fix (210-CR-FIX, per 210-VERIFICATION.md's own
  // finding at rl_policy_obs.cpp:97-100): this comment used to point at
  // plan 210-07's hand-written 20-case corpus as "what exercises the
  // `permanent` leaf" -- 210-07 was SUPERSEDED outright under ruling N-8
  // (210-05R-PLAN.md frontmatter) and will never be built, so that named a
  // non-existent future exerciser. The `permanent` leg is DELIBERATELY
  // unexercised for this MVP: `absent`/`present` are exercised by every
  // live decision boundary (200-616 per smoke seed), but nothing in this
  // phase or planned after it currently drives a buff row with
  // `permanent == true` through this walk. See 210-VERIFICATION.md's
  // `behavior_unverified_items` for the full paper trail; this is a
  // recorded gap, not an oversight.
  //
  // WR-02 fix, cheap half (210-CR-FIX): reserve() against the actor's full
  // buff_list size (an upper bound -- not every entry passes the
  // buff->check() <= 0 filter below) so push_back() below never reallocates
  // on this per-boundary hot path.
  s.buffs.reserve( p->buff_list.size() );
  for ( buff_t* buff : p->buff_list )
  {
    if ( buff->check() <= 0 )
      continue;
    buff_reading b;
    b.name = buff->name();
    b.present = true;
    b.stacks = static_cast<double>( buff->check() );
    b.has_stacks = true;
    b.permanent = false;
    s.buffs.push_back( std::move( b ) );
  }

  // Cooldowns -- same walk decision_dump's emitter uses (every named
  // cooldown with a nonzero base recharge; skips the zero-duration
  // bookkeeping cooldowns SimC creates internally).
  //
  // WR-04 fix (210-CR-FIX): decision_dump::write_state_fields
  // (decision_dump.cpp:429/451) gates `charges_fractional` on
  // `emit_maelstrom_ext = p->resources.is_active( RESOURCE_MAELSTROM )` --
  // the SC-5 byte-neutrality contract. This walk used to write it
  // unconditionally, so obs slot 1 (`cooldown.strike.charges_fractional`,
  // missing=2.0, div=2) could read the live value for a non-maelstrom
  // actor where the FIFO transport's wire request would have omitted the
  // key entirely (-> `absent` -> `1.0`), a second cross-transport
  // divergence source under the same green fingerprints. Mirror the same
  // predicate here so `read_state` is honest about "the same walk
  // decision_dump's emitter uses" for every field it claims to share, not
  // just the ones CR-01's actor filter happens to make reachable today.
  // WR-02 fix, cheap half: same reserve() treatment as buffs above.
  const bool emit_maelstrom_ext = p->resources.is_active( RESOURCE_MAELSTROM );
  s.cooldowns.reserve( p->cooldown_list.size() );
  for ( cooldown_t* cd : p->cooldown_list )
  {
    if ( cd->duration <= timespan_t::zero() && cd->base_duration <= timespan_t::zero() )
      continue;
    cooldown_reading c;
    c.name = cd->name_str;
    c.present = true;
    c.remains = cd->remains().total_seconds();
    c.has_remains = true;
    c.recharge_time = cd->recharge_event ? cd->recharge_event->remains().total_seconds() : 0.0;
    c.has_recharge_time = true;
    c.charges = static_cast<double>( cd->current_charge );
    c.has_charges = true;
    if ( emit_maelstrom_ext )
    {
      c.charges_fractional = cd->charges_fractional();
      c.has_charges_fractional = true;
    }
    c.max_charges = cd->charges;
    s.cooldowns.push_back( std::move( c ) );
  }

  // Deliberately NOT read (each is out of scope for this schema or belongs
  // to a later phase, not an oversight): solver_damage_so_far (the reward,
  // Phase 212's log), holy_power, target_debuffs, target_time_to_die, dots,
  // gcd_length, auto_attack_interval, resolved_action.

  return s;
}

// ---------------------------------------------------------------------------
// Stage 2 helpers -- PURE over the POD, mirroring obs.py's
// _apply_scale / _encode_bucket_ordinal exactly.
// ---------------------------------------------------------------------------

namespace
{
double apply_scale( double raw, rl_kind kind, const rl_obs_field& f )
{
  switch ( kind )
  {
    case rl_kind::k_int:
    case rl_kind::k_float:
    {
      const double div = f.has_div ? f.div : 1.0;
      return raw / div;
    }
    case rl_kind::k_seconds:
    {
      if ( f.has_clip_div )
      {
        const double clipped = std::max( std::min( raw, f.clip_div ), 0.0 );
        return clipped / f.clip_div;
      }
      const double div = f.has_div ? f.div : 1.0;
      return raw / div;
    }
    case rl_kind::k_bucket:
      // Bucket fields are dispatched by the caller before this function is
      // ever reached (mirrors obs.py's own `if kind == "bucket"` branch,
      // which never calls _apply_scale) -- unreachable in practice.
      return raw;
  }
  return raw;
}

double encode_bucket_ordinal( double raw, const double* buckets, std::size_t n )
{
  // Floor-matched to the largest bucket boundary <= raw (index 0 if raw is
  // below every boundary): scan ascending, keep the LAST boundary
  // satisfied with >=, no break -- mirrors obs.py:207-215 exactly.
  std::size_t idx = 0;
  for ( std::size_t i = 0; i < n; ++i )
  {
    if ( raw >= buckets[ i ] )
      idx = i;
  }
  const double denom = static_cast<double>( n > 1 ? n - 1 : 1 );
  return static_cast<double>( idx ) / denom;
}

// ---------------------------------------------------------------------------
// lookup_status -- the tri-state leaf lookup (210-05R Task 2), mirroring
// obs.py::_lookup (obs.py:98-145) EXACTLY, but over the descriptor's
// PRE-SPLIT (container, key, leaf) rather than splitting a dotted string
// (ruling 210-G14/Pattern 2 -- the generator pre-splits the source so this
// C++ never parses a string at runtime). Three outcomes:
//   absent    -- the named buff/cooldown row (or scalar leaf) does not
//                exist, or exists but the requested (RECOGNISED) leaf has
//                no value and is not permanent (WR-04's conflation --
//                deliberate, no fourth status).
//   present   -- the leaf has a real value, written to `out_val`.
//   permanent -- (buff only) the leaf is null but its containing buff
//                object carries `permanent == true` (PROTOCOL.md's
//                no-scheduled-expiration representation). `out_val` is
//                left unwritten -- callers branch on the returned status,
//                never on `out_val`, for this case (mirrors _lookup's own
//                `value` being `None` here).
//
// WR-03(b) fix (210-CR-FIX): an UNRECOGNISED leaf name -- a string this
// switch has not been taught, as opposed to a recognised leaf that simply
// has no value right now -- used to fail open to `absent` in every branch
// below. That is the wrong default for a value the net consumes: a future
// registry field this loader has not been taught yet would regenerate the
// header (fingerprints all stay green, since none of the three hashes this
// loader's own C-string literals) and then silently encode as the field's
// `missing` sentinel forever, with both transports reporting success while
// disagreeing on the observation vector. Every genuinely-unrecognised-leaf
// fallthrough below now throws, naming the slot/container/key/leaf, instead
// of returning `absent` -- WR-04's conflation (a RECOGNISED leaf whose
// value is not currently populated) is untouched, since that is a real,
// intentional encoding, not a registry/loader mismatch.
// ---------------------------------------------------------------------------

enum class lookup_status { absent, present, permanent };

[[noreturn]] void throw_unrecognised_leaf( const rl_obs_field& f, const char* container_name )
{
  throw sc_runtime_error( fmt::format(
      "rl_policy::lookup_leaf: observation field slot={} container='{}' key='{}' names "
      "unrecognised leaf '{}' -- this loader has not been taught this leaf name; a silent "
      "'absent' here would let the net see a wrong number instead of a loud stop (WR-03)",
      f.slot, container_name, f.key, f.leaf ) );
}

lookup_status lookup_leaf( const rl_state_t& s, const rl_obs_field& f, double& out_val )
{
  switch ( f.container )
  {
    case rl_container::scalar:
    {
      // `key` is unused for scalar fields (there is no container object to
      // key into); `leaf` names a top-level scalar of rl_state_t directly.
      // `fight_remains` is `derived` and never reaches this function (see
      // build_obs' own dispatch below). This schema has exactly one
      // registered scalar leaf today (`active_enemies`).
      if ( std::strcmp( f.leaf, "active_enemies" ) == 0 )
      {
        if ( s.has_active_enemies )
        {
          out_val = s.active_enemies;
          return lookup_status::present;
        }
        // Recognised leaf, no value yet -- WR-04's conflation, not WR-03's
        // unrecognised-leaf case.
        return lookup_status::absent;
      }
      throw_unrecognised_leaf( f, "scalar" );
      return lookup_status::absent;   // unreachable -- throw_unrecognised_leaf always throws
    }

    case rl_container::buff:
    {
      const buff_reading* b = s.find_buff( f.key );
      if ( !b )
        return lookup_status::absent;
      // WR-01 fix (210-CR-FIX): the leaf is read FIRST, `permanent` tested
      // only once the leaf itself has no value -- matching obs.py::_lookup
      // (obs.py:146-165), which reaches `node.get("permanent")` only after
      // `leaf is None`. The prior C++ order tested `permanent` before ever
      // looking at the leaf, so a buff row with `permanent == true` AND a
      // real `stacks` value (the exact shape decision_dump.cpp:325-328
      // emits -- `"remains":null,"permanent":true` alongside a live
      // `"stacks":N`) encoded to RL_PERMANENT_SATURATION here while Python
      // encoded the real stacks value. This schema's only buff leaf is
      // "stacks" today.
      if ( std::strcmp( f.leaf, "stacks" ) == 0 )
      {
        if ( b->has_stacks )
        {
          out_val = b->stacks;
          return lookup_status::present;
        }
        if ( b->permanent )
          return lookup_status::permanent;
        // Found, but the requested leaf has no value and the buff is not
        // permanent -- WR-04's conflation, deliberate, no fourth status.
        return lookup_status::absent;
      }
      throw_unrecognised_leaf( f, "buff" );
      return lookup_status::absent;   // unreachable -- throw_unrecognised_leaf always throws
    }

    case rl_container::cooldown:
    {
      const cooldown_reading* c = s.find_cooldown( f.key );
      if ( !c )
        return lookup_status::absent;
      // `permanent` is not a cooldown concept -- obs.py checks
      // `node.get("permanent")` on the CONTAINING object, and only buff
      // objects carry it (rl_policy.hpp's cooldown_reading has no such
      // field at all).
      if ( std::strcmp( f.leaf, "charges_fractional" ) == 0 )
      {
        if ( c->has_charges_fractional )
        {
          out_val = c->charges_fractional;
          return lookup_status::present;
        }
        return lookup_status::absent;   // WR-04's conflation
      }
      if ( std::strcmp( f.leaf, "remains" ) == 0 )
      {
        if ( c->has_remains )
        {
          out_val = c->remains;
          return lookup_status::present;
        }
        return lookup_status::absent;   // WR-04's conflation
      }
      if ( std::strcmp( f.leaf, "recharge_time" ) == 0 )
      {
        if ( c->has_recharge_time )
        {
          out_val = c->recharge_time;
          return lookup_status::present;
        }
        return lookup_status::absent;   // WR-04's conflation
      }
      if ( std::strcmp( f.leaf, "charges" ) == 0 )
      {
        if ( c->has_charges )
        {
          out_val = c->charges;
          return lookup_status::present;
        }
        return lookup_status::absent;   // WR-04's conflation
      }
      if ( std::strcmp( f.leaf, "max_charges" ) == 0 )
      {
        // max_charges carries no has_* flag in rl_policy.hpp -- it is
        // always populated (defaults to 1) once the cooldown row itself
        // is present, so a matching row always answers `present` for it.
        out_val = static_cast<double>( c->max_charges );
        return lookup_status::present;
      }
      throw_unrecognised_leaf( f, "cooldown" );
      return lookup_status::absent;   // unreachable -- throw_unrecognised_leaf always throws
    }
  }
  // Unreachable: rl_container is a 3-value enum class and every case above
  // either returns or throws. No trailing `return absent` -- an unhandled
  // enumerator is a compiler warning (-Wswitch), not a silent absent.
  throw sc_runtime_error( fmt::format(
      "rl_policy::lookup_leaf: observation field slot={} has an unhandled container enumerator",
      f.slot ) );
}
} // anonymous namespace

// ---------------------------------------------------------------------------
// build_obs -- REAL, all three leaf statuses (210-05R Task 2). Dispatch
// order mirrors obs.py:232-287 exactly: derived fields force `present`;
// otherwise `lookup_leaf` decides; `kind == bucket` is tested BEFORE the
// `permanent` branch (a permanent bucket is a registry misconfiguration,
// not a value to saturate); non-bucket `permanent` saturates to
// RL_PERMANENT_SATURATION; everything else goes through apply_scale.
// ---------------------------------------------------------------------------

void build_obs( const rl_state_t& s, float out_obs[ RL_OBS_DIM ] )
{
  for ( std::size_t slot = 0; slot < RL_OBS_DIM; ++slot )
  {
    const rl_obs_field& f = RL_OBS_FIELDS[ slot ];

    double raw = 0.0;
    lookup_status status;

    if ( f.derived )
    {
      // Only fight_remains is derived in this schema: episode.maxTime - t,
      // floored at 0 (obs.py:236-243).
      raw = std::max( RL_EPISODE_MAX_TIME - s.t, 0.0 );
      status = lookup_status::present;
    }
    else
    {
      status = lookup_leaf( s, f, raw );
      if ( status == lookup_status::absent )
        raw = f.missing;
      // status == present: `raw` was already written by lookup_leaf.
      // status == permanent: `raw` is left unused below, matching
      // obs.py's `raw = None` for this branch -- neither the bucket-absent
      // path nor apply_scale() is ever reached with a permanent status.
    }

    double encoded;
    if ( f.kind == rl_kind::k_bucket )
    {
      // obs.py:253-278: an "absent" bucket leaf encodes its `missing`
      // value DIRECTLY, bypassing the ordinal floor-match entirely
      // (WR-03 -- -1.0 must stay distinct from every legitimate ordinal
      // 0.0/0.5/1.0). A "permanent" bucket leaf is an ERROR in Python
      // (obs.py:281, raises naming the slot) -- no bucket field in this
      // schema describes a buff, so this is a loud registry-misconfiguration
      // refusal on both sides, never a silent saturation.
      if ( status == lookup_status::absent )
      {
        encoded = f.missing;
      }
      else if ( status == lookup_status::permanent )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy::build_obs: observation field slot={} container='{}' key='{}' is "
            "kind=bucket but its leaf resolved to status=permanent -- no bucket field can "
            "legitimately be permanent (permanence is a buff-expiry concept); this is a "
            "registry/request misconfiguration, not a valid build_obs() input",
            f.slot, f.container == rl_container::buff ? "buff" : "cooldown", f.key ) );
      }
      else
      {
        encoded = encode_bucket_ordinal( raw, f.buckets, f.n_buckets );
      }
    }
    else if ( status == lookup_status::permanent )
    {
      encoded = RL_PERMANENT_SATURATION;
    }
    else
    {
      encoded = apply_scale( raw, f.kind, f );
    }

    // Arithmetic in double throughout; assign to the float output only at
    // the very end (AP-7).
    out_obs[ slot ] = static_cast<float>( encoded );
  }
}

// Sanity check (comment only, not executable): with the L0 enhancement
// registry's cooldown/buff sets both empty at boundary_is_foreground's
// default, an entirely-empty `cooldowns` set makes slot 1 (strike's
// charges_fractional, missing=2.0, div=2) encode via the absent branch to
// `2.0/2 = 1.0`, never `0.0` -- and an always-up buff (`permanent == true`)
// encodes to `RL_PERMANENT_SATURATION == 1.0`, never to its `missing`
// value. Both traps (P-1/P-2) are closed by the dispatch above, not by a
// special case.

// ---------------------------------------------------------------------------
// cd_ready_now -- ported COMPLETE, including the fourth line, from
// simc_channel.py:269-298. THE TRAP THAT DOCSTRING NAMES: this function's
// body is FOUR lines, and the fourth line -- the plain single-charge
// `return remains <= 0.0` fallback -- is the ENTIRE single-charge contract,
// not a default to trim. Losing it makes every single-charge spell read as
// permanently castable.
// ---------------------------------------------------------------------------

namespace
{
bool cd_ready_now( const cooldown_reading& row )
{
  // A JSON null `remains` coerces to 0.0, i.e. ready -- so an absent
  // has_remains reads as ready, matching `.get("remains", 0.0) or 0.0`.
  const double remains = row.has_remains ? row.remains : 0.0;
  if ( row.max_charges > 1 )
  {
    const double charges = row.has_charges ? row.charges : 0.0;
    return charges > 0.0 || remains <= 0.0;
  }
  return remains <= 0.0;
}
} // anonymous namespace

// ---------------------------------------------------------------------------
// build_mask -- REAL, all four rules, mirroring mask.py:133-172 exactly.
// ---------------------------------------------------------------------------

void build_mask( const rl_state_t& s, std::uint8_t out_mask[ RL_ACTION_DIM ] )
{
  for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
  {
    const rl_action_desc& a = RL_ACTIONS[ i ];

    // R1: a wait action is legal ONLY at a foreground boundary. A `wait`
    // reply at any other boundary hard-aborts the engine
    // (protocol_abort) -- the mask is the only thing between the agent
    // and that FATAL.
    if ( a.kind == rl_action_kind::wait )
    {
      out_mask[ i ] = s.boundary_is_foreground ? 1 : 0;
      continue;
    }

    // R2: buff gate (mask.py's maskRules.buffGates). FAIL-CLOSED,
    // evaluated FIRST -- and deliberately the OPPOSITE polarity to R3's
    // absent-cooldown-row rule below. mask.py's own capitalized warning,
    // reproduced verbatim in substance: an unsatisfied buff gate means
    // "the engine will refuse this cast" (restrictive is correct), while
    // an absent cooldown row means "this action has no cooldown"
    // (permissive is correct). DO NOT HARMONISE THE TWO.
    bool denied = false;
    for ( std::size_t g = 0; g < RL_BUFF_GATE_COUNT; ++g )
    {
      const rl_buff_gate& gate = RL_BUFF_GATES[ g ];
      if ( a.token == nullptr || std::strcmp( gate.action_token, a.token ) != 0 )
        continue;
      const buff_reading* b = s.find_buff( gate.buff_name );
      const bool present = ( b != nullptr && b->present );
      if ( gate.forbidden )
      {
        // forbiddenBuff: deny when the named buff IS present.
        if ( present )
          denied = true;
      }
      else
      {
        // requiresBuff: deny when the named buff is ABSENT.
        if ( !present )
          denied = true;
      }
    }
    if ( denied )
    {
      out_mask[ i ] = 0;
      continue;
    }

    // R3/R4: cooldown-row legality. "A cast action WITHOUT a cooldownRow,
    // OR whose named row is ABSENT from request['cooldowns']: legal."
    // Deliberate FAIL-OPEN exception to this repo's usual fail-closed
    // mask convention (mask.py:22-32) -- do not harmonise with R2 above.
    if ( a.cooldown_row == nullptr )
    {
      out_mask[ i ] = 1;
      continue;
    }
    const cooldown_reading* row = s.find_cooldown( a.cooldown_row );
    if ( row == nullptr )
    {
      out_mask[ i ] = 1;
      continue;
    }
    out_mask[ i ] = cd_ready_now( *row ) ? 1 : 0;
  }
}

// ---------------------------------------------------------------------------
// build_wait -- REAL, mirroring mask.py:175-243 exactly (next_event_wait_detail).
// ---------------------------------------------------------------------------

namespace
{
struct wait_candidate
{
  double seconds;
  bool is_cooldown;
  const std::string* row_name;   // valid only if is_cooldown
  const char* literal_source;    // valid only if !is_cooldown
};
} // anonymous namespace

wait_result build_wait( const rl_state_t& s )
{
  std::vector<wait_candidate> candidates;

  // Append order IS the tie-break rule (P-3): every cooldown row's
  // candidates, in RL_ACTIONS declaration order (a row's `remains`
  // candidate before its `recharge_time` candidate), THEN swing_mh, THEN
  // gcd. Zero-valued clocks are DROPPED (> 0, never >= 0).
  for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
  {
    const rl_action_desc& a = RL_ACTIONS[ i ];
    if ( a.cooldown_row == nullptr )
      continue;
    const cooldown_reading* row = s.find_cooldown( a.cooldown_row );
    if ( row == nullptr )
      continue;
    if ( row->has_remains && row->remains > 0.0 )
      candidates.push_back( { row->remains, true, &row->name, nullptr } );
    if ( row->has_recharge_time && row->recharge_time > 0.0 )
      candidates.push_back( { row->recharge_time, true, &row->name, nullptr } );
  }

  if ( s.has_swing_mh_remains && s.swing_mh_remains > 0.0 )
    candidates.push_back( { s.swing_mh_remains, false, nullptr, "swing_mh" } );

  if ( s.has_gcd_remains && s.gcd_remains > 0.0 )
    candidates.push_back( { s.gcd_remains, false, nullptr, "gcd" } );

  if ( candidates.empty() )
    return wait_result{ RL_WAIT_FLOOR_SECONDS, "floor", true };

  // Scan with STRICT < so the FIRST-seen minimum wins on a tie (P-3) --
  // never re-derived or reshuffled elsewhere.
  std::size_t best = 0;
  for ( std::size_t i = 1; i < candidates.size(); ++i )
  {
    if ( candidates[ i ].seconds < candidates[ best ].seconds )
      best = i;
  }

  const wait_candidate& winner = candidates[ best ];

  // WR-05 fix (210-CR-FIX): `source` is a `std::string` in `wait_result`
  // now, so a cooldown-row label ("cooldown:<row>") is built directly into
  // the return value -- no thread_local buffer, no lifetime invariant tying
  // this to "exactly one wait_result alive at a time, consumed
  // synchronously". One allocation on the wait arm only, not the hot path.
  std::string source;
  if ( winner.is_cooldown )
    source = "cooldown:" + *winner.row_name;
  else
    source = winner.literal_source;

  const bool floored = winner.seconds < RL_WAIT_FLOOR_SECONDS;
  const double seconds = std::max( winner.seconds, RL_WAIT_FLOOR_SECONDS );
  return wait_result{ seconds, source, floored };
}
} // namespace rl_policy
