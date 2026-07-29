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

struct player_t;
struct action_t;
struct sim_t;

namespace solver_control
{
// Called at the same decision boundary as decision_dump::record(), from
// player_t::execute_action(), immediately after it. `apl_choice` is whatever
// the actor's own action-list evaluation chose (nullptr on an idle/wait
// decision). When sim_t::solver_control_str is empty, returns apl_choice
// unchanged.
//
// Otherwise: emits a "decision" request line on `<prefix>.out`, blocks
// reading exactly one reply line from `<prefix>.in`, and returns the
// action_t* the reply names (already resolved among the actor's own
// constructed actions by SimC internal name_str), or nullptr for a "wait"
// reply (nothing executes this boundary; the actor's readiness is
// rescheduled for the reply's requested `sec`). A "default"/"abstain" reply
// returns apl_choice unchanged. A malformed or out-of-sequence reply (bad
// JSON, `v` mismatch, `seq` mismatch, an unresolvable or not-ready action
// name, or an unknown `type`) is a hard error: a one-line diagnostic is
// written to stderr and the sim aborts -- never a silent wrong answer.
action_t* choose( player_t* p, action_t* apl_choice );

// Called once at sim end (sim_t::execute(), after iterate() completes).
// Writes the `{"type":"bye"}` terminator and closes both FIFO streams.
// No-op if the channel was never opened (solver_control= unset, or no
// decision boundary was ever reached).
void finish( sim_t* sim );
}
