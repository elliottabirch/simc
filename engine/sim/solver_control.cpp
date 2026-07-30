// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "solver_control.hpp"

#include "action/action.hpp"
#include "action/attack.hpp"
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
// 2026-07-29 fix bundle: bumped 1 -> 2. Record-schema semantics changed on
// the wire/dump side (defects (b): resolved_action is now solver-reply-gated
// in decision_dump.jsonl; (c): permanent-buff remains sentinel representation
// changed from a raw negative number to null+"permanent":true; (d):
// gcd_length/auto_attack_interval are now player-scoped, not action-scoped) --
// not purely additive, so this is a real version bump, not 116-02's
// backward-compatible one. Both sides of the wire (this constant and
// episode-driver.py's PROTOCOL_VERSION) must move in lockstep.
constexpr int SOLVER_CONTROL_PROTOCOL_VERSION = 2;

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
// Scans p->action_list for the first entry whose name_str matches `name`,
// preferring a player-CASTABLE (non-background) action_t over a same-name
// background one. action_t::action_t() (action.cpp) unconditionally
// self-registers EVERY constructed action into player->action_list --
// including internal `background = true` helper actions class modules build
// for buff-triggering/bookkeeping (e.g. sc_paladin.cpp's
// `active.background_avenging_wrath`, constructed once per talent-init
// regardless of whether the player's own APL/solver catalog also carries a
// real, player-castable `avenging_wrath` actions+= entry) -- so a same-name
// collision is possible whenever a class module ships both a background
// helper and a real spell under identical name_str. A plain first-match
// linear scan can therefore return the background helper (construction
// order is init-sequence-dependent, not alphabetical or list-position
// stable), which is never `ready()` the way solver_control's caller expects
// and stalls the episode. Found live: sc_paladin.cpp:3327 constructs
// `background_avenging_wrath` ahead of the player's real `avenging_wrath`
// action in the action_list, so a `{"type":"cast","action":"avenging_wrath"}`
// reply resolved to the background helper and never advanced. This
// preference is deliberately general (no spell special-case): ANY class
// module with a background/foreground name_str collision hits the same
// fix.
action_t* resolve_action_prefer_castable( player_t* p, const std::string& name )
{
  action_t* background_match = nullptr;
  for ( action_t* a : p->action_list )
  {
    if ( a->name_str != name )
      continue;
    if ( !a->background )
      return a;
    if ( !background_match )
      background_match = a;
  }
  // No player-castable match under this exact name -- fall back to a
  // same-name background action rather than reporting unresolvable, in case
  // some class module legitimately has no foreground counterpart for this
  // token (keeps prior behavior for that case; resolved->ready() downstream
  // still fails closed if it truly can't be cast).
  return background_match;
}

