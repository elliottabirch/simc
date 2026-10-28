// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "solver_control.hpp"

#include "action/action.hpp"
#include "action/attack.hpp"
#include "buff/buff.hpp"
#include "decision_dump.hpp"
#include "player/action_priority_list.hpp"
#include "player/player.hpp"
#include "sim/cooldown.hpp"
#include "sim/event.hpp"
#include "sim/rl_button_bind.hpp"
#include "sim/rl_stock_eval.hpp"
#include "sim/rl_policy.hpp"
#include "sim/rl_table_policy.hpp"
#include "sim/rl_target_select.hpp"
#include "sim/rl_translog.hpp"
#include "sim/solver_branch.hpp"
#include "sim/sim.hpp"
#include "util/io.hpp"

#include "rapidjson/document.h"

#include "fmt/format.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace
{
// 2026-07-29 fix bundle: bumped 1 -> 2. Record-schema semantics changed on
// the wire/dump side (defects (b): resolved_action is now solver-reply-gated
// in decision_dump.jsonl; (c): permanent-buff remains sentinel representation
// changed from a raw negative number to null+"permanent":true; (d):
// gcd_length/auto_attack_interval are now player-scoped, not action-scoped) --
// not purely additive, so this is a real version bump, not 116-02's
// backward-compatible one. Both sides of the wire (this constant and
// episode-driver.py's PROTOCOL_VERSION) must move in lockstep.
constexpr int SOLVER_CONTROL_PROTOCOL_VERSION = 2;

// 260902/FORK-04 (226-05-PLAN.md Task 1): a hand-authored, LOCAL constant --
// deliberately NOT added to the registry-generated rl_policy_constants.h
// (this phase's own prohibition: "No maskRules entry, no regeneration or
// hand-edit of rl_policy_constants.h"). Used only by accept_wait()'s
// fight-end clamp below to keep a clamped wait strictly before
// `sim->expected_iteration_time`; see that call site for the full mechanism.
constexpr double WAIT_END_OF_FIGHT_EPSILON_SECONDS = 0.001;

[[noreturn]] void protocol_abort( const std::string& msg )
{
  fmt::print( stderr, "solver_control: FATAL: {}\n", msg );
  throw std::runtime_error( "solver_control protocol violation: " + msg );
}

// ==========================================================================
// 261027-fts stage D: POLL MODE (the contract is in solver_control.hpp, "THE DECISION WINDOW"). A window poll or a queue re-poll runs choose() with g_poll set:
// the policy decides (read_state at a real decision boundary, mask, forward pass or the table's pick, the pick's target) and NOTHING ELSE happens: no row
// written or staged, no decision number kept, no retarget, no wait set, no auto-attack re-arm, no forced decision, no aim dial. What the decision was is
// left in g_poll_out for the caller.
// ==========================================================================
struct poll_out_t
{
  enum class kind_t : std::uint8_t { none, cast, wait };
  kind_t                        kind = kind_t::none;
  action_t*                     action = nullptr;  // the resolved cast (kind cast)
  int                           idx = -1;          // RL_ACTIONS index
  std::uint16_t                 target = rl_translog::CHOSEN_TARGET_SENTINEL_NO_PICK;
  bool                          explored = false;  // the button dial fired
  bool                          has_rec = false;
  rl_translog::decision_record  rec{};
  bool                          has_stock = false;
  rl_stock_eval::pick_t         stock{};
  bool                          has_table = false;
  rl_table_policy::decision_t   table{};
};
// The window / poll state is thread_local: a worker thread runs its own sim_t (threads > 1 without a translog is legal for a net arm), so one thread's
// window must never be seen or overwritten by another's. (The stage-B pending rows stay process-global: a translog forces threads=1.)
thread_local bool       g_poll = false;               // this choose() call is a poll
thread_local bool       g_skip_button_dial = false;   // the poll must not draw the exploration (button) dial
thread_local poll_out_t g_poll_out;
constexpr int WINDOW_MAX_REPLACES = 4;   // flip-flop bound (DECISIONS.md): a window or a queue may replace its pick at most this many times

// The last cast choose() handed to the engine (not a poll): queue_window_open() reads its target and exploration flag.
struct last_pick_t
{
  bool          valid = false;
  action_t*     action = nullptr;
  int           idx = -1;
  std::uint16_t target = rl_translog::CHOSEN_TARGET_SENTINEL_NO_PICK;
  bool          explored = false;
};
thread_local last_pick_t g_last_pick;

// The shared action resolver is solver_control::resolve_action(), DEFINED below inside `namespace solver_control`
// (221-01, Pattern 2) so `rl_policy_obs.cpp`'s engine-truth handle table (read_action_gate_bits) and this file's
// own accept_cast() resolve a token to the SAME action_t*, by construction, never by two independently-maintained
// loops that could drift. Phase 271.3: it reads the bound table of rl_button_bind (the action whose list is the
// actor's default list and whose recorded line declares the token, fixed once at sim init), and never scans the
// action list by name.

// Shared 'cast' epilogue (210-05R Task 1) -- both transports resolve their
// chosen action-name token through the same resolve_action()/ready() gate,
// so a not-ready cast hits the identical protocol_abort() on either arm
// (criterion 1's "zero not-ready FATALs" is one grep regardless of which
// transport answered the boundary).
//
// Phase 271.3: the shared resolver returns the action bound to the token at sim init (identity of list and
// line), or null; a token outside the header resolves to null. The existing unresolvable and not-ready aborts
// below are unchanged.
action_t* accept_cast( player_t* p, const std::string& action_name, std::uint64_t seq )
{
  action_t* resolved = solver_control::resolve_action( p, action_name );
  if ( !resolved )
    protocol_abort( "'cast' reply named an unresolvable action '" + action_name + "' (seq=" +
                     std::to_string( seq ) + ")" );
  if ( !resolved->ready() )
    protocol_abort( "'cast' reply named a not-ready action '" + action_name + "' (seq=" +
                     std::to_string( seq ) + ")" );

  // 228-02 (D-13, TGT-02): for exactly the eight targeted registry actions, apply the SAME pick
  // read_action_gate_bits already computed for this decision (rl_policy_obs.cpp's fill_pick) --
  // READ, never recomputed (D-12). A pick not stamped for THIS decision is a mask/cast
  // disagreement (T-228-02-02) and refuses by name here, in this file's own protocol_abort idiom,
  // rather than silently recomputing a possibly-different answer.
  //
  // CR-05 (260902/cr4): `is_targeted_action` is a pure NAME match; additionally scoped here to the
  // RL-controlled actor (exact strcmp against RL_ACTOR_NAME, never a prefix -- mirrors choose()'s
  // own gate at this file's :514), non-pet -- belt-and-braces alongside rl_policy_obs.cpp's own
  // actor scope, since accept_cast is reached only via the FIFO/in-process "cast" reply the driver
  // sends for the RL actor it controls, never structurally guaranteed by this file alone.
  // CR-06 (260902/cr4): the selector kill switch -- off means accept_cast never retargets/turns
  // for a registry action, matching the pre-228-02 cast path.
  const bool is_rl_actor = std::strcmp( p->name(), RL_ACTOR_NAME ) == 0 && !p->is_pet();
  if ( p->sim->target_select_enabled && is_rl_actor && rl_target_select::is_targeted_action( resolved ) )
  {
    bool      found = false;
    player_t* pick  = rl_target_select::lookup_pick( resolved, &found );
    if ( !found || !pick )
      protocol_abort( "'cast' reply named targeted action '" + action_name +
                       "' (seq=" + std::to_string( seq ) +
                       ") with no pick stamped for this decision -- refusing rather than "
                       "recomputing (228-02, D-12)" );

    // 261013-aimx class A, the loud invariant: a button the header flags as health-gated is aimed only among the enemies that pass its
    // gate (rl_target_select's candidate list), so its final pick must pass the engine's own target_ready. A pick that escaped the
    // filter (a future exploration path, a head pick outside the stamped block) would otherwise cast silently on an enemy the engine
    // would refuse, which is how the defect lived for months. Abort by name, never repair.
    if ( rl_target_select::aimed_action_health_gated( resolved ) && !resolved->target_ready( pick ) )
      protocol_abort( "'cast' reply named health-gated targeted action '" + action_name + "' (seq=" + std::to_string( seq ) +
                       ") whose pick fails the engine's own target gate -- the aim candidate list must hold only enemies that pass it "
                       "(261013-aimx class A)" );

    // WR-07 (260902/cr4): the ONE retarget function this call site and the mid-cast re-resolution
    // ladder's fallback arm (action.cpp) both call -- see rl_target_select.hpp's own doc comment.
    // Casting on a unit IS targeting it in game (D-13, ledger R11): the player's own target and
    // BOTH weapon swings follow the pick, so white damage (Windfury/Flametongue procs) lands on
    // the enemy the agent is actually fighting, not a stale prior target. `solver_control.cpp`'s
    // own FOREGROUND re-arm (below, :395-401 in this file) guarantees both swing objects are live
    // by the time any cast reaches this function.
    // CR-04 (260902/cr4): the turn (D-05's second call site) is now folded INTO retarget() itself
    // -- see that function's own doc comment (rl_target_select.cpp) -- so it is no longer a
    // separate call at this site.
    if ( !g_poll )  // 261027-fts stage D: a window poll decides; the player turns to the pick only when the press is made
      rl_target_select::retarget( resolved, p, pick );
  }

  return resolved;
}

// FORK-04, second half (owner ruling Q69, 232-07-PLAN.md Task 1): closes the 1ms-wide
// fight-end-tie band WR-01 (260902/cr2) measured in accept_wait()'s CR-04 re-floor -- see that
// function's own comment above the re-floor for the full derivation (at R=50ms remaining,
// `0.050 - 0.001 = 0.049` re-floors to exactly `0.05`, landing the Player-Ready event back on the
// tie the first shave removed). The removed turn reply arm (R6-27, 233.1-01) had the SAME
// exposure through a different route: it requested a FIXED `RL_WAIT_FLOOR_SECONDS` unconditionally, with no prior fight-end
// clamp at all, so it can land exactly on the tie whenever the fight has close to that much time
// left. One shared shave closes both: recompute the hard bound fresh (seconds remaining to
// `expected_iteration_time`, minus the epsilon) and clamp `sec` down to it only when the bound is
// POSITIVE and `sec` exceeds it. When the bound is zero or negative, leave `sec` alone -- the
// wait/turn then deliberately lands PAST fight end, which is already correct behaviour (the
// engine re-decides at the next boundary regardless, or the iteration simply ends) and must not
// change; a seeded-defect twin (`scripts/rl/probes/fight_end_shave.py`) is red without this
// function reddening the pre-fix tie and green with it.
void reshave_fight_end_tie( sim_t* sim, double& sec )
{
  const double hard_bound =
      ( sim->expected_iteration_time - sim->current_time() ).total_seconds() -
      WAIT_END_OF_FIGHT_EPSILON_SECONDS;
  if ( hard_bound > 0.0 && sec > hard_bound )
    sec = hard_bound;

  // R-AI (232-13, chain-B1 review HI-02 parts 2-3): the band this reshave actually re-clamps,
  // stated explicitly rather than only in the surrounding narrative comments above. Every final
  // wait/turn with fight-remaining R in
  // (WAIT_END_OF_FIGHT_EPSILON_SECONDS, RL_WAIT_FLOOR_SECONDS + WAIT_END_OF_FIGHT_EPSILON_SECONDS]
  // -- concretely (1 ms, 51 ms] -- lands at `R - 1 ms` (one extra agent decision one millisecond
  // before fight end), where the pre-FORK-04-second-half code let the wait overshoot past fight
  // end harmlessly for every R in that band. KEPT deliberately, not narrowed to the exact
  // R == FLOOR tie: the first shave already produced this same final decision for every
  // R >= RL_WAIT_FLOOR_SECONDS + WAIT_END_OF_FIGHT_EPSILON_SECONDS, so this reshave makes the
  // behaviour UNIFORM ("every fight's last wait lands one millisecond before the end") rather than
  // reintroducing a ~49 ms discontinuity at exactly R == FLOOR -- the uniform semantic is better
  // training data. Legality is unaffected either way (R-AI); only the realised final-wait DURATION
  // in that band differs between the tie-only and uniform readings. The `sec > 0` invariant this
  // leaves in force: `timespan_t`'s integer-millisecond quantisation keeps `hard_bound` (an
  // integer number of ms minus 1 ms) strictly positive whenever `hard_bound > 0.0` fires above, so
  // this can never schedule a zero-length wait/turn -- asserted below, never merely assumed.
  //
  // R-AI (232-13): `scripts/rl/mask.py`'s own `anchored_wait_seconds` mirror clamps its
  // pre-floor value to `request["fight_remains"]` (when present) THEN floors at
  // `WAIT_FLOOR_SECONDS` -- it does NOT re-apply this second, POST-floor reshave. That mirror is
  // engine-side documented as non-authoritative already (its own docstring: "the AUTHORITATIVE
  // clamp is the engine's accept_wait"), so this gap is a legality/duration MODEL gap only: the
  // wait/turn stays LEGAL on both sides in this band, and only the mirror's own predicted
  // `seconds` value (never consulted for legality, only for the FIFO transport's own emitted
  // duration) can differ from what this function actually schedules.
  if ( sec <= 0.0 )
  {
    protocol_abort( "reshave_fight_end_tie produced a non-positive wait/turn duration (sec=" +
                     std::to_string( sec ) +
                     ") -- the RL_WAIT_FLOOR_SECONDS/epsilon invariant this function depends on "
                     "has broken" );
  }
}

// Shared 'wait' epilogue (210-05R Task 1). The et != FOREGROUND refusal is
// kept here -- not dropped as "redundant now that the in-process mask
// already forbids a wait pick off-foreground" -- as defence-in-depth: a
// mask defect should fail loud via protocol_abort, not silently leak a
// wait into a non-FOREGROUND boundary. Never calls player_t::schedule_ready()
// (it throws if `readying` is already non-null, not yet cleared at this
// point in the call stack) -- sets the pending-wait pair that
// player_ready_event_t::execute()'s "nothing chosen" branch (player.cpp)
// consumes instead.
void accept_wait( sim_t* sim, execute_type et, double sec, const std::string& context )
{
  if ( et != execute_type::FOREGROUND )
    protocol_abort( "'wait' reply is illegal at a non-FOREGROUND boundary (boundary=" +
                     decision_dump::boundary_name( et ) + "): " + context );
  if ( sec < 0.0 )
    sec = 0.0;

  // 221-03 (ACT-05/ACT-06, Task 2 point 3): clamp to the lesser of the
  // requested seconds, the remaining fight, and the next raid-event time --
  // the ONE point both transports pass through, which is why the clamp
  // cannot live only in build_wait() (the FIFO arm's seconds are computed
  // in Python and would arrive here unclamped). Both bounds computed
  // through the SAME rl_policy::next_raid_event_in()/fight_remains
  // definitions read_state() uses -- never a third independent walk of
  // sim->raid_events. No raid event pending (next_raid_event_in() returns
  // false) means no clamp from that source, per read_state()'s own policy.
  //
  // 260902/FORK-04 (226-05-PLAN.md Task 1, `226-STALL-RECEIPT.md` Section 9):
  // the fight-end bound is shaved by WAIT_END_OF_FIGHT_EPSILON_SECONDS so the
  // resulting Player-Ready event lands STRICTLY before `expected_iteration_time`,
  // never exactly on it. Without this, a wait clamped to exactly
  // `fight_remaining` schedules `player_ready_event_t` at the SAME timestamp
  // as `sim_end_event_t` (sim.cpp:2026, `make_event<sim_end_event_t>(*this,
  // *this, expected_iteration_time)`). `sim_end_event_t` is inserted once,
  // near iteration start (`sim.cpp:2026`), so it always carries a lower
  // insertion id than any mid-fight-scheduled ready event; same-timestamp
  // ties in `event_manager_t::add_event` resolve by ascending insertion id,
  // so `sim_end_event_t::execute()` (`sim.cpp:1017-1020`, `cancel_iteration()`)
  // always runs FIRST and tears the iteration down before the tied
  // Player-Ready event's callback is ever reached -- silently discarding
  // the actor's own re-decision. This directly contradicts this function's
  // own documented invariant two paragraphs below ("Waiting a hair past a
  // raid event or past fight end is harmless (the engine re-decides at the
  // next boundary either way)") for the one case that invariant did not
  // anticipate: fight-end EXACTLY, not "a hair past" it. Measured
  // (226-05-PLAN.md Task 2's twenty-seed sweep, `pairB.simc`-derived, seeds
  // 5 and 31337): the actor's decision stream silently and permanently
  // stopped at t=573.916 and t=392.000 respectively, in both cases because
  // the immediately-prior wait request (28.86s / 233.024s) was clamped to
  // land at EXACTLY `expected_iteration_time` (600.000s). Fixed at the
  // clamp's own source, per-decision, as a bound adjustment only -- never a
  // process external to this function that notices the actor went quiet and
  // pokes it back to life.
  //
  // WR-01 (260902/cr2 code review, measured, NOT an absolute invariant as
  // previously claimed here): the CR-04 re-floor below (`sec <
  // RL_WAIT_FLOOR_SECONDS -> RL_WAIT_FLOOR_SECONDS`) runs AFTER this shave
  // and CAN put `sec` back exactly on the tie. Walking `timespan_t`'s
  // integer-millisecond arithmetic (`from_seconds` truncates): at R = 50ms
  // remaining, `fight_remaining` = 0.050 - 0.001 = 0.049, then
  // `0.049 < 0.05` re-floors to 0.05 -- `from_seconds(0.05)` = 50 ticks,
  // landing the Player-Ready event at EXACTLY `expected_iteration_time`
  // again, tying with `sim_end_event_t` a second time. So the shave does
  // NOT hold across every `sec` -- it holds everywhere except a 1ms-wide
  // band at R=50ms, where the outcome is the same harmless-in-practice
  // loss (one re-decision skipped in the fight's final 50ms) the pre-fix
  // bug caused at every R, just far rarer. FORK-04, second half (owner
  // ruling Q69, 232-07-PLAN.md): re-shaving after the re-floor closes this
  // exactly -- see `reshave_fight_end_tie()` above and its call site
  // immediately after the re-floor below. Everything below this point up
  // to that call is otherwise byte-identical to before this fix.
  const double fight_remaining = std::max(
      ( sim->expected_iteration_time - sim->current_time() ).total_seconds() -
          WAIT_END_OF_FIGHT_EPSILON_SECONDS,
      0.0 );
  if ( fight_remaining < sec )
    sec = fight_remaining;
  double raid_event_bound = 0.0;
  if ( rl_policy::next_raid_event_in( sim, raid_event_bound ) && raid_event_bound < sec )
    sec = raid_event_bound;

  // CR-04 (221-08 code review): re-apply the floor AFTER the fight-end/
  // raid-event clamp above, not just before it. Both bounds can legally
  // drive `sec` below RL_WAIT_FLOOR_SECONDS -- a raid event 1ms away, or
  // `fight_remaining == 0.0` when the iteration has already ended -- and
  // WITHOUT this re-floor, `sec` could land at exactly 0.0.
  // `player_ready_event_t::execute()` (player.cpp) then schedules a fresh
  // Player-Ready event at `timespan_t::from_seconds(0.0)`, at the SAME
  // timestamp against the SAME state: a deterministic policy re-picks the
  // (still legal, `anchor: null`) fixed wait and the loop repeats at zero elapsed time (the
  // removed turn reply arm, R6-27/233.1-01, quoted this paragraph verbatim -- the SAME hazard
  // applied to a free turn before it was deleted). This is exactly the spin `maskRules.waitZeroLengthForbidden`
  // (enhancement.json) declares forbidden -- both `mask.py` and this
  // file's own `build_wait()` already floor at RL_WAIT_FLOOR_SECONDS
  // consistently; this was the ONE point that clamped without re-flooring.
  // Waiting a hair past a raid event or past fight end is harmless (the
  // engine re-decides at the next boundary either way); a zero-length wait
  // is the one outcome the floor exists to forbid.
  if ( sec < RL_WAIT_FLOOR_SECONDS )
    sec = RL_WAIT_FLOOR_SECONDS;

  // FORK-04, second half (Q69/232-07): the re-floor immediately above can put `sec` back
  // exactly onto the fight-end tie the shave earlier in this function removed -- re-apply it
  // now, after the floor, via the shared helper. See `reshave_fight_end_tie()`'s own doc
  // comment for the full derivation and the non-positive-bound (deliberately unchanged) case.
  reshave_fight_end_tie( sim, sec );

  sim->solver_control_pending_wait_s = sec;
  sim->solver_control_has_pending_wait = true;
}

// 261021-ezq: the registry index of the fixed next-event wait (label "wait_next_event"), resolved by label once. Observer mode
// records an idle stretch as this button.
int observer_wait_index()
{
  static const int index = []() {
    for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
    {
      if ( RL_ACTIONS[ i ].kind == rl_action_kind::wait && RL_ACTIONS[ i ].label != nullptr &&
           std::strcmp( RL_ACTIONS[ i ].label, "wait_next_event" ) == 0 )
        return static_cast<int>( i );
    }
    return -1;
  }();
  if ( index < 0 )
    protocol_abort( "OBSERVE_STOCK_IDLE_WAIT_ILLEGAL: the registry has no wait action labelled wait_next_event" );
  return index;
}

// The removed turn reply arm's own function (R6-27) was here; deleted along with
// every layer of the turn action (233.1-01 Task 3).
} // anonymous namespace

