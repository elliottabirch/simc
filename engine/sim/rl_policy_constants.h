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

enum class rl_family { player_buffs, cooldowns, enemy_slots, action_leaves, deck, stats, swing_cast, position, pets, items, raid_events, sim_auras, legality, proc_chances, scalars };
enum class rl_family_kind { buff, cooldown, enemy_slot, action_expression, expression, direct, legality, proc_chance, target_fact, scalar };
enum class rl_kind { k_int, k_float, k_seconds, k_bucket };
enum class rl_action_kind { cast, wait };
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
inline constexpr int RL_ENCODER_VERSION = 3;
inline constexpr std::size_t RL_OBS_DIM = 246;
inline constexpr std::size_t RL_ACTION_DIM = 26;
inline constexpr double RL_EPISODE_MAX_TIME = 300.0;
inline constexpr double RL_WAIT_FLOOR_SECONDS = 0.05;
inline constexpr double RL_PERMANENT_SATURATION = 1.0;
inline constexpr const char* RL_OBS_SCHEMA_SHA = "rl-obs-v3:96a31ccfbd3c7eaf97eed6452a984756c7f67e78e00c6f221a3b9b9f00134045";
inline constexpr const char* RL_MASK_RULES_SHA = "e7b395d2886a54529bf3a86581492ebe69eec56aa1e4abfce670d68c808cb7be";
inline constexpr const char* RL_ACTION_SPACE_SHA = "1fadf4ed553b45e704d773719ab238e618eee4e35df9cd6a9a993636ff008665";

// ---- Observation name list (the materialised ordering) ----

