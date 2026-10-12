// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// Random-roll recorder -- tstl-sylvanas phase 250, plan 250-01 (REC-01/02/03).
// rl_rng_record=<path>, off by default, refused by name unless per_source_rng=1 is also set
// (mirrors the per_source_rng_resalt_at refusal at sim.cpp). When set, one fixed-size entry is
// written for every random roll of a fight: which roller drew it, its raw number, the chance it
// was checked against (when it was a chance roll), the outcome, and the fight time as an
// informational field (never part of a roll's address -- D-01). Every roll is left exactly as
// it is today: this file and rl_rng_record.cpp are a pure observer -- no rng method call, no
// reset() of any stream, no event, no reordering, no dispatch change (D-17, mirroring
// rl_credit.hpp's own promise at its own top of file). Trigger/press fields (plan 250-03's
// territory) are written as zero throughout this plan -- the layout below already has room for
// them so it never has to change shape.
//
// NORMATIVE SOURCE: the header and record layouts below are this phase's planned contract
// (250-01-PLAN.md, "Recording format"), proved by the static_asserts at the bottom of the
// struct definitions exactly the way rl_translog.hpp proves its own layout -- a disagreeing
// layout cannot compile.
//
// Read with scripts/rl/probes/rng_record.py (plan 250-02, built in parallel with this plan).
//
// ==========================================================================================
// EVENT RECORDS (tstl-sylvanas phase 288.7, v218-event-times): a new record kind, KIND_EVENT
// (7), written into the SAME 80-byte record beside the rolls. A roll record says WHEN a random
// number was drawn; an event record says WHEN the simulation changed something a decision-time
// observation shows (an aura appeared, was refreshed or ran out, a hit reached its target, a
// periodic effect ticked, the player's speed changed). Every static_assert of the record layout
// below is kept; every field a roll writes keeps its meaning for a roll.
//
// Rules this code keeps (proved by 288.7-EVENT-RECORDER.md's parity receipts):
//   1. Writing an event never touches a random stream, never schedules or cancels an event, and
//      never changes simulation state: every value is read through a const path that records no
//      read for the buff ledger (remains_unnoted, check_unnoted, direct event pointers) and the
//      speed values are computed by calling the composite functions directly, never through
//      the stat cache (so no cache entry is filled earlier than it would have been).
//   2. An event is NOT a row of the roll accounting: it does not count toward the footer's total
//      (FOOTER position field) or any COUNTER, so a recording with every KIND_EVENT record
//      removed is byte-equal to the recording a build without this code would have written.
//      Events are also invisible to the three patch-the-last-roll functions (on_roll_outcome,
//      annotate_draw, label_last_roll): those patch the last ROLL record by its remembered
//      offset, not whatever sits at the tail of the buffer.
//   3. rl_rng_record_events=0 (default 1) turns the writer off entirely: the recording and its
//      sidecar are then byte-equal to a build without this code.
//   4. Scope: an event is written when its SOURCE is the controlled player (the one non-pet
//      player of the simulation) or one of its pets, on any unit, plus the controlled player's
//      own speed changes. Events are written only while a fight runs (after the reset phase,
//      before combat_end), so the resets at fight boundaries write none.
//
// Field map of a KIND_EVENT record (everything not listed is zero):
//   @16 time_ms        simulation time of the event, the same clock a roll carries
//   @24 draw_ordinal   REUSED: the event's 0-based ordinal within its fight
//   @32 roller         REUSED: ENTITY, the spell or aura the event is about.
//                      A spell id when the thing has one (buff: its spell data id; dot, hit and
//                      cooldown: the action's spell id). A thing with no spell id (an auto attack,
//                      an item buff) is 0x80000000 | its index in its owner's action list (for an
//                      action) or buff list (for a buff). The sidecar's events.entities names them.
//                      EVT_SPEED_CHANGED: 0.
//   @36 stream         REUSED: UNIT, where the event happens (the buff holder, the dot's or hit's
//                      target, the player for a speed change or a cooldown).
//   @48 position       REUSED: SOURCE, who caused it (the buff's source, the dot's or hit's actor).
//                      UNIT and SOURCE share one coding: (unit_kind << 24) | actor_index, with
//                      unit_kind 0 the controlled player, 1 a pet of it, 2 an enemy (boss, add,
//                      dummy), 3 any other actor, 4 no unit (a raid-wide aura).
//   @40 trigger, @44 press, @67 trigger_kind      the outer press at that moment, as on a roll
//   @52 inner_trigger, @56 inner_press, @68 inner_trigger_kind      the inner press, as on a roll
//   @66 kind           KIND_EVENT
//   @69 outcome        OUTCOME_NOT_A_ROLL
//   @70 label          REUSED: the event CLASS, one of EVT_* below
//   @72 reserved[0..3] value A (int32 or float32, per class)
//   @76 reserved[4..7] value B (int32 or float32, per class)
// Values per class (ms = whole milliseconds, int32; "none" = 0):
//   EVT_BUFF_APPLIED        A stacks after            B remaining ms (0 = no timed expiry)
//   EVT_BUFF_REFRESHED      A stacks after            B remaining ms after (also an extend or cut of the duration)
//   EVT_BUFF_EXPIRED        A stacks lost             B 0
//   EVT_STACKS_CHANGED      A stacks after            B stacks before (stacks consumed, buff stays up)
//   EVT_DOT_APPLIED         A remaining ms            B ms to the next tick
//   EVT_DOT_REFRESHED       A remaining ms after      B ms to the next tick after
//   EVT_DOT_EXPIRED         A ticks done              B 0
//   EVT_PERIODIC_TICK       A tick number (1-based)   B remaining ms after the tick
//   EVT_HIT_LANDED          A result (sc_enums result_e: 1 miss, 2 dodge, 3 parry, 4 glance, 5 crit, 6 hit)   B 0
//   EVT_SPEED_CHANGED       A float32 spell haste multiplier (time scale, below 1 = faster)
//                           B float32 auto-attack speed multiplier (swing interval scale)
//   EVT_COOLDOWN_STATE      A recharge time left in ms after the change (a one-charge cooldown: its time left; a
//                           charged one: the time to its next charge; 0 = nothing running)
//                           B cause (bits 0-3) | charges available (bits 4-7) | charges at most (bits 8-11, both clamped to 15) |
//                           the length of one full recharge in ms at the cooldown's own multiplier now (bits 12-31, clamped to 1048575),
//                           the number a "full recharge time" or "fractional charges" reading is computed from.
//                           cause 1 started (a press), 2 adjusted (a cut or an extension), 3 reset,
//                           4 rescaled (its rate or base duration changed), 5 recharged (a charge came back on its own)
//                           ENTITY = 0x80000000 + the cooldown's index in its owner's cooldown list (named in the
//                           sidecar, family "cooldown"); written only when the state (time left or charges) changed
//   EVT_SWING_DUE           A ms from now until the auto-attack's swing is due after this operation (0xFFFFFFFF = no swing
//                           pending); B hand (0 main, 1 off) | (operation << 8), operation 1 booked, 2 rescheduled, 3 canceled;
//                           ENTITY = the hand's action (the swing is due at TIME + A)
//   EVT_STAT_CHOSEN         A the chosen stat's engine id (stat_e, int32); B the amount the buff adds to that stat, int32: the
//                           engine's own number, the whole points update_player_buff_stat applies at one stack
//                           (copysign( trunc( max( 1, |amount| ) + stat_fp_epsilon ), amount )); ENTITY = the stat buff (as for the
//                           buff classes), UNIT the player holding it, SOURCE the buff's source. "A stat buff chose which stat it raises."
//                           One event per pick, written after the pick is made and before the buff is applied or refreshed (so before
//                           that buff's EVT_BUFF_APPLIED or EVT_BUFF_REFRESHED of the same millisecond), also when the pick is the stat
//                           that is already the raised one. WHICH CASE WRITES IT: the engine has no single funnel for "a stat buff
//                           changes which stat it carries" (stat_buff_t::update_player_buff_stat is the per-stat gain, run once per stat
//                           on every bump of every stat buff and knows of no choice), so the hook is the choosing item's own call,
//                           one generic recorder function: the item that picks one of its buff's stats at random on each proc
//                           (unique_gear_midnight.cpp, venomcursed_ascendance_cb_t::execute). A stat buff of another item that picks a
//                           stat would call the same function.
// Reading: scripts/rl/probes/rng_record.py (the repo's reader) yields these records with their
// kind; a reader that wants rolls only skips kind != KIND_ROLL.
// ==========================================================================================
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "util/timespan.hpp"

