// ==========================================================================
// solver_search -- see solver_search.hpp for the options and the idea.
//
// tstl-sylvanas 280-03 (v2.18 phase 280). Everything the search needs from solver_branch.cpp is COPIED into this
// file's anonymous namespace (jnum, t_float32, rusage_cpu_s, read_vmrss_kb, count_dir_entries, top_k, mean_se,
// mix64, the fd isolation and the exact-cut event), each with a comment naming where it came from at a2db5ab83b.
// solver_branch.cpp and solver_branch.hpp are untouched, so the teacher path is byte for byte what it was.
//
// WINDOW RETURN (copy side). A semi-MDP sum over the RL actor's decisions after k, in the teacher's own form:
//     acc += pow( gamma, last_t - t_k ) * ( ( E_j - last_E ) / divisor )
// at each later decision j, where t is the float32-rounded sim time and E is the running EXPECTED-crit damage
// (p->solver_damage_expected_so_far) when solver_search_expected_crit=1, the realised damage
// (p->solver_damage_so_far) when 0. The window ends at the exact cut event at t_k + W (a real foreground decision
// through player_t::execute_action(), exactly as solver_branch's solver_branch_cut=exact). Beside R the copy also
// reports R_undisc (expected damage at the cut minus at t_k, raw units, undiscounted) and R_real (realised, raw
// units), and boot_q = pow( gamma, T - t_k ) * top_q (recorded; no plan in this phase adds it to the label yet).
// If the fight ends first the last interval runs to the close value, ended = 1 and boot_q = 0.
//
// PIPE PROTOCOL. One pipe per PASS. Each copy writes exactly one line with one write() of at most PIPE_BUF (4096)
// bytes and leaves only through _exit():
//     LEAF <copy> <pass> <cand> <rank> <w> <R> <R_undisc> <R_real> <boot_q> <t_end> <ended> <n_dec> <cut_state>
//          <vmhwm_kb> <cpu_s>
//     FAIL <copy> <code> <msg>
// <copy> is the copy's index within its pass; <cand> the index of its root candidate (0 = the net's own press);
// <rank> and <w> are 0 and 1 until the later plans (second level, chance splits) use them. A copy's value is the
// sum over its leaves of w x R; a copy with a FAIL, a missing or malformed leaf, or a bad exit is dropped whole
// and counted (n_fail). Means and standard errors are taken over COPIES (copies of one search are independent:
// owner rule D1), never over leaves.
//
// PASS RUNNER. NOT solver_branch's run_batch (whose blocking waitpid() can deadlock against a full pipe): the read
// end is non-blocking and 1 MiB, the parent poll()s and drains the pipe and reaps with waitpid( WNOHANG ) in one
// loop, with at most `jobs` copies alive.
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
bool g_any = false;  // set by validate() when the search is on

// The depth-one reading of G280-7: 8 copies per candidate first.
constexpr int PASS1_PER_CAND = 8;
// Linux guarantees a write() of at most PIPE_BUF bytes to a pipe is atomic against other writers.
constexpr std::size_t LINE_MAX_BYTES = 4096;

// ---- copy state ------------------------------------------------------------------------------
struct child_state_t
{
  bool active = false;
  player_t* p = nullptr;
  int pass = 0;
  int copy = 0;
  int cand = 0;
  int button = -1;
  std::uint64_t seq = 0;
  double t_k = 0.0;
  double window = 0.0;
  double divisor = 1.0;
  double gamma = 0.98;
  bool expected = true;
  int fd = -1;
  double E_k = 0.0;       // the scored total (expected or realised) at t_k
  double Eexp_k = 0.0;    // expected-crit total at t_k
  double D_k = 0.0;       // realised total at t_k
  double last_t = 0.0;
  double last_E = 0.0;
  double acc = 0.0;
  int n_dec = 0;
  bool cutting = false;   // set by the cut event right before p->execute_action(); on_decision cuts on it
  int cut_state = -1;     // recorded by the event at T
};
child_state_t g_child;

// ---- parent stream and counters ---------------------------------------------------------------
std::unique_ptr<std::ofstream> g_log;

struct counters_t
{
  std::uint64_t decisions = 0;
  std::uint64_t near_ties = 0;
  std::uint64_t branched = 0;       // searches that produced a pick (not skipped_fail)
  std::uint64_t overrides = 0;      // the pick differed from the net's press
  std::uint64_t skipped_fail = 0;
  std::uint64_t children = 0;       // copies forked
  std::uint64_t children_reaped = 0;
  std::uint64_t child_fail = 0;     // copies dropped whole
  std::uint64_t searches = 0;
  std::uint64_t unknown_lines = 0;  // pipe lines ignored (unknown copy, duplicate, malformed)
  double child_cpu_s = 0.0;
  double fork_cpu_s = 0.0;
  double wall_branch_s = 0.0;
  long rss_kb_first = -1;
  long rss_kb_last = -1;
  long fd_first = -1;
  long fd_last = -1;
  long leaf_vmhwm_kb_max = 0;       // over every search of the run
  long search_vmhwm_kb_max = 0;     // over the copies of the search in progress (reset by search_here)
  std::uint64_t listed_reached = 0;
  std::uint64_t listed_skipped = 0;
};
counters_t g_count;

// ---- small helpers (copied from solver_branch.cpp at a2db5ab83b, see each comment) -----------------
// solver_branch.cpp:133-137
std::string jnum( double v )
{
  if ( !std::isfinite( v ) )
    return "null";
  return fmt::format( "{:.17g}", v );
}