inline constexpr const char* RL_OBS_NAMES[RL_OBS_DIM] = {
  "player_buffs.arc_discharge.stacks",
  "player_buffs.arc_discharge.remains",
  "player_buffs.arcanoweave_insight.stacks",
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
  "player_buffs.critical_ritual.stacks",
  "player_buffs.critical_ritual.remains",
  "player_buffs.devoured_strength.stacks",
  "player_buffs.devoured_strength.remains",
  "player_buffs.doom_winds.stacks",
  "player_buffs.doom_winds.remains",
  "player_buffs.flurry.stacks",
  "player_buffs.flurry.remains",
  "player_buffs.frenzied_focus.stacks",
  "player_buffs.frenzied_focus.remains",
  "player_buffs.genius_insight.stacks",
  "player_buffs.genius_insight.remains",
  "player_buffs.hasty_ritual.stacks",
  "player_buffs.hasty_ritual.remains",
  "player_buffs.lightning_strikes.stacks",
  "player_buffs.lightning_strikes.remains",
  "player_buffs.maelstrom_weapon.stacks",
  "player_buffs.maelstrom_weapon.remains",
  "player_buffs.masterful_ritual.stacks",
  "player_buffs.masterful_ritual.remains",
  "player_buffs.rune_of_burning_haste.stacks",
  "player_buffs.rune_of_burning_haste.remains",
  "player_buffs.storm_swell.stacks",
  "player_buffs.storm_swell.remains",
  "player_buffs.storm_unleashed.stacks",
  "player_buffs.storm_unleashed.remains",
  "player_buffs.stormblast.stacks",
  "player_buffs.stormblast.remains",
  "player_buffs.stormsurge.stacks",
  "player_buffs.stormsurge.remains",
  "player_buffs.tempest.stacks",
  "player_buffs.tempest.remains",
  "player_buffs.unlimited_power.stacks",
  "player_buffs.unlimited_power.remains",
  "player_buffs.venomcursed_ascendance.stacks",
  "player_buffs.venomcursed_ascendance.remains",
  "player_buffs.venomcursed_mastery.stacks",
  "player_buffs.venomcursed_mastery.remains",
  "player_buffs.versatile_ritual.stacks",
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
  "enemy_slots.slot0.present",
  "enemy_slots.slot0.distance",
  "enemy_slots.slot0.time_to_die",
  "enemy_slots.slot0.health_pct",
  "enemy_slots.slot0.role",
  "enemy_slots.slot0.burning_core.remains",
  "enemy_slots.slot0.flame_shock.remains",
  "enemy_slots.slot0.lightning_rod.stacks",
  "enemy_slots.slot0.lightning_rod.remains",
  "enemy_slots.slot0.rune_of_unleashed_fire_lingering.ticking",
  "enemy_slots.slot0.rune_of_unleashed_fire_lingering.remains",
  "enemy_slots.slot0.venomfang.ticking",
  "enemy_slots.slot0.venomfang.remains",
  "enemy_slots.slot0.venomfang_debuff.stacks",
  "enemy_slots.slot0.venomfang_debuff.remains",
  "enemy_slots.slot1.present",
  "enemy_slots.slot1.distance",
  "enemy_slots.slot1.time_to_die",
  "enemy_slots.slot1.health_pct",
  "enemy_slots.slot1.role",
  "enemy_slots.slot1.lightning_rod.stacks",
  "enemy_slots.slot1.lightning_rod.remains",
  "enemy_slots.slot1.rune_of_unleashed_fire_lingering.ticking",
  "enemy_slots.slot1.rune_of_unleashed_fire_lingering.remains",
  "enemy_slots.slot1.venomfang.ticking",
  "enemy_slots.slot1.venomfang.remains",
  "enemy_slots.slot1.venomfang_debuff.stacks",
  "enemy_slots.slot1.venomfang_debuff.remains",
  "enemy_slots.slot2.present",
  "enemy_slots.slot2.distance",
  "enemy_slots.slot2.time_to_die",
  "enemy_slots.slot2.health_pct",
  "enemy_slots.slot2.role",
  "enemy_slots.slot2.lightning_rod.stacks",
  "enemy_slots.slot2.lightning_rod.remains",
  "enemy_slots.slot2.rune_of_unleashed_fire_lingering.ticking",
  "enemy_slots.slot2.rune_of_unleashed_fire_lingering.remains",
  "enemy_slots.slot2.venomfang.ticking",
  "enemy_slots.slot2.venomfang.remains",
  "enemy_slots.slot2.venomfang_debuff.stacks",
  "enemy_slots.slot2.venomfang_debuff.remains",
  "enemy_slots.slot3.present",
  "enemy_slots.slot3.distance",
  "enemy_slots.slot3.time_to_die",
  "enemy_slots.slot3.health_pct",
  "enemy_slots.slot3.role",
  "enemy_slots.slot3.lightning_rod.stacks",
  "enemy_slots.slot3.lightning_rod.remains",
  "enemy_slots.slot3.rune_of_unleashed_fire_lingering.ticking",
  "enemy_slots.slot3.rune_of_unleashed_fire_lingering.remains",
  "enemy_slots.slot3.venomfang.ticking",
  "enemy_slots.slot3.venomfang.remains",
  "enemy_slots.slot3.venomfang_debuff.stacks",
  "enemy_slots.slot3.venomfang_debuff.remains",
  "enemy_slots.slot4.present",
  "enemy_slots.slot4.distance",
  "enemy_slots.slot4.time_to_die",
  "enemy_slots.slot4.health_pct",
  "enemy_slots.slot4.role",
  "enemy_slots.slot4.lightning_rod.stacks",
  "enemy_slots.slot4.lightning_rod.remains",
  "enemy_slots.slot4.rune_of_unleashed_fire_lingering.ticking",
  "enemy_slots.slot4.rune_of_unleashed_fire_lingering.remains",
  "enemy_slots.slot4.venomfang.ticking",
  "enemy_slots.slot4.venomfang.remains",
  "enemy_slots.slot4.venomfang_debuff.stacks",
  "enemy_slots.slot4.venomfang_debuff.remains",
  "action_leaves.ascendance.ready",
  "action_leaves.berserking.ready",
  "action_leaves.chain_lightning.hit_damage",
  "action_leaves.chain_lightning.crit_pct_current",
  "action_leaves.chain_lightning.spell_targets",
  "action_leaves.crash_lightning.ready",
  "action_leaves.crash_lightning.hit_damage",
  "action_leaves.crash_lightning.crit_pct_current",
  "action_leaves.crash_lightning.multiplier",
  "action_leaves.crash_lightning.persistent_multiplier",
  "action_leaves.doom_winds.ready",
  "action_leaves.lava_lash.ready",
  "action_leaves.lava_lash.hit_damage",
  "action_leaves.lava_lash.crit_pct_current",
  "action_leaves.lava_lash.multiplier",
  "action_leaves.lava_lash.persistent_multiplier",
  "action_leaves.lava_lash.molten_weapon_ticking",
  "action_leaves.lava_lash.molten_weapon_remains",
  "action_leaves.lightning_bolt.ready",
  "action_leaves.lightning_bolt.hit_damage",
  "action_leaves.lightning_bolt.crit_pct_current",
  "action_leaves.lightning_bolt.in_flight",
  "action_leaves.lightning_bolt.in_flight_count",
  "action_leaves.lightning_bolt.in_flight_remains",
  "action_leaves.primordial_storm.ready",
  "action_leaves.stormstrike.ready",
  "action_leaves.stormstrike.hit_damage",
  "action_leaves.stormstrike.crit_pct_current",
  "action_leaves.sundering.ready",
  "action_leaves.sundering.hit_damage",
  "action_leaves.sundering.crit_pct_current",
  "action_leaves.sundering.multiplier",
  "action_leaves.sundering.persistent_multiplier",
  "action_leaves.surging_totem.ready",
  "action_leaves.surging_totem.pet_surging_totem_active",
  "action_leaves.surging_totem.pet_surging_totem_remains",
  "action_leaves.tempest.ready",
  "action_leaves.tempest.hit_damage",
  "action_leaves.tempest.crit_pct_current",
  "action_leaves.use_item_voracious_heart_of_ulatek.ready",
  "action_leaves.voltaic_blaze.ready",
  "action_leaves.voltaic_blaze.hit_damage",
  "action_leaves.voltaic_blaze.crit_pct_current",
  "action_leaves.voltaic_blaze.multiplier",
  "action_leaves.voltaic_blaze.persistent_multiplier",
  "action_leaves.windstrike.ready",
  "action_leaves.windstrike.hit_damage",
  "action_leaves.windstrike.crit_pct_current",
  "action_leaves.windstrike.multiplier",
  "action_leaves.windstrike.persistent_multiplier",
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
  "legality.21.flag",
  "legality.22.flag",
  "legality.23.flag",
  "legality.24.flag",
  "legality.25.flag",
  "proc_chances.maelstrom_weapon.value",
  "proc_chances.stormsurge.value",
  "proc_chances.windfury.value",
  "fight_remains",
  "active_enemies",
  "t",
  "raid_event_next_in",
  "time_to_bloodlust",
};

