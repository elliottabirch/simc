// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// 261001-bac stage 0 (research clone only, never in production): per-hit buff ledger.
//
// rl_buff_ledger=<path> (empty = off, the default) writes a JSON-lines file, one record per
// line, key `k` names the kind. The record contract (every kind the whole stage writes, with
// fields and units) is `LEDGER-FORMAT.md` beside the quick-task plans in the tstl-sylvanas
// repo (.planning/quick/261001-bac-buff-applier-credit-in-the-engine/). This header and the
// .cpp implement the skeleton: option state, writer, per-fight records, the ledger's own PRESS
// numbering (see open_press) and the per-hit sink.
//
// PURE OBSERVER. Nothing here touches the RNG, schedules or cancels an event, changes an
// action's result, or is read by any routing or rolling decision. With the option off every
// call site is a single `sim->rl_bl_on` bool test and these functions are never entered.
//
// Press numbering (the one design finding that forces this module to own a number): at
// 2bcc19760d the decision sequence number only advances when the net or the FIFO client is in
// control (`++sim->solver_control_seq`), so in a stock-rotation fight EVERY cast carries seq 0
// and the existing cause stamp (seq, class) cannot tell one press from another. The ledger
// therefore adds its own `press` (and `launch`, used from plan 05 on) to rl_cause_t / the
// action state's stamp: -1 whenever the switch is off, never read by anything but the ledger.
#pragma once

#include "sim/rl_credit.hpp"

#include <cstdint>

struct sim_t;
struct player_t;
struct action_t;
struct action_state_t;

namespace rl_buff_ledger
{
// Opens the output file (root sim only; refusals were already made by the caller, sim_t::setup)
// and writes the `hdr` record. Sets up the ledger state on the root sim.
void open_and_write_header( sim_t* sim );

// Fight begin (sim_t::reset() beside the roll recorder's call): resets the per-fight counters
// and starts the fight's record buffer with an `fb` record.
void fight_begin( sim_t* sim );

// Fight end (sim_t::combat_end() beside the roll recorder's call): appends the `fe` record
// (the RL actor's six realised and six expected credit streams, which include its pets) and
// flushes the fight's buffered records to disk.
void fight_end( sim_t* sim );

// Run end (sim_t::execute() beside the roll recorder's call): writes the `ftr` record carrying
// `"complete": true` and every counter the stage uses, then closes the file.
void write_footer( sim_t* sim );

// Assigns the next press number for a CAST-class cause that does not carry one yet, writes a
// `pr` record, and returns the number (stored by rl_cause_scope_t's constructor in the pushed
// frame's cause). A pet's CAST-class frame does NOT open a press: it adopts the latest press
// opened by a non-pet (a pet's own cast is credited to the current decision by the existing
// cause model, not a button choice of its own) and is counted as `foreign_press`.
std::int16_t open_press( player_t* p, const rl_cause_t& cause );

// One `hit` record per damage hit or tick of the RL actor and its pets, called from
// action_t::accrue_expected_damage. realised = state->result_amount (exactly what
// stats_t::add_result routes); `expected_amount` is the value accrue just computed, or 0 with
// `exp_excl` set for a Windfury-occurrence hit (priced separately, see the `xp` record).
void hit_sink( action_t* a, action_state_t* state, double expected_amount, bool exp_excl );

// A hit of an action that has no stats object routes neither realised nor expected credit; the
// footer counts them (`no_stats_hits`).
void note_no_stats_hit( action_t* a );

// While set, rl_credit_route's expected-side calls are accrue_expected_damage's own (already
// written as `hit` records) and are not written again as `xp`.
void set_in_hit_sink( sim_t* sim, bool on );

// Expected-only pricing outside hits (the Windfury occurrence price), called from
// rl_credit_route on every expected-side route not made by accrue_expected_damage.
void xp_record( player_t* p, const rl_cause_t& cause, double amount, const char* action_name );
}  // namespace rl_buff_ledger