#if defined( __linux__ )
// solver_branch.cpp:139-151
long count_dir_entries( const char* path )
{
  DIR* d = opendir( path );
  if ( !d )
    return -1;
  long n = 0;
  while ( dirent* e = readdir( d ) )
  {
    if ( std::strcmp( e->d_name, "." ) == 0 || std::strcmp( e->d_name, ".." ) == 0 )
      continue;
    ++n;
  }
  closedir( d );
  return n;
}

// solver_branch.cpp:153-164 (VmRSS); the same reader for any /proc/self/status key (VmHWM is new here)
long read_status_kb( const char* key )
{
  std::ifstream f( "/proc/self/status" );
  std::string line;
  const std::size_t klen = std::strlen( key );
  while ( std::getline( f, line ) )
  {
    if ( line.rfind( key, 0 ) == 0 )
      return std::strtol( line.c_str() + klen, nullptr, 10 );
  }
  return -1;
}

long read_vmrss_kb()
{
  return read_status_kb( "VmRSS:" );
}

long read_vmhwm_kb()
{
  return read_status_kb( "VmHWM:" );
}

// solver_branch.cpp:166-172
double rusage_cpu_s( int who )
{
  rusage ru{};
  getrusage( who, &ru );
  return static_cast<double>( ru.ru_utime.tv_sec ) + static_cast<double>( ru.ru_utime.tv_usec ) * 1e-6 +
         static_cast<double>( ru.ru_stime.tv_sec ) + static_cast<double>( ru.ru_stime.tv_usec ) * 1e-6;
}
#endif

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

// mean and SE (sample sd, n-1, over sqrt(n)); the operation order is solver_branch.cpp:275-293's exactly.
void mean_se( const std::vector<double>& x, double& mean, double& se )
{
  const std::size_t n = x.size();
  mean = std::numeric_limits<double>::quiet_NaN();
  se = std::numeric_limits<double>::quiet_NaN();
  if ( n == 0 )
    return;
  double s = 0.0;
  for ( double v : x )
    s += v;
  mean = s / static_cast<double>( n );
  if ( n < 2 )
    return;
  double ss = 0.0;
  for ( double v : x )
    ss += ( v - mean ) * ( v - mean );
  const double var = ss / static_cast<double>( n - 1 );
  se = std::sqrt( var / static_cast<double>( n ) );
}

#if defined( __linux__ )
// splitmix64 finaliser, solver_branch.cpp:310-316
std::uint64_t mix64( std::uint64_t x )
{
  x += 0x9E3779B97F4A7C15ull;
  x = ( x ^ ( x >> 30 ) ) * 0xBF58476D1CE4E5B9ull;
  x = ( x ^ ( x >> 27 ) ) * 0x94D049BB133111EBull;
  return x ^ ( x >> 31 );
}

// The dice key of one copy: a chain of mix64 over (salt base, decision, pass, copy index within the pass), so no
// copy of any search shares a key with any other copy (owner rule D1).
std::uint64_t copy_key( std::uint64_t salt_base, std::uint64_t seq, int pass, int copy )
{
  std::uint64_t k = mix64( salt_base );
  k = mix64( k ^ ( seq + 0xA24BAED4963EE407ull ) );
  k = mix64( k ^ ( static_cast<std::uint64_t>( pass ) * 0x100000001B3ull + 0x9FB21C651E98DF25ull ) );
  k = mix64( k ^ ( static_cast<std::uint64_t>( copy ) + 0xD1B54A32D192ED03ull ) );
  return k != 0 ? k : 0x2545F4914F6CDD1Dull;  // 0 would re-seed to the parent's own stream
}
#endif

[[noreturn]] void refuse( const std::string& msg )
{
  throw sc_invalid_sim_argument( msg );
}

// One write() of one line, then _exit(code). The caller keeps the line within LINE_MAX_BYTES.
// (solver_branch.cpp:179-195's child_write_and_exit)
[[noreturn]] void child_write_and_exit( const std::string& line, int code )
{
#if defined( __linux__ )
  if ( g_child.fd >= 0 )
  {
    const ssize_t w = ::write( g_child.fd, line.data(), line.size() );
    (void)w;
  }
  _exit( code );
#else
  (void)line;
  std::_Exit( code );
#endif
}

std::string make_fail_line( int code, const std::string& msg )
{
  std::string m = msg;
  for ( char& c : m )
    if ( c == ' ' || c == '\n' || c == '\r' || c == '\t' )
      c = '_';
  if ( m.size() > 160 )
    m.resize( 160 );
  if ( m.empty() )
    m = "-";
  return fmt::format( "FAIL {} {} {}\n", g_child.copy, code, m );
}

// A leaf line (this plan: one per copy, rank 0, weight 1). An oversize line is replaced by a FAIL (code 76).
[[noreturn]] void write_leaf_and_exit( double R, double R_undisc, double R_real, double boot_q, double t_end, int ended,
                                       int cut_state )
{
  if ( !std::isfinite( R ) || !std::isfinite( R_undisc ) || !std::isfinite( R_real ) || !std::isfinite( boot_q ) ||
       !std::isfinite( t_end ) )
    child_write_and_exit( make_fail_line( 77, "nonfinite_leaf_value" ), 77 );
#if defined( __linux__ )
  const long hwm = read_vmhwm_kb();
  const double cpu = rusage_cpu_s( RUSAGE_SELF );
#else
  const long hwm = 0;
  const double cpu = 0.0;
#endif
  const std::string line =
      fmt::format( "LEAF {} {} {} {} {:.17g} {:.17g} {:.17g} {:.17g} {:.17g} {:.9g} {} {} {} {} {:.9g}\n", g_child.copy,
                   g_child.pass, g_child.cand, 0, 1.0, R, R_undisc, R_real, boot_q, t_end, ended, g_child.n_dec,
                   cut_state, hwm, cpu );
  if ( line.size() > LINE_MAX_BYTES )
    child_write_and_exit( make_fail_line( 76, "line_too_long" ), 76 );
  child_write_and_exit( line, 0 );
}