struct sim_t;
struct player_t;
struct action_t;
struct action_state_t;
struct raid_event_t;
struct proc_rng_t;
struct event_t;
struct buff_t;
struct dot_t;
struct cooldown_t;

namespace rng
{
struct rng_t;
}

namespace rl_rng_record
{

inline constexpr char MAGIC[ 4 ] = { 'R', 'L', 'R', 'R' };
inline constexpr std::uint32_t FORMAT_VERSION = 1u;
inline constexpr std::uint32_t RECORD_SIZE = 80u;
inline constexpr std::uint32_t HEADER_SIZE = 64u;

inline constexpr std::uint32_t KIND_ROLL = 1u;
inline constexpr std::uint32_t KIND_FIGHT_BEGIN = 2u;
inline constexpr std::uint32_t KIND_FIGHT_END = 3u;
inline constexpr std::uint32_t KIND_COUNTER = 4u;
inline constexpr std::uint32_t KIND_RESALT = 5u;
inline constexpr std::uint32_t KIND_FOOTER = 6u;
inline constexpr std::uint32_t KIND_EVENT = 7u;   // 288.7: see the EVENT RECORDS comment at the top of this file

// Event classes, written into the label byte of a KIND_EVENT record. Never 0 (LABEL_NONE).
inline constexpr std::uint8_t EVT_BUFF_APPLIED = 1u;
inline constexpr std::uint8_t EVT_BUFF_REFRESHED = 2u;
inline constexpr std::uint8_t EVT_BUFF_EXPIRED = 3u;
inline constexpr std::uint8_t EVT_STACKS_CHANGED = 4u;
inline constexpr std::uint8_t EVT_DOT_APPLIED = 5u;
inline constexpr std::uint8_t EVT_DOT_REFRESHED = 6u;
inline constexpr std::uint8_t EVT_DOT_EXPIRED = 7u;
inline constexpr std::uint8_t EVT_PERIODIC_TICK = 8u;
inline constexpr std::uint8_t EVT_HIT_LANDED = 9u;
inline constexpr std::uint8_t EVT_SPEED_CHANGED = 10u;
inline constexpr std::uint8_t EVT_COOLDOWN_STATE = 11u;
inline constexpr std::uint8_t EVT_SWING_DUE = 12u;
inline constexpr std::uint8_t EVT_STAT_CHOSEN = 13u;
inline constexpr std::uint8_t EVT_CLASS_MAX = 13u;

inline constexpr std::uint8_t CD_CAUSE_START = 1u;
inline constexpr std::uint8_t CD_CAUSE_ADJUST = 2u;
inline constexpr std::uint8_t CD_CAUSE_RESET = 3u;
inline constexpr std::uint8_t CD_CAUSE_RESCALE = 4u;
inline constexpr std::uint8_t CD_CAUSE_RECHARGE = 5u;
inline constexpr std::uint8_t SWING_OP_BOOK = 1u;
inline constexpr std::uint8_t SWING_OP_RESCHEDULE = 2u;
inline constexpr std::uint8_t SWING_OP_CANCEL = 3u;
inline constexpr std::uint32_t SWING_NONE = 0xFFFFFFFFu;

// Unit kinds (high byte of a UNIT / SOURCE field of an event record).
inline constexpr std::uint32_t UNIT_PLAYER = 0u;
inline constexpr std::uint32_t UNIT_PET = 1u;
inline constexpr std::uint32_t UNIT_ENEMY = 2u;
inline constexpr std::uint32_t UNIT_OTHER = 3u;
inline constexpr std::uint32_t UNIT_NONE = 4u;
inline constexpr std::uint32_t ENTITY_NO_SPELL_FLAG = 0x80000000u;

inline constexpr std::uint8_t TRIGGER_KIND_NONE = 0u;
inline constexpr std::uint8_t TRIGGER_KIND_EXECUTE = 1u;
inline constexpr std::uint8_t TRIGGER_KIND_TICK = 2u;
inline constexpr std::uint8_t TRIGGER_KIND_REFILL = 3u;

inline constexpr std::uint8_t OUTCOME_FAIL = 0u;
inline constexpr std::uint8_t OUTCOME_SUCCESS = 1u;
inline constexpr std::uint8_t OUTCOME_NOT_A_ROLL = 2u;

inline constexpr double CHANCE_NONE = -1.0;
inline constexpr std::uint16_t ITERATION_NONE = 0xFFFFu;
inline constexpr std::uint8_t LABEL_NONE = 0u;
inline constexpr std::uint8_t LABEL_SWING_TABLE = 255u;

inline constexpr std::uint8_t FLAG_AFTER_RESALT = 0x01u;
inline constexpr std::uint8_t FLAG_LOGICAL_ONLY = 0x02u;
inline constexpr std::uint8_t FLAG_ANNOTATED = 0x04u;

inline constexpr std::uint32_t ROLLER_SIM_SHARED = 0u;
inline constexpr std::uint32_t ROLLER_SIM_EXPLORE = 1u;

// Highest iteration/roller-registry value this format allows before the recorder refuses by
// name rather than wrapping (250-01-PLAN.md "on_draw"'s "refuses by name past 65534"). 0xFFFF
// (ITERATION_NONE) stays reserved for "before the first fight".
inline constexpr std::uint32_t MAX_ITERATION = 65534u;

// Roller classes, written into the <path>.rollers.json sidecar. `unregistered` is a stream that
// drew before any of the five per-source seeding sites (plan 250-01 Task 2) ever registered it,
// or a stream this plan never wires at all: it is numbered lazily, at its first draw, under a
// synthetic key.
enum class roller_class_e : std::uint8_t
{
  sim_shared = 0,
  sim_explore = 1,
  player = 2,
  action = 3,
  buff = 4,
  proc_callback = 5,
  proc_object = 6,
  raid_event = 7,
  unregistered = 8,
};

// One past the highest roller_class_e value -- sizes the fixed per_class array in
// replay_state_t (254-06 Task 2, REP-06 speed round 2, re-deriving 253-03's prepared-but-
// never-applied speed patch against the post-254-04 source): a std::array indexed by
// static_cast<uint8_t>(cls) replaces a std::map<roller_class_e, ...> on the on_draw() hot
// path. Bump this if roller_class_e ever grows.
inline constexpr std::size_t ROLLER_CLASS_COUNT = 9u;

// 64 bytes.
struct alignas( 8 ) file_header
{
  char magic[ 4 ];                // @0  -- MAGIC
  std::uint32_t format_version;   // @4  -- FORMAT_VERSION
  std::uint32_t record_size;      // @8  -- RECORD_SIZE
  std::uint32_t header_size;      // @12 -- HEADER_SIZE
  std::uint64_t sim_seed;         // @16
  // bit 0 per_source_rng, bit 1 deterministic, bit 2 rl_iteration_seeds set, bit 3 average_range
  std::uint32_t flags;            // @24
  std::uint32_t thread_index;     // @28
  std::uint32_t iterations;       // @32 -- iterations requested
  std::uint32_t zero36;           // @36
  std::uint8_t reserved[ 24 ];    // @40
};
static_assert( sizeof( file_header ) == HEADER_SIZE, "rl_rng_record::file_header must be exactly HEADER_SIZE bytes" );
static_assert( alignof( file_header ) == 8, "rl_rng_record::file_header must be 8-aligned" );
static_assert( offsetof( file_header, magic ) == 0 );
static_assert( offsetof( file_header, format_version ) == 4 );
static_assert( offsetof( file_header, record_size ) == 8 );
static_assert( offsetof( file_header, header_size ) == 12 );
static_assert( offsetof( file_header, sim_seed ) == 16 );
static_assert( offsetof( file_header, flags ) == 24 );
static_assert( offsetof( file_header, thread_index ) == 28 );
static_assert( offsetof( file_header, iterations ) == 32 );
static_assert( offsetof( file_header, zero36 ) == 36 );
static_assert( offsetof( file_header, reserved ) == 40 );

// 80 bytes. See 250-01-PLAN.md's "Recording format" for the full per-kind field contract; the
// short version lives on each field below. `trigger`/`press`/`inner_trigger`/`inner_press`/
// `inner_position`/`trigger_kind`/`inner_trigger_kind` are all zero throughout THIS plan --
// plan 250-03 is the one that starts writing them, with no layout change.
struct alignas( 8 ) record
{
  double raw;                      // @0  -- exactly what real() returned; 0 for non-ROLL kinds
  double chance;                   // @8  -- CHANCE_NONE unless this was a chance roll
  std::int64_t time_ms;            // @16 -- sim->current_time().total_millis(); informational only (D-01)
  std::uint64_t draw_ordinal;      // @24 -- the physical stream's rl_trace_n() after the draw
  std::uint32_t roller;            // @32 -- logical roller (override if active, else = stream)
  std::uint32_t stream;            // @36 -- the physical stream's own roller number
  std::uint32_t trigger;           // @40 -- outer press's triggering roller (250-03)
  std::uint32_t press;             // @44 -- outer press's 1-based press number (250-03)
  std::uint32_t position;          // @48 -- 0-based position within (trigger_kind, trigger, press, roller) (250-03)
  std::uint32_t inner_trigger;     // @52 -- inner (innermost executing) press's roller (250-03)
  std::uint32_t inner_press;       // @56 -- inner press's press number (250-03)
  std::uint32_t inner_position;    // @60 -- position within the inner window (250-03)
  std::uint16_t iteration;         // @64 -- ITERATION_NONE before the first fight
  std::uint8_t kind;               // @66 -- one of KIND_*; never 0
  std::uint8_t trigger_kind;       // @67 -- one of TRIGGER_KIND_* (250-03)
  std::uint8_t inner_trigger_kind; // @68 -- one of TRIGGER_KIND_* (250-03)
  std::uint8_t outcome;            // @69 -- one of OUTCOME_*
  std::uint8_t label;              // @70 -- LABEL_NONE, an rl_proc::id + 1, or LABEL_SWING_TABLE
  std::uint8_t flags;              // @71 -- FLAG_* bits
  std::uint8_t reserved[ 8 ];      // @72
};
static_assert( sizeof( record ) == RECORD_SIZE, "rl_rng_record::record must be exactly RECORD_SIZE bytes" );
static_assert( alignof( record ) == 8, "rl_rng_record::record must be 8-aligned" );
static_assert( offsetof( record, raw ) == 0 );
static_assert( offsetof( record, chance ) == 8 );
static_assert( offsetof( record, time_ms ) == 16 );
static_assert( offsetof( record, draw_ordinal ) == 24 );
static_assert( offsetof( record, roller ) == 32 );
static_assert( offsetof( record, stream ) == 36 );
static_assert( offsetof( record, trigger ) == 40 );
static_assert( offsetof( record, press ) == 44 );
static_assert( offsetof( record, position ) == 48 );
static_assert( offsetof( record, inner_trigger ) == 52 );
static_assert( offsetof( record, inner_press ) == 56 );
static_assert( offsetof( record, inner_position ) == 60 );
static_assert( offsetof( record, iteration ) == 64 );
static_assert( offsetof( record, kind ) == 66 );
static_assert( offsetof( record, trigger_kind ) == 67 );
static_assert( offsetof( record, inner_trigger_kind ) == 68 );
static_assert( offsetof( record, outcome ) == 69 );
static_assert( offsetof( record, label ) == 70 );
static_assert( offsetof( record, flags ) == 71 );
static_assert( offsetof( record, reserved ) == 72 );

// A logical press frame: the outermost or innermost active press at the moment a roll happens.
// Unused until plan 250-03 wires the press guards at the call sites -- declared now so the
// record layout above never has to change shape when that plan starts filling these fields in.
struct press_frame_t
{
  std::uint8_t kind = TRIGGER_KIND_NONE;
  std::uint32_t trigger = 0;
  std::uint32_t press = 0;
};

// A plain-value pair of outer/inner press frames, captured at deferred-dispatch CREATION time by
// snapshot_press() below and restored at the deferred point by rl_press_scope_t's matching
// constructor -- for a deferred boundary with no action_state_t to carry a stamp on (unlike
// stamp_state()/restore_from_state(), which stamp/restore via a state object). Default-constructed
// (both frames TRIGGER_KIND_NONE) is the "never captured" sentinel, restoring as a no-op (D-07).
struct press_snapshot_t
{
  press_frame_t outer{};
  press_frame_t inner{};
};

// ---- Press-tag interface (plan 250-03, REC-04). Mirrors sim/rl_credit.hpp's rl_cause_scope_t
// exactly: an RAII guard that saves the recorder's current outer/inner frames and restores them
// in its destructor, giving correct LIFO nesting from the call stack itself -- no explicit stack
// container. Every constructor and the destructor are a single pointer check (root sim's
// rl_rng_recorder) when the recorder is off: no allocation, no rng call, no event, no reorder
// (D-17). Unlike rl_cause_scope_t (per-player, always on), this guard is recorder-owned and
// process-wide -- correct because the recorder forces threads=1 (R-08) whenever it is active, so
// there is exactly one call stack to track at a time. ----

// A guard opened at one of the three execute() dispatch entry points (or, as a fallback, inside
// action_t::execute() itself when none of them covered this call -- see action.cpp's own
// comments at each site) or at a DoT tick's own dispatch (dot.cpp). Two open modes and one
// restore mode; constructed via the matching factory-style constructor below.
struct rl_press_scope_t
{
  enum class mode_e : std::uint8_t
  {
    open_execute,  // bumps action->rl_press_count_, opens kind=execute
    open_tick,     // bumps action->rl_tick_count_, opens kind=tick
  };

