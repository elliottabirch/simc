// ==========================================================================
// Compile-time pins of the C-A field set (Phase 268 plan 01).
//
// Included last by sim/rl_policy_constants_select.h, so every translation unit that sees a spec's
// constants also compiles these pins. A header that lacks a C-A field, spells it with another type, or
// violates a cross-table rule does not compile. Phase 270 swaps the arms header and enhancement's
// supplement for Phase 267's generated headers; the same pins then prove that 267's output carries the
// field set (and, for enhancement, today's values: every table the supplement carries is pinned by value below,
// fix pass WR-02).
// ==========================================================================

#pragma once

#include <cstddef>
#include <type_traits>

#include "sim/rl_proc_counters.hpp"

namespace rl_contract
{

constexpr bool streq( const char* a, const char* b )
{
  if ( a == nullptr || b == nullptr )
    return a == b;
  while ( *a != '\0' && *a == *b )
  {
    ++a;
    ++b;
  }
  return *a == *b;
}

template <typename T, typename Elem>
inline constexpr bool array_of_v = std::is_same_v<std::remove_cv_t<std::remove_extent_t<T>>, Elem>;

template <typename T, typename Elem>
inline constexpr bool scalar_of_v = std::is_same_v<std::remove_cv_t<T>, Elem>;

constexpr std::size_t table_extent( std::size_t count )
{
  return count == 0 ? 1 : count;   // a table with no rows holds one all-null sentinel row
}

constexpr bool is_targeted_token( const char* token )
{
  for ( std::size_t k = 0; k < RL_TARGETED_TOKEN_COUNT; ++k )
    if ( streq( RL_TARGETED_TOKENS[ k ], token ) )
      return true;
  return false;
}

constexpr bool aim_spells_all_targeted()
{
  for ( std::size_t k = 0; k < RL_AIM_SPELL_COUNT; ++k )
    if ( !is_targeted_token( RL_AIM_SPELLS[ k ] ) )
      return false;
  return true;
}

constexpr bool rule_prefs_all_targeted()
{
  for ( std::size_t k = 0; k < RL_RULE_PREF_COUNT; ++k )
    if ( !is_targeted_token( RL_RULE_PREFS[ k ].token ) )
      return false;
  return true;
}

constexpr bool declared_fact_slots_in_range()
{
  for ( std::size_t k = 0; k < RL_DECLARED_FACT_COUNT; ++k )
    if ( RL_DECLARED_FACTS[ k ].slot >= RL_TARGET_FEATURES )
      return false;
  return true;
}

// WR-03 (268 fix pass): a slot shared by two declared facts makes the second row unreachable (build_feature_sources takes the
// first row for a slot and resolve_target_fact_leaf the first name match), and nobody is told. Refuse it at compile time.
constexpr bool declared_slots_unique()
{
  for ( std::size_t k = 0; k < RL_DECLARED_FACT_COUNT; ++k )
    for ( std::size_t j = k + 1; j < RL_DECLARED_FACT_COUNT; ++j )
      if ( RL_DECLARED_FACTS[ k ].slot == RL_DECLARED_FACTS[ j ].slot )
        return false;
  return true;
}

// WR-03: every targeted token has exactly one aiming rule. No row would make preference_for fall back to current_target
// silently; two rows would make the second unreachable (the first matching row wins in actor_binding_for's scan).
constexpr bool every_targeted_token_has_one_rule()
{
  for ( std::size_t t = 0; t < RL_TARGETED_TOKEN_COUNT; ++t )
  {
    std::size_t rows = 0;
    for ( std::size_t k = 0; k < RL_RULE_PREF_COUNT; ++k )
      if ( streq( RL_RULE_PREFS[ k ].token, RL_TARGETED_TOKENS[ t ] ) )
        ++rows;
    if ( rows != 1 )
      return false;
  }
  return true;
}

constexpr bool wait_labels_are_wait_rows()
{
  for ( std::size_t k = 0; k < RL_WAIT_DEF_COUNT; ++k )
  {
    bool found = false;
    for ( std::size_t a = 0; a < RL_ACTION_DIM; ++a )
      found = found || ( RL_ACTIONS[ a ].kind == rl_action_kind::wait && streq( RL_ACTIONS[ a ].label, RL_WAIT_DEFS[ k ].label ) );
    if ( !found )
      return false;
  }
  return true;
}

} // namespace rl_contract

