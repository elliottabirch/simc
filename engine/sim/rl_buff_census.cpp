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
#include "player/spawner_base.hpp"
#include "sim/cooldown.hpp"
#include "sim/proc_rng.hpp"
#include "sim/rl_census_lines.hpp"
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
  long long   start_count         = 0;      // 261028-fzl: buff_t::start_count summed over the objects of this key (reach only; 0 for a dot)
  std::string refresh_behavior    = "none"; // 261028-fzl C-3: buff_refresh_behavior enum name, lower case; a dot row says "none"
  int         object_count        = 0;      // 261028-fzl C-3 / D7: objects with this (holder, kind, name) on ONE owner (max over the owners); > 1 is a duplicate name
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
  old.start_count += r.start_count;
}

const char* refresh_behavior_name( buff_refresh_behavior b )
{
  switch ( b )
  {
    case buff_refresh_behavior::NONE: return "none";
    case buff_refresh_behavior::DISABLED: return "disabled";
    case buff_refresh_behavior::DURATION: return "duration";
    case buff_refresh_behavior::EXTEND: return "extend";
    case buff_refresh_behavior::PANDEMIC: return "pandemic";
    case buff_refresh_behavior::TICK: return "tick";
    case buff_refresh_behavior::MAX: return "max";
    case buff_refresh_behavior::CUSTOM: return "custom";
  }
  return "none";
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
  r.refresh_behavior  = refresh_behavior_name( b->refresh_behavior );
  // Quiet buffs are left out of the JSON report entirely (report_helper.cpp buff_is_dynamic), so the report cannot witness them. The
  // engine's own last-trigger time can: buff_t::reset() sets it to the minimum and only buff_t::execute() writes the current time.
  // Before the fight is played (the init-finished write) the field still holds its constructor value, so it says nothing yet.
  // An enemy's buffs are made lazily, after the iteration reset, so they still hold the constructor value zero until their first
  // execute: for them only a time above zero counts. The actor's own buffs are reset to the minimum, so time zero is a real trigger.
  const bool lazily_made = r.holder != "self";
  r.triggered = fight_played && ( lazily_made ? b->last_trigger_time() > timespan_t::zero() : b->last_trigger_time() >= timespan_t::zero() );
  r.start_count = fight_played ? static_cast<long long>( b->start_count_for_census() ) : 0;
  return r;
}

