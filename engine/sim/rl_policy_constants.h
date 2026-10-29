// ==============================================================================
// GENERATED FILE -- DO NOT EDIT (XPORT-04, D-09).
//
// Produced by:      scripts/rl/gen_rl_constants.py
// Registry source:  data/specs/enhancement.json
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
// 261028-fzl (owner ruling): the governed slots no longer gate any network input -- every slot reads its real value on every build and the
// capability bits stay as inputs. The lists feed only the addon's input-integrity guard and the census.
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
inline constexpr int RL_ENCODER_VERSION = 7;
inline constexpr std::size_t RL_OBS_DIM = 510;
inline constexpr std::size_t RL_ACTION_DIM = 22;
inline constexpr double RL_EPISODE_MAX_TIME = 300.0;
inline constexpr double RL_WAIT_FLOOR_SECONDS = 0.05;
inline constexpr double RL_PERMANENT_SATURATION = 1.0;
inline constexpr const char* RL_OBS_SCHEMA_SHA = "rl-obs-v7:df14d354a96a86080d3d4d40f61dbe4b9aad25d289310455b35e7ab7a32f7d61";
inline constexpr const char* RL_MASK_RULES_SHA = "3f930294d36b217dca01fc51600c0da9d0568e20152fb53c588b6e8ddccf7662";
inline constexpr const char* RL_ACTION_SPACE_SHA = "5e5f43d009948f4adbe65e86b1238e725596c593452d89a40e31c1b8e8e21684";

// ---- Observation name list (the materialised ordering) ----