// ---- marker and field types (every spec header) ----
static_assert( RL_HEADER_CA_VERSION == 1, "C-A: the header must carry RL_HEADER_CA_VERSION 1" );

static_assert( rl_contract::scalar_of_v<decltype( RL_TARGETED_TOKEN_COUNT ), std::size_t>, "C-A: RL_TARGETED_TOKEN_COUNT is std::size_t" );
static_assert( rl_contract::array_of_v<decltype( RL_TARGETED_TOKENS ), const char*>, "C-A: RL_TARGETED_TOKENS is an array of const char*" );
static_assert( std::extent_v<decltype( RL_TARGETED_TOKENS )> == rl_contract::table_extent( RL_TARGETED_TOKEN_COUNT ), "C-A: RL_TARGETED_TOKENS extent" );
// 271-10 (owner decisions T-2, T-3, the C-A field change): the two chooser strings left the header; the aimed-range table and the
// optional tag melee action replaced them.
static_assert( rl_contract::array_of_v<decltype( RL_AIMED_SPELL_RANGE_YARDS ), double>, "C-A: RL_AIMED_SPELL_RANGE_YARDS is an array of double" );
static_assert( std::extent_v<decltype( RL_AIMED_SPELL_RANGE_YARDS )> == rl_contract::table_extent( RL_TARGETED_TOKEN_COUNT ),
               "C-A: RL_AIMED_SPELL_RANGE_YARDS has one entry per targeted token (RL_TARGETED_TOKEN_COUNT)" );
static_assert( rl_contract::scalar_of_v<decltype( RL_TAG_MELEE_ACTION ), const char*>, "C-A: RL_TAG_MELEE_ACTION is const char* (nullptr when the spec has no override)" );
static_assert( rl_contract::scalar_of_v<decltype( RL_RULE_PREF_COUNT ), std::size_t>, "C-A: RL_RULE_PREF_COUNT is std::size_t" );
static_assert( rl_contract::array_of_v<decltype( RL_RULE_PREFS ), rl_rule_pref>, "C-A: RL_RULE_PREFS is an array of rl_rule_pref" );
static_assert( std::extent_v<decltype( RL_RULE_PREFS )> == rl_contract::table_extent( RL_RULE_PREF_COUNT ), "C-A: RL_RULE_PREFS extent" );
static_assert( rl_contract::scalar_of_v<decltype( RL_DECLARED_FACT_COUNT ), std::size_t>, "C-A: RL_DECLARED_FACT_COUNT is std::size_t" );
static_assert( rl_contract::array_of_v<decltype( RL_DECLARED_FACTS ), rl_declared_fact>, "C-A: RL_DECLARED_FACTS is an array of rl_declared_fact" );
static_assert( std::extent_v<decltype( RL_DECLARED_FACTS )> == rl_contract::table_extent( RL_DECLARED_FACT_COUNT ), "C-A: RL_DECLARED_FACTS extent" );
static_assert( rl_contract::scalar_of_v<decltype( RL_RESOURCE_NAME ), const char*>, "C-A: RL_RESOURCE_NAME is const char*" );
static_assert( rl_contract::scalar_of_v<decltype( RL_WAIT_DEF_COUNT ), std::size_t>, "C-A: RL_WAIT_DEF_COUNT is std::size_t" );
static_assert( rl_contract::array_of_v<decltype( RL_WAIT_DEFS ), rl_wait_def>, "C-A: RL_WAIT_DEFS is an array of rl_wait_def" );
static_assert( std::extent_v<decltype( RL_WAIT_DEFS )> == rl_contract::table_extent( RL_WAIT_DEF_COUNT ), "C-A: RL_WAIT_DEFS extent" );
static_assert( rl_contract::scalar_of_v<decltype( RL_COOLDOWN_ROW_ACTION_COUNT ), std::size_t>, "C-A: RL_COOLDOWN_ROW_ACTION_COUNT is std::size_t" );
static_assert( rl_contract::array_of_v<decltype( RL_COOLDOWN_ROW_ACTION ), rl_cooldown_alias>, "C-A: RL_COOLDOWN_ROW_ACTION is an array of rl_cooldown_alias" );
static_assert( std::extent_v<decltype( RL_COOLDOWN_ROW_ACTION )> == rl_contract::table_extent( RL_COOLDOWN_ROW_ACTION_COUNT ), "C-A: RL_COOLDOWN_ROW_ACTION extent" );
static_assert( rl_contract::scalar_of_v<decltype( RL_PROC_NAME_COUNT ), std::size_t>, "C-A: RL_PROC_NAME_COUNT is std::size_t" );
static_assert( rl_contract::array_of_v<decltype( RL_PROC_NAMES ), const char*>, "C-A: RL_PROC_NAMES is an array of const char*" );
static_assert( std::extent_v<decltype( RL_PROC_NAMES )> == RL_PROC_NAME_COUNT, "C-A: RL_PROC_NAMES extent equals RL_PROC_NAME_COUNT" );
static_assert( rl_contract::scalar_of_v<decltype( RL_HIT_PROVIDER_COUNT ), std::size_t>, "C-A: RL_HIT_PROVIDER_COUNT is std::size_t" );
static_assert( rl_contract::array_of_v<decltype( RL_HIT_PROVIDERS ), rl_hit_provider>, "C-A: RL_HIT_PROVIDERS is an array of rl_hit_provider" );
static_assert( std::extent_v<decltype( RL_HIT_PROVIDERS )> == rl_contract::table_extent( RL_HIT_PROVIDER_COUNT ), "C-A: RL_HIT_PROVIDERS extent" );

