// ==========================================================================
// tstl-sylvanas Phase 271, plan 271-04 (owner decisions H-2, H-3 scheme A, H-4).
// The action discovery census and the one hit-counting function. See rl_action_census.hpp.
// ==========================================================================

#include "sim/rl_action_census.hpp"

#include "action/action.hpp"
#include "action/dot.hpp"
#include "buff/buff.hpp"
#include "dbc/dbc.hpp"
#include "fmt/format.h"
#include "player/player.hpp"
#include "player/pet.hpp"
#include "player/stats.hpp"
#include "sim/rl_census_lines.hpp"
#include "sim/rl_credit.hpp"
#include "sim/rl_policy_constants_select.h"
#include "sim/rl_target_select.hpp"
#include "sim/sim.hpp"
#include "util/util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace rl_action_census
{

namespace
{
constexpr int MAX_DEPTH = 3;

// Sim option storage. A global because adding a member to sim_t would rebuild every file; the RL
// rigs run one sim per process, and the option is written once at option-parse time.
std::string g_path;

// Init-walk signature per sim, compared with the fight-end walk (initMatchesEnd).
std::mutex                                 g_mutex;
std::unordered_map<const sim_t*, std::string> g_init_signature;

// How the stock engine would classify a node from its four fields alone (ignoring chain and filter).
shape_t four_rule_shape( bool ground, double radius, double range )
{
  if ( radius > 0 && range > 0 )
    return ground ? shape_t::GROUND : shape_t::AROUND_TARGET;
  if ( radius > 0 )
    return shape_t::AROUND_PLAYER;
  if ( range > 0 )
    return shape_t::REACH;
  return shape_t::ANYWHERE;
}

bool engine_adds_reach( shape_t s )
{
  return s == shape_t::GROUND || s == shape_t::AROUND_PLAYER || s == shape_t::REACH;
}

// Shaman actions the census cannot ask (their classes are not edited in this plan): the two fork
// filters are identified by action name, with the constants rl_target_select already owns.
bool builtin_filter_for( const action_t* a, filter_descriptor_t& f )
{
  if ( a->name_str == "crash_lightning" )
  {
    f.kind           = filter_kind_t::CONE;
    f.axis           = filter_axis_t::FACING;
    f.cos_half_angle = rl_target_select::CRASH_LIGHTNING_CONE_COS_HALF_ANGLE;
    f.radius         = rl_target_select::CRASH_LIGHTNING_CONE_RADIUS_YARDS;
    return true;
  }
  if ( a->name_str == "sundering" )
  {
    f.kind       = filter_kind_t::RECT;
    f.axis       = filter_axis_t::FACING;
    f.length     = rl_target_select::SUNDERING_RECT_LENGTH_YARDS;
    f.half_width = rl_target_select::SUNDERING_RECT_HALF_WIDTH_YARDS;
    return true;
  }
  return false;
}

void fill_game( node_t& n, const action_t* a )
{
  const spell_data_t& d = a->data();
  n.spell_id            = d.id();
  n.game.cone_degrees   = std::fabs( d.cone_degrees() );
  bool got_radius       = false;
  for ( size_t i = 1; i <= d.effect_count(); ++i )
  {
    const spelleffect_data_t& e = d.effectN( i );
    for ( unsigned code : { e.target_1(), e.target_2() } )
    {
      if ( code != 0 &&
           std::find( n.game.implicit_targets.begin(), n.game.implicit_targets.end(), code ) ==
               n.game.implicit_targets.end() )
        n.game.implicit_targets.push_back( code );
    }
    if ( !got_radius && e.radius_max() > 0.0 )
    {
      got_radius              = true;
      n.game.radius           = e.radius();
      n.game.radius_max       = e.radius_max();
      n.game.add_reach_attr   = e.flags( EX_ADD_TARGET_REACH_TO_AOE );
    }
  }
}

void fill_widened_by( node_t& n, const action_t* a, player_t* p )
{
  const spell_data_t& d = a->data();
  if ( !d.ok() )
    return;
  for ( buff_t* b : p->buff_list )
  {
    if ( !b )
      continue;
    const spell_data_t& bd = b->data();
    for ( size_t i = 1; i <= bd.effect_count(); ++i )
    {
      const spelleffect_data_t& e = bd.effectN( i );
      if ( e.type() != E_APPLY_AURA )
        continue;
      if ( e.subtype() != A_ADD_FLAT_MODIFIER && e.subtype() != A_ADD_PCT_MODIFIER )
        continue;
      if ( e.property_type() != P_CHAIN_TARGETS && e.property_type() != P_MAX_TARGETS )
        continue;
      if ( !d.affected_by( e ) )
        continue;
      widen_t w;
      w.buff    = b->name_str;
      w.aura_id = bd.id();
      w.extra   = e.base_value();
      n.widened_by.push_back( w );
    }
  }
}

void fill_node( node_t& n, action_t* a, const char* via, player_t* p )
{
  n.action              = a;
  n.name                = a->name_str;
  n.via                 = via;
  n.cap_static          = a->aoe;
  n.cap_now             = a->n_targets();
  n.soft_cap            = a->reduced_aoe_targets;
  n.full_amount_targets = a->full_amount_targets;
  n.split_damage        = a->split_aoe_damage;

  fill_game( n, a );

  // chain: any effect of the node's own spell with more than one chain target.
  const spell_data_t& d = a->data();
  for ( size_t i = 1; i <= d.effect_count(); ++i )
    if ( d.effectN( i ).chain_target() > 1 )
      n.chain = true;

  // The hit rule's numbers: the action's own fields unless the class module replaces the rule.
  double radius = a->radius;
  double range  = a->range;
  bool   ground = a->ground_aoe;
  shape_t under = four_rule_shape( ground, radius, range );

  const auto* provider = dynamic_cast<const shape_provider_t*>( a );
  {
    target_set_t ts;
    if ( provider && provider->rl_census_target_set( ts ) )
    {
      n.has_target_set = true;
      n.target_set     = ts;
    }
  }
  distance_override_t ov;
  if ( provider && provider->rl_census_distance_override( ov ) && ov.active )
  {
    n.override_active = true;
    under             = ov.shape;
    radius            = ov.radius;
    range             = ov.range;
  }
  n.underlying = under;
  n.radius     = radius;
  n.range      = range;
  n.add_reach  = engine_adds_reach( under );

  filter_descriptor_t fd;
  bool                has_cb = static_cast<bool>( a->target_filter_callback );
  if ( provider && provider->rl_census_filter( fd ) && fd.kind != filter_kind_t::NONE )
    n.filter = fd;
  else if ( has_cb && builtin_filter_for( a, fd ) )
    n.filter = fd;
  else if ( has_cb )
    n.filter.kind = filter_kind_t::UNKNOWN;

  if ( n.chain )
    n.shape = shape_t::CHAIN;
  else if ( n.filter.kind != filter_kind_t::NONE )
    n.shape = shape_t::FILTERED;
  else
    n.shape = under;

  fill_widened_by( n, a, p );
}

void walk_node( action_t* a, const char* via, int depth, player_t* p, std::set<const action_t*>& seen,
                std::vector<node_t>& out )
{
  if ( !a || depth > MAX_DEPTH || !seen.insert( a ).second )
    return;
  node_t n;
  fill_node( n, a, via, p );
  out.push_back( std::move( n ) );
  walk_node( a->impact_action, "impact", depth + 1, p, seen, out );
  walk_node( a->execute_action, "execute", depth + 1, p, seen, out );
  walk_node( a->tick_action, "tick", depth + 1, p, seen, out );
  for ( action_t* c : a->child_action )
    walk_node( c, "child", depth + 1, p, seen, out );
}

bool node_multi_static( const node_t& n )
{
  return n.cap_static == -1 || n.cap_static > 1 || n.chain;
}

// ---- plain-sentence disagreements --------------------------------------------------------------------

enum class centre_t
{
  UNKNOWN,
  PLAYER,
  TARGET,
  GROUND,
  FRONT,
  CONE_FROM_PLAYER
};

const char* centre_name( centre_t c )
{
  switch ( c )
  {
    case centre_t::PLAYER: return "centred on the player";
    case centre_t::TARGET: return "centred on the target";
    case centre_t::GROUND: return "centred on a ground point";
    case centre_t::FRONT: return "an area in front of the player";
    case centre_t::CONE_FROM_PLAYER: return "a cone from the player";
    default: return "unknown";
  }
}

// What the spell data's first implicit target code says about the centre. Codes seen in the arms
// spells (research 271-AOE-HIT-DISCOVERY.md section 2.3): 1 self, 18 at self, 22 source, 47 front
// of self, 87 at area (ground), 104 enemies in cone to targeted enemy, 6 and 25 target, 16 at enemy
// in area (centred on the first target).
centre_t game_centre( const game_t& g )
{
  for ( unsigned code : g.implicit_targets )
  {
    switch ( code )
    {
      case 1:
      case 18:
      case 22: return centre_t::PLAYER;
      case 47: return centre_t::FRONT;
      case 87: return centre_t::GROUND;
      case 104: return centre_t::CONE_FROM_PLAYER;
      case 6:
      case 25: return centre_t::TARGET;
      default: break;
    }
  }
  if ( !g.implicit_targets.empty() && g.implicit_targets.front() == 16 )
    return centre_t::TARGET;
  return centre_t::UNKNOWN;
}

centre_t engine_centre( const node_t& n )
{
  if ( n.shape == shape_t::FILTERED && n.filter.kind == filter_kind_t::CONE )
    return n.filter.cos_half_angle <= 1e-9 && n.filter.axis == filter_axis_t::TOWARD_TARGET
               ? centre_t::FRONT
               : centre_t::CONE_FROM_PLAYER;
  switch ( n.underlying )
  {
    case shape_t::AROUND_PLAYER:
    case shape_t::REACH: return centre_t::PLAYER;
    case shape_t::AROUND_TARGET: return centre_t::TARGET;
    case shape_t::GROUND: return centre_t::GROUND;
    default: return centre_t::UNKNOWN;
  }
}

// "within 8 yds" in a buff's own description, as the game shows it to the player.
double tooltip_reach_yards( const player_t* p, unsigned spell_id )
{
  const char* desc = p->dbc->spell_text( spell_id ).desc();
  if ( !desc )
    return 0.0;
  const char* w = std::strstr( desc, "within " );
  while ( w )
  {
    const char* num = w + 7;
    char*       end = nullptr;
    double      v   = std::strtod( num, &end );
    if ( end != num && end && ( std::strncmp( end, " yd", 3 ) == 0 || std::strncmp( end, " yards", 6 ) == 0 ) )
      return v;
    w = std::strstr( w + 1, "within " );
  }
  return 0.0;
}

void add_disagreements( token_t& t, const player_t* p )
{
  auto add = [ & ]( std::string s ) { t.disagreements.push_back( std::move( s ) ); };
  for ( const node_t& n : t.nodes )
  {
    if ( n.proc_only )
      continue;
    const bool multi = node_multi_static( n ) || !n.widened_by.empty();
    if ( !multi )
      continue;

    if ( n.chain )
      continue;  // chain hops are counted by the existing chain-hop reading

    const centre_t gc = game_centre( n.game );
    const centre_t ec = engine_centre( n );
    if ( gc != centre_t::UNKNOWN && ec != centre_t::UNKNOWN && gc != ec )
      add( fmt::format( "node '{}' ({}): the engine hits an enemy set {}; the spell data (implicit target code {}) says {}.",
                        n.name, n.spell_id, centre_name( ec ), n.game.implicit_targets.front(), centre_name( gc ) ) );

    if ( n.game.cone_degrees > 0.0 )
    {
      if ( n.filter.kind != filter_kind_t::CONE )
        add( fmt::format( "node '{}' ({}): the game uses a cone of {:.0f} degrees; the engine has no cone here.", n.name,
                          n.spell_id, n.game.cone_degrees ) );
      else
      {
        const double deg = 2.0 * std::acos( std::max( -1.0, std::min( 1.0, n.filter.cos_half_angle ) ) ) * 180.0 /
                           3.14159265358979323846;
        if ( std::fabs( deg - n.game.cone_degrees ) > 1.0 )
          add( fmt::format( "node '{}' ({}): the game cone is {:.0f} degrees; the engine cone is {:.1f} degrees.", n.name,
                            n.spell_id, n.game.cone_degrees, deg ) );
      }
    }

    if ( n.shape != shape_t::ANYWHERE && n.game.add_reach_attr && !n.add_reach )
      add( fmt::format( "node '{}' ({}): the game adds each enemy's own reach to the area; the engine's '{}' rule adds none.",
                        n.name, n.spell_id, shape_name( n.underlying ) ) );

    if ( n.shape == shape_t::FILTERED && n.underlying == shape_t::AROUND_TARGET )
      add( fmt::format( "node '{}' ({}): on top of its filter the engine also keeps only enemies within {} yards of the "
                        "target (no reach added); the game has no such second limit.",
                        n.name, n.spell_id, n.radius ) );

    if ( n.shape == shape_t::ANYWHERE )
      add( fmt::format( "node '{}' ({}): radius and range are both 0, so the engine hits every enemy in the room.", n.name,
                        n.spell_id ) );

    for ( const widen_t& w : n.widened_by )
    {
      const double yd = tooltip_reach_yards( p, w.aura_id );
      if ( yd > 0.0 && !( n.underlying == shape_t::REACH && std::fabs( n.range - yd ) < 1e-6 ) &&
           !( n.underlying == shape_t::AROUND_PLAYER && std::fabs( n.radius - yd ) < 1e-6 ) )
        add( fmt::format( "node '{}' ({}): while '{}' is up the game tooltip says extra targets within {} yards; the engine "
                          "uses the '{}' rule with {} yards.",
                          n.name, n.spell_id, w.buff, yd, shape_name( n.underlying ),
                          n.underlying == shape_t::REACH ? n.range : n.radius ) );
    }
  }
}

// Procs: a child that executes strictly less often than its root over the fight (Reap the Storm, the
// Fervor of Battle Slam) is not part of the pressed button's area. Decided at fight end only; a
// child that shares its statistics object with the root cannot be told apart and stays counted.
void mark_proc_children( token_t& t )
{
  if ( t.nodes.empty() )
    return;
  const action_t* root = t.nodes.front().action;
  if ( !root || !root->stats )
    return;
  const unsigned root_exec = root->stats->iteration_num_executes;
  for ( node_t& n : t.nodes )
  {
    if ( n.via != "child" || !n.action || !n.action->stats || n.action->stats == root->stats )
      continue;
    // A dual action never adds to its stats' execute count (action_t::execute), and the Demolish and
    // Bladestorm strikes are dual: a zero count there says nothing, so they stay counted.
    if ( n.action->dual )
      continue;
    // 261013-aimx class B: the sample behind the verdict is recorded even when the button never ran this iteration (root_exec 0): then no
    // verdict is possible, the node stays counted as before, and the evidence says so (rootExecutes 0) instead of being absent, so the census
    // merge can tell "never pressed" from "cannot be told apart from its button".
    n.proc_evidence_executes      = static_cast<long long>( n.action->stats->iteration_num_executes );
    n.proc_evidence_root_executes = static_cast<long long>( root_exec );
    if ( root_exec == 0 )
      continue;
    if ( n.action->stats->iteration_num_executes + 1 < root_exec )  // at most one strike can still be pending
      n.proc_only = true;
  }
}

void classify_token( token_t& t, const player_t* p )
{
  t.multi_target     = false;
  t.has_multi_while  = false;
  t.chosen_node      = -1;
  int  first_static  = -1;
  int  first_widened = -1;
  for ( size_t i = 0; i < t.nodes.size(); ++i )
  {
    const node_t& n = t.nodes[ i ];
    if ( n.proc_only )
      continue;
    if ( first_static < 0 && node_multi_static( n ) )
      first_static = static_cast<int>( i );
    if ( first_widened < 0 && !n.widened_by.empty() )
      first_widened = static_cast<int>( i );
  }
  if ( first_static >= 0 )
  {
    t.multi_target = true;
    t.chosen_node  = first_static;
  }
  else if ( first_widened >= 0 )
  {
    t.multi_target    = true;
    t.has_multi_while = true;
    t.multi_while     = t.nodes[ first_widened ].widened_by.front();
    t.chosen_node     = first_widened;
  }
  // 271.6-07 (G271.6-28 as revised): the class the spell data gives this button, from its root action and its counted node.
  t.gate_class = gate_class_t::EXEMPT;
  for ( const node_t& n : t.nodes )
  {
    if ( n.via != "root" || n.action == nullptr )
      continue;
    const bool has_counted = t.chosen_node >= 0 && static_cast<std::size_t>( t.chosen_node ) < t.nodes.size();
    t.gate_class = gate_class_default( n.action, has_counted ? &t.nodes[ static_cast<std::size_t>( t.chosen_node ) ] : nullptr );
    break;
  }
  add_disagreements( t, p );
}

// ---- JSON --------------------------------------------------------------------------------------------

std::string esc( const std::string& s )
{
  std::string o = "\"";
  for ( char c : s )
  {
    if ( c == '"' || c == '\\' )
    {
      o += '\\';
      o += c;
    }
    else if ( static_cast<unsigned char>( c ) < 0x20 )
      o += ' ';
    else
      o += c;
  }
  return o + "\"";
}

std::string num( double v )
{
  if ( !std::isfinite( v ) )
    return "0";
  return fmt::format( "{}", v );
}

std::string widen_json( const widen_t& w )
{
  return fmt::format( "{{\"buff\":{},\"auraId\":{},\"extra\":{}}}", esc( w.buff ), w.aura_id, num( w.extra ) );
}

std::string node_json( const node_t& n )
{
  std::string filter = "null";
  std::string params = "null";
  std::string axis   = "null";
  switch ( n.filter.kind )
  {
    case filter_kind_t::CONE:
      filter = "\"cone\"";
      params = fmt::format( "{{\"coneCosHalfAngle\":{},\"radiusYards\":{}}}", num( n.filter.cos_half_angle ), num( n.filter.radius ) );
      axis   = n.filter.axis == filter_axis_t::FACING ? "\"facing\"" : "\"toward_target\"";
      break;
    case filter_kind_t::RECT:
      filter = "\"rect\"";
      params = fmt::format( "{{\"lengthYards\":{},\"halfWidthYards\":{}}}", num( n.filter.length ), num( n.filter.half_width ) );
      axis   = n.filter.axis == filter_axis_t::FACING ? "\"facing\"" : "\"toward_target\"";
      break;
    case filter_kind_t::UNKNOWN: filter = "\"unknown\""; break;
    default: break;
  }
  std::string widened = "[";
  for ( size_t i = 0; i < n.widened_by.size(); ++i )
    widened += ( i ? "," : "" ) + widen_json( n.widened_by[ i ] );
  widened += "]";
  std::string implicit = "[";
  for ( size_t i = 0; i < n.game.implicit_targets.size(); ++i )
    implicit += fmt::format( "{}{}", i ? "," : "", n.game.implicit_targets[ i ] );
  implicit += "]";
  // 261013-aimx class B: the sample behind a proc verdict (see node_t::proc_evidence_executes); null when none was weighed.
  const std::string evidence = n.proc_evidence_root_executes < 0
                                   ? std::string( "null" )
                                   : fmt::format( "{{\"executes\":{},\"rootExecutes\":{}}}", n.proc_evidence_executes, n.proc_evidence_root_executes );
  return fmt::format(
      "{{\"name\":{},\"via\":{},\"spellId\":{},\"shape\":{},\"underlyingShape\":{},\"radiusYards\":{},\"rangeYards\":{},"
      "\"addReach\":{},\"capStatic\":{},\"capNow\":{},\"softCap\":{},\"fullAmountTargets\":{},\"splitDamage\":{},"
      "\"chain\":{},\"filter\":{},\"filterParams\":{},\"filterAxis\":{},\"ruleOverridden\":{},\"widenedBy\":{},"
      "\"procOnly\":{},\"evidence\":{},\"game\":{{\"coneDegrees\":{},\"implicitTargets\":{},\"radiusYards\":{},\"radiusMaxYards\":{},"
      "\"addReachAttr\":{}}}}}",
      esc( n.name ), esc( n.via ), n.spell_id, esc( shape_name( n.shape ) ), esc( shape_name( n.underlying ) ),
      num( n.radius ), num( n.range ), n.add_reach ? "true" : "false", n.cap_static, n.cap_now, num( n.soft_cap ),
      n.full_amount_targets, n.split_damage ? "true" : "false", n.chain ? "true" : "false", filter, params, axis,
      n.override_active ? "true" : "false", widened, n.proc_only ? "true" : "false", evidence, num( n.game.cone_degrees ), implicit,
      num( n.game.radius ), num( n.game.radius_max ), n.game.add_reach_attr ? "true" : "false" );
}

std::string token_json( const token_t& t )
{
  std::string nodes = "[";
  for ( size_t i = 0; i < t.nodes.size(); ++i )
    nodes += ( i ? "," : "" ) + node_json( t.nodes[ i ] );
  nodes += "]";
  std::string dis = "[";
  for ( size_t i = 0; i < t.disagreements.size(); ++i )
    dis += ( i ? "," : "" ) + esc( t.disagreements[ i ] );
  dis += "]";
  return fmt::format(
      "{{\"token\":{},\"actionFound\":{},\"spellId\":{},\"rangeYards\":{},\"minRangeYards\":{},\"radiusYards\":{},"
      "\"multiTarget\":{},\"multiWhile\":{},\"nodes\":{},\"chosenNode\":{},\"disagreements\":{},\"gateClass\":{},"
      "\"refusedWhileLocked\":{},\"needsMeleeReach\":{}}}",
      esc( t.token ), t.found ? "true" : "false", t.spell_id, num( t.range ), num( t.min_range ), num( t.radius ),
      t.multi_target ? "true" : "false", t.has_multi_while ? widen_json( t.multi_while ) : std::string( "null" ), nodes,
      t.chosen_node, dis, esc( gate_class_name( t.gate_class ) ), t.refused_while_locked ? "true" : "false",
      needs_melee_reach( t ) ? "true" : "false" );
}

// What must agree between the init walk and the fight-end walk: structure, never live values
// (capNow moves with buffs; procOnly is decided only at fight end).
std::string signature( const std::vector<token_t>& tokens )
{
  std::string s;
  for ( const token_t& t : tokens )
  {
    s += fmt::format( "{}|{}|{}|{}|{}|", t.token, t.found, t.spell_id, t.range, t.radius );
    for ( const node_t& n : t.nodes )
    {
      s += fmt::format( "{},{},{},{},{},{},{},{},{},{},{},{};", n.name, n.via, n.spell_id, shape_name( n.shape ), n.radius,
                        n.range, n.cap_static, n.soft_cap, n.chain, static_cast<int>( n.filter.kind ),
                        n.override_active, n.widened_by.size() );
    }
    s += "\n";
  }
  return s;
}

player_t* census_actor( sim_t* sim )
{
  for ( player_t* p : sim->player_no_pet_list )
    if ( p && !p->is_enemy() && !p->is_pet() )
      return p;
  return nullptr;
}

std::vector<token_t> walk_all( player_t* p, bool at_fight_end )
{
  std::vector<token_t> tokens = walk( p );
  for ( token_t& t : tokens )
  {
    if ( at_fight_end )
      mark_proc_children( t );
    classify_token( t, p );
  }
  return tokens;
}

// ==========================================================================================================================================
// 261028-fzl (S1-F1a, owner order 2026-10-28): the WHOLE action list. The token walk above reads only the buttons of the registry header
// and what hangs off them; the hits family needs every action the build can fire (proc actions, item procs, pet actions). C-2 of the plan:
// one row per distinct (holder, pet, name), taken from the first object with that key, `objectCount` = how many objects share it.
// ==========================================================================================================================================

struct action_row_t
{
  std::string                       key;     // "<holder>/<pet or ->/<name>"
  std::string                       holder;  // "self" | "pet"
  std::string                       pet;     // pet name_str, empty for self
  std::string                       parent;  // empty when the object is reached from no other action
  node_t                            node;
  bool                              background = false;
  bool                              harmful    = false;
  int                               object_count = 0;
  std::vector<const action_t*>      objects;  // every object with this key (reach sums over them)
};

struct child_link_t
{
  const action_t* parent = nullptr;
  const char*     via    = "list";
};

void link_children( action_t* a, int depth, std::unordered_map<const action_t*, child_link_t>& parent_of, std::vector<action_t*>& extra )
{
  if ( !a || depth >= MAX_DEPTH )
    return;
  auto link = [ & ]( action_t* c, const char* via ) {
    if ( !c || c == a || parent_of.count( c ) )
      return;
    parent_of[ c ] = child_link_t{ a, via };
    extra.push_back( c );
    link_children( c, depth + 1, parent_of, extra );
  };
  link( a->impact_action, "impact" );
  link( a->execute_action, "execute" );
  link( a->tick_action, "tick" );
  for ( action_t* c : a->child_action )
    link( c, "child" );
}

// Every action object of one holder, in the census order: the holder's own action list, then the objects reachable only through a child link
// (impact, execute, tick, child; depth 3) in discovery order. `parent_of` records how each linked object was reached. One definition: the
// census table's `actions[]` walk and the generic hit-row binder (find_holder_node) read the same set.
void holder_objects( player_t* holder, std::unordered_map<const action_t*, child_link_t>& parent_of, std::vector<action_t*>& objects )
{
  std::vector<action_t*> extra;
  objects.assign( holder->action_list.begin(), holder->action_list.end() );
  for ( action_t* a : objects )
    link_children( a, 0, parent_of, extra );
  std::set<const action_t*> listed( objects.begin(), objects.end() );
  for ( action_t* c : extra )
    if ( listed.insert( c ).second )
      objects.push_back( c );
}

void collect_holder( player_t* holder, const char* holder_kind, const std::string& pet_name, std::map<std::string, action_row_t>& rows,
                     std::size_t& object_total )
{
  std::unordered_map<const action_t*, child_link_t> parent_of;
  std::vector<action_t*>                            objects;
  holder_objects( holder, parent_of, objects );
  std::map<std::string, int> objects_per_key;

  for ( action_t* a : objects )
  {
    if ( !a )
      continue;
    ++object_total;
    const std::string key = fmt::format( "{}/{}/{}", holder_kind, pet_name.empty() ? std::string( "-" ) : pet_name, a->name_str );
    auto              it  = rows.find( key );
    const int         held_here = ++objects_per_key[ key ];
    if ( it != rows.end() )
    {
      // objectCount is "objects with this key in ONE holder": several pets of one name (Feral Spirit wolves) must not make the row depend on how
      // many of them this fight happened to summon, or the table signature would differ between fights of one build.
      it->second.object_count = std::max( it->second.object_count, held_here );
      it->second.objects.push_back( a );
      continue;
    }
    action_row_t r;
    r.key    = key;
    r.holder = holder_kind;
    r.pet    = pet_name;
    const auto link = parent_of.find( a );
    const char* via = link == parent_of.end() ? "list" : link->second.via;
    if ( link != parent_of.end() && link->second.parent )
      r.parent = link->second.parent->name_str;
    fill_node( r.node, a, via, holder );
    r.background   = a->background;
    r.harmful      = a->harmful;
    r.object_count = 1;
    r.objects.push_back( a );
    rows.emplace( key, std::move( r ) );
  }
}

struct action_walk_t
{
  std::map<std::string, action_row_t> rows;
  std::size_t                         self_objects = 0;
  std::size_t                         pets         = 0;
  std::size_t                         pet_objects  = 0;
};

action_walk_t collect_actions( player_t* p )
{
  action_walk_t w;
  collect_holder( p, "self", std::string(), w.rows, w.self_objects );
  for ( player_t* pet : p->pet_list )
  {
    if ( !pet )
      continue;
    ++w.pets;
    collect_holder( pet, "pet", pet->name_str, w.rows, w.pet_objects );
  }
  return w;
}

// What must agree between the init walk and the fight-end walk for the table to be written from the init walk: structure, never live values.
// Pet rows are left out: no pet exists at init-finished (measured: pets=0 at init, pets>0 at combat end, A1), so the init walk can only vouch for
// the actor's own actions. The pets' rows are always taken from the combat-end walk.
std::string actions_signature( const action_walk_t& w )
{
  std::string s;
  for ( const auto& kv : w.rows )
  {
    const action_row_t& r = kv.second;
    if ( r.holder != "self" )
      continue;
    const node_t&       n = r.node;
    s += fmt::format( "{}|{}|{}|{},{},{},{},{},{},{},{},{},{};\n", r.key, r.parent, n.via, n.spell_id, shape_name( n.shape ), n.radius, n.range,
                      n.cap_static, n.soft_cap, n.chain, static_cast<int>( n.filter.kind ), r.object_count, r.harmful );
  }
  return s;
}

// Reach of one row over the fight just played: executes (and, once the cause tally lands, the button and class split).
struct reach_t
{
  long long                       executes = 0;
  std::map<std::string, long long> by_button;
  std::map<std::string, long long> by_class;
};

// 261028-fzl (D4): the census-only cause tally, per sim and action object. A pure observer fed by action_t::execute (note_execute below).
struct tally_t
{
  long long                        total = 0;
  long long                        by_class[ 7 ] = {};
  std::map<std::string, long long> by_button;
};
std::unordered_map<const sim_t*, std::unordered_map<const action_t*, tally_t>> g_tally;
// decision number -> the header button pressed in that decision, per sim (see note_execute)
std::unordered_map<const sim_t*, std::unordered_map<long long, std::string>> g_button_of_seq;

const char* const CLASS_KEYS[ 7 ] = { "cast", "procOfCast", "dot", "procOfDot", "auto", "procOfAuto", "orphan" };

// The cast tokens of the active header: the only buttons byButton names.
bool is_header_button( const std::string& name )
{
  static const std::set<std::string> buttons = [] {
    std::set<std::string> b;
    for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
      if ( RL_ACTIONS[ i ].kind == rl_action_kind::cast && RL_ACTIONS[ i ].token != nullptr )
        b.insert( RL_ACTIONS[ i ].token );
    return b;
  }();
  return buttons.count( name ) != 0;
}

// Reach of one row over the fight just played. executes is the number of times an object of this row ran action_t::execute(); the class split
// sums to it by construction (every execute is tallied once, in one class). Dual or stats-sharing actions need no special case.
reach_t reach_of( const sim_t* sim, const action_row_t& r )
{
  reach_t out;
  std::lock_guard<std::mutex> lock( g_mutex );
  const auto                  per_sim = g_tally.find( sim );
  if ( per_sim == g_tally.end() )
    return out;
  for ( const action_t* a : r.objects )
  {
    const auto it = per_sim->second.find( a );
    if ( it == per_sim->second.end() )
      continue;
    out.executes += it->second.total;
    for ( int c = 0; c < 7; ++c )
      if ( it->second.by_class[ c ] > 0 )
        out.by_class[ CLASS_KEYS[ c ] ] += it->second.by_class[ c ];
    for ( const auto& kv : it->second.by_button )
      out.by_button[ kv.first ] += kv.second;
  }
  if ( out.executes > 0 )
    for ( const char* key : CLASS_KEYS )
      out.by_class.emplace( key, 0 );  // all seven keys on every reached row (zeros included)
  return out;
}

std::string map_json( const std::map<std::string, long long>& m )
{
  std::string o = "{";
  bool        first = true;
  for ( const auto& kv : m )
  {
    o += ( first ? "" : "," ) + esc( kv.first ) + ":" + std::to_string( kv.second );
    first = false;
  }
  return o + "}";
}

std::string reach_json( const reach_t& r )
{
  return fmt::format( "{{\"executes\":{},\"byButton\":{},\"byClass\":{}}}", r.executes, map_json( r.by_button ), map_json( r.by_class ) );
}

// The row object of C-2. `reach` non-null merges the reach fields into the row (the ordinary single-process file).
std::string action_row_json( const action_row_t& r, const reach_t* reach )
{
  const std::string node = node_json( r.node );  // starts with {"name": ...
  const std::string target_set =
      r.node.has_target_set
          ? fmt::format( "{{\"kind\":{},\"dot\":{},\"impact\":{}}}", esc( r.node.target_set.kind ), esc( r.node.target_set.dot ),
                         esc( r.node.target_set.impact ) )
          : std::string( "null" );
  std::string       o    = fmt::format( "{{\"holder\":{},\"pet\":{},\"parent\":{},\"background\":{},\"harmful\":{},\"targetSet\":{},\"objectCount\":{},",
                                 esc( r.holder ), r.pet.empty() ? std::string( "null" ) : esc( r.pet ),
                                 r.parent.empty() ? std::string( "null" ) : esc( r.parent ), r.background ? "true" : "false",
                                 r.harmful ? "true" : "false", target_set, r.object_count );
  o += node.substr( 1 );
  if ( reach )
  {
    o.pop_back();
    o += fmt::format( ",\"executes\":{},\"byButton\":{},\"byClass\":{}}}", reach->executes, map_json( reach->by_button ),
                      map_json( reach->by_class ) );
  }
  return o;
}

// Rendered rows by key. `self_override` (list mode) replaces the actor's own rows with the ones rendered at init-finished.
using rendered_rows_t = std::map<std::string, std::string>;

rendered_rows_t render_rows( const sim_t* sim, const action_walk_t& w, bool with_reach, const rendered_rows_t* self_override )
{
  rendered_rows_t out;
  if ( self_override )
    out = *self_override;
  for ( const auto& kv : w.rows )
  {
    if ( self_override && kv.second.holder == "self" )
      continue;
    if ( with_reach )
    {
      const reach_t reach = reach_of( sim, kv.second );
      out[ kv.first ] = action_row_json( kv.second, &reach );
    }
    else
      out[ kv.first ] = action_row_json( kv.second, nullptr );
  }
  return out;
}

std::string join_rows( const rendered_rows_t& rows )
{
  std::string o = "[";
  bool        first = true;
  for ( const auto& kv : rows )
  {
    o += ( first ? "" : "," ) + kv.second;
    first = false;
  }
  return o + "]";
}

std::string actions_json( const sim_t* sim, const action_walk_t& w, bool with_reach )
{
  return join_rows( render_rows( sim, w, with_reach, nullptr ) );
}

std::string tokens_json( const std::vector<token_t>& tokens )
{
  std::string o = "[";
  for ( size_t i = 0; i < tokens.size(); ++i )
    o += ( i ? "," : "" ) + token_json( tokens[ i ] );
  return o + "]";
}

// The C-2 table object (no reach fields). `tokens` and `actions` are the already-rendered arrays.
std::string table_json( player_t* p, const std::string& tokens, const std::string& actions, bool init_matches_end )
{
  // Phase 271.6 plan 18 (G271.6-37): the spell ids of the buffs that lock this actor's class-module actions, sorted ascending.
  std::vector<unsigned> lock_auras = p->channel_lock_aura_spell_ids();
  std::sort( lock_auras.begin(), lock_auras.end() );
  std::string lock_list = "[";
  for ( size_t i = 0; i < lock_auras.size(); ++i )
    lock_list += ( i ? "," : "" ) + std::to_string( lock_auras[ i ] );
  lock_list += "]";
  return fmt::format( "{{\"schemaVersion\":2,\"actor\":{},\"initMatchesEnd\":{},\"lockAuraSpellIds\":{},\"tokens\":{},\"actions\":{}}}",
                      esc( p->name_str ), init_matches_end ? "true" : "false", lock_list, tokens, actions );
}

// Per-sim state the list mode keeps from the init walk (erased at combat end, research A2 item 3: a freed entry's address is reused).
struct init_state_t
{
  std::string     tokens_sig;
  std::string     actions_sig;  // the actor's own rows only (see actions_signature)
  std::string     tokens;       // rendered init-walk tokens (procOnly false, no evidence)
  rendered_rows_t self_rows;    // rendered init-walk rows of the actor's own actions (no reach)
};
std::unordered_map<const sim_t*, init_state_t> g_init_state;

// A1 measurement (plan task S1-F1a-T1): are the pet action lists populated at init? Printed once per process per phase.
void note_counts( const action_walk_t& w, const char* at, bool& printed )
{
  if ( printed )
    return;
  printed = true;
  fmt::print( stderr, "[RL_ACTION_CENSUS] actions={} pets={} petActions={} rows={} at={}\n", w.self_objects, w.pets, w.pet_objects, w.rows.size(), at );
  std::fflush( stderr );
}
bool g_printed_init = false;
bool g_printed_end  = false;

// Ordinary single-process file: one object, written at init (no reach) and rewritten at combat end (reach merged into the rows).
void write_file( sim_t* sim, player_t* p, const std::vector<token_t>& tokens, const action_walk_t& walk, bool init_matches_end, bool with_reach )
{
  const std::string body = table_json( p, tokens_json( tokens ), actions_json( sim, walk, with_reach ), init_matches_end ) + "\n";
  if ( !rl_census_lines::write_text( g_path, body, "wb" ) )
    fmt::print( stderr, "rl_action_census: cannot open '{}' for writing\n", g_path );
}

// List mode: one JSON line per entry at its combat end (C-1).
void append_list_line( sim_t* sim, player_t* p, const std::string& table, const action_walk_t& end_walk )
{
  const std::string signature = rl_census_lines::sha256_hex( table );
  const bool        first     = rl_census_lines::first_time_signature( 0, signature );
  std::string      reach = "{";
  bool             first_row = true;
  for ( const auto& kv : end_walk.rows )
  {
    const reach_t r = reach_of( sim, kv.second );
    if ( r.executes <= 0 )
      continue;
    reach += ( first_row ? "" : "," ) + esc( kv.first ) + ":" + reach_json( r );
    first_row = false;
  }
  reach += "}";
  const std::string line = fmt::format( "{{\"schemaVersion\":2,\"kind\":\"action\",\"entry\":{},\"crc32\":{},\"signature\":\"{}\",\"table\":{},\"reach\":{}}}\n",
                                        sim->rl_list_index, sim->rl_list_crc32, signature, first ? table : std::string( "null" ), reach );
  (void)p;
  if ( !rl_census_lines::write_text( g_path, line, "ab" ) )
    fmt::print( stderr, "rl_action_census: cannot open '{}' for appending\n", g_path );
}

}  // namespace

bool find_holder_node( player_t* holder, const std::string& name, node_t& out )
{
  if ( holder == nullptr )
    return false;
  std::unordered_map<const action_t*, child_link_t> parent_of;
  std::vector<action_t*>                            objects;
  holder_objects( holder, parent_of, objects );
  for ( action_t* a : objects )
  {
    if ( a == nullptr || a->name_str != name )
      continue;
    const auto  link = parent_of.find( a );
    const char* via  = link == parent_of.end() ? "list" : link->second.via;
    fill_node( out, a, via, holder );
    return true;
  }
  return false;
}

void node_of_action( action_t* a, const char* via, player_t* holder, node_t& out )
{
  fill_node( out, a, via, holder );
}

const char* shape_name( shape_t s )
{
  switch ( s )
  {
    case shape_t::CHAIN: return "CHAIN";
    case shape_t::FILTERED: return "FILTERED";
    case shape_t::GROUND: return "GROUND";
    case shape_t::AROUND_TARGET: return "AROUND_TARGET";
    case shape_t::AROUND_PLAYER: return "AROUND_PLAYER";
    case shape_t::REACH: return "REACH";
    default: return "ANYWHERE";
  }
}

std::string& path_storage()
{
  return g_path;
}

bool filter_contains( const filter_descriptor_t& f, const player_t* player, const player_t* axis_target,
                      const player_t* candidate )
{
  if ( f.kind == filter_kind_t::NONE || f.kind == filter_kind_t::UNKNOWN )
    return true;
  double ax = player->facing_x;
  double ay = player->facing_y;
  if ( f.axis == filter_axis_t::FACING )
  {
    if ( !player->sim->facing_shapes )
      return true;
  }
  else
  {
    if ( !axis_target )
      return true;
    const double dx  = axis_target->x_position - player->x_position;
    const double dy  = axis_target->y_position - player->y_position;
    const double len = std::sqrt( dx * dx + dy * dy );
    if ( len <= 0.0 )
      return true;  // no meaningful direction
    ax = dx / len;
    ay = dy / len;
  }
  if ( f.kind == filter_kind_t::CONE )
    return cone_contains( player->x_position, player->y_position, ax, ay, candidate->x_position, candidate->y_position,
                          candidate->combat_reach, f.cos_half_angle, f.radius );
  return rect_contains( player->x_position, player->y_position, ax, ay, candidate->x_position, candidate->y_position,
                        candidate->combat_reach, f.length, f.half_width );
}

const char* gate_class_name( gate_class_t c )
{
  switch ( c )
  {
    case gate_class_t::UNIT: return "UNIT";
    case gate_class_t::SHAPE: return "SHAPE";
    default: return "EXEMPT";
  }
}

bool needs_melee_reach( const token_t& t )
{
  return t.gate_class == gate_class_t::EXEMPT;
}

namespace
{
// True for a `T_UNIT_TARGET_*` member of simc's own `effect_targeting` enum (engine/dbc/data_enums.hh): the press needs a unit.
// The members are named, never numbered here; the bind-time check against the generated header is what proves this list agrees
// with the list gen_hit_rows.py read from the fork's own table (its member names come from sc_spell_info.cpp).
bool is_unit_target_code( unsigned code )
{
  switch ( static_cast<effect_targeting>( code ) )
  {
    case T_UNIT_TARGET_ENEMY:
    case T_UNIT_TARGET_ALLY:
    case T_UNIT_TARGET_ANY:
    case T_UNIT_TARGET_PARTY:
    case T_UNIT_TARGET_CHAINHEAL_ALLY:
    case T_UNIT_TARGET_MINIPET:
    case T_UNIT_TARGET_PASSENGER:
    case T_UNIT_TARGET_ALLY_OR_RAID:
    case T_UNIT_TARGET_TAP_LIST:
      return true;
    default:
      return false;
  }
}

// Phase 271.7 plan 04 (NXTB-02; the owner, 2026-10-11 about 19:35Z: "why is cleave shape gated? it needs a target"). True for the
// implicit target codes that are NOT `T_UNIT_TARGET_*` members and whose own description in the fork's target table
// (engine/dbc/sc_spell_info.cpp, `_targeting_strings`) says the shape is aimed at a targeted enemy, or whose enum member name ends
// `_TO_DESTINATION_ENEMY`: 104 "Enemies in Cone to Targeted Enemy", 108 "Gameobject in Cone to Targeted Enemy", 116 "Last Targeted
// Enemy" and 134 "Line to Enemy". A spell with such a code AND a cast range above zero is pressed on a unit, so the game's own range
// check gates it. Codes 16 ("at Enemy in Area") and 53 ("At Enemy") stay out: a press with only those has a shape round the target
// and no unit is needed to press it. The members are named, never numbered here; gen_hit_rows.py reads the same members as data from
// the fork's two files and the repository selftest compares the two lists.
constexpr bool is_targeted_enemy_code( unsigned code )
{
  switch ( static_cast<effect_targeting>( code ) )
  {
    case T_UNIT_CONE_CASTER_TO_DESTINATION_ENEMY:
    case T_GAMEOBJECT_CONE_CASTER_TO_DESTINATION_ENEMY:
    case T_UNIT_AND_DESTINATION_LAST_ENEMY:
    case T_UNIT_LINE_CASTER_TO_DESTINATION_ENEMY:
      return true;
    default:
      return false;
  }
}
// The build is this rule's selftest (the fork has no unit-test harness): the cone to a targeted enemy is code 104, and the two
// "at enemy" codes a shape-only press carries are not in the list.
static_assert( is_targeted_enemy_code( T_UNIT_CONE_CASTER_TO_DESTINATION_ENEMY ) && T_UNIT_CONE_CASTER_TO_DESTINATION_ENEMY == 104u,
               "gate class rule: a cone from the caster to a targeted enemy is target code 104 and counts as a unit press" );
static_assert( !is_targeted_enemy_code( 16u ) && !is_targeted_enemy_code( 53u ),
               "gate class rule: 'at Enemy in Area' (16) and 'At Enemy' (53) do not make a press need a unit" );
}  // namespace

gate_class_t gate_class_default( const action_t* root, const node_t* counted )
{
  if ( root != nullptr )
  {
    const spell_data_t& d = root->data();
    for ( size_t i = 1; i <= d.effect_count(); ++i )
    {
      const spelleffect_data_t& e = d.effectN( i );
      if ( is_unit_target_code( e.target_1() ) || is_unit_target_code( e.target_2() ) )
        return gate_class_t::UNIT;
    }
    // A cone or line aimed at a targeted enemy, on a spell with a cast range: pressed on a unit (see is_targeted_enemy_code).
    if ( root->range > 0.0 )
    {
      for ( size_t i = 1; i <= d.effect_count(); ++i )
      {
        const spelleffect_data_t& e = d.effectN( i );
        if ( is_targeted_enemy_code( e.target_1() ) || is_targeted_enemy_code( e.target_2() ) )
          return gate_class_t::UNIT;
      }
    }
  }
  if ( counted != nullptr && counted->shape != shape_t::ANYWHERE )
    return gate_class_t::SHAPE;
  return gate_class_t::EXEMPT;
}

bool any_enemy_in_player_shape( const player_t* p, const player_t* axis_target, const filter_descriptor_t& f, double radius )
{
  // The gust face: more yards still to run than the strike radius, so nothing is in reach.
  if ( p->current.distance_to_move > radius )
    return false;
  const sim_t* sim = p->sim;
  if ( !sim->distance_targeting_enabled )
    return true;  // no positions: the rule cannot be asked, play is unchanged
  for ( const player_t* t : sim->target_non_sleeping_list )
  {
    if ( !t->is_enemy() || !rl_target_select::rl_counts_as_enemy( t ) || sim->is_untargetable_enemy( t ) )
      continue;
    if ( f.kind != filter_kind_t::NONE && !filter_contains( f, p, axis_target, t ) )
      continue;
    if ( t->get_player_distance( *p ) <= radius + t->combat_reach )
      return true;
  }
  return false;
}

bool any_enemy_around_player( const player_t* p, double radius )
{
  return any_enemy_in_player_shape( p, nullptr, filter_descriptor_t{}, radius );
}

std::vector<token_t> walk( player_t* p )
{
  std::vector<token_t> out;
  for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
  {
    const rl_action_desc& d = RL_ACTIONS[ i ];
    if ( d.kind != rl_action_kind::cast || d.token == nullptr )
      continue;
    token_t t;
    t.token = d.token;
    action_t* root = p->find_action( d.token );
    if ( root )
    {
      t.found     = true;
      t.spell_id  = root->data().id();
      t.range     = root->range;
      t.min_range = root->data().min_range();
      t.radius    = root->radius;
      t.refused_while_locked = root->refused_while_channel_locked();
      std::set<const action_t*> seen;
      walk_node( root, "root", 0, p, seen, t.nodes );
    }
    out.push_back( std::move( t ) );
  }
  return out;
}

void check_header_gate_classes( const player_t* p, const std::vector<token_t>& walk )
{
#ifdef RL_HIT_ROW_NEEDS_UNIT_DEFINED
  for ( std::size_t k = 0; k < RL_HIT_PROVIDER_COUNT; ++k )
  {
    if ( std::strncmp( RL_HIT_PROVIDERS[ k ].provider, "census.", 7 ) != 0 )
      continue;
    const char* token = RL_HIT_PROVIDERS[ k ].provider + 7;
    // 261028-fzl (S5-F2, D4): a row of a proc, a pet's action or a carrier sum (`census.action.<name>`, `census.pet.<pet>.<name>`) belongs to
    // no button, so the spell data of no button classifies it: its class is PROC, the header's word alone, and it must say so (gate proc,
    // needs no unit). A header that gives such a row another class, or a token row the class PROC, is refused by name.
    const bool proc_row = std::strncmp( token, "action.", 7 ) == 0 || std::strncmp( token, "pet.", 4 ) == 0;
    const rl_hit_gate header_gate = RL_HIT_PROVIDERS[ k ].gate;
    if ( proc_row )
    {
      if ( header_gate != rl_hit_gate::proc || RL_HIT_ROW_NEEDS_UNIT[ k ] )
        throw sc_runtime_error( fmt::format(
            "rl_policy: registry '{}' header gives hit row '{}' (a proc, pet or carrier-sum row, no button) a class other than PROC "
            "(bind-time refusal, player '{}')",
            RL_REGISTRY_ID, token, p->name() ) );
      continue;
    }
    if ( header_gate == rl_hit_gate::proc || header_gate != ( RL_HIT_ROW_NEEDS_UNIT[ k ] ? rl_hit_gate::unit : rl_hit_gate::shape ) )
      throw sc_runtime_error( fmt::format(
          "rl_policy: registry '{}' header gives button hit row '{}' a gate class that disagrees with its needs-a-unit table entry "
          "(bind-time refusal, player '{}')",
          RL_REGISTRY_ID, token, p->name() ) );
    for ( const token_t& t : walk )
    {
      if ( t.token != token || !t.found || t.nodes.empty() || t.nodes.front().action == nullptr )
        continue;
      // An action built from an untaken talent has spell data that is not enabled for this player (it can never be pressed);
      // it says nothing about the class, as the aimed-range binder skips it too.
      if ( !t.nodes.front().action->data().ok() )
        continue;
      // An owner's recorded word replaced the data default in the generated table: the header's class is taken, uncompared.
      if ( RL_HIT_ROW_CLASS_OVERRIDDEN[ k ] )
        continue;
      const gate_class_t engine_class = t.gate_class;
      const bool         engine_unit  = engine_class == gate_class_t::UNIT;
      if ( engine_class == gate_class_t::EXEMPT || engine_unit != RL_HIT_ROW_NEEDS_UNIT[ k ] )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy: registry '{}' header says hit row '{}' {} but player '{}' classifies it {} from its spell data "
            "(bind-time refusal: the generated class table and the engine must agree; an owner override is recorded only in "
            "the registry's gateClassOverrides, never here)",
            RL_REGISTRY_ID, token, RL_HIT_ROW_NEEDS_UNIT[ k ] ? "needs a unit" : "needs no unit", p->name(),
            gate_class_name( engine_class ) ) );
      }
    }
  }
