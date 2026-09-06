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
#include "sim/rl_policy_constants.h"
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

  // OBS-05/R-W (232-03) -- the "turn to face" action's legality predicate, computed ONCE here
  // (never inside build_mask -- that function stays PURE over this POD, no player_t*, unchanged
  // contract). True iff at least one alive enemy fails the raw in-front test
  // (`p->is_in_front(*t, 0.0)`), the SAME predicate `mask.py`'s Python mirror
  // (`_any_enemy_behind_player`) computes from `all_enemies`/`player_position`. No has_* flag --
  // this is always well-defined (a fight with zero enemies is simply `false`, not "absent"),
  // matching `has_active_enemies`'s own unconditional-true convention for the same reason.
  bool any_enemy_behind_player = false;

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
                             bool* out_used_dump_time_compute = nullptr );

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
enum class rl_body_type
{
  mlp,
  dueling,
  ln_dueling
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

// Phase 230-02 (SCOR-01, R-A): the RLW1 v4 trailing SCORER section -- a SECOND, independent
// parameter set (never a `body` variant: `body` selects a layer-SPLIT convention over the ONE
// rotation parameter set, the scorer is a wholly separate net). Always mlp-shaped (`_layer_split`
// / `hidden_layer_count(rl_body_type::mlp, ...)`, reused verbatim -- never a second hidden
// convention): n_layers >= 2, every layer's has_ln == false, first layer's in_features ==
// `features + RLW1_V4_AIMING_SPELL_COUNT` (the eight targeted spells' one-hot, CK1-1), last
// layer's out_features == 1 (one score per candidate). `context_width` is declared but PINNED to
// 0 in this phase (R-C: the shared observation vector does not exist at the moment a pick is
// computed) -- a non-zero value is refused by name at load. `slots` is the TRAINING side's own
// declared candidate-slot bound (R-B) -- the engine's own overflow refusal (rl_target_select.cpp)
// compares the generic filter's live candidate count against THIS value, never a hardcoded
// engine constant, so a blob trained against a different slot count fails loudly rather than
// silently.
struct rl_scorer_t
{
  float          exploration    = 0.0f;  // the scorer's OWN epsilon dial over CANDIDATES -- mirrors
                                          // rl_weights_t::exploration's own [0.0,1.0]-finite bound
  std::uint32_t  slots          = 0;     // 1..64 -- the training side's declared candidate-slot count
  std::uint32_t  features       = 0;     // 1..RLW1_MAX_FEATURES -- the per-candidate feature count
  std::uint32_t  context_width  = 0;     // MUST be 0 in this phase (R-C) -- non-zero refused by name
  std::string    feature_sha;            // "tgt-feat-v1:<64hex>" -- cross-checked against
                                          // RL_TARGET_FEATURE_SHA (rl_policy_constants.h) at load
  std::vector<rl_layer> layers;          // mlp-shaped, n_layers >= 2 (one hidden layer minimum + the
                                          // single-output layer) -- expected_layer_count(mlp) == 2

  // Load-time-sized scratch (same single-thread/no-per-decision-allocation reasoning as
  // rl_weights_t::hidden_scratch above): hidden_scratch[i] holds scorer hidden layer i's
  // post-ReLU activations; feature_scratch holds the per-candidate input vector
  // (rl_target_select's scorer preference fills it: the declared feature list's order, then the
  // eight-wide aiming-spell one-hot) -- `features + RLW1_V4_AIMING_SPELL_COUNT` wide, sized once
  // at load, reused every scoring call, never reallocated per decision.
  mutable std::vector<std::vector<float>> hidden_scratch;
  mutable std::vector<float>              feature_scratch;
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

  // Phase 230-02 (SCOR-01, R-A/R-K): present only when format_version == 4 -- a rotation-only v3
  // blob loads with has_scorer == false and `scorer` left default-constructed. The run-time
  // switch this presence bit drives lives in rl_target_select.cpp's preference_for() (D-02): a
  // scorer-bearing blob takes the scored path, UNLESS sim->target_scorer_force_rules (default
  // off, sim.hpp/sim.cpp) forces the rules path so the previous phase's rules-arm numbers can be
  // re-run byte-identically on this binary (230-SWAP-RECEIPT.md's Task 3 proof).
  bool          has_scorer = false;
  rl_scorer_t   scorer;

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
};

rl_weights_t load_rlw1( const std::string& path );   // throws sc_runtime_error, named per refusal
void         forward( const rl_weights_t& w, const float obs[ RL_OBS_DIM ],
                       const std::uint8_t mask[ RL_ACTION_DIM ], float out_q[ RL_ACTION_DIM ] );
int          masked_argmax( const float q[ RL_ACTION_DIM ], const std::uint8_t mask[ RL_ACTION_DIM ] );

// Phase 230-02 (SCOR-01): the scorer's own forward pass -- one hidden-layer stack (ReLU, the SAME
// activation forward()'s mlp branch uses -- no second activation formula) then a single 1-wide
// linear output layer, no masking (a scorer forward pass scores exactly ONE candidate; `select()`
// in rl_target_select.cpp calls this once per candidate and argmaxes the results itself, mirroring
// every other preference_fn's own contract). `in` must point at `s.features +
// RLW1_V4_AIMING_SPELL_COUNT` floats -- the caller (rl_target_select's scorer preference) fills
// `s.feature_scratch` itself and passes `s.feature_scratch.data()`.
float forward_scorer( const rl_scorer_t& s, const float* in );
}
