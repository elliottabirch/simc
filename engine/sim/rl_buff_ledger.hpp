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

#include <cstddef>
#include <cstdint>

struct sim_t;
struct player_t;
struct action_t;
struct action_state_t;
struct buff_t;
struct dot_t;
struct dbc_proc_callback_t;
struct cooldown_t;

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

// Plan 13 (ruling R1): a CARRIED press. A class module that creates a delayed event inside a scope that has a press can capture
// that press (carry_capture) and hand it back around the code the event runs (carry_scope_t); while it is handed back, the LAST
// branch of action_t::rl_resolve_cause() (the one that stamps "no cause") writes the ENCODED NEGATIVE value -1000 - p instead of
// PRESS_ORPHAN, for a real press number p (0 .. PRESS_CARRY_MAX; a larger press is not carried and press_carry_overflow counts it).
// The value is negative on purpose: the engine reads it as "no press" everywhere (every `press >= 0` test keeps meaning a real
// press), so procs, buff applications and cooldown changes caused under it are treated exactly as under "no cause"; nothing the
// reward reads (decision number, cause class) changes, and rl_cause_t and action_state_t gain no field. Only the offline reference
// decodes it, and only for damage records (LEDGER-FORMAT.md amendment "plan 13: carried press").
inline constexpr std::int32_t PRESS_CARRY_BASE = 1000;
inline constexpr std::int32_t PRESS_CARRY_MAX  = 31767;
inline constexpr bool press_is_carried( std::int16_t v )
{
  return v <= -PRESS_CARRY_BASE && v >= -PRESS_CARRY_BASE - PRESS_CARRY_MAX;
}
inline constexpr std::int16_t press_carry_encode( std::int32_t press )  // 0 <= press <= PRESS_CARRY_MAX
{
  return static_cast<std::int16_t>( -PRESS_CARRY_BASE - press );
}
inline constexpr std::int32_t press_carry_decode( std::int16_t v )  // press_is_carried( v )
{
  return -PRESS_CARRY_BASE - v;
}
// The press value to compare when two causes are tested for sameness: a carried value stands for the "no cause" chain it came
// from (PRESS_ORPHAN), so applier lists and merged triggers group exactly as they did before the carry existed.
inline constexpr std::int16_t press_key( std::int16_t v )
{
  return press_is_carried( v ) ? PRESS_ORPHAN : v;
}

// Captures the press at the top of `p`'s cause stack for carry_scope_t: the encoded value, or PRESS_NONE (-1: nothing to carry)
// when the ledger is off, outside a fight, the stack is empty, or its top holds no real press (a sentinel or an already carried
// value). Call it in the code that creates the delayed event; with the ledger off it is one flag test and stores nothing.
std::int16_t carry_capture( const player_t* p );

// Hands a captured value back while it lives (nesting restores the outer value). A PRESS_NONE value, or the ledger off, is a no-op.
struct carry_scope_t
{
  sim_t* sim;
  std::int16_t prev;
  bool active;
  carry_scope_t( sim_t* sim_, std::int16_t carried );
  ~carry_scope_t();
  carry_scope_t( const carry_scope_t& )            = delete;
  carry_scope_t& operator=( const carry_scope_t& ) = delete;
};

// The press for a chain that begins with no cause context (the last branch of action_t::rl_resolve_cause()): PRESS_ORPHAN, or
// the handed-back carried value while a carry_scope_t lives. Only called with the ledger on.
std::int16_t orphan_press( sim_t* sim );

// Plan 12 (MJ-03): an action state's application id (`rl_bl_app`) is either 0 (none stamped), the `h` of an `app` record, or one of
// these two reserved values (no record id can take them), which say why no application record exists for that state. Every
// reader of the id treats a reserved value as "no parent".
inline constexpr std::uint64_t APP_ZERO_REF = ~std::uint64_t( 0 );      // the application's reference periodic amount was 0
inline constexpr std::uint64_t APP_PREMADE  = ~std::uint64_t( 0 ) - 1;  // a dot-applying hit executed from a pre-made state

// Plan 12 (NT-02): true from the moment a ledger file is open (one process-wide flag: the ledger refuses threads > 1, profilesets and
// non-root sims). The action state's four ledger-only fields are reset and copied only behind it; with the ledger off nothing writes them, so
// they keep their default member initialisers.
extern bool g_ledger_open;

// Opens the output file (root sim only; refusals were already made by the caller, sim_t::setup)
// and writes the `hdr` record. Sets up the ledger state on the root sim.
void open_and_write_header( sim_t* sim );

