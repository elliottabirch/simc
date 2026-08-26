// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// In-process RL transport, Stage 1/2 seam (tstl-sylvanas phase 210, plan
// 210-04, XPORT-01/02/03/05). Pinned in 210-04-PLAN.md's "Pinned C++
// interface" section so plans 210-05/06/07 implement against it with no
// coordination round -- the SPLIT below is not negotiable, field names are
// as written there.
//
// NORMATIVE SOURCE: this file, `rl_policy_obs.cpp` and `rl_policy_net.cpp`
// are a C++ implementation of `scripts/rl/obs.py` and `scripts/rl/mask.py`
// (this repo, not the fork) -- those two Python modules are NORMATIVE; this
// C++ implements them, never the other way around (PITFALLS Pitfall 9(b)).
// The reciprocal "this Python is normative for the C++" comment on the
// Python side is plan 210-05's.
//
// Absent and zero are DIFFERENT everywhere in this schema (obs slot 1's
// missing value is 2.0, not 0.0) -- hence a has_* flag beside every
// optional field below. This is a plain-old-data seam deliberately: a
// standalone test executable (plan 210-07, D-05) that runs with no sim and
// no fight cannot call builders taking `player_t*` (a `player_t` requires a
// `sim_t` and full actor init), so `read_state(player_t*)` fills this POD
// and everything downstream of it (`build_obs`/`build_mask`/`build_wait`)
// is PURE over the POD -- that purity is what the standalone executable and
// Phase 211's differ exercise.

#pragma once
#include "sim/rl_policy_constants.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct player_t;

namespace rl_policy
{
// ---- Stage 1 output / Stage 2 input: plain old data, NO engine types ----
// Absent and zero are DIFFERENT everywhere in this schema (obs slot 1's
// missing value is 2.0, not 0.0) -- hence a has_* flag beside every
// optional field.

struct buff_reading
{
  std::string name;                  // registry-supplied at lookup time, never a literal here
  bool   present   = false;
  double stacks    = 0.0;
  bool   has_stacks = false;
  double remains    = 0.0;  bool has_remains = false;   // quick task 260826-38t, D-2R slot 6
  bool   permanent = false;          // leaf null + sibling "permanent": true (obs.py:132-145)
};

struct cooldown_reading
{
  std::string name;
  bool   present                = false;
  double remains                = 0.0;  bool has_remains                = false;
  double recharge_time          = 0.0;  bool has_recharge_time          = false;
  double charges                = 0.0;  bool has_charges                = false;
  double charges_fractional     = 0.0;  bool has_charges_fractional     = false;
  int    max_charges            = 1;
};

struct rl_state_t
{
  double t                   = 0.0;   // sim->current_time().total_seconds(); slot 4 derives from it
  double gcd_remains         = 0.0;   bool has_gcd_remains        = false;
  double swing_mh_remains    = 0.0;   bool has_swing_mh_remains   = false;
  double swing_oh_remains    = 0.0;   bool has_swing_oh_remains   = false;   // quick task 260826-38t, D-2R slot 8
  double active_enemies      = 0.0;   bool has_active_enemies     = false;
  bool   boundary_is_foreground = true;   // converted ONCE from execute_type by the caller

  std::vector<buff_reading>     buffs;
  std::vector<cooldown_reading> cooldowns;

  const buff_reading*     find_buff( const char* name ) const;
  const cooldown_reading* find_cooldown( const char* name ) const;
};

// WR-05 fix (210-CR-FIX): `source` is a `std::string`, not a `const char*`
// into shared storage. `wait_result` is part of the pinned POD interface --
// a `const char*` here previously pointed into a function-local `static
// thread_local std::string` inside build_wait() that the NEXT build_wait()
// call on the same thread overwrites (and may reallocate); safe only while
// exactly one `wait_result` is alive at a time and consumed synchronously.
// That invariant is not enforced by the type, so the first future caller to
// hold two `wait_result`s concurrently (Phase 212's logger, or any wait-arm
// caller beyond solver_control::choose()'s single current one) would dangle
// silently, with a plausible-looking string, since the storage is reused
// rather than freed. This is the wait arm, not the hot path -- one
// allocation here is fine; the invariant is worth removing entirely rather
// than documenting.
struct wait_result { double seconds = 0.0; std::string source = "floor"; bool floored = false; };

// ---- Stage 1: needs the engine. NOT exercised by the standalone test executable. ----
rl_state_t read_state( const player_t* p, bool boundary_is_foreground );

// ---- Stage 2: PURE over the POD. This is what the test executable and the differ exercise. ----
void        build_obs ( const rl_state_t& s, float out_obs[ RL_OBS_DIM ] );
void        build_mask( const rl_state_t& s, std::uint8_t out_mask[ RL_ACTION_DIM ] );
wait_result build_wait( const rl_state_t& s );

// ---- Weights ----
struct rl_layer
{
  std::uint32_t in_features = 0, out_features = 0;
  std::vector<float> weight;   // row-major [out][in], PyTorch's own layout
  std::vector<float> bias;     // [out]
};

struct rl_weights_t
{
  std::uint32_t format_version = 0;
  std::uint32_t generation     = 0;
  float         exploration    = 0.0f;   // D-01: plan 210-06 refuses any non-zero value
  std::string   obs_schema_sha;          // "rl-obs-v1:<64hex>" -- NOT a bare 64-hex
  std::string   mask_rules_sha;          // bare 64 hex
  std::string   action_space_sha;        // bare 64 hex
  std::vector<rl_layer> layers;          // 3 for this net

  // WR-02 fix, cheap half (210-CR-FIX): forward()'s two hidden-layer
  // activation buffers, hoisted here and load-time-sized by load_rlw1
  // (once layers.size() == 3 is refused/confirmed) instead of being
  // allocated fresh -- two std::vector<float>s -- on every decision
  // boundary's forward() call. `mutable` because forward() takes a `const
  // rl_weights_t&` (the weights themselves are read-only per call) but
  // still needs to write into this per-net scratch storage; safe to reuse
  // across calls because solver_policy= is hard-clamped to threads=1
  // (XPORT-01/AP-3), the same reasoning the wait arm's now-removed
  // thread_local buffer (WR-05) relied on.
  mutable std::vector<float> h0_scratch;
  mutable std::vector<float> h1_scratch;
};

rl_weights_t load_rlw1( const std::string& path );   // throws sc_runtime_error, named per refusal
void         forward( const rl_weights_t& w, const float obs[ RL_OBS_DIM ], float out_q[ RL_ACTION_DIM ] );
int          masked_argmax( const float q[ RL_ACTION_DIM ], const std::uint8_t mask[ RL_ACTION_DIM ] );
}
