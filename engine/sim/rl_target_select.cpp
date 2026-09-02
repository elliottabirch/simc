// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "sim/rl_target_select.hpp"

#include "action/action.hpp"
#include "action/dot.hpp"
#include "buff/buff.hpp"
#include "player/player.hpp"
#include "sim/sim.hpp"

#include <cassert>
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

// D-14's re-resolution ladder counters -- one fight's totals, never reset mid-run.
reresolution_counts g_reresolution_counts;

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

  // G4 -- in front, gated on sim->facing_enabled exactly as target_ready's own clause (R-E:
  // harmful && range >= 0 only, so a self/friendly/trinket action is never facing-gated).
  if ( a->sim->facing_enabled && harmful && a->range >= 0 &&
       !a->player->is_in_front( *candidate, 0.0 ) )
    return false;

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

  f.time_to_die = candidate->time_to_percent( 0 ).total_seconds();
  f.health_pct  = candidate->health_percentage();
  f.is_boss     = candidate->is_boss();

  // Flame Shock on an ARBITRARY enemy, not only the current target (P-5's fix point) -- generic
  // player_t::get_dot API, no shaman-specific type needed.
  dot_t* fs = candidate->get_dot( "flame_shock", a->player );
  f.flame_shock_remaining = fs ? fs->remains().total_seconds() : 0.0;

  // R-D: deterministic geometry, never a target-cache read -- count of alive enemies within THIS
  // action's own radius of the candidate. Chain Lightning's one declared radius (10.0) serves
  // both fields (see rl_target_select.hpp's neighbours_within_splash comment); a shaped spell's
  // true splash geometry is plan 228-03's, not this plan's.
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

  return f;
}

player_t* select( action_t* a, bool harmful, preference_fn pref )
{
  std::vector<player_t*> candidates;
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

  // (1) sticky -- keep the action's own previous pick if it still passes generic_filter. Re-
  // checked EVERY decision (P-4): never assume the engine's own acquire_target invalidated it.
  player_t* previous = a->target;
  if ( previous )
    for ( player_t* c : candidates )
      if ( c == previous )
        return previous;

  // (2) preference, highest score wins; (3) tie-break on the player's CURRENT target (p->target,
  // which may differ from the action's own previous pick a->target above); (4) final tie-break on
  // the stable (actor_index, actor_spawn_index) identity pair, ascending -- a total order, so a
  // run is reproducible (TGT-02 edge: ordering).
  player_t* current_target = a->player->target;
  player_t* best           = nullptr;
  double    best_score     = 0.0;
  for ( player_t* c : candidates )
  {
    enemy_fact fact  = build_enemy_fact( a, c, previous );
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
      bool c_is_current    = ( c == current_target );
      bool best_is_current = ( best == current_target );
      if ( c_is_current != best_is_current )
        better = c_is_current;
      else
        better = std::tie( c->actor_index, c->actor_spawn_index ) <
                 std::tie( best->actor_index, best->actor_spawn_index );
    }

    if ( better )
    {
      best       = c;
      best_score = score;
    }
  }
  return best;
}

std::uint64_t begin_decision( const player_t* p )
{
  // Same WR-12 single-sim/single-thread precondition rl_policy_obs.cpp's action-handle cache
  // asserts -- this table is keyed on a bare const player_t* with no sim identity and no clear.
  assert( p->sim->threads == 1 && p->sim->profileset_map.empty() &&
          "rl_target_select's decision stamp is single-sim/single-thread by construction (228-02, "
          "mirrors 221-01's action-handle cache)" );
  return ++g_decision_stamp[ p ];
}

void fill_pick( action_t* resolved, bool harmful, preference_fn pref )
{
  if ( !resolved )
    return;
  std::uint64_t stamp = g_decision_stamp[ resolved->player ];
  player_t*     pick  = select( resolved, harmful, pref );
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

bool is_targeted_action( const action_t* resolved )
{
  if ( !resolved )
    return false;
  for ( const char* token : TARGETED_TOKENS )
    if ( resolved->name_str == token )
      return true;
  return false;
}

preference_fn preference_for( const action_t* resolved )
{
  if ( !resolved )
    return nullptr;
  const std::string& n = resolved->name_str;
  if ( n == "stormstrike" || n == "windstrike" || n == "primordial_storm" || n == "lightning_bolt" )
    return preference_longest_time_to_die;
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

double preference_longest_time_to_die( const action_t* a, const enemy_fact& fact )
{
  // D-15: "outlives the cast" -- a candidate that will still be alive after this cast lands
  // always outranks one that will not, so an about-to-despawn add never wins over one that will
  // actually survive to be hit. If NO candidate outlives the cast (every one would be excluded),
  // this still returns a valid, ordered answer via the plain time_to_die term below -- never
  // "invalid" (228-02 ledger row: D-10's own text spells this fallback out explicitly only for
  // Voltaic Blaze; the same reasoning is applied here since a preference must never return null).
  double outlives_margin = fact.time_to_die - a->execute_time().total_seconds();
  return ( outlives_margin > 0.0 ) ? ( 1.0e9 + fact.time_to_die ) : fact.time_to_die;
}

double preference_lava_lash( const action_t*, const enemy_fact& fact )
{
  // Among Flame Shock carriers in reach, the SHORTEST remaining wins (offset above every
  // non-carrier so carriers are always preferred); else the longest-lived. Copies the STRUCTURE
  // of shortest_duration_target() (sc_shaman.cpp:6877-6911).
  if ( fact.flame_shock_remaining > 0.0 )
    return 1.0e9 - fact.flame_shock_remaining;
  return fact.time_to_die;
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

} // namespace rl_target_select