#else
  for ( std::size_t k = 0; k < RL_HIT_PROVIDER_COUNT; ++k )
  {
    if ( std::strncmp( RL_HIT_PROVIDERS[ k ].provider, "census.", 7 ) == 0 )
      throw sc_runtime_error( fmt::format(
          "rl_policy: registry '{}' header declares census hit row '{}' but carries no class table for player '{}' "
          "(bind-time refusal: the header predates RL_HIT_ROW_NEEDS_UNIT; regenerate it with gen_rl_constants.py)",
          RL_REGISTRY_ID, RL_HIT_PROVIDERS[ k ].provider + 7, p->name() ) );
  }
  (void)walk;
#endif
}

void check_header_reach_classes( const player_t* p, const std::vector<token_t>& walk )
{
#ifdef RL_ACTION_NEEDS_MELEE_REACH_DEFINED
  for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
  {
    const rl_action_desc& d = RL_ACTIONS[ i ];
    if ( d.kind != rl_action_kind::cast || d.token == nullptr )
      continue;
    for ( const token_t& t : walk )
    {
      if ( t.token != d.token || !t.found || t.nodes.empty() || t.nodes.front().action == nullptr )
        continue;
      const action_t* root = t.nodes.front().action;
      // An action built from an untaken talent has spell data that is not enabled for this player (it can never be pressed); it
      // says nothing about the table. An item use has no spell data at all (use_item_t, player.cpp), so it is compared, not skipped.
      // It is recognised by used_item(), the virtual accessor: use_item_t keeps its item in its OWN shadowing member, and the base
      // member action_t::item is never assigned for it (action.hpp, the used_item() comment), so reading `item` would skip the trinket.
      if ( root->used_item() == nullptr && !root->data().ok() )
        continue;
      const bool engine_needs = needs_melee_reach( t );
      if ( engine_needs != RL_ACTION_NEEDS_MELEE_REACH[ i ] )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy: registry '{}' header says button '{}' {} an enemy within melee reach but player '{}' answers it {} "
            "(bind-time refusal: the generated reach table and the engine must agree; regenerate the census and the header)",
            RL_REGISTRY_ID, d.token, RL_ACTION_NEEDS_MELEE_REACH[ i ] ? "needs" : "does not need", p->name(),
            engine_needs ? "does" : "does not" ) );
      }
      break;
    }
  }
