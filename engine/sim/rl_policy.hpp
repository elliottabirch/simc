// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// In-process RL transport, Stage 1/2 seam (tstl-sylvanas phase 210, plan
// 210-04, XPORT-01/02/03/05). Pinned in 210-04-PLAN.md's "Pinned C++
// interface" section so plans 210-05/06/07 implement against it with no
// coordination round -- the SPLIT below is not negotiable, field names are
// as written there.
//
// NORMATIVE SOURCE: this file, `rl_policy_obs.cpp` and `rl_policy_net.cpp`
// are a C++ implementation of `scripts/rl/obs.py` and `scripts/rl/mask.py`
// (this repo, not the fork) -- those two Python modules are NORMATIVE; this
// C++ implements them, never the other way around (PITFALLS Pitfall 9(b)).
// The reciprocal "this Python is normative for the C++" comment on the
// Python side is plan 210-05's.
//
// Absent and zero are DIFFERENT everywhere in this schema (obs slot 1's
// missing value is 2.0, not 0.0) -- hence a has_* flag beside every
// optional field below. This is a plain-old-data seam deliberately: a
// standalone test executable (plan 210-07, D-05) that runs with no sim and
// no fight cannot call builders taking `player_t*` (a `player_t` requires a
// `sim_t` and full actor init), so `read_state(player_t*)` fills this POD
// and `build_mask`/`build_wait` remain PURE over it -- that purity is what
// Phase 211's differ exercises for those two.
//
// UPDATED (tstl-sylvanas phase 220, plan 220-04, OBS-01/OBS-02/OBS-07): the
// "Stage 2 is PURE over the POD" contract above was written for plan
// 210-07's standalone test executable, which was SUPERSEDED OUTRIGHT under
// ruling N-8 (rl_policy_obs.cpp's own read_first note says so) and will
// never be built. `build_obs` now ALSO reads the engine directly (a
// `player_t*` parameter, a `slot_table` of handles resolved once per actor
// at bind time) -- one census walk composes the observation vector AND its
// own name list by construction, rather than mirroring a Python-authored
// per-field table. `read_state`'s POD vectors (`s.buffs`, `s.cooldowns`,
// `s.swing_mh_remains`, `s.swing_oh_remains`, `s.gcd_remains`,
// `s.boundary_is_foreground`) are STILL REQUIRED -- `build_mask` and
// `build_wait` read every one of them and are untouched by this change.

#pragma once
#include "sc_enums.hpp"
#include "sim/rl_policy_constants_select.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct player_t;
struct sim_t;

namespace rl_policy
{
// Opaque to callers (tstl-sylvanas phase 220, plan 220-04, OBS-02/OBS-07):
// solver_control.cpp holds a `const slot_table&` and passes it straight
// through to build_obs -- it never inspects a slot_table's members, only
// bind_slots() and build_obs() (both declared below, both defined in
// rl_policy_obs.cpp) ever look inside one. Forward-declared here so the
// pinned interface stays a reference-only seam; the full definition
// (slot_binding's per-kind resolved handle, the per-slot composed name,
// the first-name-divergence slot) lives in the .cpp.
struct slot_table;
// ---- Stage 1 output / Stage 2 input: plain old data, NO engine types ----
// Absent and zero are DIFFERENT everywhere in this schema (obs slot 1's
// missing value is 2.0, not 0.0) -- hence a has_* flag beside every
// optional field.

struct buff_reading
{
  std::string name;                  // registry-supplied at lookup time, never a literal here
  bool   present   = false;
  double stacks    = 0.0;
  bool   has_stacks = false;
  double remains    = 0.0;  bool has_remains = false;   // quick task 260826-38t, D-2R slot 6
  bool   permanent = false;          // leaf null + sibling "permanent": true (obs.py:132-145)
  // 260922-mfh (D7): engine-truth stack cap, read from buff->max_stack() -- NEVER a literal --
  // so build_mask's wait-illegal-at-cap rule (rl_policy_obs.cpp) can compare against the real,
  // possibly-talented maximum instead of guessing. has_max_stacks is set true unconditionally
  // alongside has_stacks in read_state() (every live buff row on this walk has a well-defined
  // max_stack(), never absent) -- kept as an explicit flag anyway, same discipline every other
  // optional field on this POD follows.
  double max_stacks = 0.0;  bool has_max_stacks = false;
};

struct cooldown_reading
{
  std::string name;
  bool   present                = false;
  double remains                = 0.0;  bool has_remains                = false;
  double recharge_time          = 0.0;  bool has_recharge_time          = false;
  double charges                = 0.0;  bool has_charges                = false;
  double charges_fractional     = 0.0;  bool has_charges_fractional     = false;
  int    max_charges            = 1;
};

struct rl_state_t
{
  double t                   = 0.0;   // sim->current_time().total_seconds(); slot 4 derives from it
  double gcd_remains         = 0.0;   bool has_gcd_remains        = false;
  double swing_mh_remains    = 0.0;   bool has_swing_mh_remains   = false;
  double swing_oh_remains    = 0.0;   bool has_swing_oh_remains   = false;   // quick task 260826-38t, D-2R slot 8
  double active_enemies      = 0.0;   bool has_active_enemies     = false;

