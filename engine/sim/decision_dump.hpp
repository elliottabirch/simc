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
// resolved_spell_id and (only when `solver_reply_gated` is true)
// `solver_reply_type` as trailing JSON object members (each preceded by its
// own comma) -- the caller owns the enclosing object's opening `{` and
// closing `}`.
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
// `resolved_spell_id` (owner-ratified decision chain, step 2, 2026-08-01) is
// the resolved action's `data().id()` -- the numeric spell id backing the
// same action `resolved_action` names, paired 1:1 with it (null exactly
// when `resolved_action` is null, a JSON number never a string otherwise).
// Added because `resolved_action`'s name_str can rename at construction
// time based on live talent state (see sc_paladin_retribution.cpp:663-668)
// while the id is stable, or vice versa -- see decision_dump.cpp's inline
// comment at the emit site for the full rationale and the three alias
// tables this divergence forces today.
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
