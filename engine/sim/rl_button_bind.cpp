// ==========================================================================
// tstl-sylvanas Phase 271.3, plan 271.3-01 (BIND-01, BIND-02). See rl_button_bind.hpp.
// ==========================================================================

#include "sim/rl_button_bind.hpp"

#include "action/action.hpp"
#include "fmt/format.h"
#include "player/action_priority_list.hpp"
#include "player/player.hpp"
#include "sim/cooldown.hpp"
#include "sim/rl_policy_constants_select.h"
#include "sim/sim.hpp"
#include "util/git_info.hpp"
#include "util/util.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rl_button_bind
{

namespace
{

// Sim option storage. A global because adding a member to sim_t would rebuild every file; the RL rigs run
// one sim per process.
std::string g_report_path;

// The bound table: per actor, one action pointer per header action index (null for waits and for an
// unbound actor) plus the bound flag. Stored only under the single-sim single-thread precondition the other
// process-wide caches use (threads == 1 and no profilesets); every other case computes the same answer on
// demand.
struct table_t
{
  std::vector<action_t*> bound;
  bool                   is_bound = false;
};
std::mutex                                    g_mutex;
std::unordered_map<const player_t*, table_t> g_table;

bool is_rl_actor( const player_t* p )
{
  return p != nullptr && std::strcmp( p->name(), RL_ACTOR_NAME ) == 0 && !p->is_pet();
}

bool is_cast_index( std::size_t i )
{
  return i < RL_ACTION_DIM && RL_ACTIONS[ i ].kind == rl_action_kind::cast;
}

// True when the line builds `token` by its base name, or by its base name plus one name= option, whatever
// other options it carries. Used to name an offending line in a refusal; never to bind.
bool line_names_token( const std::string& line, const char* token )
{
  const auto  comma = line.find( ',' );
  std::string base  = comma == std::string::npos ? line : line.substr( 0, comma );
  if ( base == token )
    return true;
  if ( comma == std::string::npos )
    return false;
  std::size_t pos = comma + 1;
  while ( pos <= line.size() )
  {
    const auto        next  = line.find( ',', pos );
    const std::string piece = line.substr( pos, next == std::string::npos ? std::string::npos : next - pos );
    if ( piece.compare( 0, 5, "name=" ) == 0 && base + "_" + piece.substr( 5 ) == token )
      return true;
    if ( next == std::string::npos )
      break;
    pos = next + 1;
  }
  return false;
}

// The line text after the action name with a single name= part removed ("" for every valid catalog line).
std::string line_options( const std::string& line )
{
  const auto comma = line.find( ',' );
  if ( comma == std::string::npos )
    return std::string();
  std::string out;
  bool        removed_name = false;
  std::size_t pos          = comma + 1;
  while ( pos <= line.size() )
  {
    const auto        next  = line.find( ',', pos );
    const std::string piece = line.substr( pos, next == std::string::npos ? std::string::npos : next - pos );
    if ( !removed_name && piece.compare( 0, 5, "name=" ) == 0 )
      removed_name = true;
    else
    {
      if ( !out.empty() )
        out += ',';
      out += piece;
    }
    if ( next == std::string::npos )
      break;
    pos = next + 1;
  }
  return out;
}

// Mirrors the anonymous helper player.cpp uses to decide which actions count as real off-GCD actions.
bool is_real_action( const action_t* a )
{
  return a->type != ACTION_CALL && a->type != ACTION_VARIABLE && !util::str_compare_ci( a->name_str, "run_action_list" ) &&
         !util::str_compare_ci( a->name_str, "swap_action_list" );
}

bool in_off_gcd_poll_set( const action_t* a )
{
  return a->action_list != nullptr && a->trigger_gcd == timespan_t::zero() && !a->background && a->use_off_gcd &&
         is_real_action( a );
}

bool in_cast_while_casting_poll_set( const action_t* a )
{
  return a->action_list != nullptr && !a->background && a->use_while_casting && a->usable_while_casting &&
         is_real_action( a );
}

// The name of the first parsed APL-line field set on the action, or nullptr when every one is unset.
const char* first_parsed_field_set( const action_t* a )
{
  if ( !a->option.if_expr_str.empty() )
    return "if";
  if ( !a->option.interrupt_if_expr_str.empty() )
    return "interrupt_if";
  if ( !a->option.early_chain_if_expr_str.empty() )
    return "early_chain_if";
  if ( !a->option.cancel_if_expr_str.empty() )
    return "cancel_if";
  if ( !a->option.target_if_str.empty() )
    return "target_if";
  if ( !a->option.sync_str.empty() )
    return "sync";
  if ( !a->option.target_str.empty() )
    return "target";
  if ( a->option.cycle_targets != 0 )
    return "cycle_targets";
  if ( a->option.cycle_players )
    return "cycle_players";
  if ( a->option.moving != -1 )
    return "moving";
  if ( a->line_cooldown && a->line_cooldown->duration != timespan_t::zero() )
    return "line_cd";
  return nullptr;
}

struct button_t
{
  std::size_t             index = 0;
  const char*             token = "";
  std::vector<action_t*>  matches;       // default-list line-built actions whose line declares the token
  action_t*               optioned = nullptr;  // a default-list line that names the token but carries other options

  action_t* bound() const
  {
    return matches.size() == 1 ? matches[ 0 ] : nullptr;
  }
  // The action the report describes: the bound one, else the offending optioned line, else none.
  action_t* shown() const
  {
    if ( action_t* b = bound() )
      return b;
    if ( !matches.empty() )
      return matches[ 0 ];
    return optioned;
  }
};

struct eval_t
{
  action_priority_list_t* owned = nullptr;
  std::vector<button_t>   buttons;  // cast actions only, in header index order
  std::string             refusal;  // empty when every rule holds
};

std::string refusal_text( const player_t* p, const std::string& token, const std::string& list, const std::string& reason,
                          const std::string& lines )
{
  return fmt::format( "rl_button_bind: registry '{}' actor '{}' token '{}' list '{}': {} ({})", RL_REGISTRY_ID,
                      p != nullptr ? p->name() : "none", token, list, reason, lines );
}

std::string quote_line( const action_t* a )
{
  return a != nullptr ? fmt::format( "line '{}'", a->signature_str ) : std::string( "no line" );
}

// Rules R1 to R5 for one actor (R0, no actor, is the caller's). Reads only.
eval_t evaluate( const player_t* p )
{
  eval_t ev;
  ev.owned = p->find_action_priority_list( "default" );
  for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
  {
    if ( !is_cast_index( i ) )
      continue;
    button_t b;
    b.index = i;
    b.token = RL_ACTIONS[ i ].token;
    ev.buttons.push_back( std::move( b ) );
  }

  if ( ev.owned == nullptr )
  {
    if ( !ev.buttons.empty() )
      ev.refusal = refusal_text( p, ev.buttons.front().token, "none",
                                 "the actor has no default list: the episode was composed without the registry catalog",
                                 "no line" );
    return ev;
  }

  // One scan of the actor's actions: default-list line-built actions only (list pointer and recorded line,
  // never a name).
  std::vector<const action_t*> owned_lines;
  for ( action_t* a : p->action_list )
  {
    if ( a->action_list != ev.owned || a->signature == nullptr )
      continue;
    owned_lines.push_back( a );
    for ( button_t& b : ev.buttons )
    {
      if ( line_declares( a->signature_str, b.token ) )
        b.matches.push_back( a );
      else if ( b.optioned == nullptr && line_names_token( a->signature_str, b.token ) )
        b.optioned = a;
    }
  }

  const std::string list_name = ev.owned->name_str;

  // R1: every button is declared by a default-list line.
  for ( const button_t& b : ev.buttons )
  {
    if ( !b.matches.empty() )
      continue;
    if ( b.optioned != nullptr )
      ev.refusal = refusal_text( p, b.token, list_name, "the catalog line carries an APL option", quote_line( b.optioned ) );
    else
      ev.refusal = refusal_text( p, b.token, list_name,
                                 "no default-list line declares this button: the episode was composed without the registry catalog",
                                 "no line" );
    return ev;
  }

  // R2: no button is declared twice.
  for ( const button_t& b : ev.buttons )
  {
    if ( b.matches.size() < 2 )
      continue;
    ev.refusal = refusal_text( p, b.token, list_name, "two default-list lines declare this button",
                               fmt::format( "lines '{}' and '{}'", b.matches[ 0 ]->signature_str,
                                            b.matches[ 1 ]->signature_str ) );
    return ev;
  }

  // R3: every default-list line is bare auto_attack or a catalog line.
  for ( const action_t* a : owned_lines )
  {
    if ( a->signature_str == "auto_attack" )
      continue;
    bool in_catalog = false;
    for ( const button_t& b : ev.buttons )
      if ( line_declares( a->signature_str, b.token ) )
      {
        in_catalog = true;
        break;
      }
    if ( !in_catalog )
    {
      ev.refusal = refusal_text( p, "none", list_name, "the default list is not the catalog", quote_line( a ) );
      return ev;
    }
  }

  // R4: the bound action carries no parsed APL-line field.
  for ( const button_t& b : ev.buttons )
  {
    const action_t* a     = b.bound();
    const char*     field = first_parsed_field_set( a );
    if ( field != nullptr )
    {
      ev.refusal = refusal_text( p, b.token, list_name, fmt::format( "the bound action has the APL field '{}' set", field ),
                                 quote_line( a ) );
      return ev;
    }
  }

  // R5: no foreground action of the same name elsewhere shadows the button for the readers that still look
  // up by name. A background action (an untaken talent, an unequipped item) is reported, never refused.
  for ( const button_t& b : ev.buttons )
  {
    const action_t* a  = b.bound();
    const action_t* fa = p->find_action( b.token );
    if ( fa != nullptr && fa != a && !fa->background )
    {
      ev.refusal = refusal_text( p, b.token, fa->action_list != nullptr ? fa->action_list->name_str : std::string( "none" ),
                                 "a foreground action of the same name outside the bound line shadows the button",
                                 quote_line( fa ) );
      return ev;
    }
  }
  return ev;
}

// ---- report ----

std::string jstr( const std::string& s )
{
  std::string out = "\"";
  for ( char c : s )
  {
    switch ( c )
    {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ( static_cast<unsigned char>( c ) < 0x20 )
          out += fmt::format( "\\u{:04x}", static_cast<unsigned>( static_cast<unsigned char>( c ) ) );
        else
          out += c;
    }
  }
  out += '"';
  return out;
}

const char* jbool( bool v )
{
  return v ? "true" : "false";
}

bool pair_in( const std::vector<std::pair<const cooldown_t*, const cooldown_t*>>& v, const action_t* a )
{
  return std::any_of( v.begin(), v.end(), [ a ]( const std::pair<const cooldown_t*, const cooldown_t*>& c ) {
    return c.first == a->cooldown && c.second == a->internal_cooldown;
  } );
}

// One poll-set entry list, and the count of pairs in the player's vector that no owned-list poll-set action
// accounts for.
template <typename Pred>
std::string poll_set_json( const player_t* p, const action_priority_list_t* owned, Pred pred,
                           const std::vector<std::pair<const cooldown_t*, const cooldown_t*>>& pairs, int& foreign )
{
  std::string out = "[";
  bool        first = true;
  for ( const action_t* a : p->action_list )
  {
    if ( !pred( a ) )
      continue;
    if ( !first )
      out += ", ";
    first = false;
    out += fmt::format( "{{\"name\": {}, \"list\": {}, \"line\": {}, \"owned\": {}, \"pairInPollSet\": {}}}",
                        jstr( a->name_str ), a->action_list ? jstr( a->action_list->name_str ) : std::string( "null" ),
                        a->signature ? jstr( a->signature_str ) : std::string( "null" ),
                        jbool( owned != nullptr && a->action_list == owned ), jbool( pair_in( pairs, a ) ) );
  }
  out += "]";
  foreign = 0;
  for ( const auto& c : pairs )
  {
    bool accounted = false;
    for ( const action_t* a : p->action_list )
      if ( pred( a ) && owned != nullptr && a->action_list == owned && c.first == a->cooldown &&
           c.second == a->internal_cooldown )
      {
        accounted = true;
        break;
      }
    if ( !accounted )
      ++foreign;
  }
  return out;
}

std::string report_json( const sim_t* sim, const player_t* p, const eval_t* ev, const std::string& refusal )
{
  const char* maker = !sim->solver_policy_str.empty() ? "solver_policy" : !sim->solver_control_str.empty() ? "solver_control" : "none";
  const bool  bound = p != nullptr && ev != nullptr && refusal.empty();
  std::string out   = "{\n";
  out += "  \"format\": \"rl-bind-report-v1\",\n";
  out += fmt::format( "  \"registryId\": {},\n", jstr( RL_REGISTRY_ID ) );
  out += fmt::format( "  \"actor\": {},\n", p != nullptr ? jstr( p->name() ) : std::string( "null" ) );
  out += fmt::format( "  \"forkCommit\": {},\n", git_info::available() ? jstr( git_info::revision() ) : std::string( "null" ) );
  out += fmt::format( "  \"decisionMaker\": {},\n", jstr( maker ) );
  out += "  \"ownedList\": \"default\",\n";
  out += fmt::format( "  \"bound\": {},\n", jbool( bound ) );
  out += fmt::format( "  \"refusal\": {},\n", refusal.empty() ? std::string( "null" ) : jstr( refusal ) );

  out += "  \"ownedListLines\": [";
  if ( p != nullptr && ev != nullptr && ev->owned != nullptr )
  {
    bool first = true;
    for ( const action_t* a : p->action_list )
    {
      if ( a->action_list != ev->owned || a->signature == nullptr )
        continue;
      out += first ? "" : ", ";
      first = false;
      out += jstr( a->signature_str );
    }
  }
  out += "],\n";

  out += "  \"buttons\": [";
  if ( p != nullptr && ev != nullptr )
  {
    bool first_button = true;
    for ( const button_t& b : ev->buttons )
    {
      const action_t* a = b.shown();
      out += first_button ? "\n" : ",\n";
      first_button = false;
      out += fmt::format( "    {{\"index\": {}, \"token\": {}, ", b.index, jstr( b.token ) );
      if ( a == nullptr )
      {
        out += "\"list\": null, \"line\": null, \"options\": \"\", \"nameStr\": null, \"background\": null, \"useOffGcd\": null, "
               "\"useWhileCasting\": null, \"usableWhileCasting\": null, \"triggerGcdMs\": null, \"findActionIsBound\": false, "
               "\"sameNameElsewhere\": []}";
        continue;
      }
      const action_t* fa = p->find_action( b.token );
      out += fmt::format(
          "\"list\": {}, \"line\": {}, \"options\": {}, \"nameStr\": {}, \"background\": {}, \"useOffGcd\": {}, "
          "\"useWhileCasting\": {}, \"usableWhileCasting\": {}, \"triggerGcdMs\": {}, \"findActionIsBound\": {}, "
          "\"sameNameElsewhere\": [",
          a->action_list ? jstr( a->action_list->name_str ) : std::string( "null" ),
          a->signature ? jstr( a->signature_str ) : std::string( "null" ), jstr( line_options( a->signature_str ) ),
          jstr( a->name_str ), jbool( a->background ), jbool( a->use_off_gcd ), jbool( a->use_while_casting ),
          jbool( a->usable_while_casting ), static_cast<long long>( a->trigger_gcd.total_millis() ),
          jbool( fa == a ) );
      bool first_other = true;
      for ( const action_t* o : p->action_list )
      {
        if ( o == a )
          continue;
        const bool same_name = o->name_str == a->name_str;
        const bool same_line = o->signature != nullptr && line_declares( o->signature_str, b.token );
        if ( !same_name && !same_line )
          continue;
        out += first_other ? "" : ", ";
        first_other = false;
        out += fmt::format( "{{\"list\": {}, \"line\": {}, \"background\": {}}}",
                            o->action_list ? jstr( o->action_list->name_str ) : std::string( "null" ),
                            o->signature ? jstr( o->signature_str ) : std::string( "null" ), jbool( o->background ) );
      }
      out += "]}";
    }
    out += "\n  ";
  }
  out += "],\n";

  int off_gcd_foreign = 0;
  int cwc_foreign     = 0;
  if ( p != nullptr && ev != nullptr )
  {
    out += fmt::format( "  \"offGcdPollSet\": {},\n",
                        poll_set_json( p, ev->owned, in_off_gcd_poll_set, p->off_gcd_cd, off_gcd_foreign ) );
    out += fmt::format( "  \"castWhileCastingPollSet\": {},\n",
                        poll_set_json( p, ev->owned, in_cast_while_casting_poll_set, p->cast_while_casting_cd, cwc_foreign ) );
  }
  else
  {
    out += "  \"offGcdPollSet\": [],\n  \"castWhileCastingPollSet\": [],\n";
  }
  out += fmt::format( "  \"foreignPollPairs\": {{\"offGcd\": {}, \"castWhileCasting\": {}}}\n", off_gcd_foreign, cwc_foreign );
  out += "}\n";
  return out;
}

void write_report( const sim_t* sim, const player_t* p, const eval_t* ev, const std::string& refusal )
{
  const std::string text = report_json( sim, p, ev, refusal );
  std::FILE*        f    = std::fopen( g_report_path.c_str(), "wb" );
  if ( f == nullptr )
  {
    std::fprintf( stderr, "rl_button_bind: cannot open rl_bind_report '%s' for writing\n", g_report_path.c_str() );
    return;
  }
  std::fwrite( text.data(), 1, text.size(), f );
  std::fclose( f );
}

player_t* find_rl_actor( sim_t* sim )
{
  for ( player_t* p : sim->actor_list )
    if ( is_rl_actor( p ) )
      return p;
  return nullptr;
}

}  // namespace

std::string& report_path_storage()
{
  return g_report_path;
}

bool line_declares( const std::string& line, const char* token )
{
  const auto comma = line.find( ',' );
  if ( comma == std::string::npos )
    return line == token;
  const std::string base = line.substr( 0, comma );
  const std::string rest = line.substr( comma + 1 );
  if ( rest.compare( 0, 5, "name=" ) != 0 || rest.find( ',' ) != std::string::npos )
    return false;  // any option other than one name= is not a catalog line
  return base + "_" + rest.substr( 5 ) == token;
}

void on_init_finished( sim_t* sim )
{
  const bool decision_maker = !sim->solver_control_str.empty() || !sim->solver_policy_str.empty();
  player_t*  p              = find_rl_actor( sim );

  eval_t      ev;
  std::string refusal;
  if ( p == nullptr )
  {
    if ( decision_maker )
      refusal = refusal_text( nullptr, "none", "none", "no actor carries the registry's actor name", "no line" );
  }
  else
  {
    ev      = evaluate( p );
    refusal = ev.refusal;
  }

  if ( p != nullptr && sim->threads == 1 && sim->profileset_map.empty() )
  {
    table_t t;
    t.bound.assign( RL_ACTION_DIM, nullptr );
    t.is_bound = refusal.empty();
    if ( t.is_bound )
      for ( const button_t& b : ev.buttons )
        t.bound[ b.index ] = b.bound();
    std::lock_guard<std::mutex> lock( g_mutex );
    g_table[ p ] = std::move( t );
  }

  if ( !g_report_path.empty() )
    write_report( sim, p, p != nullptr ? &ev : nullptr, refusal );

  if ( decision_maker && !refusal.empty() )
    throw sc_runtime_error( refusal );
}

action_t* bound_action( const player_t* p, std::size_t action_index )
{
  if ( !is_cast_index( action_index ) || p == nullptr )
    return nullptr;
  {
    std::lock_guard<std::mutex> lock( g_mutex );
    auto                        it = g_table.find( p );
    if ( it != g_table.end() )
      return it->second.bound[ action_index ];
  }
  if ( !is_rl_actor( p ) )
    return nullptr;
  const eval_t ev = evaluate( p );
  if ( !ev.refusal.empty() )
    return nullptr;
  for ( const button_t& b : ev.buttons )
    if ( b.index == action_index )
      return b.bound();
  return nullptr;
}

action_t* bound_action_for_token( const player_t* p, const std::string& token )
{
  for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
    if ( is_cast_index( i ) && token == RL_ACTIONS[ i ].token )
      return bound_action( p, i );
  return nullptr;
}

bool is_bound( const player_t* p )
{
  if ( p == nullptr )
    return false;
  {
    std::lock_guard<std::mutex> lock( g_mutex );
    auto                        it = g_table.find( p );
    if ( it != g_table.end() )
      return it->second.is_bound;
  }
  if ( !is_rl_actor( p ) )
    return false;
  return evaluate( p ).refusal.empty();
}

void clear()
{
  std::lock_guard<std::mutex> lock( g_mutex );
  g_table.clear();
}

std::size_t entries()
{
  std::lock_guard<std::mutex> lock( g_mutex );
  return g_table.size();
}

}  // namespace rl_button_bind
