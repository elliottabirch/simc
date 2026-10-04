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
#include "action/dbc_proc_callback.hpp"
#include "action/dot.hpp"
#include "buff/buff.hpp"
#include "player/pet.hpp"
#include "player/player.hpp"
#include "sim/cooldown.hpp"
#include "sim/decision_dump.hpp"
#include "sim/event.hpp"
#include "sim/rl_proc_counters.hpp"
#include "sim/sim.hpp"
#include "util/io.hpp"
#include "util/util.hpp"

#include "fmt/format.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <new>
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
// LEDGER-FORMAT-S1.md (261003-s1c plan 01 Task 2): the dated amendment this build announces in `hdr.amendments`.
#define S1_AMENDMENT "2026-10-03-s1-dk-ch"

// The record kinds this build writes (the `hdr` record's `emits` list). stage0_checks.py reads
// it to decide which identities it can assert.
constexpr const char* EMITS_JSON =
    "[\"hdr\",\"fb\",\"fe\",\"ftr\",\"pr\",\"hit\",\"xp\",\"app\",\"ln\",\"frm\",\"rd\",\"dr\",\"cs\",\"dotr\",\"zr\",\"cyc\",\"ref\",\"use\",\"cdn\",\"ext\",\"exr\",\"rps\"]";
}  // namespace

// 261003-s1c plan 02: the record sink seam. Every record the hooks produce goes through emit() below with its kind and a
// formatter. Backend 1 (the JSON backend, option rl_buff_ledger=<path>) runs the formatter into the fight's buffer, which is
// flushed to the file at fight end exactly as before. Backend 2 (the memory backend, option rl_buff_ledger_sink=memory) counts the
// record by kind and never runs the formatter: no text is built for a record, no file is opened, and the run's end writes only
// the summary (rl_buff_ledger_summary=<path>, optional). Everything a hook does besides formatting (counters, applier-list
// reconciliation, the frame bookkeeping) happens in both backends.
enum rec_kind_t : std::uint8_t
{
  REC_FB, REC_FE, REC_DOTR, REC_PR, REC_HIT, REC_XP, REC_APP, REC_LN, REC_FRM, REC_RD, REC_DR, REC_CS, REC_ZR,
  REC_CYC, REC_REF, REC_USE, REC_CDN, REC_RPS, REC_EXT, REC_EXR,
  REC_KIND_COUNT
};
constexpr const char* REC_KIND_NAMES[ REC_KIND_COUNT ] = { "fb", "fe", "dotr", "pr", "hit", "xp", "app", "ln", "frm", "rd",
                                                           "dr", "cs", "zr", "cyc", "ref", "use", "cdn", "rps", "ext", "exr" };

// The ledger's state, defined here so sim.hpp never sees it (sim.hpp forward-declares it).
struct state_t
{
  io::ofstream out;
  std::string path;
  // 261003-s1c plan 02: the sink. text = the JSON backend (formats records, writes the file); false = the memory backend.
  bool text = true;
  std::string summary_path;                           // memory backend: optional end-of-run summary file
  std::array<std::uint64_t, REC_KIND_COUNT> rec_n{};  // records by kind, both backends (never reset: a run total)

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
    std::string guards_json;  // plan 04: names of the shadow guards that fired (status 2), quoted, comma separated
    std::uint32_t n_passes = 0;
    // Plan 05: the candidates written for the hit, so the footer's both_group_candidates is counted where the
    // record is written (hit_sink) and always equals a recount over the records.
    std::vector<buff_t*> cands;
    // Plan 06: the innermost frame when the passes ran (-2: no passes ran, the sink takes its own frame).
    std::int32_t fr = -2;
    // Plan 07: the passes were taken from the cache (record field `cached`).
    bool cached = false;
    // Plan 12 (MJ-01): the check result of an AoE pre-made hit (empty: not an AoE pre-made hit).
    std::string pmc;
  };
  std::unordered_map<std::uint64_t, hit_entry_t> hit_table;
  std::uint64_t next_hit_id = 1;
  // One scratch state per action, owned here. NEVER taken from, or released into, an action's own
  // free list (get_state / action_state_t::release): that would reorder the list and could change
  // which recycled object later code receives.
  std::unordered_map<const action_t*, std::unique_ptr<action_state_t>> scratch;

  // Plan 04: application records kept alive until the fight ends. A tick looks its parent up here to
  // count candidate buffs the tick shares with its application (footer both_group_candidates).
  struct app_info_t
  {
    const action_t* action = nullptr;
    const player_t* target = nullptr;
    std::vector<const buff_t*> cands;
    // Plan 05: the reference (nothing hidden) pre-crit amount of a premade / tick-action entry, which the
    // single-target execute compares with the real amount (premade_drift).
    double ref_pre = 0.0;
  };
  std::unordered_map<std::uint64_t, app_info_t> apps;

  // Plan 04: the guards that fired inside the pass set that is running now (names are string
  // literals; the set is cleared when a shadow scope opens), the scratch generator every accessor
  // serves inside a pass, and the once-per-fight latch of the guard probe.
  std::vector<const char*> cur_guards;
  rng::rng_t scratch_rng;
  bool probe_done = false;

  // Plan 05: the stat buffs of each player the passes touch (the dealer and its owner), found once per
  // fight from the player's buff list, and the swing launches: the next launch id of the fight and the
  // launch each repeating action scheduled its next swing under (taken by rl_resolve_cause when that swing
  // executes). Cleared at every fight begin.
  std::unordered_map<const player_t*, std::vector<stat_buff_t*>> stat_buffs;
  std::int32_t next_launch = 0;
  std::unordered_map<const action_t*, std::int32_t> swing_slot;

  // Plan 06: the ledger's frame stack. One frame per rl_cause_scope_t pushed for the RL actor or its pets outside
  // the ledger's own passes (LIFO by construction: the scope's destructor pops what its constructor pushed).
  // Every `rd`, `dr`, `cs` and `ln` record names the frame it was made in, and takes its order from one counter
  // per fight, so the order of records inside a fight is the order things happened in.
  struct frame_t
  {
    std::int32_t id = -1;
    const player_t* p = nullptr;
    const action_t* owner = nullptr;
    const action_t* act = nullptr;  // owner, else the action whose code runs in the scope (may be null)
    // (buff, non-zero) pairs already written as `rd` in this frame: at most one record per pair.
    std::vector<std::pair<const buff_t*, bool>> seen;
    // True when the last record written in this frame is a `dr`: a run of draws with no read, consume or
    // launch of this frame between them is one record (it cannot change what lies between a read and a launch).
    bool last_dr = false;
  };
  std::vector<frame_t> frames;
  std::int32_t next_frame = 0;
  std::int64_t next_order = 0;
  // Plan 06: damage-over-time condition reads of this fight, per dot: [frame non-zero, frame zero, pass
  // non-zero, pass zero]. Written as one `dotr` record at fight end.
  std::unordered_map<const dot_t*, std::array<std::uint64_t, 4>> dot_reads;
  // Plan 06: the engine's generic proc switches: callback -> the buffs it was switched on with.
  std::unordered_map<const dbc_proc_callback_t*, std::vector<buff_t*>> switches;
  // Plan 06 run totals (footer): records written by kind and the guard counters.
  std::uint64_t frames_written = 0;
  std::uint64_t reads_written = 0;
  std::uint64_t draws_written = 0;
  std::uint64_t draws_raw = 0;
  std::uint64_t consumes_written = 0;
  std::uint64_t switch_launches = 0;
  std::uint64_t zero_records = 0;
  std::uint64_t dotr_rows = 0;
  std::uint64_t applier_reconciled = 0;
  // Diagnostic (footer `applier_reconciled_by_buff`): which buffs held more stacks than their applier list explained.
  std::map<std::string, std::uint64_t> reconciled_by_buff;
  std::uint64_t frame_pop_mismatch = 0;
  std::map<std::string, std::uint64_t> launches_by_kind;

  // Plan 13 (refund probe, option rl_buff_ledger_refund_probe): the named action, whether each press of this fight (index = press
  // number) was a press of it, and the run totals of the skipped shortenings (records `rps`; footer refund_probe_*). Empty name: off.
  std::string probe_action;
  std::vector<char> press_is_probe;
  std::uint64_t probe_skipped = 0;
  double probe_skipped_seconds = 0.0;
  std::map<std::string, std::pair<std::uint64_t, double>> probe_by_cooldown;

  // Plan 13 (ruling R1): the carried press handed back by a live carry_scope_t (PRESS_NONE: none), and the run totals `hit`
  // records with a carried press (press_carried) and carries refused because the press number is too large (overflow).
  std::int16_t carried_press = PRESS_NONE;
  std::uint64_t press_carried = 0;
  std::uint64_t press_carry_overflow = 0;

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
  // Plan 04: guard counts by name (an object in the footer), ticks that share a candidate buff with
  // their application, and the periodic-application records and their passes.
  std::map<std::string, std::uint64_t> shadow_violation;
  std::uint64_t both_group_candidates = 0;
  std::uint64_t app_records = 0;
  std::uint64_t app_passes = 0;
  // Plan 05: `app` records written for a class-made snapshot handed to schedule_execute (premade) and for
  // a tick action's application snapshot (both are also counted in app_records / app_passes); direct
  // hits executed from a pre-made state that had no entry (nothing was handed to schedule_execute at the
  // time of its snapshot).
  std::uint64_t premade_records = 0;
  std::uint64_t tick_action_records = 0;
  std::uint64_t premade_uncovered = 0;
  // Plan 05: swing launches written (`ln` records of kind swing).
  std::uint64_t swing_launches = 0;
  // Plan 05 diagnostic (footer `noncandidate_reads`): buffs a reference pass read non-zero that did not
  // become candidates, counted per pass set under "<name>|<reason>": `noplayer`, `foreign` (not the
  // dealer's or its owner's own buff, and not a debuff the dealer put on the hit's own target), or
  // `cls<N>|press<pos|neg>` (the buff's own applier stamp is not a press: class N, press >= 0 or not).
  std::map<std::string, std::uint64_t> noncandidate_reads;
  // Plan 05 diagnostic (footer `premade_uncovered_by_action`): premade_uncovered per action name.
  std::map<std::string, std::uint64_t> uncovered_by_action;

  // Plan 07: the refund ledger. Per cooldown of the RL actor (and its pets): the recharge cycle in progress (-1:
  // none) and the cycles whose charge is still held, oldest first (multi-charge cooldowns; a single-charge cooldown
  // names its last cycle in `cur` and that cycle is complete as soon as the cooldown is up). Cleared at fight begin.
  struct cd_state_t
  {
    std::int32_t cur = -1;
    std::vector<std::int32_t> done;
  };
  std::unordered_map<const cooldown_t*, cd_state_t> cds;
  std::int32_t next_cycle = 0;
  bool cd_live = false;  // false from fight begin until reset_done(): the iteration's reset phase writes no cooldown record
  // Plan 07: the optional pass cache (option rl_buff_ledger_cache). An entry is found by a key made of what the engine
  // has in hand when a pass set starts (action, target, amount type, the real state's snapshot fields, the real
  // amount); it names the buffs the last reference pass for that key read, and holds one stored result per
  // combination of (stack, value, press-applied) of those buffs and of the stat buffs of the dealer and its owner.
  // A stored result keeps the ratio of every pass to the reference pass. Kept for the whole run (nothing in it is a
  // function of the fight), emptied when it grows past CACHE_MAX_VALUES stored results.
  struct cache_pass_t
  {
    const char* kind = "";
    std::uint64_t hid = 0;
    double ratio = 1.0;  // pass pre-crit amount / reference pre-crit amount
    double cc = 0.0;
    double cb = 0.0;
  };
  struct cache_val_t
  {
    int status = 0;
    double ref_pre = 0.0;
    std::vector<cache_pass_t> passes;
    std::vector<buff_t*> cands;
  };
  struct cache_entry_t
  {
    std::vector<const buff_t*> reads;
    std::unordered_map<std::string, cache_val_t> by_values;
  };
  std::unordered_map<std::string, cache_entry_t> cache;
  std::size_t cache_values_stored = 0;
  // Plan 07 run totals (footer): records written by kind, and cycles the ledger had to open late because a cooldown
  // was found recharging with no cycle known (diagnostic, expected 0).
  std::uint64_t cycles_written = 0;
  std::uint64_t refunds_written = 0;
  std::uint64_t uses_written = 0;
  std::uint64_t cdn_written = 0;
  std::uint64_t cd_recovered = 0;
  // Plan 12: buff extensions (PREREG Amendment 1 item 3; records `ext`). Counted for buffs whose source player belongs to the
  // RL actor, in a fight after reset_done(). ext_by_buff: buff name -> (records, seconds added). The probe latches
  // (option rl_buff_ledger_ext_probe) allow one probed extension per fight and per buff family.
  std::uint64_t ext_seen = 0;
  std::uint64_t ext_in_frame = 0;
  std::uint64_t ext_written = 0;
  std::uint64_t ext_unattributed = 0;
  std::uint64_t ext_probe_fired = 0;
  std::uint64_t ext_matched_by_order = 0;
  // Plan 13 group 2c (review MJ-12-01, MN-12-02): extension windows of a synchronous multi-stack buff clipped (or dropped, nothing left)
  // by a refresh that moved the shared end; asynchronous entries paired by order AFTER exact matching (some entries matched exactly);
  // the extension probe's own refreshes (records `exr`) and its late extensions of a synchronous multi-stack buff.
  std::uint64_t ext_partial_by_order = 0;
  std::uint64_t ext_window_clipped = 0;
  std::uint64_t ext_window_dropped = 0;
  std::uint64_t ext_probe_refreshed = 0;
  std::uint64_t ext_probe_sync_late = 0;
  std::map<std::string, std::pair<std::uint64_t, double>> ext_by_buff;
  // Plan 12 (MJ-01, MJ-03): AoE pre-made hits drift-checked against their entry (ok + drift) or not checked (by reason); applications
  // that wrote no `app` record (zero reference, pre-made state); ticks that named no parent, by reason; links that named another
  // action's application.
  std::uint64_t premade_aoe_checked = 0;
  std::map<std::string, std::uint64_t> premade_aoe_unchecked;
  std::uint64_t app_zero_ref = 0;
  std::map<std::string, std::uint64_t> app_zero_ref_by_action;
  std::uint64_t app_missing_premade = 0;
  std::map<std::string, std::uint64_t> app_missing_premade_by_action;
  std::uint64_t tick_parent_stale = 0;
  std::map<std::string, std::uint64_t> tick_parent_stale_by_action;
  std::uint64_t tick_null_parent = 0;
  std::map<std::string, std::uint64_t> tick_null_parent_by_reason;
  bool ext_probe_single_done = false;
  bool ext_probe_multi_done = false;
  // Plan 13 group 2c (MJ-12-02 c): per fight, a late extension of a synchronous multi-stack buff (stage 0 -> 1), then, once its window has
  // passed, a second extension of a buff whose refresh moves the end (1 -> 2) followed by a refresh of that buff (2 -> 3, record `exr`): in
  // fights of even iteration number at the first press frame INSIDE the second window, in fights of odd number at the next press frame
  // (before the window begins), so the refresh clips a window that has begun in one and drops one that has not in the other (review MJ-12-01).
  int ext_probe_sync_stage = 0;
  buff_t* ext_probe_sync_buff = nullptr;
  timespan_t ext_probe_sync_from = timespan_t::zero();  // the second extension's window: old end
  timespan_t ext_probe_sync_end = timespan_t::zero();   // the window's new end (of the last late extension)
  // Plan 12, group 2 (MJ-04): a pass entry point reached while a pass is running is refused and counted (footer
  // nested_pass_refused, by entry point); the guard probe's nested scope and nested pass (probe_nesting_ok / _failed); the
  // probe's one dot cancel per fight (first tick pass set); the event blocks handed out inside a pass (never the pool's).
  std::uint64_t nested_pass_refused = 0;
  std::map<std::string, std::uint64_t> nested_pass_refused_by_entry;
  std::uint64_t probe_nesting_ok = 0;
  std::uint64_t probe_nesting_failed = 0;
  bool probe_tick_done = false;
  std::vector<std::unique_ptr<unsigned char[]>> scratch_event_blocks;
  // Plan 12, group 2 (MJ-05): the stage-0 charge probe (option rl_buff_ledger_charge_probe): once per fight, the first cooldown
  // of the actor with two or more charges has its maximum raised by one and put back (the engine's own set_max_charges).
  std::uint64_t charge_probe_fired = 0;
  bool charge_probe_done = false;
  // Plan 12, group 2, task 4: counters of the fixes whose defects no single record shows (footer; LEDGER-FORMAT.md amendment).
  std::uint64_t cd_max_charge_changes = 0;      // MJ-05: set_max_charges changes of a tracked cooldown (no record is written for them)
  std::uint64_t cd_overlong_recharge = 0;       // MN-05: a recharge that continued with more time left than its length (after a delay)
  std::uint64_t refresh_no_cover = 0;           // MJ-06: refreshes of a single-stack buff that moved nothing (refresh behaviour disabled)
  std::map<std::string, std::uint64_t> refresh_no_cover_by_buff;
  std::uint64_t async_trim_order_differs = 0;   // MN-03: overflow trims where "earliest expiry" and "oldest inserted" picked different entries
  std::uint64_t delay_merge_mixed_cause = 0;    // MN-02: a trigger merged into a pending aura-delay event whose captured cause differs
  std::map<std::string, std::uint64_t> delay_merge_mixed_cause_by_buff;
  std::uint64_t switch_buff_down = 0;           // MN-06: a switched callback ran with none of its registered switch buffs up (no switch launch)
  std::uint64_t own_reads_skipped = 0;          // MN-04: engine bookkeeping reads that are not written as frame reads
  std::uint64_t null_target_skipped = 0;        // MN-08: pass sets not run because the hit's or swing's target was null
  std::uint64_t zero_structural_skipped = 0;    // MN-10: zero-amount direct-hit pass sets with a structurally zero direct amount (no `zr` record)
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

// ,"key":{"name":count,...} (names in map order).
void put_count_object( std::string& b, const char* key, const std::map<std::string, std::uint64_t>& m )
{
  b += ",\"";
  b += key;
  b += "\":{";
  bool first = true;
  for ( const auto& kv : m )
  {
    if ( !first )
      b += ',';
    first = false;
    put_string( b, kv.first );
    fmt::format_to( out_it( b ), ":{}", kv.second );
  }
  b += '}';
}

// The sink seam (see rec_kind_t). `format` takes the fight buffer and appends exactly one record line; it is run by the JSON
// backend only. Nothing the engine reads depends on whether it ran.
template <typename Format>
inline void emit( state_t* s, rec_kind_t kind, Format&& format )
{
  ++s->rec_n[ kind ];
  if ( s->text )
    format( s->fight_buf );
}

// 261003-s1c plan 01 Task 2 (production f549d4f346, Phase 259): the class byte of a stamp may carry RL_CAUSE_DECK_MARK (0x80), which is
// not equal to any rl_cause_class enumerator. EVERY class this file reads or compares goes through rl_cause_base(); the raw byte is never
// compared, never written. The ledger writes the BASE class in `cls` and the mark as a separate bool `dk` (LEDGER-FORMAT-S1.md).
inline int cls_out( std::uint8_t cls )
{
  return static_cast<int>( rl_cause_base( cls ) );
}

inline const char* dk_out( std::uint8_t cls )
{
  return ( cls & RL_CAUSE_DECK_MARK ) != 0 ? "true" : "false";
}

// The same cause for the ledger's purposes: seq, base class, press and launch (the mark does not make another cause: an applier entry is
// written as [press, base class, stacks], so a marked and an unmarked stamp of one press are one entry).
inline bool same_cause( const rl_cause_t& a, const rl_cause_t& b )
{
  return a.seq == b.seq && rl_cause_base( a.cls ) == rl_cause_base( b.cls ) && press_key( a.press ) == press_key( b.press ) &&
         a.launch == b.launch;
}

