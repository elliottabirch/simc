// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "solver_control.hpp"

#include "action/action.hpp"
#include "action/attack.hpp"
#include "decision_dump.hpp"
#include "player/player.hpp"
#include "sim/event.hpp"
#include "sim/rl_policy.hpp"
#include "sim/rl_target_select.hpp"
#include "sim/rl_translog.hpp"
#include "sim/sim.hpp"
#include "util/io.hpp"

#include "rapidjson/document.h"

#include "fmt/format.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
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

// Resolves a SimC internal action name (`name_str`) to one of the actor's
// already-constructed action_t objects. Never constructs a new action --
// solver_control only selects among what the actor's action list already
// built at init time (every solver-catalog spell has an unconditional
// actions+= entry in the generated episode .simc for exactly this reason).
// Scans p->action_list for the first entry whose name_str matches `name`,
// preferring a player-CASTABLE (non-background) action_t over a same-name
// background one. action_t::action_t() (action.cpp) unconditionally
// self-registers EVERY constructed action into player->action_list --
// including internal `background = true` helper actions class modules build
// for buff-triggering/bookkeeping (e.g. sc_paladin.cpp's
// `active.background_avenging_wrath`, constructed once per talent-init
// regardless of whether the player's own APL/solver catalog also carries a
// real, player-castable `avenging_wrath` actions+= entry) -- so a same-name
// collision is possible whenever a class module ships both a background
// helper and a real spell under identical name_str. A plain first-match
// linear scan can therefore return the background helper (construction
// order is init-sequence-dependent, not alphabetical or list-position
// stable), which is never `ready()` the way solver_control's caller expects
// and stalls the episode. Found live: sc_paladin.cpp:3327 constructs
// `background_avenging_wrath` ahead of the player's real `avenging_wrath`
// action in the action_list, so a `{"type":"cast","action":"avenging_wrath"}`
// reply resolved to the background helper and never advanced. This
// preference is deliberately general (no spell special-case): ANY class
// module with a background/foreground name_str collision hits the same
// fix.
action_t* resolve_action_prefer_castable( player_t* p, const std::string& name )
{
  action_t* background_match = nullptr;
  for ( action_t* a : p->action_list )
  {
    if ( a->name_str != name )
      continue;
    if ( !a->background )
      return a;
    if ( !background_match )
      background_match = a;
  }
  // No player-castable match under this exact name -- fall back to a
  // same-name background action rather than reporting unresolvable, in case
  // some class module legitimately has no foreground counterpart for this
  // token (keeps prior behavior for that case; resolved->ready() downstream
  // still fails closed if it truly can't be cast).
  return background_match;
}

// resolve_action() itself is DECLARED in solver_control.hpp and DEFINED
// below, inside `namespace solver_control` (221-01, Pattern 2) -- moved out
// of this anonymous namespace so `rl_policy_obs.cpp`'s engine-truth handle
// table (read_action_gate_bits) and this file's own accept_cast() below
// resolve a name to the SAME action_t*, by construction, never by two
// independently-maintained loops that could drift. It still calls
// resolve_action_prefer_castable() above unqualified -- an anonymous
// namespace's members are visible, unqualified, throughout this whole
// translation unit regardless of which named namespace surrounds the call
// site, so no qualification is needed for that call.

// Shared 'cast' epilogue (210-05R Task 1) -- both transports resolve their
// chosen action-name token through the same resolve_action()/ready() gate,
// so a not-ready cast hits the identical protocol_abort() on either arm
// (criterion 1's "zero not-ready FATALs" is one grep regardless of which
// transport answered the boundary).
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
    rl_target_select::retarget( resolved, p, pick );
  }

  return resolved;
}

