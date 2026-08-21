// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// P3b fork hook (simc-offline-evaluation-pipeline phase 116, 116-01). An
// additive decision-boundary request/reply hook, sibling to decision_dump=
// (same call site, same "read state, then decide" moment, same no-op-when-
// unset discipline). Where decision_dump= is a one-way JSONL append,
// solver_control=<prefix> is a bidirectional FIFO-pair protocol: the engine
// asks "what should I do at this decision boundary?" and blocks for exactly
// one reply line before proceeding. See scripts/simc-eval/PROTOCOL.md for the
// full normative wire contract.
//
// Enabled via `solver_control=<prefix>`; a no-op (single empty-string check)
// when the option is unset -- zero behaviour change for every other use of
// the binary.

#pragma once

#include "config.hpp"
#include "sc_enums.hpp"

struct player_t;
struct action_t;
struct sim_t;

namespace solver_control
{
// Called at a decision boundary, from one of THREE call sites (phase 200-04,
// FORK-01/R-8 -- widened from the original single foreground-only call):
//   1. player_t::execute_action() (FOREGROUND), immediately before
//      decision_dump::record(), same as before this phase.
//   2. special_execute_event_t::execute_action() (OFF_GCD or
//      CAST_WHILE_CASTING, via that event's own type()), the poll-event
//      bypass this phase closes.
//   3. player_t::combat_begin()'s precombat loop (boundary FOREGROUND --
//      precombat resolution is a single-shot action-by-action selection,
//      structurally the same shape as a foreground decision), the second
//      bypass this phase closes.
// `apl_choice` is whatever the actor's own action-list evaluation chose
// (nullptr on an idle/wait decision). `et` carries the boundary kind and
// defaults to FOREGROUND so the pre-existing foreground call site needs no
// change; the OTHER two call sites pass it explicitly. `et` is the ONLY
// thing this hook may branch on -- no spell, item or racial name appears
// anywhere in solver_control (D-04/R-4). When sim_t::solver_control_str is
// empty, returns apl_choice unchanged regardless of `et`.
//
// Otherwise: emits a "decision" request line on `<prefix>.out` (carrying an
// additive `"boundary"` field naming `et` -- see boundary_name() in
// decision_dump.hpp, shared with decision_dump::record() so both hooks can
// never disagree about the name), blocks reading exactly one reply line from
// `<prefix>.in`, and returns the action_t* the reply names (already resolved
// among the actor's own constructed actions by SimC internal name_str), or
// nullptr for a "wait" reply (nothing executes this boundary; the actor's
// readiness is rescheduled for the reply's requested `sec`) -- LEGAL ONLY AT
// A FOREGROUND BOUNDARY (see below). A "default" reply returns apl_choice
// unchanged unconditionally. An "abstain" reply returns apl_choice unchanged
// under sim_t::solver_control_mode_str=="verify" (the default), or is a hard
// protocol_abort under =="training" (D-06/D-07) -- the two are no longer
// synonyms (see PROTOCOL.md). A malformed or out-of-sequence reply (bad
// JSON, `v` mismatch, `seq` mismatch, an unresolvable or not-ready action
// name, an unknown `type`, or a "wait" at a non-FOREGROUND boundary) is a
// hard error: a one-line diagnostic is written to stderr and the sim aborts
// -- never a silent wrong answer.
action_t* choose( player_t* p, action_t* apl_choice, execute_type et = execute_type::FOREGROUND );

// Called once at sim end (sim_t::execute(), after iterate() completes).
// Writes the `{"type":"bye"}` terminator and closes both FIFO streams.
// No-op if the channel was never opened (solver_control= unset, or no
// decision boundary was ever reached).
void finish( sim_t* sim );
}
