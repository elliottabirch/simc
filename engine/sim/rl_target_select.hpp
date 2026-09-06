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
#include <vector>

class action_t;
class player_t;
struct sim_t;

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
  int       neighbours_within_radius = 0;     // OBS-03/R-U: live enemies within the CALLING action's own `a->radius` (+ `combat_reach`) of the candidate -- deterministic geometry, never a target-cache read. Replaces the always-equal splash/jump neighbour-count pair this struct used to carry (OBS-01/03 landed together, tstl-sylvanas 232-02).
  bool      is_current_target       = false;  // candidate == p->target
  size_t    actor_index             = 0;
  int       actor_spawn_index       = 0;      // STICKY_KEY is the PAIR (R-C / P228-8), never actor_index alone

  // 228-10 Task 1 Step 1 (D-03/D-16): the per-enemy facts D-16 names that plan 228-02's record
  // did not already carry. Every one is read directly off the CANDIDATE (never the current
  // target only, P-5's fix point), via buff_t::find()/player_t::find_dot() -- a non-allocating
  // scan of the candidate's own buff_list/dot_list, the SAME non-creating idiom
  // flame_shock_remaining above already uses (WR-04) -- never buff_t::get()/get_dot(), which
  // CREATES the object on a candidate that has never had it. 0.0 / 0 is "absent", matching every
  // other *_remaining field's own convention on this struct.
  double    burning_core_remaining  = 0.0;    // tier-set proc window (buff, source == a->player)
  int       lightning_rod_stacks    = 0;      // Stormbringer debuff (buff, source == a->player)
  double    lightning_rod_remaining = 0.0;
  double    venomfang_remaining     = 0.0;    // trinket-sourced dot (source == a->player)
  int       venomfang_debuff_stacks = 0;      // trinket-sourced buff (source == a->player)
  double    venomfang_debuff_remaining = 0.0;
  double    rune_of_unleashed_fire_lingering_remaining = 0.0;  // omnium-sourced dot (source == a->player)
};

// The four-clause generic filter (D-09), checked in the SAME order action_t::target_ready checks
// them (alive, immune-while-harmful, reach, front) -- generic_filter and target_ready must never
// disagree about what counts as reachable, or the mask and the cast drift apart (T-228-02-01).
// `harmful` is a real parameter, not read off `a`, so a future non-harmful targeted action does
// not silently skip the immunity clause by construction (R-E's guard, mirrored here).
bool generic_filter( const action_t* a, player_t* candidate, bool harmful );

// Builds the fact record for one candidate against the given resolved action. OBS-01/R5-5
// (tstl-sylvanas 232-02) dropped the `previous_pick` parameter entirely -- the sticky tie-break
// that consumed it is gone from select() (score -> current target -> stable identity), and no
// other field on enemy_fact ever depended on the action's own prior pick.
enemy_fact build_enemy_fact( const action_t* a, player_t* candidate );

// 228-07 (TGT-07, D-21): builds the FULL candidate set for one targeted action -- every enemy
// `generic_filter` would pass -- as complete `enemy_fact` records, in
// `sim->target_non_sleeping_list` order. Reuses `generic_filter`/`build_enemy_fact` VERBATIM (the
// exact functions `select()` itself calls) so this exported list can never drift from the
// selector's own real candidate set (D-20: no third copy of the filter). Exists so a caller
// outside this module (the decision dump, 228-07's parity harness field) can hand the SAME
// per-enemy numbers the fork's own preference sees to an external reimplementation of the rules,
// rather than the reimplementation trusting only the fork's OWN chosen pick.
std::vector<enemy_fact> build_candidate_facts( const action_t* a, bool harmful );

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

// WR-07 (260902/cr4): the ONE retarget function `accept_cast` (solver_control.cpp) and the
// mid-cast re-resolution ladder's fallback arm (action.cpp) both call, so the two can never drift
// apart when the ladder gains a third arm. Sets `a->target` (via `set_target`, never a raw
// `a->target = pick` write -- that skips the AoE target-cache invalidation), `p->target`, and both
// weapon attacks to `pick`. Does NOT turn the player -- that stays a separate `p->face()` call at
// each caller for this task (CR-04, a later gated task in this same quick-task plan, is what folds
// the turn into this function too).
void retarget( action_t* a, player_t* p, player_t* pick );

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

