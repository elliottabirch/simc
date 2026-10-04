// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// 261003-s1c plan 03 (fork clone simc-s1c only, never in production): the buff-applier credit rule (D-05u) inside the simulator.
//
// rl_buff_credit=off|nobody|full (absent = off) switches on a consumer of the buff ledger's records (rl_buff_ledger.hpp): per hit it splits the
// hit's realised amount between the dealer and the presses that applied the buffs the hit rode on (PREREG D-01, D-02, D-03 with the
// amendments; the Python reference is `ledger_reference.py` in the tstl-sylvanas repo's quick task 261003-s1c, and this module equals it to
// 1e-9 on every committed fixture), folds the result per decision and writes it, per fight, to `<rl_translog>.bcr` (BCR-FORMAT.md). Nothing
// the fight does depends on it: the consumer only reads records; with the switch off no code of this module runs.
//
// GENERIC ARITHMETIC ONLY: no spell, buff or action name appears in this module. Everything is keyed by ids and strings that arrive in the
// records and in the loaded verdict table.
//
// TRACER STATE (Task 1 of plan 03): the hide channel only (press-applied buffs and debuffs whose own hiding changes a hit: self and
// damage-over-time parent groups), no gates, no swing channel, no stat channel, no refunds. The build says so on stderr
// (`[RL_BUFF_CREDIT_TRACER]`); Tasks 2 and 3 extend it and remove the line.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct sim_t;

namespace rl_buff_credit
{
enum class credit_mode_t : std::uint8_t
{
  off    = 0,
  nobody = 1,
  full   = 2,
};

// "off" | "nobody" | "full" -> true; anything else false (the caller refuses at parse, never defaults).
bool parse_mode( std::string_view text, credit_mode_t& out );
const char* mode_name( credit_mode_t m );

// The module's state (defined in rl_buff_credit.cpp; sim_t holds it behind a shared_ptr so sim.hpp never sees the type).
struct module_t;

// sim_t::init, with the switch on and every option already validated: loads the verdict table (refuses an unparseable file or a site with no
// verdict), computes its sha256, opens `<rl_translog>.bcr` and writes the header, prints the two notice lines once, stores the module on the root
// sim (`rl_bc_state`). Returns the module (never null).
std::shared_ptr<module_t> configure( sim_t* root, credit_mode_t mode, const std::string& verdicts_path, const std::string& translog_path );

// One ledger record line (JSON, no trailing newline needed). Called by the ledger's emit for every record it formats while the module is on.
void consume_line( module_t& m, std::string_view line );

// rl_translog::record_close, right after the `.attr` FIGHT block: folds the fight just ended and writes the FIGHT and DECISION records.
// `decision_seqs` are the translog's decisions of the fight in write order (rl_translog_pending_seqs).
void write_fight( module_t& m, std::uint32_t iteration, bool collected, bool funnel, const std::vector<std::uint64_t>& decision_seqs );

// The ledger's footer (end of run): writes the `.bcr` FOOTER with the ledger's twelve required-zero counters, in the order of
// translog.BCR_REQUIRED_ZERO_NAMES, and closes the file.
void write_footer( module_t& m, const std::array<std::uint32_t, 12>& ledger_counters );

// The fixture entry (sim_t::setup, option rl_buff_credit_selftest=<fixture.jsonl>, with rl_buff_credit_selftest_out=<path.bcr>, rl_buff_credit and
// rl_buff_credit_verdicts): reads ledger JSON records from the fixture, feeds the module the way the live ledger does, writes the `.bcr`, runs no
// fight. Returns a process exit code (0 written, 2 refused with the reason on stderr).
int run_selftest( sim_t* sim );
}  // namespace rl_buff_credit
