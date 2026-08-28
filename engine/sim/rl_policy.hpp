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
// and `build_mask`/`build_wait` remain PURE over it -- that purity is what
// Phase 211's differ exercises for those two.
//
// UPDATED (tstl-sylvanas phase 220, plan 220-04, OBS-01/OBS-02/OBS-07): the
// "Stage 2 is PURE over the POD" contract above was written for plan
// 210-07's standalone test executable, which was SUPERSEDED OUTRIGHT under
// ruling N-8 (rl_policy_obs.cpp's own read_first note says so) and will
// never be built. `build_obs` now ALSO reads the engine directly (a
// `player_t*` parameter, a `slot_table` of handles resolved once per actor
// at bind time) -- one census walk composes the observation vector AND its
// own name list by construction, rather than mirroring a Python-authored
// per-field table. `read_state`'s POD vectors (`s.buffs`, `s.cooldowns`,
// `s.swing_mh_remains`, `s.swing_oh_remains`, `s.gcd_remains`,
// `s.boundary_is_foreground`) are STILL REQUIRED -- `build_mask` and
// `build_wait` read every one of them and are untouched by this change.

#pragma once
#include "sim/rl_policy_constants.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct player_t;

namespace rl_policy
{
// Opaque to callers (tstl-sylvanas phase 220, plan 220-04, OBS-02/OBS-07):
// solver_control.cpp holds a `const slot_table&` and passes it straight
// through to build_obs -- it never inspects a slot_table's members, only
// bind_slots() and build_obs() (both declared below, both defined in
// rl_policy_obs.cpp) ever look inside one. Forward-declared here so the
// pinned interface stays a reference-only seam; the full definition
// (slot_binding's per-kind resolved handle, the per-slot composed name,
// the first-name-divergence slot) lives in the .cpp.
struct slot_table;
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

// ---- Slot binding (phase 220, plan 220-04, OBS-02/OBS-07) ----
// bind_slots resolves every RL_OBS_FAMILIES member to an engine handle
// ONCE per actor (buff_t*/cooldown_t*/expr_t*/... -- see rl_policy_obs.cpp
// for the per-kind resolution and the file-static cache keyed on
// `const player_t*`) and returns a reference to the cached slot_table for
// `p`; a second call for the same `p` is O(1). The SAME walk that resolves
// each slot also composes that slot's name from the same family/member/leaf
// tables -- so the vector build_obs fills and the name list a caller can
// request via `rl_obs_names_out=` share one source of truth by
// construction, never a second hand-copied name list (OBS-02).
const slot_table& bind_slots( player_t* p );

// ---- Stage 2 ----
// build_mask/build_wait remain PURE over the rl_state_t POD (unchanged).
// build_obs now ALSO reads the engine directly through `t`'s resolved
// handles and, for a `direct` binding, through `p` itself -- see this
// header's own updated "Stage 2" comment above.
void        build_obs ( const player_t* p, const rl_state_t& s, const slot_table& t, float out_obs[ RL_OBS_DIM ] );
void        build_mask( const rl_state_t& s, std::uint8_t out_mask[ RL_ACTION_DIM ] );
wait_result build_wait( const rl_state_t& s );

// ---- Weights (RLW1 v2, tstl-sylvanas Phase 213-TDL Task 1) ----
//
// Three body types share this same rl_layer/rl_weights_t plumbing -- see
// scripts/rl/agent/network_bodies.py (this repo's own Python counterpart)
// for the plain-language explanation of what each one computes:
//
//   - mlp:        3 layers [hidden0, hidden1, out]. No gamma/beta anywhere.
//                 Byte-identical computation to the pre-v2 format.
//   - dueling:    4 layers [hidden0, hidden1, v_head, a_head]. v_head and
//                 a_head both BRANCH off hidden1's output (not a further
//                 chain). Q = V + A - mean(A), mean over ALL actions, taken
//                 BEFORE the engine's own mask+argmax step.
//   - ln-dueling: same 4-layer sequence as dueling, but hidden0/hidden1
//                 each carry a LayerNorm gamma/beta pair (applied after the
//                 linear layer, before ReLU); v_head/a_head never do.
enum class rl_body_type
{
  mlp,
  dueling,
  ln_dueling
};

struct rl_layer
{
  std::uint32_t in_features = 0, out_features = 0;
  std::vector<float> weight;   // row-major [out][in], PyTorch's own layout
  std::vector<float> bias;     // [out]
  bool                has_ln = false;
  std::vector<float>  gamma;   // [out], only when has_ln
  std::vector<float>  beta;    // [out], only when has_ln
};

struct rl_weights_t
{
  std::uint32_t format_version = 0;
  std::uint32_t generation     = 0;
  float         exploration    = 0.0f;   // D-01: plan 210-06 refuses any non-zero value
  rl_body_type  body           = rl_body_type::mlp;
  std::string   obs_schema_sha;          // "rl-obs-v1:<64hex>" -- NOT a bare 64-hex
  std::string   mask_rules_sha;          // bare 64 hex
  std::string   action_space_sha;        // bare 64 hex
  std::vector<rl_layer> layers;          // 3 (mlp) or 4 (dueling/ln-dueling)

  // WR-02 fix, cheap half (210-CR-FIX; widened Phase 213-TDL Task 1 for the
  // dueling bodies' branch heads): forward()'s hidden-layer activation
  // buffers, hoisted here and load-time-sized by load_rlw1 instead of being
  // allocated fresh on every decision boundary's forward() call. `mutable`
  // because forward() takes a `const rl_weights_t&` (the weights themselves
  // are read-only per call) but still needs to write into this per-net
  // scratch storage; safe to reuse across calls because solver_policy= is
  // hard-clamped to threads=1 (XPORT-01/AP-3), the same reasoning the wait
  // arm's now-removed thread_local buffer (WR-05) relied on.
  //
  // h0_scratch/h1_scratch hold the two hidden layers' post-ReLU activations
  // (all three body types). The dueling bodies' V head (a single scalar)
  // and A head (RL_ACTION_DIM-wide, written directly into forward()'s own
  // out_q[] output buffer before being recombined in place) need no
  // persistent scratch of their own -- see rl_policy_net.cpp's forward().
  mutable std::vector<float> h0_scratch;
  mutable std::vector<float> h1_scratch;
};

rl_weights_t load_rlw1( const std::string& path );   // throws sc_runtime_error, named per refusal
void         forward( const rl_weights_t& w, const float obs[ RL_OBS_DIM ], float out_q[ RL_ACTION_DIM ] );
int          masked_argmax( const float q[ RL_ACTION_DIM ], const std::uint8_t mask[ RL_ACTION_DIM ] );
}