inline constexpr const char* RL_OBS_NAMES[RL_OBS_DIM] = {
  "player_buffs.akilzons_rite.remains",
  "player_buffs.amplification_core.remains",
  "player_buffs.arc_discharge.stacks",
  "player_buffs.arc_discharge.remains",
  "player_buffs.arc_discharge.since_applied",
  "player_buffs.arcanoweave_insight.remains",
  "player_buffs.arcanoweave_insight.since_applied",
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
  "player_buffs.crash_lightning.since_applied",
  "player_buffs.critical_ritual.remains",
  "player_buffs.critical_ritual.since_applied",
  "player_buffs.devoured_strength.stacks",
  "player_buffs.devoured_strength.remains",
  "player_buffs.doom_winds.remains",
  "player_buffs.elemental_overflow.stacks",
  "player_buffs.elemental_overflow.remains",
  "player_buffs.elemental_overflow.since_applied",
  "player_buffs.exhaustion.remains",
  "player_buffs.flurry.stacks",
  "player_buffs.flurry.remains",
  "player_buffs.flurry.since_applied",
  "player_buffs.frenzied_focus.remains",
  "player_buffs.frenzied_focus.since_applied",
  "player_buffs.genius_insight.remains",
  "player_buffs.genius_insight.since_applied",
  "player_buffs.halazzis_rite.remains",
  "player_buffs.hasty_ritual.remains",
  "player_buffs.hasty_ritual.since_applied",
  "player_buffs.hot_hand.remains",
  "player_buffs.hot_hand.since_applied",
  "player_buffs.impending_execution.stacks",
  "player_buffs.impending_execution.remains",
  "player_buffs.impending_execution.since_applied",
  "player_buffs.janalais_rite.remains",
  "player_buffs.lightning_strikes.stacks",
  "player_buffs.lightning_strikes.remains",
  "player_buffs.lightning_strikes.since_applied",
  "player_buffs.lively_totems.stacks",
  "player_buffs.lively_totems.remains",
  "player_buffs.lively_totems.next_expiry",
  "player_buffs.lively_totems.expiry_2",
  "player_buffs.lively_totems.expiry_3",
  "player_buffs.lively_totems.stack_seconds",
  "player_buffs.lively_totems.since_applied",
  "player_buffs.maelstrom_weapon.stacks",
  "player_buffs.maelstrom_weapon.remains",
  "player_buffs.maelstrom_weapon.since_applied",
  "player_buffs.masterful_ritual.remains",
  "player_buffs.masterful_ritual.since_applied",
  "player_buffs.might_of_the_void.remains",
  "player_buffs.molten_weapon.remains",
  "player_buffs.nalorakks_rite.remains",
  "player_buffs.potion_of_recklessness_Crit.remains",
  "player_buffs.potion_of_recklessness_Haste.remains",
  "player_buffs.potion_of_recklessness_Mastery.remains",
  "player_buffs.potion_of_recklessness_Vers.remains",
  "player_buffs.potion_of_recklessness_penalty_Crit.remains",
  "player_buffs.potion_of_recklessness_penalty_Haste.remains",
  "player_buffs.potion_of_recklessness_penalty_Mastery.remains",
  "player_buffs.potion_of_recklessness_penalty_Vers.remains",
  "player_buffs.primordial_storm.remains",
  "player_buffs.rune_of_burning_haste.stacks",
  "player_buffs.rune_of_burning_haste.remains",
  "player_buffs.rune_of_burning_haste.since_applied",
  "player_buffs.rune_of_burning_haste.next_expiry",
  "player_buffs.rune_of_burning_haste.expiry_2",
  "player_buffs.rune_of_burning_haste.expiry_3",
  "player_buffs.rune_of_burning_haste.stack_seconds",
  "player_buffs.short_circuit.stacks",
  "player_buffs.short_circuit.remains",
  "player_buffs.short_circuit.since_applied",
  "player_buffs.static_accumulation.remains",
  "player_buffs.storm_swell.remains",
  "player_buffs.storm_unleashed.stacks",
  "player_buffs.storm_unleashed.remains",
  "player_buffs.storm_unleashed.since_applied",
  "player_buffs.stormblast.stacks",
  "player_buffs.stormblast.remains",
  "player_buffs.stormsurge.remains",
  "player_buffs.surging_elements.remains",
  "player_buffs.surging_totem.remains",
  "player_buffs.tempest.stacks",
  "player_buffs.tempest.remains",
  "player_buffs.tempest.since_applied",
  "player_buffs.totemic_rebound.stacks",
  "player_buffs.totemic_rebound.remains",
  "player_buffs.totemic_rebound.since_applied",
  "player_buffs.unlimited_power.stacks",
  "player_buffs.unlimited_power.remains",
  "player_buffs.venomcursed_ascendance.remains",
  "player_buffs.venomcursed_ascendance.since_applied",
  "player_buffs.venomcursed_mastery.remains",
  "player_buffs.venomcursed_mastery.since_applied",
  "player_buffs.versatile_ritual.remains",
  "player_buffs.versatile_ritual.since_applied",
  "player_buffs.void_execution_mandate.stacks",
  "player_buffs.void_execution_mandate.remains",
  "player_buffs.void_execution_mandate.since_applied",
  "player_buffs.voracious_heart_of_ulatek.remains",
  "player_buffs.whirling_air.remains",
  "player_buffs.whirling_earth.remains",
  "player_buffs.whirling_fire.remains",
  "player_buffs.whirling_fire.since_applied",
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
  "target_facts.chain_lightning.distance",
  "target_facts.chain_lightning.in_reach",
  "target_facts.chain_lightning.in_range",
  "target_facts.chain_lightning.in_front",
  "target_facts.chain_lightning.alive",
  "target_facts.chain_lightning.immune",
  "target_facts.chain_lightning.immunity_remaining",
  "target_facts.chain_lightning.time_to_die",
  "target_facts.chain_lightning.health_pct",
  "target_facts.chain_lightning.is_boss",
  "target_facts.chain_lightning.flame_shock_remaining",
  "target_facts.chain_lightning.neighbours_within_radius",
  "target_facts.chain_lightning.is_current_target",
  "target_facts.chain_lightning.burning_core_remaining",
  "target_facts.chain_lightning.lightning_rod_stacks",
  "target_facts.chain_lightning.lightning_rod_remaining",
  "target_facts.chain_lightning.venomfang_debuff_stacks",
  "target_facts.chain_lightning.venomfang_debuff_remaining",
  "target_facts.chain_lightning.rune_of_unleashed_fire_lingering_remaining",
  "target_facts.chain_lightning.chain_hop_count",
  "target_facts.chain_lightning.vb_new_flame_shocks_within_10yd",
  "target_facts.chain_lightning.lava_lash_spread_within_12yd",
  "target_facts.chain_lightning.flametongue_attack_remaining",
  "target_facts.chain_lightning.lashing_flames_remaining",
  "target_facts.chain_lightning.marked_for_execution_remaining",
  "target_facts.lava_lash.found",
  "target_facts.lava_lash.distance",
  "target_facts.lava_lash.in_reach",
  "target_facts.lava_lash.in_range",
  "target_facts.lava_lash.in_front",
  "target_facts.lava_lash.alive",
  "target_facts.lava_lash.immune",
  "target_facts.lava_lash.immunity_remaining",
  "target_facts.lava_lash.time_to_die",
  "target_facts.lava_lash.health_pct",
  "target_facts.lava_lash.is_boss",
  "target_facts.lava_lash.flame_shock_remaining",
  "target_facts.lava_lash.neighbours_within_radius",
  "target_facts.lava_lash.is_current_target",
  "target_facts.lava_lash.burning_core_remaining",
  "target_facts.lava_lash.lightning_rod_stacks",
  "target_facts.lava_lash.lightning_rod_remaining",
  "target_facts.lava_lash.venomfang_debuff_stacks",
  "target_facts.lava_lash.venomfang_debuff_remaining",
  "target_facts.lava_lash.rune_of_unleashed_fire_lingering_remaining",
  "target_facts.lava_lash.chain_hop_count",
  "target_facts.lava_lash.vb_new_flame_shocks_within_10yd",
  "target_facts.lava_lash.lava_lash_spread_within_12yd",
  "target_facts.lava_lash.flametongue_attack_remaining",
  "target_facts.lava_lash.lashing_flames_remaining",
  "target_facts.lava_lash.marked_for_execution_remaining",
  "target_facts.lightning_bolt.found",
  "target_facts.lightning_bolt.distance",
  "target_facts.lightning_bolt.in_reach",
  "target_facts.lightning_bolt.in_range",
  "target_facts.lightning_bolt.in_front",
  "target_facts.lightning_bolt.alive",
  "target_facts.lightning_bolt.immune",
  "target_facts.lightning_bolt.immunity_remaining",
  "target_facts.lightning_bolt.time_to_die",
  "target_facts.lightning_bolt.health_pct",
  "target_facts.lightning_bolt.is_boss",
  "target_facts.lightning_bolt.flame_shock_remaining",
  "target_facts.lightning_bolt.neighbours_within_radius",
  "target_facts.lightning_bolt.is_current_target",
  "target_facts.lightning_bolt.burning_core_remaining",
  "target_facts.lightning_bolt.lightning_rod_stacks",
  "target_facts.lightning_bolt.lightning_rod_remaining",
  "target_facts.lightning_bolt.venomfang_debuff_stacks",
  "target_facts.lightning_bolt.venomfang_debuff_remaining",
  "target_facts.lightning_bolt.rune_of_unleashed_fire_lingering_remaining",
  "target_facts.lightning_bolt.chain_hop_count",
  "target_facts.lightning_bolt.vb_new_flame_shocks_within_10yd",
  "target_facts.lightning_bolt.lava_lash_spread_within_12yd",
  "target_facts.lightning_bolt.flametongue_attack_remaining",
  "target_facts.lightning_bolt.lashing_flames_remaining",
  "target_facts.lightning_bolt.marked_for_execution_remaining",
  "target_facts.stormstrike.found",
  "target_facts.stormstrike.distance",
  "target_facts.stormstrike.in_reach",
  "target_facts.stormstrike.in_range",
  "target_facts.stormstrike.in_front",
  "target_facts.stormstrike.alive",
  "target_facts.stormstrike.immune",
  "target_facts.stormstrike.immunity_remaining",
  "target_facts.stormstrike.time_to_die",
  "target_facts.stormstrike.health_pct",
  "target_facts.stormstrike.is_boss",
  "target_facts.stormstrike.flame_shock_remaining",
  "target_facts.stormstrike.neighbours_within_radius",
  "target_facts.stormstrike.is_current_target",
  "target_facts.stormstrike.burning_core_remaining",
  "target_facts.stormstrike.lightning_rod_stacks",
  "target_facts.stormstrike.lightning_rod_remaining",
  "target_facts.stormstrike.venomfang_debuff_stacks",
  "target_facts.stormstrike.venomfang_debuff_remaining",
  "target_facts.stormstrike.rune_of_unleashed_fire_lingering_remaining",
  "target_facts.stormstrike.chain_hop_count",
  "target_facts.stormstrike.vb_new_flame_shocks_within_10yd",
  "target_facts.stormstrike.lava_lash_spread_within_12yd",
  "target_facts.stormstrike.flametongue_attack_remaining",
  "target_facts.stormstrike.lashing_flames_remaining",
  "target_facts.stormstrike.marked_for_execution_remaining",
  "target_facts.tempest.found",
  "target_facts.tempest.distance",
  "target_facts.tempest.in_reach",
  "target_facts.tempest.in_range",
  "target_facts.tempest.in_front",
  "target_facts.tempest.alive",
  "target_facts.tempest.immune",
  "target_facts.tempest.immunity_remaining",
  "target_facts.tempest.time_to_die",
  "target_facts.tempest.health_pct",
  "target_facts.tempest.is_boss",
  "target_facts.tempest.flame_shock_remaining",
  "target_facts.tempest.neighbours_within_radius",
  "target_facts.tempest.is_current_target",
  "target_facts.tempest.burning_core_remaining",
  "target_facts.tempest.lightning_rod_stacks",
  "target_facts.tempest.lightning_rod_remaining",
  "target_facts.tempest.venomfang_debuff_stacks",
  "target_facts.tempest.venomfang_debuff_remaining",
  "target_facts.tempest.rune_of_unleashed_fire_lingering_remaining",
  "target_facts.tempest.chain_hop_count",
  "target_facts.tempest.vb_new_flame_shocks_within_10yd",
  "target_facts.tempest.lava_lash_spread_within_12yd",
  "target_facts.tempest.flametongue_attack_remaining",
  "target_facts.tempest.lashing_flames_remaining",
  "target_facts.tempest.marked_for_execution_remaining",
  "target_facts.voltaic_blaze.found",
  "target_facts.voltaic_blaze.distance",
  "target_facts.voltaic_blaze.in_reach",
  "target_facts.voltaic_blaze.in_range",
  "target_facts.voltaic_blaze.in_front",
  "target_facts.voltaic_blaze.alive",
  "target_facts.voltaic_blaze.immune",
  "target_facts.voltaic_blaze.immunity_remaining",
  "target_facts.voltaic_blaze.time_to_die",
  "target_facts.voltaic_blaze.health_pct",
  "target_facts.voltaic_blaze.is_boss",
  "target_facts.voltaic_blaze.flame_shock_remaining",
  "target_facts.voltaic_blaze.neighbours_within_radius",
  "target_facts.voltaic_blaze.is_current_target",
  "target_facts.voltaic_blaze.burning_core_remaining",
  "target_facts.voltaic_blaze.lightning_rod_stacks",
  "target_facts.voltaic_blaze.lightning_rod_remaining",
  "target_facts.voltaic_blaze.venomfang_debuff_stacks",
  "target_facts.voltaic_blaze.venomfang_debuff_remaining",
  "target_facts.voltaic_blaze.rune_of_unleashed_fire_lingering_remaining",
  "target_facts.voltaic_blaze.chain_hop_count",
  "target_facts.voltaic_blaze.vb_new_flame_shocks_within_10yd",
  "target_facts.voltaic_blaze.lava_lash_spread_within_12yd",
  "target_facts.voltaic_blaze.flametongue_attack_remaining",
  "target_facts.voltaic_blaze.lashing_flames_remaining",
  "target_facts.voltaic_blaze.marked_for_execution_remaining",
  "target_facts.windstrike.found",
  "target_facts.windstrike.distance",
  "target_facts.windstrike.in_reach",
  "target_facts.windstrike.in_range",
  "target_facts.windstrike.in_front",
  "target_facts.windstrike.alive",
  "target_facts.windstrike.immune",
  "target_facts.windstrike.immunity_remaining",
  "target_facts.windstrike.time_to_die",
  "target_facts.windstrike.health_pct",
  "target_facts.windstrike.is_boss",
  "target_facts.windstrike.flame_shock_remaining",
  "target_facts.windstrike.neighbours_within_radius",
  "target_facts.windstrike.is_current_target",
  "target_facts.windstrike.burning_core_remaining",
  "target_facts.windstrike.lightning_rod_stacks",
  "target_facts.windstrike.lightning_rod_remaining",
  "target_facts.windstrike.venomfang_debuff_stacks",
  "target_facts.windstrike.venomfang_debuff_remaining",
  "target_facts.windstrike.rune_of_unleashed_fire_lingering_remaining",
  "target_facts.windstrike.chain_hop_count",
  "target_facts.windstrike.vb_new_flame_shocks_within_10yd",
  "target_facts.windstrike.lava_lash_spread_within_12yd",
  "target_facts.windstrike.flametongue_attack_remaining",
  "target_facts.windstrike.lashing_flames_remaining",
  "target_facts.windstrike.marked_for_execution_remaining",
  "shapes.crash_lightning.summed_remaining_life",
  "shapes.crash_lightning.long_lived_count",
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
  "action_leaves.potion.ready",
  "action_leaves.primordial_storm.ready",
  "action_leaves.stormstrike.ready",
  "action_leaves.sundering.ready",
  "action_leaves.surging_totem.ready",
  "action_leaves.tempest.ready",
  "action_leaves.use_item_void_execution_mandate.ready",
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
  "hits.crash_lightning",
  "hits.lava_lash",
  "hits.voltaic_blaze",
  "hits.sundering",
  "hits.primordial_storm",
  "hits.ascendance_damage",
  "hits.chain_lightning_ps",
  "hits.crash_lightning_proc",
  "hits.doom_winds_damage",
  "hits.fire_nova",
  "hits.flametongue_attack_imbuement_mastery",
  "hits.perfected_guillotine",
  "hits.searing_volley",
  "hits.tremor",
  "hits.venomfang_burst",
  "hits.lava_lash.flame_shock_spread",
  "hits.voltaic_blaze.new_flame_shocks",
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
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_1[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_2[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 30.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_3[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 150.0, false, 1.0, true, 150.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_4[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_5[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_6[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 40.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_7[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 6.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_8[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "next_expiry", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "expiry_2", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "expiry_3", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "stack_seconds", rl_kind::k_seconds, 0.0, false, 1.0, true, 100.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_9[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "next_expiry", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "expiry_2", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "expiry_3", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "stack_seconds", rl_kind::k_seconds, 0.0, false, 1.0, true, 250.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 30.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_10[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 600.0, false, 1.0, true, 600.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_11[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 99.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_12[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_13[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 30.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_14[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 600.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_15[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 3.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 30.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_16[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 120.0, false, 1.0, true, 120.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_17[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 100.0, false, 1.0, true, 100.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_18[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_19[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 600.0, false, 1.0, true, 600.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_20[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 30.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_21[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 15.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 30.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_22[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_23[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 3.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 30.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_24[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "next_expiry", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "expiry_2", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "expiry_3", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "stack_seconds", rl_kind::k_seconds, 0.0, false, 1.0, true, 100.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 30.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_25[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 30.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_26[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 600.0, false, 1.0, true, 600.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_27[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_28[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_29[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_30[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_31[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_32[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_33[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_34[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_35[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_36[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_37[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_38[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_39[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 30.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "next_expiry", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "expiry_2", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "expiry_3", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "stack_seconds", rl_kind::k_seconds, 0.0, false, 1.0, true, 200.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_40[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 30.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_41[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_42[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_43[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 30.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_44[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_45[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_46[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_47[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_48[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 30.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_49[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 30.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_50[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 15.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_51[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 150.0, false, 1.0, true, 150.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_52[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 200.0, false, 1.0, true, 200.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_53[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 600.0, false, 1.0, true, 600.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_54[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 30.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_55[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_56[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_57[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_58[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "since_applied", rl_kind::k_seconds, 30.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_PLAYER_BUFFS_MEMBERS[] = {
  { "akilzons_rite", "akilzons_rite", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_0, 1 },
  { "amplification_core", "amplification_core", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_1, 1 },
  { "arc_discharge", "arc_discharge", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_2, 3 },
  { "arcanoweave_insight", "arcanoweave_insight", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_3, 2 },
  { "ascendance", "ascendance", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_4, 1 },
  { "berserking", "berserking", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_5, 1 },
  { "bloodlust", "bloodlust", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_6, 1 },
  { "converging_storms", "converging_storms", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_7, 2 },
  { "crackling_surge", "crackling_surge", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_8, 6 },
  { "crash_lightning", "crash_lightning", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_9, 7 },
  { "critical_ritual", "critical_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_10, 2 },
  { "devoured_strength", "devoured_strength", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_11, 2 },
  { "doom_winds", "doom_winds", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_12, 1 },
  { "elemental_overflow", "elemental_overflow", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_13, 3 },
  { "exhaustion", "exhaustion", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_14, 1 },
  { "flurry", "flurry", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_15, 3 },
  { "frenzied_focus", "frenzied_focus", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_16, 2 },
  { "genius_insight", "genius_insight", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_17, 2 },
  { "halazzis_rite", "halazzis_rite", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_18, 1 },
  { "hasty_ritual", "hasty_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_19, 2 },
  { "hot_hand", "hot_hand", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_20, 2 },
  { "impending_execution", "impending_execution", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_21, 3 },
  { "janalais_rite", "janalais_rite", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_22, 1 },
  { "lightning_strikes", "lightning_strikes", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_23, 3 },
  { "lively_totems", "lively_totems", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_24, 7 },
  { "maelstrom_weapon", "maelstrom_weapon", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_25, 3 },
  { "masterful_ritual", "masterful_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_26, 2 },
  { "might_of_the_void", "might_of_the_void", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_27, 1 },
  { "molten_weapon", "molten_weapon", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_28, 1 },
  { "nalorakks_rite", "nalorakks_rite", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_29, 1 },
  { "potion_of_recklessness_Crit", "potion_of_recklessness_Crit", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_30, 1 },
  { "potion_of_recklessness_Haste", "potion_of_recklessness_Haste", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_31, 1 },
  { "potion_of_recklessness_Mastery", "potion_of_recklessness_Mastery", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_32, 1 },
  { "potion_of_recklessness_Vers", "potion_of_recklessness_Vers", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_33, 1 },
  { "potion_of_recklessness_penalty_Crit", "potion_of_recklessness_penalty_Crit", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_34, 1 },
  { "potion_of_recklessness_penalty_Haste", "potion_of_recklessness_penalty_Haste", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_35, 1 },
  { "potion_of_recklessness_penalty_Mastery", "potion_of_recklessness_penalty_Mastery", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_36, 1 },
  { "potion_of_recklessness_penalty_Vers", "potion_of_recklessness_penalty_Vers", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_37, 1 },
  { "primordial_storm", "primordial_storm", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_38, 1 },
  { "rune_of_burning_haste", "rune_of_burning_haste", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_39, 7 },
  { "short_circuit", "short_circuit", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_40, 3 },
  { "static_accumulation", "static_accumulation", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_41, 1 },
  { "storm_swell", "storm_swell", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_42, 1 },
  { "storm_unleashed", "storm_unleashed", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_43, 3 },
  { "stormblast", "stormblast", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_44, 2 },
  { "stormsurge", "stormsurge", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_45, 1 },
  { "surging_elements", "surging_elements", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_46, 1 },
  { "surging_totem", "surging_totem", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_47, 1 },
  { "tempest", "tempest", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_48, 3 },
  { "totemic_rebound", "totemic_rebound", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_49, 3 },
  { "unlimited_power", "unlimited_power", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_50, 2 },
  { "venomcursed_ascendance", "venomcursed_ascendance", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_51, 2 },
  { "venomcursed_mastery", "venomcursed_mastery", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_52, 2 },
  { "versatile_ritual", "versatile_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_53, 2 },
  { "void_execution_mandate", "void_execution_mandate", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_54, 3 },
  { "voracious_heart_of_ulatek", "voracious_heart_of_ulatek", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_55, 1 },
  { "whirling_air", "whirling_air", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_56, 1 },
  { "whirling_earth", "whirling_earth", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_57, 1 },
  { "whirling_fire", "whirling_fire", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_58, 2 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_0[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 200.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_1[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 200.0, nullptr, 0, false },
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
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_6[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 300.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_7[] = {
    { "charges_fractional", rl_kind::k_float, 2.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "charges", rl_kind::k_int, 2.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "recharge_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "full_recharge_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
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
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 100.0, nullptr, 0, false },
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
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "alive", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, true, 600.0, false, 1.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "flame_shock_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 16.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "burning_core_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lightning_rod_stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "lightning_rod_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "venomfang_debuff_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "chain_hop_count", rl_kind::k_seconds, 0.0, false, 1.0, true, 5.0, nullptr, 0, false },
    { "vb_new_flame_shocks_within_10yd", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lava_lash_spread_within_12yd", rl_kind::k_seconds, 0.0, false, 1.0, true, 5.0, nullptr, 0, false },
    { "flametongue_attack_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 1.0, nullptr, 0, false },
    { "lashing_flames_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "marked_for_execution_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_1[] = {
    { "found", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "alive", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, true, 600.0, false, 1.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "flame_shock_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 16.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "burning_core_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lightning_rod_stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "lightning_rod_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "venomfang_debuff_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "chain_hop_count", rl_kind::k_seconds, 0.0, false, 1.0, true, 5.0, nullptr, 0, false },
    { "vb_new_flame_shocks_within_10yd", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lava_lash_spread_within_12yd", rl_kind::k_seconds, 0.0, false, 1.0, true, 5.0, nullptr, 0, false },
    { "flametongue_attack_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 1.0, nullptr, 0, false },
    { "lashing_flames_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "marked_for_execution_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_2[] = {
    { "found", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "alive", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, true, 600.0, false, 1.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "flame_shock_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 16.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "burning_core_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lightning_rod_stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "lightning_rod_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "venomfang_debuff_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "chain_hop_count", rl_kind::k_seconds, 0.0, false, 1.0, true, 5.0, nullptr, 0, false },
    { "vb_new_flame_shocks_within_10yd", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lava_lash_spread_within_12yd", rl_kind::k_seconds, 0.0, false, 1.0, true, 5.0, nullptr, 0, false },
    { "flametongue_attack_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 1.0, nullptr, 0, false },
    { "lashing_flames_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "marked_for_execution_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_3[] = {
    { "found", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "alive", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, true, 600.0, false, 1.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "flame_shock_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 16.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "burning_core_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lightning_rod_stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "lightning_rod_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "venomfang_debuff_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "chain_hop_count", rl_kind::k_seconds, 0.0, false, 1.0, true, 5.0, nullptr, 0, false },
    { "vb_new_flame_shocks_within_10yd", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lava_lash_spread_within_12yd", rl_kind::k_seconds, 0.0, false, 1.0, true, 5.0, nullptr, 0, false },
    { "flametongue_attack_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 1.0, nullptr, 0, false },
    { "lashing_flames_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "marked_for_execution_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_4[] = {
    { "found", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "alive", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, true, 600.0, false, 1.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "flame_shock_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 16.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "burning_core_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lightning_rod_stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "lightning_rod_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "venomfang_debuff_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "chain_hop_count", rl_kind::k_seconds, 0.0, false, 1.0, true, 5.0, nullptr, 0, false },
    { "vb_new_flame_shocks_within_10yd", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lava_lash_spread_within_12yd", rl_kind::k_seconds, 0.0, false, 1.0, true, 5.0, nullptr, 0, false },
    { "flametongue_attack_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 1.0, nullptr, 0, false },
    { "lashing_flames_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "marked_for_execution_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_5[] = {
    { "found", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "alive", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, true, 600.0, false, 1.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "flame_shock_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 16.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "burning_core_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lightning_rod_stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "lightning_rod_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "venomfang_debuff_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "chain_hop_count", rl_kind::k_seconds, 0.0, false, 1.0, true, 5.0, nullptr, 0, false },
    { "vb_new_flame_shocks_within_10yd", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lava_lash_spread_within_12yd", rl_kind::k_seconds, 0.0, false, 1.0, true, 5.0, nullptr, 0, false },
    { "flametongue_attack_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 1.0, nullptr, 0, false },
    { "lashing_flames_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "marked_for_execution_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_6[] = {
    { "found", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "alive", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, true, 600.0, false, 1.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "flame_shock_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 16.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "burning_core_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lightning_rod_stacks", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "lightning_rod_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "venomfang_debuff_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
    { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "chain_hop_count", rl_kind::k_seconds, 0.0, false, 1.0, true, 5.0, nullptr, 0, false },
    { "vb_new_flame_shocks_within_10yd", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
    { "lava_lash_spread_within_12yd", rl_kind::k_seconds, 0.0, false, 1.0, true, 5.0, nullptr, 0, false },
    { "flametongue_attack_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 1.0, nullptr, 0, false },
    { "lashing_flames_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "marked_for_execution_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_TARGET_FACTS_MEMBERS[] = {
  { "chain_lightning", "chain_lightning", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_0, 26 },
  { "lava_lash", "lava_lash", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_1, 26 },
  { "lightning_bolt", "lightning_bolt", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_2, 26 },
  { "stormstrike", "stormstrike", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_3, 26 },
  { "tempest", "tempest", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_4, 26 },
  { "voltaic_blaze", "voltaic_blaze", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_5, 26 },
  { "windstrike", "windstrike", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_6, 26 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_SHAPES_LEAVES_0[] = {
    { "summed_remaining_life", rl_kind::k_seconds, 0.0, true, 4000.0, false, 1.0, nullptr, 0, false },
    { "long_lived_count", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_SHAPES_LEAVES_1[] = {
    { "summed_remaining_life", rl_kind::k_seconds, 0.0, true, 4000.0, false, 1.0, nullptr, 0, false },
    { "long_lived_count", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_SHAPES_MEMBERS[] = {
  { "crash_lightning", "crash_lightning", RL_OBS_FAMILY_SHAPES_LEAVES_0, 2 },
  { "sundering", "sundering", RL_OBS_FAMILY_SHAPES_LEAVES_1, 2 },
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
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_15[] = {
    { "ready", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_16[] = {
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
  { "potion", "potion", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_7, 1 },
  { "primordial_storm", "primordial_storm", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_8, 1 },
  { "stormstrike", "stormstrike", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_9, 1 },
  { "sundering", "sundering", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_10, 1 },
  { "surging_totem", "surging_totem", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_11, 1 },
  { "tempest", "tempest", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_12, 1 },
  { "use_item_void_execution_mandate", "use_item_void_execution_mandate", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_13, 1 },
  { "use_item_voracious_heart_of_ulatek", "use_item_voracious_heart_of_ulatek", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_14, 1 },
  { "voltaic_blaze", "voltaic_blaze", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_15, 1 },
  { "windstrike", "windstrike", RL_OBS_FAMILY_ACTION_LEAVES_LEAVES_16, 1 },
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
    { "value", rl_kind::k_int, 0.0, true, 4.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_DECK_LEAVES_13[] = {
    { "value", rl_kind::k_int, 0.0, true, 200.0, false, 1.0, nullptr, 0, false },
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
    { "value", rl_kind::k_float, 0.0, true, 4000.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_STATS_LEAVES_2[] = {
    { "value", rl_kind::k_float, 0.0, true, 0.2, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_STATS_LEAVES_3[] = {
    { "value", rl_kind::k_float, 0.0, true, 1.2, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_STATS_LEAVES_4[] = {
    { "value", rl_kind::k_float, 0.0, true, 5000.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_STATS_LEAVES_5[] = {
    { "value", rl_kind::k_float, 0.0, true, 5000.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_STATS_LEAVES_6[] = {
    { "value", rl_kind::k_float, 0.0, true, 2.5, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_STATS_LEAVES_7[] = {
    { "value", rl_kind::k_float, 0.0, true, 1000.0, false, 1.0, nullptr, 0, false },
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
    { "value", rl_kind::k_seconds, 0.0, false, 1.0, true, 3.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_SWING_CAST_LEAVES_1[] = {
    { "value", rl_kind::k_seconds, 0.0, false, 1.0, true, 2.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_SWING_CAST_LEAVES_2[] = {
    { "value", rl_kind::k_seconds, 0.0, false, 1.0, true, 4.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_SWING_CAST_LEAVES_3[] = {
    { "value", rl_kind::k_seconds, 0.0, false, 1.0, true, 4.0, nullptr, 0, false },
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
    { "count", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "duration", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "cooldown", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "up", rl_kind::k_int, 0.0, false, 1.0, true, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_RAID_EVENTS_LEAVES_1[] = {
    { "in", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 250.0, nullptr, 0, false },
    { "up", rl_kind::k_int, 0.0, false, 1.0, true, 1.0, nullptr, 0, false },
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
  { "04", "04", RL_OBS_FAMILY_LEGALITY_LEAVES_3, 1 },
  { "05", "05", RL_OBS_FAMILY_LEGALITY_LEAVES_4, 1 },
  { "06", "06", RL_OBS_FAMILY_LEGALITY_LEAVES_5, 1 },
  { "07", "07", RL_OBS_FAMILY_LEGALITY_LEAVES_6, 1 },
  { "08", "08", RL_OBS_FAMILY_LEGALITY_LEAVES_7, 1 },
  { "09", "09", RL_OBS_FAMILY_LEGALITY_LEAVES_8, 1 },
  { "10", "10", RL_OBS_FAMILY_LEGALITY_LEAVES_9, 1 },
  { "11", "11", RL_OBS_FAMILY_LEGALITY_LEAVES_10, 1 },
  { "12", "12", RL_OBS_FAMILY_LEGALITY_LEAVES_11, 1 },
  { "13", "13", RL_OBS_FAMILY_LEGALITY_LEAVES_12, 1 },
  { "14", "14", RL_OBS_FAMILY_LEGALITY_LEAVES_13, 1 },
  { "15", "15", RL_OBS_FAMILY_LEGALITY_LEAVES_14, 1 },
  { "16", "16", RL_OBS_FAMILY_LEGALITY_LEAVES_15, 1 },
  { "17", "17", RL_OBS_FAMILY_LEGALITY_LEAVES_16, 1 },
  { "18", "18", RL_OBS_FAMILY_LEGALITY_LEAVES_17, 1 },
  { "19", "19", RL_OBS_FAMILY_LEGALITY_LEAVES_18, 1 },
  { "20", "20", RL_OBS_FAMILY_LEGALITY_LEAVES_19, 1 },
  { "21", "21", RL_OBS_FAMILY_LEGALITY_LEAVES_20, 1 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_PROC_CHANCES_LEAVES_0[] = {
    { "value", rl_kind::k_float, 0.0, true, 0.4, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PROC_CHANCES_LEAVES_1[] = {
    { "value", rl_kind::k_float, 0.0, true, 0.8, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_PROC_CHANCES_MEMBERS[] = {
  { "stormsurge", "stormsurge_proc_chance_current", RL_OBS_FAMILY_PROC_CHANCES_LEAVES_0, 1 },
  { "windfury", "windfury_proc_chance_current", RL_OBS_FAMILY_PROC_CHANCES_LEAVES_1, 1 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_SCALARS_LEAVES_0[] = {
    { "fight_remains", rl_kind::k_seconds, 0.0, true, 600.0, false, 1.0, nullptr, 0, true },
    { "active_enemies", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "raid_event_next_in", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "dying_within_5s", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "dying_within_15s", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "enemies_in_front", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "enemies_in_melee", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "enemies_total", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "enemies_within_8yd", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "enemies_within_40yd", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "flame_shock_carrier_count", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "immunity_in", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 600.0, nullptr, 0, false },
    { "lashing_flames_carrier_count", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "lightning_rod_carrier_count", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "longest_time_to_die", rl_kind::k_seconds, 0.0, true, 600.0, false, 1.0, nullptr, 0, false },
    { "nearest_enemy_distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "soonest_time_to_die", rl_kind::k_seconds, 0.0, true, 600.0, false, 1.0, nullptr, 0, false },
    { "hits.chain_lightning", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "hits.tempest", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "hits.crash_lightning", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "hits.lava_lash", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "hits.voltaic_blaze", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "hits.sundering", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "hits.primordial_storm", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "hits.ascendance_damage", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "hits.chain_lightning_ps", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "hits.crash_lightning_proc", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "hits.doom_winds_damage", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "hits.fire_nova", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "hits.flametongue_attack_imbuement_mastery", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "hits.perfected_guillotine", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "hits.searing_volley", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "hits.tremor", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "hits.venomfang_burst", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "hits.lava_lash.flame_shock_spread", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
    { "hits.voltaic_blaze.new_flame_shocks", rl_kind::k_int, 0.0, false, 1.0, true, 50.0, nullptr, 0, false },
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
  { "", "", RL_OBS_FAMILY_SCALARS_LEAVES_0, 108 },
};

inline constexpr std::size_t RL_OBS_FAMILY_COUNT = 13;

inline constexpr rl_obs_family RL_OBS_FAMILIES[RL_OBS_FAMILY_COUNT] = {
  { rl_family::player_buffs, "player_buffs", rl_family_kind::buff, false, RL_OBS_FAMILY_PLAYER_BUFFS_MEMBERS, 59, 0, 119 },
  { rl_family::cooldowns, "cooldowns", rl_family_kind::cooldown, false, RL_OBS_FAMILY_COOLDOWNS_MEMBERS, 13, 119, 16 },
  { rl_family::target_facts, "target_facts", rl_family_kind::target_fact, false, RL_OBS_FAMILY_TARGET_FACTS_MEMBERS, 7, 135, 182 },
  { rl_family::shapes, "shapes", rl_family_kind::shape_fact, false, RL_OBS_FAMILY_SHAPES_MEMBERS, 2, 317, 4 },
  { rl_family::action_leaves, "action_leaves", rl_family_kind::action_expression, false, RL_OBS_FAMILY_ACTION_LEAVES_MEMBERS, 17, 321, 19 },
  { rl_family::deck, "deck", rl_family_kind::expression, false, RL_OBS_FAMILY_DECK_MEMBERS, 15, 340, 15 },
  { rl_family::stats, "stats", rl_family_kind::direct, false, RL_OBS_FAMILY_STATS_MEMBERS, 8, 355, 8 },
  { rl_family::swing_cast, "swing_cast", rl_family_kind::direct, false, RL_OBS_FAMILY_SWING_CAST_MEMBERS, 4, 363, 4 },
  { rl_family::pets, "pets", rl_family_kind::expression, false, RL_OBS_FAMILY_PETS_MEMBERS, 2, 367, 2 },
  { rl_family::raid_events, "raid_events", rl_family_kind::expression, false, RL_OBS_FAMILY_RAID_EVENTS_MEMBERS, 2, 369, 10 },
  { rl_family::legality, "legality", rl_family_kind::legality, false, RL_OBS_FAMILY_LEGALITY_MEMBERS, 21, 379, 21 },
  { rl_family::proc_chances, "proc_chances", rl_family_kind::proc_chance, false, RL_OBS_FAMILY_PROC_CHANCES_MEMBERS, 2, 400, 2 },
  { rl_family::scalars, "scalars", rl_family_kind::scalar, true, RL_OBS_FAMILY_SCALARS_MEMBERS, 1, 402, 108 },
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
  { "racial_berserking", 439, "racial_spell", "Berserking", 0, nullptr, 0, 0, 0, 0, 0, 0, 0, 0, 1 },
  { "trinket_voracious_heart_of_ulatek", 440, "special_effect", nullptr, 1297761, nullptr, 0, 0, 0, 0, 0, 0, 0, 1, 1 },
  { "consumable_potion_of_recklessness", 441, "potion_enabled", nullptr, 0, nullptr, 0, 0, 0, 0, 0, 0, 0, 2, 1 },
  { "enchant_arcane_mastery", 442, "special_effect", nullptr, 1236721, nullptr, 0, 0, 0, 0, 0, 0, 0, 3, 0 },
  { "enchant_berserkers_rage", 443, "special_effect", nullptr, 1236728, nullptr, 0, 0, 0, 0, 0, 0, 0, 3, 0 },
  { "embellishment_arcanoweave_lining", 444, "special_effect", nullptr, 1283697, nullptr, 0, 0, 0, 0, 0, 0, 0, 3, 0 },
  { "embellishment_hunters_ritual_stone", 445, "special_effect", nullptr, 1297382, nullptr, 0, 0, 0, 0, 0, 0, 0, 3, 0 },
  { "item_venomcursed_mastery", 446, "special_effect", nullptr, 1307923, nullptr, 0, 0, 0, 0, 0, 0, 0, 3, 0 },
  { "item_venomcursed_ascendance", 447, "special_effect", nullptr, 1317582, nullptr, 0, 0, 0, 0, 0, 0, 0, 3, 0 },
  { "weapon_venomfang", 448, "special_effect", nullptr, 1291718, nullptr, 0, 0, 0, 0, 0, 0, 0, 3, 0 },
  { "set_bite_of_zuljan_2pc", 449, "set_bonus", nullptr, 0, "MID_BOZ", 2, 0, 0, 0, 0, 0, 0, 3, 0 },
  { "tier_mid2_enh_2pc", 450, "set_bonus", nullptr, 0, "MID2", 2, 0, 0, 0, 0, 0, 0, 3, 0 },
  { "tier_mid2_enh_4pc", 451, "set_bonus", nullptr, 0, "MID2", 4, 0, 0, 0, 0, 0, 0, 3, 0 },
  { "omnium_rune_burning_haste", 452, "special_effect", nullptr, 1279610, nullptr, 0, 0, 0, 0, 1, 0, 0, 3, 0 },
  { "omnium_core_rune_unleashed_fire", 453, "special_effect", nullptr, 1279599, nullptr, 0, 0, 0, 1, 0, 0, 0, 3, 0 },
  { "omnium_rune_lingering", 454, "special_effect", nullptr, 1287555, nullptr, 0, 0, 0, 1, 1, 0, 0, 3, 0 },
  { "trinket_void_execution_mandate", 455, "special_effect", nullptr, 1250557, nullptr, 0, 0, 0, 2, 0, 0, 0, 3, 1 },
  { "mode_funnel", 456, "funnel_mode", nullptr, 0, nullptr, 0, 0, 0, 2, 0, 0, 0, 4, 0 },
  { "talent_unruly_winds", 457, "talent", nullptr, 0, nullptr, 0, 101833, 1, 2, 0, 0, 0, 4, 0 },
  { "talent_overflowing_maelstrom", 458, "talent", nullptr, 0, nullptr, 0, 101802, 1, 2, 0, 0, 0, 4, 0 },
  { "talent_ashen_catalyst", 459, "talent", nullptr, 0, nullptr, 0, 101811, 1, 2, 0, 0, 0, 4, 0 },
  { "talent_stormblast", 460, "talent", nullptr, 0, nullptr, 0, 101825, 1, 2, 0, 0, 0, 4, 0 },
  { "talent_overcharge", 461, "talent", nullptr, 0, nullptr, 0, 101808, 1, 2, 0, 0, 0, 4, 0 },
  { "talent_raging_maelstrom", 462, "talent", nullptr, 0, nullptr, 0, 101801, 1, 2, 0, 0, 0, 4, 0 },
  { "talent_flurry", 463, "talent", nullptr, 0, nullptr, 0, 101799, 1, 2, 0, 0, 0, 4, 0 },
  { "talent_hot_hand", 464, "talent", nullptr, 0, nullptr, 0, 101809, 1, 2, 0, 0, 0, 4, 0 },
  { "talent_hot_hand_r2", 465, "talent", nullptr, 0, nullptr, 0, 101809, 2, 2, 1, 0, 0, 4, 0 },
  { "talent_storms_wrath", 466, "talent", nullptr, 0, nullptr, 0, 101832, 1, 3, 0, 0, 0, 4, 0 },
  { "talent_elemental_tempo", 467, "talent", nullptr, 0, nullptr, 0, 101826, 1, 3, 0, 0, 0, 4, 0 },
  { "talent_voltaic_blaze", 468, "talent", nullptr, 0, nullptr, 0, 101819, 1, 3, 0, 0, 0, 4, 1 },
  { "talent_chaining_storms", 469, "talent", nullptr, 0, nullptr, 0, 135254, 1, 3, 0, 0, 0, 5, 0 },
  { "talent_converging_storms", 470, "talent", nullptr, 0, nullptr, 0, 101839, 1, 3, 0, 0, 0, 5, 0 },
  { "talent_stormflurry", 471, "talent", nullptr, 0, nullptr, 0, 128270, 1, 3, 0, 0, 0, 5, 0 },
  { "talent_elemental_weapons", 472, "talent", nullptr, 0, nullptr, 0, 101818, 1, 3, 0, 0, 0, 5, 0 },
  { "talent_fire_nova", 473, "talent", nullptr, 0, nullptr, 0, 136176, 1, 3, 0, 0, 0, 5, 0 },
  { "talent_lashing_flames", 474, "talent", nullptr, 0, nullptr, 0, 101812, 1, 3, 0, 0, 0, 5, 0 },
  { "talent_ride_the_lightning", 475, "talent", nullptr, 0, nullptr, 0, 101827, 1, 3, 0, 0, 0, 5, 0 },
  { "talent_doom_winds", 476, "talent", nullptr, 0, nullptr, 0, 101824, 1, 3, 0, 0, 0, 5, 1 },
  { "talent_sundering", 477, "talent", nullptr, 0, nullptr, 0, 101841, 1, 3, 0, 0, 0, 6, 1 },
  { "talent_lightning_strikes", 478, "talent", nullptr, 0, nullptr, 0, 135255, 1, 3, 0, 0, 0, 7, 0 },
  { "talent_elemental_assault", 479, "talent", nullptr, 0, nullptr, 0, 101815, 1, 3, 0, 0, 0, 7, 0 },
  { "talent_static_accumulation", 480, "talent", nullptr, 0, nullptr, 0, 101814, 1, 3, 0, 0, 0, 7, 0 },
  { "talent_static_accumulation_r2", 481, "talent", nullptr, 0, nullptr, 0, 101814, 2, 3, 1, 0, 0, 7, 0 },
  { "talent_feral_spirit", 482, "talent", nullptr, 0, nullptr, 0, 128236, 1, 4, 0, 0, 0, 7, 0 },
  { "talent_surging_elements", 483, "talent", nullptr, 0, nullptr, 0, 101829, 1, 4, 0, 0, 0, 7, 0 },
  { "talent_thunder_capacitor", 484, "talent", nullptr, 0, nullptr, 0, 135252, 1, 4, 0, 0, 0, 7, 0 },
  { "talent_deeply_rooted_elements", 485, "talent", nullptr, 0, nullptr, 0, 101816, 1, 4, 0, 0, 0, 7, 0 },
  { "talent_ascendance", 486, "talent", nullptr, 0, nullptr, 0, 114291, 1, 4, 0, 0, 0, 7, 1 },
  { "talent_thorims_invocation", 487, "talent", nullptr, 0, nullptr, 0, 101813, 1, 4, 0, 0, 0, 8, 0 },
  { "talent_primordial_storm", 488, "talent", nullptr, 0, nullptr, 0, 101828, 1, 4, 0, 0, 0, 8, 1 },
  { "talent_storm_unleashed", 489, "talent", nullptr, 0, nullptr, 0, 136971, 1, 4, 0, 0, 0, 9, 0 },
  { "talent_storm_unleashed_r2", 490, "talent", nullptr, 0, nullptr, 0, 136970, 1, 4, 1, 0, 0, 9, 0 },
  { "talent_storm_unleashed_r3", 491, "talent", nullptr, 0, nullptr, 0, 136970, 2, 5, 1, 0, 0, 9, 0 },
  { "talent_storm_unleashed_r4", 492, "talent", nullptr, 0, nullptr, 0, 136969, 1, 6, 1, 0, 0, 9, 0 },
  { "talent_storm_swell", 493, "talent", nullptr, 0, nullptr, 0, 117470, 1, 7, 0, 0, 0, 9, 0 },
  { "talent_supercharge", 494, "talent", nullptr, 0, nullptr, 0, 128225, 1, 7, 0, 0, 0, 9, 0 },
  { "talent_amplification_core", 495, "talent", nullptr, 0, nullptr, 0, 117471, 1, 7, 0, 0, 0, 9, 0 },
  { "talent_oversurge", 496, "talent", nullptr, 0, nullptr, 0, 125823, 1, 7, 0, 0, 0, 9, 0 },
  { "talent_pulse_capacitor", 497, "talent", nullptr, 0, nullptr, 0, 117463, 1, 7, 0, 0, 0, 9, 0 },
  { "talent_supportive_imbuements", 498, "talent", nullptr, 0, nullptr, 0, 125824, 1, 7, 0, 0, 0, 9, 0 },
  { "talent_totemic_coordination", 499, "talent", nullptr, 0, nullptr, 0, 117478, 1, 7, 0, 0, 0, 9, 0 },
  { "talent_earthsurge", 500, "talent", nullptr, 0, nullptr, 0, 125822, 1, 7, 0, 0, 0, 9, 0 },
  { "hero_stormbringer", 501, "talent", nullptr, 0, nullptr, 0, 117489, 1, 7, 0, 0, 0, 9, 1 },
  { "hero_totemic", 502, "talent", nullptr, 0, nullptr, 0, 117474, 1, 7, 0, 0, 0, 10, 1 },
  { "tempest_venomfang", 503, "talent", nullptr, 0, nullptr, 0, 117489, 1, 7, 1, 0, 0, 11, 0 },
  { "tempest_burning_core", 504, "talent", nullptr, 0, nullptr, 0, 117489, 1, 8, 1, 0, 0, 11, 0 },
  { "tempest_rune_lingering", 505, "talent", nullptr, 0, nullptr, 0, 117489, 1, 9, 1, 0, 0, 11, 0 },
  { "voltaic_blaze_venomfang", 506, "talent", nullptr, 0, nullptr, 0, 101819, 1, 10, 1, 0, 0, 11, 0 },
  { "voltaic_blaze_burning_core", 507, "talent", nullptr, 0, nullptr, 0, 101819, 1, 11, 1, 0, 0, 11, 0 },
  { "voltaic_blaze_rune_lingering", 508, "talent", nullptr, 0, nullptr, 0, 101819, 1, 12, 1, 0, 0, 11, 0 },
  { "voltaic_blaze_stormbringer", 509, "talent", nullptr, 0, nullptr, 0, 101819, 1, 13, 1, 0, 0, 11, 0 },
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

inline constexpr std::size_t RL_CAPABILITY_GOVERNED_SLOTS_COUNT = 0;
inline constexpr std::size_t RL_CAPABILITY_GOVERNED_SLOTS[] = { 0 };

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
inline constexpr const char* RL_NET_LAYOUT_SHA = "rl-layout-v1:4587afe55ea7c1e31c89f12d1c372203440bcae1aeeac8700eb83c35f19a5c36";

// ---- Target scorer feature list (Phase 230-02, SCOR-01) ----

inline constexpr std::size_t RL_TARGET_SLOTS = 16;
inline constexpr std::size_t RL_TARGET_FEATURES = 25;
inline constexpr const char* RL_TARGET_FEATURE_SHA = "tgt-feat-v1:1b477f658ea5fc48ee74f09935093d8a8250169cd549130608223cf3dd093355";

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
  "venomfang_debuff_stacks",
  "venomfang_debuff_remaining",
  "rune_of_unleashed_fire_lingering_remaining",
  "chain_hop_count",
  "vb_new_flame_shocks_within_10yd",
  "lava_lash_spread_within_12yd",
  "flametongue_attack_remaining",
  "lashing_flames_remaining",
  "marked_for_execution_remaining",
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
  X(venomfang_debuff_stacks) \
  X(venomfang_debuff_remaining) \
  X(rune_of_unleashed_fire_lingering_remaining) \
  X(chain_hop_count) \
  X(vb_new_flame_shocks_within_10yd) \
  X(lava_lash_spread_within_12yd) \
  X(flametongue_attack_remaining) \
  X(lashing_flames_remaining) \
  X(marked_for_execution_remaining)

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
  { "burning_core_remaining", rl_kind::k_seconds, false, 1.0, true, 6.0, -1 },
  { "lightning_rod_stacks", rl_kind::k_int, true, 1.0, false, 1.0, -1 },
  { "lightning_rod_remaining", rl_kind::k_seconds, false, 1.0, true, 8.0, -1 },
  { "venomfang_debuff_stacks", rl_kind::k_int, true, 20.0, false, 1.0, -1 },
  { "venomfang_debuff_remaining", rl_kind::k_seconds, false, 1.0, true, 8.0, -1 },
  { "rune_of_unleashed_fire_lingering_remaining", rl_kind::k_seconds, false, 1.0, true, 10.0, -1 },
  { "chain_hop_count", rl_kind::k_seconds, false, 1.0, true, 5.0, -1 },
  { "vb_new_flame_shocks_within_10yd", rl_kind::k_seconds, false, 1.0, true, 6.0, -1 },
  { "lava_lash_spread_within_12yd", rl_kind::k_seconds, false, 1.0, true, 5.0, -1 },
  { "flametongue_attack_remaining", rl_kind::k_seconds, false, 1.0, true, 1.0, -1 },
  { "lashing_flames_remaining", rl_kind::k_seconds, false, 1.0, true, 20.0, -1 },
  { "marked_for_execution_remaining", rl_kind::k_seconds, false, 1.0, true, 20.0, -1 },
};

inline constexpr std::size_t RL_AIM_CONTEXT_COUNT = 7;
inline constexpr std::size_t RL_AIM_CONTEXT_OBS_SLOTS[RL_AIM_CONTEXT_COUNT] = { 448, 450, 454, 60, 29, 98, 456 };
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

inline constexpr std::size_t RL_AIM_INPUT_COUNT = 32;
inline constexpr const char* RL_AIM_SHA = "aim-v1:35c7584837a6cc69be489e858e400dfda338c9b062d53756bc835665e8037d78";


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

// The gate class of a discovered hit row (261028-fzl): unit and shape are a button's, proc is a proc, pet or carrier-sum member's; none is a class
// provider or a legacy census row. A discovered row's class is also in the gate-class tables that follow the hit-provider table (unit: needs a unit).
enum class rl_hit_gate { none, unit, shape, proc };

struct rl_hit_provider
{
  const char*  family;
  const char*  member;
  const char*  provider;
  int          centre_action;  // the action id of the cast token whose stamped pick centres the count; -1: the engine fallback (chosen enemy, else target)
  rl_hit_gate  gate;
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

inline constexpr std::size_t RL_DECLARED_FACT_COUNT = 10;
inline constexpr rl_declared_fact RL_DECLARED_FACTS[RL_DECLARED_FACT_COUNT] = {
  { rl_fact_kind::dot_remaining, "flame_shock", 10 },
  { rl_fact_kind::dot_remaining, "burning_core", 13 },
  { rl_fact_kind::debuff_stacks, "lightning_rod", 14 },
  { rl_fact_kind::dot_remaining, "lightning_rod", 15 },
  { rl_fact_kind::debuff_stacks, "venomfang_debuff", 16 },
  { rl_fact_kind::dot_remaining, "venomfang_debuff", 17 },
  { rl_fact_kind::dot_remaining, "rune_of_unleashed_fire_lingering", 18 },
  { rl_fact_kind::dot_remaining, "flametongue_attack", 22 },
  { rl_fact_kind::dot_remaining, "lashing_flames", 23 },
  { rl_fact_kind::dot_remaining, "marked_for_execution", 24 },
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

inline constexpr std::size_t RL_HIT_PROVIDER_COUNT = 19;
inline constexpr rl_hit_provider RL_HIT_PROVIDERS[RL_HIT_PROVIDER_COUNT] = {
  { "hits", "chain_lightning", "census.chain_lightning", 2, rl_hit_gate::unit },
  { "hits", "tempest", "census.tempest", 4, rl_hit_gate::unit },
  { "hits", "crash_lightning", "census.crash_lightning", 6, rl_hit_gate::unit },
  { "hits", "lava_lash", "census.lava_lash", 7, rl_hit_gate::unit },
  { "hits", "voltaic_blaze", "census.voltaic_blaze", 8, rl_hit_gate::unit },
  { "hits", "sundering", "census.sundering", 17, rl_hit_gate::shape },
  { "hits", "primordial_storm", "census.primordial_storm", 18, rl_hit_gate::shape },
  { "hits", "ascendance_damage", "census.action.ascendance_damage", 9, rl_hit_gate::proc },
  { "hits", "chain_lightning_ps", "census.action.chain_lightning_ps", -1, rl_hit_gate::proc },
  { "hits", "crash_lightning_proc", "census.action.crash_lightning_proc", 0, rl_hit_gate::proc },
  { "hits", "doom_winds_damage", "census.action.doom_winds_damage", -1, rl_hit_gate::proc },
  { "hits", "fire_nova", "census.action.fire_nova", 8, rl_hit_gate::proc },
  { "hits", "flametongue_attack_imbuement_mastery", "census.action.flametongue_attack_imbuement_mastery", -1, rl_hit_gate::proc },
  { "hits", "perfected_guillotine", "census.action.perfected_guillotine", -1, rl_hit_gate::proc },
  { "hits", "searing_volley", "census.pet.searing_totem.searing_volley", -1, rl_hit_gate::proc },
  { "hits", "tremor", "census.pet.surging_totem.tremor", -1, rl_hit_gate::proc },
  { "hits", "venomfang_burst", "census.action.venomfang_burst", -1, rl_hit_gate::proc },
  { "hits", "lava_lash.flame_shock_spread", "shaman.lava_lash_flame_shock_spread", -1, rl_hit_gate::none },
  { "hits", "voltaic_blaze.new_flame_shocks", "shaman.voltaic_blaze_new_flame_shocks", -1, rl_hit_gate::none },
};

// The gate class of each census hit row, parallel to RL_HIT_PROVIDERS (a class-provider row reads false). NEEDS_UNIT true: the press
// needs a unit and the game's range check gates it; false: it needs no unit and is legal only with an enemy's hitbox inside its
// own shape. CLASS_OVERRIDDEN true: the class is the owner's recorded word, so the engine takes it instead of its own reading.
#define RL_HIT_ROW_NEEDS_UNIT_DEFINED 1
inline constexpr bool RL_HIT_ROW_NEEDS_UNIT[RL_HIT_PROVIDER_COUNT] = { true, true, true, true, true, false, false, false, false, false, false, false, false, false, false, false, false, false, false };
inline constexpr bool RL_HIT_ROW_CLASS_OVERRIDDEN[RL_HIT_PROVIDER_COUNT] = { false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false };

// The no-unit reach gate (271.7 class A). A cast button whose RL_ACTION_NEEDS_MELEE_REACH entry (indexed by action id; a wait reads
// false) is true needs no unit and has no hit shape; the simulator offers it only while some enemy's hitbox is within its melee reach
// (rl_target_select::MELEE_RANGE_YARDS plus the enemy's combat reach). The table is the census writer's own answer
// (needsMeleeReach); the engine's bind-time check compares it with its own reading.
#define RL_ACTION_NEEDS_MELEE_REACH_DEFINED 1
inline constexpr bool RL_ACTION_NEEDS_MELEE_REACH[RL_ACTION_DIM] = { false, false, false, false, false, false, false, false, false, true, true, true, false, false, false, true, true, false, false, true, true, false };

// The census node each hit row counts, parallel to RL_HIT_PROVIDERS (261013-aimx, class B). The engine counts that node BY NAME, never the node
// its own live classification would pick (that classification never runs the fight-end proc marking, so it can name a proc node as the
// button's count). RL_HIT_ROW_CONDITIONAL is true for a row that counts what a conditional proc node of its button would hit if it fires.
#define RL_HIT_ROW_NODE_DEFINED 1
inline constexpr const char* RL_HIT_ROW_NODE[RL_HIT_PROVIDER_COUNT] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "ascendance_damage", "chain_lightning_ps", "crash_lightning_proc", "doom_winds_damage", "fire_nova", "flametongue_attack_imbuement_mastery", "perfected_guillotine", "searing_volley", "tremor", "venomfang_burst", nullptr, nullptr };
inline constexpr bool RL_HIT_ROW_CONDITIONAL[RL_HIT_PROVIDER_COUNT] = { false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false };
