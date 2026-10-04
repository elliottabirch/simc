// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// 261003-s1c plan 03: the buff-applier credit rule (D-05u) -- see rl_buff_credit.hpp for the design statement. The arithmetic below is a
// line-by-line port of `ledger_reference.py` (tstl-sylvanas, .planning/quick/261003-s1c-ledger-credit-in-training/scripts/): the function names
// in the comments name the Python functions they equal. Where the Python reads a dict in insertion order this code keeps insertion order, so
// the two agree to the last bits wherever float addition order matters, and to 1e-9 everywhere else.
//
// NO SPELL, BUFF OR ACTION NAME appears in this file (scripts/no_spell_names.py checks every string literal of it).

#include "sim/rl_buff_credit.hpp"

#include "sim/rl_buff_ledger.hpp"
#include "sim/sim.hpp"
#include "util/io.hpp"
#include "util/util.hpp"

#include "fmt/format.h"
#include "rapidjson/document.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>

namespace rl_buff_credit
{
namespace
{
constexpr double REL_TOL = 1e-9;
constexpr double EPS     = 1e-12;
constexpr int EXACT_SHAPLEY_MAX_PLAYERS = 3;
constexpr int CLS_CAST = 0, CLS_PROC_OF_CAST = 1, CLS_DOT_TICK = 2, CLS_PROC_OF_DOT = 3, CLS_ORPHAN = 6;
constexpr std::int64_t PRESS_CARRY_BASE = 1000;
constexpr std::int64_t PRESS_CARRY_MAX  = 31767;
constexpr std::int64_t UNKNOWN_KEY      = INT64_MIN;  // a decision lookup that failed (Python: None)

// ---- .bcr layout (BCR-FORMAT.md; translog.py _BCR_*_STRUCT) ----
constexpr char BCR_MAGIC[ 4 ]            = { 'R', 'L', 'B', 'C' };
constexpr std::uint32_t BCR_VERSION      = 1u;
constexpr std::uint32_t BCR_RECORD_SIZE  = 80u;
constexpr std::uint32_t BCR_HEADER_SIZE  = 64u;
constexpr std::uint32_t BCR_KIND_FIGHT = 1u, BCR_KIND_DECISION = 2u, BCR_KIND_FOOTER = 3u;
constexpr std::uint32_t BCR_FLAG_COLLECTED = 1u, BCR_FLAG_FUNNEL = 2u;
constexpr std::uint8_t BCR_RULE_D05U = 1;

struct alignas( 8 ) bcr_header
{
  char magic[ 4 ];
  std::uint32_t format_version;
  std::uint32_t record_size;
  std::uint32_t header_size;
  std::uint8_t sw;
  std::uint8_t rule;
  std::uint8_t zero6[ 6 ];
  std::uint8_t sha[ 32 ];
  std::uint8_t zero8[ 8 ];
};
static_assert( sizeof( bcr_header ) == BCR_HEADER_SIZE, "bcr_header must be 64 bytes" );
static_assert( alignof( bcr_header ) == 8, "bcr_header must be 8-aligned" );

struct alignas( 8 ) bcr_fight
{
  std::uint32_t kind;
  std::uint32_t iteration;
  std::uint32_t n_decisions;
  std::uint32_t flags;
  double conservation_worst_all;
  double conservation_worst_chosen;
  double abs_total_all;
  double abs_total_chosen;
  std::uint8_t zero32[ 32 ];
};
static_assert( sizeof( bcr_fight ) == BCR_RECORD_SIZE, "bcr_fight must be 80 bytes" );
static_assert( alignof( bcr_fight ) == 8, "bcr_fight must be 8-aligned" );

struct alignas( 8 ) bcr_decision
{
  std::uint32_t kind;
  std::uint32_t iteration;
  std::uint32_t seq;
  std::uint32_t zero;
  double v[ 8 ];  // all: old_own, old_interval, moved_full, moved_nobody; chosen: the same four
};
static_assert( sizeof( bcr_decision ) == BCR_RECORD_SIZE, "bcr_decision must be 80 bytes" );
static_assert( alignof( bcr_decision ) == 8, "bcr_decision must be 8-aligned" );

struct alignas( 8 ) bcr_footer
{
  std::uint32_t kind;
  std::uint32_t n_fights;
  std::uint32_t required_zero[ 16 ];  // the ledger's twelve, then conservation_fail, prefix_moved, unknown_press, seq_without_decision
  std::uint32_t n_items;
  std::uint32_t n_split;
};
static_assert( sizeof( bcr_footer ) == BCR_RECORD_SIZE, "bcr_footer must be 80 bytes" );
static_assert( alignof( bcr_footer ) == 8, "bcr_footer must be 8-aligned" );

// ---- sha256 (FIPS 180-4), for the verdict table's identity ----
struct sha256_t
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

std::string hex_of( const std::uint8_t* p, std::size_t n )
{
  static const char* d = "0123456789abcdef";
  std::string s;
  for ( std::size_t i = 0; i < n; ++i )
  {
    s += d[ p[ i ] >> 4 ];
    s += d[ p[ i ] & 15 ];
  }
  return s;
}

// ---- failure ----
[[noreturn]] void fail( const std::string& msg )
{
  throw sc_runtime_error( "rl_buff_credit: " + msg );
}

// ---- the model: Applier, Player, Group (ledger_reference.py) ----
bool is_press_applier( const applier_t& a )
{
  return ( a.cls == CLS_PROC_OF_CAST || a.cls == CLS_PROC_OF_DOT ) && a.press >= 0;
}

// The four split channels of a player (ledger_reference.Player.channel) plus the two bookkeeping channels of an entry: the dealer's remainder and the
// credit a refund moved (`apply_refunds`: channel "refund"). Every split channel is on: this module plays the whole game (Python `channels=ALL`).
enum channel_t : std::uint8_t { CH_HIDE = 0, CH_STAT = 1, CH_GATE = 2, CH_SWING = 3, CH_DEALER = 4, CH_REFUND = 5 };

struct bc_player_t
{
  std::pair<std::string, std::string> key;
  std::string kind;
  std::vector<applier_t> appliers;
  bool cov_rule = false;
  bool gate     = false;
  bool swing    = false;

  bool press_applied() const
  {
    for ( const auto& a : appliers )
      if ( is_press_applier( a ) )
        return true;
    return false;
  }
  channel_t channel() const
  {
    if ( gate )
      return CH_GATE;
    if ( swing || kind == "speed" )
      return CH_SWING;
    if ( kind == "stat" )
      return CH_STAT;
    return CH_HIDE;
  }
};

struct bc_group_t
{
  bool swing    = false;                                  // Python: kind == "swing" (speed factors of a swing launch; no passes)
  std::vector<std::pair<int, double>> speed;              // swing: (player id, speed factor) in the order the chain met them
  bool per_mode = false;                                  // Python: mode == "per"
  std::vector<std::pair<std::uint32_t, int>> cands;       // (bit index, player id) of the press-applied candidates
  std::unordered_map<std::uint64_t, pass_t> passes;       // hidden-set mask -> pass record (a later record with the same mask wins)
  bool has_ref = false;
  pass_t ref{};
  bool bad = false;
  std::uint64_t pe_mask = 0;
  std::vector<std::pair<std::uint32_t, std::pair<double, double>>> crit_only;  // insertion order
};

struct players_t
{
  std::vector<bc_player_t> list;