bool is_own_class( std::uint8_t cls )
{
  const std::uint8_t base = rl_cause_base( cls );
  return base == RL_CAUSE_CAST || base == RL_CAUSE_PROC_OF_CAST || base == RL_CAUSE_DOT_TICK || base == RL_CAUSE_PROC_OF_DOT;
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

// Plan 06: what decides whether a buff or damage-over-time read is logged. g_reads_on (declared in buff.hpp,
// read by every read function) is true while the read tap is open (the reference pass of a hit) or while a ledger
// frame is open outside the ledger's own passes (shadow scope) and outside the ledger's own code (busy).
bool g_shadow_active        = false;
int g_busy                  = 0;
std::size_t g_frame_depth   = 0;
std::vector<buff_t*> g_switch_stack;  // top = the buff of the proc switch whose callback is executing (or null)

std::int32_t cur_frame( const state_t* st )
{
  return st->frames.empty() ? -1 : st->frames.back().id;
}

void update_gate()
{
  g_reads_on = g_tap_open || ( g_frame_depth > 0 && !g_shadow_active && g_busy == 0 );
}

// The ledger's own code reads buffs and actions (to write its records); none of that is a read of the fight.
struct busy_scope_t
{
  busy_scope_t()
  {
    ++g_busy;
    update_gate();
  }
  ~busy_scope_t()
  {
    --g_busy;
    update_gate();
  }
  busy_scope_t( const busy_scope_t& )            = delete;
  busy_scope_t& operator=( const busy_scope_t& ) = delete;
};
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
  st->probe_action = root->rl_buff_ledger_refund_probe_str;
  // 261003-s1c plan 02: the memory backend opens no file and writes no header (the summary file, when asked for, is written at the end).
  if ( root->rl_buff_ledger_sink_str == "memory" )
  {
    st->text         = false;
    st->summary_path = root->rl_buff_ledger_summary_str;
    root->rl_bl_state = std::move( st );
    g_ledger_open     = true;
    return;
  }
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
  fmt::format_to( out_it( b ), ",\"rl_buff_ledger_cache\":{}", root->rl_buff_ledger_cache ? 1 : 0 );
  fmt::format_to( out_it( b ), ",\"rl_buff_ledger_ext_probe\":{},\"rl_buff_ledger_charge_probe\":{}",
                  root->rl_buff_ledger_ext_probe ? 1 : 0, root->rl_buff_ledger_charge_probe ? 1 : 0 );
  b += ",\"rl_buff_ledger_refund_probe\":";
  put_string( b, root->rl_buff_ledger_refund_probe_str );
  b += "},\"emits\":";
  b += EMITS_JSON;
  // 261003-s1c plan 01 Task 2: announces LEDGER-FORMAT-S1.md (base classes in `cls`, the deck mark as `dk`, the chosen-enemy flag `ch`);
  // ledger_read.py then requires those fields.
  b += ",\"amendments\":[\"" S1_AMENDMENT "\"]";
  b += "}\n";
  st->out << b;
  st->out.flush();
  root->rl_bl_state = std::move( st );
  g_ledger_open     = true;
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
  s->apps.clear();
  s->cur_guards.clear();
  s->stat_buffs.clear();
  s->next_launch = 0;
  s->swing_slot.clear();
  s->frames.clear();
  s->next_frame = 0;
  s->next_order = 0;
  s->dot_reads.clear();
  s->cds.clear();
  s->next_cycle = 0;
  s->cd_live = false;
  g_frame_depth = 0;
  g_switch_stack.clear();
  update_gate();
  s->probe_done = false;
  s->ext_probe_single_done = false;
  s->ext_probe_multi_done = false;
  s->ext_probe_sync_stage = 0;
  s->ext_probe_sync_buff = nullptr;
  s->probe_tick_done = false;
  s->charge_probe_done = false;
  // A fixed seed at every fight begin: the scratch stream is never a function of the fight's own streams.
  s->scratch_rng.seed( 0x5CA1AB1E0DDBA11FULL );
  s->carried_press = PRESS_NONE;
  s->press_is_probe.clear();
  s->next_press = 0;
  s->last_press = -1;
  s->next_hit = 1;
  s->fight_hits = 0;
  s->fight_presses = 0;
  s->fight_xp = 0;

  emit( s, REC_FB, [ & ]( std::string& b ) {
    fmt::format_to( out_it( b ), "{{\"k\":\"fb\",\"it\":{},\"t\":", sim->current_iteration );
    put_double( b, sim->current_time().total_seconds() );
    fmt::format_to( out_it( b ), ",\"seed\":{},\"actor\":", sim->seed );
    put_string( b, s->actor->name() );
    b += "}\n";
  } );
}

void reset_done( sim_t* sim )
{
  state_t* s = state_of( sim );
  if ( s != nullptr && s->in_fight )
    s->cd_live = true;
}

void fight_end( sim_t* sim )
{
  state_t* s = state_of( sim );
  if ( s == nullptr || !s->in_fight )
    return;
  s->in_fight = false;
  s->hit_table.clear();
  s->apps.clear();

  // Same predicate rl_translog::record_close() writes as FLAG_COLLECTED (and sim_t's
  // datacollection_end() guard): iteration 0 is the warm-up fight unless there is only one.
  const bool collected = ( sim->iterations == 1 || sim->current_iteration >= 1 );

  // Plan 06: the damage-over-time condition reads of the fight, one record per fight. `counts` is by dot name over
  // both contexts ([non-zero reads, zero reads]); `by_ctx` is by "<dot>|<source actor>|<frame or pass>".
  ++s->dotr_rows;
  emit( s, REC_DOTR, [ & ]( std::string& o ) {
    std::map<std::string, std::array<std::uint64_t, 2>> by_name;
    std::map<std::string, std::array<std::uint64_t, 2>> by_ctx;
    for ( const auto& kv : s->dot_reads )
    {
      const dot_t* d = kv.first;
      const std::string src = d->source != nullptr ? std::string( d->source->name() ) : std::string();
      auto& n               = by_name[ d->name_str ];
      auto& f               = by_ctx[ fmt::format( "{}|{}|frame", d->name_str, src ) ];
      auto& p               = by_ctx[ fmt::format( "{}|{}|pass", d->name_str, src ) ];
      n[ 0 ] += kv.second[ 0 ] + kv.second[ 2 ];
      n[ 1 ] += kv.second[ 1 ] + kv.second[ 3 ];
      f[ 0 ] += kv.second[ 0 ];
      f[ 1 ] += kv.second[ 1 ];
      p[ 0 ] += kv.second[ 2 ];
      p[ 1 ] += kv.second[ 3 ];
    }
    fmt::format_to( out_it( o ), "{{\"k\":\"dotr\",\"it\":{},\"counts\":{{", sim->current_iteration );
    bool first = true;
    for ( const auto& kv : by_name )
    {
      if ( !first )
        o += ',';
      first = false;
      put_string( o, kv.first );
      fmt::format_to( out_it( o ), ":[{},{}]", kv.second[ 0 ], kv.second[ 1 ] );
    }
    o += "},\"by_ctx\":{";
    first = true;
    for ( const auto& kv : by_ctx )
    {
      if ( kv.second[ 0 ] == 0 && kv.second[ 1 ] == 0 )
        continue;
      if ( !first )
        o += ',';
      first = false;
      put_string( o, kv.first );
      fmt::format_to( out_it( o ), ":[{},{}]", kv.second[ 0 ], kv.second[ 1 ] );
    }
    o += "}}\n";
  } );

  emit( s, REC_FE, [ & ]( std::string& b ) {
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
  } );

  ++s->run_fights;
  if ( collected )
    ++s->run_collected_fights;
  s->run_hits += s->fight_hits;
  s->run_presses += s->fight_presses;
  s->run_xp += s->fight_xp;

  if ( s->text )
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
  b += ",\"shadow_violation\":{";
  {
    bool first = true;
    for ( const auto& kv : s->shadow_violation )
    {
      if ( !first )
        b += ',';
      first = false;
      put_string( b, kv.first );
      fmt::format_to( out_it( b ), ":{}", kv.second );
    }
  }
  b += '}';
  fmt::format_to( out_it( b ), ",\"passes_total\":{},\"pass_hist\":[", s->passes_total );
  for ( std::size_t i = 0; i < PASS_HIST_SIZE; ++i )
  {
    if ( i )
      b += ',';
    fmt::format_to( out_it( b ), "{}", s->pass_hist[ i ] );
  }
  // The last three counters were declared by plan 04 (LEDGER-FORMAT.md amendment); they are not part
  // of the version-1 required set.
  fmt::format_to( out_it( b ),
                  "],\"swing_rescaled\":{},\"foreign_press\":{},\"cache_hits\":{},\"cache_misses\":{},"
                  "\"cache_check\":{},\"cache_check_fail\":{},\"no_stats_hits\":{},"
                  "\"both_group_candidates\":{},\"app_records\":{},\"app_passes\":{},"
                  "\"premade_records\":{},\"tick_action_records\":{},\"premade_uncovered\":{},"
                  "\"swing_launches\":{},\"noncandidate_reads\":{{",
                  s->swing_rescaled, s->foreign_press, s->cache_hits, s->cache_misses, s->cache_check,
                  s->cache_check_fail, s->no_stats_hits, s->both_group_candidates, s->app_records, s->app_passes,
                  s->premade_records, s->tick_action_records, s->premade_uncovered, s->swing_launches );
  {
    bool first = true;
    for ( const auto& kv : s->noncandidate_reads )
    {
      if ( !first )
        b += ',';
      first = false;
      put_string( b, kv.first );
      fmt::format_to( out_it( b ), ":{}", kv.second );
    }
  }
  b += "},\"premade_uncovered_by_action\":{";
  {
    bool first = true;
    for ( const auto& kv : s->uncovered_by_action )
    {
      if ( !first )
        b += ',';
      first = false;
      put_string( b, kv.first );
      fmt::format_to( out_it( b ), ":{}", kv.second );
    }
  }
  b += "}";
  // Plan 06 counters (optional footer keys; LEDGER-FORMAT.md amendment 2026-10-02, plan 06).
  fmt::format_to( out_it( b ),
                  ",\"frames_written\":{},\"reads_written\":{},\"draws_written\":{},\"draws_raw\":{},"
                  "\"consumes_written\":{},\"switch_launches\":{},\"zero_records\":{},\"dotr_rows\":{},"
                  "\"applier_reconciled\":{},\"frame_pop_mismatch\":{},\"launches_by_kind\":{{",
                  s->frames_written, s->reads_written, s->draws_written, s->draws_raw, s->consumes_written,
                  s->switch_launches, s->zero_records, s->dotr_rows, s->applier_reconciled, s->frame_pop_mismatch );
  {
    bool first = true;
    for ( const auto& kv : s->launches_by_kind )
    {
      if ( !first )
        b += ',';
      first = false;
      put_string( b, kv.first );
      fmt::format_to( out_it( b ), ":{}", kv.second );
    }
  }
  b += "},\"applier_reconciled_by_buff\":{";
  {
    bool first = true;
    for ( const auto& kv : s->reconciled_by_buff )
    {
      if ( !first )
        b += ',';
      first = false;
      put_string( b, kv.first );
      fmt::format_to( out_it( b ), ":{}", kv.second );
    }
  }
  b += "}";
  // Plan 07 counters (optional footer keys; LEDGER-FORMAT.md amendment 2026-10-02, plan 07).
  fmt::format_to( out_it( b ),
                  ",\"cycles_written\":{},\"refunds_written\":{},\"uses_written\":{},\"cdn_written\":{},"
                  "\"cd_recovered\":{}",
                  s->cycles_written, s->refunds_written, s->uses_written, s->cdn_written, s->cd_recovered );
  // Plan 12 counters (optional footer keys; LEDGER-FORMAT.md amendment 2026-10-02, plan 12 group 1).
  fmt::format_to( out_it( b ),
                  ",\"ext_seen\":{},\"ext_in_frame\":{},\"ext_written\":{},\"ext_unattributed\":{},"
                  "\"ext_probe_fired\":{},\"ext_matched_by_order\":{},"
                  "\"ext_partial_by_order\":{},\"ext_window_clipped\":{},\"ext_window_dropped\":{},"
                  "\"ext_probe_refreshed\":{},\"ext_probe_sync_late\":{},\"ext_by_buff\":{{",
                  s->ext_seen, s->ext_in_frame, s->ext_written, s->ext_unattributed, s->ext_probe_fired,
                  s->ext_matched_by_order, s->ext_partial_by_order, s->ext_window_clipped, s->ext_window_dropped,
                  s->ext_probe_refreshed, s->ext_probe_sync_late );
  {
    bool first = true;
    for ( const auto& kv : s->ext_by_buff )
    {
      if ( !first )
        b += ',';
      first = false;
      put_string( b, kv.first );
      fmt::format_to( out_it( b ), ":[{},", kv.second.first );
      put_double( b, kv.second.second );
      b += ']';
    }
  }
  b += "}";
  // Plan 12 counters, task 2 (MJ-01, MJ-03).
  fmt::format_to( out_it( b ),
                  ",\"premade_aoe_checked\":{},\"app_zero_ref\":{},\"app_missing_premade\":{},\"tick_parent_stale\":{},"
                  "\"tick_null_parent\":{}",
                  s->premade_aoe_checked, s->app_zero_ref, s->app_missing_premade, s->tick_parent_stale, s->tick_null_parent );
  b += ",\"premade_aoe_unchecked\":{";
  {
    bool first = true;
    for ( const auto& kv : s->premade_aoe_unchecked )
    {
      if ( !first )
        b += ',';
      first = false;
      put_string( b, kv.first );
      fmt::format_to( out_it( b ), ":{}", kv.second );
    }
  }
  b += '}';
  b += ",\"app_zero_ref_by_action\":{";
  {
    bool first = true;
    for ( const auto& kv : s->app_zero_ref_by_action )
    {
      if ( !first )
        b += ',';
      first = false;
      put_string( b, kv.first );
      fmt::format_to( out_it( b ), ":{}", kv.second );
    }
  }
  b += '}';
  b += ",\"app_missing_premade_by_action\":{";
  {
    bool first = true;
    for ( const auto& kv : s->app_missing_premade_by_action )
    {
      if ( !first )
        b += ',';
      first = false;
      put_string( b, kv.first );
      fmt::format_to( out_it( b ), ":{}", kv.second );
    }
  }
  b += '}';
  b += ",\"tick_parent_stale_by_action\":{";
  {
    bool first = true;
    for ( const auto& kv : s->tick_parent_stale_by_action )
    {
      if ( !first )
        b += ',';
      first = false;
      put_string( b, kv.first );
      fmt::format_to( out_it( b ), ":{}", kv.second );
    }
  }
  b += '}';
  b += ",\"tick_null_parent_by_reason\":{";
  {
    bool first = true;
    for ( const auto& kv : s->tick_null_parent_by_reason )
    {
      if ( !first )
        b += ',';
      first = false;
      put_string( b, kv.first );
      fmt::format_to( out_it( b ), ":{}", kv.second );
    }
  }
  b += '}';
  // Plan 12 counters, group 2, task 3 (MJ-04).
  fmt::format_to( out_it( b ),
                  ",\"nested_pass_refused\":{},\"probe_nesting_ok\":{},\"probe_nesting_failed\":{}",
                  s->nested_pass_refused, s->probe_nesting_ok, s->probe_nesting_failed );
  put_count_object( b, "nested_pass_refused_by_entry", s->nested_pass_refused_by_entry );
  fmt::format_to( out_it( b ), ",\"charge_probe_fired\":{}", s->charge_probe_fired );
  // Plan 12 counters, group 2, task 4 (MJ-05, MJ-06, MN-02..MN-06, MN-08, MN-10).
  fmt::format_to( out_it( b ),
                  ",\"cd_max_charge_changes\":{},\"cd_overlong_recharge\":{},\"refresh_no_cover\":{},"
                  "\"async_trim_order_differs\":{},\"delay_merge_mixed_cause\":{},\"switch_buff_down\":{},"
                  "\"own_reads_skipped\":{},\"null_target_skipped\":{},\"zero_structural_skipped\":{}",
                  s->cd_max_charge_changes, s->cd_overlong_recharge, s->refresh_no_cover, s->async_trim_order_differs,
                  s->delay_merge_mixed_cause, s->switch_buff_down, s->own_reads_skipped, s->null_target_skipped,
                  s->zero_structural_skipped );
  put_count_object( b, "refresh_no_cover_by_buff", s->refresh_no_cover_by_buff );
  put_count_object( b, "delay_merge_mixed_cause_by_buff", s->delay_merge_mixed_cause_by_buff );
  // Plan 13 counters (ruling R1: the carried press).
  fmt::format_to( out_it( b ), ",\"press_carried\":{},\"press_carry_overflow\":{}", s->press_carried, s->press_carry_overflow );
  // Plan 13 (refund probe): always written, 0 / empty with the option absent.
  b += ",\"refund_probe_action\":";
  put_string( b, s->probe_action );
  fmt::format_to( out_it( b ), ",\"refund_probe_skipped\":{},\"refund_probe_skipped_seconds\":", s->probe_skipped );
  put_double( b, s->probe_skipped_seconds );
  b += ",\"refund_probe_skipped_by_cooldown\":{";
  {
    bool first = true;
    for ( const auto& kv : s->probe_by_cooldown )
    {
      if ( !first )
        b += ',';
      first = false;
      put_string( b, kv.first );
      fmt::format_to( out_it( b ), ":[{},", kv.second.first );
      put_double( b, kv.second.second );
      b += ']';
    }
  }
  b += '}';
  b += "}\n";

  if ( !s->text )
  {
    // The memory backend: no ledger file. The summary (optional) is one JSON object: the records counted by kind and the very
    // footer the JSON backend would have written.
    if ( !s->summary_path.empty() )
    {
      std::string sum = "{\"k\":\"sum\",\"sink\":\"memory\",\"records\":{";
      for ( int i = 0; i < REC_KIND_COUNT; ++i )
      {
        if ( i )
          sum += ',';
        fmt::format_to( out_it( sum ), "\"{}\":{}", REC_KIND_NAMES[ i ], s->rec_n[ i ] );
      }
      sum += "},\"ftr\":";
      sum += b.substr( 0, b.size() - 1 );
      sum += "}\n";
      io::ofstream f;
      f.open( s->summary_path );
      if ( !f.is_open() )
        throw sc_runtime_error( fmt::format( "rl_buff_ledger_summary=: cannot open '{}' for writing.", s->summary_path ) );
      f << sum;
      f.close();
    }
    return;
  }
  s->out << b;
  s->out.flush();
  s->out.close();
}

std::int16_t carry_capture( const player_t* p )
{
  sim_t* sim = p->sim;
  if ( !sim->rl_bl_on || p->rl_cause_stack.empty() )
    return PRESS_NONE;
  state_t* s = state_of( sim );
  if ( s == nullptr || !s->in_fight )
    return PRESS_NONE;
  const std::int16_t press = p->rl_cause_stack.back().cause.press;
  if ( press < 0 )
    return PRESS_NONE;  // a sentinel, or a value that is already carried: nothing to carry
  if ( press > PRESS_CARRY_MAX )
  {
    ++s->press_carry_overflow;
    return PRESS_NONE;
  }
  return press_carry_encode( press );
}

carry_scope_t::carry_scope_t( sim_t* sim_, std::int16_t carried ) : sim( sim_ ), prev( PRESS_NONE ), active( false )
{
  if ( !sim->rl_bl_on || carried == PRESS_NONE )
    return;
  state_t* s = state_of( sim );
  if ( s == nullptr )
    return;
  prev            = s->carried_press;
  s->carried_press = carried;
  active          = true;
}

carry_scope_t::~carry_scope_t()
{
  if ( !active )
    return;
  state_t* s = state_of( sim );
  if ( s != nullptr )
    s->carried_press = prev;
}

