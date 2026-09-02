// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// rl_target_select -- 228-02 (TGT-02/TGT-03, D-09..D-15, R2 N1). ONE function decides every
// targeted action's per-decision pick, computed ONCE per decision inside `read_action_gate_bits`
// (rl_policy_obs.cpp) and READ -- never recomputed -- by `accept_cast` (solver_control.cpp) and,
// from plan 228-04 onward, the observation block and the decision dump. This is the seam
// 226-DUMP-RNG-RECEIPT / FORK-01's own comment names in as many words: "`a->target`, not
// `p->target`: Phase 228's per-spell selectors substitute exactly this argument."
//
// Design invariant (D-12): if a future change makes any of the three readers recompute the pick
// instead of reading the stamped array, the mask and the cast can silently disagree. `lookup_pick`
// therefore reports FOUND/NOT-FOUND against a decision stamp rather than ever falling back to a
// fresh `select()` call -- the caller (accept_cast) is required to REFUSE by name when a pick is
// not found, never to paper over it by recomputing.
//
// Swapping a preference (`preference_fn`) must never require touching `generic_filter`, the
// precedence ladder in `select()`, or the cast path in `accept_cast` -- that is what makes 230's
// learned scorer (SCOR-01) a drop-in replacement for one of the eight functions at the bottom of
// this file, nothing else.
// ==========================================================================

#pragma once

#include <cstddef>
#include <cstdint>

class action_t;
class player_t;

