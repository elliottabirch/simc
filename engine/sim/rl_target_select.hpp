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

// tstl-sylvanas 262-04 (IN-02; R-6, R-7): THE one predicate every enemy-list walk asks before it counts or lists an enemy.
// False for a sheet-fight hazard actor (a floor patch or ghost is not an enemy to hit); and, ONLY under fight_style=SheetFight,
// also false for an invulnerable actor (a boss benched at 1 health between its lives is out of the fight). The immunity
// half is gated on the style on purpose: the existing shapes use invulnerable raid events and must stay byte-identical.
// Out of line (rl_target_select.cpp) so this header keeps its forward declarations only.
bool rl_counts_as_enemy( const player_t* t );

// ---- Phase 266 (plan 266-01, funnel mode, owner F9): the chosen enemy (the TAG) and its chooser ----
//
// The tag is the field `player_t::rl_chosen_enemy`, written by exactly ONE function, `refresh_chosen`
// (the chooser), so a later chooser (a raid marker, a name, a buff, a fight-sheet priority mark) plugs in
// HERE and nowhere else. In this plan nothing in play reads the tag (it is bookkeeping for the chosen
// copy in rl_credit_route); the chooser picks at the pull, for the RL actor only.

// The chosen enemy of `p`, resolving a pet or guardian to its owner's tag (so a pet's hit compares
// against its owner's tag). nullptr when there is none (a non-RL-actor player never gets one).
player_t* rl_chosen_enemy_of( const player_t* p );

// "Can be hit": `rl_counts_as_enemy( t )`, awake, not invulnerable, not an untargetable enemy
// (hazard or bystander), and legal under `generic_filter` for p's own Lightning Bolt (harmful). Throws
// by name when p has no lightning_bolt action. Reuses the two existing helpers; never a third copy.
bool rl_can_be_hit( const player_t* p, const player_t* t );

// The chooser. Keeps the tag while it can be hit; otherwise, if some enemy can be hit, picks the first
// in `sim->target_non_sleeping_list` order among those also legal for p's Stormstrike (else the first
// that can be hit); with nothing hittable the old tag is kept. REFUSES (throws) any player that is not
// the RL actor: every call site must carry the RL-actor gate itself.
void refresh_chosen( player_t* p );

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
  bool      hazard{ false };                  // tstl-sylvanas 262-04 (R-6): the actor's own sheet-fight hazard flag (floor patch, ghost). Always false by construction (262-09, review IN-01): every candidate that reaches build_enemy_fact has already passed rl_counts_as_enemy, which refuses a hazard, so a hazard is never a target and never counted. Kept as documentation of that rule, not as a net column (R-6: a per-spell pick never sees a hazard, so it would be a constant zero). The brace form dates from target_features_derive.py, which parsed every `name = default;` line of this struct as a per-target feature; Phase 259 deleted that script (feature names now come from the registry), so `= false` would be safe too.
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

  // 260914-rbp Task 1 Step 6 (R15, rulings Q16): the targeting-lens fields -- per-candidate
  // counts the dormant Phase-230 scorer's own feature contract (fill_candidate_features,
  // rl_target_select.cpp) did not carry before this task, appended in EXACTLY this order (never
  // inserted mid-struct -- that would silently reorder every existing scorer feature index).
  // `chain_hop_count` reads 0 whenever `a` has no chain-hop stash entry for THIS decision
  // (NOTE-1, Task 2c: chain_hop_count_for_start's own stash-and-stamp check is the gate, not a
  // name comparison against `a` -- a Thorim's-routed melee strike that compute_chain_hop_counts
  // resolved geometry for gets its real hop count exactly like a literal chain_lightning cast
  // does); the other two are computed for EVERY candidate regardless of the calling action `a` --
  // they are properties of the CANDIDATE relative to this actor's own Voltaic Blaze / Lava Lash
  // geometry, not of `a`.
  int       chain_hop_count                 = 0;  // this candidate's OWN greedy chain-hop count, treated as the walk's START (compute_chain_hop_counts, read via the SAME per-decision stash preference_chain_lightning reads -- chain_hop_count_for_start, below)
  int       vb_new_flame_shocks_within_10yd = 0;  // (+1 if THIS candidate itself lacks the Flame Shock, Task 2b A2 correction -- "a splash always hits its centre") enemies within Voltaic Blaze's own resolved cleave radius (+reach) of THIS candidate that lack this actor's Flame Shock, capped at the cleave's own resolved aoe -- HIT-INPUTS-DESIGN.md ss2.2(b)'s formula, centred on the candidate instead of the stamped pick
  int       lava_lash_spread_within_12yd    = 0;  // enemies within Lava Lash's own resolved spread radius (+reach) of THIS candidate that lack this actor's Flame Shock, capped at Molten Assault's own resolved spread cap, 0 unless talent.molten_assault.ok() -- HIT-INPUTS-DESIGN.md ss2.3's lava_lash formula, centred on the candidate
};

