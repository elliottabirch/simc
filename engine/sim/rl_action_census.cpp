// ==========================================================================
// tstl-sylvanas Phase 271, plan 271-04 (owner decisions H-2, H-3 scheme A, H-4).
// The action discovery census and the one hit-counting function. See rl_action_census.hpp.
// ==========================================================================

#include "sim/rl_action_census.hpp"

#include "action/action.hpp"
#include "buff/buff.hpp"
#include "dbc/dbc.hpp"
#include "fmt/format.h"
#include "player/player.hpp"
#include "player/stats.hpp"
#include "sim/rl_policy_constants_select.h"
#include "sim/rl_target_select.hpp"
#include "sim/sim.hpp"
#include "util/util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
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
  if ( root_exec == 0 )
    return;
  for ( node_t& n : t.nodes )
  {
    if ( n.via != "child" || !n.action || !n.action->stats || n.action->stats == root->stats )
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
  return fmt::format(
      "{{\"name\":{},\"via\":{},\"spellId\":{},\"shape\":{},\"underlyingShape\":{},\"radiusYards\":{},\"rangeYards\":{},"
      "\"addReach\":{},\"capStatic\":{},\"capNow\":{},\"softCap\":{},\"fullAmountTargets\":{},\"splitDamage\":{},"
      "\"chain\":{},\"filter\":{},\"filterParams\":{},\"filterAxis\":{},\"ruleOverridden\":{},\"widenedBy\":{},"
      "\"procOnly\":{},\"game\":{{\"coneDegrees\":{},\"implicitTargets\":{},\"radiusYards\":{},\"radiusMaxYards\":{},"
      "\"addReachAttr\":{}}}}}",
      esc( n.name ), esc( n.via ), n.spell_id, esc( shape_name( n.shape ) ), esc( shape_name( n.underlying ) ),
      num( n.radius ), num( n.range ), n.add_reach ? "true" : "false", n.cap_static, n.cap_now, num( n.soft_cap ),
      n.full_amount_targets, n.split_damage ? "true" : "false", n.chain ? "true" : "false", filter, params, axis,
      n.override_active ? "true" : "false", widened, n.proc_only ? "true" : "false", num( n.game.cone_degrees ), implicit,
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
      "\"multiTarget\":{},\"multiWhile\":{},\"nodes\":{},\"chosenNode\":{},\"disagreements\":{}}}",
      esc( t.token ), t.found ? "true" : "false", t.spell_id, num( t.range ), num( t.min_range ), num( t.radius ),
      t.multi_target ? "true" : "false", t.has_multi_while ? widen_json( t.multi_while ) : std::string( "null" ), nodes,
      t.chosen_node, dis );
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

void write_file( sim_t* sim, player_t* p, const std::vector<token_t>& tokens, bool init_matches_end )
{
  std::string body = fmt::format( "{{\"schemaVersion\":1,\"actor\":{},\"initMatchesEnd\":{},\"tokens\":[", esc( p->name_str ),
                                  init_matches_end ? "true" : "false" );
  for ( size_t i = 0; i < tokens.size(); ++i )
    body += ( i ? "," : "" ) + token_json( tokens[ i ] );
  body += "]}\n";
  std::FILE* f = std::fopen( g_path.c_str(), "wb" );
  if ( !f )
  {
    fmt::print( stderr, "rl_action_census: cannot open '{}' for writing\n", g_path );
    return;
  }
  std::fwrite( body.data(), 1, body.size(), f );
  std::fclose( f );
}

}  // namespace

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
      std::set<const action_t*> seen;
      walk_node( root, "root", 0, p, seen, t.nodes );
    }
    out.push_back( std::move( t ) );
  }
  return out;
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
  if ( sim->distance_targeting_enabled )
  {
    for ( player_t* t : sim->target_non_sleeping_list )
    {
      if ( t == centre || !t->is_enemy() || !rl_target_select::rl_counts_as_enemy( t ) || sim->is_untargetable_enemy( t ) )
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

void on_init_finished( sim_t* sim )
{
  if ( g_path.empty() )
    return;
  player_t* p = census_actor( sim );
  if ( !p )
    return;
  const std::vector<token_t> tokens = walk_all( p, false );
  {
    std::lock_guard<std::mutex> lock( g_mutex );
    g_init_signature[ sim ] = signature( tokens );
  }
  write_file( sim, p, tokens, false );
}

void on_combat_end( sim_t* sim )
{
  if ( g_path.empty() )
    return;
  player_t* p = census_actor( sim );
  if ( !p )
    return;
  const std::vector<token_t> tokens = walk_all( p, true );
  bool                       match  = false;
  {
    std::lock_guard<std::mutex> lock( g_mutex );
    auto it = g_init_signature.find( sim );
    match   = it != g_init_signature.end() && it->second == signature( tokens );
  }
  write_file( sim, p, tokens, match );
}

}  // namespace rl_action_census
