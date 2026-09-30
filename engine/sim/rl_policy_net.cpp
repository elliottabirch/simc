// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// In-process RL transport, weights loader + forward (three body types) +
// masked argmax -- tstl-sylvanas phase 210, plan 210-04; widened to RLW1 v2
// (network.body: mlp/dueling/ln-dueling) at Phase 213-TDL Task 1; widened
// again at Phase 222 (NET-01/NET-02) to an ARBITRARY depth (2-4 hidden
// layers, any width 128-512, widths >=16 tolerated) via a single derived
// layer-split rule (hidden_layer_count(), rl_policy.hpp), a legal-only
// dueling mean with a mirrored zero-legal fallback, and RLW1 v3's optional
// trailing input-gather + action-allow-list section. `load_rlw1` parses the
// RLW1 v2/v3 binary layout implemented on the Python side by
// scripts/rl/rlw1.py -- this loader refuses on the same conditions that
// reader refuses on (bad magic, short file, unknown version, unknown body,
// out-of-range dims, a body/layer-shape mismatch, truncated payload, an
// out-of-range or non-increasing input gather, an all-disallowed action
// set, unexpected trailing bytes), each with its own named error,
// little-endian throughout (this format is a portable on-disk layout, not
// an in-memory dump; declared explicitly here via raw byte reads on an
// x86-64/ARM64 little-endian host, matching the Python writer's explicit
// `<` struct format).
//
// RLW1 v1 is RETIRED -- a v1 file's format_version field (1) does not fall
// in the {2, 3} accepted set, so it is refused BY NAME (naming both the
// found version and the accepted set), never misread as v2/v3. v2 is still
// fully supported (reads as identity gather + all-allowed); v3 is the only
// version this reader's Python counterpart writes (scripts/rl/rlw1.py).
//
// masked_argmax reproduces tianshou 2.x's DQN.compute_q_value formula
// verbatim -- RE-VERIFIED AT SOURCE this session (A6), not merely cited
// from RESEARCH's MEDIUM-confidence citation:
//
//   scripts/rl/.venv/lib/python3.14/site-packages/tianshou/algorithm/modelfree/dqn.py:145-151
//
//     def compute_q_value(self, logits, mask):
//         if mask is not None:
//             # the masked q value should be smaller than logits.min()
//             min_value = logits.min() - logits.max() - 1.0
//             logits = logits + to_torch_as(1 - mask, logits) * min_value
//         return logits
//
//   ...then `q.argmax(dim=1)` (dqn.py:141) -- torch.argmax returns the
//   FIRST index on a tie.
//
// forward()'s dueling recombination is Phase 222 NET-02's legal-only mean:
// Q = V + A - mean_legal(A), mean over actions whose mask[] byte is
// non-zero, computed BEFORE masked_argmax's own mask+argmax step ever
// runs, falling back to the mean over EVERY action when zero actions are
// legal (mirrored exactly against scripts/rl/agent/network_bodies.py's own
// DuelingBody.forward -- a background decision boundary can produce an
// all-illegal mask, since `wait` is foreground-only). ln-dueling's
// LayerNorm (eps=1e-5, torch's own default, applied after each hidden
// linear, before ReLU, per-decision statistics only -- see
// scripts/rl/agent/network_bodies.py's own docstring for the plain-language
// explanation and why per-decision-not-batch statistics is what lets this
// C++ reproduce the training-side torch computation bit-for-bit) are this
// engine's own implementation of that same Python module's `DuelingBody`.

#include "sim/rl_policy.hpp"

#include "fmt/format.h"
#include "util/util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <utility>

namespace rl_policy
{
// NET-01 (Phase 222): the ONE derived layer-split rule for this language,
// declared in rl_policy.hpp and defined here so load_rlw1, forward() (both
// below) AND an out-of-file caller holding only a loaded rl_weights_t (the
// rl_forward_probe sim option, sc_main.cpp) all share it -- never a third,
// independently re-derived copy (222-RESEARCH.md "Anti-Patterns to Avoid").
std::size_t hidden_layer_count( rl_body_type body, std::size_t n_layers )
{
  return body == rl_body_type::mlp ? n_layers - 1 : n_layers - 2;
}

const char* body_name( rl_body_type body )
{
  switch ( body )
  {
    case rl_body_type::mlp: return "mlp";
    case rl_body_type::dueling: return "dueling";
    case rl_body_type::ln_dueling: return "ln-dueling";
    case rl_body_type::grouped_ln_dueling: return "grouped-ln-dueling";
  }
  return "unknown";
}

namespace
{
constexpr std::size_t RLW1_HEADER_BYTES = 264;
constexpr std::size_t RLW1_SHA_FIELD_BYTES = 80;
// NET-01 (Phase 222, arm subsets): {2, 3} accepted -- widening the OLD
// equality check (format_version != 2) to a SET is what lets every
// existing v2 blob keep loading (as identity gather + all-allowed) while
// the Python writer moves to v3 (scripts/rl/rlw1.py). Bumping this to an
// equality on 3 instead would refuse every committed v2 fixture by name
// (222-RESEARCH.md Pitfall 15).
constexpr std::uint32_t RLW1_MIN_SUPPORTED_FORMAT_VERSION = 2;
// Phase 230-02 (SCOR-01, R-A): 3 -> 4 -- widening the accepted SET (never narrowing it), exactly
// as the 2->3 widening did at Phase 222 (Pitfall 15's own lesson) -- every existing v2/v3 blob
// keeps loading unchanged; a v4 blob additionally carries the mandatory trailing scorer section
// below.
// 246.1-05 (CAP-01/CAP-02): 4 -> 5 -- widening the accepted SET, never narrowing it (the same
// Pitfall-15 lesson every prior bump here has followed). Format 5 additionally carries the
// mandatory trailing STRUCTURE section below, body 3 (grouped-ln-dueling) ONLY -- no scorer
// section exists at v5 (SCOR-01's v4 scorer and this body's STRUCTURE section never coexist).
// 259-05b (owner Q6): 5 -> 6 -- widening the accepted SET again. Format 4 (the retired v4 SCORER
// section) stays INSIDE the contiguous range 2..6 but is refused by name below: a contiguous
// range cannot express "all but 4", so the refusal is an explicit check at the version gate.
// Format 6 = format 5's STRUCTURE section (body 3 only) followed by the AIM section.
constexpr std::uint32_t RLW1_MAX_SUPPORTED_FORMAT_VERSION = 6;
constexpr std::uint32_t RLW1_RETIRED_SCORER_FORMAT_VERSION = 4;
constexpr std::uint32_t RLW1_AIM_FORMAT_VERSION = 6;
constexpr std::uint32_t RLW1_STRUCTURE_FORMAT_VERSION = 5;
constexpr std::uint32_t RLW1_MAX_LAYERS = 16;
constexpr std::uint32_t RLW1_MIN_FEATURES = 1;
constexpr std::uint32_t RLW1_MAX_FEATURES = 4096;
constexpr float RL_LAYER_NORM_EPS = 1e-5f;  // torch's own nn.LayerNorm default, pinned explicitly

// 259-05b -- v6 AIM section bounds, mirroring scripts/rl/rlw1.py's own aim-section limits
// byte-for-byte (the layout is fixed in 259-02-PLAN.md): a format-level range the loader checks
// BEFORE comparing against the compiled tables, so a garbage value is refused as out of range and
// a well-formed-but-different value is refused as a mismatch, each by name.
constexpr std::uint32_t RLW1_MIN_AIM_LAYERS = 2;   // 0 is a dial-only section; 1 is refused
constexpr std::uint32_t RLW1_MAX_AIM_LAYERS = RLW1_MAX_LAYERS;
constexpr std::uint32_t RLW1_MIN_AIM_SLOTS = 1;
constexpr std::uint32_t RLW1_MAX_AIM_SLOTS = 64;
constexpr std::uint32_t RLW1_MIN_AIM_INPUTS = 1;
constexpr std::uint32_t RLW1_MAX_AIM_INPUTS = 256;
constexpr std::uint32_t RLW1_MIN_AIM_SPELLS = 1;
constexpr std::uint32_t RLW1_MAX_AIM_SPELLS = 64;
// aim_present(4) + n_aim_layers(4) + exploration(4) + slots(4) + input_count(4) + spell_count(4)
// + obs_source(4) + registry_sha(80) = 108 bytes before the aim layers.
constexpr std::size_t RLW1_AIM_FIXED_BYTES = 108;

// 246.1-05 (CAP-01/CAP-02) -- v5 STRUCTURE section bounds, mirroring scripts/rl/rlw1.py's own
// _MAX_GATED/_MAX_BLOCKS/_MAX_BLOCK_SLOTS/_MAX_SPELL_REPEATS/_MAX_SPELL_FACTS/_MAX_PASSTHROUGH
// constants byte-for-byte -- format-level constants, not derived from a live registry.
constexpr std::uint32_t RLW1_MAX_GATED = 4096;
constexpr std::uint32_t RLW1_MAX_BLOCKS = 64;
constexpr std::uint32_t RLW1_MAX_BLOCK_SLOTS = 4096;
constexpr std::uint32_t RLW1_MAX_SPELL_REPEATS = 64;
constexpr std::uint32_t RLW1_MAX_SPELL_FACTS = 256;
constexpr std::uint32_t RLW1_MAX_PASSTHROUGH = 4096;

std::string decode_fingerprint( const unsigned char* p )
{
  // Strip trailing NUL padding, mirroring rlw1.py's _decode_fingerprint.
  const char* c = reinterpret_cast<const char*>( p );
  std::size_t len = 0;
  while ( len < RLW1_SHA_FIELD_BYTES && c[ len ] != '\0' )
    ++len;
  return std::string( c, len );
}

rl_body_type decode_body_type( std::uint32_t raw, const std::string& path )
{
  switch ( raw )
  {
    case 0: return rl_body_type::mlp;
    case 1: return rl_body_type::dueling;
    case 2: return rl_body_type::ln_dueling;
    case 3: return rl_body_type::grouped_ln_dueling;
    default:
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' declares body_type={}, must be 0 (mlp), 1 (dueling), 2 "
          "(ln-dueling), or 3 (grouped-ln-dueling)",
          path, raw ) );
  }
}

// NET-01 (Phase 222): a MINIMUM layer count per body (mlp >= 2, dueling and
// ln-dueling >= 3), plus the existing hard upper bound RLW1_MAX_LAYERS
// (enforced separately, at n_layers parse time, below) -- replaces the OLD
// exact-count rule (mlp==3, dueling/ln-dueling==4), which was depth-2's own
// special case. A 3-layer mlp or a 4-layer dueling/ln-dueling blob is that
// depth-2 special case, so every pre-222 blob stays readable.
std::size_t expected_layer_count( rl_body_type body )
{
  return body == rl_body_type::mlp ? 2 : 3;
}

