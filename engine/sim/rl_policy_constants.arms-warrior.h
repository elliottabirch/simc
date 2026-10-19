// ==============================================================================
// GENERATED FILE -- DO NOT EDIT (XPORT-04, D-09).
//
// Produced by:      scripts/rl/gen_rl_constants.py
// Registry source:  data/specs/arms-warrior.json
// Spec id:          'arms-warrior'
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
// because each capability's own counts differ (one capability may govern many
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

inline constexpr const char* RL_REGISTRY_ID = "arms-warrior";
inline constexpr const char* RL_ACTOR_NAME = "RL_Arms_Warrior";
inline constexpr int RL_ENCODER_VERSION = 6;
inline constexpr std::size_t RL_OBS_DIM = 506;
inline constexpr std::size_t RL_ACTION_DIM = 18;
inline constexpr double RL_EPISODE_MAX_TIME = 300.0;
inline constexpr double RL_WAIT_FLOOR_SECONDS = 0.05;
inline constexpr double RL_PERMANENT_SATURATION = 1.0;
inline constexpr const char* RL_OBS_SCHEMA_SHA = "rl-obs-v6:8fc038b781af0ecbde63d06757c2e32b0ce7311ab6043b2e45c8cc1af561336e";
inline constexpr const char* RL_MASK_RULES_SHA = "403fa658aabd986da04a24ad88c6625ad5a7fba10f3f6437922af21582877da5";
inline constexpr const char* RL_ACTION_SPACE_SHA = "3dd9a025d3f4dc44fa4eab8586f6360aa28acf3df73322535d908fcccff36327";

// ---- Observation name list (the materialised ordering) ----