  int find( const std::pair<std::string, std::string>& key ) const
  {
    for ( std::size_t i = 0; i < list.size(); ++i )
      if ( list[ i ].key == key )
        return static_cast<int>( i );
    return -1;
  }
  // _merge_player
  int merge( bc_player_t&& nw )
  {
    const int at = find( nw.key );
    if ( at < 0 )
    {
      list.push_back( std::move( nw ) );
      if ( list.size() > 64 )
        fail( "a hit has more than 64 players" );
      return static_cast<int>( list.size() - 1 );
    }
    bc_player_t& old = list[ at ];
    if ( !old.press_applied() && nw.press_applied() )
    {
      old.appliers = nw.appliers;
      old.cov_rule = nw.cov_rule;
      old.kind     = nw.kind;
    }
    old.gate  = old.gate || nw.gate;
    old.swing = old.swing || nw.swing;
    return at;
  }
};

// _build_pass_group (self and parent groups)
bc_group_t build_pass_group( const passset_t& ps, players_t& players, bool per_mode )
{
  bc_group_t g;
  g.per_mode = per_mode;
  for ( const auto& p : ps.passes )
  {
    if ( p.viol )
      g.bad = true;
    if ( p.kind == PASS_REF && !g.has_ref )
    {
      g.ref     = p;
      g.has_ref = true;
    }
    else if ( p.kind == PASS_HIDE )
      g.passes[ p.hid ] = p;
  }
  for ( const auto& cand : ps.cands )
  {
    const std::uint32_t bit = cand.bit;
    const auto single       = g.passes.find( std::uint64_t( 1 ) << bit );
    if ( g.has_ref && single != g.passes.end() )
    {
      const pass_t& s = single->second;
      if ( per_mode ? ( s.per != g.ref.per ) : ( s.pre != g.ref.pre ) )
        g.pe_mask |= std::uint64_t( 1 ) << bit;
      else if ( !per_mode && ( s.cc != g.ref.cc || s.cb != g.ref.cb ) )
      {
        bool found = false;
        for ( auto& co : g.crit_only )
          if ( co.first == bit )
          {
            co.second = { s.cc - g.ref.cc, s.cb - g.ref.cb };
            found     = true;
          }
        if ( !found )
          g.crit_only.push_back( { bit, { s.cc - g.ref.cc, s.cb - g.ref.cb } } );
      }
    }
    // _cand_player: a candidate that is not press-applied is not a player and is never hidden
    bc_player_t pl;
    pl.key      = { cand.name, cand.owner };
    pl.kind     = cand.kind;
    pl.appliers = cand.app;
    pl.cov_rule = cand.cov;
    if ( !pl.press_applied() )
      continue;
    g.cands.push_back( { bit, players.merge( std::move( pl ) ) } );
  }
  return g;
}

struct pair_t
{
  double real, exp;
};

std::pair<double, double> pass_factors( double pre, double cc, double cb, bool crit )
{
  return { crit ? pre * ( 1.0 + cb ) : pre, pre * ( 1.0 + cc * cb ) };
}

// group_factor: the ratio (realised, expected) of the amount with the players in `hidden` (a mask of player ids) hidden to the amount with
// nothing hidden; false when a needed pass is missing.
bool group_factor( const bc_group_t& g, std::uint64_t hidden, bool crit, pair_t& out )
{
  if ( g.bad )
    return false;
  if ( g.swing )
  {
    double f = 1.0;
    for ( const auto& sp : g.speed )
      if ( ( hidden >> sp.first ) & 1u )
        f /= sp.second;
    out = { f, f };
    return true;
  }
  std::uint64_t mask = 0;
  for ( const auto& c : g.cands )
    if ( ( hidden >> c.second ) & 1u )
      mask |= std::uint64_t( 1 ) << c.first;
  if ( mask == 0 )
  {
    out = { 1.0, 1.0 };
    return true;
  }
  if ( !g.has_ref )
    return false;
  const std::uint64_t pe = mask & g.pe_mask;
  double dcc = 0.0, dcb = 0.0;
  if ( !g.per_mode )
    for ( const auto& co : g.crit_only )
      if ( ( mask >> co.first ) & 1u )
      {
        dcc += co.second.first;
        dcb += co.second.second;
      }
  if ( pe == 0 && dcc == 0.0 && dcb == 0.0 )
  {
    out = { 1.0, 1.0 };
    return true;
  }
  const pass_t* p = &g.ref;
  if ( pe != 0 )
  {
    const auto it = g.passes.find( pe );
    if ( it == g.passes.end() )
      return false;
    p = &it->second;
  }
  if ( g.per_mode )
  {
    if ( g.ref.per <= 0.0 )
      return false;
    const double ratio = p->per / g.ref.per;
    out                = { ratio, ratio };
    return true;
  }
  const auto rf = pass_factors( g.ref.pre, g.ref.cc, g.ref.cb, crit );
  const auto pf = pass_factors( p->pre, p->cc + dcc, p->cb + dcb, crit );
  if ( rf.first <= 0.0 || rf.second <= 0.0 )
    return false;
  out = { pf.first / rf.first, pf.second / rf.second };
  return true;
}

// _mark_effective: false when a needed single-hide pass or the reference is missing.
bool mark_effective( const bc_group_t& g, bool crit, std::uint64_t& effective, std::uint64_t skip )
{
  for ( const auto& c : g.cands )
  {
    if ( ( skip >> c.second ) & 1u )
      continue;
    pair_t f;
    if ( !group_factor( g, std::uint64_t( 1 ) << c.second, crit, f ) )
      return false;
    if ( std::fabs( f.real - 1.0 ) > EPS || std::fabs( f.exp - 1.0 ) > EPS )
      effective |= std::uint64_t( 1 ) << c.second;
  }
  return true;
}

// applier_split: press -> fraction of the player's share (ordered by press), and the dealer's fraction (unused by the caller).
struct frac_t
{
  std::int64_t press;
  double f;
};

void applier_split( const bc_player_t& pl, std::vector<frac_t>& out )
{
  out.clear();
  const auto& apps = pl.appliers;
  std::vector<std::pair<std::int64_t, double>> press_w;  // insertion order, like a Python dict
  auto add_w = [ & ]( std::int64_t press, double w ) {
    for ( auto& pw : press_w )
      if ( pw.first == press )
      {
        pw.second += w;
        return;
      }
    press_w.push_back( { press, w } );
  };
  double other_w = 0.0;
  if ( pl.cov_rule )
  {
    std::vector<const applier_t*> chosen;
    for ( const auto& a : apps )
      if ( a.covering )
        chosen.push_back( &a );
    if ( chosen.empty() )
      for ( const auto& a : apps )
        chosen.push_back( &a );
    std::set<std::int64_t> seen;
    for ( const applier_t* a : chosen )
    {
      if ( is_press_applier( *a ) )
      {
        if ( seen.insert( a->press ).second )
          add_w( a->press, 1.0 );
      }
      else
        other_w += 1.0;
    }
  }
  else
  {
    for ( const auto& a : apps )
    {
      if ( is_press_applier( a ) )
        add_w( a.press, a.stacks );
      else
        other_w += a.stacks;
    }
  }
  double total = 0.0;
  for ( const auto& pw : press_w )
    total += pw.second;
  total += other_w;
  if ( total <= 0.0 )
  {
    const std::size_t n = apps.size();
    if ( n == 0 )
      return;
    press_w.clear();
    for ( const auto& a : apps )
      if ( is_press_applier( a ) )
        add_w( a.press, 1.0 );
    total = static_cast<double>( n );
  }
  for ( const auto& pw : press_w )
    out.push_back( { pw.first, pw.second / total } );
  std::sort( out.begin(), out.end(), []( const frac_t& a, const frac_t& b ) { return a.press < b.press; } );
}

// v_of_sets: v(S) = (realised, expected) with only the players of S present, for each subset (a mask over the sorted chosen players).
// `ids` maps the sorted chosen index to the player id. False when any needed pass is missing.
bool v_of_sets( double ra, double exp, const std::vector<bc_group_t>& groups, const std::vector<int>& ids, std::uint64_t gate_mask_idx, bool crit,
                const std::vector<std::uint64_t>& subsets, std::map<std::uint64_t, pair_t>& out )
{
  std::uint64_t everyone_ids = 0;
  for ( int id : ids )
    everyone_ids |= std::uint64_t( 1 ) << id;
  for ( std::uint64_t s : subsets )
  {
    if ( ( gate_mask_idx & ~s ) != 0 )
    {
      out[ s ] = { 0.0, 0.0 };
      continue;
    }
    std::uint64_t s_ids = 0;
    for ( std::size_t i = 0; i < ids.size(); ++i )
      if ( ( s >> i ) & 1u )
        s_ids |= std::uint64_t( 1 ) << ids[ i ];
    const std::uint64_t hidden = everyone_ids & ~s_ids;
    double fr = 1.0, fe = 1.0;
    for ( const auto& g : groups )
    {
      pair_t f;
      if ( !group_factor( g, hidden, crit, f ) )
        return false;
      fr *= f.real;
      fe *= f.exp;
    }
    out[ s ] = { ra * fr, exp * fe };
  }
  return true;
}

double factorial( int n )
{
  double r = 1.0;
  for ( int i = 2; i <= n; ++i )
    r *= i;
  return r;
}

// shares: the shares of v(B) - v(empty) among the n players for one side (0 realised, 1 expected).
void shares( const std::map<std::uint64_t, pair_t>& vals, int n, int side, std::vector<double>& out )
{
  out.assign( n, 0.0 );
  const std::uint64_t full = n >= 64 ? ~std::uint64_t( 0 ) : ( ( std::uint64_t( 1 ) << n ) - 1 );
  auto val = [ & ]( std::uint64_t s ) {
    const pair_t& p = vals.at( s );
    return side == 0 ? p.real : p.exp;
  };
  if ( n <= EXACT_SHAPLEY_MAX_PLAYERS )
  {
    // shapley: exact, by subsets of the other players in increasing size
    const double nf = factorial( n );
    for ( int p = 0; p < n; ++p )
    {
      double total = 0.0;
      const std::uint64_t others = full & ~( std::uint64_t( 1 ) << p );
      for ( int r = 0; r <= n - 1; ++r )
      {
        const double w = factorial( r ) * factorial( n - r - 1 ) / nf;
        for ( std::uint64_t s = others;; s = ( s - 1 ) & others )  // every subset of `others`
        {
          if ( __builtin_popcountll( s ) == r )
            total += w * ( val( s | ( std::uint64_t( 1 ) << p ) ) - val( s ) );
          if ( s == 0 )
            break;
        }
      }
      out[ p ] = total;
    }
    return;
  }
  // proportional_shares
  const double v_full = val( full ), v_empty = val( 0 );
  const double joint = v_full - v_empty;
  std::vector<double> removal( n );
  double total = 0.0;
  for ( int p = 0; p < n; ++p )
  {
    removal[ p ] = v_full - val( full & ~( std::uint64_t( 1 ) << p ) );
    total += removal[ p ];
  }
  for ( int p = 0; p < n; ++p )
    out[ p ] = total == 0.0 ? joint / n : joint * removal[ p ] / total;
}

std::vector<std::uint64_t> subsets_needed( int n )
{
  std::vector<std::uint64_t> out;
  const std::uint64_t full = ( std::uint64_t( 1 ) << n ) - 1;
  if ( n <= EXACT_SHAPLEY_MAX_PLAYERS )
  {
    for ( std::uint64_t s = 0; s <= full; ++s )
      out.push_back( s );
    return out;
  }
  out.push_back( full );
  out.push_back( 0 );
  for ( int p = 0; p < n; ++p )
    out.push_back( full & ~( std::uint64_t( 1 ) << p ) );
  return out;
}

// ---- the fight ----
struct entry_t
{
  char holder;         // 'P' a press (id), 'S' a decision seq (id), 'W' the item's window (the dealer's remainder of a background or orphan hit)
  std::int64_t id;
  channel_t channel;
  double real, exp;    // `exp` (the expectation column) never reaches a `.bcr`; the refund sweep's "anything to move" test reads it, as the reference's does
};

struct item_t
{
  double ra   = 0.0;
  bool ch     = false;
  bool nobody = false;
  bool own    = false;
  bool split  = false;  // a split was computed (Python: reason is None)
  std::int64_t old_press = -1;
  std::int64_t seq = 0, win = 0;
  std::vector<entry_t> entries;
};

struct verdicts_t
{
  std::string site_key = "cls";
  std::set<std::tuple<std::string, std::string, std::string, std::string>> gates;  // (parent action, frame key, child action, buff)
  std::set<std::string> gate_buffs;                                                  // the buffs named by any gate site (a read of another buff can never be a gate)
  std::size_t sites = 0;
};

void load_verdicts( const std::string& path, verdicts_t& out, std::string& sha_hex, std::uint8_t sha[ 32 ] )
{
  std::ifstream f( path, std::ios::binary );
  if ( !f )
    fail( fmt::format( "rl_buff_credit_verdicts: cannot open '{}'", path ) );
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string text = ss.str();
  sha256_t h;
  h.update( text.data(), text.size() );
  h.finish( sha );
  sha_hex = hex_of( sha, 32 );
  rapidjson::Document doc;
  doc.Parse<rapidjson::kParseFullPrecisionFlag>( text.c_str() );
  if ( doc.HasParseError() || !doc.IsObject() )
    fail( fmt::format( "rl_buff_credit_verdicts: '{}' is not a JSON object", path ) );
  if ( !doc.HasMember( "sites" ) || !doc[ "sites" ].IsArray() )
    fail( fmt::format( "rl_buff_credit_verdicts: '{}' has no 'sites' array", path ) );
  if ( doc.HasMember( "site_key" ) && doc[ "site_key" ].IsString() )
    out.site_key = doc[ "site_key" ].GetString();
  const char* frame_field = out.site_key == "kind" ? "fkind" : "fcls";
  // The verdict table is a configuration file read once here (not a ledger record): its own small field reads.
  auto field = [ & ]( const rapidjson::Value& site, const char* name ) -> const rapidjson::Value& {
    if ( !site.IsObject() || !site.HasMember( name ) )
      fail( fmt::format( "verdict site record lacks required field '{}'", name ) );
    return site[ name ];
  };
  auto text_field = [ & ]( const rapidjson::Value& site, const char* name ) {
    const auto& f = field( site, name );
    if ( !f.IsString() )
      fail( fmt::format( "verdict site record: field '{}' is not a string", name ) );
    return std::string( f.GetString(), f.GetStringLength() );
  };
  std::size_t n = 0;
  for ( const auto& s : doc[ "sites" ].GetArray() )
  {
    const std::string verdict = text_field( s, "verdict" );  // a site without a verdict is refused
    const std::string pa = text_field( s, "pa" ), ca = text_field( s, "ca" ), buff = text_field( s, "buff" );
    const auto& fk = field( s, frame_field );
    const std::string frame_key = fk.IsString() ? std::string( fk.GetString() ) : std::to_string( fk.IsInt64() ? fk.GetInt64() : 0 );
    if ( verdict == "gate" )
    {
      out.gates.insert( std::make_tuple( pa, frame_key, ca, buff ) );
      out.gate_buffs.insert( buff );
    }
    ++n;
  }
  out.sites = n;
}

bool press_is_carried( std::int64_t v )
{
  return v <= -PRESS_CARRY_BASE && v >= -PRESS_CARRY_BASE - PRESS_CARRY_MAX;
}

struct acc3_t
{
  double v[ 3 ] = { 0.0, 0.0, 0.0 };
};
struct acc2_t
{
  double v[ 2 ] = { 0.0, 0.0 };
};

// The result of one fold (decision_fold in the reference).
struct fold_t
{
  std::map<std::int64_t, std::array<double, 8>> decisions;  // seq -> the eight values in .bcr order
  double conservation_worst[ 2 ] = { 0.0, 0.0 };
  double abs_total[ 2 ]          = { 0.0, 0.0 };
  std::uint32_t conservation_fail = 0, prefix_moved = 0, unknown_press = 0, seq_without_decision = 0;
  std::uint32_t n_items = 0, n_split = 0;
};
}  // namespace

struct module_t
{
  credit_mode_t mode = credit_mode_t::off;
  std::string verdicts_path, verdicts_sha_hex;
  std::uint8_t verdicts_sha[ 32 ] = {};
  verdicts_t verdicts;
  std::string bcr_path;
  std::ofstream bcr;
  bool bcr_open = false;
  std::uint32_t n_fights_written = 0;
  std::uint64_t total_items = 0, total_split = 0;
  std::uint32_t total_counters[ 4 ] = { 0, 0, 0, 0 };