#if defined( __linux__ )
// The exact window cut, a copy of solver_branch.cpp's window_cut_event_t (lines 360-394) acting on THIS module's
// copy state. Created by become_search_child() at fork time, at exactly t_k + W. Same-time events run in ascending
// insertion order, so it runs before every event the copy itself schedules for that millisecond.
struct search_cut_event_t final : public event_t
{
  search_cut_event_t( sim_t& s, timespan_t delta ) : event_t( s, delta )
  {
  }
  const char* name() const override
  {
    return "Solver-Search-Window-Cut";
  }
  void execute() override
  {
    player_t* p = g_child.p;
    const timespan_t now = sim().current_time();
    // a. what the actor is doing at T
    g_child.cut_state = p->channeling ? 3 : p->executing ? 2 : p->gcd_ready > now ? 1 : 0;
    // b. a sleeping (dead) actor has no decision to make: loud, counted as a dropped copy
    if ( p->current.sleeping )
      child_write_and_exit( make_fail_line( 75, "sleeping_at_cut" ), 75 );
    // c. the opening of player_ready_event_t::execute() (player.cpp), in effect
    event_t::cancel( p->readying );
    p->current_execute_type = execute_type::FOREGROUND;
    if ( p->queueing )
    {
      event_t::cancel( p->queueing->queue_event );
      p->queueing = nullptr;
    }
    event_t::cancel( p->off_gcd );
    event_t::cancel( p->cast_while_casting_poll_event );
    // d. the production foreground decision path; on_decision() cuts and _exit()s inside it
    g_child.cutting = true;
    p->execute_action();
    // e. never reached when the cut works
    child_write_and_exit( make_fail_line( 74, "cut_not_reached" ), 74 );
  }
};

double scored_total( const player_t* p )
{
  return g_cfg.expected_crit ? p->solver_damage_expected_so_far : p->solver_damage_so_far;
}

// Mirrors solver_branch.cpp's become_child (396-506) step for step, for one search copy.
void become_search_child( sim_t* sim, player_t* p, std::uint64_t seq, int pass, int copy, int cand, int button,
                          std::uint64_t key, double window, int read_fd, int write_fd )
{
  const solver_branch::config_t& sb = solver_branch::cfg();
  g_child.active = true;
  g_child.p = p;
  g_child.pass = pass;
  g_child.copy = copy;
  g_child.cand = cand;
  g_child.button = button;
  g_child.seq = seq;
  g_child.t_k = t_float32( sim );
  g_child.window = window;
  g_child.divisor = sb.value_divisor;
  g_child.gamma = sb.gamma;
  g_child.expected = g_cfg.expected_crit;
  g_child.fd = write_fd;
  g_child.Eexp_k = p->solver_damage_expected_so_far;
  g_child.D_k = p->solver_damage_so_far;
  g_child.E_k = scored_total( p );

  ::close( read_fd );

  const int dn = ::open( "/dev/null", O_WRONLY );
  if ( dn >= 0 )
    ::dup2( dn, 1 );

  // fd isolation, always on for the search (T-280-05): every inherited descriptor except stdin/out/err and the
  // pipe's write end is pointed at /dev/null, so a copy can never write into a parent file.
  if ( dn >= 0 )
  {
    std::vector<int> fds;
    if ( DIR* d = opendir( "/proc/self/fd" ) )
    {
      while ( dirent* e = readdir( d ) )
      {
        if ( e->d_name[ 0 ] == '.' )
          continue;
        fds.push_back( std::atoi( e->d_name ) );
      }
      closedir( d );
    }
    for ( int fd : fds )
    {
      if ( fd <= 2 || fd == write_fd || fd == dn )
        continue;
      // The directory fd itself is already closed; dup2 onto a closed number simply opens it.
      if ( ::fcntl( fd, F_GETFD ) == -1 )
        continue;
      ::dup2( dn, fd );
    }
  }

  // Logical isolation, always on.
  rl_translog::branch_child_disable( sim );
  sim->rl_iteration_out.clear();
  sim->rl_bc_on = false;
  sim->rl_bl_on = false;
  sim->solver_fight_timeline_str.clear();
  (void)g_log.release();  // the parent's log stream: leak, never flush or close it from a copy

  // Owner rule D1: this copy rolls its own dice (the key is distinct for every copy of every search) and its
  // exploration stream is re-seeded from it; the undrawn remainder of every shuffled deck is dealt again from the
  // copy's own, just re-salted, stream. Parent untouched: all of this runs in the copy only.
  sim->resalt_source_rngs( key );
  sim->solver_explore_rng.seed( mix64( key ^ 0xE5910BEull ) );
  for ( player_t* a : sim->actor_list )
  {
    if ( !a )
      continue;
    for ( proc_rng_t* r : a->proc_rng_list )
      if ( auto* deck = dynamic_cast<shuffled_rng_t*>( r ) )
        deck->redeal_undrawn();
  }

  g_child.last_t = g_child.t_k;
  g_child.last_E = g_child.E_k;
  g_child.acc = 0.0;
  g_child.n_dec = 0;
  g_child.cutting = false;
  g_child.cut_state = -1;

  // The exact cut, created here at fork time and before the copy's own press returns (see search_cut_event_t).
  make_event<search_cut_event_t>( *sim, *sim, timespan_t::from_seconds( window ) );
}