std::int16_t orphan_press( sim_t* sim )
{
  state_t* s = state_of( sim );
  return s != nullptr && s->carried_press != PRESS_NONE ? s->carried_press : PRESS_ORPHAN;
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
  // Plan 13 (refund probe): whether this press is a press of the named action (matched by the name the `pr` record below carries).
  if ( !s->probe_action.empty() )
    s->press_is_probe.push_back( p->last_foreground_action != nullptr && s->probe_action == p->last_foreground_action->name() ? 1 : 0 );

  emit( s, REC_PR, [ & ]( std::string& b ) {
    fmt::format_to( out_it( b ), "{{\"k\":\"pr\",\"it\":{},\"t\":", p->sim->current_iteration );
    put_double( b, p->sim->current_time().total_seconds() );
    fmt::format_to( out_it( b ), ",\"press\":{},\"seq\":{},\"actor\":", press, cause.seq );
    put_string( b, p->name() );
    b += ",\"action\":";
    put_string( b, p->last_foreground_action ? p->last_foreground_action->name() : std::string() );
    fmt::format_to( out_it( b ), ",\"cls\":{},\"dk\":{}}}\n", cls_out( cause.cls ), dk_out( cause.cls ) );
  } );
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

namespace
{
void count_both_group( state_t* st, std::uint64_t parent_id, const std::vector<buff_t*>& cands );
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
  // 261003-s1c plan 01 Task 2: did this hit strike the chosen enemy (the funnel's tag) AT THE MOMENT OF THE HIT? The predicate is the one
  // rl_credit_route evaluates for the chosen copy of the credit (rl_credit_hit_is_chosen, rl_translog.cpp): a pet answers with its owner's
  // tag, nullptr is never chosen. Written as `ch`; nothing in the engine reads it.
  const bool hit_chosen = rl_credit_hit_is_chosen( a->player, state->target );

  if ( is_own_class( state->rl_cause_class ) && state->rl_cause_press == PRESS_NONE )
    ++s->lost_press;
  if ( press_is_carried( state->rl_cause_press ) )
    ++s->press_carried;

  const result_amount_type rt = a->report_amount_type( state );
  const bool tick = rt == result_amount_type::DMG_OVER_TIME || rt == result_amount_type::HEAL_OVER_TIME;

  // Plan 12 (MJ-03): the parent of a tick. The application id the DoT's state carries, unless it is a reserved value (no
  // record exists: say why) or names the application of another action (stale); a tick action's tick names its entry. Every
  // tick with no parent is counted by reason.
  std::uint64_t parent = 0;
  if ( tick )
  {
    const char* null_reason = nullptr;
    const std::uint64_t id  = state->rl_bl_app;
    if ( id == APP_ZERO_REF )
      null_reason = "zero_ref";
    else if ( id == APP_PREMADE )
      null_reason = "premade";
    else if ( id != 0 )
    {
      auto ap = s->apps.find( id );
      if ( ap != s->apps.end() && ap->second.action == a )
        parent = id;
      else
      {
        null_reason = "stale";
        ++s->tick_parent_stale;
        ++s->tick_parent_stale_by_action[ a->name() ];
      }
    }
    else if ( state->rl_bl_pm != 0 )
      parent = state->rl_bl_pm;
    else
      null_reason = "none";
    if ( parent == 0 )
    {
      ++s->tick_null_parent;
      ++s->tick_null_parent_by_reason[ fmt::format( "{}|{}", a->name(), null_reason ) ];
    }
  }
  std::string pmc_text;

  // Plan 03: attach the hide-and-recompute result run_passes() parked for this state, if any. An
  // id that is not in the table was already consumed (or the state never ran passes) and is not an
  // error; an id whose action or target differs from this hit's is STALE (status 5, counted).
  int status = 0;
  std::string cand_json;
  std::string pass_json;
  std::string guards_json;
  bool cached = false;
  std::int32_t fr = cur_frame( s );
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
        if ( it->second.fr != -2 )
          fr = it->second.fr;
        cand_json = std::move( it->second.cand_json );
        pass_json = std::move( it->second.pass_json );
        guards_json = std::move( it->second.guards_json );
        cached = it->second.cached;
        pmc_text = std::move( it->second.pmc );
        if ( !pmc_text.empty() )
        {
          if ( pmc_text == "ok" || pmc_text == "drift" )
            ++s->premade_aoe_checked;
          else
            ++s->premade_aoe_unchecked[ pmc_text ];
        }
        // Plan 05: a hit that shares a candidate with the entry it names as parent (a tick's application, a
        // pre-made state's hand-over entry).
        if ( !it->second.cands.empty() )
          count_both_group( s, tick ? parent : state->rl_bl_pm, it->second.cands );
        const std::uint32_t n = it->second.n_passes;
        s->passes_total += n;
        ++s->pass_hist[ std::min<std::size_t>( n, PASS_HIST_SIZE - 1 ) ];
      }
      s->hit_table.erase( it );
    }
    state->rl_bl_hit = 0;
  }

  emit( s, REC_HIT, [ & ]( std::string& b ) {
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
                    ",\"exp_excl\":{},\"seq\":{},\"cls\":{},\"dk\":{},\"ch\":{},\"press\":{},\"launch\":{},\"n_targets\":{},"
                    "\"status\":{},\"cand\":[",
                    exp_excl ? "true" : "false", state->rl_cause_seq, cls_out( state->rl_cause_class ),
                    dk_out( state->rl_cause_class ), hit_chosen ? "true" : "false", state->rl_cause_press, state->rl_cause_launch,
                    state->n_targets, status );
    b += cand_json;
    b += "],\"pass\":[";
    b += pass_json;
    // Plan 04: a tick of a damage-over-time effect names the `app` record its state carries (null when
    // the effect came from a state that had none); `par` repeats it as the format's parent list.
    // Plan 05: a tick action's tick (no rl_bl_app of its own) names the `app` record of the tick action's
    // application snapshot; a direct hit made from a pre-made state names the `app` record (src "premade")
    // written when the state was handed to schedule_execute. Direct hits never carry `parent`.
    if ( tick )
    {
      if ( parent != 0 )
        fmt::format_to( out_it( b ), "],\"par\":[{0}],\"parent\":{0}", parent );
      else
        b += "],\"par\":[],\"parent\":null";
    }
    else if ( state->rl_bl_pm != 0 )
      fmt::format_to( out_it( b ), "],\"par\":[{}]", state->rl_bl_pm );
    else
      b += "],\"par\":[]";
    if ( !guards_json.empty() )
    {
      b += ",\"guards\":[";
      b += guards_json;
      b += ']';
    }
    // Plan 06: the frame the hit's passes ran in (the sink's own frame when it had none).
    fmt::format_to( out_it( b ), ",\"fr\":{}", fr );
    if ( !pmc_text.empty() )
    {
      b += ",\"pmc\":";
      put_string( b, pmc_text );
    }
    if ( cached )
      b += ",\"cached\":true";
    b += "}\n";
  } );
}

void xp_record( player_t* p, const rl_cause_t& cause, double amount, const char* action_name, bool on_chosen )
{
  sim_t* sim = p->sim;
  state_t* s = state_of( sim );
  if ( s == nullptr || !s->in_fight || s->in_hit_sink || !belongs_to_actor( *s, p ) )
    return;

  ++s->fight_xp;
  emit( s, REC_XP, [ & ]( std::string& b ) {
    fmt::format_to( out_it( b ), "{{\"k\":\"xp\",\"it\":{},\"t\":", sim->current_iteration );
    put_double( b, sim->current_time().total_seconds() );
    fmt::format_to( out_it( b ), ",\"seq\":{},\"cls\":{},\"dk\":{},\"ch\":{},\"press\":{},\"launch\":{},\"amount\":", cause.seq,
                    cls_out( cause.cls ), dk_out( cause.cls ), on_chosen ? "true" : "false", cause.press, cause.launch );
    put_double( b, amount );
    b += ",\"action\":";
    put_string( b, action_name != nullptr ? action_name : "" );
    b += ",\"actor\":";
    put_string( b, p->name() );
    b += "}\n";
  } );
}

// ==========================================================================
// Plan 03: hide-and-recompute passes
// ==========================================================================

// The read tap (buff.hpp declares both). g_tap_open is true only while the REFERENCE pass of a hit
// runs; note_read records each buff once with a non-zero stack or value, in first-read order.
bool g_tap_open = false;
bool g_reads_on = false;
// Plan 12 (NT-02): see rl_buff_ledger.hpp.
bool g_ledger_open = false;

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

namespace
{
// ---- Plan 06: applier lists ------------------------------------------------------------------------------------
//
// buff_t::rl_bl_appliers is maintained by the hooks in buff.cpp (applications append, expiries of independent
// stacks and clears remove) and brought up to the buff's real stack count lazily, at every read of the list and at
// the start of every bump: consumes made through decrement() (and its stat_buff / cost_reduction overrides) take
// the OLDEST stacks, which is exactly what trimming from the front does. A count that cannot be explained (the
// list holds fewer stacks than the buff has) adds an "unknown" applier and is counted in the footer
// (`applier_reconciled`); the check `appliers` reports it.

bool covering_mode( const buff_t* b )
{
  return b->max_stack() == 1;
}

int applier_total( const buff_t* b )
{
  int total = 0;
  for ( const buff_t::rl_bl_applier_t& e : b->rl_bl_appliers )
    total += e.stacks;
  return total;
}

void reconcile_appliers( state_t* st, buff_t* b )
{
  auto& v = b->rl_bl_appliers;
  if ( covering_mode( b ) )
  {
    if ( b->current_stack <= 0 )
    {
      v.clear();
      return;
    }
    if ( v.empty() )
    {
      v.push_back( { rl_cause_t{}, 1, timespan_t::max() } );
      if ( st != nullptr )
      {
        ++st->applier_reconciled;
        ++st->reconciled_by_buff[ b->name_str + "|covering" ];
      }
      return;
    }
    // The covering appliers are the entries whose own expiry is still ahead. If none is (the buff is up through
    // an extension or a longer duration some other function gave it), the latest application stays.
    const timespan_t now = b->sim->current_time();
    const buff_t::rl_bl_applier_t newest = v.back();
    v.erase( std::remove_if( v.begin(), v.end(),
                             [ & ]( const buff_t::rl_bl_applier_t& e ) { return !( e.expiry > now ); } ),
             v.end() );
    if ( v.empty() )
      v.push_back( newest );
    return;
  }

  int total       = applier_total( b );
  const int stack = b->current_stack < 0 ? 0 : b->current_stack;
  if ( total > stack )
  {
    int excess = total - stack;
    while ( excess > 0 && !v.empty() )
    {
      if ( v.front().stacks <= excess )
      {
        excess -= v.front().stacks;
        v.erase( v.begin() );
      }
      else
      {
        v.front().stacks -= excess;
        excess = 0;
      }
    }
  }
  else if ( total < stack )
  {
    v.push_back( { rl_cause_t{}, stack - total, timespan_t::min() } );
    if ( st != nullptr )
    {
      ++st->applier_reconciled;
      ++st->reconciled_by_buff[ b->name_str + "|stacks" ];
    }
  }
}

bool is_press_class( const rl_cause_t& c )
{
  const std::uint8_t base = rl_cause_base( c.cls );
  return ( base == RL_CAUSE_PROC_OF_CAST || base == RL_CAUSE_PROC_OF_DOT ) && c.press >= 0;
}

// Plan 12: whose the stacks of an entry are at `now`: the most recently added extension whose time window (old end to
// new end) holds `now`, else the entry's own cause (a covering entry has no extensions).
const rl_cause_t& effective_cause( const buff_t::rl_bl_applier_t& e, timespan_t now )
{
  for ( auto it = e.exts.rbegin(); it != e.exts.rend(); ++it )
    if ( it->from <= now && now < it->to )
      return it->cause;
  return e.cause;
}

// The buff's appliers as `[[press,cls,stacks]...]` (one entry per (press, cls), stacks summed; a buff with more than
// one stack) or `[[press,cls,1,true]...]` (a single-stack buff: the covering appliers, one entry per press for
// presses, one per application for the rest). Returns true for the covering form.
bool write_appliers( std::string& b, state_t* st, buff_t* c )
{
  reconcile_appliers( st, c );  // (the memory backend calls reconcile_appliers alone: its only state effect)
  const bool covering = covering_mode( c );
  b += '[';
  bool first = true;
  if ( covering )
  {
    std::vector<std::pair<int, int>> seen;
    for ( const buff_t::rl_bl_applier_t& e : c->rl_bl_appliers )
    {
      if ( e.cause.press >= 0 )
      {
        const std::pair<int, int> key{ e.cause.press, cls_out( e.cause.cls ) };
        if ( std::find( seen.begin(), seen.end(), key ) != seen.end() )
          continue;
        seen.push_back( key );
      }
      if ( !first )
        b += ',';
      first = false;
      fmt::format_to( out_it( b ), "[{},{},1,true]", e.cause.press, cls_out( e.cause.cls ) );
    }
  }
  else
  {
    std::vector<std::array<int, 3>> merged;
    const timespan_t now = c->sim->current_time();
    for ( const buff_t::rl_bl_applier_t& e : c->rl_bl_appliers )
    {
      const rl_cause_t& who = effective_cause( e, now );
      bool found            = false;
      for ( std::array<int, 3>& m : merged )
        if ( press_key( static_cast<std::int16_t>( m[ 0 ] ) ) == press_key( who.press ) && m[ 1 ] == cls_out( who.cls ) )
        {
          m[ 2 ] += e.stacks;
          found = true;
          break;
        }
      if ( !found )
        merged.push_back( { static_cast<int>( who.press ), cls_out( who.cls ), e.stacks } );
    }
    for ( const std::array<int, 3>& m : merged )
    {
      if ( !first )
        b += ',';
      first = false;
      fmt::format_to( out_it( b ), "[{},{},{}]", m[ 0 ], m[ 1 ], m[ 2 ] );
    }
  }
  b += ']';
  return covering;
}

// ---- Plan 06: frames ---------------------------------------------------------------------------------------------

// One `rd` record for a buff read inside the innermost frame (a non-zero read carries the buff's appliers).
void frame_read( const buff_t* cb, int stack, double value )
{
  if ( cb->sim == nullptr || cb->is_fallback )
    return;  // a fallback buff stands in for a buff that does not exist: it can never be a gate
  state_t* s = state_of( cb->sim );
  if ( s == nullptr || !s->in_fight || s->frames.empty() || cb->sim->rl_bl_shadow || g_busy > 0 )
    return;
  state_t::frame_t& f = s->frames.back();
  // A read is non-zero when the buff is up (stacks != 0). A stale value on a buff that is not up (measured: read
  // during its own expiry) is a zero read, whatever the value member holds. A read through total_stack() can
  // include stacks still waiting in the aura delay while the buff itself is not up: non-zero, flagged `pend`.
  (void) value;
  const bool nz = stack != 0;
  for ( const auto& e : f.seen )
    if ( e.first == cb && e.second == nz )
      return;
  f.seen.emplace_back( cb, nz );
  f.last_dr = false;
  ++s->reads_written;

  buff_t* b        = const_cast<buff_t*>( cb );
  const bool pend  = nz && b->current_stack <= 0;
  const std::int64_t order = s->next_order++;
  if ( !s->text && nz && !pend )
    reconcile_appliers( s, b );  // the state effect write_appliers has
  emit( s, REC_RD, [ & ]( std::string& o ) {
    fmt::format_to( out_it( o ), "{{\"k\":\"rd\",\"it\":{},\"t\":", b->sim->current_iteration );
    put_double( o, b->sim->current_time().total_seconds() );
    fmt::format_to( out_it( o ), ",\"f\":{},\"o\":{},\"buff\":", f.id, order );
    put_string( o, b->name_str );
    o += ",\"owner\":";
    put_string( o, b->player != nullptr ? std::string( b->player->name() ) : std::string() );
    fmt::format_to( out_it( o ), ",\"nz\":{},\"stacks\":{},\"app\":", nz ? "true" : "false", stack );
    bool covering = false;
    if ( nz && !pend )
      covering = write_appliers( o, s, b );
    else
      o += "[]";
    fmt::format_to( out_it( o ), ",\"cov\":{}", covering ? "true" : "false" );
    if ( pend )
      o += ",\"pend\":true";
    o += "}\n";
  } );
}
}  // namespace

void note_read( const buff_t* b, int stack, double value )
{
  if ( !g_tap_open )
  {
    frame_read( b, stack, value );
    return;
  }
  // A buff read as zero cannot be a candidate (only a read that returned something is a dependency).
  if ( stack == 0 && value == 0.0 )
    return;
  for ( const tap_read_t& r : g_tap_reads )
    if ( r.b == b )
      return;
  g_tap_reads.push_back( { b, stack, value } );
}

// Plan 12: a buff whose source player (the engine's rl_buff_source_player rule) is the RL actor or one of its pets: the buffs the
// counters of the fixes below are about.
static bool buff_of_actor( const state_t* st, const buff_t* b )
{
  const player_t* src = ( b->source != nullptr && !b->source->is_enemy() ) ? b->source : b->player;
  return st != nullptr && st->in_fight && belongs_to_actor( *st, src );
}

// Plan 12 (MN-04): the engine's own bookkeeping reads (buff_t::check_own / remains_own / at_max_stacks_own) are not reads of the fight. Inside a
// reference pass (the tap is open) nothing changes: it is a read like any other. Otherwise it is not written as a frame read; it
// is counted (own_reads_skipped) when a frame read would have been written (a frame is open, outside the ledger's own code).
void note_own_read( const buff_t* b )
{
  if ( g_tap_open )
  {
    note_read( b, b->current_stack, b->current_value );
    return;
  }
  if ( b->sim == nullptr || b->is_fallback )
    return;
  state_t* s = state_of( b->sim );
  if ( s == nullptr || !s->in_fight || s->frames.empty() || b->sim->rl_bl_shadow || g_busy > 0 )
    return;
  ++s->own_reads_skipped;
}

// Plan 12 (MN-02): a trigger was merged into a pending aura-delay event of the same duration, which keeps the FIRST trigger's cause
// (the merged stacks are attributed to it). Counted when the merging trigger's cause is another one (no replay: see the deferred items).
void note_delay_merge( buff_t* b, const rl_cause_t& first, const rl_cause_t& merging )
{
  if ( same_cause( first, merging ) )
    return;
  state_t* s = state_of( b->sim );
  if ( !buff_of_actor( s, b ) )
    return;
  ++s->delay_merge_mixed_cause;
  ++s->delay_merge_mixed_cause_by_buff[ b->name_str ];
}

namespace
{
// Plan 12: true while the extension probe's own extend call runs (the `ext` record then says probe:true).
bool g_ext_probing = false;
void run_ext_probe( state_t* s, player_t* p, const rl_cause_t& cause );
}  // namespace

std::int32_t frame_push( player_t* p, const rl_cause_t& cause, const action_t* owner, const action_t* ctx,
                         const char* kind )
{
  sim_t* sim = p->sim;
  state_t* s = state_of( sim );
  if ( s == nullptr || !s->in_fight || sim->rl_bl_shadow || !belongs_to_actor( *s, p ) )
    return -1;

  state_t::frame_t f;
  f.id    = s->next_frame++;
  f.p     = p;
  f.owner = owner;
  f.act   = owner != nullptr ? owner : ctx;
  const std::int32_t parent = s->frames.empty() ? -1 : s->frames.back().id;
  ++s->frames_written;

  emit( s, REC_FRM, [ & ]( std::string& o ) {
    fmt::format_to( out_it( o ), "{{\"k\":\"frm\",\"it\":{},\"t\":", sim->current_iteration );
    put_double( o, sim->current_time().total_seconds() );
    fmt::format_to( out_it( o ), ",\"f\":{},\"pf\":{},\"kind\":", f.id, parent );
    put_string( o, kind != nullptr ? std::string_view( kind ) : ( owner != nullptr ? "dispatch" : "scope" ) );
    fmt::format_to( out_it( o ), ",\"seq\":{},\"cls\":{},\"dk\":{},\"press\":{},\"launch\":{},\"action\":", cause.seq,
                    cls_out( cause.cls ), dk_out( cause.cls ), cause.press, cause.launch );
    put_string( o, f.act != nullptr ? std::string( f.act->name() ) : std::string() );
    o += "}\n";
  } );

  const std::int32_t id = f.id;
  s->frames.push_back( std::move( f ) );
  g_frame_depth = s->frames.size();
  update_gate();
  // Plan 12: the stage-0-only extension probe acts from inside a press frame (never set in an identity proof).
  if ( sim->rl_buff_ledger_ext_probe )
    run_ext_probe( s, p, cause );
  return id;
}

void frame_pop( player_t* p, std::int32_t frame )
{
  state_t* s = state_of( p->sim );
  if ( s == nullptr )
    return;
  for ( std::size_t i = s->frames.size(); i-- > 0; )
  {
    if ( s->frames[ i ].id != frame )
      continue;
    if ( i + 1 != s->frames.size() )
      ++s->frame_pop_mismatch;
    s->frames.erase( s->frames.begin() + static_cast<std::ptrdiff_t>( i ), s->frames.end() );
    g_frame_depth = s->frames.size();
    update_gate();
    return;
  }
  ++s->frame_pop_mismatch;
}

std::int32_t innermost_frame( sim_t* sim )
{
  state_t* s = state_of( sim );
  return ( s == nullptr || s->frames.empty() ) ? -1 : s->frames.back().id;
}

// ---- Plan 06: applier hooks (called from buff.cpp, only when sim->rl_bl_on) ---------------------------------------

void applier_pre_bump( buff_t* b )
{
  if ( covering_mode( b ) )
    return;
  reconcile_appliers( state_of( b->sim ), b );
}

// A single-stack buff: one entry per application (start / refresh), with its own expiry, added by the bump hook before
// any callback of the bump can read the buff. Entries that can no longer cover go first (so the list stays short); a
// second application with the very same cause only extends the first one's expiry.
static void add_covering( buff_t* b, const rl_cause_t& cause, timespan_t expiry )
{
  const timespan_t now = b->sim->current_time();
  auto& v              = b->rl_bl_appliers;
  v.erase( std::remove_if( v.begin(), v.end(),
                           [ & ]( const buff_t::rl_bl_applier_t& e ) { return !( e.expiry > now ); } ),
           v.end() );
  for ( buff_t::rl_bl_applier_t& e : v )
  {
    if ( same_cause( e.cause, cause ) )
    {
      if ( expiry > e.expiry )
        e.expiry = expiry;
      return;
    }
  }
  v.push_back( { cause, 1, expiry } );
}

// Plan 12 (MN-03): the stacks an overflow removes from an asynchronous buff are the ones with the earliest own expiry (the engine
// cancels the earliest-expiring expiration events, the key applier_expire_own already uses), not the oldest inserted. Entries of
// equal expiry go oldest first. Counts a trim where the two rules would have picked different entries (async_trim_order_differs).
static void trim_by_expiry( state_t* st, buff_t* b, int excess )
{
  auto& v = b->rl_bl_appliers;
  auto key = [ & ]( const buff_t::rl_bl_applier_t& e ) {
    return e.expiry == timespan_t::min() ? timespan_t::max() : e.expiry;
  };
  bool first = true;
  while ( excess > 0 && !v.empty() )
  {
    std::size_t best = 0;
    for ( std::size_t i = 1; i < v.size(); ++i )
      if ( key( v[ i ] ) < key( v[ best ] ) )
        best = i;
    if ( first && best != 0 && buff_of_actor( st, b ) )
      ++st->async_trim_order_differs;
    first = false;
    const int take = std::min( excess, v[ best ].stacks );
    v[ best ].stacks -= take;
    excess -= take;
    if ( v[ best ].stacks <= 0 )
      v.erase( v.begin() + static_cast<std::ptrdiff_t>( best ) );
  }
}

