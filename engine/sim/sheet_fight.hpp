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
  std::optional<double> nominal_lifetime_s;  // tstl-sylvanas 262-04: an add's nominal lifetime (the writer's R-5 centre); null for a hazard
  bool must_die = false;
  double spawn_distance_yd = 0;
  std::string travel_jitter;
  std::optional<wave_stream_t> raid_stream;
};
// tstl-sylvanas 265-03: sheet-fight-spec/2 (Phase 265 D-08..D-11). A random mechanic is something a boss may do to the raid
// that lands on the player only some of the time; the controller plays it itself (never as a phase child), so the
// fight inputs the net reads do not change. A "declared cast" is one entry of a mechanic's casts list.
struct rm_cast_t
{
  int phase = 0;
  std::string at_kind;  // phase_time | boss_health | after_cast
  double in_phase_s = 0;
  std::string boss;
  double pct = 0;
  std::string cast;
  double after_s = 0;
  std::optional<std::pair<double, double>> time_jitter;  // a shift in seconds added to the time (min, max)
};
struct random_mechanic_t
{
  std::string name;
  long long ability_id = 0;
  std::string caster;
  double chance = 0;
  std::string category;  // forced_movement | stun | other_realm
  double duration_s = 0;
  std::optional<double> distance_yd;  // forced_movement only; recorded, the effect is timed by duration_s
  std::vector<rm_cast_t> casts;
};
// A priority mark names an enemy (a boss or a wave actor) and when it is "the one to hit". The controller only REPORTS
// the windows (record key priority_windows); nothing acts on them in Phase 265.
struct priority_mark_t
{
  std::string name, enemy, while_kind, rank;  // while_kind: alive | absorb_on_target; rank: top | high | normal | ignore
  bool funnel_all_damage = false;
};
struct spec_t
{
  int format = 1;  // tstl-sylvanas 265-03: 1 = sheet-fight-spec/1 (today's code path), 2 = sheet-fight-spec/2
  std::string slug, sheet_fingerprint, fragment_path, master_actor, path;
  long long encounter_id = 0;
  double max_time_s = 0, sample_interval_s = 1;
  std::vector<boss_t> bosses;
  std::vector<phase_t> phases;
  std::vector<wave_t> waves;
  std::vector<jitter_t> jitter;
  std::vector<random_mechanic_t> random_mechanics;  // format 2 only
  std::vector<priority_mark_t> priority_marks;      // format 2 only
};

// Reads and validates the spec; throws sc_invalid_sim_argument "solver_sheet_fight='<path>': <json path>: <message>"
// on the first fault (or "...: cannot open the spec file.").
spec_t load_spec( const std::string& path );
}  // namespace sheet_fight_spec

// tstl-sylvanas 265-03: the per-player start and end of a stun (defined in raid_event.cpp). Both the engine's own stun raid
// event and the sheet-fight controller's random stun call these, so the 262-09 turn hand-back (the player gets its next
// decision the moment a stun ends in a sheet fight) lives in one function body. Every non-sheet fight runs the stock
// behaviour byte for byte.
void sheet_fight_stun_start( player_t* p );
void sheet_fight_stun_end( sim_t* sim, player_t* p );
// tstl-sylvanas 265-03: a forced movement of `duration_s` for the given players (the stock movement event's per-player start;
// defined in raid_event.cpp). The vector must outlive the movement.
void sheet_fight_movement_begin( sim_t* sim, const std::vector<player_t*>& players, double duration_s );

// tstl-sylvanas 262-04: the read-only view of the fight the net is allowed to see (the fight.* inputs).
// Every field is a thing the game shows a raider (a wave of adds announced or arrived, a boss health percent, a
// scheduled downtime, a bloodlust that is coming) or the sheet's NOMINAL value; no drawn value that the game hides
// (the real travel time, add lifetime or bloodlust offset) is ever read. `*_known` false means "nothing to
// forecast"; the readers then write their no-event values.
struct sheet_fight_forecast_t
{
  bool wave_known = false;
  double wave_arrival_in_s = 0;  // drawn spawn time + the sheet's NOMINAL travel, minus now
  int wave_count = 0;
  double wave_lifetime_s = 0;  // the sheet's nominal lifetime of that wave's adds
  bool phase_known = false;
  double phase_at_boss_pct = 0;   // the health percent at which the next phase starts
  double phase_boss_pct_now = 0;  // that boss's health percent now
  bool downtime_active = false;
  bool downtime_known = false;
  double downtime_in_s = 0;  // seconds to the next scheduled downtime start (0 while one is in progress)
  bool bloodlust_known = false;
  double bloodlust_in_s = 0;  // anchor phase start + the sheet's NOMINAL offset, minus now (floored at 0)
};

// The all-unknown view when the fight style is not SheetFight or no controller exists.
sheet_fight_forecast_t sheet_fight_forecast( const sim_t* sim );

// tstl-sylvanas 265-03: which priority marks (sheet-fight-spec/2 priority_marks) are active right now, one entry per mark and
// per spawned instance of the mark's enemy (a boss has the one instance 0; a wave actor has one per spawn this fight, in spawn
// order). A mark of kind "alive" is active from the enemy's spawn to its death; "absorb_on_target" while a raid-event absorb
// buff is up on the enemy. A PURE READ: no draw, no event, no state change. NO CONSUMER EXISTS IN PHASE 265: the controller
// only reports the windows in the fight record (priority_windows); the priority-target system that will read this is deferred.
struct sheet_fight_mark_state_t
{
  int mark = 0;      // index into the spec's priority_marks
  int instance = 0;  // which copy of the enemy actor, from 0
  bool active = false;
};
// Empty when the fight style is not SheetFight or no controller exists.
std::vector<sheet_fight_mark_state_t> sheet_fight_priority_marks( const sim_t* sim );

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
  // tstl-sylvanas 262-04: the fight.* view (see sheet_fight_forecast_t); a pure read, no draw, no state change.
  sheet_fight_forecast_t forecast() const;
  // tstl-sylvanas 265-03: see sheet_fight_mark_state_t; a pure read, no consumer in Phase 265.
  std::vector<sheet_fight_mark_state_t> priority_marks_now() const;

  // tstl-sylvanas 265-03: stun bookkeeping for a sheet-fight-spec/2 fight (see sheet_fight_stun_start in raid_event.cpp). Every stun,
  // the engine's own and the controller's, goes through the shared start and end helpers; the controller counts how many hold
  // each player so overlapping stuns do not release each other. Not used (tracks_stuns() false) for a /1 spec.
  bool tracks_stuns() const;
  void stun_claim_add( const player_t* p );
  // Drops one claim; true when none is left (the stunned buff may now be lowered), also true when no claim was recorded.
  bool stun_claim_release( const player_t* p );

private:
  void _start() override {}
  void _finish() override {}

  struct impl_t;
  std::unique_ptr<impl_t> impl;
};
