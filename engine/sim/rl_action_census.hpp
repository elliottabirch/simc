// ==========================================================================
// tstl-sylvanas Phase 271, plan 271-04 (owner decisions H-2, H-3 scheme A, H-4).
//
// The ACTION DISCOVERY CENSUS and the ONE hit-counting function.
//
// Words used here. A "node" is one engine action reached from a button: the button's own action, or
// its impact, execute, tick or child action. "Engine shape" is how the engine itself chooses the
// enemies a node hits (action_t::check_distance_targeting, engine/action/action.cpp, four rules).
// "Game shape" is what the spell data says the game does. A "disagreement" is a node whose engine
// shape and game shape differ.
//
// What this file does, and does not do.
//   * walk()                reads every cast token of the active registry header, finds its action
//                           with find_action, follows impact / execute / tick / child links and
//                           records every field that decides who a node hits.
//   * count_engine_hits()   mirrors the engine's four distance rules (around the target, around the
//                           player, within reach, ground point) plus the node's filter callback.
//                           It NEVER calls target_list() (that draws random numbers for Chain
//                           Lightning and rebuilds caches), never calls set_target, never
//                           allocates and never draws a random number.
//   * on_init_finished() / on_combat_end()  write the census JSON when the sim option
//                           rl_action_census=<path> is set (init, then rewritten at fight end).
// Nothing else in the fork reads any of it in plan 271-04: the existing hit providers stay as they
// are (plan 271-10 switches the arms columns to count_engine_hits).
//
// The census only READS. It changes no decision, no random draw and no damage.
// ==========================================================================

#pragma once

#include <cmath>
#include <string>
#include <vector>

class action_t;
class player_t;
struct sim_t;