void applier_post_bump( buff_t* b, int requested, int old_stack, const rl_cause_t& cause )
{
  if ( covering_mode( b ) )
  {
    if ( b->rl_bl_applying && b->current_stack > 0 )
    {
      // Plan 12 (MJ-06): a refresh that moved nothing (refresh behaviour disabled) is handed "no new coverage" by buff.cpp.
      if ( b->rl_bl_next_expiry == timespan_t::min() )
      {
        if ( state_t* st = state_of( b->sim ); buff_of_actor( st, b ) )
        {
          ++st->refresh_no_cover;
          ++st->refresh_no_cover_by_buff[ b->name_str ];
        }
        return;
      }
      add_covering( b, cause, b->rl_bl_next_expiry );
    }
    return;
  }
  state_t* st = state_of( b->sim );
  int added;
  if ( b->max_stack() < 0 )
    added = requested;
  else if ( b->stack_behavior == buff_stack_behavior::ASYNCHRONOUS )
    added = std::min( requested, b->max_stack() );
  else
    added = b->current_stack - old_stack;
  if ( added > 0 )
  {
    // Plan 12 (MN-03): an overflow of an asynchronous buff cancelled the earliest-expiring stacks before this application's own
    // expiration event exists: remove those entries (never the one added now) by earliest expiry, then add the new one.
    if ( b->max_stack() > 0 && b->stack_behavior == buff_stack_behavior::ASYNCHRONOUS )
    {
      const int excess = applier_total( b ) + added - ( b->current_stack < 0 ? 0 : b->current_stack );
      if ( excess > 0 )
        trim_by_expiry( st, b, excess );
    }
    b->rl_bl_appliers.push_back( { cause, added, b->rl_bl_next_expiry } );
  }
  reconcile_appliers( st, b );
}

// Plan 13 (review MJ-12-01): a synchronous multi-stack buff has ONE shared end. buff_t::refresh moved it (it is not the end it had before, or the
// buff has none now), so from this moment an earlier extension added nothing: the stacks would be up in [old end, new end) anyway. Every open
// extension window {cause, from, to} of the buff's applier entries is clipped to end at this moment (to = min(to, now)); a window with nothing left
// (from >= to: it had not begun) is dropped. A refresh that leaves the end where it was (refresh behaviour disabled, or the same end) clips nothing.
void applier_end_moved( buff_t* b, timespan_t old_end )
{
  if ( b->stack_behavior == buff_stack_behavior::ASYNCHRONOUS || covering_mode( b ) )
    return;
  const timespan_t new_end = b->expiration.empty() ? timespan_t::max() : b->expiration.front()->occurs();
  if ( new_end == old_end )
    return;
  state_t* st          = state_of( b->sim );
  const timespan_t now = b->sim->current_time();
  const bool counted   = st != nullptr && buff_of_actor( st, b );
  for ( buff_t::rl_bl_applier_t& e : b->rl_bl_appliers )
  {
    for ( auto it = e.exts.begin(); it != e.exts.end(); )
    {
      if ( it->to > now )
      {
        it->to = now;
        if ( !( it->from < it->to ) )
        {
          if ( counted )
            ++st->ext_window_dropped;
          it = e.exts.erase( it );
          continue;
        }
        if ( counted )
          ++st->ext_window_clipped;
      }
      ++it;
    }
  }
}

void applier_expire_own( buff_t* b, int stacks )
{
  if ( covering_mode( b ) )
    return;
  reconcile_appliers( state_of( b->sim ), b );
  auto& v = b->rl_bl_appliers;
  int left = stacks;
  while ( left > 0 && !v.empty() )
  {
    std::size_t best = 0;
    auto key         = [ & ]( const buff_t::rl_bl_applier_t& e ) {
      return e.expiry == timespan_t::min() ? timespan_t::max() : e.expiry;
    };
    for ( std::size_t i = 1; i < v.size(); ++i )
      if ( key( v[ i ] ) < key( v[ best ] ) )
        best = i;
    const int take = std::min( left, v[ best ].stacks );
    v[ best ].stacks -= take;
    left -= take;
    if ( v[ best ].stacks <= 0 )
      v.erase( v.begin() + static_cast<std::ptrdiff_t>( best ) );
  }
}

// ---- Plan 12: extensions (PREREG Amendment 1 item 3; ruling R1 of 2026-10-02) -------------------------------------
//
// An event that moves a buff's end later is an application by the cause that made it. Single-stack buff: the extender
// is a covering applier from now until the new end, exactly as a refresh (add_covering). Multi-stack buff: the stacks
// whose end moved (all of a synchronous buff; for an asynchronous buff the entries whose own expiry equals a moved
// event's old end) get an extension {cause, from = old end, to = new end}: they belong to the extender only for the
// time the extension added; before the old end they stay with the presses that added them.
void applier_extend( buff_t* b, const rl_cause_t& cause, player_t* source, timespan_t extra, const timespan_t* old_ends,
                     const timespan_t* new_ends, std::size_t n_exp, const char* fn )
{
  state_t* st = state_of( b->sim );
  if ( st == nullptr || n_exp == 0 )
    return;
  const timespan_t now = b->sim->current_time();
  bool changed         = false;
  std::size_t unpaired = 0;  // plan 13 (MN-12-02): events with no entry plus entries with no event (asynchronous branch)

  if ( covering_mode( b ) )
  {
    add_covering( b, cause, new_ends[ 0 ] );
    changed = true;  // the extender is an entry of the covering list from here on
  }
  else
  {
    reconcile_appliers( st, b );
    auto& v = b->rl_bl_appliers;
    // Extension windows that have passed can never be the answer again (time only moves forward).
    for ( buff_t::rl_bl_applier_t& e : v )
      e.exts.erase( std::remove_if( e.exts.begin(), e.exts.end(),
                                    [ & ]( const buff_t::rl_bl_ext_t& x ) { return !( x.to > now ); } ),
                    e.exts.end() );
    if ( b->stack_behavior != buff_stack_behavior::ASYNCHRONOUS )
    {
      // One expiration for all stacks: every entry's stacks moved.
      for ( buff_t::rl_bl_applier_t& e : v )
      {
        e.exts.push_back( { cause, old_ends[ 0 ], new_ends[ 0 ] } );
        changed = true;
      }
    }
    else
    {
      // Independent stacks: each has its own expiration. Plan 13 (review MN-12-02), three steps, stack-weighted (an entry of k stacks holds
      // k expiration events with its expiry):
      //   1. exact: an entry takes the moved events whose old end equals its own expiry (at most its stacks), matched first and
      //      applied after, so an entry whose new expiry equals another event's old end is not matched twice;
      //   2. by order: the entries left over and the events left over are paired earliest with earliest (an entry takes up to its stacks);
      //      pairs made when step 1 found nothing at all count in ext_matched_by_order (plan 12), pairs made after some exact match count
      //      in ext_partial_by_order;
      //   3. what is still unpaired (an event with no entry, an entry with no event) counts in ext_unattributed.
      std::vector<std::pair<std::size_t, std::size_t>> pairs;  // (entry, first event taken)
      std::vector<char> event_used( n_exp, 0 );
      std::vector<char> entry_used( v.size(), 0 );
      for ( std::size_t i = 0; i < v.size(); ++i )
      {
        int room                = v[ i ].stacks > 0 ? v[ i ].stacks : 1;
        std::size_t first_event = n_exp;
        for ( std::size_t j = 0; j < n_exp && room > 0; ++j )
          if ( !event_used[ j ] && v[ i ].expiry == old_ends[ j ] )
          {
            event_used[ j ] = 1;
            --room;
            if ( first_event == n_exp )
              first_event = j;
          }
        if ( first_event != n_exp )
        {
          entry_used[ i ] = 1;
          pairs.emplace_back( i, first_event );
        }
      }
      const bool any_exact = !pairs.empty();
      std::vector<std::size_t> order;
      for ( std::size_t i = 0; i < v.size(); ++i )
        if ( !entry_used[ i ] )
          order.push_back( i );
      auto key = [ & ]( std::size_t i ) { return v[ i ].expiry == timespan_t::min() ? timespan_t::max() : v[ i ].expiry; };
      std::stable_sort( order.begin(), order.end(), [ & ]( std::size_t a, std::size_t c ) { return key( a ) < key( c ); } );
      std::vector<std::size_t> events;
      for ( std::size_t j = 0; j < n_exp; ++j )
        if ( !event_used[ j ] )
          events.push_back( j );
      std::stable_sort( events.begin(), events.end(), [ & ]( std::size_t a, std::size_t c ) { return old_ends[ a ] < old_ends[ c ]; } );
      std::size_t next_event = 0;
      std::size_t by_order   = 0;
      for ( std::size_t k = 0; k < order.size() && next_event < events.size(); ++k )
      {
        int room                = v[ order[ k ] ].stacks > 0 ? v[ order[ k ] ].stacks : 1;
        const std::size_t first = events[ next_event ];
        while ( room > 0 && next_event < events.size() )
        {
          event_used[ events[ next_event ] ] = 1;
          ++next_event;
          --room;
        }
        entry_used[ order[ k ] ] = 1;
        pairs.emplace_back( order[ k ], first );
        ++by_order;
      }
      if ( by_order > 0 )
      {
        if ( any_exact )
          st->ext_partial_by_order += by_order;
        else
          ++st->ext_matched_by_order;
      }
      for ( const auto& pr : pairs )
      {
        buff_t::rl_bl_applier_t& e = v[ pr.first ];
        e.exts.push_back( { cause, old_ends[ pr.second ], new_ends[ pr.second ] } );
        e.expiry = new_ends[ pr.second ];
        changed  = true;
      }
      for ( std::size_t j = 0; j < n_exp; ++j )
        if ( !event_used[ j ] )
          ++unpaired;
      for ( std::size_t i = 0; i < v.size(); ++i )
        if ( !entry_used[ i ] )
          ++unpaired;
    }
  }

  // Counters and the record: buffs whose source player belongs to the RL actor, in a fight after reset_done().
  if ( !st->in_fight || !st->cd_live || source == nullptr || !belongs_to_actor( *st, source ) )
    return;
  ++st->ext_seen;
  if ( !changed )
    ++st->ext_unattributed;
  else
    st->ext_unattributed += unpaired;
  const std::int32_t f = innermost_frame( b->sim );
  if ( f >= 0 )
    ++st->ext_in_frame;
  ++st->ext_written;
  if ( g_ext_probing )
    ++st->ext_probe_fired;
  auto& by = st->ext_by_buff[ b->name_str ];
  ++by.first;
  by.second += extra.total_seconds();

  emit( st, REC_EXT, [ & ]( std::string& o ) {
    fmt::format_to( out_it( o ), "{{\"k\":\"ext\",\"it\":{},\"t\":", b->sim->current_iteration );
    put_double( o, now.total_seconds() );
    fmt::format_to( out_it( o ), ",\"f\":{},\"buff\":", f );
    put_string( o, b->name_str );
    o += ",\"owner\":";
    put_string( o, b->player != nullptr ? std::string( b->player->name() ) : std::string() );
    o += ",\"src\":";
    put_string( o, std::string( source->name() ) );
    o += ",\"sec\":";
    put_double( o, extra.total_seconds() );
    o += ",\"old_end\":";
    put_double( o, old_ends[ 0 ].total_seconds() );
    o += ",\"end\":";
    put_double( o, new_ends[ 0 ].total_seconds() );
    fmt::format_to( out_it( o ), ",\"n_exp\":{},\"stacks\":{},\"cov\":{},\"press\":{},\"cls\":{},\"dk\":{},\"seq\":{},\"launch\":{},\"fn\":",
                    n_exp, b->current_stack, covering_mode( b ) ? "true" : "false", cause.press, cls_out( cause.cls ),
                    dk_out( cause.cls ), cause.seq, cause.launch );
    put_string( o, fn );
    if ( g_ext_probing )
      o += ",\"probe\":true";
    o += "}\n";
  } );
}

namespace
{
// The extension probe (stage-0 only; it changes the fight, so it is never set in an identity proof or the census). Called
// from frame_push, so the press frame is the innermost frame and the cause stack's top names this press. Once per fight
// and per family it extends, by 2 s, a buff the actor holds and that ends within the fight: a single-stack buff with one
// pending expiration whose covering list has no entry of this press (the earliest remaining first, ties by name), and a
// multi-stack buff (extend_duration when its stacks share one expiration, extend_async_duration when each has its own).
void run_ext_probe( state_t* s, player_t* p, const rl_cause_t& cause )
{
  if ( s->ext_probe_single_done && s->ext_probe_multi_done && s->ext_probe_sync_stage == 3 )
    return;
  if ( !s->in_fight || !s->cd_live || p != s->actor || rl_cause_base( cause.cls ) != RL_CAUSE_CAST || cause.press < 0 ||
       p->sim->rl_bl_shadow )
    return;
  busy_scope_t busy;  // the probe's own reads are no reads of the fight
  const timespan_t two  = timespan_t::from_seconds( 2.0 );
  const timespan_t late = timespan_t::from_seconds( 1.5 );
  const timespan_t now  = p->sim->current_time();

  // Plan 13 (MJ-12-02 c): the refresh that follows the second late extension of a synchronous multi-stack buff, at the next press frame while
  // the extension's window is still ahead. It is an application by this press that moves the shared end (the engine clips the window);
  // the probe writes `exr` as the independent evidence the checker clips the window by.
  const bool refresh_inside = ( p->sim->current_iteration & 1 ) == 0;
  if ( s->ext_probe_sync_stage == 2 && !( refresh_inside && now < s->ext_probe_sync_from ) )
  {
    buff_t* sb              = s->ext_probe_sync_buff;
    s->ext_probe_sync_stage = 3;
    s->ext_probe_sync_buff  = nullptr;
    if ( sb != nullptr && sb->current_stack > 0 && !sb->expiration.empty() && now < s->ext_probe_sync_end )
    {
      const timespan_t end_before = sb->expiration.front()->occurs();
      sb->execute( 1 );
      emit( s, REC_EXR, [ & ]( std::string& o ) {
        fmt::format_to( out_it( o ), "{{\"k\":\"exr\",\"it\":{},\"t\":", sb->sim->current_iteration );
        put_double( o, now.total_seconds() );
        fmt::format_to( out_it( o ), ",\"f\":{},\"buff\":", innermost_frame( sb->sim ) );
        put_string( o, sb->name_str );
        o += ",\"owner\":";
        put_string( o, sb->player != nullptr ? std::string( sb->player->name() ) : std::string() );
        o += ",\"end_before\":";
        put_double( o, end_before.total_seconds() );
        o += ",\"end\":";
        put_double( o, ( sb->expiration.empty() ? timespan_t::max() : sb->expiration.front()->occurs() ).total_seconds() );
        fmt::format_to( out_it( o ), ",\"stacks\":{},\"press\":{},\"cls\":{},\"dk\":{},\"seq\":{},\"launch\":{}}}\n", sb->current_stack,
                        cause.press, cls_out( cause.cls ), dk_out( cause.cls ), cause.seq, cause.launch );
      } );
      ++s->ext_probe_refreshed;
    }
  }

  const bool want_late = s->ext_probe_sync_stage == 0 || ( s->ext_probe_sync_stage == 1 && now >= s->ext_probe_sync_end );
  if ( s->ext_probe_single_done && s->ext_probe_multi_done && !want_late )
    return;
  buff_t* single = nullptr;
  buff_t* multi  = nullptr;
  buff_t* ending = nullptr;  // a synchronous multi-stack buff within 1.5 s of its end (stage 0), or within 6 s with a refresh that moves the end (stage 1)
  auto better    = [ & ]( const buff_t* a, const buff_t* c ) {
    if ( c == nullptr )
      return true;
    const timespan_t ra = a->expiration.front()->remains();
    const timespan_t rc = c->expiration.front()->remains();
    return ra < rc || ( ra == rc && a->name_str < c->name_str );
  };
  for ( buff_t* b : p->buff_list )
  {
    if ( b == nullptr || b->is_fallback || b->current_stack <= 0 || b->expiration.empty() )
      continue;
    if ( b->player != p || ( b->source != nullptr && b->source != p ) )
      continue;
    if ( b->max_stack() == 1 )
    {
      if ( s->ext_probe_single_done || b->stack_behavior == buff_stack_behavior::ASYNCHRONOUS || b->expiration.size() != 1 )
        continue;
      bool has_press = false;
      reconcile_appliers( s, b );
      for ( const buff_t::rl_bl_applier_t& e : b->rl_bl_appliers )
        if ( e.cause.press == cause.press )
          has_press = true;
      if ( !has_press && better( b, single ) )
        single = b;
    }
    else if ( b->max_stack() > 1 )
    {
      if ( !s->ext_probe_multi_done && ( b->stack_behavior == buff_stack_behavior::ASYNCHRONOUS || b->expiration.size() == 1 ) &&
           better( b, multi ) )
        multi = b;
      // Stage 0: any synchronous multi-stack buff within 1.5 s of its end. Stage 1: only a buff whose refresh moves the end (refresh behaviour
      // not disabled) can show a window clipped by a refresh, within 6 s of its end.
      if ( want_late && b->stack_behavior != buff_stack_behavior::ASYNCHRONOUS && b->expiration.size() == 1 )
      {
        const bool second    = s->ext_probe_sync_stage == 1;
        const timespan_t rem = b->expiration.front()->remains();
        if ( ( !second || b->refresh_behavior != buff_refresh_behavior::DISABLED ) && rem > timespan_t::zero() &&
             rem <= ( second ? timespan_t::from_seconds( 6.0 ) : late ) && better( b, ending ) )
          ending = b;
      }
    }
  }
  if ( ending == multi )
    ending = nullptr;  // never both in one call: the late extension waits for the next press frame
  g_ext_probing = true;
  if ( single != nullptr )
  {
    s->ext_probe_single_done = true;
    single->extend_duration( two );
  }
  if ( multi != nullptr )
  {
    s->ext_probe_multi_done = true;
    if ( multi->stack_behavior == buff_stack_behavior::ASYNCHRONOUS )
      multi->extend_async_duration( two );
    else
      multi->extend_duration( two );
  }
  if ( ending != nullptr )
  {
    // Shortly before its end, so the window (old end to new end) is reached while moved stacks are still up. The first one is left alone
    // (stage 1); once its window has passed the second is followed by a refresh (stage 2, executed at the next press frame).
    const bool refreshed_next = s->ext_probe_sync_stage == 1;
    const timespan_t old_end  = ending->expiration.front()->occurs();
    ending->extend_duration( two );
    ++s->ext_probe_sync_late;
    s->ext_probe_sync_from = old_end;
    s->ext_probe_sync_end  = ending->expiration.empty() ? now : ending->expiration.front()->occurs();
    if ( refreshed_next )
    {
      s->ext_probe_sync_stage = 2;
      s->ext_probe_sync_buff  = ending;
    }
    else
      s->ext_probe_sync_stage = 1;
  }
  g_ext_probing = false;
}
}  // namespace

void applier_clear( buff_t* b )
{
  b->rl_bl_appliers.clear();
}

void note_consume( buff_t* b, const char* op, int removed )
{
  if ( b->sim == nullptr )
    return;
  state_t* s = state_of( b->sim );
  if ( s == nullptr || !s->in_fight || s->frames.empty() || b->sim->rl_bl_shadow || g_busy > 0 )
    return;
  state_t::frame_t& f = s->frames.back();
  f.last_dr           = false;
  ++s->consumes_written;
  const int after = std::strcmp( op, "expire" ) == 0 ? 0 : b->current_stack;

  const std::int64_t order = s->next_order++;
  emit( s, REC_CS, [ & ]( std::string& o ) {
    fmt::format_to( out_it( o ), "{{\"k\":\"cs\",\"it\":{},\"t\":", b->sim->current_iteration );
    put_double( o, b->sim->current_time().total_seconds() );
    fmt::format_to( out_it( o ), ",\"f\":{},\"o\":{},\"buff\":", f.id, order );
    put_string( o, b->name_str );
    o += ",\"owner\":";
    put_string( o, b->player != nullptr ? std::string( b->player->name() ) : std::string() );
    o += ",\"op\":";
    put_string( o, op );
    fmt::format_to( out_it( o ), ",\"stacks\":{},\"rm\":{}}}\n", after, removed );
  } );
}

void note_dot_read( const dot_t* d, bool non_zero )
{
  if ( d->source == nullptr )
    return;
  sim_t* sim = d->source->sim;
  state_t* s = state_of( sim );
  if ( s == nullptr || !s->in_fight )
    return;
  const bool pass = g_tap_open;
  if ( !pass && ( s->frames.empty() || sim->rl_bl_shadow || g_busy > 0 ) )
    return;
  std::array<std::uint64_t, 4>& c = s->dot_reads[ d ];
  ++c[ ( pass ? 2 : 0 ) + ( non_zero ? 0 : 1 ) ];
}

void switch_register( const dbc_proc_callback_t* cb, buff_t* buff, bool on )
{
  if ( cb == nullptr || cb->listener == nullptr )
    return;
  state_t* s = state_of( cb->listener->sim );
  if ( s == nullptr )
    return;
  auto& v = s->switches[ cb ];
  if ( on )
  {
    if ( std::find( v.begin(), v.end(), buff ) == v.end() )
      v.push_back( buff );
  }
  else
  {
    v.erase( std::remove( v.begin(), v.end(), buff ), v.end() );
  }
}

void switch_enter( const dbc_proc_callback_t* cb )
{
  buff_t* sw = nullptr;
  if ( cb != nullptr && cb->listener != nullptr )
  {
    state_t* s = state_of( cb->listener->sim );
    if ( s != nullptr )
    {
      auto it = s->switches.find( cb );
      if ( it != s->switches.end() && !it->second.empty() )
      {
        // Plan 12 (MN-06 part 1): only a registered switch buff that is up can be the switch; with none up there is no switch launch
        // (it used to name the first registered buff, which was down), counted in switch_buff_down.
        for ( buff_t* c : it->second )
          if ( c->current_stack > 0 )
          {
            sw = c;
            break;
          }
        if ( sw == nullptr )
          ++s->switch_buff_down;
      }
    }
  }
  g_switch_stack.push_back( sw );
}