// ---- cross-table rules (every spec header) ----
static_assert( RL_PROC_NAME_COUNT == rl_proc::COUNT, "C-A: the proc-name table is fixed at the record format's count" );
static_assert( rl_contract::streq( RL_PROC_NAMES[ 0 ], "crit_hit" ) && rl_contract::streq( RL_PROC_NAMES[ 1 ], "auto_attack_hit" ) &&
                   rl_contract::streq( RL_PROC_NAMES[ 16 ], "dbc_proc_callback" ) && rl_contract::streq( RL_PROC_NAMES[ 17 ], "buff_chance_trigger" ),
               "C-A: proc slots 0, 1, 16, 17 are incremented by engine code for every class and carry the generic names" );
static_assert( RL_TARGETED_TOKEN_COUNT >= 1, "C-A: at least one targeted token" );
static_assert( rl_contract::aim_spells_all_targeted(), "C-A (259-05b): every RL_AIM_SPELLS token must appear in RL_TARGETED_TOKENS" );
static_assert( rl_contract::rule_prefs_all_targeted(), "C-A: every RL_RULE_PREFS token must appear in RL_TARGETED_TOKENS" );
static_assert( rl_contract::declared_fact_slots_in_range(), "C-A: every RL_DECLARED_FACTS slot must be below RL_TARGET_FEATURES" );
static_assert( rl_contract::declared_slots_unique(), "C-A: every RL_DECLARED_FACTS slot must be unique (a shared slot leaves the second row unreachable)" );
static_assert( rl_contract::every_targeted_token_has_one_rule(), "C-A: every RL_TARGETED_TOKENS token must have exactly one RL_RULE_PREFS row (no row would silently aim with current_target)" );
static_assert( rl_contract::wait_labels_are_wait_rows(), "C-A: every RL_WAIT_DEFS label must be the label of a wait row of RL_ACTIONS" );

