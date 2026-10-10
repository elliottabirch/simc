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
//     LEAF <copy> <pass> <cand> <rank> <w> <R> <R_undisc> <R_real> <boot_q> <boot_v> <t_end> <ended> <n_dec> <cut_state>
//          <vmhwm_kb> <cpu_s> <path> <splits> <sec>
//     FAIL <copy> <code> <msg>
// <copy> is the copy's index within its pass (a pair is one copy index); <cand> the index of its root candidate (0 = the
// net's own press); <rank> is the second-level rank of the leaf (0, 1, or 2 = no second-level decision arose); <sec> the
// second-level button the leaf's lineage pressed (-1 when none); <boot_v> the window-end value term (0 until it is used). A copy's value is the
// sum over its leaves of w x R; a copy with a FAIL, a missing or malformed leaf, or a bad exit is dropped whole
// and counted (n_fail). Means and standard errors are taken over COPIES (copies of one search are independent:
// owner rule D1), never over leaves.
//
// CHANCE SPLITS (280-04, SRCH-03; the weights of SRCH-01 and SRCH-02). Inside a copy a sink is attached to
// rng::rl_draw_sink (never in the parent). At a roll on a split-eligible proc generator, or a draw on a split-eligible
// deck whose exact next-card chance p is strictly between 0 and 1, with split budget left on this path, the copy forks:
// the NEW process takes the no / FAIL outcome (path weight x (1 - p)) and re-salts every stream, re-seeds the
// exploration stream and re-deals every deck so the two branches never share dice (owner rule D1); the forking process
// waits for it to finish (DFS-wait: one running process per copy, every descendant reaped by its own forker) and then
// takes the yes / SUCCESS outcome (path weight x p). The site of a roll is the proc generator's stream, tagged
// SEARCH_SITE_BASE + row index in the copy; a roll on any other stream (crit, damage range, swing timer, player,
// action and buff streams) has no tag and is never split. Every process writes its own lines:
//     LEAF ... <path> <splits>        (path = the outcome digits along the way, '-' when empty; splits = row:p:outcome;...)
//     STATS <copy> <rank> <path> <fork_fail> <row>:<n>:<sum_p>:<sum_pq>:<n_yes>;...   (rows with n > 0, '-' when none)
//     FAIL <copy> <code> <path> <msg>
//
// SITE RANKING (280-05, SRCH-04). The parent keeps, per site, over every search of the run: rolls (the path-weighted
// roll count per copy, summed over copies), copies, the path-weighted chance sum, the completed split nodes, and per
// completed split node one GAP observation |V_yes - V_no| (each side's value is the weight-normalised mean of the leaf
// returns under that side; the same R the search scores). gap_mean is an exponentially weighted mean with alpha 0.1
// (the first observation seeds it). var_share = p(1 - p) x gap_mean^2 x occurrences per window, divided by the sum over
// sites. solver_search_sites_in ranks: the top S eligible rows by its var_share (among rows with gap_n > 0) are the
// run's split-eligible set; the cold-start rule fills a short set (a path keeps S - R splits for non-ranked sites).
// solver_search_sites_out writes the merged table at exit. The audit option runs every search with S = 0.
//
// DEPTH TWO (280-06, SRCH-05 to SRCH-07). With solver_search_depth=2 (the default) pass 1 is the depth-one pass (8
// copies per root candidate, no second-level override: the cell (candidate, rank 0)); pass 2 spends the remaining copies as
// PAIRS. A pair is ONE process that plays the root press and, at the first decision with at least two selectable buttons
// that is neither forced nor exploratory (the SECOND LEVEL), forks DFS-wait: the new process re-salts, re-seeds and re-deals
// and presses the net's rank-1 button, the forker then presses the net's own rank-0 button. Both halves count as one copy
// each (two copies spent per pair). A lineage whose window ends before any second-level decision never forks and writes its
// leaves with rank 2 (no second-level decision arose); a pair none of whose lineages forked is two copies spent but ONE
// rank-0 sample (one play is one independent sample), a pair some of whose lineages forked gives a rank-0 sample (rank-0
// and rank-2 leaves) and a rank-1 sample (rank-1 and rank-2 leaves), each with leaf weights summing to 1. The odd copy
// left over goes, as a plain copy, to the best survivor's rank-0 cell. Pairs are dealt round-robin to the survivors, best
// pass-1 mean first. value(candidate) = its best cell's mean; pick = the best value among the survivors.
//
// RECORD (280-07, SRCH-08). Besides the lines above a copy writes, each with one write() of at most 4,096 bytes:
//     SECOND <copy> <path> <seq> <t> <w> <b0> <b1> <mask22> <obs_b64>   a pair lineage, just before it forks at its second level
//     CHANCE <copy> <path> <rk> <seq> <t> <w> <press> <mask22> <obs_b64> the first labelled decision after a chance split
// and every LEAF line gains <end_obs_b64> ('-' when the fight ended first) and rc=<mark>:<R_from>:<boot_from>,... (a mark per
// SECOND and CHANCE decision on the leaf's path: the window return and bootstrap measured FROM that decision). The parent
// turns them into rows (root, second, chance) and appends them to solver_search_record at the end of the fight, and rewrites
// solver_search_summary. Encodings and readings: .planning/phases/280-the-search-in-the-fork/280-RECORD.md.
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
#include "rapidjson/document.h"

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
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
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
// The bootstrap mode of the window label (280-06): 0 none, 1 q, 2 value. (Moved up by 280-07: a leaf line's rc marks read it.)
int g_boot_mode = 0;
bool g_value_shape = false;  // solver_search_value_units=shape (284-06): the window-end term is the head's own output
// The value blob (solver_search_value), loaded once at validate; a copy shares it through fork().
std::shared_ptr<rl_policy::rl_weights_t> g_value_w;

// The depth-one reading of G280-7: 8 copies per candidate first. 280-10 makes it an option (solver_search_pass1, config_t
// default 8); with the option absent the search is byte for byte what it was.
// Linux guarantees a write() of at most PIPE_BUF bytes to a pipe is atomic against other writers.
constexpr std::size_t LINE_MAX_BYTES = 4096;

// ---- copy state ------------------------------------------------------------------------------
// One split taken on the way to a leaf: the site row, the exact chance of the yes / SUCCESS outcome, and the outcome
// this process took (1 = yes, 0 = no). (280-04)
struct split_rec_t
{
  int row = 0;
  double p = 0.0;
  int outcome = 0;
};

// One marked decision on a copy's way to a leaf (280-07): the pair's second-level decision ('S' + path) or the first labelled
// decision after a chance split ('C' + path). The window return so far is kept so a leaf can state its return FROM the mark.
struct mark_t
{
  std::string key;
  double t = 0.0;
  double acc = 0.0;
};

struct child_state_t
{
  bool active = false;
  sim_t* sim = nullptr;
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
  // 280-04: the chance-split state of THIS process
  std::uint64_t key = 0;               // the dice key this process holds (a new branch gets a key of its own)
  double pw = 1.0;                     // path weight: the product of the split probabilities on the way here
  std::string path;                    // outcome digits along the way ('0' = no / FAIL, '1' = yes / SUCCESS)
  std::vector<split_rec_t> splits;     // the splits on the way here, in order
  bool sub_failed = false;             // a branch this process forked and waited for did not leave cleanly
  long fork_fail = 0;                  // fork() refusals met on the way (the roll was then sampled normally)
  // 280-06: the second level
  bool pair_pending = false;           // a pair process that has not yet forked at its second-level decision
  int rank = 0;                        // 0 = the net's own second-level button, 1 = the runner-up (after a pair fork)
  int sec = -1;                        // the second-level button this lineage pressed, -1 when none
  // 280-07
  bool chance_pending = false;         // a chance split happened and no labelled decision has been met since
  std::vector<mark_t> marks;           // the marked decisions on the way here, in order
};
child_state_t g_child;

// ---- split sites (280-04) ----------------------------------------------------------------------------
// One row per proc generator, built once in the PARENT at its first search by a read-only walk. Children share it
// through fork() and tag each row's stream with SEARCH_SITE_BASE + index.
constexpr std::uint32_t SEARCH_SITE_BASE = 0x53000000u;  // far from the recorder's small ids and RL_ROLLER_UNREGISTERED

struct site_row_t
{
  std::string key;
  int type = 0;                 // rng_type_e as an int: 1 simple, 2 real-PPM, 3 deck, 4 accumulated, 5 threshold, 6 custom
  proc_rng_t* ptr = nullptr;
  bool eligible = false;        // types 1, 2, 3, 4; threshold (5) never calls roll(), custom (6) is not eligible
  bool deck = false;
};
std::vector<site_row_t> g_sites;
bool g_sites_built = false;
int g_proof_row = -1;           // solver_search_split_site resolved to a row, or -1
bool g_audit = false;           // solver_search_nosplit_audit (280-05): every search runs with no splits
std::vector<char> g_ranked_flag;  // 280-05: per site row, 1 when sites_in put the row in the run's ranked set
int g_ranked_count = 0;
std::vector<std::string> g_ranked_top;  // "<key>:<in-file var_share>" of the ranked rows, best first

int effective_splits()
{
  return g_audit ? 0 : g_cfg.splits;
}

// ---- site statistics (280-05) -------------------------------------------------------------------------
// One row of the ranking table: read from solver_search_sites_in, updated by this run's searches, written to
// solver_search_sites_out. "Absent means 0" for the appended cumulative fields (the in-file gap_mean still seeds the
// exponentially weighted mean).
constexpr double GAP_ALPHA = 0.1;  // weight of the newest gap observation in gap_mean

struct stat_row_t
{
  std::string key;
  int type = 0;
  bool eligible = false;
  bool has_eligible = false;
  bool has_window = false;
  bool has_gap_n = false;
  double window_s = 0.0;
  // as read (shown for a row with no observations yet, and used for ranking)
  double in_occ = 0.0;
  double in_p = 0.0;
  double in_gap_mean = 0.0;
  double in_var_share = 0.0;
  long long splits = 0;          // completed split nodes at this site, cumulative
  // cumulative
  double rolls = 0.0;
  double copies = 0.0;
  double chance_sum = 0.0;
  double gap_abs_sum = 0.0;
  double gap_n = 0.0;
  double gap2_corr_sum = 0.0;    // sum over observations of (gap^2 - 2 x pooled within-cell variance): a diagnostic
  double gap_ewma = 0.0;
  bool gap_seeded = false;
  double occ() const
  {
    return copies > 0.0 ? rolls / copies : in_occ;
  }
  double p_mean() const
  {
    return rolls > 0.0 ? chance_sum / rolls : in_p;
  }
  double gap_mean() const
  {
    return gap_seeded ? gap_ewma : 0.0;
  }
};
std::vector<stat_row_t> g_in_rows;   // read at validate, in file order
std::vector<stat_row_t> g_stats;     // parallel to g_sites, built at the first search
std::vector<stat_row_t> g_carried;   // file rows whose key is not in this run's site table: written back unchanged
bool g_stats_bound = false;
long long g_gap_obs_total = 0;

// A copy's own per-path counts (the parent never counts). n and sum_p cover rolls with 0 < p < 1 on a roll row and
// informative draws (0 < p < 1) on a deck row; sum_pq is p x (1 - p); n_yes the yes / SUCCESS outcomes.
struct site_count_t
{
  std::uint64_t n = 0;
  double sum_p = 0.0;
  double sum_pq = 0.0;
  std::uint64_t n_yes = 0;
};
std::vector<site_count_t> g_counts;

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

// ---- base64 of the observation (280-07) ------------------------------------------------------------------
// Standard alphabet, '=' padding (RFC 4648). The 419 float32 inputs go out as their little-endian bytes: the box is little
// endian (checked once at validate), so a plain copy of each float's four bytes is the little-endian encoding.
std::string b64_encode( const unsigned char* d, std::size_t n )
{
  static const char* const A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  o.reserve( ( ( n + 2 ) / 3 ) * 4 );
  std::size_t i = 0;
  for ( ; i + 2 < n; i += 3 )
  {
    const unsigned v = ( static_cast<unsigned>( d[ i ] ) << 16 ) | ( static_cast<unsigned>( d[ i + 1 ] ) << 8 ) | d[ i + 2 ];
    o += A[ ( v >> 18 ) & 63 ];
    o += A[ ( v >> 12 ) & 63 ];
    o += A[ ( v >> 6 ) & 63 ];
    o += A[ v & 63 ];
  }
  if ( i + 1 == n )
  {
    const unsigned v = static_cast<unsigned>( d[ i ] ) << 16;
    o += A[ ( v >> 18 ) & 63 ];
    o += A[ ( v >> 12 ) & 63 ];
    o += "==";
  }
  else if ( i + 2 == n )
  {
    const unsigned v = ( static_cast<unsigned>( d[ i ] ) << 16 ) | ( static_cast<unsigned>( d[ i + 1 ] ) << 8 );
    o += A[ ( v >> 18 ) & 63 ];
    o += A[ ( v >> 12 ) & 63 ];
    o += A[ ( v >> 6 ) & 63 ];
    o += '=';
  }
  return o;
}

std::string obs_b64( const float* obs )
{
  unsigned char bytes[ RL_OBS_DIM * 4 ];
  std::memcpy( bytes, obs, sizeof( bytes ) );
  return b64_encode( bytes, sizeof( bytes ) );
}

// The exact length and the characters of a base64 observation read back from a pipe line (a bad token drops its copy).
bool valid_obs_b64( const std::string& t )
{
  if ( t.size() != 4 * ( ( RL_OBS_DIM * 4 + 2 ) / 3 ) )
    return false;
  for ( std::size_t i = 0; i < t.size(); ++i )
  {
    const char ch = t[ i ];
    const bool ok = ( ch >= 'A' && ch <= 'Z' ) || ( ch >= 'a' && ch <= 'z' ) || ( ch >= '0' && ch <= '9' ) || ch == '+' ||
                    ch == '/' || ( ch == '=' && i + 2 >= t.size() );
    if ( !ok )
      return false;
  }
  return true;
}

// The 22 legality bits as a string of 0/1 digits (the pipe form) and as a JSON list.
std::string mask_digits( const std::uint8_t* mask )
{
  std::string o;
  for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
    o += mask[ i ] ? '1' : '0';
  return o;
}

