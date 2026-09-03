// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "sim/rl_target_select.hpp"

#include "action/action.hpp"
#include "action/attack.hpp"
#include "action/dot.hpp"
#include "buff/buff.hpp"
#include "player/player.hpp"
#include "sim/sim.hpp"

#include <cassert>
#include <cmath>
#include <cstring>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace rl_target_select
{
namespace
{

// The eight targeted registry tokens this plan governs (228-02-PLAN.md's TARGETED constant,
// re-derived from scripts/rl/specs/enhancement.json's specs.enhancement.actions[] at planning
// time). SHAPED (crash_lightning, sundering) is plan 228-03's, deliberately absent here.
constexpr const char* TARGETED_TOKENS[] = {
  "stormstrike", "lightning_bolt", "chain_lightning", "tempest",
  "windstrike",  "lava_lash",      "voltaic_blaze",   "primordial_storm",
};

// Per-player monotonic decision counter (228-02's own stamp -- see rl_target_select.hpp's header
// comment for why neither the handle cache's key nor solver_control's `seq` can serve this role).
std::unordered_map<const player_t*, std::uint64_t> g_decision_stamp;

// The per-decision pick table, keyed on the resolved action_t* -- the SAME pointer
// read_action_gate_bits's handle cache resolves and accept_cast later receives
// (solver_control.cpp's own comment: the two must resolve to the SAME action_t* by construction),
// so no id-based lookup is needed on the accept_cast side.
struct pick_slot
{
  player_t*     pick      = nullptr;
  std::uint64_t stamp     = 0;
  bool          has_stamp = false;
};
std::unordered_map<const action_t*, pick_slot> g_pick_table;

// D-14's re-resolution ladder counters -- one fight's totals (WR-11, 260902/cr4: cleared every
// iteration by reset( sim ) below, called from sim_t::reset()).
reresolution_counts g_reresolution_counts;

// CR-04 (260902/cr4): decisions where every one of the RL-controlled actor's registry actions
// read not-ready -- the deadlock census. Same one-fight-totals lifetime as the counters above.
std::uint64_t g_every_targeted_action_illegal = 0;

// WR-05 (260902/cr4): a file-static candidate buffer reused across every select() call, cleared
// (not reallocated) per call -- same single-thread precondition begin_decision's own assert
// states (a second concurrent select() on this buffer would race). Avoids a fresh heap allocation
// on every targeted action's every decision.
std::vector<player_t*> g_candidate_buffer;

} // anonymous namespace

bool generic_filter( const action_t* a, player_t* candidate, bool harmful )
{
  // G1 -- alive.
  if ( candidate->is_sleeping() )
    return false;

  // G2 -- not immune while harmful.
  if ( harmful && candidate->debuffs.invulnerable && candidate->debuffs.invulnerable->check() )
    return false;

  // G3 -- within THIS action's own range, read per candidate's own combat_reach (P-7: 4.0 for a
  // boss, 1.0 for an add -- never one shared number). Skipped identically to target_ready's own
  // reach clause when distance targeting is off or the action has no positive range.
  if ( a->sim->distance_targeting_enabled && a->range > 0 &&
       a->player->get_player_distance( *candidate ) > a->range + candidate->combat_reach )
    return false;

  // G4 REMOVED (260902/cr4, CR-04 RULING (a)): a behind candidate is no longer excluded from the
  // selector's own candidate set. facing_enabled's meaning changed from "a behind target is
  // refused" to "the player turns to face what it casts at" -- the turn (accept_cast/retarget(),
  // and the scripted arm's own cast-commit site) makes a selected-behind candidate legal BY THE
  // TIME it is actually cast on, so excluding it here would just deadlock the selector on a
  // candidate set that no longer needs excluding. "In front" survives only as the RAW,
  // non-gating `enemy_fact::in_front` field (observation ground truth) and inside the two SHAPED
  // actions' own cone/rectangle geometry (unaffected by this change -- OR-2 keeps them from ever
  // turning).

  return true;
}

enemy_fact build_enemy_fact( const action_t* a, player_t* candidate, player_t* previous_pick )
{
  enemy_fact f;
  f.candidate    = candidate;
  f.distance     = a->player->get_player_distance( *candidate );
  f.alive        = !candidate->is_sleeping();
  f.immune       = candidate->debuffs.invulnerable && candidate->debuffs.invulnerable->check();
  f.immunity_remaining =
      candidate->debuffs.invulnerable ? candidate->debuffs.invulnerable->remains().total_seconds() : 0.0;

  bool reach_ok = !( a->sim->distance_targeting_enabled && a->range > 0 &&
                      f.distance > a->range + candidate->combat_reach );
  f.in_reach = reach_ok;
  f.in_range = reach_ok;

  // RAW geometry, never gated on sim->facing_enabled -- generic_filter applies that gate
  // separately (D-16: the observation writer needs ground truth, not the gated legality bit).
  f.in_front = a->player->is_in_front( *candidate, 0.0 );

  // WR-10 (260902/cr4): clipped at 600.0s AT THE SOURCE -- ties preference_shortest_time_to_die's
  // 1.0e9 and preference_lava_lash's 2.0e9 dominance offsets to a provable bound rather than one
  // conditional on fixed_time=1 (under fixed_time=1 no candidate's raw time_to_percent(0) exceeds
  // max_time anyway, but an unclipped value is a theoretical hazard the offsets should not have to
  // assume away). Byte identity on a 600s shape confirms this changes nothing today.
  f.time_to_die = std::min( candidate->time_to_percent( 0 ).total_seconds(), 600.0 );
  f.health_pct  = candidate->health_percentage();
  f.is_boss     = candidate->is_boss();

  // WR-04 (260902/cr4): `find_dot` -- a non-allocating scan of the candidate's existing dot_list --
  // instead of `get_dot`, which CREATES a dot_t on every candidate that has never been Flame
  // Shocked, growing `dot_list` on what this function's own callers treat as a read-only query.
  // Flame Shock on an ARBITRARY enemy, not only the current target (P-5's fix point).
  dot_t* fs = candidate->find_dot( "flame_shock", a->player );
  f.flame_shock_remaining = fs ? fs->remains().total_seconds() : 0.0;

  // R-D: deterministic geometry, never a target-cache read -- count of alive enemies within THIS
  // action's OWN resolved radius of the candidate (a->radius: 10.0 for chain_lightning, 8.0 for
  // tempest -- WHICHEVER action called build_enemy_fact -- never a hardcoded chain_lightning
  // constant, WR-06 260902/cr4 correcting this comment's prior claim that one fixed 10.0 "serves
  // both" fields regardless of caller). Both `neighbours_within_splash` and `neighbours_within_jump`
  // are set from the SAME `neighbours` count on purpose -- they are equal by construction today
  // because no action currently needs the splash count and the jump count to differ, not because
  // the two concepts are the same; a future action that DOES need them to differ would compute two
  // separate counts here and stop assigning one to both. Both field NAMES are kept (228-11-PLAN.md
  // declares both as schema leaves) -- only this comment moves. A shaped spell's true splash
  // geometry is plan 228-03's, not this plan's.
  int neighbours = 0;
  if ( a->radius > 0.0 )
  {
    for ( player_t* other : a->sim->target_non_sleeping_list )
    {
      if ( other == candidate || !other->is_enemy() )
        continue;
      if ( candidate->get_player_distance( *other ) <= a->radius + other->combat_reach )
        ++neighbours;
    }
  }
  f.neighbours_within_splash = neighbours;
  f.neighbours_within_jump   = neighbours;

  f.is_current_target = ( candidate == a->player->target );
  f.is_previous_pick  = ( candidate == previous_pick );
  f.actor_index        = candidate->actor_index;
  f.actor_spawn_index  = candidate->actor_spawn_index;

  // 228-10 Task 1 Step 1 (D-03/D-16): the missing per-enemy facts, added ONCE here and read from
  // here by the dump (and later the observation) -- never a second copy. Every lookup uses the
  // non-allocating `find`/`find_dot` idiom (WR-04's own convention above): buff_t::find() scans
  // buff_list without creating; player_t::find_dot() scans dot_list without creating. `source`
  // is always `a->player` -- these are debuffs THIS actor puts on the candidate, mirroring
  // flame_shock_remaining's own `a->player` source above.
  if ( buff_t* bc = buff_t::find( candidate, "burning_core", a->player ) )
    f.burning_core_remaining = bc->remains().total_seconds();
  if ( buff_t* lr = buff_t::find( candidate, "lightning_rod", a->player ) )
  {
    f.lightning_rod_stacks    = lr->check();
    f.lightning_rod_remaining = lr->remains().total_seconds();
  }
  if ( dot_t* vf = candidate->find_dot( "venomfang", a->player ) )
    f.venomfang_remaining = vf->remains().total_seconds();
  if ( buff_t* vfd = buff_t::find( candidate, "venomfang_debuff", a->player ) )
  {
    f.venomfang_debuff_stacks    = vfd->check();
    f.venomfang_debuff_remaining = vfd->remains().total_seconds();
  }
  if ( dot_t* ruf = candidate->find_dot( "rune_of_unleashed_fire_lingering", a->player ) )
    f.rune_of_unleashed_fire_lingering_remaining = ruf->remains().total_seconds();

  return f;
}

// 228-07 (TGT-07, D-21): the FULL candidate set for one targeted action, as complete enemy_fact
// records -- reuses generic_filter/build_enemy_fact VERBATIM (the exact functions select() itself
// calls below), so this can never enumerate a different candidate set than the one the preference
// actually scores. `previous_pick` mirrors select()'s own `a->target` read (P-4: re-checked every
// call, never cached).
std::vector<enemy_fact> build_candidate_facts( const action_t* a, bool harmful )
{
  std::vector<enemy_fact> out;
  player_t* previous = a->target;
  for ( player_t* t : a->sim->target_non_sleeping_list )
    if ( t->is_enemy() && generic_filter( a, t, harmful ) )
      out.push_back( build_enemy_fact( a, t, previous ) );
  return out;
}



namespace
{
// WR-05 (260902/cr4): the cheap half of select()'s scoring-loop fact build -- see that call
// site's own comment. NOT exposed in the header: build_enemy_fact() (above) is the one and only
// public fact-record builder every OTHER caller (the future observation writer, 228-04 onward)
// uses, with every field filled exactly as before this task.
enemy_fact build_enemy_fact_for_scoring( const action_t* a, player_t* candidate, player_t* previous_pick,
                                          preference_fn pref )
{
  enemy_fact f;
  f.candidate   = candidate;
  f.time_to_die = std::min( candidate->time_to_percent( 0 ).total_seconds(), 600.0 );  // WR-10

  if ( pref == preference_lava_lash || pref == preference_voltaic_blaze )
  {
    dot_t* fs = candidate->find_dot( "flame_shock", a->player );  // WR-04
    f.flame_shock_remaining = fs ? fs->remains().total_seconds() : 0.0;
  }

  if ( a->radius > 0.0 )
  {
    int neighbours = 0;
    for ( player_t* other : a->sim->target_non_sleeping_list )
    {
      if ( other == candidate || !other->is_enemy() )
        continue;
      if ( candidate->get_player_distance( *other ) <= a->radius + other->combat_reach )
        ++neighbours;
    }
    f.neighbours_within_splash = neighbours;
    f.neighbours_within_jump   = neighbours;
  }

  f.is_current_target = ( candidate == a->player->target );
  f.is_previous_pick  = ( candidate == previous_pick );
  f.actor_index       = candidate->actor_index;
  f.actor_spawn_index = candidate->actor_spawn_index;
  return f;
}
} // anonymous namespace

player_t* select( action_t* a, bool harmful, preference_fn pref )
{
  // WR-05 (260902/cr4): reuse the file-static buffer instead of allocating a fresh vector every
  // call -- cleared, not reallocated (capacity survives across calls after the first).
  std::vector<player_t*>& candidates = g_candidate_buffer;
  candidates.clear();
  candidates.reserve( a->sim->target_non_sleeping_list.size() );
  for ( player_t* t : a->sim->target_non_sleeping_list )
    if ( t->is_enemy() && generic_filter( a, t, harmful ) )
      candidates.push_back( t );

  // TGT-02 edge: empty -- no pick, legality bit goes to 0, the unanchored wait stays legal.
  if ( candidates.empty() )
    return nullptr;

  // TGT-02 edge: single -- that candidate is the pick regardless of preference; the sticky clause
  // cannot override it (there is nothing else to be sticky about). The general algorithm below
  // would reach the identical answer without this shortcut, but stating it explicitly matches the
  // must-have's own wording and keeps the edge case visible in code, not just in behaviour.
  if ( candidates.size() == 1 )
    return candidates.front();

  // CR-03 (260902/cr4, RULING (a) -- PREFERENCE FIRST): the sticky early-return that used to
  // live here is GONE. `previous` (the action's own prior pick, still needed for build_enemy_fact
  // /the tie-break below) is still read, but no longer decides the pick on its own -- it is now
  // ONLY the FIRST tie-break among candidates the preference scores EQUALLY. (2) preference,
  // highest score wins; (3) among EQUAL scores, the action's own previous pick (`a->target`,
  // re-checked every decision -- P-4, unchanged from the sticky clause's own discipline); (4)
  // among still-equal, the player's CURRENT target (p->target, which may differ from the action's
  // own previous pick); (5) final tie-break on the stable (actor_index, actor_spawn_index)
  // identity pair, ascending -- a total order, so a run is reproducible (TGT-02 edge: ordering).
  player_t* previous = a->target;
  player_t* current_target = a->player->target;
  player_t* best           = nullptr;
  double    best_score     = 0.0;
  for ( player_t* c : candidates )
  {
    // WR-05 (260902/cr4): the SCORING loop drives exactly one of the eight preference functions --
    // none of them read distance/in_reach/in_range/in_front/alive/immune/immunity_remaining/
    // health_pct/is_boss (generic_filter already excluded anything those would have rejected), so
    // this lite build skips them and skips the Flame Shock find_dot() lookup unless the dispatched
    // preference is one of the two that read it. build_enemy_fact() itself is UNCHANGED -- it is
    // what external readers (the observation writer, 228-04 onward) call, with every field filled.
    enemy_fact fact  = build_enemy_fact_for_scoring( a, c, previous, pref );
    double     score = pref( a, fact );

    if ( !best )
    {
      best       = c;
      best_score = score;
      continue;
    }

    bool better;
    if ( score != best_score )
    {
      better = score > best_score;
    }
    else
    {
      // CR-03 (260902/cr4): sticky is now a TIE-BREAK, checked first among equal scores.
      bool c_is_prev    = ( c == previous );
      bool best_is_prev = ( best == previous );
      if ( c_is_prev != best_is_prev )
      {
        better = c_is_prev;
      }
      else
      {
        bool c_is_current    = ( c == current_target );
        bool best_is_current = ( best == current_target );
        if ( c_is_current != best_is_current )
          better = c_is_current;
        else
          better = std::tie( c->actor_index, c->actor_spawn_index ) <
                   std::tie( best->actor_index, best->actor_spawn_index );
      }
    }

    if ( better )
    {
      best       = c;
      best_score = score;
    }
  }
  return best;
}

namespace
{
// WR-11 (260902/cr4): the single-sim/single-thread precondition this whole module is built on,
// factored into one predicate -- mirrors decision_dump.cpp:~820's own fix for the sibling
// g_action_handle_cache. `threads != 1 || !profileset_map.empty()` now DEGRADES the three module
// globals (no picks filled, no stamps bumped, lookup always reports not-found) instead of merely
// asserting -- an assert compiles out under NDEBUG, silently leaving an unlocked, unkeyed-by-sim
// std::unordered_map to race. The `assert` below is KEPT beside the runtime check as a debug-build
// tripwire (it still fires first in a debug build, before the degrade path is ever reached).
bool multi_sim_or_multi_thread( const sim_t* sim )
{
  return sim->threads != 1 || !sim->profileset_map.empty();
}
} // anonymous namespace

std::uint64_t begin_decision( const player_t* p )
{
  assert( p->sim->threads == 1 && p->sim->profileset_map.empty() &&
          "rl_target_select's decision stamp is single-sim/single-thread by construction (228-02, "
          "mirrors 221-01's action-handle cache)" );
  if ( multi_sim_or_multi_thread( p->sim ) )
    return 0;  // WR-11 degrade: no stamp bump, caller must not fill or trust any pick this "decision".
  return ++g_decision_stamp[ p ];
}

std::uint64_t current_decision_stamp( const player_t* p )
{
  auto it = g_decision_stamp.find( p );
  return it == g_decision_stamp.end() ? 0 : it->second;
}

void fill_pick( action_t* resolved, bool harmful, preference_fn pref )
{
  if ( !resolved )
    return;
  if ( multi_sim_or_multi_thread( resolved->player->sim ) )
    return;  // WR-11 degrade: never fill a pick under threads>1/profileset -- lookup_pick below
             // then reports not-found for it, the same documented fail-open decision_dump.cpp's
             // own action_resolvable/action_ready arrays already use for this precondition.
  std::uint64_t stamp = g_decision_stamp[ resolved->player ];
  // CR-02 (260902/cr4): idempotent WITHIN a decision -- a second fill_pick call for the SAME
  // action at the SAME stamp must never trigger a second select() call (the design invariant this
  // whole module exists to hold): return immediately when the table already carries this
  // decision's stamp for this action.
  auto existing = g_pick_table.find( resolved );
  if ( existing != g_pick_table.end() && existing->second.has_stamp && existing->second.stamp == stamp )
    return;
  player_t* pick = select( resolved, harmful, pref );
  g_pick_table[ resolved ] = pick_slot{ pick, stamp, true };
}

player_t* lookup_pick( const action_t* resolved, bool* out_found )
{
  auto it = g_pick_table.find( resolved );
  if ( it == g_pick_table.end() || !it->second.has_stamp )
  {
    if ( out_found )
      *out_found = false;
    return nullptr;
  }
  auto stamp_it = g_decision_stamp.find( resolved->player );
  std::uint64_t current_stamp = ( stamp_it == g_decision_stamp.end() ) ? 0 : stamp_it->second;
  if ( it->second.stamp != current_stamp )
  {
    // Tampering / staleness guard (T-228-02-02): the stamped pick belongs to an EARLIER decision.
    // Refuse by name at the caller rather than falling back to a fresh select() here -- that
    // fallback is exactly the three-copies failure D-12 exists to prevent.
    if ( out_found )
      *out_found = false;
    return nullptr;
  }
  if ( out_found )
    *out_found = true;
  return it->second.pick;
}

void reset( sim_t* )
{
  // WR-11 (260902/cr4): called from sim_t::reset() (sim.cpp), once per iteration -- clears every
  // module global (the original three, plus CR-04's deadlock counter added later the same task)
  // so a stale pick, stamp or count from a PRIOR iteration can never leak into the next one.
  // `sim` itself is unused (the tables are keyed on `player_t*`, not
  // sim identity, per this module's own single-sim/single-thread precondition) but is taken by
  // pointer to mirror raid_event_t::reset( sim )'s own signature at the call site.
  g_decision_stamp.clear();
  g_pick_table.clear();
  g_reresolution_counts = reresolution_counts{};
  g_every_targeted_action_illegal = 0;  // CR-04 (260902/cr4)
}

bool is_targeted_action( const action_t* resolved )
{
  if ( !resolved )
    return false;
  for ( const char* token : TARGETED_TOKENS )
    if ( resolved->name_str == token )
      return true;
  return false;
}

// 228-09 (D-23/TGT-08, dump half) -- see rl_target_select.hpp's own doc comment.
std::size_t targeted_action_token_count()
{
  return sizeof( TARGETED_TOKENS ) / sizeof( TARGETED_TOKENS[ 0 ] );
}

const char* const* targeted_action_tokens()
{
  return TARGETED_TOKENS;
}

preference_fn preference_for( const action_t* resolved )
{
  if ( !resolved )
    return nullptr;
  const std::string& n = resolved->name_str;
  if ( n == "stormstrike" || n == "windstrike" || n == "primordial_storm" || n == "lightning_bolt" )
    return preference_shortest_time_to_die;
  if ( n == "lava_lash" )
    return preference_lava_lash;
  if ( n == "voltaic_blaze" )
    return preference_voltaic_blaze;
  if ( n == "chain_lightning" )
    return preference_chain_lightning;
  if ( n == "tempest" )
    return preference_tempest;
  return nullptr;
}

double preference_shortest_time_to_die( const action_t* a, const enemy_fact& fact )
{
  // OR-1 (owner ruling 2026-09-02, QUESTIONS Q15 alternative (b), 228-04 Task 1 Step 0c):
  // supersedes ledger P228-7 (R-B, "keep D-10 as written") and P228-24. Owner's reasoning
  // verbatim: adds die first, so the single-target spells should finish them rather than parking
  // on the boss. D-15's "outlives the cast" dominance clause is UNCHANGED -- a candidate that
  // will still be alive after this cast lands always outranks one that will not, so an
  // about-to-despawn add never wins over one that will actually survive to be hit. What inverted
  // is the ordering WITHIN each group: a SHORTER time to die now scores higher (subtracted from
  // the dominance offset rather than added to it), so among several valid outliving-the-cast
  // candidates this preference now parks on the shortest-lived one -- exactly the opposite of
  // this function's pre-OR-1 name and behaviour (renamed from the pre-228-04 longest-lived-preferring function of the same shape).
  // R-B's measured consequence INVERTS too: under the rig's fixed_time=1, a boss's time_to_die is
  // the whole fight clock with no per-actor term (research 3.3) -- since 600.0 (D-16's clip) is
  // far larger than any real add's remaining life, the boss's dominance-offset score
  // (1.0e9 - time_to_die) is now always LOWER than any add's, so this preference parks on the
  // shortest-lived valid add and falls back to the boss only when no add passes generic_filter.
  // If NO candidate outlives the cast (every one would be excluded), this still returns a valid,
  // ordered answer via the plain time_to_die term below -- never "invalid" (228-02 ledger row:
  // D-10's own text spells this fallback out explicitly only for Voltaic Blaze; the same
  // reasoning is applied here since a preference must never return null). The dominance offset
  // (1.0e9) stays far larger than any time-to-die the D-16 clip allows (600.0 s) after inversion,
  // so an outliving candidate can never score below a non-outliving one.
  // IN-04 (260902/cr4): stated explicitly, since the two branches order in OPPOSITE directions --
  // the OUTLIVING group (candidates that survive past this cast) orders SHORTEST-time-to-die
  // first (1.0e9 - time_to_die: subtracting less scores higher for a smaller time_to_die); the
  // NON-outliving group (candidates that will not survive to be hit) orders LONGEST-time-to-die
  // first (the bare time_to_die term: a larger raw value scores higher), i.e. closest to actually
  // surviving the cast. Behaviour is UNCHANGED by this comment -- see ledger row for this task.
  double outlives_margin = fact.time_to_die - a->execute_time().total_seconds();
  return ( outlives_margin > 0.0 ) ? ( 1.0e9 - fact.time_to_die ) : fact.time_to_die;
}

double preference_lava_lash( const action_t* a, const enemy_fact& fact )
{
  // OR-1 (owner ruling 2026-09-02, 228-04 Task 1 Step 0c(b)): the carrier clause is UNCHANGED --
  // among Flame Shock carriers in reach the shortest remaining wins (offset above every
  // non-carrier so carriers are always preferred), copying the STRUCTURE of
  // shortest_duration_target() (sc_shaman.cpp:6877-6911). The non-carrier fallback now CALLS the
  // flipped base preference (`preference_shortest_time_to_die`) rather than re-implementing the
  // ordering (D-03: no third copy of any rule) -- but the base's own dominance offset (1.0e9)
  // means its outlives-the-cast branch can itself approach 1.0e9 (as time_to_die -> 0), so the
  // carrier branch's offset is raised to 2.0e9 (a full 1.0e9 above the base's ceiling) to keep
  // the two ranges provably disjoint: carrier ranges over roughly
  // [2.0e9 - flame_shock_duration, 2.0e9], the base's return value never reaches 1.0e9, so
  // carrier always outranks non-carrier regardless of how short flame_shock_remaining or
  // time_to_die get (re-checked, not assumed -- both ranges are written out in
  // 228-SCHEMA-RECEIPT.md).
  if ( fact.flame_shock_remaining > 0.0 )
    return 2.0e9 - fact.flame_shock_remaining;
  return preference_shortest_time_to_die( a, fact );
}

double preference_voltaic_blaze( const action_t*, const enemy_fact& fact )
{
  // Without Flame Shock outranks with it (offset), ordered by longest-lived within each group --
  // if every candidate carries Flame Shock this still returns the longest-lived carrier, never
  // "invalid" (the same reason the always-legal wait exists).
  return ( fact.flame_shock_remaining <= 0.0 ? 1.0e9 : 0.0 ) + fact.time_to_die;
}

double preference_chain_lightning( const action_t*, const enemy_fact& fact )
{
  // Most neighbours within its own resolved radius (the jump distance, R-A), tie-broken on time
  // to die. The neighbour count is a DETERMINISTIC approximation of the engine's randomised chain
  // walk (sc_shaman.cpp:982-1057) -- labelled as an approximation, never a prediction of it (R-D).
  return static_cast<double>( fact.neighbours_within_jump ) * 1.0e6 + fact.time_to_die;
}

double preference_tempest( const action_t*, const enemy_fact& fact )
{
  // Same formula as chain_lightning's, applied to Tempest's own resolved radius (measured 8.0) --
  // the addon's 8-yard cluster-centre rule.
  return static_cast<double>( fact.neighbours_within_splash ) * 1.0e6 + fact.time_to_die;
}

// ---------------------------------------------------------------------------------------------
// Shaped spells (228-03). Pure geometry -- plain doubles, no engine pointer beyond what the
// candidate loop below needs to read positions and combat_reach.
// ---------------------------------------------------------------------------------------------

bool crash_lightning_cone_contains( double px, double py, double fx, double fy, double cx, double cy,
                                     double bounding_allowance )
{
  double dx   = cx - px;
  double dy   = cy - py;
  double dist = std::sqrt( dx * dx + dy * dy );
  if ( dist - bounding_allowance > CRASH_LIGHTNING_CONE_RADIUS_YARDS )
    return false;
  if ( dist <= 0.0 )
    return true;  // coincident with the player: no meaningful "behind" for a zero-length vector.
  double dot = ( fx * dx + fy * dy ) / dist;
  return dot >= CRASH_LIGHTNING_CONE_COS_HALF_ANGLE;
}

bool sundering_rect_contains( double px, double py, double fx, double fy, double cx, double cy,
                               double bounding_allowance )
{
  // Facing-axis (fx,fy) and perpendicular (rotate facing 90 degrees: (-fy,fx)) projections. WR-02
  // (260902/cr4): allowance applied ALONG only, dropped from PERP -- convention stated once in
  // rl_target_select.hpp beside the shape constants.
  double dx = cx - px;
  double dy = cy - py;
  double along = dx * fx + dy * fy;
  double perp  = std::fabs( dx * ( -fy ) + dy * fx );
  return along >= -bounding_allowance && along <= SUNDERING_RECT_LENGTH_YARDS + bounding_allowance &&
         perp <= SUNDERING_RECT_HALF_WIDTH_YARDS;
}

// OR-2 (owner ruling 2026-09-02, QUESTIONS Q1): Crash Lightning and Sundering get NO selector
// and never turn the player. Wave 3 (clone a613171c41) wired `preference_shaped_crash_lightning`,
// `preference_shaped_sundering` and the `shaped_score` helper into `select()` under a superseded
// turn-toward-shape default (228-CONTEXT.md D-10's shaped row / ledger section 0 R2-1) -- all
// three are REMOVED here (228-04 Task 1 Step 0b). The geometry predicates immediately below
// (`crash_lightning_cone_contains`, `sundering_rect_contains`) are KEPT as the ONE shared copy:
// `sc_shaman.cpp`'s AoE hit filters and this plan's own descriptive shape facts (228-04 Task 2)
// both read them from the CURRENT facing, never a hypothetical one.

void retarget( action_t* a, player_t* p, player_t* pick )
{
  // WR-07 (260902/cr4): action_t::set_target -- NEVER a raw `a->target = pick` write (it skips the
  // AoE target-cache invalidation, leaving a stale cache and a silently wrong hit set).
  a->set_target( pick );
  p->target = pick;
  if ( p->main_hand_attack )
    p->main_hand_attack->set_target( pick );
  if ( p->off_hand_attack )
    p->off_hand_attack->set_target( pick );
  // CR-04 (260902/cr4, RULING (a)): the turn folds into this shared function too -- both callers
  // (accept_cast, the mid-cast re-resolution ladder's fallback arm) are retargeting an RL-actor's
  // OWN registry action (never a SHAPED one -- crash_lightning/sundering are never in the
  // registry `is_targeted_action` governs, so they never reach `retarget()` at all; OR-2 is
  // structurally preserved here with no extra exclusion needed). Gated on `facing_enabled` so the
  // option keeps a real meaning when off.
  if ( p->sim->facing_enabled )
    p->face( *pick );
}

void record_reresolution( reresolution_arm arm )
{
  switch ( arm )
  {
    case reresolution_arm::kept_the_pick:              ++g_reresolution_counts.kept_the_pick; break;
    case reresolution_arm::fell_back_to_player_target:  ++g_reresolution_counts.fell_back_to_player_target; break;
    case reresolution_arm::left_no_op_boundary:         ++g_reresolution_counts.left_no_op_boundary; break;
  }
}

reresolution_counts get_reresolution_counts()
{
  return g_reresolution_counts;
}

void record_every_targeted_action_illegal()
{
  ++g_every_targeted_action_illegal;
}

std::uint64_t get_every_targeted_action_illegal_count()
{
  return g_every_targeted_action_illegal;
}

} // namespace rl_target_select