// 260914-rbp Task 1 (R5, HIT-INPUTS-DESIGN.md ss2.1): reads the SAME per-decision greedy
// chain-hop stash preference_chain_lightning() itself reads (RULE-02, rl_target_select.cpp) for
// ONE specific start candidate -- the stamped PICK a targeted-action observation leaf
// (hits.chain_lightning) needs, and the per-candidate enemy_fact::chain_hop_count field above,
// never a fresh compute_chain_hop_counts() walk (D-20: the stash IS the walk, filled once per
// decision by select()'s own pref==preference_chain_lightning branch every decision
// read_action_gate_bits calls fill_pick() for the registry's chain_lightning token). Returns 0
// when no stash entry is current for `resolved` this decision (a stale or never-filled stash) or
// `start` has no entry in it -- the SAME "no pick => 0" contract every hits.* leaf's own binding
// comment documents, never a fallback to the retired neighbour-count approximation
// preference_chain_lightning uses internally for ITS OWN staleness case (an observation leaf
// reporting a stale value would be worse than reporting 0).
int chain_hop_count_for_start( const action_t* resolved, const player_t* start );

// 260914-rbp Task 1 (R9/R15): the Voltaic Blaze cleave / Lava Lash spread geometry this file's
// own per-candidate targeting-lens fields (enemy_fact::vb_new_flame_shocks_within_10yd/
// lava_lash_spread_within_12yd) and rl_policy_obs.cpp's own hits.voltaic_blaze.*/
// hits.lava_lash.flame_shock_spread direct_id scalars BOTH need -- resolved BY NAME from spell
// data (VB cleave radius/cap: voltaic_blaze_damage's own resolved radius/aoe, set by R13/hand
// respectively; Lava Lash spread radius: lava_lash's own resolved radius; Lava Lash spread cap:
// Molten Assault's own resolved effectN(2)), ONE definition shared by both callers (D-20's "no
// second copy" rule, applied across the module boundary). Cached per-actor (ACT-02 pattern,
// mirrors rl_policy_obs.cpp's g_action_handle_cache) since none of these values change during a
// fight -- resolved at most once per actor, never once per candidate or once per decision.
struct vb_lava_lash_geometry_t
{
  double vb_radius        = 0.0;  // voltaic_blaze_damage's own resolved radius (0 until R13 lands, or if untalented -- the action does not exist)
  int    vb_cap           = 0;    // voltaic_blaze_damage's own resolved aoe (0 if untalented/unresolved)
  double lava_lash_radius = 0.0;  // lava_lash's own resolved radius (12.0)
  int    lava_lash_cap    = 0;    // Molten Assault's own resolved effectN(2), 0 unless talent.molten_assault.ok()
};
const vb_lava_lash_geometry_t& resolve_vb_lava_lash_geometry( player_t* p );