bool valid_mask_digits( const std::string& t )
{
  if ( t.size() != RL_ACTION_DIM )
    return false;
  for ( char ch : t )
    if ( ch != '0' && ch != '1' )
      return false;
  return true;
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

// The rank a leaf is written with: 2 when this pair lineage never reached a second-level decision (280-06).
int out_rank()
{
  return g_child.pair_pending ? 2 : g_child.rank;
}

constexpr std::uint64_t PAIR_SALT = 0x3C6EF372FE94F82Bull;  // mixed into the key of a pair's rank-1 half

std::string path_token()
{
  return g_child.path.empty() ? std::string( "-" ) : g_child.path;
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
  return fmt::format( "FAIL {} {} {} {}\n", g_child.copy, code, path_token(), m );
}

// One write() of one line without leaving (the STATS line goes first, then the LEAF line).
void child_write_only( const std::string& line )
{
#if defined( __linux__ )
  if ( g_child.fd >= 0 )
  {
    const ssize_t w = ::write( g_child.fd, line.data(), line.size() );
    (void)w;
  }
#else
  (void)line;
#endif
}

std::string split_list_token()
{
  if ( g_child.splits.empty() )
    return "-";
  std::string out;
  for ( const split_rec_t& sp : g_child.splits )
  {
    if ( !out.empty() )
      out += ';';
    out += fmt::format( "{}:{:.17g}:{}", sp.row, sp.p, sp.outcome );
  }
  return out;
}

std::string stats_line()
{
  std::string items;
  for ( std::size_t i = 0; i < g_counts.size(); ++i )
  {
    const site_count_t& c = g_counts[ i ];
    if ( c.n == 0 )
      continue;
    if ( !items.empty() )
      items += ';';
    items += fmt::format( "{}:{}:{:.17g}:{:.17g}:{}", i, c.n, c.sum_p, c.sum_pq, c.n_yes );
  }
  if ( items.empty() )
    items = "-";
  return fmt::format( "STATS {} {} {} {} {}\n", g_child.copy, out_rank(), path_token(), g_child.fork_fail, items );
}

// A leaf line (this plan: one per copy, rank 0, weight 1). An oversize line is replaced by a FAIL (code 76).
[[noreturn]] void write_leaf_and_exit( double R, double R_undisc, double R_real, double boot_q, double boot_v,
                                       double t_end, int ended, int cut_state, const float* end_obs )
{
  if ( !std::isfinite( R ) || !std::isfinite( R_undisc ) || !std::isfinite( R_real ) || !std::isfinite( boot_q ) ||
       !std::isfinite( boot_v ) || !std::isfinite( t_end ) )
    child_write_and_exit( make_fail_line( 77, "nonfinite_leaf_value" ), 77 );
#if defined( __linux__ )
  const long hwm = read_vmhwm_kb();
  const double cpu = rusage_cpu_s( RUSAGE_SELF );
#else
  const long hwm = 0;
  const double cpu = 0.0;
#endif
  // a branch this process forked and waited for did not leave cleanly: this copy has no complete answer
  if ( g_child.sub_failed )
    child_write_and_exit( make_fail_line( 78, "split_branch_failed" ), 78 );
  // 280-07: the return and the bootstrap measured FROM each marked decision on this leaf's path: the window return left after
  // the mark, rediscounted to the mark's own time (R_from = (R_end - R_at_mark) / gamma^(t_mark - t_k)), and the bootstrap
  // term of the chosen mode, likewise.
  const double boot_term = g_boot_mode == 1 ? boot_q : g_boot_mode == 2 ? boot_v : 0.0;
  std::string rc;
  for ( const mark_t& m : g_child.marks )
  {
    const double disc_m = std::pow( g_child.gamma, m.t - g_child.t_k );
    rc += fmt::format( "{}{}:{:.17g}:{:.17g}", rc.empty() ? "" : ",", m.key, ( R - m.acc ) / disc_m, boot_term / disc_m );
  }
  if ( rc.empty() )
    rc = "-";
  const std::string end_tok = end_obs ? obs_b64( end_obs ) : std::string( "-" );
  const std::string line = fmt::format(
      "LEAF {} {} {} {} {:.17g} {:.17g} {:.17g} {:.17g} {:.17g} {:.17g} {:.9g} {} {} {} {} {:.9g} {} {} {} {} rc={}\n",
      g_child.copy, g_child.pass, g_child.cand, out_rank(), g_child.pw, R, R_undisc, R_real, boot_q, boot_v, t_end, ended,
      g_child.n_dec, cut_state, hwm, cpu, path_token(), split_list_token(), g_child.sec, end_tok, rc );
  const std::string stats = stats_line();
  if ( line.size() > LINE_MAX_BYTES )
    child_write_and_exit( make_fail_line( 76, "line_too_long" ), 76 );
  if ( stats.size() > LINE_MAX_BYTES )
    child_write_and_exit( make_fail_line( 76, "stats_line_too_long" ), 76 );
  child_write_only( stats );
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


// ---- the search sink and the split (280-04) --------------------------------------------------------------
// Re-deal every shuffled deck's undrawn remainder from its own (just re-salted) stream. Used by a copy's setup and by
// the new branch of a split.
void redeal_every_deck( sim_t* sim )
{
  for ( player_t* a : sim->actor_list )
  {
    if ( !a )
      continue;
    for ( proc_rng_t* r : a->proc_rng_list )
      if ( auto* deck = dynamic_cast<shuffled_rng_t*>( r ) )
        deck->redeal_undrawn();
  }
}

struct search_sink_t;
int split_here( search_sink_t& sink, int row, double p );

// The sink a copy attaches to rng::rl_draw_sink (never the parent). A slot outside [SEARCH_SITE_BASE, +rows) belongs to
// no proc generator (crit, damage ranges, swing timers, player, action and buff streams) and is never touched: this is
// what keeps those rolls out of the search, structurally.
struct search_sink_t final : public rng::rl_draw_sink_t
{
  bool busy = false;      // true while a new branch re-salts and re-deals: every hook then answers 0
  int pending_deck = 0;   // the answer given to the deck draw in progress (checked against the card dealt)

  double on_draw( std::uint32_t&, const std::uint64_t&, std::uint32_t, double raw ) override
  {
    return raw;
  }
  void on_no_draw_roll( std::uint32_t&, const std::uint64_t& ) override
  {
  }

  static int row_of( std::uint32_t slot )
  {
    if ( slot < SEARCH_SITE_BASE )
      return -1;
    const std::uint32_t idx = slot - SEARCH_SITE_BASE;
    return idx < g_sites.size() ? static_cast<int>( idx ) : -1;
  }

  // The eligibility rule at a chance (G280-5, 280-05). A path may split at most effective_splits() times (0 under the
  // no-split audit). With solver_search_split_site (proof only) exactly that one site is eligible. Otherwise a row in the
  // ranked set (solver_search_sites_in: the top S eligible rows by var_share) is always eligible while the path has
  // budget; the cold-start rule fills the rest: a non-ranked eligible row may split while the path's non-ranked splits
  // are fewer than S - R (R = the ranked count), so the ranked rows keep their share of the budget. With no file R = 0 and
  // this is plan 280-04's rule: the first S eligible chances met on a path.
  bool may_split( int row, double p ) const
  {
    if ( !( p > 0.0 && p < 1.0 ) )
      return false;
    const int budget = effective_splits();
    if ( static_cast<int>( g_child.splits.size() ) >= budget )
      return false;
    if ( g_proof_row >= 0 )
      return row == g_proof_row;
    if ( !g_sites[ static_cast<std::size_t>( row ) ].eligible )
      return false;
    if ( g_ranked_flag[ static_cast<std::size_t>( row ) ] )
      return true;
    int cold_used = 0;
    for ( const split_rec_t& sp : g_child.splits )
      if ( !g_ranked_flag[ static_cast<std::size_t>( sp.row ) ] )
        ++cold_used;
    return cold_used < budget - g_ranked_count;
  }

  void count( int row, double p, bool yes )
  {
    site_count_t& c = g_counts[ static_cast<std::size_t>( row ) ];
    ++c.n;
    c.sum_p += p;
    c.sum_pq += p * ( 1.0 - p );
    if ( yes )
      ++c.n_yes;
  }

  int pre_roll( std::uint32_t& roller_slot, double chance ) override
  {
    if ( busy )
      return rng::RL_PRE_DRAW_NORMAL;
    const int row = row_of( roller_slot );
    // a deck's own stream (the spaced re-deal rolls on it) is never split from the roll hook (Pitfall 3)
    if ( row < 0 || g_sites[ static_cast<std::size_t>( row ) ].deck )
      return rng::RL_PRE_DRAW_NORMAL;
    if ( !may_split( row, chance ) )
      return rng::RL_PRE_DRAW_NORMAL;
    return split_here( *this, row, chance );
  }

  void on_roll_outcome( std::uint32_t roller, std::uint64_t, double chance, bool outcome ) override
  {
    if ( busy )
      return;
    const int row = row_of( roller );
    if ( row < 0 || g_sites[ static_cast<std::size_t>( row ) ].deck )
      return;
    count( row, chance, outcome );
  }

  int pre_deck_draw( std::uint32_t& roller_slot, double p_success ) override
  {
    pending_deck = 0;
    if ( busy )
      return rng::RL_PRE_DRAW_NORMAL;
    const int row = row_of( roller_slot );
    if ( row < 0 || !g_sites[ static_cast<std::size_t>( row ) ].deck )
      return rng::RL_PRE_DRAW_NORMAL;
    if ( !may_split( row, p_success ) )
      return rng::RL_PRE_DRAW_NORMAL;
    const int answer = split_here( *this, row, p_success );
    pending_deck = answer;
    return answer;
  }

  void on_deck_draw_outcome( std::uint32_t& roller_slot, double p_success, bool success ) override
  {
    if ( busy )
      return;
    // a forced card that was not the card dealt would put a wrong weight on this branch: this copy is dropped whole
    if ( pending_deck != 0 )
    {
      const bool wanted = pending_deck == rng::RL_PRE_FORCE_YES;
      pending_deck = 0;
      if ( wanted != success )
        child_write_and_exit( make_fail_line( 79, "forced_card_not_dealt" ), 79 );
    }
    const int row = row_of( roller_slot );
    if ( row < 0 || !g_sites[ static_cast<std::size_t>( row ) ].deck )
      return;
    if ( p_success > 0.0 && p_success < 1.0 )
      count( row, p_success, success );
  }
};
search_sink_t g_sink;

// DFS-wait split (research lines 591-608). Returns 0 when the fork failed (the chance is then sampled normally),
// RL_PRE_FORCE_NO in the NEW process (path weight x (1 - p)), RL_PRE_FORCE_YES in the forking process after the new
// branch has finished (path weight x p). The forced outcome consumes no draw.
int split_here( search_sink_t& sink, int row, double p )
{
  sim_t* sim = g_child.sim;
  const std::size_t ordinal = g_child.splits.size();
  sink.busy = true;
  const pid_t pid = ::fork();
  if ( pid < 0 )
  {
    ++g_child.fork_fail;
    sink.busy = false;
    return rng::RL_PRE_DRAW_NORMAL;
  }
  if ( pid == 0 )
  {
    // the NEW branch: outcome no / FAIL. Its own dice (owner rule D1): a key mixed from this process's key, the split
    // ordinal on this path and the outcome; every stream re-salted, the exploration stream re-seeded, every deck
    // re-dealt. Without this the two branches would draw the same numbers after the split.
    g_child.pw *= ( 1.0 - p );
    g_child.splits.push_back( split_rec_t{ row, p, 0 } );
    g_child.path.push_back( '0' );
    std::uint64_t k = mix64( g_child.key ^ ( static_cast<std::uint64_t>( ordinal ) + 0x51ED270B0B45FFFBull ) );
    k = mix64( k ^ 0x6A09E667F3BCC909ull );  // outcome 0
    if ( k == 0 )
      k = 0x2545F4914F6CDD1Dull;
    g_child.key = k;
    sim->resalt_source_rngs( k );
    sim->solver_explore_rng.seed( mix64( k ^ 0xE5910BEull ) );
    redeal_every_deck( sim );
    g_child.chance_pending = true;  // 280-07: the next labelled decision of this branch is a chance row
    sink.busy = false;
    return rng::RL_PRE_FORCE_NO;
  }
  // the FORKING process: wait for the new branch to finish, then take outcome yes / SUCCESS on its own dice
  int st = 0;
  pid_t w;
  do
  {
    w = ::waitpid( pid, &st, 0 );
  } while ( w < 0 && errno == EINTR );
  if ( w != pid || !WIFEXITED( st ) || WEXITSTATUS( st ) != 0 )
    g_child.sub_failed = true;
  g_child.pw *= p;
  g_child.splits.push_back( split_rec_t{ row, p, 1 } );
  g_child.path.push_back( '1' );
  g_child.chance_pending = true;  // 280-07
  sink.busy = false;
  return rng::RL_PRE_FORCE_YES;
}

// The second-level pair fork (280-06, SRCH-05), DFS-wait like a chance split: the NEW process is the rank-1 half (own
// dice: a key mixed from this lineage's key, the pair constant and the half; every stream re-salted, the exploration stream
// re-seeded, every deck re-dealt) and presses the net's runner-up button; the FORKING process waits for it to finish and
// then presses the net's own best button as the rank-0 half. A lineage that never reaches this call writes its leaves
// with rank 2. `idx` is the net's best selectable button; `runner_up` the second best.
void pair_fork_here( sim_t* sim, int best, int runner_up, int& idx )
{
  const pid_t pid = ::fork();
  if ( pid < 0 )
  {
    ++g_child.fork_fail;
    child_write_and_exit( make_fail_line( 82, "pair_fork_failed" ), 82 );
  }
  if ( pid == 0 )
  {
    g_child.rank = 1;
    g_child.pair_pending = false;
    g_child.sec = runner_up;
    std::uint64_t k = mix64( mix64( g_child.key ^ PAIR_SALT ) ^ 1ull );
    if ( k == 0 )
      k = 0x2545F4914F6CDD1Dull;
    g_child.key = k;
    g_sink.busy = true;  // the re-deal rolls on the decks' own streams: no hook may answer it
    sim->resalt_source_rngs( k );
    sim->solver_explore_rng.seed( mix64( k ^ 0xE5910BEull ) );
    redeal_every_deck( sim );
    g_sink.busy = false;
    idx = runner_up;
    return;
  }
  int st = 0;
  pid_t w;
  do
  {
    w = ::waitpid( pid, &st, 0 );
  } while ( w < 0 && errno == EINTR );
  if ( w != pid || !WIFEXITED( st ) || WEXITSTATUS( st ) != 0 )
    g_child.sub_failed = true;
  g_child.rank = 0;
  g_child.pair_pending = false;
  g_child.sec = best;
  idx = best;
}

// Mirrors solver_branch.cpp's become_child (396-506) step for step, for one search copy.
void become_search_child( sim_t* sim, player_t* p, std::uint64_t seq, int pass, int copy, int cand, int button,
                          bool pair, std::uint64_t key, double window, int read_fd, int write_fd )
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
  redeal_every_deck( sim );

  // 280-04: the chance-split state of this copy, the site tags, and the sink (in the copy ONLY: this is the one place
  // rng::rl_draw_sink is ever assigned). The tags survive every later re-salt (seed() leaves the roller identity alone).
  g_child.sim = sim;
  g_child.key = key;
  g_child.pw = 1.0;
  g_child.path.clear();
  g_child.splits.clear();
  g_child.sub_failed = false;
  g_child.fork_fail = 0;
  g_child.pair_pending = pair;
  g_child.rank = 0;
  g_child.sec = -1;
  g_child.chance_pending = false;
  g_child.marks.clear();
  g_counts.assign( g_sites.size(), site_count_t{} );
  for ( std::size_t i = 0; i < g_sites.size(); ++i )
    g_sites[ i ].ptr->rl_tag_source_roller( SEARCH_SITE_BASE + static_cast<std::uint32_t>( i ) );
  if ( rng::rl_draw_sink != nullptr )
    child_write_and_exit( make_fail_line( 80, "a_draw_sink_is_already_attached" ), 80 );
  g_sink.busy = false;
  g_sink.pending_deck = 0;
  rng::rl_draw_sink = &g_sink;

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
// One site's counts on one leaf's path (STATS line item).
struct site_item_t
{
  int row = 0;
  double n = 0.0;
  double sum_p = 0.0;
  double sum_pq = 0.0;
  double n_yes = 0.0;
};

struct stats_t
{
  long fork_fail = 0;
  std::vector<site_item_t> items;
};

struct leaf_t
{
  std::string path;                // outcome digits along the way ('' for a leaf that never split)
  std::vector<split_rec_t> splits;
  bool has_stats = false;
  stats_t stats;
  int rank = 0;                    // 0, 1, or 2 (a pair lineage that never reached a second-level decision)
  int sec = -1;                    // the second-level button this leaf's lineage pressed, -1 when none
  double w = 1.0;
  double R = 0.0;
  double R_undisc = 0.0;
  double R_real = 0.0;
  double boot_q = 0.0;
  double boot_v = 0.0;
  double t_end = 0.0;
  int ended = 0;
  int n_dec = 0;
  int cut_state = -1;
  long vmhwm_kb = 0;
  double cpu_s = 0.0;
  std::string end_obs;             // 280-07: the observation at the exact cut, base64; empty when the fight ended first
  std::map<std::string, std::pair<double, double>> rc;  // 280-07: mark key -> (R_from, boot_from)
};

// A SECOND or CHANCE line of one copy (280-07), kept raw until the search has a pick.
struct interior_line_t
{
  bool second = false;            // SECOND (a pair's second-level decision) or CHANCE
  std::string path;               // the path of the process that wrote it ('' when empty)
  int rk = 0;                     // CHANCE: the writer's rank (0, 1, or 2 = a pair lineage that had not forked yet)
  std::uint64_t seq = 0;
  double t = 0.0;
  double w = 1.0;
  int b0 = -1;                    // SECOND: the two buttons; CHANCE: b0 is the press
  int b1 = -1;
  std::string mask;               // 22 digits
  std::string obs;                // base64
};

struct copy_task_t
{
  int cand = 0;    // index into the search's candidate list
  int button = 0;  // the root button this copy presses
  bool pair = false;  // a pair (two copies spent: a rank-0 half and a rank-1 half) instead of one plain copy
};

// The label of a leaf: the window return plus the chosen bootstrap term (SRCH-07).
double leaf_boot( const leaf_t& lf )
{
  return g_boot_mode == 1 ? lf.boot_q : g_boot_mode == 2 ? lf.boot_v : 0.0;
}
double leaf_label( const leaf_t& lf )
{
  return lf.R + leaf_boot( lf );
}

struct copy_result_t
{
  int cand = 0;
  int button = 0;
  bool pair = false;                     // the raw result of a pair process (turned into samples by make_samples)
  int rank = 0;                          // the sample's second-level rank (0 or 1)
  bool pair_forked = false;              // (rank-0 sample of a pair) some lineage forked
  bool pair_cut_first = false;           // (rank-0 sample of a pair) no lineage forked: one sample for two copies
  bool pair_failed = false;
  bool pair_mixed = false;               // (rank-0 sample of a pair) some lineages forked and some did not
  double value_R = 0.0;                  // sum over leaves of w x R
  double value_boot = 0.0;               // sum over leaves of w x bootstrap term
  double value_Rund = 0.0;
  double value_Rreal = 0.0;
  std::vector<leaf_t> leaves;
  std::map<std::pair<int, std::string>, stats_t> stats;  // by (leaf rank, leaf path) (STATS lines)
  std::vector<interior_line_t> interior;                 // 280-07: SECOND and CHANCE lines
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
    // FAIL <copy> <code> <path> <msg>
    r.failed = true;
    r.fail = tok.size() >= 5 ? tok[ 2 ] + " " + tok[ 4 ] + " path=" + tok[ 3 ] : "malformed_fail";
    return;
  }
  if ( tok[ 0 ] == "STATS" )
  {
    // STATS <copy> <rank> <path> <fork_fail> <row>:<n>:<sum_p>:<sum_pq>:<n_yes>;... ('-' when none)
    stats_t st;
    long srank = -1;
    bool okline = tok.size() == 6 && parse_long( tok[ 2 ], srank ) && srank >= 0 && srank <= ( r.pair ? 2 : 0 ) &&
                  parse_long( tok[ 4 ], st.fork_fail ) && st.fork_fail >= 0;
    if ( okline && tok[ 5 ] != "-" )
    {
      std::stringstream is( tok[ 5 ] );
      std::string item;
      while ( okline && std::getline( is, item, ';' ) )
      {
        std::vector<std::string> f;
        std::stringstream fs( item );
        std::string x;
        while ( std::getline( fs, x, ':' ) )
          f.push_back( x );
        long row = -1;
        site_item_t si;
        okline = f.size() == 5 && parse_long( f[ 0 ], row ) && row >= 0 &&
                 row < static_cast<long>( g_sites.size() ) && parse_double( f[ 1 ], si.n ) &&
                 parse_double( f[ 2 ], si.sum_p ) && parse_double( f[ 3 ], si.sum_pq ) &&
                 parse_double( f[ 4 ], si.n_yes ) && si.n >= 0.0 && si.n_yes >= 0.0 && si.n_yes <= si.n;
        si.row = static_cast<int>( row );
        if ( okline )
          st.items.push_back( si );
      }
    }
    const std::string path = okline && tok[ 3 ] != "-" ? tok[ 3 ] : std::string();
    const std::pair<int, std::string> skey( static_cast<int>( srank ), path );
    if ( !okline || r.stats.count( skey ) )
    {
      r.failed = true;
      r.fail = okline ? "duplicate_stats_line" : "bad_stats_line";
      ++g_count.unknown_lines;
      return;
    }
    r.stats[ skey ] = st;
    return;
  }
  if ( tok[ 0 ] == "SECOND" || tok[ 0 ] == "CHANCE" )
  {
    // SECOND <copy> <path> <seq> <t> <w> <b0> <b1> <mask22> <obs_b64>
    // CHANCE <copy> <path> <rk> <seq> <t> <w> <press> <mask22> <obs_b64>
    const bool is_second = tok[ 0 ] == "SECOND";
    interior_line_t il;
    il.second = is_second;
    long seqv = -1, a = -1, b = -1, rk = 0;
    bool okl = tok.size() == 10;
    if ( okl )
    {
      const std::size_t base = is_second ? 3 : 4;  // the index of <seq>
      okl = ( is_second || ( parse_long( tok[ 3 ], rk ) && rk >= 0 && rk <= ( r.pair ? 2 : 0 ) ) ) &&
            parse_long( tok[ base ], seqv ) && seqv >= 0 && parse_double( tok[ base + 1 ], il.t ) &&
            parse_double( tok[ base + 2 ], il.w ) && il.w > 0.0 && il.w <= 1.0 + 1e-9 && parse_long( tok[ base + 3 ], a ) &&
            a >= 0 && a < static_cast<long>( RL_ACTION_DIM ) &&
            ( !is_second || ( parse_long( tok[ 7 ], b ) && b >= 0 && b < static_cast<long>( RL_ACTION_DIM ) ) ) &&
            valid_mask_digits( tok[ 8 ] ) && valid_obs_b64( tok[ 9 ] ) && ( !is_second || r.pair );
      for ( char ch : tok[ 2 ] )
        if ( ch != '0' && ch != '1' && !( ch == '-' && tok[ 2 ].size() == 1 ) )
          okl = false;
      if ( okl && !is_second && tok[ 2 ] == "-" )
        okl = false;  // a chance row has at least one split behind it
    }
    if ( okl )
    {
      il.path = tok[ 2 ] == "-" ? std::string() : tok[ 2 ];
      il.rk = static_cast<int>( rk );
      il.seq = static_cast<std::uint64_t>( seqv );
      il.b0 = static_cast<int>( a );
      il.b1 = static_cast<int>( b );
      il.mask = tok[ 8 ];
      il.obs = tok[ 9 ];
      for ( const interior_line_t& o : r.interior )
        if ( o.second == il.second && o.path == il.path && o.rk == il.rk )
          okl = false;  // two lines for one decision of one lineage
    }
    if ( !okl )
    {
      r.failed = true;
      r.fail = is_second ? "bad_second_line" : "bad_chance_line";
      ++g_count.unknown_lines;
      return;
    }
    r.interior.push_back( il );
    return;
  }
  if ( tok[ 0 ] != "LEAF" )
  {
    ++g_count.unknown_lines;
    return;
  }
  // LEAF <copy> <pass> <cand> <rank> <w> <R> <R_undisc> <R_real> <boot_q> <boot_v> <t_end> <ended> <n_dec> <cut_state>
  //      <vmhwm_kb> <cpu_s> <path> <splits> <sec> <end_obs_b64|-> rc=<key:R_from:boot_from,...|->
  leaf_t lf;
  long fpass = 0, fcand = 0, frank = 0, fended = 0, fndec = 0, fcut = 0, fhwm = 0, fsec = -1;
  bool good = tok.size() == 22 && parse_long( tok[ 2 ], fpass ) && parse_long( tok[ 3 ], fcand ) &&
              parse_long( tok[ 4 ], frank ) && parse_double( tok[ 5 ], lf.w ) && parse_double( tok[ 6 ], lf.R ) &&
              parse_double( tok[ 7 ], lf.R_undisc ) && parse_double( tok[ 8 ], lf.R_real ) &&
              parse_double( tok[ 9 ], lf.boot_q ) && parse_double( tok[ 10 ], lf.boot_v ) &&
              parse_double( tok[ 11 ], lf.t_end ) && parse_long( tok[ 12 ], fended ) && parse_long( tok[ 13 ], fndec ) &&
              parse_long( tok[ 14 ], fcut ) && parse_long( tok[ 15 ], fhwm ) && parse_double( tok[ 16 ], lf.cpu_s ) &&
              parse_long( tok[ 19 ], fsec );
  good = good && fpass == pass && fcand == r.cand && frank >= 0 && frank <= ( r.pair ? 2 : 0 ) && lf.w > 0.0 &&
         lf.w <= 1.0 + 1e-9 && ( fended == 0 || fended == 1 ) && fndec >= 0 && fcut >= -1 && fcut <= 4 && fhwm >= 0 &&
         fsec >= -1 && fsec < static_cast<long>( RL_ACTION_DIM ) && ( frank == 2 ? fsec == -1 : true );
  // 280-07: the end observation and the marks
  if ( good )
  {
    if ( tok[ 20 ] != "-" )
    {
      good = valid_obs_b64( tok[ 20 ] );
      lf.end_obs = tok[ 20 ];
    }
    good = good && tok[ 21 ].rfind( "rc=", 0 ) == 0;
    if ( good && tok[ 21 ] != "rc=-" )
    {
      std::stringstream is( tok[ 21 ].substr( 3 ) );
      std::string item;
      while ( good && std::getline( is, item, ',' ) )
      {
        std::vector<std::string> f;
        std::stringstream fs( item );
        std::string x;
        while ( std::getline( fs, x, ':' ) )
          f.push_back( x );
        double rf = 0.0, bf = 0.0;
        good = f.size() == 3 && f[ 0 ].size() >= 2 && ( f[ 0 ][ 0 ] == 'S' || f[ 0 ][ 0 ] == 'C' ) &&
               parse_double( f[ 1 ], rf ) && parse_double( f[ 2 ], bf ) && !lf.rc.count( f[ 0 ] );
        for ( std::size_t ci = 1; good && ci < f[ 0 ].size(); ++ci )
          good = f[ 0 ][ ci ] == '0' || f[ 0 ][ ci ] == '1' || ( f[ 0 ][ ci ] == '-' && f[ 0 ].size() == 2 );
        if ( good )
          lf.rc[ f[ 0 ] ] = std::make_pair( rf, bf );
      }
    }
  }
  // the path and the split list: the outcome digits must equal the split list's outcomes, every row must exist, every
  // chance must be strictly between 0 and 1, and the weight must equal the product of the split probabilities (a
  // weight that does not follow from its own splits is a wrong leaf, never averaged in)
  if ( good )
  {
    lf.path = tok[ 17 ] == "-" ? std::string() : tok[ 17 ];
    double prod = 1.0;
    if ( tok[ 18 ] != "-" )
    {
      std::stringstream is( tok[ 18 ] );
      std::string item;
      while ( good && std::getline( is, item, ';' ) )
      {
        std::vector<std::string> f;
        std::stringstream fs( item );
        std::string x;
        while ( std::getline( fs, x, ':' ) )
          f.push_back( x );
        long row = -1, outcome = -1;
        split_rec_t sp;
        good = f.size() == 3 && parse_long( f[ 0 ], row ) && row >= 0 && row < static_cast<long>( g_sites.size() ) &&
               parse_double( f[ 1 ], sp.p ) && sp.p > 0.0 && sp.p < 1.0 && parse_long( f[ 2 ], outcome ) &&
               ( outcome == 0 || outcome == 1 );
        if ( good )
        {
          sp.row = static_cast<int>( row );
          sp.outcome = static_cast<int>( outcome );
          prod *= outcome == 1 ? sp.p : 1.0 - sp.p;
          lf.splits.push_back( sp );
        }
      }
    }
    if ( good )
    {
      std::string digits;
      for ( const split_rec_t& sp : lf.splits )
        digits.push_back( sp.outcome == 1 ? '1' : '0' );
      good = digits == lf.path && std::fabs( prod - lf.w ) <= 1e-9;
    }
    for ( const leaf_t& other : r.leaves )
      if ( good && other.path == lf.path && other.rank == static_cast<int>( frank ) )
        good = false;  // two leaves on one path (in one half of a pair)
  }
  if ( !good )
  {
    r.failed = true;
    r.fail = "bad_leaf_line";
    ++g_count.unknown_lines;
    return;
  }
  lf.rank = static_cast<int>( frank );
  lf.sec = static_cast<int>( fsec );
  lf.ended = static_cast<int>( fended );
  lf.n_dec = static_cast<int>( fndec );
  lf.cut_state = static_cast<int>( fcut );
  lf.vmhwm_kb = fhwm;
  // A copy carries one leaf per path; the weights of a complete copy sum to 1 (finalise() checks).
  r.leaves.push_back( lf );
}

// The sums of a sample over its leaves (each leaf weighted by its path weight).
void fill_values( copy_result_t& smp )
{
  smp.w_sum = 0.0;
  smp.value = 0.0;
  smp.value_R = 0.0;
  smp.value_boot = 0.0;
  smp.value_Rund = 0.0;
  smp.value_Rreal = 0.0;
  for ( const leaf_t& lf : smp.leaves )
  {
    smp.w_sum += lf.w;
    smp.value += lf.w * leaf_label( lf );
    smp.value_R += lf.w * lf.R;
    smp.value_boot += lf.w * leaf_boot( lf );
    smp.value_Rund += lf.w * lf.R_undisc;
    smp.value_Rreal += lf.w * lf.R_real;
  }
}

// Pair halves that reached the same path (rank-0 leaf and rank-1 leaf): how many, and how many returned the same number
// to 17 digits (a shared stream would show here; expected 0).
struct pair_half_t
{
  long pairs = 0;
  long ties = 0;
};

// One raw result into SAMPLES (280-06). A plain copy is one sample (rank 0) usable when it has no FAIL / bad line, at
// least one leaf, a clean exit, and leaf weights summing to 1. A pair is two copies spent: its rank-0 sample (the rank-0
// and rank-2 leaves) and, when some lineage forked, its rank-1 sample (the rank-1 and rank-2 leaves); a pair none of whose
// lineages forked is ONE sample (pair_cut_first). A pair that fails in any way is dropped whole: two failed samples.
void finalise( copy_result_t& r, std::vector<copy_result_t>& samples, pair_half_t& ph )
{
  for ( leaf_t& lf : r.leaves )
  {
    auto it = r.stats.find( std::make_pair( lf.rank, lf.path ) );
    if ( it != r.stats.end() )
    {
      lf.stats = it->second;
      lf.has_stats = true;
    }
  }
  const bool base_ok = !r.failed && !r.leaves.empty() && r.exited && r.exit_code == 0 && r.signal == 0;
  if ( !r.pair )
  {
    r.rank = 0;
    fill_values( r );
    r.ok = base_ok && std::fabs( r.w_sum - 1.0 ) < 1e-9;
    if ( !r.ok && r.fail.empty() )
      r.fail = fmt::format( "no-line exit={} signal={} leaves={} w_sum={}", r.exit_code, r.signal, r.leaves.size(),
                            r.w_sum );
    samples.push_back( r );
    return;
  }
  bool forked = false;
  for ( const leaf_t& lf : r.leaves )
    if ( lf.rank == 1 )
      forked = true;
  copy_result_t s0, s1;
  for ( copy_result_t* smp : { &s0, &s1 } )
  {
    smp->cand = r.cand;
    smp->button = r.button;
    smp->pair = true;
    smp->exited = r.exited;
    smp->exit_code = r.exit_code;
    smp->signal = r.signal;
  }
  s0.rank = 0;
  s1.rank = 1;
  for ( const leaf_t& lf : r.leaves )
  {
    if ( lf.rank != 1 )
      s0.leaves.push_back( lf );
    if ( forked && lf.rank != 0 )
      s1.leaves.push_back( lf );
  }
  fill_values( s0 );
  fill_values( s1 );
  const bool ok = base_ok && std::fabs( s0.w_sum - 1.0 ) < 1e-9 && ( !forked || std::fabs( s1.w_sum - 1.0 ) < 1e-9 );
  r.ok = ok;
  if ( !ok && r.fail.empty() )
    r.fail = fmt::format( "pair no-line exit={} signal={} leaves={} w_sum0={} w_sum1={}", r.exit_code, r.signal,
                          r.leaves.size(), s0.w_sum, s1.w_sum );
  s0.ok = s1.ok = ok;
  s0.failed = s1.failed = !ok;
  s0.fail = s1.fail = r.fail;
  s0.pair_forked = forked;
  s0.pair_cut_first = ok && !forked;
  s0.pair_failed = s1.pair_failed = !ok;
  if ( ok && forked )
    for ( const leaf_t& lf : r.leaves )
      if ( lf.rank == 2 )
        s0.pair_mixed = true;
  if ( ok && forked )
    for ( const leaf_t& a : s0.leaves )
      if ( a.rank == 0 )
        for ( const leaf_t& b : s1.leaves )
          if ( b.rank == 1 && b.path == a.path )
          {
            ++ph.pairs;
            if ( a.R == b.R )
              ++ph.ties;
          }
  samples.push_back( s0 );
  if ( forked || !ok )
    samples.push_back( s1 );
}

// Runs one pass: forks one copy per task, at most `jobs` alive at once, and reads every line to end of file.
// Returns true IN A COPY (out_button = its button); false in the parent, with `res` filled.
bool run_pass( sim_t* sim, player_t* p, std::uint64_t seq, int pass, const std::vector<copy_task_t>& tasks,
               std::vector<copy_result_t>& samples, std::vector<copy_result_t>& raw_out, pair_half_t& ph,
               int& out_button )
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

  std::vector<copy_result_t> res( tasks.size(), copy_result_t{} );
  for ( std::size_t i = 0; i < tasks.size(); ++i )
  {
    res[ i ].cand = tasks[ i ].cand;
    res[ i ].button = tasks[ i ].button;
    res[ i ].pair = tasks[ i ].pair;
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
      g_count.children_reaped += r.pair ? 2 : 1;  // a pair is two copies spent
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
                             tasks[ next ].pair, key, c.window, fds[ 0 ], fds[ 1 ] );
        out_button = tasks[ next ].button;
        return true;
      }
      pid_slot[ pid ] = next;
      ++running;
      g_count.children += tasks[ next ].pair ? 2 : 1;  // a pair is two copies spent
      ++next;
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
  samples.clear();
  for ( auto& r : res )
  {
    finalise( r, samples, ph );
    if ( !r.ok )
      g_count.child_fail += r.pair ? 2 : 1;
    for ( const leaf_t& lf : r.leaves )
    {
      g_count.leaf_vmhwm_kb_max = std::max( g_count.leaf_vmhwm_kb_max, lf.vmhwm_kb );
      g_count.search_vmhwm_kb_max = std::max( g_count.search_vmhwm_kb_max, lf.vmhwm_kb );
    }
  }
  raw_out = std::move( res );  // 280-07: the raw copies (leaves, marks, SECOND and CHANCE lines) for the row builder
  return false;
}
#endif  // __linux__

