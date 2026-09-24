// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#pragma once

/*! \file rng.hpp */
/*! \defgroup SC_RNG Random Number Generator */

#include "config.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <string_view>
#ifdef RNG_STREAM_DEBUG
#include <iostream>
#endif

#include "util/timespan.hpp"

/** \ingroup SC_RNG
 * @brief Random number generation
 */
namespace rng {

double stdnormal_cdf( double u );
double stdnormal_inv( double u );

// Per-source RNG streams (tstl-sylvanas quick task 260918-psr). Behind sim_t::per_source_rng
// (default false, byte-identical OFF path). When enabled, every dice-rolling source in the sim
// (player_t, action_t, buff_t, dbc_proc_callback_t) rolls from its OWN xoshiro256plus_t stream
// instead of the one shared sim_t::_rng, reseeded once per iteration from a small tuple of
// stable inputs -- see per_source_seed() below and each rng() accessor's call site for the
// source_key construction rules (sim.hpp's per_source_rng doc comment has the full contract).

/// splitmix64 64-bit integer mixer (public-domain algorithm, Sebastiano Vigna). Used only to
/// decorrelate a handful of small, related integer inputs deterministically -- not a
/// cryptographic requirement, just enough avalanche that adjacent (seed, iteration) pairs don't
/// produce visibly related streams.
inline uint64_t splitmix64_mix( uint64_t x )
{
  x += 0x9E3779B97F4A7C15ULL;
  uint64_t z = x;
  z = ( z ^ ( z >> 30 ) ) * 0xBF58476D1CE4E5B9ULL;
  z = ( z ^ ( z >> 27 ) ) * 0x94D049BB133111EBULL;
  return z ^ ( z >> 31 );
}

/// Deterministic FNV-1a 64-bit hash over a byte string. Deliberately NOT std::hash<std::string>
/// -- that hash's algorithm/seed is an implementation detail of the standard library vendor and
/// is not documented as stable across processes, so two builds (or even two runs linked against
/// different libstdc++ versions) are not guaranteed to agree. FNV-1a here is fully specified and
/// portable, which per_source_rng's cross-process/cross-policy pairing guarantee depends on.
inline uint64_t fnv1a64( std::string_view s )
{
  uint64_t h = 14695981039346656037ULL;
  for ( unsigned char c : s )
  {
    h ^= c;
    h *= 1099511628211ULL;
  }
  return h;
}

/// Derive a per-source RNG seed from (sim seed, thread index, iteration, stable source key).
/// thread_index is folded in beyond the brief's 3-input formula (sim seed, iteration, source
/// key) as a deliberate, documented deviation (260918-psr receipt): sim_t::seed is the SAME
/// base seed inherited by every worker thread's child sim_t (see sim_t's threaded-child
/// constructors), and each child's sim_t::current_iteration is a LOCAL per-thread counter, not
/// a global iteration index -- two threads can simultaneously be at local iteration N for two
/// genuinely different fight iterations. Omitting thread_index would let those two threads
/// derive identical per-source seeds for the same source_key, re-coupling exactly the kind of
/// cross-iteration dependency this feature exists to remove. thread_index is 0 in the common
/// single-fight-per-process pattern this codebase's RL/eval tooling actually uses (see the
/// probe's P1-P4 receipt), so folding it in is a no-op there and only matters for iterations>1
/// multi-threaded invocations.
inline uint64_t per_source_seed( uint64_t sim_seed, int thread_index, int iteration, std::string_view source_key )
{
  uint64_t x = splitmix64_mix( sim_seed );
  x = splitmix64_mix( x ^ splitmix64_mix( static_cast<uint64_t>( thread_index ) ) );
  x = splitmix64_mix( x ^ splitmix64_mix( static_cast<uint64_t>( iteration ) ) );
  x = splitmix64_mix( x ^ fnv1a64( source_key ) );
  return x;
}

// CDF cached truncated timespan_t gaussian distribution
// This is ~2x slower than non-truncated basic_rng_t::gauss( double, double )
struct truncated_gauss_t
{
private:
  double _min_cdf = 0.0;
  double _max_cdf = 0.0;
  bool _cdf_set = false;
#ifndef NDEBUG
  size_t _count = 0;
#endif

public:
  timespan_t mean;
  timespan_t stddev;
  timespan_t min;
  timespan_t max;

