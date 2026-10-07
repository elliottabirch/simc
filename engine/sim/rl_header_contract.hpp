// ==========================================================================
// Compile-time pins of the C-A field set (Phase 268 plan 01).
//
// Included last by sim/rl_policy_constants_select.h, so every translation unit that sees a spec's
// constants also compiles these pins. A header that lacks a C-A field, spells it with another type, or
// violates a cross-table rule does not compile. Phase 270 swaps the arms header and enhancement's
// supplement for Phase 267's generated headers; the same pins then prove that 267's output carries the
// field set (and, for enhancement, today's values).
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
static_assert( rl_contract::scalar_of_v<decltype( RL_CHOOSER_PROBE_ACTION ), const char*>, "C-A: RL_CHOOSER_PROBE_ACTION is const char*" );
static_assert( rl_contract::scalar_of_v<decltype( RL_CHOOSER_MELEE_ACTION ), const char*>, "C-A: RL_CHOOSER_MELEE_ACTION is const char*" );
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
static_assert( rl_contract::wait_labels_are_wait_rows(), "C-A: every RL_WAIT_DEFS label must be the label of a wait row of RL_ACTIONS" );
static_assert( RL_CHOOSER_PROBE_ACTION != nullptr, "C-A: the chooser's probe action is required" );

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

} // namespace rl_contract

static_assert( rl_contract::proc_names_equal_enum_names(), "enhancement: RL_PROC_NAMES must equal rl_proc::NAMES (P1 compares the sidecar that carries them)" );
static_assert( rl_contract::enhancement_targeted_tokens_unchanged(), "enhancement: the nine targeted tokens, in today's order" );
static_assert( rl_contract::streq( RL_RESOURCE_NAME, "maelstrom" ), "enhancement: the resource scalar is maelstrom (maelstrom_weapon is a buff)" );
static_assert( RL_COOLDOWN_ROW_ACTION_COUNT == 1 && rl_contract::streq( RL_COOLDOWN_ROW_ACTION[ 0 ].cooldown_row, "strike" ) &&
                   rl_contract::streq( RL_COOLDOWN_ROW_ACTION[ 0 ].action_token, "stormstrike" ),
               "enhancement: the cooldown-row alias is strike -> stormstrike" );
static_assert( rl_contract::streq( RL_CHOOSER_PROBE_ACTION, "lightning_bolt" ) && rl_contract::streq( RL_CHOOSER_MELEE_ACTION, "stormstrike" ),
               "enhancement: chooser probe lightning_bolt, melee stormstrike" );
#endif