// ---- pass runner ---------------------------------------------------------------------------------
struct leaf_t
{
  int rank = 0;
  double w = 1.0;
  double R = 0.0;
  double R_undisc = 0.0;
  double R_real = 0.0;
  double boot_q = 0.0;
  double t_end = 0.0;
  int ended = 0;
  int n_dec = 0;
  int cut_state = -1;
  long vmhwm_kb = 0;
  double cpu_s = 0.0;
};

struct copy_task_t
{
  int cand = 0;    // index into the search's candidate list
  int button = 0;  // the root button this copy presses
};

struct copy_result_t
{
  int cand = 0;
  int button = 0;
  std::vector<leaf_t> leaves;
  bool failed = false;  // a FAIL line, a malformed or duplicate leaf
  std::string fail;
  int exit_code = -1;
  int signal = 0;
  bool exited = false;
  // set by finalise()
  bool ok = false;
  double w_sum = 0.0;
  double value = 0.0;
};

bool parse_double( const std::string& s, double& out )
{
  if ( s.empty() )
    return false;
  char* end = nullptr;
  errno = 0;
  out = std::strtod( s.c_str(), &end );
  return end != s.c_str() && *end == '\0' && std::isfinite( out );
}

bool parse_long( const std::string& s, long& out )
{
  if ( s.empty() )
    return false;
  char* end = nullptr;
  errno = 0;
  out = std::strtol( s.c_str(), &end, 10 );
  return end != s.c_str() && *end == '\0' && errno == 0;
}

// One pipe line into the results. Anything unknown or malformed is counted and ignored (T-280-07), except that a
// malformed or duplicate LEAF for a known copy marks that copy failed so it is dropped whole.
void parse_line( const std::string& line, int pass, std::vector<copy_result_t>& res )
{
  std::vector<std::string> tok;
  {
    std::istringstream ws( line );
    std::string t;
    while ( ws >> t )
      tok.push_back( t );
  }
  if ( tok.empty() )
    return;
  long copy = -1;
  if ( tok.size() < 2 || !parse_long( tok[ 1 ], copy ) || copy < 0 || copy >= static_cast<long>( res.size() ) )
  {
    ++g_count.unknown_lines;
    return;
  }
  copy_result_t& r = res[ static_cast<std::size_t>( copy ) ];
  if ( tok[ 0 ] == "FAIL" )
  {
    r.failed = true;
    r.fail = tok.size() >= 4 ? tok[ 2 ] + " " + tok[ 3 ] : "malformed_fail";
    return;
  }
  if ( tok[ 0 ] != "LEAF" )
  {
    ++g_count.unknown_lines;
    return;
  }
  // LEAF <copy> <pass> <cand> <rank> <w> <R> <R_undisc> <R_real> <boot_q> <t_end> <ended> <n_dec> <cut_state>
  //      <vmhwm_kb> <cpu_s>
  leaf_t lf;
  long fpass = 0, fcand = 0, frank = 0, fended = 0, fndec = 0, fcut = 0, fhwm = 0;
  bool good = tok.size() == 16 && parse_long( tok[ 2 ], fpass ) && parse_long( tok[ 3 ], fcand ) &&
              parse_long( tok[ 4 ], frank ) && parse_double( tok[ 5 ], lf.w ) && parse_double( tok[ 6 ], lf.R ) &&
              parse_double( tok[ 7 ], lf.R_undisc ) && parse_double( tok[ 8 ], lf.R_real ) &&
              parse_double( tok[ 9 ], lf.boot_q ) && parse_double( tok[ 10 ], lf.t_end ) &&
              parse_long( tok[ 11 ], fended ) && parse_long( tok[ 12 ], fndec ) && parse_long( tok[ 13 ], fcut ) &&
              parse_long( tok[ 14 ], fhwm ) && parse_double( tok[ 15 ], lf.cpu_s );
  good = good && fpass == pass && fcand == r.cand && frank >= 0 && lf.w > 0.0 && lf.w <= 1.0 + 1e-9 &&
         ( fended == 0 || fended == 1 ) && fndec >= 0 && fcut >= -1 && fcut <= 4 && fhwm >= 0;
  if ( !good )
  {
    r.failed = true;
    r.fail = "bad_leaf_line";
    ++g_count.unknown_lines;
    return;
  }
  lf.rank = static_cast<int>( frank );
  lf.ended = static_cast<int>( fended );
  lf.n_dec = static_cast<int>( fndec );
  lf.cut_state = static_cast<int>( fcut );
  lf.vmhwm_kb = fhwm;
  // This plan's copies have exactly one leaf. (Plans 04 and 06 let a copy carry several; the weights then sum to 1.)
  r.leaves.push_back( lf );
}

