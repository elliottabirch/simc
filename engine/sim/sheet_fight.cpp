// ==========================================================================
// tstl-sylvanas 261-02: the sheet-fight controller. See sheet_fight.hpp.
// ==========================================================================

#include "sim/sheet_fight.hpp"

#include "action/action.hpp"
#include "action/action_state.hpp"
#include "action/spell.hpp"
#include "buff/buff.hpp"
#include "player/assessor.hpp"
#include "player/pet.hpp"
#include "player/pet_spawner.hpp"
#include "player/player.hpp"
#include "sim/event.hpp"
#include "sim/expressions.hpp"
#include "sim/rl_rng_record.hpp"
#include "sim/sim.hpp"
#include "util/git_info.hpp"
#include "util/io.hpp"
#include "util/util.hpp"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

using namespace rapidjson;

// ==========================================================================
// Spec loader
// ==========================================================================

namespace sheet_fight_spec
{
namespace
{
struct loader_t
{
  const std::string& path;
  struct jit_use_t
  {
    const jitter_t* j = nullptr;
    std::set<std::string> uses;
  };
  std::map<std::string, jit_use_t> jit;
  std::set<std::string> boss_actors, must_die_bosses, wave_actors;
  std::string master;

  explicit loader_t( const std::string& p ) : path( p ) {}

  [[noreturn]] void fail( const std::string& jp, const std::string& msg ) const
  {
    throw sc_invalid_sim_argument( fmt::format( "solver_sheet_fight='{}': {}: {}", path, jp.empty() ? "<root>" : jp, msg ) );
  }
  static std::string at( const std::string& p, size_t i ) { return fmt::format( "{}[{}]", p, i ); }
  static std::string dot( const std::string& p, const char* k ) { return p.empty() ? std::string( k ) : p + "." + k; }

  void keys( const Value& o, std::initializer_list<const char*> ks, const std::string& p ) const
  {
    if ( !o.IsObject() )
      fail( p, "must be a JSON object" );
    for ( const char* k : ks )
      if ( !o.HasMember( k ) )
        fail( dot( p, k ), fmt::format( "missing key {}", k ) );
    for ( auto it = o.MemberBegin(); it != o.MemberEnd(); ++it )
    {
      bool known = false;
      for ( const char* k : ks )
        known = known || std::string( k ) == it->name.GetString();
      if ( !known )
        fail( dot( p, it->name.GetString() ), "unknown key (not part of the contract)" );
    }
  }
  const Value& get( const Value& o, const char* k ) const { return o[ k ]; }