  // Open a new execute or tick press for `action`. `carried_state` (open_execute only; always
  // null for open_tick -- ticks have no equivalent carried-state concept) is consulted so a
  // deferred dispatch that already carries a stamped outer frame (a proc's schedule_execute(),
  // a tick_action's schedule_execute( tick_state )) keeps that outer frame rather than opening a
  // fresh one -- see rl_rng_record.cpp's open_execute() for the exact three-way branch (carried
  // stamp / already-open outer / brand new outer).
  rl_press_scope_t( sim_t* sim, action_t* action, mode_e mode, const action_state_t* carried_state = nullptr );

  // Restore the outer/inner frames from a stamped action_state_t (a travel/proc/tick boundary
  // that carries its own press stamp via stamp_state() below). A state that was never stamped
  // (rl_press_outer_kind == TRIGGER_KIND_NONE) leaves the current frames untouched -- see D-07.
  rl_press_scope_t( sim_t* sim, const action_state_t* state );

  // Restore the outer/inner frames from a plain-value snapshot captured at deferred-dispatch
  // creation time via snapshot_press() below -- for a deferred boundary that has no
  // action_state_t to carry a stamp on (buff_t's own zero-delay "on landed" proc dispatch,
  // buff.cpp, scheduled to "ensure buff is fully processed" the same way proc_event_t detaches
  // proc execution from proc triggering). A never-captured snapshot (kind TRIGGER_KIND_NONE, the
  // struct's own default) leaves the current frames untouched -- same D-07 rule as the
  // action_state_t overload above.
  rl_press_scope_t( sim_t* sim, const press_snapshot_t& snapshot );

