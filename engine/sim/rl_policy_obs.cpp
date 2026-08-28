// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// In-process RL transport, Stage 1 (read_state) and Stage 2 (build_obs/
// build_mask/build_wait) -- tstl-sylvanas phase 210, plans 210-04/210-05R/
// 210-CR-FIX. See rl_policy.hpp's own header comment for the
// NORMATIVE-source note: this file is an implementation of
// `scripts/rl/obs.py` and `scripts/rl/mask.py` (this repo), never the
// other way around.
//
// The "TWO DELIBERATE GAPS IN THIS SLICE" this comment used to describe
// (plan 210-04's objective) were closed by 210-05R (commit 1b66cde740):
// build_obs is REAL over all three lookup_status outcomes
// (present/permanent/absent), not merely "absent" for every non-derived
// field. build_mask/build_wait/cd_ready_now were already real as of
// 210-04. Nothing in this file is stubbed.

#include "sim/rl_policy.hpp"

#include "action/attack.hpp"
#include "buff/buff.hpp"
#include "player/consumable.hpp"
#include "player/player.hpp"
#include "sim/cooldown.hpp"
#include "sim/decision_dump.hpp"
#include "sim/event.hpp"
#include "sim/sim.hpp"
#include "util/io.hpp"

#include "fmt/format.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

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

  // Off-hand swing timer (quick task 260826-38t, D-2R slot 8). Mirrors the
  // main-hand read above exactly; guarded the same way decision_dump.cpp's
  // (new) off-hand emitter is guarded. The off-hand genuinely swings as of
  // this fork HEAD (commit "fix(solver_control): start the OFF-HAND
  // swinging too, not just the main hand") -- previously there was nothing
  // here to read.
  if ( p->off_hand_attack && p->off_hand_attack->execute_event )
  {
    s.swing_oh_remains = p->off_hand_attack->execute_event->remains().total_seconds();
    s.has_swing_oh_remains = true;
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
    // remains (quick task 260826-38t, D-2R slot 6) -- same source and same
    // permanent-sentinel convention decision_dump.cpp's write_buff_remains
    // uses: timespan_t::min() means "no scheduled expiry", encoded here as
    // has_remains=false + permanent=true, never as a huge negative number.
    {
      const timespan_t remains = buff->remains();
      if ( remains == timespan_t::min() )
      {
        b.has_remains = false;
        b.permanent = true;
      }
      else
      {
        b.remains = remains.total_seconds();
        b.has_remains = true;
        b.permanent = false;
      }
    }
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
// Stage 2 helpers -- PURE arithmetic, mirroring obs.py's
// _apply_scale / _encode_bucket_ordinal exactly. Retargeted (phase 220,
// plan 220-04) from the retired per-field `rl_obs_field` to the per-leaf
// `rl_leaf_desc` -- the scale/bucket fields these two functions read are
// identical between the two structs, so this is a type swap only.
// ---------------------------------------------------------------------------

namespace
{
double apply_scale( double raw, rl_kind kind, const rl_leaf_desc& f )
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

enum class lookup_status { absent, present, permanent };
} // anonymous namespace

// ---------------------------------------------------------------------------
// Slot binding (phase 220, plan 220-04, OBS-02/OBS-07): ONE census walk
// resolves every family member to an engine handle ONCE per actor AND
// composes that slot's name from the SAME family/member/leaf tables --
// so the vector build_obs fills and the name list a caller can request via
// `rl_obs_names_out=` share one source of truth by construction (this is
// what makes T-220-04-01's fingerprint check non-vacuous: a names file
// that only echoed RL_OBS_NAMES back would compare the generator to
// itself; this walk composes its own names independently and reports a
// divergence if it ever disagrees with the baked table).
//
// Resolved for real in this task: `player_buffs` (buff) and the `scalars`
// pseudo-family's `fight_remains`/`active_enemies`/`t`/`maelstrom` leaves
// (`raid_event_next_in` stays unresolved -- plan 220-05 binds it). Every
// other family resolves `unresolved` here; plans 220-05/220-06 turn those
// into real bindings by adding cases below, with NO change to this
// structure (the slot_binding/slot_table shape, the bind-once contract,
// the name-composition walk).
// ---------------------------------------------------------------------------

// These three enums (and slot_binding/slot_table below) are deliberately
// AT NAMESPACE SCOPE, not inside an anonymous namespace -- slot_table is
// forward-declared in rl_policy.hpp with external linkage (rl_policy::
// slot_table), so its member type slot_binding, and slot_binding's own
// member types, must not carry internal linkage themselves.
enum class slot_binding_kind
{
  buff,
  cooldown,
  expression,
  action_expression,
  enemy_slot,
  direct,
  unresolved
};

enum class buff_leaf_kind { stacks, remains };

// The `scalars` pseudo-family's `direct`-kind leaves this task resolves.
// `fight_remains` needs no entry here -- it is `derived` (rl_leaf_desc::
// derived), and build_obs' derived-first dispatch short-circuits before
// ever consulting a binding's `direct` id, exactly as the retired
// per-field walk did for the same field.
enum class direct_id { active_enemies, t, maelstrom };

struct slot_binding
{
  slot_binding_kind kind = slot_binding_kind::unresolved;
  const rl_leaf_desc* leaf = nullptr;   // this slot's own descriptor -- resolved once, indexed per decision
  buff_t* buff = nullptr;               // kind == buff
  buff_leaf_kind buff_leaf = buff_leaf_kind::stacks;   // kind == buff
  direct_id direct = direct_id::t;      // kind == direct
  // cooldown_t*/expr_t*/enemy-slot descriptor handles are added by plans
  // 220-05/220-06 alongside the `cooldown`/`expression`/`action_expression`/
  // `enemy_slot` resolution cases below -- no structural change needed here.
};

struct slot_table
{
  std::vector<slot_binding> bindings;        // size RL_OBS_DIM, slot order
  std::vector<std::string>  composed_names;  // size RL_OBS_DIM -- the WALK's own names
  int first_name_divergence = -1;            // -1 == the walk agreed with RL_OBS_NAMES at every slot
};

namespace
{
std::unordered_map<const player_t*, slot_table> g_slot_table_cache;
bool g_names_out_written = false;   // guards the ONE names-file write per process

std::string compose_slot_name( const rl_obs_family& fam, const rl_obs_member& mem, const rl_leaf_desc& leaf )
{
  // header_contract (220-04-PLAN.md): `bare_name` families (only `scalars`)
  // compose the bare leaf name; every other family composes
  // "<family.name>.<member>.<leaf>".
  if ( fam.bare_name )
    return leaf.leaf;
  return fmt::format( "{}.{}.{}", fam.name, mem.member, leaf.leaf );
}

// The census artifact merges several consumable buffs into ONE generic
// member name per role (`flask`, `food`, `voidtouched` == augmentation,
// `potion`) -- INCLUDED.tsv's own "merged, one row" convention (220-02's
// importer, `_MULTI_INSTANCE_ROWS`/`_RENAME_ROWS`). The underlying buff
// object's REAL name is the specific item (e.g. a flask token, not the
// literal string "flask"), so a plain `p->buff_list` name-string scan can
// never match these four -- `player_t::consumables` exists for exactly
// this reason (flask/food/augmentation are direct `buff_t*` pointers,
// populated at init regardless of which concrete item the profile chose).
// `potion` has no equivalent direct pointer; its action ("potion",
// `dbc_consumable_base_t`) exposes the SAME generic seam through its own
// public `consumable_buff` member.
buff_t* resolve_generic_consumable_buff( player_t* p, const std::string& member )
{
  if ( member == "flask" )
    return p->consumables.flask;
  if ( member == "food" )
    return p->consumables.food;
  if ( member == "voidtouched" )
    return p->consumables.augmentation;
  if ( member == "potion" )
  {
    if ( action_t* a = p->find_action( "potion" ) )
    {
      if ( auto* consumable = dynamic_cast<dbc_consumable_base_t*>( a ) )
        return consumable->consumable_buff;
    }
    return nullptr;
  }
  return nullptr;   // not a generic-consumable member -- caller falls through to a name scan
}

// rl_family_kind::buff resolution: `stacks`/`remains` are the only two
// leaves this schema's census artifact declares for this family -- an
// UNRECOGNISED leaf name binds `unresolved` (a generator/artifact
// mismatch, not a per-fight data fact). A recognised leaf ALWAYS binds
// `kind::buff`, even when `member_buff` is null -- a member whose handle
// this run simply never created (an untalented proc buff, a raid-event-
// gated buff under a fight style that never injects that event) is a
// RUNTIME absence build_obs already encodes correctly (buff==nullptr ->
// absent -> `missing`), not an unimplemented resolution strategy. Binding
// it `unresolved` instead would conflate "this fight has no such buff"
// with "plans 220-05/220-06 still owe this family a binding" -- exactly
// the distinction the acceptance criteria (player_buffs fully resolved,
// `unresolved[]` names only families this task defers) requires.
slot_binding resolve_buff_leaf( buff_t* member_buff, const rl_leaf_desc& leaf )
{
  slot_binding b;
  b.leaf = &leaf;
  b.buff = member_buff;
  if ( std::strcmp( leaf.leaf, "stacks" ) == 0 )
  {
    b.kind = slot_binding_kind::buff;
    b.buff_leaf = buff_leaf_kind::stacks;
  }
  else if ( std::strcmp( leaf.leaf, "remains" ) == 0 )
  {
    b.kind = slot_binding_kind::buff;
    b.buff_leaf = buff_leaf_kind::remains;
  }
  else
  {
    b.kind = slot_binding_kind::unresolved;
  }
  return b;
}

// rl_family_kind::scalar resolution (the `scalars` pseudo-family). Every
// leaf this task resolves reads a value `build_obs` already has to hand
// with NO per-decision engine lookup of its own: `active_enemies`/`t` come
// straight from `rl_state_t` (already populated by `read_state`, untouched
// by this plan); `maelstrom` is read directly off `p->resources` at build
// time -- there is no POD field for it (deliberately: "do NOT touch
// read_state's POD population"), which is exactly what the new
// `build_obs(player_t*, ...)` signature exists to allow. Where the RETIRED
// per-field walk gated an engine-direct read like this on
// `p->resources.is_active(RESOURCE_MAELSTROM)` (the SC-5 cross-transport
// byte-neutrality contract, `cooldowns.strike.charges_fractional`'s old
// gate), that gate is unnecessary for any read this new walk performs: the
// FIFO transport's own byte-neutrality concern was about the WIRE PROTOCOL
// omitting a key the request never asked for, not about whether the engine
// itself can answer the read -- and CR-01's actor filter means only the
// shaman ever reaches this path, so there is no other actor's absent
// resource to accidentally read as zero. Applies identically to the
// cooldowns family's own `charges_fractional` when plan 220-05 binds it.
slot_binding resolve_scalar_leaf( const rl_leaf_desc& leaf )
{
  slot_binding b;
  b.leaf = &leaf;
  if ( leaf.derived )
  {
    // Never actually read -- build_obs' derived-first dispatch short-
    // circuits before consulting `direct` for a derived leaf. Kept a
    // defined value rather than left default-constructed so a future
    // reader never has to wonder whether "direct_id::t" here means
    // anything.
    b.kind = slot_binding_kind::direct;
    b.direct = direct_id::t;
    return b;
  }
  if ( std::strcmp( leaf.leaf, "active_enemies" ) == 0 )
  {
    b.kind = slot_binding_kind::direct;
    b.direct = direct_id::active_enemies;
    return b;
  }
  if ( std::strcmp( leaf.leaf, "t" ) == 0 )
  {
    b.kind = slot_binding_kind::direct;
    b.direct = direct_id::t;
    return b;
  }
  if ( std::strcmp( leaf.leaf, "maelstrom" ) == 0 )
  {
    b.kind = slot_binding_kind::direct;
    b.direct = direct_id::maelstrom;
    return b;
  }
  // raid_event_next_in and anything else this task does not bind.
  b.kind = slot_binding_kind::unresolved;
  return b;
}

// Writes the `rl_obs_names_out=` file ONCE per process (T-220-04-02's
// rundir-scoped discipline, mirroring rl_translog='s open_and_write_header:
// eager, no parent directories created, refuses loudly on a bad path
// rather than silently skipping). Called from bind_slots() -- the ONLY
// place a fresh slot_table (and therefore a fresh set of walk-composed
// names) is ever produced.
void write_names_out_if_requested( const player_t* p, const slot_table& table )
{
  if ( g_names_out_written )
    return;
  const std::string& path = p->sim->rl_obs_names_out_str;
  if ( path.empty() )
    return;
  g_names_out_written = true;

  io::ofstream stream;
  stream.open( path, std::ios::out | std::ios::trunc );
  if ( !stream.is_open() )
  {
    throw sc_runtime_error(
        fmt::format( "rl_obs_names_out=: unable to open '{}' for writing.", path ) );
  }

  stream << "{\"schemaSha\":\"" << decision_dump::json_escape( RL_OBS_SCHEMA_SHA ) << "\",";
  stream << "\"width\":" << RL_OBS_DIM << ",";
  stream << "\"names\":[";
  for ( std::size_t i = 0; i < RL_OBS_DIM; ++i )
  {
    if ( i > 0 )
      stream << ",";
    stream << "\"" << decision_dump::json_escape( table.composed_names[ i ] ) << "\"";
  }
  stream << "],\"unresolved\":[";
  bool first = true;
  for ( std::size_t i = 0; i < RL_OBS_DIM; ++i )
  {
    if ( table.bindings[ i ].kind != slot_binding_kind::unresolved )
      continue;
    if ( !first )
      stream << ",";
    first = false;
    stream << "\"" << decision_dump::json_escape( table.composed_names[ i ] ) << "\"";
  }
  stream << "],\"firstNameDivergence\":" << table.first_name_divergence << "}";
  stream.flush();
}
} // anonymous namespace

const slot_table& bind_slots( player_t* p )
{
  auto cached = g_slot_table_cache.find( p );
  if ( cached != g_slot_table_cache.end() )
    return cached->second;

  slot_table table;
  table.bindings.reserve( RL_OBS_DIM );
  table.composed_names.reserve( RL_OBS_DIM );

  std::size_t counter = 0;
  for ( std::size_t fi = 0; fi < RL_OBS_FAMILY_COUNT; ++fi )
  {
    const rl_obs_family& fam = RL_OBS_FAMILIES[ fi ];
    for ( std::size_t mi = 0; mi < fam.n_members; ++mi )
    {
      const rl_obs_member& mem = fam.members[ mi ];

      // Buff family: resolve the handle ONCE per member (shared across
      // that member's leaves) -- never re-scan p->buff_list per leaf.
      buff_t* member_buff = nullptr;
      bool member_buff_scanned = false;

      for ( std::size_t li = 0; li < mem.n_leaves; ++li )
      {
        const rl_leaf_desc& leaf = mem.leaves[ li ];
        std::string composed = compose_slot_name( fam, mem, leaf );

        slot_binding binding;
        switch ( fam.kind )
        {
          case rl_family_kind::buff:
          {
            if ( !member_buff_scanned )
            {
              member_buff = resolve_generic_consumable_buff( p, mem.member );
              if ( member_buff == nullptr )
              {
                for ( buff_t* buff : p->buff_list )
                {
                  if ( buff->name_str == mem.member )
                  {
                    member_buff = buff;
                    break;
                  }
                }
              }
              member_buff_scanned = true;
            }
            binding = resolve_buff_leaf( member_buff, leaf );
            break;
          }
          case rl_family_kind::scalar:
            binding = resolve_scalar_leaf( leaf );
            break;
          case rl_family_kind::cooldown:
          case rl_family_kind::enemy_slot:
          case rl_family_kind::action_expression:
          case rl_family_kind::expression:
          case rl_family_kind::direct:
          default:
            // Deferred to plans 220-05/220-06 -- see this function's own
            // header comment.
            binding.kind = slot_binding_kind::unresolved;
            binding.leaf = &leaf;
            break;
        }

        if ( counter >= RL_OBS_DIM )
        {
          throw sc_runtime_error( fmt::format(
              "rl_policy::bind_slots: the family table walk produced more than RL_OBS_DIM ({}) "
              "slots at family='{}' member='{}' leaf='{}' -- this is a generator/engine table "
              "mismatch (a long table), not a runtime data problem",
              RL_OBS_DIM, fam.name, mem.member, leaf.leaf ) );
        }

        // Assert the composed name equals RL_OBS_NAMES[i]. On a mismatch,
        // do NOT abort -- record the FIRST divergent slot and still emit
        // the WALK's own composed name (never a copy of RL_OBS_NAMES) so
        // obs_fingerprint.py can see the divergence instead of the engine
        // hiding it (T-220-04-01).
        if ( table.first_name_divergence < 0 && composed != RL_OBS_NAMES[ counter ] )
          table.first_name_divergence = static_cast<int>( counter );

        table.bindings.push_back( binding );
        table.composed_names.push_back( std::move( composed ) );
        ++counter;
      }
    }
  }

  if ( counter != RL_OBS_DIM )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::bind_slots: the family table walk produced {} slots, expected RL_OBS_DIM={} "
        "-- this is a generator bug (a short table), not a runtime data problem",
        counter, RL_OBS_DIM ) );
  }

  write_names_out_if_requested( p, table );

  auto [ inserted, ok ] = g_slot_table_cache.emplace( p, std::move( table ) );
  ( void )ok;   // emplace on a key just proven absent by the find() above always succeeds
  return inserted->second;
}