// Read-only query of the CURRENT decision stamp for `p` (the value `begin_decision` most recently
// returned for this player), or 0 if no decision has ever been stamped for them. Does NOT bump the
// counter. 260902/cr4 (CR-02): used by rl_policy_obs.cpp's non-boundary read_action_gate_bits call
// (decision_dump::record(), which always runs AFTER solver_control::choose() has already
// retargeted/turned the player for THIS decision) to look up its cached PRE-decision gate-bit
// arrays by stamp, rather than trusting "an entry exists" alone.
std::uint64_t current_decision_stamp( const player_t* p );

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

// 230-04 (SCOR-02, R-B): the per-decision candidate block select() CAPTURED for `resolved` when
// the scorer preference was active -- the feature block preference_scorer actually scored for
// each candidate (CAPTURED at the moment it scored it, never recomputed here, mirroring
// lookup_pick's own D-12 discipline), the mask of which slots were real, the live candidate
// count, and the slot the pick actually took. `features` points at
// `RL_TARGET_SLOTS * RL_TARGET_FEATURES` floats, slot-major, owned by this module and valid only
// until the NEXT call into this module for the SAME action -- copy out before that if the caller
// needs to keep it (rl_translog::record_decision's own contract: memcpy's it into the row
// immediately, never retains the pointer).
struct candidate_block
{
  const float*  features    = nullptr;
  std::uint16_t mask        = 0;
  std::uint8_t  count       = 0;
  std::uint8_t  chosen_slot = 0xFFu;  // mirrors rl_translog::CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK
};

// Reads the candidate block stamped for `resolved` at the CURRENT decision. `*out_found` is
// false when no block was captured this decision (a wait, an untargeted cast, or the rules path
// was active -- the scorer never ran for this action this decision) -- the caller must then pass
// a null candidate block to rl_translog::record_decision, never fabricate one.
candidate_block lookup_candidate_block( const action_t* resolved, bool* out_found );

// 232-04 (OBS-02, R-T): the pre-cast snapshot for the two decision-instant values
// `decision_dump.cpp`'s own diagnostic build_obs() call would otherwise recompute AFTER
// `accept_cast`'s retarget/turn has already mutated `p->target`/facing -- `is_current_target`
// (all eight targeted actions) and Tempest's own `hit_damage`. Filled from
// `rl_policy_obs.cpp`'s build_obs(), gated on `rl_state_t::is_decision_boundary` so that later,
// non-boundary call can never overwrite a real decision's capture (mirrors `g_pick_table`'s own
// stamp discipline). `has_is_current_target`/`has_hit_damage` are independent: most targeted
// actions only ever populate the first.
struct target_fact_snapshot
{
  bool   has_is_current_target = false;
  bool   is_current_target     = false;
  bool   has_hit_damage        = false;
  double hit_damage            = 0.0;
};

// Stamps `resolved`'s pre-cast is_current_target for the CURRENT decision. Called ONLY from
// build_obs()'s real-decision path (is_decision_boundary == true) -- see target_fact_snapshot's
// own doc comment above for why a non-boundary call must never reach this function.
void stamp_target_fact_is_current_target( const action_t* resolved, bool is_current_target );

// Same contract as stamp_target_fact_is_current_target, for Tempest's own hit_damage leaf (the
// only registry action whose schema requests a shared_hit_damage leaf today).
void stamp_target_fact_hit_damage( const action_t* resolved, double hit_damage );

// Reads the snapshot stamped for `resolved` at the CURRENT decision. `*out_found` is false when
// the stamp is stale or absent -- the caller (decision_dump.cpp) MUST then compute fresh and flag
// the row (`target_fact_dump_time_compute`), never fabricate a value (T-232-14). The returned
// struct's own has_is_current_target/has_hit_damage flags are independent of `*out_found`, so a
// found-but-partial slot reports each half honestly.
target_fact_snapshot lookup_target_fact_snapshot( const action_t* resolved, bool* out_found );

