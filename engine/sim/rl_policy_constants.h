// ==============================================================================
// GENERATED FILE -- DO NOT EDIT (XPORT-04, D-09).
//
// Produced by:      scripts/rl/gen_rl_constants.py
// Registry source:  scripts/rl/specs/enhancement.json
// Spec id:          'enhancement'
//
// D-09's regenerate-and-diff freshness check treats any difference between a
// fresh run of the generator above and this committed file as DRIFT -- this
// file must never be hand-edited.
//
// This file is excluded from the R-4 genericity gate by EXACT FILENAME (never
// a directory) because it must name spell tokens by necessity -- the C++ maps
// an argmax index to a token it resolves into an `action_t*`.
// ==============================================================================

#pragma once

#include <cstddef>

// ---- Fixed structural preamble (structure, not registry data) ----

enum class rl_container { scalar, buff, cooldown };
enum class rl_kind { k_int, k_float, k_seconds, k_bucket };
enum class rl_action_kind { cast, wait };

struct rl_obs_field
{
  int           slot;
  rl_container  container;
  const char*   key;
  const char*   leaf;
  rl_kind       kind;
  double        missing;
  bool          has_div;
  double        div;
  bool          has_clip_div;
  double        clip_div;
  const double* buckets;
  std::size_t   n_buckets;
  bool          derived;
};

struct rl_action_desc
{
  int            id;
  const char*    token;
  rl_action_kind kind;
  const char*    cooldown_row;
};

struct rl_buff_gate
{
  const char* action_token;
  const char* buff_name;
  bool        forbidden;
};

// ---- Scalar constants ----

inline constexpr const char* RL_REGISTRY_ID = "enhancement";
inline constexpr int RL_ENCODER_VERSION = 1;
inline constexpr std::size_t RL_OBS_DIM = 5;
inline constexpr std::size_t RL_ACTION_DIM = 4;
inline constexpr double RL_EPISODE_MAX_TIME = 300.0;
inline constexpr double RL_WAIT_FLOOR_SECONDS = 0.05;
inline constexpr double RL_PERMANENT_SATURATION = 1.0;
inline constexpr const char* RL_OBS_SCHEMA_SHA = "rl-obs-v1:56938797a4c8a79612fb42627ceb10ddfb167cc92cce958f09e35cd13d721441";
inline constexpr const char* RL_MASK_RULES_SHA = "ec0f9ede8b81a48bae0c3075afa5429480c422294f14787506d54a50df6231fd";
inline constexpr const char* RL_ACTION_SPACE_SHA = "a78fcb368fa28939342de0d3fbdefc8a9b9f9d78892c1315578a3df9f86169d3";

// ---- Observation field descriptors ----

inline constexpr double RL_BUCKETS_SLOT3[] = { 1.0, 2.0, 5.0 };

inline constexpr rl_obs_field RL_OBS_FIELDS[RL_OBS_DIM] = {
  { 0, rl_container::buff, "maelstrom_weapon", "stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
  { 1, rl_container::cooldown, "strike", "charges_fractional", rl_kind::k_float, 2.0, true, 2.0, false, 1.0, nullptr, 0, false },
  { 2, rl_container::buff, "stormsurge", "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
  { 3, rl_container::scalar, "", "active_enemies", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT3, 3, false },
  { 4, rl_container::scalar, "", "fight_remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 300.0, nullptr, 0, true },
};

// ---- Action descriptors ----

inline constexpr rl_action_desc RL_ACTIONS[RL_ACTION_DIM] = {
  { 0, "stormstrike", rl_action_kind::cast, "strike" },
  { 1, "lightning_bolt", rl_action_kind::cast, nullptr },
  { 2, "chain_lightning", rl_action_kind::cast, nullptr },
  { 3, nullptr, rl_action_kind::wait, nullptr },
};

// ---- Buff-gate table ----

inline constexpr rl_buff_gate RL_BUFF_GATES[] = {
  { "lightning_bolt", "tempest", true },
};
inline constexpr std::size_t RL_BUFF_GATE_COUNT = 1;

