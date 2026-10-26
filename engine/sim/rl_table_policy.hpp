// ==========================================================================
// 261025-bk4 (wave B, lane b2; reconciled with the Python side in wave B3): THE TABLE PLAYER. The in-process arm of solver_control::choose plays a TBP1 table.
//
// Words used here. A "TBP1" file is the table player's file (written by the repository's learn/tabular/tbp.py): a compiled state set (the
// bucketing rules on the 444 observation slots), `Q[s, a]` (float32), `N[s, a]` (int32) and optionally `M2[s, a]` (float64) over the 22
// buttons, and the chapter 4 strategy that chooses from a state's row. A "state" is the bucket id of one observation (`state.py`). A
// "decided" button is one the strategy chooses among (the header's `decided_tokens`); every other cast button is the stock list's business
// (the hybrid, below). The "stock list" is the preserved copy of the hand-written rotation (rl_stock_eval.hpp).
//
// Where it sits. player_t::execute_action runs SimC's own select_action first and solver_control::choose second; the table replaces the net
// forward pass and masked_argmax in the net arm (`solver_policy=` is still required: its blob is loaded, its `allowed_actions` ANDs the mask,
// it is never consulted). Everything after the pick is the net arm's own: the row is recorded, THEN the cast is accepted (accept_cast, with
// the rules path's aim and retarget). The order of operations of one decision (research F 2.1, the contract):
//
//     1. the dry stock walk, the state, the mask, the observation          (unchanged net-arm code)
//     2. the state id is bucketed from the observation and the mask         (`bucket`, the float32 pipeline of state.py)
//     3. the plan: STOCK (defer) | NOSTATE (fallback) | FORCED_IDLE | STRATEGY, read-only; a background boundary with nothing to press is
//        a no-op that consumes no decision number
//     4. (STRATEGY plans only) the pending strategy decision's reward is folded in:  r = damage now - damage at THAT decision;
//        N += 1; alpha_n = max(update_alpha, 1/N); Q = float32( Q_old + alpha_n * (r - Q_old) ) (float64 arithmetic on the widened float32 Q);
//        M2 = N * (1 - alpha_n) * (M2_old / N_old + alpha_n * (r - Q_old)^2) (the exponentially weighted variance times N; N_old = 0 reads as variance 0)
//     5. the pick: the strategy on the (updated) row, the legal set = mask AND decided set, RNG key (fight seed, decision index)
//     6. the row is recorded (translog + `.apl` format 3 + optional `.tbt` trace), then accept_cast (or the idle fall-through)
//   and the last strategy decision of a fight is closed at combat_end (on_fight_end), immediately before the translog's close row.
//
// Reward. `reward_rule` "damage-since-last-strategy-decision-v1": r = p->solver_damage_so_far (double) at the NEXT strategy decision (chooser STRATEGY or
// STRATEGY_IDLE) minus its value at the pending strategy decision; the rows pressed in between (STOCK, FORCED_IDLE, NOSTATE) neither update nor move the pending
// decision. The fight's last pending decision is closed at combat_end with the fight's final damage (the close row's final_damage_total). `update_rule`
// "constant-alpha-floor-f64-v1" is the update above (the header's `update_alpha`, 0.05 in the lane-lrn files). Both ids are learn/tabular/tbp.py's; the reference is rungs/r000_plumbing/table_certify.py.
//
// The cooldown hybrid (H1-pre, `solver_table_defer=stock`, the default). When the dry stock walk's first ready MAPPED leaf is a cast button the
// table does not decide and that button is legal in the row's mask, it is pressed (chooser STOCK): no RNG draw, no table change. Else the strategy
// decides among the legal decided buttons. With `solver_table_defer=none` the stock list is never consulted for a press. With `solver_table_defer=all`
// (arm A1, the transport anchor) every legal stock pick is pressed (STOCK), no ready mapped leaf is the stock pick "wait" (STOCK on the idle button), an
// illegal stock pick is a FORCED_IDLE; no draw, no update. In every mode an observation outside the state set is NOSTATE (bucketing precedes the deferral test).
//
// "No press" is a fall-through: nothing is returned and no wait is set, so SimC's own idle poll (player_t::available(), gauss 100 ms) schedules
// the next try. The idle button is the registry's `wait_next_event`. It is pressed when nothing decided is legal (FORCED_IDLE) or, with
// `solver_table_wait=1`, when the strategy chooses it (the decided set then includes it).
//
// Options (all default off; global storage here, because a sim_t member would rebuild every file; the RL rigs run one sim per process):
//     solver_table_policy=<path>       the TBP1 file; empty = the table player is off (one string test)
//     solver_table_scope=fight         fight (the only scope, the default): Q, N, M2 are copied from the loaded file at every fight start
//     solver_table_fallback=stock|refuse   an observation outside the state set's domain: the stock pick (chooser NOSTATE), or abort by name
//     solver_table_defer=stock|none|all    the hybrid above
//     solver_table_wait=0|1                voluntary idle (default 0)
//     solver_table_trace=0|1               write `<rl_translog>.tbt` (layout in rl_table_policy.cpp)
//     solver_table_save=<path>             at the end of the KEPT fight (the last iteration) write the table as it stands to <path> (tbp.py's byte layout, the loaded file's header
//                                          verbatim, the CRC-32 recomputed; <path>.tmp then rename); refused TABLE_NEEDS when it names the input file. A chain of launches
//                                          carries a table across fights by starting each launch from the previous launch's saved file (quick 261026-lrn). Empty = off.
//     solver_table_clock=<k>               integer >= 0, default 0: the `e` of the three decaying strategies (lin_dec_epsilon_greedy, exp_dec_epsilon_greedy, softmax) for every
//                                          decision of this launch. It is the fight's place in its chain, so a schedule decays over fights, not over the decisions of one fight
//                                          (before version 3 `e` was the decision index). The `.tbt` trace header records it.
// Needs (refused by name TABLE_NEEDS): solver_policy=, solver_apl_stock_list=, solver_record_apl_choice=1, rl_translog=, a blob without an aim head.
// Conflicts (TABLE_CONFLICT): solver_control=, solver_policy_observe_stock, hold windows, forced decisions, solver_min_maelstrom_spend > 0,
// solver_mask_no_enemy, a branch / teacher / re-salt mode. Loader refusals start TABLE_FILE:, a refused fallback TABLE_FALLBACK_REFUSED:.
//
// The file, TBP1 version 3 (the repository's writer is learn/tabular/tbp.py, driven by tools/tbp_compile.py; its docstring documents the byte layout):
// magic `TBP1`, u32 version 3, u64 header bytes, a JSON header, Q float32 (S x 22), N int32 (S x 22), M2 float64 (S x 22), a CRC-32 trailer. The header carries
// `fingerprints` (the three registry hashes, refused TBP_FINGERPRINT against this build's baked ones), `buttons`, `state_set.n_states` (at most 2^20, TBP_STATES),
// `features` (the six rule kinds, doubles for float64 edges and float32 values), `strategy` {id, params}, the `rng` rule text of learn/tabular/rng.py, the `decided`
// block {tokens, buttons}, `update_rule` "constant-alpha-floor-f64-v1", `update_alpha` (a number in (0, 1]), `reward_rule` "damage-since-last-strategy-decision-v1", `scope` "fight" and `k`. The header's `k` (the K
// rule) is read by nobody here: the player does not apply it (table_certify.py). For optimistic_initialization the prior is already in the file's arrays (the compiler folds it; this reader folds nothing, so a saved table loads again unchanged).
// A version 2 file is refused by name (TBP_VERSION).
//
// The sim-less probe (`simc rl_table_probe=<mode> key=value ...`, run from main() before any sim_t exists): modes buckets, picks, rows, rng, libm,
// epsilons. The formats are in the comment above run_probe in rl_table_policy.cpp.
//
// Off path. With solver_table_policy empty nothing here runs and a run is byte-identical to one built before this module existed (one string test per
// decision; the `.apl` sidecar of a stock-list run is format 3 either way, with chooser NONE).
// ==========================================================================

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sc_enums.hpp"
#include "sim/rl_stock_eval.hpp"
#include "sim/rl_translog.hpp"

