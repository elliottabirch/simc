// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "solver_control.hpp"

#include "action/action.hpp"
#include "decision_dump.hpp"
#include "player/player.hpp"
#include "sim/event.hpp"
#include "sim/sim.hpp"
#include "util/io.hpp"

#include "rapidjson/document.h"

#include "fmt/format.h"

#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace
{
constexpr int SOLVER_CONTROL_PROTOCOL_VERSION = 1;

[[noreturn]] void protocol_abort( const std::string& msg )
{
  fmt::print( stderr, "solver_control: FATAL: {}\n", msg );
  throw std::runtime_error( "solver_control protocol violation: " + msg );
}

// Resolves a SimC internal action name (`name_str`) to one of the actor's
// already-constructed action_t objects. Never constructs a new action --
// solver_control only selects among what the actor's action list already
// built at init time (every solver-catalog spell has an unconditional
// actions+= entry in the generated episode .simc for exactly this reason).
action_t* resolve_action( player_t* p, const std::string& name )
{
  for ( action_t* a : p->action_list )
  {
    if ( a->name_str == name )
      return a;
  }

  // Fallback for spells whose *constructed* action_t renames its own
  // name_str based on live talent state, even though the APL/sequence
  // CONSTRUCTOR token stays fixed (found live during 116-01 smoke testing:
  // sc_paladin_retribution.cpp's templars_verdict_t sets name_str to
  // "final_verdict" once the Final Verdict talent is active, even though
  // "templars_verdict" remains the only registered create_action() factory
  // token -- there is no "final_verdict" branch in create_action at all).
  // scripts/simc-eval/name-map.json intentionally emits ONLY the canonical
  // constructor-token name ("templars_verdict", never "final_verdict" --
  // writing the display name as a sequence entry fails SILENTLY, per that
  // file's own landmine note), so this alias is tried only when the exact
  // match above fails, keeping the driver/worker/name-map unaware of the
  // live rename.
  static const std::unordered_map<std::string, std::string> NAME_STR_RENAME_ALIASES = {
    { "templars_verdict", "final_verdict" },
  };
  auto alias_it = NAME_STR_RENAME_ALIASES.find( name );
  if ( alias_it != NAME_STR_RENAME_ALIASES.end() )
  {
    for ( action_t* a : p->action_list )
    {
      if ( a->name_str == alias_it->second )
        return a;
    }
  }

  return nullptr;
}
} // anonymous namespace