#else
  (void)p;
  (void)walk;
#endif
}

void check_header_lock_table( const player_t* p, const std::vector<action_t*>& handles )
{
  const std::vector<unsigned> engine_auras = p->channel_lock_aura_spell_ids();
#ifdef RL_ACTION_LOCK_TABLE_DEFINED
  for ( std::size_t i = 0; i < RL_ACTION_DIM && i < handles.size(); ++i )
  {
    const rl_action_desc& d = RL_ACTIONS[ i ];
    if ( d.kind != rl_action_kind::cast || d.token == nullptr || handles[ i ] == nullptr )
      continue;
    // An action built from an untaken talent has spell data that is not enabled for this player: it can never be pressed.
    if ( !handles[ i ]->data().ok() )
      continue;
    // A build-dependent answer (Storm Bolt: usable while channelling only with the Slayer talent Unrelenting Onslaught): the
    // header carries the census build's answer; the training deal varies the hero tree, so a dealt build may answer otherwise.
    if ( RL_ACTION_LOCK_ANSWER_VARIES[ i ] )
      continue;
    const bool engine_refused = handles[ i ]->refused_while_channel_locked();
    if ( engine_refused != RL_ACTION_REFUSED_WHILE_LOCKED[ i ] )
    {
      throw sc_runtime_error( fmt::format(
          "rl_policy: registry '{}' header says button '{}' {} refused while locked but player '{}' answers {} "
          "(bind-time refusal: the generated lock table and the engine must agree; regenerate the census and the header)",
          RL_REGISTRY_ID, d.token, RL_ACTION_REFUSED_WHILE_LOCKED[ i ] ? "is" : "is not", p->name(),
          engine_refused ? "it is" : "it is not" ) );
    }
  }
  for ( unsigned id : engine_auras )
  {
    bool listed = false;
    for ( std::size_t k = 0; k < RL_LOCK_AURA_COUNT; ++k )
      if ( RL_LOCK_AURA_SPELL_IDS[ k ] == static_cast<long long>( id ) )
        listed = true;
    if ( !listed )
      throw sc_runtime_error( fmt::format(
          "rl_policy: registry '{}' header does not list lock aura {} but player '{}' reports it "
          "(bind-time refusal: the generated lock aura list and the engine must agree; regenerate the census and the header)",
          RL_REGISTRY_ID, id, p->name() ) );
  }
#else
  // No lock table in this header: that is right for a spec that locks nothing, and a stale header for one that does.
  for ( std::size_t i = 0; i < RL_ACTION_DIM && i < handles.size(); ++i )
  {
    const rl_action_desc& d = RL_ACTIONS[ i ];
    if ( d.kind != rl_action_kind::cast || d.token == nullptr || handles[ i ] == nullptr || !handles[ i ]->data().ok() )
      continue;
    if ( handles[ i ]->refused_while_channel_locked() )
      throw sc_runtime_error( fmt::format(
          "rl_policy: registry '{}' header carries no lock table but player '{}' refuses button '{}' while locked "
          "(bind-time refusal: the header predates the lock table; regenerate it with gen_rl_constants.py)",
          RL_REGISTRY_ID, p->name(), d.token ) );
  }
  if ( !engine_auras.empty() )
    throw sc_runtime_error( fmt::format(
        "rl_policy: registry '{}' header carries no lock table but player '{}' reports lock aura {} "
        "(bind-time refusal: the header predates the lock table; regenerate it with gen_rl_constants.py)",
        RL_REGISTRY_ID, p->name(), engine_auras.front() ) );
#endif
}