row_t dot_row( const dot_t* d, const char* holder )
{
  row_t r;
  r.holder    = holder;
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

std::string row_json( const row_t& r, bool with_reach_fields = true )
{
  std::string o = fmt::format( "{{\"holder\":\"{}\",\"kind\":\"{}\",\"name\":{},\"spellId\":{},\"maxStack\":{},"
                               "\"stackBehavior\":\"{}\",\"baseDurationSeconds\":{},\"cooldownSeconds\":{},"
                               "\"internalCooldownSeconds\":{},\"hasRppm\":{},\"rppmFrequency\":{},\"defaultChance\":{},"
                               "\"constant\":{},\"startCount\":{},\"refreshBehavior\":\"{}\",\"objectCount\":{}",
                               r.holder, r.kind, esc( r.name ), r.spell_id, r.max_stack,
                               r.asynchronous ? "asynchronous" : "default", num( r.base_duration ), num( r.cooldown ),
                               num( r.internal_cooldown ), r.has_rppm ? "true" : "false", num( r.rppm_frequency ),
                               num( r.default_chance ), r.constant ? "true" : "false", with_reach_fields ? r.start_count : 0LL, r.refresh_behavior,
                               r.object_count );
  // 261028-fzl: in a fight list the reach flags (applied / triggered) are not table fields: they are per-entry reach (C-1), so the table
  // object stays equal across entries of one build and its signature dedupes. The ordinary single-process file keeps them in the rows.
  if ( r.kind == "dot" )
  {
    if ( with_reach_fields )
      o += fmt::format( ",\"applied\":{},\"channeled\":{}", r.applied ? "true" : "false", r.channeled ? "true" : "false" );
    else
      o += fmt::format( ",\"channeled\":{}", r.channeled ? "true" : "false" );
  }
  else if ( with_reach_fields )
    o += fmt::format( ",\"triggered\":{}", r.triggered ? "true" : "false" );
  return o + "}";
}

// 290-C4: the other engine objects that mark a buff as chance-applied, written beside the buff rows (additive top-level keys; schemaVersion stays 1,
// the buff reader ignores keys it does not name). They are what a buff row cannot say about itself: an item or enchant callback that applies the
// buff (`callbacks`), the player's rolled-chance objects (`procObjects`) and its cooldowns with the owner of each (`cooldowns`). Names are the engine's
// own; nothing here is selected or named by this module -- scripts/rl/proc_buff_selector.py reads them.
//   callbacks[]    one per dbc_proc_callback_t of the actor: driverSpellId, driverName, hasRppm, rppmValue, ppm, procChance, cooldownSeconds,
//                  procBuff / customBuff (a name or ""), buffs (names in the special effect's buff list)
//   procObjects[]  one per entry of the actor's proc_rng_list: name, type (rng_type_e as an integer); 261028-fzl (S5-F2, integer-counters walk,
//                  owner rulings 7-8): a shuffled deck (type RNG_SHUFFLE) adds totalEntries (cards in the deck, shuffled_rng_t::entry_total) and
//                  successEntries (its success cards, count_total( SUCCESS )), the engine's own deck sizes the deck counters' scales divide by
//   cooldowns[]    one per entry of the actor's cooldown_list: name, durationSeconds, heldByAction (some action of the actor holds it),
//                  heldByBuff (some buff of the actor holds it as its cooldown)
//   spawners[]     261028-fzl (S5-F2, ruling 7): one per entry of the actor's spawners: name, maxPets (the engine cap on active pets of the
//                  spawner; 0 is "unlimited", a dynamic spawner that sets no cap)
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
    std::string deck;
    if ( r->type() == RNG_SHUFFLE )
      if ( const auto* shuffled = dynamic_cast<const shuffled_rng_t*>( r ) )
        deck = fmt::format( ",\"totalEntries\":{},\"successEntries\":{}", shuffled->entry_total(), shuffled->count_total( SUCCESS ) );
    o += ( first ? "" : "," ) +
         fmt::format( "{{\"name\":{},\"type\":{}{}}}", esc( std::string( r->name() ) ), static_cast<int>( r->type() ), deck );
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
         fmt::format( "{{\"name\":{},\"durationSeconds\":{},\"charges\":{},\"heldByAction\":{},\"heldByBuff\":{}}}", esc( c->name_str ),
                      num( c->duration.total_seconds() ), c->charges, held_by_action.count( c ) ? "true" : "false",
                      held_by_buff.count( c ) ? "true" : "false" );
    first = false;
  }
  o += "],\"spawners\":[";
  first = true;
  for ( const spawner::base_actor_spawner_t* sp : actor->spawners )
  {
    if ( !sp )
      continue;
    o += ( first ? "" : "," ) + fmt::format( "{{\"name\":{},\"maxPets\":{}}}", esc( sp->name() ), sp->max_pets() );
    first = false;
  }
  return o + "]";
}

// 261028-fzl (C-3): after one owner's objects are added, fold its per-key object counts into the rows: object_count is the MOST objects one owner holds
// of a (holder, kind, name), so two enemies each holding the one Flame Shock dot is 1, and a duplicate on the actor (D7) is 2.
void fold_object_counts( std::map<key_t, row_t>& rows, const std::map<key_t, int>& owner_counts )
{
  for ( const auto& kv : owner_counts )
  {
    auto it = rows.find( kv.first );
    if ( it != rows.end() )
      it->second.object_count = std::max( it->second.object_count, kv.second );
  }
}

void add_counted( std::map<key_t, row_t>& rows, std::map<key_t, int>& owner_counts, const row_t& r )
{
  add_row( rows, r );
  ++owner_counts[ key_t( r.holder, r.kind, r.name ) ];
}