  // FORK-04b (260902/226-07) -- the re-ask-period cap on the unanchored
  // wait (build_wait's anchor.kind == none branch, rl_policy_obs.cpp).
  // Same PLAYER-scoped formulas decision_dump.cpp's gcd_length/
  // auto_attack_interval keys and the swing_cast_gcd_length/
  // swing_cast_auto_attack_interval obs leaves use -- read ONCE here in
  // read_state(), never re-derived at the obs-leaf switch or at the cap
  // site (the "one reader" rule this file's swing/gcd obs-leaf comment
  // already states). Both are unconditionally well-defined (0.0 is a
  // real "no main-hand weapon" reading for auto_attack_interval, not an
  // absence), so has_* is set true unconditionally in read_state() --
  // kept as an explicit flag anyway for the same reason every other
  // field on this POD carries one.
  double gcd_length          = 0.0;   bool has_gcd_length         = false;
  double auto_attack_interval = 0.0;  bool has_auto_attack_interval = false;
  bool   boundary_is_foreground = true;   // converted ONCE from execute_type by the caller

  // 232-04 (OBS-02, R-T, deviation Rule 3 -- see 232-04-SUMMARY.md): threaded straight from
  // read_state()'s own `is_decision_boundary` parameter (already required for every caller, see
  // that function's doc comment) so build_obs() can tell its own REAL-decision call
  // (solver_control.cpp, always true) apart from decision_dump.cpp's later diagnostic build_obs()
  // call for the SAME decision (always false, that file's own CR-02 comment) WITHOUT a second
  // parameter on build_obs() itself. This is what gates the pre-cast target-fact snapshot's
  // stamp-vs-read-back split in rl_policy_obs.cpp -- see target_fact_snapshot's own doc comment
  // (rl_target_select.hpp).
  bool   is_decision_boundary  = false;

  // 221-03 (ACT-05/ACT-06) -- the two anchored-wait clamp inputs. Filled
  // in read_state() from the engine's own definitions (fight_remains =
  // expected_iteration_time - current_time(); raid_event_next_in = the
  // SHARED next_raid_event_in() helper declared below, RAW, no
  // substitution). `has_raid_event_next_in == false` means "no raid event
  // pending" -- downstream this means NO CLAMP (waiting is unbounded by
  // raid events when none are pending), the opposite substitution Phase
  // 220 plan 05's OBSERVATION scalar applies on top of the SAME helper --
  // see that helper's own doc comment in rl_policy_obs.cpp for why the two
  // consumers deliberately differ.
  double fight_remains        = 0.0;   bool has_fight_remains        = false;
  double raid_event_next_in   = 0.0;   bool has_raid_event_next_in   = false;

  // Phase 268 plan 04 (G268-7, D5, FORK-04): the current value of the spec's own resource (RL_RESOURCE_NAME, parsed once by
  // rl_spec_resource() below), filled in read_state(). build_mask's resource_threshold wait rule reads it; `has_` is false when
  // the actor does not have that resource active.
  double resource_current     = 0.0;   bool has_resource_current     = false;

  // The removed turn-legality predicate field (R6-27, 233.1-01) was here -- deleted with the
  // whole action.

  // 228-10 (Q18, D-12 "read once, use twice") -- the SAME immunity-remaining value the dump's
  // aggregate `immunity_remaining` key reports, read here through the ONE shared
  // `compute_invulnerability_window()` function so build_wait's new unanchored-wait candidate
  // and the observation-facing number can never independently drift. `has_immunity_remaining`
  // is false whenever no invulnerability is currently active (the "nothing pending" case for
  // THIS field -- distinct from raid_event_next_in's own nothing-pending case above).
  double immunity_remaining   = 0.0;   bool has_immunity_remaining   = false;

  // 221-01 (ACT-02, Pattern 1), RECOMPOSED 260831-lg6 (D-1) -- the
  // engine-truth legality layer, action-id order, exactly RL_ACTION_DIM
  // wide. Filled by read_state() calling read_action_gate_bits() below
  // (the ONE computation both this POD and decision_dump::
  // write_state_fields() share). `build_mask` (still PURE over this POD,
  // no player_t*) treats these bits as AUTHORITATIVE for every
  // `kind == cast` candidate -- when present (ALWAYS, for this POD; there
  // is no absent-bits state here, unlike mask.py's Python side) they
  // alone decide the verdict, the declarative buff-gate/cooldown-row
  // rules are retired dead code kept as comments (see build_mask's own
  // R0 comment in rl_policy_obs.cpp for the full rationale). A `wait`
  // action's slot is left at its default 0 -- `build_mask` never reads it
  // (wait actions resolve to no `action_t*` and skip R0 entirely).
  std::uint8_t action_resolvable[ RL_ACTION_DIM ] = {};
  std::uint8_t action_ready     [ RL_ACTION_DIM ] = {};

  std::vector<buff_reading>     buffs;
  std::vector<cooldown_reading> cooldowns;