// 230-04 (SCOR-02, R-B): applies the SECOND exploration dial's replacement pick -- the ONLY
// mutator of an already-stamped pick, called from solver_control.cpp's cast branch AFTER the
// action has been chosen (never from inside select()/fill_pick, which fill gate bits for every
// targeted spell every decision, not only the one actually cast). Overwrites BOTH the stamped
// pick (so accept_cast()'s own lookup_pick() call, and the mid-cast re-resolution ladder, see the
// REPLACED candidate, never the scorer's original one) and the candidate block's chosen_slot (so
// the transition log records what happened, never what was intended). Returns false, changing
// nothing, when no pick was stamped for `resolved` at the current decision -- mirrors
// lookup_pick's own staleness discipline; the caller must then leave the original pick and
// candidate block untouched.
bool apply_candidate_exploration( const action_t* resolved, player_t* replacement,
                                   std::uint8_t replacement_slot );

// True for exactly the eight targeted registry tokens this plan governs (stormstrike,
// lightning_bolt, chain_lightning, tempest, windstrike, lava_lash, voltaic_blaze,
// primordial_storm) -- matched against `resolved->name_str`, the SAME string
// `solver_control::resolve_action` matched to build `resolved` in the first place, so this can
// never drift from the registry's own token list. False for every self/ground/item action and for
// the two SHAPED actions (crash_lightning, sundering -- plan 228-03's, not this plan's, per the
// SHAPED constant in 228-02-PLAN.md).
//
// CR-05 (260902/cr4): this is a PURE NAME MATCH -- it says nothing about WHICH actor's action_t*
// was passed in. `ancestor_t` (the fork totem pet, sc_shaman.cpp) constructs its own
// `chain_lightning_t` with the SAME name_str ("chain_lightning") as the RL player's spell, so this
// predicate alone is TRUE for the pet's action too. Every caller that uses this predicate to decide
// whether the SELECTOR governs an action_t* MUST additionally scope to the RL-controlled actor
// (exact strcmp against RL_ACTOR_NAME, never a prefix -- a prefix also matches every pet record),
// non-pet, non-background -- see rl_policy_obs.cpp's read_action_gate_bits substitution and
// solver_control.cpp's accept_cast for the two call sites that now do this. The mid-cast
// re-resolution ladder (action.cpp) additionally no longer calls this predicate at all -- it keys
// on `lookup_pick()`'s own found/not-found answer instead, which cannot collide by name because a
// pet's chain_lightning action_t* is never stamped in the first place once the actor scope above
// is enforced.
bool is_targeted_action( const action_t* resolved );

// 228-09 (D-23/TGT-08, dump half): read-only access to the SAME eight-token list
// `is_targeted_action` matches against (TARGETED_TOKENS, rl_target_select.cpp, anonymous
// namespace -- not otherwise reachable outside this translation unit). Exists so a caller in a
// DIFFERENT translation unit (decision_dump.cpp's per-decision pick dump) can walk the exact
// registry this module governs without hand-duplicating the token list and risking drift from
// is_targeted_action's own definition. `targeted_action_token_count()` is the array's length
// (8 today); `targeted_action_tokens()` returns the array's base pointer, tokens in the SAME
// order TARGETED_TOKENS declares them.
std::size_t targeted_action_token_count();
const char* const* targeted_action_tokens();

// Dispatches a resolved targeted action to its own preference function by name_str. Returns
// nullptr for anything is_targeted_action() would refuse (never called in that case by
// read_action_gate_bits, but kept total rather than partial for safety).
preference_fn preference_for( const action_t* resolved );

// ---------------------------------------------------------------------------------------------
// The eight preferences (D-10, R-B). Exposed individually (rather than only through
// preference_for) so the agreement probe and any future caller can name one directly.
// ---------------------------------------------------------------------------------------------