// A copy is usable when it has no FAIL / bad line, at least one leaf, a clean exit, and its leaf weights sum to 1.
void finalise( copy_result_t& r )
{
  r.w_sum = 0.0;
  r.value = 0.0;
  for ( const leaf_t& lf : r.leaves )
  {
    r.w_sum += lf.w;
    r.value += lf.w * lf.R;
  }
  r.ok = !r.failed && !r.leaves.empty() && r.exited && r.exit_code == 0 && r.signal == 0 &&
         std::fabs( r.w_sum - 1.0 ) < 1e-9;
  if ( !r.ok && r.fail.empty() )
    r.fail = fmt::format( "no-line exit={} signal={} leaves={} w_sum={}", r.exit_code, r.signal, r.leaves.size(),
                          r.w_sum );
}

// Runs one pass: forks one copy per task, at most `jobs` alive at once, and reads every line to end of file.
// Returns true IN A COPY (out_button = its button); false in the parent, with `res` filled.
bool run_pass( sim_t* sim, player_t* p, std::uint64_t seq, int pass, const std::vector<copy_task_t>& tasks,
               std::vector<copy_result_t>& res, int& out_button )
{
  const config_t& c = g_cfg;
  const solver_branch::config_t& sb = solver_branch::cfg();

  // 1. single-thread guard (fork() from a multi-threaded process is not safe)
  const long n_threads = count_dir_entries( "/proc/self/task" );
  if ( n_threads != 1 )
    throw sc_runtime_error( fmt::format( "solver_search: fork() refused -- process has {} threads", n_threads ) );

  // 2. pre-fork flush: stdio and this module's own log only. NO translog/zstd call.
  std::fflush( nullptr );
  std::cout.flush();
  std::cerr.flush();
  std::clog.flush();
  if ( g_log )
    g_log->flush();

  if ( g_count.fd_first < 0 )
  {
    g_count.rss_kb_first = read_vmrss_kb();
    g_count.fd_first = count_dir_entries( "/proc/self/fd" );
  }

  // 3. the pipe: non-blocking read end, 1 MiB (the box's pipe-max-size) so a full pass of lines never fills it
  int fds[ 2 ];
  if ( ::pipe2( fds, O_CLOEXEC ) != 0 )
    throw sc_runtime_error( fmt::format( "solver_search: pipe2() failed: {}", std::strerror( errno ) ) );
  ::fcntl( fds[ 0 ], F_SETFL, ::fcntl( fds[ 0 ], F_GETFL ) | O_NONBLOCK );
  (void)::fcntl( fds[ 0 ], F_SETPIPE_SZ, 1 << 20 );  // best effort; the poll-driven drain is safe without it

  res.assign( tasks.size(), copy_result_t{} );
  for ( std::size_t i = 0; i < tasks.size(); ++i )
  {
    res[ i ].cand = tasks[ i ].cand;
    res[ i ].button = tasks[ i ].button;
  }

  std::string buf;
  bool eof = false;
  auto drain = [ & ]() {
    char tmp[ 4096 ];
    for ( ;; )
    {
      const ssize_t n = ::read( fds[ 0 ], tmp, sizeof( tmp ) );
      if ( n > 0 )
        buf.append( tmp, static_cast<std::size_t>( n ) );
      else if ( n == 0 )
      {
        eof = true;
        break;
      }
      else
        break;  // EAGAIN (nothing now) or EINTR (try again on the next turn)
    }
  };

  std::map<pid_t, std::size_t> pid_slot;
  int running = 0;
  std::size_t next = 0;
  bool write_closed = false;
  auto reap = [ & ]() {
    for ( ;; )
    {
      int st = 0;
      const pid_t w = ::waitpid( -1, &st, WNOHANG );
      if ( w == 0 )
        break;
      if ( w < 0 )
      {
        if ( errno == EINTR )
          continue;
        if ( errno == ECHILD && running == 0 )
          break;
        throw sc_runtime_error( fmt::format( "solver_search: waitpid() failed: {}", std::strerror( errno ) ) );
      }
      auto it = pid_slot.find( w );
      if ( it == pid_slot.end() )
        continue;  // not one of this pass's copies
      copy_result_t& r = res[ it->second ];
      r.exited = true;
      if ( WIFEXITED( st ) )
        r.exit_code = WEXITSTATUS( st );
      else if ( WIFSIGNALED( st ) )
        r.signal = WTERMSIG( st );
      pid_slot.erase( it );
      --running;
      ++g_count.children_reaped;
    }
  };

  // 4. fork (at most `jobs` alive) and read, in one poll-driven loop
  for ( ;; )
  {
    while ( next < tasks.size() && running < c.jobs )
    {
      const pid_t pid = ::fork();
      if ( pid < 0 )
        throw sc_runtime_error( fmt::format( "solver_search: fork() failed: {}", std::strerror( errno ) ) );
      if ( pid == 0 )
      {
        const std::uint64_t key = copy_key( sb.branch_salt_base, seq, pass, static_cast<int>( next ) );
        become_search_child( sim, p, seq, pass, static_cast<int>( next ), tasks[ next ].cand, tasks[ next ].button,
                             key, c.window, fds[ 0 ], fds[ 1 ] );
        out_button = tasks[ next ].button;
        return true;
      }
      pid_slot[ pid ] = next;
      ++running;
      ++next;
      ++g_count.children;
    }
    if ( next >= tasks.size() && !write_closed )
    {
      // every copy is forked: close the parent's write end so end of file means every writer is gone
      ::close( fds[ 1 ] );
      write_closed = true;
    }
    if ( !eof )
    {
      pollfd pfd{ fds[ 0 ], POLLIN, 0 };
      (void)::poll( &pfd, 1, running > 0 ? 2 : 0 );
      drain();
    }
    reap();
    if ( write_closed && running == 0 && eof )
      break;
  }
  ::close( fds[ 0 ] );

  // 5. match lines to copies, finalise
  {
    std::istringstream ls( buf );
    std::string line;
    while ( std::getline( ls, line ) )
      parse_line( line, pass, res );
  }
  for ( auto& r : res )
  {
    finalise( r );
    if ( !r.ok )
      ++g_count.child_fail;
    for ( const leaf_t& lf : r.leaves )
    {
      g_count.leaf_vmhwm_kb_max = std::max( g_count.leaf_vmhwm_kb_max, lf.vmhwm_kb );
      g_count.search_vmhwm_kb_max = std::max( g_count.search_vmhwm_kb_max, lf.vmhwm_kb );
    }
  }
  return false;
}
#endif  // __linux__

