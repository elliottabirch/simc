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
// Both go into the `.apl` sidecar (format 3, see rl_translog.hpp; format 2 until 261025-bk4) beside the net's own press, so a reader can
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

struct action_t;
struct player_t;
struct sim_t;

namespace rl_stock_eval
{

// Flag bits of the `.apl` format-3 record (contract AG-1, format 2 until 261025-bk4).
inline constexpr std::uint8_t F_REFUSED         = 1;   // a random roll would have been needed: no pick
inline constexpr std::uint8_t F_STRICT_NO_GCD   = 2;   // the strict leaf triggers no global cooldown
inline constexpr std::uint8_t F_LIST_SWITCH     = 4;   // the walk went through a run_action_list / swap_action_list
inline constexpr std::uint8_t F_VARIABLE_OP     = 8;   // a variable line whose operation is not a plain set ran
inline constexpr std::uint8_t F_DEPTH           = 16;  // the recursion depth limit was hit
inline constexpr std::uint8_t F_NAME_TRUNCATED  = 32;  // the strict leaf's name is 24 bytes or longer (format 3; 48 in format 2)
// Bit 64 is RETIRED: the retired play-stock mode (commit 0080c3dd1f) used it, nothing writes it any more, and the readers refuse it.
inline constexpr std::uint8_t F_IDLE            = 128; // observer mode (solver_policy_observe_stock=1): this row is the one row of an idle stretch

// Sim option storage (`solver_apl_stock_list=<name>`). A global because a sim_t member would rebuild every file;
// the RL rigs run one sim per process. Default empty: the module is off.
std::string& list_name_storage();

// True when the option names a list AND solver_record_apl_choice is on.
bool enabled( const sim_t* sim );

// Observer mode (261021-ezq), the option checks that need only option values. Called from sim_t::setup() before the stock-list
// refusals, so a missing option is reported under its own name. Does nothing unless solver_policy_observe_stock=1. Throws
// sc_runtime_error starting `OBSERVE_STOCK_NEEDS:` when solver_policy=, solver_apl_stock_list=, solver_record_apl_choice=1 or
// rl_translog= is missing, and starting `OBSERVE_STOCK_CONFLICT:` when solver_control=, solver_hold_windows=,
// solver_force_decision=, solver_min_maelstrom_spend > 0, solver_mask_no_enemy=1 or a branch, teacher or re-salt mode is set.
void validate_observer_options( sim_t* sim );

// Table player (261025-bk4, solver_table_policy=; rl_table_policy.hpp). Called from sim_t::setup() after validate_observer_options, once the
// solver_policy= blob is loaded. Does nothing unless solver_table_policy= is set (and refuses a stray solver_table_* option without it). Throws
// sc_runtime_error starting `TABLE_NEEDS:` when solver_policy=, solver_apl_stock_list=, solver_record_apl_choice=1 or rl_translog= is missing or
// the blob carries an aim head, and starting `TABLE_CONFLICT:` when solver_control=, solver_policy_observe_stock=1, solver_hold_windows=,
// solver_force_decision=, solver_min_maelstrom_spend > 0, solver_mask_no_enemy=1, or a branch, teacher or re-salt mode is set; then loads
// the file (`TABLE_FILE:` refusals).
void validate_table_options( sim_t* sim );

// The button index of an action: the registry's cast button whose token equals the action's engine name, else -1. The name match the
// dry walk uses for its leaves; observer mode uses it on the leaf SimC's own select_action returned.
int button_index( const action_t* a );

// Called once from sim_t::init, after every actor's init_finished(). When enabled, throws sc_runtime_error (text
// starting `rl_stock_eval: registry '<id>' list '<name>': `) when there is no RL actor, when the name is `default`
// or `precombat`, when the actor declares no such list, or when the actor's base skill is not 1. In observer mode it also
// throws `OBSERVE_STOCK_NEEDS:` unless the RL actor's active list is the list named by solver_apl_stock_list= (the profile must
// carry action_list=<L>). Stores no pointer: every decision resolves the list by name, so nothing outlives a sim.
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
