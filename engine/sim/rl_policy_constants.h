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

enum class rl_family { player_buffs, cooldowns, target_facts, shapes, action_leaves, deck, stats, swing_cast, position, pets, items, raid_events, sim_auras, legality, proc_chances, scalars };
enum class rl_family_kind { buff, cooldown, enemy_slot, action_expression, expression, direct, legality, proc_chance, target_fact, shape_fact, scalar };
enum class rl_kind { k_int, k_float, k_seconds, k_bucket };
enum class rl_action_kind { cast, wait, turn };
// 221-03 (ACT-05/ACT-06): the four declared anchor kinds a kind==wait action may carry; `none` reproduces today's next-event-minimum build_wait() behaviour byte-for-byte.
enum class rl_wait_anchor_kind { none, cooldown, swing, maelstrom, gcd };
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
  const char*         cooldown_row;   // non-null only when kind == cooldown
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

// ---- Scalar constants ----

inline constexpr const char* RL_REGISTRY_ID = "enhancement";
inline constexpr const char* RL_ACTOR_NAME = "MID2_Shaman_Enhancement_Stormbringer";
inline constexpr int RL_ENCODER_VERSION = 2;
inline constexpr std::size_t RL_OBS_DIM = 325;
inline constexpr std::size_t RL_ACTION_DIM = 21;
inline constexpr double RL_EPISODE_MAX_TIME = 300.0;
inline constexpr double RL_WAIT_FLOOR_SECONDS = 0.05;
inline constexpr double RL_PERMANENT_SATURATION = 1.0;
inline constexpr const char* RL_OBS_SCHEMA_SHA = "rl-obs-v2:7471ff88bf5120dd803e69f4acd4d9b07e8f7e5b9f849d1f65f35a32bf92de72";
inline constexpr const char* RL_MASK_RULES_SHA = "3f930294d36b217dca01fc51600c0da9d0568e20152fb53c588b6e8ddccf7662";
inline constexpr const char* RL_ACTION_SPACE_SHA = "b7d16fa7eedf4fe48d4fadd6968b0e8662600a06b352d49f318ef9a527f94d92";

// ---- Observation name list (the materialised ordering) ----

