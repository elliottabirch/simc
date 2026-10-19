// ==========================================================================
// tstl-sylvanas Phase 271.4, plan 271.4-03 (AGREE-01). See rl_stock_eval.hpp.
// ==========================================================================

#include "sim/rl_stock_eval.hpp"

#include "action/action.hpp"
#include "action/variable.hpp"
#include "fmt/format.h"
#include "player/action_priority_list.hpp"
#include "player/action_variable.hpp"
#include "player/player.hpp"
#include "sim/rl_policy_constants_select.h"
#include "sim/sim.hpp"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace rl_stock_eval
{

namespace
{

// Sim option storage. See the header.
std::string g_list_name;

// The deepest call/run chain the walk follows. The engine itself refuses a loop by throwing; the walk only
// reports it (F_DEPTH) so a recorder can never abort a fight.
constexpr int MAX_DEPTH = 16;

// The strict leaf's name buffer in the `.apl` record is 48 bytes (47 characters plus the NUL).
constexpr std::size_t NAME_BYTES = 48;

bool is_rl_actor( const player_t* p )
{
  return p != nullptr && std::strcmp( p->name(), RL_ACTOR_NAME ) == 0 && !p->is_pet();
}

std::string refusal_text( const std::string& reason )
{
  return fmt::format( "rl_stock_eval: registry '{}' list '{}': {}", RL_REGISTRY_ID, g_list_name, reason );
}

// Restores every actor variable's two mutable values when it goes out of scope (so a throw inside the walk can
// not leave a changed variable behind).
struct variable_guard_t
{
  struct saved_t
  {
    action_variable_t* var;
    double             current;
    double             previous;
  };

  std::vector<saved_t> saved;

  explicit variable_guard_t( player_t* p )
  {
    saved.reserve( p->variables.size() );
    for ( action_variable_t* v : p->variables )
      saved.push_back( { v, v->current_value_, v->previous_value_ } );
  }

  variable_guard_t( const variable_guard_t& )            = delete;
  variable_guard_t& operator=( const variable_guard_t& ) = delete;

  ~variable_guard_t()
  {
    for ( const saved_t& s : saved )
    {
      s.var->current_value_  = s.current;
      s.var->previous_value_ = s.previous;
    }
  }
};

struct walk_t
{
  std::uint8_t flags        = 0;
  bool         have_strict  = false;
  std::int16_t strict_index = -1;
  std::string  strict_name;
  bool         strict_no_gcd = false;
  bool         refused       = false;
};

// The button index of a leaf: the registry's cast button whose token equals the leaf's engine name, else -1.
int button_index( const action_t* a )
{
  const char* name = a->name();
  for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
  {
    if ( RL_ACTIONS[ i ].kind == rl_action_kind::cast && RL_ACTIONS[ i ].token != nullptr &&
         std::strcmp( name, RL_ACTIONS[ i ].token ) == 0 )
      return static_cast<int>( i );
  }
  return -1;
}

// One list at depth `depth`. Mirrors player_t::select_action's loop (player.cpp), minus its side effects. Returns the
// first ready MAPPED leaf below this list, or nullptr; the first ready leaf of any kind is recorded in `w` once.
const action_t* walk( const action_priority_list_t& list, execute_type et, walk_t& w, int depth )
{
  if ( depth > MAX_DEPTH )
  {
    w.flags |= F_DEPTH;
    return nullptr;
  }
  if ( list.random == 1 )
  {
    // select_action would draw the player's random stream here.
    w.refused = true;
    return nullptr;
  }

  const std::vector<action_t*>* a_list = &list.foreground_action_list;
  switch ( et )
  {
    case execute_type::OFF_GCD:
      a_list = &list.off_gcd_actions;
      break;
    case execute_type::CAST_WHILE_CASTING:
      a_list = &list.cast_while_casting_actions;
      break;
    default:
      break;
  }

  for ( action_t* a : *a_list )
  {
    if ( a->background )
      continue;

    if ( a->option.wait_on_ready == 1 )
      break;

    // action_ready() rolls the random stream when the line's own skill is not 1: refuse before calling it.
    if ( a->action_skill != 1 )
    {
      w.refused = true;
      return nullptr;
    }

    if ( !a->action_ready() )
      continue;

    if ( a->type == ACTION_VARIABLE )
    {
      variable_t* v = static_cast<variable_t*>( a );
      if ( v->operation != OPERATION_SET )
        w.flags |= F_VARIABLE_OP;
      // print and report lines write to a log, never to a value the lists read; they are not run.
      if ( v->operation != OPERATION_PRINT && v->operation != OPERATION_REPORT )
        a->execute();  // undone by the variable guard in evaluate()
      continue;
    }

    if ( a->type == ACTION_CALL )
    {
      call_action_list_t* call = static_cast<call_action_list_t*>( a );
      if ( call->alist != nullptr )
      {
        if ( const action_t* r = walk( *call->alist, et, w, depth + 1 ) )
          return r;
        if ( w.refused )
          return nullptr;
      }
      continue;  // nothing came back: the parent continues
    }

    if ( swap_action_list_t* sw = dynamic_cast<swap_action_list_t*>( a ) )
    {
      // run_action_list and swap_action_list: never executed. The real engine's next pass reads the sub-list as the
      // active list, so the parent does not continue.
      w.flags |= F_LIST_SWITCH;
      if ( sw->alist == nullptr )
        return nullptr;
      return walk( *sw->alist, et, w, depth + 1 );
    }

    // A leaf.
    const int idx = button_index( a );
    if ( !w.have_strict )
    {
      w.have_strict   = true;
      w.strict_index  = idx >= 0 ? static_cast<std::int16_t>( idx ) : static_cast<std::int16_t>( -2 );
      w.strict_name   = a->name();
      w.strict_no_gcd = a->trigger_gcd <= timespan_t::zero();
    }
    if ( idx >= 0 )
      return a;
    // An unmapped leaf does not stop the walk: the first mapped leaf below it is still wanted.
  }
  return nullptr;
}

}  // namespace

std::string& list_name_storage()
{
  return g_list_name;
}

bool enabled( const sim_t* sim )
{
  return sim != nullptr && sim->solver_record_apl_choice && !g_list_name.empty();
}

void on_init_finished( sim_t* sim )
{
  if ( !enabled( sim ) )
    return;

  if ( g_list_name == "default" || g_list_name == "precombat" )
    throw sc_runtime_error( refusal_text( "the name 'default' / 'precombat' is the engine's own list, not a preserved copy" ) );

  player_t* actor = nullptr;
  for ( player_t* p : sim->actor_list )
  {
    if ( is_rl_actor( p ) )
    {
      actor = p;
      break;
    }
  }
  if ( actor == nullptr )
    throw sc_runtime_error( refusal_text( "no actor carries the registry's actor name" ) );

  if ( actor->find_action_priority_list( g_list_name ) == nullptr )
    throw sc_runtime_error( refusal_text( fmt::format(
        "the actor '{}' declares no such list: the profile was composed without the preserved copy", actor->name() ) ) );

  if ( actor->base.skill != 1 )
    throw sc_runtime_error( refusal_text( fmt::format(
        "the actor '{}' has skill {} (not 1): the list walk would draw the random stream", actor->name(), actor->base.skill ) ) );
}

pick_t evaluate( player_t* p, execute_type et )
{
  pick_t out;
  switch ( et )
  {
    case execute_type::OFF_GCD:
      out.boundary = 1;
      break;
    case execute_type::CAST_WHILE_CASTING:
      out.boundary = 2;
      break;
    default:
      out.boundary = 0;
      break;
  }

  if ( p->current.skill != 1 || p->current.skill_debuff != 0 )
  {
    out.flags = F_REFUSED;
    return out;
  }

  const action_priority_list_t* list = p->find_action_priority_list( g_list_name );
  if ( list == nullptr )
  {
    out.flags = F_REFUSED;
    return out;
  }

  walk_t          w;
  const action_t* mapped = nullptr;
  {
    variable_guard_t guard( p );
    mapped = walk( *list, et, w, 0 );
  }

  out.flags = w.flags;
  if ( w.refused )
  {
    out.flags = static_cast<std::uint8_t>( out.flags | F_REFUSED );
    return out;  // no pick at all
  }

  if ( w.have_strict )
  {
    out.strict_index = w.strict_index;
    out.strict_name  = w.strict_name;
    if ( w.strict_no_gcd )
      out.flags = static_cast<std::uint8_t>( out.flags | F_STRICT_NO_GCD );
    if ( out.strict_name.size() >= NAME_BYTES )
      out.flags = static_cast<std::uint8_t>( out.flags | F_NAME_TRUNCATED );
  }
  if ( mapped != nullptr )
    out.mapped_index = static_cast<std::int16_t>( button_index( mapped ) );
  return out;
}

}  // namespace rl_stock_eval