namespace rl_target_select
{

// A plain per-enemy fact record -- no engine pointer beyond the candidate itself (D-03: the same
// shape the repo-side rig and the addon's parity harness will mirror, TGT-07). NOT sent over the
// wire by this plan; plan 228-04's observation writer builds its own leaves from the same
// per-candidate queries below, not from a cached copy of this struct.
struct enemy_fact
{
  player_t* candidate               = nullptr;
  double    distance                = 0.0;
  bool      in_reach                = false;  // range + candidate->combat_reach, THIS action's own range (D-09 G3, P-7)
  bool      in_range                = false;  // alias of in_reach -- kept as its own field per D-16's declared leaf name; identical formula today, split only if a future action needs a visibility-vs-legality distinction
  bool      in_front                = false;  // RAW geometry (is_in_front(0.0)) -- NOT gated on sim->facing_enabled; generic_filter applies that gate separately, this is ground truth for the observation writer
  bool      alive                   = false;  // !is_sleeping() (D-09 G1)
  bool      immune                  = false;  // debuffs.invulnerable->check() (D-09 G2)
  double    immunity_remaining      = 0.0;
  double    time_to_die             = 0.0;    // time_to_percent(0), same call compute_enemy_slot_candidates uses -- degenerate for bosses under fixed_time=1, R-B
  double    health_pct              = 0.0;
  bool      is_boss                 = false;
  double    flame_shock_remaining   = 0.0;    // candidate->get_dot("flame_shock", a->player)->remains() -- works on ANY enemy, not only the current target (P-5's fix point)
  int       neighbours_within_splash = 0;     // alive enemies within THIS action's own radius of the candidate (R-D: deterministic geometry, never a target-cache read)
  int       neighbours_within_jump   = 0;     // same formula, Chain Lightning's own radius -- see preference_chain_lightning's comment for why the two fields share one computation in this plan
  bool      is_current_target       = false;  // candidate == p->target
  bool      is_previous_pick        = false;  // candidate == a->target (the action's own previous pick -- P-4: re-checked every decision, never assumed still valid)
  size_t    actor_index             = 0;
  int       actor_spawn_index       = 0;      // STICKY_KEY is the PAIR (R-C / P228-8), never actor_index alone
};

// The four-clause generic filter (D-09), checked in the SAME order action_t::target_ready checks
// them (alive, immune-while-harmful, reach, front) -- generic_filter and target_ready must never
// disagree about what counts as reachable, or the mask and the cast drift apart (T-228-02-01).
// `harmful` is a real parameter, not read off `a`, so a future non-harmful targeted action does
// not silently skip the immunity clause by construction (R-E's guard, mirrored here).
bool generic_filter( const action_t* a, player_t* candidate, bool harmful );

// Builds the fact record for one candidate against the given resolved action. `previous_pick` is
// the action's own current target (a->target) at call time -- passed explicitly so callers that
// already have it (select() below) do not pay a second lookup.
enemy_fact build_enemy_fact( const action_t* a, player_t* candidate, player_t* previous_pick );

// A preference: given the resolved action and a candidate's fact record, return an ordering score
// (HIGHER WINS). The generic filter has already excluded anything genuinely invalid; a preference
// must always return SOME score among what generic_filter passed -- never a sentinel meaning "no
// opinion" -- so ties fall through to the precedence ladder's own tie-break steps, not into the
// preference function's discretion. Kept as a plain function-pointer signature (not a comparator)
// so 230's learned scorer can replace one of the eight without touching select()'s own code.
using preference_fn = double ( * )( const action_t* a, const enemy_fact& fact );

// select() -- the ONE function that decides a targeted action's pick (D-12, R2 N1). Precedence
// ladder: (1) sticky -- keep a->target if it still passes generic_filter; (2) otherwise the
// preference, highest score wins; (3) tie-break on the player's CURRENT target (p->target, which
// may differ from a->target -- another action or the engine's own acquire_target may have moved
// it more recently); (4) final tie-break on the stable (actor_index, actor_spawn_index) identity
// pair, ascending -- a total order, so a run is reproducible (TGT-02 edge: ordering). Returns
// nullptr ONLY when zero candidates pass generic_filter (TGT-02 edge: empty) -- the caller's
// legality bit must then read 0 and the unanchored wait must stay legal (T-228-02-01).
player_t* select( action_t* a, bool harmful, preference_fn pref );

// ---------------------------------------------------------------------------------------------
// Per-decision pick array plumbing (D-12). `begin_decision` is called ONCE per
// `read_action_gate_bits` invocation (never per targeted action inside that call), bumping a
// per-player monotonic counter -- this plan's own decision stamp. Neither the action-handle
// cache's key (player_t*, not decision-scoped) nor solver_control's `seq` (assigned only once the
// RL reply for THIS decision arrives, after read_state has already run and built the state the
// policy is choosing from) is available at the point read_action_gate_bits needs to stamp the
// picks it just computed -- so this plan mints its own counter, keyed the same way the action
// handle cache is (a bare `const player_t*`, same WR-12 single-sim/single-thread precondition).
// ---------------------------------------------------------------------------------------------
std::uint64_t begin_decision( const player_t* p );

// Computes and stores this targeted action's pick for the CURRENT decision (the stamp
// `begin_decision` most recently returned for `resolved->player`). Called from
// `read_action_gate_bits`, beside the existing action-handle cache fill, for every resolvable
// targeted handle. No-op (does not touch the table) if `resolved` is null.
void fill_pick( action_t* resolved, bool harmful, preference_fn pref );

// Reads the pick stamped for `resolved` at the CURRENT decision (the latest stamp
// `begin_decision` returned for `resolved->player`). `*out_found` is false when nothing has been
// stamped for this action at this decision -- the caller (accept_cast) MUST refuse by name in
// that case, never recompute (D-12's whole point: a second computation is exactly the
// three-copies failure the fork already learned once for action lookup).
player_t* lookup_pick( const action_t* resolved, bool* out_found );

// True for exactly the eight targeted registry tokens this plan governs (stormstrike,
// lightning_bolt, chain_lightning, tempest, windstrike, lava_lash, voltaic_blaze,
// primordial_storm) -- matched against `resolved->name_str`, the SAME string
// `solver_control::resolve_action` matched to build `resolved` in the first place, so this can
// never drift from the registry's own token list. False for every self/ground/item action and for
// the two SHAPED actions (crash_lightning, sundering -- plan 228-03's, not this plan's, per the
// SHAPED constant in 228-02-PLAN.md).
bool is_targeted_action( const action_t* resolved );

// Dispatches a resolved targeted action to its own preference function by name_str. Returns
// nullptr for anything is_targeted_action() would refuse (never called in that case by
// read_action_gate_bits, but kept total rather than partial for safety).
preference_fn preference_for( const action_t* resolved );

// ---------------------------------------------------------------------------------------------
// The eight preferences (D-10, R-B). Exposed individually (rather than only through
// preference_for) so the agreement probe and any future caller can name one directly.
// ---------------------------------------------------------------------------------------------

// stormstrike, windstrike, primordial_storm, lightning_bolt: longest time to die among candidates
// that outlive the cast (D-15: time_to_die - cast_time > 0). R-B's consequence: under the rig's
// fixed_time=1, every boss-type enemy's time_to_die is the fight clock with no per-actor term
// (research 3.3), so this preference cannot order two bosses at all and parks on the boss over any
// add unless the boss fails generic_filter -- recorded prominently in 228-SELECTOR-RECEIPT.md, not
// changed. If NO candidate outlives the cast, this still returns an ordered, non-null answer
// (never "invalid") by falling back to plain time_to_die ordering among the generic-filtered set
// -- the same "never invalid" reasoning D-10 states explicitly for Voltaic Blaze, applied here as
// a 228-02 ledger row since D-10's own text does not spell out this particular sub-case.
double preference_longest_time_to_die( const action_t* a, const enemy_fact& fact );

// lava_lash: among Flame Shock carriers in reach, the SHORTEST Flame Shock remaining, else the
// longest-lived -- copies the STRUCTURE of the fork's own shortest_duration_target()
// (sc_shaman.cpp:6877-6911), not a re-invented walk.
double preference_lava_lash( const action_t* a, const enemy_fact& fact );

// voltaic_blaze: a candidate WITHOUT Flame Shock, longest-lived; falls back to the longest-lived
// carrier when every candidate carries it (never invalid -- the same reason the always-legal wait
// exists).
double preference_voltaic_blaze( const action_t* a, const enemy_fact& fact );

// chain_lightning: most neighbours within its own resolved radius (the jump distance, measured
// 10.0, read via action_t::radius -- NEVER re-declared as a literal, R-A), tie-broken on time to
// die. The neighbour count is a DETERMINISTIC approximation of the engine's own randomised chain
// walk (sc_shaman.cpp:982-1057 draws sim RNG) -- labelled as an approximation, never a prediction
// of it (R-D).
double preference_chain_lightning( const action_t* a, const enemy_fact& fact );

// tempest: the addon's 8-yard cluster-centre rule -- most neighbours within its own resolved
// radius (measured 8.0), tie-broken on time to die. Same formula as chain_lightning's, applied to
// a different action's own radius -- see enemy_fact::neighbours_within_splash's comment for why
// this plan does not split the computation.
double preference_tempest( const action_t* a, const enemy_fact& fact );

// ---------------------------------------------------------------------------------------------
// Mid-cast re-resolution counters (D-14, TGT-03). action_execute_event_t::execute() (action.cpp)
// records which arm of the fallback ladder fired for every targeted action's execute event, under
// an RL-controlled sim only (the scripted APL arm never enters this path -- see that call site's
// own comment for the exact gate). Exposed here, read by 228-SELECTOR-RECEIPT.md's own tooling,
// never reset mid-run (one fight's totals).
// ---------------------------------------------------------------------------------------------
enum class reresolution_arm
{
  kept_the_pick,               // target_ready(target) was still true -- no fallback needed
  fell_back_to_player_target,  // target died/went immune; the player's own current target was ready and was substituted
  left_no_op_boundary,         // neither the current pick nor the player's target was ready -- can_execute stays false, exactly the prior silent-drop behaviour
};

void record_reresolution( reresolution_arm arm );

struct reresolution_counts
{
  std::uint64_t kept_the_pick               = 0;
  std::uint64_t fell_back_to_player_target  = 0;
  std::uint64_t left_no_op_boundary         = 0;
};

reresolution_counts get_reresolution_counts();

} // namespace rl_target_select