// Phase 230-02 (SCOR-01, T-230-01-01), reused by the v6 aim section (259-05b): an auxiliary
// section's own per-layer parse -- the SAME
// per-layer wire encoding the main layer loop in load_rlw1 below uses (uint32 in_features,
// uint32 out_features, uint32 has_ln, then weight/bias[, gamma/beta]), factored out here so the
// scorer's layer list is read by ONE parser rather than a second hand-typed copy that could drift
// from the main loop's own discipline (positive length check FIRST, then the read -- T-210-04).
// `label` (e.g. "scorer layer 0") is used only in the thrown messages, mirroring rlw1.py's own
// `_unpack_one_layer(..., label=...)`.
rl_layer parse_one_layer( const std::vector<unsigned char>& data, std::size_t& offset,
                           const std::string& path, const std::string& label )
{
  if ( offset + 12 > data.size() )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' truncated -- {}'s dimensions header needs 12 bytes at "
        "offset {}, only {} remain",
        path, label, offset, data.size() - offset ) );
  }

  std::uint32_t in_features = 0, out_features = 0, has_ln_raw = 0;
  std::memcpy( &in_features, data.data() + offset, 4 );
  std::memcpy( &out_features, data.data() + offset + 4, 4 );
  std::memcpy( &has_ln_raw, data.data() + offset + 8, 4 );

  if ( in_features < RLW1_MIN_FEATURES || in_features > RLW1_MAX_FEATURES )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' {} declares in_features={}, must be {}..{}",
        path, label, in_features, RLW1_MIN_FEATURES, RLW1_MAX_FEATURES ) );
  }
  if ( out_features < RLW1_MIN_FEATURES || out_features > RLW1_MAX_FEATURES )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' {} declares out_features={}, must be {}..{}",
        path, label, out_features, RLW1_MIN_FEATURES, RLW1_MAX_FEATURES ) );
  }
  if ( has_ln_raw != 0 && has_ln_raw != 1 )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' {} declares has_ln={}, must be 0 or 1",
        path, label, has_ln_raw ) );
  }
  const bool has_ln = has_ln_raw != 0;
  offset += 12;

  const std::uint64_t weight_count =
      static_cast<std::uint64_t>( out_features ) * static_cast<std::uint64_t>( in_features );
  const std::uint64_t weight_bytes = weight_count * 4;
  const std::uint64_t bias_bytes = static_cast<std::uint64_t>( out_features ) * 4;
  const std::uint64_t ln_bytes = has_ln ? bias_bytes * 2 : 0;
  const std::uint64_t needed = weight_bytes + bias_bytes + ln_bytes;

  if ( offset + needed > data.size() )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' truncated mid-layer -- {} ({}x{}, has_ln={}) needs {} "
        "bytes of payload at offset {}, but the file is short by {} bytes",
        path, label, in_features, out_features, has_ln, needed, offset, ( offset + needed ) - data.size() ) );
  }

  rl_layer layer;
  layer.in_features = in_features;
  layer.out_features = out_features;
  layer.has_ln = has_ln;
  layer.weight.resize( weight_count );
  std::memcpy( layer.weight.data(), data.data() + offset, static_cast<std::size_t>( weight_bytes ) );
  offset += static_cast<std::size_t>( weight_bytes );
  layer.bias.resize( out_features );
  std::memcpy( layer.bias.data(), data.data() + offset, static_cast<std::size_t>( bias_bytes ) );
  offset += static_cast<std::size_t>( bias_bytes );
  if ( has_ln )
  {
    layer.gamma.resize( out_features );
    std::memcpy( layer.gamma.data(), data.data() + offset, static_cast<std::size_t>( bias_bytes ) );
    offset += static_cast<std::size_t>( bias_bytes );
    layer.beta.resize( out_features );
    std::memcpy( layer.beta.data(), data.data() + offset, static_cast<std::size_t>( bias_bytes ) );
    offset += static_cast<std::size_t>( bias_bytes );
  }
  return layer;
}

// 246.1-05 (CAP-01/CAP-02): the raw, not-yet-cross-checked v5 STRUCTURE section -- plain local
// data, assembled into rl_grouped_layout_t only after the caller has validated it against
// raw_layers (parse_structure_section itself is oblivious to the layer list).
struct raw_structure_t
{
  std::uint32_t n_obs = 0;
  std::string   layout_sha;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> gated;
  std::vector<std::vector<std::uint32_t>>              blocks;
  std::vector<std::vector<std::uint32_t>>              per_spell;
  std::vector<std::uint32_t>                           passthrough;
};

// Reads the v5 STRUCTURE section at `offset` (advanced in place), mirroring scripts/rl/rlw1.py's
// `_unpack_structure` exactly: a positive length check for EVERY sized read before the read
// itself (T-210-04), never a try/catch around an over-read. Layout: u32 n_obs; char[80]
// layout_sha; u32 n_gated + n_gated x (u32 slot, u32 gate_slot); u32 G + per block (u32 n_slots +
// n_slots x u32 slot); u32 n_repeats, u32 n_facts + n_repeats x n_facts x u32 slot; u32
// n_passthrough + n_passthrough x u32 slot.
raw_structure_t parse_structure_section( const std::vector<unsigned char>& data, std::size_t& offset,
                                          const std::string& path )
{
  auto need = [ & ]( std::size_t n, const char* what ) {
    if ( offset + n > data.size() )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' truncated -- v5 structure {} needs {} bytes at offset "
          "{}, only {} remain",
          path, what, n, offset, data.size() - offset ) );
    }
  };

  raw_structure_t s;

  need( 4, "n_obs" );
  std::memcpy( &s.n_obs, data.data() + offset, 4 );
  offset += 4;

  need( RLW1_SHA_FIELD_BYTES, "layout_sha" );
  s.layout_sha = decode_fingerprint( data.data() + offset );
  offset += RLW1_SHA_FIELD_BYTES;

  need( 4, "n_gated" );
  std::uint32_t n_gated = 0;
  std::memcpy( &n_gated, data.data() + offset, 4 );
  offset += 4;
  if ( n_gated > RLW1_MAX_GATED )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' declares n_gated={}, must be <= {}", path, n_gated,
        RLW1_MAX_GATED ) );
  }
  need( static_cast<std::size_t>( n_gated ) * 8, "gated pairs" );
  std::uint32_t prev_slot_plus_one = 0;   // slot is unsigned -- track "prev_slot + 1" to detect
                                           // slot <= prev_slot without an initial -1 sentinel
  bool first_gated = true;
  for ( std::uint32_t i = 0; i < n_gated; ++i )
  {
    std::uint32_t slot = 0, gate_slot = 0;
    std::memcpy( &slot, data.data() + offset, 4 );
    std::memcpy( &gate_slot, data.data() + offset + 4, 4 );
    offset += 8;
    if ( !first_gated && slot < prev_slot_plus_one )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' structure gated slots are not strictly increasing at "
          "slot={}",
          path, slot ) );
    }
    first_gated = false;
    prev_slot_plus_one = slot + 1;
    if ( slot >= s.n_obs || gate_slot >= s.n_obs )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' structure gated pair ({}, {}) has a slot >= n_obs={}",
          path, slot, gate_slot, s.n_obs ) );
    }
    s.gated.emplace_back( slot, gate_slot );
  }

  need( 4, "G" );
  std::uint32_t n_blocks = 0;
  std::memcpy( &n_blocks, data.data() + offset, 4 );
  offset += 4;
  if ( n_blocks < 1 || n_blocks > RLW1_MAX_BLOCKS )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' declares G={} blocks, must be 1..{}", path, n_blocks,
        RLW1_MAX_BLOCKS ) );
  }
  for ( std::uint32_t bi = 0; bi < n_blocks; ++bi )
  {
    need( 4, "block n_slots" );
    std::uint32_t n_slots = 0;
    std::memcpy( &n_slots, data.data() + offset, 4 );
    offset += 4;
    if ( n_slots == 0 || n_slots > RLW1_MAX_BLOCK_SLOTS )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' block {} declares n_slots={}, must be 1..{}", path, bi,
          n_slots, RLW1_MAX_BLOCK_SLOTS ) );
    }
    need( static_cast<std::size_t>( n_slots ) * 4, "block slots" );
    std::vector<std::uint32_t> slots( n_slots );
    for ( std::uint32_t si = 0; si < n_slots; ++si )
    {
      std::memcpy( &slots[ si ], data.data() + offset, 4 );
      offset += 4;
      if ( slots[ si ] >= s.n_obs )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy::load_rlw1: file '{}' block {} slot {} >= n_obs={}", path, bi, slots[ si ],
            s.n_obs ) );
      }
    }
    s.blocks.push_back( std::move( slots ) );
  }

  need( 8, "n_repeats/n_facts" );
  std::uint32_t n_repeats = 0, n_facts = 0;
  std::memcpy( &n_repeats, data.data() + offset, 4 );
  std::memcpy( &n_facts, data.data() + offset + 4, 4 );
  offset += 8;
  if ( n_repeats > RLW1_MAX_SPELL_REPEATS )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' declares n_repeats={}, must be <= {}", path, n_repeats,
        RLW1_MAX_SPELL_REPEATS ) );
  }
  if ( n_facts > RLW1_MAX_SPELL_FACTS )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' declares n_facts={}, must be <= {}", path, n_facts,
        RLW1_MAX_SPELL_FACTS ) );
  }
  for ( std::uint32_t ri = 0; ri < n_repeats; ++ri )
  {
    need( static_cast<std::size_t>( n_facts ) * 4, "per_spell row" );
    std::vector<std::uint32_t> row( n_facts );
    for ( std::uint32_t fi = 0; fi < n_facts; ++fi )
    {
      std::memcpy( &row[ fi ], data.data() + offset, 4 );
      offset += 4;
      if ( row[ fi ] >= s.n_obs )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy::load_rlw1: file '{}' per_spell row {} slot {} >= n_obs={}", path, ri,
            row[ fi ], s.n_obs ) );
      }
    }
    s.per_spell.push_back( std::move( row ) );
  }

  need( 4, "n_passthrough" );
  std::uint32_t n_passthrough = 0;
  std::memcpy( &n_passthrough, data.data() + offset, 4 );
  offset += 4;
  if ( n_passthrough > RLW1_MAX_PASSTHROUGH )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' declares n_passthrough={}, must be <= {}", path,
        n_passthrough, RLW1_MAX_PASSTHROUGH ) );
  }
  need( static_cast<std::size_t>( n_passthrough ) * 4, "passthrough slots" );
  s.passthrough.resize( n_passthrough );
  for ( std::uint32_t pi = 0; pi < n_passthrough; ++pi )
  {
    std::memcpy( &s.passthrough[ pi ], data.data() + offset, 4 );
    offset += 4;
    if ( s.passthrough[ pi ] >= s.n_obs )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' passthrough slot {} >= n_obs={}", path,
          s.passthrough[ pi ], s.n_obs ) );
    }
  }

  const std::set<std::uint32_t> passthrough_set( s.passthrough.begin(), s.passthrough.end() );
  for ( const auto& [ slot, gate_slot ] : s.gated )
  {
    if ( passthrough_set.find( gate_slot ) == passthrough_set.end() )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' structure gate_slot={} (for slot={}) is not in the "
          "pass-through list",
          path, gate_slot, slot ) );
    }
  }

  return s;
}
} // anonymous namespace

