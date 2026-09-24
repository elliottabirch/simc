// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// Random-roll recorder writer -- tstl-sylvanas phase 250, plan 250-01 (REC-01/02/03). See
// rl_rng_record.hpp for the layout and the pure-observer promise (D-17). This file implements
// the recorder_t object sim_t::rl_rng_recorder owns (root only, mirrors rl_translog's own
// root-owned stream -- see sim.hpp's rl_translog_stream doc comment for why), the five writer
// entry points sim.cpp calls, and roller registration (plan 250-01 Task 2: player, action,
// buff, proc callback, proc object; raid events are a later plan).

#include "sim/rl_rng_record.hpp"

#include "action/action.hpp"
#include "player/player.hpp"
#include "player/stats.hpp"
#include "sim/sim.hpp"
#include "util/io.hpp"
#include "util/rng.hpp"
#include "util/util.hpp"

#include "fmt/format.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <utility>

namespace rl_rng_record
{
namespace
{

// Mirrors rl_translog.cpp's root_of() -- the recorder's stream, buffer and roller registry live
// on the ROOT sim only (R-08 also forces threads=1 whenever rl_rng_record= is set, so in
// practice sim == root() always; walking to the root anyway matches the established pattern and
// costs nothing).
sim_t* root_of( sim_t* sim )
{
  sim_t* root = sim;
  while ( root->parent )
    root = root->parent;
  return root;
}

std::string json_escape( std::string_view s )
{
  std::string out;
  out.reserve( s.size() );
  for ( unsigned char c : s )
  {
    switch ( c )
    {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if ( c < 0x20 )
          out += fmt::format( "\\u{:04x}", c );
        else
          out += static_cast<char>( c );
    }
  }
  return out;
}

std::string_view class_name( roller_class_e cls )
{
  switch ( cls )
  {
    case roller_class_e::sim_shared: return "sim_shared";
    case roller_class_e::sim_explore: return "sim_explore";
    case roller_class_e::player: return "player";
    case roller_class_e::action: return "action";
    case roller_class_e::buff: return "buff";
    case roller_class_e::proc_callback: return "proc_callback";
    case roller_class_e::proc_object: return "proc_object";
    case roller_class_e::raid_event: return "raid_event";
    case roller_class_e::unregistered: return "unregistered";
  }
  return "unregistered";
}

} // anonymous namespace

// Per-roller bookkeeping, indexed by roller number (registry position == roller id, so lookup
// is O(1) and the sidecar's `id` field is just this index).
struct roller_entry_t
{
  std::string key;
  roller_class_e cls = roller_class_e::unregistered;
  const std::uint64_t* draw_counter = nullptr;  // rng::rng_t::rl_draw_counter() -- observer only
  const action_t* action = nullptr;             // only ever set when cls == action

  // Per-fight tallies, cleared at fight_begin(). execute_presses/tick_presses/refills stay 0
  // throughout this plan -- the press guards that would increment them are plan 250-03's work;
  // the fields exist now so the COUNTER layout never has to change shape.
  std::uint64_t begin_ordinal = 0;
  std::uint32_t execute_presses = 0;
  std::uint32_t tick_presses = 0;
  std::uint32_t refills = 0;
  std::uint32_t no_draw_rolls = 0;
  std::uint32_t roll_entries = 0;
};

// The recorder object -- owned by sim_t::rl_rng_recorder (root only, unique_ptr, forward
// declared in sim.hpp). Implements rng::rl_draw_sink_t so basic_rng_t<Engine>::real()/roll()
// can call into it through the process-wide rng::rl_draw_sink pointer. Every public entry point
// below is the free-function counterpart's whole implementation; the free functions
// (open_and_write_header/fight_begin/fight_end/write_footer/mark_resalt/register_roller/
// register_action_roller/active, at the bottom of this file) are thin no-op-when-unset shims
// around it.
class recorder_t final : public rng::rl_draw_sink_t
{
public:
  explicit recorder_t( sim_t* root ) : root_( root ) {}

  ~recorder_t() override
  {
    if ( rng::rl_draw_sink == this )
      rng::rl_draw_sink = nullptr;
  }

  // ---- rng::rl_draw_sink_t ----
  void on_draw( std::uint32_t& roller_slot, const std::uint64_t& draw_counter,
                std::uint32_t roller_override, double raw ) override;
  void on_roll_outcome( std::uint32_t roller, std::uint64_t draw_ordinal, double chance,
                         bool outcome ) override;
  void on_no_draw_roll( std::uint32_t& roller_slot, const std::uint64_t& draw_counter ) override;

