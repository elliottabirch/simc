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
//                           rl_action_census=<path> is set (init, then rewritten at fight end). 261028-fzl: schemaVersion 2 adds the
//                           `actions` array (every action object of the actor and its pets) and, per row, the reach of the fight played
//                           (`executes`, `byButton`, `byClass`, from the cause tally note_execute feeds). In a fight list
//                           (sim_t::rl_list_active) the file is JSON Lines instead: one line per entry at its combat end, the table once per
//                           distinct signature (sha256 of its bytes) in the process, the reach on every line.
// Plan 271-10 then switched the arms hit columns to count_engine_hits (walk_classified below).
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

// 261028-fzl (S1-F1a, D2): an action whose hit set is NOT a geometric shape but a list of enemies: the carriers of one of the actor's own DoTs
// (Fire Nova: every enemy with the actor's Flame Shock on it), the impact action then counted round each carrier. The one bespoke shape hook of
// the hits family; the class module declares it, the census writes it as `targetSet`, count_target_set_hits() counts it.
struct target_set_t
{
  std::string kind;    // "dot_carriers"
  std::string dot;     // the actor's own dot, by name (flame_shock)
  std::string impact;  // the impact action's name (fire_nova_explosion)
};

// Implemented by a class module's action base. Found with dynamic_cast, so an action that does not
// derive from it is simply read from its stock fields.
struct shape_provider_t
{
  virtual ~shape_provider_t() = default;
  virtual bool rl_census_target_set( target_set_t& ) const
  {
    return false;
  }
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

// ---- the reach rule (tstl-sylvanas 271.6-07, owner ~16:00Z 2026-10-11: "it might be smart to mask aoe spells that dont
// have a target, if they dont have any units in range.", decision G271.6-15, widened by G271.6-24, keyed on the gate class by
// G271.6-28 as revised) ----------------------------------------------------------------------------------------------------
// A button of gate class SHAPE (it needs no unit of its own, and its damage is centred on the player: Bladestorm, Colossus
// Smash) may be used only when at least one enemy stands inside its strike shape where the player actually is. A button of
// class UNIT (the press needs a unit and the game's range check gates it: Mortal Strike, and Cleave by the owner's recorded
// word of ~19:35Z, "why is cleave shape gated? it needs a target") gets no reach rule here. In the engine a gust (a movement
// raid event) never moves the player: it sets `distance_to_move`, the yards still to run, so "where the player actually is" is
// the melee spot minus those yards, and with more yards to run than the strike radius no enemy is in reach. With distance
// targeting off there are no positions and the answer is yes (fights without positions play exactly as before). The enemy
// filter is the one `count_engine_hits` uses, so legality and the `hits.<button>` input are one definition (G271.6-16).
// The second form also requires the enemy to pass `f` (the front half circle of Colossus Smash, a cone round `axis_target`);
// a descriptor of kind NONE reduces it to the first form. Reads only; never draws a random number.
bool any_enemy_in_player_shape( const player_t* p, const player_t* axis_target, const filter_descriptor_t& f, double radius );
bool any_enemy_around_player( const player_t* p, double radius );

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
  // 261028-fzl (D2): the enemy list this node hits when it is a set of carriers rather than a shape (see target_set_t); has_target_set false otherwise.
  bool                has_target_set = false;
  target_set_t        target_set;
  // 261013-aimx class B: the sample behind the proc verdict. Set by mark_proc_children (fight end only) on a child node that has its own
  // statistics and is not dual: the child's execute count and its root's execute count over the iteration the census file describes (a root that
  // never ran reads rootExecutes 0, and then no verdict was possible). -1 when none applies (the init walk, a non-child node, a node that shares
  // or cannot be told apart from its root's statistics). A "plain part of the button" verdict resting on a small root count is thin: the census
  // merge tool refuses it by name.
  long long           proc_evidence_executes      = -1;
  long long           proc_evidence_root_executes = -1;
  game_t              game;
};

