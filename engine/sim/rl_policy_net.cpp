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
constexpr std::uint32_t RLW1_MAX_SUPPORTED_FORMAT_VERSION = 4;
constexpr std::uint32_t RLW1_MAX_LAYERS = 16;
constexpr std::uint32_t RLW1_MIN_FEATURES = 1;
constexpr std::uint32_t RLW1_MAX_FEATURES = 4096;
constexpr float RL_LAYER_NORM_EPS = 1e-5f;  // torch's own nn.LayerNorm default, pinned explicitly

// Phase 230-02 (SCOR-01, R-A) -- v4 scorer section bounds, mirroring scripts/rl/rlw1.py's own
// constants of the same name byte-for-byte (that module's own comment: "a format-level constant,
// not derived from a live registry" -- CK1-1's eight targeted spells are a property of the WIRE
// FORMAT, not of any one build's registry, so this is hardcoded here exactly as it is hardcoded
// there, never derived from RL_ACTION_DIM or any generated constant).
constexpr std::uint32_t RLW1_V4_AIMING_SPELL_COUNT = 8;
constexpr std::uint32_t RLW1_MIN_SCORER_LAYERS = 1;
constexpr std::uint32_t RLW1_MAX_SCORER_LAYERS = RLW1_MAX_LAYERS;
constexpr std::uint32_t RLW1_MIN_SCORER_SLOTS = 1;
constexpr std::uint32_t RLW1_MAX_SCORER_SLOTS = 64;
constexpr std::uint32_t RLW1_MIN_SCORER_FEATURES = 1;
constexpr std::uint32_t RLW1_MAX_SCORER_FEATURES = RLW1_MAX_FEATURES;

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
    default:
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' declares body_type={}, must be 0 (mlp), 1 (dueling), or "
          "2 (ln-dueling)",
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