  // the fight in progress (reset by every `fb` record)
  std::unordered_map<std::int64_t, std::int64_t> press_seq;  // press -> decision seq
  std::vector<std::int64_t> press_seqs;                      // every `pr` record's seq, file order
  std::unordered_map<std::uint64_t, passset_t> apps;         // `app` records by id
  std::unordered_map<std::int64_t, launch_rec_t> lns;                // `ln` records by launch id (the swing launches' speed buffs, the switch buff, the gate keys)
  std::unordered_map<std::int64_t, frame_rec_t> frms;              // `frm` records by frame id
  std::unordered_map<std::int64_t, std::vector<read_rec_t>> reads; // frame id -> its non-zero reads of gate buffs, file order
  std::unordered_map<std::int64_t, std::vector<std::int64_t>> draws;  // frame id -> the orders of its `dr` records (only frames that have such a read)
  std::unordered_map<std::int64_t, cycle_rec_t> cycles;            // `cyc` records by cycle id (a later record with the same id wins)
  std::unordered_map<std::int64_t, std::vector<refund_rec_t>> refs; // cycle id -> its `ref` entries, file order
  std::vector<use_rec_t> uses;                                   // `use` records, file order
  std::vector<item_t> items;
  std::uint32_t counter_unknown_press = 0;  // lookups that found no `pr` record (counted while the fold runs)
};

namespace
{
std::int64_t press_decision( module_t& m, std::int64_t press, std::uint32_t& unknown )
{
  const auto it = m.press_seq.find( press );
  if ( it == m.press_seq.end() )
  {
    ++unknown;
    return UNKNOWN_KEY;
  }
  return it->second;
}

// distribution(): the credit distribution of one hit. Fills `item.entries` (the new booking) and `item.split`.
void distribution( module_t& m, const hit_rec_t& h, item_t& item )
{
  const double ra  = item.ra;
  const double exp = h.exp_excl ? 0.0 : h.exp;
  // The dealer: new_dealer() -- a carried press goes to the decoded press; otherwise the old holder
  entry_t dealer;
  if ( press_is_carried( item.old_press ) )
  {
    dealer = { 'P', -PRESS_CARRY_BASE - item.old_press, CH_DEALER, 0.0, 0.0 };
  }
  else if ( item.own )
  {
    dealer = item.old_press >= 0 ? entry_t{ 'P', item.old_press, CH_DEALER, 0.0, 0.0 } : entry_t{ 'S', item.seq, CH_DEALER, 0.0, 0.0 };
  }
  else
    dealer = { 'W', 0, CH_DEALER, 0.0, 0.0 };

  auto whole = [ & ]( bool split ) {
    item.entries.clear();
    dealer.real = ra;
    dealer.exp  = exp;
    item.entries.push_back( dealer );
    item.split = split;
  };

  const std::int64_t status = h.status;
  if ( !( status == 0 || status == 1 ) )
    return whole( false );
  if ( ra == 0.0 && exp == 0.0 )
    return whole( true );
  const bool crit = h.crit;

  players_t players;
  std::vector<bc_group_t> groups;
  groups.push_back( build_pass_group( h.ps, players, false ) );
  const bool tick = h.tick;
  for ( const std::uint64_t pid : h.par )
  {
    const auto it = m.apps.find( pid );
    if ( it != m.apps.end() )
      groups.push_back( build_pass_group( it->second, players, tick ) );
  }
  // launch_chain: the launch records from the hit's own launch up through each parent frame's launch
  std::vector<const launch_rec_t*> chain;
  {
    std::set<std::int64_t> seen;
    std::int64_t l = h.launch;
    while ( l >= 0 && chain.size() < 64 )
    {
      const auto lit = m.lns.find( l );
      if ( lit == m.lns.end() || !seen.insert( l ).second )
        break;
      chain.push_back( &lit->second );
      const auto fit = m.frms.find( lit->second.frame );
      l              = fit != m.frms.end() ? fit->second.launch : -1;
    }
  }
  // gate_players: along the launch chain, a buff read non-zero in the launching frame before the launch, with no random draw between the read and
  // the launch, at a site whose verdict is "gate" (the loaded table); plus the switch buff of a `switch` launch, which needs no table.
  for ( const launch_rec_t* rec : chain )
  {
    if ( rec->has_switch )
    {
      bc_player_t gp;
      gp.key      = { rec->sw.name, rec->sw.owner };
      gp.kind     = "buff";
      gp.appliers = rec->sw.app;
      gp.gate     = true;
      if ( gp.press_applied() )
        players.merge( std::move( gp ) );
    }
    const auto fit = m.frms.find( rec->frame );
    if ( fit == m.frms.end() )
      continue;
    const auto rit = m.reads.find( rec->frame );
    if ( rit == m.reads.end() )
      continue;
    const frame_rec_t& frame = fit->second;
    const std::string frame_key = m.verdicts.site_key == "kind" ? frame.kind : std::to_string( frame.cls );
    std::vector<const std::string*> seen_buffs;  // the distinct buffs of the frame's reads, first-seen order (dict.fromkeys)
    for ( const read_rec_t& r0 : rit->second )
    {
      bool dup = false;
      for ( const std::string* b : seen_buffs )
        if ( *b == r0.buff )
          dup = true;
      if ( dup )
        continue;
      seen_buffs.push_back( &r0.buff );
      if ( m.verdicts.gates.count( std::make_tuple( frame.action, frame_key, rec->ca, r0.buff ) ) == 0 )
        continue;
      // nz_read_before: the latest non-zero read of the buff before the launch's order (a tie on the order: the later record)
      const read_rec_t* read = nullptr;
      for ( const read_rec_t& r1 : rit->second )
        if ( r1.buff == r0.buff && r1.order < rec->order && ( read == nullptr || r1.order >= read->order ) )
          read = &r1;
      if ( read == nullptr )
        continue;
      // draw_between: any random draw of the frame strictly between the read and the launch
      bool drew = false;
      const auto dit = m.draws.find( rec->frame );
      if ( dit != m.draws.end() )
        for ( std::int64_t o : dit->second )
          if ( read->order < o && o < rec->order )
            drew = true;
      if ( drew )
        continue;
      bc_player_t gp;
      gp.key      = { r0.buff, read->owner };
      gp.kind     = "buff";
      gp.appliers = read->app;
      gp.cov_rule = read->cov;
      gp.gate     = true;
      if ( gp.press_applied() )
        players.merge( std::move( gp ) );
    }
  }
  // the swing channel: the speed buffs of the chain's swing launches, one group of speed factors (a player met twice keeps its first factor)
  bc_group_t swing_group;
  swing_group.swing = true;
  {
    for ( const launch_rec_t* rec : chain )
      for ( const launch_buff_t& sf : rec->speed )
      {
        bc_player_t sp;
        sp.key      = { sf.name, sf.owner };
        sp.kind     = "speed";
        sp.appliers = sf.app;
        sp.swing    = true;
        if ( !sp.press_applied() )
          continue;
        bool met = false;
        for ( const auto& have : swing_group.speed )
          if ( players.list[ have.first ].key == sp.key )
            met = true;
        if ( met )
          continue;
        const int id = players.merge( std::move( sp ) );
        swing_group.speed.push_back( { id, sf.factor } );
      }
  }
  if ( !swing_group.speed.empty() )
    groups.push_back( std::move( swing_group ) );
  std::uint64_t gate_ids = 0;
  for ( std::size_t i = 0; i < players.list.size(); ++i )
    if ( players.list[ i ].gate )
      gate_ids |= std::uint64_t( 1 ) << i;
  std::uint64_t effective = 0;
  for ( const auto& g : groups )
    if ( !g.swing && !mark_effective( g, crit, effective, gate_ids ) )
      return whole( false );
  // chosen = {k: pl for k, pl in players.items() if k in effective or pl.gate or pl.swing}
  std::vector<int> chosen;
  for ( std::size_t i = 0; i < players.list.size(); ++i )
    if ( ( ( effective >> i ) & 1u ) || players.list[ i ].gate || players.list[ i ].swing )
      chosen.push_back( static_cast<int>( i ) );
  if ( chosen.empty() )
    return whole( true );
  std::sort( chosen.begin(), chosen.end(), [ & ]( int a, int b ) { return players.list[ a ].key < players.list[ b ].key; } );
  const int n = static_cast<int>( chosen.size() );
  std::uint64_t gate_mask_idx = 0;
  for ( int i = 0; i < n; ++i )
    if ( players.list[ chosen[ i ] ].gate )
      gate_mask_idx |= std::uint64_t( 1 ) << i;
  std::map<std::uint64_t, pair_t> vals;
  if ( !v_of_sets( ra, exp, groups, chosen, gate_mask_idx, crit, subsets_needed( n ), vals ) )
    return whole( false );
  std::vector<double> sh_real, sh_exp;
  shares( vals, n, 0, sh_real );
  shares( vals, n, 1, sh_exp );
  item.entries.clear();
  item.split = true;
  double got_real = 0.0, got_exp = 0.0;
  std::vector<frac_t> fr;
  for ( int k = 0; k < n; ++k )
  {
    const bc_player_t& pl = players.list[ chosen[ k ] ];
    applier_split( pl, fr );
    for ( const auto& f : fr )
    {
      const double er = sh_real[ k ] * f.f, ee = sh_exp[ k ] * f.f;
      item.entries.push_back( { 'P', f.press, pl.channel(), er, ee } );
      got_real += er;
      got_exp += ee;
    }
  }
  dealer.real = ra - got_real;
  dealer.exp  = exp - got_exp;
  item.entries.push_back( dealer );
}

void reset_fight( module_t& m )
{
  m.press_seq.clear();
  m.press_seqs.clear();
  m.apps.clear();
  m.lns.clear();
  m.frms.clear();
  m.reads.clear();
  m.draws.clear();
  m.cycles.clear();
  m.refs.clear();
  m.uses.clear();
  m.items.clear();
}

// The value rules of the entry points (shared by the live ledger and the fixture converter): a class outside the base classes 0..6 (a raw class
// byte with the deck mark?) and a candidate index that does not fit the 64-bit hidden-set mask are refused.
void check_applier( const applier_t& a, const char* where )
{
  if ( a.cls < 0 || a.cls > CLS_ORPHAN )
    fail( fmt::format( "{}: applier cls {} is not a base class 0..6 (a raw class byte with the deck mark?)", where, a.cls ) );
}

void check_appliers( const std::vector<applier_t>& v, const char* where )
{
  for ( const applier_t& a : v )
    check_applier( a, where );
}

[[noreturn]] void fail_candidate_bit( const char* kind, std::int64_t bit )
{
  fail( fmt::format( "{} record: candidate index {} does not fit a 64-bit hidden-set mask", kind, bit ) );
}

void check_passset( const passset_t& ps, const char* kind )
{
  for ( const cand_t& c : ps.cands )
  {
    if ( c.bit > 63 )
      fail_candidate_bit( kind, c.bit );
    check_appliers( c.app, "cand.app" );
  }
}

void check_launch_buff( const launch_buff_t& b, const char* where )
{
  check_appliers( b.app, where );
}

// A read is kept only when it is non-zero AND its buff is named by some gate site of the loaded table (nothing else can ever be consulted:
// nz_read_before keeps non-zero reads; is_gate needs the buff). Used by the entry point and by the fixture converter (a read the module would
// drop is not even parsed there).
bool read_is_kept( const module_t& m, bool nz, const std::string& buff )
{
  return nz && m.verdicts.gate_buffs.count( buff ) != 0;
}

// A draw is kept only for a frame that has a kept read.
bool draw_is_kept( const module_t& m, std::int64_t frame )
{
  return m.reads.count( frame ) != 0;
}
}  // namespace

void on_fight_begin( module_t& m )
{
  reset_fight( m );
}

void on_press( module_t& m, press_rec_t&& r )
{
  m.press_seq[ r.press ] = r.seq;
  m.press_seqs.push_back( r.seq );
}

void on_application( module_t& m, application_rec_t&& r )
{
  check_passset( r.ps, "app" );
  m.apps[ r.id ] = std::move( r.ps );
}

void on_launch( module_t& m, launch_rec_t&& r )
{
  if ( r.has_switch )
    check_launch_buff( r.sw, "ln switch entry" );
  for ( const launch_buff_t& b : r.speed )
    check_launch_buff( b, "ln speed entry" );
  m.lns[ r.id ] = std::move( r );
}

void on_frame( module_t& m, frame_rec_t&& r )
{
  m.frms[ r.id ] = std::move( r );
}

void on_read( module_t& m, read_rec_t&& r )
{
  if ( !read_is_kept( m, r.nz, r.buff ) )
    return;
  check_appliers( r.app, "rd.app" );
  m.reads[ r.frame ].push_back( std::move( r ) );
}

void on_draw( module_t& m, draw_rec_t&& r )
{
  // draw_between asks about draws after a read of the same frame, and a draw is written after the read it follows: a frame with no kept read has no use
  if ( draw_is_kept( m, r.frame ) )
    m.draws[ r.frame ].push_back( r.order );
}

void on_cycle( module_t& m, cycle_rec_t&& r )
{
  m.cycles[ r.id ] = r;
}

void on_refund( module_t& m, refund_rec_t&& r )
{
  m.refs[ r.cycle ].push_back( r );
}

void on_use( module_t& m, use_rec_t&& r )
{
  m.uses.push_back( r );
}

void on_hit( module_t& m, hit_rec_t&& r )
{
  if ( r.cls < 0 || r.cls > CLS_ORPHAN )
    fail( fmt::format( "hit record: cls {} is not a base class 0..6", r.cls ) );
  check_passset( r.ps, "hit" );
  item_t item;
  item.ra        = r.ra;
  item.old_press = r.press;
  item.seq       = r.seq;
  item.win       = r.win;
  item.ch        = r.ch;
  item.own       = r.cls == CLS_CAST || r.cls == CLS_PROC_OF_CAST || r.cls == CLS_DOT_TICK || r.cls == CLS_PROC_OF_DOT;
  // dealer_group: "press" iff not a pet, an own class and press >= 0; everything else is "nobody"
  item.nobody = !( !r.pet && item.own && item.old_press >= 0 );
  distribution( m, r, item );
  m.items.push_back( std::move( item ) );
}

namespace
{
// refund_fractions_u + _cap_fractions (D-05u, "time actually saved"): cast press c -> [(refunding press r, fraction of c's holdings)], sorted by r.
// For a press c whose cast used cycle y (a `use` record) of length L: N = y's open + L minus the net seconds of y's entries whose cause is NOT a
// press (press < 0); u = the time of c's cast; P = the sum of the positive net seconds of the refunding presses r != c; the fraction is
// f_c = min(P, max(0, N - u)) / L and each r gets f_c x (its seconds / P). Fractions over all of c's cycles are scaled to sum to at most 1.
using fraction_list_t = std::vector<std::pair<std::int64_t, double>>;

std::map<std::int64_t, fraction_list_t> refund_fractions_u( const module_t& m )
{
  std::map<std::int64_t, fraction_list_t> out;
  static const std::vector<refund_rec_t> none;
  for ( const use_rec_t& u : m.uses )
  {
    const auto cit        = m.cycles.find( u.cycle );
    const std::int64_t c  = u.press;
    if ( cit == m.cycles.end() || cit->second.len <= 0.0 || c < 0 )
      continue;
    const auto rit      = m.refs.find( u.cycle );
    const auto& entries = rit == m.refs.end() ? none : rit->second;
    fraction_list_t net;  // press -> net seconds, insertion order (a Python dict)
    for ( const refund_rec_t& r : entries )
      if ( r.press >= 0 && r.press != c )
      {
        bool found = false;
        for ( auto& kv : net )
          if ( kv.first == r.press )
          {
            kv.second += r.sec;
            found = true;
          }
        if ( !found )
          net.push_back( { r.press, r.sec } );
      }
    fraction_list_t pos;
    double total_sec = 0.0;
    for ( const auto& kv : net )
      if ( kv.second > 0.0 )
        pos.push_back( kv );
    for ( const auto& kv : pos )
      total_sec += kv.second;
    if ( total_sec <= 0.0 )
      continue;
    const double length = cit->second.len;
    double no_press     = 0.0;
    for ( const refund_rec_t& r : entries )
      if ( r.press < 0 )
        no_press += r.sec;
    const double natural  = cit->second.t + length - no_press;
    const double fraction = std::min( total_sec, std::max( 0.0, natural - u.t ) ) / length;
    if ( fraction <= 0.0 )
      continue;
    auto& o = out[ c ];
    for ( const auto& kv : pos )
    {
      bool found = false;
      for ( auto& have : o )
        if ( have.first == kv.first )
        {
          have.second += fraction * kv.second / total_sec;
          found = true;
        }
      if ( !found )
        o.push_back( { kv.first, fraction * kv.second / total_sec } );
    }
  }
  for ( auto& cv : out )
  {
    double total = 0.0;
    for ( const auto& kv : cv.second )
      total += kv.second;
    if ( total > 1.0 )
      for ( auto& kv : cv.second )
        kv.second /= total;
    std::sort( cv.second.begin(), cv.second.end(), []( const std::pair<std::int64_t, double>& a, const std::pair<std::int64_t, double>& b ) { return a.first < b.first; } );
  }
  return out;
}

// apply_refunds (rule D-05u): the backward sweep. Presses in descending order; for each hit, f_r x (everything c holds on that hit, every channel,
// realised and expected) moves from c to r (channel "refund"). Each item is its own table (the reference's `hold[key]`); `index` lists, per press, the
// items that hold a bucket of it. Buckets are merged by (holder, channel) first, as the reference's `hold` is, and the entries are the buckets after
// the sweep (a bucket emptied by a move stays an entry, as in the reference).
void apply_refunds( module_t& m )
{
  const auto fractions = refund_fractions_u( m );
  if ( fractions.empty() )
    return;
  auto& items = m.items;
  std::map<std::int64_t, std::vector<std::size_t>> index;
  for ( std::size_t i = 0; i < items.size(); ++i )
  {
    std::vector<entry_t> merged;
    for ( const entry_t& e : items[ i ].entries )
    {
      bool found = false;
      for ( entry_t& have : merged )
        if ( have.holder == e.holder && have.id == e.id && have.channel == e.channel )
        {
          have.real += e.real;
          have.exp += e.exp;
          found = true;
        }
      if ( !found )
        merged.push_back( e );
    }
    items[ i ].entries = std::move( merged );
    for ( const entry_t& e : items[ i ].entries )
      if ( e.holder == 'P' )
      {
        auto& v = index[ e.id ];
        if ( v.empty() || v.back() != i )
          v.push_back( i );
      }
  }
  struct snap_t
  {
    channel_t channel;
    double real, exp;
  };
  for ( auto cit = fractions.rbegin(); cit != fractions.rend(); ++cit )
  {
    const std::int64_t c = cit->first;
    const auto idx       = index.find( c );
    if ( idx == index.end() )
      continue;
    const std::vector<std::size_t> holders = idx->second;  // list(index.get(c, ()))
    for ( std::size_t i : holders )
    {
      item_t& it = items[ i ];
      std::vector<snap_t> snap;
      bool any = false;
      for ( const entry_t& e : it.entries )
        if ( e.holder == 'P' && e.id == c )
        {
          snap.push_back( { e.channel, e.real, e.exp } );
          if ( e.real != 0.0 || e.exp != 0.0 )
            any = true;
        }
      if ( snap.empty() || !any )
        continue;
      for ( const auto& rf : cit->second )
      {
        const std::int64_t r = rf.first;
        const double f       = rf.second;
        bool had_press = false;
        std::size_t dest = it.entries.size();
        for ( std::size_t k = 0; k < it.entries.size(); ++k )
          if ( it.entries[ k ].holder == 'P' && it.entries[ k ].id == r )
          {
            had_press = true;
            if ( it.entries[ k ].channel == CH_REFUND )
              dest = k;
          }
        if ( dest == it.entries.size() )
          it.entries.push_back( { 'P', r, CH_REFUND, 0.0, 0.0 } );
        for ( const snap_t& s : snap )
        {
          it.entries[ dest ].real += f * s.real;
          it.entries[ dest ].exp += f * s.exp;
          for ( entry_t& e : it.entries )
            if ( e.holder == 'P' && e.id == c && e.channel == s.channel )
            {
              e.real -= f * s.real;
              e.exp -= f * s.exp;
            }
        }
        if ( !had_press )
          index[ r ].push_back( i );
      }
    }
  }
}

// decision_fold
fold_t fold_fight( module_t& m, const std::vector<std::int64_t>* decision_seqs )
{
  apply_refunds( m );  // analyse_fight: the refund sweep, then the fold
  fold_t out;
  std::array<std::map<std::int64_t, acc3_t>, 2> old_;  // copy -> key -> [old_own, old_interval, old nobody total]
  std::array<std::map<std::int64_t, acc2_t>, 2> new_;  // copy -> key -> [new full, new nobody]
  std::set<std::int64_t> touched;
  std::uint32_t unknown = 0;
  for ( const item_t& item : m.items )
  {
    std::int64_t old_key;
    if ( item.own )
      old_key = item.old_press >= 0 ? press_decision( m, item.old_press, unknown ) : item.seq;
    else
      old_key = item.win;
    struct keyed_t
    {
      std::int64_t k;
      double real;
    };
    std::vector<keyed_t> keyed;
    keyed.reserve( item.entries.size() );
    for ( const entry_t& e : item.entries )
    {
      std::int64_t k;
      if ( e.holder == 'P' )
        k = press_decision( m, e.id, unknown );
      else if ( e.holder == 'S' )
        k = e.id;
      else
        k = item.win;
      keyed.push_back( { k, e.real } );
    }
    ++out.n_items;
    if ( item.split )
      ++out.n_split;
    touched.insert( old_key );
    for ( int c = 0; c < ( item.ch ? 2 : 1 ); ++c )
    {
      out.abs_total[ c ] += std::fabs( item.ra );
      acc3_t& o = old_[ c ][ old_key ];
      o.v[ item.own ? 0 : 1 ] += item.ra;
      if ( item.nobody )
        o.v[ 2 ] += item.ra;
      for ( const auto& kr : keyed )
      {
        acc2_t& nw = new_[ c ][ kr.k ];
        nw.v[ 0 ] += kr.real;
        if ( item.nobody )
          nw.v[ 1 ] += kr.real;
      }
    }
    for ( const auto& kr : keyed )
      touched.insert( kr.k );
  }
  out.unknown_press = unknown;
  // the decision set
  std::set<std::int64_t> dset;
  if ( decision_seqs == nullptr )
  {
    for ( std::int64_t s : m.press_seqs )
      dset.insert( s );
    for ( std::int64_t k : touched )
      if ( k != UNKNOWN_KEY && k >= 0 )
        dset.insert( k );
  }
  else
    dset.insert( decision_seqs->begin(), decision_seqs->end() );
  const std::int64_t first = dset.empty() ? 0 : *dset.begin();
  double sums[ 2 ][ 2 ] = { { 0.0, 0.0 }, { 0.0, 0.0 } };
  for ( int c = 0; c < 2; ++c )
  {
    std::set<std::int64_t> keys;
    for ( const auto& kv : old_[ c ] )
      keys.insert( kv.first );
    for ( const auto& kv : new_[ c ] )
      keys.insert( kv.first );
    for ( std::int64_t k : keys )
    {
      const auto oi = old_[ c ].find( k );
      const auto ni = new_[ c ].find( k );
      const acc3_t o = oi == old_[ c ].end() ? acc3_t{} : oi->second;
      const acc2_t n = ni == new_[ c ].end() ? acc2_t{} : ni->second;
      const double moved_full   = n.v[ 0 ] - ( o.v[ 0 ] + o.v[ 1 ] );
      const double moved_nobody = n.v[ 1 ] - o.v[ 2 ];
      sums[ c ][ 0 ] += moved_full;
      sums[ c ][ 1 ] += moved_nobody;
      if ( dset.count( k ) )
      {
        auto& row = out.decisions[ k ];
        row[ 4 * c + 0 ] = o.v[ 0 ];
        row[ 4 * c + 1 ] = o.v[ 1 ];
        row[ 4 * c + 2 ] = moved_full;
        row[ 4 * c + 3 ] = moved_nobody;
      }
    }
  }
  for ( std::int64_t k : dset )
    out.decisions.emplace( k, std::array<double, 8>{} );  // a decision nothing touched: all zeros (emplace keeps an existing row)
  for ( std::int64_t k : touched )
  {
    if ( k == UNKNOWN_KEY || dset.count( k ) )
      continue;  // an unknown press was counted at the lookup
    // MOVED, not merely touched (a background hit at the pull that kept its whole amount is no finding): a moved value above the
    // conservation tolerance of the copy, in either slice.
    bool nonzero = false;
    for ( int c = 0; c < 2; ++c )
    {
      const auto oi  = old_[ c ].find( k );
      const auto ni  = new_[ c ].find( k );
      const acc3_t o = oi == old_[ c ].end() ? acc3_t{} : oi->second;
      const acc2_t n = ni == new_[ c ].end() ? acc2_t{} : ni->second;
      const double tol = REL_TOL * std::max( out.abs_total[ c ], 1.0 );
      if ( std::fabs( n.v[ 0 ] - ( o.v[ 0 ] + o.v[ 1 ] ) ) > tol || std::fabs( n.v[ 1 ] - o.v[ 2 ] ) > tol )
        nonzero = true;
    }
    if ( nonzero )
    {
      if ( k < first )
        ++out.prefix_moved;
      else
        ++out.seq_without_decision;
    }
  }
  for ( int c = 0; c < 2; ++c )
  {
    const double scale = std::max( out.abs_total[ c ], 1.0 );
    out.conservation_worst[ c ] = std::max( std::fabs( sums[ c ][ 0 ] ), std::fabs( sums[ c ][ 1 ] ) ) / scale;
    if ( out.conservation_worst[ c ] > REL_TOL )
      ++out.conservation_fail;
  }
  return out;
}

void write_bytes( module_t& m, const void* p, std::size_t n )
{
  m.bcr.write( static_cast<const char*>( p ), static_cast<std::streamsize>( n ) );
  if ( !m.bcr )
    fail( fmt::format( "write to '{}' failed", m.bcr_path ) );
}
}  // namespace

bool parse_mode( std::string_view text, credit_mode_t& out )
{
  if ( text == "off" )
    out = credit_mode_t::off;
  else if ( text == "nobody" )
    out = credit_mode_t::nobody;
  else if ( text == "full" )
    out = credit_mode_t::full;
  else
    return false;
  return true;
}

const char* mode_name( credit_mode_t m )
{
  return m == credit_mode_t::nobody ? "nobody" : ( m == credit_mode_t::full ? "full" : "off" );
}

std::shared_ptr<module_t> configure( sim_t* root, credit_mode_t mode, const std::string& verdicts_path, const std::string& translog_path )
{
  (void)root;
  auto m = std::make_shared<module_t>();
  m->mode          = mode;
  m->verdicts_path = verdicts_path;
  load_verdicts( verdicts_path, m->verdicts, m->verdicts_sha_hex, m->verdicts_sha );
  m->bcr_path = translog_path + ".bcr";
  m->bcr.open( m->bcr_path, std::ios::out | std::ios::trunc | std::ios::binary );
  if ( !m->bcr.is_open() )
    fail( fmt::format( "rl_buff_credit: unable to open '{}' for writing", m->bcr_path ) );
  m->bcr_open = true;
  bcr_header h{};
  std::memcpy( h.magic, BCR_MAGIC, sizeof( BCR_MAGIC ) );
  h.format_version = BCR_VERSION;
  h.record_size    = BCR_RECORD_SIZE;
  h.header_size    = BCR_HEADER_SIZE;
  h.sw             = static_cast<std::uint8_t>( mode );
  h.rule           = BCR_RULE_D05U;
  std::memcpy( h.sha, m->verdicts_sha, 32 );
  write_bytes( *m, &h, sizeof( h ) );
  m->bcr.flush();
  fmt::print( stderr, "[RL_BUFF_CREDIT] mode={} rule=D-05u verdicts_sha256={}\n", mode_name( mode ), m->verdicts_sha_hex );
  std::fflush( stderr );
  return m;
}

namespace
{
void write_fight_records( module_t& m, std::uint32_t iteration, bool collected, bool funnel, const std::vector<std::int64_t>* seqs )
{
  const fold_t f = fold_fight( m, seqs );
  bcr_fight fr{};
  fr.kind                       = BCR_KIND_FIGHT;
  fr.iteration                  = iteration;
  fr.n_decisions                = static_cast<std::uint32_t>( f.decisions.size() );
  fr.flags                      = ( collected ? BCR_FLAG_COLLECTED : 0u ) | ( funnel ? BCR_FLAG_FUNNEL : 0u );
  fr.conservation_worst_all     = f.conservation_worst[ 0 ];
  fr.conservation_worst_chosen  = f.conservation_worst[ 1 ];
  fr.abs_total_all              = f.abs_total[ 0 ];
  fr.abs_total_chosen           = f.abs_total[ 1 ];
  write_bytes( m, &fr, sizeof( fr ) );
  for ( const auto& kv : f.decisions )
  {
    bcr_decision d{};
    d.kind      = BCR_KIND_DECISION;
    d.iteration = iteration;
    d.seq       = static_cast<std::uint32_t>( kv.first );
    for ( int i = 0; i < 8; ++i )
      d.v[ i ] = kv.second[ i ];
    write_bytes( m, &d, sizeof( d ) );
  }
  m.bcr.flush();
  ++m.n_fights_written;
  m.total_items += f.n_items;
  m.total_split += f.n_split;
  m.total_counters[ 0 ] += f.conservation_fail;
  m.total_counters[ 1 ] += f.prefix_moved;
  m.total_counters[ 2 ] += f.unknown_press;
  m.total_counters[ 3 ] += f.seq_without_decision;
}
}  // namespace

void write_fight( module_t& m, std::uint32_t iteration, bool collected, bool funnel, const std::vector<std::uint64_t>& decision_seqs )
{
  std::vector<std::int64_t> seqs;
  seqs.reserve( decision_seqs.size() );
  for ( std::uint64_t s : decision_seqs )
    seqs.push_back( static_cast<std::int64_t>( s ) );
  write_fight_records( m, iteration, collected, funnel, &seqs );
}

void write_footer( module_t& m, const std::array<std::uint32_t, 12>& ledger_counters )
{
  if ( !m.bcr_open )
    return;
  bcr_footer ft{};
  ft.kind     = BCR_KIND_FOOTER;
  ft.n_fights = m.n_fights_written;
  for ( int i = 0; i < 12; ++i )
    ft.required_zero[ i ] = ledger_counters[ i ];
  for ( int i = 0; i < 4; ++i )
    ft.required_zero[ 12 + i ] = m.total_counters[ i ];
  ft.n_items = static_cast<std::uint32_t>( m.total_items );
  ft.n_split = static_cast<std::uint32_t>( m.total_split );
  write_bytes( m, &ft, sizeof( ft ) );
  m.bcr.flush();
  m.bcr.close();
  m.bcr_open = false;
}

// ==========================================================================================================================================
// THE FIXTURE CONVERTER (plan 03b, owner ruling 10). The ONLY ledger-record JSON code left in the engine: it reads a fixture line, builds the
// typed record of its kind exactly as the JSON backend's text means it, and calls the same `on_*` entry point the live ledger calls. Refusals
// about JSON SHAPE (a missing field, a wrong JSON type, a hit without `win`) are here; refusals about VALUES are in the entry points. It was MOVED
// from the module's former line parser, not rewritten: the fixture path's meaning is unchanged (a field the old parser read lazily is still read
// lazily). The verdict table loader is a configuration read and keeps its own small JSON reads.
// ==========================================================================================================================================
namespace
{
namespace converter
{
struct rec_t
{
  const rapidjson::Value& v;
  const char* kind;