  const buff_reading*     find_buff( const char* name ) const;
  const cooldown_reading* find_cooldown( const char* name ) const;
};

// Phase 268 plan 04 (G268-7): the engine resource the spec header names (RL_RESOURCE_NAME), parsed once with
// util::parse_resource_type. Throws sc_runtime_error naming RL_REGISTRY_ID and the name when the engine does not know the
// name (RESOURCE_NONE), so a bad header is refused at the first bind, never read as an empty resource.
resource_e rl_spec_resource();

// 221-01 (ACT-02, Pattern 1) -- Stage 1, needs the engine (same tier as
// read_state, which calls this to fill its two POD arrays above). Exists so
// this ONE computation is shared by read_state (fills rl_state_t) AND
// decision_dump::write_state_fields (emits the same two arrays onto both
// the JSONL dump and the FIFO wire) -- never two independent walks that
// could drift (Pattern 3). Resolves each cast action's token through
// solver_control::resolve_action -- the SAME resolver accept_cast uses --
// so the bits this function computes and the FATAL gate `accept_cast`
// enforces can never disagree about which action_t* they mean.
//
// 260902/cr4 (CR-02): `is_decision_boundary` carries NO default value on this declaration --
// every call site states true or false explicitly, by design. Three call sites exist: read_state
// (this file's own caller, always the boundary), decision_dump::write_state_fields's FIFO
// REQUEST-LINE call (solver_control.cpp, also the boundary -- built before the reply/accept_cast
// have run), and decision_dump::record()'s own call (decision_dump.cpp, NEVER the boundary --
// record() always runs AFTER solver_control::choose() has already retargeted and turned the
// player for this decision). True bumps this player's decision stamp and fills a fresh pick for
// every targeted action, then CACHES the two output arrays keyed on that stamp; false returns the
// cached PRE-decision arrays instead of recomputing against state the cast already mutated -- or,
// when no cache exists for the current stamp (a scripted actor with decision_dump= and no solver
// arm never takes the boundary path), computes the plain gate bits without bumping the stamp and
// without filling any pick, and reports that via `out_used_dump_time_compute` below.
void read_action_gate_bits( const player_t* p, std::uint8_t out_resolvable[ RL_ACTION_DIM ],
                             std::uint8_t out_ready[ RL_ACTION_DIM ], bool is_decision_boundary,
                             bool* out_used_dump_time_compute = nullptr, bool boundary_is_foreground = true );

// 221-03 (ACT-05/ACT-06) -- the ONE shared raid-event walk, called from
// read_state() (this file), build_obs()'s raid_event_next_in leaf
// (rl_policy_obs.cpp, Phase 220 plan 05), and accept_wait()'s clamp
// (solver_control.cpp) -- three consumers, ONE walk, never a second/third
// independently-maintained loop over sim->raid_events (RESEARCH Pitfall 6:
// a naive expression-system read returns a ~9.2e12 saturation value rather
// than "no event"; this walks the sim's own raid_events list directly).
// Returns the RAW minimum candidate's seconds through out_seconds (a
// candidate survives the filter only when its own until_next() is
// strictly positive AND no greater than the remaining fight -- exactly
// the right filter for a CLAMP, and it discards the not-pending
// saturation value without ever naming it) plus a has-value flag; applies
// NO substitution and NO further clamp of its own -- "nothing pending"
// means out_seconds is left untouched and the function returns false.
// Each of the three callers applies its OWN policy on top of that boolean
// (see rl_state_t::has_raid_event_next_in's own doc comment above for the
// two deliberately-different policies).
bool next_raid_event_in( const sim_t* sim, double& out_seconds );

// ---------------------------------------------------------------------------------------------
// 228-10 Task 1 Step 3/4 (D-16/D-17/TGT-05/TGT-06). Two new engine-wide computations, each ONE
// function shared by every consumer (decision_dump.cpp's own aggregate keys AND, for the
// immunity window, read_state()'s own new rl_state_t::immunity_remaining field -- D-12's "read
// once, use twice").
// ---------------------------------------------------------------------------------------------

// The identity-free aggregates (D-16): fight-wide counts and timers over
// `sim->target_non_sleeping_list`, computed ONCE per call. `has_*` flags mirror this file's own
// convention -- "no enemies at all" is possible only in a degenerate/teardown state, but the
// flag is kept rather than assumed away.
struct fight_wide_aggregates_t
{
  int    enemies_total              = 0;
  int    enemies_in_melee           = 0;
  int    enemies_within_8yd         = 0;
  int    enemies_within_40yd        = 0;
  int    enemies_in_front           = 0;
  int    flame_shock_carrier_count  = 0;   // NEW counter -- walks DOTS over non-sleeping enemies,
                                            // never buff_list (the existing enemy_debuff_counts
                                            // counter can never see a dot -- P-5).
  int    lightning_rod_carrier_count = 0;  // 260923-lrc (PLAN.md D1-D3): walks BUFFS (not dots)
                                            // over non-sleeping enemies -- buff_t::find(t,
                                            // "lightning_rod", p) with check() > 0, mirroring
                                            // flame_shock_carrier_count's stateless walk one line
                                            // above but over the buff list instead of the dot
                                            // list. Same enemy set (target_non_sleeping_list)
                                            // trigger_lightning_rod_damage splashes damage to
                                            // (sc_shaman.cpp:3626), so this count equals the
                                            // number of targets the NEXT Lightning Rod pulse will
                                            // hit. NOT the existing enemy_debuff_counts counter
                                            // (add-blind, walks target_list -- misses raid-event
                                            // adds entirely, see compute_fight_wide_aggregates'
                                            // own note); NOT up() (mutates benefit bookkeeping).
  int    lashing_flames_carrier_count = 0; // 261002-8rs: enemies on the non-sleeping list carrying
                                            // THIS player's lashing_flames debuff (buff_t::find( t,
                                            // "lashing_flames", p ), check() > 0) -- the lightning_rod
                                            // idiom one field above, over the same enemy set.
  bool   has_soonest_time_to_die    = false;
  double soonest_time_to_die        = 0.0;
  bool   has_longest_time_to_die    = false;
  double longest_time_to_die        = 0.0;
  int    dying_within_5s            = 0;   // time_to_die <= 5.0 (AT OR BELOW -- stated identically
  int    dying_within_15s           = 0;   // to this counter's own 15s sibling, so the two can
                                            // never disagree about the crossing point)
  bool   has_nearest_enemy_distance = false;
  double nearest_enemy_distance     = 0.0;
};

fight_wide_aggregates_t compute_fight_wide_aggregates( player_t* p );

// TGT-06/D-17/Q18: the fight-wide invulnerability schedule, filtered from `sim->raid_events` by
// `type == "invulnerable"` -- COPIES `next_raid_event_in`'s own loop/discard-the-sentinel
// convention (never a second, independently-maintained walk), extended to also report the
// ACTIVE window's own remaining time via the matching event's `up()`/`remains()` (valid only
// while `up()`, per raid_event_t::remains()'s own precondition). `next_in`/`remaining` are 0.0
// when nothing is pending/active respectively -- the "nothing pending" wire value this plan's
// receipt records, matching every existing `*_remaining` field's own zero-is-absent convention
// on this codebase (never a saturation sentinel, never null).
struct invulnerability_window_t
{
  bool   has_next  = false;
  double next_in   = 0.0;
  bool   active    = false;
  double remaining = 0.0;
};

invulnerability_window_t compute_invulnerability_window( const sim_t* sim );

// 228-10 Task 1 Step 5 (D-16 shaped-spell block, TGT-04, OR-2): descriptive shape facts for the
// two SHAPED actions, computed from the CURRENT facing ONLY (there is no hypothetical direction
// to evaluate under OR-2). Both call the ONE shared geometry copy
// (rl_target_select::crash_lightning_cone_contains / sundering_rect_contains) against every
// live, non-sleeping enemy -- never a second copy, never a target-cache read (R-D). Lives here
// (the observation writer's own file/namespace), not in rl_target_select, because computing a
// descriptive fact about the CURRENT facing is this file's own concern -- the selector module
// governs picks, and the shaped actions have none (OR-2). "Long-lived" reuses the SAME
// threshold the identity-free aggregates' own dying-within-15s counter uses
// (rl_target_select::DYING_WITHIN_LATER_SECONDS) -- an orchestrator default, since D-16 names
// the field but not its numeric boundary; see the receipt/ledger for the row.
struct shape_hit_result_t
{
  int    enemies_hit           = 0;
  double summed_remaining_life = 0.0;
  int    long_lived_count      = 0;
};

shape_hit_result_t compute_crash_lightning_shape( player_t* p );
shape_hit_result_t compute_sundering_shape( player_t* p );

// WR-05 fix (210-CR-FIX): `source` is a `std::string`, not a `const char*`
// into shared storage. `wait_result` is part of the pinned POD interface --
// a `const char*` here previously pointed into a function-local `static
// thread_local std::string` inside build_wait() that the NEXT build_wait()
// call on the same thread overwrites (and may reallocate); safe only while
// exactly one `wait_result` is alive at a time and consumed synchronously.
// That invariant is not enforced by the type, so the first future caller to
// hold two `wait_result`s concurrently (Phase 212's logger, or any wait-arm
// caller beyond solver_control::choose()'s single current one) would dangle
// silently, with a plausible-looking string, since the storage is reused
// rather than freed. This is the wait arm, not the hot path -- one
// allocation here is fine; the invariant is worth removing entirely rather
// than documenting.
struct wait_result { double seconds = 0.0; std::string source = "floor"; bool floored = false; };

// ---- Stage 1: needs the engine. NOT exercised by the standalone test executable. ----
// `is_decision_boundary` (228-11 Task 2, closing a gap 228-09 disclosed but did not fix):
// threaded straight to this function's own internal read_action_gate_bits() call, which used to
// hardcode `true` unconditionally regardless of the caller's own context -- correct for
// solver_control.cpp's real per-decision call (always the boundary, pass true) but WRONG for
// decision_dump.cpp's diagnostic obs-vector block, which runs strictly AFTER the real decision
// already cast (pass its own is_decision_boundary, always false there) -- passing `true`
// unconditionally re-bumped the per-player decision stamp and re-selected every targeted
// action's pick via a fresh select() call, so the dump's own `obs` field could disagree with the
// translog's engine-built vector on any target_fact leaf sensitive to WHICH pick won (is_current_
// target/is_previous_pick/etc.) -- measured via dump_obs_equivalence.py on a real capture before
// this fix (38/176 target_facts slots + 4/49 action_leaves slots exceeding tolerance).
rl_state_t read_state( const player_t* p, bool boundary_is_foreground, bool is_decision_boundary );

// ---- Slot binding (phase 220, plan 220-04, OBS-02/OBS-07) ----
// bind_slots resolves every RL_OBS_FAMILIES member to an engine handle
// ONCE per actor (buff_t*/cooldown_t*/expr_t*/... -- see rl_policy_obs.cpp
// for the per-kind resolution and the file-static cache keyed on
// `const player_t*`) and returns a reference to the cached slot_table for
// `p`; a second call for the same `p` is O(1). The SAME walk that resolves
// each slot also composes that slot's name from the same family/member/leaf
// tables -- so the vector build_obs fills and the name list a caller can
// request via `rl_obs_names_out=` share one source of truth by
// construction, never a second hand-copied name list (OBS-02).
const slot_table& bind_slots( player_t* p );

// 259-07 (Q2, R3, R6): three seams the aim head shares with the observation so the two can never
// drift.
//
// apply_leaf_scale: the observation's ONE scaling rule (k_seconds with a clip -> clamp(raw, 0,
// clip)/clip; otherwise raw/div). The candidate table's facts are scaled through this same
// function (rl_target_select.cpp, fill_candidate_features) with the registry's own descriptor
// values, so the observation, the candidate table, the head and the trainer read identical
// floats. MOVED out of rl_policy_obs.cpp's anonymous namespace, arithmetic unchanged.
double apply_leaf_scale( double raw, rl_kind kind, bool has_div, double div, bool has_clip_div,
                         double clip_div );

// capability_effective_bit: capability `capability_index`'s EFFECTIVE bit (own detector AND every
// required capability) for actor `p`, memoised per actor -- the same value the observation's
// capability.<id> column and its governed-slot rebind read. Constant for the actor's whole
// fight, so the candidate table's gating costs one lookup per fact, never a detector walk.
bool capability_effective_bit( player_t* p, std::size_t capability_index );

// compute_aim_context_indicators (R6): the aim head's six context inputs for the CURRENT decision,
// `obs[RL_AIM_CONTEXT_OBS_SLOTS[j]] > 0 ? 1 : 0`, evaluated straight from the actor's cached slot
// table WITHOUT building the observation (mode (ii) runs the head BEFORE the observation is
// built). Only constant (capability) and buff (stacks/remains) bindings are supported -- every
// declared context slot is one of those; any other binding kind is refused by name.
void compute_aim_context_indicators( const player_t* p, float out[ RL_AIM_CONTEXT_COUNT ] );

// ---- Stage 2 ----
// build_mask/build_wait remain PURE over the rl_state_t POD (unchanged --
// 260831-mk7 does not touch this purity contract; it adds a PARAMETER to
// build_obs, it does not move any legality logic into build_mask).
// build_obs now ALSO reads the engine directly through `t`'s resolved
// handles and, for a `direct` binding, through `p` itself -- see this
// header's own updated "Stage 2" comment above.
//
// 260831-mk7 (D-1/D-3, mask-as-input): `mask` is the CALLER's own
// already-computed legality mask -- POST allow-list AND, POST the
// all-illegal refusal (solver_control.cpp's own read_state -> build_mask
// -> [allow-list AND] -> [all-illegal refusal] -> build_obs order, moved
// from the pre-mk7 read_state -> build_obs -> build_mask order). build_obs
// fills the trailing `legality` family's slots directly from `mask`, one
// float (0.0f/1.0f) per action index, resolved at bind time from the
// slot's own ordinal member name (rl_policy_obs.cpp's own bind-time
// dispatch) -- never a second legality computation, and never a reason to
// widen `rl_state_t`'s own POD (the mask is passed by reference, not
// copied into the state struct).
void        build_obs ( const player_t* p, const rl_state_t& s, const slot_table& t, const std::uint8_t mask[ RL_ACTION_DIM ], float out_obs[ RL_OBS_DIM ] );
void        build_mask( const rl_state_t& s, std::uint8_t out_mask[ RL_ACTION_DIM ] );
wait_result build_wait( const rl_state_t& s, const rl_wait_anchor& anchor );

// ---- Weights (RLW1 v2/v3, tstl-sylvanas Phase 210-04 -> Phase 213-TDL
// Task 1 -> Phase 222 NET-01/NET-02) ----
//
// Three body types share this same rl_layer/rl_weights_t plumbing -- see
// scripts/rl/agent/network_bodies.py (this repo's own Python counterpart)
// for the plain-language explanation of what each one computes. The layer
// split below is a DERIVED rule over n_layers (Phase 222 NET-01), not a
// fixed-depth lookup -- see hidden_layer_count() below, the ONE place this
// rule is written in this language:
//
//   - mlp:        n_layers >= 2. hidden = layers[0 .. n-2], out = layers[n-1].
//                 No gamma/beta anywhere. Byte-identical computation to the
//                 pre-v2 format at depth 2.
//   - dueling:    n_layers >= 3. hidden = layers[0 .. n-3]; v_head and
//                 a_head are layers[n-2]/layers[n-1], BOTH branching off the
//                 LAST hidden layer's output (never a further chain onto
//                 each other). Q = V + A - mean_legal(A) -- Phase 222
//                 NET-02: the mean is taken over LEGAL actions only
//                 (mask[o] != 0), falling back to the mean over every
//                 action when zero actions are legal (a background decision
//                 boundary can produce an all-illegal mask; see forward()'s
//                 own comment in rl_policy_net.cpp). The retired pre-NET-02
//                 all-actions mean formula does not appear in this file.
//   - ln-dueling: same layer split as dueling, but every HIDDEN layer
//                 (never v_head/a_head) carries a LayerNorm gamma/beta pair
//                 (applied after the linear layer, before ReLU) -- read
//                 from that layer's OWN has_ln flag (validated against the
//                 body's convention at load time), never from
//                 `body == ln_dueling` re-checked per layer; that is what
//                 makes forward() depth-agnostic instead of re-encoding the
//                 convention a third time.
// 246.1-05 (CAP-01/CAP-02): `grouped_ln_dueling` is body index 3 on the wire (RLW1 format 5,
// mandatory STRUCTURE section) -- scripts/rl/agent/network_bodies.py's `GroupedDuelingBody`. Every
// observation column a capability governs is gated (multiplied by that capability's own 0/1
// column) before five named blocks + a shared per-spell layer + the pass-through capability
// columns feed the SAME trunk + dueling combine `ln_dueling` already uses -- see
// rl_grouped_layout_t below for the split and rl_policy_net.cpp's grouped forward branch.
enum class rl_body_type
{
  mlp,
  dueling,
  ln_dueling,
  grouped_ln_dueling
};

// NET-01 (Phase 222): the ONE derived layer-split rule this language uses --
// called by both load_rlw1 (validation + scratch sizing) and forward() (the
// loop bound) in rl_policy_net.cpp, and exported here so a caller holding
// only a loaded rl_weights_t (the rl_forward_probe sim option, sc_main.cpp)
// can report a body's hidden layer sizes without re-deriving the rule a
// third time (222-RESEARCH.md "Anti-Patterns to Avoid": never re-derive the
// layer split in three places). n_layers must already satisfy the body's
// own minimum (load_rlw1 refuses otherwise); this function does not itself
// re-validate that.
std::size_t hidden_layer_count( rl_body_type body, std::size_t n_layers );
const char* body_name( rl_body_type body );

struct rl_layer
{
  std::uint32_t in_features = 0, out_features = 0;
  std::vector<float> weight;   // row-major [out][in], PyTorch's own layout
  std::vector<float> bias;     // [out]
  bool                has_ln = false;
  std::vector<float>  gamma;   // [out], only when has_ln
  std::vector<float>  beta;    // [out], only when has_ln
};

// Phase 259 (plan 259-05b, owner Q6): the RLW1 v6 AIM section -- a SECOND, independent parameter
// set (never a `body` variant: `body` selects a layer-SPLIT convention over the ONE rotation
// parameter set, the aim head is a wholly separate net). Replaces the retired v4 scorer section.
// Always mlp-shaped (`hidden_layer_count(rl_body_type::mlp, ...)`, reused verbatim -- never a
// second hidden convention): either 0 layers (a DIAL-ONLY section: it carries the random-aim rate
// and no head, so the fork aims by the rules and still fires the dial -- the rules comparison
// arm) or n_layers >= 2, every layer's has_ln == false, first layer's in_features ==
// `input_count + spell_count`, last layer's out_features == 1 (one score per candidate slot).
// `slots`, `input_count`, `spell_count` and `registry_sha` are the file's own declaration of what
// the head was built for; the loader refuses any that differ from the compiled RL_TARGET_SLOTS /
// RL_AIM_INPUT_COUNT / RL_AIM_SPELL_COUNT / RL_AIM_SHA (rl_policy_constants.h), by name.
// `obs_source` 0 = the spell net's target inputs describe the RULES' pick, 1 = the HEAD's pick.
struct rl_aim_t
{
  float          exploration   = 0.0f;  // the random-aim dial's rate -- finite, 0..1 (the BUTTON
                                         // dial's rl_weights_t::exploration is a separate field)
  std::uint32_t  slots         = 0;     // the head's declared candidate-slot count
  std::uint32_t  input_count   = 0;     // per-candidate input width (facts + context)
  std::uint32_t  spell_count   = 0;     // width of the aiming-spell one-hot
  std::uint32_t  obs_source    = 0;     // 0 rules, 1 head (see above)
  std::string    registry_sha;          // "aim-v1:<64hex>" -- cross-checked against RL_AIM_SHA at load
  std::vector<rl_layer> layers;         // empty (dial-only) or mlp-shaped, n_layers >= 2

