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

// ---- Scalar constants ----

inline constexpr const char* RL_REGISTRY_ID = "enhancement";
inline constexpr const char* RL_ACTOR_NAME = "RL_Enhancement_Shaman";
inline constexpr int RL_ENCODER_VERSION = 6;
inline constexpr std::size_t RL_OBS_DIM = 419;
inline constexpr std::size_t RL_ACTION_DIM = 22;
inline constexpr double RL_EPISODE_MAX_TIME = 300.0;
inline constexpr double RL_WAIT_FLOOR_SECONDS = 0.05;
inline constexpr double RL_PERMANENT_SATURATION = 1.0;
inline constexpr const char* RL_OBS_SCHEMA_SHA = "rl-obs-v6:ecfdf872816c860a39ba7f938a66cad3abaa352fbb7f79cfc5bf8595548bdfbd";
inline constexpr const char* RL_MASK_RULES_SHA = "3f930294d36b217dca01fc51600c0da9d0568e20152fb53c588b6e8ddccf7662";
inline constexpr const char* RL_ACTION_SPACE_SHA = "5e5f43d009948f4adbe65e86b1238e725596c593452d89a40e31c1b8e8e21684";

// ---- Observation name list (the materialised ordering) ----

inline constexpr const char* RL_OBS_NAMES[RL_OBS_DIM] = {
  "player_buffs.amplification_core.remains",
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
  "player_buffs.crackling_surge.next_expiry",
  "player_buffs.crackling_surge.expiry_2",
  "player_buffs.crackling_surge.expiry_3",
  "player_buffs.crackling_surge.stack_seconds",
  "player_buffs.crash_lightning.stacks",
  "player_buffs.crash_lightning.remains",
  "player_buffs.crash_lightning.next_expiry",
  "player_buffs.crash_lightning.expiry_2",
  "player_buffs.crash_lightning.expiry_3",
  "player_buffs.crash_lightning.stack_seconds",
  "player_buffs.critical_ritual.remains",
  "player_buffs.devoured_strength.stacks",
  "player_buffs.devoured_strength.remains",
  "player_buffs.doom_winds.remains",
  "player_buffs.elemental_overflow.remains",
  "player_buffs.exhaustion.remains",
  "player_buffs.flurry.stacks",
  "player_buffs.flurry.remains",
  "player_buffs.frenzied_focus.remains",
  "player_buffs.genius_insight.remains",
  "player_buffs.hasty_ritual.remains",
  "player_buffs.hot_hand.remains",
  "player_buffs.impending_execution.stacks",
  "player_buffs.impending_execution.remains",
  "player_buffs.lightning_strikes.stacks",
  "player_buffs.lightning_strikes.remains",
  "player_buffs.lively_totems.stacks",
  "player_buffs.lively_totems.remains",
  "player_buffs.lively_totems.next_expiry",
  "player_buffs.lively_totems.expiry_2",
  "player_buffs.lively_totems.expiry_3",
  "player_buffs.lively_totems.stack_seconds",
  "player_buffs.maelstrom_weapon.stacks",
  "player_buffs.maelstrom_weapon.remains",
  "player_buffs.masterful_ritual.remains",
  "player_buffs.molten_weapon.remains",
  "player_buffs.potion_of_recklessness_Crit.remains",
  "player_buffs.potion_of_recklessness_Haste.remains",
  "player_buffs.potion_of_recklessness_Mastery.remains",
  "player_buffs.potion_of_recklessness_Vers.remains",
  "player_buffs.primordial_storm.remains",
  "player_buffs.rune_of_burning_haste.stacks",
  "player_buffs.rune_of_burning_haste.remains",
  "player_buffs.short_circuit.stacks",
  "player_buffs.short_circuit.remains",
  "player_buffs.storm_swell.remains",
  "player_buffs.storm_unleashed.stacks",
  "player_buffs.storm_unleashed.remains",
  "player_buffs.stormblast.stacks",
  "player_buffs.stormblast.remains",
  "player_buffs.stormsurge.remains",
  "player_buffs.surging_elements.remains",
  "player_buffs.surging_totem.remains",
  "player_buffs.tempest.stacks",
  "player_buffs.tempest.remains",
  "player_buffs.totemic_rebound.stacks",
  "player_buffs.totemic_rebound.remains",
  "player_buffs.unlimited_power.stacks",
  "player_buffs.unlimited_power.remains",
  "player_buffs.venomcursed_ascendance.remains",
  "player_buffs.venomcursed_mastery.remains",
  "player_buffs.versatile_ritual.remains",
  "player_buffs.void_execution_mandate.stacks",
  "player_buffs.void_execution_mandate.remains",
  "player_buffs.whirling_air.remains",
  "player_buffs.whirling_earth.remains",
  "player_buffs.whirling_fire.remains",
  "cooldowns.ascendance.remains",
  "cooldowns.berserking.remains",
  "cooldowns.crash_lightning.remains",
  "cooldowns.doom_winds.remains",
  "cooldowns.flame_shock.remains",
  "cooldowns.lava_lash.remains",
  "cooldowns.potion.remains",
  "cooldowns.strike.charges_fractional",
  "cooldowns.strike.charges",
  "cooldowns.strike.recharge_time",
  "cooldowns.strike.full_recharge_time",
  "cooldowns.sundering.remains",
  "cooldowns.surging_totem.remains",
  "cooldowns.void_execution_mandate_1250557.remains",
  "cooldowns.voltaic_blaze.remains",
  "cooldowns.voracious_heart_of_ulatek_1297761.remains",
  "target_facts.chain_lightning.found",
  "target_facts.chain_lightning.time_to_die",
  "target_facts.chain_lightning.distance",
  "target_facts.chain_lightning.in_reach",
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
  "target_facts.chain_lightning.is_current_target",
  "target_facts.lava_lash.found",
  "target_facts.lava_lash.time_to_die",
  "target_facts.lava_lash.distance",
  "target_facts.lava_lash.in_reach",
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
  "target_facts.lava_lash.is_current_target",
  "target_facts.lightning_bolt.found",
  "target_facts.lightning_bolt.time_to_die",
  "target_facts.lightning_bolt.distance",
  "target_facts.lightning_bolt.in_reach",
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
  "target_facts.lightning_bolt.is_current_target",
  "target_facts.stormstrike.found",
  "target_facts.stormstrike.time_to_die",
  "target_facts.stormstrike.distance",
  "target_facts.stormstrike.in_reach",
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
  "target_facts.stormstrike.is_current_target",
  "target_facts.tempest.found",
  "target_facts.tempest.time_to_die",
  "target_facts.tempest.distance",
  "target_facts.tempest.in_reach",
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
  "target_facts.tempest.is_current_target",
  "target_facts.voltaic_blaze.found",
  "target_facts.voltaic_blaze.time_to_die",
  "target_facts.voltaic_blaze.distance",
  "target_facts.voltaic_blaze.in_reach",
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
  "target_facts.voltaic_blaze.is_current_target",
  "target_facts.windstrike.found",
  "target_facts.windstrike.time_to_die",
  "target_facts.windstrike.distance",
  "target_facts.windstrike.in_reach",
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
  "target_facts.windstrike.is_current_target",
  "shapes.crash_lightning.enemies_hit",
  "shapes.crash_lightning.summed_remaining_life",
  "shapes.crash_lightning.long_lived_count",
  "shapes.sundering.enemies_hit",
  "shapes.sundering.summed_remaining_life",
  "shapes.sundering.long_lived_count",
  "action_leaves.ascendance.ready",
  "action_leaves.berserking.ready",
  "action_leaves.crash_lightning.ready",
  "action_leaves.doom_winds.ready",
  "action_leaves.flame_shock.ready",
  "action_leaves.lava_lash.ready",
  "action_leaves.lightning_bolt.ready",
  "action_leaves.lightning_bolt.in_flight",
  "action_leaves.lightning_bolt.in_flight_remains",
  "action_leaves.primordial_storm.ready",
  "action_leaves.stormstrike.ready",
  "action_leaves.sundering.ready",
  "action_leaves.surging_totem.ready",
  "action_leaves.tempest.ready",
  "action_leaves.use_item_voracious_heart_of_ulatek.ready",
  "action_leaves.voltaic_blaze.ready",
  "action_leaves.windstrike.ready",
  "deck.asc_dw_draws_left.value",
  "deck.asc_dw_fail_left.value",
  "deck.asc_dw_proc_left.value",
  "deck.dre_draws_left.value",
  "deck.dre_fail_left.value",
  "deck.dre_proc_left.value",
  "deck.storm_unleashed_draws_left.value",
  "deck.storm_unleashed_fail_left.value",
  "deck.storm_unleashed_proc_left.value",
  "deck.tempest_draws_left.value",
  "deck.tempest_fail_left.value",
  "deck.tempest_proc_left.value",
  "deck.tempest_procs_this_deck.value",
  "deck.tempest_spends_since_proc.value",
  "deck.ti_chain_lightning.value",
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
  "pets.searing_totem.active",
  "pets.surging_totem.pulse_event_remains",
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
  "proc_chances.stormsurge.value",
  "proc_chances.windfury.value",
  "fight_remains",
  "active_enemies",
  "raid_event_next_in",
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
  "lashing_flames_carrier_count",
  "lightning_rod_carrier_count",
  "longest_time_to_die",
  "nearest_enemy_distance",
  "soonest_time_to_die",
  "hits.chain_lightning",
  "hits.tempest",
  "hits.lava_lash.flame_shock_spread",
  "hits.voltaic_blaze.cleave",
  "hits.voltaic_blaze.new_flame_shocks",
  "hits.fire_nova",
  "hits.chain_lightning.at_least_2",
  "hits.chain_lightning.at_least_3",
  "hits.chain_lightning.at_least_4",
  "hits.chain_lightning.at_least_5",
  "hits.tempest.at_least_2",
  "hits.tempest.at_least_3",
  "hits.tempest.at_least_4",
  "hits.tempest.at_least_5",
  "hits.voltaic_blaze.cleave.at_least_2",
  "hits.voltaic_blaze.cleave.at_least_3",
  "hits.voltaic_blaze.cleave.at_least_4",
  "hits.voltaic_blaze.cleave.at_least_5",
  "hits.crash_lightning.at_least_1",
  "hits.crash_lightning.at_least_2",
  "hits.crash_lightning.at_least_3",
  "hits.crash_lightning.at_least_4",
  "hits.crash_lightning.at_least_5",
  "hits.voltaic_blaze.new_flame_shocks.at_least_1",
  "hits.voltaic_blaze.new_flame_shocks.at_least_2",
  "hits.voltaic_blaze.new_flame_shocks.at_least_3",
  "hits.voltaic_blaze.new_flame_shocks.at_least_4",
  "hits.voltaic_blaze.new_flame_shocks.at_least_5",
  "hits.fire_nova.at_least_1",
  "hits.fire_nova.at_least_2",
  "hits.fire_nova.at_least_3",
  "hits.fire_nova.at_least_4",
  "hits.fire_nova.at_least_5",
  "hits.lava_lash.flame_shock_spread.at_least_1",
  "hits.lava_lash.flame_shock_spread.at_least_2",
  "hits.lava_lash.flame_shock_spread.at_least_3",
  "hits.lava_lash.flame_shock_spread.at_least_4",
  "hits.lava_lash.flame_shock_spread.at_least_5",
  "fight.next_wave.in",
  "fight.next_wave.count",
  "fight.next_wave.lifetime",
  "fight.next_phase.at_boss_pct",
  "fight.next_phase.boss_pct_now",
  "fight.downtime.in",
  "fight.bloodlust.in",
  "capability.racial_berserking",
  "capability.trinket_voracious_heart_of_ulatek",
  "capability.consumable_potion_of_recklessness",
  "capability.enchant_arcane_mastery",
  "capability.enchant_berserkers_rage",
  "capability.embellishment_arcanoweave_lining",
  "capability.embellishment_hunters_ritual_stone",
  "capability.item_venomcursed_mastery",
  "capability.item_venomcursed_ascendance",
  "capability.weapon_venomfang",
  "capability.set_bite_of_zuljan_2pc",
  "capability.tier_mid2_enh_2pc",
  "capability.tier_mid2_enh_4pc",
  "capability.omnium_rune_burning_haste",
  "capability.omnium_core_rune_unleashed_fire",
  "capability.omnium_rune_lingering",
  "capability.trinket_void_execution_mandate",
  "capability.mode_funnel",
  "capability.talent_unruly_winds",
  "capability.talent_overflowing_maelstrom",
  "capability.talent_ashen_catalyst",
  "capability.talent_stormblast",
  "capability.talent_overcharge",
  "capability.talent_raging_maelstrom",
  "capability.talent_flurry",
  "capability.talent_hot_hand",
  "capability.talent_hot_hand_r2",
  "capability.talent_storms_wrath",
  "capability.talent_elemental_tempo",
  "capability.talent_voltaic_blaze",
  "capability.talent_chaining_storms",
  "capability.talent_converging_storms",
  "capability.talent_stormflurry",
  "capability.talent_elemental_weapons",
  "capability.talent_fire_nova",
  "capability.talent_lashing_flames",
  "capability.talent_ride_the_lightning",
  "capability.talent_doom_winds",
  "capability.talent_sundering",
  "capability.talent_lightning_strikes",
  "capability.talent_elemental_assault",
  "capability.talent_static_accumulation",
  "capability.talent_static_accumulation_r2",
  "capability.talent_feral_spirit",
  "capability.talent_surging_elements",
  "capability.talent_thunder_capacitor",
  "capability.talent_deeply_rooted_elements",
  "capability.talent_ascendance",
  "capability.talent_thorims_invocation",
  "capability.talent_primordial_storm",
  "capability.talent_storm_unleashed",
  "capability.talent_storm_unleashed_r2",
  "capability.talent_storm_unleashed_r3",
  "capability.talent_storm_unleashed_r4",
  "capability.talent_storm_swell",
  "capability.talent_supercharge",
  "capability.talent_amplification_core",
  "capability.talent_oversurge",
  "capability.talent_pulse_capacitor",
  "capability.talent_supportive_imbuements",
  "capability.talent_totemic_coordination",
  "capability.talent_earthsurge",
  "capability.hero_stormbringer",
  "capability.hero_totemic",
  "capability.tempest_venomfang",
  "capability.tempest_burning_core",
  "capability.tempest_rune_lingering",
  "capability.voltaic_blaze_venomfang",
  "capability.voltaic_blaze_burning_core",
  "capability.voltaic_blaze_rune_lingering",
  "capability.voltaic_blaze_stormbringer",
};

