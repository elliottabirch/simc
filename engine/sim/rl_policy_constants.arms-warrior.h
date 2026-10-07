// ==============================================================================
// HAND-CUT STAND-IN -- NOT A GENERATED FILE (Phase 268 plan 06, v2.17 "The generic net backbone").
//
// This arms-warrior constants header was cut by hand for Phase 268, emitted by a scratch script that copies the
// fixed structural preamble and the C-A types block verbatim from the enhancement files and computes every slot,
// count and array size from one small table. It exists only so the arms simulator build (-DRL_SPEC=arms-warrior)
// and the generic harness can be proven before Phase 267's generator exists. Phase 270 replaces it with
// `gen_rl_constants.py --spec arms-warrior` output, with no C++ change (the compile-time pins in
// rl_header_contract.hpp prove the replacement carries the same field set).
//
// The six fingerprints below (RL_OBS_SCHEMA_SHA and its five siblings) are PLACEHOLDERS: each keeps enhancement's
// prefix and carries the sha256 of the text "hand-cut-268:<constant name>:arms-warrior" so anyone can recognise
// them by recomputing. They are not the registry's fingerprints; no net trained for arms matches them.
// ==============================================================================

#pragma once

#include <cstddef>

// ---- Fixed structural preamble (structure, not registry data) ----

enum class rl_family { player_buffs, cooldowns, target_facts, shapes, action_leaves, deck, stats, swing_cast, position, pets, items, raid_events, sim_auras, legality, proc_chances, scalars };
enum class rl_family_kind { buff, cooldown, enemy_slot, action_expression, expression, direct, legality, proc_chance, target_fact, shape_fact, scalar };
enum class rl_kind { k_int, k_float, k_seconds, k_bucket };
enum class rl_action_kind { cast, wait };
// 221-03 (ACT-05/ACT-06), narrowed 260922-mfh (D2): the declared anchor kinds a kind==wait action may carry; `none` reproduces today's next-event-minimum build_wait() behaviour byte-for-byte. `cooldown`/`gcd` DELETED -- no live action ever declares them.
enum class rl_wait_anchor_kind { none, swing, maelstrom };
enum class rl_swing_hand { none, mh, oh };