// FORK-04, second half (owner ruling Q69, 232-07-PLAN.md Task 1): closes the 1ms-wide
// fight-end-tie band WR-01 (260902/cr2) measured in accept_wait()'s CR-04 re-floor -- see that
// function's own comment above the re-floor for the full derivation (at R=50ms remaining,
// `0.050 - 0.001 = 0.049` re-floors to exactly `0.05`, landing the Player-Ready event back on the
// tie the first shave removed). `accept_turn()` below has the SAME exposure through a different
// route: it requests a FIXED `RL_WAIT_FLOOR_SECONDS` unconditionally, with no prior fight-end
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
  // (still legal, `anchor: null`) fixed wait and the loop repeats at zero elapsed time (232-03's
  // `accept_turn()` below quotes this paragraph verbatim -- the SAME hazard applies to a free
  // turn). This is exactly the spin `maskRules.waitZeroLengthForbidden`
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

// OBS-05/R-W/R-V (tstl-sylvanas phase 232, plan 232-03): "turn to face" -- a THIRD registry
// kind, promoted alongside cast/wait (never smuggled through the wait arm). Selects the NEAREST
// live enemy for which the facing model's `is_in_front` test is false (R-W), ties broken on the
// stable `(actor_index, actor_spawn_index)` identity pair ascending -- the SAME ordering
// `select()`'s own final tie-break rung already trusts (`rl_target_select.cpp`, "final tie-break
// on the stable (actor_index, actor_spawn_index) identity pair, ascending"). Faces the player at
// it via `p->face( *chosen )` ONLY -- deliberately NOT `rl_target_select::retarget()`, which ALSO
// mutates `a->set_target`/`p->target`/weapon targets: a turn has no `action_t` and must not touch
// the player's current cast target, only its facing (WR-07: never a raw `a->target`/`p->target`
// write either way). Foreground-only, mirroring `accept_wait`'s own `et` refusal above and
// `mask.py`/`build_mask`'s shared foreground-only rule for this kind (R-W).
void accept_turn( sim_t* sim, execute_type et, player_t* p, const std::string& context )
{
  if ( et != execute_type::FOREGROUND )
    protocol_abort( "'turn' reply is illegal at a non-FOREGROUND boundary (boundary=" +
                     decision_dump::boundary_name( et ) + "): " + context );

  player_t* chosen = nullptr;
  double chosen_distance = 0.0;
  for ( player_t* t : sim->target_non_sleeping_list )
  {
    if ( !t->is_enemy() )
      continue;
    // R-W: a candidate is anyone FAILING the raw in-front test (`cos_half_angle = 0.0`) --
    // the SAME predicate `mask.py`'s `_any_enemy_behind_player` and this file's own
    // `build_mask` turn-bit computation use, deliberately NOT `facing_enabled`-gated
    // (`rl_target_select.cpp:141`'s precedent: this is ground truth for the decision, not an
    // optional display feature).
    if ( p->is_in_front( *t, 0.0 ) )
      continue;
    const double dist = p->get_player_distance( *t );
    bool better;
    if ( chosen == nullptr )
      better = true;
    else if ( dist != chosen_distance )
      better = dist < chosen_distance;
    else
      better = std::tie( t->actor_index, t->actor_spawn_index ) <
               std::tie( chosen->actor_index, chosen->actor_spawn_index );
    if ( better )
    {
      chosen = t;
      chosen_distance = dist;
    }
  }

  if ( chosen == nullptr )
  {
    // The mask (both mirrors) is legal for `turn` iff at least one live enemy fails `in_front`
    // -- this branch should therefore be unreachable. A policy that reaches it anyway (a mask
    // defect, a stale request, a hand-crafted FIFO reply) gets a loud, named FATAL, never a
    // silent no-op that leaves the player facing nobody while still charging the wait -- the
    // same "fail loud" discipline `accept_cast`'s own not-ready refusal above uses.
    protocol_abort( "'turn' reply chosen but no live enemy fails the in-front test (mask should "
                     "have made this illegal): " + context );
  }

  p->face( *chosen );

  // R-V/P232-22 (this plan's whole cost model): a FREE turn spins at zero elapsed time, the SAME
  // hazard `accept_wait`'s CR-04 comment above describes for a zero-length wait -- turning
  // toward one enemy leaves another behind, the bit stays legal, and a
  // deterministic policy would re-pick it FOREVER at the SAME sim time. This is exactly the
  // spin `maskRules.waitZeroLengthForbidden` (enhancement.json) declares forbidden -- quoting
  // `accept_wait`'s own CR-04 comment above verbatim, because the hazard is identical: "a
  // zero-length wait is the one outcome the floor exists to forbid." The turn pays through the
  // SAME existing pending-wait pair `accept_wait` sets, at the SAME floor value -- never a
  // hidden one-shot-per-decision flag the Python mirror (`mask.py`) cannot see (Q232-6 is the
  // owner's route to a free-turn alternative; not taken here).
  //
  // FORK-04, second half (Q69/232-07): unlike accept_wait(), this function never computes a
  // fight-end bound at all before requesting the fixed RL_WAIT_FLOOR_SECONDS -- so it has the
  // SAME event-ordering exposure accept_wait()'s re-floor does, through a different route: a
  // turn requested when almost exactly RL_WAIT_FLOOR_SECONDS remains in the fight can land
  // EXACTLY on the fight-end tie with `sim_end_event_t`. Apply the same shared shave.
  double turn_sec = RL_WAIT_FLOOR_SECONDS;
  reshave_fight_end_tie( sim, turn_sec );
  sim->solver_control_pending_wait_s = turn_sec;
  sim->solver_control_has_pending_wait = true;
}
} // anonymous namespace

