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
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct sim_t;
struct player_t;
struct action_t;

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

// True exactly when the recorder is open (between open_and_write_header() and write_footer()).
// Cheap query for a call site that wants to skip building a key string entirely when recording
// is off.
bool active( const sim_t* sim );

} // namespace rl_rng_record
