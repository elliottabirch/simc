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
//
// The replay option -- tstl-sylvanas phase 253, plan 253-02 (REP-01/02/03/04, D-01/D-02/D-15/
// D-16/D-20). rl_rng_replay=<path> is a NO-WRITE MODE of this SAME recorder_t object: whenever
// EITHER rl_rng_record= or rl_rng_replay= is set, the object exists and tracks the same press
// frames and roller registry (one address rule serves both), but only the file, entry building,
// buffering, the RESALT/COUNTER/FIGHT_BEGIN/FIGHT_END entries, the footer and the sidecar run
// when rl_rng_record= is ALSO set (recorder_t::writing_). At every draw, when a fight window is
// open, on_draw() looks up the address (outer press kind, the press roller's key string, press
// number, the drawing roller's key string) in the CURRENT fight's table -- built sequentially,
// one fight at a time, from the recording named by rl_rng_replay= (D-15: a 10,000-fight
// recording is tens of GB and cannot be held in memory at once) -- and returns the recorded raw
// number when one is unused there, else the live number unchanged (real() returns whatever
// on_draw() returns -- see util/rng.hpp). Rollers are matched between the recording and this run
// by their sidecar key STRING, never by numeric id (D-02): replay_state_t interns every distinct
// key string it sees into a small integer once, at load, and looks up each live roller's index
// lazily on first use.

#include "sim/rl_rng_record.hpp"

#include "action/action.hpp"
#include "action/action_state.hpp"
#include "player/player.hpp"
#include "player/stats.hpp"
#include "sim/proc_rng.hpp"
#include "sim/raid_event.hpp"
#include "sim/rl_proc_counters.hpp"
#include "sim/sim.hpp"
#include "util/io.hpp"
#include "util/rng.hpp"
#include "util/util.hpp"

#include "fmt/format.h"

#include "rapidjson/document.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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

// The replay-only fresh-number stream's salt (253-02, D-17) -- XOR-ed into the sim's own seed so
// this stream never collides with any real per-source stream's own seed derivation.
inline constexpr std::uint64_t REPLAY_FRESH_SALT = 0x253253253253253ull;

// The 64-bit pattern of a double's bit representation -- used as the replay value set's key
// (253-02, D-17) so set membership is exact bit-for-bit equality, never a float tolerance.
inline std::uint64_t replay_bits_of( double d )
{
  std::uint64_t bits;
  std::memcpy( &bits, &d, sizeof( bits ) );
  return bits;
}

// Draws one double from a RAW xoshiro256plus_t engine, using the exact bit-to-double conversion
// basic_rng_t<Engine>::real() itself uses (253-02, D-17's "the replay-only stream ... same
// bit-to-double conversion real() uses"). Never goes through basic_rng_t -- calling any
// basic_rng_t method here would re-enter the on_draw() hook.
inline double replay_raw_draw( rng::xoshiro256plus_t& engine )
{
  std::uint64_t ui64 = engine.next();
  ui64 &= 0x000fffffffffffffULL;
  ui64 |= 0x3ff0000000000000ULL;
  union { std::uint64_t ui64; double d; } u;
  u.ui64 = ui64;
  return u.d - 1.0;
}

// True when `key` names a stream this run's replay never matches against (253-02, D-20): the
// exploration stream's own fixed key, or a lazily-numbered "unregistered|N" stream (not stable
// across runs).
inline bool replay_key_is_excluded( const std::string& key )
{
  static constexpr std::string_view explore_key = "sim|solver_explore_rng";
  static constexpr std::string_view unregistered_prefix = "unregistered|";
  return key == explore_key || key.compare( 0, unregistered_prefix.size(), unregistered_prefix ) == 0;
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

  // Whole-recording tallies (250-03, A-04) -- NOT cleared at fight_begin() (unlike the per-fight
  // fields above): compare_report() only ever runs on a one-fight recording, so per-fight vs.
  // whole-recording makes no observable difference there, and keeping them whole-recording avoids
  // a second reset site to keep in sync. Only ever non-zero for cls == action.
  std::uint32_t first_cast_presses = 0;
  std::uint32_t early_return_presses = 0;
};

// Replay state (253-02, REP-01/D-15/D-16/D-20) -- owned by recorder_t only when
// rl_rng_replay= is set (recorder_t::replay_). Holds the loader's read position, the interned
// key-string table, the CURRENT fight's address table (sequential per-fight loading, D-15), and
// the whole-run totals the "Interfaces this plan adds" stdout line prints.
struct replay_state_t
{
  // Index 0 is a fixed sentinel meaning "no press" -- never assigned to any real key string
  // (D-16's "a trigger field of 0 means 'no press', not sim|_rng"). Real key strings are
  // interned starting at 1. KEY_INDEX_UNCACHED/KEY_INDEX_NOT_IN_RECORDING never collide with a
  // real index (both are far above any realistic roller count).
  static constexpr std::uint32_t NO_PRESS_KEY_INDEX = 0u;
  static constexpr std::uint32_t KEY_INDEX_UNCACHED = 0xFFFFFFFEu;
  static constexpr std::uint32_t KEY_INDEX_NOT_IN_RECORDING = 0xFFFFFFFFu;

  std::string path;
  std::string address;   // "outer" (default/empty) or "inner" (D-03) -- picks which press frame
                          // (live and recorded alike) on_draw()/the loader read; kept ONLY for the
                          // printed stdout/sidecar value -- the hot-path comparison uses is_inner
                          // below (254-06 Task 2, REP-06 speed round 2).
  // Cached once by replay_init() alongside `address` above, instead of re-comparing the string on
  // every draw (replay_active_frame(), called from on_draw() up to twice per draw) and once per
  // fight (replay_fight_begin()'s own `use_inner`). Since D-21 the address is hard-wired
  // fork-wide to "outer", so this is always false today -- profiled to cost real instructions
  // anyway (a memcmp plus the surrounding call overhead on every draw, $S/speed/profile-r1-replay.txt),
  // for a value that can never change within one run. Pure no-behavior-change cache: `address` and
  // `is_inner` are set together, in the one place either is ever written, and every reader of the
  // old `address == "inner"` comparison switches to this bool.
  bool is_inner = false;
  std::ifstream in;
  std::uint64_t footer_fight_count = 0;   // the recording's own FOOTER.press (FIGHT_END count)
  std::uint64_t next_read_offset = 0;     // where the sequential per-fight reader resumes
  bool exhausted = false;                 // no more fight windows in the recording -- every
                                           // later fight_begin() finds nothing and stays live
  bool window_open = false;
  bool stopped = false;    // REP-04/D-05: set by mark_resalt(), cleared at the next fight_begin()
                            // -- from here on every draw this fight is live, counted after_stop.
  std::uint64_t fights_served = 0;        // fight windows successfully loaded so far

  // Key interning (D-02): every distinct key string the sidecar declares gets one small index,
  // in first-seen order. sidecar_id_to_key_index maps the RECORDING's own roller ids (the
  // sidecar's "id" field) to that index; live_key_index_cache maps THIS RUN's own live roller
  // ids (recorder_t::rollers_ indices) to the same index space, filled lazily on first use from
  // the live roller's own key string. sidecar_id_excluded parallels sidecar_id_to_key_index:
  // true when that RECORDED roller's own key names the exploration stream or an
  // "unregistered|" stream (D-20) -- its numbers never enter `table`, only `value_set`.
  std::unordered_map<std::string, std::uint32_t> key_string_to_index;
  std::vector<std::uint32_t> sidecar_id_to_key_index;
  std::vector<bool> sidecar_id_excluded;
  std::vector<std::uint32_t> live_key_index_cache;

  // One fight's address table: address -> the recorded raw numbers at that address, in record
  // order, plus how many have been handed out. Rebuilt from scratch by fight_begin(), read by
  // on_draw(), closed by fight_end() -- never held for more than one fight at a time (D-15).
  struct entry_run_t
  {
    std::vector<double> raw;
    std::size_t next_unused = 0;
  };
  using address_key_t = std::tuple<std::uint8_t, std::uint32_t, std::uint32_t, std::uint32_t>;

  // 254-06 Task 2 (REP-06 speed round 1): fixed hash functor for address_key_t, replacing the
  // tree-ordered std::map with an unordered_map on the two hottest replay operations -- the
  // per-fight rebuild (replay_fight_begin(), which re-inserts one entry per kept ROLL every
  // single fight) and the per-draw lookup (on_draw()'s table.find()). Profiled on the
  // one-address build (254-06, $S/speed/profile-start-replay.txt): replay_fight_begin() alone
  // cost 5.99% of the replaying run's total instruction count, plus a further slice inside
  // on_draw()'s own 4.37%, plus roughly 9% of total instructions in malloc/free churn and
  // std::_Rb_tree_insert_and_rebalance from repeated per-fight node allocation and tree
  // rebalancing. A hash table gives O(1) amortized insert/find instead of O(log n) tree
  // operations. Per-key vector order (raw.push_back() in record order) and the fight_end()
  // unused-count sum (a plain accumulation) are both independent of map iteration/insertion
  // order, so this is a pure data-structure swap with byte-identical output.
  struct address_key_hash_t
  {
    std::size_t operator()( const address_key_t& k ) const noexcept
    {
      const auto& [ kind, trigger, press, roller ] = k;
      std::size_t h = static_cast<std::size_t>( kind );
      h = h * 1000003u ^ static_cast<std::size_t>( trigger );
      h = h * 1000003u ^ static_cast<std::size_t>( press );
      h = h * 1000003u ^ static_cast<std::size_t>( roller );
      return h;
    }
  };
  std::unordered_map<address_key_t, entry_run_t, address_key_hash_t> table;

