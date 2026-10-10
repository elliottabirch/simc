// ==========================================================================
// tstl-sylvanas Phase 271.3, plan 271.3-01 (BIND-01, BIND-02).
//
// BUTTONS BIND BY IDENTITY, NEVER BY NAME.
//
// Words used here. A "button" is a registry action of kind cast. The "catalog" is the list of lines the
// registry hands the episode composer: one bare token, or one `<base>,name=<value>` line, per button. The
// "default list" is the actor's top-level action list (`actions=` / `actions+=/`). A "decision-maker" is an
// attached net or client (`solver_policy=` in-process, `solver_control=` through the pipe).
//
// What this module does. At sim init it fixes, once, which engine action object each button means: the one
// action whose action list is the actor's default list and whose recorded line declares the button's token.
// The line declares a token when the line is exactly the token, or exactly `<base>,name=<value>` with
// `<base>_<value>` equal to the token. The match is by list pointer and recorded line only. A name is never
// compared, so a hand-written line of the same name in any other list can never be a button, whatever order
// the lists were built in.
//
// With a decision-maker attached, a bind that fails (no line declares a button, two lines declare it, a
// default-list line is not part of the catalog, a bound action carries an APL option, or a foreground
// same-name action elsewhere shadows the button) aborts sim init with a message naming the registry id, the
// actor, the token, the list and the offending line. Without a decision-maker the same failures mark the
// actor unbound and never throw, so scripted stock-list runs keep working; an unbound actor resolves every
// button to null.
//
// `rl_bind_report=<path>` writes the bind report (format `rl-bind-report-v1`) once per sim init, before any
// refusal is thrown.
//
// The module only READS actions. It changes no decision, no random draw and no damage.
// ==========================================================================

#pragma once

#include <cstddef>
#include <string>

struct action_t;
struct player_t;
struct sim_t;

namespace rl_button_bind
{

// Sim option storage (the census module's shape: a sim_t member would rebuild every file). Default empty:
// no report is written.
std::string& report_path_storage();

// Called once from sim_t::init, after every actor's init_finished(). Fills the bound table, writes the
// report when its path is set, and throws sc_runtime_error when a decision-maker is attached and the bind
// failed.
void on_init_finished( sim_t* sim );

// The action bound to header action `action_index` (an index into RL_ACTIONS), or nullptr: the index is not
// a cast, the actor is not the registry's actor, or the actor is unbound.
action_t* bound_action( const player_t* p, std::size_t action_index );

// The action bound to the cast token `token`, or nullptr (a token outside the header resolves to null).
// This is the ONE resolver every button lookup reads.
action_t* bound_action_for_token( const player_t* p, const std::string& token );

// True when every button of `p` binds under the rules above.
bool is_bound( const player_t* p );

// True when `line` declares `token`: the line is exactly the token, or exactly one base name, one comma,
// `name=` and a value with no further comma, and `<base>_<value>` equals the token.
bool line_declares( const std::string& line, const char* token );

// Empties the bound table (the fight-list driver clears every process-wide cache between entries).
void clear();

// How many actors the bound table holds right now.
std::size_t entries();

}  // namespace rl_button_bind
