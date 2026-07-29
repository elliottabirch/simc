// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// P2 spike hook (simc-solver-spike-2026-07-29, R2 state-sync data generator).
// One JSON line per actor decision boundary (player_t::execute_action()),
// dumped BEFORE the chosen action mutates any state (cost/cooldown/GCD) -
// this is the same "read state, then decide" moment the ret solver's own
// snapshot(ctx) reads at. Enabled via `decision_dump=<file>`; a no-op
// (single empty-string check) when the option is unset.

#pragma once

#include "config.hpp"

#include <ostream>
#include <string>
#include <string_view>

struct player_t;
struct action_t;

namespace decision_dump
{
// Called once per player_t::execute_action() invocation, right after the
// APL/sequence has chosen (or failed to choose) an action, before that
// action executes. `chosen` is nullptr on an idle/wait decision.
void record( player_t* p, action_t* chosen );

// Shared state-block emitter (P3b solver_control hook, simc-offline-
// evaluation-pipeline phase 116, reuses this so the two decision-boundary
// hooks never drift). Writes gcd_remains, swing_mh_remains, holy_power,
// cooldowns, buffs, target_debuffs and target_time_to_die as trailing JSON
// object members (each preceded by its own comma) -- the caller owns the
// enclosing object's opening `{` and closing `}`.
void write_state_fields( std::ostream& out, player_t* p );

// JSON helpers shared with solver_control (P3b) so both hooks emit
// byte-identical escaping/clamping for the same field kinds.
std::string json_escape( std::string_view s );
double clamp_nonneg( double v );
}