  // This fight's full raw-number set (D-17's fresh-number rule): every kept ROLL entry's raw
  // number, EXCLUDED ones included (D-20's "their recorded numbers never enter the table [but]
  // do enter the value set"). Keyed by bit pattern -- see replay_bits_of().
  std::unordered_set<std::uint64_t> value_set;

  // Replay-only streams for the fresh-number rule (D-17), keyed by the LIVE roller id (never the
  // key index -- two different live rollers absent from the recording would otherwise collapse
  // onto the single KEY_INDEX_NOT_IN_RECORDING sentinel and wrongly share one stream). Lazily
  // seeded on first use, cleared at every fight_begin() ("seeded lazily per fight").
  std::unordered_map<std::uint32_t, rng::xoshiro256plus_t> fresh_streams;

  // Per-class totals for the "rl_rng_replay class=" stdout lines.
  struct class_counts_t
  {
    std::uint64_t reused = 0;
    std::uint64_t fresh = 0;
    std::uint64_t excluded = 0;
    std::uint64_t after_stop = 0;
  };
  // 254-06 Task 2 (REP-06 speed round 2, re-deriving 253-03's prepared speed patch against
  // the post-254-04 source): fixed-size array, not std::map -- roller_class_e has exactly
  // ROLLER_CLASS_COUNT values, so indexing by static_cast<uint8_t>(cls) is O(1) with no
  // per-lookup tree traversal, on every replaying draw's on_draw() hot path. Same counts,
  // same values, same effective iteration order (ascending enum value, identical to
  // std::map<roller_class_e, ...>'s own sorted order) -- a pure data-structure swap.
  std::array<class_counts_t, ROLLER_CLASS_COUNT> per_class{};

  // One fight's counters -- reset at fight_begin(), accumulated during the open window, pushed
  // to fights_detail (with its own stop_time_ms) at fight_end(). This is the per-fight row the
  // sidecar's "replay" block ("Interfaces this plan adds") writes; whole-run totals below are
  // simply the running sum of every field except stop_time_ms.
  struct fight_counts_t
  {
    std::uint64_t reused = 0;
    std::uint64_t fresh_no_address = 0;
    std::uint64_t fresh_used_up = 0;
    std::uint64_t fresh_roller_not_in_recording = 0;
    std::uint64_t excluded = 0;
    std::uint64_t after_stop = 0;
    std::uint64_t live_was_recorded = 0;
    std::uint64_t recorded_unused = 0;
    std::int64_t stop_time_ms = -1;   // -1 when no re-salt happened this fight
  };
  fight_counts_t current_fight;
  std::vector<fight_counts_t> fights_detail;   // one entry per fight_end(), in fight order

  // Whole-run totals -- the exact fields the "Interfaces this plan adds" stdout totals line
  // prints, in that order. Every field except outside_fight is the running sum of the matching
  // current_fight field, accumulated as each fight closes; outside_fight has no fight to belong
  // to by definition and is bumped directly.
  std::uint64_t reused = 0;
  std::uint64_t fresh_no_address = 0;
  std::uint64_t fresh_used_up = 0;
  std::uint64_t fresh_roller_not_in_recording = 0;
  std::uint64_t excluded = 0;
  std::uint64_t after_stop = 0;
  std::uint64_t outside_fight = 0;
  std::uint64_t live_was_recorded = 0;
  std::uint64_t recorded_unused = 0;
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
  double on_draw( std::uint32_t& roller_slot, const std::uint64_t& draw_counter,
                  std::uint32_t roller_override, double raw ) override;
  void on_roll_outcome( std::uint32_t roller, std::uint64_t draw_ordinal, double chance,
                         bool outcome ) override;
  void on_no_draw_roll( std::uint32_t& roller_slot, const std::uint64_t& draw_counter ) override;

  // ---- lifecycle, called by the free functions below ----
  // open() replaces the old open_and_write_header() name (253-02): it now constructs whichever
  // of writing/replay mode(s) root_'s options ask for -- see this method's own definition for
  // the writing_/replay_ split. Retained under its original free-function name at the bottom of
  // this file (open_and_write_header(sim_t*)) to keep sim.cpp's call site unchanged.
  void open();
  void fight_begin();
  void fight_end();
  void write_footer();
  void mark_resalt();
  std::uint32_t register_roller( rng::rng_t& stream, std::string_view key, roller_class_e cls,
                                  const action_t* action );
  void register_raid_event( raid_event_t& event );

  // ---- Press-tag interface (plan 250-03, REC-04) -- called only from rl_press_scope_t's
  // out-of-line ctors/dtor and the free functions at the bottom of this file. ----
  press_frame_t current_outer() const { return current_outer_; }
  press_frame_t current_inner() const { return current_inner_; }
  const action_t* current_inner_owner() const { return current_inner_owner_; }
  void open_execute( action_t* action, const action_state_t* carried_state );
  void open_tick( action_t* action );
  void open_refill( proc_rng_t* deck );
  void annotate_draw( rng::rng_t& stream, std::uint64_t before, double chance, bool outcome, std::uint8_t label );
  void label_last_roll( std::uint8_t label, double chance, bool success );
  void restore_from_state( const action_state_t* state );
  void restore_frames( const press_frame_t& outer, const press_frame_t& inner, const action_t* inner_owner );
  void stamp_state( action_state_t* state );
  bool inner_owner_is( const action_t* action ) const { return current_inner_owner_ == action; }
  void note_early_return( const action_t* action );

private:
  std::uint32_t ensure_registered( std::uint32_t& roller_slot, const std::uint64_t& draw_counter );
  std::uint32_t ensure_action_roller( rng::rng_t& stream );
  void append( const record& r );
  void assert_stream_ok( const char* where );
  void write_sidecar();

  // ---- Replay (253-02, REP-01/D-15/D-16). No-op bodies are never reached when replay_ is null
  // (every call site below checks `replay_` first); see each definition for the exact contract.
  void replay_init();
  void replay_fight_begin();
  void replay_fight_end();
  // Refuses (fight-scoped message) when sidecar_id is out of the sidecar's own declared range --
  // the shared bounds check both accessors below call first.
  void replay_check_sidecar_id( std::uint32_t sidecar_id ) const;
  std::uint32_t replay_key_index_for_sidecar_roller( std::uint32_t sidecar_id ) const;
  bool replay_sidecar_excluded( std::uint32_t sidecar_id ) const;
  std::uint32_t replay_key_index_for_live_roller( std::uint32_t live_id );
  // The press frame replay_->address names (outer or inner, D-03), read identically at load and
  // draw time.
  const press_frame_t& replay_active_frame() const;
  // Builds the address key from whichever frame replay_->address names (outer or inner, D-03) --
  // used identically at load time (with the recording's own frame fields) and at draw time (with
  // the LIVE current_outer_/current_inner_ frames).
  replay_state_t::address_key_t replay_address_for_draw( std::uint32_t drawing_roller );
  // True when this draw is excluded from replay altogether (D-20): the drawing roller is the
  // exploration stream or an "unregistered|" stream, or an open press's trigger roller is.
  bool replay_is_excluded_draw( std::uint32_t drawing_roller ) const;
  // The fresh-number rule (D-17): the next number, from a per-live-roller replay-only stream,
  // that is NOT one of this fight's recorded numbers.
  double replay_take_fresh_number( std::uint32_t live_roller_id );

  sim_t* root_;
  // True only when rl_rng_record= is set (253-02): gates every write-side concern (the stream,
  // the buffer, entry building, the sidecar, the footer's record/close/sidecar work). Registration
  // and the press-frame guards run regardless -- see fight_begin()/fight_end()/on_draw() below.
  bool writing_ = false;
  // Non-null only when rl_rng_replay= is set (253-02) -- see replay_state_t's own doc comment.
  std::unique_ptr<replay_state_t> replay_;
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

  // Per-label matched/unmatched call tallies (plan 250-03, R-09) -- whole-recording, not cleared
  // at fight_begin() (mirrors first_cast_presses/early_return_presses's own whole-recording
  // rationale: diagnostic context, never itself part of a reconciliation check). std::map for a
  // stable, sorted sidecar write order; label ids are sparse (rl_proc::id+1, or 255) so this is
  // never more than ~21 entries.
  struct label_tally_t
  {
    std::uint32_t matched = 0;
    std::uint32_t unmatched = 0;
  };
  std::map<std::uint8_t, label_tally_t> label_calls_;

