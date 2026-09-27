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
// Study-only swing pin (tstl-sylvanas phase 257, plan 257-07, D-17): swing_pin_hand_for() below
// compares an action_t* against player_t::main_hand_attack/off_hand_attack (declared attack_t*
// in player.hpp), which needs attack_t's complete definition to verify the derived-to-base
// pointer adjustment for that comparison -- a plain forward declaration is not enough.
#include "action/attack.hpp"
#include "player/player.hpp"
#include "player/stats.hpp"
#include "sim/event.hpp"
#include "sim/event_manager.hpp"
#include "sim/proc_rng.hpp"
#include "sim/raid_event.hpp"
#include "sim/rl_proc_counters.hpp"
#include "sim/sim.hpp"
#include "util/io.hpp"
#include "util/rng.hpp"
#include "util/util.hpp"

#include "fmt/format.h"

#include "rapidjson/document.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <regex>
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

// Study-only (tstl-sylvanas phase 257, plan 257-02, ISO-03/D-15): a C++ port of
// scripts/rl/probes/luck_packet_leak.py's classify_fields, used ONLY when
// rl_rng_replay_fresh_class_str is set, to decide a RECORDED roll's report class from its own
// recorded fields plus the sidecar roller entry that recorded it. Same rules, same order, same
// fallback as the Python -- cross-checked by fresh_class_check (M-89 (d)) against the same
// recording bytes. Never called (and label_names/sidecar_id_class_string/sidecar_id_key_string
// are never populated) when the option is unset.
std::string_view report_class_of( std::uint8_t trigger_kind, std::uint8_t label, std::uint8_t outcome,
                                   const std::string& roller_class, const std::string& roller_key,
                                   const std::unordered_map<std::uint8_t, std::string>& label_names )
{
  if ( trigger_kind == TRIGGER_KIND_REFILL )
    return "deck";

  const std::string* name = nullptr;
  if ( label != LABEL_NONE )
  {
    const auto it = label_names.find( label );
    if ( it != label_names.end() )
      name = &it->second;
  }

  if ( name != nullptr && *name == "swing_attack_table" )
    return "swing";
  if ( name != nullptr && *name == "maelstrom_weapon_gain" )
    return "maelstrom";
  if ( name == nullptr && roller_class == "proc_callback" && !roller_key.empty() )
  {
    std::string lower_key = roller_key;
    std::transform( lower_key.begin(), lower_key.end(), lower_key.begin(),
                     []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    if ( lower_key.find( "maelstrom_weapon" ) != std::string::npos )
      return "maelstrom";
  }
  if ( name != nullptr && *name == "crit_hit" )
    return "crit";
  if ( name != nullptr )
    return "proc";

  const bool is_chance_roll = ( outcome == OUTCOME_FAIL || outcome == OUTCOME_SUCCESS );
  const bool sim_owns_roller = roller_class == "sim_shared" || roller_class == "sim_explore" ||
                                roller_class == "raid_event" ||
                                ( !roller_key.empty() && roller_key.compare( 0, 4, "sim|" ) == 0 );
  if ( is_chance_roll && !sim_owns_roller )
    return "proc";

  if ( roller_class == "raid_event" )
    return "raid_event";

  return "other";
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
  // Study-only (phase 257, D-15): true exactly when sim_t::rl_rng_replay_fresh_class_str is
  // non-empty. Every fresh-class field below is left empty/default and never consulted when
  // this is false, so a plain replay run pays nothing extra and is byte-identical to before
  // this option existed.
  bool fresh_class_active = false;
  std::string fresh_class_name;   // the option's own value (one of the 6 accepted classes)
  // Parsed from the sidecar (ONLY when fresh_class_active), one entry per sidecar id, parallel
  // to sidecar_id_to_key_index below: the roller's own "class" string and raw "key" string --
  // needed by report_class_of() (the classify_fields port), which the interned key INDEX alone
  // cannot answer (it needs the literal key text for the "maelstrom_weapon" substring rule and
  // the "sim|" prefix rule).
  std::vector<std::string> sidecar_id_class_string;
  std::vector<std::string> sidecar_id_key_string;
  // The sidecar's own labelNames map (int label id -> name), needed to decide a recorded roll's
  // class from its own `label` byte (classify_fields' `label_names.get(label)`). Only parsed
  // when fresh_class_active.
  std::unordered_map<std::uint8_t, std::string> label_names;
  // Whole-run total of slots left fresh by the option (D-15) -- the fork's own count, compared
  // by the Python join in fresh_class_check (M-89 (d)) against the same recording.
  std::uint64_t fresh_class_left = 0;
  // Gap closure of 257-05's own crash (tstl-sylvanas phase 257, plan 257-08): the option's own
  // value (whole milliseconds), copied from sim_t::rl_rng_replay_fresh_from_ms once at
  // replay_init() time (already validated by sim.cpp's setup()). 0 (the default, unset) means
  // "from the first roll" -- on_draw()'s own time gate below is then always true, byte-identical
  // to plan 257-02's own unextended fresh-class behaviour.
  std::int64_t fresh_from_ms = 0;
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
    // Study-only (phase 257, D-15): parallel to `raw`, filled ONLY when
    // fresh_class_active is true -- element i is true when raw[i]'s own recorded roll
    // (fight A's) belongs to the report class the option names. Empty (never allocated
    // or indexed) when the option is unset, so on_draw()'s hot path pays nothing extra.
    std::vector<std::uint8_t> left_fresh;
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
    // Study-only (phase 257, D-15): this fight's own count of slots left fresh by the option.
    std::uint64_t fresh_class_left = 0;
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

// Study-only swing-trace/swing-pin state (tstl-sylvanas phase 257, plan 257-09, D-18, owner
// ruling A-125 -- supersedes plan 257-07's own v1 pin-list design; see rl_rng_record.hpp's own
// doc comment for A-124's finding and why). Non-empty only when sim_t::rl_swing_trace_file_str
// or sim_t::rl_swing_pin_file_str is non-empty (either requires replay_ to already be set --
// sim.cpp's setup() refuses otherwise). Reset at every fight_begin(), matching every other
// per-fight state in this file -- this whole probe harness runs one fight per process, but
// nothing here assumes that.
struct swing_trace_state_t
{
  // One hand's own state, mirroring scripts/rl/probes/timing_hold.py's `pin_model` per-hand
  // state dict field-for-field ("How D-18 is built" section 3) PLUS the booking-rule/`stale`
  // bookkeeping M-110's `trace_problems` needs (kept together since every funnel below touches
  // both at once).
  struct hand_t
  {
    bool waiting = false;         // this hand currently has an event booked (M-110's own state)
    std::int64_t due_ms = 0;      // meaningful only when waiting
    bool started = false;         // this hand has had its first-ever operation THIS fight (R7)
    char last_end_kind = -1;      // the kind of the last land/drop/cancel row (-1: none yet)
    std::int64_t last_end_ms = 0; // that row's own op_ms
    bool pending_fired = false;   // the event fired; not yet resolved as `land` or `drop`
    std::int64_t pending_fired_ms = 0;
    std::uint64_t repeats = 0;    // A-123: Skyfury's own in-place repeat, never a row

    // Pin-model bookkeeping (D-18 (b)/(c)) -- meaningless (never read) unless pin_active.
    bool last_own_op_ms_set = false;
    std::int64_t last_own_op_ms = 0;
    std::size_t j = 0;
    bool in_step = true;
  };

  // One row of a trace file -- THIS fight's own (rows below) or fight A's parsed input
  // (a_mh_rows/a_oh_rows below). `hand` is redundant once split by hand but costs nothing.
  struct row_t
  {
    char hand;
    std::int64_t op_ms;
    char kind;
    std::int64_t due_ms;
  };

  player_t* designated_player = nullptr; // the one non-pet, non-enemy player; set at fight_begin()
  bool pin_active = false;               // rl_swing_pin set (implies replay_ and fight_begin() OK)
  bool trace_active = false;             // rl_swing_trace set
  hand_t mh, oh;
  std::vector<row_t> rows; // THIS fight's own trace, in real operation order (both hands)

  // Fight A's parsed input (pin_active only), per hand, plus the sorted-unique breakpoint
  // milliseconds across BOTH hands -- the rolling check event (section 3) only ever needs to
  // wake at one of these, never polls.
  std::vector<row_t> a_mh_rows, a_oh_rows;
  std::vector<std::int64_t> breakpoints;
  std::size_t bp_cursor = 0;
  bool check_armed_ever = false;

  // M-111 (e)'s whole-run pin counters (own_overrides is derived, not stored, at print time).
  std::uint64_t own_exact = 0, own_as_of = 0, past_value = 0, beyond = 0, own_changed = 0;
  std::uint64_t injected = 0, injected_short = 0, inject_skipped = 0;
  std::uint64_t mid_cast = 0;
  std::uint64_t stale = 0;

  hand_t& hand_state( char hand ) { return hand == SWING_HAND_MH ? mh : oh; }
  const std::vector<row_t>& a_hand_rows( char hand ) const { return hand == SWING_HAND_MH ? a_mh_rows : a_oh_rows; }
};

const char* swing_row_kind_name( char kind )
{
  switch ( kind )
  {
    case SWING_ROW_BOOK: return "book";
    case SWING_ROW_REBOOK: return "rebook";
    case SWING_ROW_RESTART: return "restart";
    case SWING_ROW_RESCHED: return "resched";
    case SWING_ROW_LAND: return "land";
    case SWING_ROW_DROP: return "drop";
    case SWING_ROW_CANCEL: return "cancel";
    default: return "inject";
  }
}

bool swing_row_is_end_kind( char kind )
{
  return kind == SWING_ROW_LAND || kind == SWING_ROW_DROP || kind == SWING_ROW_CANCEL;
}

bool swing_row_is_firing_kind( char kind )
{
  return kind == SWING_ROW_LAND || kind == SWING_ROW_DROP;
}

// M-110 (D-18 (a), "How D-18 is built" section 2). The SAME text format
// rl_rng_record::recorder_t::swing_trace_write_file() (below) produces -- an INDEPENDENT C++
// reader, never a call into Python. Applies read_trace's own syntax rules with
// allow_inject=false always (a pin/fight-A input trace never carries an inject row -- one is
// refused by name, which is also how a v1 three-line pin list fails, at its header). Throws
// std::runtime_error with a short reason fragment on any malformed line; the caller (sim_t::
// setup()) wraps it in the fork's own named refusal message.
std::string parse_swing_trace_file( const std::vector<std::string>& lines,
                                     std::vector<char>& hand, std::vector<std::int64_t>& op_ms,
                                     std::vector<char>& kind, std::vector<std::int64_t>& due_ms )
{
  if ( lines.empty() )
    throw std::runtime_error( "the trace file is empty" );

  static const std::regex header_re( R"(^# rl_swing_trace format=1 actor=(\S+)$)" );
  static const std::regex row_re(
      R"(^(mh|oh) (\d+) (book|rebook|restart|resched|inject|land|drop|cancel) (-?\d+)$)" );
  static const std::regex repeats_re( R"(^repeats (mh|oh) (\d+)$)" );
  static const std::regex end_re( R"(^end (\d+)$)" );

  std::smatch m;
  if ( !std::regex_match( lines[ 0 ], m, header_re ) )
    throw std::runtime_error( "line 1 does not match the expected header format '# rl_swing_trace format=1 actor=<actor>'" );
  const std::string actor = m[ 1 ].str();

  auto kind_of = []( const std::string& s ) -> char {
    if ( s == "book" ) return SWING_ROW_BOOK;
    if ( s == "rebook" ) return SWING_ROW_REBOOK;
    if ( s == "restart" ) return SWING_ROW_RESTART;
    if ( s == "resched" ) return SWING_ROW_RESCHED;
    if ( s == "inject" ) return SWING_ROW_INJECT;
    if ( s == "land" ) return SWING_ROW_LAND;
    if ( s == "drop" ) return SWING_ROW_DROP;
    return SWING_ROW_CANCEL; // "cancel" -- the regex guarantees one of the eight names above
  };

  std::size_t i = 1;
  bool have_last = false;
  std::int64_t last_op_ms = 0;
  while ( i < lines.size() && std::regex_match( lines[ i ], m, row_re ) )
  {
    const std::string hand_s = m[ 1 ].str();
    const std::int64_t this_op_ms = std::stoll( m[ 2 ].str() );
    const char this_kind = kind_of( m[ 3 ].str() );
    const std::int64_t this_due_ms = std::stoll( m[ 4 ].str() );

    if ( this_kind == SWING_ROW_INJECT )
      throw std::runtime_error( fmt::format( "row {} is an 'inject' row, refused for this input", i ) );
    if ( have_last && this_op_ms < last_op_ms )
      throw std::runtime_error( fmt::format(
          "row {} op_ms {} is smaller than the previous row's {}", i, this_op_ms, last_op_ms ) );
    if ( swing_row_is_end_kind( this_kind ) && this_due_ms != this_op_ms )
      throw std::runtime_error(
          fmt::format( "row {} kind {} has due_ms {} != op_ms {}", i, m[ 3 ].str(), this_due_ms, this_op_ms ) );

    hand.push_back( hand_s == "mh" ? SWING_HAND_MH : SWING_HAND_OH );
    op_ms.push_back( this_op_ms );
    kind.push_back( this_kind );
    due_ms.push_back( this_due_ms );

    last_op_ms = this_op_ms;
    have_last = true;
    ++i;
  }

  const std::size_t row_count = hand.size();
  for ( const char* which : { "mh", "oh" } )
  {
    if ( i >= lines.size() )
      throw std::runtime_error( fmt::format( "missing a 'repeats {}' line", which ) );
    if ( !std::regex_match( lines[ i ], m, repeats_re ) || m[ 1 ].str() != which )
      throw std::runtime_error(
          fmt::format( "line {} is not the expected 'repeats {} <n>' line", i + 1, which ) );
    ++i;
  }

  if ( i >= lines.size() )
    throw std::runtime_error( "missing the 'end <row count>' line" );
  if ( !std::regex_match( lines[ i ], m, end_re ) )
    throw std::runtime_error( fmt::format( "line {} is not the expected 'end <row count>' line", i + 1 ) );
  const std::size_t end_count = static_cast<std::size_t>( std::stoll( m[ 1 ].str() ) );
  if ( end_count != row_count )
    throw std::runtime_error(
        fmt::format( "'end {}' does not match the parsed row count {}", end_count, row_count ) );
  ++i;
  if ( i != lines.size() )
    throw std::runtime_error( fmt::format( "{} line(s) after 'end'", lines.size() - i ) );

  return actor;
}

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

  // ---- Swing trace / swing pin (tstl-sylvanas phase 257, plan 257-09, D-18) -- called only from
  // the free functions of the same name at the bottom of this file. See each declaration's own
  // doc comment in rl_rng_record.hpp. ----
  timespan_t swing_event_created( action_t* action, timespan_t t );
  timespan_t swing_event_rescheduled( event_t* e, timespan_t delta_time );
  void swing_event_canceled( event_t* e );
  void swing_event_fired( action_t* action );
  void swing_attack_executed( action_t* action );
  // Called only from swing_check_event_t::execute() (file-local, below) -- the rolling check
  // event's own reserved-id firing.
  void swing_trace_run_checks();

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

  // Swing trace / swing pin identity (D-18, M-111 (c)). Returns true and sets `hand` when
  // `action` (creation/fire funnels) or `e` (reschedule/cancel funnels, compared as a raw event
  // pointer VALUE against the designated player's own execute_event fields -- event.cpp has no
  // action_t* to compare) is currently the ONE designated player's main_hand_attack/
  // off_hand_attack, checked at the MOMENT of the call -- covers Windlash after Ascendance's
  // hand-over with no extra logic. False (hand left unset) when the trace/pin is not active at
  // all this fight, or `action`/`e` names neither hand. Every funnel above funnels through one
  // of these two.
  bool swing_trace_hand_of_action( const action_t* action, char& hand ) const;
  bool swing_trace_hand_of_event( const event_t* e, char& hand ) const;

  // The shared row-emission core (D-18 (b)/(c), mirrors timing_hold.py's own `emit_own`
  // function field-for-field): resolves this operation's due value (the engine's own natural
  // due when the pin is inactive; D-18 (b)'s value -- exact match, own_as_of, past_value, or
  // beyond -- when it is), updates `hand`'s waiting/due/last_end/j/in_step state, appends the
  // row, and arms the rolling check event on this fight's first-ever call. Returns the due
  // value (ignored by end-kind callers, which pass has_natural_due=false).
  std::int64_t swing_trace_emit( char hand, std::int64_t op_ms, char kind, bool has_natural_due,
                                  std::int64_t natural_due );
  // D-18 (b)'s literal fallback (own_as_of / past_value / beyond) -- mirrors timing_hold.py's
  // `_literal_value`. Sets *which to one of "own_as_of"/"past_value"/"beyond".
  std::int64_t swing_trace_literal_value( char hand, std::int64_t t, std::int64_t engine_now,
                                           const char** which );
  void swing_trace_bump_counter( const char* which );
  // book/rebook/restart kind resolution (section 2's booking rules) -- flushes a pending,
  // unresolved firing as `drop` first (section 1 (iv)/(v)'s own resolution point) when one is
  // outstanding.
  char swing_trace_resolve_creation_kind( char hand, std::int64_t now_ms );
  void swing_trace_append_row( char hand, std::int64_t op_ms, char kind, std::int64_t due_ms );
  // Section 3's rolling check event: arms (or re-arms) it for the first breakpoint at or after
  // now (`initial=true`, called once per fight from swing_trace_emit) or for
  // breakpoints[bp_cursor] exactly (`initial=false`, called from swing_trace_run_checks() after
  // advancing the cursor). A guard throws when an ordinary event id nears the reserved one this
  // event is given (section 3's own 2^31 guard).
  void swing_trace_arm_check( bool initial );
  void swing_trace_run_check_for_hand( char hand, std::int64_t t );
  // Writes THIS fight's own trace file (rl_swing_trace_file_str) at fight_end() -- a no-op when
  // trace_active is false.
  void swing_trace_write_file();

  sim_t* root_;
  // True only when rl_rng_record= is set (253-02): gates every write-side concern (the stream,
  // the buffer, entry building, the sidecar, the footer's record/close/sidecar work). Registration
  // and the press-frame guards run regardless -- see fight_begin()/fight_end()/on_draw() below.
  bool writing_ = false;
  // Non-null only when rl_rng_replay= is set (253-02) -- see replay_state_t's own doc comment.
  std::unique_ptr<replay_state_t> replay_;
  // Active only when rl_swing_trace_file_str or rl_swing_pin_file_str is set (either requires
  // replay_ above) -- see swing_trace_state_t's own doc comment. Kept as a plain value (never
  // null) so a swing-trace/pin-off run pays one pointer-equivalent check (designated_player ==
  // nullptr) instead of a pointer dereference guard at every one of the funnels' own no-op fast
  // paths.
  swing_trace_state_t swing_trace_;
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
        else if ( replay_->fresh_class_active && it->second.left_fresh[ it->second.next_unused ]
                  // Gap closure of 257-05's own crash (tstl-sylvanas phase 257, plan 257-08):
                  // this slot's own class is only left fresh once fight B's OWN current time
                  // reaches the option's floor -- root_->current_time() is THIS run's live fight
                  // clock, the same source every other time_ms field in this file reads. 0 (the
                  // default, unset) makes this always true, so an unset fresh_from_ms is
                  // byte-identical to plan 257-02's own behaviour (left fresh from roll 0).
                  && static_cast<std::int64_t>( root_->current_time().total_millis() ) >= replay_->fresh_from_ms )
        {
          // Study-only (phase 257, D-15): this slot's own recorded roll (fight A's) belongs to
          // the class the option leaves fresh. Consume it exactly like a normal reuse (advance
          // next_unused, so every LATER roll at this address still keeps its own partner) but do
          // NOT set matched -- falls through to the unmatched path below, which applies D-17's
          // fresh-number rule and its own per-class fresh count, precisely as if this slot had no
          // partner at all.
          ++it->second.next_unused;
          ++replay_->fresh_class_left;
          ++fight_counts.fresh_class_left;
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
  // Study-only (phase 257, D-15): sim.cpp's setup() has already refused any value outside the
  // 6 accepted classes, and refused this being set without rl_rng_replay= -- by the time
  // replay_init() runs (called from open(), after setup()'s refusal blocks), the value is
  // already validated. An empty string is the same as unset.
  rp.fresh_class_active = !root_->rl_rng_replay_fresh_class_str.empty();
  rp.fresh_class_name = root_->rl_rng_replay_fresh_class_str;
  // Gap closure of 257-05's own crash (tstl-sylvanas phase 257, plan 257-08): already validated
  // by sim.cpp's setup() (non-negative, requires fresh_class_active) by the time replay_init()
  // runs. 0 (unset) is the default and means "from the first roll".
  rp.fresh_from_ms = root_->rl_rng_replay_fresh_from_ms;

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

  // Study-only (phase 257, D-15): the fresh-class option needs the sidecar's own labelNames map
  // (int label id -> name) to decide a recorded roll's class from its `label` byte -- refused by
  // name when the option is set and the sidecar carries none (an older sidecar, or a hand-built
  // one for a test). Never required when the option is unset.
  if ( rp.fresh_class_active )
  {
    if ( !doc.HasMember( "labelNames" ) || !doc[ "labelNames" ].IsObject() )
    {
      throw sc_runtime_error( fmt::format(
          "rl_rng_replay_fresh_class: cannot read '{}': sidecar '{}' has no 'labelNames' object "
          "(required by rl_rng_replay_fresh_class=).",
          rp.path, sidecar_path ) );
    }
    for ( auto it = doc[ "labelNames" ].MemberBegin(); it != doc[ "labelNames" ].MemberEnd(); ++it )
    {
      if ( !it->value.IsString() )
        continue;
      const int label_id = std::atoi( it->name.GetString() );
      if ( label_id < 0 || label_id > 255 )
        continue;
      rp.label_names.emplace( static_cast<std::uint8_t>( label_id ), it->value.GetString() );
    }
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
    // Study-only (phase 257, D-15): the fresh-class option needs each roller's own "class"
    // string (report_class_of()'s roller_class parameter) -- refused by name when the option is
    // set and a roller entry has none.
    std::string roller_class_string;
    if ( rp.fresh_class_active )
    {
      if ( !entry.HasMember( "class" ) || !entry[ "class" ].IsString() )
      {
        throw sc_runtime_error( fmt::format(
            "rl_rng_replay_fresh_class: cannot read '{}': sidecar '{}' roller {} has no 'class' "
            "string (required by rl_rng_replay_fresh_class=).",
            rp.path, sidecar_path, id ) );
      }
      roller_class_string = entry[ "class" ].GetString();
    }

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
      if ( rp.fresh_class_active )
      {
        rp.sidecar_id_class_string.resize( id + 1 );
        rp.sidecar_id_key_string.resize( id + 1 );
      }
    }
    rp.sidecar_id_to_key_index[ id ] = key_index;
    // D-20: the exploration stream and any "unregistered|" stream are excluded from replay
    // altogether -- their numbers still enter the fresh-number rule's value set (D-17), just
    // never the address table (Task 2's on_draw/replay_fight_begin both consult this).
    rp.sidecar_id_excluded[ id ] = replay_key_is_excluded( key );
    if ( rp.fresh_class_active )
    {
      rp.sidecar_id_class_string[ id ] = roller_class_string;
      rp.sidecar_id_key_string[ id ] = key;
    }
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

      // 254-06 Task 2 (REP-06 speed round 3): replay_sidecar_excluded()/
      // replay_key_index_for_sidecar_roller() each re-ran replay_check_sidecar_id() on the SAME
      // id this block already validated, up to 5 bounds-check calls per kept ROLL record (3 for
      // rec.roller, 2 for trigger_id) -- profiled at 1.21% of the replaying run's own instruction
      // count in replay_check_sidecar_id() alone ($S/speed/profile-r2-replay.txt). Each id is
      // validated exactly ONCE below, in the same order as before (roller, then trigger -- so the
      // same first-invalid-id throws the same exception with the same message), then read
      // directly off the validated arrays -- no behavior change, fewer redundant calls.
      replay_check_sidecar_id( rec.roller );
      const bool roller_excluded = rp.sidecar_id_excluded[ rec.roller ];
      bool press_excluded = false;
      if ( trigger_kind != TRIGGER_KIND_NONE )
      {
        replay_check_sidecar_id( trigger_id );
        press_excluded = rp.sidecar_id_excluded[ trigger_id ];
      }
      if ( !roller_excluded && !press_excluded )
      {
        // D-15/D-16: for no press (trigger_kind NONE) the trigger-roller key is the fixed
        // NO_PRESS_KEY_INDEX sentinel, never the recorded roller-0 key -- "a trigger field of 0
        // means 'no press', not sim|_rng".
        const std::uint32_t trigger_key = trigger_kind == TRIGGER_KIND_NONE
            ? replay_state_t::NO_PRESS_KEY_INDEX
            : rp.sidecar_id_to_key_index[ trigger_id ];
        const std::uint32_t roller_key = rp.sidecar_id_to_key_index[ rec.roller ];
        const replay_state_t::address_key_t key{ trigger_kind, trigger_key, press_number, roller_key };
        rp.table[ key ].raw.push_back( rec.raw );
        // Study-only (phase 257, D-15): decide THIS recorded roll's report class from fight A's
        // own recorded fields at load time (a roll's label is only attached after its draw
        // returns, so this cannot be decided at draw time) and flag the slot when it matches the
        // option's class. Parallel to raw's own push_back just above -- same index, same order.
        if ( rp.fresh_class_active )
        {
          const std::string_view roll_class = report_class_of(
              rec.trigger_kind, rec.label, rec.outcome, rp.sidecar_id_class_string[ rec.roller ],
              rp.sidecar_id_key_string[ rec.roller ], rp.label_names );
          rp.table[ key ].left_fresh.push_back( roll_class == rp.fresh_class_name ? 1u : 0u );
        }
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

  // Swing trace / swing pin (tstl-sylvanas phase 257, plan 257-09, D-18, M-111 (b)/(c)). Reset
  // EVERY fight (defensive, mirrors every other per-fight reset above -- this whole probe
  // harness runs one fight per process, but nothing here assumes that). root_->rl_swing_pin_*
  // were already parsed and validated by sim.cpp's setup(); copying them here (rather than
  // reading the root members directly at operation time) is what lets each fight track its OWN
  // per-hand state from a clean slate.
  if ( !root_->rl_swing_pin_file_str.empty() || !root_->rl_swing_trace_file_str.empty() )
  {
    swing_trace_ = swing_trace_state_t{};
    swing_trace_.pin_active = !root_->rl_swing_pin_file_str.empty();
    swing_trace_.trace_active = !root_->rl_swing_trace_file_str.empty();

    // M-111 (b)/(c): exactly one player that is neither a pet nor an enemy -- pets and enemies
    // book their own melee through the SAME action_t::start_action_execute_event()/event_t::
    // reschedule()/cancel() functions this plan hooks, so this identity check is the ONLY place
    // their exclusion is enforced; every other funnel trusts swing_trace_hand_of_action/event()'s
    // own `action->player == designated_player` comparison.
    player_t* found = nullptr;
    int qualifying = 0;
    for ( player_t* p : root_->player_no_pet_list )
    {
      if ( p->is_enemy() )
        continue;
      ++qualifying;
      found = p;
    }
    if ( qualifying != 1 )
    {
      throw sc_runtime_error( fmt::format(
          "{} needs exactly one player that is neither a pet nor an enemy (found {}).",
          swing_trace_.pin_active ? "rl_swing_pin" : "rl_swing_trace", qualifying ) );
    }
    swing_trace_.designated_player = found;

    if ( swing_trace_.pin_active )
    {
      if ( root_->rl_swing_pin_actor != found->name_str )
      {
        throw sc_runtime_error( fmt::format(
            "rl_swing_pin trace actor '{}' does not match the one designated player '{}'.",
            root_->rl_swing_pin_actor, found->name_str ) );
      }

      for ( std::size_t i = 0; i < root_->rl_swing_pin_hand.size(); ++i )
      {
        swing_trace_state_t::row_t row{ root_->rl_swing_pin_hand[ i ], root_->rl_swing_pin_op_ms[ i ],
                                         root_->rl_swing_pin_kind[ i ], root_->rl_swing_pin_due_ms[ i ] };
        if ( row.hand == SWING_HAND_MH )
          swing_trace_.a_mh_rows.push_back( row );
        else
          swing_trace_.a_oh_rows.push_back( row );
      }

      std::vector<std::int64_t> bp( root_->rl_swing_pin_op_ms.begin(), root_->rl_swing_pin_op_ms.end() );
      std::sort( bp.begin(), bp.end() );
      bp.erase( std::unique( bp.begin(), bp.end() ), bp.end() );
      swing_trace_.breakpoints = std::move( bp );
    }
  }
  else
  {
    swing_trace_ = swing_trace_state_t{};
  }
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

  // Swing pin (D-18, section 3): flush any breakpoint the rolling check event never reached.
  // The check event is deliberately scheduled with the LARGEST possible id so it sorts after
  // every ordinary event of its own millisecond (event_manager.cpp:146-148) -- but combat_end
  // can be triggered by an ordinary event at that SAME millisecond, and the event manager never
  // calls execute() on events still queued once combat is over. Fight_end() runs before the
  // event manager's own flush/reset (this file's own fight_begin()/fight_end() doc comments),
  // so every one of fight B's own operations for this fight has already happened -- running the
  // remaining breakpoints' checks synchronously here, off the static breakpoint list and each
  // hand's own FINAL state, is exactly equivalent to the check event that never got to fire (no
  // "now" is read; R4's own stop/no-swing-waiting rule applies identically either way).
  while ( swing_trace_.pin_active && swing_trace_.bp_cursor < swing_trace_.breakpoints.size() )
  {
    const std::int64_t t = swing_trace_.breakpoints[ swing_trace_.bp_cursor ];
    swing_trace_run_check_for_hand( SWING_HAND_MH, t );
    swing_trace_run_check_for_hand( SWING_HAND_OH, t );
    ++swing_trace_.bp_cursor;
  }

  // Swing trace (D-18, "How D-18 is built" section 2): the trace file is written ONCE, at fight
  // end, from memory -- a crash leaves no half file. Runs in BOTH modes (mirrors replay_ above),
  // AFTER replay_fight_end() and the flush above so a trace-only run's own fight-end bookkeeping
  // is unaffected by write order and every flushed inject row (if any) is included.
  swing_trace_write_file();
}

// Swing trace / swing pin identity (D-18, M-111 (c)). See each declaration's own doc comment in
// the class body above.
bool recorder_t::swing_trace_hand_of_action( const action_t* action, char& hand ) const
{
  if ( !swing_trace_.designated_player || !action || action->player != swing_trace_.designated_player )
    return false;

  // Checked "at the moment of the call" (M-111 (c)) -- whichever action player->main_hand_attack
  // / off_hand_attack currently name, never a fixed action_t* remembered from an earlier call.
  // This is exactly what covers Windlash after Ascendance's hand-over with no separate logic:
  // the hand-over reassigns the pointer BEFORE calling schedule_execute() on the new attack.
  if ( action == static_cast<const action_t*>( swing_trace_.designated_player->main_hand_attack ) )
  {
    hand = SWING_HAND_MH;
    return true;
  }
  if ( action == static_cast<const action_t*>( swing_trace_.designated_player->off_hand_attack ) )
  {
    hand = SWING_HAND_OH;
    return true;
  }
  return false;
}

bool recorder_t::swing_trace_hand_of_event( const event_t* e, char& hand ) const
{
  if ( !swing_trace_.designated_player || !e )
    return false;

  const attack_t* mh_attack = swing_trace_.designated_player->main_hand_attack;
  const attack_t* oh_attack = swing_trace_.designated_player->off_hand_attack;
  // event.cpp has no action_t* at this call site -- identified by comparing the event POINTER
  // VALUE itself against the designated player's own execute_event fields, at the moment of the
  // call (same "no fixed pointer remembered" rule as the action-identity check above).
  if ( mh_attack && e == static_cast<const action_t*>( mh_attack )->execute_event )
  {
    hand = SWING_HAND_MH;
    return true;
  }
  if ( oh_attack && e == static_cast<const action_t*>( oh_attack )->execute_event )
  {
    hand = SWING_HAND_OH;
    return true;
  }
  return false;
}

std::int64_t recorder_t::swing_trace_literal_value( char hand, std::int64_t t, std::int64_t engine_now,
                                                      const char** which )
{
  // _a_state_asof (D-18 (b)): fight A's state after its LAST row with op_ms <= t -- rows are
  // parsed non-decreasing by op_ms (parse_swing_trace_file's own rule), so the per-hand
  // subsequence is non-decreasing too and the loop below may stop at the first row past t.
  const auto& a_rows = swing_trace_.a_hand_rows( hand );
  bool a_has = false, a_waiting = false;
  std::int64_t a_due = 0;
  for ( const auto& r : a_rows )
  {
    if ( r.op_ms > t )
      break;
    a_has = true;
    a_waiting = !swing_row_is_end_kind( r.kind );
    a_due = r.due_ms;
  }
  if ( a_has && a_waiting && a_due > t )
  {
    *which = "own_as_of";
    return a_due;
  }
  // _a_first_value_after (R1's "past value" fallback): the first row strictly after t whose
  // kind is a VALUE kind (an END row after t is never a candidate value -- it names no due).
  for ( const auto& r : a_rows )
  {
    if ( r.op_ms > t && !swing_row_is_end_kind( r.kind ) )
    {
      *which = "past_value";
      return r.due_ms;
    }
  }
  *which = "beyond";
  return engine_now;
}

void recorder_t::swing_trace_bump_counter( const char* which )
{
  if ( std::strcmp( which, "own_as_of" ) == 0 )
    ++swing_trace_.own_as_of;
  else if ( std::strcmp( which, "past_value" ) == 0 )
    ++swing_trace_.past_value;
  else
    ++swing_trace_.beyond;
}

void recorder_t::swing_trace_append_row( char hand, std::int64_t op_ms, char kind, std::int64_t due_ms )
{
  swing_trace_.rows.push_back( { hand, op_ms, kind, due_ms } );
}

std::int64_t recorder_t::swing_trace_emit( char hand, std::int64_t op_ms, char kind,
                                            bool has_natural_due, std::int64_t natural_due )
{
  swing_trace_state_t::hand_t& h = swing_trace_.hand_state( hand );

  // pin_model's own per-millisecond reset (D-18 (b)/(c)): a fresh own-operation millisecond
  // starts a fresh in-step run against fight A's own rows of that same millisecond.
  if ( !h.last_own_op_ms_set || h.last_own_op_ms != op_ms )
  {
    h.last_own_op_ms_set = true;
    h.last_own_op_ms = op_ms;
    h.j = 0;
    h.in_step = true;
  }

  const bool is_end = swing_row_is_end_kind( kind );
  std::int64_t value = has_natural_due ? natural_due : op_ms;
  bool changed = false;

  if ( swing_trace_.pin_active )
  {
    const auto& a_rows = swing_trace_.a_hand_rows( hand );
    std::vector<const swing_trace_state_t::row_t*> a_rows_ms;
    for ( const auto& r : a_rows )
      if ( r.op_ms == op_ms )
        a_rows_ms.push_back( &r );

    bool matched = false;
    const swing_trace_state_t::row_t* a_row = nullptr;
    if ( h.in_step && h.j < a_rows_ms.size() )
    {
      a_row = a_rows_ms[ h.j ];
      const bool both_firing = swing_row_is_firing_kind( kind ) && swing_row_is_firing_kind( a_row->kind );
      matched = ( kind == a_row->kind ) || both_firing;
    }

    if ( matched )
    {
      ++h.j;
      if ( is_end )
      {
        value = op_ms;
      }
      else
      {
        value = a_row->due_ms;
        ++swing_trace_.own_exact;
        changed = has_natural_due && value != natural_due;
      }
    }
    else
    {
      h.in_step = false;
      if ( is_end )
      {
        value = op_ms;
      }
      else
      {
        const std::int64_t engine_now = has_natural_due ? natural_due : op_ms;
        const char* which = "beyond";
        value = swing_trace_literal_value( hand, op_ms, engine_now, &which );
        swing_trace_bump_counter( which );
        changed = has_natural_due && value != natural_due;
      }
    }
  }

  if ( changed )
    ++swing_trace_.own_changed;

  if ( is_end )
  {
    h.waiting = false;
    h.last_end_kind = kind;
    h.last_end_ms = op_ms;
  }
  else
  {
    h.waiting = true;
    h.due_ms = value;
  }

  swing_trace_append_row( hand, op_ms, kind, value );

  // Section 3: armed once per fight, at the first row this fight ever writes -- never in
  // fight_begin() (which runs before the event manager's reset).
  if ( swing_trace_.pin_active && !swing_trace_.check_armed_ever )
  {
    swing_trace_.check_armed_ever = true;
    swing_trace_arm_check( /*initial=*/true );
  }

  return value;
}

char recorder_t::swing_trace_resolve_creation_kind( char hand, std::int64_t now_ms )
{
  swing_trace_state_t::hand_t& h = swing_trace_.hand_state( hand );

  // Section 1 (iv)/(v)'s own resolution point: a booking that follows an UNRESOLVED firing
  // (the channel-pause "no swing ran, re-book at once" branch, action.cpp:326-345) is the
  // moment this plan learns that firing was a `drop`, not a `land` -- flush it first.
  if ( h.pending_fired )
  {
    const std::int64_t drop_ms = h.pending_fired_ms;
    h.pending_fired = false;
    swing_trace_emit( hand, drop_ms, SWING_ROW_DROP, /*has_natural_due=*/false, 0 );
  }

  if ( !h.started )
    return SWING_ROW_BOOK;
  if ( h.last_end_kind >= 0 && h.last_end_ms == now_ms )
    return ( h.last_end_kind == SWING_ROW_CANCEL ) ? SWING_ROW_REBOOK : SWING_ROW_BOOK;
  return SWING_ROW_RESTART;
}

timespan_t recorder_t::swing_event_created( action_t* action, timespan_t t )
{
  char hand;
  if ( !swing_trace_hand_of_action( action, hand ) )
    return t;

  const std::int64_t now_ms = static_cast<std::int64_t>( root_->current_time().total_millis() );
  const char kind = swing_trace_resolve_creation_kind( hand, now_ms );
  swing_trace_.hand_state( hand ).started = true;
  const std::int64_t natural_due = now_ms + static_cast<std::int64_t>( t.total_millis() );
  const std::int64_t value = swing_trace_emit( hand, now_ms, kind, /*has_natural_due=*/true, natural_due );
  return swing_trace_.pin_active ? timespan_t::from_millis( value - now_ms ) : t;
}

timespan_t recorder_t::swing_event_rescheduled( event_t* e, timespan_t delta_time )
{
  char hand;
  if ( !swing_trace_hand_of_event( e, hand ) )
    return delta_time;

  swing_trace_state_t::hand_t& h = swing_trace_.hand_state( hand );
  if ( !h.started )
  {
    ++swing_trace_.stale;
    return delta_time;
  }

  const std::int64_t now_ms = static_cast<std::int64_t>( root_->current_time().total_millis() );
  const std::int64_t natural_due = now_ms + static_cast<std::int64_t>( delta_time.total_millis() );
  const std::int64_t value = swing_trace_emit( hand, now_ms, SWING_ROW_RESCHED, /*has_natural_due=*/true, natural_due );
  return swing_trace_.pin_active ? timespan_t::from_millis( value - now_ms ) : delta_time;
}

void recorder_t::swing_event_canceled( event_t* e )
{
  char hand;
  if ( !swing_trace_hand_of_event( e, hand ) )
    return;

  swing_trace_state_t::hand_t& h = swing_trace_.hand_state( hand );
  if ( !h.started )
  {
    ++swing_trace_.stale;
    return;
  }

  const std::int64_t now_ms = static_cast<std::int64_t>( root_->current_time().total_millis() );
  swing_trace_emit( hand, now_ms, SWING_ROW_CANCEL, /*has_natural_due=*/false, 0 );
}

void recorder_t::swing_event_fired( action_t* action )
{
  char hand;
  if ( !swing_trace_hand_of_action( action, hand ) )
    return;

  swing_trace_state_t::hand_t& h = swing_trace_.hand_state( hand );
  if ( !h.started )
  {
    ++swing_trace_.stale;
    return;
  }

  // The event has come due; whether this becomes a `land` or a `drop` row is not yet known
  // here (section 1 (iv)) -- mark pending, the hand's own OWN engine state (execute_event) is
  // already null by the time this funnel is called.
  const std::int64_t now_ms = static_cast<std::int64_t>( root_->current_time().total_millis() );
  h.waiting = false;
  h.pending_fired = true;
  h.pending_fired_ms = now_ms;
}

void recorder_t::swing_attack_executed( action_t* action )
{
  char hand;
  if ( !swing_trace_hand_of_action( action, hand ) )
    return;

  swing_trace_state_t::hand_t& h = swing_trace_.hand_state( hand );

  // A-123: an in-place repeat (action->repeating == false at THIS execute -- Skyfury's own
  // generic::skyfury sets it false, calls execute(), then restores it true) is a proc-driven
  // extra attack, never a swing-clock operation. Return BEFORE resolving any pending firing --
  // the real swing's own pending state (if any) belongs to the ORDINARY repeating execute that
  // follows, not to this one.
  if ( !action->repeating )
  {
    ++h.repeats;
    return;
  }

  if ( !h.pending_fired )
  {
    // Defensive: every genuine repeating-attack execute is preceded by swing_event_fired at the
    // SAME synchronous call (action.cpp:257 then :2242) -- this should never happen. Counted
    // stale rather than fabricating a `land` row with no matching firing.
    ++swing_trace_.stale;
    return;
  }

  const std::int64_t fired_ms = h.pending_fired_ms;
  h.pending_fired = false;
  swing_trace_emit( hand, fired_ms, SWING_ROW_LAND, /*has_natural_due=*/false, 0 );

  // M-111 (e): mid_cast counts landings while the player had a cast or channel in progress.
  if ( action->player->executing || action->player->channeling )
    ++swing_trace_.mid_cast;
}

// Section 3's rolling check event. Constructed with its `id` preset to the largest `unsigned`
// value (reserved) BEFORE event_t::schedule() is called -- event_manager_t::add_event only ever
// assigns an id when `e->id == 0` (event_manager.cpp:117-118), so this id is never reassigned.
// Same-time events run in (time, id) order (event_manager.cpp:146-148); with the largest
// possible id, this event ALWAYS sorts after every ordinary event of its own millisecond,
// including ones created during that millisecond -- exactly what section 3 requires ("the check
// runs after fight B's own operations of that millisecond"). Only one is ever alive at a time
// (a per-breakpoint event scheduled at fight start would keep thousands waiting and lengthen
// event_manager_t::add_event's own same-slice insertion scan, section 3's own rationale for a
// SINGLE rolling event over one per breakpoint).
struct swing_check_event_t final : public event_t
{
  swing_check_event_t( sim_t& s, timespan_t delta ) : event_t( s )
  {
    id = std::numeric_limits<unsigned>::max();
    schedule( delta );
  }

  const char* name() const override
  {
    return "SwingTraceCheck";
  }

  void execute() override
  {
    sim_t* root = root_of( &sim() );
    if ( root->rl_rng_recorder )
      root->rl_rng_recorder->swing_trace_run_checks();
  }
};

void recorder_t::swing_trace_arm_check( bool initial )
{
  if ( swing_trace_.breakpoints.empty() )
    return;

  const std::int64_t now_ms = static_cast<std::int64_t>( root_->current_time().total_millis() );
  if ( initial )
  {
    swing_trace_.bp_cursor = static_cast<std::size_t>(
        std::lower_bound( swing_trace_.breakpoints.begin(), swing_trace_.breakpoints.end(), now_ms ) -
        swing_trace_.breakpoints.begin() );
  }
  if ( swing_trace_.bp_cursor >= swing_trace_.breakpoints.size() )
    return;

  // Section 3's own 2^31 guard: ordinary ids start at 1 and grow by one per event
  // (event_manager_t::add_event), so this only ever fires if a single fight somehow schedules
  // over two billion events -- refuse rather than risk an ordinary id ever reaching the
  // check event's own reserved id (std::numeric_limits<unsigned>::max()).
  if ( root_->event_mgr.global_event_id >= ( 1u << 31 ) )
  {
    throw sc_runtime_error(
        "rl_swing_pin: ordinary event id count reached 2^31 -- refusing to arm the rolling check "
        "event with a reserved id that could collide." );
  }

  const std::int64_t target_ms = swing_trace_.breakpoints[ swing_trace_.bp_cursor ];
  make_event<swing_check_event_t>( *root_, *root_, timespan_t::from_millis( target_ms - now_ms ) );
}

void recorder_t::swing_trace_run_check_for_hand( char hand, std::int64_t t )
{
  const auto& a_rows = swing_trace_.a_hand_rows( hand );
  std::size_t count_at_t = 0;
  for ( const auto& r : a_rows )
    if ( r.op_ms == t )
      ++count_at_t;
  if ( count_at_t == 0 )
    return;

  swing_trace_state_t::hand_t& h = swing_trace_.hand_state( hand );
  const bool same_ms_as_own = h.last_own_op_ms_set && h.last_own_op_ms == t;
  if ( same_ms_as_own && h.j >= count_at_t )
    return;

  bool a_has = false, a_waiting = false;
  std::int64_t a_due = 0;
  for ( const auto& r : a_rows )
  {
    if ( r.op_ms > t )
      break;
    a_has = true;
    a_waiting = !swing_row_is_end_kind( r.kind );
    a_due = r.due_ms;
  }
  if ( !a_has || !a_waiting || a_due <= t || !h.waiting )
  {
    ++swing_trace_.inject_skipped;
    return;
  }
  if ( h.due_ms == a_due )
    return;

  if ( same_ms_as_own )
    ++swing_trace_.injected_short;
  else
    ++swing_trace_.injected;
  swing_trace_append_row( hand, t, SWING_ROW_INJECT, a_due );
  h.due_ms = a_due;
}

void recorder_t::swing_trace_run_checks()
{
  const std::int64_t t = swing_trace_.breakpoints[ swing_trace_.bp_cursor ];
  swing_trace_run_check_for_hand( SWING_HAND_MH, t );
  swing_trace_run_check_for_hand( SWING_HAND_OH, t );
  ++swing_trace_.bp_cursor;
  swing_trace_arm_check( /*initial=*/false );
}

void recorder_t::swing_trace_write_file()
{
  if ( !swing_trace_.trace_active )
    return;

  std::ofstream out( root_->rl_swing_trace_file_str, std::ios::out | std::ios::trunc );
  if ( !out.is_open() )
  {
    throw sc_runtime_error( fmt::format(
        "rl_swing_trace: cannot write the trace file '{}' at fight end.", root_->rl_swing_trace_file_str ) );
  }
  out << "# rl_swing_trace format=1 actor=" << swing_trace_.designated_player->name_str << "\n";
  for ( const auto& row : swing_trace_.rows )
  {
    out << ( row.hand == SWING_HAND_MH ? "mh " : "oh " ) << row.op_ms << " "
        << swing_row_kind_name( row.kind ) << " " << row.due_ms << "\n";
  }
  out << "repeats mh " << swing_trace_.mh.repeats << "\n";
  out << "repeats oh " << swing_trace_.oh.repeats << "\n";
  out << "end " << swing_trace_.rows.size() << "\n";
  if ( !out )
  {
    throw sc_runtime_error( fmt::format( "rl_swing_trace: write to '{}' failed.", root_->rl_swing_trace_file_str ) );
  }
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
            << ", \"recorded_unused\": " << rp.recorded_unused;
    // Study-only (phase 257, D-15): present ONLY when the option is set, so a plain replay's
    // sidecar stays byte-identical to before this option existed.
    if ( rp.fresh_class_active )
    {
      sidecar << ", \"fresh_class\": \"" << json_escape( rp.fresh_class_name ) << "\""
              << ", \"fresh_class_left\": " << rp.fresh_class_left
              // Gap closure of 257-05's own crash (phase 257, plan 257-08): present ONLY
              // alongside fresh_class (never on its own), so a plain fresh-class run whose
              // fresh_from_ms was never set still reads 0 here -- explicit, not omitted.
              << ", \"fresh_from_ms\": " << rp.fresh_from_ms;
    }
    sidecar << "}, \"byClass\": {";
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
              << ", \"stop_time_ms\": " << fc.stop_time_ms;
      if ( rp.fresh_class_active )
      {
        sidecar << ", \"fresh_class_left\": " << fc.fresh_class_left;
      }
      sidecar << "}";
    }
    sidecar << "]}";

    // Study-only (tstl-sylvanas phase 257, plan 257-09, D-18): TWO SEPARATE top-level sidecar
    // keys (never nested under "replay" -- M-111 (e) names each its own block), each present
    // ONLY when its own option is set, so a plain replay's sidecar stays byte-identical to
    // before either option existed. Same field sets as write_footer()'s own stdout lines below.
    if ( swing_trace_.trace_active )
    {
      std::size_t mh_rows = 0, oh_rows = 0;
      for ( const auto& row : swing_trace_.rows )
        ( row.hand == SWING_HAND_MH ? mh_rows : oh_rows )++;
      sidecar << ", \"swing_trace\": {"
              << "\"rows\": " << swing_trace_.rows.size() << ", \"mh_rows\": " << mh_rows
              << ", \"oh_rows\": " << oh_rows << ", \"mh_repeats\": " << swing_trace_.mh.repeats
              << ", \"oh_repeats\": " << swing_trace_.oh.repeats
              << ", \"mid_cast\": " << swing_trace_.mid_cast << ", \"stale\": " << swing_trace_.stale
              << ", \"path\": \"" << json_escape( root_->rl_swing_trace_file_str ) << "\"}";
    }
    if ( swing_trace_.pin_active )
    {
      sidecar << ", \"swing_pin\": {"
              << "\"own_overrides\": "
              << ( swing_trace_.own_exact + swing_trace_.own_as_of + swing_trace_.past_value )
              << ", \"own_exact\": " << swing_trace_.own_exact
              << ", \"own_as_of\": " << swing_trace_.own_as_of
              << ", \"past_value\": " << swing_trace_.past_value
              << ", \"beyond\": " << swing_trace_.beyond
              << ", \"own_changed\": " << swing_trace_.own_changed
              << ", \"injected\": " << swing_trace_.injected
              << ", \"injected_short\": " << swing_trace_.injected_short
              << ", \"inject_skipped\": " << swing_trace_.inject_skipped
              << ", \"mh_repeats\": " << swing_trace_.mh.repeats
              << ", \"oh_repeats\": " << swing_trace_.oh.repeats
              << ", \"mid_cast\": " << swing_trace_.mid_cast << ", \"stale\": " << swing_trace_.stale
              << ", \"path\": \"" << json_escape( root_->rl_swing_pin_file_str ) << "\"}";
    }
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

    // Study-only (phase 257, D-15): ONE new stdout line, printed ONLY when the option is set --
    // unset, nothing here changes (byte-identical stdout to before this option existed).
    if ( replay_->fresh_class_active )
    {
      fmt::print( "rl_rng_replay fresh_class={} left_fresh={}\n", replay_->fresh_class_name,
                  replay_->fresh_class_left );
      // Gap closure of 257-05's own crash (tstl-sylvanas phase 257, plan 257-08): a SEPARATE
      // stdout line, so the pre-existing "fresh_class=... left_fresh=..." line (and its own
      // end-anchored regex in cause_isolation.py's check_fresh_class_output) stays byte-for-byte
      // unchanged -- only a caller that also checks THIS line learns the fresh-from floor.
      fmt::print( "rl_rng_replay fresh_from_ms={}\n", replay_->fresh_from_ms );
    }

    // Study-only (tstl-sylvanas phase 257, plan 257-09, D-18): TWO new stdout lines, each
    // printed ONLY when its own option is set (M-111 (e)) -- unset, nothing here changes
    // (byte-identical stdout to before either option existed). Same field sets as
    // write_sidecar()'s own "swing_trace"/"swing_pin" blocks above.
    if ( swing_trace_.trace_active )
    {
      std::size_t mh_rows = 0, oh_rows = 0;
      for ( const auto& row : swing_trace_.rows )
        ( row.hand == SWING_HAND_MH ? mh_rows : oh_rows )++;
      fmt::print( "rl_swing_trace rows={} mh_rows={} oh_rows={} mh_repeats={} oh_repeats={} "
                  "mid_cast={} stale={} path={}\n",
                  swing_trace_.rows.size(), mh_rows, oh_rows, swing_trace_.mh.repeats,
                  swing_trace_.oh.repeats, swing_trace_.mid_cast, swing_trace_.stale,
                  root_->rl_swing_trace_file_str );
    }
    if ( swing_trace_.pin_active )
    {
      fmt::print( "rl_swing_pin own_overrides={} own_exact={} own_as_of={} past_value={} beyond={} "
                  "own_changed={} injected={} injected_short={} inject_skipped={} mh_repeats={} "
                  "oh_repeats={} mid_cast={} stale={} path={}\n",
                  swing_trace_.own_exact + swing_trace_.own_as_of + swing_trace_.past_value,
                  swing_trace_.own_exact, swing_trace_.own_as_of, swing_trace_.past_value,
                  swing_trace_.beyond, swing_trace_.own_changed, swing_trace_.injected,
                  swing_trace_.injected_short, swing_trace_.inject_skipped, swing_trace_.mh.repeats,
                  swing_trace_.oh.repeats, swing_trace_.mid_cast, swing_trace_.stale,
                  root_->rl_swing_pin_file_str );
    }

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

// ---- Swing trace / swing pin (tstl-sylvanas phase 257, plan 257-09, D-18). See each
// declaration's own doc comment in rl_rng_record.hpp. Every one is a passthrough / no-op the
// moment root->rl_rng_recorder is null -- byte-identical to before either option existed.

timespan_t swing_event_created( sim_t* sim, action_t* action, timespan_t t )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return t;
  return root->rl_rng_recorder->swing_event_created( action, t );
}

timespan_t swing_event_rescheduled( sim_t* sim, event_t* e, timespan_t delta_time )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return delta_time;
  return root->rl_rng_recorder->swing_event_rescheduled( e, delta_time );
}

void swing_event_canceled( sim_t* sim, event_t* e )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return;
  root->rl_rng_recorder->swing_event_canceled( e );
}

void swing_event_fired( sim_t* sim, action_t* action )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return;
  root->rl_rng_recorder->swing_event_fired( action );
}

void swing_attack_executed( sim_t* sim, action_t* action )
{
  sim_t* root = root_of( sim );
  if ( !root->rl_rng_recorder )
    return;
  root->rl_rng_recorder->swing_attack_executed( action );
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
