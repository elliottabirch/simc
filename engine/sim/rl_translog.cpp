// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// Flight recorder writer -- tstl-sylvanas phase 212, plan 212-01
// (TLOG-01/02/03). See rl_translog.hpp for the layout, the freeze rationale
// and the compile-time proof (D-20). This file implements D-12 (root-owned
// stream), D-13 (buffered, flushed once per fight end), D-15 (footer) and
// D-16 (rl_translog= is a plain path option, OFF by default).

#include "sim/rl_translog.hpp"

#include "player/player.hpp"
#include "sim/sim.hpp"
#include "util/io.hpp"
#include "util/util.hpp"

#include "fmt/format.h"

#include <cstring>

namespace rl_translog
{
namespace
{

// Mirrors decision_dump.cpp:715-718 -- the stream, its mutex and its row
// buffer live on the ROOT sim only (sim.hpp), so with threads>1 every
// worker sim_t that inherited a non-empty rl_translog_file_str would
// otherwise open its own handle against the same path and truncate one
// another. Walk to the root and share a single writer instead.
sim_t* root_of( sim_t* sim )
{
  sim_t* root = sim;
  while ( root->parent )
    root = root->parent;
  return root;
}

// Appends one 48-byte row onto the root's in-memory buffer. Does not
// flush -- callers decide the flush cadence (D-13: once per fight end, not
// once per row).
void append_row( sim_t* root, const void* row )
{
  const auto* bytes = static_cast<const unsigned char*>( row );
  root->rl_translog_buffer.insert( root->rl_translog_buffer.end(), bytes, bytes + RECORD_SIZE );
  ++root->rl_translog_row_count;
}

// Write-site range assertions (ruling 212-G15): the row's iteration and
// thread fields are 16 and 8 bits respectively. Silently wrapping fight
// 65536 into fight 0, or thread 256 into thread 0, is a category of wrong
// answer nobody would ever suspect -- refuse loudly instead of truncating.
void assert_write_site_ranges( sim_t* sim )
{
  if ( sim->current_iteration > 65535 )
  {
    throw sc_runtime_error( fmt::format(
        "rl_translog: current_iteration ({}) exceeds the 16-bit row field -- refusing rather than "
        "truncating.",
        sim->current_iteration ) );
  }
  if ( sim->thread_index < 0 || sim->thread_index > 255 )
  {
    throw sc_runtime_error( fmt::format(
        "rl_translog: thread_index ({}) is outside the 8-bit row field's [0, 255] range -- refusing "
        "rather than truncating.",
        sim->thread_index ) );
  }
}

// 212-CR-FIX WR-04: a stream that has entered a failed state (badbit/
// failbit, e.g. an out-of-space write) does not throw on write()/flush() --
// it silently no-ops on every call from that point on. Without this check,
// a full disk mid-run yields a footerless file that D-14 makes the reader
// refuse whole, with exit code 0 and nothing printed anywhere pointing at
// the disk. Call this immediately after every flush().
void assert_stream_ok( sim_t* root, const char* where )
{
  if ( !*root->rl_translog_stream )
  {
    throw sc_runtime_error( fmt::format(
        "rl_translog: write to '{}' failed in {} after {} rows -- the stream entered a failed "
        "state (disk full, permissions, or similar); refusing to continue silently, the file on "
        "disk is now incomplete and D-14 will refuse it whole on read.",
        root->rl_translog_file_str, where, root->rl_translog_row_count ) );
  }
}

// ruling 212-G9: the one actor whose fight this recorder describes. Nothing
// in the fork records which player the in-process policy drove, so "the
// only non-pet player" is the one unambiguous answer -- asserting it here
// is what keeps the ambiguity from becoming a silent wrong number. This
// assertion lives at record_close() / write_footer() time, NOT in setup():
// sim_t::setup() runs before actors exist (player_no_pet_list is empty
// there), which is why 212-RESEARCH's "validated in sim_t::init()"
// placement does not work. combat_end() of the first fight is still
// seconds into the run -- actors exist there.
//
// 212-CR-FIX WR-03: takes the CALLER's own sim_t*, not necessarily the
// root. record_close() (below) now passes the SIM whose fight just ended --
// each sim_t owns its own player_no_pet_list (player.cpp), so a worker
// sim's close row must resolve its actor from that same worker, never from
// the root's (a different player_t object, last written by a different
// fight or never). write_footer() deliberately keeps passing the ROOT: it
// reads the root's own merged run-level collected_data, so it must resolve
// the root's own actor object. That asymmetry is intentional, not an
// inconsistency -- see write_footer()'s own call site below.
player_t* solo_actor( sim_t* s )
{
  if ( s->player_no_pet_list.size() != 1 )
  {
    throw sc_runtime_error( fmt::format(
        "rl_translog= describes one actor's fight; this sim has {} non-pet players. A raid "
        "simulation is not something this recorder can describe.",
        s->player_no_pet_list.size() ) );
  }
  return s->player_no_pet_list[ 0 ];
}

} // anonymous namespace

void open_and_write_header( sim_t* sim )
{
  sim_t* root = root_of( sim );
  if ( root->rl_translog_file_str.empty() )
    return;

  // Opened with an EXPLICIT mode of write + truncate + binary.
  // io::ofstream::open's default is write+truncate with no binary bit
  // (io.hpp:87-88) -- harmless on Linux, silently corrupting anywhere
  // else, and a file minted on one platform and read on another would
  // disagree for a reason nobody would find.
  root->rl_translog_stream = std::make_unique<io::ofstream>();
  root->rl_translog_stream->open( root->rl_translog_file_str,
                                   std::ios::out | std::ios::trunc | std::ios::binary );
  if ( !root->rl_translog_stream->is_open() )
  {
    throw sc_runtime_error(
        fmt::format( "rl_translog=: unable to open '{}' for writing.", root->rl_translog_file_str ) );
  }

  // Opening eagerly rather than lazily is deliberate: a bad path refuses
  // at startup instead of stranding a half-run simulation at the first
  // fight end, and a run that produces zero rows still leaves a file that
  // names itself.
  file_header h{};
  std::memcpy( h.magic, MAGIC, sizeof( MAGIC ) );
  h.endian_canary = ENDIAN_CANARY;
  h.format_version = FORMAT_VERSION;
  h.record_size = RECORD_SIZE;
  h.header_size = HEADER_SIZE;
  h.reserved0 = 0;
  h.reserved1 = 0;
  std::strncpy( h.obs_schema_sha, RL_OBS_SCHEMA_SHA, sizeof( h.obs_schema_sha ) - 1 );
  std::strncpy( h.mask_rules_sha, RL_MASK_RULES_SHA, sizeof( h.mask_rules_sha ) - 1 );
  std::strncpy( h.action_space_sha, RL_ACTION_SPACE_SHA, sizeof( h.action_space_sha ) - 1 );

  root->rl_translog_stream->write( reinterpret_cast<const char*>( &h ), sizeof( h ) );
  root->rl_translog_stream->flush();
  assert_stream_ok( root, "open_and_write_header" );
}

void record_decision( sim_t* sim, const player_t* p, std::uint64_t seq, const float obs[ RL_OBS_DIM ],
                       const std::uint8_t mask[ RL_ACTION_DIM ], int action_index, float q_margin,
                       bool wait_floored )
{
  sim_t* root = root_of( sim );
  if ( root->rl_translog_file_str.empty() )
    return;

  assert_write_site_ranges( sim );

  decision_record r{};
  r.damage = p->solver_damage_so_far;
  r.t = static_cast<float>( sim->current_time().total_seconds() );
  std::memcpy( r.obs, obs, sizeof( r.obs ) );
  r.q_margin = q_margin;
  // 212-CR-FIX NT-02: narrowed 64->32 bits silently, unlike iteration/thread
  // above and below, which assert_write_site_ranges() refuses loudly rather
  // than truncate. Deliberate asymmetry, not an oversight: 2^32 decisions is
  // not reachable in practice (assert_write_site_ranges guards the fields
  // that ARE reachable in practice), so this is a documented exception
  // rather than a matching loud refusal.
  r.seq = static_cast<std::uint32_t>( seq );
  r.iteration = static_cast<std::uint16_t>( sim->current_iteration );

  // Pack the legality mask one bit per action, in declaration order.
  std::uint8_t packed_mask = 0;
  for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
  {
    if ( mask[ i ] )
      packed_mask |= static_cast<std::uint8_t>( 1u << i );
  }
  r.mask = packed_mask;
  r.action = static_cast<std::uint8_t>( action_index );
  r.flags = wait_floored ? FLAG_WAIT_FLOORED : 0;
  r.thread = static_cast<std::uint8_t>( sim->thread_index );
  r.kind = KIND_DECISION;
  r.reserved = 0;

  // Append only -- no flush. The fight's close row flushes the whole
  // fight at once (D-13).
  append_row( root, &r );
  ++root->rl_translog_pending_decisions;
}

void record_close( sim_t* sim )
{
  sim_t* root = root_of( sim );
  if ( root->rl_translog_file_str.empty() )
    return;

  assert_write_site_ranges( sim );

  // 212-CR-FIX WR-03: resolve the actor from SIM (the fight that just
  // ended), not ROOT. See solo_actor()'s own doc comment for why this
  // differs from write_footer()'s call below.
  player_t* p = solo_actor( sim );

  close_record r{};
  r.final_damage_total = p->solver_damage_so_far;
  // This IS the quantity report.json reports as "fight_length"
  // (player_collected_data.cpp:317); sim->current_time() is a different
  // quantity the engine explicitly allows to differ from it (the <=
  // assert at player_collected_data.cpp:325), and using it here would
  // make the footer's reconciliation an apples-to-oranges comparison.
  // That is why this hook is placed AFTER datacollection_end(), where
  // this field is finalised.
  r.fight_length = static_cast<float>( p->iteration_fight_length.total_seconds() );
  std::memset( r.zero12, 0, sizeof( r.zero12 ) );
  r.decision_count = root->rl_translog_pending_decisions;
  r.iteration = static_cast<std::uint16_t>( sim->current_iteration );
  r.zero42 = 0;
  r.zero43 = 0;

  // The warm-up-discard verdict, decided HERE and written into the row
  // (D-09) -- the predicate lifted verbatim from sim.cpp's own
  // datacollection_end() guard, so a discarded first fight is explicit
  // data rather than something the reader re-derives from a fight number.
  std::uint8_t flags = 0;
  const bool collected = ( sim->iterations == 1 || sim->current_iteration >= 1 );
  if ( collected )
    flags |= FLAG_COLLECTED;
  r.flags = flags;
  r.thread = static_cast<std::uint8_t>( sim->thread_index );
  r.kind = KIND_CLOSE;
  r.reserved = 0;

  append_row( root, &r );
  root->rl_translog_pending_decisions = 0;
  root->rl_translog_summed_close_damage += r.final_damage_total;
  ++root->rl_translog_fight_count;
  // 212-CR-FIX BL-01: counts the SAME population the engine's own
  // collected_data.*.mean() (read in write_footer(), below) is computed
  // over -- the footer's collected_fight_count field. Incremented under
  // the exact same predicate that just set FLAG_COLLECTED above, so the
  // two can never drift apart.
  if ( collected )
    ++root->rl_translog_collected_fight_count;

  // The one write and one flush per fight D-13 asks for, against
  // decision_dump's ~300-600 per fight. This is a syscall-cost argument
  // and NOTHING ELSE: a crashed file is refused whole by the reader
  // (D-14), so buffering here does not bound crash loss and must not be
  // described as if it did.
  root->rl_translog_stream->write(
      reinterpret_cast<const char*>( root->rl_translog_buffer.data() ),
      static_cast<std::streamsize>( root->rl_translog_buffer.size() ) );
  root->rl_translog_stream->flush();
  assert_stream_ok( root, "record_close" );
  root->rl_translog_buffer.clear();
}

// 212-CR-FIX WR-06: writes whatever is currently buffered to disk without
// appending a close or footer row -- called from the in-process arm's
// cast/wait epilogues (solver_control.cpp) when accept_cast()/accept_wait()
// throws, so the rows this fight has already appended (including the
// decision that was made and then refused) survive the abort instead of
// being lost with the process. No-op when the option is unset or nothing
// is buffered.
void flush_pending( sim_t* sim )
{
  sim_t* root = root_of( sim );
  if ( root->rl_translog_file_str.empty() )
    return;
  if ( root->rl_translog_buffer.empty() )
    return;

  root->rl_translog_stream->write(
      reinterpret_cast<const char*>( root->rl_translog_buffer.data() ),
      static_cast<std::streamsize>( root->rl_translog_buffer.size() ) );
  root->rl_translog_stream->flush();
  assert_stream_ok( root, "flush_pending" );
  root->rl_translog_buffer.clear();
}

void write_footer( sim_t* sim )
{
  sim_t* root = root_of( sim );
  if ( root->rl_translog_file_str.empty() )
    return;

  // 212-CR-FIX NT-04: record_decision() and record_close() both call this;
  // write_footer() previously did not, and still writes
  // r.thread = sim->thread_index unchecked below. Harmless today (the
  // footer is written by the root, thread_index == 0), but this was the one
  // write site outside the guard.
  assert_write_site_ranges( sim );

  // 212-CR-FIX WR-03: deliberately ROOT here, not SIM -- the footer reads
  // the ROOT's own merged run-level collected_data below, so it must
  // resolve the root's own actor object. See solo_actor()'s doc comment.
  player_t* p = solo_actor( root );

  footer_record r{};
  // The engine's OWN run-level numbers -- what makes the footer an
  // independent catch for a wrong field, a wrong offset or a missed
  // fight, rather than a bare echo of the writer's own bookkeeping.
  // 212-CR-FIX BL-01: both of these are COLLECTED-fights-only (the guarded
  // datacollection_end() call skips the warm-up fight), unlike fight_count/
  // summed_close_damage below which include it -- see collected_fight_count
  // and the struct-level doc comment in rl_translog.hpp for how a reader
  // tells the two populations apart.
  r.engine_run_aggregate = p->collected_data.compound_dmg.mean();
  r.mean_collected_fight_length = static_cast<float>( p->collected_data.fight_length.mean() );
  r.fight_count = root->rl_translog_fight_count;
  // row_count must count the footer row ITSELF -- "does the total
  // include the footer" is exactly the kind of off-by-one that costs an
  // afternoon on the reader side.
  r.row_count = root->rl_translog_row_count + 1;
  r.footer_source = FOOTER_SOURCE_PER_FIGHT_ACCUMULATOR;
  r.summed_close_damage = root->rl_translog_summed_close_damage;
  r.collected_fight_count = root->rl_translog_collected_fight_count;
  r.zero36 = 0;
  r.iteration = 0xFFFF;
  r.zero42 = 0;
  r.zero43 = 0;
  r.flags = 0;
  r.thread = static_cast<std::uint8_t>( sim->thread_index );
  r.kind = KIND_FOOTER;
  r.reserved = 0;

  append_row( root, &r );
  root->rl_translog_stream->write(
      reinterpret_cast<const char*>( root->rl_translog_buffer.data() ),
      static_cast<std::streamsize>( root->rl_translog_buffer.size() ) );
  root->rl_translog_stream->flush();
  assert_stream_ok( root, "write_footer" );
  root->rl_translog_buffer.clear();
  root->rl_translog_stream->close();
}

} // namespace rl_translog
