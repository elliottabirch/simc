// ==========================================================================
// tstl-sylvanas quick task 261014-bufx (class A1): the engine buff table.
//
// Plain English. The observation inputs for "buffs on me" and "auras I put on an enemy" used to be chosen by a person. The
// discovery rule (scripts/rl/buff_discovery.py) takes them from the simulator instead, and this module is the half that only
// the simulator can answer: for the RL actor, every buff of its own buff list, and every buff or dot an enemy holds whose
// source is the actor, each with its engine name, spell id (the id the JSON report prints), maximum stacks, stack behaviour,
// base duration, the buff's own cooldown, its internal cooldown, its procs-per-minute object and its default chance.
//
// The table format is described once, in scripts/rl/buff_discovery.py (docstring, "THE ENGINE TABLE"); this writer and that
// reader share that description. A file is written whole with `schemaVersion`, `actor` and `buffs`, rows sorted by
// (holder, kind, name), identical rows of different enemies merged into one.
//
// 261028-fzl: in a fight list (sim_t::rl_list_active) the file is JSON Lines: one line per entry at its combat end, {schemaVersion 2, kind "buff",
// entry, crc32, signature (sha256 of the table bytes), table (once per distinct signature in the process, else null), reach} where reach maps
// "<holder>/<kind>/<name>" to {startCount, triggered, applied} for every row with any of them set. The table's rows carry no reach flags then.
//
// Sim hooks (src: sim.cpp). No-ops unless the sim option rl_buff_census=<path> is set. The module only READS: it never calls
// up() (that mutates benefit bookkeeping), never triggers, expires or rolls anything, and allocates nothing on the decision
// path (the two hooks run once per fight, at init-finished and at combat end). It changes no decision, no random draw and no
// damage. Enemies are walked from the simulator's actor list, not from its non-sleeping list, because every enemy has demised
// by the combat-end hook.
//
// 290-C4: beside the buff rows the file also carries three top-level arrays that a buff row cannot say about itself, for the same actor: `callbacks`
// (the item/enchant proc callbacks and the buffs they apply), `procObjects` (the actor's rolled-chance objects) and `cooldowns` (its cooldowns and who
// holds each). The proc-buff selector (scripts/rl/proc_buff_selector.py) reads them; the schema number is unchanged (the keys are additive).
// ==========================================================================

#pragma once

#include <string>

struct sim_t;

namespace rl_buff_census
{

std::string& path_storage();
void on_init_finished( sim_t* sim );
void on_combat_end( sim_t* sim );

}  // namespace rl_buff_census
