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

enum class rl_family { player_buffs, cooldowns, enemy_slots, action_leaves, deck, stats, swing_cast, position, pets, items, raid_events, sim_auras, scalars };
enum class rl_family_kind { buff, cooldown, enemy_slot, action_expression, expression, direct, scalar };
enum class rl_kind { k_int, k_float, k_seconds, k_bucket };
enum class rl_action_kind { cast, wait };

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
inline constexpr int RL_ENCODER_VERSION = 2;
inline constexpr std::size_t RL_OBS_DIM = 386;
inline constexpr std::size_t RL_ACTION_DIM = 5;
inline constexpr double RL_EPISODE_MAX_TIME = 300.0;
inline constexpr double RL_WAIT_FLOOR_SECONDS = 0.05;
inline constexpr double RL_PERMANENT_SATURATION = 1.0;
inline constexpr const char* RL_OBS_SCHEMA_SHA = "rl-obs-v2:af22166a3b4cb90079e42887114a27d163771dab63d4d42df946ab37c401ca05";
inline constexpr const char* RL_MASK_RULES_SHA = "59400ba29a6bd6102bbbb0daefae1379b58f730f76c3dde2d70d233e1e478c42";
inline constexpr const char* RL_ACTION_SPACE_SHA = "4c06590d941d87cb5894ba430991ffa98a57332e1e5687df20b161bb08fddcf9";

// ---- Observation name list (the materialised ordering) ----

