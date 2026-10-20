// ==========================================================================
// tstl-sylvanas quick task 261014-bufx (class A1): the engine buff table. See rl_buff_census.hpp.
// ==========================================================================

#include "sim/rl_buff_census.hpp"

#include "action/action.hpp"
#include "action/dbc_proc_callback.hpp"
#include "action/dot.hpp"
#include "buff/buff.hpp"
#include "dbc/dbc.hpp"
#include "fmt/format.h"
#include "item/special_effect.hpp"
#include "player/player.hpp"
#include "sim/cooldown.hpp"
#include "sim/proc_rng.hpp"
#include "sim/sim.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace rl_buff_census
{

namespace
{
// Sim option storage. A global because adding a member to sim_t would rebuild every file; the RL rigs run one sim per process.
std::string g_path;

struct row_t
{
  std::string holder;  // "self" or "enemy"
  std::string kind;    // "buff" or "dot"
  std::string name;
  unsigned    spell_id            = 0;
  int         max_stack           = 1;
  bool        asynchronous        = false;
  double      base_duration       = 0.0;
  double      cooldown            = 0.0;
  double      internal_cooldown   = 0.0;
  bool        has_rppm            = false;
  double      rppm_frequency      = 0.0;
  double      default_chance      = 0.0;
  bool        constant            = false;
  bool        applied             = false;
  bool        channeled           = false;  // a dot: its applying action is a channel (the dot is the channel's tick carrier, not an aura on the enemy)
  bool        triggered           = false;  // a buff: executed at least once this fight (last_trigger_time is set only by buff_t::execute)
};

using key_t = std::tuple<std::string, std::string, std::string>;

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

player_t* census_actor( sim_t* sim )
{
  for ( player_t* p : sim->player_no_pet_list )
    if ( p && !p->is_enemy() && !p->is_pet() )
      return p;
  return nullptr;
}

// Merge one more object into the table: identical (holder, kind, name) rows of different enemies become one row. Static fields
// keep the first value seen, ceilings keep the largest, flags are or-ed (the same rule the reader applies across fights).
void add_row( std::map<key_t, row_t>& rows, const row_t& r )
{
  const key_t key( r.holder, r.kind, r.name );
  auto        it = rows.find( key );
  if ( it == rows.end() )
  {
    rows.emplace( key, r );
    return;
  }
  row_t& old = it->second;
  if ( old.spell_id == 0 )
    old.spell_id = r.spell_id;
  if ( r.max_stack > old.max_stack )
  {
    old.max_stack    = r.max_stack;
    old.asynchronous = r.asynchronous;
  }
  old.base_duration     = std::max( old.base_duration, r.base_duration );
  old.cooldown          = std::max( old.cooldown, r.cooldown );
  old.internal_cooldown = std::max( old.internal_cooldown, r.internal_cooldown );
  old.rppm_frequency    = std::max( old.rppm_frequency, r.rppm_frequency );
  old.default_chance    = std::max( old.default_chance, r.default_chance );
  old.has_rppm          = old.has_rppm || r.has_rppm;
  old.constant          = old.constant || r.constant;
  old.applied           = old.applied || r.applied;
  old.channeled         = old.channeled || r.channeled;
  old.triggered         = old.triggered || r.triggered;
}

row_t buff_row( const buff_t* b, const char* holder, bool fight_played )
{
  row_t r;
  r.holder  = holder;
  r.kind    = "buff";
  r.name    = b->name_str;
  // The id the JSON report prints (report_json.cpp), so the two sides agree.
  r.spell_id          = b->data_reporting().id();
  r.max_stack         = std::max( b->max_stack(), 1 );
  r.asynchronous      = b->stack_behavior == buff_stack_behavior::ASYNCHRONOUS;
  r.base_duration     = b->buff_duration().total_seconds();
  r.cooldown          = b->cooldown ? b->cooldown->duration.total_seconds() : 0.0;
  r.internal_cooldown = b->internal_cooldown ? b->internal_cooldown->duration.total_seconds() : 0.0;
  r.has_rppm          = b->rppm != nullptr;
  r.rppm_frequency    = b->rppm != nullptr ? b->rppm->get_frequency() : 0.0;
  r.default_chance    = b->default_chance;
  r.constant          = b->constant;
  // Quiet buffs are left out of the JSON report entirely (report_helper.cpp buff_is_dynamic), so the report cannot witness them. The
  // engine's own last-trigger time can: buff_t::reset() sets it to the minimum and only buff_t::execute() writes the current time.
  // Before the fight is played (the init-finished write) the field still holds its constructor value, so it says nothing yet.
  // An enemy's buffs are made lazily, after the iteration reset, so they still hold the constructor value zero until their first
  // execute: for them only a time above zero counts. The actor's own buffs are reset to the minimum, so time zero is a real trigger.
  const bool lazily_made = r.holder == "enemy";
  r.triggered = fight_played && ( lazily_made ? b->last_trigger_time() > timespan_t::zero() : b->last_trigger_time() >= timespan_t::zero() );
  return r;
}

row_t dot_row( const dot_t* d )
{
  row_t r;
  r.holder    = "enemy";
  r.kind      = "dot";
  r.name      = d->name_str;
  r.max_stack = std::max( d->max_stack, 1 );
  // current_action is set only when the dot was applied at least once (action_t::trigger_dot) and is never cleared.
  r.applied = d->current_action != nullptr;
  if ( r.applied )
  {
    r.spell_id      = d->current_action->data().id();
    r.base_duration = d->current_action->dot_duration.value().total_seconds();
    r.channeled     = d->current_action->channeled;
  }
  return r;
}

std::string row_json( const row_t& r )
{
  std::string o = fmt::format( "{{\"holder\":\"{}\",\"kind\":\"{}\",\"name\":{},\"spellId\":{},\"maxStack\":{},"
                               "\"stackBehavior\":\"{}\",\"baseDurationSeconds\":{},\"cooldownSeconds\":{},"
                               "\"internalCooldownSeconds\":{},\"hasRppm\":{},\"rppmFrequency\":{},\"defaultChance\":{},"
                               "\"constant\":{}",
                               r.holder, r.kind, esc( r.name ), r.spell_id, r.max_stack,
                               r.asynchronous ? "asynchronous" : "default", num( r.base_duration ), num( r.cooldown ),
                               num( r.internal_cooldown ), r.has_rppm ? "true" : "false", num( r.rppm_frequency ),
                               num( r.default_chance ), r.constant ? "true" : "false" );
  if ( r.kind == "dot" )
    o += fmt::format( ",\"applied\":{},\"channeled\":{}", r.applied ? "true" : "false", r.channeled ? "true" : "false" );
  else
    o += fmt::format( ",\"triggered\":{}", r.triggered ? "true" : "false" );
  return o + "}";
}

// 290-C4: the other engine objects that mark a buff as chance-applied, written beside the buff rows (additive top-level keys; schemaVersion stays 1,
// the buff reader ignores keys it does not name). They are what a buff row cannot say about itself: an item or enchant callback that applies the
// buff (`callbacks`), the player's rolled-chance objects (`procObjects`) and its cooldowns with the owner of each (`cooldowns`). Names are the engine's
// own; nothing here is selected or named by this module -- scripts/rl/proc_buff_selector.py reads them.
//   callbacks[]    one per dbc_proc_callback_t of the actor: driverSpellId, driverName, hasRppm, rppmValue, ppm, procChance, cooldownSeconds,
//                  procBuff / customBuff (a name or ""), buffs (names in the special effect's buff list)
//   procObjects[]  one per entry of the actor's proc_rng_list: name, type (rng_type_e as an integer)
//   cooldowns[]    one per entry of the actor's cooldown_list: name, durationSeconds, heldByAction (some action of the actor holds it),
//                  heldByBuff (some buff of the actor holds it as its cooldown)
std::string buff_name_of( const buff_t* b )
{
  return b ? b->name_str : std::string();
}

std::string proc_objects_json( const player_t* actor )
{
  std::string o = ",\"callbacks\":[";
  bool        first = true;
  for ( action_callback_t* base : actor->callbacks.all_callbacks )
  {
    const auto* cb = dynamic_cast<const dbc_proc_callback_t*>( base );
    if ( !cb )
      continue;
    const special_effect_t& e      = cb->effect;
    const spell_data_t*     driver = e.driver();
    std::string             buffs  = "[";
    bool                    first_buff = true;
    for ( const buff_t* b : e.buff_list )
    {
      buffs += ( first_buff ? "" : "," ) + esc( buff_name_of( b ) );
      first_buff = false;
    }
    buffs += "]";
    o += ( first ? "" : "," ) +
         fmt::format( "{{\"driverSpellId\":{},\"driverName\":{},\"hasRppm\":{},\"rppmValue\":{},\"ppm\":{},\"procChance\":{},"
                      "\"cooldownSeconds\":{},\"procBuff\":{},\"customBuff\":{},\"buffs\":{}}}",
                      e.spell_id, esc( driver && driver->ok() && driver->name_cstr() ? driver->name_cstr() : "" ),
                      cb->rppm != nullptr ? "true" : "false", num( e.rppm() ), num( cb->ppm ), num( cb->proc_chance ),
                      num( e.cooldown().total_seconds() ), esc( buff_name_of( cb->proc_buff ) ), esc( buff_name_of( e.custom_buff ) ), buffs );
    first = false;
  }
  o += "],\"procObjects\":[";
  first = true;
  for ( const proc_rng_t* r : actor->proc_rng_list )
  {
    if ( !r )
      continue;
    o += ( first ? "" : "," ) + fmt::format( "{{\"name\":{},\"type\":{}}}", esc( std::string( r->name() ) ), static_cast<int>( r->type() ) );
    first = false;
  }
  std::set<const cooldown_t*> held_by_action;
  for ( const action_t* a : actor->action_list )
    if ( a && a->cooldown )
      held_by_action.insert( a->cooldown );
  std::set<const cooldown_t*> held_by_buff;
  for ( const buff_t* b : actor->buff_list )
    if ( b && b->cooldown )
      held_by_buff.insert( b->cooldown );
  o += "],\"cooldowns\":[";
  first = true;
  for ( const cooldown_t* c : actor->cooldown_list )
  {
    if ( !c )
      continue;
    o += ( first ? "" : "," ) +
         fmt::format( "{{\"name\":{},\"durationSeconds\":{},\"heldByAction\":{},\"heldByBuff\":{}}}", esc( c->name_str ),
                      num( c->duration.total_seconds() ), held_by_action.count( c ) ? "true" : "false", held_by_buff.count( c ) ? "true" : "false" );
    first = false;
  }
  return o + "]";
}

void write_table( sim_t* sim, bool fight_played )
{
  player_t* actor = census_actor( sim );
  if ( !actor )
    return;

  std::map<key_t, row_t> rows;

  // The actor's own buffs. Buffs for talents not taken still exist (make_buff is unconditional), so this is the full engine set.
  for ( const buff_t* b : actor->buff_list )
    if ( b )
      add_row( rows, buff_row( b, "self", fight_played ) );

  // Buffs and dots an enemy holds whose source is the actor. The target data objects are made lazily, so none exist at
  // init-finished; at combat end every enemy has demised and left the non-sleeping list, hence the actor list.
  for ( const player_t* e : sim->actor_list )
  {
    if ( !e || !e->is_enemy() )
      continue;
    for ( const buff_t* b : e->buff_list )
      if ( b && b->source == actor )
        add_row( rows, buff_row( b, "enemy", fight_played ) );
    for ( const dot_t* d : e->dot_list )
      if ( d && d->source == actor )
        add_row( rows, dot_row( d ) );
  }

  std::string body = fmt::format( "{{\"schemaVersion\":1,\"actor\":{},\"buffs\":[", esc( actor->name_str ) );
  bool        first = true;
  for ( const auto& kv : rows )
  {
    body += ( first ? "" : "," ) + row_json( kv.second );
    first = false;
  }
  body += "]";
  body += proc_objects_json( actor );
  body += "}\n";

  std::FILE* f = std::fopen( g_path.c_str(), "wb" );
  if ( !f )
  {
    fmt::print( stderr, "rl_buff_census: cannot open '{}' for writing\n", g_path );
    return;
  }
  std::fwrite( body.data(), 1, body.size(), f );
  std::fclose( f );
}

}  // namespace

std::string& path_storage()
{
  return g_path;
}

void on_init_finished( sim_t* sim )
{
  if ( g_path.empty() )
    return;
  write_table( sim, false );
}

void on_combat_end( sim_t* sim )
{
  if ( g_path.empty() )
    return;
  write_table( sim, true );
}

}  // namespace rl_buff_census
