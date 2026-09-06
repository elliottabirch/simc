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

#include "action/action.hpp"
#include "action/action_state.hpp"
#include "action/attack.hpp"
#include "action/dot.hpp"
#include "buff/buff.hpp"
#include "player/consumable.hpp"
#include "player/player.hpp"
#include "sim/cooldown.hpp"
#include "sim/decision_dump.hpp"
#include "sim/event.hpp"
#include "sim/expressions.hpp"
#include "sim/raid_event.hpp"
#include "sim/rl_target_select.hpp"
#include "sim/sim.hpp"
#include "sim/solver_control.hpp"
#include "util/io.hpp"

#include "fmt/format.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

namespace rl_policy
{
namespace
{
// 221-01 (ACT-02, Pattern 1) -- resolved ONCE per actor, same lazy-fill-on-
// first-use discipline as bind_slots()'s g_slot_table_cache further down
// this file (keyed on a bare const player_t*, same WR-12 single-sim/
// single-thread precondition asserted at the fill site below).
// `background`/`action_list` are stable after player_t::init_actions(), so
// one resolution per actor is honest for the whole fight (Assumptions Log
// A1) -- filled lazily on first read_action_gate_bits() call, never during
// actor init. Declared here (ahead of read_state/read_action_gate_bits,
// rather than beside g_slot_table_cache) purely because C++ requires the
// declaration precede first use in the same translation unit.
std::unordered_map<const player_t*, std::vector<action_t*>> g_action_handle_cache;

// 260902/cr4 (CR-02): the two output arrays a BOUNDARY read_action_gate_bits call computed for
// the CURRENT decision stamp, cached so the later non-boundary call (decision_dump::record(),
// which always runs after solver_control::choose() has already retargeted/turned the player)
// returns the PRE-decision mask instead of recomputing against state the cast already mutated.
struct gate_bits_cache_entry
{
  std::uint64_t stamp     = 0;
  bool          has_stamp = false;
  std::uint8_t  resolvable[ RL_ACTION_DIM ] = {};
  std::uint8_t  ready     [ RL_ACTION_DIM ] = {};
};
std::unordered_map<const player_t*, gate_bits_cache_entry> g_gate_bits_cache;
} // anonymous namespace

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
// next_raid_event_in -- 221-03 (ACT-05/ACT-06). See this function's own
// declaration in rl_policy.hpp for the full three-consumer contract. A
// candidate survives the filter only when its own until_next() is
// strictly positive AND no greater than the remaining fight -- this is
// what discards raid_event_t::until_next()'s ~9.2e12 "nothing pending"
// saturation value (RESEARCH Pitfall 6) WITHOUT ever comparing against
// that sentinel by name: any candidate that large is, by construction,
// far larger than any real fight length, so the fight-remaining bound
// alone excludes it.
// ---------------------------------------------------------------------------

bool next_raid_event_in( const sim_t* sim, double& out_seconds )
{
  const double fight_remaining =
      std::max( ( sim->expected_iteration_time - sim->current_time() ).total_seconds(), 0.0 );

  bool found = false;
  double best = 0.0;
  for ( const auto& re : sim->raid_events )
  {
    if ( !re )
      continue;
    const double candidate = re->until_next().total_seconds();
    if ( candidate <= 0.0 || candidate > fight_remaining )
      continue;
    if ( !found || candidate < best )
    {
      best = candidate;
      found = true;
    }
  }
  if ( found )
    out_seconds = best;
  return found;
}

// ---------------------------------------------------------------------------
// 228-10 Task 1 Step 3 (D-16 identity-free aggregates, TGT-05). ONE walk of
// `target_non_sleeping_list`, shared by decision_dump.cpp's new aggregate keys. Flame Shock
// carrier count is NEW code (P-5): it walks DOTS via `find_dot`, the same non-allocating idiom
// `build_enemy_fact`'s own flame_shock_remaining uses -- never `buff_list`, which the existing
// `enemy_debuff_counts` counter (decision_dump.cpp) walks and can therefore never see a dot at
// all. The gate proving this is the buff_list.count baseline compare in this plan's own verify
// block.
// ---------------------------------------------------------------------------

fight_wide_aggregates_t compute_fight_wide_aggregates( player_t* p )
{
  using rl_target_select::DYING_WITHIN_LATER_SECONDS;
  using rl_target_select::DYING_WITHIN_SOON_SECONDS;
  using rl_target_select::MELEE_RANGE_YARDS;
  using rl_target_select::NEAR_RANGE_YARDS;
  using rl_target_select::VISIBILITY_RANGE_YARDS;

  fight_wide_aggregates_t agg;
  for ( player_t* t : p->sim->target_non_sleeping_list )
  {
    if ( !t->is_enemy() )
      continue;
    ++agg.enemies_total;

    const double dist = p->get_player_distance( *t );
    if ( dist <= MELEE_RANGE_YARDS + t->combat_reach )
      ++agg.enemies_in_melee;
    if ( dist <= NEAR_RANGE_YARDS )
      ++agg.enemies_within_8yd;
    if ( dist <= VISIBILITY_RANGE_YARDS )
      ++agg.enemies_within_40yd;
    if ( p->is_in_front( *t, 0.0 ) )
      ++agg.enemies_in_front;

    // NEW code (P-5): walks DOTS over the non-sleeping list, never buff_list.
    dot_t* fs = t->find_dot( "flame_shock", p );
    if ( fs && fs->is_ticking() )
      ++agg.flame_shock_carrier_count;

    const double ttd = std::min( t->time_to_percent( 0 ).total_seconds(), 600.0 );
    if ( !agg.has_soonest_time_to_die || ttd < agg.soonest_time_to_die )
    {
      agg.has_soonest_time_to_die = true;
      agg.soonest_time_to_die     = ttd;
    }
    if ( !agg.has_longest_time_to_die || ttd > agg.longest_time_to_die )
    {
      agg.has_longest_time_to_die = true;
      agg.longest_time_to_die     = ttd;
    }
    // Stated explicitly and IDENTICALLY (at-or-below counts, above does not) so the two
    // thresholds can never disagree at the crossing point.
    if ( ttd <= DYING_WITHIN_SOON_SECONDS )
      ++agg.dying_within_5s;
    if ( ttd <= DYING_WITHIN_LATER_SECONDS )
      ++agg.dying_within_15s;

    if ( !agg.has_nearest_enemy_distance || dist < agg.nearest_enemy_distance )
    {
      agg.has_nearest_enemy_distance = true;
      agg.nearest_enemy_distance     = dist;
    }
  }
  return agg;
}

// ---------------------------------------------------------------------------
// 228-10 Task 1 Step 4(b) (D-17/TGT-06/Q18). COPIES next_raid_event_in's own loop/discard
// convention, filtered to `type == "invulnerable"`, extended to also report the ACTIVE window's
// remaining time. This is the ONE function both decision_dump.cpp's aggregate keys and
// read_state()'s new rl_state_t::immunity_remaining field call (D-12: read once, use twice) --
// never a second, independently-maintained walk of sim->raid_events.
// ---------------------------------------------------------------------------

invulnerability_window_t compute_invulnerability_window( const sim_t* sim )
{
  invulnerability_window_t w;
  const double fight_remaining =
      std::max( ( sim->expected_iteration_time - sim->current_time() ).total_seconds(), 0.0 );

  for ( const auto& re : sim->raid_events )
  {
    if ( !re || re->type != "invulnerable" )
      continue;

    if ( re->up() )
    {
      // remains() asserts is_up -- only called on the up() == true branch, per its own
      // documented precondition (raid_event.hpp).
      const double remaining = re->remains().total_seconds();
      if ( !w.active || remaining < w.remaining )
      {
        w.active    = true;
        w.remaining = remaining;
      }
      continue;
    }

    // Not currently up -- until_next() discards the ~9.2e12 "nothing pending" saturation value
    // the SAME way next_raid_event_in() does above: any candidate larger than the remaining
    // fight is, by construction, not really pending.
    const double candidate = re->until_next().total_seconds();
    if ( candidate <= 0.0 || candidate > fight_remaining )
      continue;
    if ( !w.has_next || candidate < w.next_in )
    {
      w.has_next = true;
      w.next_in  = candidate;
    }
  }
  return w;
}

// ---------------------------------------------------------------------------
// 228-10 Task 1 Step 5 (D-16 shaped-spell block, TGT-04, OR-2). CURRENT facing only -- no
// hypothetical direction, no target-cache read (R-D). Calls the ONE shared geometry copy
// (rl_target_select::crash_lightning_cone_contains / sundering_rect_contains) directly -- this
// plan's own verify gate requires that call site to live in THIS file (the observation writer),
// never wrapped a second time in the selector module.
// ---------------------------------------------------------------------------

shape_hit_result_t compute_crash_lightning_shape( player_t* p )
{
  shape_hit_result_t r;
  for ( player_t* t : p->sim->target_non_sleeping_list )
  {
    if ( !t->is_enemy() )
      continue;
    if ( !rl_target_select::crash_lightning_cone_contains( p->x_position, p->y_position, p->facing_x,
                                                            p->facing_y, t->x_position, t->y_position,
                                                            t->combat_reach ) )
      continue;
    ++r.enemies_hit;
    const double ttd = std::min( t->time_to_percent( 0 ).total_seconds(), 600.0 );  // WR-10 clip
    r.summed_remaining_life += ttd;
    if ( ttd > rl_target_select::DYING_WITHIN_LATER_SECONDS )
      ++r.long_lived_count;
  }
  return r;
}

shape_hit_result_t compute_sundering_shape( player_t* p )
{
  shape_hit_result_t r;
  for ( player_t* t : p->sim->target_non_sleeping_list )
  {
    if ( !t->is_enemy() )
      continue;
    if ( !rl_target_select::sundering_rect_contains( p->x_position, p->y_position, p->facing_x,
                                                       p->facing_y, t->x_position, t->y_position,
                                                       t->combat_reach ) )
      continue;
    ++r.enemies_hit;
    const double ttd = std::min( t->time_to_percent( 0 ).total_seconds(), 600.0 );  // WR-10 clip
    r.summed_remaining_life += ttd;
    if ( ttd > rl_target_select::DYING_WITHIN_LATER_SECONDS )
      ++r.long_lived_count;
  }
  return r;
}

// ---------------------------------------------------------------------------
// Stage 1: read_state -- needs the engine, not exercised by the standalone
// test executable (plan 210-07).
// ---------------------------------------------------------------------------

rl_state_t read_state( const player_t* p, bool boundary_is_foreground, bool is_decision_boundary )
{
  rl_state_t s;
  sim_t* sim = p->sim;

  s.t = sim->current_time().total_seconds();
  s.boundary_is_foreground = boundary_is_foreground;

  // 221-03 (ACT-05/ACT-06) -- the two anchored-wait clamp inputs. Engine's
  // own fight_remains definition (sim.cpp's expected_combat_length/
  // fight_remains expression); the SHARED next_raid_event_in() helper for
  // the raid-event bound, RAW -- no substitution when nothing is pending
  // (has_raid_event_next_in stays false, meaning "no clamp from this
  // source" downstream in accept_wait()).
  s.fight_remains = std::max( ( sim->expected_iteration_time - sim->current_time() ).total_seconds(), 0.0 );
  s.has_fight_remains = true;
  s.has_raid_event_next_in = next_raid_event_in( sim, s.raid_event_next_in );

  // OBS-05/R-W (232-03) -- the turn action's legality predicate, read ONCE here (the "one
  // reader" rule this file's other POD fields already follow) so build_mask's turn-bit check
  // below is a plain field read, not a second engine walk. Deliberately NOT gated on
  // `sim->facing_enabled` -- `rl_target_select.cpp:141`'s own precedent (`f.in_front =
  // a->player->is_in_front(*candidate, 0.0)`) says this is ground truth for the mask/dump, not
  // an optional display feature.
  {
    bool any_behind = false;
    for ( player_t* t : sim->target_non_sleeping_list )
    {
      if ( !t->is_enemy() )
        continue;
      if ( !p->is_in_front( *t, 0.0 ) )
      {
        any_behind = true;
        break;
      }
    }
    s.any_enemy_behind_player = any_behind;
  }

  // 228-10 (Q18, D-12 "read once, use twice") -- the SAME compute_invulnerability_window() call
  // decision_dump.cpp's own immunity_remaining/immunity_in aggregate keys use. Only the ACTIVE
  // window's remaining time becomes a wait candidate (build_wait, below) -- "seconds to the next
  // one" is not a reason to wake up early, only "seconds until the current one ends" is.
  {
    const invulnerability_window_t iw = compute_invulnerability_window( sim );
    if ( iw.active && iw.remaining > 0.0 )
    {
      s.immunity_remaining     = iw.remaining;
      s.has_immunity_remaining = true;
    }
  }

  // Same formula as decision_dump.cpp:355's gcd_remains -- shared helper,
  // not a re-derived expression, so the two can never drift.
  s.gcd_remains = decision_dump::clamp_nonneg( ( p->gcd_ready - sim->current_time() ).total_seconds() );
  s.has_gcd_remains = true;

  // FORK-04b (260902/226-07) -- gcd_length/auto_attack_interval, read ONCE
  // here (the "one reader" rule) so build_wait's re-ask-period cap and the
  // swing_cast_gcd_length/swing_cast_auto_attack_interval obs leaves below
  // both read this SAME POD value, never a second independently-maintained
  // formula. Identical to decision_dump.cpp's gcd_length/auto_attack_interval
  // keys (both PLAYER-scoped, not action-scoped).
  {
    timespan_t player_gcd = p->base_gcd * p->cache.attack_haste();
    if ( player_gcd < p->min_gcd )
      player_gcd = p->min_gcd;
    s.gcd_length = player_gcd.total_seconds();
    s.has_gcd_length = true;
  }
  s.auto_attack_interval = p->main_hand_attack
      ? ( p->main_hand_weapon.swing_time * p->cache.auto_attack_speed() ).total_seconds()
      : 0.0;
  s.has_auto_attack_interval = true;

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

  // 221-01 (ACT-02, Pattern 1) -- the engine-truth legality layer. Filled
  // via the SAME function decision_dump::write_state_fields calls, so this
  // POD and the wire/dump arrays can never drift apart (Pattern 3).
  // 260902/cr4 (CR-02): solver_control.cpp's own call is ALWAYS the decision boundary -- it is
  // the in-process arm's own per-decision state read, called before any reply/accept_cast has
  // run, and passes is_decision_boundary=true. 228-11 Task 2 (closing a gap 228-09 disclosed):
  // this function's OWN internal read_action_gate_bits call used to hardcode `true`
  // unconditionally here, ignoring the caller's own context entirely -- WRONG for
  // decision_dump.cpp's diagnostic obs-vector block, whose call always runs strictly AFTER the
  // real decision already cast and must read the SAME cached, non-re-bumping legality/pick state
  // write_state_fields' own adjacent read_action_gate_bits call already correctly does. Now
  // threaded straight from this function's own parameter -- see rl_policy.hpp's own doc comment.
  read_action_gate_bits( p, s.action_resolvable, s.action_ready, is_decision_boundary );

  return s;
}

// ---------------------------------------------------------------------------
// read_action_gate_bits -- 221-01 (ACT-02, Pattern 1/2). Resolves each
// `kind == cast` action's token to an action_t* through
// solver_control::resolve_action (the SAME resolver accept_cast uses, via a
// handle table cached ONCE per actor -- g_action_handle_cache above, filled
// lazily on first use, never during actor init) and computes two bits per
// action: `resolvable` (`!background` ALONE -- see the comment on that line
// below for why `data().ok()` is deliberately NOT part of this predicate)
// and `ready` (`resolvable && a->ready()` -- plain `ready()`, NOT the
// `_ready`-suffixed `action_ready()`, which is the APL's own selection
// predicate and evaluates `if_expr`/target-selection/line-cooldown/RNG
// skill rolls that `accept_cast` never consults). A `kind == wait` action
// has no `action_t*` to resolve; both of its output bits stay at their
// default 0 and `build_mask` never reads them for a wait candidate.
// ---------------------------------------------------------------------------

void read_action_gate_bits( const player_t* p, std::uint8_t out_resolvable[ RL_ACTION_DIM ],
                             std::uint8_t out_ready[ RL_ACTION_DIM ], bool is_decision_boundary,
                             bool* out_used_dump_time_compute )
{
  if ( out_used_dump_time_compute )
    *out_used_dump_time_compute = false;

  // Same WR-12 single-sim/single-thread precondition bind_slots() asserts
  // above -- this cache is keyed on a bare const player_t* with no sim
  // identity and no clear.
  assert( p->sim->threads == 1 && p->sim->profileset_map.empty() &&
          "rl_policy action-handle cache is single-sim/single-thread by construction (221-01)" );

  // 260902/cr4 (CR-02): a NON-boundary call (decision_dump::record(), which always runs AFTER
  // solver_control::choose() has already retargeted/turned the player for THIS decision) returns
  // the cached PRE-decision mask a boundary call computed for the CURRENT stamp, rather than
  // recomputing against state the cast already mutated -- recomputing here is the exact defect
  // this task fixes. When no cache matches (a scripted actor with decision_dump= and no solver
  // arm never takes the boundary path), fall through to the plain compute below WITHOUT bumping
  // the stamp and WITHOUT filling any pick (the `is_decision_boundary` checks inside the loop
  // below gate that), and report the dump-time compute via the out-parameter.
  if ( !is_decision_boundary )
  {
    auto cache_it = g_gate_bits_cache.find( p );
    std::uint64_t current_stamp = rl_target_select::current_decision_stamp( p );
    if ( cache_it != g_gate_bits_cache.end() && cache_it->second.has_stamp &&
         cache_it->second.stamp == current_stamp )
    {
      std::memcpy( out_resolvable, cache_it->second.resolvable, RL_ACTION_DIM );
      std::memcpy( out_ready, cache_it->second.ready, RL_ACTION_DIM );
      return;
    }
    if ( out_used_dump_time_compute )
      *out_used_dump_time_compute = true;
  }
  else
  {
    // 228-02 (D-12, TGT-02/03): bump this player's decision stamp ONCE per BOUNDARY
    // read_action_gate_bits call (never per targeted action below) -- every targeted action's
    // pick filled in THIS call carries the identical stamp, which is what lets accept_cast's
    // later lookup_pick() call (solver_control.cpp) distinguish "the pick belongs to this
    // decision" from "stale, refuse by name" without ever recomputing.
    rl_target_select::begin_decision( p );
  }

  auto cached = g_action_handle_cache.find( p );
  if ( cached == g_action_handle_cache.end() )
  {
    // `resolve_action` takes a non-const `player_t*` (it does not mutate
    // the actor, but SimC's own action-list scan API is non-const
    // throughout) -- read_state's own `const player_t*` parameter is the
    // Open Question 5 const-handling choice this plan resolves via
    // const_cast at this ONE call site, rather than threading a non-const
    // overload of read_state/read_action_gate_bits through every caller.
    player_t* mutable_p = const_cast<player_t*>( p );
    std::vector<action_t*> handles;
    handles.reserve( RL_ACTION_DIM );
    for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
    {
      const rl_action_desc& a = RL_ACTIONS[ i ];
      handles.push_back( a.kind == rl_action_kind::cast
                              ? solver_control::resolve_action( mutable_p, a.token )
                              : nullptr );
    }
    cached = g_action_handle_cache.emplace( p, std::move( handles ) ).first;
  }

  // CR-04 (260902/cr4): tracks whether this decision offered at least one legal registry action
  // to the RL-controlled actor, and whether any registry action existed to offer at all -- feeds
  // the "every targeted action was illegal" deadlock counter below.
  bool any_targeted_action_seen  = false;
  bool any_targeted_action_ready = false;

  const std::vector<action_t*>& handles = cached->second;
  for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
  {
    action_t* a = handles[ i ];
    // `!background` ALONE -- NOT `data().ok()`. `use_item_t` is constructed
    // with nil spell data (`_id == 0`), so `data().ok()` is FALSE for the
    // trinket and would mark it permanently unresolvable; `!background`
    // already subsumes every silent-no-op branch (untalented spell, item
    // name/slot miss, verify_actor_* failure -- each sets `background`
    // itself). See rl_policy_constants.h's own generated comment site and
    // 221-RESEARCH.md Pitfall 2 for the full citation trail.
    out_resolvable[ i ] = ( a != nullptr && !a->background ) ? 1 : 0;
    // Plain `ready()` -- the SAME call accept_cast makes before FATAL-ing
    // (solver_control.cpp). NOT `action_ready()` (the `_ready`-suffixed
    // APL-selection predicate, which additionally evaluates `if_expr`,
    // `select_target()`, `line_cooldown` and an RNG skill roll) -- using
    // that one here would make the mask disagree with the engine's own
    // FATAL gate about what "ready" means.
    //
    // 228-02 (D-11, TGT-02/03): for exactly the eight targeted registry actions
    // (rl_target_select::is_targeted_action), the per-decision selector pick SUBSTITUTES for
    // `a->target` in the conjunct FORK-01 (260902) left this exact seam for -- "`a->target`, not
    // `p->target`: Phase 228's per-spell selectors substitute exactly this argument." The pick is
    // computed ONCE here (fill_pick, D-12) and READ -- never recomputed -- by accept_cast
    // (solver_control.cpp) via lookup_pick(). Every OTHER action (self/ground/item, the two
    // SHAPED actions plan 228-03 owns, and every kind==wait entry) is completely untouched: same
    // `a->target`/`a->target_ready(a->target)` pair as before this plan.
    //
    // CR-05 (260902/cr4): `is_targeted_action` is a pure NAME match -- `ancestor_t`'s own pet
    // `chain_lightning_t` (sc_shaman.cpp) shares the SAME name_str as the RL player's spell, so
    // the substitution is additionally scoped to the RL-controlled actor (exact strcmp against
    // RL_ACTOR_NAME, mirroring solver_control.cpp's own accept_cast/choose() gate -- never a
    // prefix match, which would also match every one of that actor's pet records), non-pet. A
    // pet's (or any other actor's) same-named action falls through to the untouched
    // `a->target`/`a->target_ready(a->target)` path below, exactly like a self/ground/item action.
    // CR-06 (260902/cr4): the selector kill switch -- off means an RL run takes exactly the
    // pre-228-02 path (see sim.hpp's own option comment).
    const bool is_rl_actor = std::strcmp( p->name(), RL_ACTOR_NAME ) == 0 && !p->is_pet();
    if ( out_resolvable[ i ] && p->sim->target_select_enabled && is_rl_actor &&
         rl_target_select::is_targeted_action( a ) )
    {
      any_targeted_action_seen = true;
      if ( is_decision_boundary )
      {
        rl_target_select::fill_pick( a, /*harmful=*/true, rl_target_select::preference_for( a ) );
        bool      found = false;
        player_t* pick  = rl_target_select::lookup_pick( a, &found );
        assert( found &&
                "rl_target_select: a pick just filled for this decision must be immediately "
                "readable (228-02, D-12)" );
        out_ready[ i ] = ( a->ready() && pick != nullptr && a->target_ready( pick ) ) ? 1 : 0;
      }
      else
      {
        // 260902/cr4 (CR-02): reached only when the earlier cache lookup above found nothing for
        // this decision (a scripted actor with decision_dump= and no solver arm never took the
        // boundary path) -- compute the plain gate bits off the action's CURRENT target rather
        // than a per-decision selector pick, since none was ever stamped for this decision. No
        // stamp bump, no fill_pick -- both would corrupt a stamp a boundary call never opened.
        out_ready[ i ] = ( a->ready() && a->target != nullptr && a->target_ready( a->target ) ) ? 1 : 0;
      }
      if ( out_ready[ i ] )
        any_targeted_action_ready = true;
      continue;
    }
    // 260902/FORK-01: AND the engine's own target gate (alive, not immune
    // while harmful, in range -- action.cpp:2462-2477) -- the SAME predicate
    // action_execute_event_t::execute re-checks at execute time
    // (action.cpp:243). Without this, a cast could be offered to the agent
    // that the engine itself would refuse to land on its current target --
    // e.g. a melee ability legal at 20 yards.
    // `out_resolvable[i]` already short-circuits `&&` for a null action
    // handle (wait entries), so `target_ready` is never reached without a
    // resolved cast action; `a->target != nullptr` is an explicit guard
    // against dereferencing a null target inside `target_ready`'s first
    // line (`candidate_target->is_sleeping()`).
    out_ready[ i ] = ( out_resolvable[ i ] && a->ready() && a->target != nullptr &&
                        a->target_ready( a->target ) ) ? 1 : 0;
  }

  if ( is_decision_boundary )
  {
    // CR-04 (260902/cr4): the deadlock census -- ONE registry action existing but none of them
    // ready this decision, for the RL-controlled actor, on the boundary call only (never
    // double-counted by the later non-boundary decision_dump::record() call).
    if ( any_targeted_action_seen && !any_targeted_action_ready )
      rl_target_select::record_every_targeted_action_illegal();

    // 260902/cr4 (CR-02): cache what THIS boundary call just computed, tagged with the player and
    // the CURRENT stamp -- the later non-boundary call (decision_dump::record()) reads this back
    // instead of recomputing.
    gate_bits_cache_entry& entry = g_gate_bits_cache[ p ];
    entry.stamp     = rl_target_select::current_decision_stamp( p );
    entry.has_stamp = true;
    std::memcpy( entry.resolvable, out_resolvable, RL_ACTION_DIM );
    std::memcpy( entry.ready, out_ready, RL_ACTION_DIM );
  }
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
  direct,
  legality,   // 260831-mk7 (D-1/D-2): the mask-as-input legality family
  target_fact,  // 228-09 (D-23/TGT-08): the new per-action TARGET FACT family -- crosses
                // action_expression's member shape (member IS the registry token) with
                // enemy_slot's per-decision engine-truth value shape (a fact about the
                // action's own stamped pick, never an expression string)
  shape_fact,   // 228-11 (D-16): the two SHAPED actions' own descriptive hit-set facts --
                // member IS the action token (crash_lightning/sundering), value comes from
                // 228-10's compute_crash_lightning_shape()/compute_sundering_shape(), never a
                // per-candidate pick (the shaped actions have none, OR-2)
  unresolved
};

enum class buff_leaf_kind { stacks, remains };

// The `scalars` pseudo-family's `direct`-kind leaves this task resolves,
// PLUS (220-05 Task 2) the `stats`/`swing_cast`/`position` families' own
// `direct`-kind leaves and the `sim_auras` family's single `skyfury` member
// (declared `rl_family_kind::expression` in the generated table alongside
// deck/pets/items/raid_events, but bound `direct` here -- `sim->auras.*` is
// a raid-wide buff_t* with no `sim.auras.<name>` APL expression form at all,
// verified empirically; a direct engine read is both correct and cheaper
// than round-tripping through a nonexistent expression string).
// `fight_remains` needs no entry here -- it is `derived` (rl_leaf_desc::
// derived), and build_obs' derived-first dispatch short-circuits before
// ever consulting a binding's `direct` id, exactly as the retired
// per-field walk did for the same field.
enum class direct_id
{
  active_enemies, t, maelstrom,
  // stats
  stats_attack_haste, stats_attack_crit_chance, stats_mastery_value,
  stats_damage_versatility, stats_attack_power,
  // swing_cast
  swing_cast_auto_attack_interval, swing_cast_casting_remains, swing_cast_gcd_length,
  swing_cast_gcd_remains, swing_cast_swing_mh_remains, swing_cast_swing_oh_remains,
  // position
  position_current_distance, position_distance_to_move, position_movement_speed,
  position_x, position_y,
  // sim_auras
  sim_aura_skyfury,
  // scalars (Task 3)
  raid_event_next_in,
  // scalars (260831-0hh, BL-02)
  time_to_bloodlust,
  // 228-11 (D-16, TGT-05/TGT-06): the identity-free fight-wide aggregates and the two
  // scripted-schedule immunity timers -- every value 228-10's compute_fight_wide_aggregates()/
  // compute_invulnerability_window() already computes, cached ONCE per build_obs() call (D-12).
  fw_enemies_total, fw_enemies_in_melee, fw_enemies_within_8yd, fw_enemies_within_40yd,
  fw_enemies_in_front, fw_flame_shock_carrier_count, fw_soonest_time_to_die,
  fw_longest_time_to_die, fw_dying_within_5s, fw_dying_within_15s, fw_nearest_enemy_distance,
  fw_immunity_in, fw_immunity_remaining
};

// rl_family_kind::cooldown's own `direct`-style dispatch (220-05 Task 2):
// which cooldown_t accessor a leaf means, decided once at bind time -- never
// a strcmp inside build_obs. `full_recharge_time` is NOT here: it is the
// only leaf in this schema resolved through the ACTION rather than the raw
// cooldown_t* (see resolve_cooldown_member's own comment), so it binds
// `slot_binding_kind::expression` instead.
enum class cooldown_leaf_kind { remains, charges, charges_fractional, recharge_time, max_charges };

// 228-11 (D-A/TGT-05): the `enemy_slots` family (present/distance/time_to_die/health_pct/role
// per-slot, plus the per-effect dot/debuff leaves) was REMOVED by this plan -- the five shared
// enemy slots are gone; every targeted spell now carries its own target_fact-family leaves
// instead (see target_fact_leaf_kind below). enemy_actor_leaf_kind/enemy_effect_leaf_kind/
// enemy_dot_kind/enemy_buff_kind and their resolver/dispatch code were deleted in the same
// change (SLOT_PLUMBING_DELETED) -- not left dead beside a family that no longer exists.

// 220-06 Task 3: the `action_leaves` family's own per-leaf dispatch.
// `plain_expression` covers every leaf resolved through create_expression
// (on the action OR the player, per resolve_action_leaf's own comment) --
// the resolved expr_t lives in slot_table::owned_expressions exactly like
// every other expression-kind binding in this file, no new ownership
// mechanism needed. The three `shared_*` kinds are OBS-07's cost-split
// mechanism: hit_damage/crit_pct_current/persistent_multiplier for the SAME
// action share ONE `action_state_t*` (owned in
// slot_table::owned_action_states, allocated once at bind, snapshotted once
// per action per DECISION in build_obs -- never per leaf, never per bind).
// The two `dot_molten_weapon_*` kinds are lava_lash's Molten Weapon residual
// (220-RESEARCH.md's own citation: "per-enemy dot_list, same as
// enemy_slots") -- read fresh off the CURRENT target's dot_list each
// decision (never cached at bind time: the current target, and therefore
// which dot_t* instance answers this leaf, can change between decisions).
enum class action_leaf_kind
{
  plain_expression,
  shared_hit_damage, shared_crit_pct_current, shared_persistent_multiplier, shared_da_multiplier,
  dot_molten_weapon_ticking, dot_molten_weapon_remains
  // 228-11 (Q19, D-A/R-D): spell_targets_count REMOVED -- the deterministic geometry leaf
  // (target_fact.chain_lightning.neighbours_within_jump) replaces the spell_targets leaf outright.
};

// ---------------------------------------------------------------------------
// 228-09 (D-23/TGT-08): `target_fact` resolution (rl_family_kind::target_fact). The member IS
// the registry token (resolve_action_leaf's own shape, `p->find_action(engine_token)`); the
// per-decision VALUE is a fact about that action's stamped pick
// (rl_target_select::lookup_pick() + build_enemy_fact(), enemy_slot's own per-candidate shape,
// never a fresh select() -- D-12). `found` reports whether a pick was stamped for this action at
// all this decision (a targeted action can be legal-but-unpicked only in the all-illegal/no-
// candidate case, T-228-02-01); every other leaf reads `absent` when `found` is false, since a
// fact about a pick that does not exist has no meaning. No leaf of this family is declared by any
// census artifact yet (plan 228-11's job) -- this dispatch arm is therefore UNREACHABLE at
// runtime today; see this plan's own receipt for the explicit statement of what that does and
// does not prove.
// ---------------------------------------------------------------------------
enum class target_fact_leaf_kind
{
  found, distance, in_front, in_reach, alive, immune, time_to_die, health_pct,
  is_boss, flame_shock_remaining, is_current_target,
  actor_index, actor_spawn_index,
  // 228-11 (D-16 full inventory, TGT-04/TGT-06): the leaves D-16 names beyond what 228-09 wired
  // in -- every one already a field on enemy_fact (228-02/228-10), read the same way every
  // existing leaf above is (build_enemy_fact() -- never a fresh select() call, D-12).
  in_range, immunity_remaining, burning_core_remaining, lightning_rod_stacks,
  lightning_rod_remaining, venomfang_remaining, venomfang_debuff_stacks,
  venomfang_debuff_remaining, rune_of_unleashed_fire_lingering_remaining,
  // OBS-01 (232-02): the previous-pick boolean leaf is REMOVED (the tie-break that consumed it
  // is gone from select()). OBS-03 (232-02) collapsed the always-equal neighbour pair to one
  // leaf, renamed to name the rule it encodes.
  neighbours_within_radius
};

// ---------------------------------------------------------------------------
// 228-11 (D-16, TGT-04): `shape_fact` resolution (rl_family_kind::shape_fact). The member IS the
// action token (crash_lightning/sundering, OR-2's two SHAPED actions -- no census artifact
// declares a member of this family before this plan). The per-decision VALUE comes from 228-10's
// compute_crash_lightning_shape()/compute_sundering_shape(), computed from the CURRENT facing
// only (there is no per-decision pick to read -- the shaped actions never appear in
// targeted_picks, OR-2).
// ---------------------------------------------------------------------------
enum class shape_fact_action_kind { crash_lightning, sundering };
enum class shape_fact_leaf_kind { enemies_hit, summed_remaining_life, long_lived_count };

struct slot_binding
{
  slot_binding_kind kind = slot_binding_kind::unresolved;
  const rl_leaf_desc* leaf = nullptr;   // this slot's own descriptor -- resolved once, indexed per decision
  buff_t* buff = nullptr;               // kind == buff
  buff_leaf_kind buff_leaf = buff_leaf_kind::stacks;   // kind == buff
  direct_id direct = direct_id::t;      // kind == direct
  expr_t* expr = nullptr;               // kind == expression -- non-owning; owned by slot_table::owned_expressions
  cooldown_t* cooldown = nullptr;                        // kind == cooldown
  cooldown_leaf_kind cooldown_leaf = cooldown_leaf_kind::remains;  // kind == cooldown