// ---------------------------------------------------------------------------
// load_rlw1
// ---------------------------------------------------------------------------

rl_weights_t load_rlw1( const std::string& path )
{
  std::ifstream file( path, std::ios::binary );
  if ( !file.is_open() )
  {
    throw sc_runtime_error(
        fmt::format( "rl_policy::load_rlw1: could not open '{}'", path ) );
  }

  std::vector<unsigned char> data( ( std::istreambuf_iterator<char>( file ) ),
                                    std::istreambuf_iterator<char>() );
  file.close();

  if ( data.size() < RLW1_HEADER_BYTES )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' is {} bytes, shorter than the {}-byte fixed header",
        path, data.size(), RLW1_HEADER_BYTES ) );
  }

  if ( std::memcmp( data.data(), "RLW1", 4 ) != 0 )
  {
    throw sc_runtime_error(
        fmt::format( "rl_policy::load_rlw1: file '{}' has bad magic (expected 'RLW1')", path ) );
  }

  std::uint32_t format_version = 0;
  std::memcpy( &format_version, data.data() + 4, 4 );
  if ( format_version < RLW1_MIN_SUPPORTED_FORMAT_VERSION || format_version > RLW1_MAX_SUPPORTED_FORMAT_VERSION )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' has format_version={}, but this loader supports "
        "format_version {{{}..{}}}",
        path, format_version, RLW1_MIN_SUPPORTED_FORMAT_VERSION, RLW1_MAX_SUPPORTED_FORMAT_VERSION ) );
  }

  if ( format_version == RLW1_RETIRED_SCORER_FORMAT_VERSION )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' has format_version=4, whose scorer section is retired; "
        "its successor is the v6 aim section (this loader reads format_version 2, 3, 5 and 6)",
        path ) );
  }

  rl_weights_t w;
  w.format_version = format_version;
  std::memcpy( &w.generation, data.data() + 8, 4 );
  std::memcpy( &w.exploration, data.data() + 12, 4 );

  std::uint32_t body_raw = 0;
  std::memcpy( &body_raw, data.data() + 16, 4 );
  w.body = decode_body_type( body_raw, path );
  const bool is_grouped = w.body == rl_body_type::grouped_ln_dueling;

  // 246.1-05 (CAP-01/CAP-02): body 3 requires v5 (its STRUCTURE section has no byte layout
  // below it); v5 requires body 3 (no other body has a STRUCTURE section to parse) -- mirrors
  // scripts/rl/rlw1.py's read_rlw1 pairing check exactly, both directions.
  if ( is_grouped && format_version < RLW1_STRUCTURE_FORMAT_VERSION )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' declares body='grouped-ln-dueling' at format_version={}, "
        "but the STRUCTURE section this body requires is mandatory only at format_version >= {}",
        path, format_version, RLW1_STRUCTURE_FORMAT_VERSION ) );
  }
  if ( !is_grouped && format_version >= RLW1_STRUCTURE_FORMAT_VERSION )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' declares format_version={} (>= {}) but body='{}' is not "
        "'grouped-ln-dueling' -- the STRUCTURE section is forbidden for every other body",
        path, format_version, RLW1_STRUCTURE_FORMAT_VERSION, body_name( w.body ) ) );
  }

  w.obs_schema_sha = decode_fingerprint( data.data() + 20 );
  w.mask_rules_sha = decode_fingerprint( data.data() + 100 );
  w.action_space_sha = decode_fingerprint( data.data() + 180 );

  std::uint32_t n_layers = 0;
  std::memcpy( &n_layers, data.data() + 260, 4 );
  if ( n_layers < 1 || n_layers > RLW1_MAX_LAYERS )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' declares n_layers={}, must be 1..{}",
        path, n_layers, RLW1_MAX_LAYERS ) );
  }

  // 246.1-05 (CAP-01/CAP-02): parsed into a LOCAL vector, not w.layers directly -- a grouped blob's
  // wire layer list is `[block_0 .. block_{G-1}, shared, trunk_0 .. trunk_{T-1}, v_head, a_head]`
  // (rlw1.py's own `_validate_grouped_layers` docstring); the split into w.grouped.block_layers /
  // w.grouped.spell_layer / w.layers (trunk + heads only) happens further down, once the STRUCTURE
  // section's own block count G is known. A non-grouped blob's raw_layers becomes w.layers
  // unchanged (the split is a no-op: G=0, nothing removed from the front).
  std::vector<rl_layer> raw_layers;
  std::size_t offset = RLW1_HEADER_BYTES;
  for ( std::uint32_t li = 0; li < n_layers; ++li )
  {
    if ( offset + 12 > data.size() )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' truncated -- layer {}'s dimensions header needs 12 "
          "bytes at offset {}, only {} remain",
          path, li, offset, data.size() - offset ) );
    }

    std::uint32_t in_features = 0, out_features = 0, has_ln_raw = 0;
    std::memcpy( &in_features, data.data() + offset, 4 );
    std::memcpy( &out_features, data.data() + offset + 4, 4 );
    std::memcpy( &has_ln_raw, data.data() + offset + 8, 4 );

    if ( in_features < RLW1_MIN_FEATURES || in_features > RLW1_MAX_FEATURES )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' layer {} declares in_features={}, must be {}..{}",
          path, li, in_features, RLW1_MIN_FEATURES, RLW1_MAX_FEATURES ) );
    }
    if ( out_features < RLW1_MIN_FEATURES || out_features > RLW1_MAX_FEATURES )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' layer {} declares out_features={}, must be {}..{}",
          path, li, out_features, RLW1_MIN_FEATURES, RLW1_MAX_FEATURES ) );
    }
    if ( has_ln_raw != 0 && has_ln_raw != 1 )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' layer {} declares has_ln={}, must be 0 or 1",
          path, li, has_ln_raw ) );
    }
    const bool has_ln = has_ln_raw != 0;
    offset += 12;

    // T-210-04/T-210-11/T-210-12: range-check FIRST (above), THEN compute
    // the implied byte count in a 64-bit type and verify it against the
    // ACTUAL remaining file length BEFORE any read/resize -- a positive
    // length check, never a try/catch around an over-read. This ordering
    // is the phase's single most likely memory-safety defect. Widened
    // (Phase 213-TDL Task 1) to include the optional gamma/beta pair in
    // `needed` when has_ln is set.
    const std::uint64_t weight_count =
        static_cast<std::uint64_t>( out_features ) * static_cast<std::uint64_t>( in_features );
    const std::uint64_t weight_bytes = weight_count * 4;
    const std::uint64_t bias_bytes = static_cast<std::uint64_t>( out_features ) * 4;
    const std::uint64_t ln_bytes = has_ln ? bias_bytes * 2 : 0;  // gamma + beta, each out_features floats
    const std::uint64_t needed = weight_bytes + bias_bytes + ln_bytes;

    if ( offset + needed > data.size() )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' truncated mid-layer -- layer {} ({}x{}, has_ln={}) needs "
          "{} bytes of payload at offset {}, but the file is short by {} bytes",
          path, li, in_features, out_features, has_ln, needed, offset, ( offset + needed ) - data.size() ) );
    }

    rl_layer layer;
    layer.in_features = in_features;
    layer.out_features = out_features;
    layer.has_ln = has_ln;
    layer.weight.resize( weight_count );
    std::memcpy( layer.weight.data(), data.data() + offset, static_cast<std::size_t>( weight_bytes ) );
    offset += static_cast<std::size_t>( weight_bytes );
    layer.bias.resize( out_features );
    std::memcpy( layer.bias.data(), data.data() + offset, static_cast<std::size_t>( bias_bytes ) );
    offset += static_cast<std::size_t>( bias_bytes );
    if ( has_ln )
    {
      layer.gamma.resize( out_features );
      std::memcpy( layer.gamma.data(), data.data() + offset, static_cast<std::size_t>( bias_bytes ) );
      offset += static_cast<std::size_t>( bias_bytes );
      layer.beta.resize( out_features );
      std::memcpy( layer.beta.data(), data.data() + offset, static_cast<std::size_t>( bias_bytes ) );
      offset += static_cast<std::size_t>( bias_bytes );
    }

    raw_layers.push_back( std::move( layer ) );
  }

  // NET-01 (Phase 222, arm subsets): the optional trailing input-gather +
  // action-allow-list section, RLW1 v3. MANDATORY at v3, FORBIDDEN at v2 --
  // a v2 blob's `offset` must already equal `data.size()` at this point
  // (checked below); a v3 blob's section is parsed with the SAME
  // positive-length-check discipline (T-210-04) the layer loop above uses:
  // a 4-byte header check, then ONE combined check for the index array plus
  // the trailing allowed_actions word, and finally a strictly-increasing
  // walk (done just below, beside the other gather refusals). Default (v2,
  // or a v3 blob declaring n_input_slots==0): identity gather (input_slots
  // stays empty) + every action allowed.
  // 222-07 (WR-01): rl_policy.hpp's static_assert( RL_ACTION_DIM <= 32 )
  // beside the `allowed_actions` member guarantees this shift amount never
  // reaches or exceeds 32, so the well-defined single formula below
  // replaces the old `RL_ACTION_DIM >= 32 ? 0xFFFFFFFFu : (1u <<
  // RL_ACTION_DIM) - 1u` runtime special case -- that ternary "papered
  // over" the UB boundary at runtime rather than refusing to build past it,
  // and its own `1u << 32` branch was itself UB it never took today only
  // because RL_ACTION_DIM (24) never reached it. `0xFFFFFFFFu >> (32 -
  // RL_ACTION_DIM)` is well-defined for every RL_ACTION_DIM in [1, 32]
  // (shift amount in [0, 31]) and produces the identical "every action
  // allowed" bit pattern -- this is the documented v2 back-compat default
  // (a v2 blob, or a v3 blob declaring no subset, means identity gather +
  // all-allowed), unchanged in behaviour, only in how it is computed.
  w.allowed_actions = 0xFFFFFFFFu >> ( 32u - static_cast<std::uint32_t>( RL_ACTION_DIM ) );
  if ( format_version >= 3 )
  {
    if ( offset + 4 > data.size() )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' truncated -- the v3 subset section header needs 4 "
          "bytes at offset {}, only {} remain",
          path, offset, data.size() - offset ) );
    }
    std::uint32_t n_slots_raw = 0;
    std::memcpy( &n_slots_raw, data.data() + offset, 4 );
    offset += 4;
    if ( n_slots_raw > RLW1_MAX_FEATURES )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' declares n_input_slots={}, must be 0..{}",
          path, n_slots_raw, RLW1_MAX_FEATURES ) );
    }
    const std::uint64_t needed = static_cast<std::uint64_t>( n_slots_raw ) * 4 + 4;
    if ( offset + needed > data.size() )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' truncated mid-subset-section -- {} slot index(es) plus "
          "allowed_actions need {} bytes at offset {}, file is short by {} bytes",
          path, n_slots_raw, needed, offset, ( offset + needed ) - data.size() ) );
    }
    w.input_slots.resize( n_slots_raw );
    for ( std::uint32_t i = 0; i < n_slots_raw; ++i )
      std::memcpy( &w.input_slots[ i ], data.data() + offset + static_cast<std::size_t>( i ) * 4, 4 );
    offset += static_cast<std::size_t>( n_slots_raw ) * 4;
    std::memcpy( &w.allowed_actions, data.data() + offset, 4 );
    offset += 4;
  }

  // 246.1-05 (CAP-01/CAP-02): body 3 requires an identity v3 subset section (no gather -- the
  // STRUCTURE section's own gate/block routing already selects everything) and all-allowed
  // actions (no static allow-list -- the per-decision engine mask is the only legality signal a
  // grouped blob ever sees). Mirrors rlw1.py's write_rlw1/read_rlw1 refusals of the same name.
  if ( is_grouped )
  {
    if ( !w.input_slots.empty() )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' body='grouped-ln-dueling' declares {} input_slots -- "
          "the v3 subset section must be identity for body 3",
          path, w.input_slots.size() ) );
    }
    const std::uint32_t default_allowed_actions =
        0xFFFFFFFFu >> ( 32u - static_cast<std::uint32_t>( RL_ACTION_DIM ) );
    if ( w.allowed_actions != default_allowed_actions )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' body='grouped-ln-dueling' declares allowed_actions={:#x}, "
          "expected all-allowed {:#x} -- the v3 subset section must be all-allowed for body 3",
          path, w.allowed_actions, default_allowed_actions ) );
    }
  }

  // 246.1-05 (CAP-01/CAP-02): the v5 STRUCTURE section -- mandatory at format_version >= 5
  // (== RLW1_STRUCTURE_FORMAT_VERSION, the format-pairing check above already refused every other
  // combination), forbidden below it -- there is no byte layout for it at v2/v3/v4.
  raw_structure_t structure;
  const bool has_structure = format_version >= RLW1_STRUCTURE_FORMAT_VERSION;
  if ( has_structure )
    structure = parse_structure_section( data, offset, path );

  // 259-05b (owner Q6): the v6 trailing AIM section -- mandatory at format_version >= 6 (its
  // first u32 says whether the rest is present), forbidden below it. A positive length check
  // precedes every read (T-210-04 / T-259-17), and every refusal names the field, the value and
  // the file, mirroring scripts/rl/rlw1.py's _unpack_aim wording. Layout (little-endian, packed,
  // offsets from the section start): +0 aim_present, +4 n_aim_layers, +8 aim_exploration (f32),
  // +12 aim_slots, +16 aim_input_count, +20 aim_spell_count, +24 aim_obs_source, +28 80-byte
  // NUL-padded aim_registry_sha, +108 the aim layers in the standard per-layer encoding.
  if ( format_version >= RLW1_AIM_FORMAT_VERSION )
  {
    if ( offset + 4 > data.size() )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' truncated -- the v6 aim section's aim_present needs 4 "
          "bytes at offset {}, only {} remain",
          path, offset, data.size() - offset ) );
    }
    std::uint32_t aim_present = 0;
    std::memcpy( &aim_present, data.data() + offset, 4 );
    offset += 4;
    if ( aim_present > 1 )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' declares aim_present={}, must be 0 or 1", path,
          aim_present ) );
    }
    if ( aim_present == 1 )
    {
      constexpr std::size_t AIM_AFTER_PRESENT_BYTES = RLW1_AIM_FIXED_BYTES - 4;
      if ( offset + AIM_AFTER_PRESENT_BYTES > data.size() )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy::load_rlw1: file '{}' truncated -- the v6 aim section header needs {} "
            "more bytes at offset {}, only {} remain",
            path, AIM_AFTER_PRESENT_BYTES, offset, data.size() - offset ) );
      }
      std::uint32_t n_aim_layers = 0;
      float         aim_exploration = 0.0f;
      std::uint32_t aim_slots = 0, aim_input_count = 0, aim_spell_count = 0, aim_obs_source = 0;
      std::memcpy( &n_aim_layers, data.data() + offset, 4 );
      std::memcpy( &aim_exploration, data.data() + offset + 4, 4 );
      std::memcpy( &aim_slots, data.data() + offset + 8, 4 );
      std::memcpy( &aim_input_count, data.data() + offset + 12, 4 );
      std::memcpy( &aim_spell_count, data.data() + offset + 16, 4 );
      std::memcpy( &aim_obs_source, data.data() + offset + 20, 4 );
      const std::string aim_registry_sha = decode_fingerprint( data.data() + offset + 24 );
      offset += AIM_AFTER_PRESENT_BYTES;

      if ( n_aim_layers != 0 &&
           ( n_aim_layers < RLW1_MIN_AIM_LAYERS || n_aim_layers > RLW1_MAX_AIM_LAYERS ) )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy::load_rlw1: file '{}' declares n_aim_layers={}, must be 0 (a dial-only "
            "section) or {}..{}",
            path, n_aim_layers, RLW1_MIN_AIM_LAYERS, RLW1_MAX_AIM_LAYERS ) );
      }
      if ( !std::isfinite( aim_exploration ) || aim_exploration < 0.0f || aim_exploration > 1.0f )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy::load_rlw1: file '{}' declares aim_exploration={}, which must be finite "
            "and within 0.0..1.0",
            path, aim_exploration ) );
      }
      if ( aim_slots < RLW1_MIN_AIM_SLOTS || aim_slots > RLW1_MAX_AIM_SLOTS )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy::load_rlw1: file '{}' declares aim_slots={}, must be {}..{}", path,
            aim_slots, RLW1_MIN_AIM_SLOTS, RLW1_MAX_AIM_SLOTS ) );
      }
      if ( aim_input_count < RLW1_MIN_AIM_INPUTS || aim_input_count > RLW1_MAX_AIM_INPUTS )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy::load_rlw1: file '{}' declares aim_input_count={}, must be {}..{}", path,
            aim_input_count, RLW1_MIN_AIM_INPUTS, RLW1_MAX_AIM_INPUTS ) );
      }
      if ( aim_spell_count < RLW1_MIN_AIM_SPELLS || aim_spell_count > RLW1_MAX_AIM_SPELLS )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy::load_rlw1: file '{}' declares aim_spell_count={}, must be {}..{}", path,
            aim_spell_count, RLW1_MIN_AIM_SPELLS, RLW1_MAX_AIM_SPELLS ) );
      }
      if ( aim_obs_source > 1 )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy::load_rlw1: file '{}' declares aim_obs_source={}, must be 0 (the rules' "
            "pick) or 1 (the head's pick)",
            path, aim_obs_source ) );
      }
      if ( aim_obs_source == 1 && n_aim_layers == 0 )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy::load_rlw1: file '{}' declares aim_obs_source=1 with n_aim_layers=0 -- a "
            "dial-only aim section has no head to describe, so aim_obs_source must be 0",
            path ) );
      }

      std::vector<rl_layer> aim_layers;
      aim_layers.reserve( n_aim_layers );
      for ( std::uint32_t ai = 0; ai < n_aim_layers; ++ai )
        aim_layers.push_back( parse_one_layer( data, offset, path, fmt::format( "aim layer {}", ai ) ) );

      for ( std::size_t ai = 0; ai < aim_layers.size(); ++ai )
      {
        if ( aim_layers[ ai ].has_ln )
        {
          throw sc_runtime_error( fmt::format(
              "rl_policy::load_rlw1: file '{}' aim layer {} has_ln=true, must be false -- the aim "
              "head is always mlp-shaped",
              path, ai ) );
        }
      }
      for ( std::size_t ai = 1; ai < aim_layers.size(); ++ai )
      {
        if ( aim_layers[ ai ].in_features != aim_layers[ ai - 1 ].out_features )
        {
          throw sc_runtime_error( fmt::format(
              "rl_policy::load_rlw1: file '{}' aim layer {} in_features={} does not match aim "
              "layer {} out_features={}",
              path, ai, aim_layers[ ai ].in_features, ai - 1, aim_layers[ ai - 1 ].out_features ) );
        }
      }
      if ( !aim_layers.empty() )
      {
        const std::uint32_t expected_aim_in = aim_input_count + aim_spell_count;
        if ( aim_layers.front().in_features != expected_aim_in )
        {
          throw sc_runtime_error( fmt::format(
              "rl_policy::load_rlw1: file '{}' aim first layer in_features={} does not match "
              "aim_input_count({}) + aim_spell_count({})",
              path, aim_layers.front().in_features, aim_input_count, aim_spell_count ) );
        }
        if ( aim_layers.back().out_features != 1 )
        {
          throw sc_runtime_error( fmt::format(
              "rl_policy::load_rlw1: file '{}' aim last layer out_features={}, must be 1 (one "
              "score per candidate slot)",
              path, aim_layers.back().out_features ) );
        }
      }

      // Refusals against the GENERATED constants (this build's live tables, rl_policy_constants.h)
      // -- unconditional, never opt-in: the engine always has its own header to compare against.
      // A mismatch means the head was built for a different slot count, input list, spell list or
      // enemy-fact declaration than this binary was compiled for; refused by name, both values
      // printed, rather than silently aiming against the wrong columns (T-259-18). `run_target_head`
      // iterates the COMPILED RL_TARGET_SLOTS, so an un-refused slot mismatch would also misread
      // the candidate table.
      if ( aim_slots != RL_TARGET_SLOTS )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy::load_rlw1: file '{}' declares aim_slots={}, does not match this build's "
            "RL_TARGET_SLOTS={}",
            path, aim_slots, RL_TARGET_SLOTS ) );
      }
      if ( aim_input_count != RL_AIM_INPUT_COUNT )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy::load_rlw1: file '{}' declares aim_input_count={}, does not match this "
            "build's RL_AIM_INPUT_COUNT={}",
            path, aim_input_count, RL_AIM_INPUT_COUNT ) );
      }
      if ( aim_spell_count != RL_AIM_SPELL_COUNT )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy::load_rlw1: file '{}' declares aim_spell_count={}, does not match this "
            "build's RL_AIM_SPELL_COUNT={}",
            path, aim_spell_count, RL_AIM_SPELL_COUNT ) );
      }
      if ( aim_registry_sha != RL_AIM_SHA )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy::load_rlw1: file '{}' declares aim_registry_sha='{}', does not match this "
            "build's RL_AIM_SHA='{}' -- the declared enemy-fact / context / spell list disagrees "
            "(possibly reordered)",
            path, aim_registry_sha, RL_AIM_SHA ) );
      }

      w.has_aim_section   = true;
      w.has_aim_head      = !aim_layers.empty();
      w.aim.exploration   = aim_exploration;
      w.aim.slots         = aim_slots;
      w.aim.input_count   = aim_input_count;
      w.aim.spell_count   = aim_spell_count;
      w.aim.obs_source    = aim_obs_source;
      w.aim.registry_sha  = aim_registry_sha;
      w.aim.layers        = std::move( aim_layers );

      // Load-time scratch sizing (never per decision) -- one hidden_scratch entry per aim hidden
      // layer, and the input buffers run_target_head fills every call.
      if ( w.has_aim_head )
      {
        const std::size_t n_aim_hidden = hidden_layer_count( rl_body_type::mlp, w.aim.layers.size() );
        w.aim.hidden_scratch.resize( n_aim_hidden );
        for ( std::size_t ai = 0; ai < n_aim_hidden; ++ai )
          w.aim.hidden_scratch[ ai ].resize( w.aim.layers[ ai ].out_features );
      }
      w.aim.feature_scratch.resize( RL_TARGET_SLOTS * ( RL_AIM_INPUT_COUNT + RL_AIM_SPELL_COUNT ) );
    }
  }

  // Pitfall 14: read_rlw1 (and this loader, pre-222) never compared `offset`
  // to `data.size()` after the layer/section walk -- trailing bytes were
  // silently ignored. Strengthens v2 parsing too: closes it for every
  // format_version, not only v3.
  if ( offset != data.size() )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' has {} unexpected trailing byte(s)",
        path, data.size() - offset ) );
  }

  // NET-01 (Phase 222, arm subsets): three gather refusals, AT LOAD, never
  // per decision -- these are what make `obs[ w.input_slots[i] ]` safe to
  // read in forward() with no per-decision bounds check (same reasoning the
  // file's own CR-02 comment gives for the layer-count refusal below).
  const std::size_t n_slots = w.input_slots.size();
  if ( n_slots > RL_OBS_DIM )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' declares n_input_slots={} which exceeds RL_OBS_DIM={}",
        path, n_slots, RL_OBS_DIM ) );
  }
  for ( std::size_t i = 0; i < n_slots; ++i )
  {
    if ( w.input_slots[ i ] >= RL_OBS_DIM )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' input_slots[{}]={} is out of range for RL_OBS_DIM={}",
          path, i, w.input_slots[ i ], RL_OBS_DIM ) );
    }
    if ( i > 0 && w.input_slots[ i ] <= w.input_slots[ i - 1 ] )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' input_slots are not strictly increasing at index {} "
          "({} then {}) -- the gather order is part of the identity",
          path, i, w.input_slots[ i - 1 ], w.input_slots[ i ] ) );
    }
  }
  if ( w.allowed_actions == 0 )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' declares allowed_actions=0 -- no action would ever be "
        "legal",
        path ) );
  }

  // 246.1-05 (CAP-01/CAP-02): split raw_layers into the grouped blocks/shared-spell layer plus
  // w.layers (trunk + heads only) -- mirrors rlw1.py's own `_validate_grouped_layers` layer-order
  // contract `[block_0 .. block_{G-1}, shared, trunk_0 .. trunk_{T-1}, v_head, a_head]`. A
  // non-grouped blob's raw_layers becomes w.layers unchanged (G=0, nothing removed from the
  // front). Every check below this point that previously read `w.layers` for a grouped blob would
  // have been checking the WRONG slice (block_0, not trunk_0) -- this split is what makes the
  // EXISTING dueling/ln-dueling shape checks below correct for grouped's trunk+heads remainder
  // with no further branching in them beyond the has_ln-pattern widening a few lines down.
  if ( is_grouped )
  {
    const std::size_t n_blocks = structure.blocks.size();
    const std::size_t n_facts = structure.per_spell.empty() ? 0 : structure.per_spell.front().size();
    const std::size_t min_layers = n_blocks + 1 /*shared*/ + 1 /*>=1 trunk*/ + 2 /*heads*/;
    if ( raw_layers.size() < min_layers )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' body='grouped-ln-dueling' requires at least {} layers "
          "({} blocks + 1 shared + >=1 trunk + 2 heads), got {}",
          path, min_layers, n_blocks, raw_layers.size() ) );
    }
    for ( std::size_t bi = 0; bi < n_blocks; ++bi )
    {
      const std::size_t expected_in = structure.blocks[ bi ].size();
      if ( raw_layers[ bi ].in_features != expected_in )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy::load_rlw1: file '{}' block {} in_features={} does not match structure "
            "block {}'s own {} slots",
            path, bi, raw_layers[ bi ].in_features, bi, expected_in ) );
      }
    }
    const rl_layer& shared_layer = raw_layers[ n_blocks ];
    if ( shared_layer.in_features != n_facts )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' shared per-spell layer in_features={} does not match "
          "structure n_facts={}",
          path, shared_layer.in_features, n_facts ) );
    }
    std::uint32_t expected_trunk0_in = 0;
    for ( std::size_t bi = 0; bi < n_blocks; ++bi )
      expected_trunk0_in += raw_layers[ bi ].out_features;
    expected_trunk0_in += static_cast<std::uint32_t>( structure.per_spell.size() ) * shared_layer.out_features;
    expected_trunk0_in += static_cast<std::uint32_t>( structure.passthrough.size() );
    const std::size_t trunk_start = n_blocks + 1;
    if ( raw_layers[ trunk_start ].in_features != expected_trunk0_in )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' trunk_0.in_features={} does not match the concat "
          "invariant (sum block outs + n_repeats * shared_out + n_passthrough = {})",
          path, raw_layers[ trunk_start ].in_features, expected_trunk0_in ) );
    }

    w.grouped.block_layers.assign( raw_layers.begin(), raw_layers.begin() + static_cast<long>( n_blocks ) );
    w.grouped.spell_layer = raw_layers[ n_blocks ];
    w.layers.assign( raw_layers.begin() + static_cast<long>( trunk_start ), raw_layers.end() );

    w.grouped.n_obs = structure.n_obs;
    w.grouped.layout_sha = structure.layout_sha;
    w.grouped.block_slots = structure.blocks;
    w.grouped.per_spell = structure.per_spell;
    w.grouped.passthrough = structure.passthrough;
    w.grouped.gate_src.assign( structure.n_obs, structure.n_obs );   // sentinel: n_obs == "gate by 1"
    for ( const auto& [ slot, gate_slot ] : structure.gated )
      w.grouped.gate_src[ slot ] = gate_slot;

    // Load-time cross-checks (CAP-02): the blob's own declared width and layout fingerprint must
    // match THIS build's live generated constants -- a mismatch means this blob was trained
    // against a different registry/layout than this binary was built against.
    if ( w.grouped.n_obs != RL_OBS_DIM )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' declares obs width {} (via its STRUCTURE section), does "
          "not match this build's RL_OBS_DIM={}",
          path, w.grouped.n_obs, RL_OBS_DIM ) );
    }
    if ( w.grouped.layout_sha != RL_NET_LAYOUT_SHA )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' declares layout_sha='{}', does not match this build's "
          "RL_NET_LAYOUT_SHA='{}'",
          path, w.grouped.layout_sha, RL_NET_LAYOUT_SHA ) );
    }
  }
  else
  {
    w.layers = std::move( raw_layers );
  }

  // Shape sanity against the GENERATED constants -- otherwise a shape
  // mismatch is a buffer overrun in forward() instead of an error message.
  // The first-layer check is against the RESOLVED expected width -- n_slots
  // when a gather is declared, RL_OBS_DIM otherwise -- REPLACING the old
  // bare `== RL_OBS_DIM` check, which cannot be right once a subset can
  // narrow layer 0's input. SKIPPED for a grouped blob (246.1-05): w.layers[0]
  // is trunk_0, not a width-RL_OBS_DIM input layer -- the equivalent invariant was already
  // checked above (trunk_0.in_features against the concat width).
  const std::uint32_t expected_in = n_slots ? static_cast<std::uint32_t>( n_slots ) : RL_OBS_DIM;
  if ( !is_grouped && w.layers.front().in_features != expected_in )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' first layer in_features={} does not match the resolved "
        "input width={} ({})",
        path, w.layers.front().in_features, expected_in,
        n_slots ? "n_input_slots" : "RL_OBS_DIM" ) );
  }
  if ( w.layers.back().out_features != RL_ACTION_DIM )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' last layer out_features={} does not match RL_ACTION_DIM={}",
        path, w.layers.back().out_features, RL_ACTION_DIM ) );
  }

  // CR-02 fix (210-CR-FIX), widened Phase 213-TDL Task 1, generalised Phase
  // 222 NET-01: forward() indexes w.layers[0..N-1] via operator[] for a
  // layer count derived from n_layers and body -- so a count below the
  // body's own minimum is unchecked UB there. Refuse here, at load time,
  // where an out-of-range value is a named error instead of a heap read
  // past the vector. (The hard upper bound RLW1_MAX_LAYERS was already
  // enforced above, at n_layers parse time.)
  if ( w.layers.size() < expected_layer_count( w.body ) )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' body={} requires at least {} layers, but declares only {}",
        path, body_name( w.body ), expected_layer_count( w.body ), w.layers.size() ) );
  }

  // has_ln pattern must match body's own convention (rlw1.py's
  // _layer_split, mirrored here, Phase 222 NET-01): mlp/dueling carry
  // has_ln=false on EVERY layer; ln-dueling carries has_ln=true on every
  // HIDDEN layer ONLY (indices [0, hidden_layer_count) -- v_head/a_head are
  // output heads, no norm follows them. Depth-agnostic: derived from
  // hidden_layer_count(), never a fixed pair of indices. 246.1-05: grouped_ln_dueling's own
  // w.layers (trunk + heads only, post-split above) follows the SAME convention -- every trunk
  // layer carries has_ln=true, exactly like ln-dueling's hidden layers.
  {
    const bool expect_ln_hidden =
        w.body == rl_body_type::ln_dueling || w.body == rl_body_type::grouped_ln_dueling;
    const std::size_t n_hidden_for_ln =
        expect_ln_hidden ? hidden_layer_count( w.body, w.layers.size() ) : 0;
    for ( std::size_t i = 0; i < w.layers.size(); ++i )
    {
      const bool expected = expect_ln_hidden && ( i < n_hidden_for_ln );
      if ( w.layers[ i ].has_ln != expected )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy::load_rlw1: file '{}' body={} layer {} has_ln={}, expected {}",
            path, body_name( w.body ), i, w.layers[ i ].has_ln, expected ) );
      }
    }
  }

  if ( w.body == rl_body_type::mlp )
  {
    // A straight chain: in -> hidden[0] -> hidden[1] -> ... -> out. Interior
    // dimensions must chain -- forward() indexes each hidden scratch entry
    // by the NEXT layer's in_features, so a mismatch here is a heap
    // over-read rather than an error. Already depth-agnostic (n_layers >= 2
    // walks every layer regardless of count) -- unchanged from pre-222.
    for ( std::size_t i = 1; i < w.layers.size(); ++i )
    {
      if ( w.layers[ i ].in_features != w.layers[ i - 1 ].out_features )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy::load_rlw1: file '{}' layer {} in_features={} does not match layer {} "
            "out_features={}",
            path, i, w.layers[ i ].in_features, i - 1, w.layers[ i - 1 ].out_features ) );
      }
    }
  }
  else
  {
    // dueling / ln-dueling: hidden[0] -> hidden[1] -> ... -> hidden[n-1]
    // chains normally, but v_head and a_head (the last TWO layers) both
    // BRANCH off the LAST hidden layer's OUTPUT -- never a further chain
    // onto each other. Checked explicitly here (Pitfall 2 -- the second
    // depth pin) because the straight-chain loop above would be WRONG for
    // this branching shape. Re-indexed to hidden_layer_count() so the check
    // is correct at any depth, not just the old fixed 4-layer shape.
    const std::size_t n_hidden = hidden_layer_count( w.body, w.layers.size() );
    for ( std::size_t i = 1; i < n_hidden; ++i )
    {
      if ( w.layers[ i ].in_features != w.layers[ i - 1 ].out_features )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy::load_rlw1: file '{}' hidden layer {} in_features={} does not match hidden "
            "layer {} out_features={}",
            path, i, w.layers[ i ].in_features, i - 1, w.layers[ i - 1 ].out_features ) );
      }
    }
    const std::size_t v_idx = n_hidden;
    const std::size_t a_idx = n_hidden + 1;
    const std::uint32_t last_hidden_out = w.layers[ n_hidden - 1 ].out_features;
    if ( w.layers[ v_idx ].in_features != last_hidden_out )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' v_head in_features={} does not match the last hidden "
          "layer's out_features={}",
          path, w.layers[ v_idx ].in_features, last_hidden_out ) );
    }
    if ( w.layers[ v_idx ].out_features != 1 )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' v_head out_features={}, must be 1",
          path, w.layers[ v_idx ].out_features ) );
    }
    if ( w.layers[ a_idx ].in_features != last_hidden_out )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' a_head in_features={} does not match the last hidden "
          "layer's out_features={}",
          path, w.layers[ a_idx ].in_features, last_hidden_out ) );
    }
  }

  // D-01/D-02 (210-05R Task 3): four load-time refusals, named per
  // condition. This C++ implements the format `scripts/rl/rlw1.py`
  // (this repo) writes -- that Python module is the format's normative
  // counterpart.

  // Refusal: obs schema fingerprint. Byte-exact string comparison against
  // the FULL stored string -- deliberately NOT stripping the "rl-obs-v1:"
  // scheme prefix before comparing. The scheme string carries the encoder
  // version (obs.py:78-81), so comparing the full string automatically
  // also compares encoder versions; stripping it would silently permit an
  // encoderVersion bump with an unchanged digest, which ruling N-3 exists
  // to freeze against.
  if ( w.obs_schema_sha != RL_OBS_SCHEMA_SHA )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' obs schema fingerprint mismatch -- expected '{}', found "
        "'{}'. Re-mint the weights blob (or regenerate rl_policy_constants.h) so both describe the "
        "same observation registry.",
        path, RL_OBS_SCHEMA_SHA, w.obs_schema_sha ) );
  }

  // Refusal: mask-rules fingerprint.
  if ( w.mask_rules_sha != RL_MASK_RULES_SHA )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' mask rules fingerprint mismatch -- expected '{}', found "
        "'{}'. Re-mint the weights blob (or regenerate rl_policy_constants.h) so both describe the "
        "same mask rules.",
        path, RL_MASK_RULES_SHA, w.mask_rules_sha ) );
  }

  // Refusal: action-space fingerprint. Exists even though ROADMAP
  // criterion 4 names only the observation sha: a reordered action list
  // means argmax index 2 resolves to a different spell, the run still
  // completes, the damage is still plausible, and nothing else errors --
  // this is the only control that catches that class of drift.
  if ( w.action_space_sha != RL_ACTION_SPACE_SHA )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' action space fingerprint mismatch -- expected '{}', found "
        "'{}'. Re-mint the weights blob (or regenerate rl_policy_constants.h) so both describe the "
        "same action space.",
        path, RL_ACTION_SPACE_SHA, w.action_space_sha ) );
  }

  // Refusal: exploration VALIDITY (Phase 213, ruling 213-G17). Phase 210's
  // placeholder rejected any exploration value other than exact zero,
  // written as a single guarded line so this phase could lift it out in
  // one clean edit once the C++ draw itself existed -- it now does
  // (solver_control.cpp's in-process arm, below), so the honest guard is a
  // RANGE check, matching the writer-side half in
  // scripts/rl/weights_export.py: not finite, below zero, or above one is
  // refused by name, same message shape as the three fingerprint refusals
  // above. This lift and the draw land in the same commit -- a lift
  // without a draw would be an engine that silently ignores the dial.
  if ( !std::isfinite( w.exploration ) || w.exploration < 0.0f || w.exploration > 1.0f )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' declares exploration={}, which must be finite and within "
        "[0.0, 1.0]",
        path, w.exploration ) );
  }

  // WR-02 fix, cheap half (210-CR-FIX), generalised Phase 222 NET-01: size
  // forward()'s hidden-layer scratch buffers ONCE, here at load time, one
  // entry per hidden layer (hidden_layer_count() of them, already validated
  // above), plus the gather scratch when a subset is declared.
  {
    const std::size_t n_hidden = hidden_layer_count( w.body, w.layers.size() );
    w.hidden_scratch.resize( n_hidden );
    for ( std::size_t i = 0; i < n_hidden; ++i )
      w.hidden_scratch[ i ].resize( w.layers[ i ].out_features );
    w.obs_gather_scratch.resize( n_slots );
  }

  // 246.1-05 (CAP-01/CAP-02): grouped-body scratch, load-time-sized, never per decision --
  // mirrors the hidden_scratch discipline immediately above.
  if ( is_grouped )
  {
    rl_grouped_layout_t& g = w.grouped;
    g.gated_scratch.resize( g.n_obs );

    const std::size_t n_blocks = g.block_layers.size();
    g.block_gather_scratch.resize( n_blocks );
    g.block_scratch.resize( n_blocks );
    for ( std::size_t bi = 0; bi < n_blocks; ++bi )
    {
      g.block_gather_scratch[ bi ].resize( g.block_slots[ bi ].size() );
      g.block_scratch[ bi ].resize( g.block_layers[ bi ].out_features );
    }

    const std::size_t n_facts = g.per_spell.empty() ? 0 : g.per_spell.front().size();
    g.spell_gather_scratch.resize( n_facts );
    g.spell_scratch.resize( g.per_spell.size() );
    for ( std::size_t si = 0; si < g.per_spell.size(); ++si )
      g.spell_scratch[ si ].resize( g.spell_layer.out_features );

    std::size_t concat_width = 0;
    for ( std::size_t bi = 0; bi < n_blocks; ++bi )
      concat_width += g.block_layers[ bi ].out_features;
    concat_width += g.per_spell.size() * static_cast<std::size_t>( g.spell_layer.out_features );
    concat_width += g.passthrough.size();
    g.concat_scratch.resize( concat_width );
  }

  return w;
}