// The gate class of a hit-row button (owner ~19:20Z 2026-10-11: "crash lightning doesn not require a target, so correct that
// class discovery, and ensure similar classes dont get labeled the same"; decision G271.6-28 as revised). UNIT: the press needs a
// unit (a `T_UNIT_TARGET_*` implicit target code on the root action's spell) and the game's range check gates it. SHAPE: no
// such code and the counted node has a shape: the press needs no unit, so the reach rule gates it. EXEMPT: neither (no hit row).
enum class gate_class_t
{
  UNIT,
  SHAPE,
  EXEMPT
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
  gate_class_t         gate_class = gate_class_t::EXEMPT;  // filled by walk_classified (the data default; see gate_class_default)
  // Phase 271.6 plan 18 (G271.6-37): the engine's own answer to "does the channel lock refuse this button", read from the root
  // action's refused_while_channel_locked() in walk(). False for a token whose action was not found and for item uses.
  bool                 refused_while_locked = false;
};

// The ONE gate-class function (271.6-07, G271.6-28 as revised). The class the spell DATA gives a button: UNIT when any implicit
// target code of the root action's spell (`target_1()` or `target_2()` of any effect) is a `T_UNIT_TARGET_*` member of simc's own
// `effect_targeting` enum; otherwise SHAPE when `counted` (the node whose shape counts the hits, null when there is none) is
// non-null and its shape is not ANYWHERE; otherwise EXEMPT. The census writer records it per token (`gateClass`) and the bind-time
// check below compares it with the generated header, so the two can never be two readings. An owner's recorded override is NOT
// applied here: it lives only in the registry's gateClassOverrides and reaches the engine through the header.
gate_class_t gate_class_default( const action_t* root, const node_t* counted );
const char*  gate_class_name( gate_class_t c );

// Phase 271.7 plan 04 (NXTB-01): does the press need an enemy within melee reach? True exactly when the token's gate class is EXEMPT:
// a press that needs no unit and has no hit shape (a self buff, a button with no hit row). The census writes it per token
// (`needsMeleeReach`) and the bind check of the generated header reads it, so both use this one expression.
bool         needs_melee_reach( const token_t& t );

// The bind-time class check (271.6-07, G271.6-28 as revised; the model is the aimed-range refusal in rl_target_select). For every
// hit-row button of the active header the actor has, the class the engine reads from the spell data must equal the header's
// `RL_HIT_ROW_NEEDS_UNIT`, unless the header marks the row `RL_HIT_ROW_CLASS_OVERRIDDEN` (the owner's recorded word replaces the
// data default, and the engine takes the header's class without comparison). A mismatch throws by name. A header that declares
// census hit rows but carries no class table throws too. Reads only.
void check_header_gate_classes( const player_t* p, const std::vector<token_t>& walk );

// The bind-time reach-table check (Phase 271.7 plan 05, NXTB-01; the model is check_header_gate_classes above). For every cast button
// of the active header whose action exists for the actor, the engine's own answer `needs_melee_reach( token )` must equal the header's
// `RL_ACTION_NEEDS_MELEE_REACH` entry. A button built from an untaken talent (its spell data is not enabled for this player, so it can
// never be pressed) says nothing and is skipped, EXCEPT an item use: `use_item_t` is built with no spell data, so its `data().ok()` is
// always false, and the on-use trinket must be compared, never skipped. A mismatch throws by name. A header without the table
// (RL_ACTION_NEEDS_MELEE_REACH_DEFINED absent, as enhancement's) is not asked: the body does nothing. Reads only.
void check_header_reach_classes( const player_t* p, const std::vector<token_t>& walk );