  // Load-time-sized scratch (single-thread, no per-decision allocation): hidden_scratch[i] holds
  // aim hidden layer i's post-ReLU activations; feature_scratch holds the per-slot input buffers,
  // `RL_TARGET_SLOTS * (input_count + spell_count)` floats, sized once at load and reused every
  // scoring call.
  mutable std::vector<std::vector<float>> hidden_scratch;
  mutable std::vector<float>              feature_scratch;
};

// 246.1-05 (CAP-01/CAP-02): the RLW1 v5 STRUCTURE section's decoded shape -- present (non-default)
// only when rl_weights_t::body == grouped_ln_dueling. Mirrors scripts/rl/net_layout.py's own
// ResolvedLayout; this struct is populated from the BLOB's own STRUCTURE section at load time
// (the fork has no registry of its own -- n_obs/layout_sha are cross-checked at load against this
// build's live RL_OBS_DIM/RL_NET_LAYOUT_SHA, never re-derived here).
struct rl_grouped_layout_t
{
  std::uint32_t n_obs = 0;
  std::string   layout_sha;

  // gate_src[i] -- for observation column i, the capability column's OWN slot if governed, or
  // n_obs itself (the "point at an appended constant 1" sentinel, network_bodies.py's own
  // convention) if ungoverned. Size n_obs, built ONCE at load from the STRUCTURE section's
  // `gated` pairs (never per decision).
  std::vector<std::uint32_t> gate_src;