// ---------------------------------------------------------------------------
// forward -- three body types (rl_body_type), dispatched at the top, ANY
// depth (Phase 222 NET-01, derived via hidden_layer_count()):
//
//   mlp:        x0 = obs (or the gathered subset -- see below), then for
//               each hidden layer i: hi = relu(Wi*x(i-1) + bi); finally
//               q = W_out*h(last) + b_out. ReLU between hidden layers only,
//               no output activation. Byte-identical to the pre-v2
//               forward() at depth 2.
//   dueling:    same hidden-layer loop, then v = Wv*h(last) + bv (scalar),
//               a = Wa*h(last) + ba (RL_ACTION_DIM), and Phase 222 NET-02's
//               LEGAL-ONLY recombination: q[i] = v + a[i] - mean_legal(a),
//               mean over actions whose mask[] byte is non-zero, computed
//               HERE, before masked_argmax's own mask+argmax step ever
//               runs -- falling back to the mean over EVERY action when
//               zero actions are legal (mirrored exactly against
//               agent/network_bodies.py's own DuelingBody.forward(); never
//               NaN -- a NaN Q vector would make every masked_argmax
//               comparison false and silently return index 0).
//   ln-dueling: same as dueling, but each HIDDEN layer's raw output is
//               LayerNorm'd (eps=RL_LAYER_NORM_EPS, per-decision statistics
//               over that layer's own out_features values -- never batch
//               statistics, since this forward() only ever sees ONE
//               decision at a time) BEFORE its ReLU -- read from that
//               layer's own has_ln flag, never from `body == ln_dueling`
//               re-checked per layer.
//
// Gather: when w.input_slots is non-empty, the full-width `obs` is
// projected into obs_gather_scratch ONCE, before layer 0 -- n_slots float
// copies, zero allocation (the scratch is load-time-sized). Every layer
// after that reads exactly as it always did.
//
// Fixed-size scratch throughout (hidden_scratch/obs_gather_scratch
// load-time-sized by load_rlw1; the V head is a single local float; the A
// head is written DIRECTLY into out_q[] and recombined in place) -- no
// allocation on this, the hottest path the fork has.
// ---------------------------------------------------------------------------

