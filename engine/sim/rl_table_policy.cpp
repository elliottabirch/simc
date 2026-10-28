// ==========================================================================
// 261025-bk4 (wave B, lane b2; wave B3 reconciliation): THE TABLE PLAYER. See rl_table_policy.hpp for the contract and the order of operations.
// Wave B3 (the Python side is the specification, the fork follows): the loader reads exactly the TBP1 version 2 that tools/tbp_compile.py writes, the
// update waits for the next STRATEGY decision (rows pressed by a deferral, a forced idle or NOSTATE neither update nor move the pending decision),
// solver_table_defer=all is arm A1, and the .tbt writer is sim/tbt.py's layout byte for byte.
//
// Contents, in order: (1) the TBP1 file the fork reads, (2) the decision random number generator, (3) the six feature evaluators, (4) the nine strategies,
// (5) the runtime (one decision, the update, the trace), (6) the sim-less probe.
//
// What the fork matches bit for bit (the repository's Python is the specification; this file is its port):
//   - learn/tabular/rng.py            splitmix64, uniform, index, Box-Muller (the C++ in that module's docstring is copied below)
//   - learn/tabular/state.py + tbp.py `header_state_ids`: the float32 pipeline (float multiply, float compares, float64 edges)
//   - book/ch04_strategies.py         the nine strategies and the update
// Compiled with -ffp-contract=off (engine/CMakeLists.txt): a fused multiply-add would change a rounding the Python does not fuse. Python's NumPy
// sums an array pairwise (np_sum below reproduces it, measured equal on 66,000 random rows of 1..22 values).
// ==========================================================================

#include "sim/rl_table_policy.hpp"

#include "player/player.hpp"
#include "sim/rl_policy_constants_select.h"
#include "sim/sim.hpp"
#include "sim/solver_branch.hpp"
#include "util/io.hpp"

#include "rapidjson/document.h"