  double num( const Value& v, const std::string& p, const char* what, bool integer = false, double min = -1e300,
              double min_excl = -1e300, double max_excl = 1e300, double max = 1e300 ) const
  {
    if ( !v.IsNumber() )
      fail( p, fmt::format( "{} must be {}", what, integer ? "an integer" : "a number" ) );
    double d = v.GetDouble();
    if ( !std::isfinite( d ) || ( integer && d != std::floor( d ) ) )
      fail( p, fmt::format( "{} must be {}", what, integer ? "an integer" : "a finite number" ) );
    if ( d < min )
      fail( p, fmt::format( "{} must be at least {}", what, min ) );
    if ( d <= min_excl )
      fail( p, fmt::format( "{} must be a number greater than {}", what, min_excl ) );
    if ( d >= max_excl )
      fail( p, fmt::format( "{} must be less than {}", what, max_excl ) );
    if ( d > max )
      fail( p, fmt::format( "{} must be at most {}", what, max ) );
    return d;
  }
  std::string str( const Value& v, const std::string& p, const char* what ) const
  {
    if ( !v.IsString() || v.GetStringLength() == 0 )
      fail( p, fmt::format( "{} must be a non-empty string", what ) );
    return v.GetString();
  }
  bool boolean( const Value& v, const std::string& p, const char* what ) const
  {
    if ( !v.IsBool() )
      fail( p, fmt::format( "{} must be a boolean", what ) );
    return v.GetBool();
  }
  static bool actor_ok( const std::string& s )
  {
    return !s.empty() && std::all_of( s.begin(), s.end(), []( unsigned char c ) { return std::isalnum( c ) || c == '_'; } );
  }
  std::string actor( const Value& v, const std::string& p ) const
  {
    if ( !v.IsString() || !actor_ok( v.GetString() ) )
      fail( p, "actor must match ^[A-Za-z0-9_]+$" );
    return v.GetString();
  }
  std::string boss_ref( const Value& v, const std::string& p ) const
  {
    if ( !v.IsString() || !boss_actors.count( v.GetString() ) )
      fail( p, "unknown boss " + ( v.IsString() ? std::string( v.GetString() ) : std::string( "(not a string)" ) ) );
    return v.GetString();
  }
  // A *_jitter field: null or a key of the jitter table.
  std::string jref( const Value& v, const std::string& p, const char* kind, bool min_positive = false )
  {
    if ( v.IsNull() )
      return {};
    if ( !v.IsString() || !jit.count( v.GetString() ) )
      fail( p, "names a jitter key that is not in jitter" );
    auto& e = jit[ v.GetString() ];
    e.uses.insert( kind );
    if ( min_positive && !( e.j->min > 0 ) )
      fail( p, fmt::format( "{} jitter key {} must have min greater than 0", kind, v.GetString() ) );
    return v.GetString();
  }
  void centre( const std::string& key, double value, const std::string& p ) const
  {
    if ( key.empty() )
      return;
    const jitter_t* j = jit.at( key ).j;
    if ( std::fabs( value - ( j->min + j->max ) / 2.0 ) > 1e-9 )
      fail( p, fmt::format( "must equal the centre of jitter key {} ({})", key, ( j->min + j->max ) / 2.0 ) );
  }
  const Value& list( const Value& o, const char* k, const std::string& p ) const
  {
    const Value& v = o[ k ];
    if ( !v.IsArray() )
      fail( dot( p, k ), fmt::format( "{} must be a list", k ) );
    return v;
  }
};
}  // namespace

spec_t load_spec( const std::string& path )
{
  io::ifstream in;
  in.open( path );
  if ( !in.is_open() )
    throw sc_invalid_sim_argument( fmt::format( "solver_sheet_fight='{}': cannot open the spec file.", path ) );
  std::stringstream ss;
  ss << in.rdbuf();
  const std::string text = ss.str();
  Document doc;
  doc.Parse( text.c_str() );
  loader_t L( path );
  if ( doc.HasParseError() )
    L.fail( "<root>", fmt::format( "not valid JSON (parse error at offset {})", doc.GetErrorOffset() ) );

  spec_t spec;
  spec.path = path;
  L.keys( doc, { "format", "slug", "sheet_fingerprint", "encounter_id", "fragment_path", "master_actor", "max_time_s",
                 "sample_interval_s", "bosses", "phases", "waves", "jitter", "timers_pending_second_kill", "defaults_used" }, "" );
  if ( !doc[ "format" ].IsString() || std::string( doc[ "format" ].GetString() ) != "sheet-fight-spec/1" )
    L.fail( "format", "format must be \"sheet-fight-spec/1\"" );
  spec.slug = L.str( doc[ "slug" ], "slug", "slug" );
  {
    const Value& f = doc[ "sheet_fingerprint" ];
    bool ok = f.IsString() && f.GetStringLength() == 64;
    if ( ok )
      for ( const char* c = f.GetString(); *c; ++c )
        ok = ok && ( ( *c >= '0' && *c <= '9' ) || ( *c >= 'a' && *c <= 'f' ) );
    if ( !ok )
      L.fail( "sheet_fingerprint", "sheet_fingerprint must be 64 lowercase hex characters" );
    spec.sheet_fingerprint = f.GetString();
  }
  spec.encounter_id = static_cast<long long>( L.num( doc[ "encounter_id" ], "encounter_id", "encounter_id", true ) );
  spec.fragment_path = L.str( doc[ "fragment_path" ], "fragment_path", "fragment_path" );
  if ( spec.fragment_path.size() < 11 || spec.fragment_path.compare( spec.fragment_path.size() - 11, 11, ".fight.simc" ) != 0 ||
       spec.fragment_path[ 0 ] == '/' )
    L.fail( "fragment_path", "fragment_path must be a repo-relative path ending in .fight.simc" );
  spec.master_actor = L.actor( doc[ "master_actor" ], "master_actor" );
  L.master = spec.master_actor;
  spec.max_time_s = L.num( doc[ "max_time_s" ], "max_time_s", "max_time_s", false, -1e300, 0 );
  spec.sample_interval_s = L.num( doc[ "sample_interval_s" ], "sample_interval_s", "sample_interval_s", false, -1e300, 0 );

  // jitter table first (other fields reference it)
  const Value& jl = L.list( doc, "jitter", "" );
  spec.jitter.reserve( jl.Size() );
  for ( SizeType i = 0; i < jl.Size(); ++i )
  {
    const std::string p = loader_t::at( "jitter", i );
    L.keys( jl[ i ], { "key", "min", "max", "per" }, p );
    jitter_t j;
    const Value& k = jl[ i ][ "key" ];
    bool kok = k.IsString() && k.GetStringLength() > 0;
    if ( kok )
      for ( const char* c = k.GetString(); *c; ++c )
        kok = kok && ( ( *c >= 'a' && *c <= 'z' ) || ( *c >= '0' && *c <= '9' ) || *c == '_' );
    if ( !kok )
      L.fail( p + ".key", "key must match ^[a-z0-9_]+$" );
    j.key = k.GetString();
    if ( L.jit.count( j.key ) )
      L.fail( p + ".key", "duplicate jitter key " + j.key );
    j.min = L.num( jl[ i ][ "min" ], p + ".min", "min" );
    j.max = L.num( jl[ i ][ "max" ], p + ".max", "max" );
    if ( j.max < j.min )
      L.fail( p + ".max", "max must be at least min" );
    const Value& per = jl[ i ][ "per" ];
    if ( !per.IsString() || ( std::string( per.GetString() ) != "fight" && std::string( per.GetString() ) != "phase" ) )
      L.fail( p + ".per", "per must be \"fight\" or \"phase\"" );
    j.per = per.GetString();
    spec.jitter.push_back( j );
  }
  for ( const auto& j : spec.jitter )
    L.jit[ j.key ].j = &j;

  // bosses
  const Value& bl = L.list( doc, "bosses", "" );
  if ( bl.Size() == 0 )
    L.fail( "bosses", "bosses must be a non-empty list" );
  for ( SizeType i = 0; i < bl.Size(); ++i )
  {
    const std::string p = loader_t::at( "bosses", i );
    L.keys( bl[ i ], { "actor", "name", "npc_game_id", "max_health", "must_die" }, p );
    boss_t b;
    b.actor = L.actor( bl[ i ][ "actor" ], p + ".actor" );
    if ( b.actor == spec.master_actor )
      L.fail( p + ".actor", "actor must not be the master actor" );
    if ( !L.boss_actors.insert( b.actor ).second )
      L.fail( p + ".actor", "duplicate boss actor " + b.actor );
    b.name = L.str( bl[ i ][ "name" ], p + ".name", "name" );
    b.npc_game_id = static_cast<long long>( L.num( bl[ i ][ "npc_game_id" ], p + ".npc_game_id", "npc_game_id", true ) );
    b.max_health = L.num( bl[ i ][ "max_health" ], p + ".max_health", "max_health", false, -1e300, 0 );
    b.must_die = L.boolean( bl[ i ][ "must_die" ], p + ".must_die", "must_die" );
    if ( b.must_die )
      L.must_die_bosses.insert( b.actor );
    spec.bosses.push_back( b );
  }

  // phases
  const Value& pl = L.list( doc, "phases", "" );
  if ( pl.Size() == 0 )
    L.fail( "phases", "phases must be a non-empty list" );
  std::set<std::string> phase_names, earlier_casts;
  std::vector<std::set<std::string>> engaged_by_phase;
  int bloodlust_phases = 0;
  for ( SizeType i = 0; i < pl.Size(); ++i )
  {
    const std::string p = loader_t::at( "phases", i );
    const Value& ph = pl[ i ];
    L.keys( ph, { "name", "intermission", "start", "engage", "max_health", "raid_stream", "damage_taken", "heal", "casts", "bloodlust" }, p );
    phase_t out;
    out.name = L.str( ph[ "name" ], p + ".name", "name" );
    if ( !phase_names.insert( out.name ).second )
      L.fail( p + ".name", "duplicate phase name " + out.name );
    out.intermission = L.boolean( ph[ "intermission" ], p + ".intermission", "intermission" );
    // start
    {
      const Value& s = ph[ "start" ];
      const std::string sp = p + ".start";
      if ( !s.IsObject() )
        L.fail( sp, "start must be a JSON object" );
      std::string kind = s.HasMember( "kind" ) && s[ "kind" ].IsString() ? s[ "kind" ].GetString() : "";
      out.start.kind = kind;
      if ( i == 0 )
      {
        if ( kind != "pull" )
          L.fail( sp + ".kind", "phase 0 must start with kind \"pull\"" );
        L.keys( s, { "kind" }, sp );
      }
      else if ( kind == "boss_health" )
      {
        L.keys( s, { "kind", "boss", "pct" }, sp );
        out.start.boss = L.boss_ref( s[ "boss" ], sp + ".boss" );
        out.start.pct = L.num( s[ "pct" ], sp + ".pct", "pct", false, 0, -1e300, 100 );
      }
      else if ( kind == "after_cast" )
      {
        L.keys( s, { "kind", "cast", "after_s", "after_s_jitter" }, sp );
        if ( !s[ "cast" ].IsString() || !earlier_casts.count( s[ "cast" ].GetString() ) )
          L.fail( sp + ".cast", "cast is not declared in an earlier phase" );
        out.start.cast = s[ "cast" ].GetString();
        out.start.after_s = L.num( s[ "after_s" ], sp + ".after_s", "after_s", false, 0 );
        out.start.after_jitter = L.jref( s[ "after_s_jitter" ], sp + ".after_s_jitter", "after_s_jitter" );
        L.centre( out.start.after_jitter, out.start.after_s, sp + ".after_s" );
      }
      else
        L.fail( sp + ".kind", kind == "pull" ? "kind \"pull\" is for phase 0 only" : "kind must be pull, boss_health or after_cast" );
    }
    // engage
    std::set<std::string> engaged;
    std::map<std::string, std::string> engaged_zero;
    const Value& el = L.list( ph, "engage", p );
    for ( SizeType k = 0; k < el.Size(); ++k )
    {
      const std::string ep = loader_t::at( p + ".engage", k );
      L.keys( el[ k ], { "boss", "delay_s", "delay_jitter", "on_zero" }, ep );
      engage_t e;
      e.boss = L.boss_ref( el[ k ][ "boss" ], ep + ".boss" );
      e.delay_s = L.num( el[ k ][ "delay_s" ], ep + ".delay_s", "delay_s", false, 0 );
      e.delay_jitter = L.jref( el[ k ][ "delay_jitter" ], ep + ".delay_jitter", "delay_jitter" );
      L.centre( e.delay_jitter, e.delay_s, ep + ".delay_s" );
      const Value& oz = el[ k ][ "on_zero" ];
      if ( !oz.IsString() || ( std::string( oz.GetString() ) != "bench" && std::string( oz.GetString() ) != "die" ) )
        L.fail( ep + ".on_zero", "on_zero must be bench or die" );
      e.on_zero = oz.GetString();
      engaged.insert( e.boss );
      engaged_zero[ e.boss ] = e.on_zero;
      out.engage.push_back( e );
    }
    engaged_by_phase.push_back( engaged );
    if ( i + 1 == pl.Size() )
      for ( const auto& m : L.must_die_bosses )
        if ( engaged_zero[ m ] != "die" )
          L.fail( p + ".engage", "the last phase must engage every must_die boss with on_zero \"die\" (missing: " + m + ")" );
    // max_health
    const Value& mh = ph[ "max_health" ];
    if ( !mh.IsObject() )
      L.fail( p + ".max_health", "max_health must be an object (boss actor -> number)" );
    for ( auto it = mh.MemberBegin(); it != mh.MemberEnd(); ++it )
    {
      const std::string mp = p + ".max_health." + it->name.GetString();
      if ( !L.boss_actors.count( it->name.GetString() ) )
        L.fail( mp, std::string( "unknown boss " ) + it->name.GetString() );
      out.max_health.emplace_back( it->name.GetString(), L.num( it->value, mp, "max_health", false, -1e300, 0 ) );
    }
    // raid_stream
    const Value& sl = L.list( ph, "raid_stream", p );
    for ( SizeType k = 0; k < sl.Size(); ++k )
    {
      const std::string sp = loader_t::at( p + ".raid_stream", k );
      L.keys( sl[ k ], { "boss", "damage_per_s", "pace_jitter" }, sp );
      stream_t s;
      s.boss = L.boss_ref( sl[ k ][ "boss" ], sp + ".boss" );
      s.damage_per_s = L.num( sl[ k ][ "damage_per_s" ], sp + ".damage_per_s", "damage_per_s", false, 0 );
      s.pace_jitter = L.jref( sl[ k ][ "pace_jitter" ], sp + ".pace_jitter", "pace_jitter", true );
      out.raid_stream.push_back( s );
    }
    // damage_taken
    const Value& dl = L.list( ph, "damage_taken", p );
    for ( SizeType k = 0; k < dl.Size(); ++k )
    {
      const std::string dp = loader_t::at( p + ".damage_taken", k );
      L.keys( dl[ k ], { "boss", "multiplier" }, dp );
      damage_taken_t d;
      d.boss = L.boss_ref( dl[ k ][ "boss" ], dp + ".boss" );
      d.multiplier = L.num( dl[ k ][ "multiplier" ], dp + ".multiplier", "multiplier", false, -1e300, 0 );
      out.damage_taken.push_back( d );
    }
    // heal
    if ( !ph[ "heal" ].IsNull() )
    {
      const Value& h = ph[ "heal" ];
      const std::string hp = p + ".heal";
      L.keys( h, { "bosses", "pct_of_max_per_tick", "tick_interval_s", "ticks", "first_tick_after_start_s" }, hp );
      heal_t he;
      if ( !h[ "bosses" ].IsArray() )
        L.fail( hp + ".bosses", "bosses must be a list of boss actors" );
      for ( SizeType k = 0; k < h[ "bosses" ].Size(); ++k )
        he.bosses.push_back( L.boss_ref( h[ "bosses" ][ k ], loader_t::at( hp + ".bosses", k ) ) );
      he.pct_of_max_per_tick = L.num( h[ "pct_of_max_per_tick" ], hp + ".pct_of_max_per_tick", "pct_of_max_per_tick", false, -1e300, 0 );
      he.tick_interval_s = L.num( h[ "tick_interval_s" ], hp + ".tick_interval_s", "tick_interval_s", false, -1e300, 0 );
      he.ticks = static_cast<long long>( L.num( h[ "ticks" ], hp + ".ticks", "ticks", true, 1 ) );
      he.first_tick_after_start_s = L.num( h[ "first_tick_after_start_s" ], hp + ".first_tick_after_start_s", "first_tick_after_start_s", false, 0 );
      out.heal = he;
    }
    // casts
    const Value& cl = L.list( ph, "casts", p );
    std::set<std::string> declared_here;
    for ( SizeType k = 0; k < cl.Size(); ++k )
    {
      const std::string cp = loader_t::at( p + ".casts", k );
      L.keys( cl[ k ], { "name", "ability_id", "caster", "at_in_phase_s" }, cp );
      cast_t c;
      c.name = L.str( cl[ k ][ "name" ], cp + ".name", "name" );
      c.ability_id = static_cast<long long>( L.num( cl[ k ][ "ability_id" ], cp + ".ability_id", "ability_id", true ) );
      c.caster = L.boss_ref( cl[ k ][ "caster" ], cp + ".caster" );
      c.at_in_phase_s = L.num( cl[ k ][ "at_in_phase_s" ], cp + ".at_in_phase_s", "at_in_phase_s", false, 0 );
      declared_here.insert( c.name );
      out.casts.push_back( c );
    }
    // bloodlust
    if ( !ph[ "bloodlust" ].IsNull() )
    {
      const std::string bp = p + ".bloodlust";
      if ( ++bloodlust_phases > 1 )
        L.fail( bp, "at most one phase may have a bloodlust" );
      L.keys( ph[ "bloodlust" ], { "offset_after_start_s", "offset_jitter" }, bp );
      bloodlust_t b;
      b.offset_after_start_s = L.num( ph[ "bloodlust" ][ "offset_after_start_s" ], bp + ".offset_after_start_s", "offset_after_start_s", false, 0 );
      b.offset_jitter = L.jref( ph[ "bloodlust" ][ "offset_jitter" ], bp + ".offset_jitter", "offset_jitter" );
      L.centre( b.offset_jitter, b.offset_after_start_s, bp + ".offset_after_start_s" );
      out.bloodlust = b;
    }
    earlier_casts.insert( declared_here.begin(), declared_here.end() );
    spec.phases.push_back( out );
  }

  // waves
  const Value& wl = L.list( doc, "waves", "" );
  const int phase_count = static_cast<int>( spec.phases.size() );
  for ( SizeType i = 0; i < wl.Size(); ++i )
  {
    const std::string p = loader_t::at( "waves", i );
    const Value& w = wl[ i ];
    L.keys( w, { "name", "actor", "npc_game_id", "kind", "count", "at", "time_shift_jitter", "max_health", "must_die",
                 "spawn_distance_yd", "travel_s", "travel_jitter", "raid_stream", "lifetime_s",
                 "nominal_lifetime_s" }, p );  // tstl-sylvanas 261-07: the writer carries the add's nominal lifetime for Phase 262
    wave_t out;
    out.name = L.str( w[ "name" ], p + ".name", "name" );
    out.actor = L.actor( w[ "actor" ], p + ".actor" );
    if ( out.actor == spec.master_actor || L.boss_actors.count( out.actor ) )
      L.fail( p + ".actor", "actor must not be a boss or the master" );
    if ( !L.wave_actors.insert( out.actor ).second )
      L.fail( p + ".actor", "duplicate wave actor " + out.actor );
    out.npc_game_id = static_cast<long long>( L.num( w[ "npc_game_id" ], p + ".npc_game_id", "npc_game_id", true ) );
    out.count = static_cast<long long>( L.num( w[ "count" ], p + ".count", "count", true, 1 ) );
    const std::string kind = w[ "kind" ].IsString() ? w[ "kind" ].GetString() : "";
    if ( kind != "add" && kind != "hazard" )
      L.fail( p + ".kind", "kind must be add or hazard" );
    out.kind = kind;
    if ( !w[ "at" ].IsArray() || w[ "at" ].Size() == 0 )
      L.fail( p + ".at", "at must be a non-empty list" );
    for ( SizeType k = 0; k < w[ "at" ].Size(); ++k )
    {
      const Value& a = w[ "at" ][ k ];
      const std::string ap = loader_t::at( p + ".at", k );
      if ( !a.IsObject() )
        L.fail( ap, "must be a JSON object" );
      wave_at_t wa;
      if ( !a.HasMember( "phase" ) )
        L.fail( ap + ".phase", "missing key phase" );
      wa.phase = static_cast<int>( L.num( a[ "phase" ], ap + ".phase", "phase", true, 0, -1e300, 1e300, phase_count - 1 ) );
      const std::string ak = a.HasMember( "kind" ) && a[ "kind" ].IsString() ? a[ "kind" ].GetString() : "";
      wa.kind = ak;
      if ( ak == "phase_time" )
      {
        L.keys( a, { "kind", "phase", "in_phase_s" }, ap );
        wa.in_phase_s = L.num( a[ "in_phase_s" ], ap + ".in_phase_s", "in_phase_s", false, 0 );
      }
      else if ( ak == "boss_health" )
      {
        L.keys( a, { "kind", "phase", "boss", "pct" }, ap );
        wa.boss = L.boss_ref( a[ "boss" ], ap + ".boss" );
        if ( !engaged_by_phase[ wa.phase ].count( wa.boss ) )
          L.fail( ap + ".boss", fmt::format( "boss {} is not engaged in phase {}", wa.boss, wa.phase ) );
        wa.pct = L.num( a[ "pct" ], ap + ".pct", "pct", false, -1e300, 0, 100 );
      }
      else
        L.fail( ap + ".kind", "kind must be phase_time or boss_health" );
      out.at.push_back( wa );
    }
    out.time_shift_jitter = L.jref( w[ "time_shift_jitter" ], p + ".time_shift_jitter", "time_shift_jitter" );
    out.must_die = L.boolean( w[ "must_die" ], p + ".must_die", "must_die" );
    out.spawn_distance_yd = L.num( w[ "spawn_distance_yd" ], p + ".spawn_distance_yd", "spawn_distance_yd", false, -1e300, 0 );
    if ( kind == "add" )
    {
      if ( !w[ "max_health" ].IsNumber() )
        L.fail( p + ".max_health", "add max_health must be a number greater than 0 (an add has real health)" );
      out.max_health = L.num( w[ "max_health" ], p + ".max_health", "add max_health", false, -1e300, 0 );
      out.travel_s = L.num( w[ "travel_s" ], p + ".travel_s", "travel_s", false, 0 );
      out.travel_jitter = L.jref( w[ "travel_jitter" ], p + ".travel_jitter", "travel_jitter" );
      L.centre( out.travel_jitter, *out.travel_s, p + ".travel_s" );
      if ( !w[ "raid_stream" ].IsNull() )
      {
        const Value& r = w[ "raid_stream" ];
        const std::string rp = p + ".raid_stream";
        L.keys( r, { "damage_per_s", "start_after_spawn_s", "lifetime_jitter" }, rp );
        wave_stream_t ws;
        ws.damage_per_s = L.num( r[ "damage_per_s" ], rp + ".damage_per_s", "damage_per_s", false, 0 );
        ws.start_after_spawn_s = L.num( r[ "start_after_spawn_s" ], rp + ".start_after_spawn_s", "start_after_spawn_s", false, 0 );
        ws.lifetime_jitter = L.jref( r[ "lifetime_jitter" ], rp + ".lifetime_jitter", "lifetime_jitter", true );
        out.raid_stream = ws;
      }
      if ( !w[ "lifetime_s" ].IsNull() )
        L.fail( p + ".lifetime_s", "add lifetime_s must be null" );
      // tstl-sylvanas 261-07: validated and accepted; 262-04 reads it (fight.next_wave.lifetime): a number above 0, or null.
      if ( !w.HasMember( "nominal_lifetime_s" ) )
        L.fail( p + ".nominal_lifetime_s", "missing key nominal_lifetime_s" );
      if ( !w[ "nominal_lifetime_s" ].IsNull() )
        out.nominal_lifetime_s = L.num( w[ "nominal_lifetime_s" ], p + ".nominal_lifetime_s", "add nominal_lifetime_s", false, -1e300, 0 );
    }
    else
    {
      if ( !w[ "max_health" ].IsNull() )
        L.fail( p + ".max_health", "hazard max_health must be null" );
      if ( out.must_die )
        L.fail( p + ".must_die", "hazard must_die must be false" );
      if ( !w[ "travel_s" ].IsNull() )
        L.fail( p + ".travel_s", "hazard travel_s must be null" );
      if ( !w[ "travel_jitter" ].IsNull() )
        L.fail( p + ".travel_jitter", "hazard travel_jitter must be null" );
      if ( !w[ "raid_stream" ].IsNull() )
        L.fail( p + ".raid_stream", "hazard raid_stream must be null" );
      out.lifetime_s = L.num( w[ "lifetime_s" ], p + ".lifetime_s", "hazard lifetime_s", false, -1e300, 0 );
      if ( !w.HasMember( "nominal_lifetime_s" ) )
        L.fail( p + ".nominal_lifetime_s", "missing key nominal_lifetime_s" );
      if ( !w[ "nominal_lifetime_s" ].IsNull() )
        L.fail( p + ".nominal_lifetime_s", "hazard nominal_lifetime_s must be null" );  // tstl-sylvanas 261-07
    }
    spec.waves.push_back( out );
  }

  for ( const auto& kv : L.jit )
    if ( kv.second.j->per == "phase" )
      for ( const auto& u : kv.second.uses )
        if ( u != "pace_jitter" )
          L.fail( fmt::format( "jitter[{}].per", kv.second.j - spec.jitter.data() ),
                  fmt::format( "only pace_jitter keys may be per \"phase\" ({} is also used as {})", kv.first, u ) );
  const Value& tp = doc[ "timers_pending_second_kill" ];
  if ( !tp.IsArray() )
    L.fail( "timers_pending_second_kill", "must be a list of strings" );
  for ( SizeType i = 0; i < tp.Size(); ++i )
    if ( !tp[ i ].IsString() )
      L.fail( "timers_pending_second_kill", "must be a list of strings" );
  const Value& du = L.list( doc, "defaults_used", "" );
  for ( SizeType i = 0; i < du.Size(); ++i )
    L.keys( du[ i ], { "what", "value", "source" }, loader_t::at( "defaults_used", i ) );
  return spec;
}
}  // namespace sheet_fight_spec

