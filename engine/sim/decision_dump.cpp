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

namespace
{
// Defect (c) fix (2026-07-29 fix bundle): a buff with no scheduled expiration
// event (an indefinite/permanent buff, e.g. blessing_of_the_bronze,
// lights_deliverance) reports `buff_t::remains() == timespan_t::min()` --
// SimC's internal "no known expiry" sentinel -- which serializes as a huge
// negative number (timespan_t::min().total_seconds(), ~-9223370000000000.0)
// if dumped raw. Owner-decided wire representation: `remains:null` plus an
// explicit `permanent:true` marker instead. Finite remains are unchanged.
void write_buff_remains( std::ostream& out, timespan_t remains )
{
  if ( remains == timespan_t::min() )
    out << "null,\"permanent\":true";
  else
    out << remains.total_seconds();
}
} // anonymous namespace

// Shared decision-boundary state block -- see decision_dump.hpp. Reused
// verbatim by solver_control's "decision" request line (phase 116,
// simc-offline-evaluation-pipeline) so the two hooks can never drift.
void write_state_fields( std::ostream& out, player_t* p, action_t* chosen, bool solver_reply_gated )
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
    out << ",\"remains\":";
    write_buff_remains( out, buff->remains() );
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
      out << ",\"remains\":";
      write_buff_remains( out, buff->remains() );
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
  // ready, not the total duration) - PLAYER-scoped (2026-07-29 fix bundle,
  // defect (d)): the actor's own current haste-scaled standard GCD
  // (`player_t::base_gcd`/`min_gcd`, ATTACK_HASTE-scaled), matching what the
  // live TSTL context builder's `currentGcdInMs` means
  // (`resources.getCurrentGcdInMs()` reads a live PLAYER-scoped GCD via the
  // Sylvanas API's `core.spell_book.get_global_cooldown()`, not a specific
  // spell's own gcd()). Previously action-scoped (`chosen->gcd()`), which
  // degenerated to 0 whenever `chosen` was the zero-trigger_gcd auto_attack
  // placeholder (the entire solver_control path before the (a) fix, since
  // auto_attack never stopped being "ready") or null (any idle/wait
  // decision) -- no longer reads `chosen` at all.
  {
    timespan_t player_gcd = p->base_gcd * p->cache.attack_haste();
    if ( player_gcd < p->min_gcd )
      player_gcd = p->min_gcd;
    out << ",\"gcd_length\":" << player_gcd.total_seconds();
  }

  // Full auto-attack swing interval (distinct from swing_mh_remains above,
  // which is time-until-next-swing, not the full period) - PLAYER-scoped
  // (2026-07-29 fix bundle, defect (d)): computed directly from the main-hand
  // weapon's swing time and the actor's current auto-attack-speed haste,
  // rather than `attack_t::execute_time()` on the actual swing-timer action
  // (`melee_t` in the paladin module) -- that action class deliberately
  // special-cases its OWN execute_time() to 0 before the first swing has
  // actually landed and to 10ms before combat starts (see
  // `melee_t::execute_time()`, sc_paladin.cpp), which describes "time until
  // the next scheduled swing" (swing_mh_remains already answers that), not
  // "how long is a swing" -- the field this key is documented to mean.
  out << ",\"auto_attack_interval\":"
      << ( p->main_hand_attack ? ( p->main_hand_weapon.swing_time * p->cache.auto_attack_speed() ).total_seconds()
                                : 0.0 );

  // Resolved action identity (116-02; solver-path gating added 2026-07-29 fix
  // bundle, defect (b)) - the SimC-internal name_str of the action actually
  // about to execute at this boundary, unwrapping a sequence/strict_sequence
  // wrapper to its real next sub-action. Authoritative; the `chosen`
  // top-level key emitted by record() below is kept unchanged for backward
  // compatibility with the spike's own artefacts but is unreliable under a
  // sequence-driven run -- see PROTOCOL.md.
  //
  // `solver_reply_gated` (decision_dump::record()'s own call only, never
  // solver_control's wire-request-building call) forces `resolved_action:null`
  // plus an explicit `solver_reply_type` marker whenever the immediately-
  // preceding solver_control reply was NOT a "cast" (i.e. "wait"/"default"/
  // "abstain" -- none of those name a real cast action, so reporting
  // whatever `chosen` happens to resolve to there is exactly the
  // pre-resolution-placeholder bug this fix closes).
  //
  // `resolved_spell_id` (owner-ratified decision chain, step 2, 2026-08-01) -
  // added alongside `resolved_action` because the name alone is not a stable
  // comparison key: SimC renames the action's own `name_str` at construction
  // time based on live talent state, so the STRING is talent-dependent while
  // the spell id carries the real identity (see sc_paladin_retribution.cpp:663-668
  // -- `templars_verdict_t` picks between `find_spell(383328)` and
  // `find_specialization_spell("Templar's Verdict")` (85256) based on
  // `p->talents.final_verdict->ok()`, so `action_t::data().id()` reliably
  // discriminates Templar's Verdict vs Final Verdict even where the name_str
  // does not, or where a future rename changes the string without changing
  // the id). This is exactly why three alias tables exist today purely to
  // paper over the name-string instability: `NAME_STR_RENAME_ALIASES`
  // (solver_control.cpp:107), `RUNTIME_NAME_ALIASES`
  // (scripts/simc-eval/equivalence-check.py:205), and the rename entries in
  // scripts/simc-eval/name-map.json. This commit only EMITS the id field so
  // it can be measured; deciding whether the comparison key should switch to
  // id (and retiring/collapsing those alias tables) is step 3 of the chain,
  // deliberately NOT done here. Emitted in all three exits below (mirroring
  // `resolved_action`'s null in exit 1) so the field is never silently
  // absent -- null wherever `resolved_action` is null, a JSON number
  // (unquoted) wherever it is a string.
  if ( solver_reply_gated && sim->solver_control_last_reply_type != "cast" )
  {
    out << ",\"resolved_action\":null";
    out << ",\"resolved_spell_id\":null";
    out << ",\"solver_reply_type\":\"" << json_escape( sim->solver_control_last_reply_type ) << "\"";
  }
  else
  {
    if ( action_t* resolved = resolve_current_action( chosen ) )
    {
      out << ",\"resolved_action\":\"" << json_escape( resolved->name() ) << "\"";
      out << ",\"resolved_spell_id\":" << resolved->data().id();
    }
    else
    {
      out << ",\"resolved_action\":null";
      out << ",\"resolved_spell_id\":null";
    }
    if ( solver_reply_gated )
      out << ",\"solver_reply_type\":\"" << json_escape( sim->solver_control_last_reply_type ) << "\"";
  }
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

  // solver_reply_gated=true only when solver_control is active for this sim
  // (2026-07-29 fix bundle, defect (b)) -- see write_state_fields' own doc
  // comment above and decision_dump.hpp. `chosen` here is already the
  // POST-solver_control-resolution action: player.cpp's execute_action() now
  // calls solver_control::choose() BEFORE decision_dump::record() (reordered
  // for exactly this fix), so `sim->solver_control_last_reply_type` reflects
  // THIS boundary, not the previous one.
  write_state_fields( out, p, chosen, !sim->solver_control_str.empty() );

  out << "}\n";
  out.flush();
}
} // namespace decision_dump