// Fight begin (sim_t::reset() beside the roll recorder's call): resets the per-fight counters
// and starts the fight's record buffer with an `fb` record.
void fight_begin( sim_t* sim );

// 261001-bac plan 07: sim_t::combat_begin() calls this right after reset() returns. Cooldown records (cycles, refunds,
// uses, cooldown starts) are written only from here on in a fight: the reset phase of an iteration resets cooldowns that
// still hold the previous iteration's state (a 364 s item cooldown found recharging), which is not part of the fight.
void reset_done( sim_t* sim );

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
// (rl_bl_pm) and copied by action_t::tick to every tick's state. `cause` is the cause of the driver's
// cast that is executing (the tick action's state itself is not stamped).
void tick_action_snapshot( action_t* tick_action, action_state_t* s, const rl_cause_t& cause );

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

// 261001-bac plan 05: the swing-speed channel. A speed buff changes how many swings happen, not how much a
// swing deals, so it is hidden at the moment a swing is SCHEDULED: for a repeating, non-special action (an
// auto-attack of the RL actor or its pets) schedule_execute calls this right after it computed
// time_to_execute. The reference pass (the read tap open) must reproduce time_to_execute bit for bit, one
// pass per candidate hides it (stat buffs have their stats removed too) and records the swing time without
// it, a restoring pass must reproduce it again. The result is an `ln` record of kind `swing` (launch id from
// the fight's launch counter; `sf` lists the candidates whose hiding changes the swing time, with
// f = hidden swing time / real swing time) and the launch id is kept in a per-action slot.
void run_swing_passes( action_t* a );

// 261001-bac plan 05: rl_resolve_cause's AUTO branch takes the slot's launch id (-1 if none) into the new
// cause and clears the slot: the swing's hit and everything it sets off carry it through the existing
// promote / state carriers.
std::int32_t take_swing_launch( const action_t* a );

// 261001-bac plan 05: attack_t::reschedule_auto_attack actually rebooked a swing because the auto-attack
// speed changed mid-swing (footer swing_rescaled). The swing keeps its launch's original factors.
void note_swing_rescaled( action_t* a );

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

// 261001-bac plan 12 (group 2, review MJ-04): event_t::operator new inside a pass (sim->rl_bl_shadow) takes its memory from here,
// never from the event manager's pool or recycle list (a recycled block handed out and never queued would reorder the list and
// change which block later events get). The blocks live until the ledger closes. event_t::schedule / reschedule / cancel inside
// a pass are counted no-ops (guards `event.create`, `event.reschedule`, `event.cancel`; see event.cpp, event_manager.cpp).
void* scratch_event_block( sim_t* sim, std::size_t size );

// 261001-bac plan 04: the one hook behind all six accessors that hand out a random generator
// (action_t, player_t, sim_t, buff_t, dbc_proc_callback_t, proc_rng_t). While the ledger's passes run
// (sim->rl_bl_shadow) it counts one violation `rng.draw.<family>` and returns the ledger's scratch
// generator, which the roll recorder never sees; otherwise it returns null and the accessor's own
// stream is used unchanged. `family` is one of "action", "player", "sim", "buff", "callback",
// "proc_rng".
rng::rng_t* rng_access( sim_t* sim, const char* family );

// ==========================================================================
// 261001-bac plan 06: frames, reads, draws, consumes, launches (the raw data of the gate check)
// ==========================================================================

// True while the read tap is open (reference pass) OR a ledger frame is open outside the ledger's own passes
// (buff.hpp, dot.hpp: one load per read). Kept in step by every place that changes one of its inputs.
extern bool g_reads_on;

// rl_cause_scope_t's constructor / destructor (rl_translog.cpp): one ledger frame per scope pushed for the RL
// actor or its pets outside the ledger's own passes. frame_push writes a `frm` record and returns the frame id
// (-1 when no frame was pushed); frame_pop pops that frame (and counts frame_pop_mismatch if it was not the top).
// `owner` is the action whose dispatch pushed the scope (or null); `ctx` names the action whose code runs inside a
// scope that has no owner; `kind` is a short name of the push site (null: "scope", or "dispatch" with an owner).
std::int32_t frame_push( player_t* p, const rl_cause_t& cause, const action_t* owner, const action_t* ctx,
                         const char* kind );
void frame_pop( player_t* p, std::int32_t frame );

// The innermost open frame (-1 when none).
std::int32_t innermost_frame( sim_t* sim );

