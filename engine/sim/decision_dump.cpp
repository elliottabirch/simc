// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "decision_dump.hpp"

#include "action/action.hpp"
#include "action/attack.hpp"
#include "action/dot.hpp"
#include "action/sequence.hpp"
#include "buff/buff.hpp"
#include "player/player.hpp"
#include "sim/cooldown.hpp"
#include "sim/event.hpp"
#include "sim/sim.hpp"
#include "util/io.hpp"

namespace
{
// Resolves the actual sub-action about to execute at this decision boundary,
// unwrapping a sequence_t/strict_sequence_t wrapper action to its real next
// sub-action rather than the wrapper's own generic name_str ("default"/
// "strict_sequence") -- the exact gotcha P3-findings.md found under the
// spike's own `sequence`-driven replay, where `chosen` collapsed to the
// sequence wrapper's name ("plan") on every decision (116-02). Both
// sequence_t and strict_sequence_t track their next-to-execute sub-action
// index in a public `current_action` member which is only advanced inside
// their own schedule_execute() -- called AFTER this decision boundary -- so
// reading it here always names the sub-action about to fire, never one
// already committed. Returns nullptr only when `a` itself is null (idle/wait
// decision).
action_t* resolve_current_action( action_t* a )
{
  if ( !a )
    return nullptr;

  if ( auto* strict = dynamic_cast<strict_sequence_t*>( a ) )
  {
    if ( strict->current_action < strict->sub_actions.size() )
      return strict->sub_actions[ strict->current_action ];
    return a;
  }

  if ( auto* seq = dynamic_cast<sequence_t*>( a ) )
  {
    if ( seq->current_action >= 0 && static_cast<size_t>( seq->current_action ) < seq->sub_actions.size() )
      return seq->sub_actions[ seq->current_action ];
    return a;
  }

  return a;
}
} // anonymous namespace

namespace decision_dump
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

// Shared decision-boundary state block -- see decision_dump.hpp. Reused
// verbatim by solver_control's "decision" request line (phase 116,
// simc-offline-evaluation-pipeline) so the two hooks can never drift.
void write_state_fields( std::ostream& out, player_t* p, action_t* chosen )
{
  sim_t* sim = p->sim;

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

  // active_enemies - mirrors sim_t's own "active_enemies" APL-expression
  // semantics exactly (sim.cpp's expression-creation branch): a single-
  // target fight with no adds/pull raid events collapses to the constant 1,
  // otherwise the live sim_t::active_enemies counter (116-02, previously
  // unwired -- P2-state-mapping.md §5).
  {
    int active_enemies = 1;
    if ( !( sim->target_list.size() == 1U && !sim->has_raid_event( "adds" ) && !sim->has_raid_event( "pull" ) ) )
      active_enemies = sim->active_enemies;
    out << ",\"active_enemies\":" << active_enemies;
  }

  // DoT family - name-keyed like buffs, but "ticking" (not stack>0) is the
  // active predicate since dot_t has no stack-based inactive convention.
  // Filtered to this actor's own DoTs on its current target, mirroring the
  // target_debuffs source==p convention above. This is what supplies the
  // Expurgation family (116-02, previously unwired -- P2-state-mapping.md
  // §5): consumers read `dots.expurgation.{ticking,remains}`.
  out << ",\"dots\":{";
  if ( p->target )
  {
    bool first_dot = true;
    for ( dot_t* dot : p->target->dot_list )
    {
      if ( dot->source != p || !dot->is_ticking() )
        continue;
      if ( !first_dot )
        out << ",";
      first_dot = false;
      out << "\"" << json_escape( dot->name() ) << "\":{";
      out << "\"ticking\":" << ( dot->is_ticking() ? "true" : "false" );
      out << ",\"remains\":" << dot->remains().total_seconds();
      out << "}";
    }
  }
  out << "}";

  // Full GCD length (distinct from gcd_remains above, which is time-until-
  // ready, not the total duration) - action-scoped, so only available when
  // a real chosen action exists; 0 on an idle/wait decision (116-02,
  // previously unwired -- P2-state-mapping.md §5).
  out << ",\"gcd_length\":" << ( chosen ? chosen->gcd().total_seconds() : 0.0 );

  // Full auto-attack swing interval (distinct from swing_mh_remains above,
  // which is time-until-next-swing, not the full period) (116-02,
  // previously unwired -- P2-state-mapping.md §5).
  out << ",\"auto_attack_interval\":"
      << ( p->main_hand_attack ? p->main_hand_attack->execute_time().total_seconds() : 0.0 );

  // Resolved action identity (116-02) - the SimC-internal name_str of the
  // action actually about to execute at this boundary, unwrapping a
  // sequence/strict_sequence wrapper to its real next sub-action.
  // Authoritative; the `chosen` top-level key emitted by record() below is
  // kept unchanged for backward compatibility with the spike's own
  // artefacts but is unreliable under a sequence-driven run -- see
  // PROTOCOL.md.
  if ( action_t* resolved = resolve_current_action( chosen ) )
    out << ",\"resolved_action\":\"" << json_escape( resolved->name() ) << "\"";
  else
    out << ",\"resolved_action\":null";
}

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

  write_state_fields( out, p, chosen );

  out << "}\n";
  out.flush();
}
} // namespace decision_dump