namespace solver_control
{
// 221-01 (ACT-02, Pattern 2). Exposed so `rl_policy_obs.cpp`'s engine-truth
// handle table (read_action_gate_bits) and accept_cast() above resolve a
// token to the SAME action_t* -- this is the single seam that makes
// "mask/engine agreement" true by construction rather than by census.
//
// Phase 271.3 (BIND-01): the token resolves through rl_button_bind, which fixed
// at sim init, for every cast button of the registry header, the one action
// whose action list is the actor's default list and whose recorded line
// declares the token. Nothing here scans the action list by name, so a
// same-name line in any other list (a hand-written sub-list, a precombat line,
// a class module's helper) can never be a button, whatever order the lists
// were constructed in. Returns nullptr for a token outside the header, for an
// actor that is not the registry's actor, and for an unbound actor.
action_t* resolve_action( player_t* p, const std::string& name )
{
  return rl_button_bind::bound_action_for_token( p, name );
}

// 266-21 (funnel mode, record format 13): the actor index of the chosen enemy (the tag) a decision row
// carries, 0xFFFF (rl_translog::CHOSEN_TARGET_SENTINEL_NO_PICK) when none is set. Reads the field
// `rl_target_select::refresh_chosen` wrote (the tag is written nowhere else); a player that is not the RL
// actor never gets a tag, so it reads as no pick.
static std::uint16_t chosen_enemy_index_of( const player_t* p )
{
  return p->rl_chosen_enemy != nullptr ? static_cast<std::uint16_t>( p->rl_chosen_enemy->actor_index )
                                       : rl_translog::CHOSEN_TARGET_SENTINEL_NO_PICK;
}

// 261005-tch2 (solver_mask_no_enemy): true when no enemy can be attacked. 271-10 (owner decision T-2): "attackable" is now "can be
// hit by some aimed action" (rl_target_select::rl_can_be_hit: alive, a counted enemy, not invulnerable, not untargetable, and
// within the range of at least one aimed spell), so the walk between packs is when NO aimed spell can hit anything -- an immune
// boss, a hazard, or every enemy out of every aimed spell's range. Read live at the boundary, so a pack that arrived earlier in
// the same millisecond counts. `p` is the RL actor (both call sites are past choose()'s actor gate).
static bool no_attackable_enemy( player_t* p )
{
  for ( player_t* t : p->sim->target_non_sleeping_list )
    if ( rl_target_select::rl_can_be_hit( p, t ) )
      return false;
  return true;
}

// ==========================================================================
// 261027-fts stage B: decision rows stamped at execution. The contract is in solver_control.hpp; this is the machinery.
// ==========================================================================
interrupt_cause_t& current_interrupt_cause()
{
  static interrupt_cause_t cause = interrupt_cause_t::other;
  return cause;
}

namespace
{
inline bool is_rl_actor( const player_t* p )
{
  return std::strcmp( p->name(), RL_ACTOR_NAME ) == 0 && !p->is_pet();
}

inline sim_t* root_sim( sim_t* s )
{
  while ( s->parent )
    s = s->parent;
  return s;
}

inline bool translog_on( sim_t* s )
{
  return !root_sim( s )->rl_translog_file_str.empty();
}

// One decision's row, filled at the decision and held until its press becomes irrevocable.
struct pending_row_t
{
  bool                                  valid = false;
  bool                                  queued = false;          // false: STAGED, true: QUEUED
  bool                                  seq_given_back = false;  // true while QUEUED: solver_control_seq does not count this row yet
  sim_t*                                sim = nullptr;
  player_t*                             p = nullptr;
  action_t*                             action = nullptr;
  rl_translog::decision_record          rec{};
  std::uint64_t                         seq = 0;
  bool                                  has_apl_name = false;
  std::string                           apl_name;
  bool                                  has_stock = false;
  rl_stock_eval::pick_t                 stock;
  bool                                  has_table = false;
  rl_table_policy::decision_t           table;
  timespan_t                            decided_at = timespan_t::zero();
  timespan_t                            expected_fire = timespan_t::zero();
  bool                                  explored = false;  // 261027-fts stage D: the button dial fired (a queue re-poll never replaces an explored press)
  bool                                  dump_line = true;  // false: a press a re-poll made (no decision line in the dump), so its commit / discard writes no resolve line
};

pending_row_t g_staged;
pending_row_t g_queued;

// ==========================================================================
// 261027-fts stage D: the HOLD of a decision window (solver_control.hpp). One at a time, the RL actor's.
// ==========================================================================
struct hold_t
{
  bool                          valid = false;
  sim_t*                        sim = nullptr;
  player_t*                     p = nullptr;
  poll_out_t::kind_t            kind = poll_out_t::kind_t::none;
  int                           idx = -1;
  std::uint16_t                 target = rl_translog::CHOSEN_TARGET_SENTINEL_NO_PICK;
  bool                          explored = false;
  bool                          has_rec = false;
  rl_translog::decision_record  rec{};
  bool                          has_stock = false;
  rl_stock_eval::pick_t         stock{};
  bool                          has_table = false;
  rl_table_policy::decision_t   table{};
  timespan_t                    decided_at = timespan_t::zero();
  int                           replaces = 0;
};
struct window_t
{
  bool       active = false;   // a window is running for `p`: the poll chain is armed or ran, and the fire has not happened
  sim_t*     sim = nullptr;
  player_t*  p = nullptr;
  event_t*   ev = nullptr;     // the next poll event (nullptr once it fired, was cancelled or the chain ran out)
  bool       drawn = false;    // the first poll of the window drew the exploration (button) dial
  hold_t     hold;
};
thread_local window_t g_win;

// The re-poll chain of a QUEUED press (a cast whose cooldown ends after the instant it was pressed).
struct queue_poll_t
{
  bool       valid = false;
  sim_t*     sim = nullptr;
  player_t*  p = nullptr;
  action_t*  action = nullptr;
  std::uint16_t target = rl_translog::CHOSEN_TARGET_SENTINEL_NO_PICK;
  bool       explored = false;
  int        replaces = 0;
  event_t*   ev = nullptr;
};
thread_local queue_poll_t g_q;
thread_local int          g_q_replaces_next = 0;  // carried into the chain of the press that replaced a queued one

// The resolve line of the decision-dump (D12). g_line_written: the decision line of the current boundary is already in the file.
struct dump_outcome_t
{
  bool        set = false;
  const char* outcome = "none";
  const char* reason = nullptr;
  double      t = 0.0;
  double      t_decided = 0.0;
  long long   seq = -1;
};
dump_outcome_t g_dump_outcome;
bool           g_line_written = true;

[[noreturn]] void pending_invariant( const std::string& what )
{
  protocol_abort( "PENDING_INVARIANT: " + what );
}

inline void count( std::uint32_t slot )
{
  ++rl_translog::fight_census().n[ slot ];
}

const char* const DISCARD_NAMES[] = { "queue_failed:not_ready", "queue_failed:target_not_ready", "cooldown_reset", "ready_cancelled",
                                      "ready_preempts_queue", "replaced", "fight_end", "branch_cut", "staged_not_queued" };
const char* const INTERRUPT_NAMES[] = { "interrupt:other", "interrupt:movement", "interrupt:stun", "interrupt:invulnerable", "interrupt:raid_event" };
// interrupt_cause_t order is other, movement, stun, invulnerable, raid_event; census slots 0-4 are movement, stun, invulnerable, raid_event, other.
constexpr std::uint32_t interrupt_slot( interrupt_cause_t c )
{
  switch ( c )
  {
    case interrupt_cause_t::movement: return rl_translog::census_slot::interrupt_movement;
    case interrupt_cause_t::stun: return rl_translog::census_slot::interrupt_stun;
    case interrupt_cause_t::invulnerable: return rl_translog::census_slot::interrupt_invulnerable;
    case interrupt_cause_t::raid_event: return rl_translog::census_slot::interrupt_raid_event;
    case interrupt_cause_t::other: break;
  }
  return rl_translog::census_slot::interrupt_other;
}
static_assert( rl_translog::census_slot::queue_failed_not_ready == 5 && rl_translog::census_slot::staged_not_queued == 13 &&
                   rl_translog::census_slot::DISCARD_COUNT == 14,
               "discard_reason order is census slots 5..13" );

void dump_resolve( sim_t* sim, const char* outcome, const char* reason, double t, double t_decided, long long seq )
{
  if ( sim->decision_dump_file_str.empty() )
    return;
  if ( g_line_written )
  {
    decision_dump::record_resolve( sim, outcome, reason, t, t_decided, seq );
    return;
  }
  g_dump_outcome.set = true;
  g_dump_outcome.outcome = outcome;
  g_dump_outcome.reason = reason;
  g_dump_outcome.t = t;
  g_dump_outcome.t_decided = t_decided;
  g_dump_outcome.seq = seq;
}

void commit_pending( pending_row_t& pd, bool via_queue_event )
{
  sim_t*    sim = pd.sim;
  player_t* p = pd.p;
  if ( pd.seq_given_back )
  {
    if ( sim->solver_control_seq + 1 != pd.seq )
      pending_invariant( fmt::format( "commit of queued decision seq={} but the counter is {} (it must be {})", pd.seq, sim->solver_control_seq, pd.seq - 1 ) );
    ++sim->solver_control_seq;
  }
  else if ( sim->solver_control_seq != pd.seq )
    pending_invariant( fmt::format( "commit of staged decision seq={} but the counter is {}", pd.seq, sim->solver_control_seq ) );
  rl_translog::stamp_commit( sim, p, pd.rec );
  rl_translog::write_row( sim, p, pd.rec, pd.has_apl_name ? pd.apl_name.c_str() : nullptr, pd.has_stock ? &pd.stock : nullptr,
                          pd.has_table ? &pd.table.info : nullptr );
  if ( pd.has_table )
    rl_table_policy::commit( sim, p, pd.seq, pd.table );
  const timespan_t now = sim->current_time();
  if ( via_queue_event )
  {
    count( rl_translog::census_slot::committed_queued );
    if ( now - pd.expected_fire > timespan_t::from_millis( 2 ) )
      count( rl_translog::census_slot::delayed );
  }
  if ( pd.dump_line )
    dump_resolve( sim, "commit", nullptr, now.total_seconds(), pd.decided_at.total_seconds(), static_cast<long long>( pd.seq ) );
  pd.valid = false;
}

void discard_pending( pending_row_t& pd, std::uint32_t slot, const char* name )
{
  sim_t* sim = pd.sim;
  if ( !pd.seq_given_back )
  {
    // STAGED: the decision number was taken at the decision and nothing was stamped with it yet that a row will carry; give it back.
    if ( sim->solver_control_seq != pd.seq )
      pending_invariant( fmt::format( "discard of staged decision seq={} but the counter is {}", pd.seq, sim->solver_control_seq ) );
    --sim->solver_control_seq;
  }
  count( slot );
  if ( pd.dump_line )
    dump_resolve( sim, "discard", name, sim->current_time().total_seconds(), pd.decided_at.total_seconds(), -1 );
  pd.valid = false;
}

// A new boundary of the RL actor: whatever the last boundary left STAGED was not queued by the engine (an off-GCD pick for the action already queued,
// or one that was not usable). It is resolved here, before the dump flags move on to the new boundary.
void begin_boundary( player_t* p, execute_type et )
{
  sim_t* sim = p->sim;
  if ( g_staged.valid )
  {
    if ( g_staged.sim == sim )
      discard_pending( g_staged, rl_translog::census_slot::staged_not_queued, DISCARD_NAMES[ static_cast<int>( discard_reason::staged_not_queued ) ] );
    else
      pending_invariant( "a staged decision of another simulator" );
  }
  if ( g_queued.valid && et == execute_type::FOREGROUND )
    pending_invariant( fmt::format( "a foreground decision at t={:.3f} with the queued press '{}' of seq={} still unresolved (its queue was taken away without a hook)",
                                    sim->current_time().total_seconds(), g_queued.action ? g_queued.action->name() : "?", g_queued.seq ) );
  g_line_written = false;
  g_dump_outcome = dump_outcome_t{};
}

// The row of one decision. deferred = a cast that goes through queue_execute(): staged, committed or discarded later. Not deferred (wait, idle, a decision with
// no press): committed at once, byte for byte what record_decision() wrote.
// 261027-fts stage D: in POLL mode the filled row is only kept (g_poll_out). At the fire instant (an ordinary decision) the HOLD of the window is resolved here:
// a decision for the same button and target commits the hold's payload (the observation, mask, picks, flags and stock/table record of the poll that selected
// it, `t_decided` = that poll) -- the hold was counted as staged when it was made, so nothing is counted again; anything else drops the hold (census `replaced`).
void submit_decision( bool deferred, action_t* act, const rl_table_policy::decision_t* table, sim_t* sim, player_t* p, std::uint64_t seq,
                      const float obs[ RL_OBS_DIM ], const std::uint8_t mask[ RL_ACTION_DIM ], int action_index, float q_margin, float top_q,
                      std::uint16_t chosen_target_actor_index, bool wait_floored, bool exploratory, bool held, const float* candidate_features,
                      std::uint16_t candidate_mask, std::uint8_t candidate_count, std::uint8_t chosen_candidate_slot, const char* apl_choice_name,
                      bool aim_explored, std::uint8_t rules_candidate_slot, std::uint8_t observed_candidate_slot,
                      std::uint16_t chosen_enemy_actor_index, const rl_stock_eval::pick_t* stock_pick )
{
  if ( !translog_on( sim ) )
    return;
  rl_translog::decision_record r = rl_translog::fill_decision( sim, p, seq, obs, mask, action_index, q_margin, top_q, chosen_target_actor_index, wait_floored,
                                                               exploratory, held, candidate_features, candidate_mask, candidate_count, chosen_candidate_slot,
                                                               aim_explored, rules_candidate_slot, observed_candidate_slot, chosen_enemy_actor_index );
  if ( g_poll )
  {
    g_poll_out.has_rec = true;
    g_poll_out.rec = r;
    g_poll_out.has_stock = stock_pick != nullptr;
    if ( stock_pick != nullptr )
      g_poll_out.stock = *stock_pick;
    g_poll_out.has_table = table != nullptr;
    if ( table != nullptr )
      g_poll_out.table = *table;
    return;
  }
  timespan_t                         decided_at = sim->current_time();
  bool                               merged = false;
  const rl_stock_eval::pick_t*       stock_use = stock_pick;
  const rl_table_policy::decision_t* table_use = table;
  if ( g_win.hold.valid && g_win.hold.sim == sim && g_win.hold.p == p )
  {
    hold_t& h = g_win.hold;
    if ( h.has_rec && ( h.kind == poll_out_t::kind_t::cast ) == deferred && h.idx == action_index && h.target == chosen_target_actor_index )
    {
      r = h.rec;
      r.seq = static_cast<std::uint32_t>( seq );
      decided_at = h.decided_at;
      stock_use = h.has_stock ? &h.stock : nullptr;
      table_use = h.has_table ? &h.table : nullptr;
      merged = true;
      if ( !deferred )
        rl_translog::stamp_commit( sim, p, r );  // a wait commits at once: its counters are the fire instant's
    }
    else
      count( rl_translog::census_slot::replaced );
    // The hold is consumed. Its storage (stock_use / table_use point into it) is not touched again until the next hold is made.
    h.valid = false;
  }
  if ( !merged )
    count( rl_translog::census_slot::staged );
  if ( !deferred )
  {
    rl_translog::write_row( sim, p, r, apl_choice_name, stock_use, table_use != nullptr ? &table_use->info : nullptr );
    if ( table_use != nullptr )
      rl_table_policy::commit( sim, p, seq, *table_use );
    const double now = sim->current_time().total_seconds();
    dump_resolve( sim, "commit", nullptr, now, decided_at.total_seconds(), static_cast<long long>( seq ) );
    return;
  }
  if ( g_staged.valid )
    pending_invariant( fmt::format( "a second staged decision (seq={}) while seq={} is staged", seq, g_staged.seq ) );
  pending_row_t& pd = g_staged;
  pd.valid = true;
  pd.queued = false;
  pd.seq_given_back = false;
  pd.sim = sim;
  pd.p = p;
  pd.action = act;
  pd.rec = r;
  pd.seq = seq;
  pd.has_apl_name = apl_choice_name != nullptr;
  pd.apl_name = apl_choice_name != nullptr ? apl_choice_name : "";
  pd.has_stock = stock_use != nullptr;
  if ( stock_use != nullptr )
    pd.stock = *stock_use;
  pd.has_table = table_use != nullptr;
  if ( table_use != nullptr )
    pd.table = *table_use;
  pd.decided_at = decided_at;
  pd.expected_fire = sim->current_time();
  pd.explored = ( r.flags & rl_translog::FLAG_EXPLORATORY ) != 0;
  pd.dump_line = true;
}

// A throw out of accept_cast()/accept_wait() must not lose the row of the decision that was refused: it is exactly the row a later investigation wants.
void flush_staged_on_abort( sim_t* sim )
{
  if ( g_staged.valid && g_staged.sim == sim )
    commit_pending( g_staged, false );
}

// ==========================================================================
// 261027-fts stage D: the decision window and the re-poll (the contract is in solver_control.hpp).
// ==========================================================================
void window_log( sim_t* sim, const char* kind, const char* outcome, int idx )
{
  const std::string& path = window_log_storage();
  if ( path.empty() )
    return;
  static std::mutex    m;
  std::lock_guard<std::mutex> lock( m );
  static std::ofstream f;
  static bool          opened = false;
  if ( !opened )
  {
    f.open( path, std::ios::out | std::ios::trunc );
    opened = true;
  }
  if ( f.is_open() )
  {
    f << fmt::format( "{{\"kind\":\"{}\",\"outcome\":\"{}\",\"t\":{:.6f},\"iteration\":{},\"idx\":{}}}\n", kind, outcome,
                     sim->current_time().total_seconds(), sim->current_iteration, idx );
    f.flush();
  }
}

// True when a window / re-poll may run for `p`: the in-process policy (net or table) drives the RL actor at a foreground boundary. The observer arm, the
// FIFO transport and a branch / teacher mode never do (the branch modes are refused by name: a window cut and a branch child do not compose).
bool window_enabled( player_t* p )
{
  if ( window_ms_storage() <= 0 )
    return false;
  sim_t* sim = p->sim;
  if ( !sim->solver_control_str.empty() || sim->solver_policy_str.empty() || sim->solver_policy_observe_stock || sim->rl_bl_shadow || !is_rl_actor( p ) )
    return false;
  const solver_branch::config_t& bc = solver_branch::cfg();
  if ( bc.branch_at >= 0 || bc.teacher || solver_branch::is_child() )
    protocol_abort( "WINDOW_BRANCH: solver_window_ms > 0 under a solver_branch mode (branch / teacher / child): the decision window and a branch child do not compose "
                    "(261027-fts, decision 11); set solver_window_ms=0" );
  return !p->is_sleeping();
}

// The instant the window closes: the actor's Player-Ready event, or for a running cast the later of its end and the GCD's. Zero when neither exists.
timespan_t window_end( player_t* p )
{
  if ( p->readying != nullptr )
    return p->readying->occurs();
  if ( p->executing != nullptr && p->executing->execute_event != nullptr )
    return std::max( p->executing->execute_event->occurs(), p->gcd_ready );
  return timespan_t::zero();
}

poll_out_t run_poll( player_t* p, bool skip_button )
{
  sim_t*              sim = p->sim;
  const std::uint64_t seq0 = sim->solver_control_seq;
  g_poll = true;
  g_skip_button_dial = skip_button;
  g_poll_out = poll_out_t{};
  action_t* a = nullptr;
  try
  {
    a = solver_control::choose( p, nullptr, execute_type::FOREGROUND );
  }
  catch ( ... )
  {
    g_poll = false;
    g_skip_button_dial = false;
    sim->solver_control_seq = seq0;
    throw;
  }
  g_poll = false;
  g_skip_button_dial = false;
  sim->solver_control_seq = seq0;  // a poll consumes no decision number
  poll_out_t out = g_poll_out;
  out.action = a;
  if ( out.kind == poll_out_t::kind_t::cast && a == nullptr )
    out.kind = poll_out_t::kind_t::none;
  return out;
}

void make_hold( sim_t* sim, player_t* p, const poll_out_t& out )
{
  if ( window_probe_storage() != 0 )
    return;  // solver_window_probe: the poll ran, nothing is held
  hold_t& h = g_win.hold;
  const int replaces = h.valid ? h.replaces + 1 : 0;
  h = hold_t{};
  h.valid = true;
  h.sim = sim;
  h.p = p;
  h.kind = out.kind;
  h.idx = out.idx;
  h.target = out.target;
  h.explored = out.explored;
  h.has_rec = out.has_rec;
  if ( out.has_rec )
    h.rec = out.rec;
  h.has_stock = out.has_stock;
  if ( out.has_stock )
    h.stock = out.stock;
  h.has_table = out.has_table;
  if ( out.has_table )
    h.table = out.table;
  h.decided_at = sim->current_time();
  h.replaces = replaces;
  count( rl_translog::census_slot::staged );  // a hold is a staged decision; the fire either commits it (no second count) or drops it (replaced / fight_end)
}

// One pre-fire poll of the window.
void window_poll_run( player_t* p )
{
  sim_t*  sim = p->sim;
  hold_t& h = g_win.hold;
  if ( h.valid && h.explored )
    return;  // an explored decision is held to the fire: nothing replaces it
  const bool first = !g_win.drawn;
  const char* kind = first ? "open" : "repoll";
  poll_out_t out = run_poll( p, /*skip_button=*/!first );
  if ( first )
    g_win.drawn = true;
  if ( out.kind == poll_out_t::kind_t::none )
  {
    window_log( sim, kind, "none", -1 );
    return;
  }
  if ( out.kind == poll_out_t::kind_t::wait )
  {
    if ( h.valid )
    {
      count( rl_translog::census_slot::replaced_by_wait );
      window_log( sim, kind, "wait_kept_hold", out.idx );
    }
    else if ( out.explored )
    {
      make_hold( sim, p, out );
      window_log( sim, kind, "hold_wait_explored", out.idx );
    }
    else
      window_log( sim, kind, "wait", out.idx );
    return;
  }
  if ( h.valid )
  {
    if ( out.idx == h.idx && out.target == h.target )
    {
      count( rl_translog::census_slot::repolls );
      window_log( sim, kind, "kept", out.idx );
      return;
    }
    if ( h.replaces >= WINDOW_MAX_REPLACES )
    {
      count( rl_translog::census_slot::flipflop_bound );
      window_log( sim, kind, "flipflop_bound", out.idx );
      return;
    }
    count( rl_translog::census_slot::replaced );
    make_hold( sim, p, out );
    window_log( sim, kind, "replaced", out.idx );
    return;
  }
  make_hold( sim, p, out );
  window_log( sim, kind, out.explored ? "hold_explored" : "hold", out.idx );
}

// The execute_action() tail for a press the engine did not get from player_ready_event_t (a queued press replaced by a re-poll): the same assignments in the
// same order as player_t::execute_action(), after choose().
void press_chosen( player_t* p, action_t* action )
{
  p->last_foreground_action = action;
  action->line_cooldown->start();
  action->queue_execute( execute_type::FOREGROUND );
  if ( !action->quiet )
  {
    p->iteration_executed_foreground_actions++;
    action->total_executions++;
    if ( action->trigger_gcd > timespan_t::zero() )
      p->prev_gcd_actions.push_back( action );
    else
      p->off_gcdactions.push_back( action );
    if ( !p->is_add() && ( !p->is_pet() || p->collect_pet_sequence_data ) )
      p->sequence_add( action, action->target );
  }
}

// accept_cast()'s retarget for a press made after a poll (the poll itself does not retarget): casting on a unit IS targeting it.
void retarget_to_pick( player_t* p, action_t* a )
{
  if ( p->sim->target_select_enabled && is_rl_actor( p ) && rl_target_select::is_targeted_action( a ) )
  {
    bool      found = false;
    player_t* pick = rl_target_select::lookup_pick( a, &found );
    if ( found && pick != nullptr )
      rl_target_select::retarget( a, p, pick );
  }
}

struct window_poll_event_t : public event_t
{
  player_t* actor;
  bool      queue_chain;
  window_poll_event_t( sim_t& s, player_t* pl, timespan_t d, bool q ) : event_t( s, d ), actor( pl ), queue_chain( q ) {}
  const char* name() const override
  {
    return queue_chain ? "RL-Queue-Poll" : "RL-Window-Poll";
  }
  void execute() override;
};

void arm_queue_chain( player_t* p, action_t* a )
{
  if ( g_q.ev != nullptr || a->queue_event == nullptr )
    return;
  const timespan_t step = timespan_t::from_millis( repoll_ms_storage() );
  if ( p->sim->current_time() + step >= a->queue_event->occurs() )
    return;  // strictly before the fire only; the fire instant has its own poll
  g_q.ev = make_event<window_poll_event_t>( *p->sim, *p->sim, p, step, true );
}

// A re-poll of the queued press `a` (inside the chain, or at its fire instant). Returns true when it replaced the press.
bool queue_poll_run( player_t* p, action_t* a, bool at_fire )
{
  sim_t* sim = p->sim;
  if ( g_q.explored )
    return false;  // an explored press is held to its fire
  poll_out_t out = run_poll( p, /*skip_button=*/true );
  const char* kind = at_fire ? "queue_fire" : "queue_repoll";
  if ( window_probe_storage() != 0 )
  {
    window_log( sim, kind, "probe", out.idx );
    return false;  // solver_window_probe: the poll ran, the press stands
  }
  if ( out.kind == poll_out_t::kind_t::none )
  {
    window_log( sim, kind, "none", -1 );
    return false;
  }
  if ( out.kind == poll_out_t::kind_t::wait )
  {
    count( rl_translog::census_slot::replaced_by_wait );
    window_log( sim, kind, "wait_kept_press", out.idx );
    return false;
  }
  if ( out.action == a && out.target == g_q.target )
  {
    count( rl_translog::census_slot::repolls );
    window_log( sim, kind, "kept", out.idx );
    return false;
  }
  if ( g_q.replaces >= WINDOW_MAX_REPLACES )
  {
    count( rl_translog::census_slot::flipflop_bound );
    window_log( sim, kind, "flipflop_bound", out.idx );
    return false;
  }
  // A different cast: the queued press is cancelled (its row is discarded, census `replaced`) and the new pick is pressed through the engine's own path.
  window_log( sim, kind, "replaced", out.idx );
  const int replaces = g_q.replaces + 1;
  if ( g_queued.valid && g_queued.p == p && g_queued.action == a )
    discard_pending( g_queued, rl_translog::census_slot::replaced, DISCARD_NAMES[ static_cast<int>( discard_reason::replaced ) ] );
  else if ( !translog_on( sim ) )
    count( rl_translog::census_slot::replaced );
  if ( g_q.ev != nullptr )
    event_t::cancel( g_q.ev );
  g_q.valid = false;
  event_t::cancel( a->queue_event );
  p->queueing = nullptr;
  // The old press never executed: undo the counts execute_action() made for it (queued_action_execute_event_t does the same when its queue fails).
  p->iteration_executed_foreground_actions--;
  a->total_executions--;
  if ( !p->prev_gcd_actions.empty() && p->prev_gcd_actions.back() == a )
    p->prev_gcd_actions.pop_back();
  retarget_to_pick( p, out.action );
  if ( translog_on( sim ) && out.has_rec )
  {
    const std::uint64_t seq = ++sim->solver_control_seq;
    if ( g_staged.valid )
      pending_invariant( fmt::format( "a re-poll replaced a queued press while seq={} is staged", g_staged.seq ) );
    pending_row_t& pd = g_staged;
    pd = pending_row_t{};
    pd.valid = true;
    pd.sim = sim;
    pd.p = p;
    pd.action = out.action;
    pd.rec = out.rec;
    pd.rec.seq = static_cast<std::uint32_t>( seq );
    pd.seq = seq;
    pd.has_stock = out.has_stock;
    if ( out.has_stock )
      pd.stock = out.stock;
    pd.has_table = out.has_table;
    if ( out.has_table )
      pd.table = out.table;
    pd.decided_at = sim->current_time();
    pd.expected_fire = pd.decided_at;
    pd.explored = out.explored;
    pd.dump_line = false;
    count( rl_translog::census_slot::staged );
  }
  g_last_pick = last_pick_t{ true, out.action, out.idx, out.target, out.explored };
  g_q_replaces_next = replaces;
  press_chosen( p, out.action );
  g_q_replaces_next = 0;  // taken by queue_window_open() if the new press queued; a press that fired at once has no chain to carry it into
  return true;
}

void window_poll_event_t::execute()
{
  player_t* p = actor;
  sim_t*    sim = &this->sim();
  if ( queue_chain )
  {
    if ( g_q.ev == this )
      g_q.ev = nullptr;
    if ( !g_q.valid || g_q.p != p || g_q.sim != sim )
      return;
    action_t* a = g_q.action;
    if ( p->queueing != a || a->queue_event == nullptr )
    {
      g_q.valid = false;  // the queue was resolved by an engine path (fired, interrupted, cancelled)
      return;
    }
    if ( sim->current_time() >= a->queue_event->occurs() )
      return;
    if ( !queue_poll_run( p, a, false ) && g_q.valid && p->queueing == g_q.action )
      arm_queue_chain( p, g_q.action );
    return;
  }
  if ( g_win.ev == this )
    g_win.ev = nullptr;
  if ( !g_win.active || g_win.p != p || g_win.sim != sim || p->is_sleeping() )
    return;
  const timespan_t end = window_end( p );
  const timespan_t now = sim->current_time();
  if ( end <= now )
    return;  // the fire instant is the ordinary Player-Ready decision
  if ( p->queueing != nullptr || g_staged.valid || g_queued.valid )
    return;  // cannot happen while the window is armed from a Player-Ready; a queued press has its own chain
  window_poll_run( p );
  const timespan_t step = timespan_t::from_millis( repoll_ms_storage() );
  if ( now + step < end && g_win.active )
    g_win.ev = make_event<window_poll_event_t>( *sim, *sim, p, step, false );
}

void window_open( player_t* p, timespan_t end )
{
  sim_t*           sim = p->sim;
  const timespan_t now = sim->current_time();
  const timespan_t step = timespan_t::from_millis( repoll_ms_storage() );
  if ( g_win.active && g_win.p == p && g_win.sim == sim )
    return;  // a window is already running for this boundary (a hard cast opened it at the cast start)
  if ( g_win.hold.valid )
  {
    // A hold older than its boundary (its fire never resolved it): dropped.
    count( rl_translog::census_slot::replaced );
    g_win.hold.valid = false;
  }
  g_win = window_t{};
  if ( end - now < step )
    return;  // too close to the boundary for a poll to precede it
  const timespan_t open_at = std::max( now, end - timespan_t::from_millis( window_ms_storage() ) );
  g_win.active = true;
  g_win.sim = sim;
  g_win.p = p;
  g_win.ev = make_event<window_poll_event_t>( *sim, *sim, p, open_at - now, false );
}

// The fire instant: the ordinary decision resolved against the hold. Moves `idx` to the hold's button when the hold stands; returns true when the hold is an
// explored one (held to the fire: the aim dial is not drawn on top of it).
bool window_resolve_final( sim_t* sim, player_t* p, const std::uint8_t mask[ RL_ACTION_DIM ], int& idx, bool& exploratory, bool table_play, bool forced )
{
  if ( g_poll || !g_win.hold.valid || g_win.hold.sim != sim || g_win.hold.p != p )
    return false;
  hold_t& h = g_win.hold;
  if ( forced )
    return false;  // a forced decision (solver_force_decision) is not overridden by a hold: the hold is dropped when the row is submitted
  if ( h.idx < 0 || mask[ h.idx ] == 0 )
  {
    window_log( sim, "fire", "hold_illegal", h.idx );
    return false;  // dropped when the row is submitted (census `replaced`)
  }
  if ( h.explored )
  {
    idx = h.idx;
    exploratory = true;
    window_log( sim, "fire", "hold_explored", idx );
    return true;
  }
  if ( h.kind == poll_out_t::kind_t::cast )
  {
    if ( RL_ACTIONS[ idx ].kind == rl_action_kind::wait && !table_play )
    {
      // a wait pick leaves the press open: it fires (the table player's idle is its own plan, not a pick against the hold: the hold is dropped)
      count( rl_translog::census_slot::replaced_by_wait );
      idx = h.idx;
      window_log( sim, "fire", "wait_kept_hold", idx );
    }
    else if ( idx != h.idx && h.replaces >= WINDOW_MAX_REPLACES )
    {
      count( rl_translog::census_slot::flipflop_bound );
      idx = h.idx;
      window_log( sim, "fire", "flipflop_bound", idx );
    }
    else
      window_log( sim, "fire", idx == h.idx ? "kept" : "replaced", idx );
  }
  return false;
}

// RAII at the top of an ordinary foreground decision of the RL actor: whatever the window left (the hold, the draw flag, a running chain) is resolved or dropped
// when the decision ends, however it ends.
struct fire_guard_t
{
  sim_t*    sim;
  player_t* p;
  bool      on;
  fire_guard_t( sim_t* s, player_t* pl, bool enable ) : sim( s ), p( pl ), on( enable ) {}
  fire_guard_t( const fire_guard_t& ) = delete;
  fire_guard_t& operator=( const fire_guard_t& ) = delete;
  ~fire_guard_t()
  {
    if ( !on )
      return;
    if ( g_win.hold.valid && g_win.hold.p == p )
    {
      count( rl_translog::census_slot::replaced );  // the decision ended without a row: the hold is dropped
      g_win.hold.valid = false;
    }
    if ( g_win.p == p )
    {
      if ( g_win.ev != nullptr )
        event_t::cancel( g_win.ev );
      g_win.active = false;
      g_win.drawn = false;
      g_win.ev = nullptr;
    }
  }
};
}  // namespace

void pending_queued( player_t* p, action_t* a, double queue_delay_s )
{
  if ( !g_staged.valid || g_staged.p != p || g_staged.action != a )
    return;
  sim_t* sim = g_staged.sim;
  const solver_branch::config_t& bc = solver_branch::cfg();
  if ( bc.branch_at >= 0 || bc.teacher || solver_branch::is_child() )
    protocol_abort( "PENDING_BRANCH: a queued press under a solver_branch mode (branch / teacher / child): the window cut and the pending row are not defined together "
                    "(261027-fts, decision 11: no branch mode with fire-time rows)" );
  if ( g_queued.valid )
    pending_invariant( fmt::format( "a second queued press (seq={}) while seq={} is queued", g_staged.seq, g_queued.seq ) );
  if ( sim->solver_control_seq != g_staged.seq )
    pending_invariant( fmt::format( "queueing staged decision seq={} but the counter is {}", g_staged.seq, sim->solver_control_seq ) );
  --sim->solver_control_seq;  // the number is not spent until the press fires: damage landing in the queue window belongs to the previous row
  g_staged.seq_given_back = true;
  g_staged.queued = true;
  g_staged.expected_fire = sim->current_time() + timespan_t::from_seconds( queue_delay_s );
  count( rl_translog::census_slot::queued );
  std::swap( g_queued, g_staged );
  g_staged.valid = false;
}

void pending_commit( player_t* p, action_t* a, bool from_queue_event )
{
  if ( !from_queue_event )
  {
    if ( g_staged.valid && g_staged.p == p && g_staged.action == a )
      commit_pending( g_staged, false );
    return;
  }
  if ( g_queued.valid && g_queued.p == p && g_queued.action == a )
    commit_pending( g_queued, true );
}

void pending_discard( player_t* p, action_t* a, discard_reason reason )
{
  if ( !g_queued.valid || g_queued.p != p || ( a != nullptr && g_queued.action != a ) )
    return;
  const int i = static_cast<int>( reason );
  discard_pending( g_queued, rl_translog::census_slot::queue_failed_not_ready + static_cast<std::uint32_t>( i ), DISCARD_NAMES[ i ] );
}

void pending_discard_interrupt( player_t* p, action_t* a )
{
  if ( !g_queued.valid || g_queued.p != p || ( a != nullptr && g_queued.action != a ) )
    return;
  const interrupt_cause_t c = current_interrupt_cause();
  discard_pending( g_queued, interrupt_slot( c ), INTERRUPT_NAMES[ static_cast<int>( c ) ] );
}

int& window_ms_storage()
{
  static int v = 250;
  return v;
}
int& repoll_ms_storage()
{
  static int v = 25;
  return v;
}
std::string& window_log_storage()
{
  static std::string s;
  return s;
}
int& window_probe_storage()
{
  static int v = 0;
  return v;
}

void window_arm( player_t* p, timespan_t ready_at )
{
  if ( window_enabled( p ) )
    window_open( p, ready_at );
}
void window_arm_cast( player_t* p, timespan_t ends_at )
{
  if ( window_enabled( p ) )
    window_open( p, ends_at );
}
void window_on_ready( player_t* p )
{
  if ( g_win.p == p && g_win.ev != nullptr )
    event_t::cancel( g_win.ev );
}
void queue_window_open( player_t* p, action_t* a )
{
  if ( !window_enabled( p ) || !g_last_pick.valid || g_last_pick.action != a )
    return;
  if ( g_q.ev != nullptr )
    event_t::cancel( g_q.ev );
  g_q = queue_poll_t{};
  g_q.valid = true;
  g_q.sim = p->sim;
  g_q.p = p;
  g_q.action = a;
  g_q.target = g_last_pick.target;
  g_q.explored = g_last_pick.explored;
  g_q.replaces = g_q_replaces_next;
  g_q_replaces_next = 0;
  arm_queue_chain( p, a );
}
void queue_rescheduled( player_t* p, action_t* a )
{
  if ( g_q.valid && g_q.p == p && g_q.action == a && g_q.ev == nullptr && p->queueing == a )
    arm_queue_chain( p, a );
}
bool queue_fire_poll( player_t* p, action_t* a )
{
  if ( !g_q.valid || g_q.p != p || g_q.action != a )
    return false;
  if ( g_q.ev != nullptr )
    event_t::cancel( g_q.ev );
  if ( p->queueing != a )
  {
    g_q.valid = false;
    return false;
  }
  const bool replaced = queue_poll_run( p, a, /*at_fire=*/true );
  if ( !replaced )
    g_q.valid = false;  // the press stands: its chain is over
  return replaced;
}

void note_cast_interrupted( player_t* p )
{
  if ( is_rl_actor( p ) && translog_on( p->sim ) )
    count( rl_translog::census_slot::cast_interrupted );
}

void note_cast_failed_at_end( player_t* p, action_t* a )
{
  if ( is_rl_actor( p ) && translog_on( p->sim ) && a == p->last_foreground_action )
    count( rl_translog::census_slot::cast_failed_at_end );
}

void on_fight_end( sim_t* sim )
{
  // 261027-fts stage D: a hold the fight ended on never became a row (the fire never came).
  if ( g_win.hold.valid && g_win.hold.sim == sim )
  {
    count( rl_translog::census_slot::fight_end );
    g_win.hold.valid = false;
  }
  g_win.active = false;
  g_q.valid = false;
  if ( g_queued.valid )
  {
    if ( g_queued.sim == sim )
      discard_pending( g_queued, rl_translog::census_slot::fight_end, DISCARD_NAMES[ static_cast<int>( discard_reason::fight_end ) ] );
    else
      pending_invariant( "a queued press of another simulator at the fight end" );
  }
  if ( g_staged.valid )
  {
    if ( g_staged.sim == sim )
      discard_pending( g_staged, rl_translog::census_slot::staged_not_queued, DISCARD_NAMES[ static_cast<int>( discard_reason::staged_not_queued ) ] );
    else
      pending_invariant( "a staged decision of another simulator at the fight end" );
  }
}

void dump_decision_written( sim_t* sim )
{
  if ( !translog_on( sim ) )
    return;
  g_line_written = true;
  if ( g_dump_outcome.set )
  {
    const dump_outcome_t o = g_dump_outcome;
    g_dump_outcome = dump_outcome_t{};
    decision_dump::record_resolve( sim, o.outcome, o.reason, o.t, o.t_decided, o.seq );
    return;
  }
  if ( g_staged.valid || g_queued.valid )
    return;  // its commit or discard writes the line
  const double now = sim->current_time().total_seconds();
  decision_dump::record_resolve( sim, "none", nullptr, now, now, -1 );
}

action_t* choose( player_t* p, action_t* apl_choice, execute_type et )
{
  sim_t* sim = p->sim;
  if ( sim->solver_control_str.empty() && sim->solver_policy_str.empty() )
    return apl_choice;
  if ( is_rl_actor( p ) && translog_on( sim ) && !g_poll )
    begin_boundary( p, et );

  // Defect (a) fix (2026-07-29 fix bundle): in live play, melee auto-attack
  // begins implicitly with combat -- the player never has to "choose" it via
  // a priority list on some turn. Under solver_control, EVERY decision
  // boundary's action is instead replaced wholesale by the driver's reply, so
  // the placeholder `actions+=/auto_attack` entry this pipeline's generated
  // episode .simc always carries is never castable through the normal
  // `apl_choice`/"cast" path -- the solver's own spell catalog has no
  // "auto_attack" spellName to map, so a "cast" reply can never name it, and
  // even a stray "default"/"abstain" reply landing on it would only start the
  // FIRST swing (see `ready()`'s "not swinging" guard below), never repeat.
  // Without this, `p->main_hand_attack->execute_event` never gets scheduled
  // at all: every decision record shows swing_mh_remains:null,
  // auto_attack_interval effectively 0 (the swing-timer `melee_t` action
  // class this fires through short-circuits its own execute_time() to 0 until
  // the FIRST swing has actually landed -- see decision_dump.cpp's
  // player-scoped replacement instead), no white damage ever lands, and no
  // Crusading Strikes energize (which fires off the white-hit, not off a
  // solver-chosen spell) ever triggers. Start it here, once, at the very
  // first decision boundary this hook ever sees for this actor -- mirroring
  // live's implicit-with-combat start using the exact same
  // not-already-swinging guard the paladin's own `auto_melee_attack_t::execute()`
  // uses (sc_paladin.cpp).
  // Phase 200-04 (FORK-01/R-8) gate, corrected 2026-08-21 (CR-01): this
  // block must fire ONLY at the actor's true first in-combat FOREGROUND
  // decision, mirroring live's implicit-with-combat auto-attack start (see
  // the block's own doc comment below). One widened-surface hazard is
  // closed here:
  //   1. et == execute_type::FOREGROUND excludes an off-GCD/cast-while-
  //      casting poll from ever being "the first boundary this hook sees" --
  //      without it, an idle off-GCD poll firing before the real first
  //      foreground decision would yield in the wrong place and shift the
  //      whole timeline (200-RESEARCH.md Pitfall 3).
  // A second conjunct, `p->in_combat`, previously lived here to additionally
  // exclude a hooked PRECOMBAT boundary. It was REMOVED (not merely
  // rendered inert) by this fix: the precombat hook itself was already
  // removed the same day (see PROTOCOL.md "Version history",
  // 200-04-ad-hoc), so there is no longer a precombat boundary for the
  // conjunct to exclude -- but the stated justification for leaving it in
  // place ("harmless ... p->in_combat is already true by the time this gate
  // is ever evaluated") was itself wrong for any episode whose .simc
  // carries an explicit `actions=` list (every episode-driver.py episode):
  // player.cpp:6325 only calls enter_combat() from combat_begin() when
  // `!precombat_action_list.empty()`, and an explicit actions= list
  // suppresses the class module's default APL (and therefore its precombat
  // list) entirely, so p->in_combat is FALSE at the actor's first real
  // foreground decision. With the conjunct present, this yield block never
  // fired at t=0 for that shape, and the entire episode ran one decision
  // boundary short of the reference stream (measured: 45 -> 46 executed
  // foreground actions, seed 20260821, bbpaladin_armory profile; see CR-01
  // in 200-REVIEW.md and the re-measurement in the repo's fork-repin
  // receipt). Dropping the conjunct restores the pre-200-04 boundary count.
  // ACTOR GATE (2026-09-10), same reasoning as the re-arm block below. `solver_control_auto_attack_started`
  // is SIM-level, so this one-shot is consumed by whichever `player_t` reaches the first FOREGROUND
  // boundary of the iteration -- and pets/guardians/enemy actors all reach `choose()`. In practice
  // the RL actor wins that race (its white damage demonstrably lands), so this gate is inert today;
  // it is added because the block's entire purpose is to start THIS actor's swings, and a pet
  // consuming the one-shot would silently leave the real actor with no auto-attack for the whole
  // fight -- the exact failure the block was written to fix.
  if ( et == execute_type::FOREGROUND && !g_poll && !sim->solver_control_auto_attack_started &&
       std::strcmp( p->name(), RL_ACTOR_NAME ) == 0 && !p->is_pet() )
  {
    sim->solver_control_auto_attack_started = true;
    if ( p->main_hand_attack && p->main_hand_attack->execute_event == nullptr )
      p->main_hand_attack->schedule_execute();
    // Off-hand (2026-08-25). Everything above this line was written and
    // measured against `bbpaladin_armory` -- a TWO-HANDED actor, for which
    // `player_t::off_hand_attack` is permanently nullptr -- so the omission
    // was invisible for the entire life of the hook. It stops being
    // invisible the moment a DUAL-WIELDING actor is driven through here:
    // enhancement shaman built `off_hand_attack` (sc_shaman.cpp, the
    // `auto_attack_t` ctor's `p()->off_hand_attack = p()->melee_oh` branch)
    // but nothing ever started it swinging, because -- exactly as the long
    // comment above explains for the main hand -- the placeholder
    // `actions+=/auto_attack` entry is never reachable through a
    // solver-driven boundary. Measured on the enhancement/stormbringer
    // profile at iterations=10000: main-hand executes 69.5, off-hand
    // executes 0.0, at 1, 2 and 5 targets alike, against 74.6 / 105.6 for
    // the same rotation run as a plain APL. The lost white damage and its
    // knock-on Flametongue procs are worth ~2,061 DPS.
    //
    // Mirror the class module's own `auto_attack_t::execute()`, which
    // schedules the off-hand immediately after the main hand with no delay
    // of its own (sc_shaman.cpp: `p()->main_hand_attack->schedule_execute();
    // if ( p()->off_hand_attack ) p()->off_hand_attack->schedule_execute();`).
    // The half-swing offset a dual-wielder needs is NOT applied here and
    // must not be: `melee_t::execute_time()` already returns `t / 2` for
    // SLOT_OFF_HAND while its own `first` flag is set, and that flag is
    // restored by `melee_t::reset()` on every iteration.
    //
    // Null guard is the main hand's, verbatim in shape: a two-handed actor
    // short-circuits on the first conjunct, schedules nothing, and draws no
    // RNG -- the ret paladin path is bit-for-bit unaffected (verified: same
    // DPS, same executed-action count, same boundary count).
    //
    // No second "started" flag: `solver_control_auto_attack_started` is
    // cleared per fight by reset_iteration() (below), which sim.cpp calls
    // from combat_begin() AFTER sim_t::reset() has already cleared both
    // melee actions' `execute_event` and restored their `first` flags. Both
    // hands are therefore scheduled together, from a clean state, on the
    // first FOREGROUND boundary of every iteration. They also cannot
    // diverge: `off_hand_attack` is assigned at action-construction time,
    // so its nullness is fixed for the whole sim and cannot become true
    // after the flag has already been consumed by the main hand.
    if ( p->off_hand_attack && p->off_hand_attack->execute_event == nullptr )
      p->off_hand_attack->schedule_execute();

    // Wave A acceptance fix (2026-07-29, post-116-04): yield back to the
    // sim's event dispatcher HERE, before soliciting the actor's real first
    // decision, instead of falling through to the synchronous FIFO
    // round-trip below within this same execute_action() call. Evidenced in
    // WAVE-A-ACCEPTANCE.md §1: the reference (vanilla, non-solver_control
    // APL evaluation) flow naturally visits TWO player_t::execute_action()
    // decision boundaries at t=0 -- one for the actor's own separate
    // `actions=auto_attack` priority-list entry, one for the real
    // sequence/plan entry -- and a same-timestamp scheduled raid event (e.g.
    // Bloodlust at pull) gets processed by the sim's event queue in between
    // them, so the SECOND boundary already sees it. Without a yield here,
    // solver_control started auto-attack and solicited the real first
    // decision inline, in the SAME boundary, with no chance for that
    // same-tick event to land first, so the actor's real first decision was
    // made against a NOT-YET-updated buff/haste state, and every decision
    // after it inherited the resulting bifurcated GCD/cooldown timeline.
    //
    // Reuse the existing zero-second "pending wait" mechanism (the same one
    // a `{"type":"wait","sec":0}` reply already drives via
    // player_ready_event_t::execute()'s `solver_control_has_pending_wait`
    // branch, player.cpp) rather than adding a new wire message: this
    // schedules a fresh zero-delay Player-Ready event and returns, WITHOUT
    // ever sending a request over the FIFO for this boundary -- the driver
    // never sees it at all, so decision/wait counts on the wire are
    // unaffected. That new event is appended to the sim's event queue AFTER
    // any same-timestamp events already pending (event_manager_t::add_event
    // orders same-timestamp entries by ascending insertion id), so it
    // reproduces the reference flow's own two-boundary yield structurally,
    // not by special-casing Bloodlust or any specific raid event/profile.
    sim->solver_control_pending_wait_s = 0.0;
    sim->solver_control_has_pending_wait = true;
    return nullptr;
  }

  // 260901-rearm-agent-autos fix: the block above only ever arms both hands
  // ONCE per fight (`solver_control_auto_attack_started` never resets mid-
  // fight). Live's implicit-with-combat start is a one-shot, but a repeating
  // swing normally keeps re-arming itself from inside `action_t::execute()`
  // (`action.cpp`, `if ( repeating && !proc ) schedule_execute();`) every
  // time it actually lands. `shaman_t::moving()` (`sc_shaman.cpp`) cancels
  // both swings on every movement raid event, and a cancelled swing never
  // reaches `execute()` -- so under the APL it re-arms via the APL's own
  // `actions+=/auto_attack` priority-list entry re-selecting on the next
  // FOREGROUND boundary (`auto_attack_t::ready()`: `!p()->is_moving() &&
  // main_hand_attack->execute_event == nullptr`), but under solver_control
  // that APL choice is discarded wholesale (`player.cpp`,
  // `solver_control::choose()` replaces it), and nothing else ever
  // re-arms the swing -- so a single movement window during the fight
  // permanently ends the actor's white damage (measured:
  // 260901-tg1-agent-target-selection/RESEARCH-engine.md Q6c, main-hand
  // swings 181.88 apl / 7.29 agent on hectic-add-cleave, last policy
  // main-hand execute at t=1.786 of a 120s fight). The SAME failure shape
  // also covers a melee target dying mid-swing: `action_execute_event_t::
  // execute()` nulls `execute_event` unconditionally before checking
  // `can_execute`, and only calls `action->execute()` (the repeating
  // reschedule) when `can_execute` is true -- so a target-death stall is
  // structurally identical to a movement stall and is fixed by the same
  // predicate, for free.
  //
  // Re-arm here, every FOREGROUND decision boundary (mirroring
  // `auto_attack_t::ready()`'s own predicate exactly, not a looser one --
  // the agent must not re-arm FASTER than the APL arm can, only at parity
  // with it), whenever a hand is idle and the actor is not moving. Gated on
  // `et == execute_type::FOREGROUND` for the same reason the one-time start
  // block above is: an off-GCD/cast-while-casting poll is not a boundary
  // the reference APL's own `auto_attack` priority-list entry would be
  // re-evaluated on either, so re-arming there would let the agent arm
  // faster than parity allows. No yield/return here (unlike the block
  // above) -- this is not the fight's first decision boundary, so there is
  // no same-tick pull event ordering to protect; scheduling and falling
  // through to the rest of `choose()` is sufficient.
  //
  // ACTOR GATE (2026-09-10). This block sits ABOVE the CR-01 actor filter below, so before this
  // gate existed it re-armed the swings of EVERY `player_t` reaching `choose()` -- pets, guardians
  // and enemy actors included -- which is precisely what that filter's own comment says must not
  // happen ("every other player_t reaching this hook (pets, guardians, enemy actors) must run its
  // own APL unchanged").
  //
  // Measured cost, enhancement shaman, `patchwerk-t1-long`, one profile with only the
  // `solver_policy=` line differing: the lightning wolves ran TWO melee chains each -- their own
  // `auto_attack` action's, plus one armed here on every foreground boundary they took. At spawn
  // that shows as 4 `melee` schedules for 2 wolves against the APL path's 2. While alive they
  // landed 4 hits per tick against 2 wolves; and because only one chain is cancelled at demise,
  // hits kept landing after the pets were dead -- from t=17.8s the solver path had ZERO wolves
  // alive and two hits per tick for another five seconds. Over a 450 s fight: 762.4 wolf swings
  // against the APL path's 153.1, worth 773,988 damage = 1,720 DPS = 0.91 % of the apl-full bar,
  // credited to the agent in every net-vs-apl comparison the RL milestones have published.
  //
  // The predicate is the file's own `is_rl_actor` idiom, verbatim in shape (`:144`): exact string
  // equality against RL_ACTOR_NAME -- never a prefix, which would match the actor's own pet
  // records (the registry's `$actorNameComment` warns of exactly this) -- plus `!p->is_pet()`.
  if ( et == execute_type::FOREGROUND && !g_poll && !p->is_moving() &&
       std::strcmp( p->name(), RL_ACTOR_NAME ) == 0 && !p->is_pet() )
  {
    if ( p->main_hand_attack && p->main_hand_attack->execute_event == nullptr )
      p->main_hand_attack->schedule_execute();
    if ( p->off_hand_attack && p->off_hand_attack->execute_event == nullptr )
      p->off_hand_attack->schedule_execute();
  }

  // CR-01 fix (210-CR-FIX): in-process transport actor filter, mirroring
  // the FIFO driver's own D-11/B-3 Python-side filter (episode.py:591-596).
  // The in-process policy's action space belongs to ONE actor
  // (RL_ACTOR_NAME, generated from the registry's own `actorName` --
  // rl_policy_constants.h -- never a literal here, per R-4/D-12); every
  // other player_t reaching this hook (pets, guardians, enemy actors) must
  // run its own APL unchanged, exactly as the FIFO driver's "default" reply
  // already does for them. Gated on `solver_control_str.empty()`: the entry
  // guard above already proved at least one of solver_control_str/
  // solver_policy_str is non-empty, so an empty solver_control_str here
  // means the in-process transport (not FIFO) is about to answer this
  // boundary -- the FIFO arm's own wire-level filtering is untouched.
  // Placed BEFORE the seq increment below (not at read_state, as the
  // review's illustrative snippet showed) so `seq` -- the counter Phase
  // 212's log keys off -- counts the agent's own decisions only, never a
  // pet's. Exact string equality, never a prefix match: the registry's own
  // $actorNameComment warns a prefix match over the actor's name also
  // matches every one of its pet records.
  if ( sim->solver_control_str.empty() && std::strcmp( p->name(), RL_ACTOR_NAME ) != 0 )
    return apl_choice;

  // 261021-ezq OBSERVER MODE (solver_policy_observe_stock=1; sim.hpp has the contract). SimC's own select_action already decided
  // for this boundary (`apl_choice`); the arm below only records it. Two things happen here, ABOVE the seq increment:
  //   - a non-foreground boundary aborts (the stock list's leaves are foreground leaves; a profile with off-GCD or
  //     cast-while-casting lines would differ from pure SimC here, and the run must not pretend otherwise);
  //   - a re-poll inside an open idle stretch (nothing ready, the stretch's one row already written) writes nothing and
  //     consumes no decision number: the same precedent as the no-op decrement of the all-illegal background boundary below,
  //     so seq stays contiguous and the .apl record count equals the decision row count. It does what the scripted path does
  //     after choose() at a boundary of its own (player.cpp, execute_action: refresh_chosen, because there the boundary call of
  //     read_state is not reached): keeps the chosen-enemy tag current. Returning nullptr with no pending wait leaves SimC's own
  //     idle poll (available(), gauss 100 ms +- 10 ms) to schedule the next try, so its random draw is untouched.
  // The first-decision auto-attack yield above stays first (the first boundary of a fight is never recorded).
  if ( sim->solver_policy_observe_stock )
  {
    if ( et != execute_type::FOREGROUND )
      protocol_abort( fmt::format( "OBSERVE_STOCK_BOUNDARY: observer mode records foreground boundaries only, got boundary={} at t={:.3f}",
                                   decision_dump::boundary_name( et ), sim->current_time().total_seconds() ) );
    if ( apl_choice == nullptr && sim->solver_observe_idle_open )
    {
      rl_target_select::refresh_chosen( p );
      sim->solver_control_last_reply_type = "wait";
      sim->solver_control_has_requested_wait_sec = false;
      sim->solver_control_last_requested_wait_sec = 0.0;
      sim->solver_control_last_wait_anchor_label.clear();
      sim->solver_control_last_wait_source.clear();
      return nullptr;
    }
  }

  // 261027-fts stage D: an ordinary foreground decision of the RL actor ends whatever window preceded it (the hold is resolved in submit_decision, or dropped).
  fire_guard_t fire_guard( sim, p, !g_poll && et == execute_type::FOREGROUND && is_rl_actor( p ) && sim->solver_control_str.empty() );

  // 261005-tch2 (solver_mask_no_enemy): at a background (off-GCD / cast-while-casting) boundary no wait
  // is legal (build_mask's R1), so with every cast masked there is nothing to press: this boundary is
  // not a decision at all -- nothing executes, and it returns BEFORE the seq increment below so the
  // decision count and the translog's seq stay contiguous. One bool test when the option is off.
  // (Only the in-process transport reaches here with the option on: setup() refuses solver_control=.)
  if ( sim->solver_mask_no_enemy && et != execute_type::FOREGROUND && no_attackable_enemy( p ) )
    return nullptr;

  // Shared across both transports (210-05R Task 1) -- incremented exactly
  // once per decision boundary regardless of which transport answers it.
  // Hoisted above the transport branch below: the FIFO arm used to
  // increment this itself, right after its own lazy-open; duplicating the
  // increment into each arm would silently double-count under the
  // in-process transport for no reason connected to the port itself.
  // Phase 212's log `seq` is this counter.
  const uint64_t seq = ++sim->solver_control_seq;

  if ( !sim->solver_control_str.empty() )
  {
    // ------------------------------------------------------------------
    // FIFO transport (wire protocol) -- unchanged apart from moving under
    // this branch and the seq increment hoisting above.
    // ------------------------------------------------------------------

    // Lazy-open, mirroring decision_dump's own convention. Pairing order
    // matters: the engine opens the request stream (.out, write) BEFORE the
    // reply stream (.in, read); the driver opens them in the mirrored order
    // (.out read, then .in write) so the two blocking FIFO opens pair up
    // without either side deadlocking on the other's data (standard
    // bidirectional named-pipe handshake).
    if ( !sim->solver_control_req_stream )
    {
      sim->solver_control_req_stream = std::make_unique<io::ofstream>();
      sim->solver_control_req_stream->open( sim->solver_control_str + ".out" );
      sim->solver_control_rep_stream = std::make_unique<std::ifstream>( sim->solver_control_str + ".in" );
      if ( !sim->solver_control_req_stream->is_open() || !sim->solver_control_rep_stream->is_open() )
        protocol_abort( "could not open solver_control FIFO pair at '" + sim->solver_control_str + "'" );
    }

    io::ofstream& req = *sim->solver_control_req_stream;
    req << "{\"v\":" << SOLVER_CONTROL_PROTOCOL_VERSION;
    req << ",\"type\":\"decision\"";
    req << ",\"seq\":" << seq;
    req << ",\"t\":" << sim->current_time().total_seconds();
    // 214-01 C2: the fight index, additive (no SOLVER_CONTROL_PROTOCOL_VERSION
    // bump, same reasoning as "boundary" below) -- lets a client tag a
    // decision with its fight without inferring one from `t` running
    // backwards across a multi-fight (`iterations>1`) launch.
    req << ",\"iteration\":" << sim->current_iteration;
    req << ",\"actor\":\"" << decision_dump::json_escape( p->name() ) << "\"";
    req << ",\"apl_choice\":"
        << ( apl_choice ? ( "\"" + decision_dump::json_escape( apl_choice->name() ) + "\"" ) : std::string( "null" ) );
    // Boundary kind (phase 200-04, FORK-01/R-8, R-9) -- additive, no
    // SOLVER_CONTROL_PROTOCOL_VERSION bump (see the version-history note at
    // the top of scripts/simc-eval/PROTOCOL.md). Shared name table with
    // decision_dump::record()'s own "boundary" field via
    // decision_dump::boundary_name() so the wire and the dump can never
    // disagree.
    req << ",\"boundary\":\"" << decision_dump::boundary_name( et ) << "\"";
    // solver_damage_so_far, gcd_remains, swing_mh_remains, holy_power,
    // cooldowns, buffs, target_debuffs, target_time_to_die, active_enemies,
    // dots, gcd_length, auto_attack_interval, resolved_action -- identical to
    // decision_dump's
    // own per-decision line, factored into one shared emitter so the two
    // hooks can never drift (116-02: signature widened to take the
    // boundary's own action anchor, needed at the time for the then-
    // action-scoped gcd_length/resolved_action fields; `apl_choice` is the
    // correct anchor here since the driver's reply hasn't resolved the actual
    // cast yet at request-build time). 2026-07-29 fix bundle, defect (d):
    // gcd_length/auto_attack_interval are now PLAYER-scoped and no longer read
    // this anchor at all; `apl_choice` still anchors `resolved_action` here.
    // `solver_reply_gated` defaults false at this call site -- the wire
    // request's own `resolved_action` stays pre-reply/`apl_choice`-based,
    // exactly as PROTOCOL.md documents (the reply hasn't been read yet); only
    // decision_dump::record()'s own call opts into reply-gating (defect (b)).
    // 260902/cr4 (CR-02): this FIFO request-line build IS the decision boundary -- it runs before
    // the reply has been read and before accept_cast has retargeted/turned the player, so this is
    // the state the decision is actually made from.
    decision_dump::write_state_fields( req, p, apl_choice, false, et == execute_type::FOREGROUND,
                                        /*is_decision_boundary=*/true );
    req << "}\n";
    req.flush();

    std::string line;
    if ( !std::getline( *sim->solver_control_rep_stream, line ) )
      protocol_abort( "EOF or read failure on solver_control reply stream (seq=" + std::to_string( seq ) + ")" );

    rapidjson::Document doc;
    if ( doc.Parse( line.c_str() ).HasParseError() || !doc.IsObject() )
      protocol_abort( "malformed reply JSON (seq=" + std::to_string( seq ) + "): " + line );

    if ( !doc.HasMember( "v" ) || !doc["v"].IsInt() || doc["v"].GetInt() != SOLVER_CONTROL_PROTOCOL_VERSION )
      protocol_abort( "reply 'v' mismatch or missing (seq=" + std::to_string( seq ) + "): " + line );

    if ( !doc.HasMember( "seq" ) || !doc["seq"].IsUint64() || doc["seq"].GetUint64() != seq )
      protocol_abort( "reply 'seq' mismatch (expected " + std::to_string( seq ) + "): " + line );

    if ( !doc.HasMember( "type" ) || !doc["type"].IsString() )
      protocol_abort( "reply missing 'type': " + line );

    const std::string type = doc["type"].GetString();

    // Defect (b) fix (2026-07-29 fix bundle): classify this decision boundary's
    // reply BEFORE returning, so decision_dump::record() (called right after
    // this function returns -- player.cpp's execute_action() was reordered for
    // exactly this) can report the SOLVER's actual per-boundary resolution
    // instead of the pre-reply APL/sequence placeholder pick.
    sim->solver_control_last_reply_type = type;
    // 221-03 (ACT-05/ACT-06, Task 2 point 4): cleared for EVERY reply type
    // (not just non-wait ones) so a "cast"/"default"/"abstain"/"noop"
    // boundary's dump never carries a stale prior wait's requested_wait_sec/
    // wait_anchor -- the "wait" branch below overwrites these right before
    // calling accept_wait().
    sim->solver_control_has_requested_wait_sec = false;
    sim->solver_control_last_requested_wait_sec = 0.0;
    sim->solver_control_last_wait_anchor_label.clear();
    sim->solver_control_last_wait_source.clear();

    if ( type == "cast" )
    {
      if ( !doc.HasMember( "action" ) || !doc["action"].IsString() )
        protocol_abort( "'cast' reply missing 'action' (seq=" + std::to_string( seq ) + "): " + line );
      return accept_cast( p, doc["action"].GetString(), seq );
    }

    if ( type == "wait" )
    {
      // Phase 200-04 (FORK-01/R-8): "wait" sets solver_control_pending_wait_s,
      // consumed ONLY by player_ready_event_t::execute()'s "nothing chosen"
      // branch (player.cpp) -- a FOREGROUND-only consumer. An off-GCD/
      // cast-while-casting poll has no such consumer, so a "wait" reply there
      // would set a flag that silently leaks into the NEXT foreground
      // boundary as an unexplained idle gap (T-200-11). accept_wait() refuses
      // it, engine-side, the same way the `v` mismatch is refused above.
      if ( !doc.HasMember( "sec" ) || !doc["sec"].IsNumber() )
        protocol_abort( "'wait' reply missing numeric 'sec' (seq=" + std::to_string( seq ) + "): " + line );
      // Nothing executes this boundary. The actual re-schedule happens in
      // player_ready_event_t::execute()'s existing "nothing chosen" branch
      // (player.cpp), which checks the pending-wait pair accept_wait() sets
      // before falling back to the default poll/threshold-based idle
      // scheduling.
      // 221-03 (ACT-05/ACT-06, Task 2 point 4): the value HANDED TO
      // accept_wait() below, before its own fight-end/raid-event clamp.
      // wait_anchor stays empty/null on this arm -- the wire "wait" reply
      // carries no anchor identity (PROTOCOL.md's "wait" shape is
      // `{"sec": float}` only), unlike the in-process arm below which
      // always knows RL_ACTIONS[idx].label.
      sim->solver_control_has_requested_wait_sec = true;
      sim->solver_control_last_requested_wait_sec = doc["sec"].GetDouble();
      accept_wait( sim, et, doc["sec"].GetDouble(), "seq=" + std::to_string( seq ) + ": " + line );
      return nullptr;
    }

    // The removed turn reply arm (R6-27, 233.1-01) was here -- deleted with the whole action.

    if ( type == "noop" )
    {
      // CR-08 fix (2026-08-21, owner ruling): a legal "decline" reply at ANY
      // boundary, including under solver_control_mode=training. Before this,
      // the only replies that returned nullptr / declined to act were "wait"
      // (FOREGROUND-only, refused above at a non-FOREGROUND boundary) and
      // "abstain" (a hard protocol_abort under training mode, by design --
      // see below). That left a training-mode agent with NO way to decline
      // an off_gcd/cast_while_casting poll without either aborting the
      // episode or falling through to "default" and conceding the APL's own
      // choice (e.g. firing a trinket the agent did not want used) -- exactly
      // the co-actor confound training mode exists to remove, reopened at the
      // one boundary this phase added. "noop" closes it: nothing executes
      // this boundary, unconditionally, in every solver_control_mode, and
      // -- unlike "wait" -- it never touches solver_control_pending_wait_s
      // (there is nothing to reschedule; the actor's normal readiness timing
      // is untouched, so this is legal at a FOREGROUND boundary too, though
      // "wait" remains the correct reply there whenever a real re-poll delay
      // is wanted). Deliberately NOT a PROTOCOL_VERSION bump (see PROTOCOL.md
      // "Version history") -- an old client simply never emits this string;
      // a new client talking to an old engine gets the existing
      // unknown-reply-type hard error below, which is the real compatibility
      // contract (paired with the binary sha256 pin), not the version field.
      return nullptr;
    }

    if ( type == "default" )
    {
      // Execute the APL's own choice unchanged, in every mode.
      return apl_choice;
    }

    if ( type == "abstain" )
    {
      // D-06/D-07 (phase 200-04): "default" and "abstain" are NO LONGER
      // synonyms. Under solver_control_mode=training (T-200-12), an abstain
      // is a hard protocol_abort -- the whole point of the training-mode
      // check is to catch a client that is SILENTLY abstaining at every
      // boundary (which would hand control back to the APL while producing a
      // plausible-looking episode, reintroducing exactly the co-actor
      // confound R-8 exists to remove). The check lives engine-side on
      // purpose: if the client owned the mode, the buggy client would also
      // own the check. Under the default "verify" mode (empty
      // solver_control_mode_str also means "verify"), abstain falls through
      // to the APL's own choice unchanged -- this is what makes criterion 2's
      // byte-identity receipt a real, runnable mode.
      if ( sim->solver_control_mode_str == "training" )
        protocol_abort( "'abstain' reply is a hard error under solver_control_mode=training (seq=" +
                         std::to_string( seq ) + "): " + line );
      return apl_choice;
    }

    protocol_abort( "unknown reply 'type': " + type );
    return apl_choice; // unreachable -- protocol_abort always throws
  }

  // --------------------------------------------------------------------
  // In-process transport (210-05R Task 1, XPORT-01/02/05). No wire, no
  // FIFO, no reply-type parsing -- the decision resolves synchronously
  // in this call: read_state -> build_obs -> build_mask -> forward ->
  // masked_argmax -> look the chosen index up in RL_ACTIONS -> the same
  // shared cast/wait epilogues the FIFO arm uses above.
  // --------------------------------------------------------------------
  {
    const bool foreground = ( et == execute_type::FOREGROUND );

    // Phase 271.4 (271.4-03, G271.4-8): the dry stock-rotation pick, evaluated BEFORE bind_slots, read_state, the aim pick
    // and every exploration draw, so the list sees the state the net sees and the net's aim (p->target) never leaks into
    // the list's target selection. Nothing runs and no random number is drawn (rl_stock_eval.hpp). Off: one bool test.
    rl_stock_eval::pick_t stock_pick =
        rl_stock_eval::enabled( sim ) ? rl_stock_eval::evaluate( p, et ) : rl_stock_eval::pick_t{};

    // tstl-sylvanas phase 220, plan 220-01 (OBS-07), REORDERED by
    // 260831-mk7 (D-3, mask-as-input): rl_obs_timing=1 still wraps
    // read_state+build_obs -- and ONLY that pair, not build_mask/forward --
    // in a steady_clock stopwatch, but the two calls are no longer
    // adjacent (build_mask, the allow-list AND, and the all-illegal
    // refusal now run BETWEEN them, because build_obs needs the FINAL
    // mask to fill its own trailing `legality` family). The span is
    // therefore taken as TWO SEGMENTS -- read_state's own elapsed time
    // plus build_obs's own elapsed time -- and pushed as their SUM, so the
    // 220-01 pin ("read_state+build_obs, and ONLY that pair") stays
    // honestly true of what is actually measured, never silently widened
    // to swallow build_mask/the AND/the refusal in between. The clock
    // calls themselves are guarded on the flag: when the option is off
    // this takes ZERO samples, not samples it throws away. See sim.hpp's
    // rl_obs_timing/rl_obs_ns doc comment for why no mutex is needed on
    // the push below.
    const bool obs_timing = sim->rl_obs_timing;

    // tstl-sylvanas phase 220, plan 220-04 (OBS-02/OBS-07). bind_slots() is
    // resolved ONCE per actor (a file-static cache keyed on `const
    // player_t*` inside rl_policy_obs.cpp) and is amortised across the
    // whole run -- called here, BEFORE read_state, and deliberately OUTSIDE
    // the rl_obs_timing stopwatch, which spans only read_state+build_obs
    // (see the two-segment note above).
    const rl_policy::slot_table& table = rl_policy::bind_slots( p );

    const chrono::wall_clock::time_point read_state_t0 =
        obs_timing ? chrono::wall_clock::now() : chrono::wall_clock::time_point{};
    // 228-11 Task 2: this call is ALWAYS the real decision boundary (the in-process arm's own
    // per-decision state read, before any reply/accept_cast has run) -- see rl_policy.hpp's own
    // doc comment on read_state for why decision_dump.cpp's diagnostic call must pass false.
    const rl_policy::rl_state_t state = rl_policy::read_state( p, foreground, /*is_decision_boundary=*/true );
    // The removed pre-decision behind-enemy stamp (R6-27, 233.1-01) was here -- deleted with
    // the whole turn action; nothing downstream reads it anymore.
    std::chrono::nanoseconds obs_timing_ns{ 0 };
    if ( obs_timing )
    {
      const chrono::wall_clock::time_point read_state_t1 = chrono::wall_clock::now();
      obs_timing_ns += std::chrono::duration_cast<std::chrono::nanoseconds>( read_state_t1 - read_state_t0 );
    }

    // 260831-mk7 (D-3): build_mask MOVES UP, ahead of build_obs -- the
    // engine's own order is now read_state -> build_mask -> [allow-list
    // AND] -> [all-illegal refusal] -> build_obs -> forward, matching
    // episode.py's Python-side D-04 site (mask.legal() computed before
    // obs.encode()) and this quick task's own top-of-file order comment.
    std::uint8_t mask[ RL_ACTION_DIM ];
    rl_policy::build_mask( state, mask );
    // NET-01 (arm subsets, Phase 222): a config-declared action allow-list,
    // carried on the loaded blob, ANDed ONCE here so every downstream
    // consumer -- build_obs's own legality slots below, masked_argmax, the
    // epsilon draw's legal_indices, the Q-margin/top_q diagnostics, and
    // record_decision's translog row -- sees the SAME restricted mask.
    // build_mask stays PURE over the POD (unchanged, Phase 221's own
    // ruling); the translog therefore records the RESTRICTED mask
    // alongside the obs vector whose OWN legality slots now carry this
    // SAME restricted mask too (260831-mk7) -- the log stays arm-
    // independent and the Python side does its own gather for everything
    // EXCEPT the legality slots, which are baked in at this width already.
    {
      const std::uint32_t allowed = sim->solver_policy_weights->allowed_actions;
      for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
        mask[ i ] = static_cast<std::uint8_t>( mask[ i ] & ( ( allowed >> static_cast<std::uint32_t>( i ) ) & 1u ) );
    }

    // S1 (260927-s1-spend-timing, todo 2026-09-27-s1-maelstrom-spend-timing-override-probe.md):
    // engine-side spend-threshold override -- forbids lightning_bolt and tempest (and, 261002-8rs,
    // primordial_storm when the build's action table offers that button -- it is also a Maelstrom
    // Weapon spender) ONLY; the third
    // Lightning-family action is NEVER touched anywhere in this block, the owner's standing rule
    // "never mask Chain Lightning at one target" holds by construction, not by threshold. Fires
    // whenever the player's current Maelstrom Weapon stack count is below solver_min_maelstrom_spend.
    // Deliberately placed AFTER the allow-list AND directly above (so a narrow arm's own subset
    // can never re-legalize what this forbids) and BEFORE the all-illegal refusal below (that
    // refusal still guards the genuinely-pathological all-illegal case; this override alone
    // cannot manufacture one on its own -- the wait actions and every non-Lightning-family cast
    // stay untouched). Reads the SAME buff object the observation encoder fills for obs slot 32
    // (rl_state_t::buffs, populated once in read_state() above) via find_buff() -- never a
    // re-derivation, so this can never disagree with what the net itself sees as its own stack
    // count. sim->solver_min_maelstrom_spend == 0 (default, or the option simply absent) is a
    // single int comparison and skips the whole block -- byte-identical to a run built before
    // this option existed (proof (a), ABSENT = IDENTICAL). The two action indices were resolved
    // ONCE by name from RL_ACTIONS in sim.cpp's setup() (never a hard-coded index here).
    if ( sim->solver_min_maelstrom_spend > 0 )
    {
      const rl_policy::buff_reading* mw = state.find_buff( "maelstrom_weapon" );
      const double mw_stacks = ( mw != nullptr && mw->has_stacks ) ? mw->stacks : 0.0;
      if ( mw_stacks < static_cast<double>( sim->solver_min_maelstrom_spend ) )
      {
        mask[ sim->solver_min_maelstrom_spend_lightning_bolt_idx ] = 0;
        mask[ sim->solver_min_maelstrom_spend_tempest_idx ] = 0;
        if ( sim->solver_min_maelstrom_spend_primordial_storm_idx >= 0 )
          mask[ sim->solver_min_maelstrom_spend_primordial_storm_idx ] = 0;
      }
    }

    // 261005-tch2 (owner, 2026-10-05: "can we just mask all spells to be uncastable while not in
    // combat?"): solver_mask_no_enemy. While no enemy can be attacked (no_attackable_enemy above:
    // the walk between packs on a route layout) every non-wait button is masked. Placed with S1:
    // after the allow-list AND (a narrow arm cannot re-legalize it) and before the all-illegal
    // refusal and build_obs, so the net's own legality inputs, the legal top_q/q_margin and the
    // translog row all carry the masked set. Only a foreground boundary reaches here with the
    // condition true (the background case returned before the seq increment), and at a foreground
    // boundary the unanchored wait stays legal, so this cannot manufacture an all-illegal mask on
    // its own. Off (default): one bool test, byte-identical to a run built before this option.
    if ( sim->solver_mask_no_enemy && no_attackable_enemy( p ) )
    {
      for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
        if ( RL_ACTIONS[ i ].kind != rl_action_kind::wait )
          mask[ i ] = 0;
    }

    // 222-07 (CR-04): the allow-list AND above can manufacture an
    // all-illegal mask that build_mask's own upstream refusal already
    // passed -- 222-RESEARCH.md's Pitfall 16 names this exactly ("a
    // background boundary has no legal wait" combined with a CONTROL arm's
    // narrow allow-list makes the all-illegal state materially more
    // reachable in the subset arms than in the full arm") and its
    // recommendation is explicit: "re-assert AFTER the AND (both
    // languages)". Python already does -- episode.py's post-AND
    // EmptyMask raise, policy_loaders._policy's post-AND
    // _assert_mask_not_empty. This mirrors both, on the C++ side, BEFORE
    // build_obs/forward()/masked_argmax ever see the mask (260831-mk7:
    // this refusal now ALSO gates what build_obs's own legality slots
    // would otherwise bake in from an all-illegal, misleading mask).
    //
    // This is not defensive -- masked_argmax() does NOT fall back to
    // index 0 on an all-illegal row (see its own definition below): every
    // mask[i] is 0, so `1 - mask[i]` is 1 for every entry, every q[i] gets
    // the IDENTICAL min_value penalty, ordering among entries is
    // unchanged, and masked_argmax silently returns argmax(q) -- an
    // ILLEGAL action, which the caller then executes via RL_ACTIONS[idx].
    // For a wait-kind index that is an unanchored wait; for a cast-kind
    // index it asks the engine to cast a spell build_mask just declared
    // unusable -- the "not-ready-cast" class this project has already
    // recorded as a ~0.9s engine FATAL. Fail closed here instead, by name.
    {
      bool any_legal = false;
      for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
        any_legal = any_legal || ( mask[ i ] != 0 );
      // Phase 268 fix pass (WR-05, ruling G268-11): at an off-GCD / cast-while-casting boundary build_mask makes every
      // wait illegal (R1), so a spec whose casts are all unavailable there (arms) legitimately has NOTHING legal. That
      // is a decline, not an error: answer it with the protocol's no-op exactly as the FIFO path does for a "noop"
      // reply (nothing executes, the pending-wait fields stay clear, the dump reports solver_reply_type "noop").
      // Only a FOREGROUND boundary, where the unanchored wait is always legal, keeps the fail-closed abort below.
      if ( !any_legal && !foreground )
      {
        // 271.1 credit-conservation fix (readout 271.1-CREDIT-CONSERVATION-READOUT.md, 2026-10-10): this boundary is not
        // a decision (nothing executes, no translog row is written), so it must not consume a decision number -- the
        // same invariant the solver_mask_no_enemy return above the increment keeps. A number with no row strands every
        // own-stream credit stamped while it is current (an auto swing's AUTO stamp, ticking as DOT_TICK through
        // dot_tick_class): the close row counts it, the .attr has no DECISION record to put it in, and the replay's
        // conservation check refuses. Nothing between the increment above and here reads or stamps the number
        // (bind_slots, read_state, build_mask and the mask edits only), and the return precedes every exploration draw,
        // so play is unchanged.
        assert( sim->solver_control_seq == seq );
        --sim->solver_control_seq;
        sim->solver_control_last_reply_type = "noop";
        sim->solver_control_has_requested_wait_sec = false;
        sim->solver_control_last_requested_wait_sec = 0.0;
        sim->solver_control_last_wait_anchor_label.clear();
        sim->solver_control_last_wait_source.clear();
        return nullptr;
      }
      if ( !any_legal )
      {
        protocol_abort( fmt::format(
            "solver_policy: every action is illegal after the blob's allowed_actions AND at "
            "seq={} -- the arm's allow-list and the engine's legality gate have no action in "
            "common here",
            seq ) );
      }
    }

    // 260831-mk7 (D-1/D-3): build_obs now takes the FINAL mask (post-AND,
    // post-refusal) and fills its own trailing `legality` family from it.
    const chrono::wall_clock::time_point build_obs_t0 =
        obs_timing ? chrono::wall_clock::now() : chrono::wall_clock::time_point{};
    float obs[ RL_OBS_DIM ];
    rl_policy::build_obs( p, state, table, mask, obs );
    if ( obs_timing )
    {
      const chrono::wall_clock::time_point build_obs_t1 = chrono::wall_clock::now();
      obs_timing_ns += std::chrono::duration_cast<std::chrono::nanoseconds>( build_obs_t1 - build_obs_t0 );
      sim->rl_obs_ns.push_back( obs_timing_ns.count() );
    }

    // 261021-ezq OBSERVER EPILOGUE. Nothing past this block runs in observer mode: no forward pass (the loaded net is never
    // consulted, q fields are NaN), no action dial, no hold windows, no forced press, no branch hook, no accept_cast (no retarget).
    // The press is SimC's own: `apl_choice` itself is returned, with its own target and its own line_cd (player.cpp starts it).
    if ( sim->solver_policy_observe_stock )
    {
      constexpr float nan = std::numeric_limits<float>::quiet_NaN();
      const bool      idle_row = ( apl_choice == nullptr );
      int             observe_idx = -1;
      std::uint16_t   observe_target = rl_translog::CHOSEN_TARGET_SENTINEL_NO_PICK;
      bool            observe_explored = false;
      if ( idle_row )
      {
        // The first poll of an idle stretch: the wait_next_event button, resolved by its registry label (never a literal index).
        observe_idx = observer_wait_index();
        if ( !mask[ observe_idx ] )
          protocol_abort( fmt::format( "OBSERVE_STOCK_IDLE_WAIT_ILLEGAL: seq={} t={:.3f} nothing is ready on the list but the "
                                       "wait_next_event button (index {}) is masked", seq, sim->current_time().total_seconds(),
                                       observe_idx ) );
        stock_pick.flags = static_cast<std::uint8_t>( stock_pick.flags | rl_stock_eval::F_IDLE );
        sim->solver_observe_idle_open = true;
        sim->solver_control_last_reply_type = "wait";
      }
      else
      {
        observe_idx = rl_stock_eval::button_index( apl_choice );
        if ( observe_idx < 0 )
          protocol_abort( fmt::format( "OBSERVE_STOCK_UNMAPPED_LEAF: seq={} t={:.3f} SimC's own pick '{}' is a ready leaf with no "
                                       "registry button", seq, sim->current_time().total_seconds(), apl_choice->name() ) );
        if ( !mask[ observe_idx ] )
          protocol_abort( fmt::format( "OBSERVE_STOCK_MASK_DISAGREES: seq={} t={:.3f} SimC's own pick '{}' (button {}) is masked "
                                       "illegal by the catalog", seq, sim->current_time().total_seconds(), apl_choice->name(),
                                       observe_idx ) );
        sim->solver_observe_idle_open = false;
        sim->solver_control_last_reply_type = "cast";
        // 261025-xpl: an exploration press is a leaf that sits DIRECTLY in the walked list (the launcher's explore_default holds the
        // exploration lines; every stock leaf sits in a list the walked one calls). The main-translog FLAG_EXPLORATORY bit says so.
        observe_explored = apl_choice->action_list != nullptr && apl_choice->action_list->name_str == rl_stock_eval::list_name_storage();
        // A targeted button records its target as the net path does (the actor index of the enemy SimC's pick aims at); an
        // untargeted one records no pick.
        if ( apl_choice->target != nullptr && rl_target_select::is_targeted_action( apl_choice ) )
          observe_target = static_cast<std::uint16_t>( apl_choice->target->actor_index );
      }
      // The dry walk of this same boundary must agree with SimC's own select_action: idle = strict and mapped both -1 (and not
      // refused), played = strict == mapped == the executed leaf's button.
      const int want = idle_row ? -1 : observe_idx;
      if ( ( stock_pick.flags & rl_stock_eval::F_REFUSED ) != 0 || stock_pick.strict_index != want || stock_pick.mapped_index != want )
        protocol_abort( fmt::format( "OBSERVE_STOCK_WALK_DISAGREES: seq={} t={:.3f} SimC's own pick '{}' (button {}) but the dry walk "
                                     "says strict={} ('{}') mapped={} flags={}", seq, sim->current_time().total_seconds(),
                                     idle_row ? "<idle>" : apl_choice->name(), want, stock_pick.strict_index,
                                     stock_pick.strict_name, stock_pick.mapped_index, static_cast<int>( stock_pick.flags ) ) );
      sim->solver_control_has_requested_wait_sec = false;
      sim->solver_control_last_requested_wait_sec = 0.0;
      sim->solver_control_last_wait_anchor_label.clear();
      sim->solver_control_last_wait_source.clear();
      try
      {
        // 261027-fts: a press is staged (committed when it fires); the idle row is committed at once.
        submit_decision( !idle_row, apl_choice, nullptr, sim, p, seq, obs, mask, observe_idx, nan, nan, observe_target, false, observe_explored, false, nullptr,
                         0, 0, rl_translog::CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK,
                         apl_choice != nullptr ? apl_choice->name() : nullptr, false,
                         rl_translog::CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK,
                         rl_translog::CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK, chosen_enemy_index_of( p ), &stock_pick );
      }
      catch ( ... )
      {
        flush_staged_on_abort( sim );
        rl_translog::flush_pending( sim );
        throw;
      }
      return apl_choice;
    }

    float q[ RL_ACTION_DIM ];
    // 261025-bk4 (wave B, lane b2): THE TABLE PLAYER (solver_table_policy=; rl_table_policy.hpp has the contract and the order). With a table
    // loaded the pick comes from the table's strategy instead of the net's forward pass and masked_argmax; the net is loaded (its allowed_actions
    // already ANDed the mask above) and never consulted. validate_table_options refused every option that would reach past this point on the net
    // path (hold windows, forced decisions, the spend override, the no-enemy mask, a branch or teacher mode, the exploration dials), so what follows
    // -- the row, accept_cast with the rules path's aim and retarget -- is the net arm's own code. Off: one string test.
    const bool table_play = rl_table_policy::active();
    rl_table_policy::decision_t table_decision;
    if ( table_play )
    {
      table_decision = rl_table_policy::decide( sim, p, foreground, seq, obs, mask, stock_pick );
      if ( table_decision.noop )
      {
        // A background boundary with nothing to press (no decided button, no deferred stock press, no legal wait): not a decision, exactly
        // the all-illegal background case above -- the decision number goes back and no row is written.
        assert( sim->solver_control_seq == seq );
        --sim->solver_control_seq;
        sim->solver_control_last_reply_type = "noop";
        sim->solver_control_has_requested_wait_sec = false;
        sim->solver_control_last_requested_wait_sec = 0.0;
        sim->solver_control_last_wait_anchor_label.clear();
        sim->solver_control_last_wait_source.clear();
        return nullptr;
      }
      std::fill( std::begin( q ), std::end( q ), 0.0f );
    }
    else
      rl_policy::forward( *sim->solver_policy_weights, obs, mask, q );

    // 260922-mfh (D3): hold windows restrict SELECTION ONLY -- they must never touch the
    // `mask` that build_obs/forward above, the q_margin/top_q diagnostics below, and BOTH
    // record_decision calls below all read. This REPLACES tj1's semantics (which ANDed the
    // hold into `mask` itself, upstream of build_obs) -- PLAN.md's D3 requires the agent to
    // SEE every naturally-legal action and the learner's bootstrap target to be the max over
    // what was REALLY pressable (the correct off-policy target for the unheld greedy policy).
    // `select_mask` is derived here, read ONLY by masked_argmax and the exploration draw
    // immediately below. Disabled (the common case): sim->solver_hold_windows is empty, one
    // .empty() check, a single memcpy, zero further cost.
    std::uint8_t select_mask[ RL_ACTION_DIM ];
    std::memcpy( select_mask, mask, sizeof( mask ) );
    // 260922-mfh (D4): set true when an active window cleared at least one naturally-legal
    // action at this decision -- OR'd into the row's own FLAG_HELD bit by both record_decision
    // calls below (never "a window was declared", which would also fire on a decision no
    // window actually reached).
    bool row_held = false;
    if ( !sim->solver_hold_windows.empty() )
    {
      std::uint8_t held_mask[ RL_ACTION_DIM ];
      std::memcpy( held_mask, mask, sizeof( mask ) );
      bool any_hold_applied = false;
      const double now_s = sim->current_time().total_seconds();
      for ( const auto& w : sim->solver_hold_windows )
      {
        if ( w.start_s <= now_s && now_s < w.end_s && held_mask[ w.action_idx ] )
        {
          held_mask[ w.action_idx ] = 0;
          any_hold_applied = true;
        }
      }
      if ( any_hold_applied )
      {
        bool held_any_legal = false;
        for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
          held_any_legal = held_any_legal || ( held_mask[ i ] != 0 );
        if ( held_any_legal )
        {
          std::memcpy( select_mask, held_mask, sizeof( select_mask ) );
          row_held = true;
        }
        else
        {
          // Fail-safe (same shape as tj1's): never let holds manufacture an empty
          // select_mask. Ignore the holds for THIS decision only -- select_mask stands
          // exactly as the natural `mask` copy above left it, so masked_argmax/exploration
          // below fall back to the full naturally-legal set -- and count the override so a
          // training run can report how often its declared windows collided with the
          // engine's own legality gate. row_held stays false: nothing was actually held.
          ++sim->solver_hold_windows_override_count;
        }
      }
    }

    const int greedy_idx = table_play ? table_decision.idx : rl_policy::masked_argmax( q, select_mask );
    int idx = greedy_idx;
    bool exploratory = false;

    // 230-04 (SCOR-02, R-B): a SECOND dial, over CANDIDATES, draws from this SAME dedicated
    // `sim->solver_explore_rng` stream further below (after the action itself has been chosen
    // and only when that action is targeted) -- see this file's own candidate-dial block for the
    // full contract. With the rotation frozen (this phase) the ACTION dial below is pinned at
    // zero and the CANDIDATE dial carries the schedule; in a future alternation (the rotation
    // retrained with the scorer frozen) it is the other way round -- a reader of either dial
    // should find this comment pointing at the other one.
    //
    // Phase 213 D-08: the random-action dial. Tested against zero BEFORE
    // touching the generator -- at a dial of exactly zero the engine takes
    // ZERO draws, which is what keeps every scoring run and every pre-213
    // run byte-identical to what it was, mirroring the Python side's own
    // zero guard.
    //
    // 260922-mfh (D8): two-stage draw -- the whole `wait` family counts as ONE option beside
    // each legal cast (P(wait) = 1/(n_legal_casts+1), then uniform within waits), matching the
    // owner's own framing in the folded todo rather than a literal 50/50 cast-vs-wait roll.
    // Reads `select_mask` (D3), so a held cast can never be drawn here either. The all-illegal
    // case cannot reach here (222-07/CR-04 correction, extended to select_mask by the fail-safe
    // above): the refusal upstream already protocol_abort()s on an all-illegal natural `mask`,
    // and the fail-safe above falls back to that same natural mask whenever holding it empty
    // would leave nothing selectable -- so select_mask always has at least one legal entry.
    // 261027-fts stage D: a window draws the button dial ONCE, at its first poll; the polls after it and the fire instant do not (an explored decision is held).
    const bool skip_button_dial = g_skip_button_dial || ( !g_poll && g_win.drawn && g_win.p == p && g_win.sim == sim );
    const float exploration = sim->solver_policy_weights->exploration;
    if ( exploration > 0.0f && !skip_button_dial )
    {
      // ONE draw decides whether to explore at all.
      if ( sim->solver_explore_rng.real() < exploration )
      {
        int legal_casts[ RL_ACTION_DIM ];
        std::size_t n_casts = 0;
        int legal_waits[ RL_ACTION_DIM ];
        std::size_t n_waits = 0;
        for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
        {
          if ( !select_mask[ i ] )
            continue;
          if ( RL_ACTIONS[ i ].kind == rl_action_kind::wait )
            legal_waits[ n_waits++ ] = static_cast<int>( i );
          else
            legal_casts[ n_casts++ ] = static_cast<int>( i );
        }
        const std::size_t n_options = n_casts + ( n_waits > 0 ? 1u : 0u );
        if ( n_options > 0 )
        {
          std::size_t pick = static_cast<std::size_t>(
              sim->solver_explore_rng.range( 0.0, static_cast<double>( n_options ) ) );
          if ( pick >= n_options )
            pick = n_options - 1;   // defensive clamp for range()'s upper edge
          if ( pick < n_casts )
          {
            idx = legal_casts[ pick ];
          }
          else
          {
            // Stage 2: uniform within the wait family.
            std::size_t w = static_cast<std::size_t>(
                sim->solver_explore_rng.range( 0.0, static_cast<double>( n_waits ) ) );
            if ( w >= n_waits )
              w = n_waits - 1;
            idx = legal_waits[ w ];
          }
          // Set whenever the random branch fired, even when the drawn
          // index happens to equal the greedy one -- the bit means "a
          // random action fired", not "the action differed". Phase
          // 213-02's staleness watch splits recorded decisions into ones
          // the recording policy chose on purpose and ones it rolled for,
          // and that split is only well-defined under this reading.
          exploratory = true;
        }
      }
    }

    // 260926-f2e (Need 1): solver_force_decision override -- after the exploration draw has had
    // its (possibly zero) chance to run, before `idx` is read into `action`. This is the one
    // place in this function where overriding `idx` changes nothing else: q_margin/top_q are
    // already computed from mask/best/second_best below (unaffected -- they describe the net's
    // own honest evaluation, not what was executed) and no RNG is consumed downstream of this
    // point for this decision. Gated only by the NATURAL mask (not select_mask) -- forcing is an
    // override of selection itself, and should be refused only when true engine legality
    // disagrees, the same thing the FIFO arm's own forced_deviation.forced() checks.
    bool forced_here = false;  // 261005-branch-teacher: the teacher never overrides a forced press
    if ( !g_poll )  // 261027-fts stage D: a forced decision is made at the ordinary decision only
    for ( const auto& [ forced_k, forced_action ] : sim->solver_force_decisions )
    {
      if ( static_cast<std::int64_t>( seq - 1 ) == forced_k )
      {
        if ( !mask[ forced_action ] )
        {
          protocol_abort( fmt::format(
              "solver_force_decision: forced action {} illegal at decision k={} (seq={}) -- the "
              "engine's own natural mask disagrees", forced_action, forced_k, seq ) );
        }
        idx = forced_action;
        forced_here = true;
        break;
      }
    }

    // 261027-fts stage D: the fire instant resolves against the hold of the window (no-op without one).
    const bool hold_explored = window_resolve_final( sim, p, mask, idx, exploratory, table_play, forced_here );

    // 221-03 (ACT-05/ACT-06, Task 2 point 4): cleared for EVERY decision
    // (not just non-wait ones) -- same discipline as the FIFO arm's own
    // reset immediately after parsing `type` above -- so a "cast"
    // boundary's dump never carries a stale prior wait's requested_wait_sec/
    // wait_anchor. The wait branch below (rl_action_kind::wait) overwrites
    // these right before calling accept_wait().
    sim->solver_control_has_requested_wait_sec = false;
    sim->solver_control_last_requested_wait_sec = 0.0;
    sim->solver_control_last_wait_anchor_label.clear();
    sim->solver_control_last_wait_source.clear();

    // Confidence gap (Phase 212, plan 212-01, TLOG-02): the largest legal Q
    // minus the second largest, considering only entries whose mask byte is
    // non-zero. If fewer than two actions were legal there is no second
    // place -- write a quiet NaN rather than substituting zero. Zero is a
    // legitimate gap (a genuine tie); conflating "tied" with "no
    // alternative existed" would destroy the one number that tells the two
    // apart. Kept in float, matching what forward() produced.
    float best = -std::numeric_limits<float>::infinity();
    float second_best = -std::numeric_limits<float>::infinity();
    int legal_count = 0;
    for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
    {
      if ( !mask[ i ] )
        continue;
      ++legal_count;
      if ( q[ i ] > best )
      {
        second_best = best;
        best = q[ i ];
      }
      else if ( q[ i ] > second_best )
      {
        second_best = q[ i ];
      }
    }
    const float q_margin = table_play ? table_decision.q_margin
                                      : ( legal_count >= 2 ? ( best - second_best ) : std::numeric_limits<float>::quiet_NaN() );
    // top_q (translog version 3, this session's Task 2): the best LEGAL Q
    // value -- the net's own estimate of how much more discounted damage is
    // still coming from this decision onward, before the engine picks an
    // action. Reuses the SAME `best` the margin above was derived from
    // (never a re-derived value), so the two logged fields can never
    // silently disagree about what "best" meant at this boundary. NaN when
    // no action was legal, matching q_margin's own convention.
    const float top_q = table_play ? table_decision.top_q : ( legal_count >= 1 ? best : std::numeric_limits<float>::quiet_NaN() );

    // 261005-branch-teacher (sim/solver_branch.hpp): decision-anchored re-salt, fork() branching at
    // a decision, and the branching teacher. One bool test and a return when no mode is on. Runs
    // after the inputs, mask, values and the margin exist and before the press; may change `idx`
    // (a branch child's button, or the teacher's pick). q_margin/top_q stay the net's own numbers.
    // `action` is bound AFTER this call (it used to be bound right after the force loop above; the
    // lines in between read only mask/q/sim fields, never `action`).
    solver_branch::on_decision( sim, p, seq, q, mask, select_mask, legal_count, q_margin, top_q, exploratory,
                                forced_here, idx );
    const rl_action_desc& action = RL_ACTIONS[ idx ];

    // 261025-bk4 (b2): the table player's "no press". The idle button is expressed as SimC's own idle: nothing is returned and no wait is set, so
    // player_ready_event_t::execute takes its READY_POLL branch and available() (gauss 100 ms) schedules the next try. The row is written first, as
    // for every decision. The requested-wait fields were cleared above.
    if ( table_play && table_decision.idle )
    {
      sim->solver_control_last_reply_type = "wait";
      sim->solver_control_has_pending_wait = false;
      if ( g_poll )
      {
        g_poll_out.kind = poll_out_t::kind_t::wait;
        g_poll_out.idx = idx;
        g_poll_out.explored = exploratory;
      }
      submit_decision( false, nullptr, &table_decision, sim, p, seq, obs, mask, idx, q_margin, top_q, rl_translog::CHOSEN_TARGET_SENTINEL_NO_PICK, false,
                       exploratory, row_held, nullptr, 0, 0, rl_translog::CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK,
                       sim->solver_record_apl_choice && apl_choice ? apl_choice->name() : nullptr, false,
                       rl_translog::CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK, rl_translog::CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK,
                       chosen_enemy_index_of( p ), &stock_pick );
      return nullptr;
    }

    if ( action.kind == rl_action_kind::cast )
    {
      // 210-G23: set the same member the FIFO arm sets at its own
      // reply-type write above -- decision_dump::record() reads this and
      // the dumps must be honest whether or not anything ever diffs them.
      sim->solver_control_last_reply_type = "cast";
      // Flight recorder decision row (Phase 212, plan 212-01, TLOG-02).
      // Written BEFORE accept_cast(), not after: accept_cast() can refuse
      // a not-ready action and abort the run, and a decision that was made
      // and then refused is exactly the decision a later investigation
      // would most want to see. No-op when rl_translog= is unset.
      //
      // Corrected join premise (D-01, refuted premise R-1): 212-CONTEXT's
      // D-01 said this log and decision_dump could be joined on a shared
      // decision counter. They cannot -- decision_dump::record() writes t,
      // iteration, thread, boundary, actor and chosen, and no decision
      // counter at all (decision_dump.cpp's record()); the only three
      // occurrences of solver_control_seq in the whole engine are this
      // arm's `seq` above, sim.hpp's member declaration, and a comment in
      // sim.cpp. If a join is ever needed the key is (thread, iteration,
      // t), with both sides' times rounded to the millisecond grid the
      // simulator actually runs on -- float32 near t=300s resolves to
      // about 3e-5s, roughly 33x finer than a millisecond, so the rounding
      // is exact rather than approximate. Nothing joins today and no side
      // channel is being built (rulings 210-G21/212-G5); this comment
      // exists so the next reader does not go looking for a field that
      // was never there.
      //
      // 228-09 (D-23/TGT-08): resolve THIS action's own stamped pick for the transition-log's
      // chosen-target field, READ via lookup_pick() -- never a fresh select() (D-12). Written
      // BEFORE accept_cast() below, same reasoning as the decision row itself (a decision that
      // gets refused should still show what it named). `resolve_action` is a pure name lookup
      // (the SAME one accept_cast() makes internally, solver_control.cpp:121) -- not a second
      // selector computation.
      std::uint16_t chosen_target_actor_index = rl_translog::CHOSEN_TARGET_SENTINEL_NO_PICK;
      // 230-04 (SCOR-02, R-B): the candidate block rl_target_select CAPTURED for THIS action's
      // pick above -- READ via lookup_candidate_block(), never recomputed (D-12, same discipline
      // chosen_target_actor_index's own lookup_pick() call already follows). Defaults to "no
      // block" (nullptr features, sentinel slot) when the rules path was active, this was not a
      // targeted action, or nothing was captured this decision -- record_decision() itself then
      // writes the all-zero/sentinel candidate fields.
      const float*  candidate_block_features    = nullptr;
      std::uint16_t candidate_block_mask        = 0;
      std::uint8_t  candidate_block_count       = 0;
      std::uint8_t  candidate_block_chosen_slot = rl_translog::CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK;
      // 259-07 (R5): the rules' own slot and the slot the observation described, read from the
      // captured block (never recomputed) and written beside `chosen_slot` (what happened).
      std::uint8_t  candidate_block_rules_slot    = rl_translog::CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK;
      std::uint8_t  candidate_block_observed_slot = rl_translog::CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK;
      // 230-04 (SCOR-02, R-B) / 259-05b (R5): set true when the random-aim dial (immediately below)
      // actually fired -- written as the row's own FLAG_AIM_EXPLORED bit (bit 6), NEVER
      // FLAG_EXPLORATORY, which means the button dial only.
      bool candidate_aim_explored = false;
      {
        const bool is_rl_actor = std::strcmp( p->name(), RL_ACTOR_NAME ) == 0 && !p->is_pet();
        if ( p->sim->target_select_enabled && is_rl_actor )
        {
          action_t* resolved_for_pick = solver_control::resolve_action( p, action.token );
          if ( resolved_for_pick != nullptr && rl_target_select::is_targeted_action( resolved_for_pick ) )
          {
            bool      found = false;
            player_t* pick  = rl_target_select::lookup_pick( resolved_for_pick, &found );
            if ( found && pick != nullptr )
              chosen_target_actor_index = static_cast<std::uint16_t>( pick->actor_index );

            // 259-07 (R1, fork C2 mode (i), the aim-only probe): with a head loaded and
            // `aim_obs_source = 0` the observation above described the RULES' picks for every
            // spell; the head now aims ONLY the spell the net chose, here, before the dial
            // block below. The forward pass draws no random numbers, so this shifts no dice.
            // The head's context inputs are read straight off the built observation (mode (i)
            // has it; mode (ii) evaluated them before the observation existed and asserted the
            // equality in build_obs). `target_scorer_force_rules=1` never reaches this: the
            // rules aim and the observation follows them in both modes. After the head the
            // pick and the block are re-read so the dial and accept_cast see the head's pick.
            if ( found && pick != nullptr && sim->solver_policy_weights &&
                 sim->solver_policy_weights->has_aim_head &&
                 sim->solver_policy_weights->aim.obs_source == 0 && !sim->target_scorer_force_rules )
            {
              float context[ RL_AIM_CONTEXT_COUNT ];
              for ( std::size_t j = 0; j < RL_AIM_CONTEXT_COUNT; ++j )
                context[ j ] = obs[ RL_AIM_CONTEXT_OBS_SLOTS[ j ] ] > 0.0f ? 1.0f : 0.0f;
              rl_target_select::run_target_head( resolved_for_pick, context,
                                                 /*observation_follows_head=*/false );
              found = false;
              pick  = rl_target_select::lookup_pick( resolved_for_pick, &found );
              if ( found && pick != nullptr )
                chosen_target_actor_index = static_cast<std::uint16_t>( pick->actor_index );
            }

            bool                              block_found = false;
            rl_target_select::candidate_block block =
                rl_target_select::lookup_candidate_block( resolved_for_pick, &block_found );
            if ( block_found && block.features != nullptr )
            {
              candidate_block_features      = block.features;
              candidate_block_mask          = block.mask;
              candidate_block_count         = block.count;
              candidate_block_chosen_slot   = block.chosen_slot;
              candidate_block_rules_slot    = block.rules_slot;
              candidate_block_observed_slot = block.observed_slot;
            }

            // 230-04 (SCOR-02, R-B): the SECOND exploration dial, over CANDIDATES -- drawn on the
            // SAME dedicated `sim->solver_explore_rng` stream the action dial above already used,
            // applied ONLY here (after the action has been chosen -- this spell, never all eight
            // read_action_gate_bits pre-computes gate bits for) and only when more than one
            // candidate was legal. See the action dial's own top-of-function comment for which
            // dial carries the schedule under which mode. On a hit, the pick is REPLACED by a
            // uniform draw over the SAME legal-candidate set the block above describes --
            // `build_candidate_facts` reuses `generic_filter`/`build_enemy_fact` VERBATIM (D-20:
            // no third copy of the filter) against a game state that has not advanced since
            // `select()` scored it moments earlier this same decision, so its order matches the
            // block's own slot order exactly. `apply_head_pick` (renamed from
            // `apply_candidate_exploration`, 240-05 Task 2 -- the SAME seam `run_target_head`'s
            // own overwrite now also goes through) overwrites the STAMPED pick (so accept_cast()'s
            // own lookup_pick() call below sees the replacement) and the block's own chosen_slot
            // (so the row records what happened, never what the score would have chosen).
            //
            // 240-05 Task 3 (D5(a), must_haves: "The aim random-try dial runs on the rules path
            // too"): the `has_scorer` test this condition used to carry is REMOVED -- it never let
            // the draw fire on any arm to date (no v4 blob with a scorer section had ever been
            // loaded), and the comparator arm now carries a real scorer section too (D8(a): the
            // SAME binary, the SAME weights file, aim forced to the rules), so gating on a loaded
            // head would have made the comparator pay NO aim randomisation while the learning arms
            // paid its full cost -- an unmatched comparison. The null check on
            // `sim->solver_policy_weights` itself stays (a scripted/non-RL run may carry no
            // weights blob at all, and `.scorer.exploration` immediately below needs a live object
            // to read); every OTHER condition is unchanged: a stamped pick must exist, the
            // candidate table must be present, and there must be more than one candidate.
            if ( found && pick != nullptr && candidate_block_features != nullptr &&
                 candidate_block_count > 1 && sim->solver_policy_weights )
            {
              // 259-05b (R5, R9): the dial's rate comes from the loaded file's AIM section
              // (`aim.exploration`), available whenever a section is present -- a dial-only
              // section included, which is how the rules arm fires it -- and 0 otherwise. The
              // button dial's own rate (`exploration`) is a separate field and never read here.
              const float candidate_exploration =
                  sim->solver_policy_weights->has_aim_section
                      ? sim->solver_policy_weights->aim.exploration
                      : 0.0f;
              if ( candidate_exploration > 0.0f && !g_poll && !hold_explored &&  // 261027-fts stage D: the aim dial is drawn at the fire instant only
                   sim->solver_explore_rng.real() < candidate_exploration )
              {
                std::vector<rl_target_select::enemy_fact> legal =
                    rl_target_select::build_candidate_facts( resolved_for_pick, /*harmful=*/true );
                if ( !legal.empty() )
                {
                  const int draw = static_cast<int>(
                      sim->solver_explore_rng.range( 0.0, static_cast<double>( legal.size() ) ) );
                  const std::uint8_t draw_slot = static_cast<std::uint8_t>( draw );
                  if ( rl_target_select::apply_head_pick( resolved_for_pick,
                                                           legal[ draw ].candidate,
                                                           draw_slot ) )
                  {
                    chosen_target_actor_index =
                        static_cast<std::uint16_t>( legal[ draw ].actor_index );
                    candidate_block_chosen_slot = draw_slot;
                    candidate_aim_explored       = true;
                  }
                }
              }
            }
          }
        }
      }
      if ( g_poll )
      {
        g_poll_out.kind = poll_out_t::kind_t::cast;
        g_poll_out.idx = idx;
        g_poll_out.target = chosen_target_actor_index;
        g_poll_out.explored = exploratory;
      }
      // 261027-fts: the row is STAGED here (filled with everything the policy saw) and committed when the press executes (queue_execute() / the queue event);
      // the action pointer the engine will queue is set below, once accept_cast() has resolved it.
      submit_decision( true, nullptr, table_play ? &table_decision : nullptr, sim, p, seq, obs, mask, idx, q_margin, top_q,
                                     chosen_target_actor_index, false,
                                     exploratory, row_held,
                                     candidate_block_features, candidate_block_mask,
                                     candidate_block_count, candidate_block_chosen_slot,
                                     // 260927-d1 (D1 census, Stage 1 Task 2): the engine's own
                                     // APL pick at this decision, same action_t* the FIFO wire's
                                     // own "apl_choice" field names (solver_control.cpp:656-657
                                     // above) -- nullptr when the option is off, matching every
                                     // other "off" path's zero-cost convention.
                                     sim->solver_record_apl_choice && apl_choice ? apl_choice->name() : nullptr,
                                     // 259-05b (R5): the random-aim dial's own flag (bit 6).
                                     candidate_aim_explored,
                                     // 259-07 (R5): the rules' slot and the observed slot.
                                     candidate_block_rules_slot, candidate_block_observed_slot,
                                     // 266-21 (funnel mode, record format 13): the chosen enemy (the tag)
                                     // at this boundary, read from the field the chooser wrote, never
                                     // recomputed here.
                                     chosen_enemy_index_of( p ),
                                     // 271.4-03: the dry stock-rotation pick (null when the option is off).
                                     rl_stock_eval::enabled( sim ) ? &stock_pick : nullptr );
      // 212-CR-FIX WR-06: accept_cast() can refuse a not-ready action via
      // protocol_abort() (a throw), which unwinds past combat_end()'s
      // record_close() hook entirely for this fight -- without this catch,
      // the decision row just written above (the one thing the
      // write-before-accept ordering exists to preserve) would be buffered
      // in memory and lost with the process. Flush what is pending, then
      // propagate the original exception unchanged.
      try
      {
        action_t* const resolved_cast = accept_cast( p, action.token, seq );
        if ( g_staged.valid && g_staged.sim == sim )
          g_staged.action = resolved_cast;
        if ( !g_poll )
          g_last_pick = last_pick_t{ true, resolved_cast, idx, chosen_target_actor_index, exploratory };
        return resolved_cast;
      }
      catch ( ... )
      {
        flush_staged_on_abort( sim );
        rl_translog::flush_pending( sim );
        throw;
      }
    }

    // The removed in-process turn dispatch arm (R6-27, 233.1-01) was here -- deleted with the
    // whole action. A policy that somehow still selects `rl_action_kind::turn` (the enum member
    // itself is not regenerated out of the header until 233.1-07, per this task's own decision
    // rule) now falls through to the unknown-kind FATAL immediately below, matching 233-05c
    // CR-01's fail-closed discipline rather than silently mis-dispatching as a wait.

    if ( action.kind != rl_action_kind::wait )
    {
      // T-232-12 (threat register): a kind that reaches this dispatch and matches NEITHER cast
      // NOR turn above must FATAL by name here, never silently fall through and be treated as a
      // wait -- the wait code immediately below has no `if` guard of its own (it is the
      // structural "otherwise" for this three-way kind, same discipline mask.py's own cast
      // fallthrough uses), so a future FOURTH kind added to the registry without a matching
      // branch here would otherwise be silently mis-dispatched as a wait instead of failing
      // loud. `registry.selftest.py`'s cross-layer kind-subset invariant is the OTHER half of
      // this defence (a fourth kind cannot even be declared without also updating mask.py and
      // gen_rl_constants.py) -- this FATAL is the runtime backstop for the case both layers
      // were somehow updated but this switch was not.
      protocol_abort( "in-process reply dispatch: RL_ACTIONS[" + std::to_string( idx ) +
                       "].kind is none of cast/wait" );
    }

    // rl_action_kind::wait -- action.token is null for this entry (see
    // RL_ACTIONS' own comment in rl_policy_constants.h). 221-03 (ACT-05/
    // ACT-06): passes action.wait_anchor -- an anchor of kind "none"
    // reproduces today's next-event-minimum build_wait() behaviour byte
    // for byte; an anchored kind computes the per-anchor duration.
    sim->solver_control_last_reply_type = "wait";
    const rl_policy::wait_result wr = rl_policy::build_wait( state, action.wait_anchor );
    // 221-03 (ACT-05/ACT-06, Task 2 point 4): the value HANDED TO
    // accept_wait() below, before its own fight-end/raid-event clamp, and
    // the chosen wait's registry label (RL_ACTIONS[idx].label -- always
    // known on this arm, unlike the FIFO arm above).
    sim->solver_control_has_requested_wait_sec = true;
    sim->solver_control_last_requested_wait_sec = wr.seconds;
    sim->solver_control_last_wait_anchor_label = action.label ? action.label : "";
    // 260902/cr2 (CR-03/WR-08's fix): carry the SAME wait_result::source
    // build_wait() already computed -- never re-derived. Empty on the
    // FIFO arm by construction (never reaches this in-process branch).
    sim->solver_control_last_wait_source = wr.source;
    if ( g_poll )
    {
      g_poll_out.kind = poll_out_t::kind_t::wait;
      g_poll_out.idx = idx;
      g_poll_out.explored = exploratory;
    }
    // Flight recorder decision row, wait branch. wr.floored is the fifth
    // flag bit's only source -- it says the wait length came from the
    // floor rather than from a real timer. No-op when rl_translog= is
    // unset. 228-09 (D-23/TGT-08): a wait has no action to pick a target
    // for -- always the sentinel.
    submit_decision( false, nullptr, nullptr, sim, p, seq, obs, mask, idx, q_margin, top_q,
                                   rl_translog::CHOSEN_TARGET_SENTINEL_NO_PICK, wr.floored, exploratory,
                                   row_held, nullptr, 0, 0,
                                   rl_translog::CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK,
                                   // 260927-d1 (D1 census, Stage 1 Task 2): same as the cast
                                   // branch above.
                                   sim->solver_record_apl_choice && apl_choice ? apl_choice->name() : nullptr,
                                   false, rl_translog::CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK,
                                   rl_translog::CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK,
                                   // 266-21: the tag at this boundary, same as the cast branch above.
                                   chosen_enemy_index_of( p ),
                                   // 271.4-03: the dry stock-rotation pick, same as the cast branch above.
                                   rl_stock_eval::enabled( sim ) ? &stock_pick : nullptr );
    // 212-CR-FIX WR-06: same reasoning as the cast branch above -- flush on
    // an abort out of accept_wait() so the decision row already appended
    // survives it.
    try
    {
      if ( !g_poll )  // 261027-fts stage D: a poll sets no wait
        accept_wait( sim, et, wr.seconds,
                     "in-process (seq=" + std::to_string( seq ) + ", source=" + wr.source + ")" );
    }
    catch ( ... )
    {
      rl_translog::flush_pending( sim );
      throw;
    }
    return nullptr;
  }
}

void finish( sim_t* sim )
{
  if ( !sim->solver_control_req_stream )
    return;

  if ( sim->solver_control_req_stream->is_open() )
  {
    io::ofstream& req = *sim->solver_control_req_stream;
    req << "{\"v\":" << SOLVER_CONTROL_PROTOCOL_VERSION << ",\"type\":\"bye\"}\n";
    req.flush();
    req.close();
  }

  if ( sim->solver_control_rep_stream && sim->solver_control_rep_stream->is_open() )
    sim->solver_control_rep_stream->close();
}

// 214-01 C1. See the declaration's own doc comment in solver_control.hpp
// for the full justification; this body is deliberately just the clear.
void reset_iteration( sim_t* sim )
{
  // Phase 266 (266-09, R7): re-seed the chooser's own dice once per fight, in scripted fights too (the
  // chooser runs there as well, R13), BEFORE the early return below. The seed is derived from the sim seed,
  // the thread, the fight index and a source name of its own; seeding takes nothing from any other stream.
  sim->solver_target_rng.seed(
      rng::per_source_seed( sim->seed, sim->thread_index, sim->rng_iteration_index(), "sim|solver_target" ) );

  // 261027-fts: the census and the pending rows belong to ONE fight. on_fight_end() resolved whatever the last fight left; a survivor is a bug.
  // 261027-fts stage D: the window and the queue chain belong to one fight; their events died with the fight's event queue (never cancelled through these
  // pointers, which are stale by now).
  g_win = window_t{};
  g_q = queue_poll_t{};
  g_q_replaces_next = 0;
  g_last_pick = last_pick_t{};
  g_poll = false;
  g_skip_button_dial = false;
  if ( g_staged.valid || g_queued.valid )
    pending_invariant( fmt::format( "a {} decision (seq={}) survived into the next fight", g_queued.valid ? "queued" : "staged",
                                    g_queued.valid ? g_queued.seq : g_staged.seq ) );
  std::memset( rl_translog::fight_census().n, 0, sizeof( rl_translog::fight_census().n ) );
  g_line_written = true;
  g_dump_outcome = dump_outcome_t{};

  if ( sim->solver_control_str.empty() && sim->solver_policy_str.empty() )
    return;

  sim->solver_observe_idle_open = false;  // 261021-ezq: an idle stretch never carries over into the next fight
  sim->solver_control_auto_attack_started = false;
  sim->solver_control_has_pending_wait = false;
  sim->solver_control_pending_wait_s = 0.0;
  sim->solver_control_last_reply_type.clear();
  // solver_control_seq is DELIBERATELY NOT cleared -- see the header
  // comment. Stream handles are likewise untouched.

  // Phase 213 D-08: re-seed the DEDICATED exploration stream once per
  // fight. Derived from three values the sim already holds -- its own
  // seed, the current fight index, and the writer (thread) index --
  // combined so adjacent fights start far apart (the fight index is
  // multiplied by a large odd constant before adding). The fight index has
  // to be part of this derivation: without it, every fight in a
  // multi-fight launch would draw the identical exploration sequence at
  // the identical decision positions, which is not exploration, it is one
  // pattern repeated N times. `rng::rng_t::seed()` runs the result through
  // its own 64-bit mixer, so this derivation only needs to spread the
  // input apart, not pre-mix it.
  //
  // 260919-psb: the fight-index term uses sim->rng_iteration_index(), not the raw batch position
  // `sim->current_iteration`, for the same reason per_source_seed() callers do (see that
  // method's doc comment in sim.hpp). With rl_iteration_seeds= non-empty, `sim->seed` is already
  // the distinct COMMITTED per-iteration seed S_i by the time this runs -- reset() installs it
  // before combat_begin() schedules this event -- so the spread this term exists to provide is
  // already carried by seed itself; folding in the raw batch position on top would make the same
  // committed seed draw a different exploration sequence depending on where in the batch it
  // landed, which is exactly the batch-position dependency rl_iteration_seeds exists to remove.
  // rng_iteration_index() returns 0 in that case (the term drops out, seed alone spreads fights
  // apart) and current_iteration unchanged otherwise, so behavior with rl_iteration_seeds empty
  // is byte-identical to before this change.
  constexpr std::uint64_t EXPLORE_SEED_FIGHT_SPREAD = 0x9E3779B97F4A7C15ull;
  const std::uint64_t explore_seed = sim->seed
      + static_cast<std::uint64_t>( sim->rng_iteration_index() ) * EXPLORE_SEED_FIGHT_SPREAD
      + static_cast<std::uint64_t>( sim->thread_index );
  sim->solver_explore_rng.seed( explore_seed );

  // 261025-bk4 (b2): the table player's per-fight state (the row counter, the pending reward; scope=fight also re-copies Q, N, M2 from the file).
  rl_table_policy::on_fight_start( sim );
}
} // namespace solver_control