// ---- depth-one search at one decision ---------------------------------------------------------------
struct cell_t
{
  int button = -1;
  int rank_in_q = 0;
  int n_assigned = 0;
  int n_ok = 0;
  int n_fail = 0;
  double w_sum = 0.0;
  std::vector<double> vals;  // per-COPY values, pass 1 then pass 2, in copy order
  double pass1_mean = std::numeric_limits<double>::quiet_NaN();
  double mean = std::numeric_limits<double>::quiet_NaN();
  double se = std::numeric_limits<double>::quiet_NaN();
  double max_t_end_span = 0.0;
};

void add_pass_to_cells( const std::vector<copy_result_t>& res, std::vector<cell_t>& cells )
{
  for ( const copy_result_t& r : res )
  {
    cell_t& c = cells[ static_cast<std::size_t>( r.cand ) ];
    ++c.n_assigned;
    if ( r.ok )
    {
      ++c.n_ok;
      c.w_sum += r.w_sum;
      c.vals.push_back( r.value );
    }
    else
    {
      ++c.n_fail;
    }
  }
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

// Runs the whole search at one decision. Returns true IN A COPY (idx = the copy's candidate button).
bool search_here( sim_t* sim, player_t* p, std::uint64_t seq, const float* q, const std::vector<int>& cands, int& idx,
                  bool listed )
{
#if defined( __linux__ )
  const config_t& c = g_cfg;
  const std::int64_t k = static_cast<std::int64_t>( seq ) - 1;
  const int C = static_cast<int>( cands.size() );
  const int K = c.copies;
  const int net = idx;

  const double self0 = rusage_cpu_s( RUSAGE_SELF );
  const double kids0 = rusage_cpu_s( RUSAGE_CHILDREN );
  const auto wall0 = std::chrono::steady_clock::now();
  const double t_k = t_float32( sim );
  g_count.search_vmhwm_kb_max = 0;

  std::vector<cell_t> cells( static_cast<std::size_t>( C ) );
  for ( int i = 0; i < C; ++i )
  {
    cells[ static_cast<std::size_t>( i ) ].button = cands[ static_cast<std::size_t>( i ) ];
    cells[ static_cast<std::size_t>( i ) ].rank_in_q = i;
  }

  // ---- pass 1: 8 copies per candidate (candidate-major) ----
  std::vector<copy_task_t> t1;
  for ( int i = 0; i < C; ++i )
    for ( int j = 0; j < PASS1_PER_CAND; ++j )
      t1.push_back( copy_task_t{ i, cands[ static_cast<std::size_t>( i ) ] } );
  std::vector<copy_result_t> r1;
  int child_button = -1;
  if ( run_pass( sim, p, seq, 1, t1, r1, child_button ) )
  {
    idx = child_button;
    return true;
  }
  add_pass_to_cells( r1, cells );
  for ( cell_t& cl : cells )
  {
    double se_unused = 0.0;
    mean_se( cl.vals, cl.pass1_mean, se_unused );
  }

  // ---- halving: drop the worst candidate by its pass-1 mean (a tie drops the lower-ranked one) ----
  const int R2 = K - PASS1_PER_CAND * C;
  int dropped = -1;
  std::vector<int> order;  // candidate indices, best pass-1 mean first (ties: higher rank first)
  for ( int i = 0; i < C; ++i )
    order.push_back( i );
  const double lowest = -std::numeric_limits<double>::infinity();
  auto p1 = [ & ]( int i ) {
    const double m = cells[ static_cast<std::size_t>( i ) ].pass1_mean;
    return std::isnan( m ) ? lowest : m;
  };
  std::stable_sort( order.begin(), order.end(), [ & ]( int a, int b ) {
    if ( p1( a ) != p1( b ) )
      return p1( a ) > p1( b );
    return a < b;
  } );
  std::vector<int> survivors = order;
  if ( R2 > 0 )
  {
    dropped = order.back();  // lowest mean; on a tie the stable order put the lower-ranked (larger index) last
    survivors.pop_back();
  }

  // ---- pass 2: the remaining copies, round-robin over the survivors, best pass-1 mean first ----
  if ( R2 > 0 )
  {
    std::vector<copy_task_t> t2;
    for ( int j = 0; j < R2; ++j )
    {
      const int cand = survivors[ static_cast<std::size_t>( j ) % survivors.size() ];
      t2.push_back( copy_task_t{ cand, cands[ static_cast<std::size_t>( cand ) ] } );
    }
    std::vector<copy_result_t> r2;
    if ( run_pass( sim, p, seq, 2, t2, r2, child_button ) )
    {
      idx = child_button;
      return true;
    }
    add_pass_to_cells( r2, cells );
  }

  // ---- per-candidate cells over COPIES (never over leaves) ----
  for ( cell_t& cl : cells )
    mean_se( cl.vals, cl.mean, cl.se );

  // ---- the fail rule: a candidate with fewer than half its copies usable marks the search skipped_fail ----
  bool fail = false;
  for ( const cell_t& cl : cells )
    if ( cl.n_ok < ( cl.n_assigned + 1 ) / 2 )
      fail = true;

  // ---- pick: the best cell mean among the survivors (strict greater: an exact tie stays with the higher rank) ----
  int pick_i = 0;
  {
    bool first = true;
    for ( int i = 0; i < C; ++i )
    {
      if ( i == dropped )
        continue;
      if ( first || cells[ static_cast<std::size_t>( i ) ].mean > cells[ static_cast<std::size_t>( pick_i ) ].mean )
        pick_i = i;
      first = false;
    }
  }
  // runner-up: the best cell mean among every OTHER candidate (the dropped one included); ties to the higher rank
  int second_i = -1;
  for ( int i = 0; i < C; ++i )
  {
    if ( i == pick_i )
      continue;
    if ( second_i < 0 || cells[ static_cast<std::size_t>( i ) ].mean > cells[ static_cast<std::size_t>( second_i ) ].mean )
      second_i = i;
  }
  double margin = std::numeric_limits<double>::quiet_NaN();
  double margin_se = std::numeric_limits<double>::quiet_NaN();
  if ( !fail && second_i >= 0 )
  {
    const cell_t& a = cells[ static_cast<std::size_t>( pick_i ) ];
    const cell_t& b = cells[ static_cast<std::size_t>( second_i ) ];
    margin = a.mean - b.mean;
    if ( std::isfinite( a.se ) && std::isfinite( b.se ) )
      margin_se = std::sqrt( a.se * a.se + b.se * b.se );
  }

  int pick = net;
  if ( fail )
  {
    ++g_count.skipped_fail;
  }
  else
  {
    pick = cands[ static_cast<std::size_t>( pick_i ) ];
    ++g_count.branched;
    if ( pick != net )
      ++g_count.overrides;  // press=net: the override the search WOULD have made
    if ( c.press_best )
      idx = pick;
  }
  ++g_count.searches;

  // ---- clocks ----
  const double fork_cpu = rusage_cpu_s( RUSAGE_SELF ) - self0;
  const double child_cpu = rusage_cpu_s( RUSAGE_CHILDREN ) - kids0;
  const double wall = std::chrono::duration<double>( std::chrono::steady_clock::now() - wall0 ).count();
  g_count.child_cpu_s += child_cpu;
  g_count.fork_cpu_s += fork_cpu;
  g_count.wall_branch_s += wall;
  g_count.rss_kb_last = read_vmrss_kb();
  g_count.fd_last = count_dir_entries( "/proc/self/fd" );

  // ---- log ----
  if ( g_log )
  {
    std::string jc;
    int n_total = 0;
    for ( std::size_t i = 0; i < cells.size(); ++i )
    {
      const cell_t& cl = cells[ i ];
      n_total += cl.n_assigned;
      jc += fmt::format(
          "{}{{\"button\":{},\"rank_in_q\":{},\"q\":{},\"copies\":{},\"n_ok\":{},\"n_fail\":{},\"w_sum\":{},"
          "\"mean\":{},\"se\":{},\"pass1_mean\":{}}}",
          i ? "," : "", cl.button, cl.rank_in_q, jnum( static_cast<double>( q[ cl.button ] ) ), cl.n_assigned, cl.n_ok,
          cl.n_fail, jnum( cl.w_sum ), jnum( cl.mean ), jnum( cl.se ), jnum( cl.pass1_mean ) );
    }
    const long hwm_max = g_count.search_vmhwm_kb_max;
    *g_log << fmt::format(
        "{{\"seq\":{},\"k\":{},\"t\":{},\"net_press\":{},\"pick\":{},\"press\":{},\"listed\":{},\"cands\":[{}],"
        "\"dropped\":{},\"margin\":{},\"margin_se\":{},\"copies\":{},\"skipped_fail\":{},\"cpu_s\":{},"
        "\"fork_cpu_s\":{},\"child_cpu_s\":{},\"wall_s\":{},\"leaf_vmhwm_kb_max\":{}}}\n",
        seq, k, jnum( t_k ), net, pick, idx, listed ? 1 : 0, jc, dropped >= 0 ? cells[ static_cast<std::size_t>( dropped ) ].button : -1,
        jnum( margin ), jnum( margin_se ), n_total, fail ? 1 : 0, jnum( fork_cpu + child_cpu ), jnum( fork_cpu ),
        jnum( child_cpu ), jnum( wall ), hwm_max );
    g_log->flush();
  }
  return false;
#else
  (void)sim; (void)p; (void)seq; (void)q; (void)cands; (void)idx; (void)listed;
  throw sc_runtime_error( "solver_search: fork() needs a Linux build" );
#endif
}
}  // namespace

