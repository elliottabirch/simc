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

#include <ostream>
#include <string>
#include <string_view>

struct player_t;
struct action_t;

namespace decision_dump
{
// Called once per player_t::execute_action() invocation, right after the
// APL/sequence has chosen (or failed to choose) an action, before that
// action executes. `chosen` is nullptr on an idle/wait decision.
void record( player_t* p, action_t* chosen );

// Shared state-block emitter (P3b solver_control hook, simc-offline-
// evaluation-pipeline phase 116, reuses this so the two decision-boundary
// hooks never drift). Writes gcd_remains, swing_mh_remains, holy_power,
// cooldowns, buffs, target_debuffs, target_time_to_die, active_enemies,
// dots, gcd_length, auto_attack_interval, resolved_action,
// resolved_spell_id, resolved_item_id and (only when `solver_reply_gated` is
// true) `solver_reply_type` as trailing JSON object members (each preceded
// by its own comma) -- the caller owns the enclosing object's opening `{`
// and closing `}`.
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
void write_state_fields( std::ostream& out, player_t* p, action_t* chosen, bool solver_reply_gated = false );

// JSON helpers shared with solver_control (P3b) so both hooks emit
// byte-identical escaping/clamping for the same field kinds.
std::string json_escape( std::string_view s );
double clamp_nonneg( double v );
}
