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
#include "util/timespan.hpp"

#include <cstdint>
#include <string>

struct player_t;
struct action_t;
struct sim_t;

namespace solver_control
{
// 221-01 (ACT-02, Pattern 2). Resolves a SimC internal action name
// (`name_str`) to one of the actor's already-constructed action_t objects,
// preferring a player-CASTABLE (non-background) match -- see the
// definition's own comment in solver_control.cpp for the full resolution
// order and the background-collision rationale. Exposed here (moved out of
// an anonymous namespace) so `rl_policy_obs.cpp`'s engine-truth handle
// table and this file's own `accept_cast()` epilogue resolve a token to the
// SAME action_t*, by construction -- the mask bit and the not-ready FATAL
// gate can never disagree about which object they mean. Never constructs a
// new action; returns nullptr when no match exists under `name` (or its one
// known rename alias).
action_t* resolve_action( player_t* p, const std::string& name );

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

// ==========================================================================
// 261027-fts stage B: DECISION ROWS ARE STAMPED AT EXECUTION.
//
// A foreground cast the policy picks can need a queue: the action's cooldown ends within the engine's 250 ms tolerance, so queue_execute() puts a
// queued_action_execute_event_t on the clock and the press fires later (or never: movement, a stun, a cooldown reset, the fight end). The translog
// row of such a press is written when the press fires (the COMMIT), not when the policy chose it; a press that never fires writes no row and is
// counted in the close row's census. The pieces:
//   STAGED  choose() filled a row (rl_translog::fill_decision) and handed the action back; the row waits for queue_execute().
//   QUEUED  queue_execute() found a queue delay: the row is held, solver_control_seq is given back (damage landing in the queue window is credited to the
//           previous row, which is the point), and the queue event is the clock of the commit.
//   COMMIT  the press is irrevocable (immediately before schedule_execute for a cast, do_execute for a non-foreground action, or the decision itself for a
//           wait/idle row): stamp_commit() re-reads `t` and every cumulative counter, the decision number is taken again, the row/`.apl`/table record is written.
//   DISCARD every other end of a QUEUED press, by the reason the engine took the queue away (census). A STAGED row the engine did not queue is a discard too.
// All of it is a no-op unless rl_translog= is set. Invariants (PENDING_INVARIANT): at most one STAGED and one QUEUED, each resolved exactly once, the
// decision number given back and taken again is the one the row carries, a commit precedes schedule_execute, nothing pending survives reset_iteration.
// ==========================================================================

// Why the engine interrupted the actor: the raid-event entry points set it around the call that ends in player_t::interrupt() (default: other).
enum class interrupt_cause_t : std::uint8_t
{
  other = 0,
  movement,
  stun,
  invulnerable,
  raid_event
};
interrupt_cause_t& current_interrupt_cause();
struct interrupt_scope_t
{
  explicit interrupt_scope_t( interrupt_cause_t c ) : previous( current_interrupt_cause() ) { current_interrupt_cause() = c; }
  ~interrupt_scope_t() { current_interrupt_cause() = previous; }
  interrupt_scope_t( const interrupt_scope_t& ) = delete;
  interrupt_scope_t& operator=( const interrupt_scope_t& ) = delete;
  interrupt_cause_t previous;
};

// The reasons a staged/queued decision ends without a row (census slots 5-13 of rl_translog::census_slot; the interrupt reasons come from the cause).
enum class discard_reason : std::uint8_t
{
  queue_failed_not_ready,
  queue_failed_target_not_ready,
  cooldown_reset,
  ready_cancelled,
  ready_preempts_queue,
  replaced,
  fight_end,
  branch_cut,
  staged_not_queued
};

// action_t::queue_execute() created a queue event for `a` (delay in seconds): the staged row of the RL actor, if it is this action's, becomes QUEUED.
void pending_queued( player_t* p, action_t* a, double queue_delay_s );
// The press `a` is about to execute (immediately before schedule_execute / do_execute): commits the STAGED row (no queue needed) or, with
// from_queue_event, the QUEUED row (the queue event fired).
void pending_commit( player_t* p, action_t* a, bool from_queue_event );
// The queued press `a` ends without executing. The interrupt form classifies by current_interrupt_cause().
void pending_discard( player_t* p, action_t* a, discard_reason reason );
void pending_discard_interrupt( player_t* p, action_t* a );
// Census-only events of a COMMITTED cast (no state change): the cast bar was interrupted / the cast reached its execute event and could not execute.
void note_cast_interrupted( player_t* p );
void note_cast_failed_at_end( player_t* p, action_t* a );
// sim_t::combat_end(), immediately before rl_table_policy::on_fight_end and the close row: whatever is still staged or queued is discarded (fight_end).
void on_fight_end( sim_t* sim );
// ==========================================================================
// 261027-fts stage D: THE DECISION WINDOW (D10) AND THE RE-POLL (D3, D4). Net and table arms only; the observer arm, the FIFO transport and a branch /
// teacher mode never open one. Every option is global storage (a sim_t member would rebuild every file):
//   solver_window_ms=<n>   default 250. The window: for every foreground GCD (and cast end) the policy is first asked n ms before the instant the actor
//                          is ready again (the Player-Ready event; for a hard cast the cast end), then every solver_repoll_ms after that, then once
//                          more at the instant itself (the ordinary Player-Ready decision). 0 = off: the stage-B behaviour, byte for byte.
//   solver_repoll_ms=<n>   default 25 (the live addon's cadence).
//   solver_window_log=<f>  off by default: one JSON line per policy call that is NOT an ordinary decision boundary (diagnostic and the D3 count).
//   solver_window_probe=1  off by default, a purity probe: every poll runs (the window opens, the chain polls, the queue is re-polled) but nothing is ever
//                          held or replaced, so the launch must be byte-identical to solver_window_ms=0 -- proof that a poll leaves the simulation untouched.
// A window poll is a REAL decision boundary for the policy (read_state(..., true), the mask, the forward pass or the table's pick, the pick's target) and
// NOTHING ELSE: no row, no decision number kept, no retarget, no wait set, no APL walk. Its outcome is one HOLD (the pick that would be pressed):
//   - a cast pick becomes the hold; a later poll that picks the same button and target keeps it AND its payload (the observation, mask, picks and flags of the
//     poll that selected it: the row's `t_decided` is that poll); a different cast replaces it (the new poll's payload); a wait pick leaves it open
//     (live: a queued press cannot be recalled); at most 4 replacements per window (flipflop_bound), then the hold stays.
//   - the exploration draw (button dial) is made ONCE per window, at its first poll; an explored pick (a cast or a wait) is held to the fire and no later
//     poll replaces it; the aim dial is drawn at the fire instant only (a poll never draws it).
//   - the fire instant is the actor's ordinary Player-Ready decision: it re-decides on the real state, then resolves against the hold (same button and target:
//     the hold's row payload is committed; a wait pick leaves the hold pressed; a different cast replaces it; an illegal hold is dropped, census `replaced`).
// The press itself is the engine's own: Player-Ready -> execute_action -> queue_execute, so a press whose cooldown ends after the fire is queued exactly as
// before (stage B), and from that moment the same chain re-polls it every solver_repoll_ms strictly before its fire, plus once at the fire instant (inside the
// queue event): the same button and target keeps the queue, a wait leaves it open, a different cast cancels it (census `replaced`) and presses the new pick.
// Census: staged counts a HOLD once when it is made (a hold that reaches the fire is the row's staging); replaced / replaced_by_wait / flipflop_bound /
// repolls as in the table of rl_translog.hpp.
// ==========================================================================
int&         window_ms_storage();
int&         repoll_ms_storage();
std::string& window_log_storage();
int&         window_probe_storage();

// player_t::schedule_ready() made a Player-Ready event for a non-waiting boundary at `ready_at`; action_t::schedule_execute() started a hard cast whose bar and
// GCD end at `ends_at`. Both open the window if it is on, for the RL actor only. No-ops otherwise.
void window_arm( player_t* p, timespan_t ready_at );
void window_arm_cast( player_t* p, timespan_t ends_at );
// player_ready_event_t::execute(), before it decides: cancels the poll chain. The hold (if any) stays for the decision that follows.
void window_on_ready( player_t* p );
// action_t::queue_execute() just made a queue event for `a` (FOREGROUND, delay > 0): arms the re-poll chain of the queued press.
void queue_window_open( player_t* p, action_t* a );
// action_t::reschedule_queue_event() moved the fire of a queued press: re-arms the chain if it had run out.
void queue_rescheduled( player_t* p, action_t* a );
// queued_action_execute_event_t::execute(), first thing, for a FOREGROUND press: the poll at the fire instant. Returns true when it replaced the press (the queue
// event must do nothing more); false: the press stands, the event goes on to fire it.
bool queue_fire_poll( player_t* p, action_t* a );

// decision_dump::record(), right after it wrote the decision line of the RL actor: writes the resolve line of a decision that is already resolved (a wait, an
// idle row, a boundary that produced no decision); a staged decision's line follows at its commit or discard. No-op without a translog.
void dump_decision_written( sim_t* sim );
}
