// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "decision_dump.hpp"

#include "action/action.hpp"
#include "action/attack.hpp"
#include "buff/buff.hpp"
#include "player/player.hpp"
#include "sim/cooldown.hpp"
#include "sim/event.hpp"
#include "sim/sim.hpp"
#include "util/io.hpp"

namespace
{
// Minimal JSON-string escaping - decision-boundary identifiers (action/buff/
// cooldown name_str) are simc internal snake_case tokens; the only
// real-world tokens seen with punctuation are spell display names, which
// this dump does not emit (name_str, not spell_name()), so this covers the
// one practical case (a stray quote) defensively rather than exhaustively.
std::string json_escape( std::string_view s )
{
  std::string out;
  out.reserve( s.size() );
  for ( char c : s )
  {
    if ( c == '"' || c == '\\' )
      out.push_back( '\\' );
    out.push_back( c );
  }
  return out;
}

double clamp_nonneg( double v )
{
  return v < 0.0 ? 0.0 : v;
}
} // anonymous namespace

namespace decision_dump
{
void record( player_t* p, action_t* chosen )
{
  sim_t* sim = p->sim;
  if ( sim->decision_dump_file_str.empty() )
    return;

  if ( !sim->decision_dump_stream )
  {
    sim->decision_dump_stream = std::make_unique<io::ofstream>();
    sim->decision_dump_stream->open( sim->decision_dump_file_str );
  }
  if ( !sim->decision_dump_stream->is_open() )
    return;

  io::ofstream& out = *sim->decision_dump_stream;

  out << "{";
  out << "\"t\":" << sim->current_time().total_seconds();
  out << ",\"actor\":\"" << json_escape( p->name() ) << "\"";
  out << ",\"chosen\":" << ( chosen ? ( "\"" + json_escape( chosen->name() ) + "\"" ) : std::string( "null" ) );

  // GCD remaining - same formula as action.cpp's gcd_remains_expr_t, read
  // directly since we have no action anchor when `chosen` is null (idle/wait
  // decision).
  out << ",\"gcd_remains\":" << clamp_nonneg( ( p->gcd_ready - sim->current_time() ).total_seconds() );

  // Swing timer (main-hand only - ret paladin has no meaningful OH state).
  if ( p->main_hand_attack && p->main_hand_attack->execute_event )
    out << ",\"swing_mh_remains\":" << p->main_hand_attack->execute_event->remains().total_seconds();
  else
    out << ",\"swing_mh_remains\":null";

  // Resources - holy_power is the only resource the ret solver models;
  // dumped by enum value directly rather than iterating RESOURCE_MAX to
  // keep the line small and the field self-describing for the spike.
  out << ",\"holy_power\":" << p->resources.current[ RESOURCE_HOLY_POWER ];

  // Cooldowns - every named cooldown with a nonzero base recharge (skips
  // the zero-duration bookkeeping cooldowns SimC creates internally).
  out << ",\"cooldowns\":{";
  bool first_cd = true;
  for ( cooldown_t* cd : p->cooldown_list )
  {
    if ( cd->duration <= timespan_t::zero() && cd->base_duration <= timespan_t::zero() )
      continue;
    if ( !first_cd )
      out << ",";
    first_cd = false;
    out << "\"" << json_escape( cd->name_str ) << "\":{";
    out << "\"remains\":" << cd->remains().total_seconds();
    out << ",\"charges\":" << cd->current_charge;
    out << ",\"max_charges\":" << cd->charges;
    out << ",\"recharge_time\":" << ( cd->recharge_event ? cd->recharge_event->remains().total_seconds() : 0.0 );
    out << "}";
  }
  out << "}";

  // Buffs on the actor - active (stack > 0) only, to keep the line focused
  // on what a decision boundary actually needs to read.
  out << ",\"buffs\":{";
  bool first_buff = true;
  for ( buff_t* buff : p->buff_list )
  {
    if ( buff->check() <= 0 )
      continue;
    if ( !first_buff )
      out << ",";
    first_buff = false;
    out << "\"" << json_escape( buff->name() ) << "\":{";
    out << "\"stacks\":" << buff->check();
    out << ",\"remains\":" << buff->remains().total_seconds();
    out << "}";
  }
  out << "}";

  // Target debuffs - same active-only convention, read off the actor's
  // current target (single-target Patchwerk profile - see spike plan §7).
  out << ",\"target_debuffs\":{";
  if ( p->target )
  {
    bool first_debuff = true;
    for ( buff_t* buff : p->target->buff_list )
    {
      if ( buff->source != p || buff->check() <= 0 )
        continue;
      if ( !first_debuff )
        out << ",";
      first_debuff = false;
      out << "\"" << json_escape( buff->name() ) << "\":{";
      out << "\"stacks\":" << buff->check();
      out << ",\"remains\":" << buff->remains().total_seconds();
      out << "}";
    }
    out << "},\"target_time_to_die\":" << p->target->time_to_percent( 0 ).total_seconds();
  }
  else
  {
    out << "},\"target_time_to_die\":null";
  }

  out << "}\n";
  out.flush();
}
} // namespace decision_dump