  truncated_gauss_t( timespan_t m, timespan_t s, timespan_t min = 0_ms, timespan_t max = timespan_t::min() )
    : mean( m ), stddev( s ), min( min ), max( max )
  {}

  double min_cdf() const { return _min_cdf; }
  double max_cdf() const { return _max_cdf; }
  void calculate_cdf();
#ifndef NDEBUG
  size_t count() const { return _count; }
#endif
};

// Random-roll recorder hook (tstl-sylvanas phase 250, plan 250-01, REC-01/02/03). Pure
// observer: real()/roll() below call into this interface strictly AFTER a value already
// computed the normal way is known, and neither this interface nor anything that implements it
// may call an rng method, reset() a stream, schedule an event, or otherwise change what a roll
// draws or returns -- see basic_rng_t::real()/roll() doc comments for the exact call sites, and
// rl_rng_record.hpp (implemented by rl_rng_record::recorder_t in rl_rng_record.cpp) for the one
// concrete implementation. Deliberately dependency-free: this file includes nothing beyond
// <cstdint> to declare it, so the choke point stays as cheap as a null-pointer check when no
// recorder is attached (D-03).
struct rl_draw_sink_t
{
  virtual ~rl_draw_sink_t() = default;

  // Called from real(), after the draw is computed, immediately before it is returned.
  // `roller_slot` is the calling stream's own rl_roller_id_ member, passed by reference so an
  // unregistered stream can be numbered lazily on its first draw; `draw_counter` is that
  // stream's rl_trace_n_ AFTER this draw (its physical draw ordinal); `roller_override` is the
  // stream's rl_roller_override_ (0 = none, otherwise the logical roller this one draw should
  // be attributed to instead of the stream itself -- a raid event borrowing the shared stream
  // uses this); `raw` is the value real() is about to return.
  virtual void on_draw( std::uint32_t& roller_slot, const std::uint64_t& draw_counter,
                         std::uint32_t roller_override, double raw ) = 0;

  // Called from roll( chance ), after the comparison, only when a draw actually happened
  // (chance strictly between 0 and 1 -- see roll()'s early-return branches below).
  // `draw_ordinal` is the SAME draw_counter value on_draw() just saw for this draw, so an
  // implementation can find and annotate the entry on_draw() already wrote.
  virtual void on_roll_outcome( std::uint32_t roller, std::uint64_t draw_ordinal, double chance,
                                 bool outcome ) = 0;

  // Called from roll( chance ) when chance <= 0 or chance >= 1 -- no draw happens and no entry
  // is written for it, but an implementation still counts it (R-05: "rolls that never draw").
  // Same roller_slot/draw_counter meaning as on_draw() above.
  virtual void on_no_draw_roll( std::uint32_t& roller_slot, const std::uint64_t& draw_counter ) = 0;
};

// Process-wide. Non-null only between rl_rng_record::open_and_write_header() and
// rl_rng_record::write_footer() (or the recorder object's destructor) -- see rl_rng_record.hpp.
// Every read of this pointer in real()/roll() below is the ENTIRE cost this hook adds when
// rl_rng_record= is unset: one pointer load and a branch (D-03, D-17).
inline rl_draw_sink_t* rl_draw_sink = nullptr;

// Sentinel: a stream whose rl_roller_id_ has never been assigned a real roller number -- either
// the recorder has never been open for this stream, or it drew nothing while open before this
// check. rl_rng_record::recorder_t::on_draw() numbers a stream lazily the first time it sees
// this value.
inline constexpr std::uint32_t RL_ROLLER_UNREGISTERED = 0xFFFFFFFFu;

/**\ingroup SC_RNG
 * @brief Random number generator wrapper around an rng engine
 *
 * Implements different distribution outputs ( uniform, gauss, etc. )
 */
template <typename Engine>
struct basic_rng_t
{
public:
  explicit basic_rng_t() = default;

  basic_rng_t(const basic_rng_t&) = delete;
  basic_rng_t& operator=(const basic_rng_t&) = delete;

