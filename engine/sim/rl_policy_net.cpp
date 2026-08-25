// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// In-process RL transport, weights loader + MLP forward + masked argmax --
// tstl-sylvanas phase 210, plan 210-04. `load_rlw1` parses the RLW1 v1
// binary layout pinned in 210-02-PLAN.md ("Pinned RLW1 layout") and
// implemented on the Python side by scripts/rl/rlw1.py -- this loader
// refuses on the same conditions that reader refuses on (bad magic, short
// file, unknown version, out-of-range dims, truncated payload), each with
// its own named error, little-endian throughout (this format is a portable
// on-disk layout, not an in-memory dump; declared explicitly here via raw
// byte reads on an x86-64/ARM64 little-endian host, matching the Python
// writer's explicit `<` struct format).
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
// DELIBERATE GAP IN THIS SLICE (objective, 210-04-PLAN.md): load_rlw1
// validates magic/version/bounds/shapes only. Plan 210-06 adds the D-01
// exploration refusal and D-02's three fingerprint refusals at the marked
// comment below -- no C++ exploration code exists anywhere in this file.

#include "sim/rl_policy.hpp"

#include "fmt/format.h"
#include "util/util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>

namespace rl_policy
{
namespace
{
constexpr std::size_t RLW1_HEADER_BYTES = 260;
constexpr std::size_t RLW1_SHA_FIELD_BYTES = 80;
constexpr std::uint32_t RLW1_SUPPORTED_FORMAT_VERSION = 1;
constexpr std::uint32_t RLW1_MAX_LAYERS = 16;
constexpr std::uint32_t RLW1_MIN_FEATURES = 1;
constexpr std::uint32_t RLW1_MAX_FEATURES = 4096;

std::string decode_fingerprint( const unsigned char* p )
{
  // Strip trailing NUL padding, mirroring rlw1.py's _decode_fingerprint.
  const char* c = reinterpret_cast<const char*>( p );
  std::size_t len = 0;
  while ( len < RLW1_SHA_FIELD_BYTES && c[ len ] != '\0' )
    ++len;
  return std::string( c, len );
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
  if ( format_version != RLW1_SUPPORTED_FORMAT_VERSION )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' has format_version={}, but this loader supports only "
        "format_version={}",
        path, format_version, RLW1_SUPPORTED_FORMAT_VERSION ) );
  }

  rl_weights_t w;
  w.format_version = format_version;
  std::memcpy( &w.generation, data.data() + 8, 4 );
  std::memcpy( &w.exploration, data.data() + 12, 4 );

  w.obs_schema_sha = decode_fingerprint( data.data() + 16 );
  w.mask_rules_sha = decode_fingerprint( data.data() + 96 );
  w.action_space_sha = decode_fingerprint( data.data() + 176 );

  std::uint32_t n_layers = 0;
  std::memcpy( &n_layers, data.data() + 256, 4 );
  if ( n_layers < 1 || n_layers > RLW1_MAX_LAYERS )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' declares n_layers={}, must be 1..{}",
        path, n_layers, RLW1_MAX_LAYERS ) );
  }

  std::size_t offset = RLW1_HEADER_BYTES;
  for ( std::uint32_t li = 0; li < n_layers; ++li )
  {
    if ( offset + 8 > data.size() )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' truncated -- layer {}'s dimensions header needs 8 "
          "bytes at offset {}, only {} remain",
          path, li, offset, data.size() - offset ) );
    }

    std::uint32_t in_features = 0, out_features = 0;
    std::memcpy( &in_features, data.data() + offset, 4 );
    std::memcpy( &out_features, data.data() + offset + 4, 4 );

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
    offset += 8;

    // T-210-04/T-210-11/T-210-12: range-check FIRST (above), THEN compute
    // the implied byte count in a 64-bit type and verify it against the
    // ACTUAL remaining file length BEFORE any read/resize -- a positive
    // length check, never a try/catch around an over-read. This ordering
    // is the phase's single most likely memory-safety defect.
    const std::uint64_t weight_count =
        static_cast<std::uint64_t>( out_features ) * static_cast<std::uint64_t>( in_features );
    const std::uint64_t weight_bytes = weight_count * 4;
    const std::uint64_t bias_bytes = static_cast<std::uint64_t>( out_features ) * 4;
    const std::uint64_t needed = weight_bytes + bias_bytes;

    if ( offset + needed > data.size() )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy::load_rlw1: file '{}' truncated mid-layer -- layer {} ({}x{}) needs {} "
          "bytes of payload at offset {}, but the file is short by {} bytes",
          path, li, in_features, out_features, needed, offset, ( offset + needed ) - data.size() ) );
    }

    rl_layer layer;
    layer.in_features = in_features;
    layer.out_features = out_features;
    layer.weight.resize( weight_count );
    std::memcpy( layer.weight.data(), data.data() + offset, static_cast<std::size_t>( weight_bytes ) );
    offset += static_cast<std::size_t>( weight_bytes );
    layer.bias.resize( out_features );
    std::memcpy( layer.bias.data(), data.data() + offset, static_cast<std::size_t>( bias_bytes ) );
    offset += static_cast<std::size_t>( bias_bytes );

    w.layers.push_back( std::move( layer ) );
  }

  // Shape sanity against the GENERATED constants -- otherwise a shape
  // mismatch is a buffer overrun in forward() instead of an error message.
  if ( w.layers.front().in_features != RL_OBS_DIM )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' first layer in_features={} does not match RL_OBS_DIM={}",
        path, w.layers.front().in_features, RL_OBS_DIM ) );
  }
  if ( w.layers.back().out_features != RL_ACTION_DIM )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' last layer out_features={} does not match RL_ACTION_DIM={}",
        path, w.layers.back().out_features, RL_ACTION_DIM ) );
  }

  // CR-02 fix (210-CR-FIX): forward() is a fixed three-layer MLP -- it
  // hard-codes w.layers[0]/[1]/[2] via operator[], never .at() -- so
  // anything other than exactly 3 layers is unchecked UB there (a
  // n_layers=1 or n_layers=2 blob still passes both endpoint checks above,
  // since front()/back() alias the same one or two elements). Refuse here,
  // at load time, where an out-of-range value is a named error instead of a
  // heap read past the vector.
  if ( w.layers.size() != 3 )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::load_rlw1: file '{}' declares {} layers, but this build's forward() "
        "implements exactly 3 (in -> h0 -> h1 -> out)",
        path, w.layers.size() ) );
  }

  // Interior dimensions must chain -- nothing else checks this, and
  // forward() indexes h0/h1 by the NEXT layer's in_features, so a mismatch
  // here is a heap over-read (h0/h1 sized off ONE layer's out_features,
  // walked using the FOLLOWING layer's in_features) rather than an error.
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

  // WR-02 fix, cheap half (210-CR-FIX): size forward()'s hidden-layer
  // scratch buffers ONCE, here at load time -- w.layers.size() == 3 is
  // already confirmed above, so layers[0]/[1] are the two hidden layers
  // forward() writes h0/h1 for.
  w.h0_scratch.resize( w.layers[ 0 ].out_features );
  w.h1_scratch.resize( w.layers[ 1 ].out_features );

  return w;
}