struct rl_leaf_desc
{
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

struct rl_obs_member
{
  const char*         member;
  const char*         engine_token;
  const rl_leaf_desc* leaves;
  std::size_t         n_leaves;
};

struct rl_obs_family
{
  rl_family            id;
  const char*          name;
  rl_family_kind       kind;
  bool                 bare_name;
  const rl_obs_member* members;
  std::size_t          n_members;
  std::size_t          slot_base;
  std::size_t          n_slots;
};

struct rl_wait_anchor
{
  rl_wait_anchor_kind kind;
  rl_swing_hand       hand;           // rl_swing_hand::none unless kind == swing
};

struct rl_action_desc
{
  int            id;
  const char*    token;
  rl_action_kind kind;
  const char*    cooldown_row;
  const char*    cooldown_row_shared;
  const char*    label;
  rl_wait_anchor wait_anchor;
  const char*    illegal_at_buff_cap;   // 260922-mfh (D7): non-null only for a wait illegal at a named buff's engine-truth max_stack() cap
};

struct rl_buff_gate
{
  const char* action_token;
  const char* buff_name;
  bool        forbidden;
};

struct rl_talent_gate
{
  const char* action_token;
  const char* talent_name;
  bool        forbidden;
};

// 246.1-02 Task 2 (CAP-01/CAP-02): one row per declared capability. `requires`/
// `governed_slots`/`governed_actions` are each a (offset, count) SLICE into the
// matching flat RL_CAPABILITY_REQUIRES/RL_CAPABILITY_GOVERNED_SLOTS/
// RL_CAPABILITY_GOVERNED_ACTIONS array below -- never a fixed-width inline array,
// because each capability's own counts differ (weapon_venomfang alone governs 21
// columns; several capabilities govern zero). sim_name/sim_driver_spell_id/sim_set/
// sim_pieces are populated ONLY for the matching sim_kind (racial_spell/
// special_effect/set_bonus respectively) -- nullptr/0 otherwise, never a sentinel
// string baked INTO sim_kind itself. sim_trait_entry_id/sim_min_rank are populated
// ONLY for sim_kind talent (261002-8rs) -- 0 otherwise.
struct rl_capability
{
  const char* id;
  std::size_t column_slot;
  const char* sim_kind;
  const char* sim_name;              // racial_spell only; nullptr otherwise
  long long   sim_driver_spell_id;   // special_effect only; 0 otherwise
  const char* sim_set;               // set_bonus only; nullptr otherwise
  int         sim_pieces;             // set_bonus only; 0 otherwise
  long long   sim_trait_entry_id;     // talent only; 0 otherwise
  int         sim_min_rank;           // talent only; 0 otherwise
  std::size_t requires_offset;
  std::size_t requires_count;
  std::size_t governed_slots_offset;
  std::size_t governed_slots_count;
  std::size_t governed_actions_offset;
  std::size_t governed_actions_count;
};


#define RL_HEADER_CA_VERSION 1

// ---- C-A types begin ----
enum class rl_fact_kind { dot_remaining, debuff_stacks };
enum class rl_wait_kind { next_event, swing_mh, swing_oh, resource_threshold };

struct rl_rule_pref
{
  const char* token;
  const char* rule;
};

struct rl_declared_fact
{
  rl_fact_kind kind;
  const char*  name;   // the player-sourced aura's engine name
  std::size_t  slot;   // index into RL_TARGET_FEATURE_NAMES
};

struct rl_wait_def
{
  const char* label;   // the wait's RL_ACTIONS[i].label
  rl_wait_kind kind;
  double       threshold;
};

struct rl_cooldown_alias
{
  const char* cooldown_row;
  const char* action_token;
};

struct rl_hit_provider
{
  const char* family;
  const char* member;
  const char* provider;
};
// ---- C-A types end ----

// ---- Scalar constants ----

inline constexpr const char* RL_REGISTRY_ID = "arms-warrior";
inline constexpr const char* RL_ACTOR_NAME = "RL_Arms_Warrior";
inline constexpr int RL_ENCODER_VERSION = 6;
inline constexpr std::size_t RL_OBS_DIM = 84;
inline constexpr std::size_t RL_ACTION_DIM = 20;
inline constexpr double RL_EPISODE_MAX_TIME = 300.0;
inline constexpr double RL_WAIT_FLOOR_SECONDS = 0.05;
inline constexpr double RL_PERMANENT_SATURATION = 1.0;
inline constexpr const char* RL_OBS_SCHEMA_SHA = "rl-obs-v6:1a0d0038ab2b33da39a8d9fc12e09ca0b66062e4b9ef1ae0e9327bdcd0d3aaf5";
inline constexpr const char* RL_MASK_RULES_SHA = "09d489c9a8e3c23ddb13df319d05c399b779c1e56cd2dbdf619775065c7058f1";
inline constexpr const char* RL_ACTION_SPACE_SHA = "a31742ca9b06bd48ab67229370b335e78fcb7d21c971fd33689276b66b987154";

// ---- Observation name list (the materialised ordering) ----

inline constexpr const char* RL_OBS_NAMES[RL_OBS_DIM] = {
  "player_buffs.avatar.remains",
  "player_buffs.battlelord.remains",
  "player_buffs.bladestorm.remains",
  "player_buffs.bloodlust.remains",
  "player_buffs.colossal_might.stacks",
  "player_buffs.colossal_might.remains",
  "player_buffs.executioners_precision.stacks",
  "player_buffs.executioners_precision.remains",
  "player_buffs.martial_prowess.stacks",
  "player_buffs.martial_prowess.remains",
  "player_buffs.opportunist.remains",
  "player_buffs.sudden_death.remains",
  "player_buffs.sweeping_strikes.remains",
  "cooldowns.mortal_strike.remains",
  "cooldowns.colossus_smash.remains",
  "cooldowns.overpower.remains",
  "cooldowns.avatar.remains",
  "cooldowns.bladestorm.remains",
  "cooldowns.ravager.remains",
  "cooldowns.demolish.remains",
  "cooldowns.sweeping_strikes.remains",
  "cooldowns.cleave.remains",
  "target_facts.mortal_strike.found",
  "target_facts.mortal_strike.distance",
  "target_facts.mortal_strike.in_reach",
  "target_facts.mortal_strike.alive",
  "target_facts.mortal_strike.time_to_die",
  "target_facts.mortal_strike.health_pct",
  "target_facts.mortal_strike.is_current_target",
  "target_facts.mortal_strike.rend_remaining",
  "target_facts.mortal_strike.deep_wounds_remaining",
  "target_facts.mortal_strike.colossus_smash_remaining",
  "stats.crit.value",
  "stats.crit_rating.value",
  "stats.damage_versatility.value",
  "stats.haste.value",
  "stats.haste_rating.value",
  "stats.mastery_rating.value",
  "stats.mastery_value.value",
  "stats.versatility_rating.value",
  "swing_cast.auto_attack_interval.value",
  "swing_cast.gcd_length.value",
  "swing_cast.swing_mh_remains.value",
  "swing_cast.swing_oh_remains.value",
  "raid_events.raid_adds.in",
  "raid_events.raid_adds.remains",
  "raid_events.raid_adds.count",
  "raid_events.raid_adds.duration",
  "raid_events.raid_adds.cooldown",
  "raid_events.raid_adds.up",
  "raid_events.raid_move.in",
  "raid_events.raid_move.remains",
  "raid_events.raid_move.distance",
  "raid_events.raid_move.up",
  "legality.00.flag",
  "legality.01.flag",
  "legality.02.flag",
  "legality.03.flag",
  "legality.04.flag",
  "legality.05.flag",
  "legality.06.flag",
  "legality.07.flag",
  "legality.08.flag",
  "legality.09.flag",
  "legality.10.flag",
  "legality.11.flag",
  "legality.12.flag",
  "legality.13.flag",
  "legality.14.flag",
  "legality.15.flag",
  "legality.16.flag",
  "legality.17.flag",
  "legality.18.flag",
  "legality.19.flag",
  "fight_remains",
  "active_enemies",
  "raid_event_next_in",
  "dying_within_5s",
  "dying_within_15s",
  "enemies_in_front",
  "resource.rage",
  "hits.cleave",
  "hits.whirlwind",
  "hits.sweeping_strikes",
};

// ---- Per-family observation tables ----

inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_0[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 24.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_1[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_2[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_3[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 40.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_4[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 24.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_5[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_6[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_7[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_8[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_9[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_PLAYER_BUFFS_MEMBERS[] = {
  { "avatar", "avatar", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_0, 1 },
  { "battlelord", "battlelord", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_1, 1 },
  { "bladestorm", "bladestorm", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_2, 1 },
  { "bloodlust", "bloodlust", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_3, 1 },
  { "colossal_might", "colossal_might", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_4, 2 },
  { "executioners_precision", "executioners_precision", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_5, 2 },
  { "martial_prowess", "martial_prowess", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_6, 2 },
  { "opportunist", "opportunist", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_7, 1 },
  { "sudden_death", "sudden_death", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_8, 1 },
  { "sweeping_strikes", "sweeping_strikes", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_9, 1 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_0[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_1[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 45.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_2[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_3[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 90.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_4[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 90.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_5[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 90.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_6[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 45.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_7[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_8[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_COOLDOWNS_MEMBERS[] = {
  { "mortal_strike", "mortal_strike", RL_OBS_FAMILY_COOLDOWNS_LEAVES_0, 1 },
  { "colossus_smash", "colossus_smash", RL_OBS_FAMILY_COOLDOWNS_LEAVES_1, 1 },
  { "overpower", "overpower", RL_OBS_FAMILY_COOLDOWNS_LEAVES_2, 1 },
  { "avatar", "avatar", RL_OBS_FAMILY_COOLDOWNS_LEAVES_3, 1 },
  { "bladestorm", "bladestorm", RL_OBS_FAMILY_COOLDOWNS_LEAVES_4, 1 },
  { "ravager", "ravager", RL_OBS_FAMILY_COOLDOWNS_LEAVES_5, 1 },
  { "demolish", "demolish", RL_OBS_FAMILY_COOLDOWNS_LEAVES_6, 1 },
  { "sweeping_strikes", "sweeping_strikes", RL_OBS_FAMILY_COOLDOWNS_LEAVES_7, 1 },
  { "cleave", "cleave", RL_OBS_FAMILY_COOLDOWNS_LEAVES_8, 1 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_0[] = {
    { "found", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "alive", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "rend_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "deep_wounds_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "colossus_smash_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_TARGET_FACTS_MEMBERS[] = {
  { "mortal_strike", "mortal_strike", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_0, 10 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_STATS_LEAVES_0[] = {
    { "value", rl_kind::k_float, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_STATS_LEAVES_1[] = {
    { "value", rl_kind::k_float, 0.0, true, 3000.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_STATS_LEAVES_2[] = {
    { "value", rl_kind::k_float, 0.0, true, 0.15, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_STATS_LEAVES_3[] = {
    { "value", rl_kind::k_float, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_STATS_LEAVES_4[] = {
    { "value", rl_kind::k_float, 0.0, true, 4000.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_STATS_LEAVES_5[] = {
    { "value", rl_kind::k_float, 0.0, true, 4000.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_STATS_LEAVES_6[] = {
    { "value", rl_kind::k_float, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_STATS_LEAVES_7[] = {
    { "value", rl_kind::k_float, 0.0, true, 750.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_STATS_MEMBERS[] = {
  { "crit", "crit", RL_OBS_FAMILY_STATS_LEAVES_0, 1 },
  { "crit_rating", "crit_rating", RL_OBS_FAMILY_STATS_LEAVES_1, 1 },
  { "damage_versatility", "damage_versatility", RL_OBS_FAMILY_STATS_LEAVES_2, 1 },
  { "haste", "haste", RL_OBS_FAMILY_STATS_LEAVES_3, 1 },
  { "haste_rating", "haste_rating", RL_OBS_FAMILY_STATS_LEAVES_4, 1 },
  { "mastery_rating", "mastery_rating", RL_OBS_FAMILY_STATS_LEAVES_5, 1 },
  { "mastery_value", "mastery_value", RL_OBS_FAMILY_STATS_LEAVES_6, 1 },
  { "versatility_rating", "versatility_rating", RL_OBS_FAMILY_STATS_LEAVES_7, 1 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_SWING_CAST_LEAVES_0[] = {
    { "value", rl_kind::k_seconds, 0.0, false, 1.0, true, 4.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_SWING_CAST_LEAVES_1[] = {
    { "value", rl_kind::k_seconds, 0.0, false, 1.0, true, 1.5, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_SWING_CAST_LEAVES_2[] = {
    { "value", rl_kind::k_seconds, 0.0, false, 1.0, true, 2.1, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_SWING_CAST_LEAVES_3[] = {
    { "value", rl_kind::k_seconds, 0.0, false, 1.0, true, 2.1, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_SWING_CAST_MEMBERS[] = {
  { "auto_attack_interval", "auto_attack_interval", RL_OBS_FAMILY_SWING_CAST_LEAVES_0, 1 },
  { "gcd_length", "gcd_length", RL_OBS_FAMILY_SWING_CAST_LEAVES_1, 1 },
  { "swing_mh_remains", "swing_mh_remains", RL_OBS_FAMILY_SWING_CAST_LEAVES_2, 1 },
  { "swing_oh_remains", "swing_oh_remains", RL_OBS_FAMILY_SWING_CAST_LEAVES_3, 1 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_RAID_EVENTS_LEAVES_0[] = {
    { "in", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "count", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "duration", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "cooldown", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "up", rl_kind::k_seconds, 0.0, false, 1.0, true, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_RAID_EVENTS_LEAVES_1[] = {
    { "in", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 40.0, nullptr, 0, false },
    { "up", rl_kind::k_seconds, 0.0, false, 1.0, true, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_RAID_EVENTS_MEMBERS[] = {
  { "raid_adds", "adds", RL_OBS_FAMILY_RAID_EVENTS_LEAVES_0, 6 },
  { "raid_move", "movement", RL_OBS_FAMILY_RAID_EVENTS_LEAVES_1, 4 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_0[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_1[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_2[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_3[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_4[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_5[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_6[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_7[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_8[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_9[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_10[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_11[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_12[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_13[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_14[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_15[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_16[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_17[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_18[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_19[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_LEGALITY_MEMBERS[] = {
  { "00", "00", RL_OBS_FAMILY_LEGALITY_LEAVES_0, 1 },
  { "01", "01", RL_OBS_FAMILY_LEGALITY_LEAVES_1, 1 },
  { "02", "02", RL_OBS_FAMILY_LEGALITY_LEAVES_2, 1 },
  { "03", "03", RL_OBS_FAMILY_LEGALITY_LEAVES_3, 1 },
  { "04", "04", RL_OBS_FAMILY_LEGALITY_LEAVES_4, 1 },
  { "05", "05", RL_OBS_FAMILY_LEGALITY_LEAVES_5, 1 },
  { "06", "06", RL_OBS_FAMILY_LEGALITY_LEAVES_6, 1 },
  { "07", "07", RL_OBS_FAMILY_LEGALITY_LEAVES_7, 1 },
  { "08", "08", RL_OBS_FAMILY_LEGALITY_LEAVES_8, 1 },
  { "09", "09", RL_OBS_FAMILY_LEGALITY_LEAVES_9, 1 },
  { "10", "10", RL_OBS_FAMILY_LEGALITY_LEAVES_10, 1 },
  { "11", "11", RL_OBS_FAMILY_LEGALITY_LEAVES_11, 1 },
  { "12", "12", RL_OBS_FAMILY_LEGALITY_LEAVES_12, 1 },
  { "13", "13", RL_OBS_FAMILY_LEGALITY_LEAVES_13, 1 },
  { "14", "14", RL_OBS_FAMILY_LEGALITY_LEAVES_14, 1 },
  { "15", "15", RL_OBS_FAMILY_LEGALITY_LEAVES_15, 1 },
  { "16", "16", RL_OBS_FAMILY_LEGALITY_LEAVES_16, 1 },
  { "17", "17", RL_OBS_FAMILY_LEGALITY_LEAVES_17, 1 },
  { "18", "18", RL_OBS_FAMILY_LEGALITY_LEAVES_18, 1 },
  { "19", "19", RL_OBS_FAMILY_LEGALITY_LEAVES_19, 1 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_SCALARS_LEAVES_0[] = {
    { "fight_remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, true },
    { "active_enemies", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "raid_event_next_in", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "dying_within_5s", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "dying_within_15s", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "enemies_in_front", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "resource.rage", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "hits.cleave", rl_kind::k_int, 0.0, true, 6.0, false, 1.0, nullptr, 0, false },
    { "hits.whirlwind", rl_kind::k_int, 0.0, true, 6.0, false, 1.0, nullptr, 0, false },
    { "hits.sweeping_strikes", rl_kind::k_int, 0.0, true, 6.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_SCALARS_MEMBERS[] = {
  { "", "", RL_OBS_FAMILY_SCALARS_LEAVES_0, 10 },
};

inline constexpr std::size_t RL_OBS_FAMILY_COUNT = 8;

inline constexpr rl_obs_family RL_OBS_FAMILIES[RL_OBS_FAMILY_COUNT] = {
  { rl_family::player_buffs, "player_buffs", rl_family_kind::buff, false, RL_OBS_FAMILY_PLAYER_BUFFS_MEMBERS, 10, 0, 13 },
  { rl_family::cooldowns, "cooldowns", rl_family_kind::cooldown, false, RL_OBS_FAMILY_COOLDOWNS_MEMBERS, 9, 13, 9 },
  { rl_family::target_facts, "target_facts", rl_family_kind::target_fact, false, RL_OBS_FAMILY_TARGET_FACTS_MEMBERS, 1, 22, 10 },
  { rl_family::stats, "stats", rl_family_kind::direct, false, RL_OBS_FAMILY_STATS_MEMBERS, 8, 32, 8 },
  { rl_family::swing_cast, "swing_cast", rl_family_kind::direct, false, RL_OBS_FAMILY_SWING_CAST_MEMBERS, 4, 40, 4 },
  { rl_family::raid_events, "raid_events", rl_family_kind::expression, false, RL_OBS_FAMILY_RAID_EVENTS_MEMBERS, 2, 44, 10 },
  { rl_family::legality, "legality", rl_family_kind::legality, false, RL_OBS_FAMILY_LEGALITY_MEMBERS, 20, 54, 20 },
  { rl_family::scalars, "scalars", rl_family_kind::scalar, true, RL_OBS_FAMILY_SCALARS_MEMBERS, 1, 74, 10 },
};

// ---- Action descriptors ----

inline constexpr rl_action_desc RL_ACTIONS[RL_ACTION_DIM] = {
  { 0, "avatar", rl_action_kind::cast, "avatar", nullptr, "avatar", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 1, "bladestorm", rl_action_kind::cast, "bladestorm", nullptr, "bladestorm", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 2, "charge", rl_action_kind::cast, nullptr, nullptr, "charge", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 3, "cleave", rl_action_kind::cast, "cleave", nullptr, "cleave", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 4, "colossus_smash", rl_action_kind::cast, "colossus_smash", nullptr, "colossus_smash", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 5, "demolish", rl_action_kind::cast, "demolish", nullptr, "demolish", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 6, "execute", rl_action_kind::cast, nullptr, nullptr, "execute", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 7, "heroic_strike", rl_action_kind::cast, nullptr, nullptr, "heroic_strike", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 8, "mortal_strike", rl_action_kind::cast, "mortal_strike", nullptr, "mortal_strike", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 9, "overpower", rl_action_kind::cast, "overpower", nullptr, "overpower", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 10, "ravager", rl_action_kind::cast, "ravager", nullptr, "ravager", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 11, "rend", rl_action_kind::cast, nullptr, nullptr, "rend", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 12, "slam", rl_action_kind::cast, nullptr, nullptr, "slam", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 13, "storm_bolt", rl_action_kind::cast, nullptr, nullptr, "storm_bolt", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 14, "sweeping_strikes", rl_action_kind::cast, "sweeping_strikes", nullptr, "sweeping_strikes", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 15, "whirlwind", rl_action_kind::cast, nullptr, nullptr, "whirlwind", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 16, "wrecking_throw", rl_action_kind::cast, nullptr, nullptr, "wrecking_throw", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 17, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_next_event", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 18, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_swing_mh", { rl_wait_anchor_kind::swing, rl_swing_hand::mh }, nullptr },
  { 19, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_rage", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
};

// ---- Buff-gate table ----

inline constexpr rl_buff_gate RL_BUFF_GATES[] = {
  { nullptr, nullptr, false },
};
inline constexpr std::size_t RL_BUFF_GATE_COUNT = 0;

// ---- Talent-gate table ----

inline constexpr rl_talent_gate RL_TALENT_GATES[] = {
  { nullptr, nullptr, false },
};
inline constexpr std::size_t RL_TALENT_GATE_COUNT = 0;

// ---- Capability table (zero rows: one all-null sentinel row, every reader iterates by COUNT) ----

inline constexpr std::size_t RL_CAPABILITY_COUNT = 0;
inline constexpr rl_capability RL_CAPABILITIES[1] = {
  { nullptr, 0, nullptr, nullptr, 0, nullptr, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

inline constexpr std::size_t RL_CAPABILITY_REQUIRES_COUNT = 0;
inline constexpr const char* RL_CAPABILITY_REQUIRES[1] = {
  nullptr,
};

inline constexpr std::size_t RL_CAPABILITY_GOVERNED_SLOTS_COUNT = 0;
inline constexpr std::size_t RL_CAPABILITY_GOVERNED_SLOTS[1] = {
  0,
};

inline constexpr std::size_t RL_CAPABILITY_GOVERNED_ACTIONS_COUNT = 0;
inline constexpr const char* RL_CAPABILITY_GOVERNED_ACTIONS[1] = {
  nullptr,
};

inline constexpr double RL_CAPABILITY_FIXED_VALUE = 0.0;
inline constexpr const char* RL_NET_LAYOUT_SHA = "rl-layout-v1:21a6213d0fe2164b929ec7fb7bfd0f08d29b2c01517b69b20e6f39b73e896902";

// ---- Target scorer feature list ----

inline constexpr std::size_t RL_TARGET_SLOTS = 16;
inline constexpr std::size_t RL_TARGET_FEATURES = 15;
inline constexpr const char* RL_TARGET_FEATURE_SHA = "tgt-feat-v1:48d51713bff7fed3c4440df306cafb9d215d5c35fe0a67e27577cfbdd7f82abe";

inline constexpr const char* RL_TARGET_FEATURE_NAMES[RL_TARGET_FEATURES] = {
  "distance",
  "in_reach",
  "in_range",
  "in_front",
  "alive",
  "immune",
  "immunity_remaining",
  "time_to_die",
  "health_pct",
  "is_boss",
  "neighbours_within_radius",
  "is_current_target",
  "rend_remaining",
  "deep_wounds_remaining",
  "colossus_smash_remaining",
};

// ---- Aim head declaration ----

#define RL_TARGET_FACT_LIST(X) \
  X(distance) \
  X(in_reach) \
  X(in_range) \
  X(in_front) \
  X(alive) \
  X(immune) \
  X(immunity_remaining) \
  X(time_to_die) \
  X(health_pct) \
  X(is_boss) \
  X(neighbours_within_radius) \
  X(is_current_target) \
  X(rend_remaining) \
  X(deep_wounds_remaining) \
  X(colossus_smash_remaining)

struct rl_aim_fact_desc
{
  const char* name;
  rl_kind     kind;
  bool        has_div;
  double      div;
  bool        has_clip_div;
  double      clip_div;
  int         capability;  // index into RL_CAPABILITIES, or -1 when ungated
};

inline constexpr rl_aim_fact_desc RL_AIM_FACT_DESCS[RL_TARGET_FEATURES] = {
  { "distance", rl_kind::k_seconds, false, 1.0, true, 60.0, -1 },
  { "in_reach", rl_kind::k_int, true, 1.0, false, 1.0, -1 },
  { "in_range", rl_kind::k_int, true, 1.0, false, 1.0, -1 },
  { "in_front", rl_kind::k_int, true, 1.0, false, 1.0, -1 },
  { "alive", rl_kind::k_int, true, 1.0, false, 1.0, -1 },
  { "immune", rl_kind::k_int, true, 1.0, false, 1.0, -1 },
  { "immunity_remaining", rl_kind::k_seconds, false, 1.0, true, 60.0, -1 },
  { "time_to_die", rl_kind::k_seconds, false, 1.0, true, 60.0, -1 },
  { "health_pct", rl_kind::k_float, true, 100.0, false, 1.0, -1 },
  { "is_boss", rl_kind::k_int, true, 1.0, false, 1.0, -1 },
  { "neighbours_within_radius", rl_kind::k_seconds, false, 1.0, true, 16.0, -1 },
  { "is_current_target", rl_kind::k_int, true, 1.0, false, 1.0, -1 },
  { "rend_remaining", rl_kind::k_seconds, false, 1.0, true, 30.0, -1 },
  { "deep_wounds_remaining", rl_kind::k_seconds, false, 1.0, true, 30.0, -1 },
  { "colossus_smash_remaining", rl_kind::k_seconds, false, 1.0, true, 30.0, -1 },
};

inline constexpr std::size_t RL_AIM_CONTEXT_COUNT = 1;
inline constexpr std::size_t RL_AIM_CONTEXT_OBS_SLOTS[RL_AIM_CONTEXT_COUNT] = { 0 };
inline constexpr const char* RL_AIM_CONTEXT_NAMES[RL_AIM_CONTEXT_COUNT] = {
  "player_buffs.avatar.remains",
};

inline constexpr std::size_t RL_AIM_SPELL_COUNT = 6;
inline constexpr const char* RL_AIM_SPELLS[RL_AIM_SPELL_COUNT] = {
  "mortal_strike",
  "overpower",
  "execute",
  "slam",
  "rend",
  "colossus_smash",
};

inline constexpr std::size_t RL_AIM_INPUT_COUNT = 16;
inline constexpr const char* RL_AIM_SHA = "aim-v1:250265811b949e1a2008ebf015a93d37a28652451f3960a5ef08f26d922f0619";

// ---- C-A data (arms-warrior) ----

inline constexpr std::size_t RL_TARGETED_TOKEN_COUNT = 6;
inline constexpr const char* RL_TARGETED_TOKENS[RL_TARGETED_TOKEN_COUNT] = {
  "mortal_strike",
  "overpower",
  "execute",
  "slam",
  "rend",
  "colossus_smash",
};

inline constexpr const char* RL_CHOOSER_PROBE_ACTION = "wrecking_throw";
inline constexpr const char* RL_CHOOSER_MELEE_ACTION = "mortal_strike";

inline constexpr std::size_t RL_RULE_PREF_COUNT = 6;
inline constexpr rl_rule_pref RL_RULE_PREFS[RL_RULE_PREF_COUNT] = {
  { "mortal_strike", "current_target" },
  { "overpower", "current_target" },
  { "execute", "shortest_ttd" },
  { "slam", "current_target" },
  { "rend", "current_target" },
  { "colossus_smash", "current_target" },
};

// Declared facts name ENGINE aura names (rend_dot, not the registry spelling rend).
inline constexpr std::size_t RL_DECLARED_FACT_COUNT = 3;
inline constexpr rl_declared_fact RL_DECLARED_FACTS[RL_DECLARED_FACT_COUNT] = {
  { rl_fact_kind::dot_remaining, "rend_dot", 12 },
  { rl_fact_kind::dot_remaining, "deep_wounds", 13 },
  { rl_fact_kind::dot_remaining, "colossus_smash", 14 },
};

inline constexpr const char* RL_RESOURCE_NAME = "rage";

// wait_rage is a stand-in row that exercises the resource_threshold path at runtime (plan 268-04).
inline constexpr std::size_t RL_WAIT_DEF_COUNT = 3;
inline constexpr rl_wait_def RL_WAIT_DEFS[RL_WAIT_DEF_COUNT] = {
  { "wait_next_event", rl_wait_kind::next_event, 0.0 },
  { "wait_swing_mh", rl_wait_kind::swing_mh, 0.0 },
  { "wait_rage", rl_wait_kind::resource_threshold, 30.0 },
};

inline constexpr std::size_t RL_COOLDOWN_ROW_ACTION_COUNT = 0;
inline constexpr rl_cooldown_alias RL_COOLDOWN_ROW_ACTION[1] = {
  { nullptr, nullptr },
};

inline constexpr std::size_t RL_PROC_NAME_COUNT = 20;
inline constexpr const char* RL_PROC_NAMES[RL_PROC_NAME_COUNT] = {
  "crit_hit",
  "auto_attack_hit",
  "spare_2",
  "spare_3",
  "spare_4",
  "spare_5",
  "spare_6",
  "spare_7",
  "spare_8",
  "spare_9",
  "spare_10",
  "spare_11",
  "spare_12",
  "spare_13",
  "spare_14",
  "spare_15",
  "dbc_proc_callback",
  "buff_chance_trigger",
  "spare_18",
  "spare_19",
};

inline constexpr std::size_t RL_HIT_PROVIDER_COUNT = 3;
inline constexpr rl_hit_provider RL_HIT_PROVIDERS[RL_HIT_PROVIDER_COUNT] = {
  { "hits", "cleave", "warrior.cleave" },
  { "hits", "whirlwind", "warrior.whirlwind" },
  { "hits", "sweeping_strikes", "warrior.sweeping_strikes" },
};