// ---- the search at one decision ----------------------------------------------------------------------
// One CELL: the samples that share a root candidate and a second-level rank (280-06). At depth one only rank 0 exists.
struct cell_t
{
  int n_assigned = 0;        // samples that entered this cell (usable or not)
  int n_ok = 0;
  int n_fail = 0;
  double w_sum = 0.0;
  std::vector<double> vals;  // per-SAMPLE labels, pass 1 then pass 2, in sample order
  std::vector<double> boots; // per-sample bootstrap part of the label (sum of w x term)
  std::vector<double> Rs;    // per-sample window return R without the bootstrap
  std::vector<double> Rund;  // per-sample undiscounted expected damage over the window
  std::vector<double> Rreal; // per-sample realised damage over the window
  std::map<int, double> sec_w;  // second-level button -> weighted leaf count (the modal button of the cell)
  int n_pairs = 0;           // 280-07: pairs that gave a usable sample to this cell
  double end_w = -1.0;       // 280-07: the largest leaf path weight in the cell (leaves taken in sample, then path order)
  std::string end_obs;       // 280-07: that leaf's end observation (base64; empty when its fight ended first)
  double pass1_mean = std::numeric_limits<double>::quiet_NaN();
  double mean = std::numeric_limits<double>::quiet_NaN();
  double se = std::numeric_limits<double>::quiet_NaN();
};

// One root candidate with its two cells.
struct cand_t
{
  int button = -1;
  int rank_in_q = 0;
  int hold = 0;              // 0 not a wait button, 1 a wait button among the top candidates, 2 a wait button added
  int spent = 0;             // copies spent on this candidate (a pair is two)
  int pairs = 0;             // pairs dealt to this candidate
  int pairs_cut_first = 0;   // of those, pairs none of whose lineages reached a second-level decision
  int pairs_failed = 0;
  int pairs_mixed = 0;       // pairs where some lineages forked and some reached the cut first (counted in both samples)
  int singles2 = 0;          // plain copies dealt in pass 2 (the odd copy)
  cell_t cell[ 2 ];
  int best_rank = 0;         // the rank of the cell with the best mean
  double value = std::numeric_limits<double>::quiet_NaN();   // the best cell's mean
  double value_se = std::numeric_limits<double>::quiet_NaN();
  double pass1_value = std::numeric_limits<double>::quiet_NaN();
};