// ---- Per-family observation tables ----

inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_0[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_1[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
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
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_9[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_10[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_11[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 3.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_12[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_13[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_14[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_15[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 3.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_16[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_17[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_18[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 8.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_19[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_20[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_21[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_22[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_23[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_24[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 15.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_25[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_26[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_27[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_PLAYER_BUFFS_MEMBERS[] = {
  { "arc_discharge", "arc_discharge", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_0, 2 },
  { "arcanoweave_insight", "arcanoweave_insight", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_1, 2 },
  { "ascendance", "ascendance", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_2, 1 },
  { "berserking", "berserking", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_3, 1 },
  { "bloodlust", "bloodlust", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_4, 1 },
  { "converging_storms", "converging_storms", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_5, 2 },
  { "crackling_surge", "crackling_surge", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_6, 2 },
  { "crash_lightning", "crash_lightning", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_7, 2 },
  { "critical_ritual", "critical_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_8, 2 },
  { "devoured_strength", "devoured_strength", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_9, 2 },
  { "doom_winds", "doom_winds", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_10, 2 },
  { "flurry", "flurry", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_11, 2 },
  { "frenzied_focus", "frenzied_focus", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_12, 2 },
  { "genius_insight", "genius_insight", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_13, 2 },
  { "hasty_ritual", "hasty_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_14, 2 },
  { "lightning_strikes", "lightning_strikes", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_15, 2 },
  { "maelstrom_weapon", "maelstrom_weapon", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_16, 2 },
  { "masterful_ritual", "masterful_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_17, 2 },
  { "rune_of_burning_haste", "rune_of_burning_haste", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_18, 2 },
  { "storm_swell", "storm_swell", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_19, 2 },
  { "storm_unleashed", "storm_unleashed", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_20, 2 },
  { "stormblast", "stormblast", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_21, 2 },
  { "stormsurge", "stormsurge", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_22, 2 },
  { "tempest", "tempest", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_23, 2 },
  { "unlimited_power", "unlimited_power", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_24, 2 },
  { "venomcursed_ascendance", "venomcursed_ascendance", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_25, 2 },
  { "venomcursed_mastery", "venomcursed_mastery", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_26, 2 },
  { "versatile_ritual", "versatile_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_27, 2 },
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

inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_0[] = {
    { "present", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 40.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "role", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_1[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_2[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 18.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_3[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_4[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_5[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_6[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 8.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_7[] = {
    { "present", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 40.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "role", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_8[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_9[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_10[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_11[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 8.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_12[] = {
    { "present", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 40.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "role", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_13[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_14[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_15[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_16[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 8.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_17[] = {
    { "present", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 40.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "role", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_18[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_19[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_20[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_21[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 8.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_22[] = {
    { "present", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 40.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "role", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_23[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_24[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_25[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_26[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 8.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_ENEMY_SLOTS_MEMBERS[] = {
  { "slot0", "slot0", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_0, 5 },
  { "slot0.burning_core", "slot0.burning_core", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_1, 1 },
  { "slot0.flame_shock", "slot0.flame_shock", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_2, 1 },
  { "slot0.lightning_rod", "slot0.lightning_rod", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_3, 2 },
  { "slot0.rune_of_unleashed_fire_lingering", "slot0.rune_of_unleashed_fire_lingering", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_4, 2 },
  { "slot0.venomfang", "slot0.venomfang", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_5, 2 },
  { "slot0.venomfang_debuff", "slot0.venomfang_debuff", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_6, 2 },
  { "slot1", "slot1", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_7, 5 },
  { "slot1.lightning_rod", "slot1.lightning_rod", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_8, 2 },
  { "slot1.rune_of_unleashed_fire_lingering", "slot1.rune_of_unleashed_fire_lingering", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_9, 2 },
  { "slot1.venomfang", "slot1.venomfang", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_10, 2 },
  { "slot1.venomfang_debuff", "slot1.venomfang_debuff", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_11, 2 },
  { "slot2", "slot2", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_12, 5 },
  { "slot2.lightning_rod", "slot2.lightning_rod", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_13, 2 },
  { "slot2.rune_of_unleashed_fire_lingering", "slot2.rune_of_unleashed_fire_lingering", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_14, 2 },
  { "slot2.venomfang", "slot2.venomfang", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_15, 2 },
  { "slot2.venomfang_debuff", "slot2.venomfang_debuff", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_16, 2 },
  { "slot3", "slot3", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_17, 5 },
  { "slot3.lightning_rod", "slot3.lightning_rod", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_18, 2 },
  { "slot3.rune_of_unleashed_fire_lingering", "slot3.rune_of_unleashed_fire_lingering", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_19, 2 },
  { "slot3.venomfang", "slot3.venomfang", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_20, 2 },
  { "slot3.venomfang_debuff", "slot3.venomfang_debuff", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_21, 2 },
  { "slot4", "slot4", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_22, 5 },
  { "slot4.lightning_rod", "slot4.lightning_rod", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_23, 2 },
  { "slot4.rune_of_unleashed_fire_lingering", "slot4.rune_of_unleashed_fire_lingering", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_24, 2 },
  { "slot4.venomfang", "slot4.venomfang", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_25, 2 },
  { "slot4.venomfang_debuff", "slot4.venomfang_debuff", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_26, 2 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_0[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_1[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_2[] = {
    { "hit_damage", rl_kind::k_float, 0.0, true, 1000000.0, false, 1.0, nullptr, 0, false },
    { "crit_pct_current", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "spell_targets", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_3[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "hit_damage", rl_kind::k_float, 0.0, true, 1000000.0, false, 1.0, nullptr, 0, false },
    { "crit_pct_current", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "persistent_multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_4[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_5[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "hit_damage", rl_kind::k_float, 0.0, true, 1000000.0, false, 1.0, nullptr, 0, false },
    { "crit_pct_current", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "persistent_multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "molten_weapon_ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "molten_weapon_remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 4.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_6[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "hit_damage", rl_kind::k_float, 0.0, true, 1000000.0, false, 1.0, nullptr, 0, false },
    { "crit_pct_current", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "in_flight", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_flight_count", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "in_flight_remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 2.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_7[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_8[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "hit_damage", rl_kind::k_float, 0.0, true, 1000000.0, false, 1.0, nullptr, 0, false },
    { "crit_pct_current", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_9[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "hit_damage", rl_kind::k_float, 0.0, true, 1000000.0, false, 1.0, nullptr, 0, false },
    { "crit_pct_current", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "persistent_multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_10[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "pet_surging_totem_active", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "pet_surging_totem_remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 24.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_11[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "hit_damage", rl_kind::k_float, 0.0, true, 1000000.0, false, 1.0, nullptr, 0, false },
    { "crit_pct_current", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_12[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_13[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "hit_damage", rl_kind::k_float, 0.0, true, 1000000.0, false, 1.0, nullptr, 0, false },
    { "crit_pct_current", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "persistent_multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_14[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "hit_damage", rl_kind::k_float, 0.0, true, 1000000.0, false, 1.0, nullptr, 0, false },
    { "crit_pct_current", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "persistent_multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_ACTION_LEAVES_MEMBERS[] = {
  { "ascendance", "ascendance", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_0, 1 },
  { "berserking", "berserking", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_1, 1 },
  { "chain_lightning", "chain_lightning", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_2, 3 },
  { "crash_lightning", "crash_lightning", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_3, 5 },
  { "doom_winds", "doom_winds", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_4, 1 },
  { "lava_lash", "lava_lash", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_5, 7 },
  { "lightning_bolt", "lightning_bolt", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_6, 6 },
  { "primordial_storm", "primordial_storm", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_7, 1 },
  { "stormstrike", "stormstrike", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_8, 3 },
  { "sundering", "sundering", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_9, 5 },
  { "surging_totem", "surging_totem", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_10, 3 },
  { "tempest", "tempest", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_11, 3 },
  { "use_item_voracious_heart_of_ulatek", "use_item_voracious_heart_of_ulatek", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_12, 1 },
  { "voltaic_blaze", "voltaic_blaze", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_13, 5 },
  { "windstrike", "windstrike", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_14, 5 },
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
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_21[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_22[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_23[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_24[] = {
    { "flag", rl_kind::k_int, 0.0, false, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_25[] = {
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
  { "21", "21", RL_OBS_FAMILY_LEGALITY_LEAVES_21, 1 },
  { "22", "22", RL_OBS_FAMILY_LEGALITY_LEAVES_22, 1 },
  { "23", "23", RL_OBS_FAMILY_LEGALITY_LEAVES_23, 1 },
  { "24", "24", RL_OBS_FAMILY_LEGALITY_LEAVES_24, 1 },
  { "25", "25", RL_OBS_FAMILY_LEGALITY_LEAVES_25, 1 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_PROC_CHANCES_LEAVES_0[] = {
    { "value", rl_kind::k_float, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PROC_CHANCES_LEAVES_1[] = {
    { "value", rl_kind::k_float, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PROC_CHANCES_LEAVES_2[] = {
    { "value", rl_kind::k_float, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_PROC_CHANCES_MEMBERS[] = {
  { "maelstrom_weapon", "maelstrom_weapon_proc_chance_current", RL_OBS_FAMILY_PROC_CHANCES_LEAVES_0, 1 },
  { "stormsurge", "stormsurge_proc_chance_current", RL_OBS_FAMILY_PROC_CHANCES_LEAVES_1, 1 },
  { "windfury", "windfury_proc_chance_current", RL_OBS_FAMILY_PROC_CHANCES_LEAVES_2, 1 },
};

inline constexpr double RL_BUCKETS_SLOT242[] = { 1.0, 2.0, 5.0 };
inline constexpr rl_leaf_desc RL_OBS_FAMILY_SCALARS_LEAVES_0[] = {
    { "fight_remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, true },
    { "active_enemies", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT242, 3, false },
    { "t", rl_kind::k_seconds, 0.0, false, 1.0, true, 300.0, nullptr, 0, false },
    { "raid_event_next_in", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "time_to_bloodlust", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_SCALARS_MEMBERS[] = {
  { "", "", RL_OBS_FAMILY_SCALARS_LEAVES_0, 5 },
};

inline constexpr std::size_t RL_OBS_FAMILY_COUNT = 11;

inline constexpr rl_obs_family RL_OBS_FAMILIES[RL_OBS_FAMILY_COUNT] = {
  { rl_family::player_buffs, "player_buffs", rl_family_kind::buff, false, RL_OBS_FAMILY_PLAYER_BUFFS_MEMBERS, 28, 0, 53 },
  { rl_family::cooldowns, "cooldowns", rl_family_kind::cooldown, false, RL_OBS_FAMILY_COOLDOWNS_MEMBERS, 8, 53, 11 },
  { rl_family::enemy_slots, "enemy_slots", rl_family_kind::enemy_slot, false, RL_OBS_FAMILY_ENEMY_SLOTS_MEMBERS, 27, 64, 67 },
  { rl_family::action_leaves, "action_leaves", rl_family_kind::action_expression, false, RL_OBS_FAMILY_ACTION_LEAVES_MEMBERS, 15, 131, 50 },
  { rl_family::deck, "deck", rl_family_kind::expression, false, RL_OBS_FAMILY_DECK_MEMBERS, 13, 181, 13 },
  { rl_family::stats, "stats", rl_family_kind::direct, false, RL_OBS_FAMILY_STATS_MEMBERS, 4, 194, 4 },
  { rl_family::swing_cast, "swing_cast", rl_family_kind::direct, false, RL_OBS_FAMILY_SWING_CAST_MEMBERS, 4, 198, 4 },
  { rl_family::raid_events, "raid_events", rl_family_kind::expression, false, RL_OBS_FAMILY_RAID_EVENTS_MEMBERS, 2, 202, 10 },
  { rl_family::legality, "legality", rl_family_kind::legality, false, RL_OBS_FAMILY_LEGALITY_MEMBERS, 26, 212, 26 },
  { rl_family::proc_chances, "proc_chances", rl_family_kind::proc_chance, false, RL_OBS_FAMILY_PROC_CHANCES_MEMBERS, 3, 238, 3 },
  { rl_family::scalars, "scalars", rl_family_kind::scalar, true, RL_OBS_FAMILY_SCALARS_MEMBERS, 1, 241, 5 },
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
  { 9, "sundering", rl_action_kind::cast, "sundering", nullptr, "sundering", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
  { 10, "ascendance", rl_action_kind::cast, "ascendance", nullptr, "ascendance", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
  { 11, "doom_winds", rl_action_kind::cast, "doom_winds", nullptr, "doom_winds", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
  { 12, "primordial_storm", rl_action_kind::cast, nullptr, nullptr, "primordial_storm", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
  { 13, "surging_totem", rl_action_kind::cast, "surging_totem", nullptr, "surging_totem", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
  { 14, "use_item_voracious_heart_of_ulatek", rl_action_kind::cast, "voracious_heart_of_ulatek_1297761", "item_cd_1141", "use_item_voracious_heart_of_ulatek", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
  { 15, "berserking", rl_action_kind::cast, "berserking", nullptr, "berserking", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
  { 16, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_cd_strike", { rl_wait_anchor_kind::cooldown, "strike", rl_swing_hand::none } },
  { 17, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_cd_lava_lash", { rl_wait_anchor_kind::cooldown, "lava_lash", rl_swing_hand::none } },
  { 18, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_cd_crash_lightning", { rl_wait_anchor_kind::cooldown, "crash_lightning", rl_swing_hand::none } },
  { 19, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_cd_voltaic_blaze", { rl_wait_anchor_kind::cooldown, "voltaic_blaze", rl_swing_hand::none } },
  { 20, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_swing_mh", { rl_wait_anchor_kind::swing, nullptr, rl_swing_hand::mh } },
  { 21, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_swing_oh", { rl_wait_anchor_kind::swing, nullptr, rl_swing_hand::oh } },
  { 22, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_maelstrom", { rl_wait_anchor_kind::maelstrom, nullptr, rl_swing_hand::none } },
  { 23, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_gcd", { rl_wait_anchor_kind::gcd, nullptr, rl_swing_hand::none } },
  { 24, "potion", rl_action_kind::cast, "potion", nullptr, "potion", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
  { 25, "use_item_zuljins_guillotine_technique", rl_action_kind::cast, nullptr, nullptr, "use_item_zuljins_guillotine_technique", { rl_wait_anchor_kind::none, nullptr, rl_swing_hand::none } },
};

// ---- Buff-gate table ----

inline constexpr rl_buff_gate RL_BUFF_GATES[] = {
  { "lightning_bolt", "tempest", true },
  { "tempest", "tempest", false },
  { "stormstrike", "ascendance", true },
  { "windstrike", "ascendance", false },
  { "sundering", "primordial_storm", true },
  { "primordial_storm", "primordial_storm", false },
};
inline constexpr std::size_t RL_BUFF_GATE_COUNT = 6;

// ---- Talent-gate table ----

inline constexpr rl_talent_gate RL_TALENT_GATES[] = {
  { "windstrike", "ascendance", false },
  { "crash_lightning", "crash_lightning", false },
  { "lava_lash", "lava_lash", false },
  { "sundering", "sundering", false },
  { "ascendance", "ascendance", false },
  { "doom_winds", "doom_winds", false },
  { "doom_winds", "ascendance", true },
  { "doom_winds", "deeply_rooted_elements", true },
  { "primordial_storm", "primordial_storm", false },
  { "surging_totem", "surging_totem", false },
};
inline constexpr std::size_t RL_TALENT_GATE_COUNT = 10;

