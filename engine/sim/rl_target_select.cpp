// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "sim/rl_target_select.hpp"

#include "action/action.hpp"
#include "action/attack.hpp"
#include "action/dot.hpp"
#include "buff/buff.hpp"
#include "fmt/format.h"
#include "player/player.hpp"
#include "sim/rl_policy.hpp"
#include "sim/rl_translog.hpp"
#include "sim/shaman_rl_facts.hpp"
#include "sim/sim.hpp"
#include "util/util.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
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
//
// 230-02 (CK1-1, owner ruling Q1): this is also the scorer's own one-hot registry --
// preference_scorer (below) walks this SAME array for its aiming-spell one-hot, so the shaped
// pair can never reach the scorer either. Settled once, not a run-time discovery: Crash Lightning
// and Sundering cast in the current facing direction only, have no selector and never turn, so
// they have no pick to score and no column to occupy.
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

// 230-04 (SCOR-02, R-B): the per-decision candidate block table, keyed on the resolved
// action_t* exactly like g_pick_table above -- select() fills it ONLY when the scorer preference
// is active (candidate_block::features "the facts the scorer looked at", R-B's own wording).
// `features` is reused across calls (never reallocated once sized), same WR-05 discipline as
// g_candidate_buffer below.
struct candidate_block_slot
{
  std::vector<float> features;
  std::uint16_t       mask        = 0;
  std::uint8_t         count       = 0;
  std::uint8_t         chosen_slot = rl_translog::CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK;
  std::uint64_t         stamp       = 0;
  bool                   has_stamp   = false;
};
std::unordered_map<const action_t*, candidate_block_slot> g_candidate_block_table;

// 232-04 (OBS-02, R-T): the pre-cast snapshot table -- captures, per targeted action's CURRENT
// decision, the is_current_target / Tempest hit_damage values `build_obs()`'s own REAL-decision
// call (solver_control.cpp, `rl_state_t::is_decision_boundary == true`) computed BEFORE
// `accept_cast`'s retarget/turn can mutate `p->target`/facing. `decision_dump.cpp`'s own later
// diagnostic `build_obs()` call for the SAME decision (`is_decision_boundary == false`, that
// file's own CR-02 comment: `record()` always runs strictly after `choose()`) reads this snapshot
// back instead of recomputing against state the cast already mutated -- the
// `action_gate_dump_time_compute` precedent, applied to per-target facts. Keyed on the resolved
// `action_t*` exactly like `g_pick_table`/`g_candidate_block_table` above, same `has_stamp`/
// `stamp` read-back discipline (T-232-15). `has_is_current_target`/`has_hit_damage` are
// independent -- most targeted actions only ever get the first (232-12, ME-07: every registry
// action declaring a `hit_damage` leaf -- eight today, `action_leaves.{chain_lightning,
// crash_lightning, lava_lash, lightning_bolt, stormstrike, tempest, voltaic_blaze,
// windstrike}.hit_damage` -- gets `has_hit_damage` too, not only Tempest).
struct target_fact_snapshot_slot
{
  bool          has_is_current_target = false;
  bool          is_current_target     = false;
  bool          has_hit_damage        = false;
  double        hit_damage            = 0.0;
  std::uint64_t stamp                 = 0;
  bool          has_stamp             = false;
};
std::unordered_map<const action_t*, target_fact_snapshot_slot> g_target_fact_snapshot_table;

// RULE-02 (232-06, R-Z): the per-decision Chain Lightning hop-count stash. `select()` fills this
// ONCE per decision, before the scoring loop calls `preference_chain_lightning` once per candidate
// (preference_fn is one-candidate-at-a-time by design -- the geometry must be precomputed and
// stashed here for that function to read). Keyed on the resolved action_t* like g_pick_table /
// g_candidate_block_table / g_target_fact_snapshot_table above; `used_fallback` is reset to false
// every time this action's slot is (re)computed and set true the instant
// `preference_chain_lightning` cannot find an entry for the candidate it was asked to score -- read
// back by `chain_hop_fallback_used()` so a fallback is visible on the dump row, never silent.
struct chain_hop_slot
{
  std::unordered_map<const player_t*, int> hop_counts;
  std::uint64_t stamp         = 0;
  bool          has_stamp     = false;
  bool          used_fallback = false;
};
std::unordered_map<const action_t*, chain_hop_slot> g_chain_hop_stash;

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

enemy_fact build_enemy_fact( const action_t* a, player_t* candidate )
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

  // OBS-03/R-U (tstl-sylvanas 232-02): deterministic geometry, never a target-cache read --
  // count of alive enemies within THIS action's OWN resolved radius of the candidate (a->radius:
  // 10.0 for chain_lightning, 8.0 for tempest -- WHICHEVER action called build_enemy_fact -- never
  // a hardcoded chain_lightning constant, WR-06 260902/cr4). Was previously written into TWO
  // always-equal fields (232-12, LO-01: the pre-232-02 splash-radius and jump-radius neighbour
  // counts); OBS-03 collapsed them to the one field below since no action ever needed the two
  // counts to differ. A shaped spell's true splash geometry is plan 228-03's, not this plan's.
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
  f.neighbours_within_radius = neighbours;

  f.is_current_target = ( candidate == a->player->target );
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
// actually scores. OBS-01 (232-02) dropped build_enemy_fact's `previous_pick` parameter entirely,
// so there is no local to thread through here any more.
std::vector<enemy_fact> build_candidate_facts( const action_t* a, bool harmful )
{
  std::vector<enemy_fact> out;
  for ( player_t* t : a->sim->target_non_sleeping_list )
    if ( t->is_enemy() && generic_filter( a, t, harmful ) )
      out.push_back( build_enemy_fact( a, t ) );
  return out;
}