  // ---- lifecycle, called by the free functions below ----
  void open_and_write_header();
  void fight_begin();
  void fight_end();
  void write_footer();
  void mark_resalt();
  std::uint32_t register_roller( rng::rng_t& stream, std::string_view key, roller_class_e cls,
                                  const action_t* action );

private:
  std::uint32_t ensure_registered( std::uint32_t& roller_slot, const std::uint64_t& draw_counter );
  void append( const record& r );
  void assert_stream_ok( const char* where );
  void write_sidecar();

  sim_t* root_;
  std::unique_ptr<io::ofstream> stream_;
  std::vector<unsigned char> buffer_;
  std::uint64_t row_count_ = 0;          // total records appended (buffered or already flushed)
  std::uint64_t roll_entry_total_ = 0;   // total ROLL entries over the whole recording
  std::uint64_t fight_roll_entries_ = 0; // ROLL entries THIS fight, cleared at fight_begin()
  std::uint32_t fight_counter_entries_ = 0; // COUNTER entries THIS fight
  std::uint32_t fight_count_ = 0;        // FIGHT_END entries written so far
  bool after_resalt_ = false;
  std::uint16_t current_iteration_field_ = ITERATION_NONE;
  // Guards against sim_t::iterate()'s own end-of-run cleanup reset() (sim.cpp, called once
  // after the iteration loop exits, with current_iteration left at the LAST real iteration's
  // value -- discovered during this plan's own byte-identity/entries==counter verification:
  // that cleanup reset() re-runs every actor/buff/pet reset(), some of which draw random
  // numbers as ordinary state re-initialization, and sim_t::reset() is the one hook site the
  // plan specifies for fight_begin() -- see 250-01-PLAN.md's "Fight begin" call site. Without
  // this guard those incidental draws land in a SECOND, phantom FIGHT_BEGIN..(no FIGHT_END)
  // window with no COUNTER row ever written for it, breaking the ROLL-count-equals-counter-
  // delta invariant (250-01-PLAN.md success criterion 1) by exactly the phantom draw count.
  // Sentinel -2 is distinct from current_iteration's own constructor default of -1.
  int last_fight_end_iteration_ = -2;

  // The last ROLL entry's own (stream, draw_ordinal) -- on_roll_outcome() patches that exact
  // entry's chance/outcome fields in place. Always the tail of buffer_ when set: real() calls
  // on_draw() and roll() calls on_roll_outcome() in the same synchronous call chain, with no
  // fight boundary (which is the only thing that clears buffer_) possible in between.
  std::uint32_t last_roll_stream_ = rng::RL_ROLLER_UNREGISTERED;
  std::uint64_t last_roll_ordinal_ = 0;
  std::uint64_t outcome_mismatches_ = 0; // on_roll_outcome() calls that could not find their ROLL