  ~rl_press_scope_t();

  rl_press_scope_t( const rl_press_scope_t& ) = delete;
  rl_press_scope_t& operator=( const rl_press_scope_t& ) = delete;
  rl_press_scope_t( rl_press_scope_t&& ) = delete;
  rl_press_scope_t& operator=( rl_press_scope_t&& ) = delete;

private:
  sim_t* sim_;
  bool active_ = false;
  press_frame_t saved_outer_{};
  press_frame_t saved_inner_{};
  const action_t* saved_inner_owner_ = nullptr;
};

// A guard opened around ONE shared-stream draw a raid event makes (plan 250-03, REC-05/R-01).
// The draw itself stays on the shared stream (sim->rng()) -- this guard only sets that stream's
// existing rl_roller_override_ (util/rng.hpp, wired for exactly this by plan 250-01) to the
// event's own registered roller number for the single draw in its scope, and restores whatever
// override was active before on destruction (LIFO, matching every other guard in this file). Open
// ONE scope per draw, never a whole function -- adds_event_t::_start() (raid_event.cpp) summons
// pets BETWEEN its draws, and those summons must never carry the raid event's roller. No-op (one
// pointer check) when the recorder is off.
struct rl_raid_draw_scope_t
{
  rl_raid_draw_scope_t( sim_t* sim, const raid_event_t& event );
  ~rl_raid_draw_scope_t();

