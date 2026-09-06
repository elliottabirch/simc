// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// P2 spike hook (simc-solver-spike-2026-07-29, R2 state-sync data generator).
// One JSON line per actor decision boundary (player_t::execute_action()),
// dumped BEFORE the chosen action mutates any state (cost/cooldown/GCD) -
// this is the same "read state, then decide" moment the ret solver's own
// snapshot(ctx) reads at. Enabled via `decision_dump=<file>`; a no-op
// (single empty-string check) when the option is unset.

#pragma once

#include "config.hpp"
#include "sc_enums.hpp"

#include <ostream>
#include <string>
#include <string_view>

struct player_t;
struct action_t;

namespace decision_dump
{
// Called once per decision boundary, right after the APL/sequence/
// solver_control resolution has chosen (or failed to choose) an action,
// before that action executes. `chosen` is nullptr on an idle/wait decision.
// `et` carries the boundary kind (phase 200-04, FORK-01/R-8 -- widened
// control surface) and defaults to FOREGROUND so the pre-existing foreground
// call site (player_t::execute_action()) needs no change; the ONE NEW call
// site (special_execute_event_t::execute_action()) passes it explicitly.
// Corrected 2026-08-21 (WR-05): player_t::combat_begin()'s precombat loop
// was originally also hooked here but that leg was removed the same day
// (see PROTOCOL.md "Version history", 200-04-ad-hoc) -- it never calls
// decision_dump::record() and is not one of the widened call sites. Emits
// an additive `"boundary"` field
// (see boundary_name() below) naming `et` on every dump line -- per R-9
// (owner ruling 2026-08-21), every boundary is recorded, tagged, and
// criterion 2's byte-identity receipt is redefined as a projection over
// `boundary=="foreground"` lines rather than raw dump identity. A dump line
// with NO `boundary` field (every pre-change record) is treated as
// foreground by every downstream reader.
void record( player_t* p, action_t* chosen, execute_type et = execute_type::FOREGROUND );

// Boundary-kind label shared between decision_dump and solver_control (phase
// 200-04) so the wire's own `"boundary"` field (solver_control.cpp's request
// line) and the dump's own `"boundary"` field can never disagree about the
// name for the same `execute_type` value. Returns "foreground", "off_gcd" or
// "cast_while_casting" -- execute_type (sc_enums.hpp) has exactly these three
// values; this switch is exhaustive.
std::string boundary_name( execute_type et );

// Shared state-block emitter (P3b solver_control hook, simc-offline-
// evaluation-pipeline phase 116, reuses this so the two decision-boundary
// hooks never drift). Writes gcd_remains, swing_mh_remains, holy_power,
// cooldowns, buffs, target_debuffs, target_time_to_die, active_enemies,
// dots, gcd_length, auto_attack_interval, resolved_action,
// resolved_spell_id, resolved_item_id and (only when `solver_reply_gated` is
// true) `solver_reply_type` as trailing JSON object members (each preceded
// by its own comma) -- the caller owns the enclosing object's opening `{`
// and closing `}`. Additionally, for rage-primary actors only (143 D-09/
// D-10, arms-clean-room -- `p->primary_resource() == RESOURCE_RAGE`, so
// paladin/holy_power records are byte-identical to the pre-143 output):
// `rage` (current rage) and `rage_gains` (a per-named-gain_t ledger of
// cumulative `actual`/`overflow`/`count` for the primary resource, all-zero
// buckets skipped).
//
// `chosen` is the action about to execute at this boundary (nullptr on an
// idle/wait decision) -- it backs `resolved_action` (116-02: the
// authoritative resolved action identity, unwrapping a sequence/
// strict_sequence wrapper to its real next sub-action; `chosen` at the call
// site's own top-level key is kept for backward compatibility but is
// unreliable under a sequence-driven run -- see PROTOCOL.md). `gcd_length`
// and `auto_attack_interval` are PLAYER-scoped (2026-07-29 fix bundle,
// defect (d)) and no longer read `chosen` at all.
//
// `resolved_spell_id` (owner-ratified decision chain, step 2, 2026-08-01;
// re-tiered same day per the id0 investigation, see below) is a 5-tier
// resolution of the resolved action's numeric identity, paired 1:1 with
// `resolved_action` (null exactly when `resolved_action` is null, a JSON
// number never a string otherwise):
//   1. `data_reporting().id()` -- same as `data().id()` for any ordinary
//      spell-backed action, but additionally recovers potion/flask/food/
//      augmentation's real triggered-spell id (consumable.cpp populates
//      `s_data_reporting`, never `s_data`, for those).
//   2. the plain `action_t::id` member, if Tier 1 was still 0.
//   3. (item ids are handled separately -- see `resolved_item_id` below,
//      NOT folded into this field.)
//   4a. a stable, per-action-name sentinel in the reserved
//      9,000,000-9,000,999 band, for engine actions with a fixed literal
//      name_str and no spell id anywhere in the object (auto_attack, wait,
//      run_action_list, ...) -- see decision_dump.cpp's
//      `RESOLVED_ID_SENTINELS` table for the full list and the band-choice
//      rationale (deliberately distinct from the consuming TSTL project's
//      own, non-globally-unique 999xxx sentinel convention, DEC-013).
//   4b. for the use_item family specifically -- whose name_str is
//      dynamically item-suffixed ("use_item_<item name>") and therefore
//      cannot live in a static Tier-4a table -- a sentinel derived from the
//      action's own resolved item id, `9,100,000 + item_id`, reserved
//      sub-band 9,100,000-9,899,999. Each distinct trinket therefore still
//      gets its own distinct value.
//   5. `RESOLVED_ID_UNMAPPED` (9,999,999) plus a one-time `sim->errorf` to
//      stderr, for an action that reaches here with no id, no registered
//      Tier-4a sentinel, and no usable Tier-4b item id -- a signal that a
//      NEW id-less action class needs to be added to the table, not a
//      silent rejoin of the original id-0 collision bucket.
// This tiering exists because a bare `data().id()` read (the field's
// original 2026-08-01 implementation) was measured to still collide at 0
// across FOUR distinct action names -- auto_attack, potion,
// use_item_algethar_puzzle_box, wait -- a direct violation of "unique,
// non-colliding identity" (2026-08-01-id0-census.md). It also remains true,
// independent of the collision fix, that `resolved_action`'s name_str can
// rename at construction time based on live talent state (see
// sc_paladin_retribution.cpp:663-668) while the id is stable, or vice versa
// -- see decision_dump.cpp's inline comment at the emit site for the full
// rationale and the three alias tables this divergence forces today.
//
// `resolved_item_id` (added alongside the retier, same commit) is the
// resolved action's backing item id (`used_item()->parsed.data.id`), for
// actions whose `used_item()` (action_t's read-only virtual accessor,
// action.hpp) returns non-null (trinket/use_item actions) -- null for
// everything else. Deliberately reads through `used_item()` rather than the
// base `action_t::item` member directly: `action_t::item` feeds real
// damage-scaling decisions elsewhere in the engine (action.cpp's
// `item_scaling` check) for other action types, and use_item_t never
// populated it -- see action_t::used_item()'s own doc comment for the full
// rationale (a reporting-only need should not repurpose a gameplay-affecting
// field, independent of whether doing so is measurably inert for this
// specific action type). Kept as its own field rather than folded into
// `resolved_spell_id` because item ids and spell ids are different DBC
// content-id namespaces and a numeric collision between the
// two is possible in principle.
//
// `solver_reply_gated` (2026-07-29 fix bundle, defect (b); default false,
// only decision_dump::record() opts in, never solver_control's own
// wire-request-building call) -- when true, `resolved_action` reflects
// `sim->solver_control_last_reply_type` (set by solver_control::choose()
// immediately before this runs): non-"cast" replies (wait/default/abstain)
// force `resolved_action:null` plus an explicit `solver_reply_type` marker,
// rather than resolving `chosen` (which, ungated, would show whatever the
// pre-reply APL/sequence placeholder happened to pick -- the exact bug this
// fix closes).
//
// `solver_damage_so_far` (FORK-03, tstl-sylvanas phase 205, owner ruling
// OQ-1 2026-08-22) -- p->solver_damage_so_far read exactly, cumulative kit
// damage (pets included, D-05) since the current iteration began. The RL
// reward source at each decision boundary; additive on both call sites
// (decision_dump::record() and solver_control's request line) since both
// share this function.
//
// `obs`/`obs_mask` (260901-od1, kill-the-dual-encoder-seam) -- the SAME
// engine-encoded observation vector (RL_OBS_DIM floats) and legality mask
// (RL_ACTION_DIM 0/1 ints) `solver_control.cpp`'s in-process transport
// builds via `rl_policy::bind_slots` -> `rl_policy::read_state` ->
// `rl_policy::build_mask` -> `rl_policy::build_obs`, run here in that same
// order so the offline decision-dump consumer (scripts/rl/demos.py) never
// has to re-encode observations from the dump's own named fields itself --
// that re-encode was the root cause of the dual-encoder seam this task
// closes (obs.encode() resolving a registry source like
// `player_buffs.<name>.stacks` against a JSON shape the dump never
// actually emitted under that name). Gated by the SAME single-sim/single-
// thread predicate (`p->sim->threads == 1 && p->sim->profileset_map.empty()`)
// as `action_resolvable`/`action_ready` above, for the same WR-12 cache-
// safety reason -- `null` for both arrays on a multi-threaded/profileset
// run. Requires NO solver_policy= / solver_control= -- `bind_slots` is
// registry-constants-driven (rl_policy_constants.h), not policy-driven, so
// this is available on a bare APL-driven `decision_dump=<file>` run.
// Deliberately does NOT apply solver_control's arm-subset allow-list AND
// or its all-illegal protocol_abort refusal -- both are training-arm
// concepts (RLW1 v3's `allowed_actions`) with no meaning for a dump-only
// run; `obs_mask` here is the RAW engine legality mask `build_mask()`
// computes, from which a consumer can derive its own arm-specific
// restriction if it needs one.
//
// `boundary_is_foreground` feeds `rl_state_t::boundary_is_foreground`
// (build_mask's own wait-legality rule reads it) -- both call sites pass
// `et == execute_type::FOREGROUND`, converted once by the caller exactly
// like solver_control.cpp's in-process transport already does.
//
// `target_fact_dump_time_compute` (232-04, OBS-02/R-T) -- the exact twin of
// `action_gate_dump_time_compute` above, on the `targeted_picks`/
// `candidate_facts` rows: `record()` is NEVER the decision boundary (this
// file's own CR-02 comment, `write_state_fields`'s doc comment below), so a
// targeted action's `is_current_target`/Tempest's `hit_damage`, recomputed
// straight off `build_enemy_fact()` at dump time, would read POST-
// `accept_cast`-retarget/turn state instead of the value the decision was
// actually made from. Both JSON emit sites first consult
// `rl_target_select::lookup_target_fact_snapshot()` -- the pre-cast
// snapshot `build_obs()`'s own real-decision call stamped
// (`rl_state_t::is_decision_boundary == true`) -- and fall back to a fresh
// compute, flagged, only when nothing was stamped this decision.
//
// ACTOR SCOPE (post-landing fix, measured against a real HecticAddCleave
// capture): additionally gated on `p->resources.is_active(
// RESOURCE_MAELSTROM )`, the same predicate phase 165-01's own maelstrom-
// scoped decision_dump additions already use as this codebase's "is this
// actor the shaman this RL schema targets" check. `decision_dump::record()`
// fires from `player_t::execute_action()` for EVERY actor in the sim (the
// primary shaman, its own pets, AND every enemy/target actor) -- `obs`/
// `obs_mask` are `null` for any actor this predicate excludes. Necessary
// because `build_obs()`'s only pre-260901-od1 caller (solver_control.cpp's
// in-process transport) is gated to the ONE actor `solver_policy=` names,
// so it was never exercised against a non-shaman actor before this task
// wired it into decision_dump's all-actor hook -- measured: `bind_slots`/
// `read_state`/`build_mask` all complete cleanly for every actor type, but
// `build_obs()` itself segfaults the first time it runs for an enemy
// actor. Zero effect on the schema's actual target: `demos.rows_from_dump`'s
// consumer (`project_fight`'s `actor_name` filter) never reads a pet's or
// an enemy's own dump rows regardless.
//
// A SECOND crash (found the same session, via a real gdb backtrace on a
// RelWithDebInfo build) proved the Maelstrom-active predicate ALONE is not
// sufficient: `resources_t::active_resource` defaults to `true` for every
// `resource_e` (player_resources.hpp's ctor) and is only narrowed to `false`
// per-resource inside the generic player_t init path that walks
// `power_type_data_t::is_active_for_class()` -- an enemy actor (a
// TANK_DUMMY-type target, measured as "Fluffy_Pillow_1") still read
// `is_active(RESOURCE_MAELSTROM) == true`, reached `build_obs()`, and
// segfaulted inside the `movement.remains` expression's lambda
// (`buffs.movement->remains()`, player.cpp:12293 -- `buffs.movement` is
// constructed ONLY for non-enemy actors, player.cpp:4904, permanently null
// for every enemy). The gate now ALSO checks `!p->is_enemy()` (player.hpp's
// own `_is_enemy(type)` -- ENEMY/ENEMY_ADD/ENEMY_ADD_BOSS/TANK_DUMMY), an
// explicit, unambiguous actor-class exclusion rather than trusting the
// resource predicate alone.
// 260902/cr4 (CR-02): `is_decision_boundary` carries NO default -- both call sites (solver_control
// .cpp's FIFO request-line build, and decision_dump::record()'s own call below) state it
// explicitly. True at the request-line build (built before the reply/accept_cast have run for
// this decision); false at record()'s call (which always runs AFTER solver_control::choose() has
// already retargeted/turned the player). Threaded straight through to
// rl_policy::read_action_gate_bits -- see that function's own doc comment for the full mechanism.
void write_state_fields( std::ostream& out, player_t* p, action_t* chosen, bool solver_reply_gated,
                          bool boundary_is_foreground, bool is_decision_boundary );

// JSON helpers shared with solver_control (P3b) so both hooks emit
// byte-identical escaping/clamping for the same field kinds.
std::string json_escape( std::string_view s );
double clamp_nonneg( double v );
}