namespace
{
// Per-decision LayerNorm (never batch statistics -- see this file's own
// top-of-file comment): normalizes `x` in place over its own out_features
// values, then applies the learned per-feature gamma/beta. Mean/variance
// accumulated in double for numerical stability; population variance
// (divide by N, torch's own nn.LayerNorm convention, not N-1).
void apply_layer_norm( std::vector<float>& x, const std::vector<float>& gamma, const std::vector<float>& beta )
{
  const std::size_t n = x.size();
  double mean = 0.0;
  for ( float v : x )
    mean += static_cast<double>( v );
  mean /= static_cast<double>( n );

  double var = 0.0;
  for ( float v : x )
  {
    const double d = static_cast<double>( v ) - mean;
    var += d * d;
  }
  var /= static_cast<double>( n );

  const double inv_std = 1.0 / std::sqrt( var + static_cast<double>( RL_LAYER_NORM_EPS ) );
  for ( std::size_t i = 0; i < n; ++i )
  {
    const double normalized = ( static_cast<double>( x[ i ] ) - mean ) * inv_std;
    x[ i ] = static_cast<float>( normalized * static_cast<double>( gamma[ i ] ) + static_cast<double>( beta[ i ] ) );
  }
}

// 260920-cvf stage B, Task 2: replaces the 8-way-partial-sum `dot8()` above
// (CVF stage 1) with a function-scoped-fast-math dot product plus a
// once-at-load runtime dispatch. `dot8`'s explicit-accumulator form was
// measured COMPILER-FRAGILE (orchestrator, brain / Ryzen 9 5900X, avx2+fma,
// `-O3` no `-march`, 20k-rep repeat probe): 1.39x over the original
// dependent-chain loop at baseline flags, but only 0.87x -- a REGRESSION --
// once `-march=native` was added, because -march changed how the compiler
// chose to vectorize the 8 independent accumulators and it chose worse. The
// actual win measured on the SAME box/probe/inputs came from a different
// lever entirely: `optimize("fast-math")` scoped to a single function
// (permission to reassociate a finite-float sum; nothing else in the build
// gets -ffast-math) plus `target("avx2,fma")` for explicit AVX2/FMA codegen
// on that one function -- 9.0x (3.56us/forward) over the original loop's
// 31.8us baseline, vs 4.5x (7.09us) for fast-math alone without avx2/fma.
// max|dQ| vs the original loop over 200 inputs, gate 1e-5: 4.8e-8
// (fast-math+avx2), 4.5e-8 (dot8, for comparison) -- both several orders of
// magnitude inside tolerance. See dot_orig/dot_base/dot_avx2 below for the
// three bodies and select_dot_fn() for why the dispatch exists.
//
// This is the ONE dot product every hot forward() call in this file uses --
// the hidden-layer loop, the mlp output layer, and both dueling V/A heads
// (every `for (...) acc += w[i]*x[i]` this file used to write by hand).

// `dot_orig` is the pre-CVF, pre-dot8 form: a single dependent accumulator
// chain, no attributes, no reassociation permission. Production dispatch
// (the default, no RL_FORWARD_DOT override) never selects this -- it exists
// so stage B Task 3's timing probe can reproduce the ORIGINAL baseline
// number (31.8us/forward measured) against the same binary the AVX2 path
// ships in, rather than needing a separate build.
float dot_orig( const float* w, const float* x, std::uint32_t n )
{
  float acc = 0.0f;
  for ( std::uint32_t i = 0; i < n; ++i )
    acc += w[ i ] * x[ i ];
  return acc;
}

// `optimize("fast-math")` is FUNCTION-SCOPED, not a whole-TU or whole-build
// flag -- it permits the compiler to reassociate the float multiply-adds in
// THIS loop only. That reassociation is safe here specifically because the
// inputs are a bounded RL observation/weight vector that sc_main.cpp's
// rl_forward_probe already refuses to accept as non-finite upstream of this
// call (std::isfinite() check on every observation value) -- fast-math's
// usual hazard (NaN/Inf comparisons silently becoming UB) cannot arise on
// this path. No other function in the engine's build carries -ffast-math;
// this attribute changes codegen for this one symbol only. Measured 4.5x
// over dot_orig on its own (7.09us/forward, same box/probe as above).
// `noinline` keeps it a single, separately profilable/timeable symbol
// rather than letting its fast-math-flavored codegen get smeared across
// whatever calls it.
__attribute__( ( noinline, optimize( "fast-math" ) ) )
float dot_base( const float* w, const float* x, std::uint32_t n )
{
  float acc = 0.0f;
  for ( std::uint32_t i = 0; i < n; ++i )
    acc += w[ i ] * x[ i ];
  return acc;
}

// Same reassociation permission as dot_base, PLUS `target("avx2,fma")`:
// this function specifically is compiled to emit AVX2/FMA vector
// instructions, regardless of the baseline -march the rest of the TU is
// built for. Measured 9.0x over dot_orig (3.56us/forward). A binary built
// for a baseline x86-64 target that called this function UNCONDITIONALLY
// on a CPU lacking avx2+fma would SIGILL at the first call -- that is
// exactly why `dot_fn` below never selects this without first checking
// `__builtin_cpu_supports`.
__attribute__( ( noinline, optimize( "fast-math" ), target( "avx2,fma" ) ) )
float dot_avx2( const float* w, const float* x, std::uint32_t n )
{
  float acc = 0.0f;
  for ( std::uint32_t i = 0; i < n; ++i )
    acc += w[ i ] * x[ i ];
  return acc;
}

using dot_fn_t = float ( * )( const float*, const float*, std::uint32_t );

// 260920-cvf stage B, Task 3: RL_FORWARD_DOT={orig,base,avx2} lets the
// timing probe force a specific body so all three can be measured against
// the SAME binary/build instead of standing up three separate builds. Any
// unset/empty/unrecognized value falls through to the production
// CPU-feature dispatch below -- exactly what ships when nothing forces a
// choice. getenv() runs ONCE, in this namespace-scope initializer, never in
// the per-decision hot path.
dot_fn_t select_dot_fn()
{
  if ( const char* forced = std::getenv( "RL_FORWARD_DOT" ) )
  {
    if ( std::strcmp( forced, "orig" ) == 0 )
      return dot_orig;
    if ( std::strcmp( forced, "base" ) == 0 )
      return dot_base;
    if ( std::strcmp( forced, "avx2" ) == 0 )
      return dot_avx2;
    // Falls through on any other value, same as unset.
  }
  // GCC >= 4.8 implicitly runs __builtin_cpu_init() the first time
  // __builtin_cpu_supports() is used (verified against this fork's
  // compiler, GCC 15.2.0) -- no explicit call needed. If a future toolchain
  // ever requires it, call __builtin_cpu_init() here before the checks.
  return ( __builtin_cpu_supports( "avx2" ) && __builtin_cpu_supports( "fma" ) )
             ? dot_avx2
             : dot_base;
}

// Namespace-scope initializer: runs once, before forward() can be called --
// this is the only TU that defines or calls any of the three bodies above,
// so there is no cross-TU init-order hazard to reason about.
const dot_fn_t dot_fn = select_dot_fn();

// 246.1-05 (CAP-01/CAP-02): the grouped body's own input stage -- the gate, the five named
// blocks, the shared per-spell layer and the pass-through columns, concatenated into
// `w.grouped.concat_scratch` in block order, then per_spell order (spell order kept, never
// pooled -- WHICH spell has flame shock up is not interchangeable information), then
// pass-through. Mirrors `GroupedDuelingBody.forward` (network_bodies.py) exactly: `gated = x *
// cat([x, ones], dim=1).index_select(1, gate_src)` -- NEVER mutates the caller's `obs` (every op
// writes into this struct's own load-time-sized scratch). No allocation: every buffer here was
// sized by load_rlw1. Returns nothing -- callers read `w.grouped.concat_scratch.data()`.
void compute_grouped_input( const rl_weights_t& w, const float obs[ RL_OBS_DIM ] )
{
  const rl_grouped_layout_t& g = w.grouped;

  // The gate: gated[i] = obs[i] * (obs[gate_src[i]] if governed, else 1.0). gate_src[i] == g.n_obs
  // is the sentinel "point at an appended constant 1" (network_bodies.py's own convention) --
  // there is no appended element here, the sentinel is simply never dereferenced.
  for ( std::size_t i = 0; i < g.n_obs; ++i )
  {
    const std::uint32_t src = g.gate_src[ i ];
    const float gate_value = ( src == g.n_obs ) ? 1.0f : obs[ src ];
    g.gated_scratch[ i ] = obs[ i ] * gate_value;
  }

  std::size_t concat_pos = 0;

  // Five named blocks: gather this block's own (non-contiguous) slots into its own contiguous
  // scratch (dot_fn needs contiguous input), run Linear -> LayerNorm -> ReLU, append to concat.
  for ( std::size_t bi = 0; bi < g.block_layers.size(); ++bi )
  {
    const std::vector<std::uint32_t>& slots = g.block_slots[ bi ];
    std::vector<float>& gather = g.block_gather_scratch[ bi ];
    for ( std::size_t si = 0; si < slots.size(); ++si )
      gather[ si ] = g.gated_scratch[ slots[ si ] ];

    const rl_layer& l = g.block_layers[ bi ];
    std::vector<float>& out = g.block_scratch[ bi ];
    for ( std::uint32_t o = 0; o < l.out_features; ++o )
      out[ o ] = l.bias[ o ] + dot_fn( &l.weight[ o * l.in_features ], gather.data(), l.in_features );
    apply_layer_norm( out, l.gamma, l.beta );   // has_ln is always true for a grouped block layer
    for ( std::uint32_t o = 0; o < l.out_features; ++o )
      g.concat_scratch[ concat_pos + o ] = std::max( out[ o ], 0.0f );
    concat_pos += l.out_features;
  }

  // The shared per-spell layer, applied IDENTICALLY to each spell's own 15 gathered facts --
  // SAME weights every repeat, concatenated in spell order (never pooled).
  for ( std::size_t ri = 0; ri < g.per_spell.size(); ++ri )
  {
    const std::vector<std::uint32_t>& slots = g.per_spell[ ri ];
    for ( std::size_t fi = 0; fi < slots.size(); ++fi )
      g.spell_gather_scratch[ fi ] = g.gated_scratch[ slots[ fi ] ];

    const rl_layer& l = g.spell_layer;
    std::vector<float>& out = g.spell_scratch[ ri ];
    for ( std::uint32_t o = 0; o < l.out_features; ++o )
      out[ o ] = l.bias[ o ] + dot_fn( &l.weight[ o * l.in_features ], g.spell_gather_scratch.data(), l.in_features );
    apply_layer_norm( out, l.gamma, l.beta );   // has_ln is always true for the shared spell layer
    for ( std::uint32_t o = 0; o < l.out_features; ++o )
      g.concat_scratch[ concat_pos + o ] = std::max( out[ o ], 0.0f );
    concat_pos += l.out_features;
  }

  // Pass-through: the capability columns' own gated value, raw (no Linear/LayerNorm) -- so the
  // trunk sees the setup identity directly, not only after a block's own LayerNorm mix.
  for ( std::size_t pi = 0; pi < g.passthrough.size(); ++pi )
    g.concat_scratch[ concat_pos + pi ] = g.gated_scratch[ g.passthrough[ pi ] ];
}
} // anonymous namespace