  // ---- Press-tag state (plan 250-03, REC-04). Process-wide, not per-player: the recorder
  // forces threads=1 (R-08) whenever it is active, so there is exactly one call stack to track.
  // rl_press_scope_t's ctors/dtor save and restore these three fields, giving correct LIFO
  // nesting from the call stack itself -- exactly rl_cause_scope_t's own pattern (sim/
  // rl_credit.hpp), just recorder-owned instead of player-owned. Default frames (kind ==
  // TRIGGER_KIND_NONE) mean "no press active" -- a roll drawn in that state carries trigger 0 and
  // is reported by class, never hidden (REC-03, D-07). ----
  press_frame_t current_outer_{};
  press_frame_t current_inner_{};
  const action_t* current_inner_owner_ = nullptr;

  // Per-fight ROLL position counters (250-03, REC-04), keyed by (trigger_kind, trigger, press,
  // roller) for the outer window and the inner-field quadruple for the inner window -- exactly
  // the key rng_record.py's check() reconciles a ROLL entry's position/inner_position against.
  // std::map (not unordered_map): tuple has no default std::hash, and this is never a hot-path
  // container (one lookup per roll, not per frame). Cleared at fight_begin() alongside the other
  // per-fight position maps this class already keeps.
  using window_key_t = std::tuple<std::uint8_t, std::uint32_t, std::uint32_t, std::uint32_t>;
  std::map<window_key_t, std::uint32_t> outer_window_positions_;
  std::map<window_key_t, std::uint32_t> inner_window_positions_;
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

double recorder_t::on_draw( std::uint32_t& roller_slot, const std::uint64_t& draw_counter,
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
  // This roller's total ROLL-entry tally (COUNTER's own @60 field, "ROLL entries, this roller")
  // -- distinct from the WINDOW-scoped position below (250-03, REC-04): this counts every ROLL
  // ever assigned to this roller across the whole fight, the window position counts only rolls
  // within the SAME (trigger_kind, trigger, press) window.
  ++logical_entry.roll_entries;

  double value = raw;

  // Replay (253-02, REP-01/D-04/D-16/D-17/D-20/REP-04): a no-write mode of this same object. The
  // table probe happens BEFORE the writing-only record fields below are built, because when both
  // options are on the reused value becomes THIS run's own ROLL entry raw field too (D-16's
  // "write the returned value") -- so a self-replay's recording is byte-identical to the one it
  // replayed. Nothing here runs once the recorder has stopped replaying this fight (REP-04).
  if ( replay_ && replay_->window_open && !replay_->stopped )
  {
    replay_state_t::class_counts_t& class_counts = replay_->per_class[ static_cast<std::size_t>( logical_entry.cls ) ];
    replay_state_t::fight_counts_t& fight_counts = replay_->current_fight;

    if ( replay_is_excluded_draw( logical_clamped ) )
    {
      // D-20: the exploration stream, an "unregistered|" stream, or a press opened by one --
      // always the live number, never subject to the fresh-number rule below.
      ++replay_->excluded;
      ++fight_counts.excluded;
      ++class_counts.excluded;
    }
    else
    {
      const replay_state_t::address_key_t key = replay_address_for_draw( logical_clamped );
      const std::uint32_t roller_key = std::get<3>( key );
      bool matched = false;

      if ( roller_key == replay_state_t::KEY_INDEX_NOT_IN_RECORDING )
      {
        // D-04: the drawing roller's key string does not appear in the recording at all.
        ++replay_->fresh_roller_not_in_recording;
        ++fight_counts.fresh_roller_not_in_recording;
      }
      else
      {
        const auto it = replay_->table.find( key );
        if ( it == replay_->table.end() )
        {
          ++replay_->fresh_no_address;
          ++fight_counts.fresh_no_address;
        }
        else if ( it->second.next_unused >= it->second.raw.size() )
        {
          ++replay_->fresh_used_up;
          ++fight_counts.fresh_used_up;
        }
        else
        {
          value = it->second.raw[ it->second.next_unused ];
          ++it->second.next_unused;
          ++replay_->reused;
          ++fight_counts.reused;
          ++class_counts.reused;
          matched = true;
        }
      }

      if ( !matched )
      {
        // D-17: a fresh draw whose live number is one of THIS FIGHT's recorded numbers (at any
        // address, not only this one) is replaced by the next number from a replay-only stream
        // for this roller, skipping any number also in that set -- so one raw number never
        // decides two rolls in the same fight.
        if ( replay_->value_set.count( replay_bits_of( value ) ) )
        {
          value = replay_take_fresh_number( logical_clamped );
          ++replay_->live_was_recorded;
          ++fight_counts.live_was_recorded;
        }
        ++class_counts.fresh;
      }
    }
  }
  else if ( replay_ && replay_->window_open && replay_->stopped )
  {
    // REP-04/D-05: from the re-salt moment on, every draw this fight is live -- the recorder's
    // own after_resalt_ flag already drops these entries at load time on the NEXT replay of this
    // recording; here it is simply live-and-counted, never looked up.
    ++replay_->after_stop;
    ++replay_->current_fight.after_stop;
    ++replay_->per_class[ static_cast<std::size_t>( logical_entry.cls ) ].after_stop;
  }
  else if ( replay_ && !replay_->window_open )
  {
    // No fight window is open at all (not yet started, exhausted, or between fight_end() and the
    // next fight_begin()) -- always live, counted separately from every fresh-inside-a-fight
    // reason.
    ++replay_->outside_fight;
  }

  // 250-03 (REC-04): position is 0-based WITHIN the active (trigger_kind, trigger, press, roller)
  // window, not a running total per roller -- exactly the key rng_record.py's check() reconciles
  // against. Before this plan wires any press guard, current_outer_/current_inner_ both sit at
  // their default TRIGGER_KIND_NONE/0/0 frame for every roll, so every roller's one window key is
  // (NONE, 0, 0, roller) and position reduces to the prior per-roller running count -- byte-
  // identical to plan 250-01/250-02's behavior for any recording made before a press guard ever
  // opens. Writing-only (253-02): the ROLL record's own position/inner_position fields are
  // meaningless without a file to put them in.
  if ( writing_ )
  {
    const window_key_t outer_key{ current_outer_.kind, current_outer_.trigger, current_outer_.press, logical_clamped };
    const std::uint32_t position = outer_window_positions_[ outer_key ]++;

    const window_key_t inner_key{ current_inner_.kind, current_inner_.trigger, current_inner_.press, logical_clamped };
    const std::uint32_t inner_position = inner_window_positions_[ inner_key ]++;

    record rec{};
    rec.raw = value;
    rec.chance = CHANCE_NONE;
    rec.time_ms = static_cast<std::int64_t>( root_->current_time().total_millis() );
    rec.draw_ordinal = draw_counter;
    rec.roller = logical_clamped;
    rec.stream = stream_id;
    rec.trigger = current_outer_.trigger;
    rec.press = current_outer_.press;
    rec.position = position;
    rec.inner_trigger = current_inner_.trigger;
    rec.inner_press = current_inner_.press;
    rec.inner_position = inner_position;
    rec.iteration = current_iteration_field_;
    rec.kind = KIND_ROLL;
    rec.trigger_kind = current_outer_.kind;
    rec.inner_trigger_kind = current_inner_.kind;
    rec.outcome = OUTCOME_NOT_A_ROLL;
    rec.label = LABEL_NONE;
    rec.flags = after_resalt_ ? FLAG_AFTER_RESALT : 0;

    last_roll_stream_ = stream_id;
    last_roll_ordinal_ = draw_counter;
    append( rec );
    ++fight_roll_entries_;
    ++roll_entry_total_;
  }

  return value;
}

void recorder_t::on_roll_outcome( std::uint32_t roller, std::uint64_t draw_ordinal, double chance,
                                   bool outcome )
{
  // Writing-only (253-02): patches a ROLL entry the buffer holds -- there is nothing to patch in
  // replay-only mode (no file, no buffer).
  if ( !writing_ )
    return;

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

void recorder_t::annotate_draw( rng::rng_t& stream, std::uint64_t before, double chance, bool outcome,
                                 std::uint8_t label )
{
  // Writing-only (253-02): patches a ROLL entry the buffer holds.
  if ( !writing_ )
    return;

  // Exactly one draw happened on `stream` since `before` -- a one-result attack table (no crit
  // roll possible) draws nothing and is correctly left un-annotated; more than one draw means
  // this wasn't a single, unambiguous attack-table roll (defensive; not expected from the call
  // site's own placement).
  const std::uint64_t after = stream.rl_draw_counter();
  if ( after != before + 1 )
    return;

  const std::uint32_t roller = stream.rl_roller_id();
  if ( roller != last_roll_stream_ || after != last_roll_ordinal_ || buffer_.size() < RECORD_SIZE )
    return;

  record rec;
  std::memcpy( &rec, buffer_.data() + buffer_.size() - RECORD_SIZE, RECORD_SIZE );
  if ( rec.kind != KIND_ROLL || rec.stream != roller || rec.draw_ordinal != after )
    return;

  rec.chance = chance;
  rec.outcome = outcome ? OUTCOME_SUCCESS : OUTCOME_FAIL;
  rec.label = label;
  rec.flags = static_cast<std::uint8_t>( rec.flags | FLAG_ANNOTATED );
  std::memcpy( buffer_.data() + buffer_.size() - RECORD_SIZE, &rec, RECORD_SIZE );
}

void recorder_t::label_last_roll( std::uint8_t label, double chance, bool success )
{
  // Writing-only (253-02): patches a ROLL entry the buffer holds; the per-label tally below is
  // also whole-recording bookkeeping that means nothing without a recording.
  if ( !writing_ )
    return;

  bool matched = false;
  if ( buffer_.size() >= RECORD_SIZE )
  {
    record rec;
    std::memcpy( &rec, buffer_.data() + buffer_.size() - RECORD_SIZE, RECORD_SIZE );
    if ( rec.kind == KIND_ROLL && rec.label == LABEL_NONE && rec.chance == chance &&
         rec.outcome == ( success ? OUTCOME_SUCCESS : OUTCOME_FAIL ) )
    {
      rec.label = label;
      std::memcpy( buffer_.data() + buffer_.size() - RECORD_SIZE, &rec, RECORD_SIZE );
      matched = true;
    }
  }
  label_tally_t& tally = label_calls_[ label ];
  if ( matched )
    ++tally.matched;
  else
    ++tally.unmatched;
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

void recorder_t::register_raid_event( raid_event_t& event )
{
  // Idempotent: both raid_event_t::reset() overrides call the base reset() first (which is where
  // this is called from), so a nested event's own override and the base's call for that SAME
  // event both reach here -- keep the existing number rather than assigning a second one.
  if ( event.rl_roller_id != rng::RL_ROLLER_UNREGISTERED && event.rl_roller_id < rollers_.size() )
    return;

  const auto id = static_cast<std::uint32_t>( rollers_.size() );
  rollers_.emplace_back();
  roller_entry_t& entry = rollers_.back();
  entry.key = fmt::format( "sim|raid_event|{}|{}|{}", event.internal_id, event.type,
                            event.name.empty() ? event.type : event.name );
  entry.cls = roller_class_e::raid_event;
  // No stream/draw_counter of its own (R-01) -- fight_end()'s begin/end-ordinal reads are already
  // null-safe (`r.draw_counter ? *r.draw_counter : ...`), same as any other logical-only roller.
  event.rl_roller_id = id;

  auto& ids = keys_seen_[ entry.key ];
  ids.push_back( id );
}

std::uint32_t recorder_t::ensure_action_roller( rng::rng_t& stream )
{
  // Mirrors ensure_registered() above, but for an action's OWN per-source stream rather than a
  // stream reached through rng::rl_draw_sink::on_draw()'s roller_slot reference: action_t (and
  // every other owner of its own rng::rng_t member) exposes only rl_roller_id()/rl_set_roller_id()
  // by value, not a mutable reference to the private storage, so this cannot reuse
  // ensure_registered() verbatim. Same lazy-numbering contract: an action whose stream was never
  // registered by action_t::reset() (per_source_rng off, or a stream this plan never wires) gets
  // numbered here, at its first press, under a synthetic key -- exactly "unregistered" (D-05).
  const std::uint32_t existing = stream.rl_roller_id();
  if ( existing != rng::RL_ROLLER_UNREGISTERED && existing < rollers_.size() )
    return existing;

  const auto id = static_cast<std::uint32_t>( rollers_.size() );
  rollers_.emplace_back();
  roller_entry_t& entry = rollers_.back();
  entry.key = fmt::format( "unregistered|{}", id );
  entry.cls = roller_class_e::unregistered;
  entry.draw_counter = &stream.rl_draw_counter();
  const std::uint64_t counter = stream.rl_draw_counter();
  entry.begin_ordinal = counter > 0 ? counter - 1 : 0;
  stream.rl_set_roller_id( id );
  return id;
}

// ---- Press-tag interface (plan 250-03, REC-04) -- see rl_rng_record.hpp's rl_press_scope_t doc
// comment for the full model. Every method below is called only through rl_press_scope_t's
// out-of-line ctors/dtor or the small free functions at the bottom of this file, all of which
// already guard on root->rl_rng_recorder being non-null -- these methods themselves assume the
// recorder IS on (D-17: no rng call, no event, no reorder, no dispatch change anywhere here). ----

void recorder_t::open_execute( action_t* action, const action_state_t* carried_state )
{
  ++action->rl_press_count_;
  const std::uint32_t roller = ensure_action_roller( action->source_rng_ );
  const press_frame_t new_frame{ TRIGGER_KIND_EXECUTE, roller, action->rl_press_count_ };

  if ( carried_state && carried_state->rl_press_outer_kind != TRIGGER_KIND_NONE )
  {
    // A deferred dispatch (a proc's schedule_execute(), a tick_action's schedule_execute(
    // tick_state )) that already carries a stamped outer frame keeps it -- the outer press stays
    // whichever button (or tick) originally led here, exactly like rl_resolve_cause()'s own
    // carried-cause branches (action.cpp) run before any fresh resolution.
    current_outer_ = press_frame_t{ carried_state->rl_press_outer_kind, carried_state->rl_press_outer_trigger,
                                     carried_state->rl_press_outer_number };
  }
  else if ( current_outer_.kind == TRIGGER_KIND_NONE )
  {
    // No enclosing press and nothing carried -- THIS press is the outermost one (a genuine
    // top-level button press).
    current_outer_ = new_frame;
  }
  // else: an outer press is already open (a proc/secondary fired synchronously inside another
  // action's own execute scope) -- keep it; this new press only ever becomes the INNER frame.

  current_inner_ = new_frame;
  current_inner_owner_ = action;

  if ( roller < rollers_.size() )
    rollers_[ roller ].execute_presses = action->rl_press_count_;

  if ( action->player && action->player->first_cast && action->harmful && roller < rollers_.size() )
    ++rollers_[ roller ].first_cast_presses;
}

void recorder_t::open_tick( action_t* action )
{
  ++action->rl_tick_count_;
  const std::uint32_t roller = ensure_action_roller( action->source_rng_ );
  const press_frame_t new_frame{ TRIGGER_KIND_TICK, roller, action->rl_tick_count_ };

  if ( current_outer_.kind == TRIGGER_KIND_NONE )
  {
    // A standalone scheduled tick (dot_tick_event_t, dot_end_event_t) with no enclosing press --
    // the tick is its own outer AND inner frame (A-must-have: "every DoT tick ... is its own tick
    // press ... numbered per action per fight").
    current_outer_ = new_frame;
  }
  // else: a tick-zero or tick-on-application tick fired SYNCHRONOUSLY from within the applying
  // cast's own execute/impact dispatch (dot_t::check_tick_zero(), called from apply/refresh) --
  // current_outer_ is already that cast's outer press; keep it (A-07: "its own tick press nested
  // inside the applying cast's outer press").

  current_inner_ = new_frame;
  current_inner_owner_ = nullptr;  // ticks are never an execute()'s "owner frame" (A-04)

  if ( roller < rollers_.size() )
    rollers_[ roller ].tick_presses = action->rl_tick_count_;
}

void recorder_t::restore_from_state( const action_state_t* state )
{
  // "When it is not stamped, change nothing" -- a state whose outer kind is still the
  // never-stamped default (TRIGGER_KIND_NONE, action_state_t::initialize()'s own reset) carries
  // no press to restore; leave current_outer_/current_inner_ exactly as the caller's own scope
  // left them (D-07).
  if ( !state || state->rl_press_outer_kind == TRIGGER_KIND_NONE )
    return;

  current_outer_ = press_frame_t{ state->rl_press_outer_kind, state->rl_press_outer_trigger,
                                   state->rl_press_outer_number };
  current_inner_ = press_frame_t{ state->rl_press_inner_kind, state->rl_press_inner_trigger,
                                   state->rl_press_inner_number };
  current_inner_owner_ = nullptr;  // a restored frame is never itself an execute()'s owner frame
}

void recorder_t::restore_frames( const press_frame_t& outer, const press_frame_t& inner, const action_t* inner_owner )
{
  current_outer_ = outer;
  current_inner_ = inner;
  current_inner_owner_ = inner_owner;
}

void recorder_t::open_refill( proc_rng_t* deck )
{
  if ( !deck || deck->type() != RNG_SHUFFLE )
    return;
  const std::uint32_t roller = deck->rl_roller_id();
  if ( roller == rng::RL_ROLLER_UNREGISTERED || roller >= rollers_.size() )
    return;  // per_source_rng off, or this deck was never registered -- nothing to tag against

  ++rollers_[ roller ].refills;
  const press_frame_t new_frame{ TRIGGER_KIND_REFILL, roller, rollers_[ roller ].refills };
  // A refill wins over any enclosing press, unconditionally (REC-06, D-08, R-04) -- unlike
  // open_execute()'s carried-state/already-open-outer branches, there is no "keep the enclosing
  // frame" case here.
  current_outer_ = new_frame;
  current_inner_ = new_frame;
  current_inner_owner_ = nullptr;
}

void recorder_t::stamp_state( action_state_t* state )
{
  if ( !state )
    return;
  state->rl_press_outer_kind    = current_outer_.kind;
  state->rl_press_outer_trigger = current_outer_.trigger;
  state->rl_press_outer_number  = current_outer_.press;
  state->rl_press_inner_kind    = current_inner_.kind;
  state->rl_press_inner_trigger = current_inner_.trigger;
  state->rl_press_inner_number  = current_inner_.press;
}

void recorder_t::note_early_return( const action_t* action )
{
  const std::uint32_t roller = action->source_rng_.rl_roller_id();
  if ( roller != rng::RL_ROLLER_UNREGISTERED && roller < rollers_.size() )
    ++rollers_[ roller ].early_return_presses;
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

void recorder_t::open()
{
  writing_ = !root_->rl_rng_record_file_str.empty();

  if ( writing_ )
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
    assert_stream_ok( "open" );
  }

  // 253-02 (D-16): replay is loaded here too, whether or not writing_ is also true (record while
  // replaying is allowed -- D-01).
  if ( !root_->rl_rng_replay_file_str.empty() )
    replay_init();

  // Runs in BOTH modes: the registry (and its key strings) is what replay matches addresses
  // against, whether or not this run is ALSO writing a recording of its own.
  register_roller( root_->rng(), "sim|_rng", roller_class_e::sim_shared, nullptr );
  register_roller( root_->solver_explore_rng, "sim|solver_explore_rng", roller_class_e::sim_explore, nullptr );
}

// ---- Replay (253-02, REP-01/D-15/D-16/D-20). Every method below assumes replay_ is non-null --
// callers (open()/fight_begin()/fight_end()/on_draw()) all check `replay_`/`replay_->window_open`
// first. The address choice was removed in phase 254 (Gate 2 verdict): replay is hard-wired to
// the outer press address; there is no longer a switch. ----

void recorder_t::replay_init()
{
  replay_ = std::make_unique<replay_state_t>();
  replay_state_t& rp = *replay_;
  rp.path = root_->rl_rng_replay_file_str;
  rp.address = "outer";  // hard-wired (phase 254 Gate 2 verdict); the address choice was removed
  rp.is_inner = false;   // cached alongside rp.address above (254-06 Task 2, REP-06 speed round 2)

  rp.in.open( rp.path, std::ios::in | std::ios::binary );
  if ( !rp.in.is_open() )
  {
    throw sc_runtime_error( fmt::format(
        "rl_rng_replay: cannot read '{}': the file does not exist or cannot be opened.", rp.path ) );
  }

  file_header h{};
  rp.in.read( reinterpret_cast<char*>( &h ), sizeof( h ) );
  if ( !rp.in || static_cast<std::size_t>( rp.in.gcount() ) != sizeof( h ) )
  {
    throw sc_runtime_error( fmt::format(
        "rl_rng_replay: cannot read '{}': shorter than the {}-byte header.", rp.path, HEADER_SIZE ) );
  }
  if ( std::memcmp( h.magic, MAGIC, sizeof( MAGIC ) ) != 0 )
  {
    throw sc_runtime_error( fmt::format( "rl_rng_replay: cannot read '{}': wrong magic.", rp.path ) );
  }
  if ( h.format_version != FORMAT_VERSION )
  {
    throw sc_runtime_error( fmt::format(
        "rl_rng_replay: cannot read '{}': format_version {} is not supported (expected {}).",
        rp.path, h.format_version, FORMAT_VERSION ) );
  }
  if ( h.record_size != RECORD_SIZE || h.header_size != HEADER_SIZE )
  {
    throw sc_runtime_error( fmt::format(
        "rl_rng_replay: cannot read '{}': record_size/header_size disagree with this build's own "
        "{}/{}.",
        rp.path, RECORD_SIZE, HEADER_SIZE ) );
  }

  // Find the FOOTER: it is always the last record in the file (250-01-PLAN.md's own contract) --
  // seek to the end, read the last RECORD_SIZE bytes, and require it to be a KIND_FOOTER entry.
  rp.in.seekg( 0, std::ios::end );
  const std::streamoff file_size = rp.in.tellg();
  if ( file_size < static_cast<std::streamoff>( HEADER_SIZE ) ||
       ( static_cast<std::uint64_t>( file_size ) - HEADER_SIZE ) % RECORD_SIZE != 0 )
  {
    throw sc_runtime_error( fmt::format(
        "rl_rng_replay: cannot read '{}': truncated (not a whole number of {}-byte entries after "
        "the header).",
        rp.path, RECORD_SIZE ) );
  }

  record footer{};
  rp.in.seekg( file_size - static_cast<std::streamoff>( RECORD_SIZE ) );
  rp.in.read( reinterpret_cast<char*>( &footer ), RECORD_SIZE );
  if ( footer.kind != KIND_FOOTER )
  {
    throw sc_runtime_error( fmt::format(
        "rl_rng_replay: cannot read '{}': the last record is not a FOOTER (missing or misplaced).",
        rp.path ) );
  }
  rp.footer_fight_count = footer.press;   // FOOTER's own @44 field: the FIGHT_END count

  // 253-02 Task 2: this run must not ask for more fights than the recording can serve.
  // root_->iterations is 0 when the run has no fixed iteration count (e.g. only rl_iteration_seeds
  // is set) -- that shape has no single "how many fights does this run ask for" number, so it is
  // not checked here.
  if ( root_->iterations > 0 &&
       static_cast<std::uint64_t>( root_->iterations ) > rp.footer_fight_count )
  {
    throw sc_runtime_error( fmt::format(
        "rl_rng_replay: '{}' holds {} fights but this run asks for {}.",
        rp.path, rp.footer_fight_count, root_->iterations ) );
  }

  // The sidecar: PATH.rollers.json, read with the vendored rapidjson (D-02's key strings).
  const std::string sidecar_path = rp.path + ".rollers.json";
  std::ifstream sidecar_stream( sidecar_path, std::ios::in | std::ios::binary );
  if ( !sidecar_stream.is_open() )
  {
    throw sc_runtime_error( fmt::format(
        "rl_rng_replay: cannot read '{}': sidecar '{}' does not exist.", rp.path, sidecar_path ) );
  }
  const std::string sidecar_text( ( std::istreambuf_iterator<char>( sidecar_stream ) ),
                                   std::istreambuf_iterator<char>() );
  rapidjson::Document doc;
  doc.Parse( sidecar_text.c_str() );
  if ( doc.HasParseError() || !doc.IsObject() || !doc.HasMember( "rollers" ) || !doc[ "rollers" ].IsArray() )
  {
    throw sc_runtime_error( fmt::format(
        "rl_rng_replay: cannot read '{}': sidecar '{}' is missing or is not parseable JSON with a "
        "'rollers' array.",
        rp.path, sidecar_path ) );
  }

  const auto& rollers_json = doc[ "rollers" ];
  for ( const auto& entry : rollers_json.GetArray() )
  {
    if ( !entry.IsObject() || !entry.HasMember( "id" ) || !entry[ "id" ].IsUint() ||
         !entry.HasMember( "key" ) || !entry[ "key" ].IsString() )
    {
      throw sc_runtime_error( fmt::format(
          "rl_rng_replay: cannot read '{}': sidecar '{}' has a roller entry missing id/key.",
          rp.path, sidecar_path ) );
    }
    const std::uint32_t id = entry[ "id" ].GetUint();
    const std::string key = entry[ "key" ].GetString();

    std::uint32_t key_index;
    const auto found = rp.key_string_to_index.find( key );
    if ( found != rp.key_string_to_index.end() )
    {
      key_index = found->second;
    }
    else
    {
      // Distinct key strings are interned in first-seen order, starting at 1 -- 0 is the fixed
      // NO_PRESS_KEY_INDEX sentinel, never a real key (D-16).
      key_index = static_cast<std::uint32_t>( rp.key_string_to_index.size() ) + 1;
      rp.key_string_to_index.emplace( key, key_index );
    }

    if ( id >= rp.sidecar_id_to_key_index.size() )
    {
      rp.sidecar_id_to_key_index.resize( id + 1, replay_state_t::KEY_INDEX_NOT_IN_RECORDING );
      rp.sidecar_id_excluded.resize( id + 1, false );
    }
    rp.sidecar_id_to_key_index[ id ] = key_index;
    // D-20: the exploration stream and any "unregistered|" stream are excluded from replay
    // altogether -- their numbers still enter the fresh-number rule's value set (D-17), just
    // never the address table (Task 2's on_draw/replay_fight_begin both consult this).
    rp.sidecar_id_excluded[ id ] = replay_key_is_excluded( key );
  }

  // The sequential per-fight reader starts right after the header (D-15).
  rp.next_read_offset = HEADER_SIZE;
  rp.in.clear();
  rp.in.seekg( HEADER_SIZE, std::ios::beg );
}

void recorder_t::replay_check_sidecar_id( std::uint32_t sidecar_id ) const
{
  const replay_state_t& rp = *replay_;
  if ( sidecar_id >= rp.sidecar_id_to_key_index.size() )
  {
    throw sc_runtime_error( fmt::format(
        "rl_rng_replay: '{}' fight {}: entry names roller {} but the sidecar declares only {} "
        "roller(s).",
        rp.path, rp.fights_served, sidecar_id, rp.sidecar_id_to_key_index.size() ) );
  }
}

std::uint32_t recorder_t::replay_key_index_for_sidecar_roller( std::uint32_t sidecar_id ) const
{
  replay_check_sidecar_id( sidecar_id );
  return replay_->sidecar_id_to_key_index[ sidecar_id ];
}

bool recorder_t::replay_sidecar_excluded( std::uint32_t sidecar_id ) const
{
  replay_check_sidecar_id( sidecar_id );
  return replay_->sidecar_id_excluded[ sidecar_id ];
}

std::uint32_t recorder_t::replay_key_index_for_live_roller( std::uint32_t live_id )
{
  replay_state_t& rp = *replay_;
  if ( live_id >= rp.live_key_index_cache.size() )
    rp.live_key_index_cache.resize( live_id + 1, replay_state_t::KEY_INDEX_UNCACHED );

  std::uint32_t& slot = rp.live_key_index_cache[ live_id ];
  if ( slot == replay_state_t::KEY_INDEX_UNCACHED )
  {
    if ( live_id < rollers_.size() )
    {
      const auto found = rp.key_string_to_index.find( rollers_[ live_id ].key );
      slot = found != rp.key_string_to_index.end() ? found->second
                                                    : replay_state_t::KEY_INDEX_NOT_IN_RECORDING;
    }
    else
    {
      slot = replay_state_t::KEY_INDEX_NOT_IN_RECORDING;
    }
  }
  return slot;
}

const press_frame_t& recorder_t::replay_active_frame() const
{
  // D-03: which press address replay reads -- hard-wired to outer (the address choice was
  // removed in phase 254, Gate 2 verdict). Read identically at load time (replay_fight_begin(),
  // off the recording's own outer_* fields) and at draw time (here, off the LIVE current_outer_).
  return replay_->is_inner ? current_inner_ : current_outer_;
}

bool recorder_t::replay_is_excluded_draw( std::uint32_t drawing_roller ) const
{
  if ( drawing_roller < rollers_.size() && replay_key_is_excluded( rollers_[ drawing_roller ].key ) )
    return true;
  const press_frame_t& frame = replay_active_frame();
  if ( frame.kind != TRIGGER_KIND_NONE && frame.trigger < rollers_.size() &&
       replay_key_is_excluded( rollers_[ frame.trigger ].key ) )
    return true;
  return false;
}

replay_state_t::address_key_t recorder_t::replay_address_for_draw( std::uint32_t drawing_roller )
{
  const press_frame_t& frame = replay_active_frame();
  const std::uint8_t kind = frame.kind;
  const std::uint32_t trigger_key = kind == TRIGGER_KIND_NONE
      ? replay_state_t::NO_PRESS_KEY_INDEX
      : replay_key_index_for_live_roller( frame.trigger );
  const std::uint32_t press = kind == TRIGGER_KIND_NONE ? 0u : frame.press;
  const std::uint32_t roller_key = replay_key_index_for_live_roller( drawing_roller );
  return replay_state_t::address_key_t{ kind, trigger_key, press, roller_key };
}

double recorder_t::replay_take_fresh_number( std::uint32_t live_roller_id )
{
  replay_state_t& rp = *replay_;
  auto it = rp.fresh_streams.find( live_roller_id );
  if ( it == rp.fresh_streams.end() )
  {
    const std::string key = live_roller_id < rollers_.size() ? rollers_[ live_roller_id ].key
                                                              : std::string();
    const std::uint64_t seed = rng::per_source_seed( root_->seed ^ REPLAY_FRESH_SALT,
                                                       root_->thread_index,
                                                       root_->rng_iteration_index(), key );
    rng::xoshiro256plus_t engine;
    engine.seed( seed );
    it = rp.fresh_streams.emplace( live_roller_id, engine ).first;
  }

  double value;
  std::size_t guard = 0;
  do
  {
    value = replay_raw_draw( it->second );
    if ( ++guard > 1000000 )
    {
      throw sc_runtime_error( fmt::format(
          "rl_rng_replay: '{}': the fresh-number stream for roller {} could not find a value "
          "outside this fight's recorded set after 1,000,000 draws.",
          rp.path, live_roller_id ) );
    }
  }
  while ( rp.value_set.count( replay_bits_of( value ) ) );
  return value;
}

void recorder_t::replay_fight_begin()
{
  replay_state_t& rp = *replay_;
  rp.table.clear();
  rp.value_set.clear();
  rp.fresh_streams.clear();
  rp.stopped = false;
  rp.current_fight = replay_state_t::fight_counts_t{};
  rp.window_open = false;
  if ( rp.exhausted )
    return;

  rp.in.clear();
  rp.in.seekg( static_cast<std::streamoff>( rp.next_read_offset ) );

  record rec{};
  bool found_begin = false;
  while ( rp.in.read( reinterpret_cast<char*>( &rec ), RECORD_SIZE ) )
  {
    if ( rec.kind == KIND_FIGHT_BEGIN )
    {
      found_begin = true;
      break;
    }
    if ( rec.kind == KIND_FOOTER )
      break;   // no more fights -- the FOOTER is always last (D-15's trailing-window handling)
    // Any other kind here means a prior window closed mid-file without reaching this reader's
    // own bookkeeping -- cannot happen from a recording this reader itself produced.
  }

  if ( !found_begin )
  {
    // The recording holds no more fight windows -- this and every later fight of this run plays
    // live (D-15's "the extra reset after the last fight opens no window"; replay_init() already
    // refuses a run that asks for MORE fights than the recording holds, so reaching this point
    // mid-run means the run's own request matched the recording's count exactly).
    rp.exhausted = true;
    rp.next_read_offset = static_cast<std::uint64_t>( rp.in.tellg() );
    return;
  }

  const bool use_inner = rp.is_inner;  // 254-06 Task 2 (REP-06 speed round 2): cached, see rp.is_inner's own doc comment
  bool found_end = false;
  while ( rp.in.read( reinterpret_cast<char*>( &rec ), RECORD_SIZE ) )
  {
    if ( rec.kind == KIND_FIGHT_END )
    {
      found_end = true;
      break;
    }
    if ( rec.kind < KIND_ROLL || rec.kind > KIND_FOOTER )
    {
      throw sc_runtime_error( fmt::format(
          "rl_rng_replay: '{}' fight {}: entry has invalid kind {}.",
          rp.path, rp.fights_served, static_cast<int>( rec.kind ) ) );
    }
    if ( rec.kind == KIND_ROLL && !( rec.flags & FLAG_AFTER_RESALT ) )
    {
      const std::uint8_t trigger_kind = use_inner ? rec.inner_trigger_kind : rec.trigger_kind;
      const std::uint32_t trigger_id = use_inner ? rec.inner_trigger : rec.trigger;
      const std::uint32_t press_number = use_inner ? rec.inner_press : rec.press;

      // D-17: every kept ROLL's raw number joins the fight's value set, EXCLUDED ones included --
      // the fresh-number rule must avoid ties with numbers it can never reuse too.
      rp.value_set.insert( replay_bits_of( rec.raw ) );

      replay_check_sidecar_id( rec.roller );
      const bool roller_excluded = replay_sidecar_excluded( rec.roller );
      const bool press_excluded = trigger_kind != TRIGGER_KIND_NONE && replay_sidecar_excluded( trigger_id );
      if ( !roller_excluded && !press_excluded )
      {
        // D-15/D-16: for no press (trigger_kind NONE) the trigger-roller key is the fixed
        // NO_PRESS_KEY_INDEX sentinel, never the recorded roller-0 key -- "a trigger field of 0
        // means 'no press', not sim|_rng".
        const std::uint32_t trigger_key = trigger_kind == TRIGGER_KIND_NONE
            ? replay_state_t::NO_PRESS_KEY_INDEX
            : replay_key_index_for_sidecar_roller( trigger_id );
        const std::uint32_t roller_key = replay_key_index_for_sidecar_roller( rec.roller );
        const replay_state_t::address_key_t key{ trigger_kind, trigger_key, press_number, roller_key };
        rp.table[ key ].raw.push_back( rec.raw );
      }
    }
    // COUNTER, RESALT and after-resalt ROLL entries are skipped -- dropped at load (D-05/D-15).
  }

  if ( !found_end )
  {
    throw sc_runtime_error( fmt::format(
        "rl_rng_replay: '{}' fight {}: no FIGHT_END for the window that started reading at offset "
        "{} -- truncated or malformed recording.",
        rp.path, rp.fights_served, rp.next_read_offset ) );
  }

  rp.next_read_offset = static_cast<std::uint64_t>( rp.in.tellg() );
  rp.window_open = true;
  ++rp.fights_served;
}

void recorder_t::replay_fight_end()
{
  replay_state_t& rp = *replay_;
  if ( !rp.window_open )
    return;
  std::uint64_t unused_this_fight = 0;
  for ( const auto& kv : rp.table )
    unused_this_fight += kv.second.raw.size() - kv.second.next_unused;
  rp.recorded_unused += unused_this_fight;
  rp.current_fight.recorded_unused = unused_this_fight;
  rp.fights_detail.push_back( rp.current_fight );
  rp.table.clear();
  rp.window_open = false;
}

void recorder_t::fight_begin()
{
  // See last_fight_end_iteration_'s own doc comment: sim_t::iterate() calls reset() one extra
  // time, after the loop exits, with current_iteration unchanged from the last real iteration.
  // That call is not a new fight -- skip it whole (no FIGHT_BEGIN entry, no tally reset) so its
  // incidental draws land in the ALREADY-CLOSED prior fight's accounting instead of opening a
  // phantom window with no matching FIGHT_END/COUNTER. This guard runs in BOTH modes (253-02):
  // it is what keeps replay from ever trying to load a window for that trailing extra reset
  // (D-15's "the extra reset after the last fight opens no window").
  if ( root_->current_iteration == last_fight_end_iteration_ )
    return;

  after_resalt_ = false;
  fight_roll_entries_ = 0;
  fight_counter_entries_ = 0;
  // 250-03 (REC-04): per-fight ROLL position windows. Every rl_press_scope_t properly restores
  // current_outer_/current_inner_ via RAII before the next event fires, so both are already back
  // at their default (no press active) frame here between fights -- reset anyway, defensively,
  // rather than relying on that invariant holding across every future call site. Runs in BOTH
  // modes -- the press frames are what replay's address computation reads too (253-02).
  current_outer_ = press_frame_t{};
  current_inner_ = press_frame_t{};
  current_inner_owner_ = nullptr;
  outer_window_positions_.clear();
  inner_window_positions_.clear();

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

  // Writing-only (253-02): the FIGHT_BEGIN entry belongs to the recording file.
  if ( writing_ )
  {
    record rec{};
    rec.raw = 0.0;
    rec.chance = CHANCE_NONE;
    rec.time_ms = static_cast<std::int64_t>( root_->current_time().total_millis() );
    rec.iteration = current_iteration_field_;
    rec.kind = KIND_FIGHT_BEGIN;
    rec.outcome = OUTCOME_NOT_A_ROLL;
    append( rec );
  }

  // Replay (253-02, D-15): load THIS fight's window from the recording, sequentially, one fight
  // at a time -- never the whole recording (a 10,000-fight recording is tens of GB).
  if ( replay_ )
    replay_fight_begin();
}

void recorder_t::fight_end()
{
  // Writing-only (253-02): every COUNTER row, the FIGHT_END entry, and the per-fight flush all
  // belong to the recording file.
  if ( writing_ )
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
      // A-50 fix (orchestrator ledger, wave 1 review): the reader (rng_record.py check()) reads
      // COUNTER's @36 `stream` as the roller's OWN physical stream number -- the same value its
      // ROLL entries carry in `stream` -- and reads the raid-event marker from the @71 `flags`
      // byte, not from `stream`. The prior code wrote FLAG_LOGICAL_ONLY (0x02) into `stream` for a
      // raid-event roller and 0 into `stream` for EVERY other roller regardless of its own number,
      // which collapsed every own-stream roller's COUNTER onto the wrong key (stream 0) and made
      // `check` report bogus "last ROLL ordinal is not the COUNTER's end" mismatches on wave 1's
      // own recordings (e.g. patchwerk-t1-c-s1.rec, 133 problems). Fixed at the source, not the
      // reader: an own-stream roller's COUNTER carries its own stream number and flags 0; a
      // raid-event (logical-only, no stream of its own) roller carries stream 0 and flags
      // FLAG_LOGICAL_ONLY -- raid events are Task 2's territory (R-01), but this fix must land now
      // so Task 1's own recordings pass `check` (the sole gate this task's verify block runs).
      if ( r.cls == roller_class_e::raid_event )
      {
        rec.stream = 0u;                  // @36 -- draws stay on the shared stream (R-01)
        rec.flags = FLAG_LOGICAL_ONLY;    // @71
      }
      else
      {
        rec.stream = static_cast<std::uint32_t>( roller_id );  // @36 -- this roller's own stream
      }
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
  }

  // Runs in BOTH modes -- the trailing-reset guard at the top of fight_begin() depends on this
  // being kept current regardless of writing_ (253-02).
  ++fight_count_;
  last_fight_end_iteration_ = root_->current_iteration;

  if ( writing_ )
  {
    // Per-fight flush -- one write, one flush, mirrors rl_translog.cpp's record_close() (D-13's
    // syscall-cost rationale applies here too, not a crash-loss bound).
    stream_->write( reinterpret_cast<const char*>( buffer_.data() ),
                     static_cast<std::streamsize>( buffer_.size() ) );
    stream_->flush();
    assert_stream_ok( "fight_end" );
    buffer_.clear();
  }

  // Replay (253-02, D-15): close this fight's window -- tally its unhanded numbers, then free
  // the table before the next fight_begin() rebuilds it.
  if ( replay_ )
    replay_fight_end();
}

void recorder_t::mark_resalt()
{
  // Writing-only (253-02): the RESALT entry belongs to the recording file. after_resalt_ itself
  // is set unconditionally -- Task 2 wires replay's own stop behavior off the same flag.
  if ( writing_ )
  {
    record rec{};
    rec.raw = 0.0;
    rec.chance = CHANCE_NONE;
    rec.time_ms = static_cast<std::int64_t>( root_->current_time().total_millis() );
    rec.iteration = current_iteration_field_;
    rec.kind = KIND_RESALT;
    rec.outcome = OUTCOME_NOT_A_ROLL;
    append( rec );
  }
  after_resalt_ = true;

  // REP-04/D-05: from this moment on, replay stops for the rest of THIS fight; the fight's own
  // stop time is kept for the sidecar's per-fight row. Only the first re-salt this fight counts
  // (a second call before the next fight_begin() would otherwise overwrite an earlier stop time).
  if ( replay_ && !replay_->stopped )
  {
    replay_->stopped = true;
    replay_->current_fight.stop_time_ms = static_cast<std::int64_t>( root_->current_time().total_millis() );
  }
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
              << ", \"harmful\": " << ( a->harmful ? "true" : "false" )
              // 250-03 (REC-04, A-04): whole-recording tallies rng_record.py's compare_report()
              // reads to explain an execute-count mismatch (first_cast) or annotate one as
              // diagnostic context (early returns) -- see roller_entry_t's own doc comment.
              << ", \"firstCastPresses\": " << r.first_cast_presses
              << ", \"earlyReturnPresses\": " << r.early_return_presses;
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
  sidecar << "], \"labelNames\": {";
  for ( std::uint32_t i = 0; i < rl_proc::COUNT; ++i )
  {
    if ( i != 0 )
      sidecar << ", ";
    sidecar << "\"" << ( i + 1 ) << "\": \"" << rl_proc::NAMES[ i ] << "\"";
  }
  sidecar << ", \"" << static_cast<int>( LABEL_SWING_TABLE ) << "\": \"swing_attack_table\"}";
  sidecar << ", \"labelCalls\": {";
  bool first_label = true;
  for ( const auto& kv : label_calls_ )
  {
    if ( !first_label )
      sidecar << ", ";
    first_label = false;
    sidecar << "\"" << static_cast<int>( kv.first ) << "\": {\"matched\": " << kv.second.matched
            << ", \"unmatched\": " << kv.second.unmatched << "}";
  }
  sidecar << "}";

  // 253-02 Task 2 ("Interfaces this plan adds"): only present when replay_ is ALSO active (record
  // while replaying, D-01) -- a plain recording (no rl_rng_replay=) carries no "replay" key at
  // all, matching the interface's own "only when both options are set" note.
  if ( replay_ )
  {
    const replay_state_t& rp = *replay_;
    sidecar << ", \"replay\": {\"source\": \"" << json_escape( rp.path ) << "\", \"address\": \""
            << rp.address << "\", \"totals\": {"
            << "\"fights\": " << rp.fights_served << ", \"reused\": " << rp.reused
            << ", \"fresh_no_address\": " << rp.fresh_no_address
            << ", \"fresh_used_up\": " << rp.fresh_used_up
            << ", \"fresh_roller_not_in_recording\": " << rp.fresh_roller_not_in_recording
            << ", \"excluded\": " << rp.excluded << ", \"after_stop\": " << rp.after_stop
            << ", \"outside_fight\": " << rp.outside_fight
            << ", \"live_was_recorded\": " << rp.live_was_recorded
            << ", \"recorded_unused\": " << rp.recorded_unused << "}, \"byClass\": {";
    bool first_class = true;
    for ( std::size_t class_idx = 0; class_idx < ROLLER_CLASS_COUNT; ++class_idx )
    {
      const replay_state_t::class_counts_t& counts = rp.per_class[ class_idx ];
      // Matches the old std::map's own membership rule exactly: only classes actually touched
      // (any of the four counters incremented at least once) get an entry -- an untouched class
      // in the fixed array has all four counters at their zero default.
      if ( counts.reused == 0 && counts.fresh == 0 && counts.excluded == 0 && counts.after_stop == 0 )
        continue;
      if ( !first_class )
        sidecar << ", ";
      first_class = false;
      const roller_class_e cls = static_cast<roller_class_e>( class_idx );
      sidecar << "\"" << class_name( cls ) << "\": {\"reused\": " << counts.reused
              << ", \"fresh\": " << counts.fresh << ", \"excluded\": " << counts.excluded
              << ", \"after_stop\": " << counts.after_stop << "}";
    }
    sidecar << "}, \"fights\": [";
    for ( std::size_t i = 0; i < rp.fights_detail.size(); ++i )
    {
      const replay_state_t::fight_counts_t& fc = rp.fights_detail[ i ];
      if ( i != 0 )
        sidecar << ", ";
      sidecar << "{\"fight\": " << i << ", \"reused\": " << fc.reused
              << ", \"fresh_no_address\": " << fc.fresh_no_address
              << ", \"fresh_used_up\": " << fc.fresh_used_up
              << ", \"fresh_roller_not_in_recording\": " << fc.fresh_roller_not_in_recording
              << ", \"excluded\": " << fc.excluded << ", \"after_stop\": " << fc.after_stop
              << ", \"live_was_recorded\": " << fc.live_was_recorded
              << ", \"recorded_unused\": " << fc.recorded_unused
              << ", \"stop_time_ms\": " << fc.stop_time_ms << "}";
    }
    sidecar << "]}";
  }

  sidecar << "}";

  if ( !sidecar )
  {
    throw sc_runtime_error( fmt::format( "rl_rng_record=: write to '{}' failed.", sidecar_path ) );
  }
}

void recorder_t::write_footer()
{
  if ( writing_ )
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

  // Replay's own summary (253-02, "Interfaces this plan adds"): one stdout totals line, then one
  // stdout line per roller class present in this run's own registry. Printed whenever replay_
  // exists, regardless of writing_ -- a replay-only run has no other way to report its counts.
  if ( replay_ )
  {
    fmt::print(
        "rl_rng_replay: fights={} reused={} fresh_no_address={} fresh_used_up={} "
        "fresh_roller_not_in_recording={} excluded={} after_stop={} outside_fight={} "
        "live_was_recorded={} recorded_unused={} address={} path={}\n",
        replay_->fights_served, replay_->reused, replay_->fresh_no_address, replay_->fresh_used_up,
        replay_->fresh_roller_not_in_recording, replay_->excluded, replay_->after_stop,
        replay_->outside_fight, replay_->live_was_recorded, replay_->recorded_unused,
        replay_->address, replay_->path );

    std::set<roller_class_e> classes_present;
    for ( const roller_entry_t& r : rollers_ )
      classes_present.insert( r.cls );
    for ( roller_class_e cls : classes_present )
    {
      const replay_state_t::class_counts_t& counts = replay_->per_class[ static_cast<std::size_t>( cls ) ];
      fmt::print( "rl_rng_replay class={} reused={} fresh={} excluded={} after_stop={}\n",
                  class_name( cls ), counts.reused, counts.fresh, counts.excluded, counts.after_stop );
    }
  }
}

// ---- Free functions -- the interface rl_rng_record.hpp declares. Every one is a no-op (one
// pointer check on root->rl_rng_recorder) when neither rl_rng_record= nor rl_rng_replay= is set
// (253-02 generalizes the prior "rl_rng_record= is unset" contract to either option). ----

void open_and_write_header( sim_t* sim )
{
  sim_t* root = root_of( sim );
  if ( root->rl_rng_record_file_str.empty() && root->rl_rng_replay_file_str.empty() )
    return;

  root->rl_rng_recorder = std::make_shared<recorder_t>( root );
  root->rl_rng_recorder->open();
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

void register_raid_event( sim_t* sim, raid_event_t& event )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return;
  root->rl_rng_recorder->register_raid_event( event );
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

// ---- rl_press_scope_t (plan 250-03, REC-04). Out-of-line ctors/dtor -- needs recorder_t
// complete, mirroring rl_cause_scope_t's own placement in rl_translog.cpp (needs player_t
// complete). Every body below is a single pointer check (root->rl_rng_recorder) when the
// recorder is off: no allocation, no rng call, no event, no reorder, no dispatch change (D-17).
// ----

rl_press_scope_t::rl_press_scope_t( sim_t* sim, action_t* action, mode_e mode, const action_state_t* carried_state )
  : sim_( sim )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return;

  active_ = true;
  saved_outer_ = root->rl_rng_recorder->current_outer();
  saved_inner_ = root->rl_rng_recorder->current_inner();
  saved_inner_owner_ = root->rl_rng_recorder->current_inner_owner();

  if ( mode == mode_e::open_execute )
    root->rl_rng_recorder->open_execute( action, carried_state );
  else
    root->rl_rng_recorder->open_tick( action );
}

rl_press_scope_t::rl_press_scope_t( sim_t* sim, const action_state_t* state )
  : sim_( sim )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return;

  active_ = true;
  saved_outer_ = root->rl_rng_recorder->current_outer();
  saved_inner_ = root->rl_rng_recorder->current_inner();
  saved_inner_owner_ = root->rl_rng_recorder->current_inner_owner();

  root->rl_rng_recorder->restore_from_state( state );
}

rl_press_scope_t::rl_press_scope_t( sim_t* sim, const press_snapshot_t& snapshot )
  : sim_( sim )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return;

