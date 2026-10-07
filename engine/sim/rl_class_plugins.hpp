// ==========================================================================
// Per-class plugin tables for the RL training path (Phase 268 plan 02).
//
// SimulationCraft compiles every class module into every binary, so a class's scoring code is always
// present; what this header controls is which of it is REACHABLE. A class-specific rule (an aiming rule
// spelled "<class>.<name>" in a spec header's RL_RULE_PREFS) resolves only through the plugin that
// rl_class_plugin_for() selects from player_t::type. The generic rules (current_target, shortest_ttd)
// need no plugin and work for every class.
//
// Plan 268-03 added the per-candidate class-field hooks and the geometry-fact table; plan 268-04 appended the hit-provider
// table (the shaman rows); plan 268-05 adds the warrior rows.
// ==========================================================================

#pragma once

#include <cstddef>

#include "sim/rl_target_select.hpp"

struct action_t;
struct player_t;

// A rule resolver returns the scorer to use for ONE decision of the resolved action, so a rule that
// chooses between scorers per decision (the Thorim's-aware strike) keeps doing so.
using rule_resolver_fn = rl_target_select::preference_fn ( * )( const action_t* resolved );

struct rl_class_rule
{
  const char*      name;      // "<class>.<name>", exactly as a header's RL_RULE_PREFS spells it
  rule_resolver_fn resolve;
};

// Plan 268-03 (G268-5, G268-6): a per-class geometry fact. A spec header names a per-candidate feature in RL_TARGET_FEATURE_NAMES
// that is neither one of the 12 generic facts nor a declared fact; the class plugin serves it by name, from the enemy_fact the
// plugin's fill_class_fields completed.
struct rl_geometry_fact
{
  const char* feature;                                          // the feature's name in RL_TARGET_FEATURE_NAMES
  double ( *get )( const rl_target_select::enemy_fact& fact );  // reads the already-filled value
};

// Completes the FULL enemy_fact build_enemy_fact made for `candidate` against action `a`: copies the declared values into the
// class's named fields and computes the class's geometry counts. Nullable (a class with nothing to add).
using rl_fill_class_fields_fn = void ( * )( const action_t* a, player_t* candidate, rl_target_select::enemy_fact& fact );

// Completes the LITE enemy_fact the scoring loop builds for `candidate`, for the one scorer `pref` the decision dispatched:
// only what that scorer reads. Nullable.
using rl_fill_scoring_fields_fn = void ( * )( const action_t* a, player_t* candidate, rl_target_select::preference_fn pref,
                                              rl_target_select::enemy_fact& fact );

// Plan 268-04 (G268-7, FORK-04): a hit-count provider. A spec header's RL_HIT_PROVIDERS row names a provider spelled
// "<class>.<name>"; the class plugin that holds that name says which reading computes it. The readings are computed by
// rl_policy_obs.cpp (they need the per-decision memo that file owns); this table is what makes a provider reachable only through
// its own class's plugin. Plan 268-05 appended the three warrior readings (warrior_*).
enum class rl_hit_reading
{
  chain_lightning,
  tempest,
  crash_lightning,
  lava_lash_flame_shock_spread,
  voltaic_blaze_cleave,
  voltaic_blaze_new_flame_shocks,
  fire_nova,
  warrior_cleave,
  warrior_whirlwind,
  warrior_sweeping_strikes,
};

struct rl_class_hit_provider
{
  const char*     provider;   // "<class>.<name>", exactly as a header's RL_HIT_PROVIDERS spells it
  rl_hit_reading  reading;
};

struct rl_class_plugin
{
  const char*                class_name;
  const rl_class_rule*       rules;     // may point at a one-row null sentinel when n_rules is 0
  std::size_t                n_rules;
  rl_fill_class_fields_fn    fill_class_fields;    // nullable
  rl_fill_scoring_fields_fn  fill_scoring_fields;  // nullable
  const rl_geometry_fact*    geometry_facts;       // may point at a one-row null sentinel when n_geometry_facts is 0
  std::size_t                n_geometry_facts;
  const rl_class_hit_provider* hit_providers;      // may point at a one-row null sentinel when n_hit_providers is 0
  std::size_t                n_hit_providers;
};

// The plugin for a player's class. Never null: a class without a plugin gets the generic one, whose
// rule table is empty.
const rl_class_plugin& rl_class_plugin_for( const player_t* p );

// The resolver the plugin holds for a rule name, or nullptr when the plugin has no such rule.
rule_resolver_fn rl_class_rule_find( const rl_class_plugin& plugin, const char* rule_name );

// The geometry fact the plugin serves under a feature name, or nullptr when it has none.
const rl_geometry_fact* rl_class_geometry_find( const rl_class_plugin& plugin, const char* feature_name );

// The hit provider the plugin holds under a provider name, or nullptr when the plugin has no such provider.
const rl_class_hit_provider* rl_class_hit_provider_find( const rl_class_plugin& plugin, const char* provider_name );
