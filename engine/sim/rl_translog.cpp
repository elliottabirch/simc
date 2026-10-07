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

#include "player/pet.hpp"
#include "player/player.hpp"
#include "sim/rl_buff_credit.hpp"
#include "sim/rl_buff_ledger.hpp"
#include "sim/rl_policy.hpp"
#include "sim/rl_rng_record.hpp"
#include "sim/rl_target_select.hpp"
#include "sim/sim.hpp"
#include "util/io.hpp"
#include "util/util.hpp"

#include "fmt/format.h"

#include <zstd.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

// Pure observer for the RL transition log's per-proc counter block
// (tstl-sylvanas quick task 260917-pcn). Routes a pet's roll to its owner,
// mirroring stats.cpp's own solver_damage_so_far pet->owner rule; ignores
// enemies and a null player. Declared in rl_proc_counters.hpp, defined here
// because it needs player_t/pet_t complete.
void rl_count_proc( player_t* p, rl_proc::id which, double chance, bool success )
{
  // 261001-bac plan 12 (MJ-04): inside a ledger pass a counted no-op (it would count a real proc and relabel the last real roll).
  if ( p != nullptr && p->sim->rl_bl_shadow )
    return rl_buff_ledger::blocked( p->sim, "translog.count_proc" );
  // 250-03 (REC-04, R-09): labels the last ROLL entry this call's own draw produced, BEFORE the
  // null/pet/enemy returns below -- a null player, a pet (routed to its owner below) or an enemy
  // still drew the roll being counted, and label_last_roll()'s own chance/outcome match (not this
  // function's early returns) is what decides whether the entry gets labelled.
  if ( p != nullptr )
    rl_rng_record::label_last_roll( p->sim, static_cast<std::uint8_t>( which ) + 1, chance, success );

  if ( p == nullptr ) return;
  if ( p->is_pet() ) { p = p->cast_pet()->owner; if ( p == nullptr ) return; }
  if ( p->is_enemy() ) return;
  auto& c = p->rl_proc_counters;
  const auto i = static_cast<std::size_t>( which );
  if ( c.attempts[ i ] < 65535u ) ++c.attempts[ i ];
  if ( success && c.successes[ i ] < 65535u ) ++c.successes[ i ];
  const double clamped = chance < 0.0 ? 0.0 : ( chance > 1.0 ? 1.0 : chance );
  c.chance_sum[ i ] += static_cast<float>( clamped );
}

// rl_cause_scope_t's constructor/destructor (tstl-sylvanas quick task 260918-cbc, Stage A4) --
// declared in rl_credit.hpp, defined here because they need player_t complete (mirroring
// rl_credit_route's own placement below). Pure bookkeeping: push/pop of the player-scoped
// cause stack, nothing else. Stage A5: takes the owning action_t* (nullptr default) and pushes
// an rl_cause_frame_t rather than a bare rl_cause_t.
rl_cause_scope_t::rl_cause_scope_t( player_t* p_, rl_cause_t cause, const action_t* owner, const char* kind,
                                    const action_t* ctx )
  : p( p_ )
{
  // 261001-bac stage 0: a CAST-class cause with no press yet is a freshly resolved foreground
  // cast (action_t::rl_resolve_cause()'s only CAST producer) -- the ledger numbers it here, once.
  // Every re-push of an existing cast's cause (travel, impact, ticks) carries its press from the
  // state and skips this. One bool test when rl_buff_ledger= is off.
  // 261003-s1c plan 01 Task 2: "fresh" means an UNMARKED cast. A cause carrying RL_CAUSE_DECK_MARK is a derived scope (a Doom Winds deck
  // hit's payout, sc_shaman.cpp consume_maelstrom_weapon), never a new foreground cast, so it must not open a press of its own; it
  // inherits the ledger numbers of the frame it was opened inside instead (below). The mark is tested explicitly; the raw class byte is
  // never compared.
  const bool marked = ( cause.cls & RL_CAUSE_DECK_MARK ) != 0;
  if ( p->sim->rl_bl_on && !marked && rl_cause_base( cause.cls ) == RL_CAUSE_CAST && cause.press < 0 )
    cause.press = rl_buff_ledger::open_press( p, cause );
  // The class code builds a marked scope as `rl_cause_t{ seq, cls | RL_CAUSE_DECK_MARK }`, which drops the ledger's press and launch
  // numbers (they default to -1). Left alone, every hit under a deck-hit scope would be an own-class hit with no press (a LOST press) and
  // every buff the deck hit applied would carry no applier press. Generic and mark-based, no per-spell rule: a marked scope with no press
  // of its own, opened on the same decision seq as the frame on top of the same player's stack, takes that frame's press and launch.
  // Ledger numbers are never read by any roll, routing or scheduling decision, so with the ledger off nothing changes.
  else if ( p->sim->rl_bl_on && marked && cause.press < 0 && cause.launch < 0 && !p->rl_cause_stack.empty() &&
            p->rl_cause_stack.back().cause.seq == cause.seq )
  {
    cause.press  = p->rl_cause_stack.back().cause.press;
    cause.launch = p->rl_cause_stack.back().cause.launch;
  }
  p->rl_cause_stack.push_back( rl_cause_frame_t{ cause, owner } );
  // 261001-bac plan 06: the ledger's own frame (a `frm` record, and the stack the buff reads, draws, consumes and
  // launches of the gate check are attached to). One bool test when rl_buff_ledger= is off.
  if ( p->sim->rl_bl_on )
    rl_bl_frame = rl_buff_ledger::frame_push( p, cause, owner, ctx, kind );
}

rl_cause_scope_t::~rl_cause_scope_t()
{
  if ( rl_bl_frame >= 0 )
    rl_buff_ledger::frame_pop( p, rl_bl_frame );
  p->rl_cause_stack.pop_back();
}

// Pure observer for the RL transition log's credit-by-cause block
// (tstl-sylvanas quick task 260918-cbc, stage A). Routes one damage (or
// expected-damage) increment into the right of the six per-fight
// cumulative credit streams -- see rl_credit.hpp's top-of-file comment
// for the full model. `p` is already resolved to the owning player by
// every call site (a pet's realized/expected damage routes to its OWNER
// before calling this, mirroring stats.cpp's/accrue_expected_damage's own
// pet->owner rules). Defined here because it needs player_t complete,
// mirroring rl_count_proc's own placement immediately above.
bool rl_credit_hit_is_chosen( const player_t* p, const player_t* hit_target )
{
  return hit_target != nullptr && hit_target == rl_target_select::rl_chosen_enemy_of( p );
}