std::vector<token_t> walk_classified( player_t* p )
{
  return walk_all( p, false );
}

int count_engine_hits( const node_t& n, const player_t* centre )
{
  const action_t* a = n.action;
  if ( a == nullptr || centre == nullptr )
    return 0;
  const int cap = a->n_targets();
  if ( cap == 0 )
    return 1;  // single target: the engine takes the aoe branch only for -1 or a positive cap

  if ( n.shape == shape_t::CHAIN )
  {
    const int hops = rl_target_select::chain_hop_count_for_start( a, centre );
    const int h    = hops > 0 ? hops : 1;
    return cap > 0 ? std::min( cap, h ) : h;
  }

  sim_t*          sim    = a->sim;
  const player_t* player = a->player;
  int             hits   = 1;  // the centre: check_distance_targeting never removes the primary target
  // 271.6-07 (G271.6-19, G271.6-24): a node centred on the player (Bladestorm's circle, Colossus Smash's front half circle)
  // follows the reach rule: while the player has more yards still to run than the radius it hits nothing, and the centre
  // counts only when it passes the same test as every other enemy (the centre is not exempt). Legality and the
  // `hits.<button>` input then never disagree (G271.6-16). Nodes of every other shape, and every no-position fight, are
  // counted exactly as before; enhancement has no census rows.
  const bool around_player = sim->distance_targeting_enabled && n.underlying == shape_t::AROUND_PLAYER;
  if ( around_player )
  {
    if ( player->current.distance_to_move > n.radius )
      return 0;
    hits = 0;
  }
  if ( sim->distance_targeting_enabled )
  {
    for ( player_t* t : sim->target_non_sleeping_list )
    {
      if ( ( t == centre && !around_player ) || !t->is_enemy() || !rl_target_select::rl_counts_as_enemy( t ) || sim->is_untargetable_enemy( t ) )
        continue;
      if ( n.filter.kind == filter_kind_t::UNKNOWN )
      {
        if ( a->target_filter_callback && !a->target_filter_callback( a, t ) )
          continue;
      }
      else if ( n.filter.kind != filter_kind_t::NONE && !filter_contains( n.filter, player, centre, t ) )
        continue;

      bool inside = true;
      switch ( n.underlying )
      {
        case shape_t::GROUND:
          if ( t->debuffs.flying && t->debuffs.flying->check() )
            inside = false;
          else
            inside = t->get_position_distance( centre->x_position, centre->y_position ) <= n.radius + t->combat_reach;
          break;
        case shape_t::AROUND_TARGET: inside = t->get_player_distance( *centre ) <= n.radius; break;
        case shape_t::AROUND_PLAYER: inside = t->get_player_distance( *player ) <= n.radius + t->combat_reach; break;
        case shape_t::REACH: inside = t->get_player_distance( *player ) <= n.range + t->combat_reach; break;
        default: break;
      }
      if ( inside )
        ++hits;
    }
  }
  else
  {
    for ( player_t* t : sim->target_non_sleeping_list )
      if ( t != centre && t->is_enemy() && rl_target_select::rl_counts_as_enemy( t ) && !sim->is_untargetable_enemy( t ) )
        ++hits;
  }
  return cap > 0 ? std::min( cap, hits ) : hits;
}

