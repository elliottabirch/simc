// ==============================================================================
// GENERATED FILE -- DO NOT EDIT (XPORT-04, D-09).
//
// Produced by:      scripts/rl/gen_rl_constants.py
// Registry source:  scripts/rl/specs/arms-warrior.json
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
inline constexpr std::size_t RL_OBS_DIM = 406;
inline constexpr std::size_t RL_ACTION_DIM = 19;
inline constexpr double RL_EPISODE_MAX_TIME = 300.0;
inline constexpr double RL_WAIT_FLOOR_SECONDS = 0.05;
inline constexpr double RL_PERMANENT_SATURATION = 1.0;
inline constexpr const char* RL_OBS_SCHEMA_SHA = "rl-obs-v6:3d04d536917aab5e64f8b80f935aacb5e30c95613062b9c831ecfc1b4175f943";
inline constexpr const char* RL_MASK_RULES_SHA = "403fa658aabd986da04a24ad88c6625ad5a7fba10f3f6437922af21582877da5";
inline constexpr const char* RL_ACTION_SPACE_SHA = "b827e77ae07449536296e422d97b1384006f959145cbbc7c30aeec757783e09a";

// ---- Observation name list (the materialised ordering) ----

inline constexpr const char* RL_OBS_NAMES[RL_OBS_DIM] = {
  "player_buffs.arcanoweave_insight.remains",
  "player_buffs.avatar.remains",
  "player_buffs.bladestorm.remains",
  "player_buffs.bloodlust.remains",
  "player_buffs.collateral_damage.stacks",
  "player_buffs.collateral_damage.remains",
  "player_buffs.colossal_might.stacks",
  "player_buffs.colossal_might.remains",
  "player_buffs.critical_ritual.remains",
  "player_buffs.executioner.stacks",
  "player_buffs.executioner.remains",
  "player_buffs.executioners_precision.stacks",
  "player_buffs.executioners_precision.remains",
  "player_buffs.exhaustion.remains",
  "player_buffs.frenzied_focus.remains",
  "player_buffs.genius_insight.remains",
  "player_buffs.hasty_ritual.remains",
  "player_buffs.imminent_demise.stacks",
  "player_buffs.imminent_demise.remains",
  "player_buffs.masterful_ritual.remains",
  "player_buffs.opportunist.stacks",
  "player_buffs.opportunist.remains",
  "player_buffs.sudden_death.stacks",
  "player_buffs.sudden_death.remains",
  "player_buffs.sweeping_strikes.remains",
  "player_buffs.venomcursed_ascendance.remains",
  "player_buffs.venomcursed_mastery.remains",
  "player_buffs.versatile_ritual.remains",
  "cooldowns.avatar.remains",
  "cooldowns.bladestorm.remains",
  "cooldowns.charge.remains",
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
  "cooldowns.wrecking_throw.remains",
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
  "legality.18.flag",
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
  "hits.colossus_smash",
  "hits.demolish",
  "hits.execute",
  "hits.heroic_strike",
  "hits.mortal_strike",
  "hits.overpower",
  "hits.ravager",
  "hits.rend",
  "hits.slam",
  "hits.whirlwind",
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
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 40.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_4[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 3.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_5[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 10.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_6[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_7[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 3.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_8[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_9[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 600.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_10[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_11[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_12[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_13[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 3.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_14[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_15[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_16[] = {
    { "stacks", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_17[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_18[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_19[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_20[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_PLAYER_BUFFS_MEMBERS[] = {
  { "arcanoweave_insight", "arcanoweave_insight", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_0, 1 },
  { "avatar", "avatar", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_1, 1 },
  { "bladestorm", "bladestorm", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_2, 1 },
  { "bloodlust", "bloodlust", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_3, 1 },
  { "collateral_damage", "collateral_damage", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_4, 2 },
  { "colossal_might", "colossal_might", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_5, 2 },
  { "critical_ritual", "critical_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_6, 1 },
  { "executioner", "executioner", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_7, 2 },
  { "executioners_precision", "executioners_precision", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_8, 2 },
  { "exhaustion", "exhaustion", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_9, 1 },
  { "frenzied_focus", "frenzied_focus", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_10, 1 },
  { "genius_insight", "genius_insight", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_11, 1 },
  { "hasty_ritual", "hasty_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_12, 1 },
  { "imminent_demise", "imminent_demise", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_13, 2 },
  { "masterful_ritual", "masterful_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_14, 1 },
  { "opportunist", "opportunist", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_15, 2 },
  { "sudden_death", "sudden_death", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_16, 2 },
  { "sweeping_strikes", "sweeping_strikes", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_17, 1 },
  { "venomcursed_ascendance", "venomcursed_ascendance", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_18, 1 },
  { "venomcursed_mastery", "venomcursed_mastery", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_19, 1 },
  { "versatile_ritual", "versatile_ritual", RL_OBS_FAMILY_PLAYER_BUFFS_LEAVES_20, 1 },
};

inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_0[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 100.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_1[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 100.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_2[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_3[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_4[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_5[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_6[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_7[] = {
    { "charges_fractional", rl_kind::k_float, 2.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "charges", rl_kind::k_int, 2.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "recharge_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "full_recharge_time", rl_kind::k_seconds, 0.0, false, 1.0, true, 30.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_8[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 100.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_9[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_10[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
};
inline constexpr rl_leaf_desc RL_OBS_FAMILY_COOLDOWNS_LEAVES_11[] = {
    { "remains", rl_kind::k_seconds, 0.0, false, 1.0, true, 60.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_COOLDOWNS_MEMBERS[] = {
  { "avatar", "avatar", RL_OBS_FAMILY_COOLDOWNS_LEAVES_0, 1 },
  { "bladestorm", "bladestorm", RL_OBS_FAMILY_COOLDOWNS_LEAVES_1, 1 },
  { "charge", "charge", RL_OBS_FAMILY_COOLDOWNS_LEAVES_2, 1 },
  { "cleave", "cleave", RL_OBS_FAMILY_COOLDOWNS_LEAVES_3, 1 },
  { "colossus_smash", "colossus_smash", RL_OBS_FAMILY_COOLDOWNS_LEAVES_4, 1 },
  { "demolish", "demolish", RL_OBS_FAMILY_COOLDOWNS_LEAVES_5, 1 },
  { "mortal_strike", "mortal_strike", RL_OBS_FAMILY_COOLDOWNS_LEAVES_6, 1 },
  { "overpower", "overpower", RL_OBS_FAMILY_COOLDOWNS_LEAVES_7, 4 },
  { "ravager", "ravager", RL_OBS_FAMILY_COOLDOWNS_LEAVES_8, 1 },
  { "storm_bolt", "storm_bolt", RL_OBS_FAMILY_COOLDOWNS_LEAVES_9, 1 },
  { "sweeping_strikes", "sweeping_strikes", RL_OBS_FAMILY_COOLDOWNS_LEAVES_10, 1 },
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
    { "rend_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "deep_wounds_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "colossus_smash_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
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
    { "rend_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "deep_wounds_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "colossus_smash_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
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
    { "rend_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "deep_wounds_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "colossus_smash_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
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
    { "rend_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "deep_wounds_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "colossus_smash_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
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
    { "rend_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "deep_wounds_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "colossus_smash_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
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
    { "rend_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "deep_wounds_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "colossus_smash_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
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
    { "rend_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "deep_wounds_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "colossus_smash_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
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
    { "rend_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "deep_wounds_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "colossus_smash_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
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
    { "rend_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "deep_wounds_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "colossus_smash_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
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
    { "rend_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 25.0, nullptr, 0, false },
    { "deep_wounds_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
    { "colossus_smash_remaining", rl_kind::k_seconds, 0.0, false, 1.0, true, 15.0, nullptr, 0, false },
};
inline constexpr rl_obs_member RL_OBS_FAMILY_TARGET_FACTS_MEMBERS[] = {
  { "colossus_smash", "colossus_smash", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_0, 15 },
  { "demolish", "demolish", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_1, 15 },
  { "execute", "execute", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_2, 15 },
  { "heroic_strike", "heroic_strike", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_3, 15 },
  { "mortal_strike", "mortal_strike", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_4, 15 },
  { "overpower", "overpower", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_5, 15 },
  { "rend", "rend", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_6, 15 },
  { "slam", "slam", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_7, 15 },
  { "storm_bolt", "storm_bolt", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_8, 15 },
  { "wrecking_throw", "wrecking_throw", RL_OBS_FAMILY_TARGET_FACTS_LEAVES_9, 15 },
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
inline constexpr rl_leaf_desc RL_OBS_FAMILY_LEGALITY_LEAVES_18[] = {
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
    { "resource.rage", rl_kind::k_float, 0.0, true, 100.0, false, 1.0, nullptr, 0, false },
    { "hits.bladestorm", rl_kind::k_int, 0.0, true, 15.0, false, 1.0, nullptr, 0, false },
    { "hits.cleave", rl_kind::k_int, 0.0, true, 15.0, false, 1.0, nullptr, 0, false },
    { "hits.colossus_smash", rl_kind::k_int, 0.0, true, 15.0, false, 1.0, nullptr, 0, false },
    { "hits.demolish", rl_kind::k_int, 0.0, true, 15.0, false, 1.0, nullptr, 0, false },
    { "hits.execute", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "hits.heroic_strike", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "hits.mortal_strike", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "hits.overpower", rl_kind::k_int, 0.0, true, 15.0, false, 1.0, nullptr, 0, false },
    { "hits.ravager", rl_kind::k_int, 0.0, true, 15.0, false, 1.0, nullptr, 0, false },
    { "hits.rend", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "hits.slam", rl_kind::k_int, 0.0, true, 2.0, false, 1.0, nullptr, 0, false },
    { "hits.whirlwind", rl_kind::k_int, 0.0, true, 15.0, false, 1.0, nullptr, 0, false },
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
  { "", "", RL_OBS_FAMILY_SCALARS_LEAVES_0, 173 },
};

inline constexpr std::size_t RL_OBS_FAMILY_COUNT = 8;

inline constexpr rl_obs_family RL_OBS_FAMILIES[RL_OBS_FAMILY_COUNT] = {
  { rl_family::player_buffs, "player_buffs", rl_family_kind::buff, false, RL_OBS_FAMILY_PLAYER_BUFFS_MEMBERS, 21, 0, 28 },
  { rl_family::cooldowns, "cooldowns", rl_family_kind::cooldown, false, RL_OBS_FAMILY_COOLDOWNS_MEMBERS, 12, 28, 15 },
  { rl_family::target_facts, "target_facts", rl_family_kind::target_fact, false, RL_OBS_FAMILY_TARGET_FACTS_MEMBERS, 10, 43, 150 },
  { rl_family::stats, "stats", rl_family_kind::direct, false, RL_OBS_FAMILY_STATS_MEMBERS, 8, 193, 8 },
  { rl_family::swing_cast, "swing_cast", rl_family_kind::direct, false, RL_OBS_FAMILY_SWING_CAST_MEMBERS, 3, 201, 3 },
  { rl_family::raid_events, "raid_events", rl_family_kind::expression, false, RL_OBS_FAMILY_RAID_EVENTS_MEMBERS, 2, 204, 10 },
  { rl_family::legality, "legality", rl_family_kind::legality, false, RL_OBS_FAMILY_LEGALITY_MEMBERS, 19, 214, 19 },
  { rl_family::scalars, "scalars", rl_family_kind::scalar, true, RL_OBS_FAMILY_SCALARS_MEMBERS, 1, 233, 173 },
};

// ---- Action descriptors ----

inline constexpr rl_action_desc RL_ACTIONS[RL_ACTION_DIM] = {
  { 0, "avatar", rl_action_kind::cast, "avatar", nullptr, "avatar", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 1, "bladestorm", rl_action_kind::cast, "bladestorm", nullptr, "bladestorm", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 2, "charge", rl_action_kind::cast, "charge", nullptr, "charge", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
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
  { 13, "storm_bolt", rl_action_kind::cast, "storm_bolt", nullptr, "storm_bolt", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 14, "sweeping_strikes", rl_action_kind::cast, "sweeping_strikes", nullptr, "sweeping_strikes", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 15, "whirlwind", rl_action_kind::cast, nullptr, nullptr, "whirlwind", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 16, "wrecking_throw", rl_action_kind::cast, "wrecking_throw", nullptr, "wrecking_throw", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 17, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_next_event", { rl_wait_anchor_kind::none, rl_swing_hand::none }, nullptr },
  { 18, nullptr, rl_action_kind::wait, nullptr, nullptr, "wait_swing_mh", { rl_wait_anchor_kind::swing, rl_swing_hand::mh }, nullptr },
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

inline constexpr std::size_t RL_CAPABILITY_COUNT = 138;
inline constexpr rl_capability RL_CAPABILITIES[RL_CAPABILITY_COUNT] = {
  { "talent_fast_footwork", 268, "talent", nullptr, 0, nullptr, 0, 112206, 1, 0, 0, 0, 0, 0, 0 },
  { "talent_war_machine", 269, "talent", nullptr, 0, nullptr, 0, 112185, 1, 0, 0, 0, 0, 0, 0 },
  { "talent_thunder_clap", 270, "talent", nullptr, 0, nullptr, 0, 112205, 1, 0, 0, 0, 0, 0, 0 },
  { "talent_leeching_strikes", 271, "talent", nullptr, 0, nullptr, 0, 112238, 1, 0, 0, 0, 0, 0, 0 },
  { "talent_impending_victory", 272, "talent", nullptr, 0, nullptr, 0, 112183, 1, 0, 0, 0, 0, 0, 0 },
  { "talent_heroic_leap", 273, "talent", nullptr, 0, nullptr, 0, 112208, 1, 0, 0, 0, 0, 0, 0 },
  { "talent_crackling_thunder", 274, "talent", nullptr, 0, nullptr, 0, 118853, 1, 0, 0, 0, 0, 0, 0 },
  { "talent_storm_bolt", 275, "talent", nullptr, 0, nullptr, 0, 112198, 1, 0, 0, 0, 17, 0, 1 },
  { "talent_rend", 276, "talent", nullptr, 0, nullptr, 0, 135597, 1, 0, 0, 17, 16, 1, 1 },
  { "talent_second_wind", 277, "talent", nullptr, 0, nullptr, 0, 112190, 1, 0, 0, 33, 0, 2, 0 },
  { "talent_frothing_berserker", 278, "talent", nullptr, 0, nullptr, 0, 112216, 1, 0, 0, 33, 0, 2, 0 },
  { "talent_bounding_stride", 279, "talent", nullptr, 0, nullptr, 0, 112219, 1, 0, 0, 33, 0, 2, 0 },
  { "talent_pain_and_gain", 280, "talent", nullptr, 0, nullptr, 0, 112217, 1, 0, 0, 33, 0, 2, 0 },
  { "talent_intervene", 281, "talent", nullptr, 0, nullptr, 0, 134217, 1, 0, 0, 33, 0, 2, 0 },
  { "talent_interpose", 282, "talent", nullptr, 0, nullptr, 0, 134216, 1, 0, 0, 33, 0, 2, 0 },
  { "talent_shockwave", 283, "talent", nullptr, 0, nullptr, 0, 112242, 1, 0, 0, 33, 0, 2, 0 },
  { "talent_overwhelming_rage", 284, "talent", nullptr, 0, nullptr, 0, 112245, 1, 0, 0, 33, 0, 2, 0 },
  { "talent_rallying_cry", 285, "talent", nullptr, 0, nullptr, 0, 112188, 1, 0, 0, 33, 0, 2, 0 },
  { "talent_field_dressing", 286, "talent", nullptr, 0, nullptr, 0, 136626, 1, 0, 0, 33, 0, 2, 0 },
  { "talent_spell_reflection", 287, "talent", nullptr, 0, nullptr, 0, 112253, 1, 0, 0, 33, 0, 2, 0 },
  { "talent_wrecking_throw", 288, "talent", nullptr, 0, nullptr, 0, 112215, 1, 0, 0, 33, 17, 2, 1 },
  { "talent_shattering_throw", 289, "talent", nullptr, 0, nullptr, 0, 112214, 1, 0, 0, 50, 0, 3, 0 },
  { "talent_rumbling_earth", 290, "talent", nullptr, 0, nullptr, 0, 112241, 1, 0, 0, 50, 0, 3, 0 },
  { "talent_berserker_shout", 291, "talent", nullptr, 0, nullptr, 0, 112211, 1, 0, 0, 50, 0, 3, 0 },
  { "talent_fearless", 292, "talent", nullptr, 0, nullptr, 0, 112210, 1, 0, 0, 50, 0, 3, 0 },
  { "talent_intimidating_shout", 293, "talent", nullptr, 0, nullptr, 0, 134254, 1, 0, 0, 50, 0, 3, 0 },
  { "talent_piercing_howl", 294, "talent", nullptr, 0, nullptr, 0, 136627, 1, 0, 0, 50, 0, 3, 0 },
  { "talent_honed_reflexes", 295, "talent", nullptr, 0, nullptr, 0, 118850, 1, 0, 0, 50, 0, 3, 0 },
  { "talent_armored_to_the_teeth", 296, "talent", nullptr, 0, nullptr, 0, 112233, 1, 0, 0, 50, 0, 3, 0 },
  { "talent_armored_to_the_teeth_r2", 297, "talent", nullptr, 0, nullptr, 0, 112233, 2, 0, 1, 50, 0, 3, 0 },
  { "talent_reinforced_plates", 298, "talent", nullptr, 0, nullptr, 0, 112235, 1, 1, 0, 50, 0, 3, 0 },
  { "talent_reinforced_plates_r2", 299, "talent", nullptr, 0, nullptr, 0, 112235, 2, 1, 1, 50, 0, 3, 0 },
  { "talent_double_time", 300, "talent", nullptr, 0, nullptr, 0, 112249, 1, 2, 0, 50, 0, 3, 0 },
  { "talent_barbaric_training", 301, "talent", nullptr, 0, nullptr, 0, 112209, 1, 2, 0, 50, 0, 3, 0 },
  { "talent_javelineer", 302, "talent", nullptr, 0, nullptr, 0, 136625, 1, 2, 0, 50, 0, 3, 0 },
  { "talent_resonant_voice", 303, "talent", nullptr, 0, nullptr, 0, 134225, 1, 2, 0, 50, 0, 3, 0 },
  { "talent_crushing_force", 304, "talent", nullptr, 0, nullptr, 0, 134226, 1, 2, 0, 50, 0, 3, 0 },
  { "talent_cruel_strikes", 305, "talent", nullptr, 0, nullptr, 0, 112248, 1, 2, 0, 50, 0, 3, 0 },
  { "talent_cruel_strikes_r2", 306, "talent", nullptr, 0, nullptr, 0, 112248, 2, 2, 1, 50, 0, 3, 0 },
  { "talent_twohanded_weapon_specialization", 307, "talent", nullptr, 0, nullptr, 0, 112179, 1, 3, 0, 50, 0, 3, 0 },
  { "talent_twohanded_weapon_specialization_r2", 308, "talent", nullptr, 0, nullptr, 0, 112179, 2, 3, 1, 50, 0, 3, 0 },
  { "talent_wild_strikes", 309, "talent", nullptr, 0, nullptr, 0, 112224, 1, 4, 0, 50, 0, 3, 0 },
  { "talent_wild_strikes_r2", 310, "talent", nullptr, 0, nullptr, 0, 112224, 2, 4, 1, 50, 0, 3, 0 },
  { "talent_anger_management", 311, "talent", nullptr, 0, nullptr, 0, 134032, 1, 5, 0, 50, 0, 3, 0 },
  { "talent_champions_spear", 312, "talent", nullptr, 0, nullptr, 0, 112247, 1, 5, 0, 50, 0, 3, 0 },
  { "talent_stance_mastery", 313, "talent", nullptr, 0, nullptr, 0, 134031, 1, 5, 0, 50, 0, 3, 0 },
  { "talent_battlefield_commander", 314, "talent", nullptr, 0, nullptr, 0, 134033, 1, 5, 0, 50, 0, 3, 0 },
  { "talent_mortal_strike", 315, "talent", nullptr, 0, nullptr, 0, 112122, 1, 5, 0, 50, 17, 3, 1 },
  { "talent_overpower", 316, "talent", nullptr, 0, nullptr, 0, 112123, 1, 5, 0, 67, 20, 4, 1 },
  { "talent_sudden_death", 317, "talent", nullptr, 0, nullptr, 0, 112126, 1, 5, 0, 87, 2, 5, 0 },
  { "talent_fueled_by_violence", 318, "talent", nullptr, 0, nullptr, 0, 112128, 1, 5, 0, 89, 0, 5, 0 },
  { "talent_ignore_pain", 319, "talent", nullptr, 0, nullptr, 0, 136701, 1, 5, 0, 89, 0, 5, 0 },
  { "talent_die_by_the_sword", 320, "talent", nullptr, 0, nullptr, 0, 112121, 1, 5, 0, 89, 0, 5, 0 },
  { "talent_bloodsurge", 321, "talent", nullptr, 0, nullptr, 0, 112129, 1, 5, 0, 89, 0, 5, 0 },
  { "talent_improved_overpower", 322, "talent", nullptr, 0, nullptr, 0, 112131, 1, 5, 0, 89, 0, 5, 0 },
  { "talent_improved_execute", 323, "talent", nullptr, 0, nullptr, 0, 112125, 1, 5, 0, 89, 0, 5, 0 },
  { "talent_fervor_of_battle", 324, "talent", nullptr, 0, nullptr, 0, 135932, 1, 5, 0, 89, 0, 5, 0 },
  { "talent_tactician", 325, "talent", nullptr, 0, nullptr, 0, 112134, 1, 5, 0, 89, 0, 5, 0 },
  { "talent_colossus_smash", 326, "talent", nullptr, 0, nullptr, 0, 112144, 1, 5, 0, 89, 17, 5, 1 },
  { "talent_impale", 327, "talent", nullptr, 0, nullptr, 0, 112146, 1, 5, 0, 106, 0, 6, 0 },
  { "talent_brute_force", 328, "talent", nullptr, 0, nullptr, 0, 135936, 1, 5, 0, 106, 0, 6, 0 },
  { "talent_efficiency", 329, "talent", nullptr, 0, nullptr, 0, 135935, 1, 5, 0, 106, 0, 6, 0 },
  { "talent_overpowering_finish", 330, "talent", nullptr, 0, nullptr, 0, 114733, 1, 5, 0, 106, 0, 6, 0 },
  { "talent_mass_execution", 331, "talent", nullptr, 0, nullptr, 0, 137473, 1, 5, 0, 106, 0, 6, 0 },
  { "talent_strength_of_arms", 332, "talent", nullptr, 0, nullptr, 0, 135944, 1, 5, 0, 106, 0, 6, 0 },
  { "talent_strength_of_arms_r2", 333, "talent", nullptr, 0, nullptr, 0, 135944, 2, 5, 1, 106, 0, 6, 0 },
  { "talent_just_warming_up", 334, "talent", nullptr, 0, nullptr, 0, 135943, 1, 6, 0, 106, 0, 6, 0 },
  { "talent_broad_strokes", 335, "talent", nullptr, 0, nullptr, 0, 135942, 1, 6, 0, 106, 0, 6, 0 },
  { "talent_sharpened_blades", 336, "talent", nullptr, 0, nullptr, 0, 112320, 1, 6, 0, 106, 0, 6, 0 },
  { "talent_sharpened_blades_r2", 337, "talent", nullptr, 0, nullptr, 0, 112320, 2, 6, 1, 106, 0, 6, 0 },
  { "talent_cleave", 338, "talent", nullptr, 0, nullptr, 0, 112147, 1, 7, 0, 106, 2, 6, 1 },
  { "talent_powerful_momentum", 339, "talent", nullptr, 0, nullptr, 0, 114739, 1, 7, 0, 108, 0, 7, 0 },
  { "talent_martial_prowess", 340, "talent", nullptr, 0, nullptr, 0, 135934, 1, 7, 0, 108, 0, 7, 0 },
  { "talent_dreadnaught", 341, "talent", nullptr, 0, nullptr, 0, 112137, 1, 7, 0, 108, 0, 7, 0 },
  { "talent_deep_wounds", 342, "talent", nullptr, 0, nullptr, 0, 135940, 1, 7, 0, 108, 0, 7, 0 },
  { "talent_tactical_edge", 343, "talent", nullptr, 0, nullptr, 0, 135939, 1, 7, 0, 108, 0, 7, 0 },
  { "talent_crushing_combo", 344, "talent", nullptr, 0, nullptr, 0, 135938, 1, 7, 0, 108, 0, 7, 0 },
  { "talent_massacre", 345, "talent", nullptr, 0, nullptr, 0, 112145, 1, 7, 0, 108, 0, 7, 0 },
  { "talent_collateral_damage", 346, "talent", nullptr, 0, nullptr, 0, 135937, 1, 7, 0, 108, 2, 7, 0 },
  { "talent_bloodborne", 347, "talent", nullptr, 0, nullptr, 0, 112135, 1, 7, 0, 110, 0, 7, 0 },
  { "talent_bloodborne_r2", 348, "talent", nullptr, 0, nullptr, 0, 112135, 2, 7, 1, 110, 0, 7, 0 },
  { "talent_bladestorm", 349, "talent", nullptr, 0, nullptr, 0, 112314, 1, 8, 0, 110, 2, 7, 1 },
  { "talent_ravager", 350, "talent", nullptr, 0, nullptr, 0, 136702, 1, 8, 0, 112, 2, 8, 1 },
  { "talent_critical_thinking", 351, "talent", nullptr, 0, nullptr, 0, 112317, 1, 8, 0, 114, 0, 9, 0 },
  { "talent_critical_thinking_r2", 352, "talent", nullptr, 0, nullptr, 0, 112317, 2, 8, 1, 114, 0, 9, 0 },
  { "talent_master_tactician", 353, "talent", nullptr, 0, nullptr, 0, 114740, 1, 9, 0, 114, 0, 9, 0 },
  { "talent_bloodletting", 354, "talent", nullptr, 0, nullptr, 0, 112310, 1, 9, 0, 114, 0, 9, 0 },
  { "talent_executioners_precision", 355, "talent", nullptr, 0, nullptr, 0, 112318, 1, 9, 0, 114, 2, 9, 0 },
  { "talent_fatality", 356, "talent", nullptr, 0, nullptr, 0, 112311, 1, 9, 0, 116, 0, 9, 0 },
  { "talent_battlelord", 357, "talent", nullptr, 0, nullptr, 0, 135933, 1, 9, 0, 116, 0, 9, 0 },
  { "talent_mortal_wounds", 358, "talent", nullptr, 0, nullptr, 0, 135941, 1, 9, 0, 116, 0, 9, 0 },
  { "talent_avatar", 359, "talent", nullptr, 0, nullptr, 0, 136703, 1, 9, 0, 116, 2, 9, 1 },
  { "talent_master_of_warfare", 360, "talent", nullptr, 0, nullptr, 0, 136989, 1, 9, 0, 118, 0, 10, 0 },
  { "talent_master_of_warfare_r2", 361, "talent", nullptr, 0, nullptr, 0, 136988, 1, 9, 1, 118, 0, 10, 0 },
  { "talent_master_of_warfare_r3", 362, "talent", nullptr, 0, nullptr, 0, 136988, 2, 10, 1, 118, 0, 10, 0 },
  { "talent_master_of_warfare_r4", 363, "talent", nullptr, 0, nullptr, 0, 136987, 1, 11, 1, 118, 0, 10, 0 },
  { "talent_martial_expert", 364, "talent", nullptr, 0, nullptr, 0, 117409, 1, 12, 0, 118, 0, 10, 0 },
  { "talent_colossal_might", 365, "talent", nullptr, 0, nullptr, 0, 117416, 1, 12, 0, 118, 2, 10, 0 },
  { "talent_boneshaker", 366, "talent", nullptr, 0, nullptr, 0, 117386, 1, 12, 0, 120, 0, 10, 0 },
  { "talent_earthquaker", 367, "talent", nullptr, 0, nullptr, 0, 119858, 1, 12, 0, 120, 0, 10, 0 },
  { "talent_decimator", 368, "talent", nullptr, 0, nullptr, 0, 136073, 1, 12, 0, 120, 0, 10, 0 },
  { "talent_one_against_many", 369, "talent", nullptr, 0, nullptr, 0, 117396, 1, 12, 0, 120, 0, 10, 0 },
  { "talent_arterial_bleed", 370, "talent", nullptr, 0, nullptr, 0, 119856, 1, 12, 0, 120, 0, 10, 0 },
  { "talent_tide_of_battle", 371, "talent", nullptr, 0, nullptr, 0, 117408, 1, 12, 0, 120, 0, 10, 0 },
  { "talent_no_stranger_to_pain", 372, "talent", nullptr, 0, nullptr, 0, 117412, 1, 12, 0, 120, 0, 10, 0 },
  { "talent_veteran_vitality", 373, "talent", nullptr, 0, nullptr, 0, 119857, 1, 12, 0, 120, 0, 10, 0 },
  { "talent_cut_to_the_bone", 374, "talent", nullptr, 0, nullptr, 0, 136072, 1, 12, 0, 120, 0, 10, 0 },
  { "talent_practiced_strikes", 375, "talent", nullptr, 0, nullptr, 0, 117393, 1, 12, 0, 120, 0, 10, 0 },
  { "talent_precise_might", 376, "talent", nullptr, 0, nullptr, 0, 117391, 1, 12, 0, 120, 0, 10, 0 },
  { "talent_mountain_of_muscle_and_scars", 377, "talent", nullptr, 0, nullptr, 0, 117403, 1, 12, 0, 120, 0, 10, 0 },
  { "talent_celeritous_conclusion", 378, "talent", nullptr, 0, nullptr, 0, 136071, 1, 12, 0, 120, 0, 10, 0 },
  { "talent_dominance_of_the_colossus", 379, "talent", nullptr, 0, nullptr, 0, 117390, 1, 12, 0, 120, 0, 10, 0 },
  { "talent_imminent_demise", 380, "talent", nullptr, 0, nullptr, 0, 117385, 1, 12, 0, 120, 2, 10, 0 },
  { "talent_overwhelming_blades", 381, "talent", nullptr, 0, nullptr, 0, 117407, 1, 12, 0, 122, 0, 10, 0 },
  { "talent_relentless_pursuit", 382, "talent", nullptr, 0, nullptr, 0, 117392, 1, 12, 0, 122, 0, 10, 0 },
  { "talent_vicious_agility", 383, "talent", nullptr, 0, nullptr, 0, 123408, 1, 12, 0, 122, 0, 10, 0 },
  { "talent_violent_euphoria", 384, "talent", nullptr, 0, nullptr, 0, 136076, 1, 12, 0, 122, 0, 10, 0 },
  { "talent_death_drive", 385, "talent", nullptr, 0, nullptr, 0, 117410, 1, 12, 0, 122, 0, 10, 0 },
  { "talent_culling_cyclone", 386, "talent", nullptr, 0, nullptr, 0, 117383, 1, 12, 0, 122, 0, 10, 0 },
  { "talent_brutal_finish", 387, "talent", nullptr, 0, nullptr, 0, 123409, 1, 12, 0, 122, 0, 10, 0 },
  { "talent_fierce_followthrough", 388, "talent", nullptr, 0, nullptr, 0, 117384, 1, 12, 0, 122, 0, 10, 0 },
  { "talent_opportunist", 389, "talent", nullptr, 0, nullptr, 0, 123770, 1, 12, 0, 122, 2, 10, 0 },
  { "talent_deadly_focus", 390, "talent", nullptr, 0, nullptr, 0, 136075, 1, 12, 0, 124, 0, 10, 0 },
  { "talent_show_no_mercy", 391, "talent", nullptr, 0, nullptr, 0, 117381, 1, 12, 0, 124, 0, 10, 0 },
  { "talent_reap_the_storm", 392, "talent", nullptr, 0, nullptr, 0, 117406, 1, 12, 0, 124, 0, 10, 0 },
  { "talent_slayers_malice", 393, "talent", nullptr, 0, nullptr, 0, 117398, 1, 12, 0, 124, 0, 10, 0 },
  { "talent_unhinged", 394, "talent", nullptr, 0, nullptr, 0, 136074, 1, 12, 0, 124, 0, 10, 0 },
  { "talent_unrelenting_onslaught", 395, "talent", nullptr, 0, nullptr, 0, 117417, 1, 12, 0, 124, 0, 10, 0 },
  { "hero_colossus", 396, "talent", nullptr, 0, nullptr, 0, 117415, 1, 12, 0, 124, 17, 10, 1 },
  { "hero_slayer", 397, "talent", nullptr, 0, nullptr, 0, 117411, 1, 12, 0, 141, 0, 11, 0 },
  { "enchant_arcane_mastery", 398, "special_effect", nullptr, 1236721, nullptr, 0, 0, 0, 12, 0, 141, 1, 11, 0 },
  { "enchant_berserkers_rage", 399, "special_effect", nullptr, 1236728, nullptr, 0, 0, 0, 12, 0, 142, 1, 11, 0 },
  { "embellishment_arcanoweave_lining", 400, "special_effect", nullptr, 1283697, nullptr, 0, 0, 0, 12, 0, 143, 1, 11, 0 },
  { "embellishment_hunters_ritual_stone", 401, "special_effect", nullptr, 1297382, nullptr, 0, 0, 0, 12, 0, 144, 4, 11, 0 },
  { "item_venomcursed_mastery", 402, "special_effect", nullptr, 1307923, nullptr, 0, 0, 0, 12, 0, 148, 1, 11, 0 },
  { "item_venomcursed_ascendance", 403, "special_effect", nullptr, 1317582, nullptr, 0, 0, 0, 12, 0, 149, 1, 11, 0 },
  { "set_bite_of_zuljan_2pc", 404, "set_bonus", nullptr, 0, "MID_BOZ", 2, 0, 0, 12, 0, 150, 0, 11, 0 },
  { "mode_funnel", 405, "funnel_mode", nullptr, 0, nullptr, 0, 0, 0, 12, 0, 150, 0, 11, 0 },
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

inline constexpr std::size_t RL_CAPABILITY_GOVERNED_SLOTS_COUNT = 150;
inline constexpr std::size_t RL_CAPABILITY_GOVERNED_SLOTS[RL_CAPABILITY_GOVERNED_SLOTS_COUNT] = {
  40,
  227,
  163,
  164,
  165,
  166,
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
  225,
  133,
  134,
  135,
  136,
  137,
  138,
  139,
  140,
  141,
  142,
  143,
  144,
  145,
  146,
  147,
  42,
  230,
  178,
  179,
  180,
  181,
  182,
  183,
  184,
  185,
  186,
  187,
  188,
  189,
  190,
  191,
  192,
  34,
  222,
  103,
  104,
  105,
  106,
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
  35,
  36,
  37,
  38,
  223,
  118,
  119,
  120,
  121,
  122,
  123,
  124,
  125,
  126,
  127,
  128,
  129,
  130,
  131,
  132,
  22,
  23,
  32,
  218,
  43,
  44,
  45,
  46,
  47,
  48,
  49,
  50,
  51,
  52,
  53,
  54,
  55,
  56,
  57,
  31,
  217,
  4,
  5,
  29,
  215,
  39,
  224,
  11,
  12,
  28,
  214,
  6,
  7,
  17,
  18,
  20,
  21,
  33,
  219,
  58,
  59,
  60,
  61,
  62,
  63,
  64,
  65,
  66,
  67,
  68,
  69,
  70,
  71,
  72,
  15,
  14,
  0,
  8,
  16,
  19,
  27,
  26,
  25,
};

inline constexpr std::size_t RL_CAPABILITY_GOVERNED_ACTIONS_COUNT = 11;
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
};

inline constexpr double RL_CAPABILITY_FIXED_VALUE = 0.0;
inline constexpr const char* RL_NET_LAYOUT_SHA = "rl-layout-v1:e004ca500673be079dcc3983da5d663b52b7a1f0e6328161323e8a0336d00994";

// ---- Target scorer feature list (Phase 230-02, SCOR-01) ----

inline constexpr std::size_t RL_TARGET_SLOTS = 16;
inline constexpr std::size_t RL_TARGET_FEATURES = 15;
inline constexpr const char* RL_TARGET_FEATURE_SHA = "tgt-feat-v1:b69ebaf49134d7b0fc4222007823e58baa959630ba16a230f6c4d20d5f0a059d";

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
  { "rend_remaining", rl_kind::k_seconds, false, 1.0, true, 20.0, -1 },
  { "deep_wounds_remaining", rl_kind::k_seconds, false, 1.0, true, 8.0, -1 },
  { "colossus_smash_remaining", rl_kind::k_seconds, false, 1.0, true, 10.0, -1 },
};

inline constexpr std::size_t RL_AIM_CONTEXT_COUNT = 1;
inline constexpr std::size_t RL_AIM_CONTEXT_OBS_SLOTS[RL_AIM_CONTEXT_COUNT] = { 405 };
inline constexpr const char* RL_AIM_CONTEXT_NAMES[RL_AIM_CONTEXT_COUNT] = {
  "capability.mode_funnel",
};

inline constexpr std::size_t RL_AIM_SPELL_COUNT = 10;
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
};

inline constexpr std::size_t RL_AIM_INPUT_COUNT = 16;
inline constexpr const char* RL_AIM_SHA = "aim-v1:fbac804f577118742223ae77653b7aeae2b1d4e0923288a35b051173bf6d5d91";


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

inline constexpr std::size_t RL_TARGETED_TOKEN_COUNT = 10;
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
};
inline constexpr double RL_AIMED_SPELL_RANGE_YARDS[RL_TARGETED_TOKEN_COUNT] = {5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 0.0, 5.0, 30.0, 25.0};
inline constexpr const char* RL_TAG_MELEE_ACTION = "mortal_strike";

inline constexpr std::size_t RL_RULE_PREF_COUNT = 10;
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
};

inline constexpr std::size_t RL_DECLARED_FACT_COUNT = 3;
inline constexpr rl_declared_fact RL_DECLARED_FACTS[RL_DECLARED_FACT_COUNT] = {
  { rl_fact_kind::dot_remaining, "rend_dot", 12 },
  { rl_fact_kind::dot_remaining, "deep_wounds", 13 },
  { rl_fact_kind::dot_remaining, "colossus_smash", 14 },
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

inline constexpr std::size_t RL_HIT_PROVIDER_COUNT = 12;
inline constexpr rl_hit_provider RL_HIT_PROVIDERS[RL_HIT_PROVIDER_COUNT] = {
  { "hits", "bladestorm", "census.bladestorm" },
  { "hits", "cleave", "census.cleave" },
  { "hits", "colossus_smash", "census.colossus_smash" },
  { "hits", "demolish", "census.demolish" },
  { "hits", "execute", "census.execute" },
  { "hits", "heroic_strike", "census.heroic_strike" },
  { "hits", "mortal_strike", "census.mortal_strike" },
  { "hits", "overpower", "census.overpower" },
  { "hits", "ravager", "census.ravager" },
  { "hits", "rend", "census.rend" },
  { "hits", "slam", "census.slam" },
  { "hits", "whirlwind", "census.whirlwind" },
};