void rl_credit_route( player_t* p, rl_cause_t cause, std::uint64_t now_seq, double amount, bool expected,
                       const player_t* hit_target, const char* action_name )
{
  if ( p == nullptr ) return;

  // A cause can never be stamped with a decision seq that lies in the future of the CURRENT
  // decision boundary -- the stamp is always written at or before the routing site runs.
  assert( cause.seq < 0 || static_cast<std::uint64_t>( cause.seq ) <= now_seq );

  std::uint32_t index;
  // Phase 259 (259-11): the class is read with the deck mark masked off, so realised and expected
  // routing is bit-identical to a run that never marks anything.
  switch ( rl_cause_base( cause.cls ) )
  {
    case RL_CAUSE_CAST:
    case RL_CAUSE_PROC_OF_CAST:
      index = ( cause.seq == static_cast<std::int64_t>( now_seq ) ) ? 0u : 1u;  // own_cast : tail_cast
      break;
    case RL_CAUSE_DOT_TICK:
    case RL_CAUSE_PROC_OF_DOT:
      index = ( cause.seq == static_cast<std::int64_t>( now_seq ) ) ? 2u : 3u;  // own_dot : tail_dot
      break;
    case RL_CAUSE_AUTO:
    case RL_CAUSE_PROC_OF_AUTO:
      index = 4u;  // background
      break;
    default:
      index = 5u;  // orphan
      break;
  }

  ( expected ? p->rl_credit.exp : p->rl_credit.real )[ index ] += amount;

  // Phase 266 (266-01, owner F9, R4/R9): did this increment strike the chosen enemy (the tag) AT THE
  // MOMENT OF THE HIT? The tag of a pet is its owner's. nullptr (no target in scope, e.g. a heal or an
  // absorb on a friendly never reaches here as an enemy) is never the chosen enemy. The chosen copy below
  // adds the SAME amount in the SAME order as the all-enemy copy, in both modes (with the flag off it is
  // bookkeeping only and no play reads it, R8).
  const bool on_chosen = rl_credit_hit_is_chosen( p, hit_target );
  if ( on_chosen )
  {
    ( expected ? p->rl_credit_chosen.exp : p->rl_credit_chosen.real )[ index ] += amount;
    // The chosen running totals: the twins of solver_damage_so_far / solver_damage_expected_so_far
    // (whose own additions stay at their own sites, every one of them paired with a route call).
    ( expected ? p->solver_chosen_damage_expected_so_far : p->solver_chosen_damage_so_far ) += amount;
  }

  // 261001-bac stage 0: expected-side routings made outside accrue_expected_damage (the Windfury
  // occurrence price) are written as `xp` records; accrue's own are `hit` records already.
  if ( expected && p->sim->rl_bl_on )
    rl_buff_ledger::xp_record( p, cause, amount, action_name, on_chosen );

  // Orphan census: realized-only, keyed by the contributing stats_t's own name (the only
  // identity a realized sink has -- stats_t::add_result carries no action_state_t/action_t).
  if ( index == 5u && !expected && action_name != nullptr )
    p->rl_orphan_damage_by_action[ action_name ] += amount;

  // Phase 268 (268-05, G268-8): the background-stream twin of the census above, counted only while
  // rl_credit_census=1 (observer only: nothing reads it during play).
  if ( index == 4u && !expected && action_name != nullptr && p->sim->rl_credit_census )
    p->rl_background_damage_by_action[ action_name ] += amount;

  // tstl-sylvanas quick task 260918-atr, stage F1: the .attr sidecar's own-credit accumulator.
  // Mirrors the class predicate above for indices {0,1,2,3} (own_cast/tail_cast/own_dot/
  // tail_dot -- never {4,5}, background/orphan), but keyed by the CAUSING decision's own
  // `cause.seq` rather than the routing site's `now_seq` -- so a decision's credit lands under
  // its own index no matter how much later the damage actually resolves (a travel-delayed tail
  // cast, a DoT tick seconds later in the same fight).
  if ( index <= 3u )
  {
    if ( p->rl_fight_first_seq_set && cause.seq >= 0 &&
         static_cast<std::uint64_t>( cause.seq ) >= p->rl_fight_first_seq )
    {
      const auto idx = static_cast<std::size_t>( static_cast<std::uint64_t>( cause.seq ) -
                                                   p->rl_fight_first_seq );
      auto& vec = expected ? p->rl_own_exp : p->rl_own_real;
      if ( vec.size() <= idx )
        vec.resize( idx + 1, 0.0 );
      vec[ idx ] += amount;

      // Phase 266 (266-01): the chosen-enemy twin of the per-press own credit, same index, same order.
      if ( on_chosen )
      {
        auto& vec_chosen = expected ? p->rl_own_exp_chosen : p->rl_own_real_chosen;
        if ( vec_chosen.size() <= idx )
          vec_chosen.resize( idx + 1, 0.0 );
        vec_chosen[ idx ] += amount;
      }

      // Phase 259 (plan 259-11, owner Q15/Q16, fork option P): the expected own credit that travels
      // under a deck-hit mark is ALSO kept apart, per decision. `rl_own_exp` above still receives it
      // (every total and the translog rows are exactly as before); the per-fight pass in
      // record_close() later replaces this marked part by the decision's share of the fight's
      // pooled deck payout. Pure bookkeeping: no random number, no event.
      if ( expected && ( cause.cls & RL_CAUSE_DECK_MARK ) != 0 )
      {
        auto& marked = p->rl_own_exp_marked;
        if ( marked.size() <= idx )
          marked.resize( idx + 1, 0.0 );
        marked[ idx ] += amount;

        // Phase 266 (266-01): the chosen-enemy twin of the deck-marked part.
        if ( on_chosen )
        {
          auto& marked_chosen = p->rl_own_exp_marked_chosen;
          if ( marked_chosen.size() <= idx )
            marked_chosen.resize( idx + 1, 0.0 );
          marked_chosen[ idx ] += amount;
        }
      }
    }
    else if ( !expected )
    {
      // Realized-only pre-fight census -- see player.hpp's rl_attr_pre_fight_real doc comment
      // for why this is a diagnostic finding, never silently folded into rl_own_real.
      p->rl_attr_pre_fight_real += amount;
    }
  }
}

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

// Appends one RECORD_SIZE-byte row (RECORD_SIZE bytes; 3192 as of version 13, width 324:
// roundup8(45 + 4*324 + 4*16*23 + 5) + 160 + 96 + 112) onto the
// root's in-memory buffer. Does not
// flush -- callers decide the flush cadence (D-13: once per fight end, not
// once per row).
void append_row( sim_t* root, const void* row )
{
  const auto* bytes = static_cast<const unsigned char*>( row );
  root->rl_translog_buffer.insert( root->rl_translog_buffer.end(), bytes, bytes + RECORD_SIZE );
  ++root->rl_translog_row_count;
}

// 260924-tlz (write-time zstd, owner ruling 2026-09-24, R-TLZ): a zstd FRAME is force-closed
// every ZSTD_TRANSLOG_FRAME_ROWS rows (decision/close/footer rows all count, a single running
// total for the whole file -- not per-fight, not per-write-call), so an unclean kill loses at
// most the trailing partial frame -- the streaming-compression counterpart to the raw layout's
// own torn-ROW guarantee (D-14: the reader already refuses a short trailing row; the reader
// documented alongside this constant likewise drops a short trailing zstd frame the same way).
// A named constant (not inlined) so the frame-boundary policy is stated once, here, rather than
// re-derived at each of the three call sites below.
constexpr std::uint32_t ZSTD_TRANSLOG_FRAME_ROWS = 64;
constexpr int ZSTD_TRANSLOG_COMPRESSION_LEVEL = 3;

// Drives exactly one ZSTD_compressStream2() input chunk (`data`/`n_bytes`, arbitrary size, not
// necessarily row-aligned -- the 256-byte file_header is the one caller that is not) to
// completion for the given `op`, writing whatever compressed bytes zstd produces straight to the
// (already-open, raw/binary) underlying ofstream. `op == ZSTD_e_continue` returns once the whole
// input is consumed; `op == ZSTD_e_end` returns once zstd's own `ret == 0` signals the frame's
// epilogue (checksum/end-marker) is fully flushed, not merely that input was consumed. The one
// low-level primitive both zstd_compress_and_write_rows() (row-by-row, frame-cadence-aware) and
// open_and_write_header()'s own header write (a single one-shot ZSTD_e_continue chunk, see that
// call site's own comment for why the header lives INSIDE the compressed stream) build on.
void zstd_drive_stream( sim_t* root, const unsigned char* data, std::size_t n_bytes, ZSTD_EndDirective op )
{
  auto* cstream = reinterpret_cast<ZSTD_CStream*>( root->rl_translog_zstd_cstream );
  if ( root->rl_translog_zstd_outbuf.empty() )
    root->rl_translog_zstd_outbuf.resize( ZSTD_CStreamOutSize() );

  ZSTD_inBuffer in{ data, n_bytes, 0 };
  for ( ;; )
  {
    ZSTD_outBuffer out{ root->rl_translog_zstd_outbuf.data(), root->rl_translog_zstd_outbuf.size(), 0 };
    const std::size_t ret = ZSTD_compressStream2( cstream, &out, &in, op );
    if ( ZSTD_isError( ret ) )
    {
      throw sc_runtime_error(
          fmt::format( "rl_translog: zstd compression failed: {}", ZSTD_getErrorName( ret ) ) );
    }
    if ( out.pos > 0 )
    {
      root->rl_translog_stream->write( reinterpret_cast<const char*>( out.dst ),
                                        static_cast<std::streamsize>( out.pos ) );
    }
    const bool done = ( op == ZSTD_e_continue ) ? ( in.pos == in.size ) : ( ret == 0 );
    if ( done )
      break;
  }
}

// Pushes `n_bytes` (always a whole number of RECORD_SIZE rows -- every caller below hands this
// function exactly one of root->rl_translog_buffer's fight-aligned flushes) through the root's
// persistent ZSTD_CStream, one row at a time so the ZSTD_TRANSLOG_FRAME_ROWS cadence can be
// enforced at row granularity regardless of how many rows a single caller flushes at once. The
// row layout itself never touches disk uncompressed once this path is live (260924-tlz:
// compressed output is the ONLY writer path, no opt-out).
void zstd_compress_and_write_rows( sim_t* root, const unsigned char* data, std::size_t n_bytes )
{
  if ( n_bytes == 0 )
    return;
  assert( n_bytes % RECORD_SIZE == 0 );

  const std::size_t n_rows = n_bytes / RECORD_SIZE;
  for ( std::size_t row = 0; row < n_rows; ++row )
  {
    ++root->rl_translog_zstd_rows_in_frame;
    const bool end_this_frame = root->rl_translog_zstd_rows_in_frame >= ZSTD_TRANSLOG_FRAME_ROWS;
    zstd_drive_stream( root, data + row * RECORD_SIZE, RECORD_SIZE,
                        end_this_frame ? ZSTD_e_end : ZSTD_e_continue );
    if ( end_this_frame )
      root->rl_translog_zstd_rows_in_frame = 0;
  }
}

// Force-closes whatever zstd frame is currently open, even short of the
// ZSTD_TRANSLOG_FRAME_ROWS cadence -- called once, at write_footer(), so the file's FINAL frame
// is always a complete, independently-decodable frame rather than relying on the row cadence to
// have landed exactly on the last row. No-op if the last row already closed on the cadence
// (rl_translog_zstd_rows_in_frame == 0, nothing pending).
void zstd_finish_stream( sim_t* root )
{
  if ( root->rl_translog_zstd_rows_in_frame == 0 )
    return;
  zstd_drive_stream( root, nullptr, 0, ZSTD_e_end );
  root->rl_translog_zstd_rows_in_frame = 0;
}