inline constexpr const char* RL_OBS_NAMES[RL_OBS_DIM] = {
  "player_buffs.arc_discharge.stacks",
  "player_buffs.arc_discharge.remains",
  "player_buffs.arcanoweave_insight.remains",
  "player_buffs.ascendance.remains",
  "player_buffs.berserking.remains",
  "player_buffs.bloodlust.remains",
  "player_buffs.converging_storms.stacks",
  "player_buffs.converging_storms.remains",
  "player_buffs.crackling_surge.stacks",
  "player_buffs.crackling_surge.remains",
  "player_buffs.crash_lightning.stacks",
  "player_buffs.crash_lightning.remains",
  "player_buffs.critical_ritual.remains",
  "player_buffs.devoured_strength.stacks",
  "player_buffs.devoured_strength.remains",
  "player_buffs.doom_winds.remains",
  "player_buffs.exhaustion.remains",
  "player_buffs.flurry.stacks",
  "player_buffs.flurry.remains",
  "player_buffs.frenzied_focus.remains",
  "player_buffs.genius_insight.remains",
  "player_buffs.hasty_ritual.remains",
  "player_buffs.lightning_strikes.stacks",
  "player_buffs.lightning_strikes.remains",
  "player_buffs.maelstrom_weapon.stacks",
  "player_buffs.maelstrom_weapon.remains",
  "player_buffs.masterful_ritual.remains",
  "player_buffs.mid2_enh_4pc.stacks",
  "player_buffs.mid2_enh_4pc.remains",
  "player_buffs.potion.remains",
  "player_buffs.rune_of_burning_haste.stacks",
  "player_buffs.rune_of_burning_haste.remains",
  "player_buffs.storm_swell.remains",
  "player_buffs.storm_unleashed.stacks",
  "player_buffs.storm_unleashed.remains",
  "player_buffs.stormblast.stacks",
  "player_buffs.stormblast.remains",
  "player_buffs.stormsurge.remains",
  "player_buffs.tempest.stacks",
  "player_buffs.tempest.remains",
  "player_buffs.unlimited_power.stacks",
  "player_buffs.unlimited_power.remains",
  "player_buffs.venomcursed_ascendance.remains",
  "player_buffs.venomcursed_mastery.remains",
  "player_buffs.versatile_ritual.remains",
  "cooldowns.ascendance.remains",
  "cooldowns.berserking.remains",
  "cooldowns.crash_lightning.remains",
  "cooldowns.item_cd_1141.remains",
  "cooldowns.lava_lash.remains",
  "cooldowns.strike.charges_fractional",
  "cooldowns.strike.charges",
  "cooldowns.strike.recharge_time",
  "cooldowns.strike.full_recharge_time",
  "cooldowns.voltaic_blaze.remains",
  "cooldowns.voracious_heart_of_ulatek_1297761.remains",
  "target_facts.chain_lightning.found",
  "target_facts.chain_lightning.time_to_die",
  "target_facts.chain_lightning.distance",
  "target_facts.chain_lightning.in_reach",
  "target_facts.chain_lightning.in_range",
  "target_facts.chain_lightning.in_front",
  "target_facts.chain_lightning.immune",
  "target_facts.chain_lightning.immunity_remaining",
  "target_facts.chain_lightning.health_pct",
  "target_facts.chain_lightning.is_boss",
  "target_facts.chain_lightning.flame_shock_remaining",
  "target_facts.chain_lightning.burning_core_remaining",
  "target_facts.chain_lightning.lightning_rod_stacks",
  "target_facts.chain_lightning.lightning_rod_remaining",
  "target_facts.chain_lightning.venomfang_remaining",
  "target_facts.chain_lightning.venomfang_debuff_stacks",
  "target_facts.chain_lightning.venomfang_debuff_remaining",
  "target_facts.chain_lightning.rune_of_unleashed_fire_lingering_remaining",
  "target_facts.chain_lightning.neighbours_within_radius",
  "target_facts.chain_lightning.is_current_target",
  "target_facts.lava_lash.found",
  "target_facts.lava_lash.time_to_die",
  "target_facts.lava_lash.distance",
  "target_facts.lava_lash.in_reach",
  "target_facts.lava_lash.in_range",
  "target_facts.lava_lash.in_front",
  "target_facts.lava_lash.immune",
  "target_facts.lava_lash.immunity_remaining",
  "target_facts.lava_lash.health_pct",
  "target_facts.lava_lash.is_boss",
  "target_facts.lava_lash.flame_shock_remaining",
  "target_facts.lava_lash.burning_core_remaining",
  "target_facts.lava_lash.lightning_rod_stacks",
  "target_facts.lava_lash.lightning_rod_remaining",
  "target_facts.lava_lash.venomfang_remaining",
  "target_facts.lava_lash.venomfang_debuff_stacks",
  "target_facts.lava_lash.venomfang_debuff_remaining",
  "target_facts.lava_lash.rune_of_unleashed_fire_lingering_remaining",
  "target_facts.lava_lash.neighbours_within_radius",
  "target_facts.lava_lash.is_current_target",
  "target_facts.lightning_bolt.found",
  "target_facts.lightning_bolt.time_to_die",
  "target_facts.lightning_bolt.distance",
  "target_facts.lightning_bolt.in_reach",
  "target_facts.lightning_bolt.in_range",
  "target_facts.lightning_bolt.in_front",
  "target_facts.lightning_bolt.immune",
  "target_facts.lightning_bolt.immunity_remaining",
  "target_facts.lightning_bolt.health_pct",
  "target_facts.lightning_bolt.is_boss",
  "target_facts.lightning_bolt.flame_shock_remaining",
  "target_facts.lightning_bolt.burning_core_remaining",
  "target_facts.lightning_bolt.lightning_rod_stacks",
  "target_facts.lightning_bolt.lightning_rod_remaining",
  "target_facts.lightning_bolt.venomfang_remaining",
  "target_facts.lightning_bolt.venomfang_debuff_stacks",
  "target_facts.lightning_bolt.venomfang_debuff_remaining",
  "target_facts.lightning_bolt.rune_of_unleashed_fire_lingering_remaining",
  "target_facts.lightning_bolt.neighbours_within_radius",
  "target_facts.lightning_bolt.is_current_target",
  "target_facts.primordial_storm.found",
  "target_facts.primordial_storm.time_to_die",
  "target_facts.primordial_storm.distance",
  "target_facts.primordial_storm.in_reach",
  "target_facts.primordial_storm.in_range",
  "target_facts.primordial_storm.in_front",
  "target_facts.primordial_storm.immune",
  "target_facts.primordial_storm.immunity_remaining",
  "target_facts.primordial_storm.health_pct",
  "target_facts.primordial_storm.is_boss",
  "target_facts.primordial_storm.flame_shock_remaining",
  "target_facts.primordial_storm.burning_core_remaining",
  "target_facts.primordial_storm.lightning_rod_stacks",
  "target_facts.primordial_storm.lightning_rod_remaining",
  "target_facts.primordial_storm.venomfang_remaining",
  "target_facts.primordial_storm.venomfang_debuff_stacks",
  "target_facts.primordial_storm.venomfang_debuff_remaining",
  "target_facts.primordial_storm.rune_of_unleashed_fire_lingering_remaining",
  "target_facts.primordial_storm.neighbours_within_radius",
  "target_facts.primordial_storm.is_current_target",
  "target_facts.stormstrike.found",
  "target_facts.stormstrike.time_to_die",
  "target_facts.stormstrike.distance",
  "target_facts.stormstrike.in_reach",
  "target_facts.stormstrike.in_range",
  "target_facts.stormstrike.in_front",
  "target_facts.stormstrike.immune",
  "target_facts.stormstrike.immunity_remaining",
  "target_facts.stormstrike.health_pct",
  "target_facts.stormstrike.is_boss",
  "target_facts.stormstrike.flame_shock_remaining",
  "target_facts.stormstrike.burning_core_remaining",
  "target_facts.stormstrike.lightning_rod_stacks",
  "target_facts.stormstrike.lightning_rod_remaining",
  "target_facts.stormstrike.venomfang_remaining",
  "target_facts.stormstrike.venomfang_debuff_stacks",
  "target_facts.stormstrike.venomfang_debuff_remaining",
  "target_facts.stormstrike.rune_of_unleashed_fire_lingering_remaining",
  "target_facts.stormstrike.neighbours_within_radius",
  "target_facts.stormstrike.is_current_target",
  "target_facts.tempest.found",
  "target_facts.tempest.time_to_die",
  "target_facts.tempest.distance",
  "target_facts.tempest.in_reach",
  "target_facts.tempest.in_range",
  "target_facts.tempest.in_front",
  "target_facts.tempest.immune",
  "target_facts.tempest.immunity_remaining",
  "target_facts.tempest.health_pct",
  "target_facts.tempest.is_boss",
  "target_facts.tempest.flame_shock_remaining",
  "target_facts.tempest.burning_core_remaining",
  "target_facts.tempest.lightning_rod_stacks",
  "target_facts.tempest.lightning_rod_remaining",
  "target_facts.tempest.venomfang_remaining",
  "target_facts.tempest.venomfang_debuff_stacks",
  "target_facts.tempest.venomfang_debuff_remaining",
  "target_facts.tempest.rune_of_unleashed_fire_lingering_remaining",
  "target_facts.tempest.neighbours_within_radius",
  "target_facts.tempest.is_current_target",
  "target_facts.voltaic_blaze.found",
  "target_facts.voltaic_blaze.time_to_die",
  "target_facts.voltaic_blaze.distance",
  "target_facts.voltaic_blaze.in_reach",
  "target_facts.voltaic_blaze.in_range",
  "target_facts.voltaic_blaze.in_front",
  "target_facts.voltaic_blaze.immune",
  "target_facts.voltaic_blaze.immunity_remaining",
  "target_facts.voltaic_blaze.health_pct",
  "target_facts.voltaic_blaze.is_boss",
  "target_facts.voltaic_blaze.flame_shock_remaining",
  "target_facts.voltaic_blaze.burning_core_remaining",
  "target_facts.voltaic_blaze.lightning_rod_stacks",
  "target_facts.voltaic_blaze.lightning_rod_remaining",
  "target_facts.voltaic_blaze.venomfang_remaining",
  "target_facts.voltaic_blaze.venomfang_debuff_stacks",
  "target_facts.voltaic_blaze.venomfang_debuff_remaining",
  "target_facts.voltaic_blaze.rune_of_unleashed_fire_lingering_remaining",
  "target_facts.voltaic_blaze.neighbours_within_radius",
  "target_facts.voltaic_blaze.is_current_target",
  "target_facts.windstrike.found",
  "target_facts.windstrike.time_to_die",
  "target_facts.windstrike.distance",
  "target_facts.windstrike.in_reach",
  "target_facts.windstrike.in_range",
  "target_facts.windstrike.in_front",
  "target_facts.windstrike.immune",
  "target_facts.windstrike.immunity_remaining",
  "target_facts.windstrike.health_pct",
  "target_facts.windstrike.is_boss",
  "target_facts.windstrike.flame_shock_remaining",
  "target_facts.windstrike.burning_core_remaining",
  "target_facts.windstrike.lightning_rod_stacks",
  "target_facts.windstrike.lightning_rod_remaining",
  "target_facts.windstrike.venomfang_remaining",
  "target_facts.windstrike.venomfang_debuff_stacks",
  "target_facts.windstrike.venomfang_debuff_remaining",
  "target_facts.windstrike.rune_of_unleashed_fire_lingering_remaining",
  "target_facts.windstrike.neighbours_within_radius",
  "target_facts.windstrike.is_current_target",
  "shapes.crash_lightning.enemies_hit",
  "shapes.crash_lightning.summed_remaining_life",
  "shapes.crash_lightning.long_lived_count",
  "shapes.sundering.enemies_hit",
  "shapes.sundering.summed_remaining_life",
  "shapes.sundering.long_lived_count",
  "action_leaves.ascendance.ready",
  "action_leaves.berserking.ready",
  "action_leaves.chain_lightning.hit_damage",
  "action_leaves.chain_lightning.crit_pct_current",
  "action_leaves.crash_lightning.ready",
  "action_leaves.crash_lightning.hit_damage",
  "action_leaves.crash_lightning.crit_pct_current",
  "action_leaves.crash_lightning.multiplier",
  "action_leaves.lava_lash.ready",
  "action_leaves.lava_lash.hit_damage",
  "action_leaves.lava_lash.crit_pct_current",
  "action_leaves.lava_lash.multiplier",
  "action_leaves.lightning_bolt.ready",
  "action_leaves.lightning_bolt.hit_damage",
  "action_leaves.lightning_bolt.crit_pct_current",
  "action_leaves.lightning_bolt.in_flight",
  "action_leaves.lightning_bolt.in_flight_remains",
  "action_leaves.stormstrike.ready",
  "action_leaves.stormstrike.hit_damage",
  "action_leaves.stormstrike.crit_pct_current",
  "action_leaves.stormstrike.multiplier",
  "action_leaves.tempest.ready",
  "action_leaves.tempest.hit_damage",
  "action_leaves.tempest.crit_pct_current",
  "action_leaves.use_item_voracious_heart_of_ulatek.ready",
  "action_leaves.voltaic_blaze.ready",
  "action_leaves.voltaic_blaze.hit_damage",
  "action_leaves.voltaic_blaze.multiplier",
  "action_leaves.windstrike.ready",
  "action_leaves.windstrike.hit_damage",
  "action_leaves.windstrike.crit_pct_current",
  "action_leaves.windstrike.multiplier",
  "deck.asc_dw_draws_left.value",
  "deck.asc_dw_fail_left.value",
  "deck.asc_dw_proc_left.value",
  "deck.storm_unleashed_draws_left.value",
  "deck.storm_unleashed_fail_left.value",
  "deck.storm_unleashed_proc_left.value",
  "deck.tempest_draws_left.value",
  "deck.tempest_fail_left.value",
  "deck.tempest_proc_left.value",
  "deck.tempest_procs_this_deck.value",
  "deck.tempest_spends_since_proc.value",
  "deck.ti_chain_lightning.value",
  "deck.ti_lightning_bolt.value",
  "stats.attack_power.value",
  "stats.damage_versatility.value",
  "stats.haste.value",
  "stats.mastery_value.value",
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
  "legality.20.flag",
  "proc_chances.stormsurge.value",
  "proc_chances.windfury.value",
  "fight_remains",
  "active_enemies",
  "raid_event_next_in",
  "time_to_bloodlust",
  "dying_within_5s",
  "dying_within_15s",
  "enemies_in_front",
  "enemies_in_melee",
  "enemies_total",
  "enemies_within_8yd",
  "enemies_within_40yd",
  "flame_shock_carrier_count",
  "immunity_in",
  "immunity_remaining",
  "longest_time_to_die",
  "nearest_enemy_distance",
  "soonest_time_to_die",
};