void forward( const rl_weights_t& w, const float obs[ RL_OBS_DIM ], const std::uint8_t mask[ RL_ACTION_DIM ],
              float out_q[ RL_ACTION_DIM ] )
{
  // 246.1-05 (CAP-01/CAP-02): the grouped body computes its own input stage (gate + five blocks +
  // shared per-spell layer + pass-through, concatenated) INSTEAD of the plain gather-or-obs
  // seam below -- w.input_slots is load-time-refused to be empty for this body (the STRUCTURE
  // section's own block/per-spell/passthrough routing already selects everything RL_OBS_DIM has).
  // `x` then feeds the SAME hidden-layer loop and dueling combine below, completely unchanged --
  // w.layers for a grouped blob is already [trunk_0 .. trunk_{T-1}, v_head, a_head] (the load-time
  // split in load_rlw1), identically shaped to ln-dueling's own layer list.
  const float* x = obs;
  if ( w.body == rl_body_type::grouped_ln_dueling )
  {
    compute_grouped_input( w, obs );
    x = w.grouped.concat_scratch.data();
  }
  // NET-01 (Phase 222, arm subsets): gather the full-width obs into the
  // load-time-sized obs_gather_scratch ONCE, before layer 0, when a subset
  // is declared -- otherwise layer 0 reads obs directly. w.layers.front()'s
  // in_features was validated at load time against exactly this resolved
  // width (n_slots when a gather is declared, RL_OBS_DIM otherwise), so no
  // per-decision bounds check is needed here.
  else if ( !w.input_slots.empty() )
  {
    for ( std::size_t i = 0; i < w.input_slots.size(); ++i )
      w.obs_gather_scratch[ i ] = obs[ w.input_slots[ i ] ];
    x = w.obs_gather_scratch.data();
  }

  // WR-02 fix, cheap half (210-CR-FIX), generalised Phase 222 NET-01:
  // hidden_scratch[i] is a load-time-sized scratch buffer on `w`
  // (rl_policy.hpp's `mutable std::vector<std::vector<float>>
  // hidden_scratch`, sized by load_rlw1) instead of a fresh
  // std::vector<float> allocation per decision boundary -- this is the
  // hottest path the fork has. Shared by all three body types (the hidden
  // layers are identically shaped/positioned regardless of body). The loop
  // bound is hidden_layer_count(), the SAME derived rule load_rlw1 already
  // validated the blob against.
  const std::size_t n_hidden = hidden_layer_count( w.body, w.layers.size() );
  const float* layer_in = x;
  for ( std::size_t li = 0; li < n_hidden; ++li )
  {
    const rl_layer& l = w.layers[ li ];
    std::vector<float>& h = w.hidden_scratch[ li ];
    for ( std::uint32_t o = 0; o < l.out_features; ++o )
    {
      // 260920-cvf stage B: fast-math/AVX2-dispatched dot product (see
      // dot_fn/select_dot_fn() above) -- was dot8(), before that a single
      // dependent accumulator chain.
      h[ o ] = l.bias[ o ] + dot_fn( &l.weight[ o * l.in_features ], layer_in, l.in_features );
    }
    if ( l.has_ln )  // P-14: read the PER-LAYER flag, not `body == ln_dueling`
      apply_layer_norm( h, l.gamma, l.beta );
    for ( std::uint32_t o = 0; o < l.out_features; ++o )
      h[ o ] = std::max( h[ o ], 0.0f );
    layer_in = h.data();
  }

  if ( w.body == rl_body_type::mlp )
  {
    const rl_layer& out_layer = w.layers[ n_hidden ];  // the single output layer
    for ( std::uint32_t o = 0; o < out_layer.out_features; ++o )
    {
      // 260920-cvf stage B: fast-math/AVX2-dispatched dot product (see
      // dot_fn/select_dot_fn() above).
      out_q[ o ] = out_layer.bias[ o ] + dot_fn( &out_layer.weight[ o * out_layer.in_features ], layer_in, out_layer.in_features );
    }
    return;
  }

  // dueling / ln-dueling: V head (scalar) + A head (RL_ACTION_DIM), both
  // branching off the LAST hidden layer's output -- recombined as Phase 222
  // NET-02's legal-only mean: Q = V + A - mean_legal(A).
  const rl_layer& v_head = w.layers[ n_hidden ];
  const rl_layer& a_head = w.layers[ n_hidden + 1 ];

  // 260920-cvf stage B: fast-math/AVX2-dispatched dot product (see
  // dot_fn/select_dot_fn() above). v_head.out_features == 1, row 0 only.
  const float v = v_head.bias[ 0 ] + dot_fn( v_head.weight.data(), layer_in, v_head.in_features );

  // A head written DIRECTLY into out_q[] -- no separate scratch allocation.
  // a_sum/a_count accumulate inside this SAME loop, guarded by mask[o] --
  // NET-02: legal actions only.
  float a_sum = 0.0f;
  std::uint32_t a_count = 0;
  for ( std::uint32_t o = 0; o < a_head.out_features; ++o )
  {
    // 260920-cvf stage B: fast-math/AVX2-dispatched dot product (see
    // dot_fn/select_dot_fn() above).
    const float acc = a_head.bias[ o ] + dot_fn( &a_head.weight[ o * a_head.in_features ], layer_in, a_head.in_features );
    out_q[ o ] = acc;
    if ( mask[ o ] )
    {
      a_sum += acc;
      ++a_count;
    }
  }
  if ( a_count == 0 )
  {
    // P-16 / Pitfall 4: mirror scripts/rl/agent/network_bodies.py's
    // DuelingBody.forward EXACTLY -- a background decision boundary can
    // produce an all-illegal mask (wait is foreground-only), so this branch
    // is reachable, not merely defensive. Fall back to the mean over EVERY
    // action rather than dividing by zero: a NaN Q vector would make every
    // masked_argmax comparison false and silently return index 0.
    for ( std::uint32_t o = 0; o < a_head.out_features; ++o )
      a_sum += out_q[ o ];
    a_count = a_head.out_features;
  }
  const float a_mean = a_sum / static_cast<float>( a_count );
  for ( std::uint32_t o = 0; o < a_head.out_features; ++o )
    out_q[ o ] = v + out_q[ o ] - a_mean;
}

