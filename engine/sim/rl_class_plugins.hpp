// ==========================================================================
// Per-class plugin tables for the RL training path (Phase 268 plan 02).
//
// SimulationCraft compiles every class module into every binary, so a class's scoring code is always
// present; what this header controls is which of it is REACHABLE. A class-specific rule (an aiming rule
// spelled "<class>.<name>" in a spec header's RL_RULE_PREFS) resolves only through the plugin that
// rl_class_plugin_for() selects from player_t::type. The generic rules (current_target, shortest_ttd)
// need no plugin and work for every class.
//
// Plans 268-03 to 268-05 append the fact-geometry and hit-provider tables to rl_class_plugin.
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

struct rl_class_plugin
{
  const char*          class_name;
  const rl_class_rule* rules;     // may point at a one-row null sentinel when n_rules is 0
  std::size_t          n_rules;
};

// The plugin for a player's class. Never null: a class without a plugin gets the generic one, whose
// rule table is empty.
const rl_class_plugin& rl_class_plugin_for( const player_t* p );

// The resolver the plugin holds for a rule name, or nullptr when the plugin has no such rule.
rule_resolver_fn rl_class_rule_find( const rl_class_plugin& plugin, const char* rule_name );