// stormstrike, windstrike, primordial_storm, lightning_bolt: OR-1 (owner ruling 2026-09-02,
// QUESTIONS Q15 alternative (b), 228-04 Task 1 Step 0c) -- SHORTEST time to die among candidates
// that outlive the cast (D-15: time_to_die - cast_time > 0), superseding the pre-228-04 longest-
// lived rule (renamed from the pre-228-04 longest-lived-preferring function of the same shape; ledger P228-7/R-B and P228-24 are
// superseded). R-B's consequence INVERTS: under the rig's fixed_time=1, every boss-type enemy's
// time_to_die is the fight clock with no per-actor term (research 3.3), which is now the LOWEST
// possible score under the shortest-lived ordering -- this preference now parks on the shortest-
// lived valid add and falls back to the boss only when no add passes generic_filter (the opposite
// of 228-SELECTOR-RECEIPT.md's recorded pre-228-04 behaviour). If NO candidate outlives the cast,
// this still returns an ordered, non-null answer (never "invalid") by falling back to plain
// time_to_die ordering among the generic-filtered set -- the same "never invalid" reasoning D-10
// states explicitly for Voltaic Blaze, applied here as a 228-02 ledger row since D-10's own text
// does not spell out this particular sub-case.
double preference_shortest_time_to_die( const action_t* a, const enemy_fact& fact );

// lava_lash: among Flame Shock carriers in reach, the SHORTEST Flame Shock remaining, else the
// longest-lived -- copies the STRUCTURE of the fork's own shortest_duration_target()
// (sc_shaman.cpp:6877-6911), not a re-invented walk.
double preference_lava_lash( const action_t* a, const enemy_fact& fact );

// voltaic_blaze: a candidate WITHOUT Flame Shock, longest-lived; falls back to the longest-lived
// carrier when every candidate carries it (never invalid -- the same reason the always-legal wait
// exists).
double preference_voltaic_blaze( const action_t* a, const enemy_fact& fact );

// chain_lightning (RULE-02, 232-06, R-Z): reads the greedy hop-count stash `select()` computes
// once per decision (compute_chain_hop_counts, rl_target_select.cpp) BEFORE this function is ever
// called -- the hop count is a REAL simulation of the engine's own chain walk over already-resolved
// geometry (pure, deterministic -- never a call into sc_shaman.cpp's own randomised chain resolver,
// FORK-03), tie-broken on time to die. Falls back to the retired neighbour-count approximation,
// visibly (chain_hop_fallback_used(), below), only if the stash carries no entry for this exact
// (action, candidate, decision) -- see this function's own body for when that can happen.
double preference_chain_lightning( const action_t* a, const enemy_fact& fact );

// RULE-02 (232-06, R-Z): true iff, for `resolved`'s CURRENT decision, preference_chain_lightning
// (above) had to fall back to the plain neighbour-count expression because the hop-count stash
// carried no entry for the candidate it was asked to score. Read by decision_dump.cpp (232-06 Task
// 3) so a fallback is visible on the dump row, never silent -- a silent fallback would look like a
// rule disagreement in the parity join (T-232-2x). False when no stash entry exists at all for
// `resolved` this decision (the SAME has_stamp/stamp==current staleness guard lookup_pick and its
// siblings already use).
bool chain_hop_fallback_used( const action_t* resolved );

// tempest: the addon's 8-yard cluster-centre rule -- most neighbours within its own resolved
// radius (measured 8.0), tie-broken on time to die. Same formula as chain_lightning's, applied to
// a different action's own radius -- see the neighbour-count field's own comment on enemy_fact
// (above) for why this plan does not split the computation.
double preference_tempest( const action_t* a, const enemy_fact& fact );

// Phase 230-02 (SCOR-01, D-01/D-02): the learned scorer -- a NINTH preference, same
// function-pointer signature as the eight above, registered through `preference_for`'s own
// by-name dispatch rather than a separate call path. Reads the scorer out of
// `a->player->sim->solver_policy_weights` (refuses by assertion if `has_scorer` is false --
// `preference_for` only ever returns this pointer when the loaded weights actually carry one),
// fills the SAME file-static feature buffer every call reuses
// (`rl_policy::rl_scorer_t::feature_scratch`, sized at load) from `fact`'s fields in
// `target_features.py`'s declared order (mirrored here field-for-field, `rl_target_select.cpp`'s
// own comment states the order explicitly) plus the eight-wide aiming-spell one-hot
// (`targeted_action_tokens()`'s own order, CK1-1), and returns `rl_policy::forward_scorer`'s one
// number. Higher wins, matching every other preference's own contract -- `select()` never knows
// this preference is anything but a ninth ordinary one.
double preference_scorer( const action_t* a, const enemy_fact& fact );