// 260914-rbp Task 1 (R9); CORRECTED Task 2b (Q17 review, A2 -- HIT-INPUTS-DESIGN.md ss2.0 lines
// 315-316: "a splash always hits its centre; a cleave always hits its target"): returns
// `1 + (live enemies within `radius` (+ candidate's own combat_reach) of `candidate`)` -- the
// candidate itself is ALWAYS counted as a hit, matching the sim's own aoe accounting (a cleave's
// `aoe = 6` counts the primary target as one of the six). Every caller of this function (Voltaic
// Blaze cleave, Tempest, per-carrier Fire Nova) is exactly this "always hits its centre" shape;
// there is no caller that wants the OLD neighbours-only convention -- that convention lives on
// unchanged in build_enemy_fact's own `neighbours_within_radius` field (the target_facts.*
// one-hop proxy family, a SEPARATE computation, untouched by this correction) and in
// count_new_flame_shock_neighbours below (Lava Lash spread keeps its own centre-excluded
// convention -- the centre there is the SOURCE carrier, never a spread target). 0 when
// `radius <= 0.0` (an untalented/unresolved action) -- deliberately NOT `1` in that case, since a
// radius of 0 means the action itself does not exist on this build, not "cleave of one".
int count_hits_within_radius( player_t* caster, player_t* candidate, double radius );

// 260914-rbp Task 1 (R9); CORRECTED Task 2b (Q17 review, A2): counts live enemies within
// `radius` (+ combat_reach) of `candidate` that LACK `caster`'s own Flame Shock
// (candidate->find_dot("flame_shock", caster) -- the SAME caster-filtered read
// rl_target_select.cpp's own build_enemy_fact::flame_shock_remaining already uses, rulings Q6),
// PLUS one more if `include_pick` is true AND `candidate` itself lacks that Flame Shock --
// clamped at `cap` (the +1 counts toward the same cap, never added after clamping). Shared by
// every Flame-Shock-dependent geometry leaf this task adds: `include_pick = true` for Voltaic
// Blaze's new-Flame-Shocks count (its cleave always hits and can Flame-Shock its own pick,
// memo ss2.0/A.3); `include_pick = false` for Lava Lash's spread count (the pick is the SOURCE
// carrier that casts Lava Lash, never one of the enemies its spread plants a NEW Flame Shock on
// -- deliberately UNCHANGED by this correction). ONE definition for both conventions, per D-20.
// 0 when radius <= 0.0 or cap <= 0 (an untalented/unresolved geometry source).
int count_new_flame_shock_neighbours( player_t* caster, player_t* candidate, double radius, int cap,
                                       bool include_pick );

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

// tstl-sylvanas 260928-tb8: the observation-side "is this enemy a boss" bit. Non-boss mask for trash
// pulls (fight_style=TrashPack reads 0 for every enemy); every other fight style reads the engine's
// player_t::is_boss(). Engine mechanics keep player_t::is_boss() untouched.
bool obs_is_boss( const player_t* enemy );

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
// weapon attacks to `pick`. Does NOT turn the player -- no code path does anymore (R6-8,
// 233.1-01): the turn this comment used to describe (folded into this function by 260902/cr4)
// was deleted along with every other turn site in the engine.
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

// 230-04 (SCOR-02, R-B): the per-decision candidate block select() CAPTURED for `resolved` --
// 233.1-03 (R6-13, OV-5): captured on EVERY decision now (the rule runs and builds the table
// unconditionally, 240-05 Task 2). A full build_enemy_fact per candidate, built specifically for
// this capture (never the lite fact the decision was actually scored with) -- see
// rl_target_select.cpp's own comment at that call site. `run_target_head` (this file) reads this
// SAME captured block to score against, never recomputing it. The mask of which slots were real,
// the live candidate count, and the slot the pick actually took. `features` points at
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
  // 259-07 (R5): `rules_slot` is the slot the RULES picked (select()'s own pick -- never
  // overwritten by a head or the random-aim dial); `observed_slot` is the slot the observation's
  // target facts described for this action -- the rules' slot, except in `aim_obs_source = 1`
  // mode where the head's pick (before the dial) replaces it. `chosen_slot` stays "what happened"
  // (the dial and the head both overwrite it).
  std::uint8_t  rules_slot    = 0xFFu;
  std::uint8_t  observed_slot = 0xFFu;
};