  rl_raid_draw_scope_t( const rl_raid_draw_scope_t& ) = delete;
  rl_raid_draw_scope_t& operator=( const rl_raid_draw_scope_t& ) = delete;
  rl_raid_draw_scope_t( rl_raid_draw_scope_t&& ) = delete;
  rl_raid_draw_scope_t& operator=( rl_raid_draw_scope_t&& ) = delete;

private:
  sim_t* sim_;
  bool active_ = false;
  std::uint32_t saved_override_ = 0;
};

// A guard opened around a deck's OWN reset() call, at the two call sites only (never inside
// shuffled_rng_t::reset() itself -- dre_deck_rng_t overrides it, sc_shaman.cpp): when `deck` is
// non-null, the recorder is on and deck->type() is RNG_SHUFFLE, tallies one refill for this deck
// this fight and sets BOTH outer and inner to (refill, the deck's own roller number, the tally)
// for its lifetime -- a refill wins over any enclosing press (REC-06, D-08, R-04), unconditionally,
// not merged with whatever was active before. Restores the prior outer/inner/inner-owner on
// destruction, same LIFO shape as every other guard here. No-op for any other proc_rng_t subtype
// (a plain fight-start reset of a non-deck source_rng_ draws no refill entry) or an unregistered
// deck (per_source_rng off).
struct rl_refill_scope_t
{
  rl_refill_scope_t( sim_t* sim, proc_rng_t* deck );
  ~rl_refill_scope_t();

