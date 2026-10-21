// ==========================================================================
// tstl-sylvanas Phase 271.4, plan 271.4-03 (AGREE-01).
//
// THE DRY STOCK-ROTATION PICK: what would the hand-written list press here, in this exact state?
//
// Words used here. The "net" is the attached decision-maker (`solver_policy=`, the in-process arm of
// solver_control::choose). The "stock list" is the preserved copy of the hand-written rotation that the
// episode composer appends to the profile as a named action list (`actions.<name>=...`); the composer names it
// `stock_default`. A "leaf" is a ready line that is neither a variable line nor a call/run/swap of another
// list, that is, a line that would actually press something. A leaf is "mapped" when the registry has a cast
// button of the same name (RL_ACTIONS[ i ].kind == rl_action_kind::cast and the token equals the leaf's name).
//
// What this module does. With `solver_apl_stock_list=<name>` and `solver_record_apl_choice=1`, at every decision
// of the RL actor (once, at the top of the in-process arm, before the observation is read, before the aim pick
// and before every exploration draw) it walks the named list ONCE and reports two answers:
//   - the strict pick: the first ready leaf, mapped or not (its button index, or -2 when it has no button, and
//     its engine name);
//   - the mapped pick: the first ready leaf the registry has a button for (its button index, or -1 when none).
// Both go into the `.apl` sidecar (format 2, see rl_translog.hpp) beside the net's own press, so a reader can
// lay the net's choice next to the list's choice in the SAME state.
//
// Same state, nothing runs. The walk is a line-for-line mirror of player_t::select_action WITHOUT calling it:
// select_action marks lists as visited, may draw the player's random stream, runs ready variable lines for
// keeps and marks the reporting flag `used`. This walk draws nothing, marks nothing, starts no cooldown and
// executes no action. The one thing it runs is a ready `variable` line (the list's later conditions read the
// variable), and every actor variable is saved before the walk and restored after it, so the engine's values
// are exactly what they were. A variable operation that is not a plain `set` (add, sub, ...) would not be
// faithful under save-and-restore, so the walk raises a flag when it runs one and the write-up can say how many
// decisions that touched.
//
// Refuse, never roll. A list marked `random`, a line whose own skill is not 1, or an actor whose current skill is
// not 1 (or whose skill debuff is not 0) would make the real engine draw a random number. The walk sets the
// refused flag and returns no pick instead.
//
// Control lines steer the walk and are never a pick (G271.4-6): `call_action_list` is walked and the parent
// continues if it found nothing; `run_action_list` and `swap_action_list` are walked WITHOUT being run and the
// parent does not continue (what the real engine does on its next pass, when the active list has become the
// sub-list).
//
// Off path. When the option is off, `enabled()` is false, nothing here is called, and a run is byte-identical to
// one built before this module existed (receipts R1, R1b).
// ==========================================================================

#pragma once

#include <cstdint>
#include <string>

#include "sc_enums.hpp"

struct player_t;
struct sim_t;

namespace rl_stock_eval
{

// Flag bits of the `.apl` format-2 record (contract AG-1).
inline constexpr std::uint8_t F_REFUSED         = 1;   // a random roll would have been needed: no pick
inline constexpr std::uint8_t F_STRICT_NO_GCD   = 2;   // the strict leaf triggers no global cooldown
inline constexpr std::uint8_t F_LIST_SWITCH     = 4;   // the walk went through a run_action_list / swap_action_list
inline constexpr std::uint8_t F_VARIABLE_OP     = 8;   // a variable line whose operation is not a plain set ran
inline constexpr std::uint8_t F_DEPTH           = 16;  // the recursion depth limit was hit
inline constexpr std::uint8_t F_NAME_TRUNCATED  = 32;  // the strict leaf's name is 48 bytes or longer
inline constexpr std::uint8_t F_PLAYED          = 64;  // solver_policy_play_stock=1 pressed this record's mapped pick

// Sim option storage (`solver_apl_stock_list=<name>`). A global because a sim_t member would rebuild every file;
// the RL rigs run one sim per process. Default empty: the module is off.
std::string& list_name_storage();

// True when the option names a list AND solver_record_apl_choice is on.
bool enabled( const sim_t* sim );

// Called once from sim_t::init, after every actor's init_finished(). When enabled, throws sc_runtime_error (text
// starting `rl_stock_eval: registry '<id>' list '<name>': `) when there is no RL actor, when the name is `default`
// or `precombat`, when the actor declares no such list, or when the actor's base skill is not 1. Stores no pointer:
// every decision resolves the list by name, so nothing outlives a sim.
void on_init_finished( sim_t* sim );

// One decision's answer.
struct pick_t
{
  std::int16_t mapped_index = -1;  // button index of the first ready mapped leaf, -1 none
  std::int16_t strict_index = -1;  // first ready leaf: its button index, -1 none ready, -2 it has no button
  std::uint8_t boundary     = 0;   // 0 foreground, 1 off-global-cooldown poll, 2 cast-while-casting poll
  std::uint8_t flags        = 0;   // F_* bits
  std::string  strict_name;        // the strict leaf's engine name, empty when none
};

// The dry walk (see the top of this file). Reads the actor and its lists; changes nothing that outlives the call.
pick_t evaluate( player_t* p, execute_type et );

}  // namespace rl_stock_eval
