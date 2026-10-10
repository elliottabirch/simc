// ==========================================================================
// solver_search -- see solver_search.hpp for the options and the idea.
//
// tstl-sylvanas 280-03, TASK 1 SKELETON: options, validation and every refusal, the lifecycle calls and the
// decision-path call. The decision is selected and its candidates are chosen here; the copies, the pass runner
// and the aggregation arrive in the second commit of this plan.
// ==========================================================================
#include "sim/solver_search.hpp"

#include "action/action.hpp"
#include "player/player.hpp"
#include "sim/event.hpp"
#include "sim/proc_rng.hpp"
#include "sim/rl_policy.hpp"
#include "sim/rl_policy_constants.h"
#include "sim/rl_translog.hpp"
#include "sim/sim.hpp"
#include "sim/sim_control.hpp"
#include "sim/solver_branch.hpp"
#include "util/util.hpp"

#include "fmt/format.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if defined( __linux__ )
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace solver_search
{
namespace
{
config_t g_cfg;
bool g_any = false;

constexpr int PASS1_PER_CAND = 8;

struct counters_t
{
  std::uint64_t decisions = 0;
  std::uint64_t near_ties = 0;
  std::uint64_t listed_reached = 0;
  std::uint64_t listed_skipped = 0;
};
counters_t g_count;
std::unique_ptr<std::ofstream> g_log;

// solver_branch.cpp:133-137
std::string jnum( double v )
{
  if ( !std::isfinite( v ) )
    return "null";
  return fmt::format( "{:.17g}", v );
}

// solver_branch.cpp:174-177: the translog row's own `t`
double t_float32( const sim_t* sim )
{
  return static_cast<double>( static_cast<float>( sim->current_time().total_seconds() ) );
}

// Top-K legal indices over `sel` by q descending, ties to the LOWER index (masked_argmax's rule).
// solver_branch.cpp:218-232
std::vector<int> top_k( const float* q, const std::uint8_t* sel, int K )
{
  std::vector<int> legal;
  for ( int i = 0; i < static_cast<int>( RL_ACTION_DIM ); ++i )
    if ( sel[ i ] )
      legal.push_back( i );
  std::stable_sort( legal.begin(), legal.end(), [ q ]( int a, int b ) {
    if ( q[ a ] != q[ b ] )
      return q[ a ] > q[ b ];
    return a < b;
  } );
  if ( static_cast<int>( legal.size() ) > K )
    legal.resize( static_cast<std::size_t>( K ) );
  return legal;
}

[[noreturn]] void refuse( const std::string& msg )
{
  throw sc_invalid_sim_argument( msg );
}

// solver_branch.cpp's parse_teacher_at (771-803): "k1,k2,..." -- every token a plain decimal integer >= 0, strictly
// ascending. Returns an empty string on success, else the reason.
std::string parse_at_list( const std::string& spec, std::vector<std::int64_t>& out )
{
  out.clear();
  if ( spec.empty() )
    return std::string();
  std::stringstream ss( spec );
  std::string tok;
  std::size_t n_tok = 0;
  while ( std::getline( ss, tok, ',' ) )
  {
    ++n_tok;
    if ( tok.empty() )
      return "an empty entry";
    if ( tok.find_first_not_of( "0123456789" ) != std::string::npos )
      return fmt::format( "'{}' is not a non-negative integer", tok );
    if ( tok.size() > 18 )
      return fmt::format( "'{}' is out of range", tok );
    const std::int64_t v = std::strtoll( tok.c_str(), nullptr, 10 );
    if ( !out.empty() && v == out.back() )
      return fmt::format( "{} is listed twice", v );
    if ( !out.empty() && v < out.back() )
      return fmt::format( "{} follows {} (the list must be ascending)", v, out.back() );
    out.push_back( v );
  }
  if ( spec.back() == ',' )
    return "an empty entry";
  if ( n_tok == 0 )
    return "an empty entry";
  return std::string();
}

}  // namespace

config_t& cfg()
{
  return g_cfg;
}

bool is_child()
{
  return false;  // the copies arrive in the second commit of this plan
}

void validate( sim_t* sim )
{
  config_t& c = g_cfg;

  // A solver_search_* option without the search is a mistake, never silently ignored. (The name is looked up in
  // the parsed option list, so even a value equal to the default is caught.)
  if ( !c.search )
  {
    if ( sim->control )
      for ( const option_tuple_t& o : sim->control->options )
        if ( o.name.rfind( "solver_search", 0 ) == 0 && o.name != "solver_search" )
          refuse( fmt::format( "{} is set but solver_search is not 1 (a search option without the search).", o.name ) );
    return;
  }

#if !defined( __linux__ )
  refuse( "solver_search needs a Linux build (fork())." );
#endif

  solver_branch::config_t& sb = solver_branch::cfg();

  // 1. one mode per process: the search excludes every solver_branch mode and the teacher
  if ( sb.teacher )
    refuse( "solver_search cannot be combined with solver_teacher=1 -- one mode per process." );
  if ( sb.branch_at >= 0 )
    refuse( "solver_search cannot be combined with solver_branch_at -- one mode per process." );
  if ( sb.resalt_at_decision >= 0 )
    refuse( "solver_search cannot be combined with per_source_rng_resalt_at_decision -- one mode per process." );
  // The search always cuts exactly: solver_branch_cut is not its option, whatever its value.
  if ( sim->control )
    for ( const option_tuple_t& o : sim->control->options )
      if ( o.name == "solver_branch_cut" )
        refuse( "solver_branch_cut cannot be set with solver_search (the search always cuts its window exactly)." );

  // 2. in-process transport only
  if ( sim->solver_policy_str.empty() || !sim->solver_control_str.empty() )
    refuse( "solver_search requires solver_policy= and no solver_control= (in-process transport only)." );
  // 3. one thread (checked before iterations: the engine clamps threads to the iteration count, so with
  // iterations=1 this is always 1 and only a multi-iteration run can show more than one)
  if ( sim->threads != 1 )
    refuse( fmt::format( "solver_search requires threads=1 (effective threads={}).", sim->threads ) );
  // 4. one fight per process
  if ( sim->iterations != 1 )
    refuse( fmt::format( "solver_search requires iterations=1 (got {}).", sim->iterations ) );
  // 5. per-source dice
  if ( !sim->per_source_rng )
    refuse( "solver_search requires per_source_rng=1." );
  // 6. no profilesets, root sim only
  if ( !sim->profileset_map.empty() )
    refuse( "solver_search cannot be combined with profilesets." );
  if ( sim->parent != nullptr )
    refuse( "solver_search is set on a non-root sim." );

  // 7. study options that would double-write or change the run (the same list solver_branch refuses)
  struct
  {
    bool on;
    const char* opt;
  } const conflicts[] = {
    { !sim->decision_dump_file_str.empty(), "decision_dump" },
    { !sim->rl_rng_record_file_str.empty(), "rl_rng_record" },
    { !sim->rl_rng_replay_file_str.empty(), "rl_rng_replay" },
    { !sim->rl_swing_pin_file_str.empty(), "rl_swing_pin" },
    { !sim->rl_swing_trace_file_str.empty(), "rl_swing_trace" },
    { static_cast<bool>( sim->solver_record_apl_choice ), "solver_record_apl_choice" },
    { !sim->solver_fight_timeline_str.empty(), "solver_fight_timeline" },
    { !sim->rl_forward_probe_str.empty(), "rl_forward_probe" },
    { !sim->rl_buff_ledger_refund_probe_str.empty(), "rl_buff_ledger_refund_probe" },
    { static_cast<bool>( sim->debug_each ), "debug_each" },
    { sim->log != 0, "log=1" },
    { sim->debug != 0, "debug=1" },
  };
  for ( const auto& cf : conflicts )
    if ( cf.on )
      refuse( fmt::format( "solver_search cannot be combined with {}.", cf.opt ) );

  // 8. owner rule D1: no paired futures
  if ( !sb.independent_dice )
    refuse( "solver_branch_independent_dice=0 is refused with solver_search: every copy must roll its own dice "
            "(owner rule D1)." );
  if ( !sb.redeal )
    refuse( "solver_branch_redeal=0 is refused with solver_search: every copy must re-deal the undrawn deck cards "
            "(owner rule D1)." );

  // 9. this module's own values
  if ( c.candidates < 2 || c.candidates > 4 )
    refuse( fmt::format( "solver_search_candidates must be 2..4 (got {}).", c.candidates ) );
  if ( !( c.window > 0.0 && c.window <= 120.0 ) )
    refuse( fmt::format( "solver_search_window must be > 0 and <= 120 seconds (got {}).", c.window ) );
  if ( c.copies < PASS1_PER_CAND * c.candidates )
    refuse( fmt::format( "solver_search_copies={} is below 8 x solver_search_candidates={} = {} (pass 1 alone needs 8 "
                         "copies per candidate).",
                         c.copies, c.candidates, PASS1_PER_CAND * c.candidates ) );
  if ( c.copies > 4096 )
    refuse( fmt::format( "solver_search_copies must be at most 4096 (got {}).", c.copies ) );
  if ( c.jobs < 1 || c.jobs > 12 )
    refuse( fmt::format( "solver_search_jobs must be 1..12 (got {}).", c.jobs ) );
  if ( c.press != "net" && c.press != "best" )
    refuse( fmt::format( "solver_search_press='{}' is not 'net' or 'best'.", c.press ) );
  c.press_best = c.press == "best";
  if ( !std::isfinite( c.margin ) || c.margin < 0.0 )
    refuse( fmt::format( "solver_search_margin must be >= 0 (got {}).", c.margin ) );
  {
    const std::string why = parse_at_list( c.at, c.at_list );
    if ( !why.empty() )
      refuse( fmt::format( "solver_search_at='{}' is not an ascending list of distinct integers >= 0 (k = seq-1): {}.",
                           c.at, why ) );
  }
  if ( c.at_list.empty() && !( c.margin > 0.0 ) )
    refuse( "solver_search needs a decision set: solver_search_at=k1,k2,... or solver_search_margin > 0." );
  if ( !( sb.value_divisor > 0.0 ) )
    refuse( "solver_search requires solver_branch_value_divisor > 0 (divisorByFightShape[shape])." );
  if ( !( sb.gamma > 0.0 && sb.gamma <= 1.0 ) )
    refuse( fmt::format( "solver_search requires solver_branch_gamma in (0, 1] (got {}).", sb.gamma ) );

  // 10. greedy weights only (mirrors solver_branch.cpp's refusal at 952-963)
  {
    const auto& w = sim->solver_policy_weights;
    if ( !w )
      refuse( "solver_search requires loaded solver_policy weights." );
    if ( w->exploration > 0.0f )
      refuse( fmt::format( "solver_search requires greedy weights: the loaded weights carry exploration={}.",
                           w->exploration ) );
    if ( w->has_aim_section && w->aim.exploration > 0.0f )
      refuse( fmt::format( "solver_search requires greedy weights: the loaded aim section carries exploration={}.",
                           w->aim.exploration ) );
  }

  if ( !c.log.empty() )
  {
    g_log = std::make_unique<std::ofstream>( c.log, std::ios::out | std::ios::trunc );
    if ( !g_log->is_open() )
      refuse( fmt::format( "solver_search_log='{}' cannot be opened for writing.", c.log ) );
  }

  g_any = true;
  fmt::print( "[RL_SEARCH] candidates={} window={:.17g} copies={} jobs={} at={} margin={:.17g} press={} expected_crit={} "
              "divisor={:.17g} gamma={:.17g} salt_base={} log={}\n",
              c.candidates, c.window, c.copies, c.jobs, c.at_list.size(), c.margin, c.press, c.expected_crit ? 1 : 0,
              sb.value_divisor, sb.gamma, sb.branch_salt_base, c.log );
  std::fflush( stdout );
}

void on_decision( sim_t* sim, player_t* p, std::uint64_t seq, const float* obs, const float* q,
                  const std::uint8_t* mask, const std::uint8_t* select_mask, int legal_count, float q_margin,
                  float top_q, bool exploratory, bool forced, int& idx )
{
  (void)p; (void)obs; (void)mask; (void)top_q;
  if ( !g_any )
    return;
  config_t& c = g_cfg;
  const std::int64_t k = static_cast<std::int64_t>( seq ) - 1;
  ++g_count.decisions;
  const bool near_tie = legal_count >= 2 && !std::isnan( q_margin ) && q_margin < c.margin;
  const bool listed_mode = !c.at_list.empty();
  if ( near_tie )
    ++g_count.near_ties;
  if ( listed_mode )
  {
    if ( !std::binary_search( c.at_list.begin(), c.at_list.end(), k ) )
      return;
    ++g_count.listed_reached;
  }
  else if ( !near_tie )
  {
    return;
  }
  int n_sel = 0;
  for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
    n_sel += select_mask[ i ] ? 1 : 0;
  const char* skip = legal_count < 2 ? "fewer_than_2_legal"
                     : forced        ? "forced"
                     : exploratory   ? "exploratory"
                     : n_sel < 2     ? "held_or_fewer_than_2_selectable"
                                     : nullptr;
  if ( skip )
  {
    if ( listed_mode )
    {
      ++g_count.listed_skipped;
      if ( g_log )
        *g_log << fmt::format(
            "{{\"seq\":{},\"t\":{},\"press\":{},\"margin\":{},\"legal\":{},\"n_sel\":{},\"skipped\":\"{}\"}}\n", seq,
            jnum( t_float32( sim ) ), idx, jnum( static_cast<double>( q_margin ) ), legal_count, n_sel, skip );
    }
    return;
  }
  const std::vector<int> cands = top_k( q, select_mask, c.candidates );
  if ( cands.empty() || cands[ 0 ] != idx )
    throw sc_runtime_error( fmt::format(
        "solver_search: the net's pick {} is not the best selectable button {} at decision k={} -- refusing to "
        "search from a press the net did not choose",
        idx, cands.empty() ? -1 : cands[ 0 ], k ) );
  // Task 2 adds the batch here.
}

void on_combat_end( sim_t* sim )
{
  (void)sim;
}

void on_child_exception( sim_t* sim, const char* what )
{
  (void)sim;
  (void)what;
  _exit( 71 );
}

void on_after_footer( sim_t* sim )
{
  (void)sim;
}

void on_execute_end( sim_t* sim )
{
  (void)sim;
}

void write_summary( sim_t* sim )
{
  (void)sim;
  if ( !g_any )
    return;
  fmt::print( "[RL_SEARCH_SUMMARY] {{\"summary\":1,\"decisions\":{},\"near_ties\":{}}}\n", g_count.decisions,
              g_count.near_ties );
  std::fflush( stdout );
}
}  // namespace solver_search
