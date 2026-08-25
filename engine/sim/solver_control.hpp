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
// Called at a decision boundary, from one of TWO call sites (phase 200-04,
// FORK-01/R-8 -- widened from the original single foreground-only call;
// corrected 2026-08-21, WR-05: the precombat leg documented here as a third
// call site was removed the same day it was added -- see PROTOCOL.md
// "Version history", 200-04-ad-hoc -- and never shipped):
//   1. player_t::execute_action() (FOREGROUND), immediately before
//      decision_dump::record(), same as before this phase.
//   2. special_execute_event_t::execute_action() (OFF_GCD or
//      CAST_WHILE_CASTING, via that event's own type()), the poll-event
//      bypass this phase closes.
// player_t::combat_begin()'s precombat loop does NOT call solver_control::
// choose() -- it executes `action` directly, exactly as before this phase.
// This is the ROADMAP-criterion-1 accepted residual bypass (owner ruling:
// "dont bring in precombat"), not an oversight.
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
// A FOREGROUND BOUNDARY (see below) -- or nullptr for a "noop" reply
// (CR-08, 2026-08-21 owner ruling: nothing executes this boundary, legal at
// ANY boundary including under training mode, and never touches
// solver_control_pending_wait_s). A "default" reply returns apl_choice
// unchanged unconditionally. An "abstain" reply returns apl_choice unchanged
// under sim_t::solver_control_mode_str=="verify" (the default), or is a hard
// protocol_abort under =="training" (D-06/D-07) -- the two are no longer
// synonyms (see PROTOCOL.md); "noop" is the mode-independent decline that
// closes the hole this leaves at a non-FOREGROUND boundary under training
// mode. A malformed or out-of-sequence reply (bad JSON, `v` mismatch, `seq`
// mismatch, an unresolvable or not-ready action name, an unknown `type`, or
// a "wait" at a non-FOREGROUND boundary) is a hard error: a one-line
// diagnostic is written to stderr and the sim aborts -- never a silent
// wrong answer.
action_t* choose( player_t* p, action_t* apl_choice, execute_type et = execute_type::FOREGROUND );

// Called once at sim end (sim_t::execute(), after iterate() completes).
// Writes the `{"type":"bye"}` terminator and closes both FIFO streams.
// No-op if the channel was never opened (solver_control= unset, or no
// decision boundary was ever reached).
void finish( sim_t* sim );

// tstl-sylvanas phase 214, plan 01 (C1). Called once per FIGHT, from
// sim_t::combat_begin() immediately after reset() -- see the call site's own
// comment for why that hook (not sim_t::reset() itself, which also runs
// during construction/reset outside a fight) is the right one. A no-op
// (single early-return check) when both solver_control_str and
// solver_policy_str are empty, mirroring choose()'s own widened entry guard.
//
// Clears exactly the four per-fight members that choose()'s t=0 yield block
// and wait-reply path populate: solver_control_auto_attack_started,
// solver_control_has_pending_wait, solver_control_pending_wait_s and
// solver_control_last_reply_type. Left uncleared without this function,
// _auto_attack_started never being false again means fights 1..N-1 of a
// multi-fight (`iterations>1`) launch never start the character's melee
// swing and never take the zero-delay yield at t=0 -- so those fights land
// no white damage and make their first decision against a not-yet-updated
// buff state; a leftover _has_pending_wait/_pending_wait_s pair leaks an
// unconsumed wait into the next fight's first ready event. Neither throws.
// Both produce a complete, plausible-looking fight with wrong numbers --
// this is the actual defect .planning/todos/pending/2026-08-25-solver-
// policy-zero-damage-multi-iteration.md measured and root-caused.
//
// solver_control_seq is DELIBERATELY NOT cleared here, and must never be:
// the FIFO arm refuses a reply whose "seq" does not match the request's
// (choose()'s own protocol_abort on mismatch), and that refusal is only
// meaningful because the counter never restarts -- restarting it once per
// fight would make a stale reply from the previous fight look valid. The
// two stream handles (solver_control_req_stream/_rep_stream) are likewise
// never touched here -- the channel is one continuous stream across every
// fight of a launch, by design.
void reset_iteration( sim_t* sim );
}