// ---------------------------------------------------------------------------
// masked_argmax -- tianshou's compute_q_value formula, verbatim (see the
// header comment above for the re-verified-at-source citation).
// ---------------------------------------------------------------------------

int masked_argmax( const float q[ RL_ACTION_DIM ], const std::uint8_t mask[ RL_ACTION_DIM ] )
{
  // min_value is computed from the UNMASKED q values of this row -- never
  // substitute -INFINITY or a large negative constant (a row where every
  // legal action has a very negative Q would change the argmax under a
  // different sentinel).
  float qmin = q[ 0 ];
  float qmax = q[ 0 ];
  for ( std::size_t i = 1; i < RL_ACTION_DIM; ++i )
  {
    qmin = std::min( qmin, q[ i ] );
    qmax = std::max( qmax, q[ i ] );
  }
  const float min_value = qmin - qmax - 1.0f;

  int best = 0;
  float best_q = q[ 0 ] + static_cast<float>( 1 - mask[ 0 ] ) * min_value;
  for ( std::size_t i = 1; i < RL_ACTION_DIM; ++i )
  {
    const float masked_q = q[ i ] + static_cast<float>( 1 - mask[ i ] ) * min_value;
    // Strict > so ties resolve first-index-wins, matching torch.argmax.
    if ( masked_q > best_q )
    {
      best_q = masked_q;
      best = static_cast<int>( i );
    }
  }
  return best;
}