  /// Return engine name
  const char* name() const {
    return engine.name();
  }

  /// Seed rng engine
  void seed( uint64_t s ) {
    engine.seed( s );
  }

  /// Reseed using current state
  uint64_t reseed();

  /// Reset any state
  void reset();

  /// Uniform distribution in range [0..1)
  double real();

  // TEMPORARY trace instrumentation (tstl-sylvanas quick task 260919-scb, T1 follow-up).
  // Per-instance draw counter, incremented unconditionally in real() (the single choke point
  // range()/roll()/gauss()/exponential() all funnel through), reset to 0 alongside the rest of
  // this instance's state in reset(). Cheap (one integer increment per draw) and always
  // maintained regardless of RL_TRACE_RNG so the printed value at any call site is exact --
  // only the PRINTING is gated on the env var, at each call site, not this counter itself.
  // Remove this block (and its two touch points in real()/reset() below) once the raid-events
  // divergence traced in SCB-RECEIPT.md Section 7/8 is closed.
  uint64_t rl_trace_n() const { return rl_trace_n_; }

  // Roller identity (tstl-sylvanas phase 250, plan 250-01, REC-01/02/03/D-05). Observer only:
  // never read by any roll, reset by seed()/reseed() the way rl_trace_n_ is not either -- see
  // reset()'s own doc comment below for why rl_roller_id_/rl_roller_override_ are NOT cleared
  // there. Set once, at registration (rl_rng_record::register_roller/register_action_roller) or
  // lazily on first draw while the recorder is open (rl_rng_record::recorder_t::on_draw()).
  std::uint32_t rl_roller_id() const { return rl_roller_id_; }
  void rl_set_roller_id( std::uint32_t id ) { rl_roller_id_ = id; }

  // Logical-roller override (D-06/R-01): 0 = none (this stream's own rl_roller_id_ is the
  // logical roller), otherwise the roller number a single draw should be attributed to instead
  // -- a raid event borrowing the shared sim stream sets this immediately around its one draw
  // and clears it right after, never leaving it set across any other code.
  std::uint32_t rl_roller_override() const { return rl_roller_override_; }
  void rl_set_roller_override( std::uint32_t roller ) { rl_roller_override_ = roller; }

  // Read-only accessor to this stream's own draw counter -- rl_rng_record::register_roller()
  // stores this pointer so the recorder can read a roller's draw count at fight begin/end
  // without ever calling an rng method on it. "Observer only: never read by any roll."
  const uint64_t& rl_draw_counter() const { return rl_trace_n_; }

  /// Bernoulli Distribution
  bool roll( double chance );

  /// Uniform distribution in the range [min..max)
  double range( double min, double max );

  /// Uniform distribution in the range [0.0..max)
  double range( double max );

  /// Uniform distribution in the range [min..max)
  template <typename T, typename = std::enable_if_t<std::numeric_limits<T>::is_integer>>
  T range( T min, T max )
  {
    return static_cast<T>( std::floor( range( static_cast<double>( min ), static_cast<double>( max ) ) ) );
  }

  /// Uniform distribution in the range [0.0..max)
  template <typename T, typename = std::enable_if_t<std::numeric_limits<T>::is_integer>>
  T range( T max )
  {
    return range<T>( T{}, max );
  }

  // Uniform distribution in the range [first iterator..last iterator)
  template <
    typename T,
    typename std::enable_if_t<
      std::is_base_of_v<std::forward_iterator_tag, typename std::iterator_traits<T>::iterator_category>, int> = 0>
  T range( T first, T last )
  {
    return first + range( std::distance( first, last ) );
  }

  // Uniform distribution across [container.front()..container.back()]
  template <typename T, typename U = std::remove_reference_t<decltype( *std::begin( std::declval<T&>() ) )>>
  U& range( T& container )
  {
    return *range( container.begin(), container.end() );
  }

  /// Gaussian Distribution, Non-truncated
  double gauss( double mean, double stddev );

  /// Truncated Gaussian Distribution
  double gauss_ab( double mean, double stddev, double min, double max );
  double gauss_a( double mean, double stddev, double min );
  double gauss_b( double mean, double stddev, double max );

  /// Exponential Distribution
  double exponential( double nu );

