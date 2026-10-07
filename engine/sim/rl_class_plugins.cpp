// ==========================================================================
// Per-class plugin tables for the RL training path (Phase 268 plan 02). See rl_class_plugins.hpp.
//
// The shaman scorer bodies stay in rl_target_select.cpp (no code motion, so nothing about their
// arithmetic can change); this file holds the table that names them, reachable only through the
// SHAMAN entry.
// ==========================================================================

#include "sim/rl_class_plugins.hpp"

#include <cstring>

#include "action/action.hpp"
#include "player/player.hpp"
#include "sim/rl_target_select.hpp"

namespace
{

using rl_target_select::preference_fn;

// The Thorim's-aware strike rule chooses its scorer per decision; whether the resolved action is the
// Stormstrike (as opposed to the Windstrike) is read from the action's own name, as it always was.
preference_fn shaman_thorims_aware_strike( const action_t* resolved )
{
  return rl_target_select::preference_for_thorims_aware_strike( resolved, resolved->name_str == "stormstrike" );
}

preference_fn shaman_lava_lash( const action_t* )
{
  return rl_target_select::preference_lava_lash;
}

preference_fn shaman_voltaic_blaze( const action_t* )
{
  return rl_target_select::preference_voltaic_blaze;
}

preference_fn shaman_chain_lightning( const action_t* )
{
  return rl_target_select::preference_chain_lightning;
}

preference_fn shaman_tempest( const action_t* )
{
  return rl_target_select::preference_tempest;
}

const rl_class_rule SHAMAN_RULES[] = {
  { "shaman.thorims_aware_strike", shaman_thorims_aware_strike },
  { "shaman.lava_lash", shaman_lava_lash },
  { "shaman.voltaic_blaze", shaman_voltaic_blaze },
  { "shaman.chain_lightning", shaman_chain_lightning },
  { "shaman.tempest", shaman_tempest },
};

// A zero-row table holds one all-null sentinel row (a zero-length array is not valid C++); readers
// iterate by n_rules, never by array size.
const rl_class_rule NO_RULES[] = { { nullptr, nullptr } };

const rl_class_plugin SHAMAN_PLUGIN  = { "shaman", SHAMAN_RULES, sizeof( SHAMAN_RULES ) / sizeof( SHAMAN_RULES[ 0 ] ) };
// Arms' aiming rules are all generic; the hit providers join this entry in plan 268-04.
const rl_class_plugin WARRIOR_PLUGIN = { "warrior", NO_RULES, 0 };
const rl_class_plugin GENERIC_PLUGIN = { "generic", NO_RULES, 0 };

}  // namespace

const rl_class_plugin& rl_class_plugin_for( const player_t* p )
{
  switch ( p->type )
  {
    case SHAMAN:
      return SHAMAN_PLUGIN;
    case WARRIOR:
      return WARRIOR_PLUGIN;
    default:
      return GENERIC_PLUGIN;
  }
}

rule_resolver_fn rl_class_rule_find( const rl_class_plugin& plugin, const char* rule_name )
{
  for ( std::size_t k = 0; k < plugin.n_rules; ++k )
    if ( std::strcmp( plugin.rules[ k ].name, rule_name ) == 0 )
      return plugin.rules[ k ].resolve;
  return nullptr;
}