  rl_refill_scope_t( const rl_refill_scope_t& ) = delete;
  rl_refill_scope_t& operator=( const rl_refill_scope_t& ) = delete;
  rl_refill_scope_t( rl_refill_scope_t&& ) = delete;
  rl_refill_scope_t& operator=( rl_refill_scope_t&& ) = delete;

private:
  sim_t* sim_;
  bool active_ = false;
  press_frame_t saved_outer_{};
  press_frame_t saved_inner_{};
  const action_t* saved_inner_owner_ = nullptr;
};

// Copies the recorder's CURRENT outer/inner press frames into `state` -- called beside a state's
// existing rl_cause_seq stamp (action.cpp's per-target work, action_t::tick()'s tick_state stamp,
// dot.cpp's open-tick call sites) so a later restore (a travel/proc/tick boundary, above) can
// recover the press that was active when this state was created. No-op when the recorder is off
// or `state` is null.
void stamp_state( sim_t* sim, action_state_t* state );

// True exactly when the recorder is on and its current inner-frame owner is `action` -- meaning
// one of the three execute() dispatch entry points already opened this action's own execute
// press before calling execute() (A-04). action_t::execute()'s early-return branch and its
// direct-call fallback guard both consult this so the SAME dispatch is never counted twice.
bool inner_owner_is( sim_t* sim, const action_t* action );

// Tallies one early return against `action`'s sidecar row (250-03, A-04): an entry-point
// dispatch already opened (and counted) this action's own execute press, but action_t::execute()
// took its early return before any per-target work ran, so the press produced no report.json
// execute. Diagnostic context only -- never subtracted in rng_record.py's compare_report(), only
// printed alongside an UNEXPLAINED verdict. No-op when the recorder is off.
void note_early_return( sim_t* sim, const action_t* action );

// Captures the recorder's CURRENT outer/inner press frames as a plain value with no
// action_state_t attached -- for a deferred dispatch that has nothing to stamp a state onto
// (buff_t's zero-delay "on landed" proc dispatch, buff.cpp). Returns a default-constructed
// (TRIGGER_KIND_NONE) snapshot when the recorder is off; a later restore from that default is a
// correct no-op, matching stamp_state()/restore_from_state()'s own "never stamped" convention.
press_snapshot_t snapshot_press( sim_t* sim );

// Annotates the LAST ROLL entry written for `stream` as a swing-table draw (R-09): writes
// chance/outcome/label and sets FLAG_ANNOTATED, but ONLY when exactly one draw happened on
// `stream` since `before` (a one-result attack table draws nothing and stays un-annotated) and
// that entry's own (stream, draw_ordinal) still matches -- mirrors on_roll_outcome()'s own
// find-the-entry-just-written contract. No-op when the recorder is off.
void annotate_draw( sim_t* sim, rng::rng_t& stream, std::uint64_t before, double chance, bool outcome,
                     std::uint8_t label );

// Labels the LAST ROLL entry written (any roller/stream) with `label` (R-09) -- ONLY when that
// entry's own label is still LABEL_NONE and its chance/outcome exactly match chance/success (an
// rl_count_proc call this roll's draw did NOT itself produce leaves the entry untouched). Tallies
// one matched or unmatched call against `label` in the sidecar's labelCalls regardless. No-op
// when the recorder is off (the tally is per-recording, so nothing to tally either).
void label_last_roll( sim_t* sim, std::uint8_t label, double chance, bool success );

// ---- Writer interface. Every function is a no-op (one pointer check) when rl_rng_record= is unset. ----

// Root only -- called once, from sim_t::setup(), after every named refusal has already passed
// (per_source_rng, threads, profilesets, non-root sim -- all before this call). Opens the
// stream, writes the header, installs the process-wide draw sink (rng::rl_draw_sink) and
// registers roller 0 (sim->rng(), key "sim|_rng") and roller 1 (sim->solver_explore_rng, key
// "sim|solver_explore_rng").
void open_and_write_header( sim_t* sim );

// Called from sim_t::reset(), once per fight, right after the shared-stream reseed/reset block
// and before event_mgr.reset(). Writes a FIGHT_BEGIN entry, clears this fight's per-window
// position maps and tallies, clears the after-re-salt flag, and records every already-known
// roller's current draw counter as this fight's begin ordinal.
void fight_begin( sim_t* sim );

// Called from sim_t::combat_end(), right after rl_translog::record_close(). Writes one COUNTER
// entry per roller with any activity this fight (begin/end ordinal, execute/tick/refill/
// no-draw-roll/roll-entry tallies), then a FIGHT_END entry, flushes the buffered rows for this
// fight to disk in one write and checks the stream is still healthy.
void fight_end( sim_t* sim );

// Called from sim_t::execute(), immediately after rl_translog::write_footer(), inside the
// existing `if ( success )` guard. Writes the FOOTER entry (counted in its own total), flushes,
// closes the stream, writes the <path>.rollers.json sidecar, prints the "wrote N roll entries
// over F fights to <path>" line to stderr, clears the process-wide draw sink and destroys the
// recorder object (owned by sim_t::rl_rng_recorder).
void write_footer( sim_t* sim );

// Called from sim_t::resalt_source_rngs(), right after its `if ( !per_source_rng ) return;`
// guard, before any stream is reseeded. Writes a RESALT entry and sets the after-re-salt flag
// every later ROLL entry this fight carries (FLAG_AFTER_RESALT). Changes nothing the re-salt
// itself does (R-01) -- no stream is touched here, only an entry written and a flag set.
void mark_resalt( sim_t* sim );

// Registers (or re-registers) `stream` under `key` with class `cls`. Keeps an existing roller
// number for `key` if one was already assigned (for example by an earlier draw numbering it
// "unregistered"), replacing its key/class in that case; otherwise assigns the next number.
// Stores a pointer to `stream`'s own draw counter (rng::rng_t::rl_draw_counter()) so later
// fight-begin/fight-end/COUNTER work never has to call an rng method to read it. Called from
// the five per-source seeding sites (plan 250-01 Task 2) with the SAME key string each site
// already builds for rng::per_source_seed() -- the seed itself never changes.
void register_roller( sim_t* sim, rng::rng_t& stream, std::string_view key, roller_class_e cls );

// Same as register_roller, for an action's own per-source stream -- also remembers the owning
// action_t* so the <path>.rollers.json sidecar can report actor/actorKind/statsName/dual/
// harmful for it (plan 250-01 Task 2).
void register_action_roller( sim_t* sim, rng::rng_t& stream, std::string_view key, const action_t* action );

// Registers `event` under its own roller number (plan 250-03, REC-05/R-01) -- key
// "sim|raid_event|INTERNAL_ID|TYPE|NAME", class raid_event, no stream of its own (every one of
// its draws stays on the shared stream, tagged only via rl_raid_draw_scope_t's override). Keeps
// `event`'s existing number if it already has one (both raid_event_t::reset() overrides call the
// base reset() first, so a call from an override and the base's own call for the SAME event are
// idempotent -- second call is a no-op read of event.rl_roller_id). Called from the base
// raid_event_t::reset(), covering every raid event including nested ones.
void register_raid_event( sim_t* sim, raid_event_t& event );

// True exactly when the recorder is open (between open_and_write_header() and write_footer()).
// Cheap query for a call site that wants to skip building a key string entirely when recording
// is off.
bool active( const sim_t* sim );

// ---- Event writer (288.7). Every function below is a no-op unless events are live: the recorder
// is writing, rl_rng_record_events is on, the fight is running (between events_begin() and
// events_end()) and the simulation has exactly one non-pet player. Call sites guard with
// events_on() so the arguments of a disabled hook are never computed. ----

// True exactly while events are being written (one process-wide flag: the recorder forces
// threads=1, so there is one writer). Defined in rl_rng_record.cpp.
extern bool g_events_on;
inline bool events_on()
{
  return g_events_on;
}

// Called from sim_t::combat_begin() right after reset() (the reset phase is not part of a fight)
// and from the top of sim_t::combat_end(). No-ops when the recorder is off.
void events_begin( sim_t* sim );
void events_end( sim_t* sim );

// A buff event. `prior_stacks` is the stack count before the change (used by EVT_BUFF_EXPIRED
// for the stacks lost and by EVT_STACKS_CHANGED for the count before); ignored by the others.
void event_buff( sim_t* sim, std::uint8_t cls, const buff_t* b, int prior_stacks );

// A stat buff chose which stat it raises (EVT_STAT_CHOSEN): `stat` is the stat_e value, `amount` the amount the buff gives that stat.
void event_stat_chosen( sim_t* sim, const buff_t* b, int stat, double amount );

// A damage-over-time event for `d` (APPLIED, REFRESHED, EXPIRED, TICK).
void event_dot( sim_t* sim, std::uint8_t cls, const dot_t* d );

// A hit reaching its target (called at the two impact dispatch points, before impact() runs).
void event_hit( sim_t* sim, const action_t* a, const action_state_t* s );

// The controlled player's speed (called from player_t::adjust_dynamic_cooldowns()); written only
// when the pair of multipliers differs from the last one written this fight.
void event_speed( sim_t* sim, const player_t* p );

// A guard opened at the top of each function that changes a cooldown (start, adjust, reset, adjust_remaining_duration): when it closes it
// writes EVT_COOLDOWN_STATE if the cooldown's state (time left or charges) changed. One check of events_on() when events are off.
struct rl_cooldown_scope_t
{
  rl_cooldown_scope_t( sim_t* sim, const cooldown_t* cd, std::uint8_t cause );
  ~rl_cooldown_scope_t();

