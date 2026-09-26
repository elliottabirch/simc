// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// 260925-e1e hit ledger (owner-approved separate debug checkout only; see
// this task's brief). ONE guarded debug print at the two places the fork
// already computes both a REAL damage/expected-damage pair for the same
// event, so a downstream reader can pair every real hit with its own
// expected booking exactly, instead of E1d's "roughly 15 events per
// decision window" approximation.
//
// Call sites (both already existed; neither is created by this task):
//   1. action_t::accrue_expected_damage (action.cpp) -- runs immediately
//      after action_t::record_data for the SAME action_state_t, for every
//      damage-dealing hit/tick in the sim. record() below is called from
//      there with the real amount (state->result_amount) and the expected
//      amount that function already computed (expected_amount) side by
//      side, before either value is used for anything else.
//   2. shaman_t::trigger_windfury_weapon (sc_shaman.cpp) -- the ONE place
//      a Windfury/Unruly-Winds OCCURRENCE is priced into the expected
//      total (accrue_expected_damage skips windfury_mh's own hits via
//      is_windfury_occurrence, by design -- see that function's doc
//      comment). record_windfury_occurrence() below logs that pricing as
//      its own pseudo-event.
//
// No-behaviour-change argument: every field this header reads (action
// name, player/pet identity, background flag, target identity/health,
// result type, result_amount, the already-computed expected_amount, sim
// clock) is a value the caller already computed for its own purposes --
// this header calls no RNG (rng().*), and only WRITES a local
// std::unique_ptr<io::ofstream>/mutex_t pair that live on the sim_t and are
// never read by anything else in the fork. With rl_hit_ledger= unset
// (default), every function below is a single string::empty() check and an
// immediate return -- one branch, no allocation, no I/O -- byte-identical
// to having no hook at all. This mirrors decision_dump.cpp's own
// established shape (sim/decision_dump.hpp) with one deliberate
// difference: decision_dump's write_state_fields() calls into
// rl_policy_obs's shared_action_leaf_cache, which has a proven history of
// mutating target_cache/target_list as a side effect (see
// decision_dump.cpp's own record() doc comment, "a WRITE into engine state
// from what is supposed to be a read-only diagnostic"). This header never
// calls into that path -- it only reads action_t/action_state_t/player_t
// fields the caller already had in hand.

#pragma once

#include "config.hpp"

#include "action/action.hpp"
#include "action/action_state.hpp"
#include "player/player.hpp"
#include "sim/decision_dump.hpp"
#include "sim/sim.hpp"
#include "util/concurrency.hpp"
#include "util/io.hpp"
#include "util/util.hpp"

#include <sstream>
#include <string>