// ---- Per-family observation tables ----

inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_0[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 24.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_1[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_2[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_3[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 14.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_4[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_5[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 40.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_6[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 6.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_7[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "next_expiry", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "expiry_2", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "expiry_3", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "stack_seconds", rl_kind::k_seconds, 0.0, false, 1.0, true, 40.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_8[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 6.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "next_expiry", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "expiry_2", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "expiry_3", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "stack_seconds", rl_kind::k_seconds, 0.0, false, 1.0, true, 40.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_9[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_10[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 25.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_11[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_12[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_13[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 600.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_14[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 3.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_15[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_16[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_17[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_18[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_19[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 15.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_20[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 3.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_21[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "next_expiry", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "expiry_2", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "expiry_3", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "stack_seconds", rl_kind::k_seconds, 0.0, false, 1.0, true, 80.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_22[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_23[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_24[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_25[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_26[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_27[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_28[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_29[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_30[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_31[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_32[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_33[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_34[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_35[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_36[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_37[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 24.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_38[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_39[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_40[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 15.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_41[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_42[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_43[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_44[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_45[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 24.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_46[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 24.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_47[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 24.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_PLAYER_BUFFS_MEMBERS[] = {
  { "amplification_core", "amplification_core", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_0, 1 },
  { "arc_discharge", "arc_discharge", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_1, 2 },
  { "arcanoweave_insight", "arcanoweave_insight", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_2, 1 },
  { "ascendance", "ascendance", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_3, 1 },
  { "berserking", "berserking", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_4, 1 },
  { "bloodlust", "bloodlust", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_5, 1 },
  { "converging_storms", "converging_storms", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_6, 2 },
  { "crackling_surge", "crackling_surge", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_7, 6 },
  { "crash_lightning", "crash_lightning", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_8, 6 },
  { "critical_ritual", "critical_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_9, 1 },
  { "devoured_strength", "devoured_strength", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_10, 2 },
  { "doom_winds", "doom_winds", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_11, 1 },
  { "elemental_overflow", "elemental_overflow", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_12, 1 },
  { "exhaustion", "exhaustion", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_13, 1 },
  { "flurry", "flurry", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_14, 2 },
  { "frenzied_focus", "frenzied_focus", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_15, 1 },
  { "genius_insight", "genius_insight", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_16, 1 },
  { "hasty_ritual", "hasty_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_17, 1 },
  { "hot_hand", "hot_hand", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_18, 1 },
  { "impending_execution", "impending_execution", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_19, 2 },
  { "lightning_strikes", "lightning_strikes", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_20, 2 },
  { "lively_totems", "lively_totems", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_21, 6 },
  { "maelstrom_weapon", "maelstrom_weapon", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_22, 2 },
  { "masterful_ritual", "masterful_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_23, 1 },
  { "molten_weapon", "molten_weapon", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_24, 1 },
  { "potion_of_recklessness_Crit", "potion_of_recklessness_Crit", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_25, 1 },
  { "potion_of_recklessness_Haste", "potion_of_recklessness_Haste", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_26, 1 },
  { "potion_of_recklessness_Mastery", "potion_of_recklessness_Mastery", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_27, 1 },
  { "potion_of_recklessness_Vers", "potion_of_recklessness_Vers", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_28, 1 },
  { "primordial_storm", "primordial_storm", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_29, 1 },
  { "rune_of_burning_haste", "rune_of_burning_haste", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_30, 2 },
  { "short_circuit", "short_circuit", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_31, 2 },
  { "storm_swell", "storm_swell", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_32, 1 },
  { "storm_unleashed", "storm_unleashed", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_33, 2 },
  { "stormblast", "stormblast", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_34, 2 },
  { "stormsurge", "stormsurge", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_35, 1 },
  { "surging_elements", "surging_elements", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_36, 1 },
  { "surging_totem", "surging_totem", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_37, 1 },
  { "tempest", "tempest", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_38, 2 },
  { "totemic_rebound", "totemic_rebound", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_39, 2 },
  { "unlimited_power", "unlimited_power", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_40, 2 },
  { "venomcursed_ascendance", "venomcursed_ascendance", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_41, 1 },
  { "venomcursed_mastery", "venomcursed_mastery", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_42, 1 },
  { "versatile_ritual", "versatile_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_43, 1 },
  { "void_execution_mandate", "void_execution_mandate", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_44, 2 },
  { "whirling_air", "whirling_air", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_45, 1 },
  { "whirling_earth", "whirling_earth", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_46, 1 },
  { "whirling_fire", "whirling_fire", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_47, 1 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_0[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 180.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_1[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 180.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_2[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_3[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_4[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_5[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_6[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 300.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_7[] = {
    { "charges_fractional", rl_kind::k_float, 2.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "charges", rl_kind::k_int, 2.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "recharge_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "full_recharge_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_8[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_9[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_10[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 120.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_11[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_12[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 90.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_COOLDOWNS_MEMBERS[] = {
  { "ascendance", "ascendance", RL_OBS_FAMILY_COOLDOWNS_LEAVES_0, 1 },
  { "berserking", "berserking", RL_OBS_FAMILY_COOLDOWNS_LEAVES_1, 1 },
  { "crash_lightning", "crash_lightning", RL_OBS_FAMILY_COOLDOWNS_LEAVES_2, 1 },
  { "doom_winds", "doom_winds", RL_OBS_FAMILY_COOLDOWNS_LEAVES_3, 1 },
  { "flame_shock", "flame_shock", RL_OBS_FAMILY_COOLDOWNS_LEAVES_4, 1 },
  { "lava_lash", "lava_lash", RL_OBS_FAMILY_COOLDOWNS_LEAVES_5, 1 },
  { "potion", "potion", RL_OBS_FAMILY_COOLDOWNS_LEAVES_6, 1 },
  { "strike", "strike", RL_OBS_FAMILY_COOLDOWNS_LEAVES_7, 4 },
  { "sundering", "sundering", RL_OBS_FAMILY_COOLDOWNS_LEAVES_8, 1 },
  { "surging_totem", "surging_totem", RL_OBS_FAMILY_COOLDOWNS_LEAVES_9, 1 },
  { "void_execution_mandate_1250557", "void_execution_mandate_1250557", RL_OBS_FAMILY_COOLDOWNS_LEAVES_10, 1 },
  { "voltaic_blaze", "voltaic_blaze", RL_OBS_FAMILY_COOLDOWNS_LEAVES_11, 1 },
  { "voracious_heart_of_ulatek_1297761", "voracious_heart_of_ulatek_1297761", RL_OBS_FAMILY_COOLDOWNS_LEAVES_12, 1 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_0[] = {
    { "found", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "flame_shock_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "burning_core_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lightning_rod_stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "lightning_rod_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "venomfang_debuff_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_1[] = {
    { "found", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "flame_shock_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "burning_core_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lightning_rod_stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "lightning_rod_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "venomfang_debuff_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_2[] = {
    { "found", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "flame_shock_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "burning_core_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lightning_rod_stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "lightning_rod_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "venomfang_debuff_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_3[] = {
    { "found", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "flame_shock_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "burning_core_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lightning_rod_stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "lightning_rod_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "venomfang_debuff_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_4[] = {
    { "found", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "flame_shock_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "burning_core_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lightning_rod_stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "lightning_rod_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "venomfang_debuff_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_5[] = {
    { "found", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "flame_shock_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "burning_core_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lightning_rod_stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "lightning_rod_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "venomfang_debuff_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_6[] = {
    { "found", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "flame_shock_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "burning_core_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lightning_rod_stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "lightning_rod_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "venomfang_debuff_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_TARGET_FACTS_MEMBERS[] = {
  { "chain_lightning", "chain_lightning", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_0, 15 },
  { "lava_lash", "lava_lash", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_1, 15 },
  { "lightning_bolt", "lightning_bolt", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_2, 15 },
  { "stormstrike", "stormstrike", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_3, 15 },
  { "tempest", "tempest", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_4, 15 },
  { "voltaic_blaze", "voltaic_blaze", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_5, 15 },
  { "windstrike", "windstrike", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_6, 15 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_SHAPES_LEAVES_0[] = {
    { "enemies_hit", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "summed_remaining_life", rl_kind::k_seconds, 0.0, false, 1.0, true, 360.0, nullptr, 0, false },
    { "long_lived_count", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_SHAPES_LEAVES_1[] = {
    { "enemies_hit", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "summed_remaining_life", rl_kind::k_seconds, 0.0, false, 1.0, true, 360.0, nullptr, 0, false },
    { "long_lived_count", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
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
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_3[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_4[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_5[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_6[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_flight", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_flight_remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 0.5, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_7[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_8[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_9[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_10[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_11[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_12[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_13[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_14[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_ACTION_LEAVES_MEMBERS[] = {
  { "ascendance", "ascendance", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_0, 1 },
  { "berserking", "berserking", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_1, 1 },
  { "crash_lightning", "crash_lightning", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_2, 1 },
  { "doom_winds", "doom_winds", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_3, 1 },
  { "flame_shock", "flame_shock", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_4, 1 },
  { "lava_lash", "lava_lash", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_5, 1 },
  { "lightning_bolt", "lightning_bolt", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_6, 3 },
  { "primordial_storm", "primordial_storm", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_7, 1 },
  { "stormstrike", "stormstrike", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_8, 1 },
  { "sundering", "sundering", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_9, 1 },
  { "surging_totem", "surging_totem", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_10, 1 },
  { "tempest", "tempest", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_11, 1 },
  { "use_item_voracious_heart_of_ulatek", "use_item_voracious_heart_of_ulatek", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_12, 1 },
  { "voltaic_blaze", "voltaic_blaze", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_13, 1 },
  { "windstrike", "windstrike", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_14, 1 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_0[] = {
    { "value", rl_kind::k_int, 0.0, true, 600.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_1[] = {
    { "value", rl_kind::k_int, 0.0, true, 600.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_2[] = {
    { "value", rl_kind::k_int, 0.0, true, 3.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_3[] = {
    { "value", rl_kind::k_int, 0.0, true, 333.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_4[] = {
    { "value", rl_kind::k_int, 0.0, true, 333.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_5[] = {
    { "value", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_6[] = {
    { "value", rl_kind::k_int, 0.0, true, 250.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_7[] = {
    { "value", rl_kind::k_int, 0.0, true, 250.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_8[] = {
    { "value", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_9[] = {
    { "value", rl_kind::k_int, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_10[] = {
    { "value", rl_kind::k_int, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_11[] = {
    { "value", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_12[] = {
    { "value", rl_kind::k_int, 0.0, true, 3.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_13[] = {
    { "value", rl_kind::k_int, 0.0, true, 256.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_14[] = {
    { "value", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_DECK_MEMBERS[] = {
  { "asc_dw_draws_left", "asc_dw_draws_left", RL_OBS_FAMILY_DECK_LEAVES_0, 1 },
  { "asc_dw_fail_left", "asc_dw_fail_left", RL_OBS_FAMILY_DECK_LEAVES_1, 1 },
  { "asc_dw_proc_left", "asc_dw_proc_left", RL_OBS_FAMILY_DECK_LEAVES_2, 1 },
  { "dre_draws_left", "dre_draws_left", RL_OBS_FAMILY_DECK_LEAVES_3, 1 },
  { "dre_fail_left", "dre_fail_left", RL_OBS_FAMILY_DECK_LEAVES_4, 1 },
  { "dre_proc_left", "dre_proc_left", RL_OBS_FAMILY_DECK_LEAVES_5, 1 },
  { "storm_unleashed_draws_left", "storm_unleashed_draws_left", RL_OBS_FAMILY_DECK_LEAVES_6, 1 },
  { "storm_unleashed_fail_left", "storm_unleashed_fail_left", RL_OBS_FAMILY_DECK_LEAVES_7, 1 },
  { "storm_unleashed_proc_left", "storm_unleashed_proc_left", RL_OBS_FAMILY_DECK_LEAVES_8, 1 },
  { "tempest_draws_left", "tempest_draws_left", RL_OBS_FAMILY_DECK_LEAVES_9, 1 },
  { "tempest_fail_left", "tempest_fail_left", RL_OBS_FAMILY_DECK_LEAVES_10, 1 },
  { "tempest_proc_left", "tempest_proc_left", RL_OBS_FAMILY_DECK_LEAVES_11, 1 },
  { "tempest_procs_this_deck", "tempest_procs_this_deck", RL_OBS_FAMILY_DECK_LEAVES_12, 1 },
  { "tempest_spends_since_proc", "tempest_spends_since_proc", RL_OBS_FAMILY_DECK_LEAVES_13, 1 },
  { "ti_chain_lightning", "ti_chain_lightning", RL_OBS_FAMILY_DECK_LEAVES_14, 1 },
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

inline constexpr rl_leaf_desc RL_OBS_FAMILY_PETS_LEAVES_0[] = {
    { "active", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PETS_LEAVES_1[] = {
    { "pulse_event_remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_PETS_MEMBERS[] = {
  { "searing_totem", "searing_totem", RL_OBS_FAMILY_PETS_LEAVES_0, 1 },
  { "surging_totem", "surging_totem", RL_OBS_FAMILY_PETS_LEAVES_1, 1 },
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

inline constexpr double RL_BUCKETS_SLOT309[] = { 0.0, 2.0 };
inline constexpr double RL_BUCKETS_SLOT310[] = { 0.0, 3.0 };
inline constexpr double RL_BUCKETS_SLOT311[] = { 0.0, 4.0 };
inline constexpr double RL_BUCKETS_SLOT312[] = { 0.0, 5.0 };
inline constexpr double RL_BUCKETS_SLOT313[] = { 0.0, 2.0 };
inline constexpr double RL_BUCKETS_SLOT314[] = { 0.0, 3.0 };
inline constexpr double RL_BUCKETS_SLOT315[] = { 0.0, 4.0 };
inline constexpr double RL_BUCKETS_SLOT316[] = { 0.0, 5.0 };
inline constexpr double RL_BUCKETS_SLOT317[] = { 0.0, 2.0 };
inline constexpr double RL_BUCKETS_SLOT318[] = { 0.0, 3.0 };
inline constexpr double RL_BUCKETS_SLOT319[] = { 0.0, 4.0 };
inline constexpr double RL_BUCKETS_SLOT320[] = { 0.0, 5.0 };
inline constexpr double RL_BUCKETS_SLOT321[] = { 0.0, 1.0 };
inline constexpr double RL_BUCKETS_SLOT322[] = { 0.0, 2.0 };
inline constexpr double RL_BUCKETS_SLOT323[] = { 0.0, 3.0 };
inline constexpr double RL_BUCKETS_SLOT324[] = { 0.0, 4.0 };
inline constexpr double RL_BUCKETS_SLOT325[] = { 0.0, 5.0 };
inline constexpr double RL_BUCKETS_SLOT326[] = { 0.0, 1.0 };
inline constexpr double RL_BUCKETS_SLOT327[] = { 0.0, 2.0 };
inline constexpr double RL_BUCKETS_SLOT328[] = { 0.0, 3.0 };
inline constexpr double RL_BUCKETS_SLOT329[] = { 0.0, 4.0 };
inline constexpr double RL_BUCKETS_SLOT330[] = { 0.0, 5.0 };
inline constexpr double RL_BUCKETS_SLOT331[] = { 0.0, 1.0 };
inline constexpr double RL_BUCKETS_SLOT332[] = { 0.0, 2.0 };
inline constexpr double RL_BUCKETS_SLOT333[] = { 0.0, 3.0 };
inline constexpr double RL_BUCKETS_SLOT334[] = { 0.0, 4.0 };
inline constexpr double RL_BUCKETS_SLOT335[] = { 0.0, 5.0 };
inline constexpr double RL_BUCKETS_SLOT336[] = { 0.0, 1.0 };
inline constexpr double RL_BUCKETS_SLOT337[] = { 0.0, 2.0 };
inline constexpr double RL_BUCKETS_SLOT338[] = { 0.0, 3.0 };
inline constexpr double RL_BUCKETS_SLOT339[] = { 0.0, 4.0 };
inline constexpr double RL_BUCKETS_SLOT340[] = { 0.0, 5.0 };
inline constexpr rl_leaf_desc RL_OBS_FAMILY_SCALARS_LEAVES_0[] = {
    { "fight_remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, true },
    { "active_enemies", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "raid_event_next_in", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "dying_within_5s", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "dying_within_15s", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "enemies_in_front", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "enemies_in_melee", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "enemies_total", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "enemies_within_8yd", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "enemies_within_40yd", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "flame_shock_carrier_count", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "immunity_in", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 600.0, nullptr, 0, false },
    { "lashing_flames_carrier_count", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "lightning_rod_carrier_count", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "longest_time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "nearest_enemy_distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "soonest_time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "hits.chain_lightning", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "hits.tempest", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "hits.lava_lash.flame_shock_spread", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "hits.voltaic_blaze.cleave", rl_kind::k_int, 0.0, true, 6.0, false, 1.0, nullptr, 0, false },
    { "hits.voltaic_blaze.new_flame_shocks", rl_kind::k_int, 0.0, true, 6.0, false, 1.0, nullptr, 0, false },
    { "hits.fire_nova", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "hits.chain_lightning.at_least_2", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT309, 2, false },
    { "hits.chain_lightning.at_least_3", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT310, 2, false },
    { "hits.chain_lightning.at_least_4", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT311, 2, false },
    { "hits.chain_lightning.at_least_5", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT312, 2, false },
    { "hits.tempest.at_least_2", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT313, 2, false },
    { "hits.tempest.at_least_3", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT314, 2, false },
    { "hits.tempest.at_least_4", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT315, 2, false },
    { "hits.tempest.at_least_5", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT316, 2, false },
    { "hits.voltaic_blaze.cleave.at_least_2", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT317, 2, false },
    { "hits.voltaic_blaze.cleave.at_least_3", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT318, 2, false },
    { "hits.voltaic_blaze.cleave.at_least_4", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT319, 2, false },
    { "hits.voltaic_blaze.cleave.at_least_5", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT320, 2, false },
    { "hits.crash_lightning.at_least_1", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT321, 2, false },
    { "hits.crash_lightning.at_least_2", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT322, 2, false },
    { "hits.crash_lightning.at_least_3", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT323, 2, false },
    { "hits.crash_lightning.at_least_4", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT324, 2, false },
    { "hits.crash_lightning.at_least_5", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT325, 2, false },
    { "hits.voltaic_blaze.new_flame_shocks.at_least_1", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT326, 2, false },
    { "hits.voltaic_blaze.new_flame_shocks.at_least_2", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT327, 2, false },
    { "hits.voltaic_blaze.new_flame_shocks.at_least_3", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT328, 2, false },
    { "hits.voltaic_blaze.new_flame_shocks.at_least_4", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT329, 2, false },
    { "hits.voltaic_blaze.new_flame_shocks.at_least_5", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT330, 2, false },
    { "hits.fire_nova.at_least_1", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT331, 2, false },
    { "hits.fire_nova.at_least_2", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT332, 2, false },
    { "hits.fire_nova.at_least_3", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT333, 2, false },
    { "hits.fire_nova.at_least_4", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT334, 2, false },
    { "hits.fire_nova.at_least_5", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT335, 2, false },
    { "hits.lava_lash.flame_shock_spread.at_least_1", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT336, 2, false },
    { "hits.lava_lash.flame_shock_spread.at_least_2", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT337, 2, false },
    { "hits.lava_lash.flame_shock_spread.at_least_3", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT338, 2, false },
    { "hits.lava_lash.flame_shock_spread.at_least_4", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT339, 2, false },
    { "hits.lava_lash.flame_shock_spread.at_least_5", rl_kind::k_bucket, -1.0, false, 1.0, false, 1.0, RL_BUCKETS_SLOT340, 2, false },
    { "fight.next_wave.in", rl_kind::k_seconds, 600.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "fight.next_wave.count", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "fight.next_wave.lifetime", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "fight.next_phase.at_boss_pct", rl_kind::k_seconds, 0.0, false, 1.0, true, 100.0, nullptr, 0, false },
    { "fight.next_phase.boss_pct_now", rl_kind::k_seconds, 0.0, false, 1.0, true, 100.0, nullptr, 0, false },
    { "fight.downtime.in", rl_kind::k_seconds, 600.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "fight.bloodlust.in", rl_kind::k_seconds, 600.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "capability.racial_berserking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.trinket_voracious_heart_of_ulatek", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.consumable_potion_of_recklessness", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.enchant_arcane_mastery", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.enchant_berserkers_rage", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.embellishment_arcanoweave_lining", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.embellishment_hunters_ritual_stone", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.item_venomcursed_mastery", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.item_venomcursed_ascendance", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.weapon_venomfang", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.set_bite_of_zuljan_2pc", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.tier_mid2_enh_2pc", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.tier_mid2_enh_4pc", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.omnium_rune_burning_haste", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.omnium_core_rune_unleashed_fire", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.omnium_rune_lingering", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.trinket_void_execution_mandate", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.mode_funnel", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_unruly_winds", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_overflowing_maelstrom", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_ashen_catalyst", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_stormblast", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_overcharge", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_raging_maelstrom", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_flurry", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_hot_hand", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_hot_hand_r2", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_storms_wrath", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_elemental_tempo", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_voltaic_blaze", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_chaining_storms", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_converging_storms", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_stormflurry", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_elemental_weapons", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_fire_nova", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_lashing_flames", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_ride_the_lightning", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_doom_winds", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_sundering", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_lightning_strikes", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_elemental_assault", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_static_accumulation", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_static_accumulation_r2", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_feral_spirit", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_surging_elements", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_thunder_capacitor", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_deeply_rooted_elements", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_ascendance", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_thorims_invocation", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_primordial_storm", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_storm_unleashed", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_storm_unleashed_r2", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_storm_unleashed_r3", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_storm_unleashed_r4", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_storm_swell", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_supercharge", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_amplification_core", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_oversurge", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_pulse_capacitor", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_supportive_imbuements", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_totemic_coordination", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_earthsurge", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.hero_stormbringer", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.hero_totemic", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.tempest_venomfang", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.tempest_burning_core", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.tempest_rune_lingering", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.voltaic_blaze_venomfang", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.voltaic_blaze_burning_core", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.voltaic_blaze_rune_lingering", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.voltaic_blaze_stormbringer", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_SCALARS_MEMBERS[] = {
  { "", "", RL_OBS_FAMILY_SCALARS_LEAVES_0, 134 },
};

inline constexpr std::size_t RL_OBS_FAMILY_COUNT = 13;

inline constexpr rl_obs_family RL_OBS_FAMILIES[RL_OBS_FAMILY_COUNT] = {
  { rl_family::player_buffs, "player_buffs", rl_family_kind::buff, false, RL_OBS_FAMILY_PLAYER_BUFFS_MEMBERS, 48, 0, 78 },
  { rl_family::cooldowns, "cooldowns", rl_family_kind::cooldown, false, RL_OBS_FAMILY_COOLDOWNS_MEMBERS, 13, 78, 16 },
  { rl_family::target_facts, "target_facts", rl_family_kind::target_fact, false, RL_OBS_FAMILY_TARGET_FACTS_MEMBERS, 7, 94, 105 },
  { rl_family::shapes, "shapes", rl_family_kind::shape_fact, false, RL_OBS_FAMILY_SHAPES_MEMBERS, 2, 199, 6 },
  { rl_family::action_leaves, "action_leaves", rl_family_kind::action_expression, false, RL_OBS_FAMILY_ACTION_LEAVES_MEMBERS, 15, 205, 17 },
  { rl_family::deck, "deck", rl_family_kind::expression, false, RL_OBS_FAMILY_DECK_MEMBERS, 15, 222, 15 },
  { rl_family::stats, "stats", rl_family_kind::direct, false, RL_OBS_FAMILY_STATS_MEMBERS, 8, 237, 8 },
  { rl_family::swing_cast, "swing_cast", rl_family_kind::direct, false, RL_OBS_FAMILY_SWING_CAST_MEMBERS, 4, 245, 4 },
  { rl_family::pets, "pets", rl_family_kind::expression, false, RL_OBS_FAMILY_PETS_MEMBERS, 2, 249, 2 },
  { rl_family::raid_events, "raid_events", rl_family_kind::expression, false, RL_OBS_FAMILY_RAID_EVENTS_MEMBERS, 2, 251, 10 },
  { rl_family::legality, "legality", rl_family_kind::legality, false, RL_OBS_FAMILY_LEGALITY_MEMBERS, 22, 261, 22 },
  { rl_family::proc_chances, "proc_chances", rl_family_kind::proc_chance, false, RL_OBS_FAMILY_PROC_CHANCES_MEMBERS, 2, 283, 2 },
  { rl_family::scalars, "scalars", rl_family_kind::scalar, true, RL_OBS_FAMILY_SCALARS_MEMBERS, 1, 285, 134 },
};

// ---- Action descriptors ----

inline constexpr rl_action_desc RL_ACTIONS[RL_ACTION_DIM] = {
  { 0, "stormstrike", rl_action_kind::cast, "strike", nullptr, "stormstrike", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 1, "lightning_bolt", rl_action_kind::cast, nullptr, nullptr, "lightning_bolt", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 2, "chain_lightning", rl_action_kind::cast, nullptr, nullptr, "chain_lightning", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 3, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_next_event", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 4, "tempest", rl_action_kind::cast, nullptr, nullptr, "tempest", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 5, "windstrike", rl_action_kind::cast, "strike", nullptr, "windstrike", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 6, "crash_lightning", rl_action_kind::cast, "crash_lightning", nullptr, "crash_lightning", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 7, "lava_lash", rl_action_kind::cast, "lava_lash", nullptr, "lava_lash", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 8, "voltaic_blaze", rl_action_kind::cast, "voltaic_blaze", nullptr, "voltaic_blaze", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 9, "ascendance", rl_action_kind::cast, "ascendance", nullptr, "ascendance", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 10, "use_item_voracious_heart_of_ulatek", rl_action_kind::cast, "voracious_heart_of_ulatek_1297761", "item_cd_1141", "use_item_voracious_heart_of_ulatek", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 11, "berserking", rl_action_kind::cast, "berserking", nullptr, "berserking", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 12, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_swing_mh", { rl_wait_anchor_kind::swing, rl_swing_hand::mh }, nullptr },
  { 13, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_swing_oh", { rl_wait_anchor_kind::swing, rl_swing_hand::oh }, nullptr },
  { 14, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_maelstrom", { rl_wait_anchor_kind::maelstrom, rl_swing_hand::none }, "maelstrom_weapon" },
  { 15, "potion", rl_action_kind::cast, "potion", nullptr, "potion", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 16, "use_item_void_execution_mandate", rl_action_kind::cast, "void_execution_mandate_1250557", "item_cd_1141", "use_item_void_execution_mandate", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 17, "sundering", rl_action_kind::cast, "sundering", nullptr, "sundering", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 18, "primordial_storm", rl_action_kind::cast, nullptr, nullptr, "primordial_storm", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 19, "doom_winds", rl_action_kind::cast, "doom_winds", nullptr, "doom_winds", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 20, "surging_totem", rl_action_kind::cast, "surging_totem", nullptr, "surging_totem", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 21, "flame_shock", rl_action_kind::cast, "flame_shock", nullptr, "flame_shock", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
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
  { "crash_lightning", "crash_lightning", false },
  { "lava_lash", "lava_lash", false },
  { "ascendance", "ascendance", false },
  { "sundering", "sundering", false },
  { "primordial_storm", "primordial_storm", false },
  { "surging_totem", "surging_totem", false },
};
inline constexpr std::size_t RL_TALENT_GATE_COUNT = 6;

// ---- Capability table (246.1-02, CAP-01/CAP-02) ----

inline constexpr std::size_t RL_CAPABILITY_COUNT = 71;
inline constexpr rl_capability RL_CAPABILITIES[RL_CAPABILITY_COUNT] = {
  { "racial_berserking", 348, "racial_spell", "Berserking", 0, nullptr, 0, 0, 0, 0, 0, 0, 4, 0, 1 },
  { "trinket_voracious_heart_of_ulatek", 349, "special_effect", nullptr, 1297761, nullptr, 0, 0, 0, 0, 0, 4, 5, 1, 1 },
  { "consumable_potion_of_recklessness", 350, "potion_enabled", nullptr, 0, nullptr, 0, 0, 0, 0, 0, 9, 6, 2, 1 },
  { "enchant_arcane_mastery", 351, "special_effect", nullptr, 1236721, nullptr, 0, 0, 0, 0, 0, 15, 1, 3, 0 },
  { "enchant_berserkers_rage", 352, "special_effect", nullptr, 1236728, nullptr, 0, 0, 0, 0, 0, 16, 1, 3, 0 },
  { "embellishment_arcanoweave_lining", 353, "special_effect", nullptr, 1283697, nullptr, 0, 0, 0, 0, 0, 17, 1, 3, 0 },
  { "embellishment_hunters_ritual_stone", 354, "special_effect", nullptr, 1297382, nullptr, 0, 0, 0, 0, 0, 18, 4, 3, 0 },
  { "item_venomcursed_mastery", 355, "special_effect", nullptr, 1307923, nullptr, 0, 0, 0, 0, 0, 22, 1, 3, 0 },
  { "item_venomcursed_ascendance", 356, "special_effect", nullptr, 1317582, nullptr, 0, 0, 0, 0, 0, 23, 1, 3, 0 },
  { "weapon_venomfang", 357, "special_effect", nullptr, 1291718, nullptr, 0, 0, 0, 0, 0, 24, 15, 3, 0 },
  { "set_bite_of_zuljan_2pc", 358, "set_bonus", nullptr, 0, "MID_BOZ", 2, 0, 0, 0, 0, 39, 0, 3, 0 },
  { "tier_mid2_enh_2pc", 359, "set_bonus", nullptr, 0, "MID2", 2, 0, 0, 0, 0, 39, 11, 3, 0 },
  { "tier_mid2_enh_4pc", 360, "set_bonus", nullptr, 0, "MID2", 4, 0, 0, 0, 0, 50, 2, 3, 0 },
  { "omnium_rune_burning_haste", 361, "special_effect", nullptr, 1279610, nullptr, 0, 0, 0, 0, 1, 52, 2, 3, 0 },
  { "omnium_core_rune_unleashed_fire", 362, "special_effect", nullptr, 1279599, nullptr, 0, 0, 0, 1, 0, 54, 0, 3, 0 },
  { "omnium_rune_lingering", 363, "special_effect", nullptr, 1287555, nullptr, 0, 0, 0, 1, 1, 54, 5, 3, 0 },
  { "trinket_void_execution_mandate", 364, "special_effect", nullptr, 1250557, nullptr, 0, 0, 0, 2, 0, 59, 6, 3, 1 },
  { "mode_funnel", 365, "funnel_mode", nullptr, 0, nullptr, 0, 0, 0, 2, 0, 65, 0, 4, 0 },
  { "talent_unruly_winds", 366, "talent", nullptr, 0, nullptr, 0, 101833, 1, 2, 0, 65, 0, 4, 0 },
  { "talent_overflowing_maelstrom", 367, "talent", nullptr, 0, nullptr, 0, 101802, 1, 2, 0, 65, 0, 4, 0 },
  { "talent_ashen_catalyst", 368, "talent", nullptr, 0, nullptr, 0, 101811, 1, 2, 0, 65, 0, 4, 0 },
  { "talent_stormblast", 369, "talent", nullptr, 0, nullptr, 0, 101825, 1, 2, 0, 65, 2, 4, 0 },
  { "talent_overcharge", 370, "talent", nullptr, 0, nullptr, 0, 101808, 1, 2, 0, 67, 0, 4, 0 },
  { "talent_raging_maelstrom", 371, "talent", nullptr, 0, nullptr, 0, 101801, 1, 2, 0, 67, 0, 4, 0 },
  { "talent_flurry", 372, "talent", nullptr, 0, nullptr, 0, 101799, 1, 2, 0, 67, 2, 4, 0 },
  { "talent_hot_hand", 373, "talent", nullptr, 0, nullptr, 0, 101809, 1, 2, 0, 69, 0, 4, 0 },
  { "talent_hot_hand_r2", 374, "talent", nullptr, 0, nullptr, 0, 101809, 2, 2, 1, 69, 0, 4, 0 },
  { "talent_storms_wrath", 375, "talent", nullptr, 0, nullptr, 0, 101832, 1, 3, 0, 69, 0, 4, 0 },
  { "talent_elemental_tempo", 376, "talent", nullptr, 0, nullptr, 0, 101826, 1, 3, 0, 69, 0, 4, 0 },
  { "talent_voltaic_blaze", 377, "talent", nullptr, 0, nullptr, 0, 101819, 1, 3, 0, 69, 22, 4, 1 },
  { "talent_chaining_storms", 378, "talent", nullptr, 0, nullptr, 0, 135254, 1, 3, 0, 91, 0, 5, 0 },
  { "talent_converging_storms", 379, "talent", nullptr, 0, nullptr, 0, 101839, 1, 3, 0, 91, 2, 5, 0 },
  { "talent_stormflurry", 380, "talent", nullptr, 0, nullptr, 0, 128270, 1, 3, 0, 93, 0, 5, 0 },
  { "talent_elemental_weapons", 381, "talent", nullptr, 0, nullptr, 0, 101818, 1, 3, 0, 93, 0, 5, 0 },
  { "talent_fire_nova", 382, "talent", nullptr, 0, nullptr, 0, 136176, 1, 3, 0, 93, 0, 5, 0 },
  { "talent_lashing_flames", 383, "talent", nullptr, 0, nullptr, 0, 101812, 1, 3, 0, 93, 1, 5, 0 },
  { "talent_ride_the_lightning", 384, "talent", nullptr, 0, nullptr, 0, 101827, 1, 3, 0, 94, 0, 5, 0 },
  { "talent_doom_winds", 385, "talent", nullptr, 0, nullptr, 0, 101824, 1, 3, 0, 94, 4, 5, 1 },
  { "talent_sundering", 386, "talent", nullptr, 0, nullptr, 0, 101841, 1, 3, 0, 98, 6, 6, 1 },
  { "talent_lightning_strikes", 387, "talent", nullptr, 0, nullptr, 0, 135255, 1, 3, 0, 104, 2, 7, 0 },
  { "talent_elemental_assault", 388, "talent", nullptr, 0, nullptr, 0, 101815, 1, 3, 0, 106, 0, 7, 0 },
  { "talent_static_accumulation", 389, "talent", nullptr, 0, nullptr, 0, 101814, 1, 3, 0, 106, 0, 7, 0 },
  { "talent_static_accumulation_r2", 390, "talent", nullptr, 0, nullptr, 0, 101814, 2, 3, 1, 106, 0, 7, 0 },
  { "talent_feral_spirit", 391, "talent", nullptr, 0, nullptr, 0, 128236, 1, 4, 0, 106, 1, 7, 0 },
  { "talent_surging_elements", 392, "talent", nullptr, 0, nullptr, 0, 101829, 1, 4, 0, 107, 1, 7, 0 },
  { "talent_thunder_capacitor", 393, "talent", nullptr, 0, nullptr, 0, 135252, 1, 4, 0, 108, 0, 7, 0 },
  { "talent_deeply_rooted_elements", 394, "talent", nullptr, 0, nullptr, 0, 101816, 1, 4, 0, 108, 3, 7, 0 },
  { "talent_ascendance", 395, "talent", nullptr, 0, nullptr, 0, 114291, 1, 4, 0, 111, 6, 7, 1 },
  { "talent_thorims_invocation", 396, "talent", nullptr, 0, nullptr, 0, 101813, 1, 4, 0, 117, 1, 8, 0 },
  { "talent_primordial_storm", 397, "talent", nullptr, 0, nullptr, 0, 101828, 1, 4, 0, 118, 3, 8, 1 },
  { "talent_storm_unleashed", 398, "talent", nullptr, 0, nullptr, 0, 136971, 1, 4, 0, 121, 7, 9, 0 },
  { "talent_storm_unleashed_r2", 399, "talent", nullptr, 0, nullptr, 0, 136970, 1, 4, 1, 128, 0, 9, 0 },
  { "talent_storm_unleashed_r3", 400, "talent", nullptr, 0, nullptr, 0, 136970, 2, 5, 1, 128, 0, 9, 0 },
  { "talent_storm_unleashed_r4", 401, "talent", nullptr, 0, nullptr, 0, 136969, 1, 6, 1, 128, 0, 9, 0 },
  { "talent_storm_swell", 402, "talent", nullptr, 0, nullptr, 0, 117470, 1, 7, 0, 128, 1, 9, 0 },
  { "talent_supercharge", 403, "talent", nullptr, 0, nullptr, 0, 128225, 1, 7, 0, 129, 0, 9, 0 },
  { "talent_amplification_core", 404, "talent", nullptr, 0, nullptr, 0, 117471, 1, 7, 0, 129, 1, 9, 0 },
  { "talent_oversurge", 405, "talent", nullptr, 0, nullptr, 0, 125823, 1, 7, 0, 130, 0, 9, 0 },
  { "talent_pulse_capacitor", 406, "talent", nullptr, 0, nullptr, 0, 117463, 1, 7, 0, 130, 0, 9, 0 },
  { "talent_supportive_imbuements", 407, "talent", nullptr, 0, nullptr, 0, 125824, 1, 7, 0, 130, 0, 9, 0 },
  { "talent_totemic_coordination", 408, "talent", nullptr, 0, nullptr, 0, 117478, 1, 7, 0, 130, 0, 9, 0 },
  { "talent_earthsurge", 409, "talent", nullptr, 0, nullptr, 0, 125822, 1, 7, 0, 130, 0, 9, 0 },
  { "hero_stormbringer", 410, "talent", nullptr, 0, nullptr, 0, 117489, 1, 7, 0, 130, 39, 9, 1 },
  { "hero_totemic", 411, "talent", nullptr, 0, nullptr, 0, 117474, 1, 7, 0, 169, 18, 10, 1 },
  { "tempest_venomfang", 412, "talent", nullptr, 0, nullptr, 0, 117489, 1, 7, 1, 187, 3, 11, 0 },
  { "tempest_burning_core", 413, "talent", nullptr, 0, nullptr, 0, 117489, 1, 8, 1, 190, 1, 11, 0 },
  { "tempest_rune_lingering", 414, "talent", nullptr, 0, nullptr, 0, 117489, 1, 9, 1, 191, 1, 11, 0 },
  { "voltaic_blaze_venomfang", 415, "talent", nullptr, 0, nullptr, 0, 101819, 1, 10, 1, 192, 3, 11, 0 },
  { "voltaic_blaze_burning_core", 416, "talent", nullptr, 0, nullptr, 0, 101819, 1, 11, 1, 195, 1, 11, 0 },
  { "voltaic_blaze_rune_lingering", 417, "talent", nullptr, 0, nullptr, 0, 101819, 1, 12, 1, 196, 1, 11, 0 },
  { "voltaic_blaze_stormbringer", 418, "talent", nullptr, 0, nullptr, 0, 101819, 1, 13, 1, 197, 2, 11, 0 },
};

inline constexpr std::size_t RL_CAPABILITY_REQUIRES_COUNT = 14;
inline constexpr const char* RL_CAPABILITY_REQUIRES[RL_CAPABILITY_REQUIRES_COUNT] = {
  "omnium_core_rune_unleashed_fire",
  "omnium_core_rune_unleashed_fire",
  "talent_hot_hand",
  "talent_static_accumulation",
  "talent_storm_unleashed",
  "talent_storm_unleashed_r2",
  "talent_storm_unleashed_r3",
  "weapon_venomfang",
  "tier_mid2_enh_2pc",
  "omnium_rune_lingering",
  "weapon_venomfang",
  "tier_mid2_enh_2pc",
  "omnium_rune_lingering",
  "hero_stormbringer",
};

inline constexpr std::size_t RL_CAPABILITY_GOVERNED_SLOTS_COUNT = 199;
inline constexpr std::size_t RL_CAPABILITY_GOVERNED_SLOTS[RL_CAPABILITY_GOVERNED_SLOTS_COUNT] = {
  5,
  79,
  206,
  272,
  22,
  23,
  93,
  219,
  271,
  47,
  48,
  49,
  50,
  84,
  276,
  30,
  29,
  3,
  21,
  31,
  45,
  72,
  71,
  70,
  104,
  105,
  106,
  119,
  120,
  121,
  134,
  135,
  136,
  149,
  150,
  151,
  194,
  195,
  196,
  101,
  116,
  131,
  146,
  191,
  308,
  331,
  332,
  333,
  334,
  335,
  54,
  55,
  52,
  53,
  107,
  122,
  137,
  152,
  197,
  33,
  34,
  73,
  74,
  91,
  277,
  59,
  60,
  27,
  28,
  92,
  220,
  269,
  169,
  170,
  171,
  172,
  173,
  174,
  175,
  183,
  306,
  307,
  317,
  318,
  319,
  320,
  326,
  327,
  328,
  329,
  330,
  7,
  8,
  298,
  24,
  81,
  208,
  280,
  89,
  216,
  278,
  202,
  203,
  204,
  35,
  36,
  46,
  62,
  225,
  226,
  227,
  78,
  205,
  270,
  222,
  223,
  224,
  236,
  51,
  214,
  279,
  57,
  58,
  228,
  229,
  230,
  18,
  19,
  56,
  0,
  1,
  2,
  64,
  65,
  68,
  69,
  231,
  232,
  233,
  234,
  235,
  218,
  265,
  154,
  155,
  156,
  157,
  158,
  159,
  160,
  168,
  162,
  163,
  304,
  313,
  314,
  315,
  316,
  102,
  103,
  117,
  118,
  132,
  133,
  147,
  148,
  192,
  193,
  299,
  63,
  75,
  77,
  76,
  66,
  67,
  37,
  38,
  39,
  40,
  41,
  42,
  25,
  250,
  249,
  90,
  217,
  281,
  164,
  165,
  166,
  161,
  167,
  179,
  180,
  181,
  176,
  182,
  177,
  178,
};

inline constexpr std::size_t RL_CAPABILITY_GOVERNED_ACTIONS_COUNT = 11;
inline constexpr const char* RL_CAPABILITY_GOVERNED_ACTIONS[RL_CAPABILITY_GOVERNED_ACTIONS_COUNT] = {
  "berserking",
  "use_item_voracious_heart_of_ulatek",
  "potion",
  "use_item_void_execution_mandate",
  "voltaic_blaze",
  "doom_winds",
  "sundering",
  "ascendance",
  "primordial_storm",
  "tempest",
  "surging_totem",
};

inline constexpr double RL_CAPABILITY_FIXED_VALUE = 0.0;
inline constexpr const char* RL_NET_LAYOUT_SHA = "rl-layout-v1:5bb5399cea8d36d10d943f848b92e9e0113d72bb87d110d80bddffa36ad7dc23";

// ---- Target scorer feature list (Phase 230-02, SCOR-01) ----

inline constexpr std::size_t RL_TARGET_SLOTS = 16;
inline constexpr std::size_t RL_TARGET_FEATURES = 23;
inline constexpr const char* RL_TARGET_FEATURE_SHA = "tgt-feat-v1:4b2264a75433cfae96133e1a34adeb55b85a529f19f0245ecb3c796379c83ee9";

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
  "chain_hop_count",
  "vb_new_flame_shocks_within_10yd",
  "lava_lash_spread_within_12yd",
};

// ---- Aim head declaration (Phase 259) ----

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
  X(flame_shock_remaining) \
  X(neighbours_within_radius) \
  X(is_current_target) \
  X(burning_core_remaining) \
  X(lightning_rod_stacks) \
  X(lightning_rod_remaining) \
  X(venomfang_remaining) \
  X(venomfang_debuff_stacks) \
  X(venomfang_debuff_remaining) \
  X(rune_of_unleashed_fire_lingering_remaining) \
  X(chain_hop_count) \
  X(vb_new_flame_shocks_within_10yd) \
  X(lava_lash_spread_within_12yd)

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
  { "flame_shock_remaining", rl_kind::k_seconds, false, 1.0, true, 25.0, -1 },
  { "neighbours_within_radius", rl_kind::k_seconds, false, 1.0, true, 16.0, -1 },
  { "is_current_target", rl_kind::k_int, true, 1.0, false, 1.0, -1 },
  { "burning_core_remaining", rl_kind::k_seconds, false, 1.0, true, 6.0, 11 },
  { "lightning_rod_stacks", rl_kind::k_int, true, 1.0, false, 1.0, -1 },
  { "lightning_rod_remaining", rl_kind::k_seconds, false, 1.0, true, 8.0, -1 },
  { "venomfang_remaining", rl_kind::k_seconds, false, 1.0, true, 8.0, 9 },
  { "venomfang_debuff_stacks", rl_kind::k_int, true, 20.0, false, 1.0, 9 },
  { "venomfang_debuff_remaining", rl_kind::k_seconds, false, 1.0, true, 6.0, 9 },
  { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, false, 1.0, true, 30.0, 15 },
  { "chain_hop_count", rl_kind::k_seconds, false, 1.0, true, 5.0, -1 },
  { "vb_new_flame_shocks_within_10yd", rl_kind::k_seconds, false, 1.0, true, 6.0, -1 },
  { "lava_lash_spread_within_12yd", rl_kind::k_seconds, false, 1.0, true, 5.0, -1 },
};

inline constexpr std::size_t RL_AIM_CONTEXT_COUNT = 7;
inline constexpr std::size_t RL_AIM_CONTEXT_OBS_SLOTS[RL_AIM_CONTEXT_COUNT] = { 357, 359, 363, 43, 24, 65, 365 };
inline constexpr const char* RL_AIM_CONTEXT_NAMES[RL_AIM_CONTEXT_COUNT] = {
  "capability.weapon_venomfang",
  "capability.tier_mid2_enh_2pc",
  "capability.omnium_rune_lingering",
  "player_buffs.maelstrom_weapon.stacks",
  "player_buffs.doom_winds.remains",
  "player_buffs.tempest.remains",
  "capability.mode_funnel",
};

inline constexpr std::size_t RL_AIM_SPELL_COUNT = 7;
inline constexpr const char* RL_AIM_SPELLS[RL_AIM_SPELL_COUNT] = {
  "stormstrike",
  "lightning_bolt",
  "chain_lightning",
  "tempest",
  "windstrike",
  "lava_lash",
  "voltaic_blaze",
};

inline constexpr std::size_t RL_AIM_INPUT_COUNT = 30;
inline constexpr const char* RL_AIM_SHA = "aim-v1:dd864b4b84bb187f221e72fbab7dafa3f219ba986cf0feb4e8c90268e52ecbc9";


// ---- Generic spec tables (C-A, appended; every line above is unchanged) ----

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
  const char*  name;
  std::size_t  slot;
};

struct rl_wait_def
{
  const char* label;
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

inline constexpr std::size_t RL_TARGETED_TOKEN_COUNT = 9;
inline constexpr const char* RL_TARGETED_TOKENS[RL_TARGETED_TOKEN_COUNT] = {
  "stormstrike",
  "lightning_bolt",
  "chain_lightning",
  "tempest",
  "windstrike",
  "lava_lash",
  "voltaic_blaze",
  "primordial_storm",
  "flame_shock",
};
inline constexpr double RL_AIMED_SPELL_RANGE_YARDS[RL_TARGETED_TOKEN_COUNT] = {5.0, 40.0, 40.0, 40.0, 30.0, 5.0, 40.0, 5.0, 40.0};
inline constexpr const char* RL_TAG_MELEE_ACTION = nullptr;

inline constexpr std::size_t RL_RULE_PREF_COUNT = 9;
inline constexpr rl_rule_pref RL_RULE_PREFS[RL_RULE_PREF_COUNT] = {
  { "stormstrike", "shaman.thorims_aware_strike" },
  { "windstrike", "shaman.thorims_aware_strike" },
  { "primordial_storm", "shortest_ttd" },
  { "lightning_bolt", "shortest_ttd" },
  { "lava_lash", "shaman.lava_lash" },
  { "voltaic_blaze", "shaman.voltaic_blaze" },
  { "chain_lightning", "shaman.chain_lightning" },
  { "tempest", "shaman.tempest" },
  { "flame_shock", "shaman.voltaic_blaze" },
};

inline constexpr std::size_t RL_DECLARED_FACT_COUNT = 8;
inline constexpr rl_declared_fact RL_DECLARED_FACTS[RL_DECLARED_FACT_COUNT] = {
  { rl_fact_kind::dot_remaining, "flame_shock", 10 },
  { rl_fact_kind::dot_remaining, "burning_core", 13 },
  { rl_fact_kind::debuff_stacks, "lightning_rod", 14 },
  { rl_fact_kind::dot_remaining, "lightning_rod", 15 },
  { rl_fact_kind::dot_remaining, "venomfang", 16 },
  { rl_fact_kind::debuff_stacks, "venomfang_debuff", 17 },
  { rl_fact_kind::dot_remaining, "venomfang_debuff", 18 },
  { rl_fact_kind::dot_remaining, "rune_of_unleashed_fire_lingering", 19 },
};

inline constexpr const char* RL_RESOURCE_NAME = "maelstrom";

inline constexpr std::size_t RL_WAIT_DEF_COUNT = 4;
inline constexpr rl_wait_def RL_WAIT_DEFS[RL_WAIT_DEF_COUNT] = {
  { "wait_next_event", rl_wait_kind::next_event, 0.0 },
  { "wait_swing_mh", rl_wait_kind::swing_mh, 0.0 },
  { "wait_swing_oh", rl_wait_kind::swing_oh, 0.0 },
  { "wait_maelstrom", rl_wait_kind::resource_threshold, 0.0 },
};

inline constexpr std::size_t RL_COOLDOWN_ROW_ACTION_COUNT = 1;
inline constexpr rl_cooldown_alias RL_COOLDOWN_ROW_ACTION[RL_COOLDOWN_ROW_ACTION_COUNT] = {
  { "strike", "stormstrike" },
};

inline constexpr std::size_t RL_PROC_NAME_COUNT = 20;
inline constexpr const char* RL_PROC_NAMES[RL_PROC_NAME_COUNT] = {
  "crit_hit",
  "auto_attack_hit",
  "windfury",
  "unruly_winds",
  "stormsurge",
  "stormflurry",
  "thunder_capacitor",
  "maelstrom_weapon_gain",
  "elemental_assault",
  "fire_nova_vb",
  "awakening_storms_roll",
  "awakening_storms_rppm",
  "tempest_deck",
  "asc_dw_deck",
  "storm_unleashed_deck",
  "flurry_trigger",
  "dbc_proc_callback",
  "buff_chance_trigger",
  "spare_18",
  "spare_19",
};

inline constexpr std::size_t RL_HIT_PROVIDER_COUNT = 7;
inline constexpr rl_hit_provider RL_HIT_PROVIDERS[RL_HIT_PROVIDER_COUNT] = {
  { "hits", "chain_lightning", "shaman.chain_lightning" },
  { "hits", "tempest", "shaman.tempest" },
  { "hits", "crash_lightning", "shaman.crash_lightning" },
  { "hits", "lava_lash.flame_shock_spread", "shaman.lava_lash_flame_shock_spread" },
  { "hits", "voltaic_blaze.cleave", "shaman.voltaic_blaze_cleave" },
  { "hits", "voltaic_blaze.new_flame_shocks", "shaman.voltaic_blaze_new_flame_shocks" },
  { "hits", "fire_nova", "shaman.fire_nova" },
};