namespace rl_action_census
{

// The seven engine shapes, in the order the classifier tries them (CHAIN first).
enum class shape_t : int
{
  CHAIN,
  FILTERED,
  GROUND,
  AROUND_TARGET,
  AROUND_PLAYER,
  REACH,
  ANYWHERE
};

const char* shape_name( shape_t s );

enum class filter_kind_t : int
{
  NONE,
  CONE,
  RECT,
  UNKNOWN  // a callback that no descriptor explains; counted by calling the callback itself
};

// Where a cone or rectangle points from the player: along the player's facing vector
// (Crash Lightning, Sundering), or toward the action's target (the arms Cleave and Colossus Smash).
enum class filter_axis_t : int
{
  FACING,
  TOWARD_TARGET
};

// A filter the census can explain. Cone: enemies within `radius` (plus the enemy's own reach) of the
// player whose direction makes an angle of at most acos( cos_half_angle ) with the axis.
// Rectangle: `length` yards along the axis, `half_width` yards either side.
struct filter_descriptor_t
{
  filter_kind_t kind           = filter_kind_t::NONE;
  filter_axis_t axis           = filter_axis_t::FACING;
  double        cos_half_angle = 0.0;
  double        radius         = 0.0;
  double        length         = 0.0;
  double        half_width     = 0.0;
};

// A class module may REPLACE the engine's four-rule distance test for an action (Sweeping Strikes
// widens the reach; a Ravager pulse hits around its stored ground point). The census and the
// counting function then use these values instead of reading the stock fields.
struct distance_override_t
{
  bool    active = false;
  shape_t shape  = shape_t::REACH;
  double  radius = 0.0;
  double  range  = 0.0;
};

// Implemented by a class module's action base. Found with dynamic_cast, so an action that does not
// derive from it is simply read from its stock fields.
struct shape_provider_t
{
  virtual ~shape_provider_t() = default;
  virtual bool rl_census_filter( filter_descriptor_t& ) const
  {
    return false;
  }
  virtual bool rl_census_distance_override( distance_override_t& ) const
  {
    return false;
  }
};

// ---- pure geometry, shared by the class modules' filter callbacks and by count_engine_hits ----------
// One copy of each predicate, so a filter in class code and the census can never disagree.

// Cone from (px,py) along the UNIT axis (ax,ay). `reach` is the candidate's own combat reach, which
// widens the radius test only (same convention as rl_target_select::crash_lightning_cone_contains).
inline bool cone_contains( double px, double py, double ax, double ay, double cx, double cy, double reach,
                           double cos_half_angle, double radius )
{
  const double dx   = cx - px;
  const double dy   = cy - py;
  const double dist = std::sqrt( dx * dx + dy * dy );
  if ( dist - reach > radius )
    return false;
  if ( dist <= 0.0 )
    return true;  // coincident with the player: no meaningful direction
  return ( ax * dx + ay * dy ) / dist >= cos_half_angle;
}

// Rectangle from (px,py) along the UNIT axis (ax,ay); reach widens the along-axis length only.
inline bool rect_contains( double px, double py, double ax, double ay, double cx, double cy, double reach,
                           double length, double half_width )
{
  const double dx    = cx - px;
  const double dy    = cy - py;
  const double along = dx * ax + dy * ay;
  const double perp  = std::fabs( dx * ( -ay ) + dy * ax );
  return along >= -reach && along <= length + reach && perp <= half_width;
}

// True when `candidate` passes the descriptor for an action owned by `player` whose axis target is
// `axis_target` (used only when the axis is TOWARD_TARGET). A FACING descriptor passes everything
// when the sim has facing shapes switched off, like the shaman filters do.
bool filter_contains( const filter_descriptor_t& f, const player_t* player, const player_t* axis_target,
                      const player_t* candidate );

// ---- census records -----------------------------------------------------------------------------------

struct widen_t
{
  std::string buff;
  unsigned    aura_id = 0;
  double      extra   = 0.0;
};

struct game_t
{
  double                cone_degrees   = 0.0;
  std::vector<unsigned> implicit_targets;
  double                radius         = 0.0;
  double                radius_max     = 0.0;
  bool                  add_reach_attr = false;
};

struct node_t
{
  const action_t*     action = nullptr;
  std::string         name;
  std::string         via;  // root, impact, execute, tick, child
  unsigned            spell_id = 0;
  shape_t             shape      = shape_t::ANYWHERE;
  shape_t             underlying = shape_t::ANYWHERE;  // the four-rule shape, ignoring chain and filter
  double              radius     = 0.0;                // the hit rule's radius (an override replaces the field)
  double              range      = 0.0;                // the hit rule's range
  bool                add_reach  = false;
  int                 cap_static = 0;
  int                 cap_now    = 0;
  double              soft_cap   = 0.0;
  int                 full_amount_targets = 0;
  bool                split_damage        = false;
  bool                chain               = false;
  bool                override_active     = false;
  filter_descriptor_t filter;
  std::vector<widen_t> widened_by;
  bool                proc_only = false;
  game_t              game;
};

struct token_t
{
  std::string          token;
  bool                 found = false;
  unsigned             spell_id = 0;
  double               range = 0.0;
  double               min_range = 0.0;
  double               radius = 0.0;
  bool                 multi_target = false;
  bool                 has_multi_while = false;
  widen_t              multi_while;
  std::vector<node_t>  nodes;
  int                  chosen_node = -1;
  std::vector<std::string> disagreements;
};

// Walks every cast token of the active registry header for `p`. Reads only.
std::vector<token_t> walk( player_t* p );

// The ONE counting function. `centre` is the enemy the press is aimed at (the engine never removes
// it). Returns how many enemies the node hits right now: 1 for a single-target node, otherwise the
// centre plus every live enemy passing the node's shape (and filter), capped by the live
// n_targets() read (-1 uncapped). A CHAIN node returns the chain-hop count of the stamped decision.
int count_engine_hits( const node_t& node, const player_t* centre );

// Sim hooks (src: sim.cpp). No-ops unless rl_action_census=<path> is set.
std::string& path_storage();
void on_init_finished( sim_t* sim );
void on_combat_end( sim_t* sim );

}  // namespace rl_action_census