inline constexpr const char* RL_OBS_NAMES[RL_OBS_DIM] = {
  "player_buffs.arcanoweave_insight.remains",
  "player_buffs.avatar.remains",
  "player_buffs.battlelord.remains",
  "player_buffs.bladestorm.remains",
  "player_buffs.bloodlust.remains",
  "player_buffs.brutal_finish.remains",
  "player_buffs.celeritous_conclusion_crit.remains",
  "player_buffs.celeritous_conclusion_haste.remains",
  "player_buffs.collateral_damage.stacks",
  "player_buffs.collateral_damage.remains",
  "player_buffs.colossal_might.stacks",
  "player_buffs.colossal_might.remains",
  "player_buffs.crushing_combo.stacks",
  "player_buffs.crushing_combo.remains",
  "player_buffs.cut_to_the_bone.remains",
  "player_buffs.devoured_strength.stacks",
  "player_buffs.devoured_strength.remains",
  "player_buffs.executioner.stacks",
  "player_buffs.executioner.remains",
  "player_buffs.executioner.next_expiry",
  "player_buffs.executioner.expiry_2",
  "player_buffs.executioner.expiry_3",
  "player_buffs.executioner.stack_seconds",
  "player_buffs.executioners_precision.stacks",
  "player_buffs.executioners_precision.remains",
  "player_buffs.exhaustion.remains",
  "player_buffs.fierce_followthrough.remains",
  "player_buffs.heroic_might_accumulator.stacks",
  "player_buffs.heroic_might_accumulator.remains",
  "player_buffs.imminent_demise.stacks",
  "player_buffs.imminent_demise.remains",
  "player_buffs.martial_prowess.stacks",
  "player_buffs.martial_prowess.remains",
  "player_buffs.master_of_warfare.stacks",
  "player_buffs.master_of_warfare.remains",
  "player_buffs.master_of_warfare.next_expiry",
  "player_buffs.master_of_warfare.expiry_2",
  "player_buffs.master_of_warfare.expiry_3",
  "player_buffs.master_of_warfare.stack_seconds",
  "player_buffs.master_of_warfare_proc.remains",
  "player_buffs.opportunist.stacks",
  "player_buffs.opportunist.remains",
  "player_buffs.sudden_death.stacks",
  "player_buffs.sudden_death.remains",
  "player_buffs.sweeping_strikes.stacks",
  "player_buffs.sweeping_strikes.remains",
  "player_buffs.venomcursed_critical_strike.remains",
  "player_buffs.violent_euphoria.remains",
  "player_buffs.voracious_heart_of_ulatek.remains",
  "player_buffs.wild_strikes.remains",
  "player_buffs.winding_up.stacks",
  "player_buffs.winding_up.remains",
  "cooldowns.avatar.remains",
  "cooldowns.bladestorm.remains",
  "cooldowns.cleave.remains",
  "cooldowns.colossus_smash.remains",
  "cooldowns.demolish.remains",
  "cooldowns.mortal_strike.remains",
  "cooldowns.overpower.charges_fractional",
  "cooldowns.overpower.charges",
  "cooldowns.overpower.recharge_time",
  "cooldowns.overpower.full_recharge_time",
  "cooldowns.ravager.remains",
  "cooldowns.storm_bolt.remains",
  "cooldowns.sweeping_strikes.remains",
  "cooldowns.voracious_heart_of_ulatek_1297761.remains",
  "cooldowns.wrecking_throw.remains",
  "target_facts.cleave.distance",
  "target_facts.cleave.in_reach",
  "target_facts.cleave.in_range",
  "target_facts.cleave.in_front",
  "target_facts.cleave.alive",
  "target_facts.cleave.immune",
  "target_facts.cleave.immunity_remaining",
  "target_facts.cleave.time_to_die",
  "target_facts.cleave.health_pct",
  "target_facts.cleave.is_boss",
  "target_facts.cleave.neighbours_within_radius",
  "target_facts.cleave.is_current_target",
  "target_facts.cleave.rend_remaining",
  "target_facts.cleave.deep_wounds_remaining",
  "target_facts.cleave.colossus_smash_remaining",
  "target_facts.cleave.fatal_mark_stacks",
  "target_facts.cleave.overwhelmed_stacks",
  "target_facts.cleave.ravaged_remaining",
  "target_facts.cleave.venomfang_debuff_stacks",
  "target_facts.cleave.wrecked_stacks",
  "target_facts.colossus_smash.distance",
  "target_facts.colossus_smash.in_reach",
  "target_facts.colossus_smash.in_range",
  "target_facts.colossus_smash.in_front",
  "target_facts.colossus_smash.alive",
  "target_facts.colossus_smash.immune",
  "target_facts.colossus_smash.immunity_remaining",
  "target_facts.colossus_smash.time_to_die",
  "target_facts.colossus_smash.health_pct",
  "target_facts.colossus_smash.is_boss",
  "target_facts.colossus_smash.neighbours_within_radius",
  "target_facts.colossus_smash.is_current_target",
  "target_facts.colossus_smash.rend_remaining",
  "target_facts.colossus_smash.deep_wounds_remaining",
  "target_facts.colossus_smash.colossus_smash_remaining",
  "target_facts.colossus_smash.fatal_mark_stacks",
  "target_facts.colossus_smash.overwhelmed_stacks",
  "target_facts.colossus_smash.ravaged_remaining",
  "target_facts.colossus_smash.venomfang_debuff_stacks",
  "target_facts.colossus_smash.wrecked_stacks",
  "target_facts.demolish.distance",
  "target_facts.demolish.in_reach",
  "target_facts.demolish.in_range",
  "target_facts.demolish.in_front",
  "target_facts.demolish.alive",
  "target_facts.demolish.immune",
  "target_facts.demolish.immunity_remaining",
  "target_facts.demolish.time_to_die",
  "target_facts.demolish.health_pct",
  "target_facts.demolish.is_boss",
  "target_facts.demolish.neighbours_within_radius",
  "target_facts.demolish.is_current_target",
  "target_facts.demolish.rend_remaining",
  "target_facts.demolish.deep_wounds_remaining",
  "target_facts.demolish.colossus_smash_remaining",
  "target_facts.demolish.fatal_mark_stacks",
  "target_facts.demolish.overwhelmed_stacks",
  "target_facts.demolish.ravaged_remaining",
  "target_facts.demolish.venomfang_debuff_stacks",
  "target_facts.demolish.wrecked_stacks",
  "target_facts.execute.distance",
  "target_facts.execute.in_reach",
  "target_facts.execute.in_range",
  "target_facts.execute.in_front",
  "target_facts.execute.alive",
  "target_facts.execute.immune",
  "target_facts.execute.immunity_remaining",
  "target_facts.execute.time_to_die",
  "target_facts.execute.health_pct",
  "target_facts.execute.is_boss",
  "target_facts.execute.neighbours_within_radius",
  "target_facts.execute.is_current_target",
  "target_facts.execute.rend_remaining",
  "target_facts.execute.deep_wounds_remaining",
  "target_facts.execute.colossus_smash_remaining",
  "target_facts.execute.fatal_mark_stacks",
  "target_facts.execute.overwhelmed_stacks",
  "target_facts.execute.ravaged_remaining",
  "target_facts.execute.venomfang_debuff_stacks",
  "target_facts.execute.wrecked_stacks",
  "target_facts.heroic_strike.distance",
  "target_facts.heroic_strike.in_reach",
  "target_facts.heroic_strike.in_range",
  "target_facts.heroic_strike.in_front",
  "target_facts.heroic_strike.alive",
  "target_facts.heroic_strike.immune",
  "target_facts.heroic_strike.immunity_remaining",
  "target_facts.heroic_strike.time_to_die",
  "target_facts.heroic_strike.health_pct",
  "target_facts.heroic_strike.is_boss",
  "target_facts.heroic_strike.neighbours_within_radius",
  "target_facts.heroic_strike.is_current_target",
  "target_facts.heroic_strike.rend_remaining",
  "target_facts.heroic_strike.deep_wounds_remaining",
  "target_facts.heroic_strike.colossus_smash_remaining",
  "target_facts.heroic_strike.fatal_mark_stacks",
  "target_facts.heroic_strike.overwhelmed_stacks",
  "target_facts.heroic_strike.ravaged_remaining",
  "target_facts.heroic_strike.venomfang_debuff_stacks",
  "target_facts.heroic_strike.wrecked_stacks",
  "target_facts.mortal_strike.distance",
  "target_facts.mortal_strike.in_reach",
  "target_facts.mortal_strike.in_range",
  "target_facts.mortal_strike.in_front",
  "target_facts.mortal_strike.alive",
  "target_facts.mortal_strike.immune",
  "target_facts.mortal_strike.immunity_remaining",
  "target_facts.mortal_strike.time_to_die",
  "target_facts.mortal_strike.health_pct",
  "target_facts.mortal_strike.is_boss",
  "target_facts.mortal_strike.neighbours_within_radius",
  "target_facts.mortal_strike.is_current_target",
  "target_facts.mortal_strike.rend_remaining",
  "target_facts.mortal_strike.deep_wounds_remaining",
  "target_facts.mortal_strike.colossus_smash_remaining",
  "target_facts.mortal_strike.fatal_mark_stacks",
  "target_facts.mortal_strike.overwhelmed_stacks",
  "target_facts.mortal_strike.ravaged_remaining",
  "target_facts.mortal_strike.venomfang_debuff_stacks",
  "target_facts.mortal_strike.wrecked_stacks",
  "target_facts.overpower.distance",
  "target_facts.overpower.in_reach",
  "target_facts.overpower.in_range",
  "target_facts.overpower.in_front",
  "target_facts.overpower.alive",
  "target_facts.overpower.immune",
  "target_facts.overpower.immunity_remaining",
  "target_facts.overpower.time_to_die",
  "target_facts.overpower.health_pct",
  "target_facts.overpower.is_boss",
  "target_facts.overpower.neighbours_within_radius",
  "target_facts.overpower.is_current_target",
  "target_facts.overpower.rend_remaining",
  "target_facts.overpower.deep_wounds_remaining",
  "target_facts.overpower.colossus_smash_remaining",
  "target_facts.overpower.fatal_mark_stacks",
  "target_facts.overpower.overwhelmed_stacks",
  "target_facts.overpower.ravaged_remaining",
  "target_facts.overpower.venomfang_debuff_stacks",
  "target_facts.overpower.wrecked_stacks",
  "target_facts.rend.distance",
  "target_facts.rend.in_reach",
  "target_facts.rend.in_range",
  "target_facts.rend.in_front",
  "target_facts.rend.alive",
  "target_facts.rend.immune",
  "target_facts.rend.immunity_remaining",
  "target_facts.rend.time_to_die",
  "target_facts.rend.health_pct",
  "target_facts.rend.is_boss",
  "target_facts.rend.neighbours_within_radius",
  "target_facts.rend.is_current_target",
  "target_facts.rend.rend_remaining",
  "target_facts.rend.deep_wounds_remaining",
  "target_facts.rend.colossus_smash_remaining",
  "target_facts.rend.fatal_mark_stacks",
  "target_facts.rend.overwhelmed_stacks",
  "target_facts.rend.ravaged_remaining",
  "target_facts.rend.venomfang_debuff_stacks",
  "target_facts.rend.wrecked_stacks",
  "target_facts.slam.distance",
  "target_facts.slam.in_reach",
  "target_facts.slam.in_range",
  "target_facts.slam.in_front",
  "target_facts.slam.alive",
  "target_facts.slam.immune",
  "target_facts.slam.immunity_remaining",
  "target_facts.slam.time_to_die",
  "target_facts.slam.health_pct",
  "target_facts.slam.is_boss",
  "target_facts.slam.neighbours_within_radius",
  "target_facts.slam.is_current_target",
  "target_facts.slam.rend_remaining",
  "target_facts.slam.deep_wounds_remaining",
  "target_facts.slam.colossus_smash_remaining",
  "target_facts.slam.fatal_mark_stacks",
  "target_facts.slam.overwhelmed_stacks",
  "target_facts.slam.ravaged_remaining",
  "target_facts.slam.venomfang_debuff_stacks",
  "target_facts.slam.wrecked_stacks",
  "target_facts.storm_bolt.distance",
  "target_facts.storm_bolt.in_reach",
  "target_facts.storm_bolt.in_range",
  "target_facts.storm_bolt.in_front",
  "target_facts.storm_bolt.alive",
  "target_facts.storm_bolt.immune",
  "target_facts.storm_bolt.immunity_remaining",
  "target_facts.storm_bolt.time_to_die",
  "target_facts.storm_bolt.health_pct",
  "target_facts.storm_bolt.is_boss",
  "target_facts.storm_bolt.neighbours_within_radius",
  "target_facts.storm_bolt.is_current_target",
  "target_facts.storm_bolt.rend_remaining",
  "target_facts.storm_bolt.deep_wounds_remaining",
  "target_facts.storm_bolt.colossus_smash_remaining",
  "target_facts.storm_bolt.fatal_mark_stacks",
  "target_facts.storm_bolt.overwhelmed_stacks",
  "target_facts.storm_bolt.ravaged_remaining",
  "target_facts.storm_bolt.venomfang_debuff_stacks",
  "target_facts.storm_bolt.wrecked_stacks",
  "target_facts.wrecking_throw.distance",
  "target_facts.wrecking_throw.in_reach",
  "target_facts.wrecking_throw.in_range",
  "target_facts.wrecking_throw.in_front",
  "target_facts.wrecking_throw.alive",
  "target_facts.wrecking_throw.immune",
  "target_facts.wrecking_throw.immunity_remaining",
  "target_facts.wrecking_throw.time_to_die",
  "target_facts.wrecking_throw.health_pct",
  "target_facts.wrecking_throw.is_boss",
  "target_facts.wrecking_throw.neighbours_within_radius",
  "target_facts.wrecking_throw.is_current_target",
  "target_facts.wrecking_throw.rend_remaining",
  "target_facts.wrecking_throw.deep_wounds_remaining",
  "target_facts.wrecking_throw.colossus_smash_remaining",
  "target_facts.wrecking_throw.fatal_mark_stacks",
  "target_facts.wrecking_throw.overwhelmed_stacks",
  "target_facts.wrecking_throw.ravaged_remaining",
  "target_facts.wrecking_throw.venomfang_debuff_stacks",
  "target_facts.wrecking_throw.wrecked_stacks",
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
  "immunity_in",
  "immunity_remaining",
  "longest_time_to_die",
  "nearest_enemy_distance",
  "soonest_time_to_die",
  "fight.next_wave.in",
  "fight.next_wave.count",
  "fight.next_wave.lifetime",
  "fight.next_phase.at_boss_pct",
  "fight.next_phase.boss_pct_now",
  "fight.downtime.in",
  "fight.bloodlust.in",
  "resource.rage",
  "hits.bladestorm",
  "hits.cleave",
  "hits.cleave_reap_the_storm_cleave",
  "hits.colossus_smash",
  "hits.demolish",
  "hits.execute",
  "hits.execute_reap_the_storm_execute",
  "hits.heroic_strike",
  "hits.mortal_strike",
  "hits.overpower",
  "hits.ravager",
  "hits.rend",
  "hits.slam",
  "hits.slam_heroic_strike",
  "aggregates.rend_lacking_count",
  "aggregates.rend_lowest_remaining",
  "aggregates.execute_passing_count",
  "aggregates.execute_lowest_health",
  "capability.talent_fast_footwork",
  "capability.talent_war_machine",
  "capability.talent_thunder_clap",
  "capability.talent_leeching_strikes",
  "capability.talent_impending_victory",
  "capability.talent_heroic_leap",
  "capability.talent_crackling_thunder",
  "capability.talent_storm_bolt",
  "capability.talent_rend",
  "capability.talent_second_wind",
  "capability.talent_frothing_berserker",
  "capability.talent_bounding_stride",
  "capability.talent_pain_and_gain",
  "capability.talent_intervene",
  "capability.talent_interpose",
  "capability.talent_shockwave",
  "capability.talent_overwhelming_rage",
  "capability.talent_rallying_cry",
  "capability.talent_field_dressing",
  "capability.talent_spell_reflection",
  "capability.talent_wrecking_throw",
  "capability.talent_shattering_throw",
  "capability.talent_rumbling_earth",
  "capability.talent_berserker_shout",
  "capability.talent_fearless",
  "capability.talent_intimidating_shout",
  "capability.talent_piercing_howl",
  "capability.talent_honed_reflexes",
  "capability.talent_armored_to_the_teeth",
  "capability.talent_armored_to_the_teeth_r2",
  "capability.talent_reinforced_plates",
  "capability.talent_reinforced_plates_r2",
  "capability.talent_double_time",
  "capability.talent_barbaric_training",
  "capability.talent_javelineer",
  "capability.talent_resonant_voice",
  "capability.talent_crushing_force",
  "capability.talent_cruel_strikes",
  "capability.talent_cruel_strikes_r2",
  "capability.talent_twohanded_weapon_specialization",
  "capability.talent_twohanded_weapon_specialization_r2",
  "capability.talent_wild_strikes",
  "capability.talent_wild_strikes_r2",
  "capability.talent_anger_management",
  "capability.talent_champions_spear",
  "capability.talent_stance_mastery",
  "capability.talent_battlefield_commander",
  "capability.talent_mortal_strike",
  "capability.talent_overpower",
  "capability.talent_sudden_death",
  "capability.talent_fueled_by_violence",
  "capability.talent_ignore_pain",
  "capability.talent_die_by_the_sword",
  "capability.talent_bloodsurge",
  "capability.talent_improved_overpower",
  "capability.talent_improved_execute",
  "capability.talent_fervor_of_battle",
  "capability.talent_tactician",
  "capability.talent_colossus_smash",
  "capability.talent_impale",
  "capability.talent_brute_force",
  "capability.talent_efficiency",
  "capability.talent_overpowering_finish",
  "capability.talent_mass_execution",
  "capability.talent_strength_of_arms",
  "capability.talent_strength_of_arms_r2",
  "capability.talent_just_warming_up",
  "capability.talent_broad_strokes",
  "capability.talent_sharpened_blades",
  "capability.talent_sharpened_blades_r2",
  "capability.talent_cleave",
  "capability.talent_powerful_momentum",
  "capability.talent_martial_prowess",
  "capability.talent_dreadnaught",
  "capability.talent_deep_wounds",
  "capability.talent_tactical_edge",
  "capability.talent_crushing_combo",
  "capability.talent_massacre",
  "capability.talent_collateral_damage",
  "capability.talent_bloodborne",
  "capability.talent_bloodborne_r2",
  "capability.talent_bladestorm",
  "capability.talent_ravager",
  "capability.talent_critical_thinking",
  "capability.talent_critical_thinking_r2",
  "capability.talent_master_tactician",
  "capability.talent_bloodletting",
  "capability.talent_executioners_precision",
  "capability.talent_fatality",
  "capability.talent_battlelord",
  "capability.talent_mortal_wounds",
  "capability.talent_avatar",
  "capability.talent_master_of_warfare",
  "capability.talent_master_of_warfare_r2",
  "capability.talent_master_of_warfare_r3",
  "capability.talent_master_of_warfare_r4",
  "capability.talent_martial_expert",
  "capability.talent_colossal_might",
  "capability.talent_boneshaker",
  "capability.talent_earthquaker",
  "capability.talent_decimator",
  "capability.talent_one_against_many",
  "capability.talent_arterial_bleed",
  "capability.talent_tide_of_battle",
  "capability.talent_no_stranger_to_pain",
  "capability.talent_veteran_vitality",
  "capability.talent_cut_to_the_bone",
  "capability.talent_practiced_strikes",
  "capability.talent_precise_might",
  "capability.talent_mountain_of_muscle_and_scars",
  "capability.talent_celeritous_conclusion",
  "capability.talent_dominance_of_the_colossus",
  "capability.talent_imminent_demise",
  "capability.talent_overwhelming_blades",
  "capability.talent_relentless_pursuit",
  "capability.talent_vicious_agility",
  "capability.talent_violent_euphoria",
  "capability.talent_death_drive",
  "capability.talent_culling_cyclone",
  "capability.talent_brutal_finish",
  "capability.talent_fierce_followthrough",
  "capability.talent_opportunist",
  "capability.talent_deadly_focus",
  "capability.talent_show_no_mercy",
  "capability.talent_reap_the_storm",
  "capability.talent_slayers_malice",
  "capability.talent_unhinged",
  "capability.talent_unrelenting_onslaught",
  "capability.hero_colossus",
  "capability.hero_slayer",
  "capability.trinket_voracious_heart_of_ulatek",
  "capability.enchant_arcane_mastery",
  "capability.enchant_berserkers_rage",
  "capability.embellishment_arcanoweave_lining",
  "capability.embellishment_hunters_ritual_stone",
  "capability.item_venomcursed_mastery",
  "capability.item_venomcursed_ascendance",
  "capability.set_bite_of_zuljan_2pc",
  "capability.mode_funnel",
};