// Reads the candidate block stamped for `resolved` at the CURRENT decision. `*out_found` is
// false when no block was captured this decision (a wait, an untargeted cast, or `select()` was
// never called for this action this decision -- 233.1-03: no longer scorer-only, select() now
// stamps a block on every preference) -- the caller must then pass a null candidate block to
// rl_translog::record_decision, never fabricate one.
candidate_block lookup_candidate_block( const action_t* resolved, bool* out_found );

// 232-04 (OBS-02, R-T): the pre-cast snapshot for the decision-instant value
// `decision_dump.cpp`'s own diagnostic build_obs() call would otherwise recompute AFTER
// `accept_cast`'s retarget/turn has already mutated `p->target`/facing -- `is_current_target`
// (all eight targeted actions). Filled from `rl_policy_obs.cpp`'s build_obs(), gated on
// `rl_state_t::is_decision_boundary` so that later, non-boundary call can never overwrite a
// real decision's capture (mirrors `g_pick_table`'s own stamp discipline).
// 260923-lrc (PLAN.md D11): has_hit_damage/hit_damage REMOVED -- they served the eight now-
// deleted action_leaves.*.hit_damage census leaves (R7-4: the live addon can never read a
// damage amount); stamp_target_fact_hit_damage() is REMOVED alongside them (it was the sole
// caller of calculate_direct_amount() under engine/sim/rl_*, rl_policy_obs.cpp).
struct target_fact_snapshot
{
  bool   has_is_current_target = false;
  bool   is_current_target     = false;
};

// Stamps `resolved`'s pre-cast is_current_target for the CURRENT decision. Called ONLY from
// build_obs()'s real-decision path (is_decision_boundary == true) -- see target_fact_snapshot's
// own doc comment above for why a non-boundary call must never reach this function.
void stamp_target_fact_is_current_target( const action_t* resolved, bool is_current_target );

// Reads the snapshot stamped for `resolved` at the CURRENT decision. `*out_found` is false when
// the stamp is stale or absent -- the caller (decision_dump.cpp) MUST then compute fresh and flag
// the row (`target_fact_dump_time_compute`), never fabricate a value (T-232-14). The returned
// struct's own has_is_current_target flag is independent of `*out_found`, so a
// found-but-partial slot reports each half honestly.
target_fact_snapshot lookup_target_fact_snapshot( const action_t* resolved, bool* out_found );

// 230-04 (SCOR-02, R-B), renamed 240-05 Task 2 (D1(a)/D8(a)): the ONE seam that mutates an
// already-stamped pick -- TWO callers now, never a third: `run_target_head` (below, the target
// head's own overwrite, called from `rl_policy_obs.cpp` immediately after the rule's fill_pick
// loop, BEFORE the observation is built) and the aim random-try exploration dial
// (`solver_control.cpp`'s cast branch, AFTER the action has been chosen). Neither call site is
// inside `select()`/`fill_pick` itself, which fill gate bits for every targeted spell every
// decision, not only the one actually cast/aimed. Overwrites BOTH the stamped pick (so
// accept_cast()'s own lookup_pick() call, and the mid-cast re-resolution ladder, see the REPLACED
// candidate, never the rule's original one) and the candidate block's chosen_slot (so the
// transition log records what happened, never what was intended). Returns false, changing
// nothing, when no pick was stamped for `resolved` at the current decision -- mirrors
// lookup_pick's own staleness discipline; the caller must then leave the original pick and
// candidate block untouched.
bool apply_head_pick( const action_t* resolved, player_t* replacement,
                       std::uint8_t replacement_slot );