namespace
{

// BL-01 (232-13): resolves the MODELLED spell's geometry (radius, hop cap) -- never the CALLER's
// own action_t*. Both preference_tempest and preference_chain_lightning are dispatched for the two
// Thorim's-aware melee strikes (windstrike/stormstrike, RULE-01) as well as for the tempest/
// chain_lightning tokens themselves; a melee strike's own action_t* carries neither a radius nor an
// aoe (`stormstrike_base_t`'s ctor sets neither -- `action_t::radius`/`aoe` come only from
// `spelleffect_data.radius_max()`/`max_targets()`, action.cpp:901/921/986, :796-797/:948-950, both
// 0 for a melee weapon strike), so reading `a->radius`/`a->aoe` off the resolved (caller) action
// silently degenerated both branches to "0 neighbours"/"hop cap 1" -- BL-01's whole defect. Named
// by literal string (never a variable holding the spell name) so `find_action("chain_lightning")`/
// `find_action("tempest")` are grep-able, single-source citations, matching
// `preference_for_thorims_aware_strike`'s own `find_action("thorims_invocation")` idiom below.
// Refuses (throws, never silently falls back to the caller's own zero geometry) when the resolved
// source is null or its radius is <= 0.0 -- a structural invariant: a Thorim's-primed decision with
// no Tempest/Chain-Lightning action registered on this player is a configuration error.
struct chain_geometry
{
  double radius = 0.0;
  int    cap    = 1;
};

chain_geometry resolve_thorims_branch_geometry( const action_t* resolved, preference_fn pref )
{
  const bool      is_chain_lightning = ( pref == preference_chain_lightning );
  const action_t* source             = is_chain_lightning
                                            ? resolved->player->find_action( "chain_lightning" )
                                            : resolved->player->find_action( "tempest" );
  if ( !source || source->radius <= 0.0 )
  {
    throw sc_runtime_error( fmt::format(
        "rl_target_select::resolve_thorims_branch_geometry: the Thorim's-aware branch for '{}' "
        "resolved a null or zero-radius geometry source ('{}') -- refusing rather than silently "
        "falling back to the caller's own (zero) radius/aoe (BL-01)",
        resolved->name_str, is_chain_lightning ? "chain_lightning" : "tempest" ) );
  }

  chain_geometry geo;
  geo.radius = source->radius;
  if ( is_chain_lightning )
  {
    geo.cap = source->aoe > 0 ? source->aoe : 1;

    // ME-06/R-AK (232-13): the hop cap follows the ENGINE's own resolved `aoe`. MEASURED this
    // session on the standard `mid2-stormbringer.simc` build (Chaining Storms talented, entryId
    // 135254 present in `spec_talents`): `aoe` resolves to 5, matching the addon's talented cap
    // (`hasTalentChainingStorms ? 5 : 3`, enhancementShamanContext.ts) -- NOT the 3 a static read
    // of Chain Lightning's own talent spell (id 188443, "Chain Targets: 3") would suggest absent
    // any generic effect-merge in `sc_shaman.cpp` (which references Chaining Storms, id 334308,
    // in exactly two places -- neither an `apply_affecting_effects`-style call -- so the +2 must
    // be applied by some OTHER, more generic DBC-level mechanism this session did not trace).
    // Attempts to reproduce a WITHOUT-Chaining-Storms baseline by re-asserting a stripped
    // `spec_talents=` line LATER in the same profile (including an entirely EMPTY one) did NOT
    // change either this cap or the unrelated, independently-observable
    // `has_talent_thorims_invocation` dump field -- i.e. that override methodology itself does
    // not take effect for an `input=`-included profile in this build, so this session could NOT
    // conclusively measure the WITHOUT-talent case. Given that, this refuses on anything OTHER
    // than the two values either side of the addon's own ternary ({3, 5}) rather than asserting
    // a single unconfirmed number -- protects against a genuinely wrong cap (e.g. a future
    // spell-data change moving it to some third value) without overclaiming a with/without-talent
    // causal link this session did not establish. See the addon-parity todo for the open
    // question and how to close it properly (a real in-game or clean-profile measurement).
    if ( geo.cap != 3 && geo.cap != 5 )
    {
      throw sc_runtime_error( fmt::format(
          "rl_target_select::resolve_thorims_branch_geometry: chain_lightning's resolved aoe cap "
          "is {} -- expected 3 or 5 (the addon's own hasTalentChainingStorms ? 5 : 3 ternary); "
          "refusing rather than silently training on an unmeasured cap",
          geo.cap ) );
    }
  }
  return geo;
}

// WR-05 (260902/cr4): the cheap half of select()'s scoring-loop fact build -- see that call
// site's own comment. NOT exposed in the header: build_enemy_fact() (above) is the one and only
// public fact-record builder every OTHER caller (the future observation writer, 228-04 onward)
// uses, with every field filled exactly as before this task. `build_enemy_fact` (the OBSERVATION
// builder) is DELIBERATELY untouched by BL-01's fix below -- the strike blocks' own
// `neighbours_within_radius` obs leaf stays the strike's own (zero) geometry; a schema-semantic
// change there is Phase 233's census (R5-1), not this plan's.
enemy_fact build_enemy_fact_for_scoring( const action_t* a, player_t* candidate, preference_fn pref )
{
  enemy_fact f;
  f.candidate   = candidate;
  f.time_to_die = std::min( candidate->time_to_percent( 0 ).total_seconds(), 600.0 );  // WR-10

  if ( pref == preference_lava_lash || pref == preference_voltaic_blaze )
  {
    dot_t* fs = candidate->find_dot( "flame_shock", a->player );  // WR-04
    f.flame_shock_remaining = fs ? fs->remains().total_seconds() : 0.0;
  }

  // BL-01 (232-13): the neighbour count is computed at the MODELLED spell's own resolved radius --
  // Tempest's or Chain Lightning's, resolved by name via resolve_thorims_branch_geometry -- never
  // the caller's (`a`'s) own radius, which is 0 for the two Thorim's-aware melee strikes.
  if ( pref == preference_tempest || pref == preference_chain_lightning )
  {
    const chain_geometry geo = resolve_thorims_branch_geometry( a, pref );
    int neighbours = 0;
    for ( player_t* other : a->sim->target_non_sleeping_list )
    {
      if ( other == candidate || !other->is_enemy() )
        continue;
      if ( candidate->get_player_distance( *other ) <= geo.radius + other->combat_reach )
        ++neighbours;
    }
    f.neighbours_within_radius = neighbours;
  }
  else if ( a->radius > 0.0 )
  {
    // Defensive generality only -- no token in TARGETED_TOKENS reaches this branch today (every
    // OTHER registry preference is single-target, so `a->radius` stays 0); kept so a future
    // radius-bearing registry action does not silently skip the neighbour count.
    int neighbours = 0;
    for ( player_t* other : a->sim->target_non_sleeping_list )
    {
      if ( other == candidate || !other->is_enemy() )
        continue;
      if ( candidate->get_player_distance( *other ) <= a->radius + other->combat_reach )
        ++neighbours;
    }
    f.neighbours_within_radius = neighbours;
  }

  f.is_current_target = ( candidate == a->player->target );
  f.actor_index       = candidate->actor_index;
  f.actor_spawn_index = candidate->actor_spawn_index;
  return f;
}

// RULE-02 (232-06, R-Z; geometry parameterised 232-13/BL-01): the greedy Chain Lightning hop
// simulation, ported from the addon's enhancementShamanTargeting.ts:933-969 hop loop as pure
// geometry. For EACH candidate in `candidates` treated as the chain's START, greedily walks to the
// nearest not-yet-hit candidate -- from the SAME already-filtered `candidates` set select() just
// built (D-20 forbids a third filter loop: this never re-scans target_non_sleeping_list, re-calls
// generic_filter, or calls build_candidate_facts() a second time) -- within the explicit `radius`
// parameter (`radius + other->combat_reach`, the SAME inequality convention build_enemy_fact's own
// neighbours_within_radius above already uses), until `cap` candidates have been hit including the
// start, or no further hop is available. BOTH `radius` and `cap` are the MODELLED spell's own
// (resolve_thorims_branch_geometry's caller resolves them by name, never a hardcoded literal, never
// read off the calling action `a` -- BL-01's fix). Fills `out` keyed on each START candidate; `out`
// is a plain local the caller stashes, never a module global itself.
//
// R-Z's algebra trap, named here rather than merely in this function's caller: the addon's
// SEPARATE, uninvolved PRODUCTION hop walk (the one this ports is the addon's PURE RULES walk,
// enhancementTargetRules.ts) uses `dist - br <= R`; this walk uses `dist <= R + other_reach` --
// equal only when every actor shares the same reach/br value. 232-05's parity harness sidesteps the
// difference with uniform-reach fixtures.
//
// NEVER calls the engine's own chain-resolution helper in sc_shaman.cpp -- that helper draws from
// the sim's random number stream (FORK-03's own fix exists to keep a read-only selector path from
// ever perturbing that stream); this walk reads only already-resolved positions and combat_reach,
// so it is provably deterministic geometry -- a DETERMINISTIC APPROXIMATION of the engine's own
// randomised chain resolution, never a prediction of it (LO-05/R-D).
void compute_chain_hop_counts( double radius, int cap, const std::vector<player_t*>& candidates,
                                std::unordered_map<const player_t*, int>& out )
{
  std::vector<bool> hit( candidates.size(), false );
  for ( std::size_t start_index = 0; start_index < candidates.size(); ++start_index )
  {
    std::fill( hit.begin(), hit.end(), false );
    hit[ start_index ]    = true;
    player_t* current     = candidates[ start_index ];
    int       hop_count   = 1;
    while ( hop_count < cap )
    {
      int    nearest_index = -1;
      double nearest_dist  = std::numeric_limits<double>::max();
      for ( std::size_t candidate_index = 0; candidate_index < candidates.size(); ++candidate_index )
      {
        if ( hit[ candidate_index ] )
          continue;
        player_t* other = candidates[ candidate_index ];
        double    dist  = current->get_player_distance( *other );
        if ( dist <= radius + other->combat_reach && dist < nearest_dist )
        {
          nearest_dist  = dist;
          nearest_index = static_cast<int>( candidate_index );
        }
      }
      if ( nearest_index < 0 )
        break;
      hit[ static_cast<std::size_t>( nearest_index ) ] = true;
      current   = candidates[ static_cast<std::size_t>( nearest_index ) ];
      ++hop_count;
    }
    out[ candidates[ start_index ] ] = hop_count;
  }
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

  // Phase 230-02 (SCOR-01, R-B): the scorer's overflow refusal -- REFUSE BY NAME the instant the
  // generic filter's own live candidate count exceeds the LOADED blob's own declared
  // `scorer.slots`, never truncate and never drop the tail. Checked before the single-candidate
  // shortcut below so a slot count of 0 (already refused at load, T-230-01-01) can never reach
  // this far, and before the scoring loop so a malformed blob is refused at the FIRST decision it
  // would ever be consulted, not silently mid-loop.
  rl_policy::rl_scorer_t* scorer_scratch = nullptr;  // 230-04: non-null only when pref == preference_scorer
  if ( pref == preference_scorer )
  {
    rl_policy::rl_weights_t& w = *a->player->sim->solver_policy_weights;
    if ( candidates.size() > w.scorer.slots )
    {
      throw sc_runtime_error( fmt::format(
          "rl_target_select::select: {} candidates passed the generic filter for '{}', exceeding "
          "the loaded scorer's declared slots={} -- refusing rather than truncating",
          candidates.size(), a->name_str, w.scorer.slots ) );
    }
    // 230-04 (SCOR-02, R-B): the translog's own candidate block is a FIXED RL_TARGET_SLOTS-wide
    // table -- a SEPARATE, tighter bound than the blob's own declared scorer.slots above (which
    // may legally be as wide as 64, RLW1_MAX_SCORER_SLOTS). Refuse rather than write past the
    // fixed block's own bound; this can only fire on a blob whose declared slots exceeds
    // RL_TARGET_SLOTS, which no committed 230-* fixture does.
    if ( candidates.size() > RL_TARGET_SLOTS )
    {
      throw sc_runtime_error( fmt::format(
          "rl_target_select::select: {} candidates passed the generic filter for '{}', exceeding "
          "the transition log's fixed RL_TARGET_SLOTS={} -- refusing rather than writing past the "
          "candidate block",
          candidates.size(), a->name_str, RL_TARGET_SLOTS ) );
    }
    scorer_scratch = &w.scorer;

    // Stamp the candidate block table for THIS decision up front, before any early return, so a
    // stale block from an earlier decision can never be read as current (mirrors g_pick_table's
    // own stamp discipline). Re-filled below as candidates are actually scored.
    candidate_block_slot& slot = g_candidate_block_table[ a ];
    if ( slot.features.size() != RL_TARGET_SLOTS * RL_TARGET_FEATURES )
      slot.features.assign( RL_TARGET_SLOTS * RL_TARGET_FEATURES, 0.0f );
    else
      std::fill( slot.features.begin(), slot.features.end(), 0.0f );
    slot.mask        = 0;
    slot.count       = 0;
    slot.chosen_slot = rl_translog::CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK;
    slot.stamp        = current_decision_stamp( a->player );
    slot.has_stamp    = true;
  }

  // TGT-02 edge: single -- that candidate is the pick regardless of preference; the sticky clause
  // cannot override it (there is nothing else to be sticky about). The general algorithm below
  // would reach the identical answer without this shortcut, but stating it explicitly matches the
  // must-have's own wording and keeps the edge case visible in code, not just in behaviour.
  // 230-04: SKIPPED when the scorer is active -- the general loop below is what CAPTURES the
  // candidate block (R-B), and a single-candidate decision still needs its (trivial, one-slot)
  // block recorded, never silently skipped.
  if ( candidates.size() == 1 && pref != preference_scorer )
    return candidates.front();

  // RULE-02 (232-06, R-Z): compute the Chain Lightning hop-count stash ONCE per decision, HERE --
  // before the scoring loop below calls `pref()` once per candidate. `preference_chain_lightning`
  // cannot see the other candidates itself (`preference_fn` is one-candidate-at-a-time by design),
  // so the geometry must be precomputed and stashed for it to read. Reused, not re-derived: this
  // walks the SAME `candidates` vector the generic-filter loop above already built (D-20).
  if ( pref == preference_chain_lightning )
  {
    chain_hop_slot& hop_slot = g_chain_hop_stash[ a ];
    hop_slot.hop_counts.clear();
    // BL-01 (232-13): geometry resolved by name (Chain Lightning's own radius/cap), never read
    // off `a` -- see resolve_thorims_branch_geometry's own doc comment above.
    const chain_geometry geo = resolve_thorims_branch_geometry( a, pref );
    compute_chain_hop_counts( geo.radius, geo.cap, candidates, hop_slot.hop_counts );
    hop_slot.stamp         = current_decision_stamp( a->player );
    hop_slot.has_stamp     = true;
    hop_slot.used_fallback = false;
  }

  // CR-03 (260902/cr4, RULING (a) -- PREFERENCE FIRST): the sticky early-return that used to
  // live here is GONE. OBS-01/R5-5 (232-02) then removed the sticky TIE-BREAK too -- the action's
  // own previous pick (`a->target`) is no longer read anywhere in this function, so there is no
  // `previous` local any more. Ladder: (1) preference, highest score wins; (2) among EQUAL scores,
  // the player's CURRENT target (p->target); (3) final tie-break on the stable
  // (actor_index, actor_spawn_index) identity pair, ascending -- a total order, so a run is
  // reproducible (TGT-02 edge: ordering). Consequence: an exact tie between two candidates can now
  // flip when p->target moves for an unrelated reason (the ordering stays a total order because
  // the identity pair is the final rung, so runs remain reproducible).
  player_t*   current_target = a->player->target;
  player_t*   best           = nullptr;
  double      best_score     = 0.0;
  std::size_t best_slot      = 0;  // 230-04: index of `best` within `candidates`, kept in lockstep
  for ( std::size_t slot_index = 0; slot_index < candidates.size(); ++slot_index )
  {
    player_t* c = candidates[ slot_index ];
    // WR-05 (260902/cr4): the SCORING loop drives exactly one of the eight rule preference
    // functions -- none of them read distance/in_reach/in_range/in_front/alive/immune/
    // immunity_remaining/health_pct/is_boss (generic_filter already excluded anything those would
    // have rejected), so this lite build skips them and skips the Flame Shock find_dot() lookup
    // unless the dispatched preference is one of the two that read it. build_enemy_fact() itself
    // is UNCHANGED -- it is what external readers (the observation writer, 228-04 onward) call,
    // with every field filled.
    //
    // Phase 230-02 (SCOR-01): the NINTH preference (the learned scorer) is the one exception --
    // its v1 scorecard (R-C) reads every field the struct carries, so it gets the FULL builder
    // instead of the lite one. This costs more per candidate than the rules path, paid ONLY when
    // a scorer is actually loaded and active (preference_for's own gate) -- the eight rule
    // preferences' own performance is completely unaffected by this branch.
    enemy_fact fact  = ( pref == preference_scorer ) ? build_enemy_fact( a, c )
                                                      : build_enemy_fact_for_scoring( a, c, pref );
    double     score = pref( a, fact );

    // 230-04 (SCOR-02, R-B): CAPTURE, never recompute -- preference_scorer's own call just above
    // filled `scorer_scratch->feature_scratch` with this candidate's feature vector (the SAME
    // buffer forward_scorer just read to produce `score`); copy the declared feature prefix
    // (excluding the trailing aiming-spell one-hot, which is per-ACTION not per-candidate) into
    // this decision's persistent candidate block at THIS candidate's slot.
    if ( scorer_scratch != nullptr )
    {
      candidate_block_slot& slot = g_candidate_block_table[ a ];
      std::memcpy( slot.features.data() + slot_index * RL_TARGET_FEATURES,
                   scorer_scratch->feature_scratch.data(), RL_TARGET_FEATURES * sizeof( float ) );
      slot.mask |= static_cast<std::uint16_t>( 1u << slot_index );
      slot.count = static_cast<std::uint8_t>( slot.count + 1 );
    }

    if ( !best )
    {
      best       = c;
      best_score = score;
      best_slot  = slot_index;
      continue;
    }

    bool better;
    if ( score != best_score )
    {
      better = score > best_score;
    }
    else
    {
      // OBS-01/R5-5 (232-02): the sticky `c_is_prev`/`best_is_prev` rung that used to sit here
      // (CR-03, 260902/cr4) is REMOVED -- the current target is now the only tie-break before the
      // stable identity pair. A tie can flip when p->target moves for an unrelated reason; the
      // ordering stays a total order because the identity pair below is the final rung, so runs
      // remain reproducible.
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
      best_slot  = slot_index;
    }
  }

  if ( scorer_scratch != nullptr && best != nullptr )
    g_candidate_block_table[ a ].chosen_slot = static_cast<std::uint8_t>( best_slot );

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

// 230-04 (SCOR-02, R-B): mirrors lookup_pick's own stamp-comparison discipline exactly -- a
// block found in the table but stamped for an EARLIER decision is staleness, refused by name
// (`*out_found = false`) rather than returned, the same D-12 "never fall back to a fresh
// computation" reasoning lookup_pick's own comment states.
candidate_block lookup_candidate_block( const action_t* resolved, bool* out_found )
{
  auto it = g_candidate_block_table.find( resolved );
  if ( it == g_candidate_block_table.end() || !it->second.has_stamp )
  {
    if ( out_found )
      *out_found = false;
    return candidate_block{};
  }
  auto stamp_it = g_decision_stamp.find( resolved->player );
  std::uint64_t current_stamp = ( stamp_it == g_decision_stamp.end() ) ? 0 : stamp_it->second;
  if ( it->second.stamp != current_stamp )
  {
    if ( out_found )
      *out_found = false;
    return candidate_block{};
  }
  if ( out_found )
    *out_found = true;
  candidate_block block;
  block.features    = it->second.features.data();
  block.mask         = it->second.mask;
  block.count        = it->second.count;
  block.chosen_slot  = it->second.chosen_slot;
  return block;
}

// 232-04 (OBS-02, R-T): stamps `resolved`'s pre-cast is_current_target for the CURRENT decision.
// Called ONLY from build_obs()'s real-decision path (rl_policy_obs.cpp, gated on
// `rl_state_t::is_decision_boundary`) -- never from decision_dump.cpp's own diagnostic build_obs()
// call, so a dump-time recompute (post accept_cast retarget) can never overwrite the real
// decision's capture. A no-op when `resolved` is null (mirrors fill_pick's own guard).
void stamp_target_fact_is_current_target( const action_t* resolved, bool is_current_target )
{
  if ( !resolved )
    return;
  auto& slot = g_target_fact_snapshot_table[ resolved ];
  slot.is_current_target     = is_current_target;
  slot.has_is_current_target = true;
  slot.stamp                  = current_decision_stamp( resolved->player );
  slot.has_stamp               = true;
}

// Same contract as stamp_target_fact_is_current_target, for the `hit_damage` leaf
// (`action_leaf_kind::shared_hit_damage`) -- 232-12 (ME-07): NOT Tempest-specific; every registry
// action declaring a `hit_damage` leaf gets stamped through this same function (eight today).
void stamp_target_fact_hit_damage( const action_t* resolved, double hit_damage )
{
  if ( !resolved )
    return;
  auto& slot = g_target_fact_snapshot_table[ resolved ];
  slot.hit_damage     = hit_damage;
  slot.has_hit_damage = true;
  slot.stamp           = current_decision_stamp( resolved->player );
  slot.has_stamp        = true;
}

// Reads the snapshot stamped for `resolved` at the CURRENT decision. `*out_found` is false when
// the stamp is stale or absent (the SAME has_stamp/stamp==current guard lookup_pick/
// lookup_candidate_block already use, T-232-15) -- the caller (decision_dump.cpp) MUST then
// compute fresh and flag the row (target_fact_dump_time_compute), never fabricate a value
// (T-232-14). `has_is_current_target`/`has_hit_damage` on the returned struct are independent of
// `*out_found` -- a found-but-partial slot (e.g. is_current_target stamped, hit_damage never
// requested for this action) reports each half honestly.
target_fact_snapshot lookup_target_fact_snapshot( const action_t* resolved, bool* out_found )
{
  auto it = g_target_fact_snapshot_table.find( resolved );
  if ( it == g_target_fact_snapshot_table.end() || !it->second.has_stamp )
  {
    if ( out_found )
      *out_found = false;
    return target_fact_snapshot{};
  }
  auto stamp_it = g_decision_stamp.find( resolved->player );
  std::uint64_t current_stamp = ( stamp_it == g_decision_stamp.end() ) ? 0 : stamp_it->second;
  if ( it->second.stamp != current_stamp )
  {
    if ( out_found )
      *out_found = false;
    return target_fact_snapshot{};
  }
  if ( out_found )
    *out_found = true;
  target_fact_snapshot out;
  out.has_is_current_target = it->second.has_is_current_target;
  out.is_current_target      = it->second.is_current_target;
  out.has_hit_damage         = it->second.has_hit_damage;
  out.hit_damage             = it->second.hit_damage;
  return out;
}

bool apply_candidate_exploration( const action_t* resolved, player_t* replacement,
                                   std::uint8_t replacement_slot )
{
  auto it = g_pick_table.find( resolved );
  if ( it == g_pick_table.end() || !it->second.has_stamp )
    return false;
  auto stamp_it = g_decision_stamp.find( resolved->player );
  std::uint64_t current_stamp = ( stamp_it == g_decision_stamp.end() ) ? 0 : stamp_it->second;
  if ( it->second.stamp != current_stamp )
    return false;

  // The row records what happened, never what was intended (must_haves): overwrite the STAMPED
  // pick so accept_cast()'s own lookup_pick() call -- reached AFTER this function returns, per
  // solver_control.cpp's own call ordering -- sees the replacement, not the scorer's original.
  it->second.pick = replacement;

  auto block_it = g_candidate_block_table.find( resolved );
  if ( block_it != g_candidate_block_table.end() && block_it->second.has_stamp &&
       block_it->second.stamp == current_stamp )
    block_it->second.chosen_slot = replacement_slot;

  return true;
}

void reset( sim_t* )
{
  // WR-11 (260902/cr4): called from sim_t::reset() (sim.cpp), once per iteration -- clears every
  // module global (the original three, plus CR-04's deadlock counter added later the same task,
  // plus 230-04's candidate block table, plus 232-04's target-fact snapshot table, plus 232-06's
  // Chain Lightning hop-count stash) so a stale pick, stamp or count from a PRIOR iteration can
  // never leak into the next one. `sim` itself is unused (the tables are keyed on `player_t*`, not
  // sim identity, per this module's own single-sim/single-thread precondition) but is taken by
  // pointer to mirror raid_event_t::reset( sim )'s own signature at the call site.
  g_decision_stamp.clear();
  g_pick_table.clear();
  g_candidate_block_table.clear();  // 230-04
  g_target_fact_snapshot_table.clear();  // 232-04 (OBS-02, R-T)
  g_chain_hop_stash.clear();  // 232-06 (RULE-02, R-Z)
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

namespace
{
// RULE-01 (232-06, R-AC/P232-29): the Thorim's-aware strike SUBSTITUTION -- called once per
// decision from preference_for() below, for the two strike tokens only (windstrike, stormstrike).
// primordial_storm and lightning_bolt stay routed to preference_shortest_time_to_die directly by
// preference_for() itself; this function is never consulted for them. Gates, all read through the
// file's existing non-allocating idioms (buff_t::find / player_t::find_action -- never a fresh
// allocation per decision): the talent via find_action("thorims_invocation") being non-null;
// Maelstrom Weapon's stack count greater than zero; for Stormstrike ONLY, Doom Winds present via
// check() (sc_shaman.cpp:2126's own convention -- never up()). When the gates pass: Tempest
// present gives preference_tempest; otherwise the accessor reporting the chain-lightning kind
// gives preference_chain_lightning; anything else (lightning-bolt-primed OR not-yet-primed) falls
// through to preference_shortest_time_to_die.
//
// R-AC's real third branch: the engine's own trigger_thorims_invocation (sc_shaman.cpp:12962-12988)
// has THREE outcomes, not two -- a Tempest override, a Chain-Lightning-primed cast, and a
// Lightning-Bolt-primed-or-unprimed cast (that function's own "Default to Lightning Bolt" comment
// is the un-primed case; the primed-lightning-bolt case is the SAME branch, since both read as
// thorims_primed_kind::lightning_bolt or ::none from the accessor). A reader who only knows
// R5-19's earlier two-branch text will expect this to collapse to {tempest, chain} -- it MUST NOT:
// the lightning-bolt-primed case is a real, reachable production state (any strike cast while
// Thorim's is armed with Lightning Bolt), not a theoretical corner, and it must fall through to
// the base rule exactly like the "not yet primed" case does, matching the engine's own default.
//
// LO-04 (232-13): a shared modelling divergence, not merely a fork one -- these gates are read at
// DECISION time (this function runs before the cast resolves), while the engine evaluates the
// SAME gates at IMPACT time (`trigger_thorims_invocation`, called from `stormstrike_t::impact`/
// `windstrike_t::impact`, sc_shaman.cpp:12965-12973). Maelstrom Weapon's stack count or the
// Tempest buff can change between the two, so this function is a close model of
// `trigger_thorims_invocation`, never an exact one -- the addon's own strike-aim rule shares this
// exact divergence (enhancementTargetRules.ts).
preference_fn preference_for_thorims_aware_strike( const action_t* resolved, bool is_stormstrike )
{
  player_t* p = resolved->player;

  if ( !p->find_action( "thorims_invocation" ) )
    return preference_shortest_time_to_die;

  buff_t* maelstrom_weapon = buff_t::find( p, "maelstrom_weapon" );
  if ( !maelstrom_weapon || maelstrom_weapon->check() == 0 )
    return preference_shortest_time_to_die;

  // Windstrike takes the Thorim's branch whenever the gates above pass; Stormstrike ADDITIONALLY
  // requires Doom Winds -- it is triggered from Ascendance's own execute path and Ascendance IS an
  // agent action (232-RESEARCH.md Addendum C4), so this branch is reachable in production, not
  // dead code.
  if ( is_stormstrike )
  {
    buff_t* doom_winds = buff_t::find( p, "doom_winds" );
    if ( !doom_winds || !doom_winds->check() )
      return preference_shortest_time_to_die;
  }

  buff_t* tempest = buff_t::find( p, "tempest" );
  if ( tempest && tempest->check() )
    return preference_tempest;

  if ( shaman_thorims_primed_kind( p ) == thorims_primed_kind::chain_lightning )
    return preference_chain_lightning;

  return preference_shortest_time_to_die;
}
} // anonymous namespace

preference_fn preference_for( const action_t* resolved )
{
  if ( !resolved )
    return nullptr;
  const std::string& n = resolved->name_str;
  preference_fn rule = nullptr;
  if ( n == "stormstrike" || n == "windstrike" )
    rule = preference_for_thorims_aware_strike( resolved, n == "stormstrike" );
  else if ( n == "primordial_storm" || n == "lightning_bolt" )
    rule = preference_shortest_time_to_die;
  else if ( n == "lava_lash" )
    rule = preference_lava_lash;
  else if ( n == "voltaic_blaze" )
    rule = preference_voltaic_blaze;
  else if ( n == "chain_lightning" )
    rule = preference_chain_lightning;
  else if ( n == "tempest" )
    rule = preference_tempest;
  else
    return nullptr;

  // Phase 230-02 (SCOR-01, D-01/D-02/R-K): the run-time switch is the PRESENCE of a scorer
  // section in the loaded weights -- a rotation-only blob (has_scorer == false, v2/v3) always
  // takes the rules path; a v4 blob with a scorer takes the scored path UNLESS
  // sim->target_scorer_force_rules (default OFF, sim.hpp/sim.cpp -- 230-02 Task 2) forces the
  // rules path so the PREVIOUS phase's rules-arm numbers can be re-run byte-identically on this
  // binary (230-SWAP-RECEIPT.md's Task 3 proof). No other code path here changes -- the generic
  // filter, the precedence ladder in select() (beyond the one `pref == preference_scorer` branch
  // that widens the fact builder and the overflow check, both structurally inert for the rules
  // path) and accept_cast's cast path are all unaware which of the nine preferences won.
  const sim_t* sim = resolved->player->sim;
  if ( sim->solver_policy_weights && sim->solver_policy_weights->has_scorer &&
       !sim->target_scorer_force_rules )
    return preference_scorer;
  return rule;
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

double preference_chain_lightning( const action_t* a, const enemy_fact& fact )
{
  // RULE-02 (232-06, R-Z): reads the greedy hop-count stash `select()` computed just before the
  // scoring loop that calls this function (compute_chain_hop_counts, above), keyed on `a` and this
  // decision's stamp -- REPLACES the retired deterministic neighbour-count approximation this
  // function used before this plan. `preference_tempest` below keeps that retired approximation
  // (R5-20: the CL chain simulation is the FORK's own selector pick; the rules-module/tempest
  // neighbour-count pick stays offline/parity-only for Tempest) -- the two stay two distinct
  // function pointers (R-U) so this substitution touches exactly one dispatch slot.
  auto slot_it = g_chain_hop_stash.find( a );
  if ( slot_it != g_chain_hop_stash.end() && slot_it->second.has_stamp &&
       slot_it->second.stamp == current_decision_stamp( a->player ) )
  {
    auto hop_it = slot_it->second.hop_counts.find( fact.candidate );
    if ( hop_it != slot_it->second.hop_counts.end() )
      return static_cast<double>( hop_it->second ) * 1.0e6 + fact.time_to_die;
    // Defensive (should be unreachable when select() drove this call -- fact.candidate came from
    // the SAME `candidates` vector compute_chain_hop_counts just walked): the stash exists and is
    // current for `a`, but carries no entry for THIS candidate specifically. Flag the fallback
    // visibly on the ALREADY-stamped entry rather than silently reusing the retired approximation
    // below.
    slot_it->second.used_fallback = true;
    return static_cast<double>( fact.neighbours_within_radius ) * 1.0e6 + fact.time_to_die;
  }
  // HI-04 (232-13): reached when no stash entry exists for `a` at all this decision -- e.g. a
  // caller invoking this function directly rather than through select()'s own
  // pref==preference_chain_lightning gate (the only path today that fills the stash). Previously
  // this fell straight through to the neighbour-count expression with NO record of the fallback,
  // so chain_hop_fallback_used() could never report the one case its own doc comment names as
  // reachable -- the getter and this setter now agree. Stamp a fresh entry (empty hop_counts,
  // used_fallback=true) so the getter has something CURRENT to read, mirroring g_pick_table's own
  // "stamp before any early return" discipline (select()'s scorer branch, above). The fallback
  // value returned below is the retired neighbour-count approximation this function used before
  // RULE-02 -- labelled per R-D, never a prediction of the engine's own randomised chain walk
  // (sc_shaman.cpp:982-1057).
  chain_hop_slot& fresh = g_chain_hop_stash[ a ];
  fresh.hop_counts.clear();
  fresh.stamp         = current_decision_stamp( a->player );
  fresh.has_stamp     = true;
  fresh.used_fallback = true;
  return static_cast<double>( fact.neighbours_within_radius ) * 1.0e6 + fact.time_to_die;
}

// RULE-02 (232-06, R-Z): see this function's own declaration (rl_target_select.hpp) for the
// staleness-guard contract -- mirrors lookup_pick's has_stamp/stamp==current discipline exactly.
bool chain_hop_fallback_used( const action_t* resolved )
{
  auto it = g_chain_hop_stash.find( resolved );
  if ( it == g_chain_hop_stash.end() || !it->second.has_stamp )
    return false;
  auto stamp_it = g_decision_stamp.find( resolved->player );
  std::uint64_t current_stamp = ( stamp_it == g_decision_stamp.end() ) ? 0 : stamp_it->second;
  if ( it->second.stamp != current_stamp )
    return false;
  return it->second.used_fallback;
}

double preference_tempest( const action_t*, const enemy_fact& fact )
{
  // BL-01 (232-13): fact.neighbours_within_radius is now always computed at the GEOMETRY SOURCE's
  // own resolved radius (Tempest's, measured 8.0), resolved by name via
  // resolve_thorims_branch_geometry regardless of whether the caller was Tempest itself or a
  // Thorim's-primed melee strike (RULE-01) -- never the caller's own (possibly zero) radius. Same
  // formula as chain_lightning's, applied to Tempest's own resolved radius -- the addon's 8-yard
  // cluster-centre rule.
  return static_cast<double>( fact.neighbours_within_radius ) * 1.0e6 + fact.time_to_die;
}

// ---------------------------------------------------------------------------------------------
// Phase 230-02 (SCOR-01): the ninth preference, the learned scorer. preference_for() only ever
// returns this pointer when the loaded weights actually carry a scorer (D-02/R-K) -- the
// `has_scorer` assert below is a tripwire against a future call site bypassing that gate, never a
// real "maybe" (a preference must always return SOME score, D-12).
// ---------------------------------------------------------------------------------------------

double preference_scorer( const action_t* a, const enemy_fact& fact )
{
  rl_policy::rl_weights_t& w = *a->player->sim->solver_policy_weights;
  assert( w.has_scorer &&
          "preference_scorer called with no scorer loaded -- preference_for's own gate should "
          "have prevented this" );
  rl_policy::rl_scorer_t& s = w.scorer;

  // CK1-1: exactly eight targeted spells get a one-hot column -- this is the one place the wire
  // format's own aiming-spell width (rl_policy_net.cpp's RLW1_V4_AIMING_SPELL_COUNT, 8) and this
  // module's own TARGETED_TOKENS[] must agree; asserted here rather than merely relied upon.
  const std::size_t n_tok = targeted_action_token_count();
  assert( n_tok == 8 && "CK1-1: exactly eight targeted spells get a one-hot column" );

  // s.feature_scratch is load-time-sized to features + n_tok (rl_policy_net.cpp's load_rlw1) --
  // reused every call, never reallocated per decision (this plan's own no-allocation-in-the-
  // per-decision-score-path prohibition).
  float*      feats = s.feature_scratch.data();
  std::size_t i     = 0;

  // Order-locked to target_features.py's own derivation (struct enemy_fact's declaration order,
  // EXCLUDING candidate/actor_index/actor_spawn_index -- an identity number as a feature would
  // make the score depend on enumeration order) -- a reordering on either side of the RLW1 wire
  // is caught by the feature fingerprint refusal at load (RL_TARGET_FEATURE_SHA), never a
  // silently wrong score.
  feats[ i++ ] = static_cast<float>( fact.distance );
  feats[ i++ ] = fact.in_reach ? 1.0f : 0.0f;
  feats[ i++ ] = fact.in_range ? 1.0f : 0.0f;
  feats[ i++ ] = fact.in_front ? 1.0f : 0.0f;
  feats[ i++ ] = fact.alive ? 1.0f : 0.0f;
  feats[ i++ ] = fact.immune ? 1.0f : 0.0f;
  feats[ i++ ] = static_cast<float>( fact.immunity_remaining );
  feats[ i++ ] = static_cast<float>( fact.time_to_die );
  feats[ i++ ] = static_cast<float>( fact.health_pct );
  feats[ i++ ] = fact.is_boss ? 1.0f : 0.0f;
  feats[ i++ ] = static_cast<float>( fact.flame_shock_remaining );
  feats[ i++ ] = static_cast<float>( fact.neighbours_within_radius );
  feats[ i++ ] = fact.is_current_target ? 1.0f : 0.0f;
  feats[ i++ ] = static_cast<float>( fact.burning_core_remaining );
  feats[ i++ ] = static_cast<float>( fact.lightning_rod_stacks );
  feats[ i++ ] = static_cast<float>( fact.lightning_rod_remaining );
  feats[ i++ ] = static_cast<float>( fact.venomfang_remaining );
  feats[ i++ ] = static_cast<float>( fact.venomfang_debuff_stacks );
  feats[ i++ ] = static_cast<float>( fact.venomfang_debuff_remaining );
  feats[ i++ ] = static_cast<float>( fact.rune_of_unleashed_fire_lingering_remaining );
  assert( i == s.features &&
          "preference_scorer's fill order does not match s.features's declared count" );

  // The one-hot over the eight targeted spells, TARGETED_TOKENS[]'s own order (CK1-1) -- exactly
  // one column set, matching the resolved action's own token.
  const char* const* toks = targeted_action_tokens();
  for ( std::size_t k = 0; k < n_tok; ++k )
    feats[ i + k ] = ( a->name_str == toks[ k ] ) ? 1.0f : 0.0f;

  return static_cast<double>( rl_policy::forward_scorer( s, feats ) );
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