// Phase 230-02 (SCOR-01, T-230-01-01): the v4 scorer section's own per-layer parse -- the SAME
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

  rl_weights_t w;
  w.format_version = format_version;
  std::memcpy( &w.generation, data.data() + 8, 4 );
  std::memcpy( &w.exploration, data.data() + 12, 4 );

  std::uint32_t body_raw = 0;
  std::memcpy( &body_raw, data.data() + 16, 4 );
  w.body = decode_body_type( body_raw, path );

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

    w.layers.push_back( std::move( layer ) );
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

  // Phase 230-02 (SCOR-01, R-A): the v4 trailing SCORER section -- mandatory at v4, forbidden
  // below it (there is no byte layout for it at v2/v3). T-230-01-01: a positive length check for
  // the fixed-size scalar header + the fingerprint field, BEFORE any read for this section --
  // mirrors scripts/rl/rlw1.py's own read_rlw1 ordering exactly (that module is this loader's
  // normative counterpart for this format).
  if ( format_version >= 4 )
  {
    constexpr std::size_t SCORER_HEADER_BYTES = 20;  // n_scorer_layers, scorer_exploration,
                                                       // scorer_slots, scorer_features,
                                                       // scorer_context_width -- 5 x uint32/float32
    if ( offset + SCORER_HEADER_BYTES > data.size() )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' truncated -- v4 scorer section header needs {} bytes "
          "at offset {}, only {} remain",
          path, SCORER_HEADER_BYTES, offset, data.size() - offset ) );
    }
    std::uint32_t n_scorer_layers = 0;
    float         scorer_exploration = 0.0f;
    std::uint32_t scorer_slots = 0, scorer_features = 0, scorer_context_width = 0;
    std::memcpy( &n_scorer_layers, data.data() + offset, 4 );
    std::memcpy( &scorer_exploration, data.data() + offset + 4, 4 );
    std::memcpy( &scorer_slots, data.data() + offset + 8, 4 );
    std::memcpy( &scorer_features, data.data() + offset + 12, 4 );
    std::memcpy( &scorer_context_width, data.data() + offset + 16, 4 );
    offset += SCORER_HEADER_BYTES;

    if ( offset + RLW1_SHA_FIELD_BYTES > data.size() )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' truncated -- v4 scorer_feature_sha needs {} bytes at "
          "offset {}, only {} remain",
          path, RLW1_SHA_FIELD_BYTES, offset, data.size() - offset ) );
    }
    const std::string scorer_feature_sha = decode_fingerprint( data.data() + offset );
    offset += RLW1_SHA_FIELD_BYTES;

    if ( n_scorer_layers < RLW1_MIN_SCORER_LAYERS || n_scorer_layers > RLW1_MAX_SCORER_LAYERS )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' declares n_scorer_layers={}, must be {}..{} (0 is "
          "refused by name)",
          path, n_scorer_layers, RLW1_MIN_SCORER_LAYERS, RLW1_MAX_SCORER_LAYERS ) );
    }
    if ( !std::isfinite( scorer_exploration ) || scorer_exploration < 0.0f || scorer_exploration > 1.0f )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' declares scorer_exploration={}, which must be finite "
          "and within [0.0, 1.0]",
          path, scorer_exploration ) );
    }
    if ( scorer_slots < RLW1_MIN_SCORER_SLOTS || scorer_slots > RLW1_MAX_SCORER_SLOTS )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' declares scorer_slots={}, must be {}..{}",
          path, scorer_slots, RLW1_MIN_SCORER_SLOTS, RLW1_MAX_SCORER_SLOTS ) );
    }
    if ( scorer_features < RLW1_MIN_SCORER_FEATURES || scorer_features > RLW1_MAX_SCORER_FEATURES )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' declares scorer_features={}, must be {}..{}",
          path, scorer_features, RLW1_MIN_SCORER_FEATURES, RLW1_MAX_SCORER_FEATURES ) );
    }
    if ( scorer_context_width != 0 )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' declares scorer_context_width={}, must be 0 in this "
          "phase -- the observation vector does not exist at the moment the picks are computed "
          "(R-C)",
          path, scorer_context_width ) );
    }

    std::vector<rl_layer> scorer_layers;
    scorer_layers.reserve( n_scorer_layers );
    for ( std::uint32_t si = 0; si < n_scorer_layers; ++si )
      scorer_layers.push_back( parse_one_layer( data, offset, path, fmt::format( "scorer layer {}", si ) ) );

    // Scorer layers are always mlp-shaped (R-A: never a second hidden convention) -- reuses
    // expected_layer_count(mlp)==2 and hidden_layer_count(mlp, n)==n-1 verbatim, the SAME derived
    // rule the main net's own mlp branch uses.
    if ( scorer_layers.size() < expected_layer_count( rl_body_type::mlp ) )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' scorer requires at least {} layers, but declares only "
          "{}",
          path, expected_layer_count( rl_body_type::mlp ), scorer_layers.size() ) );
    }
    for ( std::size_t si = 0; si < scorer_layers.size(); ++si )
    {
      if ( scorer_layers[ si ].has_ln )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy::load_rlw1: file '{}' scorer layer {} has_ln=true, must be false -- the "
            "scorer is always mlp-shaped",
            path, si ) );
      }
    }
    for ( std::size_t si = 1; si < scorer_layers.size(); ++si )
    {
      if ( scorer_layers[ si ].in_features != scorer_layers[ si - 1 ].out_features )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy::load_rlw1: file '{}' scorer layer {} in_features={} does not match scorer "
            "layer {} out_features={}",
            path, si, scorer_layers[ si ].in_features, si - 1, scorer_layers[ si - 1 ].out_features ) );
      }
    }
    const std::uint32_t expected_scorer_in = scorer_features + RLW1_V4_AIMING_SPELL_COUNT;
    if ( scorer_layers.front().in_features != expected_scorer_in )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' scorer first layer in_features={} does not match "
          "scorer_features({}) + {} aiming spells (CK1-1)",
          path, scorer_layers.front().in_features, scorer_features, RLW1_V4_AIMING_SPELL_COUNT ) );
    }
    if ( scorer_layers.back().out_features != 1 )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' scorer last layer out_features={}, must be 1 (one "
          "score per candidate)",
          path, scorer_layers.back().out_features ) );
    }

    // Refusal: the scorer's own feature count/fingerprint against the GENERATED constants (this
    // build's live RL_TARGET_FEATURES/RL_TARGET_FEATURE_SHA, rl_policy_constants.h) -- unconditional
    // here (never opt-in the way rlw1.py's expect_scorer_features/expect_scorer_feature_sha are,
    // since the engine always has its own live generated header to compare against). A mismatch
    // means this blob was trained against a different declared feature list than this binary was
    // built against -- refused by name rather than silently scoring against the wrong columns.
    if ( scorer_features != RL_TARGET_FEATURES )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' declares scorer_features={}, does not match this "
          "build's RL_TARGET_FEATURES={}",
          path, scorer_features, RL_TARGET_FEATURES ) );
    }
    if ( scorer_feature_sha != RL_TARGET_FEATURE_SHA )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' declares scorer_feature_sha='{}', does not match this "
          "build's RL_TARGET_FEATURE_SHA='{}' -- the declared feature list disagrees (possibly "
          "reordered)",
          path, scorer_feature_sha, RL_TARGET_FEATURE_SHA ) );
    }

    w.has_scorer = true;
    w.scorer.exploration    = scorer_exploration;
    w.scorer.slots          = scorer_slots;
    w.scorer.features       = scorer_features;
    w.scorer.context_width  = scorer_context_width;
    w.scorer.feature_sha    = scorer_feature_sha;
    w.scorer.layers         = std::move( scorer_layers );

    // Load-time scratch sizing (never per decision) -- one hidden_scratch entry per scorer hidden
    // layer, and the feature buffer rl_target_select's scorer preference fills every call.
    const std::size_t n_scorer_hidden = hidden_layer_count( rl_body_type::mlp, w.scorer.layers.size() );
    w.scorer.hidden_scratch.resize( n_scorer_hidden );
    for ( std::size_t si = 0; si < n_scorer_hidden; ++si )
      w.scorer.hidden_scratch[ si ].resize( w.scorer.layers[ si ].out_features );
    w.scorer.feature_scratch.resize( scorer_features + RLW1_V4_AIMING_SPELL_COUNT );
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

  // Shape sanity against the GENERATED constants -- otherwise a shape
  // mismatch is a buffer overrun in forward() instead of an error message.
  // The first-layer check is against the RESOLVED expected width -- n_slots
  // when a gather is declared, RL_OBS_DIM otherwise -- REPLACING the old
  // bare `== RL_OBS_DIM` check, which cannot be right once a subset can
  // narrow layer 0's input.
  const std::uint32_t expected_in = n_slots ? static_cast<std::uint32_t>( n_slots ) : RL_OBS_DIM;
  if ( w.layers.front().in_features != expected_in )
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
  // hidden_layer_count(), never a fixed pair of indices.
  {
    const bool expect_ln_hidden = w.body == rl_body_type::ln_dueling;
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
} // anonymous namespace

void forward( const rl_weights_t& w, const float obs[ RL_OBS_DIM ], const std::uint8_t mask[ RL_ACTION_DIM ],
              float out_q[ RL_ACTION_DIM ] )
{
  // NET-01 (Phase 222, arm subsets): gather the full-width obs into the
  // load-time-sized obs_gather_scratch ONCE, before layer 0, when a subset
  // is declared -- otherwise layer 0 reads obs directly. w.layers.front()'s
  // in_features was validated at load time against exactly this resolved
  // width (n_slots when a gather is declared, RL_OBS_DIM otherwise), so no
  // per-decision bounds check is needed here.
  const float* x = obs;
  if ( !w.input_slots.empty() )
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

float forward_scorer( const rl_scorer_t& s, const float* in )
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
    // Scorer layers carry has_ln==false unconditionally (validated at load) -- ReLU only, no
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