namespace solver_control
{
// 221-01 (ACT-02, Pattern 2). Exposed so `rl_policy_obs.cpp`'s engine-truth
// handle table (read_action_gate_bits) and accept_cast() above resolve a
// token to the SAME action_t* -- this is the single seam that makes
// "mask/engine agreement" true by construction rather than by census.
// resolve_action_prefer_castable() stays file-local (anonymous namespace,
// unchanged) and is called here unqualified.
action_t* resolve_action( player_t* p, const std::string& name )
{
  if ( action_t* resolved = resolve_action_prefer_castable( p, name ) )
    return resolved;

  // Fallback for spells whose *constructed* action_t renames its own
  // name_str based on live talent state, even though the APL/sequence
  // CONSTRUCTOR token stays fixed (found live during 116-01 smoke testing:
  // sc_paladin_retribution.cpp's templars_verdict_t sets name_str to
  // "final_verdict" once the Final Verdict talent is active, even though
  // "templars_verdict" remains the only registered create_action() factory
  // token -- there is no "final_verdict" branch in create_action at all).
  // scripts/simc-eval/name-map.json intentionally emits ONLY the canonical
  // constructor-token name ("templars_verdict", never "final_verdict" --
  // writing the display name as a sequence entry fails SILENTLY, per that
  // file's own landmine note), so this alias is tried only when the exact
  // match above fails, keeping the driver/worker/name-map unaware of the
  // live rename.
  static const std::unordered_map<std::string, std::string> NAME_STR_RENAME_ALIASES = {
    { "templars_verdict", "final_verdict" },
  };
  auto alias_it = NAME_STR_RENAME_ALIASES.find( name );
  if ( alias_it != NAME_STR_RENAME_ALIASES.end() )
  {
    if ( action_t* resolved = resolve_action_prefer_castable( p, alias_it->second ) )
      return resolved;
  }

  return nullptr;
}

action_t* choose( player_t* p, action_t* apl_choice, execute_type et )
{
  sim_t* sim = p->sim;
  if ( sim->solver_control_str.empty() && sim->solver_policy_str.empty() )
    return apl_choice;

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
  if ( et == execute_type::FOREGROUND && !sim->solver_control_auto_attack_started )
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
  if ( et == execute_type::FOREGROUND && !p->is_moving() )
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

    if ( type == "turn" )
    {
      // OBS-05/R-W/R-V (232-03): a 'turn' reply carries no 'action' token and no 'sec' -- its
      // target is computed inside accept_turn() (nearest live enemy failing in_front) and its
      // cost is the fixed RL_WAIT_FLOOR_SECONDS pending-wait pair, not a caller-supplied value.
      // `solver_control_last_reply_type` is already set to "turn" by the generic assignment
      // above (this arm's own `type` string, unlike the in-process arm below which has no wire
      // string and must set it explicitly).
      accept_turn( sim, et, p, "seq=" + std::to_string( seq ) + ": " + line );
      return nullptr;
    }

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
    // OBS-05 (232-03): stamp the PRE-decision behind-enemy predicate onto `sim`, BEFORE any
    // accept_* call below can mutate facing -- see sim.hpp's own doc comment on this pair for
    // why decision_dump.cpp needs this snapshot rather than recomputing from its own (POST-
    // decision) all_enemies/player_position block.
    sim->solver_control_has_any_enemy_behind_player_at_decision = true;
    sim->solver_control_any_enemy_behind_player_at_decision = state.any_enemy_behind_player;
    // 232-12 (ME-03): the per-decision guard -- `read_state()` above already called
    // `rl_target_select::begin_decision( p )` internally, so this is the CURRENT decision's own
    // stamp, not a stale read of a prior one.
    sim->solver_control_any_enemy_behind_player_at_decision_stamp = rl_target_select::current_decision_stamp( p );
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

    // Quick task tj1 (2026-09-01, timing-jitter exploration). Per-episode
    // cooldown-timing hold, ANDed in AFTER the allow-list AND immediately
    // above and BEFORE the all-illegal refusal immediately below -- so the
    // refusal's own "post-AND" framing already covers this AND too, and a
    // hold that would manufacture an all-illegal mask is caught by the
    // SAME fail-safe shape 222-07 built for the allow-list AND, not a
    // second bespoke one. Disabled (the common case, and every non-tj1
    // caller): solver_hold_until_release_s is empty, one .empty() check,
    // zero further cost.
    if ( !sim->solver_hold_until_release_s.empty() )
    {
      // Scratch copy so the fail-safe below can fall back to the pre-hold
      // mask (post allow-list AND) without re-deriving it.
      std::uint8_t held_mask[ RL_ACTION_DIM ];
      std::memcpy( held_mask, mask, sizeof( mask ) );
      bool any_hold_applied = false;
      const double now_s = sim->current_time().total_seconds();
      for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
      {
        const double release_s = sim->solver_hold_until_release_s[ i ];
        if ( release_s >= 0.0 && now_s < release_s )
        {
          held_mask[ i ] = 0;
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
          std::memcpy( mask, held_mask, sizeof( mask ) );
        }
        else
        {
          // Fail-safe (tj1 design doc, explicit requirement): never let the
          // hold manufacture an empty mask. Ignore the hold for THIS
          // decision only -- `mask` stands exactly as the allow-list AND
          // above left it -- and count the override so a training run can
          // report how often its declared hold set collided with the
          // engine's own legality gate.
          ++sim->solver_hold_until_override_count;
        }
      }
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

    float q[ RL_ACTION_DIM ];
    rl_policy::forward( *sim->solver_policy_weights, obs, mask, q );

    const int greedy_idx = rl_policy::masked_argmax( q, mask );
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
    const float exploration = sim->solver_policy_weights->exploration;
    if ( exploration > 0.0f )
    {
      // ONE draw decides whether to explore at all.
      if ( sim->solver_explore_rng.real() < exploration )
      {
        // Collect the SAME legality list the greedy pick just read above --
        // never a freshly built one -- and draw uniformly among the legal
        // indices. The all-illegal case cannot reach here (222-07/CR-04
        // correction): the refusal added immediately after the allow-list
        // AND, above, already protocol_abort()s before forward() or
        // masked_argmax() ever run, so at least one mask[i] is guaranteed
        // non-zero here and legal_indices is never empty. (masked_argmax()
        // itself does NOT fall back to index 0 on an all-illegal row -- see
        // its own definition below -- which is exactly why the refusal
        // lives upstream of it rather than being left to it.) This block
        // therefore still must not invent a second answer for the
        // degenerate case; there is no degenerate case left to answer for.
        std::vector<int> legal_indices;
        for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
        {
          if ( mask[ i ] )
            legal_indices.push_back( static_cast<int>( i ) );
        }
        if ( !legal_indices.empty() )
        {
          const int pick = static_cast<int>(
              sim->solver_explore_rng.range( 0.0, static_cast<double>( legal_indices.size() ) ) );
          idx = legal_indices[ static_cast<std::size_t>( pick ) ];
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

    const rl_action_desc& action = RL_ACTIONS[ idx ];

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
    const float q_margin =
        legal_count >= 2 ? ( best - second_best ) : std::numeric_limits<float>::quiet_NaN();
    // top_q (translog version 3, this session's Task 2): the best LEGAL Q
    // value -- the net's own estimate of how much more discounted damage is
    // still coming from this decision onward, before the engine picks an
    // action. Reuses the SAME `best` the margin above was derived from
    // (never a re-derived value), so the two logged fields can never
    // silently disagree about what "best" meant at this boundary. NaN when
    // no action was legal, matching q_margin's own convention.
    const float top_q = legal_count >= 1 ? best : std::numeric_limits<float>::quiet_NaN();

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
      // 230-04 (SCOR-02, R-B): set true when the candidate dial (immediately below) actually
      // fired -- OR'd into the row's own FLAG_EXPLORATORY bit alongside the action dial's
      // `exploratory` above, since the bit means "a random draw fired here", not "which one".
      bool candidate_exploratory = false;
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

            bool                              block_found = false;
            rl_target_select::candidate_block block =
                rl_target_select::lookup_candidate_block( resolved_for_pick, &block_found );
            if ( block_found && block.features != nullptr )
            {
              candidate_block_features    = block.features;
              candidate_block_mask        = block.mask;
              candidate_block_count       = block.count;
              candidate_block_chosen_slot = block.chosen_slot;
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
            // block's own slot order exactly. `apply_candidate_exploration` overwrites the
            // STAMPED pick (so accept_cast()'s own lookup_pick() call below sees the replacement)
            // and the block's own chosen_slot (so the row records what happened, never what the
            // score would have chosen).
            if ( found && pick != nullptr && candidate_block_features != nullptr &&
                 candidate_block_count > 1 && sim->solver_policy_weights &&
                 sim->solver_policy_weights->has_scorer )
            {
              const float candidate_exploration = sim->solver_policy_weights->scorer.exploration;
              if ( candidate_exploration > 0.0f &&
                   sim->solver_explore_rng.real() < candidate_exploration )
              {
                std::vector<rl_target_select::enemy_fact> legal =
                    rl_target_select::build_candidate_facts( resolved_for_pick, /*harmful=*/true );
                if ( !legal.empty() )
                {
                  const int draw = static_cast<int>(
                      sim->solver_explore_rng.range( 0.0, static_cast<double>( legal.size() ) ) );
                  const std::uint8_t draw_slot = static_cast<std::uint8_t>( draw );
                  if ( rl_target_select::apply_candidate_exploration( resolved_for_pick,
                                                                       legal[ draw ].candidate,
                                                                       draw_slot ) )
                  {
                    chosen_target_actor_index =
                        static_cast<std::uint16_t>( legal[ draw ].actor_index );
                    candidate_block_chosen_slot = draw_slot;
                    candidate_exploratory       = true;
                  }
                }
              }
            }
          }
        }
      }
      rl_translog::record_decision( sim, p, seq, obs, mask, idx, q_margin, top_q,
                                     chosen_target_actor_index, false,
                                     exploratory || candidate_exploratory,
                                     candidate_block_features, candidate_block_mask,
                                     candidate_block_count, candidate_block_chosen_slot );
      // 212-CR-FIX WR-06: accept_cast() can refuse a not-ready action via
      // protocol_abort() (a throw), which unwinds past combat_end()'s
      // record_close() hook entirely for this fight -- without this catch,
      // the decision row just written above (the one thing the
      // write-before-accept ordering exists to preserve) would be buffered
      // in memory and lost with the process. Flush what is pending, then
      // propagate the original exception unchanged.
      try
      {
        return accept_cast( p, action.token, seq );
      }
      catch ( ... )
      {
        rl_translog::flush_pending( sim );
        throw;
      }
    }

    if ( action.kind == rl_action_kind::turn )
    {
      // OBS-05/R-W/R-V (232-03): unlike the FIFO arm, this transport has no wire string to read
      // "turn" off of -- set it explicitly, the same way the cast branch above sets "cast" and
      // the wait code below sets "wait", so decision_dump.cpp's solver_reply_type field is
      // honest for this boundary.
      sim->solver_control_last_reply_type = "turn";
      // A turn has no candidate block and no picked target in the sense record_decision's
      // trailing parameters describe (that machinery is `preference_scorer`'s per-CAST
      // candidate accounting) -- CHOSEN_TARGET_SENTINEL_NO_PICK, same as the wait branch below.
      // `floored=true`: the turn's cost is ALWAYS exactly RL_WAIT_FLOOR_SECONDS (R-V), never a
      // real timer, so it is "floored" by construction on every occurrence, not merely when a
      // real duration happened to clamp down to it.
      rl_translog::record_decision( sim, p, seq, obs, mask, idx, q_margin, top_q,
                                     rl_translog::CHOSEN_TARGET_SENTINEL_NO_PICK, /*floored=*/true,
                                     exploratory );
      // 212-CR-FIX WR-06: same reasoning as the cast branch above -- flush on an abort out of
      // accept_turn() so the decision row already appended survives it.
      try
      {
        accept_turn( sim, et, p, "in-process (seq=" + std::to_string( seq ) + ")" );
      }
      catch ( ... )
      {
        rl_translog::flush_pending( sim );
        throw;
      }
      return nullptr;
    }

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
                       "].kind is none of cast/turn/wait" );
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
    // Flight recorder decision row, wait branch. wr.floored is the fifth
    // flag bit's only source -- it says the wait length came from the
    // floor rather than from a real timer. No-op when rl_translog= is
    // unset. 228-09 (D-23/TGT-08): a wait has no action to pick a target
    // for -- always the sentinel.
    rl_translog::record_decision( sim, p, seq, obs, mask, idx, q_margin, top_q,
                                   rl_translog::CHOSEN_TARGET_SENTINEL_NO_PICK, wr.floored, exploratory );
    // 212-CR-FIX WR-06: same reasoning as the cast branch above -- flush on
    // an abort out of accept_wait() so the decision row already appended
    // survives it.
    try
    {
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
  if ( sim->solver_control_str.empty() && sim->solver_policy_str.empty() )
    return;

  sim->solver_control_auto_attack_started = false;
  sim->solver_control_has_pending_wait = false;
  sim->solver_control_pending_wait_s = 0.0;
  sim->solver_control_last_reply_type.clear();
  sim->solver_control_has_any_enemy_behind_player_at_decision = false;
  sim->solver_control_any_enemy_behind_player_at_decision = false;
  sim->solver_control_any_enemy_behind_player_at_decision_stamp = 0;
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
  constexpr std::uint64_t EXPLORE_SEED_FIGHT_SPREAD = 0x9E3779B97F4A7C15ull;
  const std::uint64_t explore_seed = sim->seed
      + static_cast<std::uint64_t>( sim->current_iteration ) * EXPLORE_SEED_FIGHT_SPREAD
      + static_cast<std::uint64_t>( sim->thread_index );
  sim->solver_explore_rng.seed( explore_seed );
}
} // namespace solver_control