void switch_leave()
{
  if ( !g_switch_stack.empty() )
    g_switch_stack.pop_back();
}

std::int32_t note_launch( action_t* child, player_t* child_target, const char* kind )
{
  sim_t* sim = child->sim;
  state_t* s = state_of( sim );
  if ( s == nullptr || !s->in_fight || sim->rl_bl_shadow || g_busy > 0 )
    return -1;
  // The frame the child's own player is in: the topmost frame of that player (its cause stack's top).
  state_t::frame_t* parent = nullptr;
  for ( std::size_t i = s->frames.size(); i-- > 0; )
    if ( s->frames[ i ].p == child->player )
    {
      parent = &s->frames[ i ];
      break;
    }
  if ( parent == nullptr || parent->act == child )
    return -1;

  busy_scope_t busy;
  const std::int32_t l     = s->next_launch++;
  const std::int64_t order = s->next_order++;
  parent->last_dr          = false;

  const char* lk = kind;
  buff_t* sw     = nullptr;
  if ( !g_switch_stack.empty() && g_switch_stack.back() != nullptr )
  {
    sw                     = g_switch_stack.back();
    g_switch_stack.back()  = nullptr;  // the next launch only
    lk                     = "switch";
    ++s->switch_launches;
  }
  ++s->launches_by_kind[ lk ];

  if ( !s->text && sw != nullptr )
    reconcile_appliers( s, sw );  // the state effect write_appliers has
  emit( s, REC_LN, [ & ]( std::string& o ) {
    fmt::format_to( out_it( o ), "{{\"k\":\"ln\",\"it\":{},\"t\":", sim->current_iteration );
    put_double( o, sim->current_time().total_seconds() );
    fmt::format_to( out_it( o ), ",\"l\":{},\"frame\":{},\"order\":{},\"pa\":", l, parent->id, order );
    put_string( o, parent->act != nullptr ? std::string( parent->act->name() ) : std::string() );
    o += ",\"ca\":";
    put_string( o, child->name() );
    o += ",\"ct\":";
    put_string( o, child_target != nullptr ? std::string( child_target->name() ) : std::string() );
    o += ",\"lk\":";
    put_string( o, lk );
    o += ",\"sw\":";
    if ( sw != nullptr )
    {
      o += "{\"buff\":";
      put_string( o, sw->name_str );
      o += ",\"owner\":";
      put_string( o, sw->player != nullptr ? std::string( sw->player->name() ) : std::string() );
      fmt::format_to( out_it( o ), ",\"stacks\":{},\"app\":", sw->current_stack );
      const bool covering = write_appliers( o, s, sw );
      fmt::format_to( out_it( o ), ",\"cov\":{}}}", covering ? "true" : "false" );
    }
    else
      o += "null";
    o += ",\"sf\":null}\n";
  } );
  return l;
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
//     side-effecting player_t::invalidate_cache() is never called;
//   * plan 04: the roll recorder's hook pointer (rng::rl_draw_sink) is cleared for the whole scope and
//     restored, so no draw made inside a pass can ever be written to a roll recording (every accessor
//     serves the ledger's scratch generator in there anyway, rl_buff_ledger::rng_access), and the
//     list of shadow guards that fired (state_t::cur_guards) starts empty.
class shadow_scope_t
{
public:
  shadow_scope_t( state_t* st, action_t* a, player_t* target )
    : st_( st ), sim_( a->sim ), dealer_( a->player ), owner_( nullptr ), target_( target )
  {
    // Plan 12 (MJ-04): nestable. The running pass set's guard list is moved aside (the inner scope starts empty) and the shadow
    // flags it found are put back by the destructor, so an inner scope never ends the outer scope's protection.
    saved_guards_      = std::move( st->cur_guards );
    st->cur_guards.clear();
    saved_prev_shadow_ = sim_->rl_bl_shadow;
    saved_prev_active_ = g_shadow_active;
    saved_sink_  = rng::rl_draw_sink;
    rng::rl_draw_sink = nullptr;
    saved_debug_ = sim_->debug;
    saved_log_   = sim_->log;
    sim_->debug  = false;
    sim_->log    = 0;
    sim_->rl_bl_shadow = true;
    g_shadow_active    = true;
    update_gate();
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
    rng::rl_draw_sink  = saved_sink_;
    sim_->rl_bl_shadow = saved_prev_shadow_;
    g_shadow_active    = saved_prev_active_;
    update_gate();
    sim_->debug        = saved_debug_;
    sim_->log          = saved_log_;
    // Plan 13 (review MN-12-08): the guards that fired inside this scope are appended to the restored outer list (names already in it are
    // not repeated), so a violation inside a nested scope still marks the outer pass set unsafe.
    std::vector<const char*> inner = std::move( st_->cur_guards );
    st_->cur_guards                = std::move( saved_guards_ );
    for ( const char* g : inner )
    {
      bool known = false;
      for ( const char* o : st_->cur_guards )
        if ( std::strcmp( o, g ) == 0 )
        {
          known = true;
          break;
        }
      if ( !known )
        st_->cur_guards.push_back( g );
    }
  }

private:
  state_t* st_;
  std::vector<const char*> saved_guards_;
  bool saved_prev_shadow_ = false;
  bool saved_prev_active_ = false;
  sim_t* sim_;
  player_t* dealer_;
  player_t* owner_;
  player_t* target_;
  action_t* action_ = nullptr;
  std::uint32_t saved_callback_state_ = 0;
  rng::rl_draw_sink_t* saved_sink_ = nullptr;
  bool saved_debug_ = false;
  int saved_log_    = 0;
  std::optional<player_stat_cache_t> saved_dealer_;
  std::optional<player_stat_cache_t> saved_owner_;
  std::optional<player_stat_cache_t> saved_target_;
};

// Removes one stat buff entry's current contribution from the player's stats, with the routing
// player_t::stat_gain / stat_loss use for the stats that live in `current.stats` (never through
// stat_gain / stat_loss themselves: for haste they reschedule swings and cooldowns). A stat those functions
// route to a resource, or ignore for this class, changes no amount and is left alone. The caller restores
// `current.stats` by assignment from its own copy, never by adding back.
void remove_stat_contribution( player_t* p, stat_e stat, double amount )
{
  if ( amount == 0.0 || p->convert_hybrid_stat( stat ) == STAT_NONE )
    return;
  switch ( stat )
  {
    case STAT_ALL:
      for ( attribute_e i = ATTRIBUTE_NONE; i < ATTRIBUTE_MAX; i++ )
        p->current.stats.attribute[ i ] -= amount;
      break;
    case STAT_HEALTH:
    case STAT_MANA:
    case STAT_RAGE:
    case STAT_ENERGY:
    case STAT_FOCUS:
    case STAT_RUNIC:
    case STAT_MAX_HEALTH:
    case STAT_MAX_MANA:
    case STAT_MAX_RAGE:
    case STAT_MAX_ENERGY:
    case STAT_MAX_FOCUS:
    case STAT_MAX_RUNIC:
    case STAT_RESILIENCE_RATING:
      break;
    default:
      p->current.stats.add_stat( stat, -amount );
      break;
  }
}

// Hiding a buff = what buff_t::expire leaves: stack 0 and value 0. Zeroing the fields (not a flag in
// the read functions) also catches damage_buff_t's inline readers, remains() and any engine loop.
// Plan 05: a stat buff additionally has its stat amounts removed from its player's current stats (copied
// first, restored by assignment). Everything is restored bit for bit by the destructor, in reverse order.
class hidden_buffs_t
{
public:
  void hide( buff_t* b )
  {
    saved_.push_back( { b, b->current_stack, b->current_value, nullptr, {} } );
    if ( auto* sb = dynamic_cast<stat_buff_t*>( b ) )
    {
      saved_.back().stat_owner = sb->player;
      saved_.back().stats      = sb->player->current.stats;
      for ( const stat_buff_t::buff_stat_t& bs : sb->stats )
        remove_stat_contribution( sb->player, bs.stat, bs.current_value );
    }
    b->current_stack = 0;
    b->current_value = 0.0;
  }
  ~hidden_buffs_t()
  {
    for ( auto it = saved_.rbegin(); it != saved_.rend(); ++it )
    {
      it->b->current_stack = it->stack;
      it->b->current_value = it->value;
      if ( it->stat_owner != nullptr )
        it->stat_owner->current.stats = *it->stats;
    }
  }

private:
  struct saved_t
  {
    buff_t* b;
    int stack;
    double value;
    player_t* stat_owner;
    std::optional<gear_stats_t> stats;
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
  // Plan 05: a pre-made state re-snapshotted per target in an AoE execute: the passes re-run only the
  // target part of the snapshot (snapshot_flags & STATE_TARGET with `target_rt`), as execute() did.
  bool target_only                 = false;
  result_amount_type target_rt     = result_amount_type::NONE;
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
      update_gate();
    }
  }
  ~tap_scope_t()
  {
    if ( on_ )
    {
      g_tap_open = false;
      update_gate();
    }
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
  if ( env.target_only )
    env.action->snapshot_internal( sc, env.action->snapshot_flags & STATE_TARGET, env.target_rt );
  else
    env.action->snapshot_state( sc, env.real->result_type );
  sc->result       = env.real->result;
  sc->block_result = env.real->block_result;

  amount_t out;
  out.pre = env.action->calculate_direct_amount( sc );
  out.cc  = sc->composite_crit_chance();
  out.cb  = env.action->total_crit_bonus( sc );
  return out;
}

// Plan 04, application pass: the engine's own snapshot of the applying state (as amount_pass), then the
// PERIODIC amount that snapshot would give: calculate_tick_amount( copy, 1.0 ) on a copy whose result is
// a plain hit (calculate_tick_amount applies the crit bonus itself, so the pre-crit amount needs a
// non-crit result). `pre` of the returned amount is that periodic amount. Direct damage is not
// computed here (the hit's own passes carry it). `viol_out`, when given, receives the snapshot
// mismatch against the real state (the same fields as the direct passes; there is no real periodic
// amount to compare against at application time).
amount_t app_pass( pass_env_t& env, bool log_reads, std::uint64_t* viol_out )
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
  if ( viol_out != nullptr )
    *viol_out = snapshot_diff( sc, env.real );
  sc->result = RESULT_HIT;

  amount_t out;
  out.pre = env.action->calculate_tick_amount( sc, 1.0 );
  out.cc  = sc->composite_crit_chance();
  out.cb  = env.action->total_crit_bonus( sc );
  return out;
}

// Plan 05, pre-made pass: a state class code (or a tick action's application) snapshotted itself. The
// engine's own virtual snapshot of a scratch copy (exactly the call the class made, on a state whose
// n_targets and chain_target are what the class's state had), then the pre-crit direct amount the
// execute of that state will compute: calculate_direct_amount on a plain hit with n_targets 1 and
// chain_target 0 (what a single-target execute sets; an AoE execute re-snapshots its own target part
// and carries its own entries, which name this one as their parent). A split_aoe_damage action would
// otherwise divide by the class state's n_targets of 0. `viol_out` receives the snapshot mismatch against
// the real state (there is no real amount at hand-over to compare with).
amount_t premade_pass( pass_env_t& env, bool log_reads, std::uint64_t* viol_out )
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
  if ( viol_out != nullptr )
    *viol_out = snapshot_diff( sc, env.real );
  sc->result       = RESULT_HIT;
  sc->block_result = BLOCK_RESULT_UNBLOCKED;
  sc->n_targets    = 1;
  sc->chain_target = 0;

  amount_t out;
  out.pre = env.action->calculate_direct_amount( sc );
  out.cc  = sc->composite_crit_chance();
  out.cb  = env.action->total_crit_bonus( sc );
  return out;
}

// Plan 04, tick pass: a scratch copy of the DoT's state, re-run through update_state (the tick-time
// update flags only, exactly the call action_t::tick makes first), then the pre-crit tick amount
// calculate_tick_amount( copy, tick_multiplier ) on a plain-hit copy. The real tick reproduces as
// pre x ( 1 + crit bonus ) for a crit tick and as pre for any other. `viol_out`, when given, receives
// the bits of the snapshot mismatch against the real state and VIOL_PRE_CRIT when that reproduction
// is not bit for bit.
amount_t tick_pass( pass_env_t& env, double tick_multiplier, bool log_reads, std::uint64_t* viol_out )
{
  env.scope->invalidate_caches();
  tap_scope_t tap( log_reads );
  action_state_t* sc = env.scratch;
  sc->copy_state( env.real );
  sc->action = env.action;
  env.action->update_state( sc, env.action->amount_type( sc, true ) );
  if ( viol_out != nullptr )
    *viol_out = snapshot_diff( sc, env.real );
  sc->result = RESULT_HIT;

  amount_t out;
  out.pre = env.action->calculate_tick_amount( sc, tick_multiplier );
  out.cc  = sc->composite_crit_chance();
  out.cb  = env.action->total_crit_bonus( sc );
  if ( viol_out != nullptr )
  {
    const double want = env.real->result == RESULT_CRIT ? out.pre * ( 1.0 + out.cb ) : out.pre;
    if ( !same_bits( want, env.real->result_amount ) )
      *viol_out |= VIOL_PRE_CRIT;
  }
  return out;
}

// Plan 06: press-applied = at least one entry of the buff's applier list (the covering ones for a single-stack
// buff) has class PROC_OF_CAST or PROC_OF_DOT and a press >= 0.
bool press_applied( state_t* st, const buff_t* cb )
{
  buff_t* b = const_cast<buff_t*>( cb );
  reconcile_appliers( st, b );
  const timespan_t now = b->sim->current_time();
  for ( const buff_t::rl_bl_applier_t& e : b->rl_bl_appliers )
    if ( is_press_class( effective_cause( e, now ) ) )
      return true;
  return false;
}

// Candidates: the buffs the REFERENCE pass read (non-zero stack or value) that were applied by a
// press: the dealer's own buffs, its owner's when the dealer is a pet, and debuffs on the hit's own
// target whose source is the dealer or its owner. First-read order, which is deterministic.
// Plan 05: a stat buff acts through the player's stats, not through a read the amount code makes, so it is
// a candidate of every pass set while it is up (and press-applied), on the dealer or on its owner.
const std::vector<stat_buff_t*>& stat_buffs_of( state_t* st, player_t* p )
{
  auto it = st->stat_buffs.find( p );
  if ( it == st->stat_buffs.end() )
  {
    std::vector<stat_buff_t*> v;
    for ( buff_t* b : p->buff_list )
      if ( auto* sb = dynamic_cast<stat_buff_t*>( b ) )
        v.push_back( sb );
    it = st->stat_buffs.emplace( p, std::move( v ) ).first;
  }
  return it->second;
}

void collect_candidates( state_t* st, player_t* dealer, player_t* owner, player_t* target, std::vector<buff_t*>& out )
{
  for ( const tap_read_t& r : g_tap_reads )
  {
    buff_t* b = const_cast<buff_t*>( r.b );
    if ( b->player == nullptr )
    {
      ++st->noncandidate_reads[ fmt::format( "{}|noplayer", b->name_str ) ];
      continue;
    }
    const bool own    = b->player == dealer || ( owner != nullptr && b->player == owner );
    const bool debuff = b->player == target && ( b->source == dealer || ( owner != nullptr && b->source == owner ) );
    if ( !own && !debuff )
    {
      ++st->noncandidate_reads[ fmt::format( "{}|foreign", b->name_str ) ];
      continue;
    }
    if ( !press_applied( st, b ) )
    {
      ++st->noncandidate_reads[ fmt::format( "{}|cls{}|press{}", b->name_str, cls_out( b->rl_bl_applied.cls ),
                                             b->rl_bl_applied.press >= 0 ? "pos" : "neg" ) ];
      continue;
    }
    out.push_back( b );
  }
  for ( player_t* p : { dealer, owner } )
  {
    if ( p == nullptr )
      continue;
    for ( stat_buff_t* sb : stat_buffs_of( st, p ) )
    {
      if ( sb->current_stack > 0 && press_applied( st, sb ) && std::find( out.begin(), out.end(), sb ) == out.end() )
        out.push_back( sb );
    }
  }
}

// `app_ref` non-null = an application pass: `pre` is the periodic amount and `per` its ratio to the
// reference pass's periodic amount (1 for the reference itself). Otherwise `per` is null.
void put_pass( std::string& b, const pass_rec_t& p, const double* app_ref = nullptr )
{
  if ( !b.empty() )
    b += ',';
  fmt::format_to( out_it( b ), "{{\"kind\":\"{}\",\"hid\":{},\"pre\":", p.kind, p.hid );
  put_double( b, p.amount.pre );
  b += ",\"cc\":";
  put_double( b, p.amount.cc );
  b += ",\"cb\":";
  put_double( b, p.amount.cb );
  b += ",\"per\":";
  if ( app_ref != nullptr && *app_ref != 0.0 )
    put_double( b, p.amount.pre / *app_ref );
  else
    b += "null";
  fmt::format_to( out_it( b ), ",\"viol\":{}}}", p.viol );
}

void put_candidate( std::string& b, state_t* st, std::size_t i, const buff_t* c, bool debuff )
{
  if ( !b.empty() )
    b += ',';
  fmt::format_to( out_it( b ), "{{\"i\":{},\"name\":", i );
  put_string( b, c->name_str );
  b += ",\"owner\":";
  put_string( b, c->player->name() );
  const bool is_stat = dynamic_cast<const stat_buff_t*>( c ) != nullptr;
  // The members, not check() / check_value(): the ledger's own writing is not a read of the fight.
  fmt::format_to( out_it( b ), ",\"kind\":\"{}\",\"stacks\":{},\"value\":", debuff ? "debuff" : ( is_stat ? "stat" : "buff" ),
                  c->current_stack );
  put_double( b, c->current_value );
  b += ",\"app\":";
  const bool covering = write_appliers( b, st, const_cast<buff_t*>( c ) );
  b += covering ? ",\"cov\":true}" : ",\"cov\":false}";
}

// What one pass set produced: the passes in record order, the candidates (bit i of a pass's hidden
// mask = cands[ i ]) and the status code (0 no candidates, 1 split recorded, 3 reference mismatch,
// 4 restoring mismatch; 2 is set by the caller from the guards that fired).
struct split_t
{
  std::vector<pass_rec_t> passes;
  std::vector<buff_t*> cands;
  int status   = 0;
  bool cached  = false;  // plan 07: the passes were taken from the cache, not run
};

// Plan 06: a reference pass that gave exactly 0 writes a `zr` record: the action, the frame it ran in and the buffs
// the pass read non-zero (input of the detector for amounts kept in class-private storage: a hit that deals
// nothing although buffs it reads are up). The hit's own record still carries the one reference pass entry.
void write_zero_record( state_t* st, action_t* a, player_t* target )
{
  ++st->zero_records;
  sim_t* sim     = a->sim;
  emit( st, REC_ZR, [ & ]( std::string& o ) {
    fmt::format_to( out_it( o ), "{{\"k\":\"zr\",\"it\":{},\"t\":", sim->current_iteration );
    put_double( o, sim->current_time().total_seconds() );
    o += ",\"action\":";
    put_string( o, a->name() );
    o += ",\"target\":";
    put_string( o, target != nullptr ? std::string( target->name() ) : std::string() );
    fmt::format_to( out_it( o ), ",\"f\":{},\"buffs\":[", cur_frame( st ) );
    bool first = true;
    for ( const tap_read_t& r : g_tap_reads )
    {
      if ( !first )
        o += ',';
      first = false;
      o += "{\"buff\":";
      put_string( o, r.b->name_str );
      o += ",\"owner\":";
      put_string( o, r.b->player != nullptr ? std::string( r.b->player->name() ) : std::string() );
      fmt::format_to( out_it( o ), ",\"stacks\":{}}}", r.stack );
    }
    o += "]}\n";
  } );
}

// One pass of whatever kind the caller runs: nothing hidden unless the caller hid something first.
// The first argument asks for the read tap (reference pass only); the second, when non-null, receives
// the pass's violation bits (reference and restoring passes).
using pass_fn_t = std::function<amount_t( bool, std::uint64_t* )>;

// The guard probe (stage-0 only, rl_buff_ledger_guard_probe=1): inside a pass set, once per fight, the
// first time a pass set has a candidate, plant one deliberate violation of every guard. Each call must be
// blocked (the fight must not change) and counted under its own guard name.
action_state_t* scratch_for( state_t* st, action_t* a );