  // One slot list per grouped block, in the STRUCTURE section's own block order (net_layout.py's
  // blockOrder) -- block k's own `block_layers[k].in_features` must equal `block_slots[k].size()`.
  std::vector<std::vector<std::uint32_t>> block_slots;
  // n_repeats x n_facts matrix of slots -- the shared per-spell layer's own per-repeat input
  // gather; `spell_layer.in_features` must equal `per_spell[0].size()`.
  std::vector<std::vector<std::uint32_t>> per_spell;
  // The pass-through slots (today, the 17 capability columns) -- fed to the trunk raw (gated, but
  // through no block Linear/LayerNorm of their own).
  std::vector<std::uint32_t> passthrough;

  // Weights: one Linear+LayerNorm+ReLU per block, then ONE shared Linear+LayerNorm+ReLU applied
  // identically to every per_spell row -- parsed off the SAME wire layer list as
  // rl_weights_t::layers (layer order `[block_0 .. block_{G-1}, shared, trunk_0 .. trunk_{T-1},
  // v_head, a_head]`, rlw1.py's own `_validate_grouped_layers` docstring), split at load time so
  // the trunk + v_head/a_head remainder becomes rl_weights_t::layers itself -- forward()'s
  // existing trunk/dueling code (the ln_dueling shape) runs on it completely unchanged.
  std::vector<rl_layer> block_layers;
  rl_layer               spell_layer;

