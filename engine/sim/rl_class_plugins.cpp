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

using rl_target_select::enemy_fact;
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

// ---- Plan 268-03: the shaman's per-candidate class fields and geometry facts ----

// The named aura fields enhancement's scorers and the decision dump read are completed from the declared facts. Which declared
// row feeds which named field is resolved ONCE per process (it depends only on the compiled spec header), by the feature's
// own name, so a named field and the declared value the observation and the candidate block read come from one lookup and
// cannot disagree. A header that declares no row for a field leaves it at 0 (the old "absent" value).
struct shaman_declared_slots
{
  std::size_t flame_shock_remaining;
  std::size_t burning_core_remaining;
  std::size_t lightning_rod_stacks;
  std::size_t lightning_rod_remaining;
  std::size_t venomfang_remaining;
  std::size_t venomfang_debuff_stacks;
  std::size_t venomfang_debuff_remaining;
  std::size_t rune_of_unleashed_fire_lingering_remaining;
};

const shaman_declared_slots& shaman_slots()
{
  using rl_target_select::declared_fact_index_for_feature;
  static const shaman_declared_slots slots = {
    declared_fact_index_for_feature( "flame_shock_remaining" ),
    declared_fact_index_for_feature( "burning_core_remaining" ),
    declared_fact_index_for_feature( "lightning_rod_stacks" ),
    declared_fact_index_for_feature( "lightning_rod_remaining" ),
    declared_fact_index_for_feature( "venomfang_remaining" ),
    declared_fact_index_for_feature( "venomfang_debuff_stacks" ),
    declared_fact_index_for_feature( "venomfang_debuff_remaining" ),
    declared_fact_index_for_feature( "rune_of_unleashed_fire_lingering_remaining" ),
  };
  return slots;
}

double declared_value( const enemy_fact& f, std::size_t index )
{
  return index < RL_DECLARED_FACT_COUNT ? f.declared[ index ] : 0.0;
}

// The FULL fact: the eight named aura fields from the declared values, and the three geometry counts (code moved verbatim from
// build_enemy_fact).
void shaman_fill_class_fields( const action_t* a, player_t* candidate, enemy_fact& f )
{
  const shaman_declared_slots& s = shaman_slots();
  f.flame_shock_remaining                        = declared_value( f, s.flame_shock_remaining );
  f.burning_core_remaining                       = declared_value( f, s.burning_core_remaining );
  f.lightning_rod_stacks                         = static_cast<int>( declared_value( f, s.lightning_rod_stacks ) );
  f.lightning_rod_remaining                      = declared_value( f, s.lightning_rod_remaining );
  f.venomfang_remaining                          = declared_value( f, s.venomfang_remaining );
  f.venomfang_debuff_stacks                      = static_cast<int>( declared_value( f, s.venomfang_debuff_stacks ) );
  f.venomfang_debuff_remaining                   = declared_value( f, s.venomfang_debuff_remaining );
  f.rune_of_unleashed_fire_lingering_remaining   = declared_value( f, s.rune_of_unleashed_fire_lingering_remaining );

  // 260914-rbp Task 1 Step 6 (R15, rulings Q16): the targeting-lens fields -- see enemy_fact's own per-field comments
  // (rl_target_select.hpp) for exactly what each counts. 260914-rbp Task 2c (NOTE-1): the stash lookup itself is the gate --
  // chain_hop_count_for_start returns 0 when `a` has no stash entry or the entry's stamp is stale.
  f.chain_hop_count = rl_target_select::chain_hop_count_for_start( a, candidate );
  {
    const rl_target_select::vb_lava_lash_geometry_t& geo = rl_target_select::resolve_vb_lava_lash_geometry( a->player );
    // Task 2b (Q17 review, A2): VB's cleave always hits (and can Flame-Shock) its own pick (include_pick=true); Lava Lash's
    // spread pick is the SOURCE carrier, never a spread target (include_pick=false, unchanged).
    f.vb_new_flame_shocks_within_10yd =
        rl_target_select::count_new_flame_shock_neighbours( a->player, candidate, geo.vb_radius, geo.vb_cap, true );
    f.lava_lash_spread_within_12yd = rl_target_select::count_new_flame_shock_neighbours(
        a->player, candidate, geo.lava_lash_radius, geo.lava_lash_cap, false );
  }
}