// ---------------------------------------------------------------------------------------------
// Mid-cast re-resolution counters (D-14, TGT-03). action_execute_event_t::execute() (action.cpp)
// records which arm of the fallback ladder fired for every targeted action's execute event, under
// an RL-controlled sim only (the scripted APL arm never enters this path -- see that call site's
// own comment for the exact gate). Exposed here, read by 228-SELECTOR-RECEIPT.md's own tooling.
// WR-11 (260902/cr4): cleared every iteration by the new `reset( sim )` hook below, called from
// `sim_t::reset()` -- genuinely one fight's totals now, not a whole-run accumulation (the prior
// comment's "never reset mid-run" contradicted its own "(one fight's totals)" label; this fixes
// the contradiction by making the behaviour match the label, not the other way around).
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

// CR-04 (260902/cr4): "decisions where every targeted action was illegal" -- the deadlock state
// CR-04's review finding named (an agent with `facing_enabled=1` and no legal move at all,
// silently). Required under every one of the RULING's three alternatives (turn / explicit turn
// action / accept-the-deadlock) so the state is visible in the census instead of silent. Bumped
// ONCE per decision BOUNDARY (never per targeted action) from read_action_gate_bits
// (rl_policy_obs.cpp) when NONE of the RL-controlled actor's registry actions read `ready` this
// decision. Never reset mid-fight independently -- cleared by the SAME `reset( sim )` hook as the
// other three module globals (WR-11), so it is also genuinely one fight's total.
void record_every_targeted_action_illegal();
std::uint64_t get_every_targeted_action_illegal_count();

// WR-11 (260902/cr4, mirrors decision_dump.cpp's own g_action_handle_cache fix): clears all three
// module globals (the decision-stamp table, the per-decision pick table, and the re-resolution
// counters) -- called from the engine's own per-iteration reset (`sim_t::reset()`, sim.cpp,
// alongside `raid_event_t::reset( this )`). A stale pick or stamp from a PRIOR iteration must never
// leak into the next one; the getter above is therefore now genuinely "one fight's totals" (the
// mirrored doc comment on `reresolution_counts` no longer needs the "never reset mid-run" claim
// that used to contradict its own "(one fight's totals)" parenthetical).
void reset( sim_t* sim );

// ---------------------------------------------------------------------------------------------
// Shaped spells (228-03, TGT-01/D-08, R-A / P228-6). Crash Lightning and Sundering pick a
// DIRECTION to face, not a cast target -- the "pick" is the enemy whose direction, if faced,
// puts the most enemies inside the spell's own shape. ONE named constant pair per spell, here,
// so `sc_shaman.cpp`'s per-action target filter callback and the two shaped preferences below
// (and nowhere else in the fork) share EXACTLY one copy of each number -- the "no second copy of
// the cone angle" prohibition applies to this file too, not only to the addon-vs-fork pair.
//
// Crash Lightning: 8-yard radius (spell data, `action_t::radius`, matches the addon's
// CRASH_LIGHTNING_CONE_RADIUS_YARDS); 120-degree full cone / cos(60) = 0.5 half-angle -- SOLE
// SOURCE is the addon's exported CRASH_LIGHTNING_CONE_COS_HALF_ANGLE
// (enhancementShamanContext.ts:222-224); this binary's own `spell_query=spell.id=187874` ALSO
// now prints "Cone Angle : 120 degrees" directly (a corroborating, not a competing, source --
// the addon constant is still cited as the sole source per D-08/R-A's own wording; the receipt
// records the corroboration).
constexpr double CRASH_LIGHTNING_CONE_RADIUS_YARDS   = 8.0;
constexpr double CRASH_LIGHTNING_CONE_COS_HALF_ANGLE = 0.5;