  /// Exponentially Modified Gaussian Distribution
  double exgauss( double gauss_mean, double gauss_stddev, double exp_nu );

  template <typename T, typename = std::enable_if_t<!std::numeric_limits<T>::is_signed>>
  T gauss( T mean, T stddev )
  {
    return static_cast<T>( gauss_a( static_cast<double>( mean ), static_cast<double>( stddev ), 0.0 ) );
  }

  template <typename T, typename = std::enable_if_t<!std::numeric_limits<T>::is_signed>>
  T exgauss( T gauss_mean, T gauss_stddev, T exp_nu )
  {
    return static_cast<T>( gauss_a( static_cast<double>( gauss_mean ), static_cast<double>( gauss_stddev ), 0.0 ) +
                           exponential( static_cast<double>( exp_nu ) ) );
  }

  /// Timespan uniform distribution in the range [min..max)
  timespan_t range( timespan_t min, timespan_t max );

  /// Timespan Truncated Gaussian Distribution
  timespan_t gauss( timespan_t mean, timespan_t stddev );
  timespan_t gauss_ab( timespan_t mean, timespan_t stddev, timespan_t min, timespan_t max );
  timespan_t gauss_a( timespan_t mean, timespan_t stddev, timespan_t min );
  timespan_t gauss_b( timespan_t mean, timespan_t stddev, timespan_t max );

  /// Timespan Exponential Distribution
  timespan_t exponential( timespan_t nu );

  /// Timespan exponentially Modified Gaussian Distribution
  timespan_t exgauss( timespan_t mean, timespan_t stddev, timespan_t nu );

  /// Timespan CDF-cached Truncated Gaussian Distribution
  timespan_t gauss( truncated_gauss_t& g );
  timespan_t exgauss( truncated_gauss_t& g, timespan_t nu );

  /// Timespan CDF-cached Truncated Gaussian Distribution with compile-time mean and stddev in milliseconds
  template <unsigned MEAN, unsigned STDDEV>
  timespan_t gauss();