config_t& cfg()
{
  return g_cfg;
}

bool is_child()
{
  return g_child.active;
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
  if ( !g_any )
    return;
  (void)obs;
  (void)mask;

  // 1. a copy: window accounting, and the exact cut
  if ( g_child.active )
  {
    const double t = t_float32( sim );
    const double E = scored_total( p );
    g_child.acc += std::pow( g_child.gamma, g_child.last_t - g_child.t_k ) * ( ( E - g_child.last_E ) / g_child.divisor );
    ++g_child.n_dec;
    // Only the search_cut_event_t ends the window (t is then T, in float32).
    if ( g_child.cutting )
    {
      const double boot_q = std::pow( g_child.gamma, t - g_child.t_k ) * static_cast<double>( top_q );
      write_leaf_and_exit( g_child.acc, p->solver_damage_expected_so_far - g_child.Eexp_k,
                           p->solver_damage_so_far - g_child.D_k, boot_q, t, 0, g_child.cut_state );
    }
    g_child.last_t = t;
    g_child.last_E = E;
    return;
  }

  // 2. the parent
  config_t& c = g_cfg;
  const std::int64_t k = static_cast<std::int64_t>( seq ) - 1;
  ++g_count.decisions;
  const bool near_tie = legal_count >= 2 && !std::isnan( q_margin ) && q_margin < c.margin;
  const bool listed_mode = !c.at_list.empty();
  // near ties are counted whenever a margin is given (the search is by `at` when that is, by near tie otherwise)
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
      // A listed decision the search will not search is logged by name, with no stats.
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

  search_here( sim, p, seq, q, cands, idx, listed_mode );
}