// The bind-time lock check (271.6-18, G271.6-37; the model is check_header_gate_classes above). For every cast button of the active
// header whose action exists for the actor and whose spell data is enabled for it (an untaken talent can never be pressed, so it
// says nothing; the aimed-range binder skips it too), the engine's own answer `refused_while_channel_locked()` must equal the
// header's `RL_ACTION_REFUSED_WHILE_LOCKED` entry, unless the header marks the row `RL_ACTION_LOCK_ANSWER_VARIES` (the answer
// depends on the talent build, so the table carries the census build's answer and a dealt build may differ). Every lock aura the
// actor lists must be one of the header's `RL_LOCK_AURA_SPELL_IDS`. A mismatch throws by name. A header without the lock table is
// refused only when the engine refuses some button or lists some lock aura for this actor (a stale arms header); a header for a
// spec that locks nothing (enhancement) passes. `handles` is in RL_ACTIONS order (nullptr for a wait or an action not found).
// Reads only.
void check_header_lock_table( const player_t* p, const std::vector<action_t*>& handles );

// Walks every cast token of the active registry header for `p`. Reads only.
std::vector<token_t> walk( player_t* p );

// walk() followed by the census file's own node classification (which node of each button names its hit rule: `chosen_node`,
// `multi_target`), WITHOUT the fight-end proc marking (that needs a played fight). 271-10 (H-3): the generated "census.<button>"
// hit columns resolve their node from this, once per actor at bind time, and count with count_engine_hits. Always available,
// independent of the rl_action_census=<path> option. Reads only; nothing is cached here.
std::vector<token_t> walk_classified( player_t* p );

// The ONE counting function. `centre` is the enemy the press is aimed at (the engine never removes
// it). Returns how many enemies the node hits right now: 1 for a single-target node, otherwise the
// centre plus every live enemy passing the node's shape (and filter), capped by the live
// n_targets() read (-1 uncapped). A CHAIN node returns the chain-hop count of the stamped decision.
int count_engine_hits( const node_t& node, const player_t* centre );

// 261028-fzl (D2): the hits of a node with a target set (Fire Nova): the sum over every enemy that carries the actor's own `n.target_set.dot` of
// count_engine_hits( impact node, carrier ), the engine's own per-carrier cap applying through count_engine_hits. Reads only; it finds the dot with
// find_dot (never get_dot, which would create one). The 3-argument form takes the impact node resolved once by the caller (no allocation); the
// 2-argument form resolves it from n.action->impact_action on every call (it allocates a node: census use and tests, not the decision path).
int count_target_set_hits( const node_t& n, const node_t& impact, const player_t* actor );
int count_target_set_hits( const node_t& n, const player_t* actor );

// 261028-fzl (S5-F2): the node of ONE named action object of a holder (the actor for a `census.action.<name>` hit row, a pet for a
// `census.pet.<pet>.<name>` row), found in the same object set the table's `actions[]` array is written from (the holder's action list, then
// what hangs off it through impact, execute, tick and child links to depth 3); the first object of that name. False when the holder has none
// (a build without the talent): the row then reads 0, like a button the build cannot press. Reads only.
bool find_holder_node( player_t* holder, const std::string& name, node_t& out );

// The node of one action object, filled exactly as the table walk fills it (the impact node of a target-set node is resolved by the binder
// once, through this). Reads only.
void node_of_action( action_t* a, const char* via, player_t* holder, node_t& out );

// 261028-fzl (D4): the cause tally of the action census, a pure observer. action_t::execute() calls note_execute for every execute while a census
// path is set; `cause_class` is the stamp's class byte (rl_cause_class), `button` the owner of the bottom-most CAST frame of the actor's
// rl_cause_stack (null when there is none). Only the buttons of the active header (RL_ACTIONS cast tokens) are counted in byButton. `decision_seq` is
// the stamp's decision number: a PROC_OF_CAST that runs on a later event (empty cause stack, no button) is attributed to the button pressed in
// that decision, remembered from the CAST execute of the same decision.
bool tally_active();
void note_execute( const action_t* a, unsigned char cause_class, const action_t* button, long long decision_seq );

// Sim hooks (src: sim.cpp). No-ops unless rl_action_census=<path> is set.
std::string& path_storage();
void on_init_finished( sim_t* sim );
void on_combat_end( sim_t* sim );

}  // namespace rl_action_census