  // 228-11: the enemy_slot fields formerly here were removed with the family (D-A/TGT-05).

  // kind == action_expression (220-06 Task 3).
  action_leaf_kind action_leaf = action_leaf_kind::plain_expression;
  action_t* bound_action = nullptr;              // non-owning; the engine owns every action_t
                                                  // (ALSO used by kind == target_fact, 228-09 -- the
                                                  // action whose stamped pick this leaf reads)
  action_state_t* shared_action_state = nullptr; // non-owning; owned by slot_table::owned_action_states,
                                                  // shared by all three shared_* leaves of this SAME action

  // kind == target_fact (228-09, D-23/TGT-08): which fact about bound_action's stamped pick
  // this leaf reads -- resolved ONCE at bind time from the leaf name, never re-parsed per
  // decision.
  target_fact_leaf_kind target_fact_leaf = target_fact_leaf_kind::found;

  // kind == shape_fact (228-11, D-16): which SHAPED action and which of its three descriptive
  // leaves -- both resolved ONCE at bind time from the member/leaf name, never re-parsed per
  // decision.
  shape_fact_action_kind shape_fact_action = shape_fact_action_kind::crash_lightning;
  shape_fact_leaf_kind shape_fact_leaf = shape_fact_leaf_kind::enemies_hit;