inline constexpr const char* RL_OBS_NAMES[RL_OBS_DIM] = {
  "player_buffs.amplification_core.stacks",
  "player_buffs.amplification_core.remains",
  "player_buffs.arc_discharge.stacks",
  "player_buffs.arc_discharge.remains",
  "player_buffs.arcanoweave_insight.stacks",
  "player_buffs.arcanoweave_insight.remains",
  "player_buffs.ascendance.stacks",
  "player_buffs.ascendance.remains",
  "player_buffs.berserking.stacks",
  "player_buffs.berserking.remains",
  "player_buffs.bloodlust.stacks",
  "player_buffs.bloodlust.remains",
  "player_buffs.converging_storms.stacks",
  "player_buffs.converging_storms.remains",
  "player_buffs.crackling_surge.stacks",
  "player_buffs.crackling_surge.remains",
  "player_buffs.crash_lightning.stacks",
  "player_buffs.crash_lightning.remains",
  "player_buffs.critical_ritual.stacks",
  "player_buffs.critical_ritual.remains",
  "player_buffs.damage_done.stacks",
  "player_buffs.damage_done.remains",
  "player_buffs.devoured_strength.stacks",
  "player_buffs.devoured_strength.remains",
  "player_buffs.doom_winds.stacks",
  "player_buffs.doom_winds.remains",
  "player_buffs.elemental_overflow.stacks",
  "player_buffs.elemental_overflow.remains",
  "player_buffs.exhaustion.stacks",
  "player_buffs.flametongue_weapon.stacks",
  "player_buffs.flask.stacks",
  "player_buffs.flurry.stacks",
  "player_buffs.flurry.remains",
  "player_buffs.food.stacks",
  "player_buffs.frenzied_focus.stacks",
  "player_buffs.frenzied_focus.remains",
  "player_buffs.genius_insight.stacks",
  "player_buffs.genius_insight.remains",
  "player_buffs.ghost_wolf.stacks",
  "player_buffs.hasty_ritual.stacks",
  "player_buffs.hasty_ritual.remains",
  "player_buffs.hot_hand.stacks",
  "player_buffs.hot_hand.remains",
  "player_buffs.lightning_strikes.stacks",
  "player_buffs.lightning_strikes.remains",
  "player_buffs.lively_totems.stacks",
  "player_buffs.lively_totems.remains",
  "player_buffs.maelstrom_weapon.stacks",
  "player_buffs.maelstrom_weapon.remains",
  "player_buffs.masterful_ritual.stacks",
  "player_buffs.masterful_ritual.remains",
  "player_buffs.mid2_enh_4pc.stacks",
  "player_buffs.mid2_enh_4pc.remains",
  "player_buffs.molten_weapon.stacks",
  "player_buffs.molten_weapon.remains",
  "player_buffs.movement.remains",
  "player_buffs.natures_swiftness.stacks",
  "player_buffs.natures_swiftness.remains",
  "player_buffs.potion.stacks",
  "player_buffs.potion.remains",
  "player_buffs.power_infusion.stacks",
  "player_buffs.primordial_storm.stacks",
  "player_buffs.primordial_storm.remains",
  "player_buffs.rooted.stacks",
  "player_buffs.rune_of_burning_haste.stacks",
  "player_buffs.rune_of_burning_haste.remains",
  "player_buffs.spirit_walk.stacks",
  "player_buffs.spirit_walk.remains",
  "player_buffs.static_accumulation.stacks",
  "player_buffs.storm_swell.stacks",
  "player_buffs.storm_swell.remains",
  "player_buffs.storm_unleashed.stacks",
  "player_buffs.storm_unleashed.remains",
  "player_buffs.stormblast.stacks",
  "player_buffs.stormblast.remains",
  "player_buffs.stormsurge.stacks",
  "player_buffs.stormsurge.remains",
  "player_buffs.stunned.stacks",
  "player_buffs.surging_elements.stacks",
  "player_buffs.surging_elements.remains",
  "player_buffs.surging_totem.stacks",
  "player_buffs.surging_totem.remains",
  "player_buffs.tempest.stacks",
  "player_buffs.tempest.remains",
  "player_buffs.totemic_rebound.stacks",
  "player_buffs.totemic_rebound.remains",
  "player_buffs.unlimited_power.stacks",
  "player_buffs.unlimited_power.remains",
  "player_buffs.venomcursed_ascendance.stacks",
  "player_buffs.venomcursed_ascendance.remains",
  "player_buffs.venomcursed_mastery.stacks",
  "player_buffs.venomcursed_mastery.remains",
  "player_buffs.versatile_ritual.stacks",
  "player_buffs.versatile_ritual.remains",
  "player_buffs.voidtouched.stacks",
  "player_buffs.whirling_air.stacks",
  "player_buffs.whirling_air.remains",
  "player_buffs.whirling_earth.stacks",
  "player_buffs.whirling_earth.remains",
  "player_buffs.whirling_fire.stacks",
  "player_buffs.whirling_fire.remains",
  "cooldowns.ascendance.remains",
  "cooldowns.berserking.remains",
  "cooldowns.crash_lightning.remains",
  "cooldowns.doom_winds.remains",
  "cooldowns.item_cd_1141.remains",
  "cooldowns.lava_lash.remains",
  "cooldowns.strike.charges_fractional",
  "cooldowns.strike.charges",
  "cooldowns.strike.recharge_time",
  "cooldowns.strike.full_recharge_time",
  "cooldowns.sundering.remains",
  "cooldowns.surging_totem.remains",
  "cooldowns.voltaic_blaze.remains",
  "cooldowns.voracious_heart_of_ulatek_1297761.remains",
  "enemy_slots.slot0.present",
  "enemy_slots.slot0.distance",
  "enemy_slots.slot0.time_to_die",
  "enemy_slots.slot0.health_pct",
  "enemy_slots.slot0.role",
  "enemy_slots.slot0.burning_core.stacks",
  "enemy_slots.slot0.burning_core.remains",
  "enemy_slots.slot0.burning_core.tick_time",
  "enemy_slots.slot0.casting.stacks",
  "enemy_slots.slot0.flame_shock.ticking",
  "enemy_slots.slot0.flame_shock.remains",
  "enemy_slots.slot0.flame_shock.tick_time",
  "enemy_slots.slot0.flame_shock.tick_dmg",
  "enemy_slots.slot0.flame_shock.pmultiplier",
  "enemy_slots.slot0.flametongue_attack.stacks",
  "enemy_slots.slot0.flametongue_attack.remains",
  "enemy_slots.slot0.lashing_flames.stacks",
  "enemy_slots.slot0.lashing_flames.remains",
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
  "enemy_slots.slot1.burning_core.stacks",
  "enemy_slots.slot1.burning_core.remains",
  "enemy_slots.slot1.burning_core.tick_time",
  "enemy_slots.slot1.casting.stacks",
  "enemy_slots.slot1.flame_shock.ticking",
  "enemy_slots.slot1.flame_shock.remains",
  "enemy_slots.slot1.flame_shock.tick_time",
  "enemy_slots.slot1.flame_shock.tick_dmg",
  "enemy_slots.slot1.flame_shock.pmultiplier",
  "enemy_slots.slot1.flametongue_attack.stacks",
  "enemy_slots.slot1.flametongue_attack.remains",
  "enemy_slots.slot1.lashing_flames.stacks",
  "enemy_slots.slot1.lashing_flames.remains",
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
  "enemy_slots.slot2.burning_core.stacks",
  "enemy_slots.slot2.burning_core.remains",
  "enemy_slots.slot2.burning_core.tick_time",
  "enemy_slots.slot2.casting.stacks",
  "enemy_slots.slot2.flame_shock.ticking",
  "enemy_slots.slot2.flame_shock.remains",
  "enemy_slots.slot2.flame_shock.tick_time",
  "enemy_slots.slot2.flame_shock.tick_dmg",
  "enemy_slots.slot2.flame_shock.pmultiplier",
  "enemy_slots.slot2.flametongue_attack.stacks",
  "enemy_slots.slot2.flametongue_attack.remains",
  "enemy_slots.slot2.lashing_flames.stacks",
  "enemy_slots.slot2.lashing_flames.remains",
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
  "enemy_slots.slot3.burning_core.stacks",
  "enemy_slots.slot3.burning_core.remains",
  "enemy_slots.slot3.burning_core.tick_time",
  "enemy_slots.slot3.casting.stacks",
  "enemy_slots.slot3.flame_shock.ticking",
  "enemy_slots.slot3.flame_shock.remains",
  "enemy_slots.slot3.flame_shock.tick_time",
  "enemy_slots.slot3.flame_shock.tick_dmg",
  "enemy_slots.slot3.flame_shock.pmultiplier",
  "enemy_slots.slot3.flametongue_attack.stacks",
  "enemy_slots.slot3.flametongue_attack.remains",
  "enemy_slots.slot3.lashing_flames.stacks",
  "enemy_slots.slot3.lashing_flames.remains",
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
  "enemy_slots.slot4.burning_core.stacks",
  "enemy_slots.slot4.burning_core.remains",
  "enemy_slots.slot4.burning_core.tick_time",
  "enemy_slots.slot4.casting.stacks",
  "enemy_slots.slot4.flame_shock.ticking",
  "enemy_slots.slot4.flame_shock.remains",
  "enemy_slots.slot4.flame_shock.tick_time",
  "enemy_slots.slot4.flame_shock.tick_dmg",
  "enemy_slots.slot4.flame_shock.pmultiplier",
  "enemy_slots.slot4.flametongue_attack.stacks",
  "enemy_slots.slot4.flametongue_attack.remains",
  "enemy_slots.slot4.lashing_flames.stacks",
  "enemy_slots.slot4.lashing_flames.remains",
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
  "action_leaves.chain_lightning.ready",
  "action_leaves.chain_lightning.hit_damage",
  "action_leaves.chain_lightning.crit_pct_current",
  "action_leaves.chain_lightning.multiplier",
  "action_leaves.chain_lightning.persistent_multiplier",
  "action_leaves.chain_lightning.travel_time",
  "action_leaves.chain_lightning.in_flight",
  "action_leaves.chain_lightning.in_flight_to_target",
  "action_leaves.chain_lightning.in_flight_count",
  "action_leaves.chain_lightning.in_flight_remains",
  "action_leaves.chain_lightning.spell_targets",
  "action_leaves.crash_lightning.ready",
  "action_leaves.crash_lightning.hit_damage",
  "action_leaves.crash_lightning.crit_pct_current",
  "action_leaves.crash_lightning.multiplier",
  "action_leaves.crash_lightning.persistent_multiplier",
  "action_leaves.crash_lightning.active_enemies_within_8",
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
  "action_leaves.lightning_bolt.multiplier",
  "action_leaves.lightning_bolt.persistent_multiplier",
  "action_leaves.lightning_bolt.travel_time",
  "action_leaves.lightning_bolt.in_flight",
  "action_leaves.lightning_bolt.in_flight_to_target",
  "action_leaves.lightning_bolt.in_flight_count",
  "action_leaves.lightning_bolt.in_flight_remains",
  "action_leaves.primordial_storm.ready",
  "action_leaves.stormstrike.ready",
  "action_leaves.stormstrike.hit_damage",
  "action_leaves.stormstrike.crit_pct_current",
  "action_leaves.stormstrike.multiplier",
  "action_leaves.stormstrike.persistent_multiplier",
  "action_leaves.sundering.ready",
  "action_leaves.sundering.hit_damage",
  "action_leaves.sundering.crit_pct_current",
  "action_leaves.sundering.multiplier",
  "action_leaves.sundering.persistent_multiplier",
  "action_leaves.sundering.active_enemies_within_8",
  "action_leaves.surging_totem.ready",
  "action_leaves.surging_totem.pet_surging_totem_active",
  "action_leaves.surging_totem.pet_surging_totem_remains",
  "action_leaves.tempest.ready",
  "action_leaves.tempest.hit_damage",
  "action_leaves.tempest.crit_pct_current",
  "action_leaves.tempest.multiplier",
  "action_leaves.tempest.persistent_multiplier",
  "action_leaves.tempest.travel_time",
  "action_leaves.tempest.in_flight",
  "action_leaves.tempest.in_flight_to_target",
  "action_leaves.tempest.in_flight_count",
  "action_leaves.tempest.in_flight_remains",
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
  "deck.dre_draws_left.value",
  "deck.dre_fail_left.value",
  "deck.dre_proc_left.value",
  "deck.imbuement_mastery_accumulated.value",
  "deck.lively_totems_accumulated.value",
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
  "stats.crit.value",
  "stats.damage_versatility.value",
  "stats.haste.value",
  "stats.mastery_value.value",
  "swing_cast.auto_attack_interval.value",
  "swing_cast.casting_remains.value",
  "swing_cast.gcd_length.value",
  "swing_cast.gcd_remains.value",
  "swing_cast.swing_mh_remains.value",
  "swing_cast.swing_oh_remains.value",
  "position.current_distance.value",
  "position.distance_to_move.value",
  "position.movement_speed.value",
  "position.x.value",
  "position.y.value",
  "pets.all_wolves.n_active_pets",
  "pets.all_wolves.remains",
  "pets.capacitor_totem.active",
  "pets.capacitor_totem.remains",
  "pets.earth_elemental.n_active_pets",
  "pets.earth_elemental.remains",
  "pets.fire_wolves.n_active_pets",
  "pets.fire_wolves.remains",
  "pets.lightning_wolves.n_active_pets",
  "pets.lightning_wolves.remains",
  "pets.searing_totem.active",
  "pets.searing_totem.remains",
  "pets.searing_totem.pulse_event_remains",
  "pets.surging_totem.active",
  "pets.surging_totem.remains",
  "pets.surging_totem.pulse_event_remains",
  "items.midnight_season_2_4pc.value",
  "items.trinket_has_use_buff.value",
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
  "sim_auras.skyfury.value",
  "fight_remains",
  "active_enemies",
  "maelstrom",
  "t",
  "raid_event_next_in",
};

