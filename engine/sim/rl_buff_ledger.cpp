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
#include "buff/buff.hpp"
#include "player/pet.hpp"
#include "player/player.hpp"
#include "sim/decision_dump.hpp"
#include "sim/sim.hpp"
#include "util/io.hpp"
#include "util/util.hpp"

#include "fmt/format.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rl_buff_ledger
{
namespace
{
constexpr int FORMAT_VERSION = 1;
constexpr std::size_t PASS_HIST_SIZE = 64;

// The record kinds this build writes (the `hdr` record's `emits` list). stage0_checks.py reads
// it to decide which identities it can assert.
constexpr const char* EMITS_JSON = "[\"hdr\",\"fb\",\"fe\",\"ftr\",\"pr\",\"hit\",\"xp\"]";
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

  // Plan 03: results of the hide-and-recompute passes of hits that are in flight (snapshotted in
  // execute, not yet at the hit sink). Keyed by the fresh id written to state->rl_bl_hit; the
  // action and target are kept to detect a stale id. Emptied at every fight boundary.
  struct hit_entry_t
  {
    const action_t* action = nullptr;
    const player_t* target = nullptr;
    int status = 0;
    std::string cand_json;  // the entries of hit.cand, comma separated, no brackets
    std::string pass_json;  // the entries of hit.pass, comma separated, no brackets
    std::uint32_t n_passes = 0;
  };
  std::unordered_map<std::uint64_t, hit_entry_t> hit_table;
  std::uint64_t next_hit_id = 1;
  // One scratch state per action, owned here. NEVER taken from, or released into, an action's own
  // free list (get_state / action_state_t::release): that would reorder the list and could change
  // which recycled object later code receives.
  std::unordered_map<const action_t*, std::unique_ptr<action_state_t>> scratch;

  // Per-fight
  std::int32_t next_press = 0;
  std::int16_t last_press = -1;
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

bool is_own_class( std::uint8_t cls )
{
  return cls == RL_CAUSE_CAST || cls == RL_CAUSE_PROC_OF_CAST || cls == RL_CAUSE_DOT_TICK ||
         cls == RL_CAUSE_PROC_OF_DOT;
}

// True for the RL actor and its pets.
bool belongs_to_actor( const state_t& s, const player_t* p )
{
  if ( p == nullptr )
    return false;
  const player_t* top = p;
  if ( p->is_pet() )
    top = static_cast<const pet_t*>( p )->owner;
  return top != nullptr && top == s.actor;
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
  // The passes re-run action_t::calculate_direct_amount on scratch states. Under average_range=1
  // (the default) it draws no random number; with average_range=0 it would draw (a rounding draw
  // at the end), and a ledgered run must be the same run as an unledgered one.
  if ( !root->average_range )
  {
    throw sc_runtime_error( "rl_buff_ledger= requires average_range=1: the hide-and-recompute passes would "
                            "draw random numbers from the action's generator." );
  }
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
  s->hit_table.clear();
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
  s->hit_table.clear();

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

std::int16_t open_press( player_t* p, const rl_cause_t& cause )
{
  state_t* s = state_of( p->sim );
  if ( s == nullptr || !s->in_fight || !belongs_to_actor( *s, p ) )
    return -1;

  if ( p->is_pet() )
  {
    // A pet's own cast is credited to the current decision by the existing cause model; it is
    // not a button choice. It rides the latest press a non-pet opened.
    ++s->foreign_press;
    return s->last_press;
  }

  // rl_cause_t's press is an int16 (its width is load-bearing, see rl_credit.hpp): refuse a fight
  // that would need more than 32767 presses rather than wrap.
  if ( s->next_press >= 32767 )
  {
    throw sc_runtime_error( "rl_buff_ledger=: more than 32767 presses in one fight (press numbers are 16-bit)." );
  }
  const std::int16_t press = static_cast<std::int16_t>( s->next_press++ );
  s->last_press = press;
  ++s->fight_presses;

  std::string& b = s->fight_buf;
  fmt::format_to( out_it( b ), "{{\"k\":\"pr\",\"it\":{},\"t\":", p->sim->current_iteration );
  put_double( b, p->sim->current_time().total_seconds() );
  fmt::format_to( out_it( b ), ",\"press\":{},\"seq\":{},\"actor\":", press, cause.seq );
  put_string( b, p->name() );
  b += ",\"action\":";
  put_string( b, p->last_foreground_action ? p->last_foreground_action->name() : std::string() );
  fmt::format_to( out_it( b ), ",\"cls\":{}}}\n", static_cast<int>( cause.cls ) );
  return press;
}

void note_no_stats_hit( action_t* a )
{
  state_t* s = state_of( a->sim );
  if ( s == nullptr || !s->in_fight || !belongs_to_actor( *s, a->player ) )
    return;
  ++s->no_stats_hits;
}

void set_in_hit_sink( sim_t* sim, bool on )
{
  state_t* s = state_of( sim );
  if ( s != nullptr )
    s->in_hit_sink = on;
}

void hit_sink( action_t* a, action_state_t* state, double expected_amount, bool exp_excl )
{
  sim_t* sim = a->sim;
  state_t* s = state_of( sim );
  if ( s == nullptr || !s->in_fight || !belongs_to_actor( *s, a->player ) )
    return;

  const bool pet = a->player->is_pet();
  const std::uint64_t h = s->next_hit++;
  ++s->fight_hits;

  if ( is_own_class( state->rl_cause_class ) && state->rl_cause_press == PRESS_NONE )
    ++s->lost_press;

  const result_amount_type rt = a->report_amount_type( state );
  const bool tick = rt == result_amount_type::DMG_OVER_TIME || rt == result_amount_type::HEAL_OVER_TIME;

  // Plan 03: attach the hide-and-recompute result run_passes() parked for this state, if any. An
  // id that is not in the table was already consumed (or the state never ran passes) and is not an
  // error; an id whose action or target differs from this hit's is STALE (status 5, counted).
  int status = 0;
  std::string cand_json;
  std::string pass_json;
  if ( state->rl_bl_hit != 0 )
  {
    auto it = s->hit_table.find( state->rl_bl_hit );
    if ( it != s->hit_table.end() )
    {
      if ( it->second.action != a || it->second.target != state->target )
      {
        status = 5;
        ++s->stale_hit_id;
      }
      else
      {
        status = it->second.status;
        cand_json = std::move( it->second.cand_json );
        pass_json = std::move( it->second.pass_json );
        const std::uint32_t n = it->second.n_passes;
        s->passes_total += n;
        ++s->pass_hist[ std::min<std::size_t>( n, PASS_HIST_SIZE - 1 ) ];
      }
      s->hit_table.erase( it );
    }
    state->rl_bl_hit = 0;
  }

  std::string& b = s->fight_buf;
  fmt::format_to( out_it( b ), "{{\"k\":\"hit\",\"it\":{},\"t\":", sim->current_iteration );
  put_double( b, sim->current_time().total_seconds() );
  fmt::format_to( out_it( b ), ",\"h\":{},\"actor\":", h );
  put_string( b, a->player->name() );
  fmt::format_to( out_it( b ), ",\"pet\":{},\"action\":", pet ? "true" : "false" );
  put_string( b, a->name() );
  b += ",\"target\":";
  put_string( b, state->target->name() );
  fmt::format_to( out_it( b ), ",\"at\":\"{}\",\"res\":\"{}\",\"ra\":", tick ? 't' : 'd',
                  util::result_type_string( state->result ) );
  put_double( b, state->result_amount );
  b += ",\"exp\":";
  put_double( b, expected_amount );
  fmt::format_to( out_it( b ),
                  ",\"exp_excl\":{},\"seq\":{},\"cls\":{},\"press\":{},\"launch\":{},\"n_targets\":{},"
                  "\"status\":{},\"cand\":[",
                  exp_excl ? "true" : "false", state->rl_cause_seq, static_cast<int>( state->rl_cause_class ),
                  state->rl_cause_press, state->rl_cause_launch, state->n_targets, status );
  b += cand_json;
  b += "],\"pass\":[";
  b += pass_json;
  b += "],\"par\":[]}\n";
}

void xp_record( player_t* p, const rl_cause_t& cause, double amount, const char* action_name )
{
  sim_t* sim = p->sim;
  state_t* s = state_of( sim );
  if ( s == nullptr || !s->in_fight || s->in_hit_sink || !belongs_to_actor( *s, p ) )
    return;

  ++s->fight_xp;
  std::string& b = s->fight_buf;
  fmt::format_to( out_it( b ), "{{\"k\":\"xp\",\"it\":{},\"t\":", sim->current_iteration );
  put_double( b, sim->current_time().total_seconds() );
  fmt::format_to( out_it( b ), ",\"seq\":{},\"cls\":{},\"press\":{},\"launch\":{},\"amount\":", cause.seq,
                  static_cast<int>( cause.cls ), cause.press, cause.launch );
  put_double( b, amount );
  b += ",\"action\":";
  put_string( b, action_name != nullptr ? action_name : "" );
  b += ",\"actor\":";
  put_string( b, p->name() );
  b += "}\n";
}

// ==========================================================================
// Plan 03: hide-and-recompute passes
// ==========================================================================

// The read tap (buff.hpp declares both). g_tap_open is true only while the REFERENCE pass of a hit
// runs; note_read records each buff once with a non-zero stack or value, in first-read order.
bool g_tap_open = false;

namespace
{
struct tap_read_t
{
  const buff_t* b;
  int stack;
  double value;
};
std::vector<tap_read_t> g_tap_reads;
}  // namespace

void note_read( const buff_t* b, int stack, double value )
{
  if ( !g_tap_open )
    return;
  // A buff read as zero cannot be a candidate (only a read that returned something is a dependency).
  if ( stack == 0 && value == 0.0 )
    return;
  for ( const tap_read_t& r : g_tap_reads )
    if ( r.b == b )
      return;
  g_tap_reads.push_back( { b, stack, value } );
}

namespace
{
// The snapshot fields compared bit for bit between a scratch pass and the real state. A mismatch is
// reported in the pass entry's `viol` as a bitmask: bit i = SNAPSHOT_FIELDS[ i ], then the bits below.
struct snapshot_field_t
{
  const char* name;
  double action_state_t::*member;
};

const snapshot_field_t SNAPSHOT_FIELDS[] = {
    { "crit_chance", &action_state_t::crit_chance },
    { "target_crit_chance", &action_state_t::target_crit_chance },
    { "haste", &action_state_t::haste },
    { "attack_power", &action_state_t::attack_power },
    { "spell_power", &action_state_t::spell_power },
    { "versatility", &action_state_t::versatility },
    { "da_multiplier", &action_state_t::da_multiplier },
    { "ta_multiplier", &action_state_t::ta_multiplier },
    { "rolling_ta_multiplier", &action_state_t::rolling_ta_multiplier },
    { "player_multiplier", &action_state_t::player_multiplier },
    { "versus_multiplier", &action_state_t::versus_multiplier },
    { "persistent_multiplier", &action_state_t::persistent_multiplier },
    { "pet_multiplier", &action_state_t::pet_multiplier },
    { "target_da_multiplier", &action_state_t::target_da_multiplier },
    { "target_ta_multiplier", &action_state_t::target_ta_multiplier },
    { "target_pet_multiplier", &action_state_t::target_pet_multiplier },
    { "target_mitigation_da_multiplier", &action_state_t::target_mitigation_da_multiplier },
    { "target_mitigation_ta_multiplier", &action_state_t::target_mitigation_ta_multiplier },
    { "target_armor", &action_state_t::target_armor },
};
constexpr std::size_t N_SNAPSHOT_FIELDS = sizeof( SNAPSHOT_FIELDS ) / sizeof( SNAPSHOT_FIELDS[ 0 ] );
constexpr std::uint64_t VIOL_RESULT_TYPE = std::uint64_t( 1 ) << N_SNAPSHOT_FIELDS;
constexpr std::uint64_t VIOL_PRE_CRIT = std::uint64_t( 1 ) << ( N_SNAPSHOT_FIELDS + 1 );

// Bit-for-bit double equality (no tolerance, NaN-safe).
bool same_bits( double x, double y )
{
  return std::memcmp( &x, &y, sizeof( double ) ) == 0;
}

std::uint64_t snapshot_diff( const action_state_t* x, const action_state_t* y )
{
  std::uint64_t mask = 0;
  for ( std::size_t i = 0; i < N_SNAPSHOT_FIELDS; ++i )
  {
    if ( !same_bits( x->*( SNAPSHOT_FIELDS[ i ].member ), y->*( SNAPSHOT_FIELDS[ i ].member ) ) )
      mask |= std::uint64_t( 1 ) << i;
  }
  if ( x->result_type != y->result_type )
    mask |= VIOL_RESULT_TYPE;
  return mask;
}

// A shadow scope: everything a hidden pass could leave behind is saved here and put back.
//   * sim->debug / sim->log are cleared (print_debug, print_log and the class code that writes to
//     sim->out_debug under `if ( sim->debug )` all go quiet) and restored;
//   * sim->rl_bl_shadow is set, which buff_t::stack() (benefit counters) and the parse-effects
//     snapshot_internal (post-snapshot callbacks) consult;
//   * the stat caches of the dealer, of its owner when the dealer is a pet, and of the hit's target
//     are saved by copy, invalidated before EVERY pass (with asserts compiled in, a read of a valid
//     entry re-computes and asserts equality, so a hidden pass that found an entry the previous pass
//     left valid would abort the program), and restored by assignment on exit. The recursive,
//     side-effecting player_t::invalidate_cache() is never called.
class shadow_scope_t
{
public:
  shadow_scope_t( action_t* a, player_t* target )
    : sim_( a->sim ), dealer_( a->player ), owner_( nullptr ), target_( target )
  {
    saved_debug_ = sim_->debug;
    saved_log_   = sim_->log;
    sim_->debug  = false;
    sim_->log    = 0;
    sim_->rl_bl_shadow = true;
    action_ = a;
    saved_callback_state_ = a->rl_bl_callback_state();
    if ( dealer_->is_pet() )
      owner_ = static_cast<pet_t*>( dealer_ )->owner;
    saved_dealer_.emplace( dealer_->cache );
    if ( owner_ != nullptr && owner_ != dealer_ )
      saved_owner_.emplace( owner_->cache );
    if ( target_ != dealer_ && target_ != owner_ )
      saved_target_.emplace( target_->cache );
  }

  shadow_scope_t( const shadow_scope_t& )            = delete;
  shadow_scope_t& operator=( const shadow_scope_t& ) = delete;

  // Before every pass.
  void invalidate_caches()
  {
    dealer_->cache.invalidate_all();
    if ( saved_owner_ )
      owner_->cache.invalidate_all();
    if ( saved_target_ )
      target_->cache.invalidate_all();
  }

  ~shadow_scope_t()
  {
    dealer_->cache = *saved_dealer_;
    if ( saved_owner_ )
      owner_->cache = *saved_owner_;
    if ( saved_target_ )
      target_->cache = *saved_target_;
    action_->rl_bl_set_callback_state( saved_callback_state_ );
    sim_->rl_bl_shadow = false;
    sim_->debug        = saved_debug_;
    sim_->log          = saved_log_;
  }

private:
  sim_t* sim_;
  player_t* dealer_;
  player_t* owner_;
  player_t* target_;
  action_t* action_ = nullptr;
  std::uint32_t saved_callback_state_ = 0;
  bool saved_debug_ = false;
  int saved_log_    = 0;
  std::optional<player_stat_cache_t> saved_dealer_;
  std::optional<player_stat_cache_t> saved_owner_;
  std::optional<player_stat_cache_t> saved_target_;
};

// Hiding a buff = what buff_t::expire leaves: stack 0 and value 0. Zeroing the fields (not a flag in
// the read functions) also catches damage_buff_t's inline readers, remains() and any engine loop.
// Restored bit for bit by the destructor.
class hidden_buffs_t
{
public:
  void hide( buff_t* b )
  {
    saved_.push_back( { b, b->current_stack, b->current_value } );
    b->current_stack = 0;
    b->current_value = 0.0;
  }
  ~hidden_buffs_t()
  {
    for ( auto it = saved_.rbegin(); it != saved_.rend(); ++it )
    {
      it->b->current_stack = it->stack;
      it->b->current_value = it->value;
    }
  }

private:
  struct saved_t
  {
    buff_t* b;
    int stack;
    double value;
  };
  std::vector<saved_t> saved_;
};

struct amount_t
{
  double pre = 0.0;  // pre-crit amount (what calculate_direct_amount returns)
  double cc  = 0.0;  // composite crit chance of the pass's state
  double cb  = 0.0;  // total_crit_bonus of the pass's state
};

struct pass_rec_t
{
  const char* kind;
  std::uint64_t hid;
  amount_t amount;
  std::uint64_t viol;
};

struct pass_env_t
{
  action_t* action;
  action_state_t* real;
  action_state_t* scratch;
  shadow_scope_t* scope;
};

// One amount pass: the engine's own snapshot and direct-amount code on the scratch state, which is
// first made to look like the real state did at its snapshot (fresh result fields, then the real
// result and block result put back for the amount, as in the real order: snapshot, crit roll,
// amount). Caches are invalidated first. Leaves the pass's snapshot in env.scratch.
struct tap_scope_t
{
  explicit tap_scope_t( bool on ) : on_( on )
  {
    if ( on_ )
    {
      g_tap_reads.clear();
      g_tap_open = true;
    }
  }
  ~tap_scope_t()
  {
    if ( on_ )
      g_tap_open = false;
  }
  bool on_;
};

amount_t amount_pass( pass_env_t& env, bool log_reads = false )
{
  env.scope->invalidate_caches();
  tap_scope_t tap( log_reads );
  action_state_t* sc = env.scratch;
  sc->copy_state( env.real );
  sc->action = env.action;
  sc->result       = RESULT_NONE;
  sc->block_result = BLOCK_RESULT_UNBLOCKED;
  sc->result_raw = sc->result_total = sc->result_mitigated = sc->result_absorbed = sc->result_amount =
      sc->self_absorb_amount = 0.0;
  env.action->snapshot_state( sc, env.real->result_type );
  sc->result       = env.real->result;
  sc->block_result = env.real->block_result;

  amount_t out;
  out.pre = env.action->calculate_direct_amount( sc );
  out.cc  = sc->composite_crit_chance();
  out.cb  = env.action->total_crit_bonus( sc );
  return out;
}

bool press_applied( const buff_t* b )
{
  const rl_cause_t& c = b->rl_applied_cause;
  return ( c.cls == RL_CAUSE_PROC_OF_CAST || c.cls == RL_CAUSE_PROC_OF_DOT ) && c.press >= 0;
}

// Candidates: the buffs the REFERENCE pass read (non-zero stack or value) that were applied by a
// press: the dealer's own buffs, its owner's when the dealer is a pet, and debuffs on the hit's own
// target whose source is the dealer or its owner. First-read order, which is deterministic.
void collect_candidates( player_t* dealer, player_t* owner, player_t* target, std::vector<buff_t*>& out )
{
  for ( const tap_read_t& r : g_tap_reads )
  {
    buff_t* b = const_cast<buff_t*>( r.b );
    if ( b->player == nullptr || !press_applied( b ) )
      continue;
    const bool own    = b->player == dealer || ( owner != nullptr && b->player == owner );
    const bool debuff = b->player == target && ( b->source == dealer || ( owner != nullptr && b->source == owner ) );
    if ( own || debuff )
      out.push_back( b );
  }
}

void put_pass( std::string& b, const pass_rec_t& p )
{
  if ( !b.empty() )
    b += ',';
  fmt::format_to( out_it( b ), "{{\"kind\":\"{}\",\"hid\":{},\"pre\":", p.kind, p.hid );
  put_double( b, p.amount.pre );
  b += ",\"cc\":";
  put_double( b, p.amount.cc );
  b += ",\"cb\":";
  put_double( b, p.amount.cb );
  fmt::format_to( out_it( b ), ",\"per\":null,\"viol\":{}}}", p.viol );
}

void put_candidate( std::string& b, std::size_t i, const buff_t* c, bool debuff )
{
  if ( !b.empty() )
    b += ',';
  fmt::format_to( out_it( b ), "{{\"i\":{},\"name\":", i );
  put_string( b, c->name_str );
  b += ",\"owner\":";
  put_string( b, c->player->name() );
  fmt::format_to( out_it( b ), ",\"kind\":\"{}\",\"stacks\":{},\"value\":", debuff ? "debuff" : "buff", c->check() );
  put_double( b, c->check_value() );
  const rl_cause_t& ap = c->rl_applied_cause;
  fmt::format_to( out_it( b ), ",\"app\":[[{},{},{}]],\"cov\":false}}", ap.press, static_cast<int>( ap.cls ),
                  c->check() );
}
}  // namespace

void run_passes( action_t* a, action_state_t* s )
{
  state_t* st = state_of( a->sim );
  if ( st == nullptr || !st->in_fight || !belongs_to_actor( *st, a->player ) )
    return;
  if ( s->result_type != result_amount_type::DMG_DIRECT )
    return;

  // A state that will never reach hit_sink (assess_damage skips a zero-raw hit that is not a miss;
  // an action without stats routes nothing) gets its snapshot checked but nothing parked.
  const bool will_sink = a->stats != nullptr && ( s->result_raw > 0 || action_t::result_is_miss( s->result ) );

  auto& scratch_slot = st->scratch[ a ];
  if ( !scratch_slot )
    scratch_slot.reset( a->new_state() );

  player_t* dealer = a->player;
  player_t* owner  = dealer->is_pet() ? static_cast<pet_t*>( dealer )->owner : nullptr;

  state_t::hit_entry_t entry;
  entry.action = a;
  entry.target = s->target;
  std::vector<pass_rec_t> passes;
  std::vector<buff_t*> cands;

  {
    shadow_scope_t scope( a, s->target );
    pass_env_t env{ a, s, scratch_slot.get(), &scope };

    // Reference pass: nothing hidden. Must reproduce the real snapshot and pre-crit amount.
    const amount_t ref = amount_pass( env, /*log_reads=*/true );
    std::uint64_t viol = snapshot_diff( env.scratch, s );
    if ( !same_bits( ref.pre, s->result_amount ) )
      viol |= VIOL_PRE_CRIT;
    passes.push_back( { "ref", 0, ref, viol } );
    if ( viol != 0 )
    {
      ++st->reference_mismatch;
      entry.status = 3;
    }
    else if ( ref.pre != 0.0 )
    {
      collect_candidates( dealer, owner, s->target, cands );
      if ( cands.size() > 40 )
      {
        throw sc_runtime_error( fmt::format( "rl_buff_ledger=: {} candidate buffs on one hit of '{}'; the hidden-set "
                                             "bitmask supports at most 40.",
                                             cands.size(), a->name() ) );
      }
      if ( !cands.empty() )
      {
        auto hidden_pass = [ & ]( std::uint64_t mask ) {
          hidden_buffs_t hidden;
          for ( std::size_t i = 0; i < cands.size(); ++i )
            if ( mask & ( std::uint64_t( 1 ) << i ) )
              hidden.hide( cands[ i ] );
          passes.push_back( { "hide", mask, amount_pass( env ), 0 } );
        };

        // One pass per candidate hidden alone.
        for ( std::size_t i = 0; i < cands.size(); ++i )
          hidden_pass( std::uint64_t( 1 ) << i );

        // A candidate is EFFECTIVE when hiding it alone changes the pre-crit amount. For 2 or 3
        // effective candidates every remaining hidden subset (all 2^m - 1 minus the singles) is run;
        // above 3 only the all-effective-hidden pass (the split rule needs each single removal plus
        // the joint removal). One effective candidate needs nothing more.
        std::uint64_t effective_mask = 0;
        int n_effective              = 0;
        for ( std::size_t i = 0; i < cands.size(); ++i )
        {
          if ( passes[ 1 + i ].amount.pre != ref.pre )
          {
            effective_mask |= std::uint64_t( 1 ) << i;
            ++n_effective;
          }
        }
        if ( n_effective >= 2 && n_effective <= 3 )
        {
          std::vector<std::uint64_t> subsets;
          for ( std::uint64_t sub = effective_mask; sub != 0; sub = ( sub - 1 ) & effective_mask )
            if ( ( sub & ( sub - 1 ) ) != 0 )  // two or more members; the singles are done
              subsets.push_back( sub );
          std::sort( subsets.begin(), subsets.end() );
          for ( std::uint64_t sub : subsets )
            hidden_pass( sub );
        }
        else if ( n_effective > 3 )
          hidden_pass( effective_mask );

        // Restoring pass: nothing hidden again, a fresh snapshot, so class members written by the
        // hidden passes (shaman's mw_affected_stacks / mw_consumed_stacks) return to their real values.
        const amount_t rest = amount_pass( env );
        std::uint64_t rviol = snapshot_diff( env.scratch, s );
        if ( !same_bits( rest.pre, s->result_amount ) )
          rviol |= VIOL_PRE_CRIT;
        passes.push_back( { "restore", 0, rest, rviol } );
        if ( rviol != 0 )
        {
          ++st->restoring_mismatch;
          entry.status = 4;
        }
        else
          entry.status = 1;
      }
    }
  }  // scope ends: caches, debug, log, shadow flag restored; hidden buffs were restored per pass

  if ( !will_sink )
    return;

  if ( entry.status == 1 || entry.status == 4 )
  {
    for ( std::size_t i = 0; i < cands.size(); ++i )
      put_candidate( entry.cand_json, i, cands[ i ], cands[ i ]->player == s->target );
  }
  for ( const pass_rec_t& p : passes )
    put_pass( entry.pass_json, p );
  entry.n_passes = static_cast<std::uint32_t>( passes.size() );

  const std::uint64_t id = st->next_hit_id++;
  st->hit_table.emplace( id, std::move( entry ) );
  s->rl_bl_hit = id;
}


}  // namespace rl_buff_ledger
