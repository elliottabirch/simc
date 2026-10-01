// ==========================================================================
// tstl-sylvanas 261-02: the sheet-fight controller (fight_style=SheetFight).
//
// A "sheet fight" is a boss fight played from one JSON spec file (sheet-fight-spec/1, written by the
// repository's sheet-to-profile writer and named by the sim option solver_sheet_fight=<path>). The
// controller is a raid_event_t (the engine's own kind of scheduled fight event) so it gets the per-fight
// reset/combat_begin hooks and the tagged random-draw scope for free; it is never scheduled by the base
// class (cooldown = max). It owns: the spec loader, phases that change on a boss's actual health, the
// bench-at-1-health mechanism, the one-second raid-damage stream, the fight end on the last must-die
// death, and the per-fight fork-fight/1 record (solver_fight_timeline=<file.jsonl>).
//
// Nothing here names a boss, add, hazard, NPC id or encounter id: every name and number comes from the spec.
// ==========================================================================

#pragma once

#include "config.hpp"

#include "sim/raid_event.hpp"
#include "util/timespan.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct player_t;
struct sim_t;

namespace sheet_fight_spec
{
struct jitter_t
{
  std::string key;
  double min = 0, max = 0;
  std::string per;  // "fight" or "phase"
};
struct boss_t
{
  std::string actor, name;
  long long npc_game_id = 0;
  double max_health = 0;
  bool must_die = false;
};
struct start_t
{
  std::string kind;  // pull | boss_health | after_cast
  std::string boss;
  double pct = 0;
  std::string cast;
  double after_s = 0;
  std::string after_jitter;
};
struct engage_t
{
  std::string boss;
  double delay_s = 0;
  std::string delay_jitter;
  std::string on_zero;  // bench | die
};
struct stream_t
{
  std::string boss;
  double damage_per_s = 0;
  std::string pace_jitter;
};
struct damage_taken_t
{
  std::string boss;
  double multiplier = 1;
};
struct heal_t
{
  std::vector<std::string> bosses;
  double pct_of_max_per_tick = 0, tick_interval_s = 0, first_tick_after_start_s = 0;
  long long ticks = 0;
};
struct cast_t
{
  std::string name;
  long long ability_id = 0;
  std::string caster;
  double at_in_phase_s = 0;
};
struct bloodlust_t
{
  double offset_after_start_s = 0;
  std::string offset_jitter;
};
struct phase_t
{
  std::string name;
  bool intermission = false;
  start_t start;
  std::vector<engage_t> engage;
  std::vector<std::pair<std::string, double>> max_health;
  std::vector<stream_t> raid_stream;
  std::vector<damage_taken_t> damage_taken;
  std::optional<heal_t> heal;
  std::vector<cast_t> casts;
  std::optional<bloodlust_t> bloodlust;
};
struct wave_at_t
{
  std::string kind;  // phase_time | boss_health
  int phase = 0;
  double in_phase_s = 0;
  std::string boss;
  double pct = 0;
};
struct wave_stream_t
{
  double damage_per_s = 0, start_after_spawn_s = 0;
  std::string lifetime_jitter;
};
struct wave_t
{
  std::string name, actor, kind;  // kind: add | hazard
  long long npc_game_id = 0, count = 0;
  std::vector<wave_at_t> at;
  std::string time_shift_jitter;
  std::optional<double> max_health, travel_s, lifetime_s;
  bool must_die = false;
  double spawn_distance_yd = 0;
  std::string travel_jitter;
  std::optional<wave_stream_t> raid_stream;
};
struct spec_t
{
  std::string slug, sheet_fingerprint, fragment_path, master_actor, path;
  long long encounter_id = 0;
  double max_time_s = 0, sample_interval_s = 1;
  std::vector<boss_t> bosses;
  std::vector<phase_t> phases;
  std::vector<wave_t> waves;
  std::vector<jitter_t> jitter;
};

// Reads and validates the spec; throws sc_invalid_sim_argument "solver_sheet_fight='<path>': <json path>: <message>"
// on the first fault (or "...: cannot open the spec file.").
spec_t load_spec( const std::string& path );
}  // namespace sheet_fight_spec

struct sheet_fight_event_t : public raid_event_t
{
  sheet_fight_event_t( sim_t* sim, const std::string& spec_path );
  ~sheet_fight_event_t() override;

  // A raid event written with pull=N becomes a child of phase N-1 (begun when the phase starts, deactivated when
  // the next one starts). Throws on N out of range.
  void add_phase_child( std::unique_ptr<raid_event_t> child );

  // Called by an adopted child's start() / finish() (raid_event_t::sheet_parent): records downtime windows.
  void on_child_start( raid_event_t* child );
  void on_child_finish( raid_event_t* child );

  void reset() override;
  void combat_begin() override;
  // Called from sim_t::combat_end() BEFORE target demise: writes the fight record, expires every immunity the
  // controller applied.
  void on_combat_end();
  // Per-life time to a health percent for a spec boss that is up; nullopt for any other actor.
  std::optional<timespan_t> boss_time_to_percent( const player_t* boss, double percent ) const;

private:
  void _start() override {}
  void _finish() override {}

  struct impl_t;
  std::unique_ptr<impl_t> impl;
};