// ---------------------------------------------------------------------------
// forward -- h0 = relu(W0*obs + b0), h1 = relu(W1*h0 + b1), q = W2*h1 + b2.
// ReLU between hidden layers only, no output activation, float throughout.
// ---------------------------------------------------------------------------

void forward( const rl_weights_t& w, const float obs[ RL_OBS_DIM ], float out_q[ RL_ACTION_DIM ] )
{
  const rl_layer& l0 = w.layers[ 0 ];
  const rl_layer& l1 = w.layers[ 1 ];
  const rl_layer& l2 = w.layers[ 2 ];

  // WR-02 fix, cheap half (210-CR-FIX): h0/h1 are load-time-sized scratch
  // buffers on `w` (rl_policy.hpp's `mutable std::vector<float>
  // h0_scratch/h1_scratch`, sized by load_rlw1) instead of two fresh
  // std::vector<float> allocations per decision boundary -- this is the
  // hottest path the fork has. Sizes were fixed at load time, so the loop
  // bodies below are unchanged; only the storage moved.
  std::vector<float>& h0 = w.h0_scratch;
  for ( std::uint32_t o = 0; o < l0.out_features; ++o )
  {
    float acc = l0.bias[ o ];
    for ( std::uint32_t i = 0; i < l0.in_features; ++i )
      acc += l0.weight[ o * l0.in_features + i ] * obs[ i ];
    h0[ o ] = std::max( acc, 0.0f );
  }

  std::vector<float>& h1 = w.h1_scratch;
  for ( std::uint32_t o = 0; o < l1.out_features; ++o )
  {
    float acc = l1.bias[ o ];
    for ( std::uint32_t i = 0; i < l1.in_features; ++i )
      acc += l1.weight[ o * l1.in_features + i ] * h0[ i ];
    h1[ o ] = std::max( acc, 0.0f );
  }

  for ( std::uint32_t o = 0; o < l2.out_features; ++o )
  {
    float acc = l2.bias[ o ];
    for ( std::uint32_t i = 0; i < l2.in_features; ++i )
      acc += l2.weight[ o * l2.in_features + i ] * h1[ i ];
    out_q[ o ] = acc;
  }
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
} // namespace rl_policy
