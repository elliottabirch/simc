// ==========================================================================
// solver_search -- the horizon search (tstl-sylvanas v2.18, phase 280), a module of its own beside solver_branch.
//
// tstl-sylvanas 280-03: this file and solver_search.cpp are new. solver_branch.cpp and solver_branch.hpp are NOT
// edited (the teacher path the depth-one arm of the depth-two-versus-depth-one comparison uses stays byte for byte
// what it was); the few small helpers the search needs are copied into solver_search.cpp's own anonymous namespace.
//
// At a chosen decision the parent fork()s COPIES of the whole simulator. Each copy presses ONE candidate button
// (the net's top `candidates` selectable buttons), plays the net forward to an EXACT window cut at t_k + window
// seconds, reports its window return through a pipe and leaves. The parent spends the copies by halving (8 copies
// per candidate first, drop the worst candidate by mean, the remaining copies to the survivors), aggregates per
// COPY (never per leaf), and writes one log line per search. Every copy rolls its own dice and gets its own
// re-dealt decks (owner rule D1: no paired random numbers anywhere).
//
// Off by default (zero cost and byte-identical behaviour when `solver_search` is 0: every hook returns on one bool
// test). Options (all validated and refused by name in validate()):
//
//   solver_search=1                        turn the search on (needs solver_policy= greedy weights, iterations=1,
//                                          threads=1, per_source_rng=1; no solver_branch mode, no solver_teacher)
//   solver_search_candidates=2..4          root candidates: the top of q over the selectable buttons (default 3)
//   solver_search_window=W                 seconds, > 0 and <= 120 (default 20)
//   solver_search_copies=K                 copies per search, >= 8 x candidates (default 64)
//   solver_search_at=k1,k2,...             the decisions to search (k = seq - 1, the teacher's solver_teacher_at
//                                          convention; ascending, distinct, >= 0)
//   solver_search_margin=m                 when no solver_search_at: search every near tie (q_margin < m)
//   solver_search_jobs=1..12               copies alive at once (this search's own cap)
//   solver_search_expected_crit=1|0        score expected-crit damage (1, default) or realised damage (0)
//   solver_search_press=net|best           net (default) never changes the parent's press, so the parent plays the
//                                          net's own fight; best presses the search's pick
//   solver_search_log=<file>               JSON lines: one per search, a final {"summary":1,...} line
//   solver_search_splits=0..4              (280-04) the split budget S: the most chance splits on one path (default 2).
//                                          Inside a copy, at a roll on a split-eligible proc generator (or a draw on
//                                          a deck whose exact next-card chance is strictly between 0 and 1) the copy
//                                          forks so BOTH outcomes are played, each weighted by its exact chance
//   solver_search_split_site=<site key>    (280-04, proof only) make exactly this one site split-eligible; needs
//                                          solver_search_splits >= 1; refused by name when the key is unknown or names
//                                          a row that can never split (a threshold generator)
//   solver_search_sites_in=<sites.json>    (280-05) the ranking of earlier runs: a JSON array of sites with the C-A
//                                          fields; the top S eligible rows by var_share are split-eligible for the
//                                          whole run, the cold-start rule fills a short set; refused by name when the
//                                          file or one of its elements is malformed
//   solver_search_sites_out=<sites.json>   (280-05) written by the parent at exit (temp file, then rename): the merged
//                                          table ranked by var_share, C-A fields first, cumulative fields appended
//   solver_search_nosplit_audit=0|1        (280-05) 1 runs every search of the process with no splits and marks it, so
//                                          the label's standard error without splits can be set beside the one with
//   solver_search_depth=1|2                (280-06) 1 = the depth-one search; 2 (default) = the depth-two tree: inside each
//                                          pass-2 copy pair the next decision with at least two selectable buttons forks
//                                          into the net's rank-0 and rank-1 buttons
//   solver_search_second=2                 (280-06) the second level's width; only 2 is accepted
//   solver_search_holds=0|1                (280-06) 1 (default) adds the legal wait button with the highest value as one
//                                          more root candidate when no wait button is among the top candidates
//   solver_search_value=<v7 blob>          (280-06) an RLW1 version-7 weights file (body chooser-value, with a value head); the
//                                          window-end value is read from it at the exact cut (refused by name otherwise)
//   solver_search_bootstrap=none|q|value   (280-06) what is added to the window return R at the cut: nothing; gamma^(T - t_k)
//                                          x the net's top value there (q, the depth-one teacher's own treatment); or gamma^(T
//                                          - t_k) x value_forward(end observation) / solver_branch_value_divisor (value).
//                                          Default: value when solver_search_value is given, else none
//
// Read, not registered here: solver_branch_value_divisor (> 0), solver_branch_gamma, solver_branch_salt_base,
// solver_branch_independent_dice and solver_branch_redeal (both must stay 1: paired futures are refused).
// ==========================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct sim_t;
struct player_t;