  // Load-time-sized scratch (never per decision -- same single-thread/no-per-decision-allocation
  // reasoning as rl_weights_t::hidden_scratch above). dot_fn needs a CONTIGUOUS input array
  // (rl_policy_net.cpp's own dot_fn/select_dot_fn comment), so each block/the shared per-spell
  // layer first GATHERS its own (non-contiguous) slots out of gated_scratch into its own small
  // contiguous buffer before the dot product runs -- block_gather_scratch/spell_gather_scratch
  // below are exactly that buffer, one per block plus one reused across spell repeats (spell rows
  // are processed sequentially, so a single reusable buffer is correct and cheaper than one per
  // repeat).
  mutable std::vector<float>              gated_scratch;          // size n_obs -- x[i] * gate value
  mutable std::vector<std::vector<float>> block_gather_scratch;   // one per block, size block_slots[k].size()
  mutable std::vector<std::vector<float>> block_scratch;          // one per block, post-ReLU
  mutable std::vector<float>              spell_gather_scratch;   // size n_facts, reused per spell repeat
  mutable std::vector<std::vector<float>> spell_scratch;          // one per per_spell row, post-ReLU
  mutable std::vector<float>              concat_scratch;         // the trunk's own input buffer
};

struct rl_weights_t
{
  std::uint32_t format_version = 0;
  std::uint32_t generation     = 0;
  float         exploration    = 0.0f;   // D-01: plan 210-06 refuses any non-zero value
  rl_body_type  body           = rl_body_type::mlp;
  std::string   obs_schema_sha;          // "rl-obs-v1:<64hex>" -- NOT a bare 64-hex
  std::string   mask_rules_sha;          // bare 64 hex
  std::string   action_space_sha;        // bare 64 hex
  std::vector<rl_layer> layers;          // n_layers >= the body's own minimum (2 mlp / 3 dueling+)