struct player_t;
struct sim_t;

namespace rl_table_policy
{

namespace rl_apl_choice = rl_translog::rl_apl_choice;

// Option storage. See the top of this file.
std::string& path_storage();
std::string& scope_storage();
std::string& fallback_storage();
std::string& defer_storage();
bool& wait_storage();
bool& trace_storage();
std::string& save_storage();
int& clock_storage();

// True when solver_table_policy names a file. One string test.
bool active();

// Called from rl_stock_eval::validate_table_options (sim_t::setup): loads and validates the file against the build, resolves the option strings,
// opens the trace. Throws sc_runtime_error starting `TABLE_FILE:` / `TABLE_NEEDS:`. Does nothing when the option is empty.
void load_and_validate( sim_t* sim );

// Called from solver_control::reset_iteration at every fight start (clears the fight's runtime; scope=fight also copies Q, N, M2 from the file).
void on_fight_start( sim_t* sim );

// Called from sim_t::combat_end() immediately before rl_translog::record_close: closes the last strategy decision with the fight's final damage.
void on_fight_end( sim_t* sim );

// Called from rl_translog::write_footer after the `.apl` footer: writes the `.tbt` footer record (kind 2: decision and fight-end counts) and closes the trace. Does nothing without a trace.
void close_trace( sim_t* sim );

// One decision's answer.
struct decision_t
{
  bool                        noop = false;  // background boundary with nothing to press: no row, the caller gives the decision number back
  int                         idx = -1;      // the button (RL_ACTIONS index); the idle button for a fall-through
  bool                        idle = false;  // idx is the fall-through idle button
  float                       q_margin = 0.0f;
  float                       top_q = 0.0f;
  rl_apl_choice::table_info_t info;          // the format-3 record's table fields
};

// Plans, updates, picks and traces one decision. `mask` is the FINAL legality mask (post allowed_actions AND), `obs` the observation built from it.
// Throws sc_runtime_error (TABLE_FALLBACK_REFUSED:, TABLE_NO_LEGAL:, TABLE_STRATEGY:) by name; never draws from the simulator's own random streams.
decision_t decide( sim_t* sim, player_t* p, bool foreground, std::uint64_t seq, const float* obs, const std::uint8_t* mask,
                   const rl_stock_eval::pick_t& stock );

// The sim-less probe (see rl_table_policy.cpp). `args` are the process arguments after the program name. Returns the exit code.
int run_probe( const std::vector<std::string>& args );

}  // namespace rl_table_policy