namespace solver_search
{
struct config_t
{
  bool search = false;                // solver_search
  int candidates = 3;                 // solver_search_candidates, 2..4
  double window = 20.0;               // solver_search_window, seconds, (0, 120]
  int copies = 64;                    // solver_search_copies, >= 8 x candidates
  std::string at;                     // solver_search_at, "k1,k2,..." (k = seq - 1)
  int jobs = 1;                       // solver_search_jobs, 1..12
  bool expected_crit = true;          // solver_search_expected_crit
  std::string press = "net";          // solver_search_press, "net" | "best"
  double margin = 0.0;                // solver_search_margin, > 0 searches near ties when `at` is empty
  std::string log;                    // solver_search_log
  int splits = 2;                     // solver_search_splits, 0..4 (280-04)
  std::string split_site;             // solver_search_split_site (280-04, proof only)
  std::string sites_in;               // solver_search_sites_in (280-05)
  std::string sites_out;              // solver_search_sites_out (280-05)
  bool nosplit_audit = false;         // solver_search_nosplit_audit (280-05)
  int depth = 2;                      // solver_search_depth, 1 or 2 (280-06)
  int second = 2;                     // solver_search_second, only 2 (280-06)
  bool holds = true;                  // solver_search_holds (280-06)
  std::string value;                  // solver_search_value (280-06)
  std::string bootstrap;              // solver_search_bootstrap (280-06): "", none, q or value
  // set by validate()
  std::vector<std::int64_t> at_list;  // parsed from `at`
  bool press_best = false;
};

config_t& cfg();

// Called once at the end of sim_t::setup(), before solver_branch::validate(). Returns at once when the search is
// off (after refusing a stray solver_search_* option that was set without it).
void validate( sim_t* sim );

// Called from solver_control::choose()'s in-process arm for every RL-actor decision, right after
// solver_branch::on_decision, once the observation, q, the masks, q_margin and top_q exist and before
// RL_ACTIONS[idx] is read. `obs` is the observation the net just read (the translog's inputs). May change idx
// (a search copy: its candidate button; the parent with solver_search_press=best: the search's pick).
void on_decision( sim_t* sim, player_t* p, std::uint64_t seq, const float* obs, const float* q,
                  const std::uint8_t* mask, const std::uint8_t* select_mask, int legal_count, float q_margin,
                  float top_q, bool exploratory, bool forced, int& idx );

// Called from combat_end() right after solver_branch::on_combat_end(): a copy whose fight ends before its window
// does reports its leaf here and leaves.
void on_combat_end( sim_t* sim );

bool is_child();

// Called from iterate()'s catch block in a copy: writes a FAIL line and _exit(71).
[[noreturn]] void on_child_exception( sim_t* sim, const char* what );

// Called from execute() right after rl_translog::write_footer(): a copy that gets here has no result (FAIL, _exit(72)).
void on_after_footer( sim_t* sim );

// Called from execute() after the success block: a copy that reaches it has no result -- FAIL and _exit(73), so a
// copy can never go on to write the parent's report files.
void on_execute_end( sim_t* sim );

// Parent only, at the end of execute(): the search summary (a last log line and one stdout line).
void write_summary( sim_t* sim );
}  // namespace solver_search