  // Phase 259 (plan 259-05b): present only when format_version >= 6 AND the file's `aim_present`
  // is 1 -- a rotation-only blob loads with has_aim_section == false and `aim` left
  // default-constructed. `has_aim_section` means the random-aim dial's rate is available
  // (`aim.exploration`, a dial-only section included); `has_aim_head` means the section carries
  // layers (the head is consulted). The run-time switch on top of these lives in
  // rl_target_select.cpp's run_target_head: a head-bearing blob aims by the head UNLESS
  // sim->target_scorer_force_rules (default off, sim.hpp/sim.cpp) forces the rules' aim.
  bool          has_aim_section = false;
  bool          has_aim_head    = false;
  rl_aim_t      aim;

  // Phase 222 (NET-01, arm subsets): an optional input GATHER and a static
  // action ALLOW-LIST, both carried on the blob (RLW1 v3's trailing
  // section; a v2 blob reads as identity gather + all-allowed). Resolved
  // ONCE at load time -- see rl_policy_net.cpp's load_rlw1 for the three
  // bounds refusals that make obs[ input_slots[i] ] safe to read in
  // forward() with no per-decision check.
  std::vector<std::uint32_t> input_slots;         // empty == identity gather (network takes the full obs)
  std::uint32_t              allowed_actions = 0; // bit i set == action i statically allowed