  /// Shuffle a range: https://en.wikipedia.org/wiki/Fisher%E2%80%93Yates_shuffle
  template <
    typename T,
    typename std::enable_if_t<
      std::is_same_v<typename std::iterator_traits<T>::iterator_category, std::random_access_iterator_tag>, int> = 0>
  void shuffle( T first, T last )
  {
    using diff_t = typename std::iterator_traits<T>::difference_type;
    diff_t n = last - first;
    for ( diff_t i = 0; i < n - 1; i++ )
      std::swap( first[ i ], first[ range( i, n ) ] );
  }

private:
  Engine engine;
  // Allow re-use of unused ( but necessary ) random number of a previous call to gauss()
  double gauss_pair_value = 0.0;
  bool   gauss_pair_use = false;
#ifdef RNG_STREAM_DEBUG
  uint64_t n = 0U;
#endif
  // TEMPORARY (260919-scb T1) -- see rl_trace_n() above.
  uint64_t rl_trace_n_ = 0U;
  // Roller identity (250-01, D-05/D-06). Default RL_ROLLER_UNREGISTERED: numbered lazily on
  // first draw while a recorder is open, or set explicitly by rl_rng_record::register_roller().
  // Deliberately NOT cleared by reset() -- a stream's roller number is a per-object identity
  // assigned once per run, not per-fight state (mirrors rl_trace_n_'s own "TEMPORARY" note: this
  // one is not temporary, but the "never reset" rule is the same kind of deliberate choice).
  std::uint32_t rl_roller_id_ = RL_ROLLER_UNREGISTERED;
  // Logical-roller override (250-01, D-06/R-01). 0 = none. Also never cleared by reset() --
  // callers that set it (raid-event draw wrappers) are responsible for clearing it themselves
  // immediately after their one draw.
  std::uint32_t rl_roller_override_ = 0;
};

/// Reseed using current state
template <typename Engine>
uint64_t basic_rng_t<Engine>::reseed()
{
  const uint64_t s = engine.next();
#ifdef RNG_STREAM_DEBUG
  ++n;
  std::cout << fmt::format( "[RNG] fn=reseed() engine={} n={} next={}",
                           engine.name(), n, s ) << std::endl;
#endif
  seed( s );
  reset();
  return s;
}

/// Reset state
template <typename Engine>
void basic_rng_t<Engine>::reset()
{
  gauss_pair_value = 0;
  gauss_pair_use = false;
#ifdef RNG_STREAM_DEBUG
  n = 0U;
#endif
  rl_trace_n_ = 0U;  // TEMPORARY (260919-scb T1)
}

// ==========================================================================
// Probability Distributions
// ==========================================================================

/// Uniform distribution in range [0..1)
template <typename Engine>
inline double basic_rng_t<Engine>::real()
{
  /// MAGIC! http://en.wikipedia.org/wiki/Double-precision_floating-point_format
  uint64_t ui64 = engine.next();
  ++rl_trace_n_;  // TEMPORARY (260919-scb T1) -- see rl_trace_n() doc comment
#ifdef RNG_STREAM_DEBUG
  auto u64raw = ui64;
#endif
  ui64 &= 0x000fffffffffffff;
  ui64 |= 0x3ff0000000000000;
  union { uint64_t ui64; double d; } u;
  u.ui64 = ui64;
#ifdef RNG_STREAM_DEBUG
  ++n;
  std::cout << fmt::format( "[RNG] fn=real() engine={} n={} next={} value={}",
                           engine.name(), n, u64raw, u.d - 1.0 ) << std::endl;
#endif
  const double value = u.d - 1.0;
  // Random-roll recorder hook (250-01, D-03/D-04/D-17): pure observation AFTER value is
  // computed the normal way above -- see rl_draw_sink_t's own doc comment for the promise this
  // keeps. One pointer check when no recorder is attached.
  if ( rng::rl_draw_sink )
    rng::rl_draw_sink->on_draw( rl_roller_id_, rl_trace_n_, rl_roller_override_, value );
  return value;
}

/// Bernoulli Distribution
template <typename Engine>
bool basic_rng_t<Engine>::roll( double chance )
{
  if ( chance <= 0 )
  {
    if ( rng::rl_draw_sink )
      rng::rl_draw_sink->on_no_draw_roll( rl_roller_id_, rl_trace_n_ );
    return false;
  }
  if ( chance >= 1 )
  {
    if ( rng::rl_draw_sink )
      rng::rl_draw_sink->on_no_draw_roll( rl_roller_id_, rl_trace_n_ );
    return true;
  }
  const bool ok = real() < chance;
  // Random-roll recorder hook (250-01, D-02/D-04): the chance is known here, one level above
  // real()'s own hook call -- captured and attached to the SAME draw via draw_ordinal. Passes
  // rl_roller_id_ (the physical stream), not any override: the recorder matches this call back
  // to the ROLL entry on_draw() just wrote by (stream, draw_ordinal), never by logical roller.
  if ( rng::rl_draw_sink )
    rng::rl_draw_sink->on_roll_outcome( rl_roller_id_, rl_trace_n_, chance, ok );
  return ok;
}

/// Uniform distribution in the range [min..max)
template <typename Engine>
double basic_rng_t<Engine>::range( double min, double max )
{
  assert( min <= max );
  return min + real() * ( max - min );
}

/// Uniform distribution in the range [0.0..max)
template <typename Engine>
double basic_rng_t<Engine>::range( double max )
{
  assert( 0.0 <= max );
  return real() * max;
}

/**
 * @brief Gaussian Distribution
 *
 * This code adapted from ftp://ftp.taygeta.com/pub/c/boxmuller.c
 * Implements the Polar form of the Box-Muller Transformation
 *
 * (c) Copyright 1994, Everett F. Carter Jr.
 *     Permission is granted by the author to use
 *     this software for any application provided this
 *     copyright notice is preserved.
 */
template <typename Engine>
double basic_rng_t<Engine>::gauss( double mean, double stddev )
{
  assert(stddev >= 0 && "Calling gauss with negative stddev");

  if ( stddev == 0 )
    return mean;

  if ( gauss_pair_use )
  {
    gauss_pair_use = false;
    return mean + gauss_pair_value * stddev;
  }

  double x1, x2, w, y1, y2;
  do
  {
    x1 = 2.0 * real() - 1.0;
    x2 = 2.0 * real() - 1.0;
    w = x1 * x1 + x2 * x2;
  }
  while ( w >= 1.0 || w == 0.0 );

  w = std::sqrt( ( -2.0 * std::log( w ) ) / w );
  y1 = x1 * w;
  y2 = x2 * w;

  gauss_pair_value = y2;
  gauss_pair_use = true;

  return mean + y1 * stddev;
}

template <typename Engine>
double basic_rng_t<Engine>::gauss_ab( double mean, double stddev, double min, double max )
{
  assert( stddev >= 0.0 && "Stddev must be non-negative." );
  assert( min <= max && "Minimum must be less than or equal to maximum." );

  if ( stddev == 0.0 )
  {
    assert( mean >= min && mean <= max && "Mean must be contained within the interval [min, max]" );
    return mean;
  }

  if ( min == max )
    return min;

  double min_cdf      = stdnormal_cdf( ( min - mean ) / stddev );
  double max_cdf      = stdnormal_cdf( ( max - mean ) / stddev );
  double uniform      = real();
  double rescaled_cdf = min_cdf + uniform * ( max_cdf - min_cdf );
  return mean + stddev * stdnormal_inv( rescaled_cdf );
}

template <typename Engine>
double basic_rng_t<Engine>::gauss_a( double mean, double stddev, double min )
{
  return gauss_ab( mean, stddev, min, std::numeric_limits<double>::infinity() );
}

template <typename Engine>
double basic_rng_t<Engine>::gauss_b( double mean, double stddev, double max )
{
  return gauss_ab( mean, stddev, -std::numeric_limits<double>::infinity(), max );
}

/// Exponential Distribution
template <typename Engine>
double basic_rng_t<Engine>::exponential( double nu )
{
  double x;
  do { x = real(); } while ( x >= 1.0 ); // avoid ln(k) where k <= 0. this should be guaranteed by `real()`, but just in case
  return - std::log( 1 - x ) * nu;
}

/// Exponentially Modified Gaussian Distribution
template <typename Engine>
double basic_rng_t<Engine>::exgauss( double gauss_mean, double gauss_stddev, double exp_nu )
{
  return gauss( gauss_mean, gauss_stddev ) + exponential( exp_nu );
}

/// Timespan uniform distribution in the range [min..max)
template <typename Engine>
timespan_t basic_rng_t<Engine>::range( timespan_t min, timespan_t max )
{
  return timespan_t::from_native( range( timespan_t::to_native( min ), timespan_t::to_native( max ) ) );
}

/// Timespan Gaussian Distribution
template <typename Engine>
timespan_t basic_rng_t<Engine>::gauss( timespan_t mean, timespan_t stddev )
{
  return timespan_t::from_native( gauss_a( static_cast<double>( timespan_t::to_native( mean ) ),
                                           static_cast<double>( timespan_t::to_native( stddev ) ),
                                           0.0 ) );
}

template <typename Engine>
timespan_t basic_rng_t<Engine>::gauss_ab( timespan_t mean, timespan_t stddev, timespan_t min, timespan_t max )
{
  return timespan_t::from_native( gauss_ab( static_cast<double>( timespan_t::to_native( mean ) ),
                                            static_cast<double>( timespan_t::to_native( stddev ) ),
                                            static_cast<double>( timespan_t::to_native( min ) ),
                                            static_cast<double>( timespan_t::to_native( max ) ) ) );
}

template <typename Engine>
timespan_t basic_rng_t<Engine>::gauss_a( timespan_t mean, timespan_t stddev, timespan_t min )
{
  return timespan_t::from_native( gauss_a( static_cast<double>( timespan_t::to_native( mean ) ),
                                           static_cast<double>( timespan_t::to_native( stddev ) ),
                                           static_cast<double>( timespan_t::to_native( min ) ) ) );
}

template <typename Engine>
timespan_t basic_rng_t<Engine>::gauss_b( timespan_t mean, timespan_t stddev, timespan_t max )
{
  return timespan_t::from_native( gauss_ab( static_cast<double>( timespan_t::to_native( mean ) ),
                                            static_cast<double>( timespan_t::to_native( stddev ) ),
                                            0.0,
                                            static_cast<double>( timespan_t::to_native( max ) ) ) );
}

template <typename Engine>
timespan_t basic_rng_t<Engine>::exponential( timespan_t nu )
{
  return timespan_t::from_native( exponential( static_cast<double>( timespan_t::to_native( nu ) ) ) );
}

/// Timespan exponentially Modified Gaussian Distribution
template <typename Engine>
timespan_t basic_rng_t<Engine>::exgauss( timespan_t mean, timespan_t stddev, timespan_t nu )
{
  return gauss( mean, stddev ) + exponential( nu );
}

template <typename Engine>
timespan_t basic_rng_t<Engine>::gauss( truncated_gauss_t& g )
{
  assert( g.stddev >= 0_ms && "Stddev must be non-negative." );
  if ( g.stddev == 0_ms )
    return g.mean;
  if ( g.min == g.max )
    return g.min;

  g.calculate_cdf();

  double rescaled = g.min_cdf() + real() * ( g.max_cdf() - g.min_cdf() );
  return timespan_t::from_native( timespan_t::to_native( g.mean ) +
                                  timespan_t::to_native( g.stddev ) * stdnormal_inv( rescaled ) );
}

template <typename Engine>
timespan_t basic_rng_t<Engine>::exgauss( truncated_gauss_t& g, timespan_t nu )
{
  return gauss( g ) + exponential( nu );
}

template <typename Engine>
template <unsigned MEAN, unsigned STDDEV>
timespan_t basic_rng_t<Engine>::gauss()
{
  if constexpr ( STDDEV == 0 )
  {
    return timespan_t::from_native( MEAN );
  }
  else
  {
    static constexpr auto mean = timespan_t::to_native( timespan_t::from_millis( MEAN ) );
    static constexpr auto stddev = timespan_t::to_native( timespan_t::from_millis( STDDEV ) );

    static const double min_cdf = stdnormal_cdf( ( 0.0 - mean ) / stddev );
    static const double max_cdf = stdnormal_cdf( ( std::numeric_limits<double>::infinity() - mean ) / stddev );

    assert( min_cdf == stdnormal_cdf( ( 0.0 - mean ) / stddev ) );
    assert( max_cdf == stdnormal_cdf( ( std::numeric_limits<double>::infinity() - mean ) / stddev ) );

    double rescaled = min_cdf + real() * ( max_cdf - min_cdf );
    return timespan_t::from_native( mean + stddev * stdnormal_inv( rescaled ) );
  }
}

/// RNG Engines

/**
 * @brief XORSHIFT-128 Random Number Generator
 *
 * All credit goes to Sebastiano Vigna (vigna@acm.org) @2014
 * http://xorshift.di.unimi.it/
 */
struct xorshift128_t
{
  uint64_t next() noexcept;
  void seed( uint64_t start ) noexcept;
  const char* name() const noexcept;
private:
  std::array<uint64_t, 2> s;
};

/**
 * @brief xoshiro256+ Random Number Generator
 *
 * If, however, one has to generate only 64-bit floating-point numbers (by extracting the upper 53 bits) xoshiro256+
 * is a slightly (≈15%) faster [compared to xoshiro256** or xoshiro256++] generator with analogous statistical
 * properties.
 *
 * All credit goes to David Blackman and Sebastiano Vigna (vigna@acm.org) @2018
 * http://prng.di.unimi.it/
 */
struct xoshiro256plus_t
{
  uint64_t next() noexcept;
  void seed( uint64_t start ) noexcept;
  const char* name() const noexcept;
private:
  std::array<uint64_t, 4> s;
};

/**
 * @brief XORSHIFT-1024 Random Number Generator
 *
 * All credit goes to Sebastiano Vigna (vigna@acm.org) @2014
 * http://xorshift.di.unimi.it/
 */
struct xorshift1024_t
{
  uint64_t next() noexcept;
  void seed( uint64_t start ) noexcept;
  const char* name() const noexcept;
private:
  std::array<uint64_t, 16> s;
  int p;
};

// "Default" rng
// Explicitly *NOT* a type alias to allow forward declaraions
struct rng_t : public basic_rng_t<xoshiro256plus_t> {};

} // rng