// A child action `child` is being started from the frame the RL actor's cause stack is in (the topmost ledger
// frame of child->player), by execute_on_target (`kind` "execute_on_target"), by its own scope in execute()
// ("execute") or by schedule_execute with or without a state ("schedule"). When such a frame is open and its
// action differs from the child, writes an `ln` record (a `switch` launch when an engine proc switch is
// running, see switch_scope_t) and returns the new launch id, which the caller puts into the child's cause.
// Returns -1 when nothing was recorded.
std::int32_t note_launch( action_t* child, player_t* child_target, const char* kind );

// activate_with_buff / deactivate_with_buff register the generic proc switch (D-03's test-free gate); while a
// switched callback's execute runs (switch_scope_t, dbc_proc_callback.cpp), the next launch written carries the
// switch buff with its appliers.
void switch_register( const dbc_proc_callback_t* cb, buff_t* buff, bool on );
void switch_enter( const dbc_proc_callback_t* cb );
void switch_leave();
struct switch_scope_t
{
  explicit switch_scope_t( const dbc_proc_callback_t* cb ) { switch_enter( cb ); }
  ~switch_scope_t() { switch_leave(); }
  switch_scope_t( const switch_scope_t& )            = delete;
  switch_scope_t& operator=( const switch_scope_t& ) = delete;
};

// dot.hpp: a damage-over-time condition read (is_ticking, current_stack, remains), counted per fight.
void note_dot_read( const dot_t* d, bool non_zero );

// ==========================================================================
// 261001-bac plan 07: the refund ledger (cooldown recharge cycles, refunds, uses, cooldown starts)
// ==========================================================================
//
// Every cooldown of the RL actor and its pets keeps a list of recharge cycles. A cycle opens when a recharge starts
// (cooldown_t::start, or the next charge of a multi-charge cooldown starting to recharge) with its real length, and
// every change to the time it has left is an entry (`ref`) carrying the cause on top of the actor's cause stack at
// that moment; the cast that consumes the charge a cycle produced is a `use` naming the cycle (oldest charge first).
// A cast that starts a cooldown object is also written as `cdn` (so cooldown swaps, Storm Unleashed's second Crash
// Lightning cooldown, show up in the audit even though no cooldown function sees them).
//
// All of it reads cooldown fields and writes ledger records after the engine's own logic, behind one `sim->rl_bl_on`
// test at the call site; nothing here changes a cooldown, an event or a random number.

// What the ledger needs to see of a cooldown before a mutator runs (milliseconds, integers as the engine keeps them).
// Plan 12 (NT-02): trivially default-constructible (a default-constructed one is never read: cd_capture sets every field).
struct cd_snap_t
{
  std::int64_t rem_ms;  // time left in the cycle in progress (0: none); the recharge event's remains for charges > 1
  int cc;               // current_charge
  bool up;
  bool ev;  // a recharge event was running
};
cd_snap_t cd_capture( const cooldown_t* cd );

// cooldown_t::start(), after the engine's own logic, on the two paths that start something (a charge consumed while a
// recharge was already running, and the normal start). `before` was taken at the top of start().
void cd_started( cooldown_t* cd, const cd_snap_t& before, const action_t* a );

// cooldown_t::start() returned at once because the cooldown has no duration (nothing started): written as a `cdn`
// record with `ign` true, so a swap onto such a cooldown still shows which action asked for it.
void cd_start_ignored( cooldown_t* cd, const action_t* a );

// recharge_event_t::execute(), after the engine's own logic: a charge came back on its own.
void cd_recharged( cooldown_t* cd );

// RAII around adjust / reset / adjust_remaining_duration / adjust_base_duration (placed after the shadow guard). The
// constructor takes the snapshot, the destructor writes the net change as `ref` entries. A scope opened inside
// another (adjust -> reset) writes nothing: the outermost one owns the change and names itself as the source.
class cd_scope_t
{
public:
  cd_scope_t( cooldown_t* cd, const char* src, bool on )
  {
    if ( on )
      begin( cd, src );
  }
  ~cd_scope_t()
  {
    if ( begun_ )
      end();
  }
  cd_scope_t( const cd_scope_t& )            = delete;
  cd_scope_t& operator=( const cd_scope_t& ) = delete;

private:
  void begin( cooldown_t* cd, const char* src );
  void end();
  // Plan 12 (NT-02): only begun_ is set by the constructor; every other member is set in begin(), read only after it.
  bool begun_ = false;
  bool nested_;  // another scope of the same cooldown is open: this one writes nothing
  bool track_;   // the cooldown belongs to the RL actor or its pets and a fight is running
  int before_max_;  // plan 12 (MJ-05): the cooldown's maximum charges when the scope opened
  cooldown_t* cd_;
  const char* src_;
  cd_snap_t before_;
};
}  // namespace rl_buff_ledger