// 240-05 Task 2 (D1(a)): re-aims one targeted spell for the CURRENT decision, using the loaded
// target head. Reads the candidate block `select()` already stamped for `resolved` this decision
// (via the SAME staleness-checked access `lookup_candidate_block` uses -- never recomputed, never
// an extra `build_enemy_fact` pass), scores every REAL slot with `rl_policy::forward_scorer` (the
// slot's 23 stored facts plus the eight-wide spell one-hot), picks the winner with the SAME tie
// ladder `select()` itself uses (one definition, two callers), and overwrites the stamped pick and
// chosen slot through `apply_head_pick` above. No-op -- the rule's own aim stands -- when `resolved`
// is null, when no head is loaded, or when no candidate block was captured this decision (an
// overflowing rules-path decision, or an action `fill_pick` never reached this decision): never
// throws, never truncates, and the skip is counted (`get_target_head_no_block_count`) so the
// fallback is measurable, not silent.
//
// 259-07 (R1, R6): `context` points at RL_AIM_CONTEXT_COUNT 0/1 indicators (the six context
// inputs of the head's 36-wide input, each `obs[RL_AIM_CONTEXT_OBS_SLOTS[j]] > 0`) -- evaluated by
// the caller: from the cached slot table BEFORE the observation is built in `aim_obs_source = 1`
// mode, off the built observation in `aim_obs_source = 0` mode. `observation_follows_head` is true
// only in the former: the head's pick is then also recorded as the block's `observed_slot` (the
// observation built right after describes it); false leaves `observed_slot` at the rules' slot.
void run_target_head( const action_t* resolved, const float* context, bool observation_follows_head );

// One fight's total of `run_target_head` calls that found no usable candidate block for their
// decision (see that function's own doc comment for the three cases) -- cleared by `reset( sim )`
// below, same one-fight-totals lifetime as every other counter in this file.
std::uint64_t get_target_head_no_block_count();

// True for exactly the nine targeted registry tokens this plan governs (stormstrike,
// lightning_bolt, chain_lightning, tempest, windstrike, lava_lash, voltaic_blaze,
// primordial_storm, flame_shock -- the ninth joined in 261002-8rv) -- matched against `resolved->name_str`, the SAME string
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

// 228-09 (D-23/TGT-08, dump half): read-only access to the SAME nine-token list
// `is_targeted_action` matches against (TARGETED_TOKENS, rl_target_select.cpp, anonymous
// namespace -- not otherwise reachable outside this translation unit). Exists so a caller in a
// DIFFERENT translation unit (decision_dump.cpp's per-decision pick dump) can walk the exact
// registry this module governs without hand-duplicating the token list and risking drift from
// is_targeted_action's own definition. `targeted_action_token_count()` is the array's length
// (9 today: flame_shock joined in 261002-8rv); `targeted_action_tokens()` returns the array's base pointer, tokens in the SAME
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

// chain_lightning (RULE-02, 232-06, R-Z; geometry parameterised 232-13/BL-01): reads the greedy
// hop-count stash `select()` computes once per decision (compute_chain_hop_counts,
// rl_target_select.cpp) BEFORE this function is ever called, now at the MODELLED spell's own
// resolved radius/cap (never the caller's) -- LO-05/R-D: this is a DETERMINISTIC APPROXIMATION of
// the engine's own randomised chain walk over already-resolved geometry (pure, deterministic --
// never a call into sc_shaman.cpp's own randomised chain resolver, FORK-03), labelled as an
// approximation, never a prediction of it (the engine's own walk draws from sim->rng(); this one
// never does, so it can only claim to model which targets a deterministic nearest-neighbour walk
// over the SAME geometry would hit, not which targets the engine's randomised resolver actually
// hits), tie-broken on time to die. Falls back to the retired neighbour-count approximation,
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

// Phase 230-02 (SCOR-01, D-01/D-02): the learned scorer used to be registered here as a NINTH
// preference, dispatched through `preference_for`'s own by-name switch. REMOVED 240-05 Task 2
// (D1(a)/D8(a)): the rule now runs on EVERY decision unconditionally (that is what makes the
// RULES comparator free -- see `preference_for`'s own comment), and the scorer's arithmetic moved
// to `run_target_head` (above), which scores the candidate table the rule already built instead
// of replacing the rule's own preference. `preference_for` no longer has a branch returning a
// scorer preference.

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
