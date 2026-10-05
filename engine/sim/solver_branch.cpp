// ==========================================================================
// solver_branch -- see solver_branch.hpp for the three modes.
//
// WINDOW RETURN (child side). A semi-MDP sum over the RL actor's decisions after k. At each
// later decision j:
//     acc += pow(gamma, last_t - t_k) * ((D_j - last_D) / divisor)
// where t is the float32-rounded sim time (exactly the translog row's `t`), D is
// p->solver_damage_so_far (exactly the row's `damage`, the registry's boundaryDamageField),
// divisor is divisorByFightShape[shape] and gamma the per-second discount. The window ends at
// the first decision m with t_m >= t_k + W; there boot = pow(gamma, t_m - t_k) * top_q_m is
// reported as a SEPARATE field (never added into R). If the fight ends first, the last interval
// runs to the close value (solver_damage_so_far at record_close), boot = 0, ended = 1.
//
// FORK PROTOCOL. The parent (single-threaded: /proc/self/task is checked before every batch)
// flushes stdio and its own sidecars -- NEVER the translog zstd stream or any translog stream,
// whose bytes must stay exactly those of an unbranched run -- then forks one child per
// (button, salt), button-major and salt-ascending, at most `jobs` alive at once. Each child
// writes exactly ONE line into a pipe with one write() and leaves only through _exit(), so no
// destructor or atexit handler can flush an inherited stream into a parent file:
//     OK <button> <salt> <R> <boot> <t_end> <ended 0|1> <Dwin> <n_dec>
//     FAIL <button> <salt> <code> <msg>
// ==========================================================================
#include "sim/solver_branch.hpp"

#include "player/player.hpp"
#include "sim/rl_policy.hpp"
#include "sim/rl_policy_constants.h"
#include "sim/rl_translog.hpp"
#include "sim/sim.hpp"
#include "util/util.hpp"

#include "fmt/format.h"

