// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// Per-fight cumulative proc-roll observers for the RL transition log
// (tstl-sylvanas quick task 260917-pcn, "GPL rung 3"). PURE OBSERVERS:
// nothing in this file touches the RNG or control flow -- every format-9
// translog row copies these running per-fight totals so a window's counts
// are consecutive-row differences, exactly like `damage`.

#pragma once

#include <cstddef>
#include <cstdint>

struct player_t;

namespace rl_proc
{

enum class id : std::uint8_t
{
  crit_hit = 0, auto_attack_hit, windfury, unruly_winds, stormsurge, stormflurry, thunder_capacitor,
  maelstrom_weapon_gain, elemental_assault, fire_nova_vb, awakening_storms_roll, awakening_storms_rppm,
  tempest_deck, asc_dw_deck, storm_unleashed_deck, flurry_trigger, dbc_proc_callback, buff_chance_trigger,
  spare_18, spare_19, COUNT
};

inline constexpr std::uint32_t COUNT = 20u;
static_assert( static_cast<std::uint32_t>( id::COUNT ) == COUNT, "rl_proc::COUNT must match the enum" );

inline constexpr const char* NAMES[ COUNT ] = {
  "crit_hit", "auto_attack_hit", "windfury", "unruly_winds", "stormsurge", "stormflurry", "thunder_capacitor",
  "maelstrom_weapon_gain", "elemental_assault", "fire_nova_vb", "awakening_storms_roll", "awakening_storms_rppm",
  "tempest_deck", "asc_dw_deck", "storm_unleashed_deck", "flurry_trigger", "dbc_proc_callback", "buff_chance_trigger",
  "spare_18", "spare_19" };

// Struct-of-arrays so a translog row can memcpy each array into a flat, numpy-friendly block.
struct counters_t
{
  std::uint16_t attempts[ COUNT ];    // rolls attempted (saturating at 65535)
  std::uint16_t successes[ COUNT ];   // rolls that succeeded (saturating)
  float chance_sum[ COUNT ];          // sum over attempts of the chance rolled against, clamped to [0, 1]
};
static_assert( sizeof( counters_t ) == 8u * COUNT, "counters_t must be exactly 8 bytes per mechanic" );

inline void reset( counters_t& c )
{
  for ( std::uint32_t i = 0; i < COUNT; ++i ) { c.attempts[ i ] = 0; c.successes[ i ] = 0; c.chance_sum[ i ] = 0.0f; }
}

} // namespace rl_proc

// Defined in engine/sim/rl_translog.cpp (needs player_t/pet_t complete). Routes a pet's roll to its owner
// (mirrors stats.cpp's solver_damage_so_far pet->owner rule), ignores enemies and a null player.
void rl_count_proc( player_t* p, rl_proc::id which, double chance, bool success );
