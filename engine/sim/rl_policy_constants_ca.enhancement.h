#pragma once

// Hand-written by Phase 268 plan 01 (v2.17 "The generic net backbone"): enhancement's C-A fields.
//
// C-A is the cross-phase contract for the new fields of a spec's generated constants header (see
// .planning/REQUIREMENTS.md, contract C-A in the tstl-sylvanas repository). The generated enhancement
// header, engine/sim/rl_policy_constants.h, stays byte-identical to what scripts/rl/gen_rl_constants.py
// emits today (repin.js --preflight regenerates it and requires equality), so the new fields live here.
//
// This file is included ONLY by engine/sim/rl_policy_constants_select.h, and only while the generated
// header lacks the marker macro RL_HEADER_CA_VERSION. When Phase 267's regenerated header (which carries
// the marker and the same fields) lands in Phase 270, the guard skips this file with no C++ change and
// this file can then be deleted. The value pins in rl_header_contract.hpp are what prove, at that point,
// that the regenerated header carries today's values.
//
// Conventions: every name is `inline constexpr` at namespace scope, like the generated header's own
// constants. Every variable-length table has a <NAME>_COUNT constant of type std::size_t; a spec with no
// rows has COUNT 0 and ONE all-null sentinel row (a zero-length array is not valid C++); every reader
// iterates by COUNT, never by the array size.

#include <cstddef>

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

// ---- C-A data (enhancement) ----

// The nine targeted tokens, in today's order (rl_target_select.cpp TARGETED_TOKENS). NOT the seven
// aimed spells: primordial_storm and flame_shock are targeted but not aimed by the head.
inline constexpr std::size_t RL_TARGETED_TOKEN_COUNT = 9;
inline constexpr const char* RL_TARGETED_TOKENS[RL_TARGETED_TOKEN_COUNT] = {
  "stormstrike", "lightning_bolt", "chain_lightning", "tempest",
  "windstrike",  "lava_lash",      "voltaic_blaze",   "primordial_storm",
  "flame_shock",
};

// The chooser's ranged can-be-hit probe and its preferred melee action.
inline constexpr const char* RL_CHOOSER_PROBE_ACTION = "lightning_bolt";
inline constexpr const char* RL_CHOOSER_MELEE_ACTION = "stormstrike";

// One rule per targeted token (from rl_target_select.cpp preference_for). Generic rules: current_target,
// shortest_ttd. A class rule is spelled <class>.<name> and resolved through that class's plugin.
inline constexpr std::size_t RL_RULE_PREF_COUNT = 9;
inline constexpr rl_rule_pref RL_RULE_PREFS[RL_RULE_PREF_COUNT] = {
  { "stormstrike",       "shaman.thorims_aware_strike" },
  { "windstrike",        "shaman.thorims_aware_strike" },
  { "primordial_storm",  "shortest_ttd" },
  { "lightning_bolt",    "shortest_ttd" },
  { "lava_lash",         "shaman.lava_lash" },
  { "voltaic_blaze",     "shaman.voltaic_blaze" },
  { "chain_lightning",   "shaman.chain_lightning" },
  { "tempest",           "shaman.tempest" },
  { "flame_shock",       "shaman.voltaic_blaze" },
};

// Declared per-candidate facts; slot = index into RL_TARGET_FEATURE_NAMES.
inline constexpr std::size_t RL_DECLARED_FACT_COUNT = 8;
inline constexpr rl_declared_fact RL_DECLARED_FACTS[RL_DECLARED_FACT_COUNT] = {
  { rl_fact_kind::dot_remaining,  "flame_shock",                       10 },
  { rl_fact_kind::dot_remaining,  "burning_core",                      13 },
  { rl_fact_kind::debuff_stacks,  "lightning_rod",                     14 },
  { rl_fact_kind::dot_remaining,  "lightning_rod",                     15 },
  { rl_fact_kind::dot_remaining,  "venomfang",                         16 },
  { rl_fact_kind::debuff_stacks,  "venomfang_debuff",                  17 },
  { rl_fact_kind::dot_remaining,  "venomfang_debuff",                  18 },
  { rl_fact_kind::dot_remaining,  "rune_of_unleashed_fire_lingering",  19 },
};

// The engine resource name accepted by util::parse_resource_type (maelstrom_weapon is a buff, not this).
inline constexpr const char* RL_RESOURCE_NAME = "maelstrom";

// Wait vocabulary. wait_maelstrom's row below is NEVER consulted: RL_ACTIONS row 14 carries its own
// anchor (the earlier of the next main-hand and off-hand swing), and that legacy anchor stays
// authoritative for any wait whose legacy anchor kind is not `none`.
inline constexpr std::size_t RL_WAIT_DEF_COUNT = 4;
inline constexpr rl_wait_def RL_WAIT_DEFS[RL_WAIT_DEF_COUNT] = {
  { "wait_next_event", rl_wait_kind::next_event,         0.0 },
  { "wait_swing_mh",   rl_wait_kind::swing_mh,           0.0 },
  { "wait_swing_oh",   rl_wait_kind::swing_oh,           0.0 },
  { "wait_maelstrom",  rl_wait_kind::resource_threshold, 0.0 },
};

// Cooldown-row to action alias (replaces the "strike" -> "stormstrike" literal in rl_policy_obs.cpp).
inline constexpr std::size_t RL_COOLDOWN_ROW_ACTION_COUNT = 1;
inline constexpr rl_cooldown_alias RL_COOLDOWN_ROW_ACTION[RL_COOLDOWN_ROW_ACTION_COUNT] = {
  { "strike", "stormstrike" },
};

// Proc names, fixed count 20, same order as rl_proc::NAMES.
inline constexpr std::size_t RL_PROC_NAME_COUNT = 20;
inline constexpr const char* RL_PROC_NAMES[RL_PROC_NAME_COUNT] = {
  "crit_hit", "auto_attack_hit", "windfury", "unruly_winds", "stormsurge", "stormflurry", "thunder_capacitor",
  "maelstrom_weapon_gain", "elemental_assault", "fire_nova_vb", "awakening_storms_roll", "awakening_storms_rppm",
  "tempest_deck", "asc_dw_deck", "storm_unleashed_deck", "flurry_trigger", "dbc_proc_callback", "buff_chance_trigger",
  "spare_18", "spare_19" };

// Hit-count providers (family "hits"); a provider is spelled <class>.<name>.
inline constexpr std::size_t RL_HIT_PROVIDER_COUNT = 7;
inline constexpr rl_hit_provider RL_HIT_PROVIDERS[RL_HIT_PROVIDER_COUNT] = {
  { "hits", "chain_lightning",                 "shaman.chain_lightning" },
  { "hits", "tempest",                         "shaman.tempest" },
  { "hits", "crash_lightning",                 "shaman.crash_lightning" },
  { "hits", "lava_lash.flame_shock_spread",    "shaman.lava_lash_flame_shock_spread" },
  { "hits", "voltaic_blaze.cleave",            "shaman.voltaic_blaze_cleave" },
  { "hits", "voltaic_blaze.new_flame_shocks",  "shaman.voltaic_blaze_new_flame_shocks" },
  { "hits", "fire_nova",                       "shaman.fire_nova" },
};
