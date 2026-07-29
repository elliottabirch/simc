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

#include <string>

struct player_t;
struct action_t;

namespace decision_dump
{
// Called once per player_t::execute_action() invocation, right after the
// APL/sequence has chosen (or failed to choose) an action, before that
// action executes. `chosen` is nullptr on an idle/wait decision.
void record( player_t* p, action_t* chosen );
}