// ==========================================================================
// The controller
// ==========================================================================

namespace
{
double r3( double x )
{
  return std::round( x * 1000.0 ) / 1000.0;
}

// The raid-damage stream: a background spell owned by the sleeping fight master, executed on a boss once per tick.
// Going through the action path gives absorbs, vulnerability, immunity, damage-taken accounting and the health
// callbacks for free. A magic school, so armour never scales it.
struct sheet_raid_stream_t : public spell_t
{
  std::function<void( action_state_t* )> on_assessed;

  explicit sheet_raid_stream_t( player_t* master ) : spell_t( "sheet_raid_stream", master )
  {
    school      = SCHOOL_ARCANE;
    may_crit    = false;
    may_miss    = false;
    callbacks   = false;
    background  = true;
    not_a_proc  = true;
    trigger_gcd = 0_ms;
    // tstl-sylvanas 261-03: the stream has no spell data, so no target multiplier would be snapshotted; ask for it so
    // the phase's damage-taken multiplier (the vulnerable debuff, see composite_target_da_multiplier) reaches it.
    snapshot_flags |= STATE_TGT_MUL_DA;
    update_flags |= STATE_TGT_MUL_DA;
  }

  // Only the controller's multiplier (the vulnerable debuff): no other target debuff scales the raid's own damage.
  double composite_target_da_multiplier( player_t* t ) const override
  {
    return ( t->debuffs.vulnerable && t->debuffs.vulnerable->check() ) ? 1.0 + t->debuffs.vulnerable->check_value() : 1.0;
  }

  void assess_damage( result_amount_type rt, action_state_t* s ) override
  {
    spell_t::assess_damage( rt, s );
    if ( on_assessed )
      on_assessed( s );
  }
};

// tstl-sylvanas 261-03: one add or hazard. A runtime pet of the fight master (the engine's own dynamic-pet
// mechanism, the same class of object the pull system spawns) with health as its primary resource. The
// controller keeps one instance record per spawn; a pet object can be reused by a later spawn, so the
// per-instance numbers are differences against the snapshots taken here.
struct sheet_mob_t : public pet_t
{
  int inst = -1;            // index of the controller's current instance record for this pet object
  double spawn_s = 0;       // sim time of the current spawn
  double dmg_base = 0;      // iteration_dmg_taken at the current spawn

  sheet_mob_t( player_t* o, util::string_view n ) : pet_t( o->sim, o, n, PET_ENEMY ) {}

  // Skip pet_t's override: it sets a pet's health from the owner's (here a sleeping master); an add has its own.
  void init_resources( bool force ) override { player_t::init_resources( force ); }
  resource_e primary_resource() const override { return RESOURCE_HEALTH; }

  // Time to a health percent from the damage THIS add has taken since it spawned.
  timespan_t time_to_percent( double percent ) const override
  {
    const double target_hp = resources.max[ RESOURCE_HEALTH ] * ( percent / 100.0 );
    const double cur       = resources.current[ RESOURCE_HEALTH ];
    if ( target_hp >= cur )
      return timespan_t::zero();
    const double elapsed = ( sim->current_time() ).total_seconds() - spawn_s;
    const double dmg     = iteration_dmg_taken - dmg_base;
    if ( elapsed > 0 && dmg > 0 )
      return timespan_t::from_seconds( ( cur - target_hp ) / ( dmg / elapsed ) );
    return pet_t::time_to_percent( percent );
  }
};
using sheet_spawner_t = spawner::pet_spawner_t<sheet_mob_t, player_t>;
}  // namespace