// ---- enhancement value pins (only the enhancement build: no RL_SPEC_HEADER) ----
#ifndef RL_SPEC_HEADER
namespace rl_contract
{

constexpr bool proc_names_equal_enum_names()
{
  for ( std::size_t k = 0; k < RL_PROC_NAME_COUNT; ++k )
    if ( !streq( RL_PROC_NAMES[ k ], rl_proc::NAMES[ k ] ) )
      return false;
  return true;
}

constexpr bool enhancement_targeted_tokens_unchanged()
{
  constexpr const char* const today[ 9 ] = { "stormstrike", "lightning_bolt", "chain_lightning", "tempest", "windstrike",
                                             "lava_lash",   "voltaic_blaze",  "primordial_storm", "flame_shock" };
  if ( RL_TARGETED_TOKEN_COUNT != 9 )
    return false;
  for ( std::size_t k = 0; k < 9; ++k )
    if ( !streq( RL_TARGETED_TOKENS[ k ], today[ k ] ) )
      return false;
  return true;
}

// WR-02 (268 fix pass): the four tables the first version of these pins left out. Each compares every row against the
// value the supplement carries today, so Phase 270's header swap fails to compile on a silent change.
struct pinned_rule { const char* token; const char* rule; };
struct pinned_fact { rl_fact_kind kind; const char* name; std::size_t slot; const char* feature; };
struct pinned_wait { const char* label; rl_wait_kind kind; double threshold; };
struct pinned_hit  { const char* family; const char* member; const char* provider; };

constexpr bool enhancement_rule_prefs_unchanged()
{
  constexpr pinned_rule today[ 9 ] = {
    { "stormstrike",      "shaman.thorims_aware_strike" },
    { "windstrike",       "shaman.thorims_aware_strike" },
    { "primordial_storm", "shortest_ttd" },
    { "lightning_bolt",   "shortest_ttd" },
    { "lava_lash",        "shaman.lava_lash" },
    { "voltaic_blaze",    "shaman.voltaic_blaze" },
    { "chain_lightning",  "shaman.chain_lightning" },
    { "tempest",          "shaman.tempest" },
    { "flame_shock",      "shaman.voltaic_blaze" },   // deliberate (261002-8rs): Flame Shock is aimed like Voltaic Blaze
  };
  if ( RL_RULE_PREF_COUNT != 9 )
    return false;
  for ( std::size_t k = 0; k < 9; ++k )
    if ( !streq( RL_RULE_PREFS[ k ].token, today[ k ].token ) || !streq( RL_RULE_PREFS[ k ].rule, today[ k ].rule ) )
      return false;
  return true;
}

constexpr bool enhancement_declared_facts_unchanged()
{
  constexpr pinned_fact today[ 8 ] = {
    { rl_fact_kind::dot_remaining, "flame_shock",                      10, "flame_shock_remaining" },
    { rl_fact_kind::dot_remaining, "burning_core",                     13, "burning_core_remaining" },
    { rl_fact_kind::debuff_stacks, "lightning_rod",                    14, "lightning_rod_stacks" },
    { rl_fact_kind::dot_remaining, "lightning_rod",                    15, "lightning_rod_remaining" },
    { rl_fact_kind::dot_remaining, "venomfang",                        16, "venomfang_remaining" },
    { rl_fact_kind::debuff_stacks, "venomfang_debuff",                 17, "venomfang_debuff_stacks" },
    { rl_fact_kind::dot_remaining, "venomfang_debuff",                 18, "venomfang_debuff_remaining" },
    { rl_fact_kind::dot_remaining, "rune_of_unleashed_fire_lingering", 19, "rune_of_unleashed_fire_lingering_remaining" },
  };
  if ( RL_DECLARED_FACT_COUNT != 8 )
    return false;
  for ( std::size_t k = 0; k < 8; ++k )
  {
    if ( RL_DECLARED_FACTS[ k ].kind != today[ k ].kind || !streq( RL_DECLARED_FACTS[ k ].name, today[ k ].name ) ||
         RL_DECLARED_FACTS[ k ].slot != today[ k ].slot )
      return false;
    if ( RL_DECLARED_FACTS[ k ].slot >= RL_TARGET_FEATURES || !streq( RL_TARGET_FEATURE_NAMES[ RL_DECLARED_FACTS[ k ].slot ], today[ k ].feature ) )
      return false;
  }
  return true;
}

constexpr bool enhancement_wait_defs_unchanged()
{
  constexpr pinned_wait today[ 4 ] = {
    { "wait_next_event", rl_wait_kind::next_event,         0.0 },
    { "wait_swing_mh",   rl_wait_kind::swing_mh,           0.0 },
    { "wait_swing_oh",   rl_wait_kind::swing_oh,           0.0 },
    { "wait_maelstrom",  rl_wait_kind::resource_threshold, 0.0 },
  };
  if ( RL_WAIT_DEF_COUNT != 4 )
    return false;
  for ( std::size_t k = 0; k < 4; ++k )
    if ( !streq( RL_WAIT_DEFS[ k ].label, today[ k ].label ) || RL_WAIT_DEFS[ k ].kind != today[ k ].kind ||
         RL_WAIT_DEFS[ k ].threshold != today[ k ].threshold )
      return false;
  return true;
}

constexpr bool enhancement_hit_providers_unchanged()
{
  constexpr pinned_hit today[ 7 ] = {
    { "hits", "chain_lightning",                "shaman.chain_lightning" },
    { "hits", "tempest",                        "shaman.tempest" },
    { "hits", "crash_lightning",                "shaman.crash_lightning" },
    { "hits", "lava_lash.flame_shock_spread",   "shaman.lava_lash_flame_shock_spread" },
    { "hits", "voltaic_blaze.cleave",           "shaman.voltaic_blaze_cleave" },
    { "hits", "voltaic_blaze.new_flame_shocks", "shaman.voltaic_blaze_new_flame_shocks" },
    { "hits", "fire_nova",                      "shaman.fire_nova" },
  };
  if ( RL_HIT_PROVIDER_COUNT != 7 )
    return false;
  for ( std::size_t k = 0; k < 7; ++k )
    if ( !streq( RL_HIT_PROVIDERS[ k ].family, today[ k ].family ) || !streq( RL_HIT_PROVIDERS[ k ].member, today[ k ].member ) ||
         !streq( RL_HIT_PROVIDERS[ k ].provider, today[ k ].provider ) )
      return false;
  return true;
}

// 271-10 (T-2): the seven aiming spells' ranges are the hand table the generated one replaced; the generator's table must keep
// them. The first seven tokens are stormstrike, lightning_bolt, chain_lightning, tempest, windstrike, lava_lash, voltaic_blaze.
constexpr bool enhancement_aimed_ranges_unchanged()
{
  constexpr double today[ 7 ] = { 5.0, 40.0, 40.0, 40.0, 30.0, 5.0, 40.0 };
  for ( std::size_t k = 0; k < 7; ++k )
    if ( RL_AIMED_SPELL_RANGE_YARDS[ k ] != today[ k ] )
      return false;
  return true;
}

} // namespace rl_contract