bool tally_active()
{
  return !g_path.empty();
}

void note_execute( const action_t* a, unsigned char cause_class, const action_t* button, long long decision_seq )
{
  if ( g_path.empty() || a == nullptr )
    return;
  const unsigned base = rl_cause_base( cause_class );
  const int      idx  = base < 7 ? static_cast<int>( base ) : 6;
  std::lock_guard<std::mutex> lock( g_mutex );
  tally_t& t = g_tally[ a->sim ][ a ];
  ++t.total;
  ++t.by_class[ idx ];
  // Only the foreground press and what it set off name a button; a button is a cast token of the active header.
  if ( idx != RL_CAUSE_CAST && idx != RL_CAUSE_PROC_OF_CAST )
    return;
  auto& button_of_seq = g_button_of_seq[ a->sim ];
  if ( button != nullptr && is_header_button( button->name_str ) )
  {
    // The press of this decision, remembered for the procs it schedules (below).
    if ( idx == RL_CAUSE_CAST )
      button_of_seq[ decision_seq ] = button->name_str;
    ++t.by_button[ button->name_str ];
  }
  else if ( button == nullptr && idx == RL_CAUSE_PROC_OF_CAST )
  {
    // A proc that runs on a later event (the cause stack is empty by then) still carries the decision number of the press that caused it.
    const auto it = button_of_seq.find( decision_seq );
    if ( it != button_of_seq.end() )
      ++t.by_button[ it->second ];
  }
}