struct sheet_fight_event_t::impl_t
{
  struct sample_t
  {
    double t, pct, hp, max_hp, ttd;
    bool has_ttd;
  };
  struct boss_rt_t
  {
    player_t* actor = nullptr;
    const sheet_fight_spec::boss_t* spec = nullptr;
    int idx = 0;
    bool spawned = false, dead = false, benched = false, zero_bench = false;
    bool floor_hold = false;   // intermission: the boss cannot die (the real bosses sit at 1 health until the heal)
    bool vuln_applied = false; // the phase's damage-taken multiplier is on this boss
    double spawn_s = 0, death_s = -1, first_dmg = -1, first_pdmg = -1, first_swing = -1, stream_taken = 0;
    double life_start_s = 0, life_dmg_base = 0, max_at_spawn = 0, stream_rate = 0;
    std::vector<sample_t> samples;
    std::vector<double> pct_values;  // distinct percent thresholds (> 0) named by phase starts and wave triggers
  };
  struct phase_change_t
  {
    double at_s;
    int phase;
  };
  struct cast_rec_t
  {
    double at_s;
    long long ability_id;
    std::string caster, name;
  };

  sheet_fight_event_t& self;
  sim_t* sim;
  sheet_fight_spec::spec_t spec;
  std::vector<boss_rt_t> bosses;
  std::vector<std::vector<std::unique_ptr<raid_event_t>>> children;
  std::vector<char> trigger_ready;
  int current_phase = -1;
  bool ended_by_kill = false;
  bool active_mismatch_reported = false;  // the once-per-fight active_enemies self-check
  size_t tick_index = 0;
  double bloodlust_at = -1;  // sim time bloodlust fired this fight, or -1 (261-04)
  std::vector<phase_change_t> phase_changes;
  std::vector<cast_rec_t> casts_rec;
  sheet_raid_stream_t* stream = nullptr;  // owned by the master (its action_list), like the stock raid_damage_t

  // ---- adds (261-03) ---------------------------------------------------------------------------
  struct inst_t
  {
    int wave = -1;
    sheet_mob_t* mob = nullptr;
    bool alive = false, arrived = false, hazard = false;
    double spawn_s = 0, arrival_s = -1, death_s = -1, despawn_s = -1;
    double first_dmg = -1, first_pdmg = -1, first_swing = -1, stream_taken = 0;
    double dmg_base = 0, dmg_total = 0;
    double stream_from_s = 0, stream_rate = 0;
  };
  struct wave_rt_t
  {
    sheet_spawner_t* spawner = nullptr;
    std::vector<char> armed;  // per spec.waves[w].at entry: a boss_health trigger waiting for its crossing
  };
  struct window_t
  {
    std::string type;
    int phase;
    double start_s, end_s;
    const raid_event_t* ev;
  };
  std::vector<window_t> windows;
  std::vector<wave_rt_t> wave_rt;
  std::vector<std::unique_ptr<inst_t>> insts;
  std::unordered_map<const player_t*, int> mob_inst;

  // ---- events ---------------------------------------------------------------------------------
  struct tick_event_t : event_t
  {
    impl_t* im;
    tick_event_t( sim_t& s, impl_t* i, timespan_t t ) : event_t( s, t ), im( i ) {}
    const char* name() const override { return "sheet_fight_tick"; }
    void execute() override { im->tick(); }
  };
  struct cast_event_t : event_t
  {
    impl_t* im;
    int phase, cast;
    cast_event_t( sim_t& s, impl_t* i, int ph, int c, timespan_t t ) : event_t( s, t ), im( i ), phase( ph ), cast( c ) {}
    const char* name() const override { return "sheet_fight_cast"; }
    void execute() override { im->do_cast( phase, cast ); }
  };
  struct start_event_t : event_t
  {
    impl_t* im;
    int phase;
    start_event_t( sim_t& s, impl_t* i, int ph, timespan_t t ) : event_t( s, t ), im( i ), phase( ph ) {}
    const char* name() const override { return "sheet_fight_phase_start"; }
    void execute() override
    {
      im->trigger_ready[ phase ] = 1;
      im->try_advance();
    }
  };

  struct wave_event_t : event_t
  {
    impl_t* im;
    int wave, phase;
    wave_event_t( sim_t& s, impl_t* i, int w, int ph, timespan_t t ) : event_t( s, t ), im( i ), wave( w ), phase( ph ) {}
    const char* name() const override { return "sheet_fight_wave"; }
    void execute() override
    {
      if ( im->current_phase == phase )  // an instance whose phase has ended before it fires is dropped
        im->spawn_wave( wave );
    }
  };
  struct engage_event_t : event_t
  {
    impl_t* im;
    int phase, boss;
    std::string on_zero;
    engage_event_t( sim_t& s, impl_t* i, int ph, int b, std::string oz, timespan_t t )
      : event_t( s, t ), im( i ), phase( ph ), boss( b ), on_zero( std::move( oz ) ) {}
    const char* name() const override { return "sheet_fight_engage"; }
    void execute() override
    {
      if ( im->current_phase != phase )
        return;
      auto& b = im->bosses[ boss ];
      im->engage_boss( b, on_zero );
      im->apply_phase_effects( phase, b );
      im->retarget_players( retarget_source::ACTOR_ARISE, b.actor );
      im->sample( b );
    }
  };
  struct heal_event_t : event_t
  {
    impl_t* im;
    int phase, tick;
    heal_event_t( sim_t& s, impl_t* i, int ph, int k, timespan_t t ) : event_t( s, t ), im( i ), phase( ph ), tick( k ) {}
    const char* name() const override { return "sheet_fight_heal"; }
    void execute() override { im->heal_tick( phase, tick ); }
  };
  struct bloodlust_event_t : event_t
  {
    impl_t* im;
    int phase;
    bloodlust_event_t( sim_t& s, impl_t* i, int ph, timespan_t t ) : event_t( s, t ), im( i ), phase( ph ) {}
    const char* name() const override { return "sheet_fight_bloodlust"; }
    void execute() override
    {
      if ( im->current_phase == phase )  // a phase that has already ended gets no bloodlust
        im->do_bloodlust();
    }
  };
  struct arrive_event_t : event_t
  {
    impl_t* im;
    int inst;
    arrive_event_t( sim_t& s, impl_t* i, int n, timespan_t t ) : event_t( s, t ), im( i ), inst( n ) {}
    const char* name() const override { return "sheet_fight_arrive"; }
    void execute() override { im->on_arrive( inst ); }
  };

  impl_t( sheet_fight_event_t& s, sim_t* sm, sheet_fight_spec::spec_t sp ) : self( s ), sim( sm ), spec( std::move( sp ) )
  {
    children.resize( spec.phases.size() );
    trigger_ready.assign( spec.phases.size(), 0 );
  }

  double now() const { return sim->current_time().total_seconds(); }
  boss_rt_t* find_boss( const std::string& actor )
  {
    for ( auto& b : bosses )
      if ( b.spec->actor == actor )
        return &b;
    return nullptr;
  }
  boss_rt_t* find_boss( const player_t* p )
  {
    for ( auto& b : bosses )
      if ( b.actor == p )
        return &b;
    return nullptr;
  }

  void retarget_players( retarget_source why, player_t* boss )
  {
    for ( auto* p : sim->player_non_sleeping_list )
      p->acquire_target( why, boss );
  }

  // ---- health samples ------------------------------------------------------------------------
  std::optional<timespan_t> ttd( const boss_rt_t& b, double percent ) const
  {
    if ( !b.spawned || b.dead )
      return std::nullopt;
    const double max_hp = b.actor->resources.max[ RESOURCE_HEALTH ];
    const double cur = b.actor->resources.current[ RESOURCE_HEALTH ];
    if ( max_hp <= 0 || cur / max_hp * 100.0 <= percent )
      return timespan_t::zero();
    const double elapsed = now() - b.life_start_s;
    const double dmg = b.actor->iteration_dmg_taken - b.life_dmg_base;
    if ( elapsed <= 0 || dmg <= 0 )
      return timespan_t::max();
    return timespan_t::from_seconds( ( cur - percent * 0.01 * max_hp ) / ( dmg / elapsed ) );
  }

  void sample( boss_rt_t& b )
  {
    if ( !b.spawned )
      return;
    const double max_hp = b.actor->resources.max[ RESOURCE_HEALTH ];
    const double cur = b.actor->resources.current[ RESOURCE_HEALTH ];
    sample_t s{ r3( now() ), max_hp > 0 ? cur / max_hp * 100.0 : 0.0, cur, max_hp, 0.0, false };
    auto t = b.dead ? std::optional<timespan_t>( timespan_t::zero() ) : ttd( b, 0.0 );
    if ( t && *t != timespan_t::max() )
    {
      s.ttd = r3( t->total_seconds() );
      s.has_ttd = true;
    }
    // tstl-sylvanas 261-08: two samples at one instant used to collapse into one, which lost the
    // 1-health bench sample a boss takes when his life ends just before a phase gives him a new
    // maximum health. The reader closes a life only on a sample at or below 1 health
    // (scripts/wcl/boss-timeline.js:195), so that sample must survive. The record contract wants strictly
    // increasing times (scripts/fights/fight-contracts.js:652), so a sample at the same instant with a
    // DIFFERENT maximum health is stamped one millisecond (the record's resolution, r3) after the previous one.
    // A sample at the same instant with the SAME maximum still replaces the previous one, and keeps its stamp.
    if ( !b.samples.empty() && b.samples.back().t >= s.t - 1e-9 )
    {
      if ( b.samples.back().max_hp == s.max_hp )
      {
        s.t = b.samples.back().t;
        b.samples.back() = s;
      }
      else
      {
        s.t = r3( b.samples.back().t + 0.001 );
        b.samples.push_back( s );
      }
    }
    else
      b.samples.push_back( s );
  }

  // ---- adds ------------------------------------------------------------------------------------
  // The centre of a jitter key's range (a nominal fight, and a field not yet wired to its draw).
  double jit_centre( const std::string& key, double none ) const
  {
    if ( key.empty() )
      return none;
    for ( const auto& j : spec.jitter )
      if ( j.key == key )
        return ( j.min + j.max ) / 2.0;
    return none;
  }

  // ---- jitter (261-04) ---------------------------------------------------------------------------
  // One value per fight-level key, one per phase for a per-phase key; filled once per fight by draw_jitter().
  std::vector<double> drawn_fight;
  std::vector<std::vector<double>> drawn_phase;