  const rapidjson::Value& field( const char* name ) const
  {
    if ( !v.IsObject() || !v.HasMember( name ) )
      fail( fmt::format( "{} record lacks required field '{}'", kind, name ) );
    return v[ name ];
  }
  bool has( const char* name ) const { return v.IsObject() && v.HasMember( name ); }
  std::int64_t i64( const char* name ) const
  {
    const auto& f = field( name );
    if ( f.IsInt64() )
      return f.GetInt64();
    if ( f.IsUint64() )
      return static_cast<std::int64_t>( f.GetUint64() );
    fail( fmt::format( "{} record: field '{}' is not an integer", kind, name ) );
  }
  std::uint64_t u64( const char* name ) const
  {
    const auto& f = field( name );
    if ( f.IsUint64() )
      return f.GetUint64();
    if ( f.IsInt64() && f.GetInt64() >= 0 )
      return static_cast<std::uint64_t>( f.GetInt64() );
    fail( fmt::format( "{} record: field '{}' is not a non-negative integer", kind, name ) );
  }
  double num( const char* name, bool null_is_zero = false ) const
  {
    const auto& f = field( name );
    if ( f.IsNumber() )
      return f.GetDouble();
    if ( null_is_zero && f.IsNull() )
      return 0.0;
    fail( fmt::format( "{} record: field '{}' is not a number", kind, name ) );
  }
  bool boolean( const char* name ) const
  {
    const auto& f = field( name );
    if ( !f.IsBool() )
      fail( fmt::format( "{} record: field '{}' is not a boolean", kind, name ) );
    return f.GetBool();
  }
  std::string str( const char* name ) const
  {
    const auto& f = field( name );
    if ( !f.IsString() )
      fail( fmt::format( "{} record: field '{}' is not a string", kind, name ) );
    return std::string( f.GetString(), f.GetStringLength() );
  }
  const rapidjson::Value& arr( const char* name ) const
  {
    const auto& f = field( name );
    if ( !f.IsArray() )
      fail( fmt::format( "{} record: field '{}' is not an array", kind, name ) );
    return f;
  }
};

// The kind of a record line without parsing it: the string after the first `"k"` key.
std::string_view kind_of( std::string_view line )
{
  const auto k = line.find( "\"k\"" );
  if ( k == std::string_view::npos )
    return {};
  std::size_t i = k + 3;
  while ( i < line.size() && ( line[ i ] == ' ' || line[ i ] == ':' ) )
    ++i;
  if ( i >= line.size() || line[ i ] != '"' )
    return {};
  ++i;
  const auto e = line.find( '"', i );
  if ( e == std::string_view::npos )
    return {};
  return line.substr( i, e - i );
}

// parse_applier: the SHAPE of an applier entry [press, cls, stacks, ...]; the class value is checked by the entry points. A three-element entry
// (the stacked form) is read with covering = true, as is a covering entry.
applier_t parse_applier( const rapidjson::Value& raw, const char* where )
{
  if ( !raw.IsArray() || raw.Size() < 3 )
    fail( fmt::format( "{}: an applier entry is not [press, cls, stacks, ...]", where ) );
  if ( !raw[ 0 ].IsInt64() || !raw[ 1 ].IsInt() || !raw[ 2 ].IsNumber() )
    fail( fmt::format( "{}: an applier entry has a non-numeric field", where ) );
  const bool covering = raw.Size() > 3 ? ( raw[ 3 ].IsBool() ? raw[ 3 ].GetBool() : raw[ 3 ].GetDouble() != 0.0 ) : true;
  return applier_t{ raw[ 0 ].GetInt64(), raw[ 1 ].GetInt(), raw[ 2 ].GetDouble(), covering };
}

std::vector<applier_t> parse_appliers( const rapidjson::Value& list, const char* where )
{
  std::vector<applier_t> out;
  for ( const auto& a : list.GetArray() )
    out.push_back( parse_applier( a, where ) );
  return out;
}

passset_t parse_passset( const rec_t& r )
{
  passset_t out;
  for ( const auto& c : r.arr( "cand" ).GetArray() )
  {
    rec_t cr{ c, "cand entry" };
    cand_t cd;
    const std::int64_t bit = cr.i64( "i" );
    if ( bit < 0 || bit > 63 )
      fail_candidate_bit( r.kind, bit );
    cd.bit   = static_cast<std::uint32_t>( bit );
    cd.name  = cr.str( "name" );
    cd.owner = cr.str( "owner" );
    cd.kind  = cr.str( "kind" );
    cd.cov   = cr.field( "cov" ).IsBool() ? cr.boolean( "cov" ) : cr.num( "cov" ) != 0.0;
    cd.app   = parse_appliers( cr.arr( "app" ), "cand.app" );
    out.cands.push_back( std::move( cd ) );
  }
  for ( const auto& p : r.arr( "pass" ).GetArray() )
  {
    rec_t pr{ p, "pass entry" };
    pass_t ps;
    const std::string k = pr.str( "kind" );
    ps.kind             = k == "ref" ? PASS_REF : ( k == "hide" ? PASS_HIDE : PASS_OTHER );
    ps.hid              = pr.u64( "hid" );
    ps.pre              = pr.num( "pre", true );
    ps.cc               = pr.num( "cc", true );
    ps.cb               = pr.num( "cb", true );
    ps.per              = pr.num( "per", true );
    const auto& viol    = pr.field( "viol" );
    ps.viol             = viol.IsBool() ? viol.GetBool() : ( viol.IsNumber() && viol.GetDouble() != 0.0 );
    out.passes.push_back( ps );
  }
  return out;
}

launch_rec_t convert_launch( const rec_t& r )
{
  launch_rec_t ln;
  ln.frame = r.i64( "frame" );
  ln.order = r.has( "order" ) ? r.i64( "order" ) : -1;
  ln.ca    = r.has( "ca" ) && r.field( "ca" ).IsString() ? r.str( "ca" ) : std::string();
  // parse_switch: only a `switch` launch with a truthy `sw` (null, an empty object or list, "" and false are falsy); an entry is the object
  // {buff, owner?, app?, ...} or the list [buff, appliers]
  if ( r.str( "lk" ) == "switch" && r.has( "sw" ) )
  {
    const auto& sw    = r.field( "sw" );
    const bool truthy = !( sw.IsNull() || ( sw.IsObject() && sw.MemberCount() == 0 ) || ( sw.IsArray() && sw.Size() == 0 ) ||
                           ( sw.IsString() && sw.GetStringLength() == 0 ) || ( sw.IsBool() && !sw.GetBool() ) || ( sw.IsNumber() && sw.GetDouble() == 0.0 ) );
    if ( truthy )
    {
      if ( sw.IsObject() )
      {
        rec_t sr{ sw, "ln switch entry" };
        ln.sw.name  = sr.str( "buff" );
        ln.sw.owner = sr.has( "owner" ) ? sr.str( "owner" ) : std::string();
        if ( sr.has( "app" ) )
          ln.sw.app = parse_appliers( sr.arr( "app" ), "ln switch entry" );
      }
      else if ( sw.IsArray() && sw.Size() >= 2 && sw[ 0 ].IsString() && sw[ 1 ].IsArray() )
      {
        ln.sw.name = std::string( sw[ 0 ].GetString(), sw[ 0 ].GetStringLength() );
        ln.sw.app  = parse_appliers( sw[ 1 ], "ln switch entry" );
      }
      else
        fail( "ln record: the switch entry is neither {buff, ...} nor [buff, appliers]" );
      ln.has_switch = true;
    }
  }
  // parse_swing_speed: only a `swing` launch with a non-empty `sf`; an entry is the object {buff, owner?, app?, f?} or the list [buff, app]
  // (no factor: dropped, but its appliers are still validated like the reference's parse_buff_entry does)
  if ( r.str( "lk" ) == "swing" && r.has( "sf" ) && r.field( "sf" ).IsArray() )
    for ( const auto& raw : r.field( "sf" ).GetArray() )
    {
      launch_buff_t sp;
      bool has_f = false;
      if ( raw.IsObject() )
      {
        rec_t sr{ raw, "ln speed entry" };
        sp.name  = sr.str( "buff" );
        sp.owner = sr.has( "owner" ) ? sr.str( "owner" ) : std::string();
        if ( sr.has( "app" ) )
          sp.app = parse_appliers( sr.arr( "app" ), "ln speed entry" );
        if ( sr.has( "f" ) && !sr.field( "f" ).IsNull() )
        {
          sp.factor = sr.num( "f" );
          has_f     = true;
        }
      }
      else if ( raw.IsArray() && raw.Size() >= 2 && raw[ 1 ].IsArray() )
        sp.app = parse_appliers( raw[ 1 ], "ln speed entry" );
      else
        fail( "ln record: a speed entry is neither {buff, ...} nor [buff, appliers]" );
      if ( has_f && sp.factor > 0.0 )
        ln.speed.push_back( std::move( sp ) );
      else
        check_appliers( sp.app, "ln speed entry" );  // a dropped entry never reaches an entry point: its appliers are still validated
    }
  return ln;
}

hit_rec_t convert_hit( const rec_t& h )
{
  hit_rec_t out;
  out.ra  = h.num( "ra", true );
  // (a value outside the int range cannot be a base class: -1 is refused by on_hit like any other non-class)
  const std::int64_t cls = h.i64( "cls" );
  out.cls   = cls < INT32_MIN || cls > INT32_MAX ? -1 : static_cast<int>( cls );
  out.press = h.i64( "press" );
  out.seq   = h.i64( "seq" );
  if ( !h.has( "win" ) )
    fail( "hit record lacks required field 'win' (the ledger does not announce 2026-10-04-s1-win)" );
  out.win  = h.i64( "win" );
  out.ch   = h.has( "ch" ) && h.field( "ch" ).IsBool() && h.field( "ch" ).GetBool();
  out.pet  = h.has( "pet" ) && h.field( "pet" ).IsBool() && h.field( "pet" ).GetBool();
  out.exp_excl = h.boolean( "exp_excl" );
  out.exp      = out.exp_excl ? 0.0 : h.num( "exp", true );
  const std::int64_t status = h.i64( "status" );
  out.status                = status < INT32_MIN || status > INT32_MAX ? -1 : static_cast<int>( status );
  // The rest of the record is read only when the module's arithmetic can reach it (the old parser read it lazily): a status other than 0/1, or
  // nothing to move, ends the distribution at the whole amount.
  const bool main_path = ( out.status == 0 || out.status == 1 ) && !( out.ra == 0.0 && out.exp == 0.0 );
  if ( !main_path )
    return out;
  const std::string res = h.str( "res" );
  out.crit              = res == "crit" || res == "crit-block" || res == "crit_block" || res == "multistrike_crit";
  out.ps                = parse_passset( h );
  out.tick              = h.str( "at" ) == "t";
  for ( const auto& pid : h.arr( "par" ).GetArray() )
    if ( pid.IsUint64() )
      out.par.push_back( pid.GetUint64() );
  out.launch = h.i64( "launch" );
  return out;
}

// One record line -> its typed record -> its entry point. `live_path` is the Task 1 ledger feed: hits arrive typed from the ledger hook there, so a hit
// line is skipped (a double feed would double the items).
void convert_line( module_t& m, std::string_view line, bool live_path )
{
  const std::string_view kind = kind_of( line );
  if ( kind != "fb" && kind != "pr" && kind != "app" && kind != "hit" && kind != "ln" && kind != "frm" && kind != "rd" && kind != "dr" && kind != "cyc" &&
       kind != "ref" && kind != "use" )
    return;  // the other record kinds (xp, cs, zr, cdn, ext, ...) move nothing in this rule
  if ( live_path && kind == "hit" )
    return;
  if ( ( kind == "rd" || kind == "dr" ) && m.verdicts.gate_buffs.empty() )
    return;  // no gate site in the table: no read or draw can matter
  rapidjson::Document doc;
  std::string text( line );
  doc.Parse<rapidjson::kParseFullPrecisionFlag>( text.c_str() );
  if ( doc.HasParseError() || !doc.IsObject() )
    fail( fmt::format( "a ledger record line is not a JSON object: {}", text.substr( 0, 80 ) ) );
  if ( kind == "fb" )
    on_fight_begin( m );
  else if ( kind == "pr" )
  {
    rec_t r{ doc, "pr" };
    on_press( m, press_rec_t{ r.i64( "press" ), r.i64( "seq" ) } );
  }
  else if ( kind == "app" )
  {
    rec_t r{ doc, "app" };
    application_rec_t a;
    a.id = r.u64( "h" );
    a.ps = parse_passset( r );
    on_application( m, std::move( a ) );
  }
  else if ( kind == "ln" )
  {
    rec_t r{ doc, "ln" };
    launch_rec_t ln = convert_launch( r );
    ln.id           = r.i64( "l" );
    on_launch( m, std::move( ln ) );
  }
  else if ( kind == "frm" )
  {
    rec_t r{ doc, "frm" };
    frame_rec_t fr;
    fr.launch = r.i64( "launch" );
    fr.action = r.has( "action" ) && r.field( "action" ).IsString() ? r.str( "action" ) : std::string();
    fr.kind   = r.has( "kind" ) && r.field( "kind" ).IsString() ? r.str( "kind" ) : std::string();
    fr.cls    = r.has( "cls" ) ? r.i64( "cls" ) : 0;
    fr.id     = r.i64( "f" );
    on_frame( m, std::move( fr ) );
  }
  else if ( kind == "rd" )
  {
    rec_t r{ doc, "rd" };
    read_rec_t rd;
    rd.nz   = r.boolean( "nz" );
    rd.buff = r.str( "buff" );
    if ( read_is_kept( m, rd.nz, rd.buff ) )  // the rest is read only for a read the module keeps
    {
      rd.order = r.i64( "o" );
      rd.owner = r.str( "owner" );
      rd.app   = parse_appliers( r.arr( "app" ), "rd.app" );
      rd.cov   = r.has( "cov" ) && !r.field( "cov" ).IsNull() && ( r.field( "cov" ).IsBool() ? r.field( "cov" ).GetBool() : r.field( "cov" ).GetDouble() != 0.0 );
      rd.frame = r.i64( "f" );
    }
    else
      rd.frame = r.i64( "f" );
    on_read( m, std::move( rd ) );
  }
  else if ( kind == "dr" )
  {
    rec_t r{ doc, "dr" };
    draw_rec_t d;
    d.frame = r.i64( "f" );
    if ( draw_is_kept( m, d.frame ) )
      d.order = r.i64( "o" );
    on_draw( m, std::move( d ) );
  }
  else if ( kind == "cyc" )
  {
    rec_t r{ doc, "cyc" };
    on_cycle( m, cycle_rec_t{ r.i64( "c" ), r.num( "t" ), r.num( "len" ) } );
  }
  else if ( kind == "ref" )
  {
    rec_t r{ doc, "ref" };
    on_refund( m, refund_rec_t{ r.i64( "c" ), r.i64( "press" ), r.num( "sec" ) } );
  }
  else if ( kind == "use" )
  {
    rec_t r{ doc, "use" };
    on_use( m, use_rec_t{ r.i64( "c" ), r.i64( "press" ), r.num( "t" ) } );
  }
  else if ( kind == "hit" )
  {
    rec_t r{ doc, "hit" };
    on_hit( m, convert_hit( r ) );
  }
}
}  // namespace converter
}  // namespace

void consume_line( module_t& m, std::string_view line )
{
  converter::convert_line( m, line, /*live_path=*/true );
}

// ---- the fixture entry ----
int run_selftest( sim_t* sim )
{
  try
  {
    credit_mode_t mode = credit_mode_t::off;
    if ( !parse_mode( sim->rl_buff_credit_str, mode ) || mode == credit_mode_t::off )
      fail( "rl_buff_credit_selftest needs rl_buff_credit=nobody|full" );
    if ( sim->rl_buff_credit_verdicts_str.empty() )
      fail( "rl_buff_credit_selftest needs rl_buff_credit_verdicts=<path>" );
    if ( sim->rl_buff_credit_selftest_out_str.empty() )
      fail( "rl_buff_credit_selftest needs rl_buff_credit_selftest_out=<path.bcr>" );
    // configure() appends ".bcr" to a translog path; the fixture entry names the output file itself.
    std::string base = sim->rl_buff_credit_selftest_out_str;
    if ( base.size() < 4 || base.compare( base.size() - 4, 4, ".bcr" ) != 0 )
      fail( "rl_buff_credit_selftest_out must end in .bcr" );
    base.resize( base.size() - 4 );
    auto m = configure( sim, mode, sim->rl_buff_credit_verdicts_str, base );
    std::ifstream in( sim->rl_buff_credit_selftest_str );
    if ( !in )
      fail( fmt::format( "rl_buff_credit_selftest: cannot open '{}'", sim->rl_buff_credit_selftest_str ) );
    std::array<std::uint32_t, 12> ledger_counters{};
    // the ledger footer names the twelve required-zero counters in the contract's order
    static const char* names[ 12 ] = { "lost_press",       "reference_mismatch", "restoring_mismatch",    "stale_hit_id",
                                       "unsafe_hits",      "ext_unattributed",   "tick_parent_stale",     "frame_pop_mismatch",
                                       "applier_reconciled", "cd_recovered",     "nested_pass_refused",   "press_carry_overflow" };
    std::string line;
    std::uint64_t lineno = 0;
    bool seen_ftr        = false;
    while ( std::getline( in, line ) )
    {
      ++lineno;
      if ( line.empty() )
        continue;
      const std::string_view kind = converter::kind_of( line );
      if ( kind == "fe" || kind == "ftr" )
      {
        rapidjson::Document doc;
        doc.Parse<rapidjson::kParseFullPrecisionFlag>( line.c_str() );
        if ( doc.HasParseError() || !doc.IsObject() )
          fail( fmt::format( "{}:{}: not a JSON object", sim->rl_buff_credit_selftest_str, lineno ) );
        converter::rec_t r{ doc, kind == "fe" ? "fe" : "ftr" };
        if ( kind == "fe" )
        {
          const bool collected = r.boolean( "collected" );
          write_fight_records( *m, static_cast<std::uint32_t>( r.u64( "it" ) ), collected, false, nullptr );
        }
        else
        {
          for ( int i = 0; i < 12; ++i )
            if ( doc.HasMember( names[ i ] ) && doc[ names[ i ] ].IsUint64() )
              ledger_counters[ i ] = static_cast<std::uint32_t>( doc[ names[ i ] ].GetUint64() );
          seen_ftr = true;
        }
        continue;
      }
      converter::convert_line( *m, line, /*live_path=*/false );
    }
    if ( !seen_ftr )
      fail( fmt::format( "{}: the fixture has no footer record", sim->rl_buff_credit_selftest_str ) );
    write_footer( *m, ledger_counters );
    return 0;
  }
  catch ( const std::exception& e )
  {
    fmt::print( stderr, "{}\n", e.what() );
    return 2;
  }
}
}  // namespace rl_buff_credit
