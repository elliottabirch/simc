// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// Flight recorder for the in-process RL transport -- tstl-sylvanas phase
// 212, plan 212-01 (TLOG-01/02/03). One binary file per process carrying a
// fixed-size row for every decision the in-process policy makes, a row for
// every fight's close-out built from that fight's own live damage counter
// (never from report.json -- D-07), and a summary row at the end proving
// the run reached the end (D-15).
//
// NORMATIVE SOURCE: the row and header layouts below are ruling 212-G1 --
// 212-RESEARCH.md M-1's compiled, asserted ordering, verbatim. This is a
// frozen version-1 layout: the reader refuses on an exact version mismatch,
// so any later field addition or reordering is a version bump that orphans
// every file written under version 1. There is no layout generator and none
// is being built (D-05 dropped under N-8, MVP replan
// .planning/notes/v29-mvp-replan-2026-08-25.md) -- both sides of this
// layout are hand-written ONCE and proved by the compile-time assertions at
// the bottom of this file (D-20): a disagreeing layout cannot compile.
//
// ONE 48-byte row shape carries all three kinds (decision, fight-close,
// footer) and the `kind` byte sits at the same offset (46) in every one of
// them (D-02), so a reader can classify a row before it interprets it.

#pragma once

#include "sim/rl_policy_constants.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

struct sim_t;
struct player_t;

