// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// 261001-bac stage 0 (research clone only, never in production): per-hit buff ledger.
//
// rl_buff_ledger=<path> (empty = off, the default) writes a JSON-lines file, one record per
// line, key `k` names the kind. The record contract (every kind the whole stage writes, with
// fields and units) is `LEDGER-FORMAT.md` beside the quick-task plans in the tstl-sylvanas
// repo (.planning/quick/261001-bac-buff-applier-credit-in-the-engine/). This header and the
// .cpp implement the skeleton: option state, writer, per-fight records, the ledger's own PRESS
// numbering (see open_press) and the per-hit sink.
//
// PURE OBSERVER. Nothing here touches the RNG, schedules or cancels an event, changes an
// action's result, or is read by any routing or rolling decision. With the option off every
// call site is a single `sim->rl_bl_on` bool test and these functions are never entered.
//
// Press numbering (the one design finding that forces this module to own a number): at
// 2bcc19760d the decision sequence number only advances when the net or the FIFO client is in
// control (`++sim->solver_control_seq`), so in a stock-rotation fight EVERY cast carries seq 0
// and the existing cause stamp (seq, class) cannot tell one press from another. The ledger
// therefore adds its own `press` (and `launch`, used from plan 05 on) to rl_cause_t / the
// action state's stamp: -1 whenever the switch is off, never read by anything but the ledger.
#pragma once

#include "sim/rl_credit.hpp"

#include "util/rng.hpp"

#include <cstdint>

struct sim_t;
struct player_t;
struct action_t;
struct action_state_t;

