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
// THE WHOLE RULE (plan 03 Task 3): the hide, stat, swing and gate channels (press-applied buffs and debuffs whose own hiding changes a hit, stat buffs,
// the speed buffs of a swing launch, and the gate players of a launch chain: a buff read non-zero before a launch at a site the loaded verdict table
// calls `gate`, plus the switch buff of a `switch` launch), self, damage-over-time parent and swing groups, then the D-05u refund sweep, then the
// per-decision fold for both slices and both copies. D-05a, D-05 and D-05a-next are NOT here (the training rule is D-05u only).
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

// ---- the typed records (plan 03b, owner ruling 10: ONE structured interface) ----
//
// Every record the module uses enters it as one of the plain structs below, through the matching `on_*` entry point: the ledger hook builds the
// struct from the values it holds at its emit point, and the fixture entry (rl_buff_credit_selftest=) converts a fixture line into the same
// struct and calls the same entry point. Nothing in the module's arithmetic reads JSON. A launch that is a switch launch carries `has_switch`
// and `sw`; a swing launch carries `speed` (the launch kind itself is not read). Refusals about VALUES (a class outside 0..6, a candidate index
// above 63) live in the entry points; refusals about JSON SHAPE live in the fixture converter.
struct applier_t
{
  std::int64_t press = 0;
  int cls            = 0;  // the BASE class 0..6 (never the raw byte with the deck mark)
  double stacks      = 0.0;
  bool covering      = true;
};

struct cand_t
{
  std::uint32_t bit = 0;  // index into the pass set's hidden-set mask, 0..63
  std::string name, owner, kind;
  std::vector<applier_t> app;
  bool cov = false;
};

enum pass_kind_t : std::uint8_t
{
  PASS_REF,
  PASS_HIDE,
  PASS_OTHER,
};

struct pass_t
{
  pass_kind_t kind    = PASS_OTHER;
  std::uint64_t hid   = 0;
  double pre = 0.0, cc = 0.0, cb = 0.0, per = 0.0;
  bool viol           = false;
};

struct passset_t
{
  std::vector<cand_t> cands;
  std::vector<pass_t> passes;
};

struct press_rec_t  // `pr`
{
  std::int64_t press = 0, seq = 0;
};

struct application_rec_t  // `app`
{
  std::uint64_t id = 0;
  passset_t ps;
};

// A buff entry of a launch: the switch buff of a switch launch (`factor` unused) or one speed buff of a swing launch.
struct launch_buff_t
{
  std::string name, owner;
  std::vector<applier_t> app;
  double factor = 0.0;
};

struct launch_rec_t  // `ln`
{
  std::int64_t id    = 0;
  std::int64_t frame = -1;
  std::int64_t order = -1;
  std::string ca;                  // the child action (the verdict table's third key); empty when absent
  bool has_switch = false;         // a `switch` launch with a truthy switch entry
  launch_buff_t sw;                // its buff and appliers
  std::vector<launch_buff_t> speed;  // a `swing` launch's speed buffs: only entries with a finite factor > 0
};

struct frame_rec_t  // `frm`
{
  std::int64_t id     = 0;
  std::int64_t launch = -1;
  std::string action;  // empty when absent
  std::string kind;    // empty when absent
  std::int64_t cls    = 0;
};

struct read_rec_t  // `rd`
{
  std::int64_t frame = 0, order = 0;
  bool nz            = false;  // the read was non-zero
  std::string buff, owner;
  std::vector<applier_t> app;
  bool cov = false;
};

struct draw_rec_t  // `dr`
{
  std::int64_t frame = 0, order = 0;
};

struct cycle_rec_t  // `cyc`
{
  std::int64_t id = 0;
  double t = 0.0, len = 0.0;
};

struct refund_rec_t  // `ref`
{
  std::int64_t cycle = 0, press = 0;
  double sec = 0.0;
};

struct use_rec_t  // `use`
{
  std::int64_t cycle = 0, press = 0;
  double t = 0.0;
};

struct hit_rec_t  // `hit`
{
  double ra = 0.0, exp = 0.0;
  bool exp_excl = false;
  std::int64_t seq = 0, press = -1, launch = -1, win = 0;
  int cls    = 0;  // the BASE class 0..6
  int status = 0;
  bool ch = false, pet = false, crit = false, tick = false;
  passset_t ps;                     // the hit's candidates and passes
  std::vector<std::uint64_t> par;  // the ids of the `app` records the hit names as parents
};

// The entry points. Each takes its record by value (moved in); the module keeps what it needs. `on_fight_begin` resets the fight's tables.
void on_fight_begin( module_t& m );
void on_press( module_t& m, press_rec_t&& r );
void on_application( module_t& m, application_rec_t&& r );
void on_launch( module_t& m, launch_rec_t&& r );
void on_frame( module_t& m, frame_rec_t&& r );
void on_read( module_t& m, read_rec_t&& r );
void on_draw( module_t& m, draw_rec_t&& r );
void on_cycle( module_t& m, cycle_rec_t&& r );
void on_refund( module_t& m, refund_rec_t&& r );
void on_use( module_t& m, use_rec_t&& r );
void on_hit( module_t& m, hit_rec_t&& r );

// Would the module keep this record? A read is kept only when it is non-zero and its buff is named by a gate site of the loaded verdict table;
// a draw only for a frame that has a kept read. The ledger asks before it builds a read or draw record the module would drop (the same
// test the entry points apply, so the answer cannot differ).
bool read_is_kept( const module_t& m, bool nz, const std::string& buff );
bool draw_is_kept( const module_t& m, std::int64_t frame );

// rl_translog::record_close, right after the `.attr` FIGHT block: folds the fight just ended and writes the FIGHT and DECISION records.
// `decision_seqs` are the translog's decisions of the fight in write order (rl_translog_pending_seqs).
void write_fight( module_t& m, std::uint32_t iteration, bool collected, bool funnel, const std::vector<std::uint64_t>& decision_seqs );

// The ledger's footer (end of run): writes the `.bcr` FOOTER with the ledger's twelve required-zero counters, in the order of
// translog.BCR_REQUIRED_ZERO_NAMES, and closes the file.
void write_footer( module_t& m, const std::array<std::uint32_t, 12>& ledger_counters );

// The fixture entry (sim_t::setup, option rl_buff_credit_selftest=<fixture.jsonl>, with rl_buff_credit_selftest_out=<path.bcr>, rl_buff_credit and
// rl_buff_credit_verdicts): reads ledger JSON records from the fixture, converts each into the typed record of its kind and calls the entry point the
// live ledger calls, writes the `.bcr`, runs no fight. It is the only place a ledger record's JSON is read. Returns a process exit code (0 written, 2 refused with the reason on stderr).
int run_selftest( sim_t* sim );
}  // namespace rl_buff_credit
