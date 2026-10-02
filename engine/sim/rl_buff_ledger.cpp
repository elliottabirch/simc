// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// 261001-bac stage 0 (research clone only): per-hit buff ledger -- see rl_buff_ledger.hpp for
// the design statement and the no-behaviour-change argument. The record contract is
// LEDGER-FORMAT.md in the tstl-sylvanas repo's quick-task 261001-bac directory.

#include "sim/rl_buff_ledger.hpp"

#include "action/action.hpp"
#include "action/action_state.hpp"
#include "player/pet.hpp"
#include "player/player.hpp"
#include "sim/decision_dump.hpp"
#include "sim/sim.hpp"
#include "util/io.hpp"
#include "util/util.hpp"

#include "fmt/format.h"

#include <array>
#include <cmath>
#include <iterator>
#include <string>

namespace rl_buff_ledger
{
namespace
{
constexpr int FORMAT_VERSION = 1;
constexpr std::size_t PASS_HIST_SIZE = 64;

// The record kinds this build writes (the `hdr` record's `emits` list). stage0_checks.py reads
// it to decide which identities it can assert.
constexpr const char* EMITS_JSON = "[\"hdr\",\"fb\",\"fe\",\"ftr\"]";
}  // namespace

// The ledger's state, defined here so sim.hpp never sees it (sim.hpp forward-declares it).
struct state_t
{
  io::ofstream out;
  std::string path;

  // The current fight's buffered records; flushed at fight end only. Cleared at fight begin, so a
  // trailing fight_begin() (sim_t's final reset() after the last iteration) is never written.
  std::string fight_buf;
  bool in_fight = false;
  bool in_hit_sink = false;

  player_t* actor = nullptr;  // the one non-pet, non-enemy player; its pets' hits are included

  // Per-fight
  std::int64_t next_press = 0;
  std::int64_t last_press = -1;
  std::uint64_t next_hit = 1;
  std::uint64_t fight_hits = 0;
  std::uint64_t fight_presses = 0;
  std::uint64_t fight_xp = 0;

  // Per-run
  std::uint64_t run_fights = 0;
  std::uint64_t run_collected_fights = 0;
  std::uint64_t run_hits = 0;
  std::uint64_t run_presses = 0;
  std::uint64_t run_xp = 0;