// ---------------------------------------------------------------------------
// build_obs -- REAL for the families this task resolves (player_buffs,
// scalars); every other family's slots read their bound `unresolved`
// status (-> `missing`) until plans 220-05/220-06 add real bindings, with
// NO change to this dispatch. Order mirrors obs.py:232-287 and the retired
// per-field walk exactly: derived fields force `present`; otherwise the
// binding decides a tri-state status (absent/present/permanent); `kind ==
// bucket` is tested BEFORE the `permanent` branch (a permanent bucket is a
// registry misconfiguration, not a value to saturate); non-bucket
// `permanent` saturates to RL_PERMANENT_SATURATION; everything else goes
// through apply_scale. NO string comparison anywhere in this function's
// body -- every slot's resolution (which accessor to call, which leaf a
// buff binding means) was already decided once, at bind time, above.
// ---------------------------------------------------------------------------

void build_obs( const player_t* p, const rl_state_t& s, const slot_table& t, float out_obs[ RL_OBS_DIM ] )
{
  for ( std::size_t slot = 0; slot < RL_OBS_DIM; ++slot )
  {
    const slot_binding& b = t.bindings[ slot ];
    const rl_leaf_desc& f = *b.leaf;

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
      switch ( b.kind )
      {
        case slot_binding_kind::buff:
        {
          buff_t* buff = b.buff;
          if ( buff == nullptr || buff->check() <= 0 )
          {
            // Not up (or unresolved) -- absent for BOTH leaves of this
            // member, matching the retired walk's own `s.buffs` filter
            // (a not-up buff was skipped entirely, so find_buff() would
            // return nullptr for it there too).
            status = lookup_status::absent;
          }
          else if ( b.buff_leaf == buff_leaf_kind::stacks )
          {
            raw = static_cast<double>( buff->check() );
            status = lookup_status::present;
          }
          else   // remains
          {
            const timespan_t remains = buff->remains();
            if ( remains == timespan_t::min() )
            {
              status = lookup_status::permanent;
            }
            else
            {
              raw = remains.total_seconds();
              status = lookup_status::present;
            }
          }
          break;
        }
        case slot_binding_kind::direct:
        {
          switch ( b.direct )
          {
            case direct_id::active_enemies:
              if ( s.has_active_enemies )
              {
                raw = s.active_enemies;
                status = lookup_status::present;
              }
              else
              {
                status = lookup_status::absent;
              }
              break;
            case direct_id::t:
              raw = s.t;
              status = lookup_status::present;
              break;
            case direct_id::maelstrom:
              // Read directly off the engine, not off rl_state_t -- there
              // is no POD field for it (see resolve_scalar_leaf's own
              // comment on why that gate is unnecessary here).
              raw = p->resources.current[ RESOURCE_MAELSTROM ];
              status = lookup_status::present;
              break;
            default:
              status = lookup_status::absent;
              break;
          }
          break;
        }
        case slot_binding_kind::cooldown:
        case slot_binding_kind::expression:
        case slot_binding_kind::action_expression:
        case slot_binding_kind::enemy_slot:
        case slot_binding_kind::unresolved:
        default:
          // Deferred to plans 220-05/220-06 -- an unresolved binding
          // yields `absent` (header_contract), never a special value.
          status = lookup_status::absent;
          break;
      }
    }

    if ( status == lookup_status::absent )
      raw = f.missing;
    // status == present: `raw` was already written above.
    // status == permanent: `raw` is left unused below, matching obs.py's
    // `raw = None` for this branch -- neither the bucket-absent path nor
    // apply_scale() is ever reached with a permanent status.

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
            "rl_policy::build_obs: observation slot={} leaf='{}' is kind=bucket but its binding "
            "resolved to status=permanent -- no bucket field can legitimately be permanent "
            "(permanence is a buff-expiry concept); this is a registry/binding misconfiguration, "
            "not a valid build_obs() input",
            slot, f.leaf ) );
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

// Sanity check (comment only, not executable): with `player_buffs` fully
// bound and every OTHER family still `unresolved` (this task's own scope),
// an unresolved-cooldown slot (e.g. `cooldowns.strike.charges_fractional`,
// missing=2.0, div=2) encodes via the absent branch to `2.0/2 = 1.0`, never
// `0.0` -- and an always-up buff (`permanent == true`, its `remains` leaf)
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