// The LITE scoring fact: only what the dispatched scorer reads (WR-05's own "skip what the dispatched preference doesn't need"
// discipline). Moved verbatim from build_enemy_fact_for_scoring, its Flame Shock read replaced by the declared lookup.
void shaman_fill_scoring_fields( const action_t* a, player_t* candidate, preference_fn pref, enemy_fact& f )
{
  if ( pref == rl_target_select::preference_lava_lash || pref == rl_target_select::preference_voltaic_blaze )
  {
    const std::size_t fs = shaman_slots().flame_shock_remaining;  // WR-04: the non-creating lookup lives in read_declared_fact
    f.flame_shock_remaining =
        fs < RL_DECLARED_FACT_COUNT ? rl_target_select::read_declared_fact( RL_DECLARED_FACTS[ fs ], a->player, candidate ) : 0.0;

    // 260914-rbp Task 1 Step 6 (R15, rulings Q16): only the ONE new targeting-lens field the DISPATCHED preference actually
    // reads. 260914-rbp Task 2c (LO-3): named `vb_ll_geo`.
    const rl_target_select::vb_lava_lash_geometry_t& vb_ll_geo = rl_target_select::resolve_vb_lava_lash_geometry( a->player );
    // Task 2b (Q17 review, A2): same include_pick convention as shaman_fill_class_fields above.
    if ( pref == rl_target_select::preference_voltaic_blaze )
      f.vb_new_flame_shocks_within_10yd = rl_target_select::count_new_flame_shock_neighbours(
          a->player, candidate, vb_ll_geo.vb_radius, vb_ll_geo.vb_cap, true );
    else  // preference_lava_lash
      f.lava_lash_spread_within_12yd = rl_target_select::count_new_flame_shock_neighbours(
          a->player, candidate, vb_ll_geo.lava_lash_radius, vb_ll_geo.lava_lash_cap, false );
  }
}

double shaman_chain_hop_count( const enemy_fact& f )
{
  return static_cast<double>( f.chain_hop_count );
}

double shaman_vb_new_flame_shocks( const enemy_fact& f )
{
  return static_cast<double>( f.vb_new_flame_shocks_within_10yd );
}

double shaman_lava_lash_spread( const enemy_fact& f )
{
  return static_cast<double>( f.lava_lash_spread_within_12yd );
}

const rl_geometry_fact SHAMAN_GEOMETRY[] = {
  { "chain_hop_count", shaman_chain_hop_count },
  { "vb_new_flame_shocks_within_10yd", shaman_vb_new_flame_shocks },
  { "lava_lash_spread_within_12yd", shaman_lava_lash_spread },
};

const rl_class_rule SHAMAN_RULES[] = {
  { "shaman.thorims_aware_strike", shaman_thorims_aware_strike },
  { "shaman.lava_lash", shaman_lava_lash },
  { "shaman.voltaic_blaze", shaman_voltaic_blaze },
  { "shaman.chain_lightning", shaman_chain_lightning },
  { "shaman.tempest", shaman_tempest },
};

// A zero-row table holds one all-null sentinel row (a zero-length array is not valid C++); readers
// iterate by n_rules / n_geometry_facts, never by array size.
const rl_class_rule    NO_RULES[]    = { { nullptr, nullptr } };
const rl_geometry_fact NO_GEOMETRY[] = { { nullptr, nullptr } };

const rl_class_plugin SHAMAN_PLUGIN = { "shaman",
                                        SHAMAN_RULES,
                                        sizeof( SHAMAN_RULES ) / sizeof( SHAMAN_RULES[ 0 ] ),
                                        shaman_fill_class_fields,
                                        shaman_fill_scoring_fields,
                                        SHAMAN_GEOMETRY,
                                        sizeof( SHAMAN_GEOMETRY ) / sizeof( SHAMAN_GEOMETRY[ 0 ] ) };
// Arms' aiming rules and facts are all generic or declared; the hit providers join this entry in plan 268-04.
const rl_class_plugin WARRIOR_PLUGIN = { "warrior", NO_RULES, 0, nullptr, nullptr, NO_GEOMETRY, 0 };
const rl_class_plugin GENERIC_PLUGIN = { "generic", NO_RULES, 0, nullptr, nullptr, NO_GEOMETRY, 0 };

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

const rl_geometry_fact* rl_class_geometry_find( const rl_class_plugin& plugin, const char* feature_name )
{
  for ( std::size_t k = 0; k < plugin.n_geometry_facts; ++k )
    if ( std::strcmp( plugin.geometry_facts[ k ].feature, feature_name ) == 0 )
      return &plugin.geometry_facts[ k ];
  return nullptr;
}