  // Footer counters. Every counter the stage will use is declared now (all 0 until the plan that
  // owns it lands), so the footer never changes shape.
  std::uint64_t lost_press = 0;
  std::uint64_t stale_hit_id = 0;
  std::uint64_t reference_mismatch = 0;
  std::uint64_t restoring_mismatch = 0;
  std::uint64_t premade_drift = 0;
  std::uint64_t unsafe_hits = 0;
  std::uint64_t passes_total = 0;
  std::array<std::uint64_t, PASS_HIST_SIZE> pass_hist{};
  std::uint64_t swing_rescaled = 0;
  std::uint64_t foreign_press = 0;
  std::uint64_t cache_hits = 0;
  std::uint64_t cache_misses = 0;
  std::uint64_t cache_check = 0;
  std::uint64_t cache_check_fail = 0;
  std::uint64_t no_stats_hits = 0;
};

namespace
{
sim_t* root_of( sim_t* sim )
{
  sim_t* root = sim;
  while ( root->parent )
    root = root->parent;
  return root;
}

state_t* state_of( sim_t* sim )
{
  return root_of( sim )->rl_bl_state.get();
}

using out_it = std::back_insert_iterator<std::string>;

void put_double( std::string& b, double v )
{
  if ( std::isfinite( v ) )
    fmt::format_to( out_it( b ), "{:.17g}", v );
  else
    b += "null";
}

void put_string( std::string& b, std::string_view s )
{
  b += '"';
  b += decision_dump::json_escape( s );
  b += '"';
}

void flush_fight( state_t& s )
{
  s.out << s.fight_buf;
  s.out.flush();
  s.fight_buf.clear();
}
}  // namespace

void open_and_write_header( sim_t* sim )
{
  sim_t* root = root_of( sim );
  auto st = std::make_shared<state_t>();
  st->path = root->rl_buff_ledger_str;
  st->out.open( st->path );
  if ( !st->out.is_open() )
  {
    throw sc_runtime_error( fmt::format( "rl_buff_ledger=: cannot open '{}' for writing.", st->path ) );
  }
  std::string b;
  b += "{\"k\":\"hdr\",\"format\":\"rl_buff_ledger\",\"version\":";
  fmt::format_to( out_it( b ), "{}", FORMAT_VERSION );
  fmt::format_to( out_it( b ), ",\"seed\":{},\"iterations\":{},\"threads\":{}", root->seed, root->iterations,
                  root->threads );
  b += ",\"options\":{";
  b += "\"rl_buff_ledger\":";
  put_string( b, st->path );
  b += "},\"emits\":";
  b += EMITS_JSON;
  b += "}\n";
  st->out << b;
  st->out.flush();
  root->rl_bl_state = std::move( st );
}

void fight_begin( sim_t* sim )
{
  state_t* s = state_of( sim );
  if ( s == nullptr )
    return;

  // The actor: the one non-pet, non-enemy player. Resolved per fight (actors exist from reset on).
  if ( sim->player_no_pet_list.size() != 1 )
  {
    throw sc_runtime_error( fmt::format(
        "rl_buff_ledger= describes one actor's fight; this sim has {} non-pet players.",
        sim->player_no_pet_list.size() ) );
  }
  s->actor = sim->player_no_pet_list[ 0 ];

  s->fight_buf.clear();
  s->in_fight = true;
  s->in_hit_sink = false;
  s->next_press = 0;
  s->last_press = -1;
  s->next_hit = 1;
  s->fight_hits = 0;
  s->fight_presses = 0;
  s->fight_xp = 0;

  std::string& b = s->fight_buf;
  fmt::format_to( out_it( b ), "{{\"k\":\"fb\",\"it\":{},\"t\":", sim->current_iteration );
  put_double( b, sim->current_time().total_seconds() );
  fmt::format_to( out_it( b ), ",\"seed\":{},\"actor\":", sim->seed );
  put_string( b, s->actor->name() );
  b += "}\n";
}

void fight_end( sim_t* sim )
{
  state_t* s = state_of( sim );
  if ( s == nullptr || !s->in_fight )
    return;
  s->in_fight = false;

  // Same predicate rl_translog::record_close() writes as FLAG_COLLECTED (and sim_t's
  // datacollection_end() guard): iteration 0 is the warm-up fight unless there is only one.
  const bool collected = ( sim->iterations == 1 || sim->current_iteration >= 1 );

  std::string& b = s->fight_buf;
  fmt::format_to( out_it( b ), "{{\"k\":\"fe\",\"it\":{},\"t\":", sim->current_iteration );
  put_double( b, sim->current_time().total_seconds() );
  fmt::format_to( out_it( b ), ",\"collected\":{},\"real\":[", collected ? "true" : "false" );
  for ( std::uint32_t i = 0; i < rl_credit::STREAM_COUNT; ++i )
  {
    if ( i )
      b += ',';
    put_double( b, s->actor->rl_credit.real[ i ] );
  }
  b += "],\"exp\":[";
  for ( std::uint32_t i = 0; i < rl_credit::STREAM_COUNT; ++i )
  {
    if ( i )
      b += ',';
    put_double( b, s->actor->rl_credit.exp[ i ] );
  }
  fmt::format_to( out_it( b ), "],\"n_hit\":{},\"n_press\":{},\"n_xp\":{}}}\n", s->fight_hits, s->fight_presses,
                  s->fight_xp );

  ++s->run_fights;
  if ( collected )
    ++s->run_collected_fights;
  s->run_hits += s->fight_hits;
  s->run_presses += s->fight_presses;
  s->run_xp += s->fight_xp;

  flush_fight( *s );
}

void write_footer( sim_t* sim )
{
  sim_t* root = root_of( sim );
  state_t* s = root->rl_bl_state.get();
  if ( s == nullptr )
    return;

  std::string b;
  b += "{\"k\":\"ftr\",\"complete\":true";
  fmt::format_to( out_it( b ), ",\"fights\":{},\"collected_fights\":{},\"n_hit\":{},\"n_press\":{},\"n_xp\":{}",
                  s->run_fights, s->run_collected_fights, s->run_hits, s->run_presses, s->run_xp );
  fmt::format_to( out_it( b ), ",\"lost_press\":{},\"stale_hit_id\":{},\"reference_mismatch\":{}", s->lost_press,
                  s->stale_hit_id, s->reference_mismatch );
  fmt::format_to( out_it( b ), ",\"restoring_mismatch\":{},\"premade_drift\":{},\"unsafe_hits\":{}",
                  s->restoring_mismatch, s->premade_drift, s->unsafe_hits );
  b += ",\"shadow_violation\":{}";
  fmt::format_to( out_it( b ), ",\"passes_total\":{},\"pass_hist\":[", s->passes_total );
  for ( std::size_t i = 0; i < PASS_HIST_SIZE; ++i )
  {
    if ( i )
      b += ',';
    fmt::format_to( out_it( b ), "{}", s->pass_hist[ i ] );
  }
  fmt::format_to( out_it( b ),
                  "],\"swing_rescaled\":{},\"foreign_press\":{},\"cache_hits\":{},\"cache_misses\":{},"
                  "\"cache_check\":{},\"cache_check_fail\":{},\"no_stats_hits\":{}}}\n",
                  s->swing_rescaled, s->foreign_press, s->cache_hits, s->cache_misses, s->cache_check,
                  s->cache_check_fail, s->no_stats_hits );

  s->out << b;
  s->out.flush();
  s->out.close();
}

}  // namespace rl_buff_ledger