// ---------------------------------------------------------------------------
// forward_scorer -- Phase 230-02 (SCOR-01). One hidden-layer stack (ReLU, the SAME activation
// forward()'s mlp branch above uses -- no second activation formula) then a single 1-wide linear
// output layer, no masking (this scores exactly ONE candidate; rl_target_select::select() calls
// it once per candidate and argmaxes the results itself, mirroring every other preference_fn's
// own contract). Scorer layers are always mlp-shaped (validated at load: has_ln==false on every
// layer), so this reuses hidden_layer_count(rl_body_type::mlp, ...) verbatim -- the SAME derived
// rule forward()'s own mlp branch uses, never a second hidden convention.
// ---------------------------------------------------------------------------

float forward_aim( const rl_aim_t& s, const float* in )
{
  const std::size_t n_hidden = hidden_layer_count( rl_body_type::mlp, s.layers.size() );
  const float* layer_in = in;
  for ( std::size_t li = 0; li < n_hidden; ++li )
  {
    const rl_layer& l = s.layers[ li ];
    std::vector<float>& h = s.hidden_scratch[ li ];
    for ( std::uint32_t o = 0; o < l.out_features; ++o )
    {
      float acc = l.bias[ o ];
      for ( std::uint32_t i = 0; i < l.in_features; ++i )
        acc += l.weight[ o * l.in_features + i ] * layer_in[ i ];
      h[ o ] = acc;
    }
    // Aim layers carry has_ln==false unconditionally (validated at load) -- ReLU only, no
    // LayerNorm branch needed here (unlike forward()'s own hidden loop, which must still read a
    // per-layer flag since the main net's ln-dueling body CAN carry one).
    for ( std::uint32_t o = 0; o < l.out_features; ++o )
      h[ o ] = std::max( h[ o ], 0.0f );
    layer_in = h.data();
  }

  const rl_layer& out_layer = s.layers[ n_hidden ];  // the single 1-wide output layer
  float acc = out_layer.bias[ 0 ];
  for ( std::uint32_t i = 0; i < out_layer.in_features; ++i )
    acc += out_layer.weight[ i ] * layer_in[ i ];
  return acc;
}
} // namespace rl_policy
