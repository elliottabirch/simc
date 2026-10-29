// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// The compact training-data shard stream (261029-trp S2-F3). The layout, the encoding and the reward arithmetic are documented in rl_shard.hpp; the Python twin is
// src/sylvanas_rl/contract/shard.py (the codec) and src/sylvanas_rl/sim/shard_derive.py (the derivation and the byte comparison).
//
// This file is compiled with -ffp-contract=off (engine/CMakeLists.txt): the reward sums and the fixed-point rounding must round exactly as numpy does, and a fused
// multiply-add would change a rounding Python does not fuse.

#include "sim/rl_shard.hpp"

#include "player/player.hpp"
#include "sim/sim.hpp"
#include "util/util.hpp"

#include "fmt/format.h"

#include <zstd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#ifndef SC_GIT_REV
#define SC_GIT_REV "unknown"
#endif

namespace rl_shard
{
namespace
{

constexpr int ZSTD_SHARD_LEVEL = 3;                     // the level of shard_derive.ZSTD_LEVEL
constexpr std::uint32_t FORMAT = 1u;                    // shard.FORMAT
constexpr std::uint32_t FIGHT_FRAME_MAGIC = 0x31544653u;  // "SFT1" little-endian, shard.FIGHT_FRAME_MAGIC
constexpr std::size_t FIGHT_HEADER_BYTES = 72;          // shard.fight_frame_layout().size
constexpr std::size_t NET_SHA_BYTES = 32;
constexpr std::uint8_t FLAG_EXPLORED = 1;               // shard.FLAG_EXPLORED
constexpr int FIXED_OFFSET = 32768;
constexpr double FIXED_STEPS = 65535.0;
constexpr std::size_t U8_MAX = 255;
constexpr std::size_t U16_MAX = 65535;

// The packed row, in order: obs groups, then the tail (shard.row_fields / quant_table.ROW_TAIL).
constexpr std::size_t OFF_ACTION = RL_QUANT_OBS_BYTES;
constexpr std::size_t OFF_MASK = OFF_ACTION + 1;
constexpr std::size_t OFF_TARGET = OFF_MASK + 4;
constexpr std::size_t OFF_FLAGS = OFF_TARGET + 2;
constexpr std::size_t OFF_REWARD = OFF_FLAGS + 1;
constexpr std::size_t OFF_DAMAGE_TO_END = OFF_REWARD + 4;
constexpr std::size_t OFF_T = OFF_DAMAGE_TO_END + 4;
constexpr std::size_t OFF_T_DECIDED = OFF_T + 4;
static_assert( OFF_T_DECIDED + 4 == RL_SHARD_ROW_BYTES, "the packed row tail must end at RL_SHARD_ROW_BYTES" );

constexpr std::size_t CREDIT_BACKGROUND = 4;  // contract/translog.py CREDIT_NAMES.index("background")
constexpr std::size_t CREDIT_ORPHAN = 5;      // CREDIT_NAMES.index("orphan")
static_assert( rl_credit::STREAM_COUNT == 6u, "the shard's background streams are indices 4 and 5 of the credit block" );

// translog.CONSERVATION_RELATIVE_TOLERANCE
constexpr double CONSERVATION_RELATIVE_TOLERANCE = 1e-9;

struct buffered_row_t
{
  float obs[ RL_OBS_DIM ];
  double damage;       // the row's cumulative damage
  double background;   // credit_real[background] + credit_real[orphan] of the row (float64 sum, as numpy's two-element sum)
  std::uint32_t seq;
  std::uint32_t mask;
  std::uint16_t chosen_target;
  std::uint8_t action;
  std::uint8_t flags;
  float t;
  float t_decided;
};

struct aborted_t
{
  std::size_t entry_index;
  std::string refusal;
};

struct plant_t
{
  bool on = false;
  std::uint32_t row = 0;
  std::size_t slot = 0;
  float value = 0.0f;
};

struct state_t
{
  bool open = false;
  bool entry_active = false;
  std::FILE* file = nullptr;
  std::string path;
  std::uint8_t net_sha256[ NET_SHA_BYTES ] = {};
  std::size_t entry_index = 0;
  std::vector<buffered_row_t> rows;
  std::vector<std::uint32_t> written_entries;
  std::vector<aborted_t> aborted;
  std::uint64_t fights = 0;
  std::uint64_t total_rows = 0;
  plant_t plant;
};

state_t& state()
{
  static state_t s;
  return s;
}

template <typename T>
void put( std::vector<unsigned char>& out, std::size_t offset, T value )
{
  std::memcpy( out.data() + offset, &value, sizeof( T ) );
}

std::string json_escape( const std::string& text )
{
  std::string out;
  for ( const char c : text )
  {
    switch ( c )
    {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ( static_cast<unsigned char>( c ) < 0x20 || static_cast<unsigned char>( c ) > 0x7e )
          out += fmt::format( "\\u{:04x}", static_cast<unsigned>( static_cast<unsigned char>( c ) ) );
        else
          out += c;
    }
  }
  return out;
}

std::string c_hex_float( double value )
{
  if ( std::isnan( value ) )
    return "nan";
  if ( std::isinf( value ) )
    return value > 0 ? "inf" : "-inf";
  char buffer[ 64 ];
  std::snprintf( buffer, sizeof( buffer ), "%a", value );
  return buffer;
}

std::string g17( double value )
{
  char buffer[ 64 ];
  std::snprintf( buffer, sizeof( buffer ), "%.17g", value );
  return buffer;
}

const char* class_name( rl_quant_class cls )
{
  switch ( cls )
  {
    case rl_quant_class::bit: return "bit";
    case rl_quant_class::u8_lattice: return "u8_lattice";
    case rl_quant_class::u16_lattice: return "u16_lattice";
    case rl_quant_class::i16_fixed: return "i16_fixed";
  }
  return "?";
}

std::string basename_of( const std::string& path )
{
  const std::size_t cut = path.find_last_of( '/' );
  return cut == std::string::npos ? path : path.substr( cut + 1 );
}

// One zstd frame of `data` appended to the file and flushed (an unclean kill loses at most the trailing partial frame).
void write_frame( state_t& s, const unsigned char* data, std::size_t n_bytes )
{
  std::vector<unsigned char> compressed( ZSTD_compressBound( n_bytes ) );
  const std::size_t written = ZSTD_compress( compressed.data(), compressed.size(), data, n_bytes, ZSTD_SHARD_LEVEL );
  if ( ZSTD_isError( written ) )
    throw sc_runtime_error( fmt::format( "rl_shard: zstd compression failed: {}", ZSTD_getErrorName( written ) ) );
  if ( std::fwrite( compressed.data(), 1, written, s.file ) != written || std::fflush( s.file ) != 0 )
    throw sc_runtime_error( fmt::format( "rl_shard: write to '{}' failed (disk full, permissions, or similar); refusing to continue silently", s.path ) );
}

// The header frame's canonical JSON: keys sorted, compact separators, ASCII (shard.header_bytes of shard.make_header).
std::string header_json( const std::string& translog_base )
{
  struct field_t
  {
    const char* name;
    const char* dtype;
    std::size_t count;
  };
  std::vector<field_t> fields;
  if ( RL_QUANT_BIT_BYTES > 0 )
    fields.push_back( { "obs_bits", "u1", RL_QUANT_BIT_BYTES } );
  if ( RL_QUANT_U8_COUNT > 0 )
    fields.push_back( { "obs_u8", "u1", RL_QUANT_U8_COUNT } );
  if ( RL_QUANT_U16_COUNT > 0 )
    fields.push_back( { "obs_u16", "<u2", RL_QUANT_U16_COUNT } );
  if ( RL_QUANT_I16_COUNT > 0 )
    fields.push_back( { "obs_i16", "<i2", RL_QUANT_I16_COUNT } );
  fields.push_back( { "action", "u1", 1 } );
  fields.push_back( { "mask", "<u4", 1 } );
  fields.push_back( { "chosen_target_actor_index", "<u2", 1 } );
  fields.push_back( { "flags", "u1", 1 } );
  fields.push_back( { "reward", "<f4", 1 } );
  fields.push_back( { "damage_to_end", "<f4", 1 } );
  fields.push_back( { "t", "<f4", 1 } );
  fields.push_back( { "t_decided", "<f4", 1 } );

  std::string row_fields = "[";
  for ( std::size_t i = 0; i < fields.size(); ++i )
  {
    if ( i > 0 )
      row_fields += ",";
    row_fields += fmt::format( "{{\"count\":{},\"dtype\":\"{}\",\"field\":\"{}\"}}", fields[ i ].count, fields[ i ].dtype, fields[ i ].name );
  }
  row_fields += "]";

  const std::string fight_frame = fmt::format(
      "{{\"fields\":[{{\"count\":1,\"dtype\":\"<u4\",\"field\":\"frame_magic\"}},{{\"count\":1,\"dtype\":\"<u4\",\"field\":\"entry_index\"}},"
      "{{\"count\":1,\"dtype\":\"<u4\",\"field\":\"shape\"}},{{\"count\":1,\"dtype\":\"<u4\",\"field\":\"setup\"}},"
      "{{\"count\":1,\"dtype\":\"<u4\",\"field\":\"stat\"}},{{\"count\":1,\"dtype\":\"<u4\",\"field\":\"slot\"}},"
      "{{\"count\":{},\"dtype\":\"u1\",\"field\":\"net_sha256\"}},{{\"count\":1,\"dtype\":\"<u4\",\"field\":\"rows\"}},"
      "{{\"count\":1,\"dtype\":\"<f8\",\"field\":\"final_damage\"}},{{\"count\":1,\"dtype\":\"<f4\",\"field\":\"fight_length\"}}],\"magic\":{},\"size\":{}}}",
      NET_SHA_BYTES, FIGHT_FRAME_MAGIC, FIGHT_HEADER_BYTES );

  return fmt::format(
      "{{\"action_dim\":{},\"binary_banner\":\"{}\",\"fight_frame\":{},\"format\":{},\"obs_fingerprint\":\"{}\",\"row_fields\":{},\"row_size\":{},"
      "\"spec_id\":\"{}\",\"table_sha256\":\"{}\",\"translog_base\":\"{}\",\"width\":{}}}",
      RL_ACTION_DIM, json_escape( SC_GIT_REV ), fight_frame, FORMAT, json_escape( RL_OBS_SCHEMA_SHA ), row_fields, RL_SHARD_ROW_BYTES,
      json_escape( RL_REGISTRY_ID ), json_escape( RL_QUANT_TABLE_SHA256 ), json_escape( translog_base ), RL_OBS_DIM );
}

// The first slot of `obs` (slot order) outside its class, or false. On true `refusal` is the RL_SHARD_OUT_OF_CLASS line.
// Encoding, identically in shard.Codec.encode_observation: bit v == 0 or 1; lattice k = rint(double(v) * div), 0 <= k <= max, lut[k] bitwise equal; fixed v finite in
// [lo, hi], q = rint((double(v) - lo) * inv_step), stored int16 = q - 32768. Writes the packed observation to `out` (RL_QUANT_OBS_BYTES, zeroed by the caller).
bool encode_observation( const float* obs, unsigned char* out, std::string& refusal )
{
  for ( std::size_t k = 0; k < RL_OBS_DIM; ++k )
  {
    const rl_quant_slot& slot = RL_QUANT_SLOTS[ k ];
    const float v = obs[ k ];
    bool ok = false;
    std::string range;
    switch ( slot.cls )
    {
      case rl_quant_class::bit:
      {
        if ( v == 0.0f )
          ok = true;
        else if ( v == 1.0f )
        {
          out[ slot.column / 8 ] = static_cast<unsigned char>( out[ slot.column / 8 ] | ( 1u << ( slot.column % 8 ) ) );
          ok = true;
        }
        range = "[0,1]";
        break;
      }
      case rl_quant_class::u8_lattice:
      case rl_quant_class::u16_lattice:
      {
        const bool wide = slot.cls == rl_quant_class::u16_lattice;
        const std::size_t maximum = wide ? U16_MAX : U8_MAX;
        const double x = static_cast<double>( v );
        if ( std::isfinite( x ) )
        {
          const double code = std::nearbyint( x * slot.div );
          if ( code >= 0.0 && code <= static_cast<double>( maximum ) )
          {
            const std::size_t index = static_cast<std::size_t>( code );
            const float decoded = wide ? RL_QUANT_U16_LUT[ slot.column ][ index ] : RL_QUANT_U8_LUT[ slot.column ][ index ];
            std::uint32_t bits_decoded = 0, bits_value = 0;
            std::memcpy( &bits_decoded, &decoded, 4 );
            std::memcpy( &bits_value, &v, 4 );
            if ( bits_decoded == bits_value )
            {
              ok = true;
              if ( wide )
              {
                const std::uint16_t word = static_cast<std::uint16_t>( index );
                std::memcpy( out + RL_QUANT_U16_OFFSET + 2 * slot.column, &word, 2 );
              }
              else
                out[ RL_QUANT_U8_OFFSET + slot.column ] = static_cast<unsigned char>( index );
            }
          }
        }
        range = wide ? "[0,65535]" : "[0,255]";
        break;
      }
      case rl_quant_class::i16_fixed:
      {
        const double x = static_cast<double>( v );
        if ( std::isfinite( x ) && x >= slot.lo && x <= slot.hi )
        {
          const double q = std::nearbyint( ( x - slot.lo ) * slot.inv_step );
          if ( q >= 0.0 && q <= FIXED_STEPS )
          {
            const std::int16_t stored = static_cast<std::int16_t>( static_cast<int>( q ) - FIXED_OFFSET );
            std::memcpy( out + RL_QUANT_I16_OFFSET + 2 * slot.column, &stored, 2 );
            ok = true;
          }
        }
        range = "[" + g17( slot.lo ) + "," + g17( slot.hi ) + "]";
        break;
      }
    }
    if ( !ok )
    {
      refusal = fmt::format( "RL_SHARD_OUT_OF_CLASS slot={}:{} value={} class={} range={}", k, RL_OBS_NAMES[ k ], c_hex_float( static_cast<double>( v ) ), class_name( slot.cls ), range );
      return false;
    }
  }
  return true;
}

void abort_entry( state_t& s, const std::string& refusal )
{
  s.aborted.push_back( { s.entry_index, refusal } );
  fmt::print( stderr, "[RL_SHARD] entry={} aborted: {}\n", s.entry_index, refusal );
  std::fflush( stderr );
  s.rows.clear();
}

}  // namespace

void open( const std::string& path, const std::string& translog_path, const std::uint8_t net_sha256[ 32 ] )
{
  state_t& s = state();
  if ( s.open )
    throw sc_runtime_error( "rl_shard: open refused -- a shard is already open in this process" );
  s.file = std::fopen( path.c_str(), "wb" );
  if ( s.file == nullptr )
    throw sc_runtime_error( fmt::format( "rl_shard: could not open '{}' for writing", path ) );
  s.path = path;
  std::memcpy( s.net_sha256, net_sha256, NET_SHA_BYTES );
  s.open = true;
  s.fights = 0;
  s.total_rows = 0;
  s.written_entries.clear();
  s.aborted.clear();
  const std::string header = header_json( basename_of( translog_path ) );
  write_frame( s, reinterpret_cast<const unsigned char*>( header.data() ), header.size() );
}

bool enabled()
{
  return state().open;
}

void begin_entry( const sim_t* sim, std::size_t entry_index )
{
  state_t& s = state();
  s.rows.clear();
  s.entry_index = entry_index;
  s.entry_active = s.open && !sim->rl_list_warmup;
  s.plant = plant_t{};
  if ( !s.open || sim->rl_shard_plant_str.empty() )
    return;
  // <row>:<slot>:<value>
  const std::string& text = sim->rl_shard_plant_str;
  const std::size_t first = text.find( ':' );
  const std::size_t second = first == std::string::npos ? std::string::npos : text.find( ':', first + 1 );
  if ( first == std::string::npos || second == std::string::npos )
    throw sc_runtime_error( fmt::format( "rl_shard_plant='{}' must be <row>:<slot>:<value>", text ) );
  char* end = nullptr;
  const unsigned long row = std::strtoul( text.substr( 0, first ).c_str(), &end, 10 );
  const unsigned long slot = std::strtoul( text.substr( first + 1, second - first - 1 ).c_str(), &end, 10 );
  const double value = std::strtod( text.substr( second + 1 ).c_str(), &end );
  if ( slot >= RL_OBS_DIM )
    throw sc_runtime_error( fmt::format( "rl_shard_plant='{}': slot {} is outside the observation (width {})", text, slot, RL_OBS_DIM ) );
  s.plant.on = true;
  s.plant.row = static_cast<std::uint32_t>( row );
  s.plant.slot = static_cast<std::size_t>( slot );
  s.plant.value = static_cast<float>( value );
}

bool active()
{
  return state().entry_active;
}

const rl_translog::decision_record& plant( const rl_translog::decision_record& row, std::uint32_t row_index, rl_translog::decision_record& scratch )
{
  const state_t& s = state();
  if ( !s.plant.on || s.plant.row != row_index )
    return row;
  scratch = row;
  scratch.obs[ s.plant.slot ] = s.plant.value;
  return scratch;
}

void add_row( const rl_translog::decision_record& row )
{
  state_t& s = state();
  buffered_row_t b;
  std::memcpy( b.obs, row.obs, sizeof( b.obs ) );
  b.damage = row.damage;
  b.background = row.credit_real[ CREDIT_BACKGROUND ] + row.credit_real[ CREDIT_ORPHAN ];
  b.seq = row.seq;
  b.mask = row.mask;
  b.chosen_target = row.chosen_target_actor_index;
  b.action = row.action;
  b.flags = ( row.flags & rl_translog::FLAG_EXPLORATORY ) != 0 ? FLAG_EXPLORED : static_cast<std::uint8_t>( 0 );
  b.t = row.t;
  b.t_decided = row.t_decided;
  s.rows.push_back( b );
}

void close_fight( const sim_t*, const player_t* p, const rl_translog::close_record& close )
{
  state_t& s = state();
  if ( !s.entry_active )
    return;
  const std::size_t n = s.rows.size();
  if ( n == 0 )
  {
    abort_entry( s, fmt::format( "RL_SHARD_REWARD_INPUT entry={} no decision rows", s.entry_index ) );
    return;
  }
  // Rows in seq order (shard_derive.derive_fights: a stable argsort of the file-order rows by seq).
  std::stable_sort( s.rows.begin(), s.rows.end(), []( const buffered_row_t& a, const buffered_row_t& b ) { return a.seq < b.seq; } );

  // The per-cause reward, translog.per_cause_rewards in its operation order.
  const double final_damage = close.final_damage_total;
  for ( std::size_t k = 0; k + 1 < n; ++k )
  {
    if ( s.rows[ k + 1 ].damage < s.rows[ k ].damage )
    {
      abort_entry( s, fmt::format( "RL_SHARD_REWARD_INPUT entry={} boundary-damage readings are not monotone non-decreasing at index {}: readings[{}]={} > readings[{}]={}", s.entry_index,
                                   k, k, g17( s.rows[ k ].damage ), k + 1, g17( s.rows[ k + 1 ].damage ) ) );
      return;
    }
  }
  const double last_reading = s.rows[ n - 1 ].damage;
  if ( last_reading > final_damage * ( 1.0 + 0.0 ) )
  {
    abort_entry( s, fmt::format( "RL_SHARD_REWARD_INPUT entry={} the last boundary-damage reading ({}) exceeds compound_dmg ({})", s.entry_index, g17( last_reading ), g17( final_damage ) ) );
    return;
  }
  const double prefix = s.rows[ 0 ].damage;
  const double close_background = close.credit_real[ CREDIT_BACKGROUND ] + close.credit_real[ CREDIT_ORPHAN ];
  const double falls_below = -1e-6 * std::max( 1.0, final_damage );
  std::vector<double> reward64( n );
  double reward_sum = 0.0;
  for ( std::size_t k = 0; k < n; ++k )
  {
    const std::uint64_t seq = s.rows[ k ].seq;
    const std::size_t idx = static_cast<std::size_t>( seq - p->rl_fight_first_seq );
    const double own = idx < p->rl_own_real.size() ? p->rl_own_real[ idx ] : 0.0;
    const double next = k + 1 < n ? s.rows[ k + 1 ].background : close_background;
    double background = next - s.rows[ k ].background;
    if ( background < falls_below )
    {
      abort_entry( s, fmt::format( "RL_SHARD_REWARD_INPUT entry={} the background credit falls by {} at row {}", s.entry_index, g17( -background ), k ) );
      return;
    }
    background = std::max( background, 0.0 );
    reward64[ k ] = own + background;
    reward_sum += reward64[ k ];
  }
  const double error = reward_sum + prefix - final_damage;
  if ( std::fabs( error ) > CONSERVATION_RELATIVE_TOLERANCE * std::max( std::fabs( final_damage ), 1.0 ) )
  {
    abort_entry( s, fmt::format( "RL_SHARD_REWARD_CONSERVATION entry={} sum(reward) {} + prefix {} differs from final_damage_total {} by {}", s.entry_index, g17( reward_sum ), g17( prefix ),
                                 g17( final_damage ), g17( error ) ) );
    return;
  }

  // The observation, packed. The first row/slot outside its class refuses the entry.
  std::vector<unsigned char> frame( FIGHT_HEADER_BYTES + n * RL_SHARD_ROW_BYTES, 0 );
  for ( std::size_t k = 0; k < n; ++k )
  {
    unsigned char* row = frame.data() + FIGHT_HEADER_BYTES + k * RL_SHARD_ROW_BYTES;
    std::string refusal;
    if ( !encode_observation( s.rows[ k ].obs, row, refusal ) )
    {
      abort_entry( s, refusal );
      return;
    }
  }

  // damage_to_end: the sequential reverse float64 sum (numpy cumsum on the reversed array), then float32.
  double running = 0.0;
  for ( std::size_t k = n; k-- > 0; )
  {
    running = k + 1 == n ? reward64[ k ] : running + reward64[ k ];
    unsigned char* row = frame.data() + FIGHT_HEADER_BYTES + k * RL_SHARD_ROW_BYTES;
    row[ OFF_ACTION ] = s.rows[ k ].action;
    put<std::uint32_t>( frame, FIGHT_HEADER_BYTES + k * RL_SHARD_ROW_BYTES + OFF_MASK, s.rows[ k ].mask );
    put<std::uint16_t>( frame, FIGHT_HEADER_BYTES + k * RL_SHARD_ROW_BYTES + OFF_TARGET, s.rows[ k ].chosen_target );
    row[ OFF_FLAGS ] = s.rows[ k ].flags;
    put<float>( frame, FIGHT_HEADER_BYTES + k * RL_SHARD_ROW_BYTES + OFF_REWARD, static_cast<float>( reward64[ k ] ) );
    put<float>( frame, FIGHT_HEADER_BYTES + k * RL_SHARD_ROW_BYTES + OFF_DAMAGE_TO_END, static_cast<float>( running ) );
    put<float>( frame, FIGHT_HEADER_BYTES + k * RL_SHARD_ROW_BYTES + OFF_T, s.rows[ k ].t );
    put<float>( frame, FIGHT_HEADER_BYTES + k * RL_SHARD_ROW_BYTES + OFF_T_DECIDED, s.rows[ k ].t_decided );
  }

  // The fight header (shard.fight_payload): magic, entry_index, shape, setup, stat, slot, net_sha256, rows, final_damage, fight_length.
  put<std::uint32_t>( frame, 0, FIGHT_FRAME_MAGIC );
  put<std::uint32_t>( frame, 4, static_cast<std::uint32_t>( close.iteration ) );
  put<std::uint32_t>( frame, 8, close.fight_tag[ 1 ] );
  put<std::uint32_t>( frame, 12, close.fight_tag[ 2 ] );
  put<std::uint32_t>( frame, 16, close.fight_tag[ 3 ] );
  put<std::uint32_t>( frame, 20, close.fight_tag[ 4 ] );
  std::memcpy( frame.data() + 24, s.net_sha256, NET_SHA_BYTES );
  put<std::uint32_t>( frame, 56, static_cast<std::uint32_t>( n ) );
  put<double>( frame, 60, final_damage );
  put<float>( frame, 68, close.fight_length );

  write_frame( s, frame.data(), frame.size() );
  s.written_entries.push_back( static_cast<std::uint32_t>( close.iteration ) );
  ++s.fights;
  s.total_rows += n;
  s.rows.clear();
}

void finish()
{
  state_t& s = state();
  if ( !s.open )
    return;
  std::string entries = "[";
  for ( std::size_t i = 0; i < s.written_entries.size(); ++i )
    entries += ( i > 0 ? "," : "" ) + std::to_string( s.written_entries[ i ] );
  entries += "]";
  std::string aborted = "[";
  for ( std::size_t i = 0; i < s.aborted.size(); ++i )
    aborted += fmt::format( "{}{{\"entry_index\":{},\"refusal\":\"{}\"}}", i > 0 ? "," : "", s.aborted[ i ].entry_index, json_escape( s.aborted[ i ].refusal ) );
  aborted += "]";
  const std::string close = fmt::format( "{{\"aborted_entries\":{},\"entry_indices\":{},\"fights\":{},\"rows\":{}}}", aborted, entries, s.fights, s.total_rows );
  write_frame( s, reinterpret_cast<const unsigned char*>( close.data() ), close.size() );
  std::fclose( s.file );
  s.file = nullptr;
  s.open = false;
  s.entry_active = false;
}

}  // namespace rl_shard