void write_table( sim_t* sim, bool fight_played )
{
  player_t* actor = census_actor( sim );
  if ( !actor )
    return;

  std::map<key_t, row_t> rows;

  // The actor's own buffs. Buffs for talents not taken still exist (make_buff is unconditional), so this is the full engine set.
  {
    std::map<key_t, int> owner_counts;
    for ( const buff_t* b : actor->buff_list )
      if ( b )
        add_counted( rows, owner_counts, buff_row( b, "self", fight_played ) );
    fold_object_counts( rows, owner_counts );
  }

  // Buffs and dots an enemy holds. The target data objects are made lazily, so none exist at init-finished; at combat end every enemy has
  // demised and left the non-sleeping list, hence the actor list. 261028-fzl (C-3): an object whose source is the actor is holder "enemy"
  // (as before); every other enemy buff or dot (the enemy's own auras, an aura a pet put on it) is holder "enemy_foreign".
  for ( const player_t* e : sim->actor_list )
  {
    if ( !e || !e->is_enemy() )
      continue;
    std::map<key_t, int> owner_counts;
    for ( const buff_t* b : e->buff_list )
      if ( b )
        add_counted( rows, owner_counts, buff_row( b, b->source == actor ? "enemy" : "enemy_foreign", fight_played ) );
    for ( const dot_t* d : e->dot_list )
      if ( d )
        add_counted( rows, owner_counts, dot_row( d, d->source == actor ? "enemy" : "enemy_foreign" ) );
    fold_object_counts( rows, owner_counts );
  }

  // 261028-fzl (C-1): in a fight list the table object omits the reach flags and each entry appends one line at combat end; the table is written
  // once per distinct signature (sha256 of its bytes) in this process. The init-finished write of a list is only the truncation at entry 0.
  //
  // The signature and the table cover the INIT rows only (owner note after S1, receipt S1-F1a finding d): the actor's own buffs and the actor-side
  // arrays (callbacks, procObjects, cooldowns), which exist at init-finished. Enemy rows (holder enemy / enemy_foreign) are made lazily during
  // play, so a table that held them would split one build into several signatures. They travel beside it in the line's additive `enemyRows` array:
  // each enemy row's bytes are written the first time they are seen in this process and not again (a changed ceiling is new bytes, so it is written
  // too; the reader merges rows of one key with the same rule as across fights). The per-entry flags stay in `reach`.
  if ( sim->rl_list_active )
  {
    if ( !fight_played )
    {
      if ( sim->rl_list_index == 0 )
      {
        rl_census_lines::reset_signatures( 1 );
        rl_census_lines::reset_signatures( 2 );
        if ( !rl_census_lines::write_text( g_path, std::string(), "wb" ) )
          fmt::print( stderr, "rl_buff_census: cannot open '{}' for writing\n", g_path );
      }
      return;
    }
    std::string table = fmt::format( "{{\"schemaVersion\":2,\"actor\":{},\"buffs\":[", esc( actor->name_str ) );
    std::string enemy_rows = "[";
    std::string reach = "{";
    bool        first_row = true;
    bool        first_enemy = true;
    bool        first_reach = true;
    for ( const auto& kv : rows )
    {
      const row_t& r = kv.second;
      const std::string bytes = row_json( r, false );
      if ( r.holder == "self" )
      {
        table += ( first_row ? "" : "," ) + bytes;
        first_row = false;
      }
      else if ( rl_census_lines::first_time_signature( 2, bytes ) )
      {
        enemy_rows += ( first_enemy ? "" : "," ) + bytes;
        first_enemy = false;
      }
      const bool any = r.start_count > 0 || r.triggered || r.applied;
      if ( any )
      {
        reach += ( first_reach ? "" : "," ) +
                 fmt::format( "{}:{{\"startCount\":{},\"triggered\":{},\"applied\":{}}}",
                              esc( r.holder + "/" + r.kind + "/" + r.name ), r.start_count, r.triggered ? "true" : "false", r.applied ? "true" : "false" );
        first_reach = false;
      }
    }
    table += "]";
    table += proc_objects_json( actor );
    table += "}";
    enemy_rows += "]";
    reach += "}";
    const std::string signature = rl_census_lines::sha256_hex( table );
    const bool        first     = rl_census_lines::first_time_signature( 1, signature );
    const std::string line =
        fmt::format( "{{\"schemaVersion\":2,\"kind\":\"buff\",\"entry\":{},\"crc32\":{},\"signature\":\"{}\",\"table\":{},\"enemyRows\":{},\"reach\":{}}}\n",
                     sim->rl_list_index, sim->rl_list_crc32, signature, first ? table : std::string( "null" ), enemy_rows, reach );
    if ( !rl_census_lines::write_text( g_path, line, "ab" ) )
      fmt::print( stderr, "rl_buff_census: cannot open '{}' for appending\n", g_path );
    return;
  }

  std::string body = fmt::format( "{{\"schemaVersion\":2,\"actor\":{},\"buffs\":[", esc( actor->name_str ) );
  bool        first = true;
  for ( const auto& kv : rows )
  {
    body += ( first ? "" : "," ) + row_json( kv.second );
    first = false;
  }
  body += "]";
  body += proc_objects_json( actor );
  body += "}\n";

  if ( !rl_census_lines::write_text( g_path, body, "wb" ) )
    fmt::print( stderr, "rl_buff_census: cannot open '{}' for writing\n", g_path );
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