namespace solver_control
{
action_t* choose( player_t* p, action_t* apl_choice )
{
  sim_t* sim = p->sim;
  if ( sim->solver_control_str.empty() )
    return apl_choice;

  // Lazy-open, mirroring decision_dump's own convention. Pairing order
  // matters: the engine opens the request stream (.out, write) BEFORE the
  // reply stream (.in, read); the driver opens them in the mirrored order
  // (.out read, then .in write) so the two blocking FIFO opens pair up
  // without either side deadlocking on the other's data (standard
  // bidirectional named-pipe handshake).
  if ( !sim->solver_control_req_stream )
  {
    sim->solver_control_req_stream = std::make_unique<io::ofstream>();
    sim->solver_control_req_stream->open( sim->solver_control_str + ".out" );
    sim->solver_control_rep_stream = std::make_unique<std::ifstream>( sim->solver_control_str + ".in" );
    if ( !sim->solver_control_req_stream->is_open() || !sim->solver_control_rep_stream->is_open() )
      protocol_abort( "could not open solver_control FIFO pair at '" + sim->solver_control_str + "'" );
  }

  const uint64_t seq = ++sim->solver_control_seq;

  io::ofstream& req = *sim->solver_control_req_stream;
  req << "{\"v\":" << SOLVER_CONTROL_PROTOCOL_VERSION;
  req << ",\"type\":\"decision\"";
  req << ",\"seq\":" << seq;
  req << ",\"t\":" << sim->current_time().total_seconds();
  req << ",\"actor\":\"" << decision_dump::json_escape( p->name() ) << "\"";
  req << ",\"apl_choice\":"
      << ( apl_choice ? ( "\"" + decision_dump::json_escape( apl_choice->name() ) + "\"" ) : std::string( "null" ) );
  // gcd_remains, swing_mh_remains, holy_power, cooldowns, buffs,
  // target_debuffs, target_time_to_die, active_enemies, dots, gcd_length,
  // auto_attack_interval, resolved_action -- identical to decision_dump's
  // own per-decision line, factored into one shared emitter so the two
  // hooks can never drift (116-02: signature widened to take the
  // boundary's own action anchor, needed for the action-scoped
  // gcd_length/resolved_action fields; `apl_choice` is the correct anchor
  // here since the driver's reply hasn't resolved the actual cast yet at
  // request-build time).
  decision_dump::write_state_fields( req, p, apl_choice );
  req << "}\n";
  req.flush();

  std::string line;
  if ( !std::getline( *sim->solver_control_rep_stream, line ) )
    protocol_abort( "EOF or read failure on solver_control reply stream (seq=" + std::to_string( seq ) + ")" );

  rapidjson::Document doc;
  if ( doc.Parse( line.c_str() ).HasParseError() || !doc.IsObject() )
    protocol_abort( "malformed reply JSON (seq=" + std::to_string( seq ) + "): " + line );

  if ( !doc.HasMember( "v" ) || !doc["v"].IsInt() || doc["v"].GetInt() != SOLVER_CONTROL_PROTOCOL_VERSION )
    protocol_abort( "reply 'v' mismatch or missing (seq=" + std::to_string( seq ) + "): " + line );

  if ( !doc.HasMember( "seq" ) || !doc["seq"].IsUint64() || doc["seq"].GetUint64() != seq )
    protocol_abort( "reply 'seq' mismatch (expected " + std::to_string( seq ) + "): " + line );

  if ( !doc.HasMember( "type" ) || !doc["type"].IsString() )
    protocol_abort( "reply missing 'type': " + line );

  const std::string type = doc["type"].GetString();

  if ( type == "cast" )
  {
    if ( !doc.HasMember( "action" ) || !doc["action"].IsString() )
      protocol_abort( "'cast' reply missing 'action' (seq=" + std::to_string( seq ) + "): " + line );
    const std::string action_name = doc["action"].GetString();
    action_t* resolved = resolve_action( p, action_name );
    if ( !resolved )
      protocol_abort( "'cast' reply named an unresolvable action '" + action_name + "' (seq=" +
                       std::to_string( seq ) + ")" );
    if ( !resolved->ready() )
      protocol_abort( "'cast' reply named a not-ready action '" + action_name + "' (seq=" +
                       std::to_string( seq ) + ")" );
    return resolved;
  }

  if ( type == "wait" )
  {
    if ( !doc.HasMember( "sec" ) || !doc["sec"].IsNumber() )
      protocol_abort( "'wait' reply missing numeric 'sec' (seq=" + std::to_string( seq ) + "): " + line );
    double sec = doc["sec"].GetDouble();
    if ( sec < 0.0 )
      sec = 0.0;
    // Nothing executes this boundary. The actual re-schedule happens in
    // player_ready_event_t::execute()'s existing "nothing chosen" branch
    // (player.cpp), which checks this pending-wait pair before falling back
    // to the default poll/threshold-based idle scheduling -- see that call
    // site for why this can't be scheduled directly from inside choose()
    // (player_t::schedule_ready() throws if `readying` is already
    // non-null, and it is not yet cleared at this point in the call stack).
    sim->solver_control_pending_wait_s = sec;
    sim->solver_control_has_pending_wait = true;
    return nullptr;
  }

  if ( type == "default" || type == "abstain" )
  {
    // Execute the APL's own choice unchanged -- abstain is handled
    // identically to default per the protocol contract.
    return apl_choice;
  }

  protocol_abort( "unknown reply 'type': " + type );
  return apl_choice; // unreachable -- protocol_abort always throws
}

void finish( sim_t* sim )
{
  if ( !sim->solver_control_req_stream )
    return;

  if ( sim->solver_control_req_stream->is_open() )
  {
    io::ofstream& req = *sim->solver_control_req_stream;
    req << "{\"v\":" << SOLVER_CONTROL_PROTOCOL_VERSION << ",\"type\":\"bye\"}\n";
    req.flush();
    req.close();
  }

  if ( sim->solver_control_rep_stream && sim->solver_control_rep_stream->is_open() )
    sim->solver_control_rep_stream->close();
}
} // namespace solver_control