#include <algorithm>
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
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace solver_branch
{
namespace
{
config_t g_cfg;
bool g_any = false;  // set by validate() when one of the three modes is on

// ---- child state ---------------------------------------------------------------------------
struct child_state_t
{
  bool active = false;
  player_t* p = nullptr;
  int button = -1;
  std::uint64_t salt = 0;
  std::uint64_t k = 0;
  double t_k = 0.0;
  double D_k = 0.0;
  double window = 0.0;
  double divisor = 1.0;
  double gamma = 0.98;
  int fd = -1;
  bool translog_mode = false;
  double last_t = 0.0;
  double last_D = 0.0;
  double acc = 0.0;
  int n_dec = 0;
  bool have_line = false;
  std::string line;
};
child_state_t g_child;

// ---- parent streams and counters ----------------------------------------------------------
std::unique_ptr<std::ofstream> g_branch_out;
std::unique_ptr<std::ofstream> g_teacher_log;

struct counters_t
{
  std::uint64_t decisions = 0;
  std::uint64_t near_ties = 0;
  std::uint64_t branched = 0;
  std::uint64_t overrides = 0;
  std::uint64_t skipped_fail = 0;
  std::uint64_t children = 0;
  std::uint64_t children_reaped = 0;
  std::uint64_t child_fail = 0;
  double child_cpu_s = 0.0;
  double fork_cpu_s = 0.0;
  double wall_branch_s = 0.0;
  long rss_kb_first = -1;
  long rss_kb_last = -1;
  long fd_first = -1;
  long fd_last = -1;
};
counters_t g_count;

// ---- small helpers -----------------------------------------------------------------------
std::string jnum( double v )
{
  if ( !std::isfinite( v ) )
    return "null";
  return fmt::format( "{:.17g}", v );
}

#if defined( __linux__ )
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

long read_vmrss_kb()
{
  std::ifstream f( "/proc/self/status" );
  std::string line;
  while ( std::getline( f, line ) )
  {
    if ( line.rfind( "VmRSS:", 0 ) == 0 )
      return std::strtol( line.c_str() + 6, nullptr, 10 );
  }
  return -1;
}

double rusage_cpu_s( int who )
{
  rusage ru{};
  getrusage( who, &ru );
  return static_cast<double>( ru.ru_utime.tv_sec ) + static_cast<double>( ru.ru_utime.tv_usec ) * 1e-6 +
         static_cast<double>( ru.ru_stime.tv_sec ) + static_cast<double>( ru.ru_stime.tv_usec ) * 1e-6;
}
#endif

double t_float32( const sim_t* sim )
{
  return static_cast<double>( static_cast<float>( sim->current_time().total_seconds() ) );
}

// One write() of one line, then _exit(code). The line is bounded well under PIPE_BUF.
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

// R, boot and Dwin in %.17g (exact double round trip); t_end in %.9g (a float32 value).
std::string make_ok_line( double R, double boot, double t_end, int ended, double dwin, int n_dec )
{
  return fmt::format( "OK {} {} {:.17g} {:.17g} {:.9g} {} {:.17g} {}\n", g_child.button, g_child.salt, R, boot,
                      t_end, ended, dwin, n_dec );
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
  return fmt::format( "FAIL {} {} {} {}\n", g_child.button, g_child.salt, code, m );
}

// Top-K legal indices over `sel` by q descending, ties to the LOWER index (masked_argmax's rule).
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

// ---- batch --------------------------------------------------------------------------------
struct child_result_t
{
  int button = -1;
  std::uint64_t salt = 0;
  bool ok = false;
  double R = 0.0;
  double boot = 0.0;
  double t_end = 0.0;
  int ended = 0;
  double dwin = 0.0;
  int n_dec = 0;
  int exit_code = -1;
  int signal = 0;
  std::string fail;
};

struct batch_result_t
{
  std::vector<child_result_t> res;  // button-major, salt-ascending
  std::uint64_t child_fail = 0;
  double child_cpu_s = 0.0;
  double fork_cpu_s = 0.0;
  double wall_s = 0.0;
};

struct button_stats_t
{
  int n_ok = 0;
  double mean_R = std::numeric_limits<double>::quiet_NaN();
  double se_R = std::numeric_limits<double>::quiet_NaN();
  double mean_RB = std::numeric_limits<double>::quiet_NaN();
  double se_RB = std::numeric_limits<double>::quiet_NaN();
};

// mean and SE (sample sd, n-1, over sqrt(n)) in increasing salt order -- the Python side
// mirrors this exact operation order.
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

button_stats_t stats_for( const batch_result_t& br, int button )
{
  std::vector<double> r, rb;
  for ( const auto& c : br.res )
  {
    if ( c.button != button || !c.ok )
      continue;
    r.push_back( c.R );
    rb.push_back( c.R + c.boot );
  }
  button_stats_t s;
  s.n_ok = static_cast<int>( r.size() );
  mean_se( r, s.mean_R, s.se_R );
  mean_se( rb, s.mean_RB, s.se_RB );
  return s;
}

#if defined( __linux__ )
void become_child( sim_t* sim, player_t* p, std::uint64_t seq, int button, std::uint64_t salt, double window,
                   const std::string& translog_dir, int read_fd, int write_fd )
{
  config_t& c = g_cfg;
  g_child.active = true;
  g_child.p = p;
  g_child.button = button;
  g_child.salt = salt;
  g_child.k = seq - 1;
  g_child.t_k = t_float32( sim );
  g_child.D_k = p->solver_damage_so_far;
  g_child.window = window;
  g_child.divisor = c.value_divisor;
  g_child.gamma = c.gamma;
  g_child.fd = write_fd;
  g_child.translog_mode = !translog_dir.empty();

  ::close( read_fd );

  const int dn = ::open( "/dev/null", O_WRONLY );
  if ( dn >= 0 )
    ::dup2( dn, 1 );

  if ( c.child_fd_isolation && dn >= 0 )
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

  // Test-only non-vacuity plant (P3c): with isolation OFF, write through the inherited .attr
  // stream so the leak comparison is shown able to fail.
  if ( std::getenv( "RL_BRANCH_PLANT_LEAK" ) != nullptr && !c.child_fd_isolation && sim->rl_translog_attr_stream )
  {
    sim->rl_translog_attr_stream->write( "LEAK", 4 );
    sim->rl_translog_attr_stream->flush();
  }

  // Logical isolation, always on.
  if ( g_child.translog_mode )
  {
    rl_translog::branch_child_reopen(
        sim, fmt::format( "{}/k{}-b{}-s{}.translog.bin", translog_dir, g_child.k, button, salt ) );
  }
  else
  {
    rl_translog::branch_child_disable( sim );
  }
  sim->rl_iteration_out.clear();
  sim->rl_bc_on = false;
  sim->rl_bl_on = false;
  sim->solver_fight_timeline_str.clear();
  (void)g_branch_out.release();
  (void)g_teacher_log.release();
  c.teacher = false;
  c.branch_at = -1;
  c.resalt_at_decision = -1;

  sim->resalt_source_rngs( salt );

  g_child.last_t = g_child.t_k;
  g_child.last_D = g_child.D_k;
  g_child.acc = 0.0;
  g_child.n_dec = 0;
}
#endif

// Runs one batch. Returns true IN THE CHILD (out_button = the child's button); false in the
// parent, with `br` filled.
bool run_batch( sim_t* sim, player_t* p, std::uint64_t seq, const std::vector<int>& buttons,
                const std::vector<std::uint64_t>& salts, double window, const std::string& translog_dir,
                batch_result_t& br, int& out_button )
{
#if defined( __linux__ )
  config_t& c = g_cfg;

  // 1. single-thread guard
  const long n_threads = count_dir_entries( "/proc/self/task" );
  if ( n_threads != 1 )
    throw sc_runtime_error( fmt::format( "solver_branch: fork() refused -- process has {} threads", n_threads ) );

  // 2. pre-fork flush: stdio and this module's own sidecars only. NO translog/zstd call.
  std::fflush( nullptr );
  std::cout.flush();
  std::cerr.flush();
  std::clog.flush();
  if ( g_branch_out )
    g_branch_out->flush();
  if ( g_teacher_log )
    g_teacher_log->flush();

  // 3. clocks
  const double self0 = rusage_cpu_s( RUSAGE_SELF );
  const double kids0 = rusage_cpu_s( RUSAGE_CHILDREN );
  const auto wall0 = std::chrono::steady_clock::now();
  if ( g_count.fd_first < 0 )
  {
    g_count.rss_kb_first = read_vmrss_kb();
    g_count.fd_first = count_dir_entries( "/proc/self/fd" );
  }

  // 4. pipe
  int fds[ 2 ];
  if ( ::pipe2( fds, O_CLOEXEC ) != 0 )
    throw sc_runtime_error( fmt::format( "solver_branch: pipe2() failed: {}", std::strerror( errno ) ) );
  ::fcntl( fds[ 0 ], F_SETFL, ::fcntl( fds[ 0 ], F_GETFL ) | O_NONBLOCK );

  br.res.clear();
  for ( int b : buttons )
    for ( std::uint64_t s : salts )
    {
      child_result_t r;
      r.button = b;
      r.salt = s;
      br.res.push_back( r );
    }

  std::string buf;
  auto drain = [ & ]() {
    char tmp[ 4096 ];
    for ( ;; )
    {
      const ssize_t n = ::read( fds[ 0 ], tmp, sizeof( tmp ) );
      if ( n > 0 )
        buf.append( tmp, static_cast<std::size_t>( n ) );
      else
        break;
    }
  };

  std::map<pid_t, std::size_t> pid_slot;
  int running = 0;
  auto reap_one = [ & ]() {
    int st = 0;
    const pid_t w = ::waitpid( -1, &st, 0 );
    if ( w <= 0 )
      throw sc_runtime_error( fmt::format( "solver_branch: waitpid() failed: {}", std::strerror( errno ) ) );
    auto it = pid_slot.find( w );
    if ( it != pid_slot.end() )
    {
      child_result_t& r = br.res[ it->second ];
      if ( WIFEXITED( st ) )
        r.exit_code = WEXITSTATUS( st );
      else if ( WIFSIGNALED( st ) )
        r.signal = WTERMSIG( st );
      pid_slot.erase( it );
      --running;
      ++g_count.children_reaped;
    }
    drain();
  };

  // 5. fork, button-major and salt-ascending
  for ( std::size_t slot = 0; slot < br.res.size(); ++slot )
  {
    while ( running >= c.branch_jobs )
      reap_one();
    const pid_t pid = ::fork();
    if ( pid < 0 )
      throw sc_runtime_error( fmt::format( "solver_branch: fork() failed: {}", std::strerror( errno ) ) );
    if ( pid == 0 )
    {
      become_child( sim, p, seq, br.res[ slot ].button, br.res[ slot ].salt, window, translog_dir, fds[ 0 ],
                    fds[ 1 ] );
      out_button = br.res[ slot ].button;
      return true;
    }
    pid_slot[ pid ] = slot;
    ++running;
    ++g_count.children;
  }

  // 6. close the write end, reap the rest, read to EOF
  ::close( fds[ 1 ] );
  while ( running > 0 )
    reap_one();
  {
    // all writers are gone: read to EOF (blocking is safe now)
    ::fcntl( fds[ 0 ], F_SETFL, ::fcntl( fds[ 0 ], F_GETFL ) & ~O_NONBLOCK );
    char tmp[ 4096 ];
    for ( ;; )
    {
      const ssize_t n = ::read( fds[ 0 ], tmp, sizeof( tmp ) );
      if ( n > 0 )
        buf.append( tmp, static_cast<std::size_t>( n ) );
      else if ( n == 0 )
        break;
      else if ( errno != EINTR )
        break;
    }
  }
  ::close( fds[ 0 ] );

  // 7. match lines to (button, salt)
  {
    std::istringstream ls( buf );
    std::string line;
    while ( std::getline( ls, line ) )
    {
      std::istringstream ws( line );
      std::string tag;
      int b = -1;
      unsigned long long s = 0;
      ws >> tag >> b >> s;
      child_result_t* r = nullptr;
      for ( auto& x : br.res )
        if ( x.button == b && x.salt == s )
          r = &x;
      if ( !r )
        continue;
      if ( tag == "OK" )
      {
        std::string sR, sB, sT, sE, sD, sN;
        ws >> sR >> sB >> sT >> sE >> sD >> sN;
        r->R = std::strtod( sR.c_str(), nullptr );
        r->boot = std::strtod( sB.c_str(), nullptr );
        r->t_end = std::strtod( sT.c_str(), nullptr );
        r->ended = std::atoi( sE.c_str() );
        r->dwin = std::strtod( sD.c_str(), nullptr );
        r->n_dec = std::atoi( sN.c_str() );
        r->ok = true;
      }
      else if ( tag == "FAIL" )
      {
        int code = 0;
        std::string msg;
        ws >> code >> msg;
        r->fail = fmt::format( "{} {}", code, msg );
      }
    }
  }
  br.child_fail = 0;
  for ( auto& r : br.res )
  {
    if ( r.ok && ( r.exit_code != 0 || r.signal != 0 ) )
      r.ok = false;
    if ( !r.ok )
    {
      if ( r.fail.empty() )
        r.fail = fmt::format( "no-line exit={} signal={}", r.exit_code, r.signal );
      ++br.child_fail;
    }
  }
  g_count.child_fail += br.child_fail;

  // 8. clocks
  br.child_cpu_s = rusage_cpu_s( RUSAGE_CHILDREN ) - kids0;
  br.fork_cpu_s = rusage_cpu_s( RUSAGE_SELF ) - self0;
  br.wall_s = std::chrono::duration<double>( std::chrono::steady_clock::now() - wall0 ).count();
  g_count.child_cpu_s += br.child_cpu_s;
  g_count.fork_cpu_s += br.fork_cpu_s;
  g_count.wall_branch_s += br.wall_s;
  g_count.rss_kb_last = read_vmrss_kb();
  g_count.fd_last = count_dir_entries( "/proc/self/fd" );
  return false;
#else
  (void)sim; (void)p; (void)seq; (void)buttons; (void)salts; (void)window; (void)translog_dir; (void)br;
  (void)out_button;
  throw sc_runtime_error( "solver_branch: fork() branching needs a Linux build" );
#endif
}

std::vector<int> resolve_buttons( const std::string& spec, const float* q, const std::uint8_t* mask,
                                  const std::uint8_t* select_mask, std::uint64_t seq )
{
  std::vector<int> out;
  if ( spec.rfind( "top:", 0 ) == 0 )
  {
    const int K = std::atoi( spec.c_str() + 4 );
    out = top_k( q, select_mask, K );
    if ( static_cast<int>( out.size() ) < K )
      throw sc_runtime_error( fmt::format(
          "solver_branch_buttons: top:{} asked at decision k={} but only {} buttons are selectable there", K, seq - 1,
          out.size() ) );
    return out;
  }
  std::stringstream ss( spec );
  std::string tok;
  while ( std::getline( ss, tok, ',' ) )
  {
    const int b = std::atoi( tok.c_str() );
    if ( !mask[ b ] )
      throw sc_runtime_error( fmt::format(
          "solver_branch_buttons: button {} is illegal at decision k={} (the engine's natural mask)", b, seq - 1 ) );
    out.push_back( b );
  }
  return out;
}

bool parse_buttons_spec( const std::string& spec, int& n_buttons )
{
  if ( spec.rfind( "top:", 0 ) == 0 )
  {
    const std::string rest = spec.substr( 4 );
    if ( rest.empty() || rest.find_first_not_of( "0123456789" ) != std::string::npos )
      return false;
    n_buttons = std::atoi( rest.c_str() );
    return n_buttons >= 1 && n_buttons <= static_cast<int>( RL_ACTION_DIM );
  }
  std::stringstream ss( spec );
  std::string tok;
  std::vector<int> seen;
  while ( std::getline( ss, tok, ',' ) )
  {
    if ( tok.empty() || tok.find_first_not_of( "0123456789" ) != std::string::npos )
      return false;
    const int b = std::atoi( tok.c_str() );
    if ( b < 0 || b >= static_cast<int>( RL_ACTION_DIM ) )
      return false;
    if ( std::find( seen.begin(), seen.end(), b ) != seen.end() )
      return false;
    seen.push_back( b );
  }
  n_buttons = static_cast<int>( seen.size() );
  return n_buttons >= 1;
}

[[noreturn]] void refuse( const std::string& msg )
{
  throw sc_invalid_sim_argument( msg );
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
  const bool resalt = c.resalt_at_decision >= 0;
  const bool branch = c.branch_at >= 0;
  const bool teacher = c.teacher;
  if ( !resalt && !branch && !teacher )
    return;

  const std::string name = teacher ? "solver_teacher" : branch ? "solver_branch_at" : "per_source_rng_resalt_at_decision";

#if !defined( __linux__ )
  refuse( name + " needs a Linux build (fork())." );
#endif

  // 7. one resalt anchor per run
  if ( resalt && sim->per_source_rng_resalt_at >= timespan_t::zero() )
    refuse( "per_source_rng_resalt_at_decision cannot be combined with per_source_rng_resalt_at -- one resalt anchor "
            "per run." );
  // 8. one mode per process
  if ( ( resalt ? 1 : 0 ) + ( branch ? 1 : 0 ) + ( teacher ? 1 : 0 ) > 1 )
    refuse( name + " -- one mode per process: per_source_rng_resalt_at_decision, solver_branch_at and solver_teacher "
                   "are mutually exclusive." );
  // 2. in-process transport only
  if ( sim->solver_policy_str.empty() || !sim->solver_control_str.empty() )
    refuse( name + " requires solver_policy= and no solver_control= (in-process transport only)." );
  // 3. one fight per process
  if ( sim->iterations != 1 )
    refuse( fmt::format( "{} requires iterations=1 (got {}).", name, sim->iterations ) );
  // 4. per-source dice
  if ( !sim->per_source_rng )
    refuse( name + " requires per_source_rng=1." );
  // 5. one thread
  if ( sim->threads != 1 )
    refuse( fmt::format( "{} requires threads=1 (effective threads={}).", name, sim->threads ) );
  // 6. no profilesets
  if ( !sim->profileset_map.empty() )
    refuse( name + " cannot be combined with profilesets." );
  if ( sim->parent != nullptr )
    refuse( name + " is set on a non-root sim." );

  // 10. study options that would double-write or change the run
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
      refuse( fmt::format( "{} cannot be combined with {}.", name, cf.opt ) );

  if ( branch || teacher )
  {
    // 9. value and batch parameters
    if ( !( c.value_divisor > 0.0 ) )
      refuse( name + " requires solver_branch_value_divisor > 0 (divisorByFightShape[shape])." );
    if ( !( c.gamma > 0.0 && c.gamma <= 1.0 ) )
      refuse( fmt::format( "{} requires solver_branch_gamma in (0, 1] (got {}).", name, c.gamma ) );
    if ( c.branch_jobs < 1 || c.branch_jobs > 12 )
      refuse( fmt::format( "{} requires solver_branch_jobs in 1..12 (got {}).", name, c.branch_jobs ) );
  }
  if ( teacher )
  {
    if ( !( c.teacher_window > 0.0 ) )
      refuse( fmt::format( "solver_teacher_window must be > 0 (got {}).", c.teacher_window ) );
    if ( c.teacher_n * c.teacher_topk > 256 )
      refuse( fmt::format( "solver_teacher_n x solver_teacher_topk = {} exceeds 256 children per batch.",
                           c.teacher_n * c.teacher_topk ) );
    if ( !c.branch_translog_dir.empty() )
      refuse( "solver_branch_translog_dir requires solver_branch_at, solver_branch_window=0 and rl_translog=." );
  }
  if ( branch )
  {
    int nb = 0;
    if ( !parse_buttons_spec( c.branch_buttons, nb ) )
      refuse( fmt::format( "solver_branch_buttons='{}' is not 'a,b,c' (distinct action indices) or 'top:K'.",
                           c.branch_buttons ) );
    if ( c.branch_n * nb > 256 )
      refuse( fmt::format( "solver_branch_n x buttons = {} exceeds 256 children per batch.", c.branch_n * nb ) );
    if ( c.branch_window < 0.0 )
      refuse( "solver_branch_window must be >= 0." );
    if ( !c.branch_translog_dir.empty() )
    {
      if ( c.branch_window != 0.0 || sim->rl_translog_file_str.empty() )
        refuse( "solver_branch_translog_dir requires solver_branch_at, solver_branch_window=0 and rl_translog=." );
      if ( !sim->rl_buff_credit_str.empty() && sim->rl_buff_credit_str != "off" )
        refuse( "solver_branch_translog_dir cannot be combined with rl_buff_credit." );
    }
  }
  else if ( !c.branch_translog_dir.empty() )
  {
    refuse( "solver_branch_translog_dir requires solver_branch_at, solver_branch_window=0 and rl_translog=." );
  }

  // 11. greedy weights only for branch/teacher
  if ( branch || teacher )
  {
    const auto& w = sim->solver_policy_weights;
    if ( !w )
      refuse( name + " requires loaded solver_policy weights." );
    if ( w->exploration > 0.0f )
      refuse( fmt::format( "{} requires greedy weights: the loaded weights carry exploration={}.", name,
                           w->exploration ) );
    if ( w->has_aim_section && w->aim.exploration > 0.0f )
      refuse( fmt::format( "{} requires greedy weights: the loaded aim section carries exploration={}.", name,
                           w->aim.exploration ) );
  }

  if ( branch && !c.branch_out.empty() )
  {
    g_branch_out = std::make_unique<std::ofstream>( c.branch_out, std::ios::out | std::ios::trunc );
    if ( !g_branch_out->is_open() )
      refuse( fmt::format( "solver_branch_out='{}' cannot be opened for writing.", c.branch_out ) );
  }

  if ( teacher && !c.teacher_log.empty() )
  {
    g_teacher_log = std::make_unique<std::ofstream>( c.teacher_log, std::ios::out | std::ios::trunc );
    if ( !g_teacher_log->is_open() )
      refuse( fmt::format( "solver_teacher_log='{}' cannot be opened for writing.", c.teacher_log ) );
  }

  g_any = true;
  if ( teacher )
    fmt::print( "[RL_TEACHER] margin={:.17g} n={} window={:.17g} topk={} bootstrap={} divisor={:.17g} gamma={:.17g} "
                "jobs={} fd_isolation={} salt_base={} log={}\n",
                c.teacher_margin, c.teacher_n, c.teacher_window, c.teacher_topk, c.teacher_bootstrap ? 1 : 0,
                c.value_divisor, c.gamma, c.branch_jobs, c.child_fd_isolation ? 1 : 0, c.branch_salt_base,
                c.teacher_log );
  else if ( resalt )
    fmt::print( "[RL_RESALT_AT_DECISION] k={} salt={}\n", c.resalt_at_decision, sim->per_source_rng_salt );
  else if ( branch )
    fmt::print( "[RL_BRANCH] at={} buttons={} n={} salt_base={} window={} jobs={} divisor={:.17g} translog_dir={}\n",
                c.branch_at, c.branch_buttons, c.branch_n, c.branch_salt_base, c.branch_window, c.branch_jobs,
                c.value_divisor, c.branch_translog_dir );
  std::fflush( stdout );
}

void on_decision( sim_t* sim, player_t* p, std::uint64_t seq, const float* q, const std::uint8_t* mask,
                  const std::uint8_t* select_mask, int legal_count, float q_margin, float top_q, bool exploratory,
                  bool forced, int& idx )
{
  if ( !g_any )
    return;

  // 1. branch child: window accounting
  if ( g_child.active )
  {
    const double t = t_float32( sim );
    const double D = p->solver_damage_so_far;
    g_child.acc += std::pow( g_child.gamma, g_child.last_t - g_child.t_k ) * ( ( D - g_child.last_D ) / g_child.divisor );
    ++g_child.n_dec;
    if ( g_child.window > 0.0 && t >= g_child.t_k + g_child.window )
    {
      const double boot = std::pow( g_child.gamma, t - g_child.t_k ) * static_cast<double>( top_q );
      child_write_and_exit( make_ok_line( g_child.acc, boot, t, 0, D - g_child.D_k, g_child.n_dec ), 0 );
    }
    g_child.last_t = t;
    g_child.last_D = D;
    return;
  }

  config_t& c = g_cfg;
  const std::int64_t k = static_cast<std::int64_t>( seq ) - 1;

  // 2. decision-anchored re-salt
  if ( c.resalt_at_decision >= 0 && k == c.resalt_at_decision )
    sim->resalt_source_rngs( sim->per_source_rng_salt );

  // 3. branch at k
  if ( c.branch_at >= 0 && k == c.branch_at )
  {
    const std::vector<int> buttons = resolve_buttons( c.branch_buttons, q, mask, select_mask, seq );
    std::vector<std::uint64_t> salts;
    for ( int i = 0; i < c.branch_n; ++i )
      salts.push_back( c.branch_salt_base + static_cast<std::uint64_t>( i ) );
    batch_result_t br;
    int child_button = -1;
    if ( run_batch( sim, p, seq, buttons, salts, c.branch_window, c.branch_translog_dir, br, child_button ) )
    {
      idx = child_button;
      return;
    }
    if ( g_branch_out )
    {
      const double t_k = t_float32( sim );
      for ( const auto& r : br.res )
      {
        *g_branch_out << fmt::format(
            "{{\"k\":{},\"t_k\":{},\"button\":{},\"salt\":{},\"ok\":{},\"R\":{},\"boot\":{},\"t_end\":{},\"ended\":{},"
            "\"Dwin\":{},\"n_dec\":{},\"fail\":{}}}\n",
            k, jnum( t_k ), r.button, r.salt, r.ok ? 1 : 0, jnum( r.R ), jnum( r.boot ), jnum( r.t_end ), r.ended,
            jnum( r.dwin ), r.n_dec, r.ok ? std::string( "null" ) : fmt::format( "\"{}\"", r.fail ) );
      }
      std::string blist;
      for ( std::size_t i = 0; i < buttons.size(); ++i )
      {
        const button_stats_t s = stats_for( br, buttons[ i ] );
        *g_branch_out << fmt::format(
            "{{\"button\":{},\"q\":{},\"n_ok\":{},\"mean_R\":{},\"se_R\":{},\"mean_RB\":{},\"se_RB\":{}}}\n",
            buttons[ i ], jnum( static_cast<double>( q[ buttons[ i ] ] ) ), s.n_ok, jnum( s.mean_R ), jnum( s.se_R ),
            jnum( s.mean_RB ), jnum( s.se_RB ) );
        blist += ( i ? "," : "" ) + std::to_string( buttons[ i ] );
      }
      *g_branch_out << fmt::format(
          "{{\"batch\":1,\"k\":{},\"buttons\":[{}],\"children\":{},\"child_fail\":{},\"child_cpu_s\":{},"
          "\"fork_cpu_s\":{},\"wall_s\":{},\"threads_at_fork\":1}}\n",
          k, blist, br.res.size(), br.child_fail, jnum( br.child_cpu_s ), jnum( br.fork_cpu_s ), jnum( br.wall_s ) );
      g_branch_out->flush();
    }
  }

  // 4. teacher
  if ( c.teacher )
  {
    ++g_count.decisions;
    const bool near_tie = legal_count >= 2 && !std::isnan( q_margin ) && q_margin < c.teacher_margin;
    if ( !near_tie )
      return;
    ++g_count.near_ties;
    int n_sel = 0;
    for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
      n_sel += select_mask[ i ] ? 1 : 0;
    if ( c.teacher_n <= 0 || forced || exploratory || n_sel < 2 )
      return;

    const std::vector<int> cands = top_k( q, select_mask, c.teacher_topk );
    if ( cands.empty() || cands[ 0 ] != idx )
      throw sc_runtime_error( fmt::format(
          "solver_teacher: the net's pick {} is not the best selectable button {} at decision k={} -- refusing to "
          "branch from a press the net did not choose",
          idx, cands.empty() ? -1 : cands[ 0 ], k ) );
    std::vector<std::uint64_t> salts;
    for ( int i = 0; i < c.teacher_n; ++i )
      salts.push_back( c.branch_salt_base + seq * 1000u + static_cast<std::uint64_t>( i ) );

    batch_result_t br;
    int child_button = -1;
    if ( run_batch( sim, p, seq, cands, salts, c.teacher_window, std::string(), br, child_button ) )
    {
      idx = child_button;
      return;
    }

    const int need = ( c.teacher_n + 1 ) / 2;
    std::vector<button_stats_t> st;
    bool fail = false;
    for ( int b : cands )
    {
      st.push_back( stats_for( br, b ) );
      fail = fail || st.back().n_ok < need;
    }
    auto score = [ & ]( std::size_t i ) { return c.teacher_bootstrap ? st[ i ].mean_RB : st[ i ].mean_R; };

    // paired difference (net's pick minus runner-up) over salts where both are OK
    std::vector<double> diffs;
    if ( cands.size() >= 2 )
    {
      for ( std::uint64_t s0 : salts )
      {
        const child_result_t* a = nullptr;
        const child_result_t* b = nullptr;
        for ( const auto& r : br.res )
        {
          if ( r.salt != s0 || !r.ok )
            continue;
          if ( r.button == cands[ 0 ] )
            a = &r;
          else if ( r.button == cands[ 1 ] )
            b = &r;
        }
        if ( a && b )
          diffs.push_back( c.teacher_bootstrap ? ( a->R + a->boot ) - ( b->R + b->boot ) : a->R - b->R );
      }
    }
    double diff_mean, diff_se;
    mean_se( diffs, diff_mean, diff_se );

    const int net = idx;
    int pick = net;
    if ( fail )
    {
      ++g_count.skipped_fail;
    }
    else
    {
      std::size_t best = 0;
      for ( std::size_t i = 1; i < cands.size(); ++i )
        if ( score( i ) > score( best ) )  // strict: an exact tie stays with the net (rank order)
          best = i;
      pick = cands[ best ];
      ++g_count.branched;
      if ( pick != net )
        ++g_count.overrides;
      idx = pick;
    }

    if ( g_teacher_log )
    {
      std::string jc, jq, jok, jmR, jsR, jmRB, jsRB;
      for ( std::size_t i = 0; i < cands.size(); ++i )
      {
        const char* sep = i ? "," : "";
        jc += sep + std::to_string( cands[ i ] );
        jq += sep + jnum( static_cast<double>( q[ cands[ i ] ] ) );
        jok += sep + std::to_string( st[ i ].n_ok );
        jmR += sep + jnum( st[ i ].mean_R );
        jsR += sep + jnum( st[ i ].se_R );
        jmRB += sep + jnum( st[ i ].mean_RB );
        jsRB += sep + jnum( st[ i ].se_RB );
      }
      *g_teacher_log << fmt::format(
          "{{\"seq\":{},\"t\":{},\"net\":{},\"cands\":[{}],\"q\":[{}],\"margin\":{},\"n\":{},\"ok\":[{}],"
          "\"mean_R\":[{}],\"se_R\":[{}],\"mean_RB\":[{}],\"se_RB\":[{}],\"diff_mean\":{},\"diff_se\":{},"
          "\"pick\":{},\"override\":{},\"skipped_fail\":{},\"children\":{},\"child_fail\":{},"
          "\"child_cpu_s\":{},\"fork_cpu_s\":{},\"wall_s\":{}}}\n",
          seq, jnum( t_float32( sim ) ), net, jc, jq, jnum( static_cast<double>( q_margin ) ), c.teacher_n, jok, jmR, jsR,
          jmRB, jsRB, jnum( diff_mean ), jnum( diff_se ), pick, pick != net ? 1 : 0, fail ? 1 : 0, br.res.size(),
          br.child_fail, jnum( br.child_cpu_s ), jnum( br.fork_cpu_s ), jnum( br.wall_s ) );
    }
  }
}

void on_combat_end( sim_t* sim )
{
  if ( !g_child.active )
    return;
  player_t* p = g_child.p;
  const double D_end = p->solver_damage_so_far;
  const double t_end = t_float32( sim );
  g_child.acc +=
      std::pow( g_child.gamma, g_child.last_t - g_child.t_k ) * ( ( D_end - g_child.last_D ) / g_child.divisor );
  const std::string line = make_ok_line( g_child.acc, 0.0, t_end, 1, D_end - g_child.D_k, g_child.n_dec );
  if ( g_child.translog_mode )
  {
    g_child.line = line;
    g_child.have_line = true;
    return;
  }
  child_write_and_exit( line, 0 );
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
  if ( g_child.have_line )
    child_write_and_exit( g_child.line, 0 );
  child_write_and_exit( make_fail_line( 72, "child reached the footer without a result line" ), 72 );
}

void on_execute_end( sim_t* sim )
{
  (void)sim;
  if ( !g_child.active )
    return;
  child_write_and_exit( make_fail_line( 73, "child reached the end of execute() without a result" ), 73 );
}

void write_summary( sim_t* sim )
{
  (void)sim;
  const config_t& c = g_cfg;
  if ( !g_any || !c.teacher || g_child.active )
    return;
#if defined( __linux__ )
  const double self_cpu = rusage_cpu_s( RUSAGE_SELF );
  const double kids_cpu = rusage_cpu_s( RUSAGE_CHILDREN );
#else
  const double self_cpu = 0.0, kids_cpu = 0.0;
#endif
  const std::string js = fmt::format(
      "{{\"summary\":1,\"decisions\":{},\"near_ties\":{},\"branched\":{},\"overrides\":{},\"skipped_fail\":{},"
      "\"children\":{},\"children_reaped\":{},\"child_fail\":{},\"child_cpu_s\":{},\"fork_cpu_s\":{},"
      "\"wall_branch_s\":{},\"self_cpu_s\":{},\"children_cpu_total_s\":{},\"rss_kb_first\":{},\"rss_kb_last\":{},"
      "\"fd_first\":{},\"fd_last\":{},\"config\":{{\"margin\":{},\"n\":{},\"window\":{},\"topk\":{},"
      "\"bootstrap\":{},\"divisor\":{},\"gamma\":{},\"jobs\":{},\"salt_base\":{},\"fd_isolation\":{}}}}}",
      g_count.decisions, g_count.near_ties, g_count.branched, g_count.overrides, g_count.skipped_fail, g_count.children,
      g_count.children_reaped, g_count.child_fail, jnum( g_count.child_cpu_s ), jnum( g_count.fork_cpu_s ),
      jnum( g_count.wall_branch_s ), jnum( self_cpu ), jnum( kids_cpu ), g_count.rss_kb_first, g_count.rss_kb_last,
      g_count.fd_first, g_count.fd_last, jnum( c.teacher_margin ), c.teacher_n, jnum( c.teacher_window ), c.teacher_topk,
      c.teacher_bootstrap ? 1 : 0, jnum( c.value_divisor ), jnum( c.gamma ), c.branch_jobs, c.branch_salt_base,
      c.child_fd_isolation ? 1 : 0 );
  if ( g_teacher_log )
  {
    *g_teacher_log << js << "\n";
    g_teacher_log->flush();
  }
  fmt::print( "[RL_TEACHER_SUMMARY] {}\n", js );
  std::fflush( stdout );
}
}  // namespace solver_branch