int count_target_set_hits( const node_t& n, const node_t& impact, const player_t* actor )
{
  if ( !n.has_target_set || actor == nullptr || actor->sim == nullptr )
    return 0;
  const sim_t* sim  = actor->sim;
  int          hits = 0;
  for ( player_t* carrier : sim->target_non_sleeping_list )
  {
    if ( !carrier || !carrier->is_enemy() || !rl_target_select::rl_counts_as_enemy( carrier ) || sim->is_untargetable_enemy( carrier ) )
      continue;
    bool carries = false;
    for ( const dot_t* d : carrier->dot_list )
    {
      if ( d && d->source == actor && d->name_str == n.target_set.dot && d->is_ticking() )
      {
        carries = true;
        break;
      }
    }
    if ( carries )
      hits += count_engine_hits( impact, carrier );
  }
  return hits;
}

int count_target_set_hits( const node_t& n, const player_t* actor )
{
  if ( !n.has_target_set || n.action == nullptr || n.action->impact_action == nullptr )
    return 0;
  node_t impact;
  fill_node( impact, n.action->impact_action, "impact", n.action->player );
  return count_target_set_hits( n, impact, actor );
}

void on_init_finished( sim_t* sim )
{
  if ( g_path.empty() )
    return;
  player_t* p = census_actor( sim );
  if ( !p )
    return;
  const std::vector<token_t> tokens = walk_all( p, false );
  const action_walk_t        walk   = collect_actions( p );
  note_counts( walk, "init", g_printed_init );
  {
    std::lock_guard<std::mutex> lock( g_mutex );
    g_init_signature[ sim ] = signature( tokens );
    init_state_t st;
    st.tokens_sig  = g_init_signature[ sim ];
    st.actions_sig = actions_signature( walk );
    st.tokens      = tokens_json( tokens );
    st.self_rows   = render_rows( sim, walk, false, nullptr );
    g_init_state[ sim ] = std::move( st );
  }
  if ( sim->rl_list_active )
  {
    // List mode (261028-fzl C-1): entry 0 truncates the file and starts the signature set over; every entry appends its own line at combat end.
    if ( sim->rl_list_index == 0 )
    {
      rl_census_lines::reset_signatures( 0 );
      if ( !rl_census_lines::write_text( g_path, std::string(), "wb" ) )
        fmt::print( stderr, "rl_action_census: cannot open '{}' for writing\n", g_path );
    }
    return;
  }
  write_file( sim, p, tokens, walk, false, false );
}