void on_combat_end( sim_t* sim )
{
  if ( !g_child.active )
    return;
  player_t* p = g_child.p;
  const double E_end = scored_total( p );
  const double t_end = t_float32( sim );
  g_child.acc += std::pow( g_child.gamma, g_child.last_t - g_child.t_k ) * ( ( E_end - g_child.last_E ) / g_child.divisor );
  // the fight ended before the window did: ended = 1, no bootstrap, cut_state 4
  write_leaf_and_exit( g_child.acc, p->solver_damage_expected_so_far - g_child.Eexp_k,
                       p->solver_damage_so_far - g_child.D_k, 0.0, t_end, 1, 4 );
}

void on_child_exception( sim_t* sim, const char* what )
{
  (void)sim;
  child_write_and_exit( make_fail_line( 71, what ? what : "" ), 71 );
}

void on_after_footer( sim_t* sim )
{
  (void)sim;
  if ( !g_child.active )
    return;
  child_write_and_exit( make_fail_line( 72, "copy reached the footer without a result line" ), 72 );
}

void on_execute_end( sim_t* sim )
{
  (void)sim;
  if ( !g_child.active )
    return;
  child_write_and_exit( make_fail_line( 73, "copy reached the end of execute() without a result" ), 73 );
}

void write_summary( sim_t* sim )
{
  (void)sim;
  const config_t& c = g_cfg;
  if ( !g_any || g_child.active )
    return;
  const solver_branch::config_t& sb = solver_branch::cfg();
#if defined( __linux__ )
  const double self_cpu = rusage_cpu_s( RUSAGE_SELF );
  const double kids_cpu = rusage_cpu_s( RUSAGE_CHILDREN );
#else
  const double self_cpu = 0.0, kids_cpu = 0.0;
#endif
  // The ten counters the frozen teacher reader knows, then this module's own, then a config object of NUMBERS only
  // (the depth, second, splits, holds and bootstrap keys are 1, 0, 0, 0, 0 in this plan; later plans fill theirs).
  const std::string js = fmt::format(
      "{{\"summary\":1,\"decisions\":{},\"near_ties\":{},\"branched\":{},\"overrides\":{},\"skipped_fail\":{},"
      "\"children\":{},\"child_fail\":{},\"child_cpu_s\":{},\"fork_cpu_s\":{},\"wall_branch_s\":{},"
      "\"children_reaped\":{},\"searches\":{},\"unknown_lines\":{},\"self_cpu_s\":{},\"children_cpu_total_s\":{},"
      "\"rss_kb_first\":{},\"rss_kb_last\":{},\"fd_first\":{},\"fd_last\":{},\"leaf_vmhwm_kb_max\":{},"
      "\"listed\":{},\"listed_reached\":{},\"listed_skipped\":{},"
      "\"config\":{{\"margin\":{},\"copies\":{},\"window\":{},\"candidates\":{},\"depth\":1,\"second\":0,\"splits\":0,"
      "\"holds\":0,\"bootstrap\":0,\"gamma\":{},\"jobs\":{},\"salt_base\":{},\"press_best\":{},\"expected_crit\":{},"
      "\"divisor\":{}}}}}",
      g_count.decisions, g_count.near_ties, g_count.branched, g_count.overrides, g_count.skipped_fail, g_count.children,
      g_count.child_fail, jnum( g_count.child_cpu_s ), jnum( g_count.fork_cpu_s ), jnum( g_count.wall_branch_s ),
      g_count.children_reaped, g_count.searches, g_count.unknown_lines, jnum( self_cpu ), jnum( kids_cpu ),
      g_count.rss_kb_first, g_count.rss_kb_last, g_count.fd_first, g_count.fd_last, g_count.leaf_vmhwm_kb_max,
      c.at_list.size(), g_count.listed_reached, g_count.listed_skipped, jnum( c.margin ), c.copies, jnum( c.window ),
      c.candidates, jnum( sb.gamma ), c.jobs, sb.branch_salt_base, c.press_best ? 1 : 0, c.expected_crit ? 1 : 0,
      jnum( sb.value_divisor ) );
  if ( g_log )
  {
    *g_log << js << "\n";
    g_log->flush();
  }
  fmt::print( "[RL_SEARCH_SUMMARY] {}\n", js );
  std::fflush( stdout );
}
}  // namespace solver_search
