// ==========================================================================
// solver_branch -- decision-anchored re-salt, fork() branching at a decision,
// and the branching teacher (tstl-sylvanas 261005-branch-teacher).
//
// Three mutually exclusive modes, all off by default (zero cost and byte-identical
// behaviour when none is set -- every hook returns on one bool test):
//
//   per_source_rng_resalt_at_decision=k   re-salt every per-source stream at decision k
//                                          (seq-1 == k), after the net's inputs, mask and
//                                          values exist and before the press. The clock
//                                          form per_source_rng_resalt_at= is unchanged.
//   solver_branch_at=k                     at decision k fork one child per (button, salt);
//                                          each child re-salts with its salt, presses its
//                                          button, plays on and reports ONE pipe line.
//   solver_teacher=1                       at every near tie (legal top-two gap < margin)
//                                          branch the top-K buttons, press the best mean.
//
// Label mode (261005-tch), two teacher-only options, both off by default (today's teacher):
//   solver_teacher_at=k1,k2,...            branch at exactly these decisions (k = seq-1; ascending,
//                                          distinct, >= 0), whatever the margin, and nowhere else. A
//                                          listed k the teacher will not branch writes a `skipped` line.
//   solver_teacher_press=best|net          best (default) presses the best mean; net never changes the
//                                          press, so the parent plays the net's own fight.
//
// Window cut (261005-tch2), branch and teacher, off by default (today's cut):
//   solver_branch_cut=decision|exact       decision (default) ends a child's window at its first decision at or
//                                          after t_k + W (today, byte for byte). exact schedules a window-end
//                                          event at exactly t_k + W: there the child makes a real foreground
//                                          decision through player_t::execute_action() (the net's value at T,
//                                          whatever the actor is doing), writes its line and exits.
//
// The config is module-level (cfg()): every mode refuses threads != 1, profilesets and
// iterations != 1, so there is exactly one sim per process. sim.hpp is deliberately not
// edited. See solver_branch.cpp for the fork/pipe protocol and the window return.
// ==========================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct sim_t;
struct player_t;

namespace solver_branch
{
struct config_t
{
  // Decision-anchored re-salt (salt = sim->per_source_rng_salt).
  int resalt_at_decision = -1;

  // Branch at one decision.
  int branch_at = -1;
  std::string branch_buttons;        // "a,b,c" or "top:K"
  int branch_n = 16;                 // salts per button
  std::uint64_t branch_salt_base = 1;
  double branch_window = 30.0;       // seconds after t_k; 0 = play to fight end
  std::string branch_translog_dir;   // each child writes its own full translog here
  std::string branch_out;            // JSONL: per child, per button, per batch

  // Shared by branch and teacher.
  int branch_jobs = 1;               // concurrent children per parent
  double value_divisor = 0.0;        // divisorByFightShape[shape]; required > 0
  double gamma = 0.98;               // per-second discount
  bool child_fd_isolation = true;    // point every inherited fd at /dev/null in the child

  // Teacher.
  bool teacher = false;
  double teacher_margin = 0.25;
  int teacher_n = 16;
  double teacher_window = 30.0;
  int teacher_topk = 2;
  bool teacher_bootstrap = true;
  std::string teacher_log;

  // Teacher label mode (261005-tch).
  std::string teacher_at;                    // "k1,k2,..." ; empty = every near tie (today)
  std::string teacher_press = "best";        // "best" (today) | "net"
  std::vector<std::int64_t> teacher_at_list;  // parsed from teacher_at by validate()
  bool teacher_press_net = false;            // set by validate()

  // Window cut (261005-tch2).
  std::string branch_cut = "decision";       // "decision" (today) | "exact"
  bool cut_exact = false;                    // set by validate()
};

config_t& cfg();

// Called once at the end of sim_t::setup(). Returns at once when no mode is on.
void validate( sim_t* sim );

// Called from solver_control::choose()'s in-process arm for every RL-actor decision, after
// the q values, q_margin and top_q exist and before RL_ACTIONS[idx] is read. May change idx
// (branch child: its button; teacher parent: the teacher's pick).
void on_decision( sim_t* sim, player_t* p, std::uint64_t seq, const float* q, const std::uint8_t* mask,
                  const std::uint8_t* select_mask, int legal_count, float q_margin, float top_q,
                  bool exploratory, bool forced, int& idx );

// Called from combat_end() right after rl_translog::record_close().
void on_combat_end( sim_t* sim );

bool is_child();

// Called from iterate()'s catch block in a child: writes a FAIL line and _exit(71).
[[noreturn]] void on_child_exception( sim_t* sim, const char* what );

// Called from execute() right after rl_translog::write_footer(): a translog-mode child writes
// its line and _exit(0) here.
void on_after_footer( sim_t* sim );

// Called from execute() after the success block: a child that reaches it has no result
// (the run failed without an exception reaching iterate()) -- FAIL and _exit(73), so a child
// can never go on to write the parent's report files.
void on_execute_end( sim_t* sim );

// Parent only, at the end of execute(): the teacher summary (sidecar line + stdout line).
void write_summary( sim_t* sim );
}  // namespace solver_branch