  // One draw: the range's centre with NO draw at all when jitter is off or the range has no width, else exactly one
  // uniform draw from the shared stream inside its own tagged scope (so a recording attributes it to this controller).
  double draw_one( const sheet_fight_spec::jitter_t& j )
  {
    if ( !sim->solver_sheet_jitter || !( j.max > j.min ) )
      return ( j.min + j.max ) / 2.0;
    rl_rng_record::rl_raid_draw_scope_t guard( sim, self );
    return sim->rng().range( j.min, j.max );
  }

  // Draw every spec key in the spec's list order; a per-phase key once for each spec phase, in phase order (a fixed
  // number of draws per fight whatever happens in it).
  void draw_jitter()
  {
    drawn_fight.assign( spec.jitter.size(), 0.0 );
    drawn_phase.assign( spec.jitter.size(), std::vector<double>() );
    for ( size_t k = 0; k < spec.jitter.size(); ++k )
    {
      if ( spec.jitter[ k ].per == "phase" )
        for ( size_t p = 0; p < spec.phases.size(); ++p )
          drawn_phase[ k ].push_back( draw_one( spec.jitter[ k ] ) );
      else
        drawn_fight[ k ] = draw_one( spec.jitter[ k ] );
    }
  }

  // This fight's value for a key (the phase's value for a per-phase key); `none` for an absent key.
  double jit_value( const std::string& key, double none, int phase = -1 ) const
  {
    if ( key.empty() )
      return none;
    for ( size_t k = 0; k < spec.jitter.size() && k < drawn_fight.size(); ++k )
      if ( spec.jitter[ k ].key == key )
      {
        if ( spec.jitter[ k ].per == "phase" )
          return phase >= 0 && phase < static_cast<int>( drawn_phase[ k ].size() ) ? drawn_phase[ k ][ phase ] : jit_centre( key, none );
        return drawn_fight[ k ];
      }
    return jit_centre( key, none );
  }

  // The engine caches each action's target list; a spawn, an arrival or a death must rebuild them
  // (the pull system's regenerate_cache).
  void invalidate_target_caches()
  {
    for ( auto* p : sim->player_non_sleeping_list )
      for ( auto* a : p->action_list )
        a->target_cache.is_valid = false;
  }

  inst_t* find_inst( const player_t* p )
  {
    auto it = mob_inst.find( p );
    return it == mob_inst.end() ? nullptr : insts[ it->second ].get();
  }

  void spawn_wave( int w )
  {
    const auto& wv = spec.waves[ w ];
    auto& wr       = wave_rt[ w ];
    if ( !wr.spawner )
      return;
    const bool hazard = wv.kind == "hazard";
    // A hazard leaves on its own schedule (spec lifetime_s); an add has no clock at all.
    const timespan_t life = ( hazard && wv.lifetime_s ) ? timespan_t::from_seconds( *wv.lifetime_s ) : timespan_t::min();
    auto mobs = wr.spawner->spawn( life, static_cast<unsigned>( wv.count ) );
    for ( auto* m : mobs )
    {
      m->full_name_str = m->name_str = wv.actor;
      // Behind the player, the spec's distance from the boss at the origin: no facing-gated spell reaches it early.
      m->x_position = -wv.spawn_distance_yd;
      m->y_position = 0;
      if ( !hazard )
      {
        m->resources.base[ RESOURCE_HEALTH ]              = *wv.max_health;
        m->resources.infinite_resource[ RESOURCE_HEALTH ] = false;
        m->init_resources( true );
      }

      auto in = std::make_unique<inst_t>();
      in->wave     = w;
      in->mob      = m;
      in->alive    = true;
      in->hazard   = hazard;
      in->spawn_s  = now();
      in->dmg_base = m->iteration_dmg_taken;
      if ( wv.raid_stream )
      {
        in->stream_rate   = wv.raid_stream->damage_per_s / jit_value( wv.raid_stream->lifetime_jitter, 1.0 );  // tstl-sylvanas 261-04: a longer lifetime = a slower stream
        in->stream_from_s = in->spawn_s + wv.raid_stream->start_after_spawn_s;
      }
      const int idx = static_cast<int>( insts.size() );
      m->inst       = idx;
      m->spawn_s    = in->spawn_s;
      m->dmg_base   = in->dmg_base;
      mob_inst[ m ] = idx;
      insts.push_back( std::move( in ) );
      if ( !hazard )
      {
        const double travel = jit_value( wv.travel_jitter, wv.travel_s.value_or( 0.0 ) );  // tstl-sylvanas 261-04
        make_event<arrive_event_t>( *sim, *sim, this, idx, timespan_t::from_seconds( travel ) );
      }
    }
    invalidate_target_caches();
  }

  // The add reaches melee: it is put next to the first engaged boss, 2 yards to the side (walking in is SIM-11).
  void on_arrive( int idx )
  {
    auto& in = *insts[ idx ];
    if ( !in.alive || in.arrived )
      return;
    in.arrived   = true;
    in.arrival_s = now();
    double bx = 0, by = 0;
    for ( auto& b : bosses )
      if ( b.spawned && !b.dead && !b.benched && !b.actor->is_sleeping() )
      {
        bx = b.actor->x_position;
        by = b.actor->y_position;
        break;
      }
    in.mob->x_position = bx;
    in.mob->y_position = by + 2.0;
    invalidate_target_caches();
  }

  // The spawner's demise callback: an add dies only by damage; fight-end demises are not deaths.
  void on_mob_demise( sheet_mob_t* m )
  {
    if ( m->inst < 0 )
      return;
    auto& in = *insts[ m->inst ];
    if ( !in.alive )
      return;
    in.alive     = false;
    in.dmg_total = m->iteration_dmg_taken - in.dmg_base;
    if ( sim->event_mgr.canceled )
      return;
    if ( in.hazard )
      in.despawn_s = now();  // a hazard leaves on its schedule; it is never killed
    else
      in.death_s = now();
    invalidate_target_caches();
  }

  // ---- downtime windows ----------------------------------------------------------------------
  void child_start( raid_event_t* ev )
  {
    // The engine names its movement events movement_distance / movement_direction; the record says "movement".
    const std::string type = ev->type.rfind( "movement", 0 ) == 0 ? std::string( "movement" ) : ev->type;
    windows.push_back( { type, ev->pull - 1, r3( now() ), -1.0, ev } );
  }
  void child_finish( raid_event_t* ev )
  {
    for ( auto it = windows.rbegin(); it != windows.rend(); ++it )
      if ( it->ev == ev && it->end_s < 0 )
      {
        it->end_s = r3( now() );
        return;
      }
  }

  // ---- phases --------------------------------------------------------------------------------
  void engage_boss( boss_rt_t& b, const std::string& on_zero )
  {
    player_t* a = b.actor;
    if ( a->is_sleeping() )
    {
      a->initial.sleeping = false;
      a->arise();
    }
    if ( !b.spawned )
    {
      b.spawned = true;
      b.spawn_s = now();
      b.life_start_s = b.spawn_s;
      b.life_dmg_base = a->iteration_dmg_taken;
      b.max_at_spawn = a->resources.max[ RESOURCE_HEALTH ];
    }
    else if ( b.benched )
    {
      b.benched = false;
      a->debuffs.invulnerable->decrement();
      retarget_players( retarget_source::ACTOR_INVULNERABLE, a );
    }
    b.zero_bench = on_zero == "bench";
  }

  // A boss listed by the phase gets its second life (new maximum health), its damage-taken multiplier and, in an
  // intermission, the no-death floor. Applied once per boss per phase, when the boss is up.
  void apply_phase_effects( int j, boss_rt_t& b )
  {
    const auto& ph = spec.phases[ j ];
    player_t* a    = b.actor;
    for ( const auto& mh : ph.max_health )
      if ( mh.first == b.spec->actor )
      {
        // Only max and initial: base stays, so the next fight's arise (init_resources) restores the first life's maximum.
        a->resources.max[ RESOURCE_HEALTH ]     = mh.second;
        a->resources.initial[ RESOURCE_HEALTH ] = mh.second;
        if ( a->resources.current[ RESOURCE_HEALTH ] > mh.second )
          a->resources.current[ RESOURCE_HEALTH ] = mh.second;
        b.life_start_s  = now();
        b.life_dmg_base = a->iteration_dmg_taken;
        sample( b );  // the reader splits a new life on a change of maximum
      }
    for ( const auto& d : ph.damage_taken )
      if ( d.boss == b.spec->actor && !b.vuln_applied && std::fabs( d.multiplier - 1.0 ) > 1e-12 )
      {
        a->debuffs.vulnerable->increment( 1, d.multiplier - 1.0 );  // composite_player_vulnerability multiplies by 1 + value
        b.vuln_applied = true;
      }
    if ( ph.intermission && !b.benched && !b.dead )
    {
      b.floor_hold = true;
      if ( a->resources.current[ RESOURCE_HEALTH ] < 2.0 )
        a->resources.current[ RESOURCE_HEALTH ] = 2.0;
    }
  }

  void end_phase_effects()
  {
    for ( auto& b : bosses )
    {
      b.floor_hold = false;
      if ( b.vuln_applied )
      {
        b.actor->debuffs.vulnerable->decrement();
        b.vuln_applied = false;
      }
    }
  }

  void heal_tick( int phase, int tick )
  {
    // tstl-sylvanas 261-08: a heal delivers all its declared ticks. It used to drop a tick that came due
    // after its own phase had ended; the 35th tick of the Coiled Altar intermission is due 0.05 s after the
    // last phase starts on its timer (in the real kill it landed 0.03 s into that phase). The per-boss guard
    // below (not spawned, dead, or asleep) still skips a boss, and the chain still ends after h.ticks ticks.
    const auto& h = *spec.phases[ phase ].heal;
    for ( const auto& name : h.bosses )
    {
      auto* b = find_boss( name );
      if ( !b || !b->spawned || b->dead || b->actor->is_sleeping() )
        continue;
      b->actor->resource_gain( RESOURCE_HEALTH, h.pct_of_max_per_tick / 100.0 * b->actor->resources.max[ RESOURCE_HEALTH ] );
    }
    if ( tick + 1 < h.ticks )
      make_event<heal_event_t>( *sim, *sim, this, phase, tick + 1, timespan_t::from_seconds( h.tick_interval_s ) );
  }