// What the chance splits of one search add up to (280-04), over the copies of every pass.
struct site_total_t
{
  double n = 0.0;
  double sum_p = 0.0;
  double sum_pq = 0.0;
  double n_yes = 0.0;
};

struct split_agg_t
{
  std::vector<site_total_t> rows;  // per site row, each leaf's counts weighted by its path weight, usable copies only
  long splits_total = 0;           // splits taken: leaves minus copies, over usable copies
  long leaves_total = 0;
  long copies_split = 0;           // usable copies with at least one split
  long fork_fail = 0;              // fork() refusals reported by the leaves (a shared prefix is counted once per leaf)
  double max_abs_wsum_err = 0.0;   // largest |sum of leaf weights - 1| over copies that returned leaves cleanly
  long sibling_pairs = 0;          // pairs of sibling leaves (same path but the last digit) in usable copies
  long sibling_ties = 0;           // of those, pairs whose two returns are identical to 17 digits (shared dice?)
};

void add_samples( const std::vector<copy_result_t>& samples, std::vector<cand_t>& cands, split_agg_t& agg,
                  std::map<int, int>& leaves_hist )
{
  if ( agg.rows.size() != g_sites.size() )
    agg.rows.assign( g_sites.size(), site_total_t{} );
  for ( const copy_result_t& r : samples )
  {
    cand_t& cd = cands[ static_cast<std::size_t>( r.cand ) ];
    cell_t& c = cd.cell[ r.rank ];
    ++c.n_assigned;
    if ( r.pair && r.rank == 0 )
    {
      ++cd.pairs;
      if ( r.pair_cut_first )
        ++cd.pairs_cut_first;
      if ( r.pair_failed )
        ++cd.pairs_failed;
      if ( r.pair_mixed )
        ++cd.pairs_mixed;
    }
    if ( !r.failed && !r.leaves.empty() )
      agg.max_abs_wsum_err = std::max( agg.max_abs_wsum_err, std::fabs( r.w_sum - 1.0 ) );
    for ( const leaf_t& lf : r.leaves )
      agg.fork_fail += lf.stats.fork_fail;
    if ( r.ok )
    {
      ++c.n_ok;
      if ( r.pair )
        ++c.n_pairs;
      // 280-07: the cell's end observation is the one of the leaf with the LARGEST path weight (never the highest return,
      // which would select on luck); samples come in copy order, pass 1 first, and the first leaf to reach a weight wins
      // a tie; inside a sample leaves are taken in (path, rank) order so the choice never depends on pipe arrival order.
      {
        std::vector<const leaf_t*> lv;
        for ( const leaf_t& lf : r.leaves )
          lv.push_back( &lf );
        std::sort( lv.begin(), lv.end(), []( const leaf_t* a, const leaf_t* b ) {
          return a->path != b->path ? a->path < b->path : a->rank < b->rank;
        } );
        for ( const leaf_t* lf : lv )
          if ( lf->w > c.end_w )
          {
            c.end_w = lf->w;
            c.end_obs = lf->end_obs;
          }
      }
      c.w_sum += r.w_sum;
      c.vals.push_back( r.value );
      c.boots.push_back( r.value_boot );
      c.Rs.push_back( r.value_R );
      c.Rund.push_back( r.value_Rund );
      c.Rreal.push_back( r.value_Rreal );
      for ( const leaf_t& lf : r.leaves )
        if ( lf.sec >= 0 )
          c.sec_w[ lf.sec ] += lf.w;
      const long nl = static_cast<long>( r.leaves.size() );
      agg.leaves_total += nl;
      agg.splits_total += nl - 1;
      ++leaves_hist[ static_cast<int>( nl ) ];
      if ( nl > 1 )
        ++agg.copies_split;
      // sibling leaves: two leaves whose paths agree on every digit but the last. Independent dice make an identical
      // return to 17 digits essentially impossible, so a tie is the sign of a shared stream (T-280-10).
      for ( std::size_t a = 0; a < r.leaves.size(); ++a )
        for ( std::size_t b = a + 1; b < r.leaves.size(); ++b )
        {
          const std::string& pa = r.leaves[ a ].path;
          const std::string& pb = r.leaves[ b ].path;
          if ( !pa.empty() && pa.size() == pb.size() && pa.compare( 0, pa.size() - 1, pb, 0, pb.size() - 1 ) == 0 &&
               pa.back() != pb.back() )
          {
            ++agg.sibling_pairs;
            if ( r.leaves[ a ].R == r.leaves[ b ].R )
              ++agg.sibling_ties;
          }
        }
      for ( const leaf_t& lf : r.leaves )
        for ( const site_item_t& it : lf.stats.items )
        {
          site_total_t& t = agg.rows[ static_cast<std::size_t>( it.row ) ];
          t.n += lf.w * it.n;
          t.sum_p += lf.w * it.sum_p;
          t.sum_pq += lf.w * it.sum_pq;
          t.n_yes += lf.w * it.n_yes;
        }
    }
    else
    {
      ++c.n_fail;
    }
  }
}

// A JSON string body: backslash and quote escaped (site keys are player and generator names).
std::string jstr( const std::string& v )
{
  std::string o;
  for ( char ch : v )
  {
    if ( ch == '\\' || ch == '"' )
      o += '\\';
    o += ch;
  }
  return o;
}