void fire_guard_probe( state_t* st, action_t* a, player_t* dealer, player_t* target, buff_t* cand )
{
  cand->expire();
  dealer->resource_gain( dealer->primary_resource(), 1.0 );
  a->cooldown->adjust( timespan_t::from_seconds( -1.0 ) );
  dealer->stat_gain( dealer->convert_hybrid_stat( STAT_STR_AGI_INT ), 1.0 );
  (void) a->rng().real();
  (void) cand->rng().real();

  // Plan 12 (MJ-04): the families the review found unguarded, called through the BASE methods so no class override runs first
  // (a class override acting before the base guard is a known gap, see the deferred-items todo).
  action_state_t* scr = scratch_for( st, a );
  a->action_t::execute();
  a->action_t::schedule_execute( nullptr );
  a->action_t::trigger_dot( scr );
  {
    // An event made inside a pass: its memory comes from the ledger, it is never queued; rescheduling and cancelling it are
    // counted no-ops too.
    event_t* ev = make_event( *a->sim, timespan_t::from_seconds( 1.0 ), [] {} );
    ev->reschedule( timespan_t::from_seconds( 2.0 ) );
    event_t::cancel( ev );
  }
  rl_count_proc( dealer, rl_proc::id::crit_hit, 0.5, true );
  // A pass started inside the pass must be refused (counted under nested_pass_refused_by_entry["run_passes"]).
  run_passes( a, scr );
  // A scope opened and closed inside the pass must leave the outer scope's protection exactly as it was.
  {
    const std::vector<const char*> guards_before = st->cur_guards;
    const bool shadow_before = a->sim->rl_bl_shadow;
    const bool active_before = g_shadow_active;
    const bool debug_before  = a->sim->debug;
    const int log_before     = a->sim->log;
    // Plan 13 (review MN-12-08): one guard is taken out of the outer list; a violation of it inside the inner scope must come back into
    // the outer list when the inner scope is destroyed (the outer pass set is then marked unsafe by it).
    const char* const planted = "buff.expire";
    std::vector<const char*> without;
    for ( const char* g : guards_before )
      if ( std::strcmp( g, planted ) != 0 )
        without.push_back( g );
    st->cur_guards = without;
    {
      shadow_scope_t inner( st, a, target );
      cand->expire();
    }
    const bool came_back = std::any_of( st->cur_guards.begin(), st->cur_guards.end(),
                                        [ & ]( const char* g ) { return std::strcmp( g, planted ) == 0; } );
    st->cur_guards = guards_before;
    const bool intact = shadow_before && active_before && a->sim->rl_bl_shadow == shadow_before &&
                        g_shadow_active == active_before && a->sim->debug == debug_before &&
                        a->sim->log == log_before && came_back;
    if ( intact )
      ++st->probe_nesting_ok;
    else
      ++st->probe_nesting_failed;
  }
}

// The pass sequence shared by direct hits, application passes and ticks: a reference pass (nothing
// hidden), one pass per candidate hidden alone, every hidden subset for 2 or 3 effective candidates
// (the all-effective pass above 3), and a restoring pass. A candidate is EFFECTIVE when hiding it alone
// changes `pre`. A reference pass whose violation bits are non-zero ends the sequence (status 3); a
// reference amount of exactly 0 or no candidate leaves the reference pass alone (status 0).
split_t run_split( state_t* st, action_t* a, player_t* dealer, player_t* owner, player_t* target,
                   const pass_fn_t& pass, bool allow_probe, bool direct_hit = false )
{
  split_t out;
  std::uint64_t viol = 0;
  const amount_t ref = pass( /*log_reads=*/true, &viol );
  out.passes.push_back( { "ref", 0, ref, viol } );
  if ( viol != 0 )
  {
    ++st->reference_mismatch;
    out.status = 3;
    return out;
  }
  if ( ref.pre == 0.0 )
  {
    // Plan 12 (MN-10): a direct hit whose direct amount is structurally zero (a pure damage-over-time or no-damage action) is not a
    // "amount kept in class-private storage" suspect: no `zr` record, counted. The reference pass entry stays.
    if ( direct_hit && a->rl_bl_direct_structurally_zero( scratch_for( st, a ) ) )
    {
      ++st->zero_structural_skipped;
      return out;
    }
    write_zero_record( st, a, target );
    return out;
  }

  std::vector<buff_t*>& cands = out.cands;
  collect_candidates( st, dealer, owner, target, cands );
  if ( cands.size() > 40 )
  {
    throw sc_runtime_error( fmt::format( "rl_buff_ledger=: {} candidate buffs on one hit of '{}'; the hidden-set "
                                         "bitmask supports at most 40.",
                                         cands.size(), a->name() ) );
  }
  if ( cands.empty() )
    return out;

  if ( allow_probe && a->sim->rl_buff_ledger_guard_probe && !st->probe_done )
  {
    st->probe_done = true;
    fire_guard_probe( st, a, dealer, target, cands[ 0 ] );
  }

  auto hidden_pass = [ & ]( std::uint64_t mask ) {
    hidden_buffs_t hidden;
    for ( std::size_t i = 0; i < cands.size(); ++i )
      if ( mask & ( std::uint64_t( 1 ) << i ) )
        hidden.hide( cands[ i ] );
    out.passes.push_back( { "hide", mask, pass( false, nullptr ), 0 } );
  };

  // One pass per candidate hidden alone.
  for ( std::size_t i = 0; i < cands.size(); ++i )
    hidden_pass( std::uint64_t( 1 ) << i );

  // For 2 or 3 effective candidates every remaining hidden subset (all 2^m - 1 minus the singles) is
  // run; above 3 only the all-effective-hidden pass (the split rule needs each single removal plus the
  // joint removal). One effective candidate needs nothing more.
  std::uint64_t effective_mask = 0;
  int n_effective              = 0;
  for ( std::size_t i = 0; i < cands.size(); ++i )
  {
    if ( out.passes[ 1 + i ].amount.pre != ref.pre )
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

  // Restoring pass: nothing hidden again, a fresh snapshot, so class members written by the hidden
  // passes (shaman's mw_affected_stacks / mw_consumed_stacks) return to their real values.
  std::uint64_t rviol = 0;
  const amount_t rest = pass( false, &rviol );
  out.passes.push_back( { "restore", 0, rest, rviol } );
  if ( rviol != 0 )
  {
    ++st->restoring_mismatch;
    out.status = 4;
  }
  else
    out.status = 1;
  return out;
}

// After a pass set: a guard that fired makes the set unsafe (status 2, counted once per set) unless a
// mismatch already gave it status 3 or 4. Returns the guard names as a quoted, comma separated list.
std::string finish_unsafe( state_t* st, split_t& sp )
{
  std::string names;
  if ( st->cur_guards.empty() )
    return names;
  ++st->unsafe_hits;
  if ( sp.status == 0 || sp.status == 1 )
    sp.status = 2;
  for ( const char* g : st->cur_guards )
  {
    if ( !names.empty() )
      names += ',';
    put_string( names, g );
  }
  return names;
}

// Candidate entries are written for any status with candidates (1 split, 2 unsafe, 4 restoring mismatch).
bool candidates_written( const split_t& sp )
{
  return sp.status == 1 || sp.status == 2 || sp.status == 4;
}

// 261003-s1c plan 02: the memory backend builds no text; the only state effect of writing a candidate entry (put_candidate ->
// write_appliers) is bringing the buff's applier list up to its stacks (reconcile_appliers), which is kept.
void write_candidates( std::string& out, state_t* st, const split_t& sp, const player_t* target )
{
  if ( !candidates_written( sp ) )
    return;
  for ( std::size_t i = 0; i < sp.cands.size(); ++i )
  {
    if ( st->text )
      put_candidate( out, st, i, sp.cands[ i ], sp.cands[ i ]->player == target );
    else
      reconcile_appliers( st, sp.cands[ i ] );
  }
}

// ---- Plan 07: the optional pass cache --------------------------------------------------------------------------
//
// With rl_buff_ledger_cache=1 a pass set whose inputs equal those of an earlier one skips its passes and takes their
// results from the earlier one: the ratio of every pass to the reference pass (the cache key holds the real amount,
// so the reference amount is the stored one) and the crit chance and bonus of every pass. The key is what the engine
// has in hand before a pass set starts; the buffs the amount code read are known only from the earlier reference
// pass, so the second part of the lookup compares the stack, value and press-applied state of exactly those buffs
// (plus every stat buff of the dealer and its owner, which act through the player's stats whether or not a read
// shows them). Anything the amount code reads that is neither (a resource, the target's health, a dot) is not in the
// key: every 16th cache hit therefore also runs the full passes, compares at 1e-12 relative and counts the
// disagreements (footer cache_check, cache_check_fail); the fresh result is the one used on a recheck. Results of
// sets that had a guard fire, a mismatch, a zero reference amount (the `zr` record needs the real reads) are never
// stored. With the option off none of this runs.

constexpr double CACHE_RECHECK_TOL         = 1e-12;
constexpr std::uint64_t CACHE_RECHECK_EVERY = 16;
constexpr std::size_t CACHE_MAX_VALUES     = std::size_t( 1 ) << 20;

enum cache_kind_t : std::uint8_t
{
  CK_HIT,         // direct hit, snapshot made in execute
  CK_HIT_TARGET,  // direct hit of a pre-made state, target part re-snapshotted per target
  CK_APP,         // damage-over-time application
  CK_PREMADE,     // pre-made / tick-action application snapshot
  CK_TICK         // direct tick
};

struct cache_ctx_t
{
  bool active  = false;  // the cache was consulted for this pass set (the passes run, or ran, and may be stored)
  bool recheck = false;  // a stored result is to be compared with the fresh passes
  std::string k0;
  state_t::cache_val_t stored;
};

template <class T>
void key_put( std::string& k, const T& v )
{
  k.append( reinterpret_cast<const char*>( &v ), sizeof( T ) );
}

std::string cache_key0( cache_kind_t kind, const action_t* a, const action_state_t* s, double tick_multiplier,
                        result_amount_type pre_rt )
{
  std::string k;
  k.reserve( 256 );
  key_put( k, static_cast<std::uint8_t>( kind ) );
  key_put( k, a );
  key_put( k, s->target );
  key_put( k, static_cast<int>( s->result_type ) );
  key_put( k, s->n_targets );
  key_put( k, s->chain_target );
  if ( kind != CK_APP && kind != CK_PREMADE )
  {
    key_put( k, static_cast<int>( s->result ) );
    key_put( k, static_cast<int>( s->block_result ) );
  }
  if ( kind == CK_HIT || kind == CK_HIT_TARGET || kind == CK_TICK )
    key_put( k, s->result_amount );
  if ( kind == CK_TICK )
    key_put( k, tick_multiplier );
  if ( kind == CK_HIT_TARGET )
    key_put( k, static_cast<int>( pre_rt ) );
  for ( std::size_t i = 0; i < N_SNAPSHOT_FIELDS; ++i )
    key_put( k, s->*( SNAPSHOT_FIELDS[ i ].member ) );
  return k;
}

std::string cache_values( state_t* st, player_t* dealer, player_t* owner, const std::vector<const buff_t*>& reads )
{
  std::string v;
  v.reserve( 16 * ( reads.size() + 32 ) );
  auto put_buff = [ & ]( const buff_t* b ) {
    const int stack    = b->current_stack;
    const double value = b->current_value;
    key_put( v, stack );
    key_put( v, value );
    const bool up = stack > 0 || value != 0.0;
    key_put( v, static_cast<std::uint8_t>( up && press_applied( st, b ) ? 1 : 0 ) );
  };
  for ( const buff_t* b : reads )
    put_buff( b );
  for ( player_t* p : { dealer, owner } )
  {
    if ( p == nullptr )
      continue;
    for ( stat_buff_t* sb : stat_buffs_of( st, p ) )
    {
      put_buff( sb );
      for ( const stat_buff_t::buff_stat_t& bs : sb->stats )
        key_put( v, bs.current_value );
    }
  }
  return v;
}

// Fills `out` and returns true when the passes can be skipped. Returns false, with `cx` telling the caller what to do
// afterwards, when the cache is off, missed, or this hit is one of the 16th that are re-run and compared.
bool cache_lookup( state_t* st, action_t* a, player_t* dealer, player_t* owner, const action_state_t* s,
                   cache_kind_t kind, double tick_multiplier, result_amount_type pre_rt, split_t& out, cache_ctx_t& cx )
{
  if ( !a->sim->rl_buff_ledger_cache || a->sim->rl_buff_ledger_guard_probe )
    return false;
  cx.active = true;
  cx.k0     = cache_key0( kind, a, s, tick_multiplier, pre_rt );
  auto it   = st->cache.find( cx.k0 );
  if ( it == st->cache.end() )
  {
    ++st->cache_misses;
    return false;
  }
  const std::string v = cache_values( st, dealer, owner, it->second.reads );
  auto vt             = it->second.by_values.find( v );
  if ( vt == it->second.by_values.end() )
  {
    ++st->cache_misses;
    return false;
  }
  ++st->cache_hits;
  if ( st->cache_hits % CACHE_RECHECK_EVERY == 0 )
  {
    cx.recheck = true;
    cx.stored  = vt->second;
    return false;
  }
  const state_t::cache_val_t& val = vt->second;
  out.status                      = val.status;
  out.cands                       = val.cands;
  out.cached                      = true;
  out.passes.reserve( val.passes.size() );
  for ( std::size_t i = 0; i < val.passes.size(); ++i )
  {
    const state_t::cache_pass_t& p = val.passes[ i ];
    out.passes.push_back( { p.kind, p.hid, { i == 0 ? val.ref_pre : val.ref_pre * p.ratio, p.cc, p.cb }, 0 } );
  }
  return true;
}

bool cache_close( double x, double y )
{
  const double scale = std::max( std::fabs( x ), std::fabs( y ) );
  return scale == 0.0 || std::fabs( x - y ) <= CACHE_RECHECK_TOL * scale;
}

// After the passes of a consulted pass set ran: compare with the stored result on a recheck, and store the fresh
// result when it is clean (status 0 or 1, no guard, a non-zero reference amount).
void cache_store( state_t* st, const cache_ctx_t& cx, player_t* dealer, player_t* owner, const split_t& sp,
                  const std::string& guards )
{
  if ( !cx.active )
    return;
  const bool storable = guards.empty() && ( sp.status == 0 || sp.status == 1 ) && !sp.passes.empty() &&
                        sp.passes[ 0 ].amount.pre != 0.0;
  if ( cx.recheck )
  {
    ++st->cache_check;
    bool same = storable && sp.status == cx.stored.status && sp.passes.size() == cx.stored.passes.size() &&
                sp.cands == cx.stored.cands && cache_close( sp.passes[ 0 ].amount.pre, cx.stored.ref_pre );
    for ( std::size_t i = 0; same && i < sp.passes.size(); ++i )
    {
      const pass_rec_t& p            = sp.passes[ i ];
      const state_t::cache_pass_t& q = cx.stored.passes[ i ];
      same = std::strcmp( p.kind, q.kind ) == 0 && p.hid == q.hid && cache_close( p.amount.cc, q.cc ) &&
             cache_close( p.amount.cb, q.cb ) && cache_close( p.amount.pre / sp.passes[ 0 ].amount.pre, q.ratio );
    }
    if ( !same )
      ++st->cache_check_fail;
  }
  if ( !storable )
    return;
  std::vector<const buff_t*> reads;
  reads.reserve( g_tap_reads.size() );
  for ( const tap_read_t& r : g_tap_reads )  // only the reference pass opens the tap: these are its reads
    reads.push_back( r.b );
  if ( st->cache_values_stored >= CACHE_MAX_VALUES )
  {
    st->cache.clear();
    st->cache_values_stored = 0;
  }
  state_t::cache_entry_t& e = st->cache[ cx.k0 ];
  if ( e.reads != reads )
  {
    st->cache_values_stored -= e.by_values.size();
    e.by_values.clear();
    e.reads = reads;
  }
  state_t::cache_val_t val;
  val.status  = sp.status;
  val.ref_pre = sp.passes[ 0 ].amount.pre;
  val.cands   = sp.cands;
  for ( const pass_rec_t& p : sp.passes )
    val.passes.push_back( { p.kind, p.hid, p.amount.pre / val.ref_pre, p.amount.cc, p.amount.cb } );
  auto ins = e.by_values.insert_or_assign( cache_values( st, dealer, owner, e.reads ), std::move( val ) );
  if ( ins.second )
    ++st->cache_values_stored;
}

action_state_t* scratch_for( state_t* st, action_t* a )
{
  auto& slot = st->scratch[ a ];
  if ( !slot )
    slot.reset( a->new_state() );
  return slot.get();
}

// Writes one `app` record (plan 04: a DoT application's periodic amounts; plan 05: a premade or tick-action
// snapshot's direct amounts, `src` non-null) at once (an application with no direct damage has no hit to
// ride) and registers it in the fight's table. Its id is drawn from the same per-fight serial as `hit.h`.
// `ref` is the reference pass's `pre`, the divisor of every pass's `per`.
std::uint64_t write_app_record( state_t* st, action_t* a, action_state_t* s, const split_t& sp,
                                const std::string& guards, double ref, const char* src,
                                const rl_cause_t* cause = nullptr )
{
  const std::uint64_t h = st->next_hit++;
  ++st->app_records;
  st->app_passes += sp.passes.size();
  state_t::app_info_t info;
  info.action  = a;
  info.target  = s->target;
  info.ref_pre = ref;
  for ( const buff_t* c : sp.cands )
    info.cands.push_back( c );
  st->apps.emplace( h, std::move( info ) );

  sim_t* sim = a->sim;
  // A tick action's application snapshot is not stamped (the stamp is written on the states of the hits);
  // the caller hands in the cause of the cast that is executing.
  const std::int64_t r_seq   = cause != nullptr ? cause->seq : s->rl_cause_seq;
  const std::uint8_t r_cls_raw = cause != nullptr ? cause->cls : s->rl_cause_class;
  const std::int16_t r_press = cause != nullptr ? cause->press : s->rl_cause_press;
  const std::int32_t r_launch = cause != nullptr ? cause->launch : s->rl_cause_launch;
  // (the memory backend builds no text: write_candidates then only reconciles the applier lists)
  std::string cand_json;
  write_candidates( cand_json, st, sp, s->target );
  std::string pass_json;
  if ( st->text )
    for ( const pass_rec_t& p : sp.passes )
      put_pass( pass_json, p, &ref );
  emit( st, REC_APP, [ & ]( std::string& b ) {
    fmt::format_to( out_it( b ), "{{\"k\":\"app\",\"it\":{},\"t\":", sim->current_iteration );
    put_double( b, sim->current_time().total_seconds() );
    fmt::format_to( out_it( b ), ",\"h\":{},\"actor\":", h );
    put_string( b, a->player->name() );
    b += ",\"action\":";
    put_string( b, a->name() );
    b += ",\"target\":";
    put_string( b, s->target->name() );
    fmt::format_to( out_it( b ), ",\"seq\":{},\"cls\":{},\"dk\":{},\"press\":{},\"launch\":{},\"n_targets\":{},\"status\":{},\"cand\":[",
                    r_seq, cls_out( r_cls_raw ), dk_out( r_cls_raw ), r_press, r_launch, s->n_targets, sp.status );
    b += cand_json;
    b += "],\"pass\":[";
    b += pass_json;
    b += ']';
    if ( src != nullptr )
      fmt::format_to( out_it( b ), ",\"src\":\"{}\"", src );
    if ( sp.cached )
      b += ",\"cached\":true";
    if ( !guards.empty() )
    {
      b += ",\"guards\":[";
      b += guards;
      b += ']';
    }
    b += "}\n";
  } );
  return h;
}

// Plan 04: the application's periodic passes, written as an `app` record. Only for a hit that applies a
// damage-over-time effect with no tick_action (those are plan 05's, see run_premade). The record's id is
// stamped on the state, so the DoT's state (a copy) and every later tick can name it. An application
// whose reference periodic amount is exactly 0 writes nothing.
void run_app_passes( state_t* st, action_t* a, action_state_t* s, action_state_t* scratch, player_t* dealer,
                     player_t* owner )
{
  split_t sp;
  std::string guards;
  cache_ctx_t cx;
  if ( !cache_lookup( st, a, dealer, owner, s, CK_APP, 0.0, result_amount_type::NONE, sp, cx ) )
  {
    {
      shadow_scope_t scope( st, a, s->target );
      pass_env_t env{ a, s, scratch, &scope };
      pass_fn_t fn = [ & ]( bool log_reads, std::uint64_t* viol ) { return app_pass( env, log_reads, viol ); };
      sp     = run_split( st, a, dealer, owner, s->target, fn, /*allow_probe=*/false );
      guards = finish_unsafe( st, sp );
    }
    cache_store( st, cx, dealer, owner, sp, guards );
  }
  const double ref_periodic = sp.passes[ 0 ].amount.pre;
  if ( sp.status == 0 && ref_periodic == 0.0 )
  {
    // Plan 12 (MJ-03): no record is written; the state says why, so a tick of this application is a counted null parent.
    s->rl_bl_app = APP_ZERO_REF;
    ++st->app_zero_ref;
    ++st->app_zero_ref_by_action[ a->name() ];
    return;
  }

  s->rl_bl_app = write_app_record( st, a, s, sp, guards, ref_periodic, nullptr );
}

// Plan 05: the passes of a state whose snapshot was made before its execute: a state class code snapshotted
// itself and handed to schedule_execute (src "premade"), or a tick action's application snapshot
// (src "tick_action"). The result is an `app` record (the direct amounts under every hidden set,
// `per` = pre / reference pre) whose id is stamped on the state (`rl_bl_pm`); the hits made from this state
// name it in `par`. A state whose reference amount is exactly 0 writes nothing.
void run_premade( state_t* st, action_t* a, action_state_t* s, const char* src, const rl_cause_t* cause = nullptr )
{
  action_state_t* scratch = scratch_for( st, a );
  player_t* dealer        = a->player;
  player_t* owner         = dealer->is_pet() ? static_cast<pet_t*>( dealer )->owner : nullptr;

  split_t sp;
  std::string guards;
  cache_ctx_t cx;
  if ( !cache_lookup( st, a, dealer, owner, s, CK_PREMADE, 0.0, result_amount_type::NONE, sp, cx ) )
  {
    {
      shadow_scope_t scope( st, a, s->target );
      pass_env_t env{ a, s, scratch, &scope };
      pass_fn_t fn = [ & ]( bool log_reads, std::uint64_t* viol ) { return premade_pass( env, log_reads, viol ); };
      sp     = run_split( st, a, dealer, owner, s->target, fn, /*allow_probe=*/false );
      guards = finish_unsafe( st, sp );
    }
    cache_store( st, cx, dealer, owner, sp, guards );
  }
  const double ref = sp.passes[ 0 ].amount.pre;
  if ( sp.status == 0 && ref == 0.0 )
    return;

  s->rl_bl_pm = write_app_record( st, a, s, sp, guards, ref, src, cause );
  if ( std::strcmp( src, "premade" ) == 0 )
    ++st->premade_records;
  else
    ++st->tick_action_records;
}

// A candidate shared between a hit's own passes and the entry its state names as parent is counted once
// per hit (footer both_group_candidates): the product rule of LEDGER-FORMAT.md is then only approximate.
void count_both_group( state_t* st, std::uint64_t parent_id, const std::vector<buff_t*>& cands )
{
  if ( parent_id == 0 || cands.empty() )
    return;
  auto app = st->apps.find( parent_id );
  if ( app == st->apps.end() )
    return;
  for ( const buff_t* c : cands )
  {
    if ( std::find( app->second.cands.begin(), app->second.cands.end(), c ) != app->second.cands.end() )
    {
      ++st->both_group_candidates;
      return;
    }
  }
}

// The two guard-name tables of rng_access.
struct family_name_t
{
  const char* family;
  const char* guard;
};
constexpr family_name_t RNG_FAMILIES[] = {
    { "action", "rng.draw.action" },     { "player", "rng.draw.player" },     { "sim", "rng.draw.sim" },
    { "buff", "rng.draw.buff" },         { "callback", "rng.draw.callback" }, { "proc_rng", "rng.draw.proc_rng" },
};
}  // namespace