  std::vector<roller_entry_t> rollers_;
  // key string -> every roller id ever assigned that key, in assignment order. size() > 1 means
  // two DIFFERENT rollers share a key -- reported under the sidecar's duplicateKeys, never
  // merged (D-05).
  std::unordered_map<std::string, std::vector<std::uint32_t>> keys_seen_;
};

std::uint32_t recorder_t::ensure_registered( std::uint32_t& roller_slot, const std::uint64_t& draw_counter )
{
  if ( roller_slot != rng::RL_ROLLER_UNREGISTERED )
    return roller_slot;

  const auto id = static_cast<std::uint32_t>( rollers_.size() );
  rollers_.emplace_back();
  roller_entry_t& entry = rollers_.back();
  entry.key = fmt::format( "unregistered|{}", id );
  entry.cls = roller_class_e::unregistered;
  entry.draw_counter = &draw_counter;
  // "begin ordinal = counter minus 1" (250-01-PLAN.md "on_draw") -- draw_counter here is
  // rl_trace_n_ AFTER this draw (real() increments before calling on_draw()), so counter minus
  // 1 is this stream's ordinal BEFORE the draw that triggered the lazy registration.
  entry.begin_ordinal = draw_counter > 0 ? draw_counter - 1 : 0;
  roller_slot = id;
  return id;
}

void recorder_t::on_draw( std::uint32_t& roller_slot, const std::uint64_t& draw_counter,
                           std::uint32_t roller_override, double raw )
{
  const std::uint32_t stream_id = ensure_registered( roller_slot, draw_counter );
  const std::uint32_t logical = roller_override != 0 ? roller_override : stream_id;

  // logical may reference a roller not yet in rollers_ only if a caller sets an override to a
  // number this recorder never assigned -- defensive clamp rather than an out-of-range access;
  // this cannot happen from anything this plan wires (no override setter exists until raid
  // events, a later plan), so this is pure defense.
  const std::uint32_t logical_clamped = logical < rollers_.size() ? logical : stream_id;
  roller_entry_t& logical_entry = rollers_[ logical_clamped ];
  const std::uint32_t position = logical_entry.roll_entries++;

  record rec{};
  rec.raw = raw;
  rec.chance = CHANCE_NONE;
  rec.time_ms = static_cast<std::int64_t>( root_->current_time().total_millis() );
  rec.draw_ordinal = draw_counter;
  rec.roller = logical_clamped;
  rec.stream = stream_id;
  rec.trigger = 0;
  rec.press = 0;
  rec.position = position;
  rec.inner_trigger = 0;
  rec.inner_press = 0;
  rec.inner_position = position;
  rec.iteration = current_iteration_field_;
  rec.kind = KIND_ROLL;
  rec.trigger_kind = TRIGGER_KIND_NONE;
  rec.inner_trigger_kind = TRIGGER_KIND_NONE;
  rec.outcome = OUTCOME_NOT_A_ROLL;
  rec.label = LABEL_NONE;
  rec.flags = after_resalt_ ? FLAG_AFTER_RESALT : 0;

  last_roll_stream_ = stream_id;
  last_roll_ordinal_ = draw_counter;
  append( rec );
  ++fight_roll_entries_;
  ++roll_entry_total_;
}

void recorder_t::on_roll_outcome( std::uint32_t roller, std::uint64_t draw_ordinal, double chance,
                                   bool outcome )
{
  // `roller` here is the caller's rl_roller_id_ (the physical stream) -- basic_rng_t::roll()
  // deliberately passes the stream, not any override, so this matches the ROLL entry by
  // (stream, draw_ordinal) exactly as 250-01-PLAN.md's on_roll_outcome contract specifies.
  if ( roller == last_roll_stream_ && draw_ordinal == last_roll_ordinal_ && buffer_.size() >= RECORD_SIZE )
  {
    record rec;
    std::memcpy( &rec, buffer_.data() + buffer_.size() - RECORD_SIZE, RECORD_SIZE );
    if ( rec.kind == KIND_ROLL && rec.stream == roller && rec.draw_ordinal == draw_ordinal )
    {
      rec.chance = chance;
      rec.outcome = outcome ? OUTCOME_SUCCESS : OUTCOME_FAIL;
      std::memcpy( buffer_.data() + buffer_.size() - RECORD_SIZE, &rec, RECORD_SIZE );
      return;
    }
  }
  ++outcome_mismatches_;
}

void recorder_t::on_no_draw_roll( std::uint32_t& roller_slot, const std::uint64_t& draw_counter )
{
  const std::uint32_t stream_id = ensure_registered( roller_slot, draw_counter );
  ++rollers_[ stream_id ].no_draw_rolls;
}

std::uint32_t recorder_t::register_roller( rng::rng_t& stream, std::string_view key, roller_class_e cls,
                                            const action_t* action )
{
  // Cast to the same reference type basic_rng_t exposes its roller id/draw-counter through.
  // rng::rng_t derives from basic_rng_t<xoshiro256plus_t> with no additional state, so this
  // reference IS the same object real()/roll() read and write.
  const std::uint32_t existing = stream.rl_roller_id();
  if ( existing != rng::RL_ROLLER_UNREGISTERED && existing < rollers_.size() )
  {
    // Already has a number -- either promoted from a lazy "unregistered" numbering by an
    // earlier draw, or this exact object being registered again (proc_rng_t::reseed_source_rng()
    // is called both per fight and by the re-salt walk -- the caller is responsible for calling
    // this only on the per-fight path, per 250-01-PLAN.md Task 2's proc-object note). Either
    // way this is the SAME stream object, never a duplicate-key sighting.
    roller_entry_t& entry = rollers_[ existing ];
    entry.key = std::string( key );
    entry.cls = cls;
    entry.draw_counter = &stream.rl_draw_counter();
    if ( action )
      entry.action = action;
    return existing;
  }

  const auto id = static_cast<std::uint32_t>( rollers_.size() );
  rollers_.emplace_back();
  roller_entry_t& entry = rollers_.back();
  entry.key = std::string( key );
  entry.cls = cls;
  entry.draw_counter = &stream.rl_draw_counter();
  entry.action = action;
  stream.rl_set_roller_id( id );

  auto& ids = keys_seen_[ std::string( key ) ];
  ids.push_back( id );
  return id;
}

void recorder_t::append( const record& r )
{
  const auto* bytes = reinterpret_cast<const unsigned char*>( &r );
  buffer_.insert( buffer_.end(), bytes, bytes + RECORD_SIZE );
  ++row_count_;
  if ( r.kind == KIND_COUNTER )
    ++fight_counter_entries_;
}

void recorder_t::assert_stream_ok( const char* where )
{
  if ( !*stream_ )
  {
    throw sc_runtime_error( fmt::format(
        "rl_rng_record: write to '{}' failed in {} after {} rows -- the stream entered a failed "
        "state (disk full, permissions, or similar); refusing to continue silently, the file on "
        "disk is now incomplete.",
        root_->rl_rng_record_file_str, where, row_count_ ) );
  }
}

void recorder_t::open_and_write_header()
{
  stream_ = std::make_unique<io::ofstream>();
  stream_->open( root_->rl_rng_record_file_str, std::ios::out | std::ios::trunc | std::ios::binary );
  if ( !stream_->is_open() )
  {
    throw sc_runtime_error(
        fmt::format( "rl_rng_record=: unable to open '{}' for writing.", root_->rl_rng_record_file_str ) );
  }

  file_header h{};
  std::memcpy( h.magic, MAGIC, sizeof( MAGIC ) );
  h.format_version = FORMAT_VERSION;
  h.record_size = RECORD_SIZE;
  h.header_size = HEADER_SIZE;
  h.sim_seed = root_->seed;
  h.flags = ( root_->per_source_rng ? 0x1u : 0u ) | ( root_->deterministic ? 0x2u : 0u ) |
            ( !root_->rl_iteration_seeds.empty() ? 0x4u : 0u ) | ( root_->average_range ? 0x8u : 0u );
  h.thread_index = static_cast<std::uint32_t>( root_->thread_index );
  h.iterations = root_->iterations > 0 ? static_cast<std::uint32_t>( root_->iterations ) : 0u;
  h.zero36 = 0;
  std::memset( h.reserved, 0, sizeof( h.reserved ) );

  stream_->write( reinterpret_cast<const char*>( &h ), sizeof( h ) );
  stream_->flush();
  assert_stream_ok( "open_and_write_header" );

  register_roller( root_->rng(), "sim|_rng", roller_class_e::sim_shared, nullptr );
  register_roller( root_->solver_explore_rng, "sim|solver_explore_rng", roller_class_e::sim_explore, nullptr );
}

void recorder_t::fight_begin()
{
  // See last_fight_end_iteration_'s own doc comment: sim_t::iterate() calls reset() one extra
  // time, after the loop exits, with current_iteration unchanged from the last real iteration.
  // That call is not a new fight -- skip it whole (no FIGHT_BEGIN entry, no tally reset) so its
  // incidental draws land in the ALREADY-CLOSED prior fight's accounting instead of opening a
  // phantom window with no matching FIGHT_END/COUNTER.
  if ( root_->current_iteration == last_fight_end_iteration_ )
    return;

  after_resalt_ = false;
  fight_roll_entries_ = 0;
  fight_counter_entries_ = 0;

  const int it = root_->current_iteration;
  if ( it < 0 )
  {
    current_iteration_field_ = ITERATION_NONE;
  }
  else if ( static_cast<std::uint32_t>( it ) > MAX_ITERATION )
  {
    throw sc_runtime_error( fmt::format(
        "rl_rng_record: current_iteration ({}) exceeds the recorder's {}-entry iteration field -- "
        "refusing rather than wrapping.",
        it, MAX_ITERATION ) );
  }
  else
  {
    current_iteration_field_ = static_cast<std::uint16_t>( it );
  }

  for ( roller_entry_t& r : rollers_ )
  {
    r.execute_presses = 0;
    r.tick_presses = 0;
    r.refills = 0;
    r.no_draw_rolls = 0;
    r.roll_entries = 0;
    r.begin_ordinal = r.draw_counter ? *r.draw_counter : 0;
  }

  record rec{};
  rec.raw = 0.0;
  rec.chance = CHANCE_NONE;
  rec.time_ms = static_cast<std::int64_t>( root_->current_time().total_millis() );
  rec.iteration = current_iteration_field_;
  rec.kind = KIND_FIGHT_BEGIN;
  rec.outcome = OUTCOME_NOT_A_ROLL;
  append( rec );
}

void recorder_t::fight_end()
{
  for ( std::size_t roller_id = 0; roller_id < rollers_.size(); ++roller_id )
  {
    roller_entry_t& r = rollers_[ roller_id ];
    const std::uint64_t end_ordinal = r.draw_counter ? *r.draw_counter : r.begin_ordinal;
    const bool any_activity = r.execute_presses || r.tick_presses || r.refills || r.no_draw_rolls ||
                               r.roll_entries || end_ordinal != r.begin_ordinal;
    if ( !any_activity )
      continue;

    // COUNTER reuses the ROLL record's byte layout with different field meanings -- see
    // rl_rng_record.hpp's per-field offset comments and 250-01-PLAN.md's "Recording format"
    // COUNTER paragraph for the mapping used below (offset -> meaning, not the ROLL name).
    record rec{};
    rec.time_ms = static_cast<std::int64_t>( r.begin_ordinal );      // @16 begin ordinal
    rec.draw_ordinal = end_ordinal;                                  // @24 end ordinal
    rec.roller = static_cast<std::uint32_t>( roller_id );            // @32 roller
    rec.stream = ( r.cls == roller_class_e::raid_event ) ? FLAG_LOGICAL_ONLY : 0u; // @36
    rec.trigger = 0;                                                 // @40
    rec.press = r.execute_presses;                                   // @44 execute presses
    rec.position = r.tick_presses;                                   // @48 tick presses
    rec.inner_trigger = r.refills;                                   // @52 refills
    rec.inner_press = r.no_draw_rolls;                                // @56 no-draw rolls (R-05)
    rec.inner_position = r.roll_entries;                              // @60 ROLL entries, this roller
    rec.iteration = current_iteration_field_;
    rec.kind = KIND_COUNTER;
    rec.outcome = OUTCOME_NOT_A_ROLL;
    append( rec );
  }

  record fe{};
  fe.raw = 0.0;
  fe.chance = CHANCE_NONE;
  fe.time_ms = static_cast<std::int64_t>( root_->current_time().total_millis() );
  fe.draw_ordinal = fight_roll_entries_;   // ROLL entries in this fight's window
  fe.press = fight_counter_entries_;       // COUNTER records this fight
  fe.iteration = current_iteration_field_;
  fe.kind = KIND_FIGHT_END;
  fe.outcome = OUTCOME_NOT_A_ROLL;
  append( fe );

  ++fight_count_;
  last_fight_end_iteration_ = root_->current_iteration;

  // Per-fight flush -- one write, one flush, mirrors rl_translog.cpp's record_close() (D-13's
  // syscall-cost rationale applies here too, not a crash-loss bound).
  stream_->write( reinterpret_cast<const char*>( buffer_.data() ),
                   static_cast<std::streamsize>( buffer_.size() ) );
  stream_->flush();
  assert_stream_ok( "fight_end" );
  buffer_.clear();
}

void recorder_t::mark_resalt()
{
  record rec{};
  rec.raw = 0.0;
  rec.chance = CHANCE_NONE;
  rec.time_ms = static_cast<std::int64_t>( root_->current_time().total_millis() );
  rec.iteration = current_iteration_field_;
  rec.kind = KIND_RESALT;
  rec.outcome = OUTCOME_NOT_A_ROLL;
  append( rec );
  after_resalt_ = true;
}

void recorder_t::write_sidecar()
{
  const std::string sidecar_path = root_->rl_rng_record_file_str + ".rollers.json";
  std::ofstream sidecar( sidecar_path, std::ios::out | std::ios::trunc );
  if ( !sidecar.is_open() )
  {
    throw sc_runtime_error( fmt::format( "rl_rng_record=: unable to open '{}' for writing.", sidecar_path ) );
  }

  sidecar << "{\"format\": 1, \"rollEntries\": " << roll_entry_total_ << ", \"rollers\": [";
  for ( std::size_t i = 0; i < rollers_.size(); ++i )
  {
    const roller_entry_t& r = rollers_[ i ];
    if ( i != 0 )
      sidecar << ", ";
    sidecar << "{\"id\": " << i << ", \"key\": \"" << json_escape( r.key ) << "\", \"class\": \""
            << class_name( r.cls ) << "\", \"ownStream\": "
            << ( r.cls != roller_class_e::raid_event ? "true" : "false" );
    if ( r.cls == roller_class_e::action && r.action != nullptr )
    {
      const action_t* a = r.action;
      const player_t* p = a->player;
      const std::string actor_name = p ? p->name_str : std::string( "unknown" );
      std::string_view actor_kind = "player";
      if ( p )
      {
        if ( p->is_pet() )
          actor_kind = "pet";
        else if ( p->is_enemy() )
          actor_kind = "enemy";
      }
      const std::string stats_name = a->stats ? a->stats->name_str : std::string();
      sidecar << ", \"actor\": \"" << json_escape( actor_name ) << "\""
              << ", \"actorKind\": \"" << actor_kind << "\""
              << ", \"statsName\": \"" << json_escape( stats_name ) << "\""
              << ", \"dual\": " << ( a->dual ? "true" : "false" )
              << ", \"harmful\": " << ( a->harmful ? "true" : "false" );
    }
    sidecar << "}";
  }
  sidecar << "], \"duplicateKeys\": [";
  bool first_dup = true;
  for ( const auto& kv : keys_seen_ )
  {
    if ( kv.second.size() < 2 )
      continue;
    if ( !first_dup )
      sidecar << ", ";
    first_dup = false;
    sidecar << "{\"key\": \"" << json_escape( kv.first ) << "\", \"ids\": [";
    for ( std::size_t i = 0; i < kv.second.size(); ++i )
    {
      if ( i != 0 )
        sidecar << ", ";
      sidecar << kv.second[ i ];
    }
    sidecar << "]}";
  }
  sidecar << "]}";

  if ( !sidecar )
  {
    throw sc_runtime_error( fmt::format( "rl_rng_record=: write to '{}' failed.", sidecar_path ) );
  }
}

void recorder_t::write_footer()
{
  record rec{};
  rec.raw = 0.0;
  rec.chance = CHANCE_NONE;
  rec.time_ms = static_cast<std::int64_t>( root_->current_time().total_millis() );
  rec.draw_ordinal = roll_entry_total_;                              // total ROLL entries
  rec.press = fight_count_;                                          // FIGHT_END count
  rec.position = static_cast<std::uint32_t>( row_count_ + 1 );       // total records, incl. footer
  rec.iteration = ITERATION_NONE;
  rec.kind = KIND_FOOTER;
  rec.outcome = OUTCOME_NOT_A_ROLL;
  append( rec );

  stream_->write( reinterpret_cast<const char*>( buffer_.data() ),
                   static_cast<std::streamsize>( buffer_.size() ) );
  stream_->flush();
  assert_stream_ok( "write_footer" );
  buffer_.clear();
  stream_->close();

  write_sidecar();

  fmt::print( stderr, "rl_rng_record: wrote {} roll entries over {} fights to {}\n", roll_entry_total_,
              fight_count_, root_->rl_rng_record_file_str );
}

// ---- Free functions -- the interface rl_rng_record.hpp declares. Every one is a no-op (one
// pointer check on root->rl_rng_recorder) when rl_rng_record= is unset. ----

void open_and_write_header( sim_t* sim )
{
  sim_t* root = root_of( sim );
  if ( root->rl_rng_record_file_str.empty() )
    return;

  root->rl_rng_recorder = std::make_shared<recorder_t>( root );
  root->rl_rng_recorder->open_and_write_header();
  rng::rl_draw_sink = root->rl_rng_recorder.get();
}

void fight_begin( sim_t* sim )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return;
  root->rl_rng_recorder->fight_begin();
}

void fight_end( sim_t* sim )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return;
  root->rl_rng_recorder->fight_end();
}

void write_footer( sim_t* sim )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return;
  root->rl_rng_recorder->write_footer();
  // Clears rng::rl_draw_sink (recorder_t's destructor) and frees the object -- nothing may draw
  // into a closed recorder.
  root->rl_rng_recorder.reset();
}

void mark_resalt( sim_t* sim )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return;
  root->rl_rng_recorder->mark_resalt();
}

void register_roller( sim_t* sim, rng::rng_t& stream, std::string_view key, roller_class_e cls )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return;
  root->rl_rng_recorder->register_roller( stream, key, cls, nullptr );
}

void register_action_roller( sim_t* sim, rng::rng_t& stream, std::string_view key, const action_t* action )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return;
  root->rl_rng_recorder->register_roller( stream, key, roller_class_e::action, action );
}

bool active( const sim_t* sim )
{
  const sim_t* root = sim;
  while ( root->parent )
    root = root->parent;
  return static_cast<bool>( root->rl_rng_recorder );
}

} // namespace rl_rng_record