// ---- Per-family observation tables ----

inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_0[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_1[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_2[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_3[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 14.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_4[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_5[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 40.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_6[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 6.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_7[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_8[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_9[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_10[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_11[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_12[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_13[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_14[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_15[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_16[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_17[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 3.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_18[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_19[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_20[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_21[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_22[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_23[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_24[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 3.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_25[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_26[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_27[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_28[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_29[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_30[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_31[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_32[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_33[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_34[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 14.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_35[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_36[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_37[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_38[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_39[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_40[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_41[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_42[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_43[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_44[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_45[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 24.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_46[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_47[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_48[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 15.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_49[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_50[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_51[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_52[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_53[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 24.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_54[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 24.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_55[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 24.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_PLAYER_BUFFS_MEMBERS[] = {
  { "amplification_core", "amplification_core", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_0, 2 },
  { "arc_discharge", "arc_discharge", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_1, 2 },
  { "arcanoweave_insight", "arcanoweave_insight", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_2, 2 },
  { "ascendance", "ascendance", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_3, 2 },
  { "berserking", "berserking", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_4, 2 },
  { "bloodlust", "bloodlust", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_5, 2 },
  { "converging_storms", "converging_storms", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_6, 2 },
  { "crackling_surge", "crackling_surge", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_7, 2 },
  { "crash_lightning", "crash_lightning", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_8, 2 },
  { "critical_ritual", "critical_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_9, 2 },
  { "damage_done", "damage_done", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_10, 2 },
  { "devoured_strength", "devoured_strength", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_11, 2 },
  { "doom_winds", "doom_winds", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_12, 2 },
  { "elemental_overflow", "elemental_overflow", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_13, 2 },
  { "exhaustion", "exhaustion", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_14, 1 },
  { "flametongue_weapon", "flametongue_weapon", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_15, 1 },
  { "flask", "flask", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_16, 1 },
  { "flurry", "flurry", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_17, 2 },
  { "food", "food", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_18, 1 },
  { "frenzied_focus", "frenzied_focus", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_19, 2 },
  { "genius_insight", "genius_insight", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_20, 2 },
  { "ghost_wolf", "ghost_wolf", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_21, 1 },
  { "hasty_ritual", "hasty_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_22, 2 },
  { "hot_hand", "hot_hand", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_23, 2 },
  { "lightning_strikes", "lightning_strikes", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_24, 2 },
  { "lively_totems", "lively_totems", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_25, 2 },
  { "maelstrom_weapon", "maelstrom_weapon", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_26, 2 },
  { "masterful_ritual", "masterful_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_27, 2 },
  { "mid2_enh_4pc", "mid2_enh_4pc", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_28, 2 },
  { "molten_weapon", "molten_weapon", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_29, 2 },
  { "movement", "movement", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_30, 1 },
  { "natures_swiftness", "natures_swiftness", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_31, 2 },
  { "potion", "potion", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_32, 2 },
  { "power_infusion", "power_infusion", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_33, 1 },
  { "primordial_storm", "primordial_storm", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_34, 2 },
  { "rooted", "rooted", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_35, 1 },
  { "rune_of_burning_haste", "rune_of_burning_haste", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_36, 2 },
  { "spirit_walk", "spirit_walk", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_37, 2 },
  { "static_accumulation", "static_accumulation", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_38, 1 },
  { "storm_swell", "storm_swell", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_39, 2 },
  { "storm_unleashed", "storm_unleashed", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_40, 2 },
  { "stormblast", "stormblast", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_41, 2 },
  { "stormsurge", "stormsurge", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_42, 2 },
  { "stunned", "stunned", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_43, 1 },
  { "surging_elements", "surging_elements", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_44, 2 },
  { "surging_totem", "surging_totem", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_45, 2 },
  { "tempest", "tempest", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_46, 2 },
  { "totemic_rebound", "totemic_rebound", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_47, 2 },
  { "unlimited_power", "unlimited_power", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_48, 2 },
  { "venomcursed_ascendance", "venomcursed_ascendance", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_49, 2 },
  { "venomcursed_mastery", "venomcursed_mastery", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_50, 2 },
  { "versatile_ritual", "versatile_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_51, 2 },
  { "voidtouched", "voidtouched", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_52, 1 },
  { "whirling_air", "whirling_air", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_53, 2 },
  { "whirling_earth", "whirling_earth", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_54, 2 },
  { "whirling_fire", "whirling_fire", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_55, 2 },
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
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_4[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_5[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_6[] = {
    { "charges_fractional", rl_kind::k_float, 2.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "charges", rl_kind::k_int, 2.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "recharge_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "full_recharge_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_7[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_8[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_9[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_10[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 90.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_COOLDOWNS_MEMBERS[] = {
  { "ascendance", "ascendance", RL_OBS_FAMILY_COOLDOWNS_LEAVES_0, 1 },
  { "berserking", "berserking", RL_OBS_FAMILY_COOLDOWNS_LEAVES_1, 1 },
  { "crash_lightning", "crash_lightning", RL_OBS_FAMILY_COOLDOWNS_LEAVES_2, 1 },
  { "doom_winds", "doom_winds", RL_OBS_FAMILY_COOLDOWNS_LEAVES_3, 1 },
  { "item_cd_1141", "item_cd_1141", RL_OBS_FAMILY_COOLDOWNS_LEAVES_4, 1 },
  { "lava_lash", "lava_lash", RL_OBS_FAMILY_COOLDOWNS_LEAVES_5, 1 },
  { "strike", "strike", RL_OBS_FAMILY_COOLDOWNS_LEAVES_6, 4 },
  { "sundering", "sundering", RL_OBS_FAMILY_COOLDOWNS_LEAVES_7, 1 },
  { "surging_totem", "surging_totem", RL_OBS_FAMILY_COOLDOWNS_LEAVES_8, 1 },
  { "voltaic_blaze", "voltaic_blaze", RL_OBS_FAMILY_COOLDOWNS_LEAVES_9, 1 },
  { "voracious_heart_of_ulatek_1297761", "voracious_heart_of_ulatek_1297761", RL_OBS_FAMILY_COOLDOWNS_LEAVES_10, 1 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_0[] = {
    { "present", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 40.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "role", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_1[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "tick_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 3.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_2[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_3[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 18.0, nullptr, 0, false },
    { "tick_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 3.0, nullptr, 0, false },
    { "tick_dmg", rl_kind::k_float, 0.0, true, 100000.0, false, 1.0, nullptr, 0, false },
    { "pmultiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_4[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_5[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_6[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_7[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_8[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_9[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_10[] = {
    { "present", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 40.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "role", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_11[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "tick_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 3.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_12[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_13[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 18.0, nullptr, 0, false },
    { "tick_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 3.0, nullptr, 0, false },
    { "tick_dmg", rl_kind::k_float, 0.0, true, 100000.0, false, 1.0, nullptr, 0, false },
    { "pmultiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_14[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_15[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_16[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_17[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_18[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_19[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_20[] = {
    { "present", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 40.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "role", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_21[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "tick_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 3.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_22[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_23[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 18.0, nullptr, 0, false },
    { "tick_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 3.0, nullptr, 0, false },
    { "tick_dmg", rl_kind::k_float, 0.0, true, 100000.0, false, 1.0, nullptr, 0, false },
    { "pmultiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_24[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_25[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_26[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_27[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_28[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_29[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_30[] = {
    { "present", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 40.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "role", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_31[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "tick_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 3.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_32[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_33[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 18.0, nullptr, 0, false },
    { "tick_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 3.0, nullptr, 0, false },
    { "tick_dmg", rl_kind::k_float, 0.0, true, 100000.0, false, 1.0, nullptr, 0, false },
    { "pmultiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_34[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_35[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_36[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_37[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_38[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_39[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_40[] = {
    { "present", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 40.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "role", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_41[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "tick_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 3.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_42[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_43[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 18.0, nullptr, 0, false },
    { "tick_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 3.0, nullptr, 0, false },
    { "tick_dmg", rl_kind::k_float, 0.0, true, 100000.0, false, 1.0, nullptr, 0, false },
    { "pmultiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_44[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_45[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_46[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_47[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_48[] = {
    { "ticking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_49[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_ENEMY_SLOTS_MEMBERS[] = {
  { "slot0", "slot0", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_0, 5 },
  { "slot0.burning_core", "slot0.burning_core", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_1, 3 },
  { "slot0.casting", "slot0.casting", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_2, 1 },
  { "slot0.flame_shock", "slot0.flame_shock", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_3, 5 },
  { "slot0.flametongue_attack", "slot0.flametongue_attack", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_4, 2 },
  { "slot0.lashing_flames", "slot0.lashing_flames", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_5, 2 },
  { "slot0.lightning_rod", "slot0.lightning_rod", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_6, 2 },
  { "slot0.rune_of_unleashed_fire_lingering", "slot0.rune_of_unleashed_fire_lingering", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_7, 2 },
  { "slot0.venomfang", "slot0.venomfang", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_8, 2 },
  { "slot0.venomfang_debuff", "slot0.venomfang_debuff", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_9, 2 },
  { "slot1", "slot1", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_10, 5 },
  { "slot1.burning_core", "slot1.burning_core", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_11, 3 },
  { "slot1.casting", "slot1.casting", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_12, 1 },
  { "slot1.flame_shock", "slot1.flame_shock", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_13, 5 },
  { "slot1.flametongue_attack", "slot1.flametongue_attack", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_14, 2 },
  { "slot1.lashing_flames", "slot1.lashing_flames", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_15, 2 },
  { "slot1.lightning_rod", "slot1.lightning_rod", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_16, 2 },
  { "slot1.rune_of_unleashed_fire_lingering", "slot1.rune_of_unleashed_fire_lingering", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_17, 2 },
  { "slot1.venomfang", "slot1.venomfang", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_18, 2 },
  { "slot1.venomfang_debuff", "slot1.venomfang_debuff", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_19, 2 },
  { "slot2", "slot2", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_20, 5 },
  { "slot2.burning_core", "slot2.burning_core", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_21, 3 },
  { "slot2.casting", "slot2.casting", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_22, 1 },
  { "slot2.flame_shock", "slot2.flame_shock", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_23, 5 },
  { "slot2.flametongue_attack", "slot2.flametongue_attack", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_24, 2 },
  { "slot2.lashing_flames", "slot2.lashing_flames", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_25, 2 },
  { "slot2.lightning_rod", "slot2.lightning_rod", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_26, 2 },
  { "slot2.rune_of_unleashed_fire_lingering", "slot2.rune_of_unleashed_fire_lingering", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_27, 2 },
  { "slot2.venomfang", "slot2.venomfang", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_28, 2 },
  { "slot2.venomfang_debuff", "slot2.venomfang_debuff", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_29, 2 },
  { "slot3", "slot3", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_30, 5 },
  { "slot3.burning_core", "slot3.burning_core", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_31, 3 },
  { "slot3.casting", "slot3.casting", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_32, 1 },
  { "slot3.flame_shock", "slot3.flame_shock", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_33, 5 },
  { "slot3.flametongue_attack", "slot3.flametongue_attack", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_34, 2 },
  { "slot3.lashing_flames", "slot3.lashing_flames", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_35, 2 },
  { "slot3.lightning_rod", "slot3.lightning_rod", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_36, 2 },
  { "slot3.rune_of_unleashed_fire_lingering", "slot3.rune_of_unleashed_fire_lingering", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_37, 2 },
  { "slot3.venomfang", "slot3.venomfang", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_38, 2 },
  { "slot3.venomfang_debuff", "slot3.venomfang_debuff", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_39, 2 },
  { "slot4", "slot4", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_40, 5 },
  { "slot4.burning_core", "slot4.burning_core", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_41, 3 },
  { "slot4.casting", "slot4.casting", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_42, 1 },
  { "slot4.flame_shock", "slot4.flame_shock", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_43, 5 },
  { "slot4.flametongue_attack", "slot4.flametongue_attack", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_44, 2 },
  { "slot4.lashing_flames", "slot4.lashing_flames", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_45, 2 },
  { "slot4.lightning_rod", "slot4.lightning_rod", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_46, 2 },
  { "slot4.rune_of_unleashed_fire_lingering", "slot4.rune_of_unleashed_fire_lingering", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_47, 2 },
  { "slot4.venomfang", "slot4.venomfang", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_48, 2 },
  { "slot4.venomfang_debuff", "slot4.venomfang_debuff", RL_OBS_FAMILY_ENEMY_SLOTS_LEAVES_49, 2 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_0[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_1[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_2[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "hit_damage", rl_kind::k_float, 0.0, true, 1000000.0, false, 1.0, nullptr, 0, false },
    { "crit_pct_current", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "persistent_multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "travel_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 2.0, nullptr, 0, false },
    { "in_flight", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_flight_to_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_flight_count", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "in_flight_remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 2.0, nullptr, 0, false },
    { "spell_targets", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_3[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "hit_damage", rl_kind::k_float, 0.0, true, 1000000.0, false, 1.0, nullptr, 0, false },
    { "crit_pct_current", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "persistent_multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "active_enemies_within_8", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
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
    { "multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "persistent_multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "travel_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 2.0, nullptr, 0, false },
    { "in_flight", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_flight_to_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
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
    { "multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "persistent_multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_9[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "hit_damage", rl_kind::k_float, 0.0, true, 1000000.0, false, 1.0, nullptr, 0, false },
    { "crit_pct_current", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "persistent_multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "active_enemies_within_8", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
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
    { "multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "persistent_multiplier", rl_kind::k_float, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "travel_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 2.0, nullptr, 0, false },
    { "in_flight", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_flight_to_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_flight_count", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "in_flight_remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 2.0, nullptr, 0, false },
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
  { "chain_lightning", "chain_lightning", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_2, 11 },
  { "crash_lightning", "crash_lightning", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_3, 6 },
  { "doom_winds", "doom_winds", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_4, 1 },
  { "lava_lash", "lava_lash", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_5, 7 },
  { "lightning_bolt", "lightning_bolt", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_6, 10 },
  { "primordial_storm", "primordial_storm", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_7, 1 },
  { "stormstrike", "stormstrike", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_8, 5 },
  { "sundering", "sundering", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_9, 6 },
  { "surging_totem", "surging_totem", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_10, 3 },
  { "tempest", "tempest", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_11, 10 },
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
    { "value", rl_kind::k_seconds, 0.0, false, 1.0, true, 1000.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_4[] = {
    { "value", rl_kind::k_seconds, 0.0, false, 1.0, true, 1000.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_5[] = {
    { "value", rl_kind::k_seconds, 0.0, false, 1.0, true, 1000.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_6[] = {
    { "value", rl_kind::k_int, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_7[] = {
    { "value", rl_kind::k_int, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_8[] = {
    { "value", rl_kind::k_int, 0.0, true, 250.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_9[] = {
    { "value", rl_kind::k_int, 0.0, true, 250.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_10[] = {
    { "value", rl_kind::k_int, 0.0, true, 250.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_11[] = {
    { "value", rl_kind::k_int, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_12[] = {
    { "value", rl_kind::k_int, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_13[] = {
    { "value", rl_kind::k_int, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_14[] = {
    { "value", rl_kind::k_int, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_15[] = {
    { "value", rl_kind::k_int, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_16[] = {
    { "value", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_17[] = {
    { "value", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_DECK_MEMBERS[] = {
  { "asc_dw_draws_left", "asc_dw_draws_left", RL_OBS_FAMILY_DECK_LEAVES_0, 1 },
  { "asc_dw_fail_left", "asc_dw_fail_left", RL_OBS_FAMILY_DECK_LEAVES_1, 1 },
  { "asc_dw_proc_left", "asc_dw_proc_left", RL_OBS_FAMILY_DECK_LEAVES_2, 1 },
  { "dre_draws_left", "dre_draws_left", RL_OBS_FAMILY_DECK_LEAVES_3, 1 },
  { "dre_fail_left", "dre_fail_left", RL_OBS_FAMILY_DECK_LEAVES_4, 1 },
  { "dre_proc_left", "dre_proc_left", RL_OBS_FAMILY_DECK_LEAVES_5, 1 },
  { "imbuement_mastery_accumulated", "imbuement_mastery_accumulated", RL_OBS_FAMILY_DECK_LEAVES_6, 1 },
  { "lively_totems_accumulated", "lively_totems_accumulated", RL_OBS_FAMILY_DECK_LEAVES_7, 1 },
  { "storm_unleashed_draws_left", "storm_unleashed_draws_left", RL_OBS_FAMILY_DECK_LEAVES_8, 1 },
  { "storm_unleashed_fail_left", "storm_unleashed_fail_left", RL_OBS_FAMILY_DECK_LEAVES_9, 1 },
  { "storm_unleashed_proc_left", "storm_unleashed_proc_left", RL_OBS_FAMILY_DECK_LEAVES_10, 1 },
  { "tempest_draws_left", "tempest_draws_left", RL_OBS_FAMILY_DECK_LEAVES_11, 1 },
  { "tempest_fail_left", "tempest_fail_left", RL_OBS_FAMILY_DECK_LEAVES_12, 1 },
  { "tempest_proc_left", "tempest_proc_left", RL_OBS_FAMILY_DECK_LEAVES_13, 1 },
  { "tempest_procs_this_deck", "tempest_procs_this_deck", RL_OBS_FAMILY_DECK_LEAVES_14, 1 },
  { "tempest_spends_since_proc", "tempest_spends_since_proc", RL_OBS_FAMILY_DECK_LEAVES_15, 1 },
  { "ti_chain_lightning", "ti_chain_lightning", RL_OBS_FAMILY_DECK_LEAVES_16, 1 },
  { "ti_lightning_bolt", "ti_lightning_bolt", RL_OBS_FAMILY_DECK_LEAVES_17, 1 },
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
inline constexpr rl_leaf_desc RL_OBS_FAMILY_STATS_LEAVES_4[] = {
    { "value", rl_kind::k_float, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_STATS_MEMBERS[] = {
  { "attack_power", "attack_power", RL_OBS_FAMILY_STATS_LEAVES_0, 1 },
  { "crit", "crit", RL_OBS_FAMILY_STATS_LEAVES_1, 1 },
  { "damage_versatility", "damage_versatility", RL_OBS_FAMILY_STATS_LEAVES_2, 1 },
  { "haste", "haste", RL_OBS_FAMILY_STATS_LEAVES_3, 1 },
  { "mastery_value", "mastery_value", RL_OBS_FAMILY_STATS_LEAVES_4, 1 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_SWING_CAST_LEAVES_0[] = {
    { "value", rl_kind::k_seconds, 0.0, false, 1.0, true, 4.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_SWING_CAST_LEAVES_1[] = {
    { "value", rl_kind::k_seconds, 0.0, false, 1.0, true, 4.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_SWING_CAST_LEAVES_2[] = {
    { "value", rl_kind::k_seconds, 0.0, false, 1.0, true, 1.5, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_SWING_CAST_LEAVES_3[] = {
    { "value", rl_kind::k_seconds, 0.0, false, 1.0, true, 1.5, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_SWING_CAST_LEAVES_4[] = {
    { "value", rl_kind::k_seconds, 0.0, false, 1.0, true, 2.1, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_SWING_CAST_LEAVES_5[] = {
    { "value", rl_kind::k_seconds, 0.0, false, 1.0, true, 2.1, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_SWING_CAST_MEMBERS[] = {
  { "auto_attack_interval", "auto_attack_interval", RL_OBS_FAMILY_SWING_CAST_LEAVES_0, 1 },
  { "casting_remains", "casting_remains", RL_OBS_FAMILY_SWING_CAST_LEAVES_1, 1 },
  { "gcd_length", "gcd_length", RL_OBS_FAMILY_SWING_CAST_LEAVES_2, 1 },
  { "gcd_remains", "gcd_remains", RL_OBS_FAMILY_SWING_CAST_LEAVES_3, 1 },
  { "swing_mh_remains", "swing_mh_remains", RL_OBS_FAMILY_SWING_CAST_LEAVES_4, 1 },
  { "swing_oh_remains", "swing_oh_remains", RL_OBS_FAMILY_SWING_CAST_LEAVES_5, 1 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_POSITION_LEAVES_0[] = {
    { "value", rl_kind::k_seconds, 0.0, false, 1.0, true, 40.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_POSITION_LEAVES_1[] = {
    { "value", rl_kind::k_seconds, 0.0, false, 1.0, true, 40.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_POSITION_LEAVES_2[] = {
    { "value", rl_kind::k_float, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_POSITION_LEAVES_3[] = {
    { "value", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_POSITION_LEAVES_4[] = {
    { "value", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_POSITION_MEMBERS[] = {
  { "current_distance", "current_distance", RL_OBS_FAMILY_POSITION_LEAVES_0, 1 },
  { "distance_to_move", "distance_to_move", RL_OBS_FAMILY_POSITION_LEAVES_1, 1 },
  { "movement_speed", "movement_speed", RL_OBS_FAMILY_POSITION_LEAVES_2, 1 },
  { "x", "x", RL_OBS_FAMILY_POSITION_LEAVES_3, 1 },
  { "y", "y", RL_OBS_FAMILY_POSITION_LEAVES_4, 1 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_PETS_LEAVES_0[] = {
    { "n_active_pets", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PETS_LEAVES_1[] = {
    { "active", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PETS_LEAVES_2[] = {
    { "n_active_pets", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PETS_LEAVES_3[] = {
    { "n_active_pets", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PETS_LEAVES_4[] = {
    { "n_active_pets", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PETS_LEAVES_5[] = {
    { "active", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "pulse_event_remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 3.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PETS_LEAVES_6[] = {
    { "active", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 24.0, nullptr, 0, false },
    { "pulse_event_remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 3.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_PETS_MEMBERS[] = {
  { "all_wolves", "all_wolves", RL_OBS_FAMILY_PETS_LEAVES_0, 2 },
  { "capacitor_totem", "capacitor_totem", RL_OBS_FAMILY_PETS_LEAVES_1, 2 },
  { "earth_elemental", "earth_elemental", RL_OBS_FAMILY_PETS_LEAVES_2, 2 },
  { "fire_wolves", "fire_wolves", RL_OBS_FAMILY_PETS_LEAVES_3, 2 },
  { "lightning_wolves", "lightning_wolves", RL_OBS_FAMILY_PETS_LEAVES_4, 2 },
  { "searing_totem", "searing_totem", RL_OBS_FAMILY_PETS_LEAVES_5, 3 },
  { "surging_totem", "surging_totem", RL_OBS_FAMILY_PETS_LEAVES_6, 3 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_ITEMS_LEAVES_0[] = {
    { "value", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ITEMS_LEAVES_1[] = {
    { "value", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_ITEMS_MEMBERS[] = {
  { "midnight_season_2_4pc", "midnight_season_2_4pc", RL_OBS_FAMILY_ITEMS_LEAVES_0, 1 },
  { "trinket_has_use_buff", "trinket_has_use_buff", RL_OBS_FAMILY_ITEMS_LEAVES_1, 1 },
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

inline constexpr rl_leaf_desc RL_OBS_FAMILY_SIM_AURAS_LEAVES_0[] = {
    { "value", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_SIM_AURAS_MEMBERS[] = {
  { "skyfury", "skyfury", RL_OBS_FAMILY_SIM_AURAS_LEAVES_0, 1 },
};

inline constexpr double RL_BUCKETS_SLOT382[] = { 1.0, 2.0, 5.0 };
inline constexpr rl_leaf_desc RL_OBS_FAMILY_SCALARS_LEAVES_0[] = {
    { "fight_remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, true },
    { "active_enemies", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT382, 3, false },
    { "maelstrom", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "t", rl_kind::k_seconds, 0.0, false, 1.0, true, 300.0, nullptr, 0, false },
    { "raid_event_next_in", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_SCALARS_MEMBERS[] = {
  { "", "", RL_OBS_FAMILY_SCALARS_LEAVES_0, 5 },
};

inline constexpr std::size_t RL_OBS_FAMILY_COUNT = 13;

inline constexpr rl_obs_family RL_OBS_FAMILIES[RL_OBS_FAMILY_COUNT] = {
  { rl_family::player_buffs, "player_buffs", rl_family_kind::buff, false, RL_OBS_FAMILY_PLAYER_BUFFS_MEMBERS, 56, 0, 101 },
  { rl_family::cooldowns, "cooldowns", rl_family_kind::cooldown, false, RL_OBS_FAMILY_COOLDOWNS_MEMBERS, 11, 101, 14 },
  { rl_family::enemy_slots, "enemy_slots", rl_family_kind::enemy_slot, false, RL_OBS_FAMILY_ENEMY_SLOTS_MEMBERS, 50, 115, 130 },
  { rl_family::action_leaves, "action_leaves", rl_family_kind::action_expression, false, RL_OBS_FAMILY_ACTION_LEAVES_MEMBERS, 15, 245, 73 },
  { rl_family::deck, "deck", rl_family_kind::expression, false, RL_OBS_FAMILY_DECK_MEMBERS, 18, 318, 18 },
  { rl_family::stats, "stats", rl_family_kind::direct, false, RL_OBS_FAMILY_STATS_MEMBERS, 5, 336, 5 },
  { rl_family::swing_cast, "swing_cast", rl_family_kind::direct, false, RL_OBS_FAMILY_SWING_CAST_MEMBERS, 6, 341, 6 },
  { rl_family::position, "position", rl_family_kind::direct, false, RL_OBS_FAMILY_POSITION_MEMBERS, 5, 347, 5 },
  { rl_family::pets, "pets", rl_family_kind::expression, false, RL_OBS_FAMILY_PETS_MEMBERS, 7, 352, 16 },
  { rl_family::items, "items", rl_family_kind::expression, false, RL_OBS_FAMILY_ITEMS_MEMBERS, 2, 368, 2 },
  { rl_family::raid_events, "raid_events", rl_family_kind::expression, false, RL_OBS_FAMILY_RAID_EVENTS_MEMBERS, 2, 370, 10 },
  { rl_family::sim_auras, "sim_auras", rl_family_kind::expression, false, RL_OBS_FAMILY_SIM_AURAS_MEMBERS, 1, 380, 1 },
  { rl_family::scalars, "scalars", rl_family_kind::scalar, true, RL_OBS_FAMILY_SCALARS_MEMBERS, 1, 381, 5 },
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