  // kind == legality (260831-mk7, D-1/D-2): the action index this slot's
  // ordinal member name resolves to -- parsed ONCE at bind time from
  // `mem.member` ("00".."25"), never re-parsed per decision. -1 means
  // unresolved (caught by the bind-time dispatch below, which never
  // constructs a `legality`-kind binding with this left at -1).
  int legality_action_index = -1;
};

struct slot_table
{
  std::vector<slot_binding> bindings;        // size RL_OBS_DIM, slot order
  std::vector<std::string>  composed_names;  // size RL_OBS_DIM -- the WALK's own names
  int first_name_divergence = -1;            // -1 == the walk agreed with RL_OBS_NAMES at every slot
  // 220-05 OBS-04: expr_t objects returned by player_t::create_expression are
  // owned here (kept alive for the actor's whole run, same lifetime as the
  // slot_table itself in g_slot_table_cache) -- slot_binding::expr is a
  // non-owning raw pointer into this vector, never re-created per decision.
  std::vector<std::unique_ptr<expr_t>> owned_expressions;
  // 220-06 Task 3 (OBS-07): ONE action_state_t* per action that declares any
  // of hit_damage/crit_pct_current/persistent_multiplier, allocated once at
  // bind time via action_t::get_state() (a plain `new`, safe to `delete` --
  // action_state_expr_t's own destructor does exactly that, see
  // action.cpp:3199-3202) and reused every decision -- never reallocated,
  // never released mid-run (same process-lifetime-leak convention this file
  // already uses for owned_expressions and every raw engine handle it never
  // frees).
  std::vector<std::unique_ptr<action_state_t>> owned_action_states;
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
  if ( std::strcmp( leaf.leaf, "raid_event_next_in" ) == 0 )
  {
    // 220-05 Task 3 (RIG-05): computed from the public sim->raid_events +
    // until_next() in build_obs, never through create_expression -- see
    // that switch arm's own comment for the missing-substitution rule.
    b.kind = slot_binding_kind::direct;
    b.direct = direct_id::raid_event_next_in;
    return b;
  }
  if ( std::strcmp( leaf.leaf, "time_to_bloodlust" ) == 0 )
  {
    // 260831-0hh (BL-02): engine-emitted countdown, computed in build_obs
    // via player_t::calculate_time_to_bloodlust() -- never through
    // create_expression, mirroring raid_event_next_in's own direct dispatch.
    b.kind = slot_binding_kind::direct;
    b.direct = direct_id::time_to_bloodlust;
    return b;
  }
  // 228-11 (D-16, TGT-05/TGT-06): the identity-free fight-wide aggregates and the two
  // scripted-schedule immunity timers -- computed in build_obs via 228-10's
  // compute_fight_wide_aggregates()/compute_invulnerability_window(), never through
  // create_expression, mirroring raid_event_next_in's/time_to_bloodlust's own direct dispatch.
  direct_id fw_id = direct_id::t;  // never read unless fw_matched is also set true below
  bool fw_matched = true;
  if ( std::strcmp( leaf.leaf, "enemies_total" ) == 0 )                    fw_id = direct_id::fw_enemies_total;
  else if ( std::strcmp( leaf.leaf, "enemies_in_melee" ) == 0 )            fw_id = direct_id::fw_enemies_in_melee;
  else if ( std::strcmp( leaf.leaf, "enemies_within_8yd" ) == 0 )          fw_id = direct_id::fw_enemies_within_8yd;
  else if ( std::strcmp( leaf.leaf, "enemies_within_40yd" ) == 0 )         fw_id = direct_id::fw_enemies_within_40yd;
  else if ( std::strcmp( leaf.leaf, "enemies_in_front" ) == 0 )            fw_id = direct_id::fw_enemies_in_front;
  else if ( std::strcmp( leaf.leaf, "flame_shock_carrier_count" ) == 0 )   fw_id = direct_id::fw_flame_shock_carrier_count;
  else if ( std::strcmp( leaf.leaf, "soonest_time_to_die" ) == 0 )         fw_id = direct_id::fw_soonest_time_to_die;
  else if ( std::strcmp( leaf.leaf, "longest_time_to_die" ) == 0 )         fw_id = direct_id::fw_longest_time_to_die;
  else if ( std::strcmp( leaf.leaf, "dying_within_5s" ) == 0 )             fw_id = direct_id::fw_dying_within_5s;
  else if ( std::strcmp( leaf.leaf, "dying_within_15s" ) == 0 )            fw_id = direct_id::fw_dying_within_15s;
  else if ( std::strcmp( leaf.leaf, "nearest_enemy_distance" ) == 0 )      fw_id = direct_id::fw_nearest_enemy_distance;
  else if ( std::strcmp( leaf.leaf, "immunity_in" ) == 0 )                 fw_id = direct_id::fw_immunity_in;
  else if ( std::strcmp( leaf.leaf, "immunity_remaining" ) == 0 )          fw_id = direct_id::fw_immunity_remaining;
  else                                                                     fw_matched = false;
  if ( fw_matched )
  {
    b.kind = slot_binding_kind::direct;
    b.direct = fw_id;
    return b;
  }
  // anything else this task does not bind.
  b.kind = slot_binding_kind::unresolved;
  return b;
}

// rl_family_kind::legality resolution (260831-mk7, D-1/D-2): one member per
// declared action, member NAME is a zero-padded ORDINAL INDEX -- never an
// action token (the census artifact's own derivation rule, mirrored here:
// genericity.selftest.py's Half-1 token derivation would otherwise promote
// action labels to forbidden tokens). The ordinal in the member name IS the
// action index -- parsed here, once, at bind time via strtol, never
// re-parsed per decision and never derived from a second source (no
// engine handle to resolve at all: build_obs reads the CALLER's own
// already-computed `mask` argument directly at this index, D-3).
slot_binding resolve_legality_leaf( const char* member_name, const rl_leaf_desc& leaf )
{
  slot_binding b;
  b.leaf = &leaf;
  char* end = nullptr;
  long parsed = std::strtol( member_name, &end, 10 );
  if ( member_name[ 0 ] == '\0' || end == member_name || *end != '\0' || parsed < 0 ||
       static_cast<std::size_t>( parsed ) >= RL_ACTION_DIM )
  {
    // A generator/artifact mismatch (a non-numeric or out-of-range member
    // name under a family declaring materializedFromActions), not a
    // runtime data problem -- unresolved, matching every other resolver's
    // own convention for a shape it cannot bind.
    b.kind = slot_binding_kind::unresolved;
    return b;
  }
  b.kind = slot_binding_kind::legality;
  b.legality_action_index = static_cast<int>( parsed );
  return b;
}

// Shared plumbing for every `expression`-kind binding (220-05 OBS-04/Task 2):
// a resolved (possibly null) expr_t is moved into `table.owned_expressions`
// (kept alive for the actor's whole run -- eval() is called once PER
// DECISION from build_obs, never re-created there) and the binding holds
// only a non-owning raw pointer into that vector. A null `expr` (whether
// because create_expression legitimately returned nullptr, or because the
// caller already caught a throw) binds `unresolved`.
slot_binding bind_expression_result( std::unique_ptr<expr_t> expr, const rl_leaf_desc& leaf,
                                      slot_table& table )
{
  slot_binding b;
  b.leaf = &leaf;
  if ( !expr )
  {
    b.kind = slot_binding_kind::unresolved;
    return b;
  }
  table.owned_expressions.push_back( std::move( expr ) );
  b.kind = slot_binding_kind::expression;
  b.expr = table.owned_expressions.back().get();
  return b;
}

// rl_family_kind::expression resolution (220-05 OBS-04, Pitfall 5): the
// class-agnostic seam -- every deck/pet/item/raid-event member is an
// expression NAME that arrives from the census artifact's `engine_token`,
// never a spell token core RL code names itself (ruling R-4).
// `player_t::create_expression` throws `sc_invalid_apl_argument` for an
// unrecognised name (player.cpp:12263/12281/12596) -- wrapped in try/catch
// HERE, at bind time, so a failure becomes an `unresolved` binding (named in
// the names file) rather than aborting the run mid-fight with a confusing
// APL-argument message.
slot_binding resolve_expression_leaf( player_t* p, const std::string& engine_token,
                                       const rl_leaf_desc& leaf, slot_table& table )
{
  std::unique_ptr<expr_t> expr;
  try
  {
    expr = p->create_expression( engine_token );
  }
  catch ( const std::exception& )
  {
    expr = nullptr;
  }
  return bind_expression_result( std::move( expr ), leaf, table );
}

// Same as resolve_expression_leaf, but resolved through an ACTION's own
// create_expression rather than the player's -- 220-05 Task 2's
// `full_recharge_time` leaf (the cooldown family's `strike` member) needs
// this: `cooldown.<name>.full_recharge_time` is not a recognised generic
// cooldown expression (only `cooldown.<name>.remains` and friends are), but
// `action.<name>.full_recharge_time` reaches the action's own real cooldown
// object directly. A null `action` (find_action failed to resolve the
// engine_token->action-name mapping) binds `unresolved` without ever
// calling create_expression on a null pointer.
slot_binding resolve_action_expression_leaf( action_t* action, const std::string& expr_name,
                                              const rl_leaf_desc& leaf, slot_table& table )
{
  std::unique_ptr<expr_t> expr;
  if ( action != nullptr )
  {
    try
    {
      expr = action->create_expression( expr_name );
    }
    catch ( const std::exception& )
    {
      expr = nullptr;
    }
  }
  return bind_expression_result( std::move( expr ), leaf, table );
}

// rl_family_kind::cooldown resolution (220-05 Task 2): the named cooldown_t*
// is resolved ONCE per member (bind_slots' own member-level cache, mirroring
// the buff family's member_buff cache) and every leaf but one dispatches to
// a `cooldown`-kind binding read directly off that handle in build_obs --
// never through create_expression, since a raw pointer read is both cheaper
// (OBS-07's hot-path concern) and simpler than round-tripping an expression
// string for the SAME accessor read_state already used pre-220-04
// (rl_policy_obs.cpp:189-200's retired walk).
//
// `full_recharge_time` is the ONE exception, per this task's own action
// text: it is resolved through the ACTION, not the cooldown_t*, because
// `cooldown.<name>.full_recharge_time` is not a real generic-cooldown
// expression (verified empirically) and a cooldown NAME does not always
// equal its action's name -- in this schema `full_recharge_time` appears
// ONLY on the `strike` member (the shared Stormstrike/Windstrike cooldown
// row), whose real action is named "stormstrike", not "strike"; that one
// mapping is hardcoded here rather than assumed identical to every other
// cooldown member (which never need it -- no OTHER member in this census
// declares a `full_recharge_time` leaf).
//
// Deliberately does NOT gate `charges_fractional` on
// `p->resources.is_active(RESOURCE_MAELSTROM)` (the SC-5 byte-neutrality
// contract 220-04 preserved for the `maelstrom` scalar) -- that gate exists
// for the retired FIFO transport's wire-omission concern, not for whether
// the engine can answer the read, and CR-01's actor filter means only the
// shaman ever reaches this path (resolve_scalar_leaf's own 220-04 comment
// makes the identical argument for `maelstrom`).
slot_binding resolve_cooldown_leaf( player_t* p, cooldown_t* cd, const std::string& cooldown_token,
                                     const rl_leaf_desc& leaf, slot_table& table )
{
  if ( std::strcmp( leaf.leaf, "full_recharge_time" ) == 0 )
  {
    const std::string action_token = ( cooldown_token == "strike" ) ? "stormstrike" : cooldown_token;
    return resolve_action_expression_leaf( p->find_action( action_token ), "full_recharge_time", leaf, table );
  }

  slot_binding b;
  b.leaf = &leaf;
  if ( cd == nullptr )
  {
    b.kind = slot_binding_kind::unresolved;
    return b;
  }
  b.cooldown = cd;
  b.kind = slot_binding_kind::cooldown;
  if ( std::strcmp( leaf.leaf, "remains" ) == 0 )
    b.cooldown_leaf = cooldown_leaf_kind::remains;
  else if ( std::strcmp( leaf.leaf, "charges" ) == 0 )
    b.cooldown_leaf = cooldown_leaf_kind::charges;
  else if ( std::strcmp( leaf.leaf, "charges_fractional" ) == 0 )
    b.cooldown_leaf = cooldown_leaf_kind::charges_fractional;
  else if ( std::strcmp( leaf.leaf, "recharge_time" ) == 0 )
    b.cooldown_leaf = cooldown_leaf_kind::recharge_time;
  else if ( std::strcmp( leaf.leaf, "max_charges" ) == 0 )
    b.cooldown_leaf = cooldown_leaf_kind::max_charges;
  else
    b.kind = slot_binding_kind::unresolved;   // artifact/generator mismatch, not a runtime fact
  return b;
}

// rl_family_kind::direct resolution for `stats`/`swing_cast`/`position`
// (220-05 Task 2) -- a small closed mapping from (family id, member name) to
// a `direct_id`, decided once at bind time. Each member in these three
// families carries exactly one leaf (always named "value" in this schema),
// so no per-leaf dispatch is needed beyond the member-name match itself.
slot_binding resolve_stats_swing_cast_position_leaf( rl_family fam_id, const std::string& member,
                                                      const rl_leaf_desc& leaf )
{
  slot_binding b;
  b.leaf = &leaf;
  b.kind = slot_binding_kind::direct;

  if ( fam_id == rl_family::stats )
  {
    if ( member == "attack_power" )      b.direct = direct_id::stats_attack_power;
    else if ( member == "crit" )         b.direct = direct_id::stats_attack_crit_chance;
    else if ( member == "damage_versatility" ) b.direct = direct_id::stats_damage_versatility;
    else if ( member == "haste" )        b.direct = direct_id::stats_attack_haste;
    else if ( member == "mastery_value" ) b.direct = direct_id::stats_mastery_value;
    else b.kind = slot_binding_kind::unresolved;
  }
  else if ( fam_id == rl_family::swing_cast )
  {
    if ( member == "auto_attack_interval" ) b.direct = direct_id::swing_cast_auto_attack_interval;
    else if ( member == "casting_remains" ) b.direct = direct_id::swing_cast_casting_remains;
    else if ( member == "gcd_length" )      b.direct = direct_id::swing_cast_gcd_length;
    else if ( member == "gcd_remains" )     b.direct = direct_id::swing_cast_gcd_remains;
    else if ( member == "swing_mh_remains" ) b.direct = direct_id::swing_cast_swing_mh_remains;
    else if ( member == "swing_oh_remains" ) b.direct = direct_id::swing_cast_swing_oh_remains;
    else b.kind = slot_binding_kind::unresolved;
  }
  else if ( fam_id == rl_family::position )
  {
    if ( member == "current_distance" )    b.direct = direct_id::position_current_distance;
    else if ( member == "distance_to_move" ) b.direct = direct_id::position_distance_to_move;
    else if ( member == "movement_speed" ) b.direct = direct_id::position_movement_speed;
    else if ( member == "x" )              b.direct = direct_id::position_x;
    else if ( member == "y" )              b.direct = direct_id::position_y;
    else b.kind = slot_binding_kind::unresolved;
  }
  else
  {
    b.kind = slot_binding_kind::unresolved;
  }
  return b;
}

// rl_family_kind::expression resolution for the `pets` family (220-05 Task
// 2). Two sub-strategies, both class-agnostic in NAME (the wiring lives
// here, in a shared file, never in sc_shaman.cpp -- ruling R-4 is about core
// RL code naming no spell/pet token; this switch is the census's own
// resolution logic, exactly like raid_events' "adds"/"movement" -> "raid_event.*"
// composition below):
//   - "n_active_pets"/"active" leaves compose "pet.<token>.active", which
//     resolves through player_t::create_expression's pet_spawner_t branch to
//     a live COUNT (pet_spawner_t::create_expression, "active" ->
//     n_active_pets()) for a multi-pet spawner, or a 0/1 liveness bit for a
//     single find_pet() match -- either way a correct numeric encoding.
//   - "remains" composes "pet.<token>.remains" (spawner: soonest-to-expire
//     active pet; single pet: its own expiration).
//   - "pulse_event_remains" (searing_totem/surging_totem only) uses the two
//     new shaman_t-level expressions this task's fork edit added, NOT
//     "pet.<token>.pulse_event_remains" -- that string throws (verified
//     empirically: no existing APL expression reaches a totem's raw
//     pulse_event timer through the spawner path).
// "the wolves" (all_wolves/fire_wolves/lightning_wolves) route through the
// shaman's own soft-failing `feral_spirit.active`/`feral_spirit.remains`
// forms instead of `pet.<token>.*` -- there is no per-color wolf split in
// this fork (one Feral Spirit cast spawns an undifferentiated wolf pack), so
// all three members read the SAME feral_spirit.* value; 220-07's liveness
// census prunes the resulting cross-slot duplication by its own
// "redundant-with:<slot>" rule (CONTEXT.md ruling), not by special-casing it
// away here.
slot_binding resolve_pets_leaf( player_t* p, const std::string& member, const std::string& engine_token,
                                 const rl_leaf_desc& leaf, slot_table& table )
{
  static const std::set<std::string> wolves{ "all_wolves", "fire_wolves", "lightning_wolves" };

  if ( wolves.count( member ) )
  {
    if ( std::strcmp( leaf.leaf, "n_active_pets" ) == 0 )
      return resolve_expression_leaf( p, "feral_spirit.active", leaf, table );
    if ( std::strcmp( leaf.leaf, "remains" ) == 0 )
      return resolve_expression_leaf( p, "feral_spirit.remains", leaf, table );
    return bind_expression_result( nullptr, leaf, table );   // artifact mismatch -> unresolved
  }

  if ( std::strcmp( leaf.leaf, "pulse_event_remains" ) == 0 )
  {
    if ( member == "surging_totem" )
      return resolve_expression_leaf( p, "surging_totem_pulse_remains", leaf, table );
    if ( member == "searing_totem" )
      return resolve_expression_leaf( p, "searing_totem_pulse_remains", leaf, table );
    return bind_expression_result( nullptr, leaf, table );
  }

  std::string suffix;
  if ( std::strcmp( leaf.leaf, "n_active_pets" ) == 0 || std::strcmp( leaf.leaf, "active" ) == 0 )
    suffix = "active";
  else if ( std::strcmp( leaf.leaf, "remains" ) == 0 )
    suffix = "remains";
  else
    return bind_expression_result( nullptr, leaf, table );   // artifact mismatch -> unresolved

  return resolve_expression_leaf( p, "pet." + engine_token + "." + suffix, leaf, table );
}

// rl_family_kind::expression resolution for `items` (220-05 Task 2). Two
// members, two different real expression forms -- neither is the bare
// census token, exactly like raid_events/pets above.
//   - "midnight_season_2_4pc" -> "set_bonus.midnight_season_2_4pc" (a
//     player-scoped set-bonus check, real regardless of talent state).
//   - "trinket_has_use_buff" -> the OR of BOTH trinket slots'
//     `trinket.<N>.has_use_buff` (the census declares ONE aggregate member
//     for what the game exposes as two independently-equipped slots; a
//     composite `make_fn_expr` over up to two resolved sub-expressions is
//     the simplest correct aggregation -- max() over {0,1} boolean-ish
//     reads is OR). A slot with no trinket present is expected to throw
//     nothing here (verified empirically both trinket slots resolve
//     cleanly for this profile); either sub-expression failing to resolve
//     still lets the composite bind through the other.
slot_binding resolve_items_leaf( player_t* p, const std::string& member, const rl_leaf_desc& leaf,
                                  slot_table& table )
{
  if ( member == "midnight_season_2_4pc" )
    return resolve_expression_leaf( p, "set_bonus.midnight_season_2_4pc", leaf, table );

  if ( member == "trinket_has_use_buff" )
  {
    expr_t* e1 = nullptr;
    expr_t* e2 = nullptr;
    try
    {
      if ( auto e = p->create_expression( "trinket.1.has_use_buff" ) )
      {
        table.owned_expressions.push_back( std::move( e ) );
        e1 = table.owned_expressions.back().get();
      }
    }
    catch ( const std::exception& ) { }
    try
    {
      if ( auto e = p->create_expression( "trinket.2.has_use_buff" ) )
      {
        table.owned_expressions.push_back( std::move( e ) );
        e2 = table.owned_expressions.back().get();
      }
    }
    catch ( const std::exception& ) { }

    if ( e1 == nullptr && e2 == nullptr )
      return bind_expression_result( nullptr, leaf, table );

    auto composite = make_fn_expr( "trinket_has_use_buff", [ e1, e2 ]() {
      double v = 0.0;
      if ( e1 != nullptr ) v = std::max( v, e1->eval() );
      if ( e2 != nullptr ) v = std::max( v, e2->eval() );
      return v;
    } );
    return bind_expression_result( std::move( composite ), leaf, table );
  }

  return bind_expression_result( nullptr, leaf, table );   // artifact mismatch -> unresolved
}

// rl_family_kind::expression resolution for `sim_auras` (220-05 Task 2),
// bound `direct` (not `expression`) -- see direct_id's own comment above for
// why: `sim->auras.skyfury` is a raid-wide buff_t* with no matching APL
// expression string at all (verified empirically), so the correct and
// cheapest read is direct engine access, exactly like the `maelstrom`
// scalar.
slot_binding resolve_sim_auras_leaf( const std::string& member, const rl_leaf_desc& leaf )
{
  slot_binding b;
  b.leaf = &leaf;
  if ( member == "skyfury" )
  {
    b.kind = slot_binding_kind::direct;
    b.direct = direct_id::sim_aura_skyfury;
  }
  else
  {
    b.kind = slot_binding_kind::unresolved;
  }
  return b;
}

// ---------------------------------------------------------------------------
// 228-11 (D-A/TGT-05): `enemy_slots` resolution (parse_enemy_slot_member, resolve_enemy_slot_leaf,
// compute_enemy_slot_candidates, RL_ENEMY_SLOT_COUNT) was REMOVED here -- the five shared enemy
// slots are gone, replaced by the per-action target_fact family below. get_enemy_handle_cache
// (next) is UNCHANGED and stays -- action_leaf_kind::dot_molten_weapon_* still reads it.
// ---------------------------------------------------------------------------

// 220-06 Task 2: a lazily-filled per-enemy handle cache, keyed on the raw
// `player_t*`. Filled the FIRST time an enemy appears in a candidate slot --
// not at bind time, because adds arise mid-fight under HecticAddCleave and
// do not exist when the shaman's slot table is built (220-RESEARCH.md
// Assumptions Log A5: add `player_t*` stability is INFERRED from
// `adds_event_t`'s pre-created `std::vector<pet_t*> adds`, not PROVEN).
//
// VALIDATED on every use, never trusted from a one-time fill, via two
// independent checks: (1) `t->actor_index` must still match what the cache
// was filled for -- catches the corruption case where a `player_t*` value
// gets reused for a DIFFERENT actor identity, the honest response to an
// inferred-not-proven stability assumption; (2) a still-null handle is NOT
// treated as a permanent "this effect doesn't exist on this enemy" answer --
// it is RESCANNED every call while it is still null, because the engine can lazily
// construct a target-specific debuff/dot object (`td()`) the first time this
// actor is actually TARGETED, which can happen strictly after this function
// first saw the actor in a slot.
struct enemy_handle_cache
{
  std::size_t validated_actor_index = static_cast<std::size_t>( -1 );
  buff_t* burning_core = nullptr;
  buff_t* casting = nullptr;
  buff_t* flametongue_attack = nullptr;
  buff_t* lashing_flames = nullptr;
  buff_t* lightning_rod = nullptr;
  buff_t* venomfang_debuff = nullptr;
  dot_t*  flame_shock = nullptr;
  dot_t*  rune_of_unleashed_fire_lingering = nullptr;
  dot_t*  venomfang = nullptr;
  // 220-08 WR-03 (220-REVIEW.md): molten_weapon (lava_lash's residual,
  // action_leaf_kind::dot_molten_weapon_*) reused this SAME per-target
  // lazy cache instead of its own bespoke per-decision dot_list scan --
  // same source==p predicate, same target-identity validation, no new
  // caching mechanism.
  dot_t*  molten_weapon = nullptr;
};

std::unordered_map<const player_t*, enemy_handle_cache> g_enemy_handle_cache;

enemy_handle_cache& get_enemy_handle_cache( const player_t* p, player_t* t )
{
  auto& c = g_enemy_handle_cache[ t ];
  if ( c.validated_actor_index != t->actor_index )
  {
    c = enemy_handle_cache{};
    c.validated_actor_index = t->actor_index;
  }

  const bool need_buff_scan = c.burning_core == nullptr || c.casting == nullptr ||
      c.flametongue_attack == nullptr || c.lashing_flames == nullptr ||
      c.lightning_rod == nullptr || c.venomfang_debuff == nullptr;
  if ( need_buff_scan )
  {
    for ( buff_t* b : t->buff_list )
    {
      if ( b->source != p )
        continue;
      if ( c.burning_core == nullptr && b->name_str == "burning_core" )                  c.burning_core = b;
      else if ( c.casting == nullptr && b->name_str == "casting" )                       c.casting = b;
      else if ( c.flametongue_attack == nullptr && b->name_str == "flametongue_attack" ) c.flametongue_attack = b;
      else if ( c.lashing_flames == nullptr && b->name_str == "lashing_flames" )         c.lashing_flames = b;
      else if ( c.lightning_rod == nullptr && b->name_str == "lightning_rod" )           c.lightning_rod = b;
      else if ( c.venomfang_debuff == nullptr && b->name_str == "venomfang_debuff" )     c.venomfang_debuff = b;
    }
  }

  // 220-08 WR-03 addendum: molten_weapon does NOT gate need_dot_scan on its
  // own -- lava_lash is not yet in the RL action registry (action_leaves.
  // lava_lash.* is dead-by-action-set until Phase 221), so under the
  // in-process RL rig its dot practically never exists, and including it
  // here would make need_dot_scan permanently true (a full dot_list scan
  // EVERY call, for EVERY target, forever) the instant flame_shock/rune_of_
  // unleashed_fire_lingering/venomfang all resolve -- exactly the WR-04
  // perpetual-rescan problem, reintroduced by accident (measured: +9-10%
  // mean_ns on the OBS-07 rig before this fix, confirmed reproducible
  // across two runs). molten_weapon still rides the SAME scan loop below
  // opportunistically whenever one happens for the other three reasons, so
  // it is found for free the moment lava_lash starts being cast during a
  // scan window -- it simply never independently triggers one.
  const bool need_dot_scan = c.flame_shock == nullptr ||
      c.rune_of_unleashed_fire_lingering == nullptr || c.venomfang == nullptr;
  if ( need_dot_scan )
  {
    for ( dot_t* d : t->dot_list )
    {
      if ( d->source != p )
        continue;
      if ( c.flame_shock == nullptr && d->name_str == "flame_shock" )
        c.flame_shock = d;
      else if ( c.rune_of_unleashed_fire_lingering == nullptr && d->name_str == "rune_of_unleashed_fire_lingering" )
        c.rune_of_unleashed_fire_lingering = d;
      else if ( c.venomfang == nullptr && d->name_str == "venomfang" )
        c.venomfang = d;
      else if ( c.molten_weapon == nullptr && d->name_str == "molten_weapon" )
        c.molten_weapon = d;
    }
  }

  return c;
}

// ---------------------------------------------------------------------------
// 220-06 Task 3: `action_leaves` resolution (rl_family_kind::action_expression).
// ---------------------------------------------------------------------------

// OBS-07's cost-split mechanism: ONE `action_state_t*` per action that
// declares any of hit_damage/crit_pct_current/persistent_multiplier,
// allocated once at bind time (never per decision) and shared by all three
// leaves of that SAME action.
action_state_t* get_or_create_shared_action_state( action_t* a, slot_table& table )
{
  for ( auto& s : table.owned_action_states )
  {
    if ( s->action == a )
      return s.get();
  }
  action_state_t* fresh = a->get_state();
  table.owned_action_states.push_back( std::unique_ptr<action_state_t>( fresh ) );
  return fresh;
}

// rl_family_kind::action_expression resolution. `engine_token` IS the
// action's `find_action()` token for every member in this census (verified
// against the generated table: no `engineToken` override for `action_leaves`,
// unlike `pets`/`items`/`raid_events`). A null `find_action()` result binds
// EVERY leaf of this member `unresolved` and lets the census name it -- an
// untalented action is a census input, not an error (this task's own action
// text, step 1).
slot_binding resolve_action_leaf( player_t* p, const std::string& engine_token, const rl_leaf_desc& leaf,
                                   slot_table& table )
{
  action_t* a = p->find_action( engine_token );
  if ( a == nullptr )
  {
    slot_binding b;
    b.leaf = &leaf;
    b.kind = slot_binding_kind::unresolved;
    return b;
  }

  const std::string leaf_name = leaf.leaf;

  // The four shared-snapshot leaves (OBS-07, Task 3 step 2; `multiplier`
  // added 221-07) -- see build_obs' own dispatch for the derivation
  // arithmetic.
  if ( leaf_name == "hit_damage" || leaf_name == "crit_pct_current" ||
       leaf_name == "persistent_multiplier" || leaf_name == "multiplier" )
  {
    // 221-07 WR-01: `a->create_expression("multiplier")` is NOT usable for
    // this leaf -- action_t::create_expression (action.cpp) unconditionally
    // tries `dot_t::create_expression(nullptr, this, this, name, true)`
    // BEFORE its own name-match chain reaches "multiplier"
    // (action.cpp:~3368), and dot_t's own "multiplier" handler
    // (dot.cpp:561) is a DIFFERENT, unrelated alias -- the DOT tick
    // multiplier (`dot->state ? dot->state->ta_multiplier : 0`), which is
    // permanently 0 for every non-DOT action in this registry (proven via
    // live instrumentation: the real action_t::create_expression body for
    // "multiplier" never once executed for crash_lightning/lava_lash/
    // voltaic_blaze/sundering/windstrike across a live episode). The
    // MEANINGFUL "how hard is this ability hitting right now" signal is
    // `composite_da_multiplier()` -- the same per-action virtual this
    // schema's own persistent_multiplier leaf already calls the sibling
    // `composite_persistent_multiplier()` for -- so `multiplier` is folded
    // into the SAME shared-snapshot cache (no new `snapshot_state()` call,
    // OBS-07's cost discipline unchanged) rather than routed through
    // `create_expression` at all.
    //
    // 221-07 WR-02: `windstrike`/`voltaic_blaze` are DISPATCHER/CARRIER
    // actions with zero direct-damage component of their own (windstrike_t's
    // own comment: "Actual damaging attacks are done by stormstrike_attack_t";
    // voltaic_blaze_t's damage is entirely its `impact_action`,
    // voltaic_blaze_damage_t) -- `a->calculate_direct_amount()` on the
    // dispatcher itself is structurally always 0 (confirmed live:
    // base_dd_min/max=0, spell_power_mod.direct=0 on the resolved
    // `windstrike`/`voltaic_blaze` action objects). The REAL damage-dealing
    // child action is separately named and separately `find_action()`-able,
    // so the shared cache is snapshotted against THAT action instead --
    // mirrors this same file's own `full_recharge_time`/"strike" ->
    // "stormstrike" hardcoded remap precedent (resolve_cooldown_leaf, just
    // above). `ready`/every OTHER leaf of this member is UNCHANGED -- the
    // RL agent still needs the DISPATCHER's own ready() bit for legality,
    // only the four shared-cache damage leaves redirect.
    action_t* damage_action = a;
    if ( engine_token == "windstrike" )
    {
      if ( action_t* mh = p->find_action( "windstrike_mh" ) )
        damage_action = mh;
    }
    else if ( engine_token == "voltaic_blaze" )
    {
      if ( action_t* dmg = p->find_action( "voltaic_blaze_damage" ) )
        damage_action = dmg;
    }
    else if ( engine_token == "stormstrike" )
    {
      // 260901-pb1 LEVER A: `stormstrike` is the same dispatcher/carrier
      // shape as `windstrike` just above (stormstrike_t fires both a
      // main-hand and (talented) off-hand stormstrike_attack_t child; the
      // real damage-dealing action is separately named and separately
      // find_action()-able) -- mirrors this file's own
      // resolve_cooldown_leaf "strike" -> "stormstrike" precedent, one
      // level deeper (dispatcher action -> its own mh child).
      if ( action_t* mh = p->find_action( "stormstrike_mh" ) )
        damage_action = mh;
    }

    slot_binding b;
    b.leaf = &leaf;
    b.kind = slot_binding_kind::action_expression;
    b.bound_action = damage_action;
    b.shared_action_state = get_or_create_shared_action_state( damage_action, table );
    b.action_leaf = ( leaf_name == "hit_damage" ) ? action_leaf_kind::shared_hit_damage
                   : ( leaf_name == "crit_pct_current" ) ? action_leaf_kind::shared_crit_pct_current
                   : ( leaf_name == "persistent_multiplier" ) ? action_leaf_kind::shared_persistent_multiplier
                   : action_leaf_kind::shared_da_multiplier;
    return b;
  }

  // lava_lash's Molten Weapon residual -- read fresh off the CURRENT
  // target's dot_list each decision (220-RESEARCH.md's own citation:
  // "per-enemy dot_list, same as enemy_slots"), never through
  // create_expression: this action_leaves member is not per-slot, and the
  // action's own dot object tracks whichever target lava_lash last hit.
  if ( leaf_name == "molten_weapon_ticking" || leaf_name == "molten_weapon_remains" )
  {
    slot_binding b;
    b.leaf = &leaf;
    b.kind = slot_binding_kind::action_expression;
    b.bound_action = a;
    b.action_leaf = ( leaf_name == "molten_weapon_ticking" ) ? action_leaf_kind::dot_molten_weapon_ticking
                                                               : action_leaf_kind::dot_molten_weapon_remains;
    return b;
  }

  // The in_flight family -- the 3-part "action.<token>.<leaf>" form is
  // MANDATORY (action.cpp:4090-4110's `in_flight_singleton` branch): calling
  // `a->create_expression("in_flight")` directly makes the engine search
  // `player->action_list` for an action literally NAMED "in_flight".
  if ( leaf_name == "in_flight" || leaf_name == "in_flight_count" ||
       leaf_name == "in_flight_remains" || leaf_name == "in_flight_to_target" )
  {
    return resolve_action_expression_leaf( a, "action." + engine_token + "." + leaf_name, leaf, table );
  }

  // active_enemies_within_<yards> -- the census's member-name-safe leaf
  // spelling (a literal "." is not a valid member/leaf-name character); the
  // real expression is the 2-part "active_enemies_within.<yards>" form ON
  // THE ACTION (action.cpp:3673-3702) -- the 3-part "action.X..." form fails
  // its `splits.size() == 3` guard for this leaf (action.cpp:4096).
  if ( leaf_name.rfind( "active_enemies_within_", 0 ) == 0 )
  {
    const std::string yards = leaf_name.substr( std::strlen( "active_enemies_within_" ) );
    return resolve_action_expression_leaf( a, "active_enemies_within." + yards, leaf, table );
  }

  // pet.surging_totem.{active,remains} -- likewise the census's
  // member-name-safe spelling; resolved through the PLAYER (not the
  // action), wrapped -- it throws when the pet/spawner is absent
  // (player.cpp:12570-12633).
  if ( leaf_name == "pet_surging_totem_active" )
    return resolve_expression_leaf( p, "pet.surging_totem.active", leaf, table );
  if ( leaf_name == "pet_surging_totem_remains" )
    return resolve_expression_leaf( p, "pet.surging_totem.remains", leaf, table );

  // 228-11 (Q19, D-A/R-D): the "spell_targets" special case formerly here (FORK-03
  // Candidate 1) is REMOVED -- the census artifact no longer declares a `spell_targets` leaf
  // (the deterministic geometry leaf, target_fact.chain_lightning.neighbours_within_jump,
  // replaces it outright), so this bind-time special case is unreachable dead code beside a
  // retired leaf. Its own reasoning (avoiding create_expression's forced target_list() RNG
  // draw for "spell_targets") no longer applies to anything this schema declares.

  // Everything else this census declares (ready, travel_time, and any of
  // cast_time/execute_time/cost/usable_in/available_targets/the charge
  // leaves this census happens to use) is a plain action-scoped expression
  // -- a->create_expression(leaf_name). Never resolved through a
  // "cooldown.<spell>.*" name (the dead-alias-row trap, 220-RESEARCH.md
  // Pitfall 6) -- this path always goes through the ACTION. `multiplier`
  // moved OUT of this fallback (221-07 WR-01) -- see the shared-snapshot
  // branch above for why the bare create_expression("multiplier") name is
  // unusable for this leaf.
  return resolve_action_expression_leaf( a, leaf_name, leaf, table );
}

// 228-09 (D-23/TGT-08): `rl_family_kind::target_fact` resolution -- bind-time only. `member` IS
// the registry token, resolved through `find_action()` EXACTLY like `resolve_action_leaf` above
// (a null result binds `unresolved`, per that function's own comment: an untalented action is a
// census input, not an error). The LEAF NAME picks which fact about that action's per-decision
// stamped pick this slot reads -- decided ONCE here, never re-parsed per decision, mirroring
// the retired `resolve_enemy_slot_leaf`'s own WR-03 discipline (228-11: that resolver is gone,
// its discipline is the one this function follows).
slot_binding resolve_target_fact_leaf( player_t* p, const std::string& engine_token,
                                        const rl_leaf_desc& leaf, slot_table& )
{
  slot_binding b;
  b.leaf = &leaf;

  action_t* a = p->find_action( engine_token );
  if ( a == nullptr )
  {
    b.kind = slot_binding_kind::unresolved;
    return b;
  }

  const std::string& leaf_name = leaf.leaf;
  target_fact_leaf_kind fk;
  if ( leaf_name == "found" )                       fk = target_fact_leaf_kind::found;
  else if ( leaf_name == "distance" )                fk = target_fact_leaf_kind::distance;
  else if ( leaf_name == "in_front" )                fk = target_fact_leaf_kind::in_front;
  else if ( leaf_name == "in_reach" )                fk = target_fact_leaf_kind::in_reach;
  else if ( leaf_name == "alive" )                   fk = target_fact_leaf_kind::alive;
  else if ( leaf_name == "immune" )                  fk = target_fact_leaf_kind::immune;
  else if ( leaf_name == "time_to_die" )             fk = target_fact_leaf_kind::time_to_die;
  else if ( leaf_name == "health_pct" )               fk = target_fact_leaf_kind::health_pct;
  else if ( leaf_name == "is_boss" )                  fk = target_fact_leaf_kind::is_boss;
  else if ( leaf_name == "flame_shock_remaining" )    fk = target_fact_leaf_kind::flame_shock_remaining;
  else if ( leaf_name == "is_current_target" )        fk = target_fact_leaf_kind::is_current_target;
  else if ( leaf_name == "actor_index" )              fk = target_fact_leaf_kind::actor_index;
  else if ( leaf_name == "actor_spawn_index" )        fk = target_fact_leaf_kind::actor_spawn_index;
  // 228-11 (D-16 full inventory): every leaf below is already a field on enemy_fact
  // (228-02/228-10) -- see target_fact_leaf_kind's own comment.
  else if ( leaf_name == "in_range" )                                     fk = target_fact_leaf_kind::in_range;
  else if ( leaf_name == "immunity_remaining" )                           fk = target_fact_leaf_kind::immunity_remaining;
  else if ( leaf_name == "burning_core_remaining" )                       fk = target_fact_leaf_kind::burning_core_remaining;
  else if ( leaf_name == "lightning_rod_stacks" )                         fk = target_fact_leaf_kind::lightning_rod_stacks;
  else if ( leaf_name == "lightning_rod_remaining" )                      fk = target_fact_leaf_kind::lightning_rod_remaining;
  else if ( leaf_name == "venomfang_remaining" )                          fk = target_fact_leaf_kind::venomfang_remaining;
  else if ( leaf_name == "venomfang_debuff_stacks" )                      fk = target_fact_leaf_kind::venomfang_debuff_stacks;
  else if ( leaf_name == "venomfang_debuff_remaining" )                   fk = target_fact_leaf_kind::venomfang_debuff_remaining;
  else if ( leaf_name == "rune_of_unleashed_fire_lingering_remaining" )   fk = target_fact_leaf_kind::rune_of_unleashed_fire_lingering_remaining;
  else if ( leaf_name == "neighbours_within_radius" )                     fk = target_fact_leaf_kind::neighbours_within_radius;
  else
  {
    b.kind = slot_binding_kind::unresolved;   // artifact/generator mismatch, not a runtime fact
    return b;
  }

  b.kind = slot_binding_kind::target_fact;
  b.bound_action = a;
  b.target_fact_leaf = fk;
  return b;
}

// 228-11 (D-16, TGT-04): `rl_family_kind::shape_fact` resolution -- bind-time only. `member` is
// the action token (crash_lightning/sundering, the ONLY two shaped actions); the leaf name picks
// which of the three descriptive shape leaves this slot reads. Never resolves through
// `find_action()` -- the value comes from a fight-wide/facing-only computation
// (compute_crash_lightning_shape/compute_sundering_shape), not from a specific action_t*.
slot_binding resolve_shape_fact_leaf( const std::string& engine_token, const rl_leaf_desc& leaf )
{
  slot_binding b;
  b.leaf = &leaf;

  shape_fact_action_kind ak;
  if ( engine_token == "crash_lightning" )      ak = shape_fact_action_kind::crash_lightning;
  else if ( engine_token == "sundering" )       ak = shape_fact_action_kind::sundering;
  else
  {
    b.kind = slot_binding_kind::unresolved;   // artifact/generator mismatch -- not one of the two shaped tokens
    return b;
  }

  const std::string& leaf_name = leaf.leaf;
  shape_fact_leaf_kind fk;
  if ( leaf_name == "enemies_hit" )                    fk = shape_fact_leaf_kind::enemies_hit;
  else if ( leaf_name == "summed_remaining_life" )     fk = shape_fact_leaf_kind::summed_remaining_life;
  else if ( leaf_name == "long_lived_count" )          fk = shape_fact_leaf_kind::long_lived_count;
  else
  {
    b.kind = slot_binding_kind::unresolved;
    return b;
  }

  b.kind = slot_binding_kind::shape_fact;
  b.shape_fact_action = ak;
  b.shape_fact_leaf = fk;
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
  // 220-08 WR-12: g_slot_table_cache is keyed on a bare player_t* with no
  // sim identity and no clear -- correctness rests entirely on the
  // solver_policy= single-sim/single-thread clamp in sim.cpp (threads=1,
  // profileset refusal), which is asserted THERE but never checked at this
  // entry point. get_enemy_handle_cache validates t->actor_index (rl_policy_
  // obs.cpp:1108); this bare pointer lookup validated nothing. A future
  // in-process multi-sim caller (profileset support, a batch driver, a unit
  // test harness) would silently read a cached slot_table full of dangling
  // buff_t*/expr_t* for a recycled player_t* address -- assert the
  // precondition here rather than depending on it staying true forever.
  assert( p->sim->threads == 1 && p->sim->profileset_map.empty() &&
          "rl_policy slot cache is single-sim/single-thread by construction (220-08 WR-12)" );

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

      // Cooldown family (220-05 Task 2): same one-scan-per-member discipline
      // as the buff family above, over p->cooldown_list instead of
      // p->buff_list.
      cooldown_t* member_cooldown = nullptr;
      bool member_cooldown_scanned = false;

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
          case rl_family_kind::expression:
            // 220-05 Task 1 proved the seam on `deck`; Task 2 adds
            // `pets`/`items`/`raid_events` (Task 3) and (bound `direct`,
            // not `expression` -- see resolve_sim_auras_leaf's own comment)
            // `sim_auras`.
            if ( fam.id == rl_family::deck )
            {
              binding = resolve_expression_leaf( p, mem.engine_token, leaf, table );
            }
            else if ( fam.id == rl_family::pets )
            {
              binding = resolve_pets_leaf( p, mem.member, mem.engine_token, leaf, table );
            }
            else if ( fam.id == rl_family::items )
            {
              binding = resolve_items_leaf( p, mem.member, leaf, table );
            }
            else if ( fam.id == rl_family::sim_auras )
            {
              binding = resolve_sim_auras_leaf( mem.member, leaf );
            }
            else if ( fam.id == rl_family::raid_events )
            {
              // 220-05 Task 3 (RIG-05): "raid_event.<token>.<leaf>" --
              // member.engine_token is "adds"/"movement" (the census's own
              // engineToken override, chosen to avoid the genericity-token
              // collision "adds" has against source.split('.')[1] elsewhere
              // -- CONTEXT.md's own ruling). Constant-folded at creation time
              // under a single-shape fight (sim.cpp:3824-3840) -- correct,
              // per this task's own action text; the census must see these
              // move under HecticAddCleave/LightMovement instead.
              //
              // 221-07 WR-03: the "movement" member's remains/distance/up
              // leaves are NOT readable off the base raid_event_t's own
              // remains()/distance_max/up() -- movement_event_t
              // (raid_event.cpp:1136) tracks its own timing/distance in
              // SUBCLASS-LOCAL fields (move_distance/move_distance_max) that
              // the base class's generic filter expressions
              // (raid_event.cpp:2627-2691, what "raid_event.movement.*"
              // resolves through) never read, and movement_event_t never
              // sets the base class's `duration`, so the active-window
              // remains()/up() machinery never sees a nonzero window either
              // -- proven live: raid_move.in ticked 15->13.8->5.5->4.36->
              // 4.27s across a LightMovement corpus while raid_event.
              // movement.remains/distance/up read constant 0.0 the ENTIRE
              // fight, including while the player was actively mid-move
              // (confirmed against `movement.remains`/`movement.distance`,
              // player.cpp:12285-12306, which DID vary in the same window:
              // remains 1.40->1.30->1.20->1.10->1.00s, distance 10.44->
              // 9.70->8.95->8.21->7.47y). Redirected to that per-player
              // source instead, ONLY for these three leaves of the
              // "movement" member -- `in`/`cooldown`/etc are UNCHANGED
              // (those DO read correctly-populated base-class scheduling
              // fields; raid_adds.remains/up under HecticAddCleave prove the
              // generic path is fine for event types that DO set `duration`
              // -- this is a movement-specific gap, not a family-wide one).
              const bool is_movement_member = std::strcmp( mem.engine_token, "movement" ) == 0;
              if ( is_movement_member && std::strcmp( leaf.leaf, "remains" ) == 0 )
              {
                binding = resolve_expression_leaf( p, "movement.remains", leaf, table );
              }
              else if ( is_movement_member && std::strcmp( leaf.leaf, "distance" ) == 0 )
              {
                binding = resolve_expression_leaf( p, "movement.distance", leaf, table );
              }
              else if ( is_movement_member && std::strcmp( leaf.leaf, "up" ) == 0 )
              {
                // No player-scoped "movement.up" expression exists -- derive
                // it from the SAME `current.distance_to_move` field
                // "movement.remains"/"movement.distance" themselves read
                // (player.cpp:12285-12299): a player is "moving" exactly
                // when there is nonzero distance left to cover.
                auto up_expr = make_fn_expr( "movement_up", [ p ] {
                  return p->current.distance_to_move > 0.0 ? 1.0 : 0.0;
                } );
                binding = bind_expression_result( std::move( up_expr ), leaf, table );
              }
              else
              {
                binding = resolve_expression_leaf(
                  p, "raid_event." + std::string( mem.engine_token ) + "." + leaf.leaf, leaf, table );
              }
            }
            else
            {
              binding.kind = slot_binding_kind::unresolved;
              binding.leaf = &leaf;
            }
            break;
          case rl_family_kind::cooldown:
          {
            if ( !member_cooldown_scanned )
            {
              for ( cooldown_t* cd : p->cooldown_list )
              {
                if ( cd->name_str == mem.member )
                {
                  member_cooldown = cd;
                  break;
                }
              }
              member_cooldown_scanned = true;
            }
            binding = resolve_cooldown_leaf( p, member_cooldown, mem.member, leaf, table );
            break;
          }
          case rl_family_kind::direct:
            // 220-05 Task 2: stats/swing_cast/position -- see
            // resolve_stats_swing_cast_position_leaf's own comment.
            binding = resolve_stats_swing_cast_position_leaf( fam.id, mem.member, leaf );
            break;
          case rl_family_kind::action_expression:
            // 220-06 Task 3 -- see resolve_action_leaf's own comment.
            binding = resolve_action_leaf( p, mem.engine_token, leaf, table );
            break;
          case rl_family_kind::legality:
            // 260831-mk7 (D-1/D-2) -- see resolve_legality_leaf's own comment.
            binding = resolve_legality_leaf( mem.member, leaf );
            break;
          case rl_family_kind::proc_chance:
            // 260901-pb1 LEVER A (D-2): same class-agnostic seam as `deck`
            // just above -- mem.engine_token names a shaman_t::create_expression
            // forward (windfury_proc_chance_current / stormsurge_proc_chance_current
            // / maelstrom_weapon_proc_chance_current), resolved generically. A
            // distinct rl_family_kind (not folded into `expression`) so this
            // widen's own diff self-names, per the census family's own
            // $comment.
            binding = resolve_expression_leaf( p, mem.engine_token, leaf, table );
            break;
          case rl_family_kind::target_fact:
            // 228-09/228-11 (D-23/TGT-08) -- see resolve_target_fact_leaf's own comment.
            binding = resolve_target_fact_leaf( p, mem.engine_token, leaf, table );
            break;
          case rl_family_kind::shape_fact:
            // 228-11 (D-16, TGT-04) -- see resolve_shape_fact_leaf's own comment.
            binding = resolve_shape_fact_leaf( mem.engine_token, leaf );
            break;
          default:
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

void build_obs( const player_t* p, const rl_state_t& s, const slot_table& t,
                const std::uint8_t mask[ RL_ACTION_DIM ], float out_obs[ RL_OBS_DIM ] )
{
  // 228-11 (D-16, D-12 "read once, use twice"): the identity-free fight-wide aggregates and the
  // invulnerability window, computed AT MOST ONCE per build_obs() call -- lazily, so a decision
  // whose schema declares none of these thirteen scalars pays nothing. `const_cast` here mirrors
  // the ONE sanctioned call site this file already uses for the same const-vs-SimC's-own-
  // non-const-read-API reason (read_action_gate_bits's own comment, "Open Question 5").
  bool fw_agg_computed = false;
  rl_policy::fight_wide_aggregates_t fw_agg;
  auto get_fw_agg = [ & ]() -> const rl_policy::fight_wide_aggregates_t&
  {
    if ( !fw_agg_computed )
    {
      fw_agg = compute_fight_wide_aggregates( const_cast<player_t*>( p ) );
      fw_agg_computed = true;
    }
    return fw_agg;
  };
  bool fw_iw_computed = false;
  rl_policy::invulnerability_window_t fw_iw;
  auto get_fw_iw = [ & ]() -> const rl_policy::invulnerability_window_t&
  {
    if ( !fw_iw_computed )
    {
      fw_iw = compute_invulnerability_window( p->sim );
      fw_iw_computed = true;
    }
    return fw_iw;
  };

  // 220-06 Task 3 (OBS-07): the shared-snapshot cache for
  // hit_damage/crit_pct_current/persistent_multiplier/da_multiplier (4th
  // slot added 221-07 WR-01 -- see resolve_action_leaf's own comment for why
  // `multiplier` could not stay on the generic create_expression path),
  // keyed by the SAME `action_state_t*` slot_table already owns per action
  // -- computed at most ONCE per action per decision, local to this call (a
  // plain local unordered_map, never persisted across decisions: the state
  // pointer itself is stable across decisions, but the VALUES it derives
  // are not).
  std::unordered_map<action_state_t*, std::array<double, 4>> shared_action_leaf_cache;
  auto get_shared_action_leaves = [ & ]( action_t* a, action_state_t* state ) -> const std::array<double, 4>&
  {
    auto found = shared_action_leaf_cache.find( state );
    if ( found != shared_action_leaf_cache.end() )
      return found->second;

    // Mirrors persistent_multiplier_expr_t's own n_targets logic
    // (action.cpp:3466-3481) so composite_persistent_multiplier is correct
    // for cleave-shaped actions, THEN calls snapshot_state ONCE -- never
    // four times -- and derives all four leaves from that ONE state,
    // replicating each retired expression's own arithmetic exactly:
    // action.cpp:3226-3251 (hit_damage), :3514-3533 (crit_pct_current),
    // :3460-3484 (persistent_multiplier). `da_multiplier` calls the same
    // `composite_da_multiplier( state )` virtual `action_multiplier`-style
    // code elsewhere in this engine already relies on for "how hard is this
    // hitting right now" (e.g. crash_lightning_t's own TWW2 4pc override).
    // `state->result` is fixed to RESULT_HIT once here (never averaged with
    // crit) -- the same contract amount_expr_t's own hit_damage
    // construction uses (action.cpp:3212-3224, constructed with an explicit
    // RESULT_HIT, not RESULT_NONE, so average_crit stays false).
    state->target = a->target;
    int num_targets = a->aoe;   // 228-11 (Q19): a->aoe, never the retired virtual accessor -- IDENTICAL value
                                // for every action this schema declares (none overrides the
                                // virtual n_targets() { return aoe; } base, action.hpp:836-837),
                                // avoiding the retired P226-45 symbol entirely (P226_45_FALLBACK_GONE).
    if ( num_targets == -1 || num_targets > 1 )
    {
      // 260902/FORK-03 (Candidate 1, sealed) -- do NOT force a->target_list()
      // to resolve here, and do NOT invalidate the cache to force a future
      // resolve either. action_t::target_list() recomputes via
      // available_targets() + check_distance_targeting() whenever its cache
      // is invalid (action.cpp:1758-1769); for a shaman chain-bounce action
      // under distance_targeting_enabled=1, that recompute
      // (__check_distance_targeting, sc_shaman.cpp:1004-1067) draws from
      // sim->rng().range(...) to randomly search for the best bounce path --
      // measured: this is the actual random-stream perturbation this task's
      // tracer exists to close (an add-bearing fight's DPS mean moved from
      // 343514.6 to 349746.6 at the same seed with only a decision_dump=
      // line added). Read the target cache's CURRENT size only if it is
      // already valid (a real cast recently resolved it).
      //
      // 228-11 (Q19, D-A/R-D): the P226-45 min(live enemy count, this action's own n_targets()
      // cap) stopgap is DELETED (P226_45_FALLBACK_GONE) -- replaced by the SAME deterministic
      // per-target geometry the new target_fact/shape_fact leaves already compute (228-02/
      // 228-10), never a new computation. For a TARGETED multi-target action (chain_lightning,
      // tempest, voltaic_blaze) the count is the current pick's own neighbours_within_radius
      // (OBS-03, 232-02: the always-equal splash/jump pair is now one `a->radius`-parameterised
      // field); for the two SHAPED actions (crash_lightning, sundering) it is that action's own
      // shape fact's enemies_hit. Both come from lookup_pick()/build_enemy_fact() and
      // compute_crash_lightning_shape()/compute_sundering_shape() -- never select(), never
      // target_list() (RNG-free either way). An unrecognised multi-target action (none exist in
      // this registry today) or a targeted action with no stamped pick this decision falls back
      // to the live enemy count alone, honestly labelled here as the one remaining imperfect case
      // rather than silently guessed.
      const int live_enemies = s.has_active_enemies
                                    ? static_cast<int>( s.active_enemies )
                                    : static_cast<int>( p->sim->active_enemies );
      int geometry_count = live_enemies;
      const std::string& an = a->name_str;
      if ( an == "chain_lightning" || an == "tempest" || an == "voltaic_blaze" )
      {
        bool      pick_found = false;
        player_t* pick       = rl_target_select::lookup_pick( a, &pick_found );
        if ( pick_found && pick != nullptr )
        {
          geometry_count = rl_target_select::build_enemy_fact( a, pick ).neighbours_within_radius;
        }
      }
      else if ( an == "crash_lightning" )
      {
        geometry_count = compute_crash_lightning_shape( const_cast<player_t*>( p ) ).enemies_hit;
      }
      else if ( an == "sundering" )
      {
        geometry_count = compute_sundering_shape( const_cast<player_t*>( p ) ).enemies_hit;
      }
      const int max_targets = a->target_cache.is_valid
                                   ? static_cast<int>( a->target_cache.list.size() )
                                   : geometry_count;
      num_targets = ( num_targets < 0 ) ? max_targets : std::min( max_targets, num_targets );
    }
    state->n_targets = std::max( 1, num_targets );
    state->chain_target = 0;
    state->result = RESULT_HIT;

    a->snapshot_state( state, result_amount_type::NONE );

    double hit_damage = a->calculate_direct_amount( state );
    state->result_amount = hit_damage;
    if ( state->target != nullptr )
      state->target->target_mitigation( a->get_school(), result_amount_type::DMG_DIRECT, state );
    hit_damage = state->result_amount;

    const double crit_pct_current = std::min( 100.0, state->composite_crit_chance() * 100.0 );
    const double persistent_multiplier = a->composite_persistent_multiplier( state );
    const double da_multiplier = a->composite_da_multiplier( state );

    auto& entry = shared_action_leaf_cache[ state ];
    entry = { hit_damage, crit_pct_current, persistent_multiplier, da_multiplier };
    return entry;
  };

  for ( std::size_t slot = 0; slot < RL_OBS_DIM; ++slot )
  {
    const slot_binding& b = t.bindings[ slot ];
    const rl_leaf_desc& f = *b.leaf;

    double raw = 0.0;
    // 228-09 (Rule 1 auto-fix): explicit initializer -- every existing branch below already sets
    // this before it is read, but adding the new target_fact case (this plan) pushed GCC's
    // -Wmaybe-uninitialized past its confidence threshold on this giant switch. `absent` is the
    // header_contract default every genuinely-unresolved path already falls back to.
    lookup_status status = lookup_status::absent;

    if ( f.derived )
    {
      // Only fight_remains is derived in this schema: episode.maxTime - t,
      // floored at 0 (obs.py:236-243).
      //
      // 260902/227-RA: RL_EPISODE_MAX_TIME is a compile-time constant baked
      // for the single rlReady spec's declared 300s episode. The rig now
      // also emits 450s/600s route-template shapes (227-CONTEXT.md
      // TMPL-02); on those this leaf read "0s left" for the entire back
      // half of the fight. The sim's own runtime max_time is exact here --
      // the rig always emits fixed_time=1, vary_combat_length=0 combat, so
      // there is no per-iteration variance to average over (unlike
      // sim_t::expected_max_time(), which folds in vary_combat_length and
      // is deliberately NOT used). Falls back to the generated constant
      // only when max_time is not strictly positive (a mis-initialised
      // sim), so the constant keeps a reader and this leaf never subtracts
      // from zero.
      const double fight_len = p->sim->max_time.total_seconds() > 0.0
                                    ? p->sim->max_time.total_seconds()
                                    : RL_EPISODE_MAX_TIME;
      raw = std::max( fight_len - s.t, 0.0 );
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

            // 220-05 Task 2: stats -- always well-defined, never absent.
            case direct_id::stats_attack_haste:
              raw = p->cache.attack_haste();
              status = lookup_status::present;
              break;
            case direct_id::stats_attack_crit_chance:
              raw = p->cache.attack_crit_chance();
              status = lookup_status::present;
              break;
            case direct_id::stats_mastery_value:
              raw = p->cache.mastery_value();
              status = lookup_status::present;
              break;
            case direct_id::stats_damage_versatility:
              raw = p->cache.damage_versatility();
              status = lookup_status::present;
              break;
            case direct_id::stats_attack_power:
              raw = p->cache.attack_power();
              status = lookup_status::present;
              break;

            // 220-05 Task 2: swing_cast. gcd_remains/swing_mh_remains/
            // swing_oh_remains read the SAME rl_state_t POD read_state
            // already populates (has_* flags -> absent, exactly like
            // active_enemies above) -- never re-derived here. gcd_length and
            // auto_attack_interval mirror decision_dump.cpp's own
            // player-scoped formulas (this task's own read_first); both are
            // unconditionally well-defined (0.0 is a real "no main-hand
            // weapon" reading for auto_attack_interval, not an absence).
            case direct_id::swing_cast_gcd_remains:
              if ( s.has_gcd_remains )
              {
                raw = s.gcd_remains;
                status = lookup_status::present;
              }
              else
              {
                status = lookup_status::absent;
              }
              break;
            case direct_id::swing_cast_swing_mh_remains:
              if ( s.has_swing_mh_remains )
              {
                raw = s.swing_mh_remains;
                status = lookup_status::present;
              }
              else
              {
                status = lookup_status::absent;
              }
              break;
            case direct_id::swing_cast_swing_oh_remains:
              if ( s.has_swing_oh_remains )
              {
                raw = s.swing_oh_remains;
                status = lookup_status::present;
              }
              else
              {
                status = lookup_status::absent;
              }
              break;
            case direct_id::swing_cast_gcd_length:
              // FORK-04b (260902/226-07): reads the POD read_state() already
              // populated -- one reader, not re-derived here (see that
              // field's own comment in rl_policy.hpp).
              raw = s.gcd_length;
              status = lookup_status::present;
              break;
            case direct_id::swing_cast_auto_attack_interval:
              raw = s.auto_attack_interval;
              status = lookup_status::present;
              break;
            case direct_id::swing_cast_casting_remains:
              if ( p->executing && p->executing->execute_event )
              {
                raw = p->executing->execute_event->remains().total_seconds();
                status = lookup_status::present;
              }
              else if ( p->channeling && p->channeling->execute_event )
              {
                raw = p->channeling->execute_event->remains().total_seconds();
                status = lookup_status::present;
              }
              else
              {
                status = lookup_status::absent;   // not currently casting
              }
              break;

            // 220-05 Task 2: position. Always well-defined.
            case direct_id::position_current_distance:
              raw = p->current.distance;
              status = lookup_status::present;
              break;
            case direct_id::position_distance_to_move:
              raw = p->current.distance_to_move;
              status = lookup_status::present;
              break;
            case direct_id::position_movement_speed:
              raw = p->composite_movement_speed();
              status = lookup_status::present;
              break;
            case direct_id::position_x:
              raw = p->x_position;
              status = lookup_status::present;
              break;
            case direct_id::position_y:
              raw = p->y_position;
              status = lookup_status::present;
              break;

            // 220-05 Task 2: sim_auras.skyfury -- constructed unconditionally
            // (sim.cpp:2825), so ->check() is always a safe read.
            case direct_id::sim_aura_skyfury:
              raw = p->sim->auras.skyfury->check();
              status = lookup_status::present;
              break;

            // 220-05 Task 3 (RIG-05), REFACTORED 221-03 (ACT-05/ACT-06):
            // the soonest-to-fire raid event across the whole sim -- was an
            // inline walk of sim->raid_events + until_next() here; NOW
            // factored into the shared next_raid_event_in() helper
            // (rl_policy.hpp/this file, above) so this leaf, read_state()'s
            // own raid_event_next_in POD field, and accept_wait()'s clamp
            // (solver_control.cpp) share ONE walk, never three independently
            // -maintained ones that could drift (221-03 Task 2 point 1/3).
            // "Nothing pending" (the helper returns false) substitutes the
            // SAME value the fight_remains scalar computes above
            // (max(RL_EPISODE_MAX_TIME - s.t, 0)) -- THIS consumer's own
            // policy, deliberately different from read_state()'s POD field
            // (which leaves has_raid_event_next_in false / "no clamp" in
            // that case) -- see the helper's own doc comment for why the
            // two policies differ. Never the raw ~1e12 saturation sentinel
            // (Pitfall 7); never a second walk of sim->raid_events.
            case direct_id::raid_event_next_in:
            {
              double v = 0.0;
              if ( !next_raid_event_in( p->sim, v ) )
              {
                // 260902/227-RA: same runtime-length substitution as the
                // fight_remains derived branch above -- see that comment
                // for the full citation. The rig's fixed-length combat
                // makes p->sim->max_time exact here; RL_EPISODE_MAX_TIME
                // is used only as the defensive fallback.
                const double fight_len = p->sim->max_time.total_seconds() > 0.0
                                              ? p->sim->max_time.total_seconds()
                                              : RL_EPISODE_MAX_TIME;
                v = std::max( fight_len - s.t, 0.0 );
              }
              raw = v;
              status = lookup_status::present;
              break;
            }

            // 260831-0hh (BL-02): engine-emitted bloodlust countdown. Uses
            // the member function, not a raw sim->bloodlust_time -
            // current_time() -- calculate_time_to_bloodlust() correctly
            // suppresses an Exhaustion-locked bloodlust (player.cpp:
            // 12880-12886) and its "no future bloodlust" sentinel return
            // (3 * expected_iteration_time) is benign under this leaf's
            // clipDiv scaler (saturates to 1.0, needs no substitution,
            // unlike raid_event_next_in above).
            case direct_id::time_to_bloodlust:
              raw = p->calculate_time_to_bloodlust();
              status = lookup_status::present;
              break;

            // 228-11 (D-16, TGT-05): the identity-free fight-wide aggregates -- every value
            // already computed by 228-10's compute_fight_wide_aggregates(), cached once above.
            // The four `has_*`-gated fields (soonest/longest time to die, nearest distance) are
            // `absent` (their leaf's own `missing` default) in the degenerate no-enemies case,
            // matching every other optional POD field's own convention on this file.
            case direct_id::fw_enemies_total:
              raw = static_cast<double>( get_fw_agg().enemies_total );
              status = lookup_status::present;
              break;
            case direct_id::fw_enemies_in_melee:
              raw = static_cast<double>( get_fw_agg().enemies_in_melee );
              status = lookup_status::present;
              break;
            case direct_id::fw_enemies_within_8yd:
              raw = static_cast<double>( get_fw_agg().enemies_within_8yd );
              status = lookup_status::present;
              break;
            case direct_id::fw_enemies_within_40yd:
              raw = static_cast<double>( get_fw_agg().enemies_within_40yd );
              status = lookup_status::present;
              break;
            case direct_id::fw_enemies_in_front:
              raw = static_cast<double>( get_fw_agg().enemies_in_front );
              status = lookup_status::present;
              break;
            case direct_id::fw_flame_shock_carrier_count:
              raw = static_cast<double>( get_fw_agg().flame_shock_carrier_count );
              status = lookup_status::present;
              break;
            case direct_id::fw_soonest_time_to_die:
              if ( get_fw_agg().has_soonest_time_to_die )
              {
                raw = get_fw_agg().soonest_time_to_die;
                status = lookup_status::present;
              }
              else
              {
                status = lookup_status::absent;
              }
              break;
            case direct_id::fw_longest_time_to_die:
              if ( get_fw_agg().has_longest_time_to_die )
              {
                raw = get_fw_agg().longest_time_to_die;
                status = lookup_status::present;
              }
              else
              {
                status = lookup_status::absent;
              }
              break;
            case direct_id::fw_dying_within_5s:
              raw = static_cast<double>( get_fw_agg().dying_within_5s );
              status = lookup_status::present;
              break;
            case direct_id::fw_dying_within_15s:
              raw = static_cast<double>( get_fw_agg().dying_within_15s );
              status = lookup_status::present;
              break;
            case direct_id::fw_nearest_enemy_distance:
              if ( get_fw_agg().has_nearest_enemy_distance )
              {
                raw = get_fw_agg().nearest_enemy_distance;
                status = lookup_status::present;
              }
              else
              {
                status = lookup_status::absent;
              }
              break;

            // 228-11 (D-16, D-17, TGT-06, Q18): the two scripted-schedule immunity timers.
            // "Nothing pending"/"not active" encodes as 0.0 (never a saturation sentinel, never
            // null) -- matching every existing `*_remaining` field's own convention (228-10's own
            // "nothing pending" wire-value discipline).
            case direct_id::fw_immunity_in:
              raw = get_fw_iw().has_next ? get_fw_iw().next_in : 0.0;
              status = lookup_status::present;
              break;
            case direct_id::fw_immunity_remaining:
              // D-12 "read once, use twice": rl_state_t::immunity_remaining is the SAME value,
              // already filled by read_state() through this SAME compute_invulnerability_window()
              // function -- reused directly rather than recomputed.
              raw = s.has_immunity_remaining ? s.immunity_remaining : 0.0;
              status = lookup_status::present;
              break;

            default:
              status = lookup_status::absent;
              break;
          }
          break;
        }
        case slot_binding_kind::expression:
        {
          // 220-05 OBS-04: eval() is called ONCE per decision here -- the
          // expr_t itself was resolved once at bind time (resolve_expression_leaf).
          // A null b.expr (should not happen for a binding of this kind, but
          // guarded defensively) reads absent rather than dereferencing null.
          if ( b.expr == nullptr )
          {
            status = lookup_status::absent;
          }
          else
          {
            raw = b.expr->eval();
            status = lookup_status::present;
          }
          break;
        }
        case slot_binding_kind::cooldown:
        {
          // 220-05 Task 2: a direct cooldown_t* read, resolved once at bind
          // time (resolve_cooldown_leaf); no create_expression round-trip.
          if ( b.cooldown == nullptr )
          {
            status = lookup_status::absent;
            break;
          }
          switch ( b.cooldown_leaf )
          {
            case cooldown_leaf_kind::remains:
              raw = b.cooldown->remains().total_seconds();
              break;
            case cooldown_leaf_kind::charges:
              raw = static_cast<double>( b.cooldown->current_charge );
              break;
            case cooldown_leaf_kind::charges_fractional:
              raw = b.cooldown->charges_fractional();
              break;
            case cooldown_leaf_kind::recharge_time:
              raw = b.cooldown->recharge_event
                ? b.cooldown->recharge_event->remains().total_seconds() : 0.0;
              break;
            case cooldown_leaf_kind::max_charges:
              raw = static_cast<double>( b.cooldown->charges );
              break;
          }
          status = lookup_status::present;
          break;
        }
        // 228-11 (D-A/TGT-05, SLOT_PLUMBING_DELETED): the enemy_slot per-slot dispatch
        // (present/distance/time_to_die/health_pct/role + the per-effect dot/debuff leaves) was
        // REMOVED here along with the family it served -- see target_fact below for its replacement.
        case slot_binding_kind::action_expression:
        {
          switch ( b.action_leaf )
          {
            case action_leaf_kind::shared_hit_damage:
            case action_leaf_kind::shared_crit_pct_current:
            case action_leaf_kind::shared_persistent_multiplier:
            case action_leaf_kind::shared_da_multiplier:
            {
              if ( b.bound_action == nullptr || b.shared_action_state == nullptr )
              {
                status = lookup_status::absent;
                break;
              }
              const std::array<double, 4>& vals =
                  get_shared_action_leaves( b.bound_action, b.shared_action_state );
              raw = ( b.action_leaf == action_leaf_kind::shared_hit_damage ) ? vals[ 0 ]
                  : ( b.action_leaf == action_leaf_kind::shared_crit_pct_current ) ? vals[ 1 ]
                  : ( b.action_leaf == action_leaf_kind::shared_persistent_multiplier ) ? vals[ 2 ]
                  : vals[ 3 ];
              status = lookup_status::present;
              break;
            }
            // 228-11 (Q19, D-A/R-D, P226_45_FALLBACK_GONE): action_leaf_kind::spell_targets_count
            // and its P226-45 min(live enemy count, this action's own n_targets() cap) fallback
            // were REMOVED here -- action_leaves.chain_lightning.spell_targets is no longer a
            // declared leaf; target_fact.chain_lightning.neighbours_within_jump replaces it.
            case action_leaf_kind::dot_molten_weapon_ticking:
            case action_leaf_kind::dot_molten_weapon_remains:
            {
              // 220-08 WR-03: reuses the SAME per-target lazy handle cache
              // enemy_slots' effect leaves already use -- no per-decision
              // string comparison, and no new caching mechanism (get_enemy_
              // handle_cache validates target identity on every call, same
              // as every other consumer of this cache).
              dot_t* mw = ( p->target != nullptr ) ? get_enemy_handle_cache( p, p->target ).molten_weapon : nullptr;
              if ( mw == nullptr || !mw->is_ticking() )
              {
                status = lookup_status::absent;
              }
              else if ( b.action_leaf == action_leaf_kind::dot_molten_weapon_ticking )
              {
                raw = 1.0;
                status = lookup_status::present;
              }
              else
              {
                raw = mw->remains().total_seconds();
                status = lookup_status::present;
              }
              break;
            }
            default:
              status = lookup_status::absent;
              break;
          }
          break;
        }
        case slot_binding_kind::legality:
        {
          // 260831-mk7 (D-1/D-3): reads the CALLER's own already-computed
          // `mask` argument directly at this slot's own action index --
          // POST allow-list AND, POST the all-illegal refusal (the caller,
          // solver_control.cpp, computes and finalizes `mask` BEFORE
          // calling build_obs now -- see rl_policy.hpp's own updated
          // build_obs doc comment). Never a second legality computation:
          // this is exactly the same bits masked_argmax/the epsilon draw/
          // record_decision's own packed-mask column see.
          raw = mask[ b.legality_action_index ] != 0 ? 1.0 : 0.0;
          status = lookup_status::present;
          break;
        }
        case slot_binding_kind::target_fact:
        {
          // 228-09/228-11 (D-23/TGT-08): reads rl_target_select::lookup_pick() -- NEVER a fresh
          // select() call (D-12) -- for `b.bound_action`'s per-decision stamped pick, then
          // build_enemy_fact() for the specific fact this leaf names.
          if ( b.bound_action == nullptr )
          {
            status = lookup_status::absent;
            break;
          }
          bool      found = false;
          player_t* pick  = rl_target_select::lookup_pick( b.bound_action, &found );
          if ( b.target_fact_leaf == target_fact_leaf_kind::found )
          {
            raw = found ? 1.0 : 0.0;
            status = lookup_status::present;
            break;
          }
          if ( !found || pick == nullptr )
          {
            status = lookup_status::absent;
            break;
          }
          const rl_target_select::enemy_fact fact =
              rl_target_select::build_enemy_fact( b.bound_action, pick );
          bool matched = true;
          switch ( b.target_fact_leaf )
          {
            case target_fact_leaf_kind::distance:              raw = fact.distance; break;
            case target_fact_leaf_kind::in_front:               raw = fact.in_front ? 1.0 : 0.0; break;
            case target_fact_leaf_kind::in_reach:               raw = fact.in_reach ? 1.0 : 0.0; break;
            case target_fact_leaf_kind::alive:                  raw = fact.alive ? 1.0 : 0.0; break;
            case target_fact_leaf_kind::immune:                 raw = fact.immune ? 1.0 : 0.0; break;
            case target_fact_leaf_kind::time_to_die:            raw = fact.time_to_die; break;
            case target_fact_leaf_kind::health_pct:             raw = fact.health_pct; break;
            case target_fact_leaf_kind::is_boss:                raw = fact.is_boss ? 1.0 : 0.0; break;
            case target_fact_leaf_kind::flame_shock_remaining:  raw = fact.flame_shock_remaining; break;
            case target_fact_leaf_kind::is_current_target:      raw = fact.is_current_target ? 1.0 : 0.0; break;
            case target_fact_leaf_kind::actor_index:            raw = static_cast<double>( fact.actor_index ); break;
            case target_fact_leaf_kind::actor_spawn_index:      raw = static_cast<double>( fact.actor_spawn_index ); break;
            // 228-11 (D-16 full inventory): every value below already exists on enemy_fact
            // (228-02/228-10) -- no new computation, same read-through-build_enemy_fact() path
            // every leaf above already uses.
            case target_fact_leaf_kind::in_range:                            raw = fact.in_range ? 1.0 : 0.0; break;
            case target_fact_leaf_kind::immunity_remaining:                  raw = fact.immunity_remaining; break;
            case target_fact_leaf_kind::burning_core_remaining:              raw = fact.burning_core_remaining; break;
            case target_fact_leaf_kind::lightning_rod_stacks:                raw = static_cast<double>( fact.lightning_rod_stacks ); break;
            case target_fact_leaf_kind::lightning_rod_remaining:             raw = fact.lightning_rod_remaining; break;
            case target_fact_leaf_kind::venomfang_remaining:                 raw = fact.venomfang_remaining; break;
            case target_fact_leaf_kind::venomfang_debuff_stacks:             raw = static_cast<double>( fact.venomfang_debuff_stacks ); break;
            case target_fact_leaf_kind::venomfang_debuff_remaining:          raw = fact.venomfang_debuff_remaining; break;
            case target_fact_leaf_kind::rune_of_unleashed_fire_lingering_remaining: raw = fact.rune_of_unleashed_fire_lingering_remaining; break;
            case target_fact_leaf_kind::neighbours_within_radius:            raw = static_cast<double>( fact.neighbours_within_radius ); break;
            default:
              matched = false;
              break;
          }
          status = matched ? lookup_status::present : lookup_status::absent;
          break;
        }
        case slot_binding_kind::shape_fact:
        {
          // 228-11 (D-16, TGT-04): reads 228-10's own compute_crash_lightning_shape()/
          // compute_sundering_shape() -- the CURRENT facing only, no per-decision pick (the
          // shaped actions never appear in targeted_picks, OR-2). `p` is only ever read here
          // (const-safe: neither compute function mutates the player), so no const_cast is
          // needed at this call site.
          const rl_policy::shape_hit_result_t shape =
              ( b.shape_fact_action == shape_fact_action_kind::crash_lightning )
                  ? compute_crash_lightning_shape( const_cast<player_t*>( p ) )
                  : compute_sundering_shape( const_cast<player_t*>( p ) );
          switch ( b.shape_fact_leaf )
          {
            case shape_fact_leaf_kind::enemies_hit:            raw = static_cast<double>( shape.enemies_hit ); break;
            case shape_fact_leaf_kind::summed_remaining_life:  raw = shape.summed_remaining_life; break;
            case shape_fact_leaf_kind::long_lived_count:       raw = static_cast<double>( shape.long_lived_count ); break;
          }
          status = lookup_status::present;
          break;
        }
        case slot_binding_kind::unresolved:
        default:
          // An unresolved binding yields `absent` (header_contract), never a
          // special value.
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
    else if ( !f.derived && b.kind == slot_binding_kind::direct && b.direct == direct_id::t &&
              f.kind == rl_kind::k_seconds && f.has_clip_div )
    {
      // 260902/227-RA: the clock leaf ('t') normalises by the simulation's
      // own runtime fight length, not by the constant `clip_div` baked
      // into the generated descriptor table (still 300.0, unchanged --
      // the spec's declared divisor is deliberately left alone so
      // `gen_obs_schema.py --check` stays green; only the VALUE computed
      // here moves). The rig's fixed-length combat makes max_time exact.
      // Falls back to the descriptor's own clip_div when max_time is not
      // strictly positive, matching the fallback used at the other two
      // 227-RA sites above.
      //
      // `!f.derived` is REQUIRED here, not decorative: resolve_scalar_leaf
      // (above) deliberately binds the derived `fight_remains` leaf's own
      // slot_binding to `direct_id::t` too (its own comment: "a defined
      // value ... so a future reader never has to wonder" -- the wondering
      // happens exactly here). `fight_remains` also has kind=k_seconds and
      // has_clip_div=true (clipDiv 60), so without this guard this branch
      // would silently hijack fight_remains' encoding as well, re-scaling
      // an already-correct site-1 value by max_time/60 instead of the
      // schema's own 60s clip. Caught by a same-seed old-vs-new binary dump
      // diff on the 300s fixture during this task's own verification
      // (227-LENGTH-RECEIPT.md).
      const double fight_len = p->sim->max_time.total_seconds() > 0.0
                                    ? p->sim->max_time.total_seconds()
                                    : f.clip_div;
      const double clipped = std::max( std::min( raw, fight_len ), 0.0 );
      encoded = clipped / fight_len;
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
//
// 260831-lg6 (D-1): build_mask's own R3/R4/R5 call sites (the only callers
// in this file) were retired -- engine truth is authoritative there now,
// so this function has no live caller left. Kept defined, never deleted
// (same "comment, not silent deletion" discipline as build_mask's own
// retired blocks), `[[maybe_unused]]` to keep the build warning-clean
// rather than pretending a caller still exists.
// ---------------------------------------------------------------------------

namespace
{
[[maybe_unused]] bool cd_ready_now( const cooldown_reading& row )
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
// read_anchored_wait -- 221-03 (ACT-05/ACT-06). ONE pure anchor reader,
// called from BOTH build_mask()'s wait-legality rule below AND
// build_wait()'s per-anchor duration below -- mirrors mask.py's own
// anchored_wait_seconds() reading rules exactly, including the
// charge-based recharge-clock preference. Two computations of the same
// quantity is the drift class this helper exists to prevent -- do not
// inline the reading logic in either caller. Returns the RAW value only
// (unclamped, unfloored); each caller applies its OWN clamp/floor policy
// on top (build_mask compares raw directly to the floor and to
// fight_remains for legality; build_wait clamps+floors it into a
// wait_result).
// ---------------------------------------------------------------------------

namespace
{
struct anchored_wait_reading
{
  double      raw     = 0.0;
  bool        has_raw = false;
  std::string source;
};

anchored_wait_reading read_anchored_wait( const rl_state_t& s, const rl_wait_anchor& anchor )
{
  anchored_wait_reading out;
  switch ( anchor.kind )
  {
    case rl_wait_anchor_kind::cooldown:
    {
      out.source = std::string( "cooldown:" ) + ( anchor.cooldown_row ? anchor.cooldown_row : "" );
      const cooldown_reading* row = s.find_cooldown( anchor.cooldown_row );
      if ( row != nullptr )
      {
        // Charge-based (max_charges > 1): prefer the RECHARGE clock.
        // Single-charge: the REMAINING clock. Same two clocks cd_ready_now
        // above already reads.
        const bool charge_based = row->max_charges > 1;
        const bool has_val = charge_based ? row->has_recharge_time : row->has_remains;
        const double val = charge_based ? row->recharge_time : row->remains;
        if ( has_val && val > 0.0 )
        {
          out.raw = val;
          out.has_raw = true;
        }
      }
      break;
    }
    case rl_wait_anchor_kind::swing:
    {
      const bool mh = ( anchor.hand == rl_swing_hand::mh );
      out.source = mh ? "swing_mh" : "swing_oh";
      const bool has_val = mh ? s.has_swing_mh_remains : s.has_swing_oh_remains;
      const double val = mh ? s.swing_mh_remains : s.swing_oh_remains;
      if ( has_val && val > 0.0 )
      {
        out.raw = val;
        out.has_raw = true;
      }
      break;
    }
    case rl_wait_anchor_kind::maelstrom:
    {
      // No engine "next Maelstrom stack" event -- DEFINED as the earlier
      // of the next main-hand/off-hand swing (221-RESEARCH.md Assumptions
      // Log A7; the registered spec's own $anchorComment on this anchor
      // carries the full citation).
      out.source = "maelstrom";
      if ( s.has_swing_mh_remains && s.swing_mh_remains > 0.0 )
      {
        out.raw = s.swing_mh_remains;
        out.has_raw = true;
      }
      if ( s.has_swing_oh_remains && s.swing_oh_remains > 0.0 &&
           ( !out.has_raw || s.swing_oh_remains < out.raw ) )
      {
        out.raw = s.swing_oh_remains;
        out.has_raw = true;
      }
      break;
    }
    case rl_wait_anchor_kind::gcd:
    {
      out.source = "gcd";
      if ( s.has_gcd_remains && s.gcd_remains > 0.0 )
      {
        out.raw = s.gcd_remains;
        out.has_raw = true;
      }
      break;
    }
    case rl_wait_anchor_kind::none:
    default:
      break;
  }
  return out;
}

// The fight-remaining AND raid-event-next-in clamp, shared by
// build_wait()'s duration computation below (mirrors mask.py's
// anchored_wait_seconds() clamp exactly -- both bounds applied, in the
// same order, before any floor). build_mask()'s wait-legality rule above
// does NOT use this helper -- it compares the RAW value symmetrically
// against both bounds (WR-01, 221-08 code review: "does the anchor's raw
// duration fit before whichever bound comes first", NOT "is the clamped
// value still above the floor" -- the latter silently admits a raw value
// clamped down to a bound well above the floor, which is a real
// divergence from build_wait()'s own duration for the SAME wire state).
double clamp_anchored_wait_pre_floor( const rl_state_t& s, double raw )
{
  double seconds_pre_floor = raw;
  if ( s.has_fight_remains && s.fight_remains < seconds_pre_floor )
    seconds_pre_floor = s.fight_remains;
  if ( s.has_raid_event_next_in && s.raid_event_next_in < seconds_pre_floor )
    seconds_pre_floor = s.raid_event_next_in;
  return seconds_pre_floor;
}
} // anonymous namespace

// ---------------------------------------------------------------------------
// build_mask -- mirrors mask.py:legal() exactly (260831-lg6, D-1): R1's
// wait rule, then for kind=="cast" R0's engine truth is AUTHORITATIVE
// (the POD's arrays are always filled, so R2/R3/R4/R5's old AND
// composition is retired dead code, kept as comments below, never a
// live branch).
// ---------------------------------------------------------------------------

void build_mask( const rl_state_t& s, std::uint8_t out_mask[ RL_ACTION_DIM ] )
{
  for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
  {
    const rl_action_desc& a = RL_ACTIONS[ i ];

    // R1: a wait action is legal ONLY at a foreground boundary. A `wait`
    // reply at any other boundary hard-aborts the engine
    // (protocol_abort) -- the mask is the only thing between the agent
    // and that FATAL. Wait actions have no action_t* to resolve and skip
    // R0's engine-truth AND below entirely. 221-03 (ACT-05/ACT-06): an
    // ANCHORED wait (a.wait_anchor.kind != none) ADDITIONALLY requires the
    // anchor's RAW seconds (read_anchored_wait, never the floored/clamped
    // value) to be present, strictly above RL_WAIT_FLOOR_SECONDS, before
    // fight end, AND (WR-01 fix, 221-08 code review) before the next raid
    // event. The invariant is symmetric across both bounds: the anchor's
    // RAW duration must fit entirely before WHICHEVER of fight-end or the
    // next raid event comes first -- mirrors mask.py's legal() exactly,
    // including why the CLAMPED value is never compared to the floor
    // directly (a raw value clamped down to a fight_remains/raid_event_
    // next_in well above the floor would then read legal, when the
    // anchor's raw duration does not actually fit before that bound; an
    // earlier draft of this fix made exactly that mistake). Absent
    // fight_remains/raid_event_next_in is a documented fail-open (waiting
    // past the end of a fight or a raid event is harmless). The
    // null-anchor (fixed) wait keeps the bare foreground rule below,
    // unchanged.
    if ( a.kind == rl_action_kind::wait )
    {
      if ( !s.boundary_is_foreground )
      {
        out_mask[ i ] = 0;
        continue;
      }
      if ( a.wait_anchor.kind == rl_wait_anchor_kind::none )
      {
        out_mask[ i ] = 1;
        continue;
      }
      const anchored_wait_reading reading = read_anchored_wait( s, a.wait_anchor );
      bool legal_wait = reading.has_raw && reading.raw > RL_WAIT_FLOOR_SECONDS;
      if ( legal_wait && s.has_fight_remains && !( reading.raw < s.fight_remains ) )
        legal_wait = false;
      if ( legal_wait && s.has_raid_event_next_in && !( reading.raw < s.raid_event_next_in ) )
        legal_wait = false;
      out_mask[ i ] = legal_wait ? 1 : 0;
      continue;
    }

    // OBS-05/R-W (232-03): a turn action is legal ONLY at a foreground boundary (same rule as
    // wait -- a turn reply at any other boundary is equally out of place) AND iff
    // `s.any_enemy_behind_player` (read ONCE in read_state(), the SAME raw in-front geometry
    // `mask.py`'s Python mirror computes from `all_enemies`/`player_position`). Mirrors mask.py's
    // legal() exactly: no engine action_ready/action_resolvable bit is consulted here -- a turn
    // has no action_t, so R0's AND below is never reached for this kind.
    if ( a.kind == rl_action_kind::turn )
    {
      out_mask[ i ] = ( s.boundary_is_foreground && s.any_enemy_behind_player ) ? 1 : 0;
      continue;
    }

    // R0: engine truth is AUTHORITATIVE (260831-lg6, D-1 -- RECOMPOSED from
    // the prior 221-01 AND). `s.action_resolvable`/`s.action_ready` were
    // computed ONCE in read_state() via read_action_gate_bits(), through
    // the SAME resolver (solver_control::resolve_action) that
    // accept_cast() uses before its own not-ready FATAL. `build_mask`
    // stays PURE over the rl_state_t POD (no player_t* parameter,
    // unchanged) -- these bits are READ from the POD, never computed here.
    //
    // Under the OLD (221-01) composition, R2/R3/R4/R5 below were ANDed on
    // top of this bit even when it was already true. 260831-lg6 measured
    // (R-1..R-4 in that quick task's PLAN.md) that `action_ready[i]` IS
    // `accept_cast`'s own predicate through the SAME resolver, so it is
    // already necessary and sufficient for FATAL-safety; every declarative
    // rule below is either a coarser restatement of a `ready()` override
    // this bit already executes, or reads an object a proc-driven bypass
    // has already moved the action away from (a cooldown-POINTER swap the
    // declarative `cooldowns` dump never even emits). ANDing a narrower-
    // or-wrong declarative rule on top of an already-correct engine
    // verdict can only ever make the mask MORE illegal than the engine
    // actually is, never less -- so when this bit is true, it is now THE
    // WHOLE VERDICT: `out_mask[i] = 1` immediately, R2/R3/R4/R5 NOT
    // evaluated at all for this action.
    //
    // Unlike mask.py's Python side, there is NO absent-bits fallback here:
    // `s.action_resolvable`/`s.action_ready` are POD MEMBERS, always
    // filled by read_state() before build_mask() ever runs (never null,
    // never absent) -- so the declarative fallback Python keeps for "no
    // engine truth on this wire" (old dumps, the golden-encode fixture,
    // probes/blindfold.py) has NO C++ counterpart BY CONSTRUCTION. The
    // R2/R3/R4/R5 blocks below are kept as COMMENTS, never silently
    // deleted, each naming the engine `ready()` predicate that subsumes it
    // -- a reader can see WHAT dead code this recomposition retired and
    // WHY, without re-deriving it from the git history.
    if ( !s.action_resolvable[ i ] || !s.action_ready[ i ] )
    {
      out_mask[ i ] = 0;
      continue;
    }

    // R2 (RETIRED, subsumed by R0 above): buff gate (mask.py's
    // maskRules.buffGates) used to be ANDed here, fail-closed. Every
    // registered buffGates entry is a transcription of a shaman
    // `ready()` override that `a->ready()` -- and therefore R0's
    // `action_ready` bit -- already calls virtually: stormstrike_t,
    // windstrike_t, sundering_t, primordial_storm_t, lightning_bolt_t,
    // tempest_t (all six in `~/code/simc/engine/class_modules/
    // sc_shaman.cpp`). R0 is strictly MORE precise than this declarative
    // buff-presence-only check for at least one of the six (windstrike_t's
    // real predicate additionally refuses in the LAST FRACTION of an
    // Ascendance window, `remains() <= queue_delay()`, which a bare
    // buff-presence gate cannot express at all) -- see mask.py's own
    // docstring, R-3, for the full receipt trail.

    // R3/R4 (RETIRED, subsumed by R0 above): the action's own
    // `cooldown_row` readiness (`action_t::ready()`'s
    // `if ( !cooldown->is_ready() ) return false;`, `action.cpp:2634` at
    // the pin this recomposition landed against) used to be checked here
    // via `s.find_cooldown(a.cooldown_row)` + `cd_ready_now(...)`. This is
    // the rule R-2 measured as WRONG for a proc-driven cooldown-pointer
    // swap: the action's `cooldown` member can be reassigned to a bypass
    // row the declarative dump never even emits (the row is created with
    // no duration, so decision_dump.cpp's `duration <= 0` skip drops it
    // entirely) -- `a->ready()` dereferences whichever object the pointer
    // ACTUALLY names, which R0's `action_ready` bit already reflects
    // through the same resolver `accept_cast` uses.

    // R5 (RETIRED, subsumed by R0 above): shared cooldown row AND (221-01,
    // Pattern 1) -- `cooldown_row_shared` readiness used to be ANDed on
    // top of the own-row rule. `use_item_t::ready()`
    // (`~/code/simc/engine/player/player.cpp`, `bool ready() override`
    // near line 10251) already returns false when
    // `cooldown_group->remains() > 0` before falling through to
    // `action_t::ready()` -- R0's bit already executes this through the
    // same resolver.

    // Deliberately NOT evaluated here: RL_TALENT_GATES[] (221-01,
    // ACT-01/ACT-02). Talent legality is DECLARATIVE data for the
    // fingerprint and for humans -- it is EXECUTED by R0's
    // `action_resolvable` bit above (an untalented action's action_t is
    // `background`, so `action_resolvable` is already 0 for it, R-4: the
    // engine already resolved the talent tree, Python/C++ never
    // re-implement it). Do not "fix" this apparent omission by adding a
    // second talent check here.
    out_mask[ i ] = 1;
  }
}

// ---------------------------------------------------------------------------
// build_wait -- REAL. anchor.kind == none mirrors mask.py's
// next_event_wait_detail() exactly, BYTE FOR BYTE -- same candidate set,
// same tie-break, the same FORK-04b re-ask-period cap (260902/226-07:
// caps the winning candidate at max(gcd_length, auto_attack_interval)
// before the floor), same floor (RESEARCH Pitfall 7). An anchored kind (221-03,
// ACT-05/ACT-06) instead calls the SAME read_anchored_wait() pure reader
// build_mask()'s wait-legality rule calls above, then clamps to the two
// known bounds and floors -- never a second, independently-maintained
// reading of the anchor's own value (the drift class this helper exists to
// prevent).
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

wait_result build_wait( const rl_state_t& s, const rl_wait_anchor& anchor )
{
  if ( anchor.kind != rl_wait_anchor_kind::none )
  {
    // 221-03 (ACT-05/ACT-06): clamp to fight-remaining and the shared
    // next-raid-event bound WHEN THOSE ARE KNOWN on this rl_state_t
    // (s.has_fight_remains / s.has_raid_event_next_in), then floor. The
    // AUTHORITATIVE clamp is accept_wait() (solver_control.cpp, the ONE
    // point both transports converge) -- this clamp exists only so the
    // in-process arm's own computed seconds already matches what
    // accept_wait would apply anyway for a value computed from this SAME
    // rl_state_t, never as a substitute for accept_wait's own live-state
    // clamp.
    const anchored_wait_reading reading = read_anchored_wait( s, anchor );
    if ( !reading.has_raw )
      return wait_result{ RL_WAIT_FLOOR_SECONDS, reading.source, true };

    // WR-01 (221-08 code review): shares clamp_anchored_wait_pre_floor()
    // with build_mask()'s wait-legality rule above -- ONE clamp
    // computation, never two independently-maintained copies.
    const double seconds_pre_floor = clamp_anchored_wait_pre_floor( s, reading.raw );

    const bool floored = seconds_pre_floor < RL_WAIT_FLOOR_SECONDS;
    const double seconds = std::max( seconds_pre_floor, RL_WAIT_FLOOR_SECONDS );
    return wait_result{ seconds, reading.source, floored };
  }

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

  // 228-10 Task 1 Step 6(a) (Q18, owner ruling 2026-09-02 alternative (b)): the
  // immunity-remaining candidate -- the SAME value read_state() already computed via
  // compute_invulnerability_window() (D-12: read once, use twice; never a second walk of
  // sim->raid_events here). Appended after the cooldown-row loop, before swing_mh -- the SAME
  // relative position scripts/rl/mask.py's next_event_wait_detail() appends its own mirror
  // candidate, so a tie between this and a cooldown-row/swing/gcd candidate breaks identically
  // on both sides (P-3's append-order-is-tie-break rule). The 226-07 re-ask-period cap is
  // scoped to `winner.is_cooldown` only (below) and never applies to this literal-source
  // candidate -- an immunity window ending is exactly the kind of external event the cap exists
  // to let the wait reach, not something to clamp down to one re-ask period.
  if ( s.has_immunity_remaining && s.immunity_remaining > 0.0 )
    candidates.push_back( { s.immunity_remaining, false, nullptr, "immunity_remaining" } );

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

  double seconds_pre_floor = winner.seconds;

  // FORK-04b (260902/226-07) -- the re-ask-period cap. Named mechanism:
  // when the swing clocks are absent (target immune/moving/out of reach)
  // and every rotational cooldown reads 0, the winner above can be a
  // long NON-rotational cooldown (a potion, a racial) -- up to 233 s on
  // the measured seed 31337 t=392.000 decision (226-STALL-RECEIPT.md
  // §13). An unanchored wait's emitted seconds must never exceed ONE
  // re-ask period = max(gcd_length, auto_attack_interval), read from the
  // SAME rl_state_t fields the swing_cast_gcd_length/
  // swing_cast_auto_attack_interval obs leaves read (never a third,
  // independently-derived source). Mirrors mask.py's
  // next_event_wait_detail() byte-for-byte -- same cap, same "reask_cap"
  // source label, applied here BEFORE the floor exactly as the Python
  // side applies it before its own floor. The eight event-anchored
  // waits (anchor.kind != none, above), accept_wait()'s own fight-end/
  // raid-event clamps, the floor and the tie-break are ALL untouched by
  // this cap -- it lives only in this anchor.kind == none branch.
  // Scoped to COOLDOWN-sourced winners only (winner.is_cooldown) --
  // measured deviation from a literal "cap any winner" reading (plan
  // checker, byte-identity Patchwerk run): a swing_mh/gcd-sourced winner
  // is ALREADY bounded by auto_attack_interval/gcd_length by construction
  // (swing_mh_remains counts down from a scheduled event whose full
  // period IS auto_attack_interval; gcd_remains counts down from
  // gcd_length), so capping it serves no purpose -- and doing so anyway
  // made the byte-identity control FAIL: `execute_event->remains()`
  // (swing_mh_remains, an absolute-time subtraction) and a FRESH
  // `swing_time * auto_attack_speed()` recompute (auto_attack_interval)
  // are two independently-rounded floating-point paths that are not
  // always bit-equal even when representing the same instant, so an
  // uncapped-by-construction swing/gcd winner could spuriously read a
  // few microseconds above the cap and get needlessly relabelled. The
  // cap's entire justification (226-STALL-RECEIPT.md section 13) is the
  // NON-rotational-cooldown winner case (swing clocks absent, a potion/
  // racial cooldown wins) -- restricting to winner.is_cooldown captures
  // exactly that case and nothing else, with no new epsilon constant.
  if ( winner.is_cooldown )
  {
    bool has_cap = false;
    double cap = 0.0;
    if ( s.has_gcd_length && s.gcd_length > 0.0 )
    {
      cap = s.gcd_length;
      has_cap = true;
    }
    if ( s.has_auto_attack_interval && s.auto_attack_interval > 0.0 )
    {
      if ( !has_cap || s.auto_attack_interval > cap )
        cap = s.auto_attack_interval;
      has_cap = true;
    }
    if ( has_cap && seconds_pre_floor > cap )
    {
      seconds_pre_floor = cap;
      source = "reask_cap";
    }
  }

  const bool floored = seconds_pre_floor < RL_WAIT_FLOOR_SECONDS;
  const double seconds = std::max( seconds_pre_floor, RL_WAIT_FLOOR_SECONDS );
  return wait_result{ seconds, source, floored };
}
} // namespace rl_policy