  void start_phase( int j )
  {
    const auto& ph = spec.phases[ j ];
    sim->print_log( "sheet fight: phase {} '{}' starts", j, ph.name );
    if ( current_phase >= 0 )
    {
      for ( auto& c : children[ current_phase ] )
        c->deactivate( "sheet phase ended" );
      end_phase_effects();
    }
    current_phase = j;
    phase_changes.push_back( { now(), j } );
    for ( size_t e = 0; e < ph.engage.size(); ++e )
    {
      const auto& en = ph.engage[ e ];
      auto* eb       = find_boss( en.boss );
      const double delay = jit_value( en.delay_jitter, en.delay_s );  // tstl-sylvanas 261-04: this fight's drawn walk-in gap
      if ( delay > 0 )  // the walk-in gap: nothing is engageable until then
        make_event<engage_event_t>( *sim, *sim, this, j, eb->idx, en.on_zero, timespan_t::from_seconds( delay ) );
      else
        engage_boss( *eb, en.on_zero );
    }
    for ( auto& b : bosses )
      if ( b.spawned && !b.dead )
        apply_phase_effects( j, b );
    if ( ph.heal )
      make_event<heal_event_t>( *sim, *sim, this, j, 0, timespan_t::from_seconds( ph.heal->first_tick_after_start_s ) );
    for ( auto& b : bosses )
      b.stream_rate = 0;
    for ( auto& s : ph.raid_stream )
      find_boss( s.boss )->stream_rate = s.damage_per_s * jit_value( s.pace_jitter, 1.0, j );  // tstl-sylvanas 261-04: this phase's raid pace
    if ( ph.bloodlust )  // tstl-sylvanas 261-04: the sheet's anchor phase start + the drawn offset, never a fixed second
      make_event<bloodlust_event_t>( *sim, *sim, this, j,
                                     timespan_t::from_seconds( jit_value( ph.bloodlust->offset_jitter, ph.bloodlust->offset_after_start_s ) ) );
    for ( size_t c = 0; c < ph.casts.size(); ++c )
      make_event<cast_event_t>( *sim, *sim, this, j, static_cast<int>( c ), timespan_t::from_seconds( ph.casts[ c ].at_in_phase_s ) );
    for ( size_t w = 0; w < spec.waves.size(); ++w )
      for ( size_t k = 0; k < spec.waves[ w ].at.size(); ++k )
      {
        const auto& a = spec.waves[ w ].at[ k ];
        if ( a.kind == "boss_health" )
          wave_rt[ w ].armed[ k ] = a.phase == j ? 1 : 0;  // armed from its phase's start; disarmed when another phase starts
        if ( a.phase != j )
          continue;
        if ( a.kind == "phase_time" )
        {
          const double shift = jit_value( spec.waves[ w ].time_shift_jitter, 0.0 );  // tstl-sylvanas 261-04: one value per fight; clamped to the phase start below
          make_event<wave_event_t>( *sim, *sim, this, static_cast<int>( w ), j,
                                    timespan_t::from_seconds( std::max( 0.0, a.in_phase_s + shift ) ) );
        }
      }
    for ( auto& c : children[ j ] )
      c->combat_begin();
    for ( auto& b : bosses )
      sample( b );
  }

  void try_advance()
  {
    while ( current_phase + 1 < static_cast<int>( spec.phases.size() ) && trigger_ready[ current_phase + 1 ] )
      start_phase( current_phase + 1 );
  }

  // Bloodlust for every player who can take it (the pull system's loop, raid_event.cpp; the player's own bloodlust
  // action cannot fire: the style keeps overrides.bloodlust at 0, so the engine's time/percent check is never scheduled).
  // Indices, not iterators: triggering a buff can add actors.
  void do_bloodlust()
  {
    for ( size_t i = 0; i < sim->player_non_sleeping_list.size(); i++ )
    {
      auto* p = sim->player_non_sleeping_list[ i ];
      if ( p->is_pet() || p->buffs.exhaustion->check() )
        continue;
      p->buffs.bloodlust->trigger();
      p->buffs.exhaustion->trigger();
    }
    bloodlust_at = now();
  }

  void do_cast( int phase, int cast )
  {
    const auto& c = spec.phases[ phase ].casts[ cast ];
    casts_rec.push_back( { r3( now() ), c.ability_id, c.caster, c.name } );
    for ( size_t j = 0; j < spec.phases.size(); ++j )
    {
      const auto& st = spec.phases[ j ].start;
      if ( st.kind == "after_cast" && st.cast == c.name )
        make_event<start_event_t>( *sim, *sim, this, static_cast<int>( j ),
                                   timespan_t::from_seconds( jit_value( st.after_jitter, st.after_s ) ) );  // tstl-sylvanas 261-04: the drawn break length
    }
  }

  // A boss crossed a health threshold (pct 0 = the 1-health zero callback): any phase starting on it becomes ready.
  void crossed( const boss_rt_t& b, double pct )
  {
    for ( size_t j = 1; j < spec.phases.size(); ++j )
    {
      const auto& st = spec.phases[ j ].start;
      if ( st.kind == "boss_health" && st.boss == b.spec->actor && std::fabs( st.pct - pct ) < 1e-9 )
        trigger_ready[ j ] = 1;
    }
    try_advance();
  }

  void zero_cb( int i, bool increasing )
  {
    auto& b = bosses[ i ];
    if ( increasing || !b.spawned || b.dead )
      return;
    if ( b.floor_hold )
    {
      // Intermission: the boss cannot die; hold it just above the 1-health line so every later hit fires again.
      b.actor->resources.current[ RESOURCE_HEALTH ] = 2.0;
      return;
    }
    if ( b.zero_bench && !b.benched )
    {
      // Inside resource_loss, before the death check: hold at 1 health (direct write: no recursion, survives
      // player_t::do_damage's death test) and make the boss immune so ignore_invulnerable_targets takes it out of
      // every target list.
      b.actor->resources.current[ RESOURCE_HEALTH ] = 1.0;
      b.benched = true;
      b.actor->debuffs.invulnerable->increment();
      retarget_players( retarget_source::ACTOR_INVULNERABLE, b.actor );
      sample( b );
    }
    fire_health_waves( b, 0.0 );
    crossed( b, 0.0 );
  }

  // A boss crossed a health threshold downward: every armed boss_health wave trigger on it fires once.
  void fire_health_waves( const boss_rt_t& b, double pct )
  {
    for ( size_t w = 0; w < spec.waves.size(); ++w )
      for ( size_t k = 0; k < spec.waves[ w ].at.size(); ++k )
      {
        const auto& a = spec.waves[ w ].at[ k ];
        if ( a.kind == "boss_health" && wave_rt[ w ].armed[ k ] && a.boss == b.spec->actor && std::fabs( a.pct - pct ) < 1e-9 )
        {
          wave_rt[ w ].armed[ k ] = 0;
          spawn_wave( static_cast<int>( w ) );
        }
      }
  }

  void pct_cb( int i, double pct, bool increasing )
  {
    auto& b = bosses[ i ];
    if ( increasing || !b.spawned || b.dead )
      return;
    fire_health_waves( b, pct );  // before crossed(): a phase change would disarm them
    crossed( b, pct );
  }

  void death_cb( int i )
  {
    auto& b = bosses[ i ];
    if ( sim->event_mgr.canceled || b.dead || !b.spawned )  // fight-end demises are not deaths
      return;
    b.dead = true;
    b.death_s = now();
    sample( b );
    for ( auto& o : bosses )
      if ( o.spec->must_die && !o.dead )
        return;
    ended_by_kill = true;
    sim->cancel_iteration();
  }

  // ---- stream and tick -----------------------------------------------------------------------
  void tick()
  {
    const double interval = spec.sample_interval_s;
    if ( tick_index > 0 && stream )
    {
      for ( auto& b : bosses )
      {
        if ( !b.spawned || b.dead || b.benched || b.stream_rate <= 0 || b.actor->is_sleeping() )
          continue;
        stream->base_dd_min = stream->base_dd_max = b.stream_rate * interval;
        stream->target = b.actor;
        stream->execute();
      }
    }
    if ( tick_index > 0 && stream )
    {
      for ( auto& up : insts )
      {
        auto& in = *up;
        if ( !in.alive || in.stream_rate <= 0 || now() + 1e-9 < in.stream_from_s )
          continue;
        stream->base_dd_min = stream->base_dd_max = in.stream_rate * interval;
        stream->target = in.mob;
        stream->execute();
      }
    }
    if ( !active_mismatch_reported )
    {
      int expected = 0;
      for ( const auto* t : sim->target_non_sleeping_list )
        if ( !t->sheet_hazard )
          ++expected;
      if ( expected != sim->active_enemies )
      {
        active_mismatch_reported = true;
        fmt::print( stderr, "[RL_SHEET_FIGHT] active_enemies={} expected={} t={:.3f}\n", sim->active_enemies, expected, now() );
      }
    }
    for ( auto& b : bosses )
      if ( !b.dead )
        sample( b );
    ++tick_index;
    make_event<tick_event_t>( *sim, *sim, this, timespan_t::from_seconds( interval ) );
  }

  void on_stream_assessed( action_state_t* s )
  {
    if ( !s->target || s->result_amount <= 0 )
      return;
    if ( auto* b = find_boss( s->target ) )
    {
      b->stream_taken += s->result_amount;
      if ( b->first_dmg < 0 )
        b->first_dmg = now();
    }
    else if ( auto* in = find_inst( s->target ) )
    {
      in->stream_taken += s->result_amount;
      if ( in->first_dmg < 0 )
        in->first_dmg = now();
    }
  }

  void on_player_damage( action_state_t* s )
  {
    if ( !s->target || s->result_amount <= 0 )
      return;
    auto* b = find_boss( s->target );
    if ( !b )
    {
      if ( auto* in = find_inst( s->target ) )
      {
        const double t = now();
        if ( in->first_pdmg < 0 )
          in->first_pdmg = t;
        if ( in->first_dmg < 0 )
          in->first_dmg = t;
        if ( in->first_swing < 0 && s->action && !s->action->special )
          in->first_swing = t;
      }
      return;
    }
    const double t = now();
    if ( b->first_pdmg < 0 )
      b->first_pdmg = t;
    if ( b->first_dmg < 0 )
      b->first_dmg = t;
    if ( b->first_swing < 0 && s->action && !s->action->special )
      b->first_swing = t;
  }