// Write-site range assertions (ruling 212-G15): the row's iteration and
// thread fields are 16 and 8 bits respectively. Silently wrapping fight
// 65536 into fight 0, or thread 256 into thread 0, is a category of wrong
// answer nobody would ever suspect -- refuse loudly instead of truncating.
void assert_write_site_ranges( sim_t* sim )
{
  if ( fight_index( sim ) > 65535 )
  {
    throw sc_runtime_error( fmt::format(
        "rl_translog: current_iteration ({}) exceeds the 16-bit row field -- refusing rather than "
        "truncating.",
        fight_index( sim ) ) );
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
  // 261005-fight-list: a list entry after the first has adopted the previous entry's open writer (adopt(),
  // called before setup()); the header, the zstd stream and the sidecars were created once by entry 0.
  // An ordinary run (and branch_child_reopen(), which releases the stream first) never holds a stream here.
  if ( root->rl_translog_stream )
    return;

  // 260924-tlz (write-time zstd, owner ruling 2026-09-24, R-TLZ): the on-disk file is the
  // caller's own rl_translog= path PLUS ".zst" -- `rl_translog_file_str` itself is left
  // untouched (every sidecar name below, `.procs.json`/`.credit.json`/`.attr`, is still built
  // from the UNCOMPRESSED base path, unchanged) so no caller that composes an
  // `rl_translog=<dir>/translog.bin` option line needs to change what it passes; the reader
  // side (scripts/rl/translog.py's resolve_translog_path()) is what looks for the `.zst`
  // sibling when the exact name it was given does not exist. Compressed output is the ONLY
  // writer path from this point -- no opt-out flag, no raw-fwrite fallback (feedback_new_
  // behaviour_becomes_default_delete_old_path).
  const std::string zst_path = root->rl_translog_file_str + ".zst";

  // Opened with an EXPLICIT mode of write + truncate + binary.
  // io::ofstream::open's default is write+truncate with no binary bit
  // (io.hpp:87-88) -- harmless on Linux, silently corrupting anywhere
  // else, and a file minted on one platform and read on another would
  // disagree for a reason nobody would find.
  root->rl_translog_stream = std::make_unique<io::ofstream>();
  root->rl_translog_stream->open( zst_path, std::ios::out | std::ios::trunc | std::ios::binary );
  if ( !root->rl_translog_stream->is_open() )
  {
    throw sc_runtime_error(
        fmt::format( "rl_translog=: unable to open '{}' for writing.", zst_path ) );
  }

  // 260924-tlz: the ONE zstd compression context for this file's whole lifetime -- created
  // here (before the header write below), freed in write_footer(). Per the brief's own layout
  // rule, the 256-byte file_header is INSIDE the compressed stream, same as every row that
  // follows it -- not a raw/uncompressed prefix -- so a reader decompresses the WHOLE file
  // (header included) before it does any offset-based parsing at all (scripts/rl/translog.py's
  // resolve_translog_path()/decode() sniff the zstd magic at byte 0 of the .zst file for exactly
  // this reason: with the header compressed, that magic -- not 'RLTL' -- is genuinely the first
  // four bytes on disk).
  root->rl_translog_zstd_cstream = reinterpret_cast<ZSTD_CCtx_s*>( ZSTD_createCStream() );
  if ( root->rl_translog_zstd_cstream == nullptr )
  {
    throw sc_runtime_error(
        fmt::format( "rl_translog=: unable to create a zstd compression stream for '{}'.", zst_path ) );
  }
  {
    const std::size_t set_level_ret = ZSTD_CCtx_setParameter(
        reinterpret_cast<ZSTD_CStream*>( root->rl_translog_zstd_cstream ), ZSTD_c_compressionLevel,
        ZSTD_TRANSLOG_COMPRESSION_LEVEL );
    if ( ZSTD_isError( set_level_ret ) )
    {
      throw sc_runtime_error( fmt::format( "rl_translog=: unable to set zstd compression level: {}",
                                            ZSTD_getErrorName( set_level_ret ) ) );
    }
  }

  // Opening eagerly rather than lazily is deliberate: a bad path refuses
  // at startup instead of stranding a half-run simulation at the first
  // fight end, and a run that produces zero rows still leaves a file that
  // names itself.
  file_header h{};
  std::memcpy( h.magic, MAGIC, sizeof( MAGIC ) );
  h.endian_canary = ENDIAN_CANARY;
  h.format_version = file_format_version( root );
  h.record_size = RECORD_SIZE;
  h.header_size = HEADER_SIZE;
  // Phase 213 D-08/213-G17: populate both from the loaded weights when a
  // policy was loaded (root->solver_policy_weights non-null); zero
  // otherwise. A null pointer means the log was written by a run with no
  // policy loaded, so zero is the truthful value for both fields -- and
  // every pre-213 log reads the same way for the same reason: the dial
  // really was zero, because the loader refused anything else.
  if ( root->solver_policy_weights )
  {
    h.exploration = root->solver_policy_weights->exploration;
    h.round_index = root->solver_policy_weights->generation;
  }
  else
  {
    h.exploration = 0.0f;
    h.round_index = 0;
  }
  // Version 11 (phase 259, plans 259-05 / 259-05b): the loaded weights' aim state, stamped from
  // the file's aim section and the comparator switch. 0.0 / 0 with no weights or no section.
  h.aim_exploration = 0.0f;
  h.aim_state = 0;
  if ( root->solver_policy_weights && root->solver_policy_weights->has_aim_section )
  {
    const rl_policy::rl_weights_t& aw = *root->solver_policy_weights;
    h.aim_exploration = aw.aim.exploration;
    h.aim_state = AIM_STATE_SECTION | ( aw.has_aim_head ? AIM_STATE_HEAD : 0u ) |
                  ( aw.aim.obs_source == 1 ? AIM_STATE_OBS_HEAD : 0u );
  }
  if ( root->target_scorer_force_rules )
    h.aim_state |= AIM_STATE_FORCE_RULES;
  // Version 13 (266-01, 266-09): the funnel mode word is the option's value (0 or 1, R10); the chooser state
  // word's bit 0 is the random-tag option (R10), every other bit 0.
  h.funnel_mode = root->solver_funnel_mode ? 1u : 0u;
  h.chooser_state = root->solver_random_chosen_enemy ? 1u : 0u;
  // tstl-sylvanas phase 218, plan 218-02 (RIG-01): source the header's
  // fight-shape word from the sim option instead of hardcoding zero. 0
  // stays reachable -- it is sim_t::rl_fight_shape_index's own default,
  // so a run that never sets rl_fight_shape_index= writes the same zero
  // an old (pre-218) run implicitly wrote.
  h.fight_shape_index = static_cast<std::uint32_t>( root->rl_fight_shape_index );
  // 261005-fight-list: a format-15 (list) header names no single fight: the three per-fight words read the
  // sentinel and the reader takes them from each fight-close row's tag instead.
  if ( root->rl_list_active )
  {
    h.fight_shape_index = LIST_WORD_SENTINEL;
    h.funnel_mode = LIST_WORD_SENTINEL;
    h.chooser_state = LIST_WORD_SENTINEL;
  }
  std::strncpy( h.obs_schema_sha, RL_OBS_SCHEMA_SHA, sizeof( h.obs_schema_sha ) - 1 );
  std::strncpy( h.mask_rules_sha, RL_MASK_RULES_SHA, sizeof( h.mask_rules_sha ) - 1 );
  std::strncpy( h.action_space_sha, RL_ACTION_SPACE_SHA, sizeof( h.action_space_sha ) - 1 );

  // 260924-tlz: the header goes through the SAME compressed stream every row after it does
  // (zstd_drive_stream(), ZSTD_e_continue -- this call does not end a frame, so the header and
  // the first ZSTD_TRANSLOG_FRAME_ROWS rows share frame 0; a kill mid-header-write already left
  // an unreadable (too-short) file under the pre-260924-tlz raw format too, so this is not a new
  // failure mode).
  zstd_drive_stream( root, reinterpret_cast<const unsigned char*>( &h ), sizeof( h ), ZSTD_e_continue );
  root->rl_translog_stream->flush();
  assert_stream_ok( root, "open_and_write_header" );

  // Version 9 (260917-pcn): a JSON sidecar naming the proc-counter registry, written once per
  // translog file right next to it (`<path>.procs.json`), plain text and deliberately NOT part
  // of the binary format itself -- so a Python reader never hand-types the 20 registry names in
  // a second place. Numbers come straight from the live constants (never hand-typed either).
  {
    const std::string sidecar_path = root->rl_translog_file_str + ".procs.json";
    std::ofstream sidecar( sidecar_path, std::ios::out | std::ios::trunc );
    if ( !sidecar.is_open() )
    {
      throw sc_runtime_error(
          fmt::format( "rl_translog=: unable to open '{}' for writing.", sidecar_path ) );
    }
    sidecar << "{\"format_version\": " << file_format_version( root ) << ", \"proc_count\": " << rl_proc::COUNT
            << ", \"record_size\": " << RECORD_SIZE << ", \"proc_block_offset\": " << PROC_BLOCK_OFFSET
            << ", \"names\": [";
    for ( std::uint32_t i = 0; i < rl_proc::COUNT; ++i )
    {
      if ( i != 0 )
        sidecar << ", ";
      sidecar << "\"" << RL_PROC_NAMES[ i ] << "\"";   // plan 268-04: the spec header names the procs (RL_PROC_NAME_COUNT == rl_proc::COUNT, pinned)
    }
    sidecar << "]}";
  }

  // Version 10 (260918-cbc): a JSON sidecar naming the credit-by-cause streams, written once
  // per translog file right next to it (`<path>.credit.json`), plain text and deliberately NOT
  // part of the binary format itself -- mirrors the `.procs.json` sidecar immediately above.
  // Numbers come straight from the live constants (never hand-typed either). `credit_streams`
  // is 12 -- the two arrays (`credit_real`/`credit_exp`) of `rl_credit::STREAM_COUNT` each --
  // while `names` lists the six stream names shared by both arrays.
  {
    const std::string sidecar_path = root->rl_translog_file_str + ".credit.json";
    std::ofstream sidecar( sidecar_path, std::ios::out | std::ios::trunc );
    if ( !sidecar.is_open() )
    {
      throw sc_runtime_error(
          fmt::format( "rl_translog=: unable to open '{}' for writing.", sidecar_path ) );
    }
    sidecar << "{\"format_version\": " << file_format_version( root )
            << ", \"credit_block_offset\": " << CREDIT_BLOCK_OFFSET
            << ", \"chosen_block_offset\": " << CHOSEN_BLOCK_OFFSET
            << ", \"credit_streams\": " << ( 2u * rl_credit::STREAM_COUNT )
            << ", \"record_size\": " << RECORD_SIZE << ", \"names\": [";
    for ( std::uint32_t i = 0; i < rl_credit::STREAM_COUNT; ++i )
    {
      if ( i != 0 )
        sidecar << ", ";
      sidecar << "\"" << rl_credit::NAMES[ i ] << "\"";
    }
    sidecar << "]}";
  }

  // Stage F1 (260918-atr): the .attr sidecar itself -- a SEPARATE binary file, own header,
  // opened here alongside the main stream (never lazily -- same "a bad path refuses at startup"
  // rationale as the main stream's own open call above).
  {
    const std::string attr_path = root->rl_translog_file_str + ".attr";
    root->rl_translog_attr_stream = std::make_unique<io::ofstream>();
    root->rl_translog_attr_stream->open( attr_path, std::ios::out | std::ios::trunc | std::ios::binary );
    if ( !root->rl_translog_attr_stream->is_open() )
    {
      throw sc_runtime_error( fmt::format( "rl_translog=: unable to open '{}' for writing.", attr_path ) );
    }

    rl_attr::file_header ah{};
    std::memcpy( ah.magic, rl_attr::MAGIC, sizeof( rl_attr::MAGIC ) );
    ah.format_version = rl_attr::FORMAT_VERSION;
    ah.record_size = rl_attr::RECORD_SIZE;
    ah.header_size = rl_attr::HEADER_SIZE;
    std::memset( ah.zero16, 0, sizeof( ah.zero16 ) );

    root->rl_translog_attr_stream->write( reinterpret_cast<const char*>( &ah ), sizeof( ah ) );
    root->rl_translog_attr_stream->flush();
    if ( !*root->rl_translog_attr_stream )
    {
      throw sc_runtime_error( fmt::format(
          "rl_translog=: write to '{}' failed writing the header.", attr_path ) );
    }
  }

  // 260927-d1 (D1 census, Stage 1 Task 2): the `.apl` sidecar -- opened here, ONLY when
  // solver_record_apl_choice=1, alongside the main stream and the `.attr` sidecar above (same "a
  // bad path refuses at startup" rationale). Option absent/false: this whole block is skipped, no
  // file is created, byte-identical to a run built before this option existed -- see
  // rl_translog.hpp's APL CHOICE SIDECAR section.
  if ( root->solver_record_apl_choice )
  {
    const std::string apl_path = root->rl_translog_file_str + ".apl";
    root->rl_translog_apl_stream = std::make_unique<io::ofstream>();
    root->rl_translog_apl_stream->open( apl_path, std::ios::out | std::ios::trunc | std::ios::binary );
    if ( !root->rl_translog_apl_stream->is_open() )
    {
      throw sc_runtime_error( fmt::format( "rl_translog=: unable to open '{}' for writing.", apl_path ) );
    }

    rl_apl_choice::file_header ph{};
    std::memcpy( ph.magic, rl_apl_choice::MAGIC, sizeof( rl_apl_choice::MAGIC ) );
    ph.format_version = rl_apl_choice::FORMAT_VERSION;
    ph.record_size = rl_apl_choice::RECORD_SIZE;
    ph.header_size = rl_apl_choice::HEADER_SIZE;
    std::memset( ph.zero16, 0, sizeof( ph.zero16 ) );

    root->rl_translog_apl_stream->write( reinterpret_cast<const char*>( &ph ), sizeof( ph ) );
    root->rl_translog_apl_stream->flush();
    if ( !*root->rl_translog_apl_stream )
    {
      throw sc_runtime_error( fmt::format(
          "rl_translog=: write to '{}' failed writing the header.", apl_path ) );
    }
  }
}

void record_decision( sim_t* sim, player_t* p, std::uint64_t seq, const float obs[ RL_OBS_DIM ],
                       const std::uint8_t mask[ RL_ACTION_DIM ], int action_index, float q_margin,
                       float top_q, std::uint16_t chosen_target_actor_index, bool wait_floored,
                       bool exploratory, bool held,
                       const float* candidate_features, std::uint16_t candidate_mask,
                       std::uint8_t candidate_count, std::uint8_t chosen_candidate_slot,
                       const char* apl_choice_name, bool aim_explored,
                       std::uint8_t rules_candidate_slot, std::uint8_t observed_candidate_slot,
                       std::uint16_t chosen_enemy_actor_index )
{
  sim_t* root = root_of( sim );
  if ( root->rl_translog_file_str.empty() )
    return;

  assert_write_site_ranges( sim );

  decision_record r{};
  r.damage = p->solver_damage_so_far;
  // Version 5 (260901-pb1 Task 3, D-3/D-4): the crit-expectation-corrected
  // sibling, read exactly the same way as r.damage above.
  r.damage_expected = p->solver_damage_expected_so_far;
  r.t = static_cast<float>( sim->current_time().total_seconds() );
  std::memcpy( r.obs, obs, sizeof( r.obs ) );
  r.q_margin = q_margin;
  r.top_q = top_q;   // version 3: best legal Q, or NaN -- see rl_translog.hpp's own field comment
  // Version 7 (230-04, SCOR-02, R-B): the candidate block rl_target_select CAPTURED at the
  // moment its scorer preference scored it -- never recomputed here (D-12). `candidate_features
  // == nullptr` means no block was captured this decision (a wait, an untargeted cast, or the
  // rules path was active): write an all-zero block with the no-pick sentinel rather than
  // leaving the caller to zero four parameters on every non-scorer call site.
  if ( candidate_features != nullptr )
  {
    std::memcpy( r.candidate_features, candidate_features, sizeof( r.candidate_features ) );
    r.candidate_mask = candidate_mask;
    r.candidate_count = candidate_count;
    r.chosen_candidate_slot = chosen_candidate_slot;
    // Version 11 (259-05, 259-07 R5): what the rules picked, what the observation described
    // (before any random aim) and, above, what happened -- read by the caller from the captured
    // block, three truthful numbers that differ exactly when a head or the dial moved the aim.
    r.rules_candidate_slot = rules_candidate_slot;
    r.observed_candidate_slot = observed_candidate_slot;
  }
  else
  {
    std::memset( r.candidate_features, 0, sizeof( r.candidate_features ) );
    r.candidate_mask = 0;
    r.candidate_count = 0;
    r.chosen_candidate_slot = CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK;
    r.rules_candidate_slot = CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK;
    r.observed_candidate_slot = CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK;
  }
  // 212-CR-FIX NT-02: narrowed 64->32 bits silently, unlike iteration/thread
  // above and below, which assert_write_site_ranges() refuses loudly rather
  // than truncate. Deliberate asymmetry, not an oversight: 2^32 decisions is
  // not reachable in practice (assert_write_site_ranges guards the fields
  // that ARE reachable in practice), so this is a documented exception
  // rather than a matching loud refusal.
  r.seq = static_cast<std::uint32_t>( seq );
  r.iteration = static_cast<std::uint16_t>( fight_index( sim ) );  // 261005-fight-list: the entry index in a list
  // Version 6 (228-09, D-23/TGT-08): the CHOSEN action's own stamped pick, caller-resolved
  // (lookup_pick(), never recomputed here -- this file is a writer, not a decision-maker).
  r.chosen_target_actor_index = chosen_target_actor_index;
  // Version 13 (266-21, funnel mode): the chosen enemy (the tag) at this boundary, caller-resolved --
  // 0xFFFF (CHOSEN_TARGET_SENTINEL_NO_PICK) when no tag is set.
  r.chosen_enemy_actor_index = chosen_enemy_actor_index;

  // Pack the legality mask one bit per action, in declaration order.
  // Version 4: widened uint8 -> uint32 (up to 32 actions instead of 8) to
  // pre-pay Phase 221's 24-slot action set (tstl-sylvanas plan 220-03).
  std::uint32_t packed_mask = 0;
  for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
  {
    if ( mask[ i ] )
      packed_mask |= ( 1u << i );
  }
  r.mask = packed_mask;
  r.action = static_cast<std::uint8_t>( action_index );
  // Phase 213 D-08: bit 3 (FLAG_EXPLORATORY) had no writer until now --
  // OR'd in whenever the random-action branch fired, regardless of
  // whether the drawn index happened to equal the greedy one (the bit
  // means "a random action fired", never "the action differed").
  // 260922-mfh (D4): bit 5 (FLAG_HELD) OR'd in whenever an active solver_hold_windows= window
  // cleared at least one naturally-legal action at this decision -- see solver_control.cpp's
  // row_held and this file's own top-of-file FLAG_HELD comment. `r.mask` above is always the
  // NATURAL mask (D3); this bit is the only place a hold is visible in the row at all.
  // 259-07 (R1, R5): FLAG_OBS_HEAD_AIMED (bit 7) -- the observation's target facts described the
  // HEAD's picks: a head-bearing file in `aim_obs_source = 1` mode with the force-rules switch
  // off. A property of the fight's mode, stamped on every decision row (casts and waits alike).
  const bool obs_head_aimed = sim->solver_policy_weights != nullptr &&
                              sim->solver_policy_weights->has_aim_head &&
                              sim->solver_policy_weights->aim.obs_source == 1 &&
                              !sim->target_scorer_force_rules;
  r.flags = static_cast<std::uint8_t>( ( wait_floored ? FLAG_WAIT_FLOORED : 0 ) |
                                        ( exploratory ? FLAG_EXPLORATORY : 0 ) |
                                        ( held ? FLAG_HELD : 0 ) |
                                        ( aim_explored ? FLAG_AIM_EXPLORED : 0 ) |
                                        ( obs_head_aimed ? FLAG_OBS_HEAD_AIMED : 0 ) );
  r.thread = static_cast<std::uint8_t>( sim->thread_index );
  r.kind = KIND_DECISION;
  // Version 9 (260917-pcn): the per-fight cumulative proc-roll observer totals at this
  // decision boundary -- see rl_proc_counters.hpp and rl_translog.hpp's top-of-file comment.
  std::memcpy( r.proc_attempts, p->rl_proc_counters.attempts, sizeof( r.proc_attempts ) );
  std::memcpy( r.proc_successes, p->rl_proc_counters.successes, sizeof( r.proc_successes ) );
  std::memcpy( r.proc_chance_sum, p->rl_proc_counters.chance_sum, sizeof( r.proc_chance_sum ) );
  // Version 10 (260918-cbc): the per-fight cumulative credit-by-cause totals at this decision
  // boundary -- see rl_credit.hpp and rl_translog.hpp's top-of-file comment.
  std::memcpy( r.credit_real, p->rl_credit.real, sizeof( r.credit_real ) );
  std::memcpy( r.credit_exp, p->rl_credit.exp, sizeof( r.credit_exp ) );
  // Version 13 (266-01): the chosen-enemy copy of the running totals and the credit streams at this
  // decision boundary (zero for a player that never gets a tag).
  r.damage_chosen = p->solver_chosen_damage_so_far;
  r.damage_expected_chosen = p->solver_chosen_damage_expected_so_far;
  std::memcpy( r.credit_real_chosen, p->rl_credit_chosen.real, sizeof( r.credit_real_chosen ) );
  std::memcpy( r.credit_exp_chosen, p->rl_credit_chosen.exp, sizeof( r.credit_exp_chosen ) );

  // Stage F1 (260918-atr): the first decision row THIS FIGHT writes stamps
  // p->rl_fight_first_seq -- see player.hpp's own doc comment for why this is the one site that
  // can name it. Cleared (rl_fight_first_seq_set = false) alongside rl_credit at the top of
  // every fight, player.cpp's datacollection_begin().
  if ( !p->rl_fight_first_seq_set )
  {
    p->rl_fight_first_seq = seq;
    p->rl_fight_first_seq_set = true;
  }

  // Append only -- no flush. The fight's close row flushes the whole
  // fight at once (D-13).
  append_row( root, &r );
  ++root->rl_translog_pending_decisions;
  // Stage F1 (260918-atr): captured in write order, alongside the counter immediately above --
  // record_close()'s .attr DECISION records read this back rather than assuming a gapless seq
  // run within the fight.
  root->rl_translog_pending_seqs.push_back( seq );

  // 260927-d1 (D1 census, Stage 1 Task 2): the `.apl` sidecar's own DECISION record, written
  // IMMEDIATELY (unlike `.attr` above, which batches at fight close) -- see rl_translog.hpp's APL
  // CHOICE SIDECAR section for why. No-op (one bool check, no allocation) when the option is off.
  if ( root->rl_translog_apl_stream )
  {
    rl_apl_choice::decision_record ar{};
    ar.kind = rl_apl_choice::KIND_DECISION;
    ar.seq = static_cast<std::uint32_t>( seq );  // same narrowing convention as r.seq above
    std::memset( ar.name, 0, sizeof( ar.name ) );
    if ( apl_choice_name != nullptr )
    {
      std::strncpy( ar.name, apl_choice_name, sizeof( ar.name ) - 1 );
    }
    root->rl_translog_apl_stream->write( reinterpret_cast<const char*>( &ar ), sizeof( ar ) );
    root->rl_translog_apl_stream->flush();
    if ( !*root->rl_translog_apl_stream )
    {
      throw sc_runtime_error( fmt::format(
          "rl_translog=: write to '{}.apl' failed at decision seq={}.",
          root->rl_translog_file_str, seq ) );
    }
    ++root->rl_translog_apl_decisions;
  }
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
  // Version 5 (260901-pb1 Task 3, D-3/D-4): the crit-expectation-corrected
  // sibling, complete by combat_end exactly like final_damage_total above.
  r.final_damage_expected_total = p->solver_damage_expected_so_far;
  // This IS the quantity report.json reports as "fight_length"
  // (player_collected_data.cpp:317); sim->current_time() is a different
  // quantity the engine explicitly allows to differ from it (the <=
  // assert at player_collected_data.cpp:325), and using it here would
  // make the footer's reconciliation an apples-to-oranges comparison.
  // That is why this hook is placed AFTER datacollection_end(), where
  // this field is finalised.
  r.fight_length = static_cast<float>( p->iteration_fight_length.total_seconds() );
  std::memset( r.fight_tag, 0, sizeof( r.fight_tag ) );
  std::memset( r.zero12_rest, 0, sizeof( r.zero12_rest ) );
  r.decision_count = root->rl_translog_pending_decisions;
  r.zero_mask = 0;   // version 4: widened to uint32 to sit at decision_record's own mask offset
  r.iteration = static_cast<std::uint16_t>( fight_index( sim ) );  // 261005-fight-list: the entry index in a list
  if ( sim->rl_list_active )
  {
    // 261005-fight-list (format 15): the fight's own tag -- see close_record::fight_tag. Written only in a
    // list; in every other file these eight words stay zero, byte for byte as before.
    r.fight_tag[ 0 ] = static_cast<std::uint32_t>( sim->rl_list_index );
    r.fight_tag[ 1 ] = static_cast<std::uint32_t>( sim->rl_fight_shape_index );
    r.fight_tag[ 2 ] = sim->rl_tag_setup;
    r.fight_tag[ 3 ] = sim->rl_tag_stat;
    r.fight_tag[ 4 ] = sim->rl_tag_slot;
    r.fight_tag[ 5 ] = ( sim->solver_funnel_mode ? 1u : 0u ) | ( sim->solver_random_chosen_enemy ? 2u : 0u );
    r.fight_tag[ 6 ] = sim->rl_list_crc32;
    r.fight_tag[ 7 ] = sim->rl_list_bytes;
  }
  r.zero_action = 0; // version 4: collapses the old zero62/zero63 uint8 PAIR

  // The warm-up-discard verdict, decided HERE and written into the row
  // (D-09) -- the predicate lifted verbatim from sim.cpp's own
  // datacollection_end() guard, so a discarded first fight is explicit
  // data rather than something the reader re-derives from a fight number.
  std::uint8_t flags = 0;
  // 261005-fight-list: in a list every entry is collected except the warm-up (each entry's own simulator runs
  // iterations=1, so the ordinary predicate would call every entry collected).
  const bool collected = sim->rl_list_active ? !sim->rl_list_warmup
                                             : ( sim->iterations == 1 || sim->current_iteration >= 1 );
  if ( collected )
    flags |= FLAG_COLLECTED;
  r.flags = flags;
  r.thread = static_cast<std::uint8_t>( sim->thread_index );
  r.kind = KIND_CLOSE;
  r.reserved = 0;
  r.reserved2 = 0;
  // Version 9 (260917-pcn): the fight's own FINAL proc-roll observer totals, from the same
  // solo actor `p` (not root) -- see rl_translog.hpp's top-of-file comment.
  std::memcpy( r.proc_attempts, p->rl_proc_counters.attempts, sizeof( r.proc_attempts ) );
  std::memcpy( r.proc_successes, p->rl_proc_counters.successes, sizeof( r.proc_successes ) );
  std::memcpy( r.proc_chance_sum, p->rl_proc_counters.chance_sum, sizeof( r.proc_chance_sum ) );
  // Version 10 (260918-cbc): the fight's own FINAL credit-by-cause totals, from the same solo
  // actor `p` (not root) -- see rl_translog.hpp's top-of-file comment.
  std::memcpy( r.credit_real, p->rl_credit.real, sizeof( r.credit_real ) );
  std::memcpy( r.credit_exp, p->rl_credit.exp, sizeof( r.credit_exp ) );
  // Version 13 (266-01): the fight's own FINAL chosen-enemy totals and streams, from the same solo
  // actor `p` (not root).
  r.final_damage_total_chosen = p->solver_chosen_damage_so_far;
  r.final_damage_expected_total_chosen = p->solver_chosen_damage_expected_so_far;
  std::memcpy( r.credit_real_chosen, p->rl_credit_chosen.real, sizeof( r.credit_real_chosen ) );
  std::memcpy( r.credit_exp_chosen, p->rl_credit_chosen.exp, sizeof( r.credit_exp_chosen ) );

  // Diagnostic-only orphan census (260918-cbc): names exactly which actions are landing with
  // no cause context, rather than a bare aggregate percentage. Gated on sim->debug (already the
  // convention every other per-fight debug dump in this codebase uses) OR the
  // RL_CREDIT_ORPHAN_CENSUS env var, so a normal run never pays this. Read-only over
  // `rl_orphan_damage_by_action` -- prints, never mutates.
  if ( sim->debug || std::getenv( "RL_CREDIT_ORPHAN_CENSUS" ) != nullptr )
  {
    // Stage A5 (260918-cbc): the cause stack should always be back to depth 0 at a fight's
    // close row -- every rl_cause_scope_t is a properly nested RAII guard, so a non-zero depth
    // here means a frame leaked (an exception path that skipped a destructor, or a scope that
    // outlived its intended lifetime). player_t.hpp's own assert for this is compiled out in
    // Release; this print is the only signal in a release/RelWithDebInfo build.
    fmt::print( stderr, "RL_CAUSE_STACK_DEPTH {}\n", p->rl_cause_stack.size() );
    for ( const auto& entry : p->rl_orphan_damage_by_action )
    {
      if ( entry.second > 0.0 )
        fmt::print( stderr, "RL_ORPHAN {} {}\n", entry.first, entry.second );
    }
  }

  append_row( root, &r );

  // Stage F1 (260918-atr): the .attr sidecar's own FIGHT + DECISION records for this fight,
  // written right after the close row above -- see rl_translog.hpp's ATTR SIDECAR section.
  // Guarded on rl_translog_attr_stream (always non-null by this point when the main translog is
  // active -- open_and_write_header() throws rather than leaving it null) purely defensively.
  if ( root->rl_translog_attr_stream )
  {
    const std::uint32_t n_decisions = root->rl_translog_pending_decisions;
    assert( root->rl_translog_pending_seqs.size() == n_decisions );

    double sum_own_real = 0.0;
    double sum_own_real_chosen = 0.0;  // 266-21 (.attr version 3): the chosen-enemy twin, same order
    for ( std::uint32_t i = 0; i < n_decisions; ++i )
    {
      const std::uint64_t s = root->rl_translog_pending_seqs[ i ];
      const std::size_t idx = static_cast<std::size_t>( s - p->rl_fight_first_seq );
      if ( idx < p->rl_own_real.size() )
        sum_own_real += p->rl_own_real[ idx ];
      if ( idx < p->rl_own_real_chosen.size() )
        sum_own_real_chosen += p->rl_own_real_chosen[ idx ];
    }

    // Phase 259 (plan 259-11, owner Q15/Q16, binding resolution R12, fork research option P): the
    // per-fight DECK-DRAW POOL PASS. A deck hit's payout (marked on its cause stamp, see
    // RL_CAUSE_DECK_MARK in rl_credit.hpp) is credited to whichever press happened to cause a hit --
    // a coin flip per press. Here, once per fight and before anything is written, that marked part
    // is pooled and shared out by each press's EXPECTED number of deck hits:
    //   own_exp[k] <- own_exp[k] - marked[k] + p[k] * pool / sum(p),   pool = sum(marked).
    // Every press keeps everything that was not a deck payout; the pool is the fight's realised
    // average payout per expected hit times each press's expected hits. The pass consumes no random
    // number, never touches own_real, and preserves the fight's sum of own_exp, so the repo's
    // per-fight identity (sum own_exp + background + orphan == final_damage_expected_total) stays
    // exact. A fight with draws but no hit prices its draws at zero; every draw in a fight shares
    // one per-chance price (both stated to the owner in DECISIONS.md).
    std::vector<double> attr_own_exp( n_decisions, 0.0 );
    std::vector<double> attr_deck_p( n_decisions, 0.0 );
    std::vector<double> attr_marked( n_decisions, 0.0 );
    double deck_sum_before = 0.0, deck_sum_marked = 0.0, deck_sum_p = 0.0;
    for ( std::uint32_t i = 0; i < n_decisions; ++i )
    {
      const std::uint64_t s = root->rl_translog_pending_seqs[ i ];
      const std::size_t idx = static_cast<std::size_t>( s - p->rl_fight_first_seq );
      attr_own_exp[ i ] = idx < p->rl_own_exp.size() ? p->rl_own_exp[ idx ] : 0.0;
      attr_marked[ i ] = idx < p->rl_own_exp_marked.size() ? p->rl_own_exp_marked[ idx ] : 0.0;
      attr_deck_p[ i ] = idx < p->rl_deck_p.size() ? p->rl_deck_p[ idx ] : 0.0;
      deck_sum_before += attr_own_exp[ i ];
      deck_sum_marked += attr_marked[ i ];
      deck_sum_p += attr_deck_p[ i ];
    }
    const double deck_pool = deck_sum_marked;
    const bool deck_census = std::getenv( "RL_DECK_POOL_CENSUS" ) != nullptr;
    if ( deck_sum_p > 0.0 )
    {
      if ( deck_census )
      {
        // Diagnostic only (env-gated, prints, never mutates): the pre-pass numbers of every press
        // that drew or carried marked credit, so the marked share of a hit press can be read off.
        for ( std::uint32_t i = 0; i < n_decisions; ++i )
        {
          if ( attr_deck_p[ i ] > 0.0 || attr_marked[ i ] != 0.0 )
            fmt::print( stderr, "RL_DECK_POOL iteration={} seq={} p={} own_exp_pre={} marked={}\n", r.iteration,
                        root->rl_translog_pending_seqs[ i ], attr_deck_p[ i ], attr_own_exp[ i ], attr_marked[ i ] );
        }
        fmt::print( stderr, "RL_DECK_POOL_FIGHT iteration={} pool={} sum_p={}\n", r.iteration, deck_pool, deck_sum_p );
      }
      for ( std::uint32_t i = 0; i < n_decisions; ++i )
        attr_own_exp[ i ] = attr_own_exp[ i ] - attr_marked[ i ] + attr_deck_p[ i ] * deck_pool / deck_sum_p;
    }
    else if ( deck_pool != 0.0 )
    {
      // A deck hit without any draw chance is impossible: marked credit exists only under a hit's
      // own scope, and a hit needs a success card left in the cards the consume drew from.
      throw sc_runtime_error( fmt::format(
          "rl_translog=: fight {} has a deck pool of {} marked expected credit but no decision with a "
          "deck draw chance (sum deck_p == 0) -- the deck-draw pricing accounting is broken; refusing "
          "to write a label that would silently drop that credit.",
          r.iteration, deck_pool ) );
    }
    double deck_sum_after = 0.0;
    for ( std::uint32_t i = 0; i < n_decisions; ++i )
      deck_sum_after += attr_own_exp[ i ];
    if ( std::fabs( deck_sum_after - deck_sum_before ) > 1e-9 * std::max( 1.0, std::fabs( deck_sum_before ) ) )
    {
      throw sc_runtime_error( fmt::format(
          "rl_translog=: fight {} deck-draw pool pass moved the fight's sum of own_exp from {} to {} "
          "(tolerance 1e-9 relative) -- the pass must only redistribute; refusing to write it.",
          r.iteration, deck_sum_before, deck_sum_after ) );
    }

    // Phase 266 (plan 266-21, owner F9, R9): the SAME deck-draw pool pass, run a second time on the
    // CHOSEN copy -- the marked expected credit that landed on the chosen enemy, pooled and shared out by
    // the SAME per-press expected deck hits (`attr_deck_p`, `deck_sum_p`: the draw chances do not depend
    // on which enemy a hit struck), in exactly the all-enemy pass's order of operations, so that with
    // one enemy every number here is bit-for-bit its all-enemy twin:
    //   own_exp_chosen[k] <- own_exp_chosen[k] - marked_chosen[k] + p[k] * pool_chosen / sum(p).
    std::vector<double> attr_own_exp_chosen( n_decisions, 0.0 );
    std::vector<double> attr_marked_chosen( n_decisions, 0.0 );
    double chosen_sum_before = 0.0, chosen_sum_marked = 0.0;
    for ( std::uint32_t i = 0; i < n_decisions; ++i )
    {
      const std::uint64_t s = root->rl_translog_pending_seqs[ i ];
      const std::size_t idx = static_cast<std::size_t>( s - p->rl_fight_first_seq );
      attr_own_exp_chosen[ i ] = idx < p->rl_own_exp_chosen.size() ? p->rl_own_exp_chosen[ idx ] : 0.0;
      attr_marked_chosen[ i ] = idx < p->rl_own_exp_marked_chosen.size() ? p->rl_own_exp_marked_chosen[ idx ] : 0.0;
      chosen_sum_before += attr_own_exp_chosen[ i ];
      chosen_sum_marked += attr_marked_chosen[ i ];
    }
    const double deck_pool_chosen = chosen_sum_marked;
    if ( deck_sum_p > 0.0 )
    {
      for ( std::uint32_t i = 0; i < n_decisions; ++i )
        attr_own_exp_chosen[ i ] =
            attr_own_exp_chosen[ i ] - attr_marked_chosen[ i ] + attr_deck_p[ i ] * deck_pool_chosen / deck_sum_p;
    }
    else if ( deck_pool_chosen != 0.0 )
    {
      throw sc_runtime_error( fmt::format(
          "rl_translog=: fight {} has a chosen-enemy deck pool of {} marked expected credit but no decision "
          "with a deck draw chance (sum deck_p == 0) -- the deck-draw pricing accounting is broken; refusing "
          "to write a label that would silently drop that credit.",
          r.iteration, deck_pool_chosen ) );
    }
    double chosen_sum_after = 0.0;
    for ( std::uint32_t i = 0; i < n_decisions; ++i )
      chosen_sum_after += attr_own_exp_chosen[ i ];
    if ( std::fabs( chosen_sum_after - chosen_sum_before ) > 1e-9 * std::max( 1.0, std::fabs( chosen_sum_before ) ) )
    {
      throw sc_runtime_error( fmt::format(
          "rl_translog=: fight {} deck-draw pool pass moved the fight's sum of own_exp_chosen from {} to {} "
          "(tolerance 1e-9 relative) -- the pass must only redistribute; refusing to write it.",
          r.iteration, chosen_sum_before, chosen_sum_after ) );
    }

    rl_attr::fight_record fr{};
    fr.kind = rl_attr::KIND_FIGHT;
    fr.iteration = r.iteration;
    fr.n_decisions = n_decisions;
    fr.zero = 0;
    fr.sum_own_real = sum_own_real;
    fr.deck_pool_exp = deck_pool;  // .attr version 2: this fight's pooled deck payout (plan 259-11)
    fr.sum_own_real_chosen = sum_own_real_chosen;  // .attr version 3 (266-21): the chosen-enemy twins
    fr.deck_pool_exp_chosen = deck_pool_chosen;
    root->rl_translog_attr_stream->write( reinterpret_cast<const char*>( &fr ), sizeof( fr ) );

    for ( std::uint32_t i = 0; i < n_decisions; ++i )
    {
      const std::uint64_t s = root->rl_translog_pending_seqs[ i ];
      const std::size_t idx = static_cast<std::size_t>( s - p->rl_fight_first_seq );

      rl_attr::decision_record dr{};
      dr.kind = rl_attr::KIND_DECISION;
      dr.seq = static_cast<std::uint32_t>( s );  // 212-CR-FIX NT-02's own narrowing convention
      dr.own_real = idx < p->rl_own_real.size() ? p->rl_own_real[ idx ] : 0.0;
      dr.own_exp = attr_own_exp[ i ];   // repriced by the deck-draw pool pass above (259-11)
      dr.deck_p = attr_deck_p[ i ];     // .attr version 2: this press's expected number of deck hits
      dr.own_real_chosen = idx < p->rl_own_real_chosen.size() ? p->rl_own_real_chosen[ idx ] : 0.0;  // .attr version 3 (266-21)
      dr.own_exp_chosen = attr_own_exp_chosen[ i ];  // repriced by the chosen-copy pool pass above
      root->rl_translog_attr_stream->write( reinterpret_cast<const char*>( &dr ), sizeof( dr ) );
    }

    root->rl_translog_attr_stream->flush();
    if ( !*root->rl_translog_attr_stream )
    {
      throw sc_runtime_error( fmt::format(
          "rl_translog=: write to '{}.attr' failed in record_close after {} fights -- the stream "
          "entered a failed state; refusing to continue silently.",
          root->rl_translog_file_str, root->rl_translog_fight_count ) );
    }

    // Pre-fight census diagnostic: same gate and convention as the orphan census above.
    if ( ( sim->debug || std::getenv( "RL_CREDIT_ORPHAN_CENSUS" ) != nullptr ) &&
         p->rl_attr_pre_fight_real > 0.0 )
    {
      fmt::print( stderr, "RL_ATTR_PRE_FIGHT {}\n", p->rl_attr_pre_fight_real );
    }
  }
  // 261003-s1c plan 03: the `.bcr` buff-credit sidecar's FIGHT + DECISION records for this fight, right after the `.attr` block above (BCR-FORMAT.md):
  // the credit module folds the fight's hits per decision over the translog's own decisions. No-op unless rl_buff_credit=nobody|full.
  if ( root->rl_bc_on )
    rl_buff_credit::write_fight( *root->rl_bc_state, r.iteration, collected, root->solver_funnel_mode, root->rl_translog_pending_seqs );
  root->rl_translog_pending_seqs.clear();

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
  // 260924-tlz: rows go through the zstd stream, never a raw write -- see
  // zstd_compress_and_write_rows()'s own doc comment above.
  zstd_compress_and_write_rows( root, root->rl_translog_buffer.data(), root->rl_translog_buffer.size() );
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

  // 260924-tlz: rows go through the zstd stream, never a raw write -- see
  // zstd_compress_and_write_rows()'s own doc comment above. Deliberately NOT ended as a frame
  // here (no zstd_finish_stream() call) -- this is the abort path, not a clean close (D-14's own
  // "at most the trailing partial frame is lost" guarantee is exactly what covers an unclean
  // kill after this call).
  zstd_compress_and_write_rows( root, root->rl_translog_buffer.data(), root->rl_translog_buffer.size() );
  root->rl_translog_stream->flush();
  assert_stream_ok( root, "flush_pending" );
  root->rl_translog_buffer.clear();
}

void branch_child_disable( sim_t* sim )
{
  sim_t* root = root_of( sim );
  // Deliberate leaks: the parent owns these objects' buffers and file descriptors; the child
  // must neither flush nor close them (it leaves through _exit(), so nothing else will).
  (void)root->rl_translog_stream.release();
  (void)root->rl_translog_attr_stream.release();
  (void)root->rl_translog_apl_stream.release();
  root->rl_translog_zstd_cstream = nullptr;
  root->rl_translog_file_str.clear();
}

void branch_child_reopen( sim_t* sim, const std::string& base )
{
  sim_t* root = root_of( sim );
  if ( root->rl_translog_fight_count != 0 )
  {
    throw sc_runtime_error( fmt::format(
        "rl_translog: branch_child_reopen refused -- {} fight(s) already closed in this process",
        root->rl_translog_fight_count ) );
  }
  (void)root->rl_translog_stream.release();
  (void)root->rl_translog_attr_stream.release();
  (void)root->rl_translog_apl_stream.release();
  root->rl_translog_zstd_cstream = nullptr;
  root->rl_translog_zstd_rows_in_frame = 0;
  root->rl_translog_file_str = base;
  open_and_write_header( sim );
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

  // 261005-fight-list: in a list only the LAST entry writes the footer; an earlier entry just banks its own
  // engine aggregates and leaves every stream open for the next entry (release()/adopt()).
  if ( root->rl_list_active && !root->rl_list_last )
  {
    end_entry( root );
    return;
  }

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
  if ( root->rl_list_active )
  {
    // 261005-fight-list: this process's collected_data holds only the LAST entry, so the run-level means are
    // the means over every collected entry: bank this entry too (unless it is the warm-up), then divide.
    end_entry( root );
    if ( root->rl_list_kept_count == 0 )
    {
      throw sc_runtime_error( "rl_translog: a fight list ended with no collected entry -- nothing to average." );
    }
    r.engine_run_aggregate = root->rl_list_sum_damage / static_cast<double>( root->rl_list_kept_count );
    r.mean_collected_fight_length =
        static_cast<float>( root->rl_list_sum_length / static_cast<double>( root->rl_list_kept_count ) );
  }
  r.fight_count = root->rl_translog_fight_count;
  // row_count must count the footer row ITSELF -- "does the total
  // include the footer" is exactly the kind of off-by-one that costs an
  // afternoon on the reader side.
  r.row_count = root->rl_translog_row_count + 1;
  r.footer_source = FOOTER_SOURCE_PER_FIGHT_ACCUMULATOR;
  r.summed_close_damage = root->rl_translog_summed_close_damage;
  r.collected_fight_count = root->rl_translog_collected_fight_count;
  r.zero36 = 0;
  std::memset( r.zero40, 0, sizeof( r.zero40 ) );
  r.iteration = 0xFFFF;
  r.zero_action = 0; // version 4: collapses the old zero62/zero63 uint8 PAIR
  r.flags = 0;
  r.thread = static_cast<std::uint8_t>( sim->thread_index );
  r.kind = KIND_FOOTER;
  r.reserved = 0;
  r.reserved2 = 0;
  // Version 9 (260917-pcn): no fight owns the footer -- written zero.
  std::memset( r.zero_proc_block, 0, sizeof( r.zero_proc_block ) );
  // Version 10 (260918-cbc): no fight owns the footer -- written zero.
  std::memset( r.zero_credit_block, 0, sizeof( r.zero_credit_block ) );
  // Version 13 (266-01): no fight owns the footer -- the chosen block is written zero too.
  std::memset( r.zero_chosen_block, 0, sizeof( r.zero_chosen_block ) );

  append_row( root, &r );
  // 260924-tlz: rows go through the zstd stream, never a raw write -- see
  // zstd_compress_and_write_rows()'s own doc comment above.
  zstd_compress_and_write_rows( root, root->rl_translog_buffer.data(), root->rl_translog_buffer.size() );
  // Force-close the final zstd frame (even short of the ZSTD_TRANSLOG_FRAME_ROWS cadence) --
  // this is the ONE clean-shutdown site, so the file's last frame must always be a complete,
  // independently-decodable frame rather than relying on the row cadence to have landed exactly
  // on the footer row.
  zstd_finish_stream( root );
  root->rl_translog_stream->flush();
  assert_stream_ok( root, "write_footer" );
  root->rl_translog_buffer.clear();
  root->rl_translog_stream->close();
  ZSTD_freeCStream( reinterpret_cast<ZSTD_CStream*>( root->rl_translog_zstd_cstream ) );
  root->rl_translog_zstd_cstream = nullptr;

  // Stage F1 (260918-atr): the .attr sidecar's own FOOTER record, then close it -- mirrors the
  // main stream's own footer-then-close sequence immediately above. n_fights is the SAME
  // population the main footer's own fight_count field reads (root->rl_translog_fight_count),
  // already incremented by every record_close() call this run made.
  if ( root->rl_translog_attr_stream )
  {
    rl_attr::footer_record afr{};
    afr.kind = rl_attr::KIND_FOOTER;
    afr.n_fights = root->rl_translog_fight_count;
    afr.zero1 = 0;
    afr.zero2 = 0;
    afr.zero_f = 0.0;
    afr.zero_f2 = 0.0;  // .attr version 2 (259-05)
    root->rl_translog_attr_stream->write( reinterpret_cast<const char*>( &afr ), sizeof( afr ) );
    root->rl_translog_attr_stream->flush();
    if ( !*root->rl_translog_attr_stream )
    {
      throw sc_runtime_error( fmt::format(
          "rl_translog=: write to '{}.attr' failed writing the footer.", root->rl_translog_file_str ) );
    }
    root->rl_translog_attr_stream->close();
  }

  // 260927-d1 (D1 census, Stage 1 Task 2): the `.apl` sidecar's own FOOTER record, then close it
  // -- mirrors the `.attr` sidecar's own footer-then-close sequence immediately above.
  // n_decisions is the count of DECISION records this run actually wrote (root->
  // rl_translog_apl_decisions, incremented once per record_decision() call while the stream is
  // open), NOT the main translog's own decision count -- the two are expected to agree by
  // construction (both increment once per record_decision() call) but this sidecar names its own
  // count rather than borrowing the main stream's, so a reader can catch the two disagreeing.
  if ( root->rl_translog_apl_stream )
  {
    rl_apl_choice::footer_record pfr{};
    pfr.kind = rl_apl_choice::KIND_FOOTER;
    pfr.n_decisions = root->rl_translog_apl_decisions;
    std::memset( pfr.zero, 0, sizeof( pfr.zero ) );
    root->rl_translog_apl_stream->write( reinterpret_cast<const char*>( &pfr ), sizeof( pfr ) );
    root->rl_translog_apl_stream->flush();
    if ( !*root->rl_translog_apl_stream )
    {
      throw sc_runtime_error( fmt::format(
          "rl_translog=: write to '{}.apl' failed writing the footer.", root->rl_translog_file_str ) );
    }
    root->rl_translog_apl_stream->close();
  }
}

// ---- Fight-list support (quick 261005-mix, plan 01) ----

std::uint32_t file_format_version( const sim_t* sim )
{
  return sim->rl_list_active ? FORMAT_VERSION_FIGHT_LIST : FORMAT_VERSION;
}

int fight_index( const sim_t* sim )
{
  return sim->rl_list_active ? sim->rl_list_index : sim->current_iteration;
}

namespace
{
// CRC-32/IEEE (reflected, polynomial 0xEDB88320), the algorithm zlib.crc32 implements.
struct crc_table_t
{
  std::uint32_t t[ 256 ];
  constexpr crc_table_t() : t()
  {
    for ( std::uint32_t i = 0; i < 256; ++i )
    {
      std::uint32_t c = i;
      for ( int k = 0; k < 8; ++k )
        c = ( c & 1u ) ? ( 0xEDB88320u ^ ( c >> 1 ) ) : ( c >> 1 );
      t[ i ] = c;
    }
  }
};
constexpr crc_table_t CRC_TABLE{};
} // anonymous namespace

std::uint32_t crc32( const unsigned char* data, std::size_t n )
{
  std::uint32_t c = 0xFFFFFFFFu;
  for ( std::size_t i = 0; i < n; ++i )
    c = CRC_TABLE.t[ ( c ^ data[ i ] ) & 0xFFu ] ^ ( c >> 8 );
  return c ^ 0xFFFFFFFFu;
}

void end_entry( sim_t* sim )
{
  sim_t* root = root_of( sim );
  if ( root->rl_translog_file_str.empty() || !root->rl_list_active || root->rl_list_warmup )
    return;
  player_t* p = solo_actor( root );
  root->rl_list_sum_damage += p->collected_data.compound_dmg.mean();
  root->rl_list_sum_length += p->collected_data.fight_length.mean();
  ++root->rl_list_kept_count;
}

void release( sim_t* sim, carry_t& carry )
{
  sim_t* root = root_of( sim );
  if ( carry.stream || carry.attr_stream || carry.apl_stream || carry.zstd_cstream != nullptr )
  {
    throw sc_runtime_error( "rl_translog: release refused -- the carry already holds a writer." );
  }
  carry.stream = std::move( root->rl_translog_stream );
  carry.buffer = std::move( root->rl_translog_buffer );
  carry.row_count = root->rl_translog_row_count;
  carry.fight_count = root->rl_translog_fight_count;
  carry.collected_fight_count = root->rl_translog_collected_fight_count;
  carry.pending_decisions = root->rl_translog_pending_decisions;
  carry.pending_seqs = std::move( root->rl_translog_pending_seqs );
  carry.summed_close_damage = root->rl_translog_summed_close_damage;
  carry.attr_stream = std::move( root->rl_translog_attr_stream );
  carry.apl_stream = std::move( root->rl_translog_apl_stream );
  carry.apl_decisions = root->rl_translog_apl_decisions;
  carry.zstd_cstream = root->rl_translog_zstd_cstream;
  carry.zstd_outbuf = std::move( root->rl_translog_zstd_outbuf );
  carry.zstd_rows_in_frame = root->rl_translog_zstd_rows_in_frame;
  carry.sum_collected_damage = root->rl_list_sum_damage;
  carry.sum_collected_length = root->rl_list_sum_length;
  carry.collected_entry_count = root->rl_list_kept_count;

  // leave the source in a defined, empty state: nothing it owns can close or free the moved stream
  root->rl_translog_stream.reset();
  root->rl_translog_attr_stream.reset();
  root->rl_translog_apl_stream.reset();
  root->rl_translog_zstd_cstream = nullptr;
  root->rl_translog_buffer.clear();
  root->rl_translog_pending_seqs.clear();
  root->rl_translog_zstd_outbuf.clear();
  root->rl_translog_row_count = 0;
  root->rl_translog_fight_count = 0;
  root->rl_translog_collected_fight_count = 0;
  root->rl_translog_pending_decisions = 0;
  root->rl_translog_summed_close_damage = 0.0;
  root->rl_translog_apl_decisions = 0;
  root->rl_translog_zstd_rows_in_frame = 0;
  root->rl_list_sum_damage = 0.0;
  root->rl_list_sum_length = 0.0;
  root->rl_list_kept_count = 0;
}

void adopt( sim_t* sim, carry_t& carry )
{
  sim_t* root = root_of( sim );
  if ( root->rl_translog_stream || root->rl_translog_attr_stream || root->rl_translog_apl_stream ||
       root->rl_translog_zstd_cstream != nullptr || root->rl_translog_fight_count != 0 ||
       root->rl_translog_row_count != 0 )
  {
    throw sc_runtime_error( "rl_translog: adopt refused -- the destination sim already holds a writer." );
  }
  root->rl_translog_stream = std::move( carry.stream );
  root->rl_translog_buffer = std::move( carry.buffer );
  root->rl_translog_row_count = carry.row_count;
  root->rl_translog_fight_count = carry.fight_count;
  root->rl_translog_collected_fight_count = carry.collected_fight_count;
  root->rl_translog_pending_decisions = carry.pending_decisions;
  root->rl_translog_pending_seqs = std::move( carry.pending_seqs );
  root->rl_translog_summed_close_damage = carry.summed_close_damage;
  root->rl_translog_attr_stream = std::move( carry.attr_stream );
  root->rl_translog_apl_stream = std::move( carry.apl_stream );
  root->rl_translog_apl_decisions = carry.apl_decisions;
  root->rl_translog_zstd_cstream = carry.zstd_cstream;
  root->rl_translog_zstd_outbuf = std::move( carry.zstd_outbuf );
  root->rl_translog_zstd_rows_in_frame = carry.zstd_rows_in_frame;
  root->rl_list_sum_damage = carry.sum_collected_damage;
  root->rl_list_sum_length = carry.sum_collected_length;
  root->rl_list_kept_count = carry.collected_entry_count;

  carry.stream.reset();
  carry.attr_stream.reset();
  carry.apl_stream.reset();
  carry.zstd_cstream = nullptr;
  carry.buffer.clear();
  carry.pending_seqs.clear();
  carry.zstd_outbuf.clear();
}

} // namespace rl_translog