#include "fmt/format.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rl_table_policy
{

namespace
{

// ---- option storage --------------------------------------------------------------------------------------------------------------------
std::string g_path;
std::string g_scope;
std::string g_fallback;
std::string g_defer;
bool        g_wait = false;
bool        g_trace = false;
std::string g_save;           // solver_table_save=<path>: the kept fight's table is written here at its end (empty = off)
int         g_clock = 0;      // solver_table_clock=<k>: the `e` of the decaying strategies (the fight's place in its chain)

// ---- constants shared with the repository's Python -------------------------------------------------------------------------------------
// The TBP1 version this reader reads (learn/tabular/tbp.py FORMAT_VERSION): version 3, exactly what tools/tbp_compile.py writes. Version 2 added the M2
// array, the `decided` block, the update_rule / reward_rule / scope ids and the 2^20 state cap; version 3 (quick 261026-lrn) adds the header field `update_alpha`
// and the update rule `constant-alpha-floor-f64-v1`, and the optimistic prior is folded into the file's arrays by the compiler (this reader no longer folds it:
// a file the player saves must load again unchanged). A version 2 file is refused by name (TBP_VERSION, hard swap: it is read by an older checkout), never by a shim;
// the ids below are tbp.py's UPDATE_RULE / REWARD_RULE / SCOPE (the Python side is the specification, the fork follows: quick 261025-bk4 wave B3).
constexpr std::uint32_t TBP_VERSION = 3u;
constexpr std::size_t   TBP_PREAMBLE = 16;
constexpr std::uint64_t TBP_MAX_STATES = 1ull << 20;
constexpr const char*   UPDATE_RULE_ID = "constant-alpha-floor-f64-v1";
constexpr const char*   REWARD_RULE_ID = "damage-since-last-strategy-decision-v1";
// tbp.py REWARD_RULE_RETURN: Q is the mean Monte Carlo return (damage from the press to the fight end). The return of a press is unknown until the fight ends, so under this
// rule the player applies NO in-fight update: Q, N and M2 stay as the file loaded them, and the trace records "no update" (has_update = 0) at every record (261026-rbt).
constexpr const char*   REWARD_RULE_RETURN_ID = "damage-to-fight-end-v1";
constexpr const char*   SCOPE_ID = "fight";
// learn/tabular/rng.py RNG_RULE, verbatim.
constexpr const char* RNG_RULE_TEXT =
    "splitmix64-v1: state = mix(mix(fight_seed) ^ decision_index); draw = mix(state) then state += GOLDEN; uniform = (draw >> 11) * 2^-53; normal = "
    "Box-Muller sqrt(-2 ln(1 - u1)) cos(2 pi u2), two draws";
constexpr double TWO_PI = 6.283185307179586;
constexpr std::int64_t MAX_INDEX_N = 1ll << 20;
constexpr int          BUTTONS = static_cast<int>( RL_ACTION_DIM );
constexpr int          MAX_VALUES = 24;

// The nine strategies, 1-based in learn/tabular/tbp.py STRATEGY_IDS order (the `.apl` record's strategy byte).
enum strategy_t : int
{
  S_NONE = 0,
  S_PURE_EXPLOITATION = 1,
  S_PURE_EXPLORATION = 2,
  S_EPSILON_GREEDY = 3,
  S_LIN_DEC_EPSILON_GREEDY = 4,
  S_EXP_DEC_EPSILON_GREEDY = 5,
  S_OPTIMISTIC_INITIALIZATION = 6,
  S_SOFTMAX = 7,
  S_UPPER_CONFIDENCE_BOUND = 8,
  S_THOMPSON_SAMPLING = 9
};

struct strategy_name_t
{
  const char*  id;
  strategy_t   value;
  const char*  params[ 4 ];  // parameter names, nullptr terminated
};
const strategy_name_t STRATEGY_NAMES[] = {
    { "pure_exploitation", S_PURE_EXPLOITATION, { nullptr } },
    { "pure_exploration", S_PURE_EXPLORATION, { nullptr } },
    { "epsilon_greedy", S_EPSILON_GREEDY, { "epsilon", nullptr } },
    { "lin_dec_epsilon_greedy", S_LIN_DEC_EPSILON_GREEDY, { "n_episodes", "init_epsilon", "min_epsilon", "decay_ratio" } },
    { "exp_dec_epsilon_greedy", S_EXP_DEC_EPSILON_GREEDY, { "n_episodes", "init_epsilon", "min_epsilon", "decay_ratio" } },
    { "optimistic_initialization", S_OPTIMISTIC_INITIALIZATION, { "optimistic_estimate", "initial_count", nullptr } },
    { "softmax", S_SOFTMAX, { "n_episodes", "init_temp", "min_temp", "decay_ratio" } },
    { "upper_confidence_bound", S_UPPER_CONFIDENCE_BOUND, { "c", nullptr } },
    { "thompson_sampling", S_THOMPSON_SAMPLING, { "alpha", "beta", nullptr } },
};

[[noreturn]] void refuse( const char* prefix, const std::string& detail )
{
  throw sc_runtime_error( fmt::format( "{}: {}", prefix, detail ) );
}

[[noreturn]] void file_refusal( const std::string& token, const std::string& detail )
{
  refuse( "TABLE_FILE", fmt::format( "{}: {}", token, detail ) );
}

// ---- (2) the decision random number generator: learn/tabular/rng.py, the C++ of its docstring, plus the draws a trace records --------------
constexpr std::uint64_t GOLDEN = 0x9E3779B97F4A7C15ull;

inline std::uint64_t mix( std::uint64_t x )
{
  std::uint64_t z = x + GOLDEN;
  z = ( z ^ ( z >> 30 ) ) * 0xBF58476D1CE4E5B9ull;
  z = ( z ^ ( z >> 27 ) ) * 0x94D049BB133111EBull;
  return z ^ ( z >> 31 );
}

struct stream_t
{
  std::uint64_t s;
  int           draws = 0;
  double        values[ MAX_VALUES ];  // what the strategy consumed, in order: a uniform, an index's uniform, or a normal
  int           n_values = 0;

  stream_t( std::uint64_t fight_seed, std::uint64_t decision_index ) : s( mix( mix( fight_seed ) ^ decision_index ) ), values{} {}

  std::uint64_t next()
  {
    const std::uint64_t out = mix( s );
    s += GOLDEN;
    ++draws;
    return out;
  }
  double uniform_raw() { return static_cast<double>( next() >> 11 ) * 0x1.0p-53; }
  void   note( double v )
  {
    if ( n_values < MAX_VALUES )
      values[ n_values++ ] = v;
  }
  double uniform()
  {
    const double u = uniform_raw();
    note( u );
    return u;
  }
  // rng.py DecisionStream.index: min(int(uniform * n), n - 1), 1 <= n <= 2^20.
  int index( std::int64_t n )
  {
    if ( n < 1 || n > MAX_INDEX_N )
      refuse( "TABLE_STRATEGY", fmt::format( "rng-index-range: index({}) needs 1 <= n <= {}", n, MAX_INDEX_N ) );
    const double u = uniform_raw();
    note( u );
    std::int64_t k = static_cast<std::int64_t>( u * static_cast<double>( n ) );
    if ( k > n - 1 )
      k = n - 1;
    return static_cast<int>( k );
  }
  double normal_raw()
  {
    const double u1 = 1.0 - uniform_raw();
    const double u2 = uniform_raw();
    return std::sqrt( -2.0 * std::log( u1 ) ) * std::cos( TWO_PI * u2 );
  }
  double normal()
  {
    const double z = normal_raw();
    note( z );
    return z;
  }
};

// numpy's float64 pairwise sum (np.sum of a contiguous array without an initial value), measured bit-equal to the Python side for n = 1..22.
double np_sum( const double* a, std::size_t n )
{
  if ( n < 8 )
  {
    double res = 0.0;
    for ( std::size_t i = 0; i < n; ++i )
      res += a[ i ];
    return res;
  }
  // n <= 128 always here (a row has 22 buttons); numpy recurses above 128 and so would this, but a row never gets there.
  double r[ 8 ];
  for ( std::size_t j = 0; j < 8; ++j )
    r[ j ] = a[ j ];
  std::size_t i = 8;
  for ( ; i < n - ( n % 8 ); i += 8 )
    for ( std::size_t j = 0; j < 8; ++j )
      r[ j ] += a[ i + j ];
  double res = ( ( r[ 0 ] + r[ 1 ] ) + ( r[ 2 ] + r[ 3 ] ) ) + ( ( r[ 4 ] + r[ 5 ] ) + ( r[ 6 ] + r[ 7 ] ) );
  for ( ; i < n; ++i )
    res += a[ i ];
  return res;
}

// sha256 (FIPS 180-4) for the .tbt header: a copy of rl_buff_credit.cpp's (that one lives in its anonymous namespace).
struct table_sha256_t
{
  std::uint32_t h[ 8 ] = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
  std::uint8_t buf[ 64 ];
  std::size_t fill        = 0;
  std::uint64_t total_len = 0;

  static std::uint32_t rotr( std::uint32_t x, unsigned n ) { return ( x >> n ) | ( x << ( 32u - n ) ); }

  void block( const std::uint8_t* p )
  {
    static const std::uint32_t K[ 64 ] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
        0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
        0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
        0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u };
    std::uint32_t w[ 64 ];
    for ( int i = 0; i < 16; ++i )
      w[ i ] = ( std::uint32_t( p[ 4 * i ] ) << 24 ) | ( std::uint32_t( p[ 4 * i + 1 ] ) << 16 ) | ( std::uint32_t( p[ 4 * i + 2 ] ) << 8 ) |
               std::uint32_t( p[ 4 * i + 3 ] );
    for ( int i = 16; i < 64; ++i )
    {
      const std::uint32_t s0 = rotr( w[ i - 15 ], 7 ) ^ rotr( w[ i - 15 ], 18 ) ^ ( w[ i - 15 ] >> 3 );
      const std::uint32_t s1 = rotr( w[ i - 2 ], 17 ) ^ rotr( w[ i - 2 ], 19 ) ^ ( w[ i - 2 ] >> 10 );
      w[ i ]                 = w[ i - 16 ] + s0 + w[ i - 7 ] + s1;
    }
    std::uint32_t a = h[ 0 ], b = h[ 1 ], c = h[ 2 ], d = h[ 3 ], e = h[ 4 ], f = h[ 5 ], g = h[ 6 ], hh = h[ 7 ];
    for ( int i = 0; i < 64; ++i )
    {
      const std::uint32_t S1  = rotr( e, 6 ) ^ rotr( e, 11 ) ^ rotr( e, 25 );
      const std::uint32_t ch  = ( e & f ) ^ ( ~e & g );
      const std::uint32_t t1  = hh + S1 + ch + K[ i ] + w[ i ];
      const std::uint32_t S0  = rotr( a, 2 ) ^ rotr( a, 13 ) ^ rotr( a, 22 );
      const std::uint32_t maj = ( a & b ) ^ ( a & c ) ^ ( b & c );
      const std::uint32_t t2  = S0 + maj;
      hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[ 0 ] += a; h[ 1 ] += b; h[ 2 ] += c; h[ 3 ] += d; h[ 4 ] += e; h[ 5 ] += f; h[ 6 ] += g; h[ 7 ] += hh;
  }

  void update( const void* data, std::size_t n )
  {
    const auto* p = static_cast<const std::uint8_t*>( data );
    total_len += n;
    while ( n > 0 )
    {
      const std::size_t take = std::min<std::size_t>( 64 - fill, n );
      std::memcpy( buf + fill, p, take );
      fill += take;
      p += take;
      n -= take;
      if ( fill == 64 )
      {
        block( buf );
        fill = 0;
      }
    }
  }

  void finish( std::uint8_t out[ 32 ] )
  {
    const std::uint64_t bits = total_len * 8u;
    const std::uint8_t one   = 0x80;
    update( &one, 1 );
    const std::uint8_t zero = 0;
    while ( fill != 56 )
      update( &zero, 1 );
    std::uint8_t len[ 8 ];
    for ( int i = 0; i < 8; ++i )
      len[ i ] = static_cast<std::uint8_t>( bits >> ( 56 - 8 * i ) );
    update( len, 8 );
    for ( int i = 0; i < 8; ++i )
    {
      out[ 4 * i ]     = static_cast<std::uint8_t>( h[ i ] >> 24 );
      out[ 4 * i + 1 ] = static_cast<std::uint8_t>( h[ i ] >> 16 );
      out[ 4 * i + 2 ] = static_cast<std::uint8_t>( h[ i ] >> 8 );
      out[ 4 * i + 3 ] = static_cast<std::uint8_t>( h[ i ] );
    }
  }
};

// ---- (1) the TBP1 file the fork reads ---------------------------------------------------------------------------------------------------
enum class rule_t
{
  integer_buckets,
  float_buckets,
  any_up,
  ready,
  mask_bit,
  cooldown_phase
};

struct feature_t
{
  rule_t              rule = rule_t::mask_bit;
  int                 slot = -1;
  std::vector<int>    slots;
  int                 button = -1;
  float               multiply = 1.0f;
  float               tolerance = 0.0f;
  float               low = 0.0f;
  float               high = 0.0f;
  float               clip_div = 1.0f;
  float               edge_s = 0.0f;
  std::vector<double> edges;  // float64: never narrowed
  std::uint64_t       radix = 1;
};

struct params_t
{
  double       epsilon = 0.0;
  std::int64_t n_episodes = 0;
  double       init_epsilon = 0.0;
  double       min_epsilon = 0.0;
  double       decay_ratio = 0.0;
  double       optimistic_estimate = 0.0;
  std::int64_t initial_count = 0;
  double       init_temp = 0.0;
  double       min_temp = 0.0;
  double       c = 0.0;
  double       alpha = 0.0;
  double       beta = 0.0;
};

struct table_t
{
  std::string            path;
  std::uint32_t          crc = 0;
  std::uint8_t           sha256[ 32 ] = {};  // of the whole file: the .tbt header carries its first 16 bytes
  std::uint64_t          bytes = 0;
  std::string            state_set;
  std::uint64_t          n_states = 0;
  std::vector<feature_t> features;
  strategy_t             strategy = S_NONE;
  params_t               params;
  bool                   decided[ BUTTONS ] = {};
  std::string            scope = "fight";
  double                 update_alpha = 0.0;  // the header's `update_alpha`, in (0, 1]
  std::string            header_text;         // the header bytes of the file exactly as read (JSON padded with spaces): a saved table repeats them verbatim
  bool                   return_reward = false;  // reward_rule is REWARD_RULE_RETURN_ID: no in-fight update
  std::vector<float>     Q;
  std::vector<std::int32_t> N;
  std::vector<double>    M2;
  int                    idle_index = -1;
};

table_t g_table;

std::vector<unsigned char> read_file( const std::string& path )
{
  std::ifstream in( path, std::ios::binary );
  if ( !in.is_open() )
    file_refusal( "TBP_OPEN", fmt::format( "cannot open '{}'", path ) );
  return std::vector<unsigned char>( std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() );
}

template <typename T>
T read_le( const unsigned char* p )
{
  T v;
  std::memcpy( &v, p, sizeof( T ) );
  return v;
}

using value_t = rapidjson::Value;

[[noreturn]] void header_refusal( const std::string& detail )
{
  file_refusal( "TBP_HEADER", detail );
}

const value_t& member( const value_t& obj, const char* key, std::string_view where )
{
  if ( !obj.IsObject() )
    header_refusal( fmt::format( "{}: must be an object", where ) );
  const auto it = obj.FindMember( key );
  if ( it == obj.MemberEnd() )
    header_refusal( fmt::format( "{}: key '{}' is missing", where, key ) );
  return it->value;
}

bool has_member( const value_t& obj, const char* key )
{
  return obj.IsObject() && obj.FindMember( key ) != obj.MemberEnd();
}

std::string str_of( const value_t& obj, const char* key, std::string_view where )
{
  const value_t& v = member( obj, key, where );
  if ( !v.IsString() )
    header_refusal( fmt::format( "{}: '{}' must be a string", where, key ) );
  return std::string( v.GetString(), v.GetStringLength() );
}

std::int64_t int_of( const value_t& obj, const char* key, std::string_view where )
{
  const value_t& v = member( obj, key, where );
  if ( !v.IsInt64() )
    header_refusal( fmt::format( "{}: '{}' must be an integer", where, key ) );
  return v.GetInt64();
}

double num_of( const value_t& obj, const char* key, std::string_view where )
{
  const value_t& v = member( obj, key, where );
  if ( !v.IsNumber() )
    header_refusal( fmt::format( "{}: '{}' must be a number", where, key ) );
  return v.GetDouble();
}

// A float32 field: the double the file holds, rounded to the nearest float32 exactly as np.float32(value) does.
float f32_of( const value_t& obj, const char* key, std::string_view where )
{
  return static_cast<float>( num_of( obj, key, where ) );
}

const value_t& array_of( const value_t& obj, const char* key, std::string_view where )
{
  const value_t& v = member( obj, key, where );
  if ( !v.IsArray() )
    header_refusal( fmt::format( "{}: '{}' must be a list", where, key ) );
  return v;
}

int button_of( const value_t& obj, const char* key, std::string_view where )
{
  const std::int64_t b = int_of( obj, key, where );
  if ( b < 0 || b >= BUTTONS )
    header_refusal( fmt::format( "{}: button {} is outside 0..{}", where, b, BUTTONS - 1 ) );
  return static_cast<int>( b );
}

int slot_of( const value_t& v, std::string_view where )
{
  if ( !v.IsInt64() || v.GetInt64() < 0 || v.GetInt64() >= static_cast<std::int64_t>( RL_OBS_DIM ) )
    header_refusal( fmt::format( "{}: observation slot must be an integer in 0..{}", where, RL_OBS_DIM - 1 ) );
  return static_cast<int>( v.GetInt64() );
}

int idle_button_index()
{
  for ( int i = 0; i < BUTTONS; ++i )
    if ( RL_ACTIONS[ i ].kind == rl_action_kind::wait && RL_ACTIONS[ i ].label != nullptr && std::strcmp( RL_ACTIONS[ i ].label, "wait_next_event" ) == 0 )
      return i;
  return -1;
}

// Parses the header's `params` for the strategy and checks the name set is exactly the strategy's (tbp.py _check_header).
params_t params_of( const value_t& block, const strategy_name_t& st )
{
  const value_t& obj = member( block, "params", "header strategy" );
  if ( !obj.IsObject() )
    header_refusal( "header strategy 'params' must be an object" );
  std::size_t expected = 0;
  for ( int i = 0; i < 4 && st.params[ i ] != nullptr; ++i )
  {
    ++expected;
    if ( !has_member( obj, st.params[ i ] ) )
      header_refusal( fmt::format( "strategy '{}' takes the parameter '{}', the header does not hold it", st.id, st.params[ i ] ) );
  }
  if ( obj.MemberCount() != expected )
    header_refusal( fmt::format( "strategy '{}' takes {} parameters, the header holds {}", st.id, expected, obj.MemberCount() ) );
  params_t p;
  const std::string where = "header strategy params";
  auto get = [ & ]( const char* name ) { return num_of( obj, name, where ); };
  switch ( st.value )
  {
    case S_EPSILON_GREEDY:
      p.epsilon = get( "epsilon" );
      break;
    case S_LIN_DEC_EPSILON_GREEDY:
    case S_EXP_DEC_EPSILON_GREEDY:
      p.n_episodes = static_cast<std::int64_t>( get( "n_episodes" ) );
      p.init_epsilon = get( "init_epsilon" );
      p.min_epsilon = get( "min_epsilon" );
      p.decay_ratio = get( "decay_ratio" );
      break;
    case S_OPTIMISTIC_INITIALIZATION:
      p.optimistic_estimate = get( "optimistic_estimate" );
      p.initial_count = static_cast<std::int64_t>( get( "initial_count" ) );
      break;
    case S_SOFTMAX:
      p.n_episodes = static_cast<std::int64_t>( get( "n_episodes" ) );
      p.init_temp = get( "init_temp" );
      p.min_temp = get( "min_temp" );
      p.decay_ratio = get( "decay_ratio" );
      break;
    case S_UPPER_CONFIDENCE_BOUND:
      p.c = get( "c" );
      break;
    case S_THOMPSON_SAMPLING:
      p.alpha = get( "alpha" );
      p.beta = get( "beta" );
      break;
    default:
      break;
  }
  return p;
}

// The parameter checks the Python raises at call time (bad-decay, bad-temperature, bad-c, bad-thompson, bad-initial-count), made once at load.
void check_params( strategy_t s, const params_t& p )
{
  switch ( s )
  {
    case S_EPSILON_GREEDY:
      if ( !( p.epsilon >= 0.0 && p.epsilon <= 1.0 ) )
        header_refusal( fmt::format( "bad-epsilon: epsilon {} is outside [0, 1]", p.epsilon ) );
      break;
    case S_LIN_DEC_EPSILON_GREEDY:
    {
      const double decay_episodes = static_cast<double>( p.n_episodes ) * p.decay_ratio;
      if ( !( decay_episodes > 0.0 ) || p.min_epsilon > p.init_epsilon )
        header_refusal( "bad-decay: n_episodes x decay_ratio must be positive and min_epsilon <= init_epsilon" );
      break;
    }
    case S_EXP_DEC_EPSILON_GREEDY:
    {
      const std::int64_t decay_episodes = static_cast<std::int64_t>( static_cast<double>( p.n_episodes ) * p.decay_ratio );
      if ( decay_episodes < 1 || p.n_episodes - decay_episodes < 0 || p.min_epsilon > p.init_epsilon )
        header_refusal( "bad-decay: int(n_episodes x decay_ratio) must be at least 1 and min_epsilon <= init_epsilon" );
      break;
    }
    case S_OPTIMISTIC_INITIALIZATION:
      if ( p.initial_count < 1 )
        header_refusal( fmt::format( "bad-initial-count: initial_count {} must be at least 1", p.initial_count ) );
      break;
    case S_SOFTMAX:
    {
      const double decay_episodes = static_cast<double>( p.n_episodes ) * p.decay_ratio;
      if ( p.min_temp <= 0.0 || p.init_temp < p.min_temp )
        header_refusal( "bad-temperature: need 0 < min_temp <= init_temp" );
      if ( !( decay_episodes > 0.0 ) )
        header_refusal( "bad-decay: n_episodes x decay_ratio must be positive" );
      break;
    }
    case S_UPPER_CONFIDENCE_BOUND:
      if ( p.c < 0.0 )
        header_refusal( fmt::format( "bad-c: c {} must not be negative", p.c ) );
      break;
    case S_THOMPSON_SAMPLING:
      if ( !( p.alpha > 0.0 ) || p.beta < 0.0 )
        header_refusal( fmt::format( "bad-thompson: need alpha > 0 and beta >= 0, got alpha {} beta {}", p.alpha, p.beta ) );
      break;
    default:
      break;
  }
}

feature_t feature_of( const value_t& f, std::size_t index )
{
  const std::string where = fmt::format( "header feature {}", index );
  feature_t         out;
  const std::string rule = str_of( f, "rule", where );
  const std::int64_t radix = int_of( f, "radix", where );
  if ( radix < 2 )
    header_refusal( fmt::format( "{}: radix {} is below 2", where, radix ) );
  out.radix = static_cast<std::uint64_t>( radix );
  if ( rule == "integer_buckets" || rule == "float_buckets" )
  {
    out.rule = rule == "integer_buckets" ? rule_t::integer_buckets : rule_t::float_buckets;
    out.slot = slot_of( member( f, "slot", where ), where );
    out.multiply = f32_of( f, "multiply", where );
    out.low = f32_of( f, "low", where );
    out.high = f32_of( f, "high", where );
    if ( out.rule == rule_t::integer_buckets )
      out.tolerance = f32_of( f, "integer_tolerance", where );
    const value_t& edges = array_of( f, "edges", where );
    for ( const value_t& e : edges.GetArray() )
    {
      if ( !e.IsNumber() )
        header_refusal( fmt::format( "{}: 'edges' must hold numbers", where ) );
      out.edges.push_back( e.GetDouble() );
    }
    if ( static_cast<std::int64_t>( out.edges.size() ) + 1 != radix )
      header_refusal( fmt::format( "{}: {} edges do not make radix {}", where, out.edges.size(), radix ) );
    for ( std::size_t i = 1; i < out.edges.size(); ++i )
      if ( !( out.edges[ i - 1 ] <= out.edges[ i ] ) )
        header_refusal( fmt::format( "{}: 'edges' must be non-decreasing (np.digitize needs monotonic bins)", where ) );
  }
  else if ( rule == "any_up" )
  {
    out.rule = rule_t::any_up;
    for ( const value_t& s : array_of( f, "slots", where ).GetArray() )
      out.slots.push_back( slot_of( s, where ) );
    if ( out.slots.empty() )
      header_refusal( fmt::format( "{}: an any_up rule needs at least one slot", where ) );
    if ( radix != 2 )
      header_refusal( fmt::format( "{}: an any_up rule has radix 2", where ) );
  }
  else if ( rule == "ready" )
  {
    out.rule = rule_t::ready;
    out.slot = slot_of( member( f, "slot", where ), where );
    if ( radix != 2 )
      header_refusal( fmt::format( "{}: a ready rule has radix 2", where ) );
  }
  else if ( rule == "mask_bit" )
  {
    out.rule = rule_t::mask_bit;
    out.button = button_of( f, "button", where );
    if ( radix != 2 )
      header_refusal( fmt::format( "{}: a mask_bit rule has radix 2", where ) );
  }
  else if ( rule == "cooldown_phase" )
  {
    out.rule = rule_t::cooldown_phase;
    out.slot = slot_of( member( f, "slot", where ), where );
    out.button = button_of( f, "button", where );
    out.clip_div = f32_of( f, "clip_div", where );
    out.edge_s = f32_of( f, "edge_s", where );
    if ( radix != 3 )
      header_refusal( fmt::format( "{}: a cooldown_phase rule has radix 3", where ) );
  }
  else
  {
    header_refusal( fmt::format( "{}: unknown rule '{}'", where, rule ) );
  }
  return out;
}

// Reads and validates one TBP1 file against this build. Refusal tokens are tbp.py's (TBP_MAGIC, TBP_VERSION, TBP_TRUNCATED, TBP_HEADER, TBP_CRC, TBP_SHAPE)
// plus TBP_FINGERPRINT (the three contract hashes), TBP_STATES and TBP_OPEN.
table_t load_table( const std::string& path )
{
  const std::vector<unsigned char> data = read_file( path );
  if ( data.size() < TBP_PREAMBLE || std::memcmp( data.data(), "TBP1", 4 ) != 0 )
    file_refusal( "TBP_MAGIC", fmt::format( "{}: does not start with the magic 'TBP1'", path ) );
  const std::uint32_t version = read_le<std::uint32_t>( data.data() + 4 );
  if ( version != TBP_VERSION )
    file_refusal( "TBP_VERSION", fmt::format( "{}: format version {}, this reader reads {}", path, version, TBP_VERSION ) );
  const std::uint64_t header_bytes = read_le<std::uint64_t>( data.data() + 8 );
  if ( header_bytes % 8 != 0 || header_bytes > data.size() || TBP_PREAMBLE + header_bytes + 4 > data.size() )
    file_refusal( "TBP_TRUNCATED", fmt::format( "{}: the header says {} bytes, the file holds {}", path, header_bytes, data.size() ) );

  table_t t;
  t.path = path;
  t.bytes = data.size();
  t.header_text.assign( reinterpret_cast<const char*>( data.data() ) + TBP_PREAMBLE, static_cast<std::size_t>( header_bytes ) );

  rapidjson::Document doc;
  {
    const std::string text( reinterpret_cast<const char*>( data.data() ) + TBP_PREAMBLE, static_cast<std::size_t>( header_bytes ) );
    doc.Parse<rapidjson::kParseFullPrecisionFlag>( text.c_str() );
  }
  if ( doc.HasParseError() )
    header_refusal( fmt::format( "{}: the header is not JSON (offset {})", path, doc.GetErrorOffset() ) );
  if ( !doc.IsObject() )
    header_refusal( "header must be an object" );

  if ( str_of( doc, "format", "header" ) != "TBP1" || int_of( doc, "version", "header" ) != static_cast<std::int64_t>( version ) )
    header_refusal( "header format/version disagree with the preamble" );
  if ( str_of( doc, "spec_id", "header" ) != RL_REGISTRY_ID )
    header_refusal( fmt::format( "spec_id '{}' is not this build's registry '{}'", str_of( doc, "spec_id", "header" ), RL_REGISTRY_ID ) );

  const value_t& prints = member( doc, "fingerprints", "header" );
  struct print_t
  {
    const char* key;
    const char* baked;
  };
  for ( const print_t& pr : { print_t{ "obs_schema_sha", RL_OBS_SCHEMA_SHA }, print_t{ "mask_rules_sha", RL_MASK_RULES_SHA },
                              print_t{ "action_space_sha", RL_ACTION_SPACE_SHA } } )
  {
    const std::string got = str_of( prints, pr.key, "header fingerprints" );
    if ( got != pr.baked )
      file_refusal( "TBP_FINGERPRINT", fmt::format( "{}: {} is '{}', this build bakes '{}'", path, pr.key, got, pr.baked ) );
  }
  if ( int_of( doc, "observation_width", "header" ) != static_cast<std::int64_t>( RL_OBS_DIM ) )
    header_refusal( fmt::format( "observation_width {} is not this build's {}", int_of( doc, "observation_width", "header" ), RL_OBS_DIM ) );

  // The buttons: the registry's tokens in order (a tokenless wait is `wait:<label>`).
  const value_t& buttons = array_of( doc, "buttons", "header" );
  if ( buttons.Size() != RL_ACTION_DIM )
    header_refusal( fmt::format( "'buttons' holds {} entries, this build has {}", buttons.Size(), RL_ACTION_DIM ) );
  std::vector<std::string> tokens;
  for ( rapidjson::SizeType i = 0; i < buttons.Size(); ++i )
  {
    if ( !buttons[ i ].IsString() )
      header_refusal( "'buttons' must hold strings" );
    tokens.emplace_back( buttons[ i ].GetString(), buttons[ i ].GetStringLength() );
    const rl_action_desc& a = RL_ACTIONS[ i ];
    const std::string     want = a.token != nullptr ? std::string( a.token ) : fmt::format( "wait:{}", a.label != nullptr ? a.label : "" );
    if ( tokens.back() != want )
      header_refusal( fmt::format( "button {} is '{}' in the file, '{}' in this build", i, tokens.back(), want ) );
  }

  const value_t& set = member( doc, "state_set", "header" );
  t.state_set = str_of( set, "name", "header state_set" );
  const std::int64_t n_states = int_of( set, "n_states", "header state_set" );
  if ( n_states < 1 )
    header_refusal( "n_states must be positive" );
  if ( static_cast<std::uint64_t>( n_states ) > TBP_MAX_STATES )
    file_refusal( "TBP_STATES", fmt::format( "{}: {} states exceed the dense-table cap of {}", path, n_states, TBP_MAX_STATES ) );
  t.n_states = static_cast<std::uint64_t>( n_states );

  const value_t& features = array_of( doc, "features", "header" );
  std::uint64_t  product = 1;
  for ( rapidjson::SizeType i = 0; i < features.Size(); ++i )
  {
    feature_t f = feature_of( features[ i ], i );
    // A mask_bit / cooldown_phase names its token as well: it must be the button's token.
    if ( f.rule == rule_t::mask_bit || f.rule == rule_t::cooldown_phase )
    {
      const std::string named = str_of( features[ i ], "token", fmt::format( "header feature {}", i ) );
      if ( named != tokens[ static_cast<std::size_t>( f.button ) ] )
        header_refusal( fmt::format( "header feature {}: token '{}' is not button {}'s '{}'", i, named, f.button, tokens[ static_cast<std::size_t>( f.button ) ] ) );
    }
    product *= f.radix;
    if ( product > TBP_MAX_STATES * 4096ull )
      header_refusal( "the features' radix product overflows the state cap" );
    t.features.push_back( std::move( f ) );
  }
  if ( product != t.n_states )
    header_refusal( fmt::format( "the features' radix product is {}, the state set says {} states", product, t.n_states ) );

  // The decided buttons: tbp.py's `decided` block {tokens, buttons}. Tokens are resolved against this build's cast buttons, and `buttons[i]` must be the
  // index of `tokens[i]` in the header's own `buttons` list (the check is by token text, `strcmp( RL_ACTIONS[b].token, tok ) == 0`), never by position alone.
  const value_t& decided_block = member( doc, "decided", "header" );
  const value_t& decided = array_of( decided_block, "tokens", "header decided" );
  const value_t& decided_buttons = array_of( decided_block, "buttons", "header decided" );
  if ( decided.Size() == 0 )
    header_refusal( "decided.tokens is empty: the player would decide nothing" );
  if ( decided_buttons.Size() != decided.Size() )
    header_refusal( fmt::format( "decided.buttons holds {} entries, decided.tokens {}", decided_buttons.Size(), decided.Size() ) );
  for ( rapidjson::SizeType i = 0; i < decided.Size(); ++i )
  {
    if ( !decided[ i ].IsString() )
      header_refusal( "'decided.tokens' must hold strings" );
    const std::string token( decided[ i ].GetString(), decided[ i ].GetStringLength() );
    int               found = -1;
    for ( int b = 0; b < BUTTONS; ++b )
      if ( RL_ACTIONS[ b ].kind == rl_action_kind::cast && RL_ACTIONS[ b ].token != nullptr && std::strcmp( RL_ACTIONS[ b ].token, token.c_str() ) == 0 )
        found = b;
    if ( found < 0 )
      header_refusal( fmt::format( "decided token '{}' is not a cast button of this build", token ) );
    if ( !decided_buttons[ i ].IsInt64() || decided_buttons[ i ].GetInt64() != found )
      header_refusal( fmt::format( "decided.buttons[{}] is not {}, the index of token '{}' in this build", i, found, token ) );
    if ( t.decided[ found ] )
      header_refusal( fmt::format( "decided token '{}' appears twice", token ) );
    t.decided[ found ] = true;
  }

  const value_t& strategy = member( doc, "strategy", "header" );
  const std::string strategy_id = str_of( strategy, "id", "header strategy" );
  const strategy_name_t* named = nullptr;
  for ( const strategy_name_t& s : STRATEGY_NAMES )
    if ( strategy_id == s.id )
      named = &s;
  if ( named == nullptr )
    header_refusal( fmt::format( "strategy id '{}' is not one of the nine", strategy_id ) );
  t.strategy = named->value;
  t.params = params_of( strategy, *named );
  check_params( t.strategy, t.params );

  if ( str_of( doc, "rng", "header" ) != RNG_RULE_TEXT )
    header_refusal( "the rng rule is not splitmix64-v1 as this reader implements it" );
  if ( str_of( doc, "update_rule", "header" ) != UPDATE_RULE_ID )
    header_refusal( fmt::format( "update_rule '{}' is not '{}'", str_of( doc, "update_rule", "header" ), UPDATE_RULE_ID ) );
  t.update_alpha = num_of( doc, "update_alpha", "header" );
  if ( !( std::isfinite( t.update_alpha ) && t.update_alpha > 0.0 && t.update_alpha <= 1.0 ) )
    header_refusal( fmt::format( "update_alpha {} is outside (0, 1]", t.update_alpha ) );
  {
    const std::string reward_rule = str_of( doc, "reward_rule", "header" );
    if ( reward_rule != REWARD_RULE_ID && reward_rule != REWARD_RULE_RETURN_ID )
      header_refusal( fmt::format( "reward_rule '{}' is neither '{}' nor '{}'", reward_rule, REWARD_RULE_ID, REWARD_RULE_RETURN_ID ) );
    t.return_reward = reward_rule == REWARD_RULE_RETURN_ID;
  }
  t.scope = str_of( doc, "scope", "header" );
  if ( t.scope != SCOPE_ID )
    header_refusal( fmt::format( "scope '{}' is not '{}' (this build plays the fight scope only: Q, N and M2 are copied from the file at every fight start)", t.scope, SCOPE_ID ) );
  // The K rule is recorded and read by nobody (the player does not apply it: table_certify.py); it must be there and well formed (tbp.py _check_header).
  const value_t& krule = member( doc, "k", "header" );
  if ( int_of( krule, "value", "header k" ) < 0 )
    header_refusal( "k.value must not be negative" );

  // The arrays: Q float32, N int32, optionally M2 float64, then the CRC-32 of every byte before it.
  const std::uint64_t cells = t.n_states * static_cast<std::uint64_t>( BUTTONS );
  const std::uint64_t need = TBP_PREAMBLE + header_bytes + cells * 16ull + 4ull;  // Q float32, N int32, M2 float64, CRC-32
  if ( data.size() != need )
    file_refusal( "TBP_TRUNCATED", fmt::format( "{}: {} states x {} buttons need {} bytes, the file holds {}", path, t.n_states, BUTTONS, need, data.size() ) );
  const std::uint32_t crc = rl_translog::crc32( data.data(), data.size() - 4 );
  if ( crc != read_le<std::uint32_t>( data.data() + data.size() - 4 ) )
    file_refusal( "TBP_CRC", fmt::format( "{}: the CRC-32 of the file does not match its trailer", path ) );
  t.crc = crc;
  {
    table_sha256_t h;
    h.update( data.data(), data.size() );
    h.finish( t.sha256 );
  }

  const unsigned char* arrays = data.data() + TBP_PREAMBLE + header_bytes;
  t.Q.resize( cells );
  t.N.resize( cells );
  std::memcpy( t.Q.data(), arrays, cells * 4 );
  std::memcpy( t.N.data(), arrays + cells * 4, cells * 4 );
  t.M2.resize( cells );
  std::memcpy( t.M2.data(), arrays + cells * 8, cells * 8 );
  for ( std::uint64_t i = 0; i < cells; ++i )
  {
    if ( !std::isfinite( t.Q[ i ] ) )
      file_refusal( "TBP_SHAPE", fmt::format( "{}: Q holds a NaN or an infinity (cell {})", path, i ) );
    if ( t.N[ i ] < 0 )
      file_refusal( "TBP_SHAPE", fmt::format( "{}: N holds a negative count (cell {})", path, i ) );
    if ( !( std::isfinite( t.M2[ i ] ) && t.M2[ i ] >= 0.0 ) )
      file_refusal( "TBP_SHAPE", fmt::format( "{}: M2 holds a negative or non-finite value (cell {})", path, i ) );
  }

  // optimistic_initialization (version 3): the prior is already in the file's arrays (tbp.optimistic_file_arrays folds it at compile time), so the table the player
  // starts from, the table it saves and the table the next launch loads are the same thing; nothing is folded here.

  t.idle_index = idle_button_index();
  if ( t.idle_index < 0 )
    header_refusal( "this build's registry has no wait action labelled wait_next_event" );
  return t;
}

// ---- (3) the six feature evaluators: state.py's float32 pipeline, as tbp.py header_state_ids reproduces it ------------------------------
inline std::size_t digitize( const std::vector<double>& edges, double x )
{
  // np.digitize(x, edges) with right=False on non-decreasing bins: the number of edges at or below x.
  std::size_t n = 0;
  for ( const double e : edges )
    if ( e <= x )
      ++n;
  return n;
}

// The state id of one observation row, or false when a value is outside its bucket's domain (state.py BUCKET_VALUE_OUT_OF_RANGE).
bool bucket( const table_t& t, const float* obs, const std::uint8_t* mask, std::uint64_t& id )
{
  std::uint64_t acc = 0;
  for ( const feature_t& f : t.features )
  {
    std::uint64_t part = 0;
    switch ( f.rule )
    {
      case rule_t::integer_buckets:
      {
        const float scaled = obs[ f.slot ] * f.multiply;
        const float counts = std::nearbyintf( scaled );  // np.rint: half to even under the default rounding mode
        const bool  ok = ( std::fabs( scaled - counts ) <= f.tolerance ) && ( counts >= f.low ) && ( counts <= f.high );
        if ( !ok )
          return false;
        part = digitize( f.edges, static_cast<double>( static_cast<std::int64_t>( counts ) ) );
        break;
      }
      case rule_t::float_buckets:
      {
        const float scaled = obs[ f.slot ] * f.multiply;
        const bool  ok = ( scaled >= f.low ) && ( scaled <= f.high );
        if ( !ok )
          return false;
        part = digitize( f.edges, static_cast<double>( scaled ) );
        break;
      }
      case rule_t::any_up:
      {
        bool up = false;
        for ( const int slot : f.slots )
        {
          const float v = obs[ slot ];
          if ( !( v >= 0.0f && v <= 1.0f ) )
            return false;
          up = up || ( v > 0.0f );
        }
        part = up ? 1 : 0;
        break;
      }
      case rule_t::ready:
      {
        const float v = obs[ f.slot ];
        if ( !( v >= 0.0f && v <= 1.0f ) )
          return false;
        part = ( !( v > 0.0f ) ) ? 1 : 0;
        break;
      }
      case rule_t::mask_bit:
        part = mask[ f.button ] != 0 ? 1 : 0;
        break;
      case rule_t::cooldown_phase:
      {
        const float v = obs[ f.slot ];
        if ( !( v >= 0.0f && v <= 1.0f ) )
          return false;
        const bool soon = ( v > 0.0f ) && ( v * f.clip_div < f.edge_s );
        part = mask[ f.button ] != 0 ? 0 : ( soon ? 1 : 2 );  // READY 0, SOON 1, LATER 2
        break;
      }
    }
    acc = acc * f.radix + part;
  }
  id = acc;
  return true;
}

// ---- (4) the nine strategies: book/ch04_strategies.py ----------------------------------------------------------------------------------
struct choice_t
{
  int    action = -1;
  bool   has_probs = false;
  double probs[ BUTTONS ] = {};
  int    draws = 0;
  double values[ MAX_VALUES ] = {};
  int    n_values = 0;
};

double linear_decay( std::int64_t e, std::int64_t n_episodes, double init_value, double min_value, double decay_ratio )
{
  const double decay_episodes = static_cast<double>( n_episodes ) * decay_ratio;
  if ( !( decay_episodes > 0.0 ) || min_value > init_value )
    refuse( "TABLE_STRATEGY", "bad-decay: n_episodes x decay_ratio must be positive and min <= init" );
  double value = 1 - static_cast<double>( e ) / decay_episodes;
  value *= init_value - min_value;
  value += min_value;
  return std::min( std::max( value, min_value ), init_value );
}

// exp_dec_epsilons(...)[min(e, n_episodes - 1)] without the array: np.logspace(-2, 0, decay_episodes) is power(10.0, linspace(-2, 0, num)) with
// linspace(i) = i * (2.0 / (num - 1)) + (-2.0), the last point exactly 0.0, and np.pad 'edge' repeats the last entry for e >= decay_episodes.
double exp_dec_epsilon_at( std::int64_t e, std::int64_t n_episodes, double init_epsilon, double min_epsilon, double decay_ratio )
{
  const std::int64_t decay_episodes = static_cast<std::int64_t>( static_cast<double>( n_episodes ) * decay_ratio );
  if ( decay_episodes < 1 || n_episodes - decay_episodes < 0 || min_epsilon > init_epsilon )
    refuse( "TABLE_STRATEGY", "bad-decay: int(n_episodes x decay_ratio) must be at least 1 and min <= init" );
  std::int64_t i = std::min<std::int64_t>( e, n_episodes - 1 );
  if ( i < 0 )
    i = 0;
  if ( i > decay_episodes - 1 )
    i = decay_episodes - 1;
  double y;
  if ( decay_episodes > 1 )
  {
    const double step = 2.0 / static_cast<double>( decay_episodes - 1 );
    y = static_cast<double>( i ) * step;
    y += -2.0;
    if ( i == decay_episodes - 1 )
      y = 0.0;
  }
  else
  {
    y = 0.0 * 2.0;
    y += -2.0;
  }
  double eps = 0.01 / std::pow( 10.0, y );
  eps *= init_epsilon - min_epsilon;
  eps += min_epsilon;
  return eps;
}

// `Q` is the row widened to float64, `N` the counts, `sd` the per-cell reward standard deviation or null, `legal` the available buttons (mask AND decided).
choice_t run_strategy( const table_t& t, const double* Q, const std::int64_t* N, const double* sd, const std::uint8_t* legal, std::uint64_t fight_seed,
                       std::uint64_t decision_index, std::int64_t e )
{
  const params_t& p = t.params;
  choice_t        out;
  stream_t        stream( fight_seed, decision_index );

  int avail[ BUTTONS ];
  int n_avail = 0;
  for ( int b = 0; b < BUTTONS; ++b )
    if ( legal[ b ] )
      avail[ n_avail++ ] = b;
  if ( n_avail == 0 )
    refuse( "TABLE_STRATEGY", "no-available-action: the mask allows no button" );
  for ( int b = 0; b < BUTTONS; ++b )
    if ( std::isnan( Q[ b ] ) )
      refuse( "TABLE_STRATEGY", "nan-in-q: the Q row holds a NaN" );

  // np.argmax(np.where(mask, Q, -inf)): the first maximum among the available buttons.
  auto greedy = [ & ]() {
    int best = avail[ 0 ];
    for ( int k = 1; k < n_avail; ++k )
      if ( Q[ avail[ k ] ] > Q[ best ] )
        best = avail[ k ];
    return best;
  };
  auto one_hot = [ & ]( int action ) {
    out.has_probs = true;
    for ( int b = 0; b < BUTTONS; ++b )
      out.probs[ b ] = 0.0;
    out.probs[ action ] = 1.0;
    out.action = action;
  };
  auto epsilon_choice = [ & ]( double epsilon ) {
    if ( !( 0.0 <= epsilon && epsilon <= 1.0 ) )
      refuse( "TABLE_STRATEGY", fmt::format( "bad-epsilon: epsilon {} is outside [0, 1]", epsilon ) );
    const int g = greedy();
    int       action = g;
    if ( !( stream.uniform() > epsilon ) )
      action = avail[ stream.index( n_avail ) ];
    out.has_probs = true;
    for ( int b = 0; b < BUTTONS; ++b )
      out.probs[ b ] = 0.0;
    for ( int k = 0; k < n_avail; ++k )
      out.probs[ avail[ k ] ] = epsilon / static_cast<double>( n_avail );
    out.probs[ g ] += 1.0 - epsilon;
    out.action = action;
  };

  switch ( t.strategy )
  {
    case S_PURE_EXPLOITATION:
    case S_OPTIMISTIC_INITIALIZATION:
      one_hot( greedy() );
      break;
    case S_PURE_EXPLORATION:
    {
      out.action = avail[ stream.index( n_avail ) ];
      out.has_probs = true;
      for ( int b = 0; b < BUTTONS; ++b )
        out.probs[ b ] = 0.0;
      for ( int k = 0; k < n_avail; ++k )
        out.probs[ avail[ k ] ] = 1.0 / static_cast<double>( n_avail );
      break;
    }
    case S_EPSILON_GREEDY:
      epsilon_choice( p.epsilon );
      break;
    case S_LIN_DEC_EPSILON_GREEDY:
      epsilon_choice( linear_decay( e, p.n_episodes, p.init_epsilon, p.min_epsilon, p.decay_ratio ) );
      break;
    case S_EXP_DEC_EPSILON_GREEDY:
      epsilon_choice( exp_dec_epsilon_at( e, p.n_episodes, p.init_epsilon, p.min_epsilon, p.decay_ratio ) );
      break;
    case S_SOFTMAX:
    {
      const double temp = linear_decay( e, p.n_episodes, p.init_temp, p.min_temp, p.decay_ratio );
      double scaled[ BUTTONS ];
      double norm[ BUTTONS ];
      double expq[ BUTTONS ];
      double probs_av[ BUTTONS ];
      double cum[ BUTTONS ];
      double mx = -std::numeric_limits<double>::infinity();
      for ( int k = 0; k < n_avail; ++k )
      {
        scaled[ k ] = Q[ avail[ k ] ] / temp;
        if ( scaled[ k ] > mx )
          mx = scaled[ k ];
      }
      for ( int k = 0; k < n_avail; ++k )
      {
        norm[ k ] = scaled[ k ] - mx;
        expq[ k ] = std::exp( norm[ k ] );
      }
      const double total = np_sum( expq, static_cast<std::size_t>( n_avail ) );
      for ( int k = 0; k < n_avail; ++k )
        probs_av[ k ] = expq[ k ] / total;
      const double check = np_sum( probs_av, static_cast<std::size_t>( n_avail ) );
      if ( !( std::fabs( check - 1.0 ) <= 1e-8 + 1e-5 * 1.0 ) )
        refuse( "TABLE_STRATEGY", fmt::format( "bad-probabilities: the softmax probabilities sum to {}", check ) );
      double running = 0.0;
      for ( int k = 0; k < n_avail; ++k )
      {
        running = ( k == 0 ) ? probs_av[ 0 ] : running + probs_av[ k ];
        cum[ k ] = running;
      }
      const double u = stream.uniform();
      int          position = 0;  // np.searchsorted(cum, u, side='right'): the entries at or below u
      for ( int k = 0; k < n_avail; ++k )
        if ( cum[ k ] <= u )
          ++position;
      position = std::min( position, n_avail - 1 );
      out.has_probs = true;
      for ( int b = 0; b < BUTTONS; ++b )
        out.probs[ b ] = 0.0;
      for ( int k = 0; k < n_avail; ++k )
        out.probs[ avail[ k ] ] = probs_av[ k ];
      out.action = avail[ position ];
      break;
    }
    case S_UPPER_CONFIDENCE_BOUND:
    {
      int untried = -1;
      for ( int k = 0; k < n_avail && untried < 0; ++k )
        if ( N[ avail[ k ] ] == 0 )
          untried = avail[ k ];
      if ( untried >= 0 )
      {
        one_hot( untried );
        break;
      }
      std::int64_t sum_n = 0;
      for ( int b = 0; b < BUTTONS; ++b )
        sum_n += N[ b ];
      const std::int64_t e_pulls = std::max<std::int64_t>( sum_n, 1 );
      const double       log_e = std::log( static_cast<double>( e_pulls ) );
      int                best = -1;
      double             best_value = 0.0;
      for ( int k = 0; k < n_avail; ++k )
      {
        const int    b = avail[ k ];
        const double U = std::sqrt( p.c * log_e / static_cast<double>( N[ b ] ) );
        const double v = Q[ b ] + U;
        if ( best < 0 || v > best_value )
        {
          best = b;
          best_value = v;
        }
      }
      one_hot( best );
      break;
    }
    case S_THOMPSON_SAMPLING:
    {
      double samples[ BUTTONS ];
      for ( int b = 0; b < BUTTONS; ++b )
      {
        const double width = ( sd != nullptr && N[ b ] >= 2 ) ? sd[ b ] : p.alpha;
        const double scale = width / ( std::sqrt( static_cast<double>( std::max<std::int64_t>( N[ b ], 1 ) ) ) + p.beta );
        const double noise = stream.normal();
        samples[ b ] = Q[ b ] + scale * noise;
      }
      int best = avail[ 0 ];
      for ( int k = 1; k < n_avail; ++k )
        if ( samples[ avail[ k ] ] > samples[ best ] )
          best = avail[ k ];
      out.action = best;
      out.has_probs = false;
      break;
    }
    default:
      refuse( "TABLE_STRATEGY", "strategy-unknown" );
  }
  out.draws = stream.draws;
  out.n_values = stream.n_values;
  for ( int i = 0; i < stream.n_values; ++i )
    out.values[ i ] = stream.values[ i ];
  return out;
}

// ---- (5) the runtime ---------------------------------------------------------------------------------------------------------------------
// The `.tbt` trace (solver_table_trace=1): `<rl_translog>.tbt`, little-endian, in the layout of the repository's sim/tbt.py (the Python side is the specification, the
// fork follows: quick 261025-bk4 wave B3). Written once per decision at the same point as the translog row (before accept_cast), once per fight for the closing update
// (kind 3), and a footer (kind 2) when the translog's footer is written.
//   header   40 bytes  "TBT1", u32 version 2, u32 record size 800, u32 header size 40, the first 16 bytes of the sha256 of the TBP1 file played, u64 clock (solver_table_clock: the `e`
//                      of the decaying strategies for every decision of this launch; version 2 added it, quick 261026-lrn)
//   record   800 bytes, `<IIIIQIIBBBBI24d22f22i22d22dIB3xdfifid`
//            @0 u32 kind (1 decision, 2 footer, 3 fight end)  @4 u32 seq (footer: decision records; fight end: 0)  @8 u32 iteration (fight index; footer: fight-end records)
//            @12 u32 decision_index (fight end: the decisions the fight made)  @16 u64 fight seed (sim->seed)  @24 u32 state id (0xFFFFFFFF none)  @28 u32 packed FINAL mask
//            @32 u8 chooser  @33 u8 strategy  @34 u8 button (255 none)  @35 u8 has_update  @36 u32 raw 64-bit draws consumed
//            @40 f64[24] the values the strategy consumed in order (a uniform, an index's uniform, or ONE normal for the two uniforms inside it; Thompson: 22 normals)
//            @232 f32[22] Q row  @320 i32[22] N row  @408 f64[22] M2 row (the row as the pick saw it: after the previous update, zeros when no state)
//            @584 f64[22] the probabilities the strategy sampled from (0.0 on an illegal button; all zero for Thompson and for every row without a strategy)
//            @760 u32 update: state  @764 u8 update: button  @765 3 pad  @768 f64 reward  @776 f32 Q_old  @780 i32 N_old  @784 f32 Q_new  @788 i32 N_new  @792 f64 M2_new
// The update block is the fold applied at THIS record (has_update = 1) for the pending strategy decision.
struct alignas( 8 ) trace_record_t
{
  std::uint32_t kind;
  std::uint32_t seq;
  std::uint32_t iteration;
  std::uint32_t decision_index;
  std::uint64_t fight_seed;
  std::uint32_t state_id;
  std::uint32_t mask;
  std::uint8_t  chooser;
  std::uint8_t  strategy;
  std::uint8_t  button;
  std::uint8_t  has_update;
  std::uint32_t n_draws;
  double        values[ MAX_VALUES ];
  float         q[ BUTTONS ];
  std::int32_t  n[ BUTTONS ];
  double        m2[ BUTTONS ];
  double        prob[ BUTTONS ];
  std::uint32_t upd_state;
  std::uint8_t  upd_button;
  std::uint8_t  pad[ 3 ];
  double        upd_reward;
  float         upd_q_old;
  std::int32_t  upd_n_old;
  float         upd_q_new;
  std::int32_t  upd_n_new;
  double        upd_m2_new;
};
static_assert( sizeof( trace_record_t ) == 800, "trace record layout" );
static_assert( offsetof( trace_record_t, fight_seed ) == 16 && offsetof( trace_record_t, state_id ) == 24 && offsetof( trace_record_t, chooser ) == 32 &&
                   offsetof( trace_record_t, n_draws ) == 36 && offsetof( trace_record_t, values ) == 40 && offsetof( trace_record_t, q ) == 232 &&
                   offsetof( trace_record_t, n ) == 320 && offsetof( trace_record_t, m2 ) == 408 && offsetof( trace_record_t, prob ) == 584 &&
                   offsetof( trace_record_t, upd_state ) == 760 && offsetof( trace_record_t, upd_reward ) == 768 && offsetof( trace_record_t, upd_n_new ) == 788 &&
                   offsetof( trace_record_t, upd_m2_new ) == 792,
               "trace record offsets" );

struct pending_t
{
  bool          valid = false;
  std::uint32_t state = 0;
  int           button = -1;
  double        damage = 0.0;
};

struct runtime_t
{
  std::vector<float>        Q;
  std::vector<std::int32_t> N;
  std::vector<double>       M2;
  std::uint64_t             rows = 0;
  pending_t                 pending;
  player_t*                 actor = nullptr;
  std::unique_ptr<io::ofstream> trace;
  std::string               trace_path;
  std::uint32_t             trace_decisions = 0;  // the footer's counts
  std::uint32_t             trace_fight_ends = 0;
};

runtime_t g_rt;
bool      g_defer_stock = true;  // defer=stock (default)
bool      g_defer_all = false;   // defer=all (arm A1)
bool      g_refuse_fallback = false;
bool      g_loaded = false;

struct update_info_t
{
  bool          applied = false;
  std::uint32_t state = 0;
  int           button = 0;
  double        reward = 0.0;
  float         q_old = 0.0f, q_new = 0.0f;
  std::int32_t  n_old = 0, n_new = 0;
  double        m2_new = 0.0;
};

// update_rule 'constant-alpha-floor-f64-v1' (tbp.py UPDATE_RULES; book/ch04_strategies.update is the reference): N[s,a] += 1 (refused at 2^31-1); alpha_n = max(alpha, 1.0 / double(N[s,a]));
// Q_old = double(Q[s,a]); Q[s,a] = float32(Q_old + alpha_n * (r - Q_old)). M2 is the exponentially weighted variance times N: v_old = M2 / double(N_old) (0.0 when N_old = 0);
// delta = r - Q_old; v_new = (1.0 - alpha_n) * (v_old + alpha_n * (delta * delta)); M2 = double(N_new) * v_new. Every product and sum is rounded to double on its own
// (rl_table_policy.cpp is compiled with -ffp-contract=off).
update_info_t apply_update( const pending_t& pend, double damage_now )
{
  update_info_t u;
  if ( g_table.return_reward )
    return u;  // reward_rule damage-to-fight-end-v1: the return of this press is unknown until the fight ends, so Q, N and M2 stay as loaded; applied = false, the trace says so
  const std::size_t cell = static_cast<std::size_t>( pend.state ) * BUTTONS + static_cast<std::size_t>( pend.button );
  const double      reward = damage_now - pend.damage;
  if ( !std::isfinite( reward ) )
    refuse( "TABLE_STRATEGY", fmt::format( "the reward {} is not finite", reward ) );
  if ( g_rt.N[ cell ] == std::numeric_limits<std::int32_t>::max() )
    refuse( "TABLE_STRATEGY", fmt::format( "n-overflow: the count of cell ({}, {}) is at the int32 maximum", pend.state, pend.button ) );
  const double q_old = static_cast<double>( g_rt.Q[ cell ] );
  u.applied = true;
  u.state = pend.state;
  u.button = pend.button;
  u.reward = reward;
  u.q_old = g_rt.Q[ cell ];
  u.n_old = g_rt.N[ cell ];
  const std::int32_t n_old = g_rt.N[ cell ];
  g_rt.N[ cell ] += 1;
  const double alpha_n = std::max( g_table.update_alpha, 1.0 / static_cast<double>( g_rt.N[ cell ] ) );
  const double delta = reward - q_old;
  const float  q_new = static_cast<float>( q_old + alpha_n * delta );
  if ( !std::isfinite( q_new ) )
    refuse( "TABLE_STRATEGY", fmt::format( "the updated Q of cell ({}, {}) is not finite", pend.state, pend.button ) );
  g_rt.Q[ cell ] = q_new;
  const double v_old = n_old > 0 ? g_rt.M2[ cell ] / static_cast<double>( n_old ) : 0.0;
  const double v_new = ( 1.0 - alpha_n ) * ( v_old + alpha_n * ( delta * delta ) );
  g_rt.M2[ cell ] = static_cast<double>( g_rt.N[ cell ] ) * v_new;
  u.q_new = q_new;
  u.n_new = g_rt.N[ cell ];
  u.m2_new = g_rt.M2[ cell ];
  return u;
}

// solver_table_save: the table at the end of the kept fight, in the file layout of tbp.py (_encode): the preamble (magic, version, header length), the loaded file's header bytes
// verbatim, Q float32, N int32, M2 float64 (little-endian, the host's own byte order on x86-64), and the CRC-32/IEEE of every byte before the trailer. It goes to <path>.tmp first and
// is renamed, so a reader never sees half a file. The loader reads it back with no change (the header is the loaded file's).
void save_table( const std::string& path )
{
  const table_t&      t = g_table;
  const std::uint64_t cells = t.n_states * static_cast<std::uint64_t>( BUTTONS );
  std::vector<unsigned char> out;
  out.reserve( static_cast<std::size_t>( TBP_PREAMBLE + t.header_text.size() + cells * 16ull + 4ull ) );
  auto put = [ & ]( const void* p, std::size_t n ) {
    const unsigned char* b = static_cast<const unsigned char*>( p );
    out.insert( out.end(), b, b + n );
  };
  const std::uint32_t version = TBP_VERSION;
  const std::uint64_t header_bytes = t.header_text.size();
  put( "TBP1", 4 );
  put( &version, 4 );
  put( &header_bytes, 8 );
  put( t.header_text.data(), t.header_text.size() );
  put( g_rt.Q.data(), static_cast<std::size_t>( cells ) * 4 );
  put( g_rt.N.data(), static_cast<std::size_t>( cells ) * 4 );
  put( g_rt.M2.data(), static_cast<std::size_t>( cells ) * 8 );
  const std::uint32_t crc = rl_translog::crc32( out.data(), out.size() );
  put( &crc, 4 );
  const std::string tmp = path + ".tmp";
  {
    std::ofstream file( tmp, std::ios::binary | std::ios::trunc );
    if ( !file.is_open() )
      refuse( "TABLE_FILE", fmt::format( "unable to open '{}' for writing (solver_table_save)", tmp ) );
    file.write( reinterpret_cast<const char*>( out.data() ), static_cast<std::streamsize>( out.size() ) );
    file.flush();
    if ( !file )
      refuse( "TABLE_FILE", fmt::format( "write to '{}' failed (solver_table_save)", tmp ) );
  }
  if ( std::rename( tmp.c_str(), path.c_str() ) != 0 )
    refuse( "TABLE_FILE", fmt::format( "rename '{}' to '{}' failed (solver_table_save)", tmp, path ) );
}

void write_trace( const trace_record_t& r )
{
  if ( !g_rt.trace )
    return;
  g_rt.trace->write( reinterpret_cast<const char*>( &r ), sizeof( r ) );
  g_rt.trace->flush();
  if ( !*g_rt.trace )
    refuse( "TABLE_FILE", fmt::format( "write to '{}' failed", g_rt.trace_path ) );
  if ( r.kind == 1 )
    ++g_rt.trace_decisions;
  else if ( r.kind == 3 )
    ++g_rt.trace_fight_ends;
}

void fill_update( trace_record_t& r, const update_info_t& u )
{
  if ( !u.applied )
    return;
  r.has_update = 1;
  r.upd_state = u.state;
  r.upd_button = static_cast<std::uint8_t>( u.button );
  r.upd_reward = u.reward;
  r.upd_q_old = u.q_old;
  r.upd_n_old = u.n_old;
  r.upd_q_new = u.q_new;
  r.upd_n_new = u.n_new;
  r.upd_m2_new = u.m2_new;
}

void open_trace( sim_t* sim )
{
  g_rt.trace_path = sim->rl_translog_file_str + ".tbt";
  g_rt.trace = std::make_unique<io::ofstream>();
  g_rt.trace->open( g_rt.trace_path, std::ios::out | std::ios::trunc | std::ios::binary );
  if ( !g_rt.trace->is_open() )
    refuse( "TABLE_NEEDS", fmt::format( "unable to open '{}' for writing", g_rt.trace_path ) );
  unsigned char header[ 40 ] = {};
  std::memcpy( header, "TBT1", 4 );
  const std::uint32_t version = 2, record = sizeof( trace_record_t ), hsize = 40;
  const std::uint64_t clock_value = static_cast<std::uint64_t>( g_clock );
  std::memcpy( header + 4, &version, 4 );
  std::memcpy( header + 8, &record, 4 );
  std::memcpy( header + 12, &hsize, 4 );
  std::memcpy( header + 16, g_table.sha256, 16 );
  std::memcpy( header + 32, &clock_value, 8 );
  g_rt.trace->write( reinterpret_cast<const char*>( header ), sizeof( header ) );
  g_rt.trace->flush();
}

}  // namespace

// ---- public ----------------------------------------------------------------------------------------------------------------------------
std::string& path_storage() { return g_path; }
std::string& scope_storage() { return g_scope; }
std::string& fallback_storage() { return g_fallback; }
std::string& defer_storage() { return g_defer; }
bool&        wait_storage() { return g_wait; }
bool&        trace_storage() { return g_trace; }
std::string& save_storage() { return g_save; }
int&         clock_storage() { return g_clock; }

bool active()
{
  return !g_path.empty();
}

void load_and_validate( sim_t* sim )
{
  if ( g_path.empty() )
  {
    if ( !g_scope.empty() || !g_fallback.empty() || !g_defer.empty() || g_wait || g_trace || !g_save.empty() || g_clock != 0 )
      refuse( "TABLE_NEEDS", "a solver_table_* option is set without solver_table_policy=" );
    return;
  }
  if ( !g_scope.empty() && g_scope != SCOPE_ID )
    refuse( "TABLE_NEEDS", fmt::format( "solver_table_scope='{}' must be '{}' (the only scope this build plays)", g_scope, SCOPE_ID ) );
  if ( !g_fallback.empty() && g_fallback != "stock" && g_fallback != "refuse" )
    refuse( "TABLE_NEEDS", fmt::format( "solver_table_fallback='{}' must be stock or refuse", g_fallback ) );
  if ( !g_defer.empty() && g_defer != "stock" && g_defer != "none" && g_defer != "all" )
    refuse( "TABLE_NEEDS", fmt::format( "solver_table_defer='{}' must be stock, none or all", g_defer ) );
  if ( g_clock < 0 )
    refuse( "TABLE_NEEDS", fmt::format( "solver_table_clock={} must be an integer >= 0", g_clock ) );
  if ( !g_save.empty() && g_save == g_path )
    refuse( "TABLE_NEEDS", "solver_table_save= names the file solver_table_policy= reads: the input table is never overwritten" );
  g_refuse_fallback = g_fallback == "refuse";
  g_defer_stock = g_defer != "none" && g_defer != "all";
  g_defer_all = g_defer == "all";

  if ( g_loaded )
    return;  // setup() ran again in this process (a child sim): the file is read once, the runtime is not reset
  g_table = load_table( g_path );
  g_table.scope = SCOPE_ID;

  if ( g_wait )
    g_table.decided[ g_table.idle_index ] = true;

  g_rt = runtime_t();
  g_rt.Q = g_table.Q;
  g_rt.N = g_table.N;
  g_rt.M2 = g_table.M2;
  g_loaded = true;
  if ( g_trace )
  {
    if ( sim->rl_translog_file_str.empty() )
      refuse( "TABLE_NEEDS", "solver_table_trace=1 needs rl_translog=" );
    open_trace( sim );
  }
}

void on_fight_start( sim_t* )
{
  if ( !g_loaded )
    return;
  g_rt.rows = 0;
  g_rt.pending = pending_t();
  // scope fight: Q, N and M2 start from the immutable file at every fight start (the warm-up fight and the kept fight alike).
  g_rt.Q = g_table.Q;
  g_rt.N = g_table.N;
  g_rt.M2 = g_table.M2;
}

void on_fight_end( sim_t* sim )
{
  if ( !g_loaded )
    return;
  if ( g_rt.pending.valid && g_rt.actor != nullptr )
  {
    const update_info_t u = apply_update( g_rt.pending, g_rt.actor->solver_damage_so_far );
    g_rt.pending = pending_t();
    if ( g_rt.trace )
    {
      trace_record_t r{};
      r.kind = 3;
      r.seq = 0;
      r.iteration = static_cast<std::uint32_t>( rl_translog::fight_index( sim ) );
      r.decision_index = static_cast<std::uint32_t>( g_rt.rows );
      r.fight_seed = static_cast<std::uint64_t>( sim->seed );
      r.state_id = rl_apl_choice::STATE_ID_NONE;
      r.button = 255;
      fill_update( r, u );
      write_trace( r );
    }
  }
  // solver_table_save: the kept fight is the last iteration (the warm-up fight, iteration 0, is discarded by on_fight_start's copy from the file, so the saved table is
  // the incoming file plus this fight's updates and nothing else).
  if ( !g_save.empty() && sim->current_iteration == sim->iterations - 1 )
    save_table( g_save );
}

void close_trace( sim_t* )
{
  if ( !g_rt.trace )
    return;
  trace_record_t r{};
  r.kind = 2;
  r.seq = g_rt.trace_decisions;
  r.iteration = g_rt.trace_fight_ends;
  r.state_id = rl_apl_choice::STATE_ID_NONE;
  r.button = 255;
  write_trace( r );
  g_rt.trace->close();
  g_rt.trace.reset();
}

decision_t decide( sim_t* sim, player_t* p, bool foreground, std::uint64_t seq, const float* obs, const std::uint8_t* mask, const rl_stock_eval::pick_t& stock )
{
  const table_t& t = g_table;
  decision_t     d;
  g_rt.actor = p;

  std::uint64_t sid = 0;
  const bool    in_domain = bucket( t, obs, mask, sid );
  if ( in_domain && sid >= t.n_states )
    refuse( "TABLE_STRATEGY", fmt::format( "the state id {} is outside the {} states of the file", sid, t.n_states ) );

  std::uint8_t legal[ BUTTONS ];
  int          n_legal = 0;
  for ( int b = 0; b < BUTTONS; ++b )
  {
    legal[ b ] = static_cast<std::uint8_t>( mask[ b ] != 0 && t.decided[ b ] );
    n_legal += legal[ b ];
  }
  const int  mapped = stock.mapped_index;
  const bool stock_legal = mapped >= 0 && mapped < BUTTONS && mask[ mapped ] != 0;

  // The plan: read-only, so a no-op leaves the table and the counters untouched. The order is table_certify.py's reference: the observation is bucketed BEFORE the
  // deferral test (an observation outside the state set is NOSTATE whatever the deferral would say), then defer=all, then defer=stock, then the legal set.
  enum class plan_t { stock, stock_idle, nostate_press, nostate_idle, forced_idle, strategy };
  plan_t plan = plan_t::strategy;
  if ( !in_domain )
  {
    if ( g_refuse_fallback )
      refuse( "TABLE_FALLBACK_REFUSED",
              fmt::format( "seq={} t={:.3f}: the observation is outside the state set '{}' (solver_table_fallback=refuse)", seq, sim->current_time().total_seconds(),
                           t.state_set ) );
    plan = stock_legal ? plan_t::nostate_press : plan_t::nostate_idle;
  }
  else if ( g_defer_all )
  {
    // arm A1: every legal stock pick is pressed; no ready mapped leaf is the stock pick "wait"; an illegal stock pick is a forced idle. No draw, no update.
    if ( stock_legal )
      plan = plan_t::stock;
    else if ( mapped < 0 )
      plan = plan_t::stock_idle;
    else
      plan = plan_t::forced_idle;
  }
  else if ( g_defer_stock && stock_legal && !t.decided[ mapped ] )
    plan = plan_t::stock;
  else if ( n_legal == 0 )
    plan = plan_t::forced_idle;

  const bool wants_idle = plan == plan_t::nostate_idle || plan == plan_t::forced_idle || plan == plan_t::stock_idle;
  if ( wants_idle && mask[ t.idle_index ] == 0 )
  {
    if ( !foreground )
    {
      d.noop = true;  // nothing to press at this background boundary: no row, no decision number
      return d;
    }
    refuse( "TABLE_NO_LEGAL", fmt::format( "seq={} t={:.3f}: no decided button, no deferred stock press and no legal idle button", seq, sim->current_time().total_seconds() ) );
  }

  // 261027-fts (D8): decide() changes no table state. The row counter advances at commit() (the prospective index is the counter as it stands: only one
  // decision is ever uncommitted), so a discarded decision burns no index and the next decision draws from the same key.
  const std::uint32_t decision_index = static_cast<std::uint32_t>( g_rt.rows );
  rl_apl_choice::table_info_t& info = d.info;
  info.state_id = in_domain ? static_cast<std::uint32_t>( sid ) : rl_apl_choice::STATE_ID_NONE;
  info.decision_index = decision_index;
  d.q_margin = std::numeric_limits<float>::quiet_NaN();
  d.top_q = std::numeric_limits<float>::quiet_NaN();

  choice_t      choice;
  bool          have_choice = false;
  switch ( plan )
  {
    case plan_t::stock:
      d.idx = mapped;
      info.chooser = rl_apl_choice::CHOOSER_STOCK;
      break;
    case plan_t::stock_idle:
      d.idx = t.idle_index;
      d.idle = true;
      info.chooser = rl_apl_choice::CHOOSER_STOCK;
      break;
    case plan_t::nostate_press:
      d.idx = mapped;
      info.chooser = rl_apl_choice::CHOOSER_NOSTATE;
      break;
    case plan_t::nostate_idle:
      d.idx = t.idle_index;
      d.idle = true;
      info.chooser = rl_apl_choice::CHOOSER_NOSTATE;
      break;
    case plan_t::forced_idle:
      d.idx = t.idle_index;
      d.idle = true;
      info.chooser = rl_apl_choice::CHOOSER_FORCED_IDLE;
      break;
    case plan_t::strategy:
    {
      // 261027-fts (D8): the pending strategy decision's reward is folded in at commit() (the damage since IT: a deferred press, a forced idle or a NOSTATE
      // row in between neither updated nor moved it), AFTER this pick: the strategy chooses on the row as the previous commit left it.
      const std::size_t row = static_cast<std::size_t>( sid ) * BUTTONS;
      double            Qd[ BUTTONS ];
      std::int64_t      Nd[ BUTTONS ];
      double            sd[ BUTTONS ];
      for ( int b = 0; b < BUTTONS; ++b )
      {
        Qd[ b ] = static_cast<double>( g_rt.Q[ row + static_cast<std::size_t>( b ) ] );
        Nd[ b ] = static_cast<std::int64_t>( g_rt.N[ row + static_cast<std::size_t>( b ) ] );
        // tbp.table_reward_sd: sqrt(M2 / N) where N >= 2, 0.0 elsewhere.
        sd[ b ] = Nd[ b ] >= 2 ? std::sqrt( g_rt.M2[ row + static_cast<std::size_t>( b ) ] / static_cast<double>( Nd[ b ] ) ) : 0.0;
      }
      choice = run_strategy( t, Qd, Nd, sd, legal, static_cast<std::uint64_t>( sim->seed ), decision_index, g_clock );
      have_choice = true;
      d.idx = choice.action;
      d.idle = d.idx == t.idle_index;
      info.chooser = d.idle ? rl_apl_choice::CHOOSER_STRATEGY_IDLE : rl_apl_choice::CHOOSER_STRATEGY;
      info.strategy = static_cast<std::uint8_t>( t.strategy );
      info.probability = choice.has_probs ? choice.probs[ d.idx ] : std::numeric_limits<double>::quiet_NaN();
      info.q_chosen = g_rt.Q[ row + static_cast<std::size_t>( d.idx ) ];
      info.n_chosen = g_rt.N[ row + static_cast<std::size_t>( d.idx ) ];
      // The best and second best legal Q of the row (floats, as the net arm's margin is), NaN with fewer than two / none legal.
      float best = -std::numeric_limits<float>::infinity(), second = -std::numeric_limits<float>::infinity();
      for ( int b = 0; b < BUTTONS; ++b )
      {
        if ( !legal[ b ] )
          continue;
        const float q = g_rt.Q[ row + static_cast<std::size_t>( b ) ];
        if ( q > best )
        {
          second = best;
          best = q;
        }
        else if ( q > second )
          second = q;
      }
      d.top_q = best;
      d.q_margin = n_legal >= 2 ? best - second : std::numeric_limits<float>::quiet_NaN();
      d.staged.strategy = true;
      d.staged.state = static_cast<std::uint32_t>( sid );
      d.staged.button = d.idx;
      break;
    }
  }
  d.staged.valid = true;
  d.staged.decision_index = decision_index;

  if ( g_rt.trace )
  {
    trace_record_t r{};
    r.kind = 1;
    r.seq = static_cast<std::uint32_t>( seq );
    r.iteration = static_cast<std::uint32_t>( rl_translog::fight_index( sim ) );
    r.decision_index = decision_index;
    r.fight_seed = static_cast<std::uint64_t>( sim->seed );
    r.state_id = info.state_id;
    std::uint32_t packed = 0;
    for ( int b = 0; b < BUTTONS; ++b )
      if ( mask[ b ] )
        packed |= 1u << b;
    r.mask = packed;
    r.chooser = info.chooser;
    r.strategy = info.strategy;
    r.button = static_cast<std::uint8_t>( d.idx );
    r.n_draws = static_cast<std::uint32_t>( choice.draws );
    for ( int i = 0; i < MAX_VALUES; ++i )
      r.values[ i ] = i < choice.n_values ? choice.values[ i ] : 0.0;
    if ( in_domain )
    {
      const std::size_t row = static_cast<std::size_t>( sid ) * BUTTONS;
      for ( int b = 0; b < BUTTONS; ++b )
      {
        r.q[ b ] = g_rt.Q[ row + static_cast<std::size_t>( b ) ];
        r.n[ b ] = g_rt.N[ row + static_cast<std::size_t>( b ) ];
        r.m2[ b ] = g_rt.M2[ row + static_cast<std::size_t>( b ) ];
      }
    }
    for ( int b = 0; b < BUTTONS; ++b )
      r.prob[ b ] = have_choice && choice.has_probs ? choice.probs[ b ] : 0.0;
    // The update block stays empty here (has_update = 0): commit() fills it with the fold applied at the commit and writes the record.
    static_assert( sizeof( d.staged.trace ) == sizeof( r ), "the staged .tbt record is a trace_record_t" );
    std::memcpy( d.staged.trace, &r, sizeof( r ) );
    d.staged.has_trace = true;
  }
  return d;
}

void commit( sim_t* sim, player_t* p, std::uint64_t seq, const decision_t& d )
{
  if ( !g_loaded || !d.staged.valid )
    return;
  (void)sim;
  if ( d.staged.decision_index != static_cast<std::uint32_t>( g_rt.rows ) )
    refuse( "TABLE_STRATEGY", fmt::format( "decision-index drift at commit: decided as {} but the row counter is {} (seq={})", d.staged.decision_index, g_rt.rows, seq ) );
  ++g_rt.rows;
  update_info_t upd;
  if ( d.staged.strategy )
  {
    const double now_damage = p->solver_damage_so_far;
    if ( g_rt.pending.valid )
    {
      upd = apply_update( g_rt.pending, now_damage );
      g_rt.pending = pending_t();
    }
    g_rt.pending.valid = true;
    g_rt.pending.state = d.staged.state;
    g_rt.pending.button = d.staged.button;
    g_rt.pending.damage = now_damage;
  }
  if ( g_rt.trace && d.staged.has_trace )
  {
    trace_record_t r{};
    std::memcpy( &r, d.staged.trace, sizeof( r ) );
    r.seq = static_cast<std::uint32_t>( seq );
    fill_update( r, upd );
    write_trace( r );
  }
}

// ---- (6) the sim-less probe --------------------------------------------------------------------------------------------------------------
// `simc rl_table_probe=<mode> key=value ...`: run from main() before any sim_t exists; reads binary files, writes one binary file, prints one summary
// line on stdout. Every number crosses as raw little-endian bits (no text round trip). The Python side that drives it and compares is
// scripts/rl/table_player_gates.py in the repository (quick 261025-bk4, lane b2).
//   mode buckets   tbp=<tbp1> obs=<f32 N x 444> mask=<u8 N x 22> out=<i64 N>
//                  out[i] = the state id of row i, or -1 when the row is outside the state set's domain
//   mode picks     tbp=<tbp1> obs= mask= keys=<u64 pairs: fight seed, decision index, N of them> out=<24-byte records>
//                  the strategy of the file on the file's own Q, N, M2 row of each row's state (initial table, no updates); legal = mask AND decided
//   mode rows      tbp=<tbp1> rows=<392-byte records> out=<24-byte records> sd=<0|1>
//                  rows: f32 Q[22] @0, i32 N[22] @88, f64 M2[22] @176, u8 legal[24] @352, u64 seed @376, u64 decision index @384; the strategy and its
//                  parameters come from the file's header; sd=1 passes the per-cell reward sd sqrt(M2/N) (cells with N >= 2), sd=0 the scalar alpha
//   picks / rows record: i64 state id (-1 refused; 0 in rows mode) @0, i32 button (-1 none) @8, u32 raw draws @12, f64 probability (NaN none) @16
//   mode rng       in=<u64 pairs: seed, index> out=<144-byte records>
//                  per pair: u64 raw[8] (the first eight draws of the stream), f64 uniform[4], f64 normal[3] (fresh streams), i64 index(22), index(7), index(2^20)
//   mode libm      in=<f64 x N> out=<5 u64 per input: bits of exp(x), log(x), cos(x), sqrt(x), pow(10, x)>
//   mode epsilons  n_episodes= init= min= ratio= out=<f64 n_episodes>: the exp_dec_epsilon_at values for e = 0..n_episodes-1
namespace
{

std::string arg_of( const std::vector<std::string>& args, const std::string& key, bool required = true )
{
  const std::string prefix = key + "=";
  for ( const std::string& a : args )
    if ( a.compare( 0, prefix.size(), prefix ) == 0 )
      return a.substr( prefix.size() );
  if ( required )
    refuse( "TABLE_PROBE", fmt::format( "missing argument {}=", key ) );
  return std::string();
}

void write_file( const std::string& path, const void* data, std::size_t n )
{
  std::ofstream out( path, std::ios::binary | std::ios::trunc );
  if ( !out.is_open() )
    refuse( "TABLE_PROBE", fmt::format( "cannot open '{}' for writing", path ) );
  out.write( static_cast<const char*>( data ), static_cast<std::streamsize>( n ) );
  if ( !out )
    refuse( "TABLE_PROBE", fmt::format( "write to '{}' failed", path ) );
}

struct pick_out_t
{
  std::int64_t state;
  std::int32_t button;
  std::uint32_t draws;
  double       probability;
};
static_assert( sizeof( pick_out_t ) == 24, "pick record" );

}  // namespace

int run_probe( const std::vector<std::string>& args )
{
  try
  {
    const std::string mode = arg_of( args, "rl_table_probe" );
    const std::string out_path = arg_of( args, "out" );
    if ( mode == "libm" )
    {
      const std::vector<unsigned char> in = read_file( arg_of( args, "in" ) );
      const std::size_t n = in.size() / 8;
      std::vector<std::uint64_t> out( n * 5 );
      for ( std::size_t i = 0; i < n; ++i )
      {
        const double x = read_le<double>( in.data() + i * 8 );
        const double r[ 5 ] = { std::exp( x ), std::log( x ), std::cos( x ), std::sqrt( x ), std::pow( 10.0, x ) };
        for ( int k = 0; k < 5; ++k )
          std::memcpy( &out[ i * 5 + static_cast<std::size_t>( k ) ], &r[ k ], 8 );
      }
      write_file( out_path, out.data(), out.size() * 8 );
      fmt::print( "rl_table_probe libm: {} inputs\n", n );
      return 0;
    }
    if ( mode == "rng" )
    {
      const std::vector<unsigned char> in = read_file( arg_of( args, "in" ) );
      const std::size_t n = in.size() / 16;
      std::vector<unsigned char> out( n * 144 );
      for ( std::size_t i = 0; i < n; ++i )
      {
        const std::uint64_t seed = read_le<std::uint64_t>( in.data() + i * 16 ), index = read_le<std::uint64_t>( in.data() + i * 16 + 8 );
        unsigned char*      o = out.data() + i * 144;
        std::uint64_t raw[ 8 ];
        double        uni[ 4 ], nor[ 3 ];
        std::int64_t  idx[ 3 ];
        {
          stream_t s( seed, index );
          for ( auto& r : raw )
            r = s.next();
        }
        {
          stream_t s( seed, index );
          for ( auto& u : uni )
            u = s.uniform_raw();
        }
        {
          stream_t s( seed, index );
          for ( auto& z : nor )
            z = s.normal_raw();
        }
        {
          stream_t s( seed, index );
          idx[ 0 ] = s.index( 22 );
          idx[ 1 ] = s.index( 7 );
          idx[ 2 ] = s.index( MAX_INDEX_N );
        }
        std::memcpy( o, raw, 64 );
        std::memcpy( o + 64, uni, 32 );
        std::memcpy( o + 96, nor, 24 );
        std::memcpy( o + 120, idx, 24 );
      }
      write_file( out_path, out.data(), out.size() );
      fmt::print( "rl_table_probe rng: {} pairs\n", n );
      return 0;
    }
    if ( mode == "epsilons" )
    {
      const std::int64_t n = std::stoll( arg_of( args, "n_episodes" ) );
      const double init = std::stod( arg_of( args, "init" ) ), mn = std::stod( arg_of( args, "min" ) ), ratio = std::stod( arg_of( args, "ratio" ) );
      std::vector<double> out( static_cast<std::size_t>( n ) );
      for ( std::int64_t e = 0; e < n; ++e )
        out[ static_cast<std::size_t>( e ) ] = exp_dec_epsilon_at( e, n, init, mn, ratio );
      write_file( out_path, out.data(), out.size() * 8 );
      fmt::print( "rl_table_probe epsilons: {} values\n", n );
      return 0;
    }

    const table_t t = load_table( arg_of( args, "tbp" ) );
    if ( mode == "buckets" || mode == "picks" )
    {
      const std::vector<unsigned char> obs = read_file( arg_of( args, "obs" ) );
      const std::vector<unsigned char> mk = read_file( arg_of( args, "mask" ) );
      const std::size_t row_bytes = RL_OBS_DIM * 4;
      if ( obs.size() % row_bytes != 0 )
        refuse( "TABLE_PROBE", "the obs file is not a whole number of 444-float rows" );
      const std::size_t n = obs.size() / row_bytes;
      if ( mk.size() != n * RL_ACTION_DIM )
        refuse( "TABLE_PROBE", "the mask file does not hold 22 bytes per obs row" );
      std::vector<float> row( RL_OBS_DIM );
      std::vector<std::uint64_t> keys;
      if ( mode == "picks" )
      {
        const std::vector<unsigned char> kb = read_file( arg_of( args, "keys" ) );
        if ( kb.size() != n * 16 )
          refuse( "TABLE_PROBE", "the keys file does not hold 16 bytes per obs row" );
        keys.resize( n * 2 );
        std::memcpy( keys.data(), kb.data(), kb.size() );
      }
      std::vector<std::int64_t> ids( n );
      std::vector<pick_out_t>   picks( mode == "picks" ? n : 0 );
      std::size_t               refused = 0;
      for ( std::size_t i = 0; i < n; ++i )
      {
        std::memcpy( row.data(), obs.data() + i * row_bytes, row_bytes );
        const std::uint8_t* m = mk.data() + i * RL_ACTION_DIM;
        std::uint64_t sid = 0;
        const bool    ok = bucket( t, row.data(), m, sid );
        ids[ i ] = ok ? static_cast<std::int64_t>( sid ) : -1;
        refused += ok ? 0 : 1;
        if ( mode != "picks" )
          continue;
        pick_out_t& o = picks[ i ];
        o = pick_out_t{ ids[ i ], -1, 0, std::numeric_limits<double>::quiet_NaN() };
        if ( !ok )
          continue;
        std::uint8_t legal[ BUTTONS ];
        int          n_legal = 0;
        for ( int b = 0; b < BUTTONS; ++b )
        {
          legal[ b ] = static_cast<std::uint8_t>( m[ b ] != 0 && t.decided[ b ] );
          n_legal += legal[ b ];
        }
        if ( n_legal == 0 )
          continue;
        double       Qd[ BUTTONS ], sd[ BUTTONS ];
        std::int64_t Nd[ BUTTONS ];
        for ( int b = 0; b < BUTTONS; ++b )
        {
          const std::size_t c = static_cast<std::size_t>( sid ) * BUTTONS + static_cast<std::size_t>( b );
          Qd[ b ] = static_cast<double>( t.Q[ c ] );
          Nd[ b ] = t.N[ c ];
          sd[ b ] = Nd[ b ] >= 2 ? std::sqrt( t.M2[ c ] / static_cast<double>( Nd[ b ] ) ) : 0.0;
        }
        const choice_t c = run_strategy( t, Qd, Nd, sd, legal, keys[ i * 2 ], keys[ i * 2 + 1 ], static_cast<std::int64_t>( keys[ i * 2 + 1 ] ) );
        o.button = c.action;
        o.draws = static_cast<std::uint32_t>( c.draws );
        o.probability = c.has_probs ? c.probs[ c.action ] : std::numeric_limits<double>::quiet_NaN();
      }
      if ( mode == "buckets" )
        write_file( out_path, ids.data(), ids.size() * 8 );
      else
        write_file( out_path, picks.data(), picks.size() * sizeof( pick_out_t ) );
      fmt::print( "rl_table_probe {}: {} rows, {} outside the state set\n", mode, n, refused );
      return 0;
    }
    if ( mode == "rows" )
    {
      const std::vector<unsigned char> in = read_file( arg_of( args, "rows" ) );
      constexpr std::size_t REC = 392;
      if ( in.size() % REC != 0 )
        refuse( "TABLE_PROBE", "the rows file is not a whole number of 392-byte records" );
      const std::size_t n = in.size() / REC;
      const bool        use_sd = arg_of( args, "sd" ) == "1";
      std::vector<pick_out_t> picks( n );
      for ( std::size_t i = 0; i < n; ++i )
      {
        const unsigned char* r = in.data() + i * REC;
        double               Qd[ BUTTONS ], sd[ BUTTONS ];
        std::int64_t         Nd[ BUTTONS ];
        std::uint8_t         legal[ BUTTONS ];
        for ( int b = 0; b < BUTTONS; ++b )
        {
          Qd[ b ] = static_cast<double>( read_le<float>( r + 4 * b ) );
          Nd[ b ] = read_le<std::int32_t>( r + 88 + 4 * b );
          const double m2 = read_le<double>( r + 176 + 8 * b );
          sd[ b ] = Nd[ b ] >= 2 ? std::sqrt( m2 / static_cast<double>( Nd[ b ] ) ) : 0.0;
          legal[ b ] = r[ 352 + b ];
        }
        pick_out_t& o = picks[ i ];
        o = pick_out_t{ 0, -1, 0, std::numeric_limits<double>::quiet_NaN() };
        const choice_t c = run_strategy( t, Qd, Nd, use_sd ? sd : nullptr, legal, read_le<std::uint64_t>( r + 376 ), read_le<std::uint64_t>( r + 384 ),
                                         static_cast<std::int64_t>( read_le<std::uint64_t>( r + 384 ) ) );
        o.button = c.action;
        o.draws = static_cast<std::uint32_t>( c.draws );
        o.probability = c.has_probs ? c.probs[ c.action ] : std::numeric_limits<double>::quiet_NaN();
      }
      write_file( out_path, picks.data(), picks.size() * sizeof( pick_out_t ) );
      fmt::print( "rl_table_probe rows: {} rows\n", n );
      return 0;
    }
    refuse( "TABLE_PROBE", fmt::format( "unknown mode '{}'", mode ) );
  }
  catch ( const std::exception& e )
  {
    fmt::print( stderr, "Error: {}\n", e.what() );
    return 1;
  }
}

}  // namespace rl_table_policy