void* scratch_event_block( sim_t* sim, std::size_t size )
{
  constexpr std::size_t BLOCK = util::next_power_of_two( 2 * sizeof( event_t ) );
  if ( size > BLOCK )
    throw std::bad_alloc();
  state_t* st = state_of( sim );
  if ( st == nullptr )
    return ::operator new( BLOCK );
  st->scratch_event_blocks.emplace_back( new unsigned char[ BLOCK ] );
  return st->scratch_event_blocks.back().get();
}

void note_blocked( sim_t* sim, const char* guard )
{
  state_t* st = state_of( sim );
  if ( st == nullptr )
    return;
  ++st->shadow_violation[ guard ];
  for ( const char* g : st->cur_guards )
    if ( std::strcmp( g, guard ) == 0 )
      return;
  st->cur_guards.push_back( guard );
}

rng::rng_t* rng_access( sim_t* sim, const char* family )
{
  if ( !sim->rl_bl_shadow )
  {
    // Plan 06: a random generator handed out inside a ledger frame (outside the ledger's own passes). A run of
    // draws with no read, consume or launch of the frame between them is one `dr` record.
    state_t* s = state_of( sim );
    if ( s != nullptr && s->in_fight && !s->frames.empty() && g_busy == 0 )
    {
      ++s->draws_raw;
      state_t::frame_t& f = s->frames.back();
      if ( !f.last_dr )
      {
        f.last_dr = true;
        ++s->draws_written;
        const std::int64_t order = s->next_order++;
        emit( s, REC_DR, [ & ]( std::string& o ) {
          fmt::format_to( out_it( o ), "{{\"k\":\"dr\",\"it\":{},\"t\":", sim->current_iteration );
          put_double( o, sim->current_time().total_seconds() );
          fmt::format_to( out_it( o ), ",\"f\":{},\"o\":{},\"src\":", f.id, order );
          put_string( o, family );
          o += "}\n";
        } );
      }
    }
    return nullptr;
  }
  state_t* st = state_of( sim );
  if ( st == nullptr )
    return nullptr;
  const char* guard = "rng.draw.other";
  for ( const family_name_t& f : RNG_FAMILIES )
    if ( std::strcmp( f.family, family ) == 0 )
      guard = f.guard;
  note_blocked( sim, guard );
  return &st->scratch_rng;
}

// Plan 12 (MJ-04): every pass entry point calls this first. A pass that starts while another pass is running (an execute reached
// from inside an amount computation) is refused: it would open a scope inside a scope and run mutators in the outer pass's name.
// Counted in the footer (nested_pass_refused, by entry point); outside the guard probe the count is 0 (a check asserts it).
static bool refuse_nested( state_t* st, const sim_t* sim, const char* entry )
{
  if ( !sim->rl_bl_shadow )
    return false;
  ++st->nested_pass_refused;
  ++st->nested_pass_refused_by_entry[ entry ];
  return true;
}

// Plan 12 (MN-08): a pass set whose target is null is not run at all (no scope is opened, no pass executes): the scope and the
// passes dereference the target. Counted in the footer (null_target_skipped).
static bool skip_null_target( state_t* st, const player_t* target )
{
  if ( target != nullptr )
    return false;
  ++st->null_target_skipped;
  return true;
}

// Plan 12 (MJ-03): a dot-applying hit executed from a pre-made state writes no `app` record (its DoT state would rest on a
// snapshot this execute did not make): the state says so, and the count tells the ticks' null parents apart from a zero reference.
void note_app_missing_premade( state_t* st, action_t* a, action_state_t* s )
{
  if ( a->tick_action == nullptr && a->dot_duration > timespan_t::zero() && action_t::result_is_hit( s->result ) )
  {
    s->rl_bl_app = APP_PREMADE;
    ++st->app_missing_premade;
    ++st->app_missing_premade_by_action[ a->name() ];
  }
}

// Plan 12 (MJ-01): the check result of an AoE pre-made hit: "ok" (the real amount equals the entry's reference amount bit for
// bit), "drift" (it does not; `drift` is set), or why it could not be checked: "multi_target" (more than one target, or a chain),
// "other_target" (the hit's target is not the target the state was made for), "no_entry" (nothing was handed over, or the entry
// belongs to another action), "target_changed" (the re-snapshotted fields differ from the hand-over snapshot), "result" (a miss,
// dodge, parry or glancing blow: its amount is not the reference amount).
const char* aoe_premade_check( state_t* st, action_t* a, action_state_t* s, const action_state_t* pre, bool& drift )
{
  if ( s->n_targets != 1 || s->chain_target != 0 )
    return "multi_target";
  if ( s->target != pre->target )
    return "other_target";
  if ( pre->rl_bl_pm == 0 )
    return "no_entry";
  auto it = st->apps.find( pre->rl_bl_pm );
  if ( it == st->apps.end() || it->second.action != a )
    return "no_entry";
  if ( snapshot_diff( s, pre ) != 0 )
    return "target_changed";
  if ( action_t::result_is_miss( s->result ) || s->result == RESULT_GLANCE )
    return "result";
  if ( same_bits( s->result_amount, it->second.ref_pre ) )
    return "ok";
  drift = true;
  return "drift";
}

void run_passes( action_t* a, action_state_t* s, const action_state_t* pre )
{
  state_t* st = state_of( a->sim );
  if ( st == nullptr || !st->in_fight || !belongs_to_actor( *st, a->player ) )
    return;
  if ( refuse_nested( st, a->sim, "run_passes" ) )
    return;
  if ( skip_null_target( st, s->target ) )
    return;
  // Plan 05: a pre-made state re-snapshotted per target (AoE execute). The entry written when it was handed
  // to schedule_execute rides this hit as its parent. Plan 12 (MJ-01): taken BEFORE the result-type return (an AoE execute
  // of a pre-made DMG_OVER_TIME state named no parent), and a pre-made state with no entry is counted here as it is in
  // premade_hit (premade_uncovered now covers both branches).
  if ( pre != nullptr )
  {
    s->rl_bl_pm = pre->rl_bl_pm;
    if ( pre->rl_bl_pm == 0 )
    {
      ++st->premade_uncovered;
      ++st->uncovered_by_action[ a->name() ];
    }
  }
  if ( s->result_type != result_amount_type::DMG_DIRECT )
    return;

  // A state that will never reach hit_sink (assess_damage skips a zero-raw hit that is not a miss;
  // an action without stats routes nothing) gets its snapshot checked but nothing parked.
  const bool will_sink = a->stats != nullptr && ( s->result_raw > 0 || action_t::result_is_miss( s->result ) );

  action_state_t* scratch = scratch_for( st, a );
  player_t* dealer        = a->player;
  player_t* owner         = dealer->is_pet() ? static_cast<pet_t*>( dealer )->owner : nullptr;

  state_t::hit_entry_t entry;
  entry.action = a;
  entry.target = s->target;
  entry.fr     = cur_frame( st );

  // Plan 12 (MJ-01): an AoE pre-made hit at one target is drift-checked against its entry's reference amount exactly as
  // premade_hit does for a single-target execute. Every AoE pre-made hit carries the result of this check (record field pmc).
  if ( pre != nullptr )
  {
    bool drift = false;
    entry.pmc  = aoe_premade_check( st, a, s, pre, drift );
    if ( drift )
    {
      ++st->premade_drift;
      if ( will_sink )
      {
        entry.status           = 6;
        const std::uint64_t id = st->next_hit_id++;
        st->hit_table.emplace( id, std::move( entry ) );
        s->rl_bl_hit = id;
      }
      return;
    }
  }
  split_t sp;
  cache_ctx_t cx;
  if ( !cache_lookup( st, a, dealer, owner, s, pre != nullptr ? CK_HIT_TARGET : CK_HIT, 0.0,
                      pre != nullptr ? pre->result_type : result_amount_type::NONE, sp, cx ) )
  {
    {
      shadow_scope_t scope( st, a, s->target );
      pass_env_t env{ a, s, scratch, &scope };
      if ( pre != nullptr )
      {
        env.target_only = true;
        env.target_rt   = pre->result_type;
      }
      // Reference and restoring passes must reproduce the real snapshot and pre-crit amount bit for bit.
      pass_fn_t fn = [ & ]( bool log_reads, std::uint64_t* viol ) {
        const amount_t am = amount_pass( env, log_reads );
        if ( viol != nullptr )
        {
          *viol = snapshot_diff( env.scratch, s );
          if ( !same_bits( am.pre, s->result_amount ) )
            *viol |= VIOL_PRE_CRIT;
        }
        return am;
      };
      sp                 = run_split( st, a, dealer, owner, s->target, fn, /*allow_probe=*/true, /*direct_hit=*/true );
      entry.guards_json  = finish_unsafe( st, sp );
    }  // scope ends: caches, debug, log, shadow flag, recorder hook restored; hidden buffs were restored per pass
    cache_store( st, cx, dealer, owner, sp, entry.guards_json );
  }

  if ( will_sink )
  {
    entry.status = sp.status;
    entry.cached = sp.cached;
    write_candidates( entry.cand_json, st, sp, s->target );
    if ( candidates_written( sp ) && !sp.cands.empty() )  // (cand_json is non-empty exactly then in the JSON backend)
      entry.cands = sp.cands;
    if ( st->text )
      for ( const pass_rec_t& p : sp.passes )
        put_pass( entry.pass_json, p );
    entry.n_passes = static_cast<std::uint32_t>( sp.passes.size() );

    const std::uint64_t id = st->next_hit_id++;
    st->hit_table.emplace( id, std::move( entry ) );
    s->rl_bl_hit = id;
  }

  // Plan 04: a hit that applies a damage-over-time effect (no tick_action) also records what the effect's
  // periodic amount will be under every hidden set. A miss applies nothing. (Not for a pre-made state:
  // its DoT state would rest on a snapshot this execute did not make.)
  if ( pre == nullptr && a->tick_action == nullptr && a->dot_duration > timespan_t::zero() &&
       action_t::result_is_hit( s->result ) )
    run_app_passes( st, a, s, scratch, dealer, owner );
  else if ( pre != nullptr )
    note_app_missing_premade( st, a, s );
}

void premade_snapshot( action_t* a, action_state_t* s )
{
  state_t* st = state_of( a->sim );
  if ( st == nullptr || !st->in_fight || !belongs_to_actor( *st, a->player ) )
    return;
  if ( refuse_nested( st, a->sim, "premade_snapshot" ) )
    return;
  if ( skip_null_target( st, s->target ) )
    return;
  if ( s->result_type != result_amount_type::DMG_DIRECT && s->result_type != result_amount_type::DMG_OVER_TIME )
    return;
  run_premade( st, a, s, "premade" );
}

void tick_action_snapshot( action_t* tick_action, action_state_t* s, const rl_cause_t& cause )
{
  state_t* st = state_of( tick_action->sim );
  if ( st == nullptr || !st->in_fight || !belongs_to_actor( *st, tick_action->player ) )
    return;
  if ( refuse_nested( st, tick_action->sim, "tick_action_snapshot" ) )
    return;
  if ( skip_null_target( st, s->target ) )
    return;
  if ( s->result_type != result_amount_type::DMG_DIRECT && s->result_type != result_amount_type::DMG_OVER_TIME )
    return;
  run_premade( st, tick_action, s, "tick_action", &cause );
}

void premade_hit( action_t* a, action_state_t* s, const action_state_t* pre )
{
  state_t* st = state_of( a->sim );
  if ( st == nullptr || !st->in_fight || !belongs_to_actor( *st, a->player ) )
    return;
  if ( refuse_nested( st, a->sim, "premade_hit" ) )
    return;
  s->rl_bl_pm = pre->rl_bl_pm;
  note_app_missing_premade( st, a, s );
  if ( pre->rl_bl_pm == 0 )
  {
    ++st->premade_uncovered;
    ++st->uncovered_by_action[ a->name() ];
    return;
  }
  auto it = st->apps.find( pre->rl_bl_pm );
  if ( it == st->apps.end() || it->second.action != a )
    return;
  // A miss, dodge or parry computes an amount of 0; a glancing blow scales it: neither is a drift.
  if ( action_t::result_is_miss( s->result ) || s->result == RESULT_GLANCE )
    return;
  if ( same_bits( s->result_amount, it->second.ref_pre ) )
    return;

  // The real amount is not what the hand-over reference predicted: the hit's split is ignored (status 6).
  ++st->premade_drift;
  const bool will_sink = a->stats != nullptr && ( s->result_raw > 0 || action_t::result_is_miss( s->result ) );
  if ( !will_sink )
    return;
  state_t::hit_entry_t entry;
  entry.action = a;
  entry.target = s->target;
  entry.status = 6;
  entry.fr     = cur_frame( st );
  const std::uint64_t id = st->next_hit_id++;
  st->hit_table.emplace( id, std::move( entry ) );
  s->rl_bl_hit = id;
}

void run_tick_passes( action_t* a, action_state_t* d_state, double tick_multiplier )
{
  state_t* st = state_of( a->sim );
  if ( st == nullptr || !st->in_fight || !belongs_to_actor( *st, a->player ) )
    return;
  if ( refuse_nested( st, a->sim, "run_tick_passes" ) )
    return;
  if ( skip_null_target( st, d_state->target ) )
    return;

  const bool will_sink =
      a->stats != nullptr && ( d_state->result_raw > 0 || action_t::result_is_miss( d_state->result ) );

  action_state_t* scratch = scratch_for( st, a );
  player_t* dealer        = a->player;
  player_t* owner         = dealer->is_pet() ? static_cast<pet_t*>( dealer )->owner : nullptr;

  state_t::hit_entry_t entry;
  entry.action = a;
  entry.target = d_state->target;
  entry.fr     = cur_frame( st );
  split_t sp;
  cache_ctx_t cx;
  if ( !cache_lookup( st, a, dealer, owner, d_state, CK_TICK, tick_multiplier, result_amount_type::NONE, sp, cx ) )
  {
    {
      shadow_scope_t scope( st, a, d_state->target );
      pass_env_t env{ a, d_state, scratch, &scope };
      // Plan 12 (MJ-04): the guard probe's second one-shot per fight, in the first tick pass set: cancel the ticking
      // damage-over-time effect (a counted no-op, guard `dot.cancel`; the fight must not change).
      if ( a->sim->rl_buff_ledger_guard_probe && !st->probe_tick_done )
      {
        if ( dot_t* probed = a->find_dot( d_state->target ) )
        {
          st->probe_tick_done = true;
          probed->cancel();
        }
      }
      pass_fn_t fn = [ & ]( bool log_reads, std::uint64_t* viol ) {
        return tick_pass( env, tick_multiplier, log_reads, viol );
      };
      sp                = run_split( st, a, dealer, owner, d_state->target, fn, /*allow_probe=*/false );
      entry.guards_json = finish_unsafe( st, sp );
    }
    cache_store( st, cx, dealer, owner, sp, entry.guards_json );
  }

  // (A tick that shares a candidate buff with its application is counted in hit_sink, where its record is
  // written: the product rule in LEDGER-FORMAT.md is then only approximate for that buff.)
  // A previous tick's parked entry that never reached the sink (zero raw amount) is dropped.
  if ( d_state->rl_bl_hit != 0 )
  {
    st->hit_table.erase( d_state->rl_bl_hit );
    d_state->rl_bl_hit = 0;
  }
  if ( !will_sink )
    return;

  entry.status = sp.status;
  entry.cached = sp.cached;
  write_candidates( entry.cand_json, st, sp, d_state->target );
  if ( candidates_written( sp ) && !sp.cands.empty() )  // (cand_json is non-empty exactly then in the JSON backend)
    entry.cands = sp.cands;
  if ( st->text )
    for ( const pass_rec_t& p : sp.passes )
      put_pass( entry.pass_json, p );
  entry.n_passes = static_cast<std::uint32_t>( sp.passes.size() );

  const std::uint64_t id = st->next_hit_id++;
  st->hit_table.emplace( id, std::move( entry ) );
  d_state->rl_bl_hit = id;
}

// ==========================================================================
// Plan 05: swing passes and swing launches
// ==========================================================================

void run_swing_passes( action_t* a )
{
  state_t* st = state_of( a->sim );
  if ( st == nullptr || !st->in_fight || !belongs_to_actor( *st, a->player ) )
    return;
  if ( refuse_nested( st, a->sim, "run_swing_passes" ) )
    return;
  if ( skip_null_target( st, a->target ) )
    return;

  player_t* dealer = a->player;
  player_t* owner  = dealer->is_pet() ? static_cast<pet_t*>( dealer )->owner : nullptr;
  const timespan_t real = a->time_to_execute;

  int status = 0;
  std::vector<buff_t*> cands;
  std::vector<timespan_t> hidden_times;
  std::string guards;
  {
    shadow_scope_t scope( st, a, a->target );
    // Reference: the engine's own swing time on invalidated caches, with the read tap open. It must be the
    // swing time the action just computed, bit for bit (timespan_t is an integer number of milliseconds).
    scope.invalidate_caches();
    timespan_t ref;
    {
      tap_scope_t tap( true );
      ref = a->execute_time();
    }
    if ( ref != real )
    {
      ++st->reference_mismatch;
      status = 3;
    }
    else
    {
      collect_candidates( st, dealer, owner, a->target, cands );
      for ( buff_t* c : cands )
      {
        hidden_buffs_t hidden;
        hidden.hide( c );
        scope.invalidate_caches();
        hidden_times.push_back( a->execute_time() );
      }
      // Restoring pass: nothing hidden again, fresh caches; the swing time must come back.
      scope.invalidate_caches();
      if ( a->execute_time() != real )
      {
        ++st->restoring_mismatch;
        status = 4;
      }
    }
    // Guards that fired inside the scope (a mutator or a draw reached from execute_time).
    if ( !st->cur_guards.empty() )
    {
      ++st->unsafe_hits;
      if ( status == 0 )
        status = 2;
      for ( const char* g : st->cur_guards )
      {
        if ( !guards.empty() )
          guards += ',';
        put_string( guards, g );
      }
    }
  }

  const std::int32_t l = st->next_launch++;
  st->swing_slot[ a ]  = l;
  ++st->swing_launches;

  sim_t* sim     = a->sim;
  if ( !st->text && ( status == 0 || status == 2 ) )
  {
    // The state effect write_appliers has, for the candidates the record would list.
    for ( std::size_t i = 0; i < cands.size(); ++i )
      if ( !( hidden_times[ i ] == real || real.total_millis() == 0 ) )
        reconcile_appliers( st, const_cast<buff_t*>( cands[ i ] ) );
  }
  emit( st, REC_LN, [ & ]( std::string& b ) {
    fmt::format_to( out_it( b ), "{{\"k\":\"ln\",\"it\":{},\"t\":", sim->current_iteration );
    put_double( b, sim->current_time().total_seconds() );
    fmt::format_to( out_it( b ), ",\"l\":{},\"frame\":-1,\"order\":-1,\"pa\":", l );
    put_string( b, a->name() );
    b += ",\"ca\":";
    put_string( b, a->name() );
    b += ",\"ct\":";
    put_string( b, a->target->name() );
    b += ",\"lk\":\"swing\",\"sw\":null,\"sf\":[";
    // One entry per candidate whose hiding changes the swing time. f = the swing time with the buff hidden
    // divided by the real swing time (> 1 for a speed buff: without it the swing takes longer).
    bool first = true;
    if ( status == 0 || status == 2 )
    {
      for ( std::size_t i = 0; i < cands.size(); ++i )
      {
        if ( hidden_times[ i ] == real || real.total_millis() == 0 )
          continue;
        const buff_t* c = cands[ i ];
        if ( !first )
          b += ',';
        first = false;
        b += "{\"buff\":";
        put_string( b, c->name_str );
        b += ",\"owner\":";
        put_string( b, c->player->name() );
        fmt::format_to( out_it( b ), ",\"stacks\":{},\"kind\":\"{}\",\"app\":", c->current_stack,
                        dynamic_cast<const stat_buff_t*>( c ) != nullptr ? "stat" : "speed" );
        write_appliers( b, st, const_cast<buff_t*>( c ) );
        b += ",\"f\":";
        put_double( b, static_cast<double>( hidden_times[ i ].total_millis() ) /
                           static_cast<double>( real.total_millis() ) );
        b += '}';
      }
    }
    fmt::format_to( out_it( b ), "],\"status\":{},\"tx\":{}", status, real.total_millis() );
    if ( !guards.empty() )
    {
      b += ",\"guards\":[";
      b += guards;
      b += ']';
    }
    b += "}\n";
  } );
}