  // 222-07 (WR-01): `allowed_actions` is a u32 bitmask on the wire (RLW1's
  // own encoding, mirrored by both `(allowed >> i) & 1u` call sites --
  // solver_control.cpp's allow-list AND and load_rlw1's default-value
  // computation) -- a shift count of i >= 32 is undefined behaviour, and on
  // x86 silently wraps mod 32 rather than trapping. Refuse the widening at
  // COMPILE time rather than papering over it at load time with a runtime
  // `RL_ACTION_DIM >= 32` special case (that special case used to compute
  // the "all actions allowed" default, which is legitimate v2/no-subset
  // back-compat behaviour -- the bug was HOW it computed it, not that it
  // did). Widen this field (e.g. to a byte array or a 64-bit word) before
  // widening RL_ACTION_DIM past this ceiling; matches the Python side's own
  // refusal (subsets.py::_MAX_ACTIONS = 32, rlw1.write_rlw1's
  // `bit_length() > 32` check).
  static_assert( RL_ACTION_DIM <= 32,
                  "rl_policy: allowed_actions is a u32 bitmask -- RLW1's 32-action ceiling "
                  "(subsets.py::_MAX_ACTIONS). Widen the wire field before widening the action "
                  "set." );

  // WR-02 fix, cheap half (210-CR-FIX; widened Phase 213-TDL Task 1 for the
  // dueling bodies' branch heads; generalised Phase 222 NET-01 to an
  // arbitrary depth): forward()'s hidden-layer activation buffers, hoisted
  // here and load-time-sized by load_rlw1 instead of being allocated fresh
  // on every decision boundary's forward() call. `mutable` because
  // forward() takes a `const rl_weights_t&` (the weights themselves are
  // read-only per call) but still needs to write into this per-net scratch
  // storage; safe to reuse across calls because solver_policy= is
  // hard-clamped to threads=1 (XPORT-01/AP-3), the same reasoning the wait
  // arm's now-removed thread_local buffer (WR-05) relied on.
  //
  // hidden_scratch[i] holds hidden layer i's post-ReLU activations, one
  // entry per hidden layer (hidden_layer_count(body, layers.size()) of
  // them, sized at LOAD -- never per decision, same threads=1 reasoning as
  // above), replacing the old fixed two-buffer scratch pair this struct used pre-222.
  // The dueling bodies' V head (a single scalar) and A head
  // (RL_ACTION_DIM-wide, written directly into forward()'s own out_q[]
  // output buffer before being recombined in place) need no persistent
  // scratch of their own -- see rl_policy_net.cpp's forward().
  //
  // obs_gather_scratch holds the projected (gathered) observation when
  // input_slots is non-empty, sized at LOAD to input_slots.size() -- also
  // never allocated per decision.
  mutable std::vector<std::vector<float>> hidden_scratch;
  mutable std::vector<float>              obs_gather_scratch;

  // 246.1-05 (CAP-01/CAP-02): populated only when body == grouped_ln_dueling (default-constructed
  // empty otherwise). `layers` above holds ONLY the trunk + v_head/a_head remainder for a grouped
  // blob -- see rl_grouped_layout_t's own doc comment for the load-time split.
  rl_grouped_layout_t grouped;
};

rl_weights_t load_rlw1( const std::string& path );   // throws sc_runtime_error, named per refusal
void         forward( const rl_weights_t& w, const float obs[ RL_OBS_DIM ],
                       const std::uint8_t mask[ RL_ACTION_DIM ], float out_q[ RL_ACTION_DIM ] );
int          masked_argmax( const float q[ RL_ACTION_DIM ], const std::uint8_t mask[ RL_ACTION_DIM ] );

// Phase 259 (plan 259-05b): the aim head's own forward pass -- one hidden-layer stack (ReLU, the
// SAME activation forward()'s mlp branch uses -- no second activation formula) then a single
// 1-wide linear output layer, no masking (a forward pass scores exactly ONE candidate slot;
// run_target_head in rl_target_select.cpp calls this once per legal slot and argmaxes the results
// itself). Draws no random numbers. `in` must point at `a.input_count + a.spell_count` floats --
// the caller fills `a.feature_scratch` itself and passes its data pointer.
float forward_aim( const rl_aim_t& a, const float* in );

// 260914-rbp Task 2c (ADD-2): clears rl_policy_obs.cpp's own per-actor g_hits_action_handle_cache
// (the hits.* family's find_action() handle cache) -- declared here, defined in rl_policy_obs.cpp,
// called from rl_target_select::reset( sim_t* ) (a DIFFERENT translation unit, the one sim.cpp's
// own sim_t::reset() already calls) so a stale handle from a torn-down iteration's player_t*
// address can never leak into the next iteration. See rl_target_select.cpp's reset() for the
// call site and the ME-1 sibling cache it clears alongside this one.
void clear_hits_action_handle_cache();

// 261005-fight-list (quick 261005-mix, plan 02): empties EVERY process-wide pointer-keyed cache of
// rl_policy_obs.cpp (action handles, hits handles, gate bits, aim-context pending record, capability bits, slot
// tables, enemy handles) and returns the count of entries still held (process_cache_entries()). Used only by the
// `rl_fight_list=` driver in sc_main.cpp, between entries; see the definition for why each is needed.
void clear_process_caches();
std::size_t process_cache_entries();
}