  // ---- the fight.* view (tstl-sylvanas 262-04) -------------------------------------------------
  // What the net is allowed to see (IN-01). A pure read: no draw, no event, no state change. Nothing here reads a value the
  // game hides: the wave forecast is the DRAWN spawn time (the announce is visible) plus the sheet's NOMINAL travel
  // (R-5, the centre of the jitter range); the lifetime and the bloodlust offset are the sheet's nominal centres.
  // An event whose time is decided by a boss's health (a health-triggered phase, wave or window) is NOT known in time,
  // so it is never forecast as "soon" (pitfall P4); only the health pair says how near a health-triggered phase is.
  sheet_fight_forecast_t forecast() const
  {
    sheet_fight_forecast_t f;
    if ( current_phase < 0 || phase_changes.empty() )
      return f;
    const double t           = now();
    const double phase_start = phase_changes.back().at_s;

    // The next must-die add wave: a phase-time trigger of the CURRENT phase (a later phase has not scheduled its own yet),
    // arrival = drawn spawn + nominal travel, strictly later than now. An instance already spawned but not yet at its
    // nominal arrival still counts: the game shows the add walking in.
    for ( size_t w = 0; w < spec.waves.size(); ++w )
    {
      const auto& wv = spec.waves[ w ];
      if ( wv.kind != "add" || !wv.must_die )
        continue;
      const double shift = jit_value( wv.time_shift_jitter, 0.0 );
      for ( const auto& a : wv.at )
      {
        if ( a.kind != "phase_time" || a.phase != current_phase )
          continue;
        const double spawn   = phase_start + std::max( 0.0, a.in_phase_s + shift );
        const double arrival = spawn + wv.travel_s.value_or( 0.0 );
        const double in      = arrival - t;
        if ( in > 0.0 && ( !f.wave_known || in < f.wave_arrival_in_s ) )
        {
          f.wave_known        = true;
          f.wave_arrival_in_s = in;
          f.wave_count        = static_cast<int>( wv.count );
          f.wave_lifetime_s   = wv.nominal_lifetime_s.value_or( 0.0 );
        }
      }
    }

    // The next phase, only when its start is a boss's health crossing (a timer-started phase reads no event, P5).
    const int next = current_phase + 1;
    if ( next < static_cast<int>( spec.phases.size() ) && spec.phases[ next ].start.kind == "boss_health" )
      for ( const auto& b : bosses )
        if ( b.spec->actor == spec.phases[ next ].start.boss && b.spawned && !b.dead )
        {
          const double max_hp = b.actor->resources.max[ RESOURCE_HEALTH ];
          if ( max_hp > 0 )
          {
            f.phase_known        = true;
            f.phase_at_boss_pct  = spec.phases[ next ].start.pct;
            f.phase_boss_pct_now = b.actor->resources.current[ RESOURCE_HEALTH ] / max_hp * 100.0;
          }
          break;
        }

    // Downtime: the current phase's no-damage windows (a stun, a forced movement, an immune boss). One in progress reads
    // 0; else the earliest start already scheduled by timestamp. A health-triggered window (first_pct) is unknown until it
    // begins (P4).
    for ( const auto& c : children[ current_phase ] )
    {
      const bool downtime = c->type == "stun" || c->type == "invulnerable" || c->type.rfind( "movement", 0 ) == 0;
      if ( !downtime )
        continue;
      if ( c->up() )
      {
        f.downtime_known  = true;
        f.downtime_active = true;
        f.downtime_in_s   = 0.0;
        break;
      }
      if ( c->first_pct != -1 )
        continue;
      const double u = c->until_next().total_seconds();
      if ( u > 0.0 && u < 1.0e6 && ( !f.downtime_known || u < f.downtime_in_s ) )
      {
        f.downtime_known = true;
        f.downtime_in_s  = u;
      }
    }

    // Bloodlust: the current phase carries it, it has not fired yet; the sheet's nominal offset after the phase start,
    // floored at 0 (due now) once that moment has passed without it firing.
    const auto& ph = spec.phases[ current_phase ];
    if ( ph.bloodlust && bloodlust_at < 0 )
    {
      f.bloodlust_known = true;
      f.bloodlust_in_s  = std::max( 0.0, phase_start + ph.bloodlust->offset_after_start_s - t );
    }
    return f;
  }

  // ---- the fight record ----------------------------------------------------------------------
  void write_record()
  {
    if ( sim->solver_fight_timeline_str.empty() )
      return;
    StringBuffer sb;
    Writer<StringBuffer> w( sb );
    auto key = [ & ]( const char* k ) { w.Key( k ); };
    auto num_or_null = [ & ]( double v ) {
      if ( v < 0 )
        w.Null();
      else
        w.Double( r3( v ) );
    };
    w.StartObject();
    key( "format" ); w.String( "fork-fight/1" );
    key( "slug" ); w.String( spec.slug.c_str() );
    key( "encounter_id" ); w.Int64( spec.encounter_id );
    key( "provenance" );
    w.StartObject();
    key( "engine_git_rev" ); w.String( git_info::available() && git_info::revision()[ 0 ] ? git_info::revision() : "unknown" );
    key( "seed" ); w.String( std::to_string( sim->seed ).c_str() );
    key( "iteration" ); w.Int( std::max( sim->current_iteration, 0 ) );
    key( "sheet_fingerprint" ); w.String( spec.sheet_fingerprint.c_str() );
    key( "spec_path" ); w.String( spec.path.c_str() );
    key( "nominal" ); w.Bool( !sim->solver_sheet_jitter );
    w.EndObject();
    // jitter drawn (261-04): what this fight actually used, one value per key (a per-phase key: one value per spec phase).
    key( "jitter_drawn" );
    w.StartObject();
    for ( size_t k = 0; k < spec.jitter.size(); ++k )
    {
      const auto& j = spec.jitter[ k ];
      key( j.key.c_str() );
      if ( j.per == "phase" )
      {
        w.StartArray();
        for ( size_t p = 0; p < spec.phases.size(); ++p )
          w.Double( jit_value( j.key, ( j.min + j.max ) / 2.0, static_cast<int>( p ) ) );
        w.EndArray();
      }
      else
        w.Double( jit_value( j.key, ( j.min + j.max ) / 2.0 ) );
    }
    w.EndObject();
    key( "ended_by" ); w.String( ended_by_kill ? "kill" : "wipe" );
    key( "duration_s" ); w.Double( r3( now() ) );
    key( "enemies" );
    w.StartArray();
    for ( auto& b : bosses )
    {
      if ( !b.spawned )
        continue;
      w.StartObject();
      key( "actor" ); w.String( b.spec->actor.c_str() );
      key( "actor_index" ); w.Int( static_cast<int>( b.actor->actor_index ) );
      key( "npc_game_id" ); w.Int64( b.spec->npc_game_id );
      key( "kind" ); w.String( "boss" );
      key( "must_die" ); w.Bool( b.spec->must_die );
      key( "hazard" ); w.Bool( false );
      key( "spawn_s" ); w.Double( r3( b.spawn_s ) );
      key( "melee_arrival_s" ); w.Null();
      key( "despawn_s" ); w.Null();
      key( "death_s" ); num_or_null( b.death_s );
      key( "first_damage_taken_s" ); num_or_null( b.first_dmg );
      key( "first_player_damage_s" ); num_or_null( b.first_pdmg );
      key( "first_player_swing_s" ); num_or_null( b.first_swing );
      key( "total_damage_taken" ); w.Double( b.actor->iteration_dmg_taken );
      key( "raid_stream_damage_taken" ); w.Double( b.stream_taken );
      key( "max_health" ); w.Double( b.max_at_spawn );
      w.EndObject();
    }
    for ( auto& up : insts )
    {
      const auto& in = *up;
      const auto& wv = spec.waves[ in.wave ];
      w.StartObject();
      key( "actor" ); w.String( wv.actor.c_str() );
      key( "actor_index" ); w.Int( static_cast<int>( in.mob->actor_index ) );
      key( "npc_game_id" ); w.Int64( wv.npc_game_id );
      key( "kind" ); w.String( wv.kind.c_str() );
      key( "must_die" ); w.Bool( wv.must_die );
      key( "hazard" ); w.Bool( wv.kind == "hazard" );
      key( "spawn_s" ); w.Double( r3( in.spawn_s ) );
      key( "melee_arrival_s" ); num_or_null( in.arrival_s );
      key( "despawn_s" ); num_or_null( in.despawn_s );
      key( "death_s" ); num_or_null( in.death_s );
      key( "first_damage_taken_s" ); num_or_null( in.first_dmg );
      key( "first_player_damage_s" ); num_or_null( in.first_pdmg );
      key( "first_player_swing_s" ); num_or_null( in.first_swing );
      key( "total_damage_taken" ); w.Double( in.alive ? in.mob->iteration_dmg_taken - in.dmg_base : in.dmg_total );
      key( "raid_stream_damage_taken" ); w.Double( in.stream_taken );
      key( "max_health" );
      if ( wv.max_health )
        w.Double( *wv.max_health );
      else
        w.Null();
      w.EndObject();
    }
    w.EndArray();
    key( "boss_health" );
    w.StartObject();
    for ( auto& b : bosses )
    {
      if ( !b.spawned )
        continue;
      key( b.spec->actor.c_str() );
      w.StartArray();
      for ( const auto& s : b.samples )
      {
        w.StartArray();
        w.Double( s.t );
        w.Double( s.pct );
        w.Double( s.hp );
        w.Double( s.max_hp );
        if ( s.has_ttd )
          w.Double( s.ttd );
        else
          w.Null();
        w.EndArray();
      }
      w.EndArray();
    }
    w.EndObject();
    key( "phase_changes" );
    w.StartArray();
    for ( const auto& pc : phase_changes )
    {
      const auto& ph = spec.phases[ pc.phase ];
      w.StartObject();
      key( "at_s" ); w.Double( r3( pc.at_s ) );
      key( "phase" ); w.Int( pc.phase );
      key( "name" ); w.String( ph.name.c_str() );
      key( "trigger" );
      w.StartObject();
      key( "kind" ); w.String( ph.start.kind.c_str() );
      if ( ph.start.kind == "boss_health" )
      {
        key( "boss" ); w.String( ph.start.boss.c_str() );
        key( "pct" ); w.Double( ph.start.pct );
      }
      else if ( ph.start.kind == "after_cast" )
      {
        key( "cast" ); w.String( ph.start.cast.c_str() );
        key( "after_s" ); w.Double( ph.start.after_s );
      }
      w.EndObject();
      w.EndObject();
    }
    w.EndArray();
    key( "enemy_casts" );
    w.StartArray();
    for ( const auto& c : casts_rec )
    {
      w.StartObject();
      key( "at_s" ); w.Double( c.at_s );
      key( "type" ); w.String( "cast" );
      key( "ability_id" ); w.Int64( c.ability_id );
      key( "caster" ); w.String( c.caster.c_str() );
      key( "name" ); w.String( c.name.c_str() );
      w.EndObject();
    }
    w.EndArray();
    key( "bloodlust" );
    if ( bloodlust_at < 0 )
      w.Null();
    else
    {
      w.StartObject();
      key( "at_s" ); w.Double( r3( bloodlust_at ) );
      w.EndObject();
    }
    key( "intermissions" );
    w.StartArray();
    for ( size_t k = 0; k < phase_changes.size(); ++k )
    {
      if ( !spec.phases[ phase_changes[ k ].phase ].intermission )
        continue;
      w.StartObject();
      key( "phase" ); w.Int( phase_changes[ k ].phase );
      key( "start_s" ); w.Double( r3( phase_changes[ k ].at_s ) );
      key( "end_s" );
      if ( k + 1 < phase_changes.size() )
        w.Double( r3( phase_changes[ k + 1 ].at_s ) );
      else
        w.Null();
      w.EndObject();
    }
    w.EndArray();
    key( "downtime_windows" );
    w.StartArray();
    for ( const auto& d : windows )
    {
      w.StartObject();
      key( "type" ); w.String( d.type.c_str() );
      key( "phase" ); w.Int( d.phase );
      key( "start_s" ); w.Double( d.start_s );
      key( "end_s" );
      if ( d.end_s < 0 )
        w.Null();
      else
        w.Double( d.end_s );
      w.EndObject();
    }
    w.EndArray();
    w.EndObject();

    if ( sim->solver_fight_timeline_str.empty() )
      return;
    io::ofstream out;
    out.open( sim->solver_fight_timeline_str,
              sim->current_iteration <= 0 ? ( std::ios::out | std::ios::trunc ) : ( std::ios::out | std::ios::app ) );
    if ( !out.is_open() )
      throw sc_runtime_error( fmt::format( "solver_fight_timeline='{}': cannot open the file for writing.", sim->solver_fight_timeline_str ) );
    out << sb.GetString() << "\n";
  }
};

