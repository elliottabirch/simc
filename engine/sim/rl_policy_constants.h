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
inline constexpr const char* RL_ACTOR_NAME = "MID2_Shaman_Enhancement_Stormbringer";
inline constexpr int RL_ENCODER_VERSION = 1;
inline constexpr std::size_t RL_OBS_DIM = 9;
inline constexpr std::size_t RL_ACTION_DIM = 5;
inline constexpr double RL_EPISODE_MAX_TIME = 300.0;
inline constexpr double RL_WAIT_FLOOR_SECONDS = 0.05;
inline constexpr double RL_PERMANENT_SATURATION = 1.0;
inline constexpr const char* RL_OBS_SCHEMA_SHA = "rl-obs-v1:c5e5fa655a8e1a588aa1acd3eea948b4db19e66a8f1a6c59a5fb1447d0ea8e9d";
inline constexpr const char* RL_MASK_RULES_SHA = "59400ba29a6bd6102bbbb0daefae1379b58f730f76c3dde2d70d233e1e478c42";
inline constexpr const char* RL_ACTION_SPACE_SHA = "4c06590d941d87cb5894ba430991ffa98a57332e1e5687df20b161bb08fddcf9";

// ---- Observation field descriptors ----

inline constexpr double RL_BUCKETS_SLOT3[] = { 1.0, 2.0, 5.0 };

inline constexpr rl_obs_field RL_OBS_FIELDS[RL_OBS_DIM] = {
  { 0, rl_container::buff, "maelstrom_weapon", "stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
  { 1, rl_container::cooldown, "strike", "charges_fractional", rl_kind::k_float, 2.0, true, 2.0, false, 1.0, nullptr, 0, false },
  { 2, rl_container::buff, "stormsurge", "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
  { 3, rl_container::scalar, "", "active_enemies", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT3, 3, false },
  { 4, rl_container::scalar, "", "fight_remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, true },
  { 5, rl_container::buff, "tempest", "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
  { 6, rl_container::buff, "tempest", "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
  { 7, rl_container::scalar, "", "swing_mh_remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 2.1, nullptr, 0, false },
  { 8, rl_container::scalar, "", "swing_oh_remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 2.1, nullptr, 0, false },
};

// ---- Action descriptors ----

inline constexpr rl_action_desc RL_ACTIONS[RL_ACTION_DIM] = {
  { 0, "stormstrike", rl_action_kind::cast, "strike" },
  { 1, "lightning_bolt", rl_action_kind::cast, nullptr },
  { 2, "chain_lightning", rl_action_kind::cast, nullptr },
  { 3, nullptr, rl_action_kind::wait, nullptr },
  { 4, "tempest", rl_action_kind::cast, nullptr },
};

// ---- Buff-gate table ----

inline constexpr rl_buff_gate RL_BUFF_GATES[] = {
  { "lightning_bolt", "tempest", true },
  { "tempest", "tempest", false },
};
inline constexpr std::size_t RL_BUFF_GATE_COUNT = 2;