namespace rl_translog
{

// ---- File identity ----

// The four characters 'R','L','T','L' -- not a NUL-terminated string, so no
// trailing NUL is part of the magic itself.
inline constexpr char MAGIC[ 4 ] = { 'R', 'L', 'T', 'L' };
inline constexpr std::uint32_t ENDIAN_CANARY = 0x01020304u;
inline constexpr std::uint32_t FORMAT_VERSION = 1u;
inline constexpr std::uint32_t RECORD_SIZE = 48u;
inline constexpr std::uint32_t HEADER_SIZE = 256u;

// Row kind discriminator (offset 46 in every row struct below, D-02). Zero
// is deliberately not a valid kind -- a zero-filled region left by a short
// write (a crashed/truncated file, D-14) must never classify as a valid
// row; it must classify as "not a row at all".
inline constexpr std::uint8_t KIND_DECISION = 1;
inline constexpr std::uint8_t KIND_CLOSE = 2;
inline constexpr std::uint8_t KIND_FOOTER = 3;

// Footer source discriminator (D-07/D-15). The only value this phase ever
// writes -- the footer's per-fight numbers come from the writer's own
// running sum of close-row damage, never from report.json.
inline constexpr std::uint32_t FOOTER_SOURCE_PER_FIGHT_ACCUMULATOR = 1u;

// Decision-row flag bits (std::uint8_t). Bits 0, 1 and 3 are frozen in the
// layout and written ZERO by this phase:
//  - bit 0 (FLAG_TERMINATED) and bit 1 (FLAG_TRUNCATED) distinguish a fight
//    that ended by target death from one that ended by the clock. That is
//    how a trainer defines a fight's end, and that definition belongs to
//    Phase 213, not this plan.
//  - bit 3 (FLAG_EXPLORATORY) marks a random action taken instead of the
//    best one. It has no writer until Phase 213 lands the dice roll in
//    C++ -- Phase 210 currently REFUSES to load a weights blob that asks
//    for any randomness at all.
// Reserving the bit positions now costs nothing; retrofitting one later is
// a version bump.
inline constexpr std::uint8_t FLAG_TERMINATED = 1u << 0;
inline constexpr std::uint8_t FLAG_TRUNCATED = 1u << 1;
inline constexpr std::uint8_t FLAG_WAIT_FLOORED = 1u << 2;
inline constexpr std::uint8_t FLAG_EXPLORATORY = 1u << 3;
inline constexpr std::uint8_t FLAG_COLLECTED = 1u << 4;

// ---- Row layouts (212-RESEARCH M-1, ruling 212-G1, verbatim) ----
//
// The layout closes under NATURAL alignment -- measured by the researcher:
// record sizeof=48/alignof=8, header sizeof=256/alignof=8. No struct-
// packing pragma is used anywhere in this file: under byte packing the
// assertions below stop being a test of anything (a misaligned float64
// load becomes legal and the compiler is no longer proving the offsets the
// assertions claim), and packing would hide exactly the field-ordering
// mistake those assertions exist to catch.

struct decision_record
{
  double damage;                    // @0  -- p->solver_damage_so_far at this boundary
  float t;                          // @8  -- sim->current_time().total_seconds()
  float obs[ RL_OBS_DIM ];          // @12 -- already float32, copied straight across
  float q_margin;                   // @32 -- best minus second-best legal Q, or quiet NaN
  std::uint32_t seq;                // @36 -- sim->solver_control_seq at this boundary
  std::uint16_t iteration;          // @40 -- sim->current_iteration
  std::uint8_t mask;                // @42 -- one bit per legal action, declaration order
  std::uint8_t action;              // @43 -- chosen action index
  std::uint8_t flags;               // @44
  std::uint8_t thread;              // @45 -- sim->thread_index
  std::uint8_t kind;                // @46 -- KIND_DECISION
  std::uint8_t reserved;            // @47 -- written zero
};

struct close_record
{
  double final_damage_total;        // @0  -- p->solver_damage_so_far, complete by combat_end
  float fight_length;                // @8  -- p->iteration_fight_length.total_seconds()
  std::uint32_t zero12[ 6 ];        // @12..35 -- written zero
  std::uint32_t decision_count;     // @36 -- decision rows appended for this fight
  std::uint16_t iteration;          // @40
  std::uint8_t zero42;               // @42
  std::uint8_t zero43;               // @43
  std::uint8_t flags;                // @44 -- FLAG_COLLECTED set per D-09; others zero this phase
  std::uint8_t thread;                // @45
  std::uint8_t kind;                  // @46 -- KIND_CLOSE
  std::uint8_t reserved;              // @47 -- written zero
};

// 212-CR-FIX BL-01: this footer carries two DIFFERENT populations of fight,
// and the field names below say which is which so a reader never has to
// guess. `engine_run_aggregate` and `mean_collected_fight_length` are the
// engine's OWN run-level numbers (p->collected_data.*.mean()), which only
// ever accumulate COLLECTED fights -- sim.cpp's datacollection_end() guard
// skips the discarded warm-up fight entirely. `fight_count` and
// `summed_close_damage`, by contrast, are the writer's own running totals
// from record_close(), which runs for EVERY fight including the warm-up.
// `collected_fight_count` is the fourth number that makes this checkable:
// it is incremented in record_close() under the exact same predicate that
// sets a close row's FLAG_COLLECTED, so it counts the SAME population
// `engine_run_aggregate`/`mean_collected_fight_length` were computed over.
// A reader wanting `summed_close_damage / fight_count` as a check against
// `engine_run_aggregate` must instead sum only the close rows with
// FLAG_COLLECTED set and divide by `collected_fight_count` -- mixing the
// two populations (as the pre-fix footer silently invited) is guaranteed
// to disagree for any run with iterations > 1, for a reason that has
// nothing to do with a wrong field, a wrong offset or a missed fight.
struct footer_record
{
  double engine_run_aggregate;         // @0  -- p->collected_data.compound_dmg.mean(); COLLECTED fights only
  float mean_collected_fight_length;   // @8  -- p->collected_data.fight_length.mean(); COLLECTED fights only
  std::uint32_t fight_count;           // @12 -- ALL fights, warm-up included
  std::uint32_t row_count;             // @16 -- INCLUDES the footer row itself
  std::uint32_t footer_source;         // @20 -- FOOTER_SOURCE_PER_FIGHT_ACCUMULATOR
  double summed_close_damage;          // @24 -- writer's own running sum of close-row damage; ALL fights, warm-up included
  std::uint32_t collected_fight_count; // @32 -- count of close rows with FLAG_COLLECTED set (excludes warm-up); was the unused `zero32` slot
  std::uint32_t zero36;                 // @36
  std::uint16_t iteration;              // @40 -- written 0xFFFF, a sentinel: no fight owns the footer
  std::uint8_t zero42;                   // @42
  std::uint8_t zero43;                   // @43
  std::uint8_t flags;                    // @44 -- written zero
  std::uint8_t thread;                   // @45
  std::uint8_t kind;                     // @46 -- KIND_FOOTER
  std::uint8_t reserved;                 // @47 -- written zero
};

// The header. `reserved1` exists purely to keep the three fingerprint
// character arrays eight-aligned starting at offset 32 -- it carries no
// data.
//
// Why 80 and 72, not 64 each: the observation fingerprint is not a bare
// digest -- it is the 74-character prefixed form "rl-obs-v1:<64 hex>"
// (rl_policy_constants.h's RL_OBS_SCHEMA_SHA), and the prefix carries the
// encoder version, so a reader comparing these two strings automatically
// also compares encoder versions. The other two fingerprints are bare
// 64-hex strings. All three are stored as the STRINGS the generated
// constants header already spells, NUL-terminated and zero-filled to the
// end of the array -- that is what makes D-04's "each refusal names which
// config drifted" a direct string comparison on the reader side.
struct file_header
{
  char magic[ 4 ];                    // @0   -- MAGIC, not NUL-terminated
  std::uint32_t endian_canary;        // @4   -- ENDIAN_CANARY
  std::uint32_t format_version;       // @8   -- FORMAT_VERSION
  std::uint32_t record_size;          // @12  -- RECORD_SIZE
  std::uint32_t header_size;          // @16  -- HEADER_SIZE
  std::uint32_t reserved0;            // @20
  std::uint64_t reserved1;            // @24  -- alignment padding only, see above
  char obs_schema_sha[ 80 ];          // @32  -- RL_OBS_SCHEMA_SHA, "rl-obs-v1:<64 hex>"
  char mask_rules_sha[ 72 ];          // @112 -- RL_MASK_RULES_SHA, bare 64 hex
  char action_space_sha[ 72 ];        // @184 -- RL_ACTION_SPACE_SHA, bare 64 hex
};

// ---- Compile-time proof (D-20): the build itself is the proof ----

// 212-CR-FIX WR-01: these four assertions tie RECORD_SIZE/HEADER_SIZE --
// the free-standing literals append_row()'s memcpy actually copies and
// open_and_write_header() stamps into the file -- to the structs' own
// sizeof. Before this fix RECORD_SIZE=48u and HEADER_SIZE=256u were
// separate literals that merely happened to agree with every struct's
// sizeof; nothing tied them together, so a future layout edit that changed
// one struct's size without also updating RECORD_SIZE would compile clean
// while append_row() silently over-read (or truncated) every row of that
// kind. This is the one hole D-20's "a disagreeing layout cannot compile"
// premise did not already close.
static_assert( sizeof( decision_record ) == RECORD_SIZE, "decision_record must be exactly RECORD_SIZE bytes" );
static_assert( alignof( decision_record ) == 8, "decision_record must be 8-aligned" );
static_assert( sizeof( close_record ) == RECORD_SIZE, "close_record must be exactly RECORD_SIZE bytes" );
static_assert( alignof( close_record ) == 8, "close_record must be 8-aligned" );
static_assert( sizeof( footer_record ) == RECORD_SIZE, "footer_record must be exactly RECORD_SIZE bytes" );
static_assert( alignof( footer_record ) == 8, "footer_record must be 8-aligned" );
static_assert( sizeof( file_header ) == HEADER_SIZE, "file_header must be exactly HEADER_SIZE bytes" );
static_assert( alignof( file_header ) == 8, "file_header must be 8-aligned" );

// decision_record field offsets
static_assert( offsetof( decision_record, damage ) == 0 );
static_assert( offsetof( decision_record, t ) == 8 );
static_assert( offsetof( decision_record, obs ) == 12 );
static_assert( offsetof( decision_record, q_margin ) == 32 );
static_assert( offsetof( decision_record, seq ) == 36 );
static_assert( offsetof( decision_record, iteration ) == 40 );
static_assert( offsetof( decision_record, mask ) == 42 );
static_assert( offsetof( decision_record, action ) == 43 );
static_assert( offsetof( decision_record, flags ) == 44 );
static_assert( offsetof( decision_record, thread ) == 45 );
static_assert( offsetof( decision_record, kind ) == 46 );
static_assert( offsetof( decision_record, reserved ) == 47 );

// close_record field offsets
static_assert( offsetof( close_record, final_damage_total ) == 0 );
static_assert( offsetof( close_record, fight_length ) == 8 );
static_assert( offsetof( close_record, zero12 ) == 12 );
static_assert( offsetof( close_record, decision_count ) == 36 );
static_assert( offsetof( close_record, iteration ) == 40 );
static_assert( offsetof( close_record, zero42 ) == 42 );
static_assert( offsetof( close_record, zero43 ) == 43 );
static_assert( offsetof( close_record, flags ) == 44 );
static_assert( offsetof( close_record, thread ) == 45 );
static_assert( offsetof( close_record, kind ) == 46 );
static_assert( offsetof( close_record, reserved ) == 47 );

// footer_record field offsets
static_assert( offsetof( footer_record, engine_run_aggregate ) == 0 );
static_assert( offsetof( footer_record, mean_collected_fight_length ) == 8 );
static_assert( offsetof( footer_record, fight_count ) == 12 );
static_assert( offsetof( footer_record, row_count ) == 16 );
static_assert( offsetof( footer_record, footer_source ) == 20 );
static_assert( offsetof( footer_record, summed_close_damage ) == 24 );
static_assert( offsetof( footer_record, collected_fight_count ) == 32 );
static_assert( offsetof( footer_record, zero36 ) == 36 );
static_assert( offsetof( footer_record, iteration ) == 40 );
static_assert( offsetof( footer_record, zero42 ) == 42 );
static_assert( offsetof( footer_record, zero43 ) == 43 );
static_assert( offsetof( footer_record, flags ) == 44 );
static_assert( offsetof( footer_record, thread ) == 45 );
static_assert( offsetof( footer_record, kind ) == 46 );
static_assert( offsetof( footer_record, reserved ) == 47 );

// file_header field offsets
static_assert( offsetof( file_header, magic ) == 0 );
static_assert( offsetof( file_header, endian_canary ) == 4 );
static_assert( offsetof( file_header, format_version ) == 8 );
static_assert( offsetof( file_header, record_size ) == 12 );
static_assert( offsetof( file_header, header_size ) == 16 );
static_assert( offsetof( file_header, reserved0 ) == 20 );
static_assert( offsetof( file_header, reserved1 ) == 24 );
static_assert( offsetof( file_header, obs_schema_sha ) == 32 );
static_assert( offsetof( file_header, mask_rules_sha ) == 112 );
static_assert( offsetof( file_header, action_space_sha ) == 184 );

// Cross-kind assertions (D-02's entire premise: a reader can classify a
// row before it interprets it, because `kind` and `reserved` sit at the
// same offset in every row shape).
static_assert( offsetof( decision_record, kind ) == offsetof( close_record, kind ) );
static_assert( offsetof( close_record, kind ) == offsetof( footer_record, kind ) );
static_assert( offsetof( decision_record, kind ) == 46 );
static_assert( offsetof( decision_record, reserved ) == offsetof( close_record, reserved ) );
static_assert( offsetof( close_record, reserved ) == offsetof( footer_record, reserved ) );
static_assert( offsetof( decision_record, reserved ) == 47 );

// Dimension guards.
static_assert( RL_ACTION_DIM <= 8, "the legality bitfield is one byte; a wider action set must fail the build" );
static_assert( RL_OBS_DIM == 5, "the row reserves exactly five float32 obs slots at offset 12" );

// 212-CR-FIX WR-07: the three fingerprint strings are generated
// (rl_policy_constants.h) and NOT under this file's control -- nothing
// stopped them from growing past their fixed-width header fields. Before
// this fix, open_and_write_header()'s std::strncpy truncated silently on
// overflow (guaranteed NUL-terminated by value-initialisation, but
// truncated all the same), which would turn D-04's fingerprint refusal
// into a permanently misleading "config drifted" message naming a config
// that did not actually drift -- just got cut short. `< 80`/`< 72` (not
// `<=`) because strncpy's destination needs room for the trailing NUL.
static_assert( std::string_view( RL_OBS_SCHEMA_SHA ).size() < 80,
               "RL_OBS_SCHEMA_SHA must fit file_header::obs_schema_sha with room for its NUL" );
static_assert( std::string_view( RL_MASK_RULES_SHA ).size() < 72,
               "RL_MASK_RULES_SHA must fit file_header::mask_rules_sha with room for its NUL" );
static_assert( std::string_view( RL_ACTION_SPACE_SHA ).size() < 72,
               "RL_ACTION_SPACE_SHA must fit file_header::action_space_sha with room for its NUL" );

// ---- Writer interface. All five are no-ops when rl_translog= is unset. ----

// Root only -- called once, from sim_t::setup(). Opens the stream and
// writes the 256-byte header.
void open_and_write_header( sim_t* sim );

// Called from inside the in-process arm of solver_control::choose(), once
// per decision boundary that arm answers. Appends only -- no flush; the
// fight's close row flushes the whole fight at once (D-13).
void record_decision( sim_t* sim, const player_t* p, std::uint64_t seq,
                       const float obs[ RL_OBS_DIM ], const std::uint8_t mask[ RL_ACTION_DIM ],
                       int action_index, float q_margin, bool wait_floored );

// Called from sim_t::combat_end(), after datacollection_end(). Builds and
// appends the close row, then flushes the buffered rows for this fight to
// disk in one write (D-13 -- syscall cost, not crash-loss bounding; a
// crashed file is refused whole by the reader, D-14).
void record_close( sim_t* sim );

// Called from sim_t::execute(), immediately after analyze(), inside the
// existing `if ( success )` guard. Appends the footer row, flushes and
// closes the stream.
void write_footer( sim_t* sim );

// 212-CR-FIX WR-06: called from the in-process arm's cast/wait epilogues
// (solver_control.cpp) when accept_cast()/accept_wait() throws. On that
// path record_close() never runs for the in-flight fight -- the exception
// unwinds past combat_end()'s hook entirely -- so without this call the
// buffered rows, INCLUDING the decision that was made and then refused
// (the one thing the write-before-accept ordering exists to preserve),
// would be lost with the process, and the file that survives has no
// footer, so D-14 makes the reader refuse it whole anyway. Writes whatever
// is currently buffered to disk and clears the buffer; does NOT append a
// close or footer row, because the fight did not actually end. No-op when
// rl_translog= is unset or nothing is buffered.
void flush_pending( sim_t* sim );

} // namespace rl_translog