sheet_fight_event_t::sheet_fight_event_t( sim_t* s, const std::string& spec_path ) : raid_event_t( s, "sheet_fight" )
{
  // The base scheduler never fires this event (it is driven by its own hooks): cooldown = max, no timestamps.
  cooldown.mean = timespan_t::max();
  name = "sheet_fight";

  impl = std::make_unique<impl_t>( *this, s, sheet_fight_spec::load_spec( spec_path ) );
  auto& im = *impl;

  if ( !s->target || s->target->name_str != im.spec.master_actor )
    throw sc_invalid_sim_argument( fmt::format( "solver_sheet_fight='{}': the first enemy= line is '{}', the spec's master_actor is '{}'.",
                                                spec_path, s->target ? s->target->name_str : std::string( "<none>" ), im.spec.master_actor ) );
  im.bosses.resize( im.spec.bosses.size() );
  for ( size_t i = 0; i < im.spec.bosses.size(); ++i )
  {
    auto& b = im.bosses[ i ];
    b.spec = &im.spec.bosses[ i ];
    b.idx = static_cast<int>( i );
    b.actor = s->find_player( b.spec->actor );
    if ( !b.actor || !b.actor->is_enemy() )
      throw sc_invalid_sim_argument( fmt::format( "solver_sheet_fight='{}': boss actor '{}' is not an enemy= line in the profile.", spec_path, b.spec->actor ) );
  }

  // One spawner per wave, owned by the fight master, named "<wave actor> spawner" (the pull system's convention).
  im.wave_rt.resize( im.spec.waves.size() );
  for ( size_t w = 0; w < im.spec.waves.size(); ++w )
  {
    const auto& wv = im.spec.waves[ w ];
    const std::string actor = wv.actor;
    const bool hazard       = wv.kind == "hazard";
    // The flag is set on creation, before the pet is initialised or arises, and never changes afterwards.
    auto* sp = new sheet_spawner_t( actor + " spawner", s->target, [ actor, hazard ]( player_t* owner ) {
      auto* m         = new sheet_mob_t( owner, actor );
      m->sheet_hazard = hazard;
      return m;
    } );
    impl_t* pi = &im;
    sp->set_event_callback( spawner::pet_event_type::DEMISE, [ pi ]( spawner::pet_event_type, sheet_mob_t* m ) { pi->on_mob_demise( m ); } );
    im.wave_rt[ w ].spawner = sp;
    im.wave_rt[ w ].armed.assign( wv.at.size(), 0 );
  }

  // Thresholds each boss needs: every pct > 0 named by a phase start or a wave trigger.
  for ( auto& b : im.bosses )
  {
    std::set<double> pcts;
    for ( const auto& ph : im.spec.phases )
      if ( ph.start.kind == "boss_health" && ph.start.boss == b.spec->actor && ph.start.pct > 0 )
        pcts.insert( ph.start.pct );
    for ( const auto& w : im.spec.waves )
      for ( const auto& a : w.at )
        if ( a.kind == "boss_health" && a.boss == b.spec->actor )
          pcts.insert( a.pct );
    b.pct_values.assign( pcts.begin(), pcts.end() );
  }
  // Registered ONCE per boss (the callback vector only grows, so never per fight): the 1-health zero callback, one
  // percent callback per threshold, and one on-demise callback. Upward crossings (the intermission heal) are ignored
  // inside the callbacks.
  impl_t* p = &im;
  for ( size_t i = 0; i < im.bosses.size(); ++i )
  {
    const int idx = static_cast<int>( i );
    player_t* a = im.bosses[ i ].actor;
    a->register_resource_callback( RESOURCE_HEALTH, 1.0, [ p, idx ]( bool inc ) { p->zero_cb( idx, inc ); }, false, false );
    for ( double pct : im.bosses[ i ].pct_values )
      a->register_resource_callback( RESOURCE_HEALTH, pct, [ p, idx, pct ]( bool inc ) { p->pct_cb( idx, pct, inc ); }, true, false );
    a->callbacks_on_demise.emplace_back( a, [ p, idx ]( player_t* ) { p->death_cb( idx ); } );
  }
  // First player damage / first auto-attack per boss: one assessor on every non-pet, non-enemy player.
  s->register_actor_initializer( INIT_ACTOR_ASSESSORS + 11, [ p ]( player_t* pl ) {
    if ( pl->is_enemy() || pl->is_pet() )
      return;
    pl->assessor_out_damage.add( assessor::TARGET_DAMAGE + 2, [ p ]( result_amount_type, action_state_t* st ) {
      p->on_player_damage( st );
      return assessor::CONTINUE;
    } );
  }, "assessors_sheet_fight" );

  static std::atomic<bool> noticed{ false };
  if ( !noticed.exchange( true ) )
    fmt::print( stderr, "[RL_SHEET_FIGHT] spec={} bosses={} phases={} waves={} jitter={} timeline={}\n", spec_path, im.spec.bosses.size(),
                im.spec.phases.size(), im.spec.waves.size(), s->solver_sheet_jitter ? 1 : 0,
                s->solver_fight_timeline_str.empty() ? std::string( "none" ) : s->solver_fight_timeline_str );
}

sheet_fight_event_t::~sheet_fight_event_t() = default;

void sheet_fight_event_t::add_phase_child( std::unique_ptr<raid_event_t> child )
{
  const int k = child->pull - 1;
  if ( k < 0 || k >= static_cast<int>( impl->children.size() ) )
    throw sc_invalid_sim_argument( fmt::format( "solver_sheet_fight='{}': raid event '{}' has pull={}, the spec has {} phases.",
                                                impl->spec.path, child->type, child->pull, impl->children.size() ) );
  child->sheet_parent = this;
  impl->children[ k ].push_back( std::move( child ) );
}

void sheet_fight_event_t::on_child_start( raid_event_t* child )
{
  impl->child_start( child );
}

void sheet_fight_event_t::on_child_finish( raid_event_t* child )
{
  impl->child_finish( child );
}

void sheet_fight_event_t::reset()
{
  raid_event_t::reset();
  auto& im = *impl;
  for ( auto& ph : im.children )
    for ( auto& c : ph )
      c->reset();
  im.current_phase = -1;
  im.ended_by_kill = false;
  im.active_mismatch_reported = false;
  im.tick_index = 0;
  im.bloodlust_at = -1;
  im.phase_changes.clear();
  im.casts_rec.clear();
  im.insts.clear();
  im.mob_inst.clear();
  im.windows.clear();
  for ( auto& wr : im.wave_rt )
    std::fill( wr.armed.begin(), wr.armed.end(), 0 );
  im.trigger_ready.assign( im.spec.phases.size(), 0 );
  for ( auto& b : im.bosses )
  {
    const auto sleeps = [ & ] {
      for ( const auto& e : im.spec.phases[ 0 ].engage )
        if ( e.boss == b.spec->actor )
          return false;
      return true;
    }();
    b.actor->initial.sleeping = sleeps;
    b.spawned = b.dead = b.benched = b.zero_bench = b.floor_hold = b.vuln_applied = false;
    b.spawn_s = 0;
    b.death_s = b.first_dmg = b.first_pdmg = b.first_swing = -1;
    b.stream_taken = 0;
    b.life_start_s = b.life_dmg_base = b.max_at_spawn = b.stream_rate = 0;
    b.samples.clear();
  }
}

void sheet_fight_event_t::combat_begin()
{
  auto& im = *impl;
  if ( !im.stream )
  {
    im.stream = new sheet_raid_stream_t( sim->target );
    im.stream->init();
    im.stream->on_assessed = [ p = impl.get() ]( action_state_t* st ) { p->on_stream_assessed( st ); };
  }
  // tstl-sylvanas 261-04: every jittered value for this fight is drawn here, once, before the first phase starts.
  im.draw_jitter();
  im.start_phase( 0 );
  // The phase-0 bosses already arose with the other actors; every player re-acquires onto them.
  for ( auto& b : im.bosses )
    if ( b.spawned && !b.actor->is_sleeping() )
      im.retarget_players( retarget_source::ACTOR_ARISE, b.actor );
  make_event<impl_t::tick_event_t>( *sim, *sim, impl.get(), timespan_t::zero() );
  im.try_advance();
}

void sheet_fight_event_t::on_combat_end()
{
  auto& im = *impl;
  im.write_record();
  // Expire every immunity the controller applied BEFORE target demise: an immunity still up would push a dead actor
  // back into the target list (and active_enemies) for the next fight.
  for ( auto& b : im.bosses )
  {
    if ( b.vuln_applied )
    {
      b.actor->debuffs.vulnerable->expire();
      b.vuln_applied = false;
    }
    b.floor_hold = false;
    if ( b.benched )
    {
      b.benched = false;
      b.actor->debuffs.invulnerable->decrement();
    }
    if ( b.spec )
    {
      bool engaged_first = false;
      for ( const auto& e : im.spec.phases[ 0 ].engage )
        engaged_first = engaged_first || e.boss == b.spec->actor;
      if ( !engaged_first )
        b.actor->initial.sleeping = true;
    }
  }
}

std::optional<timespan_t> sheet_fight_event_t::boss_time_to_percent( const player_t* boss, double percent ) const
{
  auto& im = *impl;
  for ( const auto& b : im.bosses )
    if ( b.actor == boss )
      return im.ttd( b, percent );
  return std::nullopt;
}

sheet_fight_forecast_t sheet_fight_event_t::forecast() const
{
  return impl->forecast();
}

// tstl-sylvanas 262-04: the free-function view the observation readers call; all-unknown outside SheetFight.
sheet_fight_forecast_t sheet_fight_forecast( const sim_t* sim )
{
  if ( !sim || sim->fight_style != FIGHT_STYLE_SHEET_FIGHT || !sim->sheet_fight )
    return {};
  return sim->sheet_fight->forecast();
}