static_assert( rl_contract::proc_names_equal_enum_names(), "enhancement: RL_PROC_NAMES must equal rl_proc::NAMES (P1 compares the sidecar that carries them)" );
static_assert( rl_contract::enhancement_targeted_tokens_unchanged(), "enhancement: the nine targeted tokens, in today's order" );
static_assert( rl_contract::enhancement_rule_prefs_unchanged(), "enhancement: the nine aiming rules (token -> rule), in today's order" );
static_assert( rl_contract::enhancement_declared_facts_unchanged(), "enhancement: the eight declared facts (kind, engine name, slot, feature name at that slot)" );
static_assert( rl_contract::enhancement_wait_defs_unchanged(), "enhancement: the four wait definitions (label, kind, threshold)" );
static_assert( rl_contract::enhancement_hit_providers_unchanged(), "enhancement: the seven hit providers (family, member, provider)" );
static_assert( rl_contract::streq( RL_RESOURCE_NAME, "maelstrom" ), "enhancement: the resource scalar is maelstrom (maelstrom_weapon is a buff)" );
static_assert( RL_COOLDOWN_ROW_ACTION_COUNT == 1 && rl_contract::streq( RL_COOLDOWN_ROW_ACTION[ 0 ].cooldown_row, "strike" ) &&
                   rl_contract::streq( RL_COOLDOWN_ROW_ACTION[ 0 ].action_token, "stormstrike" ),
               "enhancement: the cooldown-row alias is strike -> stormstrike" );
static_assert( RL_TAG_MELEE_ACTION == nullptr, "enhancement: no tag melee action override (melee reach is the shortest aimed range)" );
static_assert( rl_contract::enhancement_aimed_ranges_unchanged(), "enhancement: the aimed ranges, in token order (5, 40, 40, 40, 30, 5, 40 for the seven aiming spells)" );
#endif