// ---- Per-family observation tables ----

inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_0[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_1[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_2[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_3[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_4[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 40.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_5[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_6[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_7[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_8[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 3.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_9[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 24.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_10[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_11[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_12[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 25.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_13[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "next_expiry", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "expiry_2", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "expiry_3", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "stack_seconds", rl_kind::k_seconds, 0.0, false, 1.0, true, 100.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_14[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_15[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 600.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_16[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_17[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_18[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 3.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_19[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 3.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_20[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "next_expiry", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "expiry_2", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "expiry_3", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
    { "stack_seconds", rl_kind::k_seconds, 0.0, false, 1.0, true, 300.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_21[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_22[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_23[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_24[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 18.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_25[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_26[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 8.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_27[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_28[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_29[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_PLAYER_BUFFS_MEMBERS[] = {
  { "arcanoweave_insight", "arcanoweave_insight", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_0, 1 },
  { "avatar", "avatar", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_1, 1 },
  { "battlelord", "battlelord", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_2, 1 },
  { "bladestorm", "bladestorm", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_3, 1 },
  { "bloodlust", "bloodlust", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_4, 1 },
  { "brutal_finish", "brutal_finish", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_5, 1 },
  { "celeritous_conclusion_crit", "celeritous_conclusion_crit", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_6, 1 },
  { "celeritous_conclusion_haste", "celeritous_conclusion_haste", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_7, 1 },
  { "collateral_damage", "collateral_damage", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_8, 2 },
  { "colossal_might", "colossal_might", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_9, 2 },
  { "crushing_combo", "crushing_combo", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_10, 2 },
  { "cut_to_the_bone", "cut_to_the_bone", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_11, 1 },
  { "devoured_strength", "devoured_strength", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_12, 2 },
  { "executioner", "executioner", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_13, 6 },
  { "executioners_precision", "executioners_precision", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_14, 2 },
  { "exhaustion", "exhaustion", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_15, 1 },
  { "fierce_followthrough", "fierce_followthrough", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_16, 1 },
  { "heroic_might_accumulator", "heroic_might_accumulator", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_17, 2 },
  { "imminent_demise", "imminent_demise", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_18, 2 },
  { "martial_prowess", "martial_prowess", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_19, 2 },
  { "master_of_warfare", "master_of_warfare", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_20, 6 },
  { "master_of_warfare_proc", "master_of_warfare_proc", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_21, 1 },
  { "opportunist", "opportunist", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_22, 2 },
  { "sudden_death", "sudden_death", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_23, 2 },
  { "sweeping_strikes", "sweeping_strikes", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_24, 2 },
  { "venomcursed_critical_strike", "venomcursed_critical_strike", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_25, 1 },
  { "violent_euphoria", "violent_euphoria", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_26, 1 },
  { "voracious_heart_of_ulatek", "voracious_heart_of_ulatek", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_27, 1 },
  { "wild_strikes", "wild_strikes", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_28, 1 },
  { "winding_up", "winding_up", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_29, 2 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_0[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 90.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_1[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 90.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_2[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 5.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_3[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 45.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_4[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_5[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 6.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_6[] = {
    { "charges_fractional", rl_kind::k_float, 2.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "charges", rl_kind::k_int, 2.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "recharge_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "full_recharge_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_7[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 90.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_8[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_9[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_10[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 90.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_11[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 45.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_COOLDOWNS_MEMBERS[] = {
  { "avatar", "avatar", RL_OBS_FAMILY_COOLDOWNS_LEAVES_0, 1 },
  { "bladestorm", "bladestorm", RL_OBS_FAMILY_COOLDOWNS_LEAVES_1, 1 },
  { "cleave", "cleave", RL_OBS_FAMILY_COOLDOWNS_LEAVES_2, 1 },
  { "colossus_smash", "colossus_smash", RL_OBS_FAMILY_COOLDOWNS_LEAVES_3, 1 },
  { "demolish", "demolish", RL_OBS_FAMILY_COOLDOWNS_LEAVES_4, 1 },
  { "mortal_strike", "mortal_strike", RL_OBS_FAMILY_COOLDOWNS_LEAVES_5, 1 },
  { "overpower", "overpower", RL_OBS_FAMILY_COOLDOWNS_LEAVES_6, 4 },
  { "ravager", "ravager", RL_OBS_FAMILY_COOLDOWNS_LEAVES_7, 1 },
  { "storm_bolt", "storm_bolt", RL_OBS_FAMILY_COOLDOWNS_LEAVES_8, 1 },
  { "sweeping_strikes", "sweeping_strikes", RL_OBS_FAMILY_COOLDOWNS_LEAVES_9, 1 },
  { "voracious_heart_of_ulatek_1297761", "voracious_heart_of_ulatek_1297761", RL_OBS_FAMILY_COOLDOWNS_LEAVES_10, 1 },
  { "wrecking_throw", "wrecking_throw", RL_OBS_FAMILY_COOLDOWNS_LEAVES_11, 1 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_0[] = {
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "alive", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 16.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "rend_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "deep_wounds_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "colossus_smash_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "fatal_mark_stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "overwhelmed_stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "ravaged_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "wrecked_stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_1[] = {
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "alive", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 16.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "rend_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "deep_wounds_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "colossus_smash_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "fatal_mark_stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "overwhelmed_stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "ravaged_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "wrecked_stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_2[] = {
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "alive", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 16.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "rend_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "deep_wounds_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "colossus_smash_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "fatal_mark_stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "overwhelmed_stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "ravaged_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "wrecked_stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_3[] = {
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "alive", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 16.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "rend_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "deep_wounds_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "colossus_smash_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "fatal_mark_stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "overwhelmed_stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "ravaged_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "wrecked_stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_4[] = {
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "alive", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 16.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "rend_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "deep_wounds_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "colossus_smash_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "fatal_mark_stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "overwhelmed_stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "ravaged_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "wrecked_stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_5[] = {
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "alive", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 16.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "rend_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "deep_wounds_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "colossus_smash_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "fatal_mark_stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "overwhelmed_stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "ravaged_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "wrecked_stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_6[] = {
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "alive", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 16.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "rend_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "deep_wounds_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "colossus_smash_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "fatal_mark_stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "overwhelmed_stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "ravaged_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "wrecked_stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_7[] = {
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "alive", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 16.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "rend_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "deep_wounds_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "colossus_smash_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "fatal_mark_stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "overwhelmed_stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "ravaged_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "wrecked_stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_8[] = {
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "alive", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 16.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "rend_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "deep_wounds_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "colossus_smash_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "fatal_mark_stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "overwhelmed_stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "ravaged_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "wrecked_stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_9[] = {
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "alive", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 16.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "rend_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "deep_wounds_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "colossus_smash_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "fatal_mark_stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "overwhelmed_stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "ravaged_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "wrecked_stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_TARGET_FACTS_LEAVES_10[] = {
    { "distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "in_reach", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_range", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "in_front", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "alive", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immune", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "health_pct", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "is_boss", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "neighbours_within_radius", rl_kind::k_seconds, 0.0, false, 1.0, true, 16.0, nullptr, 0, false },
    { "is_current_target", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "rend_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "deep_wounds_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "colossus_smash_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "fatal_mark_stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "overwhelmed_stacks", rl_kind::k_int, 0.0, true, 5.0, false, 1.0, nullptr, 0, false },
    { "ravaged_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 12.0, nullptr, 0, false },
    { "venomfang_debuff_stacks", rl_kind::k_int, 0.0, true, 20.0, false, 1.0, nullptr, 0, false },
    { "wrecked_stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_TARGET_FACTS_MEMBERS[] = {
  { "cleave", "cleave", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_0, 20 },
  { "colossus_smash", "colossus_smash", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_1, 20 },
  { "demolish", "demolish", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_2, 20 },
  { "execute", "execute", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_3, 20 },
  { "heroic_strike", "heroic_strike", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_4, 20 },
  { "mortal_strike", "mortal_strike", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_5, 20 },
  { "overpower", "overpower", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_6, 20 },
  { "rend", "rend", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_7, 20 },
  { "slam", "slam", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_8, 20 },
  { "storm_bolt", "storm_bolt", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_9, 20 },
  { "wrecking_throw", "wrecking_throw", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_10, 20 },
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
inline constexpr rl_obs_member RL_OBS_FAMILY_SWING_CAST_MEMBERS[] = {
  { "auto_attack_interval", "auto_attack_interval", RL_OBS_FAMILY_SWING_CAST_LEAVES_0, 1 },
  { "gcd_length", "gcd_length", RL_OBS_FAMILY_SWING_CAST_LEAVES_1, 1 },
  { "swing_mh_remains", "swing_mh_remains", RL_OBS_FAMILY_SWING_CAST_LEAVES_2, 1 },
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
};

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
    { "immunity_in", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "immunity_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 600.0, nullptr, 0, false },
    { "longest_time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "nearest_enemy_distance", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "soonest_time_to_die", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "fight.next_wave.in", rl_kind::k_seconds, 600.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "fight.next_wave.count", rl_kind::k_seconds, 0.0, false, 1.0, true, 10.0, nullptr, 0, false },
    { "fight.next_wave.lifetime", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "fight.next_phase.at_boss_pct", rl_kind::k_seconds, 0.0, false, 1.0, true, 100.0, nullptr, 0, false },
    { "fight.next_phase.boss_pct_now", rl_kind::k_seconds, 0.0, false, 1.0, true, 100.0, nullptr, 0, false },
    { "fight.downtime.in", rl_kind::k_seconds, 600.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "fight.bloodlust.in", rl_kind::k_seconds, 600.0, false, 1.0, true, 60.0, nullptr, 0, false },
    { "resource.rage", rl_kind::k_float, 0.0, true, 150.0, false, 1.0, nullptr, 0, false },
    { "hits.bladestorm", rl_kind::k_int, 0.0, true, 15.0, false, 1.0, nullptr, 0, false },
    { "hits.cleave", rl_kind::k_int, 0.0, true, 15.0, false, 1.0, nullptr, 0, false },
    { "hits.cleave_reap_the_storm_cleave", rl_kind::k_int, 0.0, true, 15.0, false, 1.0, nullptr, 0, false },
    { "hits.colossus_smash", rl_kind::k_int, 0.0, true, 15.0, false, 1.0, nullptr, 0, false },
    { "hits.demolish", rl_kind::k_int, 0.0, true, 15.0, false, 1.0, nullptr, 0, false },
    { "hits.execute", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "hits.execute_reap_the_storm_execute", rl_kind::k_int, 0.0, true, 15.0, false, 1.0, nullptr, 0, false },
    { "hits.heroic_strike", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "hits.mortal_strike", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "hits.overpower", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "hits.ravager", rl_kind::k_int, 0.0, true, 15.0, false, 1.0, nullptr, 0, false },
    { "hits.rend", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "hits.slam", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "hits.slam_heroic_strike", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "aggregates.rend_lacking_count", rl_kind::k_seconds, 0.0, false, 1.0, true, 16.0, nullptr, 0, false },
    { "aggregates.rend_lowest_remaining", rl_kind::k_seconds, 20.0, false, 1.0, true, 20.0, nullptr, 0, false },
    { "aggregates.execute_passing_count", rl_kind::k_seconds, 0.0, false, 1.0, true, 16.0, nullptr, 0, false },
    { "aggregates.execute_lowest_health", rl_kind::k_float, 100.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_fast_footwork", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_war_machine", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_thunder_clap", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_leeching_strikes", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_impending_victory", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_heroic_leap", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_crackling_thunder", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_storm_bolt", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_rend", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_second_wind", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_frothing_berserker", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_bounding_stride", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_pain_and_gain", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_intervene", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_interpose", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_shockwave", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_overwhelming_rage", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_rallying_cry", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_field_dressing", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_spell_reflection", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_wrecking_throw", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_shattering_throw", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_rumbling_earth", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_berserker_shout", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_fearless", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_intimidating_shout", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_piercing_howl", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_honed_reflexes", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_armored_to_the_teeth", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_armored_to_the_teeth_r2", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_reinforced_plates", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_reinforced_plates_r2", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_double_time", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_barbaric_training", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_javelineer", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_resonant_voice", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_crushing_force", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_cruel_strikes", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_cruel_strikes_r2", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_twohanded_weapon_specialization", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_twohanded_weapon_specialization_r2", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_wild_strikes", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_wild_strikes_r2", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_anger_management", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_champions_spear", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_stance_mastery", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_battlefield_commander", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_mortal_strike", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_overpower", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_sudden_death", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_fueled_by_violence", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_ignore_pain", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_die_by_the_sword", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_bloodsurge", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_improved_overpower", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_improved_execute", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_fervor_of_battle", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_tactician", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_colossus_smash", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_impale", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_brute_force", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_efficiency", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_overpowering_finish", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_mass_execution", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_strength_of_arms", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_strength_of_arms_r2", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_just_warming_up", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_broad_strokes", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_sharpened_blades", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_sharpened_blades_r2", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_cleave", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_powerful_momentum", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_martial_prowess", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_dreadnaught", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_deep_wounds", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_tactical_edge", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_crushing_combo", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_massacre", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_collateral_damage", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_bloodborne", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_bloodborne_r2", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_bladestorm", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_ravager", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_critical_thinking", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_critical_thinking_r2", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_master_tactician", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_bloodletting", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_executioners_precision", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_fatality", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_battlelord", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_mortal_wounds", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_avatar", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_master_of_warfare", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_master_of_warfare_r2", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_master_of_warfare_r3", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_master_of_warfare_r4", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_martial_expert", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_colossal_might", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_boneshaker", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_earthquaker", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_decimator", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_one_against_many", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_arterial_bleed", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_tide_of_battle", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_no_stranger_to_pain", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_veteran_vitality", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_cut_to_the_bone", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_practiced_strikes", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_precise_might", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_mountain_of_muscle_and_scars", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_celeritous_conclusion", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_dominance_of_the_colossus", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_imminent_demise", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_overwhelming_blades", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_relentless_pursuit", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_vicious_agility", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_violent_euphoria", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_death_drive", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_culling_cyclone", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_brutal_finish", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_fierce_followthrough", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_opportunist", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_deadly_focus", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_show_no_mercy", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_reap_the_storm", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_slayers_malice", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_unhinged", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.talent_unrelenting_onslaught", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.hero_colossus", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.hero_slayer", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.trinket_voracious_heart_of_ulatek", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.enchant_arcane_mastery", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.enchant_berserkers_rage", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.embellishment_arcanoweave_lining", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.embellishment_hunters_ritual_stone", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.item_venomcursed_mastery", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.item_venomcursed_ascendance", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.set_bite_of_zuljan_2pc", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
    { "capability.mode_funnel", rl_kind::k_int, 0.0, true, 1.0, false, 1.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_SCALARS_MEMBERS[] = {
  { "", "", RL_OBS_FAMILY_SCALARS_LEAVES_0, 180 },
};

inline constexpr std::size_t RL_OBS_FAMILY_COUNT = 8;

inline constexpr rl_obs_family RL_OBS_FAMILIES[RL_OBS_FAMILY_COUNT] = {
  { rl_family::player_buffs, "player_buffs", rl_family_kind::buff, false, RL_OBS_FAMILY_PLAYER_BUFFS_MEMBERS, 30, 0, 52 },
  { rl_family::cooldowns, "cooldowns", rl_family_kind::cooldown, false, RL_OBS_FAMILY_COOLDOWNS_MEMBERS, 12, 52, 15 },
  { rl_family::target_facts, "target_facts", rl_family_kind::target_fact, false, RL_OBS_FAMILY_TARGET_FACTS_MEMBERS, 11, 67, 220 },
  { rl_family::stats, "stats", rl_family_kind::direct, false, RL_OBS_FAMILY_STATS_MEMBERS, 8, 287, 8 },
  { rl_family::swing_cast, "swing_cast", rl_family_kind::direct, false, RL_OBS_FAMILY_SWING_CAST_MEMBERS, 3, 295, 3 },
  { rl_family::raid_events, "raid_events", rl_family_kind::expression, false, RL_OBS_FAMILY_RAID_EVENTS_MEMBERS, 2, 298, 10 },
  { rl_family::legality, "legality", rl_family_kind::legality, false, RL_OBS_FAMILY_LEGALITY_MEMBERS, 18, 308, 18 },
  { rl_family::scalars, "scalars", rl_family_kind::scalar, true, RL_OBS_FAMILY_SCALARS_MEMBERS, 1, 326, 180 },
};

// ---- Action descriptors ----

inline constexpr rl_action_desc RL_ACTIONS[RL_ACTION_DIM] = {
  { 0, "avatar", rl_action_kind::cast, "avatar", nullptr, "avatar", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 1, "bladestorm", rl_action_kind::cast, "bladestorm", nullptr, "bladestorm", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 2, "cleave", rl_action_kind::cast, "cleave", nullptr, "cleave", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 3, "colossus_smash", rl_action_kind::cast, "colossus_smash", nullptr, "colossus_smash", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 4, "demolish", rl_action_kind::cast, "demolish", nullptr, "demolish", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 5, "execute", rl_action_kind::cast, nullptr, nullptr, "execute", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 6, "heroic_strike", rl_action_kind::cast, nullptr, nullptr, "heroic_strike", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 7, "mortal_strike", rl_action_kind::cast, "mortal_strike", nullptr, "mortal_strike", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 8, "overpower", rl_action_kind::cast, "overpower", nullptr, "overpower", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 9, "ravager", rl_action_kind::cast, "ravager", nullptr, "ravager", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 10, "rend", rl_action_kind::cast, nullptr, nullptr, "rend", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 11, "slam", rl_action_kind::cast, nullptr, nullptr, "slam", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 12, "storm_bolt", rl_action_kind::cast, "storm_bolt", nullptr, "storm_bolt", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 13, "sweeping_strikes", rl_action_kind::cast, "sweeping_strikes", nullptr, "sweeping_strikes", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 14, "wrecking_throw", rl_action_kind::cast, "wrecking_throw", nullptr, "wrecking_throw", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 15, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_next_event", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 16, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_swing_mh", { rl_wait_anchor_kind::swing, rl_swing_hand::mh }, nullptr },
  { 17, "use_item_voracious_heart_of_ulatek", rl_action_kind::cast, "voracious_heart_of_ulatek_1297761", "item_cd_1141", "use_item_voracious_heart_of_ulatek", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
};

// ---- Buff-gate table ----

inline constexpr rl_buff_gate RL_BUFF_GATES[] = {
  { "heroic_strike", "master_of_warfare_proc", false },
};
inline constexpr std::size_t RL_BUFF_GATE_COUNT = 1;

// ---- Talent-gate table ----

inline constexpr rl_talent_gate RL_TALENT_GATES[] = { { "", "", false } };
inline constexpr std::size_t RL_TALENT_GATE_COUNT = 0;

// ---- Capability table (246.1-02, CAP-01/CAP-02) ----

inline constexpr std::size_t RL_CAPABILITY_COUNT = 139;
inline constexpr rl_capability RL_CAPABILITIES[RL_CAPABILITY_COUNT] = {
  { "talent_fast_footwork", 367, "talent", nullptr, 0, nullptr, 0, 112206, 1, 0, 0, 0, 0, 0, 0 },
  { "talent_war_machine", 368, "talent", nullptr, 0, nullptr, 0, 112185, 1, 0, 0, 0, 0, 0, 0 },
  { "talent_thunder_clap", 369, "talent", nullptr, 0, nullptr, 0, 112205, 1, 0, 0, 0, 0, 0, 0 },
  { "talent_leeching_strikes", 370, "talent", nullptr, 0, nullptr, 0, 112238, 1, 0, 0, 0, 0, 0, 0 },
  { "talent_impending_victory", 371, "talent", nullptr, 0, nullptr, 0, 112183, 1, 0, 0, 0, 0, 0, 0 },
  { "talent_heroic_leap", 372, "talent", nullptr, 0, nullptr, 0, 112208, 1, 0, 0, 0, 0, 0, 0 },
  { "talent_crackling_thunder", 373, "talent", nullptr, 0, nullptr, 0, 118853, 1, 0, 0, 0, 0, 0, 0 },
  { "talent_storm_bolt", 374, "talent", nullptr, 0, nullptr, 0, 112198, 1, 0, 0, 0, 17, 0, 1 },
  { "talent_rend", 375, "talent", nullptr, 0, nullptr, 0, 135597, 1, 0, 0, 17, 17, 1, 1 },
  { "talent_second_wind", 376, "talent", nullptr, 0, nullptr, 0, 112190, 1, 0, 0, 34, 0, 2, 0 },
  { "talent_frothing_berserker", 377, "talent", nullptr, 0, nullptr, 0, 112216, 1, 0, 0, 34, 0, 2, 0 },
  { "talent_bounding_stride", 378, "talent", nullptr, 0, nullptr, 0, 112219, 1, 0, 0, 34, 0, 2, 0 },
  { "talent_pain_and_gain", 379, "talent", nullptr, 0, nullptr, 0, 112217, 1, 0, 0, 34, 0, 2, 0 },
  { "talent_intervene", 380, "talent", nullptr, 0, nullptr, 0, 134217, 1, 0, 0, 34, 0, 2, 0 },
  { "talent_interpose", 381, "talent", nullptr, 0, nullptr, 0, 134216, 1, 0, 0, 34, 0, 2, 0 },
  { "talent_shockwave", 382, "talent", nullptr, 0, nullptr, 0, 112242, 1, 0, 0, 34, 0, 2, 0 },
  { "talent_overwhelming_rage", 383, "talent", nullptr, 0, nullptr, 0, 112245, 1, 0, 0, 34, 0, 2, 0 },
  { "talent_rallying_cry", 384, "talent", nullptr, 0, nullptr, 0, 112188, 1, 0, 0, 34, 0, 2, 0 },
  { "talent_field_dressing", 385, "talent", nullptr, 0, nullptr, 0, 136626, 1, 0, 0, 34, 0, 2, 0 },
  { "talent_spell_reflection", 386, "talent", nullptr, 0, nullptr, 0, 112253, 1, 0, 0, 34, 0, 2, 0 },
  { "talent_wrecking_throw", 387, "talent", nullptr, 0, nullptr, 0, 112215, 1, 0, 0, 34, 17, 2, 1 },
  { "talent_shattering_throw", 388, "talent", nullptr, 0, nullptr, 0, 112214, 1, 0, 0, 51, 0, 3, 0 },
  { "talent_rumbling_earth", 389, "talent", nullptr, 0, nullptr, 0, 112241, 1, 0, 0, 51, 0, 3, 0 },
  { "talent_berserker_shout", 390, "talent", nullptr, 0, nullptr, 0, 112211, 1, 0, 0, 51, 0, 3, 0 },
  { "talent_fearless", 391, "talent", nullptr, 0, nullptr, 0, 112210, 1, 0, 0, 51, 0, 3, 0 },
  { "talent_intimidating_shout", 392, "talent", nullptr, 0, nullptr, 0, 134254, 1, 0, 0, 51, 0, 3, 0 },
  { "talent_piercing_howl", 393, "talent", nullptr, 0, nullptr, 0, 136627, 1, 0, 0, 51, 0, 3, 0 },
  { "talent_honed_reflexes", 394, "talent", nullptr, 0, nullptr, 0, 118850, 1, 0, 0, 51, 0, 3, 0 },
  { "talent_armored_to_the_teeth", 395, "talent", nullptr, 0, nullptr, 0, 112233, 1, 0, 0, 51, 0, 3, 0 },
  { "talent_armored_to_the_teeth_r2", 396, "talent", nullptr, 0, nullptr, 0, 112233, 2, 0, 1, 51, 0, 3, 0 },
  { "talent_reinforced_plates", 397, "talent", nullptr, 0, nullptr, 0, 112235, 1, 1, 0, 51, 0, 3, 0 },
  { "talent_reinforced_plates_r2", 398, "talent", nullptr, 0, nullptr, 0, 112235, 2, 1, 1, 51, 0, 3, 0 },
  { "talent_double_time", 399, "talent", nullptr, 0, nullptr, 0, 112249, 1, 2, 0, 51, 0, 3, 0 },
  { "talent_barbaric_training", 400, "talent", nullptr, 0, nullptr, 0, 112209, 1, 2, 0, 51, 0, 3, 0 },
  { "talent_javelineer", 401, "talent", nullptr, 0, nullptr, 0, 136625, 1, 2, 0, 51, 0, 3, 0 },
  { "talent_resonant_voice", 402, "talent", nullptr, 0, nullptr, 0, 134225, 1, 2, 0, 51, 0, 3, 0 },
  { "talent_crushing_force", 403, "talent", nullptr, 0, nullptr, 0, 134226, 1, 2, 0, 51, 0, 3, 0 },
  { "talent_cruel_strikes", 404, "talent", nullptr, 0, nullptr, 0, 112248, 1, 2, 0, 51, 0, 3, 0 },
  { "talent_cruel_strikes_r2", 405, "talent", nullptr, 0, nullptr, 0, 112248, 2, 2, 1, 51, 0, 3, 0 },
  { "talent_twohanded_weapon_specialization", 406, "talent", nullptr, 0, nullptr, 0, 112179, 1, 3, 0, 51, 0, 3, 0 },
  { "talent_twohanded_weapon_specialization_r2", 407, "talent", nullptr, 0, nullptr, 0, 112179, 2, 3, 1, 51, 0, 3, 0 },
  { "talent_wild_strikes", 408, "talent", nullptr, 0, nullptr, 0, 112224, 1, 4, 0, 51, 0, 3, 0 },
  { "talent_wild_strikes_r2", 409, "talent", nullptr, 0, nullptr, 0, 112224, 2, 4, 1, 51, 0, 3, 0 },
  { "talent_anger_management", 410, "talent", nullptr, 0, nullptr, 0, 134032, 1, 5, 0, 51, 0, 3, 0 },
  { "talent_champions_spear", 411, "talent", nullptr, 0, nullptr, 0, 112247, 1, 5, 0, 51, 0, 3, 0 },
  { "talent_stance_mastery", 412, "talent", nullptr, 0, nullptr, 0, 134031, 1, 5, 0, 51, 0, 3, 0 },
  { "talent_battlefield_commander", 413, "talent", nullptr, 0, nullptr, 0, 134033, 1, 5, 0, 51, 0, 3, 0 },
  { "talent_mortal_strike", 414, "talent", nullptr, 0, nullptr, 0, 112122, 1, 5, 0, 51, 18, 3, 1 },
  { "talent_overpower", 415, "talent", nullptr, 0, nullptr, 0, 112123, 1, 5, 0, 69, 21, 4, 1 },
  { "talent_sudden_death", 416, "talent", nullptr, 0, nullptr, 0, 112126, 1, 5, 0, 90, 2, 5, 0 },
  { "talent_fueled_by_violence", 417, "talent", nullptr, 0, nullptr, 0, 112128, 1, 5, 0, 92, 0, 5, 0 },
  { "talent_ignore_pain", 418, "talent", nullptr, 0, nullptr, 0, 136701, 1, 5, 0, 92, 0, 5, 0 },
  { "talent_die_by_the_sword", 419, "talent", nullptr, 0, nullptr, 0, 112121, 1, 5, 0, 92, 0, 5, 0 },
  { "talent_bloodsurge", 420, "talent", nullptr, 0, nullptr, 0, 112129, 1, 5, 0, 92, 0, 5, 0 },
  { "talent_improved_overpower", 421, "talent", nullptr, 0, nullptr, 0, 112131, 1, 5, 0, 92, 0, 5, 0 },
  { "talent_improved_execute", 422, "talent", nullptr, 0, nullptr, 0, 112125, 1, 5, 0, 92, 0, 5, 0 },
  { "talent_fervor_of_battle", 423, "talent", nullptr, 0, nullptr, 0, 135932, 1, 5, 0, 92, 0, 5, 0 },
  { "talent_tactician", 424, "talent", nullptr, 0, nullptr, 0, 112134, 1, 5, 0, 92, 0, 5, 0 },
  { "talent_colossus_smash", 425, "talent", nullptr, 0, nullptr, 0, 112144, 1, 5, 0, 92, 18, 5, 1 },
  { "talent_impale", 426, "talent", nullptr, 0, nullptr, 0, 112146, 1, 5, 0, 110, 0, 6, 0 },
  { "talent_brute_force", 427, "talent", nullptr, 0, nullptr, 0, 135936, 1, 5, 0, 110, 0, 6, 0 },
  { "talent_efficiency", 428, "talent", nullptr, 0, nullptr, 0, 135935, 1, 5, 0, 110, 0, 6, 0 },
  { "talent_overpowering_finish", 429, "talent", nullptr, 0, nullptr, 0, 114733, 1, 5, 0, 110, 0, 6, 0 },
  { "talent_mass_execution", 430, "talent", nullptr, 0, nullptr, 0, 137473, 1, 5, 0, 110, 0, 6, 0 },
  { "talent_strength_of_arms", 431, "talent", nullptr, 0, nullptr, 0, 135944, 1, 5, 0, 110, 0, 6, 0 },
  { "talent_strength_of_arms_r2", 432, "talent", nullptr, 0, nullptr, 0, 135944, 2, 5, 1, 110, 0, 6, 0 },
  { "talent_just_warming_up", 433, "talent", nullptr, 0, nullptr, 0, 135943, 1, 6, 0, 110, 0, 6, 0 },
  { "talent_broad_strokes", 434, "talent", nullptr, 0, nullptr, 0, 135942, 1, 6, 0, 110, 0, 6, 0 },
  { "talent_sharpened_blades", 435, "talent", nullptr, 0, nullptr, 0, 112320, 1, 6, 0, 110, 0, 6, 0 },
  { "talent_sharpened_blades_r2", 436, "talent", nullptr, 0, nullptr, 0, 112320, 2, 6, 1, 110, 0, 6, 0 },
  { "talent_cleave", 437, "talent", nullptr, 0, nullptr, 0, 112147, 1, 7, 0, 110, 18, 6, 1 },
  { "talent_powerful_momentum", 438, "talent", nullptr, 0, nullptr, 0, 114739, 1, 7, 0, 128, 0, 7, 0 },
  { "talent_martial_prowess", 439, "talent", nullptr, 0, nullptr, 0, 135934, 1, 7, 0, 128, 0, 7, 0 },
  { "talent_dreadnaught", 440, "talent", nullptr, 0, nullptr, 0, 112137, 1, 7, 0, 128, 0, 7, 0 },
  { "talent_deep_wounds", 441, "talent", nullptr, 0, nullptr, 0, 135940, 1, 7, 0, 128, 0, 7, 0 },
  { "talent_tactical_edge", 442, "talent", nullptr, 0, nullptr, 0, 135939, 1, 7, 0, 128, 0, 7, 0 },
  { "talent_crushing_combo", 443, "talent", nullptr, 0, nullptr, 0, 135938, 1, 7, 0, 128, 0, 7, 0 },
  { "talent_massacre", 444, "talent", nullptr, 0, nullptr, 0, 112145, 1, 7, 0, 128, 0, 7, 0 },
  { "talent_collateral_damage", 445, "talent", nullptr, 0, nullptr, 0, 135937, 1, 7, 0, 128, 2, 7, 0 },
  { "talent_bloodborne", 446, "talent", nullptr, 0, nullptr, 0, 112135, 1, 7, 0, 130, 0, 7, 0 },
  { "talent_bloodborne_r2", 447, "talent", nullptr, 0, nullptr, 0, 112135, 2, 7, 1, 130, 0, 7, 0 },
  { "talent_bladestorm", 448, "talent", nullptr, 0, nullptr, 0, 112314, 1, 8, 0, 130, 3, 7, 1 },
  { "talent_ravager", 449, "talent", nullptr, 0, nullptr, 0, 136702, 1, 8, 0, 133, 3, 8, 1 },
  { "talent_critical_thinking", 450, "talent", nullptr, 0, nullptr, 0, 112317, 1, 8, 0, 136, 0, 9, 0 },
  { "talent_critical_thinking_r2", 451, "talent", nullptr, 0, nullptr, 0, 112317, 2, 8, 1, 136, 0, 9, 0 },
  { "talent_master_tactician", 452, "talent", nullptr, 0, nullptr, 0, 114740, 1, 9, 0, 136, 0, 9, 0 },
  { "talent_bloodletting", 453, "talent", nullptr, 0, nullptr, 0, 112310, 1, 9, 0, 136, 0, 9, 0 },
  { "talent_executioners_precision", 454, "talent", nullptr, 0, nullptr, 0, 112318, 1, 9, 0, 136, 2, 9, 0 },
  { "talent_fatality", 455, "talent", nullptr, 0, nullptr, 0, 112311, 1, 9, 0, 138, 0, 9, 0 },
  { "talent_battlelord", 456, "talent", nullptr, 0, nullptr, 0, 135933, 1, 9, 0, 138, 0, 9, 0 },
  { "talent_mortal_wounds", 457, "talent", nullptr, 0, nullptr, 0, 135941, 1, 9, 0, 138, 0, 9, 0 },
  { "talent_avatar", 458, "talent", nullptr, 0, nullptr, 0, 136703, 1, 9, 0, 138, 2, 9, 1 },
  { "talent_master_of_warfare", 459, "talent", nullptr, 0, nullptr, 0, 136989, 1, 9, 0, 140, 0, 10, 0 },
  { "talent_master_of_warfare_r2", 460, "talent", nullptr, 0, nullptr, 0, 136988, 1, 9, 1, 140, 0, 10, 0 },
  { "talent_master_of_warfare_r3", 461, "talent", nullptr, 0, nullptr, 0, 136988, 2, 10, 1, 140, 0, 10, 0 },
  { "talent_master_of_warfare_r4", 462, "talent", nullptr, 0, nullptr, 0, 136987, 1, 11, 1, 140, 0, 10, 0 },
  { "talent_martial_expert", 463, "talent", nullptr, 0, nullptr, 0, 117409, 1, 12, 0, 140, 0, 10, 0 },
  { "talent_colossal_might", 464, "talent", nullptr, 0, nullptr, 0, 117416, 1, 12, 0, 140, 2, 10, 0 },
  { "talent_boneshaker", 465, "talent", nullptr, 0, nullptr, 0, 117386, 1, 12, 0, 142, 0, 10, 0 },
  { "talent_earthquaker", 466, "talent", nullptr, 0, nullptr, 0, 119858, 1, 12, 0, 142, 0, 10, 0 },
  { "talent_decimator", 467, "talent", nullptr, 0, nullptr, 0, 136073, 1, 12, 0, 142, 0, 10, 0 },
  { "talent_one_against_many", 468, "talent", nullptr, 0, nullptr, 0, 117396, 1, 12, 0, 142, 0, 10, 0 },
  { "talent_arterial_bleed", 469, "talent", nullptr, 0, nullptr, 0, 119856, 1, 12, 0, 142, 0, 10, 0 },
  { "talent_tide_of_battle", 470, "talent", nullptr, 0, nullptr, 0, 117408, 1, 12, 0, 142, 0, 10, 0 },
  { "talent_no_stranger_to_pain", 471, "talent", nullptr, 0, nullptr, 0, 117412, 1, 12, 0, 142, 0, 10, 0 },
  { "talent_veteran_vitality", 472, "talent", nullptr, 0, nullptr, 0, 119857, 1, 12, 0, 142, 0, 10, 0 },
  { "talent_cut_to_the_bone", 473, "talent", nullptr, 0, nullptr, 0, 136072, 1, 12, 0, 142, 0, 10, 0 },
  { "talent_practiced_strikes", 474, "talent", nullptr, 0, nullptr, 0, 117393, 1, 12, 0, 142, 0, 10, 0 },
  { "talent_precise_might", 475, "talent", nullptr, 0, nullptr, 0, 117391, 1, 12, 0, 142, 0, 10, 0 },
  { "talent_mountain_of_muscle_and_scars", 476, "talent", nullptr, 0, nullptr, 0, 117403, 1, 12, 0, 142, 0, 10, 0 },
  { "talent_celeritous_conclusion", 477, "talent", nullptr, 0, nullptr, 0, 136071, 1, 12, 0, 142, 0, 10, 0 },
  { "talent_dominance_of_the_colossus", 478, "talent", nullptr, 0, nullptr, 0, 117390, 1, 12, 0, 142, 0, 10, 0 },
  { "talent_imminent_demise", 479, "talent", nullptr, 0, nullptr, 0, 117385, 1, 12, 0, 142, 2, 10, 0 },
  { "talent_overwhelming_blades", 480, "talent", nullptr, 0, nullptr, 0, 117407, 1, 12, 0, 144, 0, 10, 0 },
  { "talent_relentless_pursuit", 481, "talent", nullptr, 0, nullptr, 0, 117392, 1, 12, 0, 144, 0, 10, 0 },
  { "talent_vicious_agility", 482, "talent", nullptr, 0, nullptr, 0, 123408, 1, 12, 0, 144, 0, 10, 0 },
  { "talent_violent_euphoria", 483, "talent", nullptr, 0, nullptr, 0, 136076, 1, 12, 0, 144, 0, 10, 0 },
  { "talent_death_drive", 484, "talent", nullptr, 0, nullptr, 0, 117410, 1, 12, 0, 144, 0, 10, 0 },
  { "talent_culling_cyclone", 485, "talent", nullptr, 0, nullptr, 0, 117383, 1, 12, 0, 144, 0, 10, 0 },
  { "talent_brutal_finish", 486, "talent", nullptr, 0, nullptr, 0, 123409, 1, 12, 0, 144, 0, 10, 0 },
  { "talent_fierce_followthrough", 487, "talent", nullptr, 0, nullptr, 0, 117384, 1, 12, 0, 144, 0, 10, 0 },
  { "talent_opportunist", 488, "talent", nullptr, 0, nullptr, 0, 123770, 1, 12, 0, 144, 2, 10, 0 },
  { "talent_deadly_focus", 489, "talent", nullptr, 0, nullptr, 0, 136075, 1, 12, 0, 146, 0, 10, 0 },
  { "talent_show_no_mercy", 490, "talent", nullptr, 0, nullptr, 0, 117381, 1, 12, 0, 146, 0, 10, 0 },
  { "talent_reap_the_storm", 491, "talent", nullptr, 0, nullptr, 0, 117406, 1, 12, 0, 146, 2, 10, 0 },
  { "talent_slayers_malice", 492, "talent", nullptr, 0, nullptr, 0, 117398, 1, 12, 0, 148, 0, 10, 0 },
  { "talent_unhinged", 493, "talent", nullptr, 0, nullptr, 0, 136074, 1, 12, 0, 148, 0, 10, 0 },
  { "talent_unrelenting_onslaught", 494, "talent", nullptr, 0, nullptr, 0, 117417, 1, 12, 0, 148, 0, 10, 0 },
  { "hero_colossus", 495, "talent", nullptr, 0, nullptr, 0, 117415, 1, 12, 0, 148, 18, 10, 1 },
  { "hero_slayer", 496, "talent", nullptr, 0, nullptr, 0, 117411, 1, 12, 0, 166, 0, 11, 0 },
  { "trinket_voracious_heart_of_ulatek", 497, "special_effect", nullptr, 1297761, nullptr, 0, 0, 0, 12, 0, 166, 4, 11, 1 },
  { "enchant_arcane_mastery", 498, "special_effect", nullptr, 1236721, nullptr, 0, 0, 0, 12, 0, 170, 0, 12, 0 },
  { "enchant_berserkers_rage", 499, "special_effect", nullptr, 1236728, nullptr, 0, 0, 0, 12, 0, 170, 0, 12, 0 },
  { "embellishment_arcanoweave_lining", 500, "special_effect", nullptr, 1283697, nullptr, 0, 0, 0, 12, 0, 170, 1, 12, 0 },
  { "embellishment_hunters_ritual_stone", 501, "special_effect", nullptr, 1297382, nullptr, 0, 0, 0, 12, 0, 171, 0, 12, 0 },
  { "item_venomcursed_mastery", 502, "special_effect", nullptr, 1307923, nullptr, 0, 0, 0, 12, 0, 171, 0, 12, 0 },
  { "item_venomcursed_ascendance", 503, "special_effect", nullptr, 1317582, nullptr, 0, 0, 0, 12, 0, 171, 0, 12, 0 },
  { "set_bite_of_zuljan_2pc", 504, "set_bonus", nullptr, 0, "MID_BOZ", 2, 0, 0, 12, 0, 171, 0, 12, 0 },
  { "mode_funnel", 505, "funnel_mode", nullptr, 0, nullptr, 0, 0, 0, 12, 0, 171, 0, 12, 0 },
};

inline constexpr std::size_t RL_CAPABILITY_REQUIRES_COUNT = 12;
inline constexpr const char* RL_CAPABILITY_REQUIRES[RL_CAPABILITY_REQUIRES_COUNT] = {
  "talent_armored_to_the_teeth",
  "talent_reinforced_plates",
  "talent_cruel_strikes",
  "talent_twohanded_weapon_specialization",
  "talent_wild_strikes",
  "talent_strength_of_arms",
  "talent_sharpened_blades",
  "talent_bloodborne",
  "talent_critical_thinking",
  "talent_master_of_warfare",
  "talent_master_of_warfare_r2",
  "talent_master_of_warfare_r3",
};

inline constexpr std::size_t RL_CAPABILITY_GOVERNED_SLOTS_COUNT = 171;
inline constexpr std::size_t RL_CAPABILITY_GOVERNED_SLOTS[RL_CAPABILITY_GOVERNED_SLOTS_COUNT] = {
  63,
  320,
  247,
  248,
  249,
  250,
  251,
  252,
  253,
  254,
  255,
  256,
  257,
  258,
  259,
  260,
  261,
  318,
  207,
  208,
  209,
  210,
  211,
  212,
  213,
  214,
  215,
  216,
  217,
  218,
  219,
  220,
  221,
  360,
  66,
  322,
  267,
  268,
  269,
  270,
  271,
  272,
  273,
  274,
  275,
  276,
  277,
  278,
  279,
  280,
  281,
  57,
  315,
  167,
  168,
  169,
  170,
  171,
  172,
  173,
  174,
  175,
  176,
  177,
  178,
  179,
  180,
  181,
  357,
  58,
  59,
  60,
  61,
  316,
  187,
  188,
  189,
  190,
  191,
  192,
  193,
  194,
  195,
  196,
  197,
  198,
  199,
  200,
  201,
  358,
  42,
  43,
  55,
  311,
  87,
  88,
  89,
  90,
  91,
  92,
  93,
  94,
  95,
  96,
  97,
  98,
  99,
  100,
  101,
  352,
  54,
  310,
  67,
  68,
  69,
  70,
  71,
  72,
  73,
  74,
  75,
  76,
  77,
  78,
  79,
  80,
  81,
  350,
  8,
  9,
  53,
  309,
  349,
  62,
  317,
  359,
  23,
  24,
  52,
  308,
  10,
  11,
  29,
  30,
  40,
  41,
  351,
  355,
  56,
  312,
  107,
  108,
  109,
  110,
  111,
  112,
  113,
  114,
  115,
  116,
  117,
  118,
  119,
  120,
  121,
  353,
  15,
  16,
  65,
  325,
  0,
};

inline constexpr std::size_t RL_CAPABILITY_GOVERNED_ACTIONS_COUNT = 12;
inline constexpr const char* RL_CAPABILITY_GOVERNED_ACTIONS[RL_CAPABILITY_GOVERNED_ACTIONS_COUNT] = {
  "storm_bolt",
  "rend",
  "wrecking_throw",
  "mortal_strike",
  "overpower",
  "colossus_smash",
  "cleave",
  "bladestorm",
  "ravager",
  "avatar",
  "demolish",
  "use_item_voracious_heart_of_ulatek",
};

inline constexpr double RL_CAPABILITY_FIXED_VALUE = 0.0;
inline constexpr const char* RL_NET_LAYOUT_SHA = "rl-layout-v1:b7feddf4bd295232117f7f08164c779a883e17a7730b754c1b66b023c884c59c";

// ---- Target scorer feature list (Phase 230-02, SCOR-01) ----

inline constexpr std::size_t RL_TARGET_SLOTS = 16;
inline constexpr std::size_t RL_TARGET_FEATURES = 20;
inline constexpr const char* RL_TARGET_FEATURE_SHA = "tgt-feat-v1:f99400a14fb0686480339542393c9f054e3edb84f95e927a346fe4c8f26c2a95";

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
  "fatal_mark_stacks",
  "overwhelmed_stacks",
  "ravaged_remaining",
  "venomfang_debuff_stacks",
  "wrecked_stacks",
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
  X(neighbours_within_radius) \
  X(is_current_target) \
  X(rend_remaining) \
  X(deep_wounds_remaining) \
  X(colossus_smash_remaining) \
  X(fatal_mark_stacks) \
  X(overwhelmed_stacks) \
  X(ravaged_remaining) \
  X(venomfang_debuff_stacks) \
  X(wrecked_stacks)

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
  { "rend_remaining", rl_kind::k_seconds, false, 1.0, true, 20.0, -1 },
  { "deep_wounds_remaining", rl_kind::k_seconds, false, 1.0, true, 8.0, -1 },
  { "colossus_smash_remaining", rl_kind::k_seconds, false, 1.0, true, 10.0, -1 },
  { "fatal_mark_stacks", rl_kind::k_int, true, 5.0, false, 1.0, -1 },
  { "overwhelmed_stacks", rl_kind::k_int, true, 5.0, false, 1.0, -1 },
  { "ravaged_remaining", rl_kind::k_seconds, false, 1.0, true, 12.0, -1 },
  { "venomfang_debuff_stacks", rl_kind::k_int, true, 20.0, false, 1.0, -1 },
  { "wrecked_stacks", rl_kind::k_int, true, 10.0, false, 1.0, -1 },
};

inline constexpr std::size_t RL_AIM_CONTEXT_COUNT = 1;
inline constexpr std::size_t RL_AIM_CONTEXT_OBS_SLOTS[RL_AIM_CONTEXT_COUNT] = { 505 };
inline constexpr const char* RL_AIM_CONTEXT_NAMES[RL_AIM_CONTEXT_COUNT] = {
  "capability.mode_funnel",
};

inline constexpr std::size_t RL_AIM_SPELL_COUNT = 11;
inline constexpr const char* RL_AIM_SPELLS[RL_AIM_SPELL_COUNT] = {
  "mortal_strike",
  "overpower",
  "execute",
  "slam",
  "heroic_strike",
  "rend",
  "colossus_smash",
  "demolish",
  "wrecking_throw",
  "storm_bolt",
  "cleave",
};

inline constexpr std::size_t RL_AIM_INPUT_COUNT = 21;
inline constexpr const char* RL_AIM_SHA = "aim-v1:01c4a8777dbe09b09fea6d4412472f665d775620d2395018aed3b7986cd77fb4";


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

inline constexpr std::size_t RL_TARGETED_TOKEN_COUNT = 11;
inline constexpr const char* RL_TARGETED_TOKENS[RL_TARGETED_TOKEN_COUNT] = {
  "mortal_strike",
  "overpower",
  "execute",
  "slam",
  "heroic_strike",
  "rend",
  "colossus_smash",
  "demolish",
  "wrecking_throw",
  "storm_bolt",
  "cleave",
};
inline constexpr double RL_AIMED_SPELL_RANGE_YARDS[RL_TARGETED_TOKEN_COUNT] = {5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 0.0, 5.0, 25.0, 20.0, 5.0};
inline constexpr const char* RL_TAG_MELEE_ACTION = "mortal_strike";

inline constexpr std::size_t RL_RULE_PREF_COUNT = 11;
inline constexpr rl_rule_pref RL_RULE_PREFS[RL_RULE_PREF_COUNT] = {
  { "mortal_strike", "current_target" },
  { "overpower", "current_target" },
  { "execute", "shortest_ttd" },
  { "slam", "current_target" },
  { "heroic_strike", "current_target" },
  { "rend", "current_target" },
  { "colossus_smash", "current_target" },
  { "demolish", "current_target" },
  { "wrecking_throw", "current_target" },
  { "storm_bolt", "current_target" },
  { "cleave", "current_target" },
};

inline constexpr std::size_t RL_DECLARED_FACT_COUNT = 8;
inline constexpr rl_declared_fact RL_DECLARED_FACTS[RL_DECLARED_FACT_COUNT] = {
  { rl_fact_kind::dot_remaining, "rend_dot", 12 },
  { rl_fact_kind::dot_remaining, "deep_wounds", 13 },
  { rl_fact_kind::dot_remaining, "colossus_smash", 14 },
  { rl_fact_kind::debuff_stacks, "fatal_mark", 15 },
  { rl_fact_kind::debuff_stacks, "overwhelmed", 16 },
  { rl_fact_kind::dot_remaining, "ravaged", 17 },
  { rl_fact_kind::debuff_stacks, "venomfang_debuff", 18 },
  { rl_fact_kind::debuff_stacks, "wrecked", 19 },
};

inline constexpr const char* RL_RESOURCE_NAME = "rage";

inline constexpr std::size_t RL_WAIT_DEF_COUNT = 2;
inline constexpr rl_wait_def RL_WAIT_DEFS[RL_WAIT_DEF_COUNT] = {
  { "wait_next_event", rl_wait_kind::next_event, 0.0 },
  { "wait_swing_mh", rl_wait_kind::swing_mh, 0.0 },
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

inline constexpr std::size_t RL_HIT_PROVIDER_COUNT = 14;
inline constexpr rl_hit_provider RL_HIT_PROVIDERS[RL_HIT_PROVIDER_COUNT] = {
  { "hits", "bladestorm", "census.bladestorm" },
  { "hits", "cleave", "census.cleave" },
  { "hits", "cleave_reap_the_storm_cleave", "census.cleave" },
  { "hits", "colossus_smash", "census.colossus_smash" },
  { "hits", "demolish", "census.demolish" },
  { "hits", "execute", "census.execute" },
  { "hits", "execute_reap_the_storm_execute", "census.execute" },
  { "hits", "heroic_strike", "census.heroic_strike" },
  { "hits", "mortal_strike", "census.mortal_strike" },
  { "hits", "overpower", "census.overpower" },
  { "hits", "ravager", "census.ravager" },
  { "hits", "rend", "census.rend" },
  { "hits", "slam", "census.slam" },
  { "hits", "slam_heroic_strike", "census.slam" },
};

// The gate class of each census hit row, parallel to RL_HIT_PROVIDERS (a class-provider row reads false). NEEDS_UNIT true: the press
// needs a unit and the game's range check gates it; false: it needs no unit and is legal only with an enemy's hitbox inside its
// own shape. CLASS_OVERRIDDEN true: the class is the owner's recorded word, so the engine takes it instead of its own reading.
#define RL_HIT_ROW_NEEDS_UNIT_DEFINED 1
inline constexpr bool RL_HIT_ROW_NEEDS_UNIT[RL_HIT_PROVIDER_COUNT] = { false, true, true, false, true, true, true, true, true, true, true, true, true, true };
inline constexpr bool RL_HIT_ROW_CLASS_OVERRIDDEN[RL_HIT_PROVIDER_COUNT] = { false, false, false, false, false, false, false, false, false, false, false, false, false, false };

// The channel lock (G271.6-37). While the player is channelling, or any aura of RL_LOCK_AURA_SPELL_IDS is up, the simulator refuses
// every cast button whose RL_ACTION_REFUSED_WHILE_LOCKED entry (indexed by action id; a wait reads false) is true; item uses and
// waits are never refused. RL_ACTION_LOCK_ANSWER_VARIES is true for a button whose answer depends on the talent build; the
// table then carries the census build's answer. The engine's bind-time check compares its own answers with these tables and skips
// a row whose answer varies. All of it is the simulator's own answer carried by the action census, never typed in a generator.
#define RL_ACTION_LOCK_TABLE_DEFINED 1
inline constexpr bool RL_ACTION_REFUSED_WHILE_LOCKED[RL_ACTION_DIM] = { true, true, true, true, true, true, true, true, true, true, true, true, false, true, true, false, false, false };
inline constexpr bool RL_ACTION_LOCK_ANSWER_VARIES[RL_ACTION_DIM] = { false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, false };
inline constexpr std::size_t RL_LOCK_AURA_COUNT = 2;
inline constexpr long long RL_LOCK_AURA_SPELL_IDS[RL_LOCK_AURA_COUNT] = {
  227847,
  446035,
};

// The no-unit reach gate (271.7 class A). A cast button whose RL_ACTION_NEEDS_MELEE_REACH entry (indexed by action id; a wait reads
// false) is true needs no unit and has no hit shape; the simulator offers it only while some enemy's hitbox is within its melee reach
// (rl_target_select::MELEE_RANGE_YARDS plus the enemy's combat reach). The table is the census writer's own answer
// (needsMeleeReach); the engine's bind-time check compares it with its own reading.
#define RL_ACTION_NEEDS_MELEE_REACH_DEFINED 1
inline constexpr bool RL_ACTION_NEEDS_MELEE_REACH[RL_ACTION_DIM] = { true, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, true };

// The aimed buttons with a declared target-health gate (261013-aimx, class A), parallel to RL_TARGETED_TOKENS. A button whose entry is true
// is aimed only among the enemies that pass its target-health gate (the registry's maskRules.targetHealthGates row, applied through the
// simulator's own target_ready), so the aim head is never offered an enemy the cast would be refused on while another qualifies.
#define RL_TARGETED_TOKEN_HEALTH_GATED_DEFINED 1
inline constexpr bool RL_TARGETED_TOKEN_HEALTH_GATED[RL_TARGETED_TOKEN_COUNT] = { false, false, true, false, false, false, false, false, false, false, false };

// The census node each hit row counts, parallel to RL_HIT_PROVIDERS (261013-aimx, class B). The engine counts that node BY NAME, never the node
// its own live classification would pick (that classification never runs the fight-end proc marking, so it can name a proc node as the
// button's count). RL_HIT_ROW_CONDITIONAL is true for a row that counts what a conditional proc node of its button would hit if it fires.
#define RL_HIT_ROW_NODE_DEFINED 1
inline constexpr const char* RL_HIT_ROW_NODE[RL_HIT_PROVIDER_COUNT] = { "bladestorm_mh", "cleave", "reap_the_storm_cleave", "colossus_smash", "demolish_third_attack", "execute_damage", "reap_the_storm_execute", "heroic_strike", "mortal_strike", "overpower", "ravager_tick", "rend", "slam", "heroic_strike" };
inline constexpr bool RL_HIT_ROW_CONDITIONAL[RL_HIT_PROVIDER_COUNT] = { false, false, true, false, false, false, true, false, false, false, false, false, false, true };

// The declared aggregates (261013-aimx, class C): a count or a lowest value over the enemies in reach of an aimed button, that is, over the button's
// own candidate list (the list its aim head scores, already narrowed by the button's declared target-health gate). The observation leaf of a row is
// `aggregates.<name>`. count = how many candidates; count_zero = how many have a raw fact of 0; min_positive = the lowest raw fact above 0; min = the
// lowest raw fact. With no candidate a count reads 0 and a minimum reads the saturated end of its scale. fact_slot is the position of the fact in
// the targeting block's candidate facts (0 when the row reads no fact).
enum class rl_aggregate_reduce { count, count_zero, min_positive, min };
struct rl_declared_aggregate
{
  const char*          name;
  const char*          button;
  const char*          fact;
  std::size_t          fact_slot;
  rl_aggregate_reduce  reduce;
};
#define RL_DECLARED_AGGREGATES_DEFINED 1
inline constexpr std::size_t RL_DECLARED_AGGREGATE_COUNT = 4;
inline constexpr rl_declared_aggregate RL_DECLARED_AGGREGATES[RL_DECLARED_AGGREGATE_COUNT] = {
  { "rend_lacking_count", "rend", "rend_remaining", 12, rl_aggregate_reduce::count_zero },
  { "rend_lowest_remaining", "rend", "rend_remaining", 12, rl_aggregate_reduce::min_positive },
  { "execute_passing_count", "execute", nullptr, 0, rl_aggregate_reduce::count },
  { "execute_lowest_health", "execute", "health_pct", 8, rl_aggregate_reduce::min },
};

// The enemies sidecar (Phase 290 stream FB): next to each translog the simulator writes `<translog>.enemies.zst`, one record per decision with the
// enemy each aimed button would hit (as an actor index), the table of every enemy actor (position, reach, health, time to die, flags), the damage
// each enemy took since the previous decision, the player's position and target and the next start and end of every raid event; once per fight
// the per-fight constants. A rank is an enemy's order among the process's enemy actors by actor index (the event sidecar's `unit`). Every number below
// is derived from the registry's `enemiesSidecar` block by scripts/rl/enemies_layout.py; a member list is `{ name, type code, count, byte offset }`
// (type codes B u8, H u16, I u32, f f32, d f64). The writer's structs must match them member for member (a static_assert), and RL_ENEMIES_SHA
// is written into the file header.
#define RL_ENEMIES_DEFINED 1
inline constexpr std::size_t RL_ENEMIES_FORMAT_VERSION = 1;
inline constexpr std::size_t RL_ENEMIES_RANK_CAP = 20;
inline constexpr std::size_t RL_ENEMIES_PICK_COUNT = 11;
inline constexpr std::size_t RL_ENEMIES_CAPABILITY_COUNT = 139;
inline constexpr std::size_t RL_ENEMIES_RAID_EVENT_CAP = 24;
inline constexpr std::size_t RL_ENEMIES_RAID_NAME_BYTES = 24;
inline constexpr std::size_t RL_ENEMIES_RECORD_SIZE = 1832;
inline constexpr std::size_t RL_ENEMIES_HEADER_SIZE = 120;
inline constexpr const char* RL_ENEMIES_SHA = "enemies-v1:f8cb32933ee4f9b2e6f16ae2630b72663d6160c7c4d8c5918fa7a99038047ad3";
inline constexpr std::size_t RL_ENEMIES_CLASS_CONSTANT_COUNT = 0;
inline constexpr const char* RL_ENEMIES_CLASS_CONSTANT_NAMES[1] = {
  "",
};
inline constexpr std::size_t RL_ENEMIES_PICK_SOURCE_COUNT = 4;
inline constexpr const char* RL_ENEMIES_PICK_SOURCE_NAMES[RL_ENEMIES_PICK_SOURCE_COUNT] = {
  "none",
  "head",
  "rules",
  "random_aim",
};
inline constexpr std::size_t RL_ENEMIES_RANK_FLAG_COUNT = 6;
inline constexpr const char* RL_ENEMIES_RANK_FLAG_NAMES[RL_ENEMIES_RANK_FLAG_COUNT] = {
  "alive",
  "immune",
  "hazard",
  "do_not_hit",
  "boss",
  "counts_as_enemy",
};
inline constexpr std::size_t RL_ENEMIES_DAMAGE_STREAM_COUNT = 2;
inline constexpr const char* RL_ENEMIES_DAMAGE_STREAM_NAMES[RL_ENEMIES_DAMAGE_STREAM_COUNT] = {
  "real",
  "expected",
};
struct rl_enemies_member
{
  const char* name;
  char        type;
  std::size_t count;
  std::size_t offset;
};
inline constexpr std::size_t RL_ENEMIES_FIGHT_MEMBER_COUNT = 14;
inline constexpr rl_enemies_member RL_ENEMIES_FIGHT_MEMBERS[RL_ENEMIES_FIGHT_MEMBER_COUNT] = {
  { "kind", 'I', 1, 0 },
  { "iteration", 'I', 1, 4 },
  { "n_decisions", 'I', 1, 8 },
  { "enemy_actor_count", 'I', 1, 12 },
  { "rank_over_cap", 'I', 1, 16 },
  { "raid_event_count", 'I', 1, 20 },
  { "raid_over_cap", 'I', 1, 24 },
  { "reserved", 'I', 1, 28 },
  { "button_range", 'd', 11, 32 },
  { "button_radius", 'd', 11, 120 },
  { "class_constant", 'd', 1, 208 },
  { "capability", 'B', 139, 216 },
  { "raid_name", 'B', 576, 355 },
  { "raid_type", 'B', 576, 931 },
};
inline constexpr std::size_t RL_ENEMIES_DECISION_MEMBER_COUNT = 32;
inline constexpr rl_enemies_member RL_ENEMIES_DECISION_MEMBERS[RL_ENEMIES_DECISION_MEMBER_COUNT] = {
  { "kind", 'I', 1, 0 },
  { "seq", 'I', 1, 4 },
  { "pick_actor", 'H', 11, 8 },
  { "pick_source", 'B', 11, 30 },
  { "cand_count", 'B', 11, 41 },
  { "cand_mask", 'I', 11, 52 },
  { "actor_index", 'H', 20, 96 },
  { "spawn_index", 'B', 20, 136 },
  { "flags", 'B', 20, 156 },
  { "x", 'd', 20, 176 },
  { "y", 'd', 20, 336 },
  { "combat_reach", 'f', 20, 496 },
  { "health_pct", 'f', 20, 576 },
  { "time_to_die", 'f', 20, 656 },
  { "immunity_remaining", 'f', 20, 736 },
  { "damage_real", 'f', 20, 816 },
  { "damage_expected", 'f', 20, 896 },
  { "damage_unranked", 'f', 2, 976 },
  { "player_x", 'd', 1, 984 },
  { "player_y", 'd', 1, 992 },
  { "player_facing_x", 'd', 1, 1000 },
  { "player_facing_y", 'd', 1, 1008 },
  { "player_target_actor", 'H', 1, 1016 },
  { "player_move_distance", 'd', 1, 1024 },
  { "player_move_remains", 'd', 1, 1032 },
  { "raid_next_start", 'd', 24, 1040 },
  { "raid_next_end", 'd', 24, 1232 },
  { "raid_duration", 'f', 24, 1424 },
  { "raid_cooldown", 'f', 24, 1520 },
  { "raid_up", 'B', 24, 1616 },
  { "raid_duration_mean", 'f', 24, 1640 },
  { "raid_cooldown_mean", 'f', 24, 1736 },
};
inline constexpr std::size_t RL_ENEMIES_FOOTER_MEMBER_COUNT = 4;
inline constexpr rl_enemies_member RL_ENEMIES_FOOTER_MEMBERS[RL_ENEMIES_FOOTER_MEMBER_COUNT] = {
  { "kind", 'I', 1, 0 },
  { "n_fights", 'I', 1, 4 },
  { "reserved0", 'I', 1, 8 },
  { "reserved1", 'I', 1, 12 },
};
