// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// The compact training-data shard stream (261029-trp S2-F3, transport T2b; owner ruling 2026-10-29, fzl CONTEXT item 5).
//
// A fight-list process that is given `rl_shard=<path>` writes, beside the unchanged float32 translog, one shard file: a concatenation of zstd frames
//
//     header frame (canonical JSON)  |  one frame per fight, written at the fight's close  |  close frame (canonical JSON)
//
// A fight frame is a fixed 72-byte little-endian fight header (magic "SFT1", the entry index, the close row's shape / setup / stat / slot tag words, the sha256 of
// the RLW1 file the process loaded, the row count, the final damage as float64, the fight length as float32) followed by `rows` packed 615-byte rows: the
// observation in the four class groups of the registry-generated quant table (rl_policy_constants.h, "Quant table": bits LSB-first, u8 lattice codes, u16 lattice
// codes, int16 fixed codes), then action (u8), mask (u32), chosen_target_actor_index (u16), flags (u8, bit 0 = the translog's explored bit), reward (f32),
// damage_to_end (f32), t (f32), t_decided (f32).
//
// THE TWIN. The layout, the encoding and the arithmetic are the C++ twin of src/sylvanas_rl/contract/shard.py and src/sylvanas_rl/sim/shard_derive.py (the Python
// derivation of the same shard from the translog, `shard_derive verify` compares the DECOMPRESSED header and every fight payload byte for byte; compressed bytes are never
// compared, zstd output depends on the library version). The reward is the per-cause reward of contract/translog.py `per_cause_rewards` (own credit of the row's cause,
// from the .attr numbers, plus the background and orphan credit that landed in the row's window) computed in float64 in the same operation order; damage_to_end is the
// sequential reverse float64 sum of those rewards, then float32. Nothing here reads anything the translog does not carry: the stream only READS the values the translog
// rows hold (the exact float32 of the row), so the translog, `.attr`, `.apl`, `.procs.json` and `.credit.json` bytes are the same with the option on and off.
//
// REFUSALS. A value outside its slot's class is never clipped: the ENTRY is not written to the shard, its index and the line below go into the close frame's
// `aborted_entries`, the line is printed to stderr, and the list goes on (the translog still holds the entry; the process exit code is not changed):
//     RL_SHARD_OUT_OF_CLASS slot=<index>:<leaf> value=<%a> class=<bit|u8_lattice|u16_lattice|i16_fixed> range=[lo,hi]
// Rewards that do not tile the fight: RL_SHARD_REWARD_CONSERVATION entry=<k> ...; reward inputs the Python raises a RewardInputError for (a falling damage or background
// column, no rows): RL_SHARD_REWARD_INPUT entry=<k> <reason>.
//
// A shard without its close frame is refused by every reader (`shard-unclosed`): a process that dies mid-list leaves no close frame. A warm-up entry (rl_list_warmup)
// writes no frame.
//
// TEST HOOK. `rl_shard_plant=<row>:<slot>:<value>` (an entry option, never a process option) overwrites observation slot <slot> of the <row>-th (0-based, write order)
// decision row of THAT entry, in the translog row and the shard alike, after the policy has read the observation (play is unchanged). It exists to prove the refusal
// path against the Python derivation on a real list; without it the code path is a single empty-string test per row.

#pragma once

#include "sim/rl_translog.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

struct sim_t;
struct player_t;

namespace rl_shard
{

// Opens the shard file at `path` (process-wide, once) and writes the header frame. `translog_path` names the entry-0 rl_translog= value (its last path component is the
// header's translog_base); `net_sha256` is the sha256 of the RLW1 bytes the process loaded.
void open( const std::string& path, const std::string& translog_path, const std::uint8_t net_sha256[ 32 ] );

// True when a shard file is open.
bool enabled();

// Called after an entry's setup(): starts the entry's row buffer, reads the entry's plant option.
void begin_entry( const sim_t* sim, std::size_t entry_index );

// True while the current entry's rows are being buffered (a shard is open and the entry is not a warm-up).
bool active();

// The row to write: `row` itself, or `scratch` holding a copy of it with the planted value when this entry plants into the `row_index`-th row.
const rl_translog::decision_record& plant( const rl_translog::decision_record& row, std::uint32_t row_index, rl_translog::decision_record& scratch );

// Buffers one decision row (exactly the record that goes into the translog).
void add_row( const rl_translog::decision_record& row );

// Called from record_close(): computes the rewards, encodes the fight and writes its frame, or records the entry as aborted.
void close_fight( const sim_t* sim, const player_t* p, const rl_translog::close_record& close );

// Writes the close frame and closes the file. No-op when no shard is open.
void finish();

}  // namespace rl_shard