namespace rl_buff_ledger
{
// Press values below zero. A press >= 0 is a foreground cast of the RL actor (see open_press).
// -1 means "no press": the switch is off, or a stamp that was never given one -- on an own-class
// hit that is a LOST press (footer `lost_press`, must be 0). Chains that legitimately start
// outside a cast carry a sentinel instead, so the lost-press check stays exact: a DoT applied by
// an auto-attack swing's proc keeps ticking as class DOT_TICK (the class forced at tick time loses
// the origin class); measured at 2bcc19760d, about 390 own-class hits per stock fight are such ticks.
inline constexpr std::int16_t PRESS_NONE   = -1;
inline constexpr std::int16_t PRESS_AUTO   = -2;  // the chain began at an AUTO swing (rl_resolve_cause)
inline constexpr std::int16_t PRESS_ORPHAN = -3;  // the chain began with no cause context at all

// Opens the output file (root sim only; refusals were already made by the caller, sim_t::setup)
// and writes the `hdr` record. Sets up the ledger state on the root sim.
void open_and_write_header( sim_t* sim );

// Fight begin (sim_t::reset() beside the roll recorder's call): resets the per-fight counters
// and starts the fight's record buffer with an `fb` record.
void fight_begin( sim_t* sim );

// Fight end (sim_t::combat_end() beside the roll recorder's call): appends the `fe` record
// (the RL actor's six realised and six expected credit streams, which include its pets) and
// flushes the fight's buffered records to disk.
void fight_end( sim_t* sim );

// Run end (sim_t::execute() beside the roll recorder's call): writes the `ftr` record carrying
// `"complete": true` and every counter the stage uses, then closes the file.
void write_footer( sim_t* sim );

// Assigns the next press number for a CAST-class cause that does not carry one yet, writes a
// `pr` record, and returns the number (stored by rl_cause_scope_t's constructor in the pushed
// frame's cause). A pet's CAST-class frame does NOT open a press: it adopts the latest press
// opened by a non-pet (a pet's own cast is credited to the current decision by the existing
// cause model, not a button choice of its own) and is counted as `foreign_press`.
std::int16_t open_press( player_t* p, const rl_cause_t& cause );

// One `hit` record per damage hit or tick of the RL actor and its pets, called from
// action_t::accrue_expected_damage. realised = state->result_amount (exactly what
// stats_t::add_result routes); `expected_amount` is the value accrue just computed, or 0 with
// `exp_excl` set for a Windfury-occurrence hit (priced separately, see the `xp` record).
void hit_sink( action_t* a, action_state_t* state, double expected_amount, bool exp_excl );

// A hit of an action that has no stats object routes neither realised nor expected credit; the
// footer counts them (`no_stats_hits`).
void note_no_stats_hit( action_t* a );

// While set, rl_credit_route's expected-side calls are accrue_expected_damage's own (already
// written as `hit` records) and are not written again as `xp`.
void set_in_hit_sink( sim_t* sim, bool on );

// Expected-only pricing outside hits (the Windfury occurrence price), called from
// rl_credit_route on every expected-side route not made by accrue_expected_damage.
void xp_record( player_t* p, const rl_cause_t& cause, double amount, const char* action_name );

// 261001-bac plan 03: hide-and-recompute for a direct hit whose state the engine snapshotted
// inside action_t::execute. Called from execute()'s per-target work after the real amount
// (`s->result_amount = calculate_direct_amount( s )`) and before schedule_travel( s ), only when
// the state was snapshotted there (no pre_execute_state). The real hit is NOT touched: every pass
// runs on a ledger-owned scratch copy inside a shadow scope (debug/log silenced, stat caches of the
// dealer, its owner and the target saved, invalidated before every pass and restored by
// assignment, benefit counters / post-snapshot callbacks / the parse-callback mask left as the
// real pass had them). Passes: a REFERENCE pass (nothing hidden; its snapshot fields and pre-crit
// amount must equal the real state's bit for bit), one pass per candidate buff hidden, every
// hidden subset for up to 3 effective candidates (the all-hidden pass above 3), then a RESTORING
// pass (nothing hidden; its snapshot fields and pre-crit amount must equal the real state's bit
// for bit). The result is parked in a side table under a fresh id written to state->rl_bl_hit and
// attached to the hit record by hit_sink.
//
// 261001-bac plan 05: with `pre` non-null the state is a PRE-MADE state (class code snapshotted it) that an
// AoE execute re-snapshotted per target: the passes re-run only the target part of the snapshot
// (snapshot_flags & STATE_TARGET, as execute() did), and the hit's entry names the entry written when the
// pre-made state was handed to schedule_execute (`pre->rl_bl_pm`) as its parent.
void run_passes( action_t* a, action_state_t* s, const action_state_t* pre = nullptr );

// 261001-bac plan 05: a state class code snapshotted itself (a full snapshot_internal at this very sim
// time, recorded in action_state_t::rl_bl_snap) is being handed to schedule_execute: run the passes now,
// before the buffs can change (Crash Lightning's bonus hit is snapshotted at the strike, "as the buff may
// be scheduled to expire on the same timestamp"). The result is an `app` record (src "premade") whose id
// is stamped on the state (rl_bl_pm); the hits made from the state name it in `par`.
void premade_snapshot( action_t* a, action_state_t* s );

// 261001-bac plan 05: the same for a tick action's application snapshot (`execute_state` of the tick
// action, snapshotted in the driver's execute); src "tick_action". The id is stamped on that state
// (rl_bl_pm) and copied by action_t::tick to every tick's state.
void tick_action_snapshot( action_t* tick_action, action_state_t* s );

// 261001-bac plan 05: execute() with a pre-made state in a SINGLE-target execute (no per-target
// re-snapshot): `s` is the real state (a copy of `pre`), after the real amount. Carries the entry id
// (rl_bl_pm) and compares the real pre-crit amount with the entry's reference amount bit for bit; a
// difference counts premade_drift and marks the hit status 6. A pre-made state with no entry counts
// premade_uncovered.
void premade_hit( action_t* a, action_state_t* s, const action_state_t* pre );

// 261001-bac plan 04: the same hide-and-recompute passes for one DIRECT tick of a damage-over-time
// effect (an action with no tick_action), called from action_t::tick after calculate_tick_amount and
// before assess_damage. `tick_multiplier` is the factor the engine passed to calculate_tick_amount
// (dot tick factor times stacks). Each pass re-runs update_state (the tick-time update flags only)
// on a scratch copy of the DoT's state; the reference pass must reproduce the real tick bit for bit.
// The result is parked under a fresh id written to d->state->rl_bl_hit, attached by hit_sink, and the
// tick's hit record names the application (`parent`, the `app` record the DoT's state carries).
void run_tick_passes( action_t* a, action_state_t* dot_state, double tick_multiplier );

// 261001-bac plan 04 shadow guards. Inside the ledger's own passes (sim->rl_bl_shadow) every function
// that changes a buff, a cooldown, a resource, a stat or a proc counter returns at once through one of
// these (the neutral return value is the argument): the call is counted under `guard` in the footer's
// `shadow_violation` and the pass set that is running is marked unsafe. Outside a pass nothing calls
// them.
void note_blocked( sim_t* sim, const char* guard );
template <typename T>
inline T blocked( sim_t* sim, const char* guard, T neutral )
{
  note_blocked( sim, guard );
  return neutral;
}
inline void blocked( sim_t* sim, const char* guard )
{
  note_blocked( sim, guard );
}

// 261001-bac plan 04: the one hook behind all six accessors that hand out a random generator
// (action_t, player_t, sim_t, buff_t, dbc_proc_callback_t, proc_rng_t). While the ledger's passes run
// (sim->rl_bl_shadow) it counts one violation `rng.draw.<family>` and returns the ledger's scratch
// generator, which the roll recorder never sees; otherwise it returns null and the accessor's own
// stream is used unchanged. `family` is one of "action", "player", "sim", "buff", "callback",
// "proc_rng".
rng::rng_t* rng_access( sim_t* sim, const char* family );
}  // namespace rl_buff_ledger