action_t* resolve_action( player_t* p, const std::string& name )
{
  if ( action_t* resolved = resolve_action_prefer_castable( p, name ) )
    return resolved;

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
    if ( action_t* resolved = resolve_action_prefer_castable( p, alias_it->second ) )
      return resolved;
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

  // Defect (a) fix (2026-07-29 fix bundle): in live play, melee auto-attack
  // begins implicitly with combat -- the player never has to "choose" it via
  // a priority list on some turn. Under solver_control, EVERY decision
  // boundary's action is instead replaced wholesale by the driver's reply, so
  // the placeholder `actions+=/auto_attack` entry this pipeline's generated
  // episode .simc always carries is never castable through the normal
  // `apl_choice`/"cast" path -- the solver's own spell catalog has no
  // "auto_attack" spellName to map, so a "cast" reply can never name it, and
  // even a stray "default"/"abstain" reply landing on it would only start the
  // FIRST swing (see `ready()`'s "not swinging" guard below), never repeat.
  // Without this, `p->main_hand_attack->execute_event` never gets scheduled
  // at all: every decision record shows swing_mh_remains:null,
  // auto_attack_interval effectively 0 (the swing-timer `melee_t` action
  // class this fires through short-circuits its own execute_time() to 0 until
  // the FIRST swing has actually landed -- see decision_dump.cpp's
  // player-scoped replacement instead), no white damage ever lands, and no
  // Crusading Strikes energize (which fires off the white-hit, not off a
  // solver-chosen spell) ever triggers. Start it here, once, at the very
  // first decision boundary this hook ever sees for this actor -- mirroring
  // live's implicit-with-combat start using the exact same
  // not-already-swinging guard the paladin's own `auto_melee_attack_t::execute()`
  // uses (sc_paladin.cpp).
  if ( !sim->solver_control_auto_attack_started )
  {
    sim->solver_control_auto_attack_started = true;
    if ( p->main_hand_attack && p->main_hand_attack->execute_event == nullptr )
      p->main_hand_attack->schedule_execute();

    // Wave A acceptance fix (2026-07-29, post-116-04): yield back to the
    // sim's event dispatcher HERE, before soliciting the actor's real first
    // decision, instead of falling through to the synchronous FIFO
    // round-trip below within this same execute_action() call. Evidenced in
    // WAVE-A-ACCEPTANCE.md §1: the reference (vanilla, non-solver_control
    // APL evaluation) flow naturally visits TWO player_t::execute_action()
    // decision boundaries at t=0 -- one for the actor's own separate
    // `actions=auto_attack` priority-list entry, one for the real
    // sequence/plan entry -- and a same-timestamp scheduled raid event (e.g.
    // Bloodlust at pull) gets processed by the sim's event queue in between
    // them, so the SECOND boundary already sees it. Without a yield here,
    // solver_control started auto-attack and solicited the real first
    // decision inline, in the SAME boundary, with no chance for that
    // same-tick event to land first, so the actor's real first decision was
    // made against a NOT-YET-updated buff/haste state, and every decision
    // after it inherited the resulting bifurcated GCD/cooldown timeline.
    //
    // Reuse the existing zero-second "pending wait" mechanism (the same one
    // a `{"type":"wait","sec":0}` reply already drives via
    // player_ready_event_t::execute()'s `solver_control_has_pending_wait`
    // branch, player.cpp) rather than adding a new wire message: this
    // schedules a fresh zero-delay Player-Ready event and returns, WITHOUT
    // ever sending a request over the FIFO for this boundary -- the driver
    // never sees it at all, so decision/wait counts on the wire are
    // unaffected. That new event is appended to the sim's event queue AFTER
    // any same-timestamp events already pending (event_manager_t::add_event
    // orders same-timestamp entries by ascending insertion id), so it
    // reproduces the reference flow's own two-boundary yield structurally,
    // not by special-casing Bloodlust or any specific raid event/profile.
    sim->solver_control_pending_wait_s = 0.0;
    sim->solver_control_has_pending_wait = true;
    return nullptr;
  }

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
  // boundary's own action anchor, needed at the time for the then-
  // action-scoped gcd_length/resolved_action fields; `apl_choice` is the
  // correct anchor here since the driver's reply hasn't resolved the actual
  // cast yet at request-build time). 2026-07-29 fix bundle, defect (d):
  // gcd_length/auto_attack_interval are now PLAYER-scoped and no longer read
  // this anchor at all; `apl_choice` still anchors `resolved_action` here.
  // `solver_reply_gated` defaults false at this call site -- the wire
  // request's own `resolved_action` stays pre-reply/`apl_choice`-based,
  // exactly as PROTOCOL.md documents (the reply hasn't been read yet); only
  // decision_dump::record()'s own call opts into reply-gating (defect (b)).
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

  // Defect (b) fix (2026-07-29 fix bundle): classify this decision boundary's
  // reply BEFORE returning, so decision_dump::record() (called right after
  // this function returns -- player.cpp's execute_action() was reordered for
  // exactly this) can report the SOLVER's actual per-boundary resolution
  // instead of the pre-reply APL/sequence placeholder pick.
  sim->solver_control_last_reply_type = type;

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