  active_ = true;
  saved_outer_ = root->rl_rng_recorder->current_outer();
  saved_inner_ = root->rl_rng_recorder->current_inner();
  saved_inner_owner_ = root->rl_rng_recorder->current_inner_owner();

  // "When it is not stamped, change nothing" -- mirrors restore_from_state()'s own D-07 rule, but
  // for a plain-value snapshot instead of an action_state_t.
  if ( snapshot.outer.kind == TRIGGER_KIND_NONE )
    return;

  root->rl_rng_recorder->restore_frames( snapshot.outer, snapshot.inner, nullptr );
}

rl_press_scope_t::~rl_press_scope_t()
{
  if ( !active_ )
    return;
  sim_t* root = root_of( sim_ );
  if ( !root->rl_rng_recorder )
    return;
  root->rl_rng_recorder->restore_frames( saved_outer_, saved_inner_, saved_inner_owner_ );
}

// ---- rl_raid_draw_scope_t (plan 250-03, REC-05/R-01). Out-of-line ctors/dtor for the same
// reason rl_press_scope_t's are -- needs recorder_t complete. Sets/restores sim->rng()'s OWN
// rl_roller_override_ (util/rng.hpp), never rollers_ or any press frame -- a raid event's draw is
// tagged at the physical-stream level (on_draw()'s existing roller_override parameter), not
// through the press mechanism. ----