// ---- Per-family observation tables ----

inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_0[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_1[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_2[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 14.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_3[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_4[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 40.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_5[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 6.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_6[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_7[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_8[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_9[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_10[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_11[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 600.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_12[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 3.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_13[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_14[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_15[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_16[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 3.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_17[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_18[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_19[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_20[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_21[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_22[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_23[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_24[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_25[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_26[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_27[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 15.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_28[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_29[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_30[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_PLAYER_BUFFS_MEMBERS[] = {
  { "arc_discharge", "arc_discharge", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_0, 2 },
  { "arcanoweave_insight", "arcanoweave_insight", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_1, 1 },
  { "ascendance", "ascendance", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_2, 1 },
  { "berserking", "berserking", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_3, 1 },
  { "bloodlust", "bloodlust", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_4, 1 },
  { "converging_storms", "converging_storms", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_5, 2 },
  { "crackling_surge", "crackling_surge", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_6, 2 },
  { "crash_lightning", "crash_lightning", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_7, 2 },
  { "critical_ritual", "critical_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_8, 1 },
  { "devoured_strength", "devoured_strength", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_9, 2 },
  { "doom_winds", "doom_winds", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_10, 1 },
  { "exhaustion", "exhaustion", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_11, 1 },
  { "flurry", "flurry", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_12, 2 },
  { "frenzied_focus", "frenzied_focus", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_13, 1 },
  { "genius_insight", "genius_insight", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_14, 1 },
  { "hasty_ritual", "hasty_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_15, 1 },
  { "lightning_strikes", "lightning_strikes", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_16, 2 },
  { "maelstrom_weapon", "maelstrom_weapon", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_17, 2 },
  { "masterful_ritual", "masterful_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_18, 1 },
  { "mid2_enh_4pc", "mid2_enh_4pc", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_19, 2 },
  { "potion", "potion", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_20, 1 },
  { "rune_of_burning_haste", "rune_of_burning_haste", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_21, 2 },
  { "storm_swell", "storm_swell", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_22, 1 },
  { "storm_unleashed", "storm_unleashed", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_23, 2 },
  { "stormblast", "stormblast", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_24, 2 },
  { "stormsurge", "stormsurge", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_25, 1 },
  { "tempest", "tempest", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_26, 2 },
  { "unlimited_power", "unlimited_power", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_27, 2 },
  { "venomcursed_ascendance", "venomcursed_ascendance", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_28, 1 },
  { "venomcursed_mastery", "venomcursed_mastery", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_29, 1 },
  { "versatile_ritual", "versatile_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_30, 1 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_0[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 180.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_1[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 180.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_2[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_3[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_4[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_5[] = {
    { "charges_fractional", rl_kind::k_float, 2.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "charges", rl_kind::k_int, 2.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "recharge_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "full_recharge_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_6[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_7[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 90.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_COOLDOWNS_MEMBERS[] = {
  { "ascendance", "ascendance", RL_OBS_FAMILY_COOLDOWNS_LEAVES_0, 1 },
  { "berserking", "berserking", RL_OBS_FAMILY_COOLDOWNS_LEAVES_1, 1 },
  { "crash_lightning", "crash_lightning", RL_OBS_FAMILY_COOLDOWNS_LEAVES_2, 1 },
  { "item_cd_1141", "item_cd_1141", RL_OBS_FAMILY_COOLDOWNS_LEAVES_3, 1 },
  { "lava_lash", "lava_lash", RL_OBS_FAMILY_COOLDOWNS_LEAVES_4, 1 },
  { "strike", "strike", RL_OBS_FAMILY_COOLDOWNS_LEAVES_5, 4 },
  { "voltaic_blaze", "voltaic_blaze", RL_OBS_FAMILY_COOLDOWNS_LEAVES_6, 1 },
  { "voracious_heart_of_ulatek_1297761", "voracious_heart_of_ulatek_1297761", RL_OBS_FAMILY_COOLDOWNS_LEAVES_7, 1 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_0[] = {
    { "found", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 600.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "flame_shock_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 18.0, nullptr, 0, false },
    { "burning_core_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lightning_rod_stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "lightning_rod_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 8.0, false, 1.0, nullptr, 0, false },
    { "venomfang_debuff_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_1[] = {
    { "found", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 600.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "flame_shock_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 18.0, nullptr, 0, false },
    { "burning_core_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lightning_rod_stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "lightning_rod_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 8.0, false, 1.0, nullptr, 0, false },
    { "venomfang_debuff_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_2[] = {
    { "found", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 600.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "flame_shock_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 18.0, nullptr, 0, false },
    { "burning_core_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lightning_rod_stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "lightning_rod_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 8.0, false, 1.0, nullptr, 0, false },
    { "venomfang_debuff_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_3[] = {
    { "found", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 600.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "flame_shock_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 18.0, nullptr, 0, false },
    { "burning_core_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lightning_rod_stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "lightning_rod_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 8.0, false, 1.0, nullptr, 0, false },
    { "venomfang_debuff_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_4[] = {
    { "found", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 600.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "flame_shock_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 18.0, nullptr, 0, false },
    { "burning_core_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lightning_rod_stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "lightning_rod_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 8.0, false, 1.0, nullptr, 0, false },
    { "venomfang_debuff_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_5[] = {
    { "found", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 600.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "flame_shock_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 18.0, nullptr, 0, false },
    { "burning_core_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lightning_rod_stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "lightning_rod_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 8.0, false, 1.0, nullptr, 0, false },
    { "venomfang_debuff_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_6[] = {
    { "found", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 600.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "flame_shock_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 18.0, nullptr, 0, false },
    { "burning_core_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lightning_rod_stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "lightning_rod_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 8.0, false, 1.0, nullptr, 0, false },
    { "venomfang_debuff_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_7[] = {
    { "found", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 600.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "flame_shock_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 18.0, nullptr, 0, false },
    { "burning_core_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lightning_rod_stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "lightning_rod_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 8.0, false, 1.0, nullptr, 0, false },
    { "venomfang_debuff_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_TARGET_FACTS_MEMBERS[] = {
  { "chain_lightning", "chain_lightning", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_0, 20 },
  { "lava_lash", "lava_lash", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_1, 20 },
  { "lightning_bolt", "lightning_bolt", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_2, 20 },
  { "primordial_storm", "primordial_storm", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_3, 20 },
  { "stormstrike", "stormstrike", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_4, 20 },
  { "tempest", "tempest", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_5, 20 },
  { "voltaic_blaze", "voltaic_blaze", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_6, 20 },
  { "windstrike", "windstrike", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_7, 20 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_SHAPES_LEAVES_0[] = {
    { "enemies_hit", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "summed_remaining_life", rl_kind::k_seconds, 0.0, false, 1.0, true, 3600.0, nullptr, 0, false },
    { "long_lived_count", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_SHAPES_LEAVES_1[] = {
    { "enemies_hit", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "summed_remaining_life", rl_kind::k_seconds, 0.0, false, 1.0, true, 3600.0, nullptr, 0, false },
    { "long_lived_count", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_SHAPES_MEMBERS[] = {
  { "crash_lightning", "crash_lightning", RL_OBS_FAMILY_SHAPES_LEAVES_0, 3 },
  { "sundering", "sundering", RL_OBS_FAMILY_SHAPES_LEAVES_1, 3 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_0[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_1[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_2[] = {
    { "hit_damage", rl_kind::k_float, 0.0, true, 300000.0, false, 1.0, nullptr, 0, false },
    { "crit_pct_current", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_3[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "hit_damage", rl_kind::k_float, 0.0, true, 300000.0, false, 1.0, nullptr, 0, false },
    { "crit_pct_current", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_4[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "hit_damage", rl_kind::k_float, 0.0, true, 300000.0, false, 1.0, nullptr, 0, false },
    { "crit_pct_current", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_5[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "hit_damage", rl_kind::k_float, 0.0, true, 300000.0, false, 1.0, nullptr, 0, false },
    { "crit_pct_current", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "in_flight", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_flight_remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 2.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_6[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "hit_damage", rl_kind::k_float, 0.0, true, 300000.0, false, 1.0, nullptr, 0, false },
    { "crit_pct_current", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_7[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "hit_damage", rl_kind::k_float, 0.0, true, 300000.0, false, 1.0, nullptr, 0, false },
    { "crit_pct_current", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_8[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_9[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "hit_damage", rl_kind::k_float, 0.0, true, 300000.0, false, 1.0, nullptr, 0, false },
    { "multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_10[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "hit_damage", rl_kind::k_float, 0.0, true, 300000.0, false, 1.0, nullptr, 0, false },
    { "crit_pct_current", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_ACTION_LEAVES_MEMBERS[] = {
  { "ascendance", "ascendance", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_0, 1 },
  { "berserking", "berserking", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_1, 1 },
  { "chain_lightning", "chain_lightning", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_2, 2 },
  { "crash_lightning", "crash_lightning", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_3, 4 },
  { "lava_lash", "lava_lash", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_4, 4 },
  { "lightning_bolt", "lightning_bolt", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_5, 5 },
  { "stormstrike", "stormstrike", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_6, 4 },
  { "tempest", "tempest", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_7, 3 },
  { "use_item_voracious_heart_of_ulatek", "use_item_voracious_heart_of_ulatek", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_8, 1 },
  { "voltaic_blaze", "voltaic_blaze", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_9, 3 },
  { "windstrike", "windstrike", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_10, 4 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_0[] = {
    { "value", rl_kind::k_int, 0.0, true, 600.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_1[] = {
    { "value", rl_kind::k_int, 0.0, true, 600.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_2[] = {
    { "value", rl_kind::k_int, 0.0, true, 600.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_3[] = {
    { "value", rl_kind::k_int, 0.0, true, 250.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_4[] = {
    { "value", rl_kind::k_int, 0.0, true, 250.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_5[] = {
    { "value", rl_kind::k_int, 0.0, true, 250.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_6[] = {
    { "value", rl_kind::k_int, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_7[] = {
    { "value", rl_kind::k_int, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_8[] = {
    { "value", rl_kind::k_int, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_9[] = {
    { "value", rl_kind::k_int, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_10[] = {
    { "value", rl_kind::k_int, 0.0, true, 256.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_11[] = {
    { "value", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_12[] = {
    { "value", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_DECK_MEMBERS[] = {
  { "asc_dw_draws_left", "asc_dw_draws_left", RL_OBS_FAMILY_DECK_LEAVES_0, 1 },
  { "asc_dw_fail_left", "asc_dw_fail_left", RL_OBS_FAMILY_DECK_LEAVES_1, 1 },
  { "asc_dw_proc_left", "asc_dw_proc_left", RL_OBS_FAMILY_DECK_LEAVES_2, 1 },
  { "storm_unleashed_draws_left", "storm_unleashed_draws_left", RL_OBS_FAMILY_DECK_LEAVES_3, 1 },
  { "storm_unleashed_fail_left", "storm_unleashed_fail_left", RL_OBS_FAMILY_DECK_LEAVES_4, 1 },
  { "storm_unleashed_proc_left", "storm_unleashed_proc_left", RL_OBS_FAMILY_DECK_LEAVES_5, 1 },
  { "tempest_draws_left", "tempest_draws_left", RL_OBS_FAMILY_DECK_LEAVES_6, 1 },
  { "tempest_fail_left", "tempest_fail_left", RL_OBS_FAMILY_DECK_LEAVES_7, 1 },
  { "tempest_proc_left", "tempest_proc_left", RL_OBS_FAMILY_DECK_LEAVES_8, 1 },
  { "tempest_procs_this_deck", "tempest_procs_this_deck", RL_OBS_FAMILY_DECK_LEAVES_9, 1 },
  { "tempest_spends_since_proc", "tempest_spends_since_proc", RL_OBS_FAMILY_DECK_LEAVES_10, 1 },
  { "ti_chain_lightning", "ti_chain_lightning", RL_OBS_FAMILY_DECK_LEAVES_11, 1 },
  { "ti_lightning_bolt", "ti_lightning_bolt", RL_OBS_FAMILY_DECK_LEAVES_12, 1 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_STATS_LEAVES_0[] = {
    { "value", rl_kind::k_float, 0.0, true, 200000.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_STATS_LEAVES_1[] = {
    { "value", rl_kind::k_float, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_STATS_LEAVES_2[] = {
    { "value", rl_kind::k_float, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_STATS_LEAVES_3[] = {
    { "value", rl_kind::k_float, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_STATS_MEMBERS[] = {
  { "attack_power", "attack_power", RL_OBS_FAMILY_STATS_LEAVES_0, 1 },
  { "damage_versatility", "damage_versatility", RL_OBS_FAMILY_STATS_LEAVES_1, 1 },
  { "haste", "haste", RL_OBS_FAMILY_STATS_LEAVES_2, 1 },
  { "mastery_value", "mastery_value", RL_OBS_FAMILY_STATS_LEAVES_3, 1 },
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
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_20[] = {
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
  { "20", "20", RL_OBS_FAMILY_LEGALITY_LEAVES_20, 1 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_PROC_CHANCES_LEAVES_0[] = {
    { "value", rl_kind::k_float, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PROC_CHANCES_LEAVES_1[] = {
    { "value", rl_kind::k_float, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_PROC_CHANCES_MEMBERS[] = {
  { "stormsurge", "stormsurge_proc_chance_current", RL_OBS_FAMILY_PROC_CHANCES_LEAVES_0, 1 },
  { "windfury", "windfury_proc_chance_current", RL_OBS_FAMILY_PROC_CHANCES_LEAVES_1, 1 },
};

inline constexpr double RL_BUCKETS_SLOT309[] = { 1.0, 2.0, 5.0 };
inline constexpr rl_leaf_desc RL_OBS_FAMILY_SCALARS_LEAVES_0[] = {
    { "fight_remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, true },
    { "active_enemies", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT309, 3, false },
    { "raid_event_next_in", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "time_to_bloodlust", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "dying_within_5s", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "dying_within_15s", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "enemies_in_front", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "enemies_in_melee", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "enemies_total", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "enemies_within_8yd", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "enemies_within_40yd", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "flame_shock_carrier_count", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "immunity_in", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "longest_time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 600.0, nullptr, 0, false },
    { "nearest_enemy_distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "soonest_time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 600.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_SCALARS_MEMBERS[] = {
  { "", "", RL_OBS_FAMILY_SCALARS_LEAVES_0, 17 },
};

inline constexpr std::size_t RL_OBS_FAMILY_COUNT = 12;

inline constexpr rl_obs_family RL_OBS_FAMILIES[RL_OBS_FAMILY_COUNT] = {
  { rl_family::player_buffs, "player_buffs", rl_family_kind::buff, false, RL_OBS_FAMILY_PLAYER_BUFFS_MEMBERS, 31, 0, 45 },
  { rl_family::cooldowns, "cooldowns", rl_family_kind::cooldown, false, RL_OBS_FAMILY_COOLDOWNS_MEMBERS, 8, 45, 11 },
  { rl_family::target_facts, "target_facts", rl_family_kind::target_fact, false, RL_OBS_FAMILY_TARGET_FACTS_MEMBERS, 8, 56, 160 },
  { rl_family::shapes, "shapes", rl_family_kind::shape_fact, false, RL_OBS_FAMILY_SHAPES_MEMBERS, 2, 216, 6 },
  { rl_family::action_leaves, "action_leaves", rl_family_kind::action_expression, false, RL_OBS_FAMILY_ACTION_LEAVES_MEMBERS, 11, 222, 32 },
  { rl_family::deck, "deck", rl_family_kind::expression, false, RL_OBS_FAMILY_DECK_MEMBERS, 13, 254, 13 },
  { rl_family::stats, "stats", rl_family_kind::direct, false, RL_OBS_FAMILY_STATS_MEMBERS, 4, 267, 4 },
  { rl_family::swing_cast, "swing_cast", rl_family_kind::direct, false, RL_OBS_FAMILY_SWING_CAST_MEMBERS, 4, 271, 4 },
  { rl_family::raid_events, "raid_events", rl_family_kind::expression, false, RL_OBS_FAMILY_RAID_EVENTS_MEMBERS, 2, 275, 10 },
  { rl_family::legality, "legality", rl_family_kind::legality, false, RL_OBS_FAMILY_LEGALITY_MEMBERS, 21, 285, 21 },
  { rl_family::proc_chances, "proc_chances", rl_family_kind::proc_chance, false, RL_OBS_FAMILY_PROC_CHANCES_MEMBERS, 2, 306, 2 },
  { rl_family::scalars, "scalars", rl_family_kind::scalar, true, RL_OBS_FAMILY_SCALARS_MEMBERS, 1, 308, 17 },
};

// ---- Action descriptors ----

inline constexpr rl_action_desc RL_ACTIONS[RL_ACTION_DIM] = {
  { 0, "stormstrike", rl_action_kind::cast, "strike", nullptr, "stormstrike", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
  { 1, "lightning_bolt", rl_action_kind::cast, nullptr, nullptr, "lightning_bolt", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
  { 2, "chain_lightning", rl_action_kind::cast, nullptr, nullptr, "chain_lightning", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
  { 3, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_next_event", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
  { 4, "tempest", rl_action_kind::cast, nullptr, nullptr, "tempest", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
  { 5, "windstrike", rl_action_kind::cast, "strike", nullptr, "windstrike", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
  { 6, "crash_lightning", rl_action_kind::cast, "crash_lightning", nullptr, "crash_lightning", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
  { 7, "lava_lash", rl_action_kind::cast, "lava_lash", nullptr, "lava_lash", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
  { 8, "voltaic_blaze", rl_action_kind::cast, "voltaic_blaze", nullptr, "voltaic_blaze", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
  { 9, "ascendance", rl_action_kind::cast, "ascendance", nullptr, "ascendance", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
  { 10, "use_item_voracious_heart_of_ulatek", rl_action_kind::cast, "voracious_heart_of_ulatek_1297761", "item_cd_1141", "use_item_voracious_heart_of_ulatek", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
  { 11, "berserking", rl_action_kind::cast, "berserking", nullptr, "berserking", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
  { 12, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_cd_strike", { rl_wait_anchor_kind::cooldown, "strike", rl_swing_hand::none } },
  { 13, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_cd_lava_lash", { rl_wait_anchor_kind::cooldown, "lava_lash", rl_swing_hand::none } },
  { 14, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_cd_crash_lightning", { rl_wait_anchor_kind::cooldown, "crash_lightning", rl_swing_hand::none } },
  { 15, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_cd_voltaic_blaze", { rl_wait_anchor_kind::cooldown, "voltaic_blaze", rl_swing_hand::none } },
  { 16, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_swing_mh", { rl_wait_anchor_kind::swing, nullptr, rl_swing_hand::mh } },
  { 17, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_swing_oh", { rl_wait_anchor_kind::swing, nullptr, rl_swing_hand::oh } },
  { 18, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_maelstrom", { rl_wait_anchor_kind::maelstrom, nullptr, rl_swing_hand::none } },
  { 19, "potion", rl_action_kind::cast, "potion", nullptr, "potion", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
  { 20, nullptr, rl_action_kind::turn, nullptr, nullptr, "turn_to_face", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
};

// ---- Buff-gate table ----

inline constexpr rl_buff_gate RL_BUFF_GATES[] = {
  { "lightning_bolt", "tempest", true },
  { "tempest", "tempest", false },
  { "stormstrike", "ascendance", true },
  { "windstrike", "ascendance", false },
};
inline constexpr std::size_t RL_BUFF_GATE_COUNT = 4;

// ---- Talent-gate table ----

inline constexpr rl_talent_gate RL_TALENT_GATES[] = {
  { "windstrike", "ascendance", false },
  { "crash_lightning", "crash_lightning", false },
  { "lava_lash", "lava_lash", false },
  { "ascendance", "ascendance", false },
};
inline constexpr std::size_t RL_TALENT_GATE_COUNT = 4;

// ---- Target scorer feature list (Phase 230-02, SCOR-01) ----

inline constexpr std::size_t RL_TARGET_SLOTS = 8;
inline constexpr std::size_t RL_TARGET_FEATURES = 20;
inline constexpr const char* RL_TARGET_FEATURE_SHA = "tgt-feat-v1:f6986134d64961cb602ec85a9bc5aa3a0d69f929815b0cee2c6fc502de38dd0c";

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
  "flame_shock_remaining",
  "neighbours_within_radius",
  "is_current_target",
  "burning_core_remaining",
  "lightning_rod_stacks",
  "lightning_rod_remaining",
  "venomfang_remaining",
  "venomfang_debuff_stacks",
  "venomfang_debuff_remaining",
  "rune_of_unleashed_fire_lingering_remaining",
};