// Sundering: 11 yards long (spell record effect radius), 4.5 yards WIDE, read as the FULL width
// (QUESTIONS Q14) -- half-width 2.25 yards either side of the facing axis.
constexpr double SUNDERING_RECT_LENGTH_YARDS     = 11.0;
constexpr double SUNDERING_RECT_HALF_WIDTH_YARDS = 2.25;

// 228-10 (D-16 aggregates + shaped block, orchestrator default -- see the ledger): the ONE named
// threshold both the identity-free aggregates' "dying within 15s" counter and the shaped block's
// "long-lived" counter share -- D-16 names both fields but not a numeric boundary, so this plan
// reuses the SAME number for both rather than inventing a second, undeclared one. Header-level so
// both rl_policy_obs.cpp (the aggregates) and this file's own shape facts read the identical
// constant across translation units.
constexpr double DYING_WITHIN_LATER_SECONDS = 15.0;
constexpr double DYING_WITHIN_SOON_SECONDS  = 5.0;

// 228-10 (D-16 aggregates): the two RAW distance thresholds the identity-free aggregates use
// ("within 8 yd", "within 40 yd" -- plain distance, no combat_reach allowance, since these are
// visibility-window counts, not legality tests). "In melee" uses MELEE_RANGE_YARDS + the
// candidate's own combat_reach, matching DISCUSSION-targeting-rules-and-fields.md 2.2's own
// "inside 5 yards + its hitbox" wording.
constexpr double MELEE_RANGE_YARDS      = 5.0;
constexpr double NEAR_RANGE_YARDS       = 8.0;
constexpr double VISIBILITY_RANGE_YARDS = 40.0;

// WR-02 (260902/cr4): the candidate's own `bounding_allowance` (combat_reach) is a hitbox
// extension of the CANDIDATE's position, not of the spell's own width -- so both shapes apply it
// the SAME way: on the cone's radius (crash_lightning_cone_contains, unchanged) and on the
// rectangle's ALONG axis (the length the candidate's hitbox can poke past either end), but
// DROPPED from the rectangle's PERPENDICULAR axis (sundering_rect_contains no longer widens
// SUNDERING_RECT_HALF_WIDTH_YARDS by it). `shape_hitset_agreement.py`'s independent
// `_rect_contains` copy moves with this convention -- see that probe's own comment.

// Pure geometry predicates -- plain doubles only, no engine pointer (R-D: deterministic
// geometry, never a target-cache read). `(px,py)` is the player's position, `(fx,fy)` a UNIT
// facing vector (real, from `player_t::facing_x/y`, OR a hypothetical direction the shaped
// preference is trying), `(cx,cy)` the candidate's position, `bounding_allowance` the
// candidate's own `combat_reach` (both shapes' spell records carry "Add Target (Dest) Combat
// Reach to AOE" -- the allowance is real, not invented).
bool crash_lightning_cone_contains( double px, double py, double fx, double fy, double cx, double cy,
                                     double bounding_allowance );
bool sundering_rect_contains( double px, double py, double fx, double fy, double cx, double cy,
                               double bounding_allowance );

// OR-2 (owner ruling 2026-09-02, QUESTIONS Q1, 228-04 Task 1 Step 0b): the two shaped
// preferences that used to live here (`preference_shaped_crash_lightning`,
// `preference_shaped_sundering`) are REMOVED -- Crash Lightning and Sundering never pick a
// direction, they cast in the CURRENT facing. The geometry predicates above stay as the one
// shared copy the shaman module's AoE hit filters read from.

// 228-10 Task 1 Step 5 (D-16 shaped-spell block): the shape-fact COMPUTATION itself lives in
// rl_policy_obs.cpp / namespace rl_policy (rl_policy::compute_crash_lightning_shape /
// compute_sundering_shape) -- it is the OBSERVATION writer's own concern, not the selector
// module's, and this plan's own verify gate checks that those two computations call the
// geometry predicates directly from rl_policy_obs.cpp, never through a second wrapper here. The
// geometry predicates above (crash_lightning_cone_contains / sundering_rect_contains) remain the
// ONE shared copy both that computation and sc_shaman.cpp's own AoE hit filters call.

} // namespace rl_target_select
