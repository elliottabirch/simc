// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// Credit-by-cause for the RL transition log (tstl-sylvanas quick task
// 260918-cbc, stage A of 2 -- "why does a decision window's credit stay
// unpriced"). PURE OBSERVER: nothing in this file (or any of its call
// sites) touches the RNG, schedules or cancels an event, or reorders an
// action's execute/impact/tick. Every routine here is bookkeeping that
// answers, for a point of damage, WHICH DECISION caused it and WHAT KIND
// of event it was -- a decision's own cast, a later tail of an earlier
// decision (travel, delayed strikes, splash), a DoT tick, an auto-attack
// (and anything it set off), or an orphan with no cause context.
//
// The cause model: `player_t::execute_action()` sets `last_foreground_action`
// immediately before `action->queue_execute()`, so "the current decision"
// at any moment of a fight is `sim->solver_control_seq` (bumped once per
// solver decision in solver_control.cpp, BEFORE record_decision() writes
// the row carrying it). `action_t::execute()` stamps every
// `action_state_t` it creates with a `rl_cause_t` -- the decision seq plus
// an event class -- using the stamp rule in rl_credit_stamp_execute()'s
// call site (action.cpp). `action_state_t::copy_state()` carries the
// stamp into a DoT's own state at application/refresh, so a tick years
// later still remembers which decision applied it. A player-scoped cause
// STACK (`player_t::rl_cause_stack`) is pushed around `impact()` and the
// direct-tick assessment in `tick()`, and around the per-target loop in
// `execute()` itself, so a proc/secondary ability fired synchronously
// inside those scopes inherits the right cause by construction (promoted
// to its PROC_OF_* sibling class).
//
// Six cumulative per-fight streams, realized and expectation-corrected
// versions of each (twelve numbers total), fed by the SAME additions the
// engine already makes to `solver_damage_so_far` /
// `solver_damage_expected_so_far` -- so `sum(streams) == that total` is an
// identity, not a tolerance. See rl_translog.hpp's format-10 section for
// the on-disk layout.
#pragma once

#include <cstdint>

struct player_t;

// The event class an action_state_t's stamp carries. A plain enum (not
// `enum class`) with explicit RL_CAUSE_* names, matching the un-scoped
// style action_state_t's own result_e/block_result_e fields already use,
// so a reader can compare `state->rl_cause_class == RL_CAUSE_DOT_TICK`
// without a cast.
enum rl_cause_class : std::uint8_t
{
  RL_CAUSE_CAST          = 0,  // a decision's own foreground cast
  RL_CAUSE_PROC_OF_CAST  = 1,  // fired synchronously inside a CAST's execute/impact scope
  RL_CAUSE_DOT_TICK      = 2,  // a DoT tick assessed directly (no tick_action)
  RL_CAUSE_PROC_OF_DOT   = 3,  // fired synchronously inside a DOT_TICK's assessment scope
  RL_CAUSE_AUTO          = 4,  // an auto-attack swing (repeating, non-special action)
  RL_CAUSE_PROC_OF_AUTO  = 5,  // fired synchronously inside an AUTO's execute/impact scope
  RL_CAUSE_ORPHAN        = 6,  // no cause context -- timer/buff-driven, or a lost stack
};

// A stamp: which decision caused this, and what kind of event it was.
// `seq == -1` means "never stamped" (should not reach a sink; the sink
// routing treats an unstamped state the same as ORPHAN via the enum's
// default). A `seq` never exceeds the CURRENT `sim->solver_control_seq`
// at routing time -- asserted in rl_credit_route() under !NDEBUG.
struct rl_cause_t
{
  std::int64_t seq = -1;
  std::uint8_t cls = RL_CAUSE_ORPHAN;
};

namespace rl_credit
{

// Six cumulative per-fight credit streams, in this fixed index order --
// mirrored by the translog's CREDIT_BLOCK layout (rl_translog.hpp) and by
// stage B's Python-side decoder. Do not reorder without updating both.
inline constexpr std::uint32_t STREAM_COUNT = 6u;

inline constexpr const char* NAMES[ STREAM_COUNT ] = {
  "own_cast", "tail_cast", "own_dot", "tail_dot", "background", "orphan"
};

} // namespace rl_credit

// Struct-of-arrays so a translog row can memcpy each array into a flat
// block, mirroring rl_proc::counters_t's own convention.
struct rl_credit_streams_t
{
  double real[ rl_credit::STREAM_COUNT ] = {};  // realized (solver_damage_so_far's own additions)
  double exp[ rl_credit::STREAM_COUNT ] = {};   // expectation-corrected (solver_damage_expected_so_far's)

  void reset()
  {
    for ( auto& v : real ) v = 0.0;
    for ( auto& v : exp ) v = 0.0;
  }
};

// Routes one damage (or expected-damage) increment into the right credit
// stream. `p` is already resolved to the owning player (pets route to
// their owner before calling this, mirroring stats.cpp's own
// solver_damage_so_far pet->owner rule). `cause` is the stamp read off
// the contributing action_state_t (or, for the realized sinks that do not
// carry a state -- stats_t::add_result -- `p->rl_sink_cause`, set by
// assess_damage() immediately before the call that loses the state).
// `now_seq` is `sim->solver_control_seq` at the routing site. `expected`
// selects the realized vs expectation-corrected stream pair.
//
// `action_name`, when non-null and the cause resolves to the orphan
// stream on a REALIZED route, bumps `p->rl_orphan_damage_by_action[name]`
// -- a per-fight census (reset alongside solver_damage_so_far) that lets
// a diagnostic print name exactly which actions are landing with no
// cause context, rather than a bare aggregate percentage.
//
// Defined in rl_translog.cpp (needs player_t complete, mirroring
// rl_count_proc's own placement in rl_proc_counters.hpp).
void rl_credit_route( player_t* p, rl_cause_t cause, std::uint64_t now_seq, double amount, bool expected,
                       const char* action_name = nullptr );