namespace rl_hit_ledger
{
// Shared line-writer: both record() and record_windfury_occurrence() below
// build a line and hand it here. Lazy-open + mutex, identical pattern to
// decision_dump::record()'s own root-stream handling (decision_dump.cpp),
// so N worker sim_t's sharing one non-empty rl_hit_ledger_str never each
// open (out|trunc) against the same path and clobber one another.
inline void write_line( sim_t* sim, const std::string& line )
{
  sim_t* root = sim;
  while ( root->parent )
    root = root->parent;

  auto_lock_t lock( root->rl_hit_ledger_mutex );
  if ( !root->rl_hit_ledger_stream )
  {
    root->rl_hit_ledger_stream = std::make_unique<io::ofstream>();
    root->rl_hit_ledger_stream->open( root->rl_hit_ledger_str );
  }
  if ( !root->rl_hit_ledger_stream->is_open() )
    return;

  *root->rl_hit_ledger_stream << line;
  root->rl_hit_ledger_stream->flush();
}

// action_t::accrue_expected_damage's call site (action.cpp). `state` is the
// exact action_state_t record_data() just wrote from (real side); `a` is
// the action that owns it; `expected_amount` is the value
// accrue_expected_damage already computed (or 0.0 + excluded=true for a
// windfury_mh occurrence hit, whose own expected pricing is booked
// separately by record_windfury_occurrence() below -- see
// action_t::is_windfury_occurrence's doc comment in action.cpp).
inline void record( action_t* a, action_state_t* state, double expected_amount, bool excluded_from_expected )
{
  sim_t* sim = a->sim;
  if ( sim->rl_hit_ledger_str.empty() )
    return;

  player_t* p = a->player;
  bool is_pet = p->is_pet();

  // source classification per this task's brief: player / pet / proc.
  // `background` is the fork's own established flag for a non-foreground
  // action (DoT ticks, procs, secondary/triggered abilities) -- the same
  // predicate action_t itself and every existing report already use to
  // separate "the agent chose this" from "this fired as a side effect".
  const char* source = is_pet ? "pet" : ( a->background ? "proc" : "player" );

  // Trigger tag: the action's own name already encodes this (e.g.
  // "windfury_mh", "flametongue"), surfaced here as its own field per the
  // brief's explicit request rather than left for a downstream parser to
  // infer from the name string.
  std::string name = a->name();
  const char* trigger = "";
  if ( name.find( "windfury" ) != std::string::npos )
    trigger = "windfury";
  else if ( name.find( "flametongue" ) != std::string::npos )
    trigger = "flametongue";

  // Target health BEFORE this hit. player_t::do_damage() (player.cpp) has
  // already run by the time assess_damage() reaches record_data/
  // accrue_expected_damage (it runs earlier in the same
  // assessor_out_damage pipeline, at TARGET_DAMAGE priority) and reduces
  // health by exactly state->result_amount when result_amount > 0 -- so
  // adding that same amount back to the CURRENT (post-loss) health
  // reconstructs the pre-hit value exactly, with no separate snapshot
  // needed and no new field threaded through action_state_t. This is
  // exact, not approximate, under the sim's single-threaded serial event
  // processing (nothing else can change this target's health between
  // do_damage() and this call for the same event).
  double health_after = state->target->resources.current[ RESOURCE_HEALTH ];
  double health_before = health_after;
  if ( state->result_amount > 0.0 )
    health_before = health_after + state->result_amount;

  // 260926-e1g (Q2 tool 2): the .attr sidecar's own own-credit classification
  // (translog.CREDIT_NAMES: own_cast/tail_cast/own_dot/tail_dot/background/
  // orphan) is exactly rl_credit_route's switch over (cause_class, cause_seq
  // vs now_seq) -- rl_translog.cpp:83-97. Logging the three raw inputs to
  // that switch (never re-deriving the routed index in C++, so this can
  // never drift from the real routing) lets a downstream reader classify
  // every hit-ledger row the SAME way production training's own credit
  // streams are classified, with no window-matching or decision-delta
  // approximation (E1d/E1b step3's own limitation) -- this row already IS
  // the one event. `n_targets` is action_state_t's own pre-existing,
  // side-effect-free snapshot of how many targets THIS execution hit
  // (action_state.hpp), not re-derived from target_list().
  const auto cause_seq = state->rl_cause_seq;
  const auto cause_class = state->rl_cause_class;

  std::ostringstream line;
  line << "{";
  line << "\"t\":" << sim->current_time().total_seconds();
  line << ",\"iteration\":" << sim->current_iteration;
  line << ",\"actor\":\"" << decision_dump::json_escape( p->name() ) << "\"";
  line << ",\"source\":\"" << source << "\"";
  line << ",\"action\":\"" << decision_dump::json_escape( name ) << "\"";
  line << ",\"trigger\":\"" << trigger << "\"";
  line << ",\"target\":\"" << decision_dump::json_escape( state->target->name() ) << "\"";
  line << ",\"result\":\"" << util::result_type_string( state->result ) << "\"";
  line << ",\"real\":" << state->result_amount;
  line << ",\"expected\":" << expected_amount;
  line << ",\"excluded_from_expected\":" << ( excluded_from_expected ? "true" : "false" );
  line << ",\"target_health_before\":" << health_before;
  line << ",\"target_health_after\":" << health_after;
  line << ",\"cause_seq\":" << cause_seq;
  line << ",\"cause_class\":" << static_cast<int>( cause_class );
  line << ",\"now_seq\":" << sim->solver_control_seq;
  line << ",\"n_targets\":" << state->n_targets;
  line << "}\n";

  write_line( sim, line.str() );
}

// shaman_t::trigger_windfury_weapon's call site (sc_shaman.cpp). This is
// NOT a real hit -- it is the fork's own EXPECTED-side pricing of "this
// Windfury/Unruly-Winds occurrence roll", booked once whether or not the
// roll(s) below it succeed. Logged as its own pseudo-event (real=0, since
// the corresponding real damage -- if the roll(s) succeed -- shows up as
// separate windfury_mh `action` rows from record() above, each with its
// own `excluded_from_expected:true`) so a downstream reader can reconcile
// "every expected booking" against "every real hit" per the brief.
inline void record_windfury_occurrence( player_t* p, const action_state_t* trigger_state, double wf_expected_amount )
{
  sim_t* sim = p->sim;
  if ( sim->rl_hit_ledger_str.empty() )
    return;

  // 260926-e1g (Q2 tool 2): shaman_t::trigger_windfury_weapon's own
  // rl_credit_route call (sc_shaman.cpp) routes this occurrence price under
  // `{trigger_state->rl_cause_seq, trigger_state->rl_cause_class}` VERBATIM
  // -- the triggering hit's OWN already-stamped cause, no re-promotion at
  // this site -- so logging those same two fields here is exactly what the
  // real routing call used, not a re-derivation.
  const auto cause_seq = trigger_state->rl_cause_seq;
  const auto cause_class = trigger_state->rl_cause_class;

  std::ostringstream line;
  line << "{";
  line << "\"t\":" << sim->current_time().total_seconds();
  line << ",\"iteration\":" << sim->current_iteration;
  line << ",\"actor\":\"" << decision_dump::json_escape( p->name() ) << "\"";
  line << ",\"source\":\"proc\"";
  line << ",\"action\":\"windfury_occurrence\"";
  line << ",\"trigger\":\"windfury\"";
  line << ",\"target\":\"" << decision_dump::json_escape( trigger_state->target->name() ) << "\"";
  line << ",\"result\":\"n/a\"";
  line << ",\"real\":0";
  line << ",\"expected\":" << wf_expected_amount;
  line << ",\"excluded_from_expected\":false";
  line << ",\"target_health_before\":" << trigger_state->target->resources.current[ RESOURCE_HEALTH ];
  line << ",\"target_health_after\":" << trigger_state->target->resources.current[ RESOURCE_HEALTH ];
  line << ",\"cause_seq\":" << cause_seq;
  line << ",\"cause_class\":" << static_cast<int>( cause_class );
  line << ",\"now_seq\":" << sim->solver_control_seq;
  line << ",\"n_targets\":" << trigger_state->n_targets;
  line << "}\n";

  write_line( sim, line.str() );
}
}  // namespace rl_hit_ledger