std::int32_t take_swing_launch( const action_t* a )
{
  state_t* st = state_of( a->sim );
  if ( st == nullptr || !st->in_fight )
    return -1;
  auto it = st->swing_slot.find( a );
  if ( it == st->swing_slot.end() )
    return -1;
  const std::int32_t l = it->second;
  st->swing_slot.erase( it );
  return l;
}

void note_swing_rescaled( action_t* a )
{
  state_t* st = state_of( a->sim );
  if ( st == nullptr || !st->in_fight || !belongs_to_actor( *st, a->player ) )
    return;
  ++st->swing_rescaled;
}

// ==========================================================================
// Plan 07: the refund ledger (cooldown cycles, refunds, uses, cooldown starts)
// ==========================================================================

namespace
{
// The cooldowns whose scope is open right now (outermost first): a scope opened for a cooldown that already has one
// (adjust -> reset) writes nothing, the outermost scope owns the change and names itself as its source.
std::vector<const cooldown_t*> g_cd_active;
// Plan 12 (MJ-05): the cooldowns whose set_max_charges scope is open: cooldown_t::set_max_charges restarts the cooldown `charges`
// times and moves its time by hand; none of that is a press, a use or a refund, so cd_started writes nothing for them.
std::vector<const cooldown_t*> g_cd_mc_active;

bool cd_in_mc_scope( const cooldown_t* cd )
{
  return !g_cd_mc_active.empty() && std::find( g_cd_mc_active.begin(), g_cd_mc_active.end(), cd ) != g_cd_mc_active.end();
}

bool cd_tracked( const state_t* st, const cooldown_t* cd )
{
  return st != nullptr && st->in_fight && st->cd_live && cd->charges >= 1 && cd->player != nullptr && belongs_to_actor( *st, cd->player );
}

// Time left in the cycle in progress, in milliseconds (0: none). The recharge event's remains for a multi-charge
// cooldown, ready - now otherwise.
std::int64_t cd_remaining_ms( const cooldown_t* cd )
{
  if ( cd->charges > 1 )
    return cd->recharge_event != nullptr ? cd->recharge_event->remains().total_millis() : 0;
  const timespan_t now = cd->sim.current_time();
  return cd->ready > now ? ( cd->ready - now ).total_millis() : 0;
}

// The cause on top of the cooldown owner's cause stack (the refunding press); an owner with no frame open falls back
// to the actor's stack, and none at all is an orphan with press -1 (refunds with no press cause refund nobody).
rl_cause_t cd_top_cause( const state_t* st, const cooldown_t* cd )
{
  if ( !cd->player->rl_cause_stack.empty() )
    return cd->player->rl_cause_stack.back().cause;
  if ( st->actor != nullptr && st->actor != cd->player && !st->actor->rl_cause_stack.empty() )
    return st->actor->rl_cause_stack.back().cause;
  return rl_cause_t{};
}

void cd_begin_record( std::string& o, const char* kind, const cooldown_t* cd )
{
  fmt::format_to( out_it( o ), "{{\"k\":\"{}\",\"it\":{},\"t\":", kind, cd->sim.current_iteration );
  put_double( o, cd->sim.current_time().total_seconds() );
}

double ms_to_seconds( std::int64_t ms )
{
  return static_cast<double>( ms ) / 1000.0;
}

std::int32_t cd_open_cycle( state_t* st, const cooldown_t* cd, std::int64_t len_ms, const action_t* a, bool recovered,
                            bool max_charge_change = false )
{
  const std::int32_t id = st->next_cycle++;
  ++st->cycles_written;
  if ( recovered )
    ++st->cd_recovered;
  emit( st, REC_CYC, [ & ]( std::string& o ) {
    cd_begin_record( o, "cyc", cd );
    fmt::format_to( out_it( o ), ",\"c\":{},\"cd\":", id );
    put_string( o, cd->name_str );
    o += ",\"actor\":";
    put_string( o, cd->player->name() );
    o += ",\"len\":";
    put_double( o, ms_to_seconds( len_ms ) );
    o += ",\"action\":";
    put_string( o, a != nullptr ? std::string( a->name() ) : std::string() );
    fmt::format_to( out_it( o ), ",\"ch\":{}", cd->charges > 1 ? cd->current_charge : 0 );
    if ( recovered )
      o += ",\"rec\":true";
    // Plan 12 (MJ-05): a recharge that runs only after a maximum-charges change opens a cycle of the remaining length.
    if ( max_charge_change )
      o += ",\"mc\":true";
    o += "}\n";
  } );
  return id;
}

void cd_write_ref( state_t* st, const cooldown_t* cd, std::int32_t c, std::int64_t ms, const char* src, bool nobody = false )
{
  // Plan 12 (MJ-05): a maximum-charges change refunds nobody (press -1, class 6).
  const rl_cause_t cause = nobody ? rl_cause_t{} : cd_top_cause( st, cd );
  ++st->refunds_written;
  emit( st, REC_REF, [ & ]( std::string& o ) {
    cd_begin_record( o, "ref", cd );
    fmt::format_to( out_it( o ), ",\"c\":{},\"sec\":", c );
    put_double( o, ms_to_seconds( ms ) );
    fmt::format_to( out_it( o ), ",\"press\":{},\"cls\":{},\"dk\":{},\"src\":\"{}\",\"seq\":{},\"launch\":{}}}\n", cause.press,
                    cls_out( cause.cls ), dk_out( cause.cls ), src, cause.seq, cause.launch );
  } );
}

void cd_write_use( state_t* st, const cooldown_t* cd, std::int32_t c )
{
  const rl_cause_t cause = cd_top_cause( st, cd );
  ++st->uses_written;
  emit( st, REC_USE, [ & ]( std::string& o ) {
    cd_begin_record( o, "use", cd );
    fmt::format_to( out_it( o ), ",\"c\":{},\"press\":{},\"cls\":{},\"dk\":{},\"seq\":{}}}\n", c, cause.press,
                    cls_out( cause.cls ), dk_out( cause.cls ), cause.seq );
  } );
}

void cd_write_cdn( state_t* st, const cooldown_t* cd, const action_t* a, bool ignored = false )
{
  const rl_cause_t cause = cd_top_cause( st, cd );
  ++st->cdn_written;
  emit( st, REC_CDN, [ & ]( std::string& o ) {
    cd_begin_record( o, "cdn", cd );
    o += ",\"cd\":";
    put_string( o, cd->name_str );
    o += ",\"action\":";
    put_string( o, a != nullptr ? std::string( a->name() ) : std::string() );
    fmt::format_to( out_it( o ), ",\"press\":{},\"cls\":{},\"dk\":{},\"seq\":{}", cause.press, cls_out( cause.cls ),
                    dk_out( cause.cls ), cause.seq );
    if ( ignored )
      o += ",\"ign\":true";
    o += "}\n";
  } );
}
}  // namespace

cd_snap_t cd_capture( const cooldown_t* cd )
{
  cd_snap_t s;
  s.up     = cd->up();
  s.cc     = cd->current_charge;
  s.ev     = cd->recharge_event != nullptr;
  s.rem_ms = cd_remaining_ms( cd );
  return s;
}

void cd_started( cooldown_t* cd, const cd_snap_t& before, const action_t* a )
{
  state_t* st = state_of( &cd->sim );
  if ( !cd_tracked( st, cd ) )
    return;
  if ( cd_in_mc_scope( cd ) )
    return;
  busy_scope_t busy;
  state_t::cd_state_t& cs = st->cds[ cd ];
  cd_write_cdn( st, cd, a );
  if ( cd->charges > 1 )
  {
    // A charge was consumed: the oldest one that came from a completed cycle, unless an initial charge (older than
    // every cycle of this fight) is still held.
    if ( before.cc <= static_cast<int>( cs.done.size() ) && !cs.done.empty() )
    {
      cd_write_use( st, cd, cs.done.front() );
      cs.done.erase( cs.done.begin() );
    }
    if ( cd->recharge_event != nullptr && ( !before.ev || cs.cur < 0 ) )
      cs.cur = cd_open_cycle( st, cd, cd_remaining_ms( cd ), a, /*recovered=*/before.ev );
  }
  else
  {
    // One charge: the cycle that ran before this cast produced the charge it uses, if the cooldown was up (a cooldown
    // restarted while it was still down abandons its cycle, nothing was produced).
    if ( before.up && cs.cur >= 0 )
      cd_write_use( st, cd, cs.cur );
    cs.cur = cd_open_cycle( st, cd, cd_remaining_ms( cd ), a, false );
  }

  // Plan 12 (MJ-05): the stage-0 charge probe, once per fight, for the first tracked cooldown with two or more charges. The latch is
  // set before the calls (set_max_charges starts the cooldown again, which comes back through here).
  if ( cd->sim.rl_buff_ledger_charge_probe && !st->charge_probe_done && cd->charges >= 2 )
  {
    st->charge_probe_done = true;
    ++st->charge_probe_fired;
    const int original = cd->charges;
    cd->set_max_charges( original + 1 );
    cd->set_max_charges( original );
  }
}

void cd_start_ignored( cooldown_t* cd, const action_t* a )
{
  state_t* st = state_of( &cd->sim );
  if ( !cd_tracked( st, cd ) )
    return;
  if ( cd_in_mc_scope( cd ) )
    return;
  busy_scope_t busy;
  cd_write_cdn( st, cd, a, /*ignored=*/true );
}

void cd_recharged( cooldown_t* cd )
{
  state_t* st = state_of( &cd->sim );
  if ( !cd_tracked( st, cd ) )
    return;
  busy_scope_t busy;
  state_t::cd_state_t& cs = st->cds[ cd ];
  if ( cs.cur >= 0 )
  {
    cs.done.push_back( cs.cur );
    cs.cur = -1;
  }
  if ( cd->recharge_event != nullptr )
    cs.cur = cd_open_cycle( st, cd, cd_remaining_ms( cd ), nullptr, false );
}

bool cd_probe_skip( cooldown_t* cd, const char* src, std::int64_t would_save_ms )
{
  if ( would_save_ms <= 0 )
    return false;
  state_t* st = state_of( &cd->sim );
  if ( st == nullptr || st->probe_action.empty() || !cd_tracked( st, cd ) )
    return false;
  // The seconds the call would take off the running recharge: never more than the recharge has left (0: nothing to shorten, not skipped).
  would_save_ms = std::min( would_save_ms, cd_remaining_ms( cd ) );
  if ( would_save_ms <= 0 )
    return false;
  // The outermost call owns the change (the ledger's own rule for its scopes): a shortening made from inside another call on the same
  // cooldown (adjust -> reset, adjust_base_duration -> adjust_remaining_duration) is part of that call and is decided by it.
  if ( std::find( g_cd_active.begin(), g_cd_active.end(), cd ) != g_cd_active.end() )
    return false;
  const rl_cause_t cause = cd_top_cause( st, cd );
  // Only a REAL press (>= 0) of the named action; a carried value (below -1000), the sentinels and "none" are not presses.
  if ( cause.press < 0 || static_cast<std::size_t>( cause.press ) >= st->press_is_probe.size() || st->press_is_probe[ cause.press ] == 0 )
    return false;

  busy_scope_t busy;
  ++st->probe_skipped;
  const double sec = ms_to_seconds( would_save_ms );
  st->probe_skipped_seconds += sec;
  auto& e = st->probe_by_cooldown[ cd->name_str ];
  ++e.first;
  e.second += sec;
  const auto it = st->cds.find( cd );
  emit( st, REC_RPS, [ & ]( std::string& o ) {
    cd_begin_record( o, "rps", cd );
    fmt::format_to( out_it( o ), ",\"c\":{},\"cd\":", it != st->cds.end() ? it->second.cur : -1 );
    put_string( o, cd->name_str );
    o += ",\"actor\":";
    put_string( o, cd->player->name() );
    o += ",\"sec\":";
    put_double( o, sec );
    fmt::format_to( out_it( o ), ",\"press\":{},\"cls\":{},\"dk\":{},\"src\":\"{}\"}}\n", cause.press, cls_out( cause.cls ),
                    dk_out( cause.cls ), src );
  } );
  return true;
}

bool refund_probe_report( const sim_t& sim, refund_probe_report_t& out )
{
  const sim_t* root = &sim;
  while ( root->parent )
    root = root->parent;
  const state_t* st = root->rl_bl_state.get();
  if ( st == nullptr || st->probe_action.empty() )
    return false;
  out.action       = st->probe_action;
  out.count        = st->probe_skipped;
  out.seconds      = st->probe_skipped_seconds;
  out.by_cooldown  = st->probe_by_cooldown;
  return true;
}

void cd_scope_t::begin( cooldown_t* cd, const char* src )
{
  // Plan 12 (NT-02): the members other than begun_ are set here, and only here.
  begun_      = true;
  cd_         = cd;
  nested_     = false;
  track_      = false;
  src_        = src;
  before_max_ = cd->charges;
  // Plan 13 (review MN-12-01): a set_max_charges scope marks its cooldown BEFORE the nested test, so it suppresses the restarts' records
  // (and the outer scope's "charges came back" refund) when it runs inside another open scope of the same cooldown too; end() pops it
  // for every scope of that source, nested or not.
  if ( std::strcmp( src, "set_max_charges" ) == 0 )
    g_cd_mc_active.push_back( cd );
  if ( std::find( g_cd_active.begin(), g_cd_active.end(), cd ) != g_cd_active.end() )
  {
    nested_ = true;
    return;
  }
  g_cd_active.push_back( cd );
  state_t* st = state_of( &cd->sim );
  if ( !cd_tracked( st, cd ) )
    return;
  track_  = true;
  before_ = cd_capture( cd );
}

void cd_scope_t::end()
{
  if ( std::strcmp( src_, "set_max_charges" ) == 0 && !g_cd_mc_active.empty() )
    g_cd_mc_active.pop_back();
  if ( nested_ )
    return;
  if ( !g_cd_active.empty() )
    g_cd_active.pop_back();
  if ( !track_ )
    return;
  state_t* st = state_of( &cd_->sim );
  if ( !cd_tracked( st, cd_ ) )
    return;
  busy_scope_t busy;
  state_t::cd_state_t& cs = st->cds[ cd_ ];
  const cd_snap_t after   = cd_capture( cd_ );
  const char* src         = src_;

  if ( std::strcmp( src, "set_max_charges" ) == 0 )
  {
    // Plan 12 (MJ-05): a maximum-charges change writes no press, use, cycle or refund of its own (cooldown_t::set_max_charges restarts
    // the cooldown once per charge and moves its time by hand). The ledger's bookkeeping of this cooldown is brought to the engine's
    // new state instead, without inventing records.
    ++st->cd_max_charge_changes;
    const bool multi_before = before_max_ > 1;
    const bool multi_after  = cd_->charges > 1;
    // Whether a recharge was running (single charge: the time left of the one cycle; several: the recharge event).
    const bool open_before = multi_before ? before_.ev : before_.rem_ms > 0;
    const bool open_after  = multi_after ? after.ev : after.rem_ms > 0;
    if ( !multi_before && multi_after )
    {
      // One charge -> several: a finished cycle that produced the held charge is now a held completed cycle.
      if ( cs.cur >= 0 && !open_before )
      {
        cs.done.assign( 1, cs.cur );
        cs.cur = -1;
      }
    }
    else if ( multi_before && !multi_after )
    {
      // Several -> one charge: the cooldown names only its last cycle; with a charge held that is the newest completed one.
      const std::int32_t newest = cs.done.empty() ? -1 : cs.done.back();
      cs.done.clear();
      if ( !open_after )
        cs.cur = open_before ? -1 : newest;
    }
    if ( multi_after )
    {
      while ( static_cast<int>( cs.done.size() ) > after.cc )
        cs.done.pop_back();
    }
    if ( open_before && open_after )
    {
      // A recharge that runs before and after keeps its cycle; a net change of its time is ONE refund that refunds nobody.
      if ( cs.cur < 0 )
        cs.cur = cd_open_cycle( st, cd_, before_.rem_ms, nullptr, /*recovered=*/true );
      const std::int64_t saved = before_.rem_ms - after.rem_ms;
      if ( saved != 0 )
        cd_write_ref( st, cd_, cs.cur, saved, "set_max_charges", /*nobody=*/true );
    }
    else if ( open_before && !open_after )
      cs.cur = -1;  // the recharge that ran before is abandoned
    else if ( !open_before && open_after )
      cs.cur = cd_open_cycle( st, cd_, after.rem_ms, nullptr, false, /*max_charge_change=*/true );
    return;
  }

  if ( cd_->charges <= 1 )
  {
    // The one cycle: the net change of its remaining time. A cooldown that was up has no cycle in progress.
    if ( before_.rem_ms <= 0 )
      return;
    if ( cs.cur < 0 )
      cs.cur = cd_open_cycle( st, cd_, before_.rem_ms, nullptr, /*recovered=*/true );
    const std::int64_t saved = before_.rem_ms - after.rem_ms;
    if ( saved != 0 )
      cd_write_ref( st, cd_, cs.cur, saved, ( std::strcmp( src, "adjust" ) == 0 && saved < 0 ) ? "delay" : src );
    return;
  }

  // Several charges: the recharge event serves one charge at a time.
  const int gained = after.cc - before_.cc;
  const std::int64_t len_ms = ( cd_->recharge_multiplier * cd_->base_duration ).total_millis();
  if ( gained <= 0 )
  {
    if ( !before_.ev || !after.ev || before_.rem_ms == after.rem_ms )
      return;
    if ( cs.cur < 0 )
      cs.cur = cd_open_cycle( st, cd_, before_.rem_ms, nullptr, /*recovered=*/true );
    const std::int64_t saved = before_.rem_ms - after.rem_ms;
    cd_write_ref( st, cd_, cs.cur, saved, ( std::strcmp( src, "adjust" ) == 0 && saved < 0 ) ? "delay" : src );
    return;
  }

  // Charges came back at once: the cycle in progress completes with what it had left, every further charge is a
  // cycle that never had to run (refunded in full), and a recharge that continues is a new cycle that opens already
  // partly elapsed (length - what it has left).
  int first_extra = 0;
  if ( before_.ev )
  {
    if ( cs.cur < 0 )
      cs.cur = cd_open_cycle( st, cd_, before_.rem_ms, nullptr, /*recovered=*/true );
    if ( before_.rem_ms != 0 )
      cd_write_ref( st, cd_, cs.cur, before_.rem_ms, src );
    cs.done.push_back( cs.cur );
    cs.cur      = -1;
    first_extra = 1;
  }
  for ( int i = first_extra; i < gained; ++i )
  {
    const std::int32_t id = cd_open_cycle( st, cd_, len_ms, nullptr, false );
    cd_write_ref( st, cd_, id, len_ms, src );
    cs.done.push_back( id );
  }
  if ( after.ev )
  {
    // Plan 12 (MN-05): a recharge that continues with more time left than its length (a delay came first) is a cycle of that longer
    // length with nothing refunded; a negative refund would be booked to the press that reset the charge.
    if ( after.rem_ms > len_ms )
    {
      ++st->cd_overlong_recharge;
      cs.cur = cd_open_cycle( st, cd_, after.rem_ms, nullptr, false );
    }
    else
    {
      const std::int32_t id = cd_open_cycle( st, cd_, len_ms, nullptr, false );
      const std::int64_t elapsed = len_ms - after.rem_ms;
      if ( elapsed != 0 )
        cd_write_ref( st, cd_, id, elapsed, src );
      cs.cur = id;
    }
  }
}

}  // namespace rl_buff_ledger

// Plan 06 note (kept at the end of the file so no line number above it moves): the engine's generic proc switch --
// dbc_proc_callback_t::activate_with_buff( buff ) turns a callback on while `buff` is up, deactivate_with_buff( buff )
// while it is not -- registers itself with switch_register() (action/dbc_proc_callback.cpp), and the detached execution of
// the callback (proc_event_t::execute) wraps cb->execute in a switch_scope_t, so the first launch the callback makes is
// written as a `switch` launch carrying that buff and its appliers (D-03: a gate that needs no test).