void on_combat_end( sim_t* sim )
{
  if ( g_path.empty() )
    return;
  player_t* p = census_actor( sim );
  if ( !p )
    return;
  const action_walk_t end_walk = collect_actions( p );
  note_counts( end_walk, "end", g_printed_end );

  if ( sim->rl_list_active )
  {
    // The table's token rows and the actor's own action rows come from the INIT walk when the fight-end structure equals it (stable per build, so
    // one signature per build); the pets' rows come from the fight-end walk (no pet exists at init, A1). A build whose end structure differs
    // writes the end structure under initMatchesEnd false (its own signature). procOnly / evidence are not table fields here: the reader derives
    // them from the reach (child executes against root executes).
    init_state_t st;
    {
      std::lock_guard<std::mutex> lock( g_mutex );
      auto it = g_init_state.find( sim );
      if ( it != g_init_state.end() )
        st = it->second;
    }
    const std::vector<token_t> end_tokens = walk_all( p, false );
    const bool                 match      = !st.tokens_sig.empty() && st.tokens_sig == signature( end_tokens ) && st.actions_sig == actions_signature( end_walk );
    const std::string          table      = match ? table_json( p, st.tokens, join_rows( render_rows( sim, end_walk, false, &st.self_rows ) ), true )
                                                  : table_json( p, tokens_json( end_tokens ), actions_json( sim, end_walk, false ), false );
    append_list_line( sim, p, table, end_walk );
    {
      std::lock_guard<std::mutex> lock( g_mutex );
      g_init_state.erase( sim );
      g_init_signature.erase( sim );
      g_tally.erase( sim );
      g_button_of_seq.erase( sim );
    }
    return;
  }

  const std::vector<token_t> tokens = walk_all( p, true );
  bool                       match  = false;
  {
    std::lock_guard<std::mutex> lock( g_mutex );
    auto it = g_init_signature.find( sim );
    auto st = g_init_state.find( sim );
    match   = it != g_init_signature.end() && it->second == signature( tokens ) && st != g_init_state.end() &&
            st->second.actions_sig == actions_signature( end_walk );
  }
  write_file( sim, p, tokens, end_walk, match, true );
  {
    // The next iteration of an ordinary process starts its tally over.
    std::lock_guard<std::mutex> lock( g_mutex );
    g_tally.erase( sim );
    g_button_of_seq.erase( sim );
  }
}

}  // namespace rl_action_census