// ---- the ranking file: reading, binding to the site table, observing, writing (280-05) ------------------------
// Reads solver_search_sites_in with the vendored rapidjson. The top level is a JSON array (the literal reading of C-A);
// every element is an object with site_key (string), type (integer 0..6), occurrences_per_window, p_mean (0..1),
// gap_mean, var_share (finite, not negative) and splits (integer, not negative); the appended cumulative fields
// (window_s, rolls, copies, chance_sum, gap_abs_sum, gap_n, gap2_corrected, eligible) are optional, absent meaning 0;
// unknown keys are ignored. Any violation is refused naming the file, the element index and the field (T-280-12).
void read_sites_in( const std::string& file, double window )
{
  g_in_rows.clear();
  std::ifstream in( file, std::ios::in | std::ios::binary );
  if ( !in.is_open() )
    refuse( fmt::format( "solver_search_sites_in='{}' cannot be opened for reading.", file ) );
  const std::string text( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
  if ( text.size() > ( 64u << 20 ) )
    refuse( fmt::format( "solver_search_sites_in='{}' is larger than 64 MiB.", file ) );
  rapidjson::Document doc;
  doc.Parse( text.c_str() );
  if ( doc.HasParseError() )
    refuse( fmt::format( "solver_search_sites_in='{}' is not parseable JSON (rapidjson error {} at byte {}).", file,
                         static_cast<int>( doc.GetParseError() ), doc.GetErrorOffset() ) );
  if ( !doc.IsArray() )
    refuse( fmt::format( "solver_search_sites_in='{}' must hold a JSON array of sites at the top level.", file ) );

  auto bad = [ & ]( std::size_t i, const char* field, const char* why ) {
    refuse( fmt::format( "solver_search_sites_in='{}': element {} field '{}' {}.", file, i, field, why ) );
  };
  auto get_num = [ & ]( const rapidjson::Value& e, std::size_t i, const char* f, bool required, bool nonneg,
                        bool& present ) -> double {
    present = false;
    if ( !e.HasMember( f ) )
    {
      if ( required )
        bad( i, f, "is missing" );
      return 0.0;
    }
    const rapidjson::Value& v = e[ f ];
    if ( !v.IsNumber() )
      bad( i, f, "is not a number" );
    const double d = v.GetDouble();
    if ( !std::isfinite( d ) )
      bad( i, f, "is not finite" );
    if ( nonneg && d < 0.0 )
      bad( i, f, "is negative" );
    present = true;
    return d;
  };

  std::set<std::string> seen;
  std::size_t i = 0;
  for ( const rapidjson::Value& e : doc.GetArray() )
  {
    if ( !e.IsObject() )
      bad( i, "(element)", "is not a JSON object" );
    stat_row_t r;
    if ( !e.HasMember( "site_key" ) )
      bad( i, "site_key", "is missing" );
    if ( !e[ "site_key" ].IsString() || e[ "site_key" ].GetStringLength() == 0 )
      bad( i, "site_key", "is not a non-empty string" );
    r.key = e[ "site_key" ].GetString();
    if ( !seen.insert( r.key ).second )
      bad( i, "site_key", "repeats an earlier element's key" );
    if ( !e.HasMember( "type" ) )
      bad( i, "type", "is missing" );
    if ( !e[ "type" ].IsInt() || e[ "type" ].GetInt() < 0 || e[ "type" ].GetInt() > 6 )
      bad( i, "type", "is not an integer 0..6" );
    r.type = e[ "type" ].GetInt();
    bool pr = false;
    r.in_occ = get_num( e, i, "occurrences_per_window", true, true, pr );
    r.in_p = get_num( e, i, "p_mean", true, true, pr );
    if ( r.in_p > 1.0 )
      bad( i, "p_mean", "is above 1" );
    r.in_gap_mean = get_num( e, i, "gap_mean", true, true, pr );
    r.in_var_share = get_num( e, i, "var_share", true, true, pr );
    if ( !e.HasMember( "splits" ) )
      bad( i, "splits", "is missing" );
    if ( !e[ "splits" ].IsInt64() || e[ "splits" ].GetInt64() < 0 )
      bad( i, "splits", "is not a non-negative integer" );
    r.splits = e[ "splits" ].GetInt64();
    // the appended cumulative fields (optional)
    r.window_s = get_num( e, i, "window_s", false, true, r.has_window );
    if ( r.has_window && std::fabs( r.window_s - window ) > 1e-9 )
      bad( i, "window_s", "differs from solver_search_window (the cumulative counts are per window)" );
    bool b = false;
    r.rolls = get_num( e, i, "rolls", false, true, b );
    r.copies = get_num( e, i, "copies", false, true, b );
    r.chance_sum = get_num( e, i, "chance_sum", false, true, b );
    r.gap_abs_sum = get_num( e, i, "gap_abs_sum", false, true, b );
    r.gap_n = get_num( e, i, "gap_n", false, true, r.has_gap_n );
    double g2 = get_num( e, i, "gap2_corrected", false, false, b );
    r.gap2_corr_sum = g2 * r.gap_n;
    if ( e.HasMember( "eligible" ) )
    {
      if ( !e[ "eligible" ].IsBool() )
        bad( i, "eligible", "is not true or false" );
      r.has_eligible = true;
      r.eligible = e[ "eligible" ].GetBool();
    }
    // the exponentially weighted mean continues from the in-file value when the file says it has observations
    r.gap_ewma = r.in_gap_mean;
    r.gap_seeded = r.has_gap_n ? r.gap_n > 0.0 : r.in_gap_mean > 0.0;
    g_in_rows.push_back( r );
    ++i;
  }
}

// The in-file row's part in ranking: it counts when it has gap observations (gap_n > 0; with no gap_n field, a gap_mean
// above 0).
bool in_row_ranks( const stat_row_t& r )
{
  return r.has_gap_n ? r.gap_n > 0.0 : r.in_gap_mean > 0.0;
}

// Joins the site table with the file rows (matching keys), carries the rest, and picks the run's ranked set. Run once
// by ensure_sites() right after the table is built.
void bind_site_stats()
{
  g_stats.assign( g_sites.size(), stat_row_t{} );
  g_carried.clear();
  g_ranked_flag.assign( g_sites.size(), 0 );
  g_ranked_count = 0;
  g_ranked_top.clear();
  std::unordered_map<std::string, std::size_t> by_key;
  for ( std::size_t i = 0; i < g_in_rows.size(); ++i )
    by_key[ g_in_rows[ i ].key ] = i;
  std::vector<char> used( g_in_rows.size(), 0 );
  for ( std::size_t i = 0; i < g_sites.size(); ++i )
  {
    auto it = by_key.find( g_sites[ i ].key );
    if ( it != by_key.end() )
    {
      if ( g_in_rows[ it->second ].type != g_sites[ i ].type )
        refuse( fmt::format( "solver_search_sites_in='{}': site '{}' has type {} in the file but type {} in this fight.",
                             g_cfg.sites_in, g_sites[ i ].key, g_in_rows[ it->second ].type, g_sites[ i ].type ) );
      g_stats[ i ] = g_in_rows[ it->second ];
      used[ it->second ] = 1;
    }
    g_stats[ i ].key = g_sites[ i ].key;
    g_stats[ i ].type = g_sites[ i ].type;
    g_stats[ i ].eligible = g_sites[ i ].eligible;
    g_stats[ i ].has_eligible = true;
  }
  for ( std::size_t i = 0; i < g_in_rows.size(); ++i )
    if ( !used[ i ] )
      g_carried.push_back( g_in_rows[ i ] );

  // the ranked set: the top S eligible rows of THIS fight's table by in-file var_share among rows that have gap
  // observations, ties by key; fixed for the whole run (nothing this run observes changes it)
  std::vector<std::size_t> cand;
  for ( std::size_t i = 0; i < g_sites.size(); ++i )
    if ( g_sites[ i ].eligible && by_key.count( g_sites[ i ].key ) && in_row_ranks( g_stats[ i ] ) )
      cand.push_back( i );
  std::sort( cand.begin(), cand.end(), [ & ]( std::size_t a, std::size_t b ) {
    if ( g_stats[ a ].in_var_share != g_stats[ b ].in_var_share )
      return g_stats[ a ].in_var_share > g_stats[ b ].in_var_share;
    return g_sites[ a ].key < g_sites[ b ].key;
  } );
  const std::size_t S = static_cast<std::size_t>( effective_splits() );
  for ( std::size_t j = 0; j < cand.size() && j < S; ++j )
  {
    g_ranked_flag[ cand[ j ] ] = 1;
    ++g_ranked_count;
    g_ranked_top.push_back( fmt::format( "{}:{:.6g}", g_sites[ cand[ j ] ].key, g_stats[ cand[ j ] ].in_var_share ) );
  }
  g_stats_bound = true;
}

// The mode the search log and the stderr receipt name.
const char* split_mode_name()
{
  if ( g_audit )
    return "nosplit_audit";
  if ( !g_cfg.split_site.empty() )
    return "proof_site";
  const int S = effective_splits();
  if ( g_ranked_count == 0 )
    return "cold_start";
  return g_ranked_count >= S ? "ranked" : "ranked+cold_start";
}

// One completed split node's gap, in observation order.
struct gap_obs_t
{
  int row = 0;
  double gap = 0.0;
};

// The gap observations of one pass, in copy order and, inside a copy, in lexicographic order of the node's path prefix
// (a fixed order). A copy's split tree: a node is a path prefix; its two sides are the prefix plus '0' and plus '1'; a side's
// value is the weight-normalised mean of the leaf returns under it. A node counts only in a usable copy and only when
// both sides have leaves; the gap is |V_yes - V_no| of that one copy.
void collect_gaps( const std::vector<copy_result_t>& res, std::vector<gap_obs_t>& out )
{
  struct node_t
  {
    double w[ 2 ] = { 0.0, 0.0 };
    double wr[ 2 ] = { 0.0, 0.0 };
    int row = -1;
    bool inconsistent = false;
  };
  for ( const copy_result_t& r : res )
  {
    if ( !r.ok )
      continue;
    std::map<std::string, node_t> nodes;
    for ( const leaf_t& lf : r.leaves )
      for ( std::size_t i = 0; i < lf.path.size() && i < lf.splits.size(); ++i )
      {
        node_t& nd = nodes[ lf.path.substr( 0, i ) ];
        const int d = lf.path[ i ] == '1' ? 1 : 0;
        const int row = lf.splits[ i ].row;
        if ( nd.row < 0 )
          nd.row = row;
        else if ( nd.row != row )
          nd.inconsistent = true;
        nd.w[ d ] += lf.w;
        nd.wr[ d ] += lf.w * leaf_label( lf );
      }
    for ( const auto& kv : nodes )
    {
      const node_t& nd = kv.second;
      if ( nd.inconsistent || nd.row < 0 || !( nd.w[ 0 ] > 0.0 ) || !( nd.w[ 1 ] > 0.0 ) )
        continue;
      out.push_back( gap_obs_t{ nd.row, std::fabs( nd.wr[ 1 ] / nd.w[ 1 ] - nd.wr[ 0 ] / nd.w[ 0 ] ) } );
    }
  }
}

// Adds one finished search to the run's statistics: every table row sees every usable copy; its rolls and chance sum are
// the path-weighted totals of agg; each gap observation updates its site (the exponentially weighted mean seeds on the
// first one). pooled_var is the search's pooled within-cell variance of copy values (diagnostic only).
void apply_search_stats( const split_agg_t& agg, long copies_ok, const std::vector<gap_obs_t>& obs, double pooled_var )
{
  if ( !g_stats_bound )
    return;
  for ( std::size_t i = 0; i < g_stats.size() && i < agg.rows.size(); ++i )
  {
    g_stats[ i ].copies += static_cast<double>( copies_ok );
    g_stats[ i ].rolls += agg.rows[ i ].n;
    g_stats[ i ].chance_sum += agg.rows[ i ].sum_p;
  }
  for ( const gap_obs_t& o : obs )
  {
    stat_row_t& s = g_stats[ static_cast<std::size_t>( o.row ) ];
    ++s.splits;
    s.gap_n += 1.0;
    s.has_gap_n = true;
    s.gap_abs_sum += o.gap;
    s.gap2_corr_sum += o.gap * o.gap - 2.0 * pooled_var;
    s.gap_ewma = s.gap_seeded ? ( 1.0 - GAP_ALPHA ) * s.gap_ewma + GAP_ALPHA * o.gap : o.gap;
    s.gap_seeded = true;
    ++g_gap_obs_total;
  }
}

// The merged table as the JSON array solver_search_sites_out holds: every row of this fight's table (in-file statistics
// plus this run's) and every carried row, var_share recomputed over the union (so it sums to 1), ranked by var_share
// descending with ties by key; the C-A fields first and in that order, the cumulative fields after them.
std::string sites_table_json( double window )
{
  std::vector<const stat_row_t*> rows;
  if ( g_stats_bound )
  {
    for ( const stat_row_t& r : g_stats )
      rows.push_back( &r );
    for ( const stat_row_t& r : g_carried )
      rows.push_back( &r );
  }
  else
  {
    // no search ran: nothing was observed, the file's rows go back as they came
    for ( const stat_row_t& r : g_in_rows )
      rows.push_back( &r );
  }
  std::vector<double> raw( rows.size(), 0.0 );
  double total = 0.0;
  for ( std::size_t i = 0; i < rows.size(); ++i )
  {
    const stat_row_t& r = *rows[ i ];
    const double p = r.p_mean();
    raw[ i ] = p * ( 1.0 - p ) * r.gap_mean() * r.gap_mean() * r.occ();
    total += raw[ i ];
  }
  std::vector<std::size_t> order( rows.size() );
  for ( std::size_t i = 0; i < order.size(); ++i )
    order[ i ] = i;
  std::stable_sort( order.begin(), order.end(), [ & ]( std::size_t a, std::size_t b ) {
    if ( raw[ a ] != raw[ b ] )
      return raw[ a ] > raw[ b ];
    return rows[ a ]->key < rows[ b ]->key;
  } );
  std::string out = "[";
  bool first = true;
  for ( std::size_t oi : order )
  {
    const stat_row_t& r = *rows[ oi ];
    const bool elig = r.has_eligible ? r.eligible : ( r.type >= 1 && r.type <= 4 );
    const double share = total > 0.0 ? raw[ oi ] / total : 0.0;
    out += first ? "\n  " : ",\n  ";
    first = false;
    out += fmt::format(
        "{{\"site_key\":\"{}\",\"type\":{},\"occurrences_per_window\":{},\"p_mean\":{},\"gap_mean\":{},"
        "\"var_share\":{},\"splits\":{},\"window_s\":{},\"rolls\":{},\"copies\":{},\"chance_sum\":{},"
        "\"gap_abs_sum\":{},\"gap_n\":{},\"gap2_corrected\":{},\"eligible\":{}}}",
        jstr( r.key ), r.type, jnum( r.occ() ), jnum( r.p_mean() ), jnum( r.gap_mean() ), jnum( share ), r.splits,
        jnum( window ),  // read_sites_in() refused any file row whose window_s differs from this run's window
        jnum( r.rolls ), jnum( r.copies ), jnum( r.chance_sum ), jnum( r.gap_abs_sum ), jnum( r.gap_n ),
        jnum( r.gap_n > 0.0 ? r.gap2_corr_sum / r.gap_n : 0.0 ), elig ? "true" : "false" );
  }
  out += first ? "]\n" : "\n]\n";
  return out;
}

// Written once by the parent at exit: a temp file beside the target, then rename (T-280-13).
void write_sites_out( const std::string& file, double window )
{
  const std::string tmp = fmt::format( "{}.tmp.{}", file, static_cast<long>( ::getpid() ) );
  {
    std::ofstream o( tmp, std::ios::out | std::ios::trunc | std::ios::binary );
    if ( !o.is_open() )
      throw sc_runtime_error( fmt::format( "solver_search_sites_out='{}': cannot write the temporary file '{}'.", file, tmp ) );
    o << sites_table_json( window );
    o.flush();
    if ( !o.good() )
    {
      o.close();
      std::remove( tmp.c_str() );
      throw sc_runtime_error( fmt::format( "solver_search_sites_out='{}': writing '{}' failed.", file, tmp ) );
    }
  }
  if ( std::rename( tmp.c_str(), file.c_str() ) != 0 )
  {
    std::remove( tmp.c_str() );
    throw sc_runtime_error( fmt::format( "solver_search_sites_out='{}': renaming '{}' failed: {}.", file, tmp,
                                         std::strerror( errno ) ) );
  }
}

// The site table, built ONCE in the parent at its first search by a read-only walk of every player's proc_rng_list
// (no tag, no state change). Keys: the stream key proc_rng.cpp builds for a generator ("<player>|procrng|<index>|<type>|
// <name>"), and "<player>|deck|<name>" for a deck (G280-4). Eligible: simple, real-PPM, accumulated and deck. A
// threshold generator is registered but never eligible: it draws through range(), never roll(), so this hook cannot
// split it (recorded deviation from G280-4); a custom generator is not eligible either.
void ensure_sites( sim_t* sim )
{
  if ( g_sites_built )
    return;
  for ( player_t* a : sim->actor_list )
  {
    if ( !a )
      continue;
    for ( std::size_t i = 0; i < a->proc_rng_list.size(); ++i )
    {
      proc_rng_t* r = a->proc_rng_list[ i ];
      if ( !r )
        continue;
      site_row_t row;
      row.type = static_cast<int>( r->type() );
      row.ptr = r;
      row.deck = row.type == static_cast<int>( RNG_SHUFFLE );
      row.eligible = row.type == static_cast<int>( RNG_SIMPLE ) || row.type == static_cast<int>( RNG_RPPM ) ||
                     row.type == static_cast<int>( RNG_SHUFFLE ) || row.type == static_cast<int>( RNG_ACCUMULATE );
      row.key = row.deck ? fmt::format( "{}|deck|{}", a->name_str, r->name() )
                         : fmt::format( "{}|procrng|{}|{}|{}", a->name_str, i, row.type, r->name() );
      g_sites.push_back( row );
    }
  }
  g_sites_built = true;
  bind_site_stats();  // 280-05: the file's rows joined to the table, the ranked set fixed for the run
  if ( !g_cfg.split_site.empty() )
  {
    for ( std::size_t i = 0; i < g_sites.size(); ++i )
      if ( g_sites[ i ].key == g_cfg.split_site )
      {
        if ( !g_sites[ i ].eligible )
          refuse( fmt::format( "solver_search_split_site='{}' names a site that can never split (type {}: a threshold "
                               "or custom generator does not call roll()).",
                               g_cfg.split_site, g_sites[ i ].type ) );
        g_proof_row = static_cast<int>( i );
        break;
      }
    if ( g_proof_row < 0 )
      refuse( fmt::format( "solver_search_split_site='{}' is not a site of this fight ({} sites in the table).",
                           g_cfg.split_site, g_sites.size() ) );
  }
  if ( g_log )
  {
    std::string rows;
    for ( std::size_t i = 0; i < g_sites.size(); ++i )
      rows += fmt::format( "{}{{\"row\":{},\"key\":\"{}\",\"type\":{},\"eligible\":{}}}", i ? "," : "", i,
                           jstr( g_sites[ i ].key ), g_sites[ i ].type, g_sites[ i ].eligible ? 1 : 0 );
    *g_log << fmt::format( "{{\"sites\":1,\"count\":{},\"rows\":[{}]}}\n", g_sites.size(), rows );
    g_log->flush();
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

// One root candidate as the decision builds it (280-06): the button, its rank by q over the selectable buttons, and
// whether it is a wait button (1) or a wait button ADDED as the hold candidate (2).
struct cand_spec_t
{
  int button = 0;
  int rank_q = 0;
  int hold = 0;
};

// A cell's most pressed second-level button (weighted by leaf weight; ties to the lower button), -1 when none.
int modal_sec( const cell_t& cl, double& n )
{
  int best = -1;
  n = 0.0;
  for ( const auto& kv : cl.sec_w )
    if ( kv.second > n )
    {
      n = kv.second;
      best = kv.first;
    }
  return best;
}


// ---- the record: rows, the buffer, the summary figures (280-07, SRCH-08) -----------------------------------------
// Every row is built as one JSON line in the parent right after its search and kept in memory; write_summary() appends them
// to solver_search_record at the end of the fight (O_APPEND, one write() per line) and rewrites solver_search_summary.
std::vector<std::string> g_rows;
long long g_rows_root = 0, g_rows_second = 0, g_rows_chance = 0, g_rare_rows = 0;
long long g_record_bytes = 0;
double g_copies_rows = 0.0;           // copies spent by the searches that wrote rows
long long g_samples_total = 0, g_pair_cut_total = 0, g_fork_fail_total = 0;
long long g_chance_dropped_cap = 0;   // chance rows the solver_search_chance_rows cap left out
long long g_rows_skipped_mark = 0;    // interior rows left out because a leaf lacked the mark (never expected)

struct root_stat_t
{
  bool audit = false;
  bool decisive = false;
  double se_pct = std::numeric_limits<double>::quiet_NaN();
  bool hold_any = false;
  bool hold_won = false;
  double hold_payoff = std::numeric_limits<double>::quiet_NaN();
  int second_changed = -1;  // -1 depth 1 (no second level), 0 or 1
};
std::vector<root_stat_t> g_root_stats;

// What every row of one search shares.
struct rec_ctx_t
{
  std::string fight_json;
  double window_s = 0.0;
  int copies = 0;
  double cpu_s = 0.0;
  std::string boot_mode;
  double divisor = 1.0;
  double gamma = 1.0;
  std::int64_t k = 0;
  bool audit = false;
  std::string split_mode;
  int depth = 2;
  std::uint64_t root_seq = 0;
};

std::string json_str_or_null( const std::string& v )
{
  return v.empty() ? std::string( "null" ) : fmt::format( "\"{}\"", v );
}

std::string json_bool( bool b )
{
  return b ? "true" : "false";
}

std::string mask_json( const std::string& digits )
{
  std::string o = "[";
  for ( std::size_t i = 0; i < digits.size(); ++i )
  {
    if ( i )
      o += ',';
    o += digits[ i ];
  }
  return o + "]";
}

std::string hold_event_json( int button )
{
  return RL_ACTIONS[ button ].kind == rl_action_kind::wait ? fmt::format( "\"{}\"", jstr( RL_ACTIONS[ button ].label ) )
                                                           : std::string( "null" );
}

std::string cand_json( int button, double mean, double se, int n_ok, int n_fail, double w_sum, const std::string& second,
                       const std::string& end_obs )
{
  return fmt::format(
      "{{\"button\":{},\"hold_event\":{},\"mean\":{},\"se\":{},\"n_ok\":{},\"n_fail\":{},\"w_sum\":{},\"second\":[{}],"
      "\"end_obs_b64\":{}}}",
      button, hold_event_json( button ), jnum( mean ), jnum( se ), n_ok, n_fail, jnum( w_sum ), second,
      json_str_or_null( end_obs ) );
}

std::string splits_json_n( const std::vector<split_rec_t>& sp, std::size_t n )
{
  std::string o;
  for ( std::size_t i = 0; i < n && i < sp.size(); ++i )
    o += fmt::format( "{}{{\"site\":\"{}\",\"p\":{},\"outcome\":{}}}", i ? "," : "",
                      jstr( g_sites[ static_cast<std::size_t>( sp[ i ].row ) ].key ), jnum( sp[ i ].p ), sp[ i ].outcome );
  return o;
}

double outcome_prob( const split_rec_t& sp )
{
  return sp.outcome == 1 ? sp.p : 1.0 - sp.p;
}

bool starts_with( const std::string& s, const std::string& pre )
{
  return s.size() >= pre.size() && s.compare( 0, pre.size(), pre ) == 0;
}

// The tail every row ends with: C-A's twenty fields come first, then these recorded additions in this order.
std::string row_tail( const rec_ctx_t& cx, double window_return, const std::string& second_changed,
                      const std::string& hold_won )
{
  return fmt::format(
      ",\"label_units\":\"reward_discounted_over_divisor\",\"boot_mode\":\"{}\",\"window_return\":{},\"divisor\":{},"
      "\"gamma\":{},\"k\":{},\"audit_nosplit\":{},\"second_level_changed\":{},\"hold_won\":{},\"split_mode\":\"{}\","
      "\"depth\":{}}}",
      cx.boot_mode, jnum( window_return ), jnum( cx.divisor ), jnum( cx.gamma ), cx.k, json_bool( cx.audit ), second_changed,
      hold_won, cx.split_mode, cx.depth );
}

// One row, C-A's field order: rec_version, kind, seq, parent_seq, fight, t, obs_b64, mask, cands, pick, second_pick, margin,
// margin_se, decisive, path_weight, window_s, copies, cpu_s, splits, bootstrap_share; then the tail.
std::string make_row( const rec_ctx_t& cx, const char* kind, std::uint64_t seq, const std::string& parent_seq, double t,
                      const std::string& obs, const std::string& mask, const std::string& cands, int pick,
                      const std::string& second_pick, double margin, double margin_se, bool decisive, double path_weight,
                      const std::string& splits, double bootstrap_share, const std::string& tail )
{
  return fmt::format(
      "{{\"rec_version\":1,\"kind\":\"{}\",\"seq\":{},\"parent_seq\":{},\"fight\":{},\"t\":{},\"obs_b64\":\"{}\",\"mask\":{},"
      "\"cands\":[{}],\"pick\":{},\"second_pick\":{},\"margin\":{},\"margin_se\":{},\"decisive\":{},\"path_weight\":{},"
      "\"window_s\":{},\"copies\":{},\"cpu_s\":{},\"splits\":[{}],\"bootstrap_share\":{}{}",
      kind, seq, parent_seq, cx.fight_json, jnum( t ), obs, mask, cands, pick, second_pick, jnum( margin ),
      jnum( margin_se ), json_bool( decisive ), jnum( path_weight ), jnum( cx.window_s ), cx.copies, jnum( cx.cpu_s ), splits,
      jnum( bootstrap_share ), tail );
}

// A set of leaves read from one marked decision: the weight-normalised mean of the label (window return plus bootstrap
// term, both measured from that decision), the return and bootstrap parts alone, the weights, and the leaf with the largest
// path weight (ties to the first in (path, rank) order).
struct group_t
{
  bool ok = false;
  int n = 0;
  double w_sum = 0.0;
  double label = std::numeric_limits<double>::quiet_NaN();
  double R = std::numeric_limits<double>::quiet_NaN();
  double boot = std::numeric_limits<double>::quiet_NaN();
  const leaf_t* best = nullptr;
};

group_t summarise_group( std::vector<const leaf_t*> lv, const std::string& key )
{
  group_t g;
  std::sort( lv.begin(), lv.end(), []( const leaf_t* a, const leaf_t* b ) {
    return a->path != b->path ? a->path < b->path : a->rank < b->rank;
  } );
  double sw = 0.0, sl = 0.0, sr = 0.0, sb = 0.0;
  for ( const leaf_t* lf : lv )
  {
    auto it = lf->rc.find( key );
    if ( it == lf->rc.end() )
      return g;  // a leaf without the mark: the group is unusable
    sw += lf->w;
    sl += lf->w * ( it->second.first + it->second.second );
    sr += lf->w * it->second.first;
    sb += lf->w * it->second.second;
    if ( !g.best || lf->w > g.best->w )
      g.best = lf;
    ++g.n;
  }
  if ( g.n == 0 || !( sw > 0.0 ) )
    return g;
  g.ok = true;
  g.w_sum = sw;
  g.label = sl / sw;
  g.R = sr / sw;
  g.boot = sb / sw;
  return g;
}

double share_of( const group_t& g )
{
  return g_boot_mode != 0 && g.label != 0.0 && std::isfinite( g.label ) ? g.boot / g.label
                                                                        : std::numeric_limits<double>::quiet_NaN();
}

bool rare_split_list( const std::vector<split_rec_t>& sp, std::size_t n )
{
  for ( std::size_t i = 0; i < n && i < sp.size(); ++i )
    if ( outcome_prob( sp[ i ] ) < 0.1 )
      return true;
  return false;
}

// The second and chance rows of one search (the root row is built by the caller). Second rows: one per SECOND line of each
// usable pair of the winning candidate. Chance rows: every CHANCE line of every usable copy, kept up to the cap in the order
// (under the winning candidate, the less likely outcome of the last split, the lower copy number).
void build_interior_rows( const rec_ctx_t& cx, int pick_i, const std::vector<const std::vector<copy_result_t>*>& passes,
                          std::vector<std::string>& out, long long& n_second, long long& n_chance, long long& n_rare )
{
  const std::string parent = std::to_string( cx.root_seq );
  // -- second rows
  for ( std::size_t pi = 0; pi < passes.size(); ++pi )
    for ( std::size_t ci = 0; ci < passes[ pi ]->size(); ++ci )
    {
      const copy_result_t& r = ( *passes[ pi ] )[ ci ];
      if ( !r.ok || !r.pair || r.cand != pick_i )
        continue;
      std::vector<const interior_line_t*> sl;
      for ( const interior_line_t& il : r.interior )
        if ( il.second )
          sl.push_back( &il );
      std::sort( sl.begin(), sl.end(), []( const interior_line_t* a, const interior_line_t* b ) { return a->path < b->path; } );
      for ( const interior_line_t* il : sl )
      {
        std::vector<const leaf_t*> h0, h1;
        for ( const leaf_t& lf : r.leaves )
          if ( starts_with( lf.path, il->path ) )
          {
            if ( lf.rank == 0 )
              h0.push_back( &lf );
            else if ( lf.rank == 1 )
              h1.push_back( &lf );
          }
        const std::string key = "S" + ( il->path.empty() ? std::string( "-" ) : il->path );
        const group_t g0 = summarise_group( h0, key );
        const group_t g1 = summarise_group( h1, key );
        if ( !g0.ok || !g1.ok )
        {
          ++g_rows_skipped_mark;
          continue;
        }
        const bool first_better = g0.label >= g1.label;
        const group_t& gb = first_better ? g0 : g1;
        const std::vector<split_rec_t>& spl = g0.best->splits;
        const std::string cands =
            cand_json( il->b0, g0.label, std::numeric_limits<double>::quiet_NaN(), 1, 0, g0.w_sum, "",
                       g0.best->end_obs ) +
            "," +
            cand_json( il->b1, g1.label, std::numeric_limits<double>::quiet_NaN(), 1, 0, g1.w_sum, "", g1.best->end_obs );
        out.push_back( make_row( cx, "second", il->seq, parent, il->t, il->obs, mask_json( il->mask ), cands,
                                 first_better ? il->b0 : il->b1, "null", std::fabs( g0.label - g1.label ),
                                 std::numeric_limits<double>::quiet_NaN(), false, il->w,
                                 splits_json_n( spl, il->path.size() ), share_of( gb ),
                                 row_tail( cx, gb.R, "null", "null" ) ) );
        ++n_second;
        if ( rare_split_list( spl, il->path.size() ) )
          ++n_rare;
      }
    }
  // -- chance rows
  struct chance_cand_t
  {
    std::size_t pass = 0;
    std::size_t copy = 0;
    const interior_line_t* il = nullptr;
    group_t g;
    bool under = false;
    double out_prob = 1.0;
    const std::vector<split_rec_t>* splits = nullptr;
  };
  std::vector<chance_cand_t> cc;
  for ( std::size_t pi = 0; pi < passes.size(); ++pi )
    for ( std::size_t ci = 0; ci < passes[ pi ]->size(); ++ci )
    {
      const copy_result_t& r = ( *passes[ pi ] )[ ci ];
      if ( !r.ok )
        continue;
      for ( const interior_line_t& il : r.interior )
      {
        if ( il.second )
          continue;
        std::vector<const leaf_t*> lv;
        for ( const leaf_t& lf : r.leaves )
          if ( starts_with( lf.path, il.path ) && ( il.rk == 2 || lf.rank == il.rk ) )
            lv.push_back( &lf );
        chance_cand_t c;
        c.pass = pi;
        c.copy = ci;
        c.il = &il;
        c.g = summarise_group( lv, "C" + il.path );
        if ( !c.g.ok )
        {
          ++g_rows_skipped_mark;
          continue;
        }
        c.under = r.cand == pick_i;
        c.splits = &c.g.best->splits;
        c.out_prob = outcome_prob( c.g.best->splits[ il.path.size() - 1 ] );
        cc.push_back( c );
      }
    }
  std::stable_sort( cc.begin(), cc.end(), []( const chance_cand_t& a, const chance_cand_t& b ) {
    if ( a.under != b.under )
      return a.under;
    if ( a.out_prob != b.out_prob )
      return a.out_prob < b.out_prob;
    if ( a.pass != b.pass )
      return a.pass < b.pass;
    if ( a.copy != b.copy )
      return a.copy < b.copy;
    if ( a.il->path != b.il->path )
      return a.il->path < b.il->path;
    return a.il->rk < b.il->rk;
  } );
  const std::size_t cap = static_cast<std::size_t>( g_cfg.chance_rows );
  if ( cc.size() > cap )
  {
    g_chance_dropped_cap += static_cast<long long>( cc.size() - cap );
    cc.resize( cap );
  }
  std::sort( cc.begin(), cc.end(), []( const chance_cand_t& a, const chance_cand_t& b ) {
    if ( a.pass != b.pass )
      return a.pass < b.pass;
    if ( a.copy != b.copy )
      return a.copy < b.copy;
    if ( a.il->path != b.il->path )
      return a.il->path < b.il->path;
    return a.il->rk < b.il->rk;
  } );
  for ( const chance_cand_t& c : cc )
  {
    const interior_line_t& il = *c.il;
    const std::string cands = cand_json( il.b0, c.g.label, std::numeric_limits<double>::quiet_NaN(), 1, 0, c.g.w_sum, "",
                                         c.g.best->end_obs );
    out.push_back( make_row( cx, "chance", il.seq, parent, il.t, il.obs, mask_json( il.mask ), cands, il.b0, "null",
                             std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), false, il.w,
                             splits_json_n( *c.splits, il.path.size() ), share_of( c.g ),
                             row_tail( cx, c.g.R, "null", "null" ) ) );
    ++n_chance;
    if ( rare_split_list( *c.splits, il.path.size() ) )
      ++n_rare;
  }
}

// Runs the whole search at one decision. Returns true IN A COPY (idx = the copy's candidate button).
bool search_here( sim_t* sim, player_t* p, std::uint64_t seq, const float* obs, const std::uint8_t* mask, const float* q,
                  const std::vector<cand_spec_t>& specs, int& idx, bool listed )
{
#if defined( __linux__ )
  const config_t& c = g_cfg;
  const std::int64_t k = static_cast<std::int64_t>( seq ) - 1;
  const int C = static_cast<int>( specs.size() );
  const int K = c.copies;
  const int net = idx;
  const bool d2 = c.depth == 2;

  const double self0 = rusage_cpu_s( RUSAGE_SELF );
  const double kids0 = rusage_cpu_s( RUSAGE_CHILDREN );
  const auto wall0 = std::chrono::steady_clock::now();
  const double t_k = t_float32( sim );
  g_count.search_vmhwm_kb_max = 0;

  split_agg_t agg;
  agg.rows.assign( g_sites.size(), site_total_t{} );
  std::vector<cand_t> cands( static_cast<std::size_t>( C ) );
  bool hold_added = false;
  bool hold_any = false;
  for ( int i = 0; i < C; ++i )
  {
    cand_t& cd = cands[ static_cast<std::size_t>( i ) ];
    cd.button = specs[ static_cast<std::size_t>( i ) ].button;
    cd.rank_in_q = specs[ static_cast<std::size_t>( i ) ].rank_q;
    cd.hold = specs[ static_cast<std::size_t>( i ) ].hold;
    hold_any = hold_any || cd.hold != 0;
    hold_added = hold_added || cd.hold == 2;
  }
  std::map<int, int> leaves_hist;
  pair_half_t ph;

  // ---- pass 1: solver_search_pass1 copies per candidate (default 8; candidate-major), no second-level override: the (candidate, rank 0) cell ----
  std::vector<copy_task_t> t1;
  for ( int i = 0; i < C; ++i )
    for ( int j = 0; j < c.pass1; ++j )
      t1.push_back( copy_task_t{ i, cands[ static_cast<std::size_t>( i ) ].button, false } );
  std::vector<copy_result_t> r1, raw1, raw2;
  int child_button = -1;
  if ( run_pass( sim, p, seq, 1, t1, r1, raw1, ph, child_button ) )
  {
    idx = child_button;
    return true;
  }
  for ( cand_t& cd : cands )
    cd.spent = c.pass1;
  add_samples( r1, cands, agg, leaves_hist );
  std::vector<gap_obs_t> gap_obs;  // 280-05: one observation per completed split node, pass 1 then pass 2
  collect_gaps( r1, gap_obs );
  // the sites split on the first copy of pass 1 (the log receipt of the cold-start rule, SRCH-04)
  std::string first_copy_sites;
  if ( !r1.empty() )
  {
    std::vector<int> seen;
    for ( const leaf_t& lf : r1[ 0 ].leaves )
      for ( const split_rec_t& sp : lf.splits )
        if ( std::find( seen.begin(), seen.end(), sp.row ) == seen.end() )
        {
          seen.push_back( sp.row );
          if ( !first_copy_sites.empty() )
            first_copy_sites += ',';
          first_copy_sites += g_sites[ static_cast<std::size_t>( sp.row ) ].key;
        }
  }
  if ( first_copy_sites.empty() )
    first_copy_sites = "-";
  for ( cand_t& cd : cands )
  {
    double se_unused = 0.0;
    mean_se( cd.cell[ 0 ].vals, cd.cell[ 0 ].pass1_mean, se_unused );
  }

  // ---- halving: drop the worst candidate by its pass-1 mean (a tie drops the lower-ranked one) ----
  const int R2 = K - c.pass1 * C;
  int dropped = -1;
  std::vector<int> order;  // candidate indices, best pass-1 mean first (ties: higher rank first)
  for ( int i = 0; i < C; ++i )
    order.push_back( i );
  const double lowest = -std::numeric_limits<double>::infinity();
  auto p1 = [ & ]( int i ) {
    const double m = cands[ static_cast<std::size_t>( i ) ].cell[ 0 ].pass1_mean;
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

  // ---- pass 2: the remaining copies, round-robin over the survivors, best pass-1 mean first. At depth 2 they are
  // PAIRS (two copies each; the odd copy is one plain copy for the best survivor); at depth 1 they are plain copies. ----
  if ( R2 > 0 )
  {
    std::vector<copy_task_t> t2;
    const std::size_t nsurv = survivors.size();
    if ( d2 )
    {
      const int n_pairs = R2 / 2;
      for ( int j = 0; j < n_pairs; ++j )
      {
        const int cand = survivors[ static_cast<std::size_t>( j ) % nsurv ];
        t2.push_back( copy_task_t{ cand, cands[ static_cast<std::size_t>( cand ) ].button, true } );
        cands[ static_cast<std::size_t>( cand ) ].spent += 2;
      }
      if ( R2 % 2 != 0 )
      {
        const int cand = survivors[ 0 ];
        t2.push_back( copy_task_t{ cand, cands[ static_cast<std::size_t>( cand ) ].button, false } );
        cands[ static_cast<std::size_t>( cand ) ].spent += 1;
        ++cands[ static_cast<std::size_t>( cand ) ].singles2;
      }
    }
    else
    {
      for ( int j = 0; j < R2; ++j )
      {
        const int cand = survivors[ static_cast<std::size_t>( j ) % nsurv ];
        t2.push_back( copy_task_t{ cand, cands[ static_cast<std::size_t>( cand ) ].button, false } );
        cands[ static_cast<std::size_t>( cand ) ].spent += 1;
      }
    }
    std::vector<copy_result_t> r2;
    if ( run_pass( sim, p, seq, 2, t2, r2, raw2, ph, child_button ) )
    {
      idx = child_button;
      return true;
    }
    add_samples( r2, cands, agg, leaves_hist );
    collect_gaps( r2, gap_obs );
  }

  // ---- per-candidate cells over SAMPLES (never over leaves); value = the best cell's mean ----
  for ( cand_t& cd : cands )
  {
    for ( cell_t& cl : cd.cell )
      mean_se( cl.vals, cl.mean, cl.se );
    const cell_t& a0 = cd.cell[ 0 ];
    const cell_t& a1 = cd.cell[ 1 ];
    cd.best_rank = ( std::isnan( a0.mean ) || ( !std::isnan( a1.mean ) && a1.mean > a0.mean ) ) ? 1 : 0;
    cd.value = cd.cell[ cd.best_rank ].mean;
    cd.value_se = cd.cell[ cd.best_rank ].se;
    cd.pass1_value = a0.pass1_mean;
  }

  // ---- 280-05: this search's statistics join the run's. The pooled within-cell variance of sample values (the
  // noise-correction diagnostic of Pitfall 16): the cells' sums of squares over the sum of their degrees of freedom. ----
  {
    double ss_all = 0.0, df_all = 0.0;
    long copies_ok = 0;
    for ( const cand_t& cd : cands )
      for ( const cell_t& cl : cd.cell )
      {
        copies_ok += static_cast<long>( cl.vals.size() );
        if ( cl.vals.size() < 2 )
          continue;
        double m = 0.0;
        for ( double v : cl.vals )
          m += v;
        m /= static_cast<double>( cl.vals.size() );
        for ( double v : cl.vals )
          ss_all += ( v - m ) * ( v - m );
        df_all += static_cast<double>( cl.vals.size() - 1 );
      }
    apply_search_stats( agg, copies_ok, gap_obs, df_all > 0.0 ? ss_all / df_all : 0.0 );
  }

  // ---- the fail rule: a candidate with fewer than half its samples usable marks the search skipped_fail ----
  bool fail = false;
  for ( const cand_t& cd : cands )
  {
    const int ok = cd.cell[ 0 ].n_ok + cd.cell[ 1 ].n_ok;
    const int asg = cd.cell[ 0 ].n_assigned + cd.cell[ 1 ].n_assigned;
    if ( ok < ( asg + 1 ) / 2 )
      fail = true;
  }

  // ---- pick: the best value among the survivors (strict greater: an exact tie stays with the higher rank) ----
  auto better = []( double x, double y ) { return !std::isnan( x ) && ( std::isnan( y ) || x > y ); };
  int pick_i = -1;
  int pick0_i = -1;  // the pick using the rank-0 cells only
  for ( int i = 0; i < C; ++i )
  {
    if ( i == dropped )
      continue;
    const cand_t& cd = cands[ static_cast<std::size_t>( i ) ];
    if ( pick_i < 0 || better( cd.value, cands[ static_cast<std::size_t>( pick_i ) ].value ) )
      pick_i = i;
    if ( pick0_i < 0 || better( cd.cell[ 0 ].mean, cands[ static_cast<std::size_t>( pick0_i ) ].cell[ 0 ].mean ) )
      pick0_i = i;
  }
  // runner-up: the best value among every OTHER candidate (the dropped one included); ties to the higher rank
  int second_i = -1;
  for ( int i = 0; i < C; ++i )
  {
    if ( i == pick_i )
      continue;
    if ( second_i < 0 || better( cands[ static_cast<std::size_t>( i ) ].value, cands[ static_cast<std::size_t>( second_i ) ].value ) )
      second_i = i;
  }
  double margin = std::numeric_limits<double>::quiet_NaN();
  double margin_se = std::numeric_limits<double>::quiet_NaN();
  if ( !fail && second_i >= 0 )
  {
    const cand_t& a = cands[ static_cast<std::size_t>( pick_i ) ];
    const cand_t& b = cands[ static_cast<std::size_t>( second_i ) ];
    margin = a.value - b.value;
    if ( std::isfinite( a.value_se ) && std::isfinite( b.value_se ) )
      margin_se = std::sqrt( a.value_se * a.value_se + b.value_se * b.value_se );
  }
  const int second_pick = d2 ? cands[ static_cast<std::size_t>( pick_i ) ].best_rank : -1;
  const bool second_changed = d2 && pick_i != pick0_i;

  int pick = net;
  if ( fail )
  {
    ++g_count.skipped_fail;
  }
  else
  {
    pick = cands[ static_cast<std::size_t>( pick_i ) ].button;
    ++g_count.branched;
    if ( pick != net )
      ++g_count.overrides;  // press=net: the override the search WOULD have made
    if ( c.press_best )
      idx = pick;
  }
  ++g_count.searches;
  const bool hold_won = !fail && RL_ACTIONS[ pick ].kind == rl_action_kind::wait;
  // 280-06 (SRCH-07): what the pick's best cell says about the window: the mean window return R (no bootstrap), the
  // undiscounted expected and the realised damage over it, and the bootstrap term's share of the whole label
  // (null in mode none, and when there is no pick or the label's mean is zero).
  double window_return = std::numeric_limits<double>::quiet_NaN();
  double R_und_mean = std::numeric_limits<double>::quiet_NaN();
  double R_real_mean = std::numeric_limits<double>::quiet_NaN();
  double boot_share = std::numeric_limits<double>::quiet_NaN();
  if ( !fail && pick_i >= 0 )
  {
    const cell_t& bc = cands[ static_cast<std::size_t>( pick_i ) ].cell[ cands[ static_cast<std::size_t>( pick_i ) ].best_rank ];
    auto mean_of = []( const std::vector<double>& v ) {
      double sum = 0.0;
      for ( double x : v )
        sum += x;
      return v.empty() ? std::numeric_limits<double>::quiet_NaN() : sum / static_cast<double>( v.size() );
    };
    window_return = mean_of( bc.Rs );
    R_und_mean = mean_of( bc.Rund );
    R_real_mean = mean_of( bc.Rreal );
    if ( g_boot_mode != 0 && bc.mean != 0.0 )
      boot_share = mean_of( bc.boots ) / bc.mean;
  }
  const char* boot_mode_name = g_boot_mode == 0 ? "none" : g_boot_mode == 1 ? "q" : "value";

  // ---- clocks ----
  const double fork_cpu = rusage_cpu_s( RUSAGE_SELF ) - self0;
  const double child_cpu = rusage_cpu_s( RUSAGE_CHILDREN ) - kids0;
  const double wall = std::chrono::duration<double>( std::chrono::steady_clock::now() - wall0 ).count();
  g_count.child_cpu_s += child_cpu;
  g_count.fork_cpu_s += fork_cpu;
  g_count.wall_branch_s += wall;
  g_count.rss_kb_last = read_vmrss_kb();
  g_count.fd_last = count_dir_entries( "/proc/self/fd" );


  // ---- 280-07: the record rows (a search that failed has no label and writes none) ----
  {
    int n_total_r = 0, samples_r = 0, pairs_cut_r = 0;
    for ( const cand_t& cd : cands )
    {
      n_total_r += cd.spent;
      pairs_cut_r += cd.pairs_cut_first;
      for ( int r = 0; r < ( d2 ? 2 : 1 ); ++r )
        samples_r += cd.cell[ r ].n_assigned;
    }
    g_samples_total += samples_r;
    g_pair_cut_total += pairs_cut_r;
    g_fork_fail_total += agg.fork_fail;
    if ( !fail && pick_i >= 0 )
    {
      rec_ctx_t cx;
      cx.fight_json = fmt::format( "{{\"shape\":{},\"setup\":{},\"seed\":{}}}", json_str_or_null( jstr( c.fight_shape ) ),
                                   json_str_or_null( jstr( c.fight_setup ) ), static_cast<unsigned long long>( sim->seed ) );
      cx.window_s = c.window;
      cx.copies = n_total_r;
      cx.cpu_s = fork_cpu + child_cpu;
      cx.boot_mode = boot_mode_name;
      cx.divisor = solver_branch::cfg().value_divisor;
      cx.gamma = solver_branch::cfg().gamma;
      cx.k = k;
      cx.audit = g_audit;
      cx.split_mode = split_mode_name();
      cx.depth = c.depth;
      cx.root_seq = seq;
      std::string cj;
      for ( std::size_t i = 0; i < cands.size(); ++i )
      {
        const cand_t& cd = cands[ i ];
        const cell_t& bc = cd.cell[ cd.best_rank ];
        std::string second;
        if ( d2 )
          for ( int r = 0; r < 2; ++r )
          {
            const cell_t& cl = cd.cell[ r ];
            double modal_n = 0.0;
            const int modal = modal_sec( cl, modal_n );
            second += fmt::format( "{}{{\"button\":{},\"mean\":{},\"se\":{},\"n_ok\":{},\"rank\":{},\"n_pairs\":{}}}",
                                   r ? "," : "", cl.n_pairs > 0 && modal >= 0 ? std::to_string( modal ) : std::string( "null" ),
                                   jnum( cl.mean ), jnum( cl.se ), cl.n_ok, r, cl.n_pairs );
          }
        if ( i )
          cj += ',';
        cj += cand_json( cd.button, cd.value, cd.value_se, bc.n_ok, bc.n_fail, bc.w_sum, second, bc.end_obs );
      }
      const bool decisive = std::isfinite( margin ) && std::isfinite( margin_se ) && margin > 2.0 * margin_se;
      const cand_t& pk = cands[ static_cast<std::size_t>( pick_i ) ];
      g_rows.push_back( make_row( cx, "root", seq, "null", t_k, obs_b64( obs ), mask_json( mask_digits( mask ) ), cj, pick,
                                  second_pick >= 0 ? std::to_string( second_pick ) : std::string( "null" ), margin,
                                  margin_se, decisive, 1.0, "", boot_share,
                                  row_tail( cx, window_return, d2 ? json_bool( second_changed ) : std::string( "null" ),
                                            json_bool( hold_won ) ) ) );
      ++g_rows_root;
      root_stat_t rs;
      rs.audit = g_audit;
      rs.decisive = decisive;
      if ( std::isfinite( pk.value_se ) && std::isfinite( pk.value ) && pk.value != 0.0 )
        rs.se_pct = 100.0 * pk.value_se / std::fabs( pk.value );
      rs.hold_any = hold_any;
      rs.hold_won = hold_won;
      if ( hold_won )
      {
        double best_non_hold = std::numeric_limits<double>::quiet_NaN();
        for ( const cand_t& cd : cands )
          if ( RL_ACTIONS[ cd.button ].kind != rl_action_kind::wait && std::isfinite( cd.value ) &&
               ( std::isnan( best_non_hold ) || cd.value > best_non_hold ) )
            best_non_hold = cd.value;
        if ( std::isfinite( best_non_hold ) && best_non_hold != 0.0 )
          rs.hold_payoff = 100.0 * ( pk.value - best_non_hold ) / std::fabs( best_non_hold );
      }
      rs.second_changed = d2 ? ( second_changed ? 1 : 0 ) : -1;
      g_root_stats.push_back( rs );
      g_copies_rows += static_cast<double>( n_total_r );
      std::vector<const std::vector<copy_result_t>*> passes{ &raw1, &raw2 };
      build_interior_rows( cx, pick_i, passes, g_rows, g_rows_second, g_rows_chance, g_rare_rows );
    }
  }

  // the SRCH-04 log receipt: one stderr line per search naming the sites split on the first copy
  std::string top_list;
  for ( const std::string& t : g_ranked_top )
    top_list += ( top_list.empty() ? "" : "," ) + t;
  if ( top_list.empty() )
    top_list = "-";
  std::fprintf( stderr, "[RL_SEARCH_SPLIT] seq=%llu mode=%s top=%s sites=%s\n", static_cast<unsigned long long>( seq ),
                split_mode_name(), top_list.c_str(), first_copy_sites.c_str() );
  std::fflush( stderr );

  // ---- log ----
  if ( g_log )
  {
    std::string jc;
    int n_total = 0;
    int samples_total = 0;
    int pairs_total = 0, pairs_cut = 0, pairs_failed = 0, pairs_mixed = 0;
    for ( std::size_t i = 0; i < cands.size(); ++i )
    {
      const cand_t& cd = cands[ i ];
      n_total += cd.spent;
      pairs_total += cd.pairs;
      pairs_cut += cd.pairs_cut_first;
      pairs_failed += cd.pairs_failed;
      pairs_mixed += cd.pairs_mixed;
      std::string cells_json;
      for ( int r = 0; r < ( d2 ? 2 : 1 ); ++r )
      {
        const cell_t& cl = cd.cell[ r ];
        samples_total += cl.n_assigned;
        double modal_n = 0.0;
        const int modal = modal_sec( cl, modal_n );
        cells_json += fmt::format(
            "{}{{\"rank\":{},\"copies\":{},\"n_ok\":{},\"n_fail\":{},\"w_sum\":{},\"mean\":{},\"se\":{},"
            "\"modal_button\":{},\"modal_n\":{}}}",
            r ? "," : "", r, cl.n_assigned, cl.n_ok, cl.n_fail, jnum( cl.w_sum ), jnum( cl.mean ), jnum( cl.se ),
            modal >= 0 ? std::to_string( modal ) : std::string( "null" ), jnum( modal_n ) );
      }
      const int c_ok = cd.cell[ 0 ].n_ok + cd.cell[ 1 ].n_ok;
      const int c_fail = cd.cell[ 0 ].n_fail + cd.cell[ 1 ].n_fail;
      const double c_w = cd.cell[ 0 ].w_sum + cd.cell[ 1 ].w_sum;
      const std::string hold_event =
          cd.hold != 0 ? fmt::format( "\"{}\"", RL_ACTIONS[ cd.button ].label ) : std::string( "null" );
      jc += fmt::format(
          "{}{{\"button\":{},\"rank_in_q\":{},\"q\":{},\"hold_event\":{},\"copies\":{},\"n_ok\":{},\"n_fail\":{},"
          "\"w_sum\":{},\"mean\":{},\"se\":{},\"pass1_mean\":{},\"value\":{},\"pass1_value\":{},\"pairs\":{},"
          "\"pairs_cut_first\":{},\"pairs_failed\":{},\"singles2\":{},\"best_rank\":{},\"cells\":[{}]}}",
          i ? "," : "", cd.button, cd.rank_in_q, jnum( static_cast<double>( q[ cd.button ] ) ), hold_event, cd.spent, c_ok,
          c_fail, jnum( c_w ), jnum( cd.value ), jnum( cd.value_se ), jnum( cd.pass1_value ), jnum( cd.value ),
          jnum( cd.pass1_value ), cd.pairs, cd.pairs_cut_first, cd.pairs_failed, cd.singles2, cd.best_rank, cells_json );
    }
    std::string hist_json;
    for ( const auto& kv : leaves_hist )
      hist_json += fmt::format( "{}\"{}\":{}", hist_json.empty() ? "" : ",", kv.first, kv.second );
    const long hwm_max = g_count.search_vmhwm_kb_max;
    std::string site_stats_json;
    for ( std::size_t i = 0; i < agg.rows.size(); ++i )
    {
      const site_total_t& t = agg.rows[ i ];
      if ( !( t.n > 0.0 ) )
        continue;
      if ( !site_stats_json.empty() )
        site_stats_json += ",";
      site_stats_json += fmt::format( "{{\"key\":\"{}\",\"n\":{},\"sum_p\":{},\"sum_pq\":{},\"n_yes\":{}}}",
                                      jstr( g_sites[ i ].key ), jnum( t.n ), jnum( t.sum_p ), jnum( t.sum_pq ),
                                      jnum( t.n_yes ) );
    }
    *g_log << fmt::format(
        "{{\"seq\":{},\"k\":{},\"t\":{},\"net_press\":{},\"pick\":{},\"press\":{},\"listed\":{},\"cands\":[{}],"
        "\"dropped\":{},\"margin\":{},\"margin_se\":{},\"copies\":{},\"skipped_fail\":{},\"cpu_s\":{},"
        "\"fork_cpu_s\":{},\"child_cpu_s\":{},\"wall_s\":{},\"leaf_vmhwm_kb_max\":{},"
        "\"split_mode\":\"{}\",\"splits_total\":{},\"leaves_total\":{},\"copies_split\":{},\"fork_fail\":{},"
        "\"max_abs_wsum_err\":{},\"sibling_pairs\":{},\"sibling_ties\":{},\"audit_nosplit\":{},\"gap_obs\":{},"
        "\"depth\":{},\"second_pick\":{},\"second_level_changed\":{},\"pairs\":{},\"pair_forks\":{},"
        "\"pair_cut_first\":{},\"pair_failed\":{},\"pair_mixed\":{},\"samples\":{},\"leaves_per_copy\":{{{}}},"
        "\"hold_candidate\":{},\"hold_added\":{},\"hold_won\":{},\"pair_half_pairs\":{},\"pair_half_ties\":{},"
        "\"boot_mode\":\"{}\",\"bootstrap_share\":{},\"window_return\":{},\"R_undisc_mean\":{},\"R_real_mean\":{},"
        "\"site_stats\":[{}]}}\n",
        seq, k, jnum( t_k ), net, pick, idx, listed ? 1 : 0, jc,
        dropped >= 0 ? cands[ static_cast<std::size_t>( dropped ) ].button : -1, jnum( margin ), jnum( margin_se ),
        n_total, fail ? 1 : 0, jnum( fork_cpu + child_cpu ), jnum( fork_cpu ), jnum( child_cpu ), jnum( wall ), hwm_max,
        split_mode_name(), agg.splits_total, agg.leaves_total, agg.copies_split, agg.fork_fail,
        jnum( agg.max_abs_wsum_err ), agg.sibling_pairs, agg.sibling_ties, g_audit ? 1 : 0, gap_obs.size(), c.depth,
        second_pick >= 0 ? std::to_string( second_pick ) : std::string( "null" ),
        d2 ? ( second_changed ? "1" : "0" ) : "null", pairs_total, pairs_total - pairs_cut - pairs_failed, pairs_cut,
        pairs_failed, pairs_mixed, samples_total, hist_json, hold_any ? 1 : 0, hold_added ? 1 : 0, hold_won ? 1 : 0, ph.pairs,
        ph.ties, boot_mode_name, g_boot_mode == 0 ? std::string( "null" ) : jnum( boot_share ), jnum( window_return ),
        jnum( R_und_mean ), jnum( R_real_mean ), site_stats_json );
    g_log->flush();
  }
  return false;
#else
  (void)sim; (void)p; (void)seq; (void)obs; (void)mask; (void)q; (void)specs; (void)idx; (void)listed;
  throw sc_runtime_error( "solver_search: fork() needs a Linux build" );
#endif
}
// ---- the record file and the summary file (280-07) ------------------------------------------------------------------
// The record is opened O_APPEND by the parent and each row goes out in ONE write() call (Phase 281 may run several fight
// processes into one file; a regular file's appended write is atomic with respect to the offset). A short write is finished
// in a loop, which only happens on a full disk.
void write_record_rows( const std::string& file )
{
#if defined( __linux__ )
  const int fd = ::open( file.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644 );
  if ( fd < 0 )
    throw sc_runtime_error( fmt::format( "solver_search_record='{}' cannot be opened for appending: {}.", file,
                                         std::strerror( errno ) ) );
  for ( const std::string& row : g_rows )
  {
    std::string line = row;
    line += '\n';
    const char* d = line.data();
    std::size_t left = line.size();
    while ( left > 0 )
    {
      const ssize_t w = ::write( fd, d, left );
      if ( w < 0 )
      {
        if ( errno == EINTR )
          continue;
        const int e = errno;
        ::close( fd );
        throw sc_runtime_error( fmt::format( "solver_search_record='{}': write failed: {}.", file, std::strerror( e ) ) );
      }
      d += w;
      left -= static_cast<std::size_t>( w );
    }
    g_record_bytes += static_cast<long long>( line.size() );
  }
  ::close( fd );
#else
  (void)file;
#endif
}

double median_of( std::vector<double> v )
{
  if ( v.empty() )
    return std::numeric_limits<double>::quiet_NaN();
  std::sort( v.begin(), v.end() );
  const std::size_t n = v.size();
  return n % 2 ? v[ n / 2 ] : 0.5 * ( v[ n / 2 - 1 ] + v[ n / 2 ] );
}

// summary.json: C-A's seventeen fields in C-A's order, then the recorded additions. Definitions are in 280-RECORD.md.
void write_summary_json( const std::string& file )
{
  const double n_roots = static_cast<double>( g_root_stats.size() );
  auto pct = [ & ]( long long hits ) {
    return n_roots > 0.0 ? jnum( 100.0 * static_cast<double>( hits ) / n_roots ) : std::string( "null" );
  };
  long long decisive = 0, hold_cand = 0, hold_won = 0, changed = 0;
  std::vector<double> se_main, se_audit, payoff;
  for ( const root_stat_t& r : g_root_stats )
  {
    decisive += r.decisive ? 1 : 0;
    hold_cand += r.hold_any ? 1 : 0;
    hold_won += r.hold_won ? 1 : 0;
    changed += r.second_changed == 1 ? 1 : 0;
    if ( std::isfinite( r.se_pct ) )
      ( r.audit ? se_audit : se_main ).push_back( r.se_pct );
    if ( std::isfinite( r.hold_payoff ) )
      payoff.push_back( r.hold_payoff );
  }
  const bool any_depth1 = g_cfg.depth == 1;
  double payoff_mean = std::numeric_limits<double>::quiet_NaN();
  if ( !payoff.empty() )
  {
    double sum = 0.0;
    for ( double x : payoff )
      sum += x;
    payoff_mean = sum / static_cast<double>( payoff.size() );
  }
  const long long rows = g_rows_root + g_rows_second + g_rows_chance;
#if defined( __linux__ )
  const double peak_kb = static_cast<double>( std::max( read_vmhwm_kb(), g_count.leaf_vmhwm_kb_max ) );
#else
  const double peak_kb = 0.0;
#endif
  const std::string js = fmt::format(
      "{{\"decisions\":{},\"roots\":{},\"seconds\":{},\"chance_rows\":{},\"copies\":{},\"cpu_s\":{},\"wall_s\":{},"
      "\"inside_round_wall_s\":{},\"decisive_pct\":{},\"label_se_pct\":{},\"label_se_nosplit_pct\":{},"
      "\"second_level_changed_pick_pct\":{},\"hold_candidate_pct\":{},\"hold_won_pct\":{},\"hold_payoff_pct\":{},"
      "\"rare_branch_rows\":{},\"peak_rss_mb\":{},\"searches\":{},\"copies_spent\":{},\"samples\":{},\"pair_cut_first\":{},"
      "\"fork_fail\":{},\"child_fail\":{},\"fork_cpu_s\":{},\"child_cpu_s\":{},\"record_bytes\":{},"
      "\"inside_round_wall_s_source\":\"fork_constant\"}}\n",
      rows, g_rows_root, g_rows_second, g_rows_chance, jnum( g_copies_rows ),
      jnum( g_count.fork_cpu_s + g_count.child_cpu_s ), jnum( g_count.wall_branch_s ), jnum( 0.0 ), pct( decisive ),
      jnum( median_of( se_main ) ), se_audit.empty() ? std::string( "null" ) : jnum( median_of( se_audit ) ),
      any_depth1 ? std::string( "null" ) : pct( changed ), pct( hold_cand ), pct( hold_won ), jnum( payoff_mean ),
      g_rare_rows, jnum( peak_kb / 1024.0 ), g_count.searches, g_count.children, g_samples_total, g_pair_cut_total,
      g_fork_fail_total, g_count.child_fail, jnum( g_count.fork_cpu_s ), jnum( g_count.child_cpu_s ), g_record_bytes );
  const std::string tmp = fmt::format( "{}.tmp.{}", file, static_cast<long>( ::getpid() ) );
  {
    std::ofstream o( tmp, std::ios::out | std::ios::trunc | std::ios::binary );
    if ( !o.is_open() )
      throw sc_runtime_error( fmt::format( "solver_search_summary='{}': cannot write the temporary file '{}'.", file, tmp ) );
    o << js;
    o.flush();
    if ( !o.good() )
    {
      o.close();
      std::remove( tmp.c_str() );
      throw sc_runtime_error( fmt::format( "solver_search_summary='{}': writing '{}' failed.", file, tmp ) );
    }
  }
  if ( std::rename( tmp.c_str(), file.c_str() ) != 0 )
  {
    std::remove( tmp.c_str() );
    throw sc_runtime_error( fmt::format( "solver_search_summary='{}': renaming '{}' failed: {}.", file, tmp,
                                         std::strerror( errno ) ) );
  }
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
  if ( c.depth != 1 && c.depth != 2 )
    refuse( fmt::format( "solver_search_depth must be 1 or 2 (got {}).", c.depth ) );
  if ( c.second != 2 )
    refuse( fmt::format( "solver_search_second must be 2 (the second level is fixed at two buttons; got {}).", c.second ) );
  {
    // pass 1 needs solver_search_pass1 copies per candidate (default 8); the hold candidate is one more; depth two needs at
    // least one pair after it
    if ( c.pass1 < 1 )
      refuse( fmt::format( "solver_search_pass1 must be at least 1 (got {}): the first pass needs at least one copy per "
                           "candidate.", c.pass1 ) );
    if ( c.pass1 > 256 )
      refuse( fmt::format( "solver_search_pass1 must be at most 256 (got {}).", c.pass1 ) );
    const int n_cands_max = c.candidates + ( c.holds ? 1 : 0 );
    const int need = c.pass1 * n_cands_max + ( c.depth == 2 ? 2 : 0 );
    if ( c.copies < need )
      refuse( fmt::format( "solver_search_copies={} is below {} = {} x ({} candidates{}){} (pass 1 alone needs {} copies "
                           "per candidate; solver_search_copies must cover them).",
                           c.copies, need, c.pass1, c.candidates, c.holds ? " + 1 hold" : "",
                           c.depth == 2 ? " + 2 for one pair at depth 2" : "", c.pass1 ) );
  }
  if ( c.copies > 4096 )
    refuse( fmt::format( "solver_search_copies must be at most 4096 (got {}).", c.copies ) );
  if ( c.jobs < 1 || c.jobs > 12 )
    refuse( fmt::format( "solver_search_jobs must be 1..12 (got {}).", c.jobs ) );
  if ( c.splits < 0 || c.splits > 4 )
    refuse( fmt::format( "solver_search_splits must be 0..4 (got {}).", c.splits ) );
  if ( !c.split_site.empty() && c.splits < 1 )
    refuse( "solver_search_split_site needs solver_search_splits >= 1 (a split site with no splits allowed)." );
  if ( c.nosplit_audit && !c.split_site.empty() )
    refuse( "solver_search_nosplit_audit=1 cannot be combined with solver_search_split_site (an audit search never splits)." );
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

  // 280-05: the ranking file in, a writable place for the ranking file out, the audit switch
  g_audit = c.nosplit_audit;
  if ( !c.sites_in.empty() )
    read_sites_in( c.sites_in, c.window );
  if ( !c.sites_out.empty() )
  {
    // refuse now, not at exit, when the output place cannot be written (the probe file is removed at once)
    const std::string probe = fmt::format( "{}.probe.{}", c.sites_out, static_cast<long>( ::getpid() ) );
    {
      std::ofstream o( probe, std::ios::out | std::ios::trunc );
      if ( !o.is_open() )
        refuse( fmt::format( "solver_search_sites_out='{}' cannot be written (probe file '{}' did not open).", c.sites_out,
                             probe ) );
    }
    std::remove( probe.c_str() );
  }

  // 280-06: the window-end value blob and the bootstrap mode
  if ( !c.value.empty() )
  {
    try
    {
      g_value_w = std::make_shared<rl_policy::rl_weights_t>( rl_policy::load_rlw1( c.value ) );
    }
    catch ( const std::exception& e )
    {
      refuse( fmt::format( "solver_search_value='{}' cannot be loaded: {}", c.value, e.what() ) );
    }
    if ( g_value_w->body != rl_policy::rl_body_type::chooser_value || !g_value_w->has_value_head )
      refuse( fmt::format( "solver_search_value='{}' is not a version-7 chooser-value weights file with a value head "
                           "(body '{}').",
                           c.value, rl_policy::body_name( g_value_w->body ) ) );
  }
  // 284-06: the window-end value term's units
  if ( c.value_units != "raw" && c.value_units != "shape" )
    refuse( fmt::format( "solver_search_value_units='{}' is not 'raw' or 'shape'.", c.value_units ) );
  if ( c.value_units == "shape" && !g_value_w )
    refuse( "solver_search_value_units=shape needs solver_search_value=<v7 blob> (there is no value head to read the "
            "term from)." );
  g_value_shape = c.value_units == "shape";
  {
    std::string bm = c.bootstrap;
    if ( bm.empty() )
      bm = g_value_w ? "value" : "none";
    if ( bm != "none" && bm != "q" && bm != "value" )
      refuse( fmt::format( "solver_search_bootstrap='{}' is not 'none', 'q' or 'value'.", c.bootstrap ) );
    if ( bm == "value" && !g_value_w )
      refuse( "solver_search_bootstrap=value needs solver_search_value=<v7 blob> (there is no window-end value to add)." );
    g_boot_mode = bm == "none" ? 0 : bm == "q" ? 1 : 2;
  }

  // 280-07: the record, the summary, the fight names and the chance-row cap
  {
    const std::uint32_t one = 1;
    unsigned char first_byte = 0;
    std::memcpy( &first_byte, &one, 1 );
    if ( first_byte != 1 )
      refuse( "solver_search needs a little-endian machine (the record's observations are little-endian float32)." );
  }
  if ( c.chance_rows < 0 || c.chance_rows > 64 )
    refuse( fmt::format( "solver_search_chance_rows must be 0..64 (got {}).", c.chance_rows ) );
  if ( !c.fight.empty() )
  {
    const std::size_t comma = c.fight.find( ',' );
    const bool one_comma = comma != std::string::npos && c.fight.find( ',', comma + 1 ) == std::string::npos;
    bool plain = true;
    for ( char ch : c.fight )
      if ( static_cast<unsigned char>( ch ) < 0x21 || static_cast<unsigned char>( ch ) > 0x7e || ch == '"' || ch == '\\' )
        plain = false;
    if ( !one_comma || comma == 0 || comma + 1 >= c.fight.size() || !plain )
      refuse( fmt::format( "solver_search_fight='{}' must be '<shape>,<setup>' (exactly two non-empty parts, one comma, "
                           "printable characters without spaces, quotes or backslashes).",
                           c.fight ) );
    c.fight_shape = c.fight.substr( 0, comma );
    c.fight_setup = c.fight.substr( comma + 1 );
  }
  {
    const std::string outs[] = { c.record, c.summary, c.sites_out, c.log };
    const char* const names[] = { "solver_search_record", "solver_search_summary", "solver_search_sites_out",
                                  "solver_search_log" };
    for ( int a = 0; a < 4; ++a )
      for ( int b = a + 1; b < 4; ++b )
        if ( !outs[ a ].empty() && outs[ a ] == outs[ b ] )
          refuse( fmt::format( "{} and {} name the same file '{}'.", names[ a ], names[ b ], outs[ a ] ) );
    // both are refused now, not at the end of the fight, when their place cannot be written
    for ( int a = 0; a < 2; ++a )
    {
      if ( outs[ a ].empty() )
        continue;
#if defined( __linux__ )
      const std::string dir = outs[ a ].find( '/' ) == std::string::npos ? std::string( "." )
                                                                           : outs[ a ].substr( 0, outs[ a ].rfind( '/' ) );
      const bool exists = ::access( outs[ a ].c_str(), F_OK ) == 0;
      const bool ok = exists ? ::access( outs[ a ].c_str(), W_OK ) == 0 : ::access( dir.c_str(), W_OK | X_OK ) == 0;
      if ( !ok )
        refuse( fmt::format( "{}='{}' cannot be written (no write access to the file or its directory).", names[ a ],
                             outs[ a ] ) );
#endif
    }
  }

  g_any = true;
  fmt::print( "[RL_SEARCH] candidates={} window={:.17g} copies={} jobs={} at={} margin={:.17g} press={} expected_crit={} "
              "divisor={:.17g} gamma={:.17g} salt_base={} splits={} split_site={} audit_nosplit={} sites_in={} sites_out={} "
              "depth={} second={} holds={} value={} bootstrap={} record={} summary={} fight={} chance_rows={} pass1={} value_units={} log={}\n",
              c.candidates, c.window, c.copies, c.jobs, c.at_list.size(), c.margin, c.press, c.expected_crit ? 1 : 0,
              sb.value_divisor, sb.gamma, sb.branch_salt_base, c.splits, c.split_site.empty() ? "-" : c.split_site,
              c.nosplit_audit ? 1 : 0, c.sites_in.empty() ? "-" : c.sites_in, c.sites_out.empty() ? "-" : c.sites_out,
              c.depth, c.depth == 2 ? c.second : 0, c.holds ? 1 : 0, c.value.empty() ? "-" : c.value,
              g_boot_mode == 0 ? "none" : g_boot_mode == 1 ? "q" : "value", c.record.empty() ? "-" : c.record,
              c.summary.empty() ? "-" : c.summary, c.fight.empty() ? "-" : c.fight, c.chance_rows, c.pass1, c.value_units, c.log );
  std::fflush( stdout );
}

void on_decision( sim_t* sim, player_t* p, std::uint64_t seq, const float* obs, const float* q,
                  const std::uint8_t* mask, const std::uint8_t* select_mask, int legal_count, float q_margin,
                  float top_q, bool exploratory, bool forced, int& idx )
{
  if ( !g_any )
    return;

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
      const double disc = std::pow( g_child.gamma, t - g_child.t_k );
      const double boot_q = disc * static_cast<double>( top_q );
      // 280-06: the window-end value on the observation built at the exact cut (rl_policy::value_forward returns the mean
      // of the quantiles x value_scale in raw damage units; divided by the shape's divisor it is in the reward units of R).
      // 284-06: with solver_search_value_units=shape the term is the head's own output (value_forward / value_scale), exact
      // for a head trained in each shape's reward units; raw (the default) is the expression above, unchanged.
      const double boot_v =
          g_value_w ? ( g_value_shape ? disc * rl_policy::value_forward( *g_value_w, obs ) /
                                            static_cast<double>( g_value_w->value_scale )
                                      : disc * rl_policy::value_forward( *g_value_w, obs ) / g_child.divisor )
                    : 0.0;
      write_leaf_and_exit( g_child.acc, p->solver_damage_expected_so_far - g_child.Eexp_k,
                           p->solver_damage_so_far - g_child.D_k, boot_q, boot_v, t, 0, g_child.cut_state, obs );
    }
    g_child.last_t = t;
    g_child.last_E = E;
    // 280-06: the second level. A pair process that has not forked yet forks at the first decision with at least two
    // selectable buttons that is neither forced nor exploratory: the net's best button for the forker (rank 0), its
    // runner-up for the new process (rank 1).
    // 280-07: the same decision is a labelled decision for the record. The forker writes a SECOND line (the observation,
    // the mask and the two buttons) just before it forks, and marks the decision so each leaf below can state its return
    // from here; a branch of a chance split writes a CHANCE line at its first labelled decision after the split (after the
    // pair fork when both fall on one decision, so the line carries this process's own press and rank).
    if ( ( g_child.pair_pending || g_child.chance_pending ) && !forced && !exploratory )
    {
      int n_sel2 = 0;
      for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
        n_sel2 += select_mask[ i ] ? 1 : 0;
      if ( n_sel2 >= 2 )
      {
        if ( g_child.pair_pending )
        {
          const std::vector<int> top2 = top_k( q, select_mask, 2 );
          if ( top2.size() != 2 || top2[ 0 ] != idx )
            child_write_and_exit( make_fail_line( 81, "second_level_net_pick_is_not_the_best_button" ), 81 );
          g_child.marks.push_back( mark_t{ "S" + path_token(), t, g_child.acc } );
          const std::string second_line =
              fmt::format( "SECOND {} {} {} {:.9g} {:.17g} {} {} {} {}\n", g_child.copy, path_token(), seq, t, g_child.pw,
                           top2[ 0 ], top2[ 1 ], mask_digits( mask ), obs_b64( obs ) );
          if ( second_line.size() > LINE_MAX_BYTES )
            child_write_and_exit( make_fail_line( 76, "second_line_too_long" ), 76 );
          child_write_only( second_line );
          pair_fork_here( sim, top2[ 0 ], top2[ 1 ], idx );
        }
        if ( g_child.chance_pending )
        {
          g_child.chance_pending = false;
          g_child.marks.push_back( mark_t{ "C" + path_token(), t, g_child.acc } );
          const std::string chance_line =
              fmt::format( "CHANCE {} {} {} {} {:.9g} {:.17g} {} {} {}\n", g_child.copy, path_token(), out_rank(), seq, t,
                           g_child.pw, idx, mask_digits( mask ), obs_b64( obs ) );
          if ( chance_line.size() > LINE_MAX_BYTES )
            child_write_and_exit( make_fail_line( 76, "chance_line_too_long" ), 76 );
          child_write_only( chance_line );
        }
      }
    }
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

  // 280-06: the candidate list with the hold candidate (G280-8). A wait button already among the top candidates is
  // marked (hold 1); with solver_search_holds=1 and no wait button among them, the selectable wait button with the
  // highest q is appended (hold 2). The wait kind is read from the action table, never from fixed indices.
  std::vector<cand_spec_t> specs;
  bool any_wait = false;
  for ( std::size_t i = 0; i < cands.size(); ++i )
  {
    cand_spec_t sp;
    sp.button = cands[ i ];
    sp.rank_q = static_cast<int>( i );
    sp.hold = RL_ACTIONS[ cands[ i ] ].kind == rl_action_kind::wait ? 1 : 0;
    any_wait = any_wait || sp.hold != 0;
    specs.push_back( sp );
  }
  if ( c.holds && !any_wait )
  {
    int best_wait = -1;
    for ( int b = 0; b < static_cast<int>( RL_ACTION_DIM ); ++b )
      if ( select_mask[ b ] && RL_ACTIONS[ b ].kind == rl_action_kind::wait &&
           ( best_wait < 0 || q[ b ] > q[ best_wait ] ) )
        best_wait = b;
    if ( best_wait >= 0 )
    {
      cand_spec_t sp;
      sp.button = best_wait;
      sp.hold = 2;
      for ( int b = 0; b < static_cast<int>( RL_ACTION_DIM ); ++b )
        if ( select_mask[ b ] && b != best_wait && ( q[ b ] > q[ best_wait ] || ( q[ b ] == q[ best_wait ] && b < best_wait ) ) )
          ++sp.rank_q;
      specs.push_back( sp );
    }
  }

  ensure_sites( sim );
  search_here( sim, p, seq, obs, mask, q, specs, idx, listed_mode );
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
                       p->solver_damage_so_far - g_child.D_k, 0.0, 0.0, t_end, 1, 4, nullptr );
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
  // (depth, second, splits, holds, bootstrap: 280-06 fills them from the options; bootstrap 0 none, 1 q, 2 value).
  const std::string js = fmt::format(
      "{{\"summary\":1,\"decisions\":{},\"near_ties\":{},\"branched\":{},\"overrides\":{},\"skipped_fail\":{},"
      "\"children\":{},\"child_fail\":{},\"child_cpu_s\":{},\"fork_cpu_s\":{},\"wall_branch_s\":{},"
      "\"children_reaped\":{},\"searches\":{},\"unknown_lines\":{},\"self_cpu_s\":{},\"children_cpu_total_s\":{},"
      "\"rss_kb_first\":{},\"rss_kb_last\":{},\"fd_first\":{},\"fd_last\":{},\"leaf_vmhwm_kb_max\":{},"
      "\"listed\":{},\"listed_reached\":{},\"listed_skipped\":{},"
      "\"config\":{{\"margin\":{},\"copies\":{},\"window\":{},\"candidates\":{},\"depth\":{},\"second\":{},\"splits\":{},"
      "\"holds\":{},\"bootstrap\":{},\"gamma\":{},\"jobs\":{},\"salt_base\":{},\"press_best\":{},\"expected_crit\":{},"
      "\"divisor\":{},\"pass1\":{}}}}}",
      g_count.decisions, g_count.near_ties, g_count.branched, g_count.overrides, g_count.skipped_fail, g_count.children,
      g_count.child_fail, jnum( g_count.child_cpu_s ), jnum( g_count.fork_cpu_s ), jnum( g_count.wall_branch_s ),
      g_count.children_reaped, g_count.searches, g_count.unknown_lines, jnum( self_cpu ), jnum( kids_cpu ),
      g_count.rss_kb_first, g_count.rss_kb_last, g_count.fd_first, g_count.fd_last, g_count.leaf_vmhwm_kb_max,
      c.at_list.size(), g_count.listed_reached, g_count.listed_skipped, jnum( c.margin ), c.copies, jnum( c.window ),
      c.candidates, c.depth, c.depth == 2 ? c.second : 0, c.splits, c.holds ? 1 : 0, g_boot_mode, jnum( sb.gamma ), c.jobs, sb.branch_salt_base, c.press_best ? 1 : 0,
      c.expected_crit ? 1 : 0, jnum( sb.value_divisor ), c.pass1 );
  if ( g_log )
  {
    *g_log << js << "\n";
    g_log->flush();
  }
  fmt::print( "[RL_SEARCH_SUMMARY] {}\n", js );
  std::fflush( stdout );
  // 280-05: the ranking table, once, at exit
  if ( !c.sites_out.empty() )
    write_sites_out( c.sites_out, c.window );
  // 280-07: the record rows, then the summary file (the summary names the bytes the record just grew by)
  if ( !c.record.empty() )
    write_record_rows( c.record );
  if ( !c.summary.empty() )
    write_summary_json( c.summary );
}
}  // namespace solver_search