  rl_cooldown_scope_t( const rl_cooldown_scope_t& ) = delete;
  rl_cooldown_scope_t& operator=( const rl_cooldown_scope_t& ) = delete;
  rl_cooldown_scope_t( rl_cooldown_scope_t&& ) = delete;
  rl_cooldown_scope_t& operator=( rl_cooldown_scope_t&& ) = delete;

private:
  sim_t* sim_;
  const cooldown_t* cd_;
  std::uint8_t cause_;
  bool active_ = false;
  std::uint32_t before_remaining_ = 0;
  std::uint32_t before_charges_ = 0;
};

// The controlled player's auto-attack swing timer: after an action's execute event is booked (`e` is the new event), after an event is
// rescheduled or before it is canceled (`event` names it). Written only when the action or event is the controlled player's main-hand or
// off-hand attack at that moment.
void event_swing_booked( sim_t* sim, const action_t* a, const event_t* e );
void event_swing_event( sim_t* sim, const event_t* e, std::uint8_t op );

// ---- Swing trace / swing pin (tstl-sylvanas phase 257, plan 257-09, D-18, owner ruling A-125 --
// supersedes plan 257-07's own v1 pin-list design, A-124's finding: simc moves a swing LAZILY
// (event_t::reschedule only records a new due time; occurs() shows it, event.hpp:75), so a
// booked "final landing time" is provisional, not the time the engine actually shows at any
// earlier instant -- the v1 list-of-final-times pin therefore woke fight B 20-130ms away from
// fight A. D-18 replaces it with a per-hand OPERATION TRACE (every add/reschedule/cancel/fire,
// with the due time the engine showed right after it) and a pin that replays that trace AS OF
// each of fight B's own operations, not as a list of final times. Study-only; refused on the
// scoring path (simc_channel.py). Every function below is a no-op / passthrough / false (one
// pointer check on root->rl_rng_recorder, then a player-identity check) unless
// sim_t::rl_swing_trace_file_str or sim_t::rl_swing_pin_file_str is set AND the
// exactly-one-player check at fight begin passed. ----

// The trace file's own row kinds (M-110's format, "How D-18 is built" section 2) -- shared
// between sim.cpp's setup()-time parser (into sim_t::rl_swing_pin_kind's parallel array, which
// cannot itself be a vector<char> of these named constants only because sim.hpp must not
// include this header) and every funnel below. SWING_ROW_INJECT never appears in a PARSED pin
// trace (M-110's read_trace, allow_inject=false for every fight-A/pin-input trace); it is
// written only into a PINNED fight's OWN trace file by the funnels below.
inline constexpr char SWING_ROW_BOOK = 0;
inline constexpr char SWING_ROW_REBOOK = 1;
inline constexpr char SWING_ROW_RESTART = 2;
inline constexpr char SWING_ROW_RESCHED = 3;
inline constexpr char SWING_ROW_LAND = 4;
inline constexpr char SWING_ROW_DROP = 5;
inline constexpr char SWING_ROW_CANCEL = 6;
inline constexpr char SWING_ROW_INJECT = 7;
inline constexpr char SWING_HAND_MH = 0;
inline constexpr char SWING_HAND_OH = 1;

// Study-only pin-trace parser (M-110, D-18 (a)). The SAME text format `rl_rng_record::
// write_swing_trace_file` (rl_rng_record.cpp, called from fight_end()) produces -- an
// INDEPENDENT C++ reader, never a call into Python. Applies read_trace's own syntax rules with
// allow_inject=false always (a pin/fight-A input trace never carries an inject row -- one is
// refused by name, which is also how a v1 three-line pin list fails, at its header). Throws
// std::runtime_error with a short reason fragment on any malformed line; the caller (sim_t::
// setup()) wraps it in the fork's own named refusal message. Returns the header's own actor
// name; appends one entry per parsed row to the four parallel arrays (never clearing them
// first -- the caller passes fresh, empty vectors).
std::string parse_swing_trace_file( const std::vector<std::string>& lines,
                                     std::vector<char>& hand, std::vector<std::int64_t>& op_ms,
                                     std::vector<char>& kind, std::vector<std::int64_t>& due_ms );

// Called from action_t::start_action_execute_event(), BEFORE make_event() constructs the new
// execute event -- for EVERY action, not only auto-attacks; the player-identity check inside
// makes this a cheap passthrough for anything that is not currently the one designated player's
// main_hand_attack/off_hand_attack. Returns the delta to book: `t` unchanged when the trace/pin
// is inactive for this action; when rl_swing_trace is active, `t` unchanged (the trace only
// OBSERVES this booking -- a book/rebook/restart row, per D-18 (a)); when rl_swing_pin is ALSO
// active, the pin's own D-18 (b)/(c) value in place of `t` (never touching WHICH event
// operation happens, only its time argument, per M-111 (d)'s inherited invariant).
timespan_t swing_event_created( sim_t* sim, action_t* action, timespan_t t );

// Called from the top of event_t::reschedule(), BEFORE _sim.event_mgr.global_event_id is
// consumed for this reschedule -- a passthrough for anything that is not currently one of the
// designated player's own execute_event pointers (identified by comparing `e` itself, a cheap
// pointer-value check, against player->main_hand_attack->execute_event / off_hand_attack->
// execute_event AT THE MOMENT OF THE CALL -- covers Windlash after Ascendance's hand-over with
// no separate logic, exactly like the v1 pin's own action-identity check, one level indirect).
// Returns the delta_time to use in place of the caller's own.
timespan_t swing_event_rescheduled( sim_t* sim, event_t* e, timespan_t delta_time );

// Called from the top of event_t::cancel( event_t*& ), for a non-null, not-yet-canceled event
// -- BEFORE `e->canceled = true` -- a no-op for anything that is not currently one of the
// designated player's own execute_event pointers (same identity check as above). Writes a
// `cancel` row (trace-active) / advances the pin's own per-hand state (pin-active); no return
// value -- the cancel itself is never overridden, only observed.
void swing_event_canceled( sim_t* sim, event_t* e );

// Called from action_execute_event_t::execute(), immediately AFTER `action->execute_event =
// nullptr;` -- a no-op unless `action` is currently a designated hand. The event has come due;
// whether it becomes a `land` row (action_t::execute() runs and calls swing_attack_executed
// below) or a `drop` row (the dead-target/channel-pause branches leave the hand idle or re-book
// it without ever reaching action_t::execute()) is not yet known here, so this funnel only
// marks the hand PENDING at this millisecond -- the next funnel call for this hand (whichever
// comes first: swing_attack_executed's own land, or a later swing_event_created's own book/
// rebook/restart) resolves the pending row as `land` or `drop` respectively before writing its
// own row, per "How D-18 is built" section 1 (iv)/(v).
void swing_event_fired( sim_t* sim, action_t* action );

// Called from action_t::execute(), immediately BEFORE its own repeating self-rebook tail call
// (`if ( repeating && !proc ) schedule_execute();`) -- a no-op unless `action` is currently a
// designated hand. An in-place repeat (action->repeating == false -- Skyfury's own generic::
// skyfury sets it false, calls execute(), then restores it true, A-123) is a proc-driven extra
// attack, never a swing-clock operation: tallies one repeat and returns BEFORE resolving any
// pending firing. Otherwise resolves this hand's pending firing (set by swing_event_fired
// above) as a `land` row, and tallies mid_cast when the player had a cast or channel in
// progress at this exact moment (M-111 (e)). Must run BEFORE the self-rebook, exactly like the
// v1 pin's own site, or that immediate re-book would see the pending firing still unresolved.
void swing_attack_executed( sim_t* sim, action_t* action );

} // namespace rl_rng_record
