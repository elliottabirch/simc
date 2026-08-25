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
  // here; the hand-written corpus (plan 210-07) is what exercises the
  // `permanent` leaf.
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
    c.charges_fractional = cd->charges_fractional();
    c.has_charges_fractional = true;
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
} // anonymous namespace

// ---------------------------------------------------------------------------
// build_obs -- THE TRACER-THIN BODY (objective, deliberate gap #1).
// ---------------------------------------------------------------------------

void build_obs( const rl_state_t& s, float out_obs[ RL_OBS_DIM ] )
{
  for ( std::size_t slot = 0; slot < RL_OBS_DIM; ++slot )
  {
    const rl_obs_field& f = RL_OBS_FIELDS[ slot ];

    double raw;
    bool is_absent;

    if ( f.derived )
    {
      // Only fight_remains is derived in this schema: episode.maxTime - t,
      // floored at 0 (obs.py:236-243).
      raw = std::max( RL_EPISODE_MAX_TIME - s.t, 0.0 );
      is_absent = false;
    }
    else
    {
      // [210-05] tri-state lookup lands here -- this slice takes the
      // "absent" branch for every non-derived field unconditionally.
      // Plan 210-05 adds the present/permanent branches into this same
      // loop; no signature, no caller, no table changes.
      raw = f.missing;
      is_absent = true;
    }

    double encoded;
    if ( f.kind == rl_kind::k_bucket )
    {
      // obs.py:253-278: an "absent" bucket leaf encodes its `missing`
      // value DIRECTLY, bypassing the ordinal floor-match entirely
      // (WR-03 -- -1.0 must stay distinct from every legitimate ordinal
      // 0.0/0.5/1.0).
      if ( is_absent )
        encoded = f.missing;
      else
        encoded = encode_bucket_ordinal( raw, f.buckets, f.n_buckets );
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

  // `source` is a `const char*` in the pinned interface; a cooldown-row
  // label ("cooldown:<row>") is dynamically formatted and must outlive the
  // immediate caller. A thread_local buffer is sufficient here: threads=1
  // is a hard clamp for solver_policy= (XPORT-01/AP-3), and the caller
  // (solver_control::choose()'s in-process arm) consumes this result
  // synchronously, before any other call on this thread could reuse the
  // buffer.
  static thread_local std::string label_storage;
  const char* source;
  if ( winner.is_cooldown )
  {
    label_storage = "cooldown:" + *winner.row_name;
    source = label_storage.c_str();
  }
  else
  {
    source = winner.literal_source;
  }

  const bool floored = winner.seconds < RL_WAIT_FLOOR_SECONDS;
  const double seconds = std::max( winner.seconds, RL_WAIT_FLOOR_SECONDS );
  return wait_result{ seconds, source, floored };
}
} // namespace rl_policy