rl_raid_draw_scope_t::rl_raid_draw_scope_t( sim_t* sim, const raid_event_t& event )
  : sim_( sim )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return;

  active_ = true;
  saved_override_ = sim->rng().rl_roller_override();
  sim->rng().rl_set_roller_override( event.rl_roller_id );
}

rl_raid_draw_scope_t::~rl_raid_draw_scope_t()
{
  if ( !active_ )
    return;
  sim_->rng().rl_set_roller_override( saved_override_ );
}

// ---- rl_refill_scope_t (plan 250-03, REC-06/D-08/R-04). Out-of-line ctors/dtor, same reason as
// the guards above. ----

rl_refill_scope_t::rl_refill_scope_t( sim_t* sim, proc_rng_t* deck )
  : sim_( sim )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return;

  active_ = true;
  saved_outer_ = root->rl_rng_recorder->current_outer();
  saved_inner_ = root->rl_rng_recorder->current_inner();
  saved_inner_owner_ = root->rl_rng_recorder->current_inner_owner();

  root->rl_rng_recorder->open_refill( deck );
}

rl_refill_scope_t::~rl_refill_scope_t()
{
  if ( !active_ )
    return;
  sim_t* root = root_of( sim_ );
  if ( !root->rl_rng_recorder )
    return;
  root->rl_rng_recorder->restore_frames( saved_outer_, saved_inner_, saved_inner_owner_ );
}

void stamp_state( sim_t* sim, action_state_t* state )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return;
  root->rl_rng_recorder->stamp_state( state );
}

bool inner_owner_is( sim_t* sim, const action_t* action )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return false;
  return root->rl_rng_recorder->inner_owner_is( action );
}

void note_early_return( sim_t* sim, const action_t* action )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return;
  root->rl_rng_recorder->note_early_return( action );
}

press_snapshot_t snapshot_press( sim_t* sim )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return press_snapshot_t{};
  return press_snapshot_t{ root->rl_rng_recorder->current_outer(), root->rl_rng_recorder->current_inner() };
}

void annotate_draw( sim_t* sim, rng::rng_t& stream, std::uint64_t before, double chance, bool outcome,
                     std::uint8_t label )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return;
  root->rl_rng_recorder->annotate_draw( stream, before, chance, outcome, label );
}

void label_last_roll( sim_t* sim, std::uint8_t label, double chance, bool success )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return;
  root->rl_rng_recorder->label_last_roll( label, chance, success );
}

} // namespace rl_rng_record
