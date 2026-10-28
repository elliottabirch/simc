// ==========================================================================
// 261028-fzl (S1-F1a): shared helpers of the two census writers (rl_action_census, rl_buff_census) for the fight-list JSON Lines mode.
//
// Header-only on purpose (one definition per inline function, no build-file change). Everything here is bookkeeping for the census
// writers; nothing reads or changes play.
//   * sha256_hex      FIPS 180-4, a copy of the one in rl_table_policy.cpp / rl_buff_credit.cpp (each lives in an anonymous namespace there).
//   * signature_state the per-PROCESS set of table signatures already written, one set per census kind. It is deliberately NOT keyed by a
//                     sim_t address: a fight list builds one sim_t per entry and a freed entry's address is reused by the next one.
//   * append_text     the file helper: "wb" truncates (entry 0 of a list, or an ordinary process), "ab" appends one line.
// ==========================================================================

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string>

namespace rl_census_lines
{

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
      const std::size_t take = 64 - fill < n ? 64 - fill : n;
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

// Lower-case hex of the sha256 of `bytes`.
inline std::string sha256_hex( const std::string& bytes )
{
  sha256_t h;
  h.update( bytes.data(), bytes.size() );
  std::uint8_t out[ 32 ];
  h.finish( out );
  static const char* digits = "0123456789abcdef";
  std::string        hex;
  for ( std::uint8_t b : out )
  {
    hex += digits[ b >> 4 ];
    hex += digits[ b & 0x0f ];
  }
  return hex;
}

// The per-process set of table signatures already written, one set per census kind (0 = action, 1 = buff). Not keyed by sim address.
struct signature_state_t
{
  std::mutex            mutex;
  std::set<std::string> written[ 2 ];
};

inline signature_state_t& signature_state()
{
  static signature_state_t s;
  return s;
}

// True the first time `signature` is offered for `kind` in this process (and records it).
inline bool first_time_signature( int kind, const std::string& signature )
{
  signature_state_t&          s = signature_state();
  std::lock_guard<std::mutex> lock( s.mutex );
  return s.written[ kind ].insert( signature ).second;
}

// Entry 0 of a list starts the process's signature set over (a second list in one process would be a new file).
inline void reset_signatures( int kind )
{
  signature_state_t&          s = signature_state();
  std::lock_guard<std::mutex> lock( s.mutex );
  s.written[ kind ].clear();
}

// mode "wb" truncates and writes; "ab" appends. Returns false (after a stderr line from the caller) when the file cannot be opened.
inline bool write_text( const std::string& path, const std::string& text, const char* mode )
{
  std::FILE* f = std::fopen( path.c_str(), mode );
  if ( !f )
    return false;
  std::fwrite( text.data(), 1, text.size(), f );
  std::fclose( f );
  return true;
}

}  // namespace rl_census_lines
