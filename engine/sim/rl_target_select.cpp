// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "sim/rl_target_select.hpp"

#include "action/action.hpp"
#include "action/attack.hpp"
#include "action/dot.hpp"
#include "buff/buff.hpp"
#include "fmt/format.h"
#include "player/pet.hpp"
#include "player/player.hpp"
#include "sim/rl_class_plugins.hpp"
#include "sim/rl_policy.hpp"
#include "sim/rl_translog.hpp"
#include "sim/sheet_fight.hpp"  // tstl-sylvanas 264-05 (O4): sheet_fight_do_not_hit
#include "sim/sim.hpp"
#include "util/util.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <tuple>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rl_target_select
{
namespace
{

// The targeted registry tokens (the actions that get a per-spell pick) come from the spec header:
// RL_TARGETED_TOKENS / RL_TARGETED_TOKEN_COUNT, in the header's own order. Plan 268-02 moved the list
// out of this file. The compile-time check that every aimed spell is a targeted token now lives in
// rl_header_contract.hpp (it was the 259-05b check), so a spec header that violates it does not build.
//
// 230-02 (CK1-1, owner ruling Q1): the list is also the target head's own one-hot registry (240-05
// Task 2) -- run_target_head (below) walks this SAME list for its aiming-spell one-hot, so the
// shaped pair can never reach the head either.

// Per-player monotonic decision counter (228-02's own stamp -- see rl_target_select.hpp's header
// comment for why neither the handle cache's key nor solver_control's `seq` can serve this role).
std::unordered_map<const player_t*, std::uint64_t> g_decision_stamp;

// The per-decision pick table, keyed on the resolved action_t* -- the SAME pointer
// read_action_gate_bits's handle cache resolves and accept_cast later receives
// (solver_control.cpp's own comment: the two must resolve to the SAME action_t* by construction),
// so no id-based lookup is needed on the accept_cast side.
struct pick_slot
{
  player_t*     pick      = nullptr;
  std::uint64_t stamp     = 0;
  bool          has_stamp = false;
};
std::unordered_map<const action_t*, pick_slot> g_pick_table;

// 230-04 (SCOR-02, R-B): the per-decision candidate block table, keyed on the resolved
// action_t* exactly like g_pick_table above (candidate_block::features "the facts the scorer
// looked at", R-B's own wording). 233.1-03 (R6-13, OV-5): select() now fills it on EVERY
// preference, not only the scorer's -- the rules path fills it from its own full build_enemy_fact
// per candidate (never the lite fact it scores the decision with), so scorer_block_equivalence.py
// has a real, non-vacuous row to compare on a rules-only corpus. `features` is reused across calls
// (never reallocated once sized), same WR-05 discipline as g_candidate_buffer below.
struct candidate_block_slot
{
  std::vector<float> features;
  std::uint16_t       mask        = 0;
  std::uint8_t         count       = 0;
  std::uint8_t         chosen_slot = rl_translog::CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK;
  std::uint64_t         stamp       = 0;
  bool                   has_stamp   = false;
  // 259-07 (R5): the rules' own pick and the slot the observation described -- see
  // candidate_block's own doc comment in rl_target_select.hpp. Set by select() (both = the
  // rules' pick); run_target_head moves `observed_slot` only when the observation follows the head.
  std::uint8_t           rules_slot    = rl_translog::CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK;
  std::uint8_t           observed_slot = rl_translog::CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK;

  // 240-05 (D1(a)): parallel per-slot actor identity, captured in LOCKSTEP with `features` above
  // by select()'s own per-candidate loop -- zero extra walk. Deliberately NOT part of the public
  // candidate_block struct or the transition log (an identity number as a scoring INPUT is
  // excluded from `features` for exactly the reason fill_candidate_features's own comment states
  // for the feature array itself: it would make the score depend on enumeration order). Read ONLY
  // by run_target_head (below), which needs the real player_t* both for the existing tie ladder's
  // final (actor_index, actor_spawn_index) rung and to hand a real candidate to apply_head_pick.
  std::vector<player_t*> actors;
};
std::unordered_map<const action_t*, candidate_block_slot> g_candidate_block_table;

// 232-04 (OBS-02, R-T): the pre-cast snapshot table -- captures, per targeted action's CURRENT
// decision, the is_current_target value `build_obs()`'s own REAL-decision
// call (solver_control.cpp, `rl_state_t::is_decision_boundary == true`) computed BEFORE
// `accept_cast`'s retarget/turn can mutate `p->target`/facing. `decision_dump.cpp`'s own later
// diagnostic `build_obs()` call for the SAME decision (`is_decision_boundary == false`, that
// file's own CR-02 comment: `record()` always runs strictly after `choose()`) reads this snapshot
// back instead of recomputing against state the cast already mutated -- the
// `action_gate_dump_time_compute` precedent, applied to per-target facts. Keyed on the resolved
// `action_t*` exactly like `g_pick_table`/`g_candidate_block_table` above, same `has_stamp`/
// `stamp` read-back discipline (T-232-15).
// 260923-lrc (PLAN.md D11): has_hit_damage/hit_damage REMOVED -- they served the eight now-
// deleted action_leaves.*.hit_damage census leaves (R7-4: the live addon can never read a
// damage amount); stamp_target_fact_hit_damage() is REMOVED alongside them.
struct target_fact_snapshot_slot
{
  bool          has_is_current_target = false;
  bool          is_current_target     = false;
  std::uint64_t stamp                 = 0;
  bool          has_stamp             = false;
};
std::unordered_map<const action_t*, target_fact_snapshot_slot> g_target_fact_snapshot_table;

// RULE-02 (232-06, R-Z): the per-decision Chain Lightning hop-count stash. `select()` fills this
// ONCE per decision, before the scoring loop calls `preference_chain_lightning` once per candidate
// (preference_fn is one-candidate-at-a-time by design -- the geometry must be precomputed and
// stashed here for that function to read). Keyed on the resolved action_t* like g_pick_table /
// g_candidate_block_table / g_target_fact_snapshot_table above; `used_fallback` is reset to false
// every time this action's slot is (re)computed and set true the instant
// `preference_chain_lightning` cannot find an entry for the candidate it was asked to score -- read
// back by `chain_hop_fallback_used()` so a fallback is visible on the dump row, never silent.
struct chain_hop_slot
{
  std::unordered_map<const player_t*, int> hop_counts;
  std::uint64_t stamp         = 0;
  bool          has_stamp     = false;
  bool          used_fallback = false;
};
std::unordered_map<const action_t*, chain_hop_slot> g_chain_hop_stash;

// D-14's re-resolution ladder counters -- one fight's totals (WR-11, 260902/cr4: cleared every
// iteration by reset( sim ) below, called from sim_t::reset()).
reresolution_counts g_reresolution_counts;

// CR-04 (260902/cr4): decisions where every one of the RL-controlled actor's registry actions
// read not-ready -- the deadlock census. Same one-fight-totals lifetime as the counters above.
std::uint64_t g_every_targeted_action_illegal = 0;

// 240-05 (must_haves): run_target_head calls that found no usable candidate block for their
// decision -- never a throw, never a truncation, the skip is COUNTED so the fallback is
// measurable. Same one-fight-totals lifetime as the counters above.
std::uint64_t g_target_head_no_block_count = 0;

// WR-05 (260902/cr4): a file-static candidate buffer reused across every select() call, cleared
// (not reallocated) per call -- same single-thread precondition begin_decision's own assert
// states (a second concurrent select() on this buffer would race). Avoids a fresh heap allocation
// on every targeted action's every decision.
std::vector<player_t*> g_candidate_buffer;
// tstl-sylvanas 264-05 (O4): the candidate set before the do-not-hit pass, kept only on a decision where the pass removed someone.
std::vector<player_t*> g_unpruned_candidate_buffer;

// 233.1-03 (R6-13, OV-5): the rules path's own candidate-feature scratch, beside g_candidate_buffer
// above -- `w.scorer.feature_scratch` does NOT exist when `has_scorer == false` (no weights blob
// loaded), so reusing it on the rules path would be a null deref. Sized once to RL_TARGET_FEATURES
// and reused across every select() call and every candidate within a call, mirroring
// g_candidate_buffer's own reuse discipline (never reallocated once sized).
std::vector<float> g_rules_feature_scratch;

// 260914-rbp Task 2c (ME-1 + ADD-2): keyed on (sim_t*, player_t*), not a bare `player_t*` -- a
// bare-pointer key can collide across sim instances (a destroyed sim_t's player_t address reused
// by a later sim_t's allocator, e.g. under a profileset sweep or threads>1), silently returning a
// PRIOR sim's geometry for an unrelated actor. Pairing with the owning sim_t's own address does
// not eliminate every theoretical reuse (sim_t* itself is also a bare pointer), but it removes the
// single-dimension collision this cache had before, and the cache is cleared every iteration via
// reset( sim_t* ) below regardless -- the two together are the "key by (sim, player)" alternative
// this task's plan names alongside the module's existing multi_sim_or_multi_thread degrade.
struct actor_cache_key_t
{
  const sim_t*    sim    = nullptr;
  const player_t* player = nullptr;
  bool operator==( const actor_cache_key_t& o ) const { return sim == o.sim && player == o.player; }
};
struct actor_cache_key_hash_t
{
  std::size_t operator()( const actor_cache_key_t& k ) const noexcept
  {
    return std::hash<const void*>()( k.sim ) ^ ( std::hash<const void*>()( k.player ) << 1 );
  }
};

// 260914-rbp Task 1 (R9/R15): per-actor cache backing resolve_vb_lava_lash_geometry() below --
// resolved at most once per actor (ACT-02 pattern), never once per candidate or once per
// decision, since none of these values change during a fight. Cleared every iteration by
// reset( sim_t* ) below (ME-1, Task 2c) -- see actor_cache_key_t's own comment for the keying.
std::unordered_map<actor_cache_key_t, vb_lava_lash_geometry_t, actor_cache_key_hash_t>
    g_vb_lava_lash_geometry_cache;

} // anonymous namespace

// 260914-rbp Task 1 (R5, HIT-INPUTS-DESIGN.md ss2.1): see rl_target_select.hpp's own declaration
// comment for the full contract. Defined here (public linkage, outside the anonymous namespace
// above) specifically so rl_policy_obs.cpp -- a DIFFERENT translation unit -- can call it for the
// hits.chain_lightning direct_id scalar; g_chain_hop_stash/g_decision_stamp themselves stay
// internal-linkage module state, read only through this accessor and chain_hop_fallback_used's
// own sibling accessor below.
int chain_hop_count_for_start( const action_t* resolved, const player_t* start )
{
  if ( resolved == nullptr || start == nullptr )
    return 0;
  auto slot_it = g_chain_hop_stash.find( resolved );
  if ( slot_it == g_chain_hop_stash.end() || !slot_it->second.has_stamp )
    return 0;
  auto stamp_it = g_decision_stamp.find( resolved->player );
  std::uint64_t current_stamp = ( stamp_it == g_decision_stamp.end() ) ? 0 : stamp_it->second;
  if ( slot_it->second.stamp != current_stamp )
    return 0;
  auto hop_it = slot_it->second.hop_counts.find( start );
  if ( hop_it == slot_it->second.hop_counts.end() )
    return 0;
  return hop_it->second;
}

// 260914-rbp Task 1 (R9/R15): see rl_target_select.hpp's own declaration comment. Public linkage
// for the same cross-translation-unit reason as chain_hop_count_for_start above --
// rl_policy_obs.cpp's hits.voltaic_blaze.*/hits.lava_lash.flame_shock_spread scalars need the
// SAME resolved radius/cap this file's own per-candidate enemy_fact fields use.
const vb_lava_lash_geometry_t& resolve_vb_lava_lash_geometry( player_t* p )
{
  const actor_cache_key_t key{ p->sim, p };
  auto found = g_vb_lava_lash_geometry_cache.find( key );
  if ( found != g_vb_lava_lash_geometry_cache.end() )
    return found->second;

  vb_lava_lash_geometry_t geo;
  if ( action_t* vb_damage = p->find_action( "voltaic_blaze_damage" ) )
  {
    // R13 (this task, sc_shaman.cpp): sets this child action's own `radius` to 10.0 -- read here
    // BY NAME (the resolved field), never a bare literal. `aoe` is already set by hand at this
    // action's own ctor (1 + Voltaic Blaze's effectN(4), HIT-INPUTS-DESIGN.md ss2.2(a)).
    geo.vb_radius = vb_damage->radius;
    geo.vb_cap    = vb_damage->aoe > 0 ? vb_damage->aoe : 0;
  }
  if ( action_t* lava_lash = p->find_action( "lava_lash" ) )
    geo.lava_lash_radius = lava_lash->radius;
  // player_t::find_talent_spell -- the SAME generic lookup player_t::create_expression's own
  // "talent.<name>" parsing uses (player.cpp) -- resolved here rather than through an
  // expr_t/create_expression round trip since this is a plain spell-data read, not an APL
  // arithmetic string (RESEARCH SS B3's own reasoning against expression strings for a derived
  // fork scalar, applied here to a talent lookup instead of a buff/cooldown arithmetic one).
  player_talent_t molten_assault =
      p->find_talent_spell( talent_tree::SPECIALIZATION, "molten_assault", p->specialization(), true );
  if ( molten_assault.ok() )
    geo.lava_lash_cap = static_cast<int>( molten_assault->effectN( 2 ).base_value() );

  return g_vb_lava_lash_geometry_cache.emplace( key, geo ).first->second;
}

int count_hits_within_radius( player_t* caster, player_t* candidate, double radius )
{
  if ( radius <= 0.0 )
    return 0;
  int count = 0;
  for ( player_t* other : caster->sim->target_non_sleeping_list )
  {
    if ( other == candidate || !other->is_enemy() || !rl_counts_as_enemy( other ) )
      continue;
    if ( candidate->get_player_distance( *other ) <= radius + other->combat_reach )
      ++count;
  }
  // Task 2b (Q17 review, A2 -- HIT-INPUTS-DESIGN.md ss2.0 lines 315-316): the candidate itself
  // is always hit -- every caller of this function is a "hits its centre" shape (cleave/splash).
  return 1 + count;
}

int count_new_flame_shock_neighbours( player_t* caster, player_t* candidate, double radius, int cap,
                                       bool include_pick )
{
  if ( radius <= 0.0 || cap <= 0 )
    return 0;
  int count = 0;
  for ( player_t* other : caster->sim->target_non_sleeping_list )
  {
    if ( other == candidate || !other->is_enemy() || !rl_counts_as_enemy( other ) )
      continue;
    if ( candidate->get_player_distance( *other ) > radius + other->combat_reach )
      continue;
    // Q6 (rulings, HIT-INPUTS-DESIGN.md ss2.5): caster-filtered -- THIS actor's own Flame Shock,
    // the SAME find_dot("flame_shock", caster) idiom build_enemy_fact's own flame_shock_remaining
    // field already uses (WR-04, non-allocating).
    dot_t* fs = other->find_dot( "flame_shock", caster );
    if ( fs && fs->is_ticking() )
      continue;
    ++count;
  }
  // Task 2b (Q17 review, A2): Voltaic Blaze's cleave always hits (and can Flame-Shock) its own
  // pick -- `include_pick` adds that +1 (before the cap, same as every neighbour) when the pick
  // itself still lacks this caster's Flame Shock. Lava Lash's spread passes include_pick=false:
  // its pick is the SOURCE carrier casting Lava Lash, never a spread target (deliberately
  // UNCHANGED convention).
  if ( include_pick )
  {
    dot_t* pick_fs = candidate->find_dot( "flame_shock", caster );
    if ( !pick_fs || !pick_fs->is_ticking() )
      ++count;
  }
  return std::min( cap, count );
}

bool rl_counts_as_enemy( const player_t* t )
{
  if ( t->sheet_hazard )
    return false;
  if ( t->sim->fight_style == FIGHT_STYLE_SHEET_FIGHT && t->debuffs.invulnerable && t->debuffs.invulnerable->check() )
    return false;
  return true;
}

bool generic_filter( const action_t* a, player_t* candidate, bool harmful )
{
  // G1 -- alive.
  if ( candidate->is_sleeping() )
    return false;

  // G1b -- tstl-sylvanas 261-03: never a hazard.
  if ( candidate->sheet_hazard )
    return false;

  // G2 -- not immune while harmful.
  if ( harmful && candidate->debuffs.invulnerable && candidate->debuffs.invulnerable->check() )
    return false;

  // G3 -- within THIS action's own range, read per candidate's own combat_reach (P-7: 4.0 for a
  // boss, 1.0 for an add -- never one shared number). Skipped identically to target_ready's own
  // reach clause when distance targeting is off or the action has no positive range.
  if ( a->sim->distance_targeting_enabled && a->range > 0 &&
       a->player->get_player_distance( *candidate ) > a->range + candidate->combat_reach )
    return false;

  // G4 RESTORED (R6-8, owner, 2026-09-07): "no turning allowed ... the target picker will ever
  // only suggest targets that are in front and in range." facing_enabled's meaning REVERTS to its
  // pre-cr4 meaning -- "a behind target is refused" -- not "the player turns to face what it
  // casts at" (that turn mechanism is deleted at every site this phase, 233.1-01). A behind
  // candidate is therefore excluded from the selector's own candidate set again; there is no
  // later turn that would have made it legal by cast time. "In front" is otherwise still also
  // available as the RAW, non-gating `enemy_fact::in_front` field below (observation ground
  // truth, D-16) and inside the two SHAPED actions' own cone/rectangle geometry (OR-2, unaffected
  // -- they never turned). 233.1 pre-rebuild review ME-2 (orchestrator, 2026-09-08): the gate is
  // the pre-cr4 predicate LITERALLY -- `sim->facing_enabled && harmful && range >= 0` -- so this
  // clause and action_t::target_ready's fourth clause never disagree about what counts as
  // reachable (rl_target_select.hpp's own invariant, T-228-02-01), and facing_enabled=0 still
  // means "the stock engine, no facing model" on BOTH arms (sim.hpp's doc, D-24 overlay pair).
  // The rig sets facing_enabled=1 on every episode and every bar, so this changes no measurement.
  if ( a->sim->facing_enabled && harmful && a->range >= 0 &&
       !a->player->is_in_front( *candidate, 0.0 ) )
    return false;

  return true;
}

// ---- tstl-sylvanas 264-05 (O4) + 261006-dnh-F1: the sheet-declared do-not-hit rule (STRICT) --------------------------------
// A boss the sheet declares do-not-hit (for a phase or a window; see sheet_fight_do_not_hit) is NEVER a candidate for a
// single-target AIMED press while the declaration is active -- even when it is the only enemy an aimed spell can reach, and even
// when every candidate is do-not-hit (owner ruling 2026-10-06: "we should also mask immune targets as eligible, so we cant ever
// choose them as targets", and "Drop him too" for the lone case; this reverses O4's old keep-when-alone exception). When the drop
// empties the set, select() returns nullptr: the aimed button's legality bit goes to 0, exactly as for any other empty set.
// Keyed on the declaration only, so it covers every aimed token whatever RL_TARGETED_TOKENS holds (the eight at width 325, the ninth
// after the talent build). generic_filter, rl_counts_as_enemy and every enemy count are untouched, an area spell still reaches the
// boss, and the rule never looks at a shield. It reads the controller's state and draws nothing. With no entry (every non-sheet
// fight, a /1 spec, a /2 spec without an entry, or solver_sheet_do_not_hit=0) none of the functions below changes anything.

// Fills `kept` with the members of `set` that are not do-not-hit (same order; possibly none) and returns true, only when the rule
// fires: at least one member is do-not-hit. Counts nothing.
static bool dnh_split( const sim_t* sim, const std::vector<player_t*>& set, std::vector<player_t*>& kept )
{
  if ( sim->fight_style != FIGHT_STYLE_SHEET_FIGHT || sim->sheet_fight == nullptr )
    return false;
  bool any = false;
  for ( const player_t* t : set )
    if ( sheet_fight_do_not_hit( sim, t ) )
    {
      any = true;
      break;
    }
  if ( !any )
    return false;
  kept.clear();
  for ( player_t* t : set )
    if ( !sheet_fight_do_not_hit( sim, t ) )
      kept.push_back( t );
  return true;
}

// select()'s own pass (counts for the record): every do-not-hit candidate is removed (one drop counted per removed candidate per
// active entry; a drop that leaves the set empty is also counted as a sole_dropped). When it prunes, the unpruned set is left in
// `unpruned` (the Chain Lightning hop walk keeps reading every candidate, because a chain still reaches a do-not-hit boss).
static bool dnh_prune_candidates( const sim_t* sim, std::vector<player_t*>& set, std::vector<player_t*>& unpruned )
{
  if ( sim->fight_style != FIGHT_STYLE_SHEET_FIGHT || sim->sheet_fight == nullptr )
    return false;
  std::size_t n = 0;
  for ( const player_t* t : set )
    if ( sheet_fight_do_not_hit( sim, t ) )
      ++n;
  if ( n == 0 )
    return false;
  const bool empties = ( n == set.size() );
  unpruned = set;
  set.clear();
  for ( player_t* t : unpruned )
  {
    if ( sheet_fight_do_not_hit( sim, t ) )
      sheet_fight_note_do_not_hit( sim, t, empties );
    else
      set.push_back( t );
  }
  return true;
}

// ---- Phase 266 (plan 266-01, funnel mode, owner F9): the chosen enemy (the tag) and its chooser ----

player_t* rl_chosen_enemy_of( const player_t* p )
{
  if ( p == nullptr )
    return nullptr;
  // A pet or guardian (an ancestor totem, a Feral Spirit wolf) answers with its owner's tag. Walk up so a
  // pet of a pet still resolves; the RL actor itself is not a pet.
  while ( p->is_pet() )
  {
    const player_t* owner = p->cast_pet()->owner;
    if ( owner == nullptr )
      return nullptr;
    p = owner;
  }
  return p->rl_chosen_enemy;
}

namespace
{

// Phase 268 plan 02 (G268-4, G268-5): everything the chooser and the aiming rules read from the spec
// header, resolved ONCE per actor at bind time (the actor's arise, through refresh_chosen) and read per
// decision without name compares or find_action walks. A bad header (a missing probe action, an unknown
// rule name) is refused right there with the registry id in the message, never at a decision.
using rule_row_t = std::pair<std::string, rule_resolver_fn>;

// Phase 268 plan 03 (G268-6, FORK-03; research Pitfall 6): where each of the RL_TARGET_FEATURES per-candidate facts comes from,
// decided ONCE per actor at bind time for every index of RL_TARGET_FEATURE_NAMES. A fact is one of three kinds: one of the 12
// generic facts of enemy_fact, a declared fact (a row of RL_DECLARED_FACTS whose slot is this index), or a per-class geometry
// fact the class plugin serves by name. A name none of the three can serve is refused at bind. The candidate block is then
// filled BY INDEX from this table, never by expanding a list of C++ member names, so a spec whose fact names are not enemy_fact
// members still compiles.
enum class generic_fact_id : std::uint8_t
{
  distance, in_reach, in_range, in_front, alive, immune, immunity_remaining, time_to_die, health_pct, is_boss,
  neighbours_within_radius, is_current_target
};

enum class fact_source_kind : std::uint8_t { generic, declared, geometry };

struct feature_source_t
{
  fact_source_kind kind    = fact_source_kind::generic;
  generic_fact_id  generic = generic_fact_id::distance;  // kind == generic
  std::size_t      declared = 0;                         // kind == declared: the index into RL_DECLARED_FACTS
  double ( *geometry )( const enemy_fact& ) = nullptr;   // kind == geometry
};

using feature_sources_t = std::array<feature_source_t, RL_TARGET_FEATURES>;

// The 12 generic facts, by the name they carry in RL_TARGET_FEATURE_NAMES. Returns false for any other name.
bool generic_fact_by_name( const char* name, generic_fact_id& out )
{
  struct row { const char* name; generic_fact_id id; };
  static const row ROWS[] = {
    { "distance", generic_fact_id::distance },
    { "in_reach", generic_fact_id::in_reach },
    { "in_range", generic_fact_id::in_range },
    { "in_front", generic_fact_id::in_front },
    { "alive", generic_fact_id::alive },
    { "immune", generic_fact_id::immune },
    { "immunity_remaining", generic_fact_id::immunity_remaining },
    { "time_to_die", generic_fact_id::time_to_die },
    { "health_pct", generic_fact_id::health_pct },
    { "is_boss", generic_fact_id::is_boss },
    { "neighbours_within_radius", generic_fact_id::neighbours_within_radius },
    { "is_current_target", generic_fact_id::is_current_target },
  };
  for ( const row& r : ROWS )
    if ( std::strcmp( r.name, name ) == 0 )
    {
      out = r.id;
      return true;
    }
  return false;
}

// The generic fact as a double: a bool or an int widens exactly (so the later cast to float gives the float the old direct
// cast gave), a double is read as it is.
double generic_fact_value( const enemy_fact& f, generic_fact_id id )
{
  switch ( id )
  {
    case generic_fact_id::distance:                 return f.distance;
    case generic_fact_id::in_reach:                 return f.in_reach ? 1.0 : 0.0;
    case generic_fact_id::in_range:                 return f.in_range ? 1.0 : 0.0;
    case generic_fact_id::in_front:                 return f.in_front ? 1.0 : 0.0;
    case generic_fact_id::alive:                    return f.alive ? 1.0 : 0.0;
    case generic_fact_id::immune:                   return f.immune ? 1.0 : 0.0;
    case generic_fact_id::immunity_remaining:       return f.immunity_remaining;
    case generic_fact_id::time_to_die:              return f.time_to_die;
    case generic_fact_id::health_pct:               return f.health_pct;
    case generic_fact_id::is_boss:                  return f.is_boss ? 1.0 : 0.0;
    case generic_fact_id::neighbours_within_radius: return static_cast<double>( f.neighbours_within_radius );
    case generic_fact_id::is_current_target:        return f.is_current_target ? 1.0 : 0.0;
  }
  return 0.0;
}

feature_sources_t build_feature_sources( const player_t* p )
{
  const rl_class_plugin& plugin = rl_class_plugin_for( p );
  feature_sources_t      out;
  for ( std::size_t i = 0; i < RL_TARGET_FEATURES; ++i )
  {
    feature_source_t src;
    bool             resolved = false;
    for ( std::size_t k = 0; k < RL_DECLARED_FACT_COUNT && !resolved; ++k )
      if ( RL_DECLARED_FACTS[ k ].slot == i )
      {
        src.kind     = fact_source_kind::declared;
        src.declared = k;
        resolved     = true;
      }
    if ( !resolved && generic_fact_by_name( RL_TARGET_FEATURE_NAMES[ i ], src.generic ) )
    {
      src.kind = fact_source_kind::generic;
      resolved = true;
    }
    if ( !resolved )
    {
      if ( const rl_geometry_fact* g = rl_class_geometry_find( plugin, RL_TARGET_FEATURE_NAMES[ i ] ) )
      {
        src.kind     = fact_source_kind::geometry;
        src.geometry = g->get;
        resolved     = true;
      }
    }
    if ( !resolved )
    {
      throw sc_runtime_error( fmt::format(
          "rl_target_select: registry '{}' names per-candidate feature '{}' (index {}) that is neither a generic fact, a declared "
          "fact nor a geometry fact of player '{}' (class plugin '{}') (bind-time refusal)",
          RL_REGISTRY_ID, RL_TARGET_FEATURE_NAMES[ i ], i, p->name(), plugin.class_name ) );
    }
    out[ i ] = src;
  }
  return out;
}

struct actor_binding_t
{
  const action_t*                                       probe = nullptr;  // RL_CHOOSER_PROBE_ACTION, never null once bound
  const action_t*                                       melee = nullptr;  // RL_CHOOSER_MELEE_ACTION, null when the actor has none
  std::vector<rule_row_t>                               rule_rows;        // RL_RULE_PREFS: token -> resolver
  std::unordered_map<const action_t*, rule_resolver_fn> by_action;        // filled on the first decision of each action
  feature_sources_t                                     feature_sources;  // the per-candidate fact source of each feature index
};

std::unordered_map<const player_t*, actor_binding_t> g_actor_binding_cache;

// The generic rules, available to every class. current_target scores every candidate the same, so the
// tie ladder in candidate_is_better (the player's current target first, then the stable actor order)
// decides: the pick is the player's current target whenever it is a legal candidate.
preference_fn resolve_generic_current_target( const action_t* )
{
  return preference_current_target;
}

preference_fn resolve_generic_shortest_ttd( const action_t* )
{
  return preference_shortest_time_to_die;
}

rule_resolver_fn resolve_rule_name( const player_t* p, const char* token, const char* rule )
{
  if ( std::strcmp( rule, "current_target" ) == 0 )
    return resolve_generic_current_target;
  if ( std::strcmp( rule, "shortest_ttd" ) == 0 )
    return resolve_generic_shortest_ttd;
  const rl_class_plugin& plugin   = rl_class_plugin_for( p );
  rule_resolver_fn       resolver = rl_class_rule_find( plugin, rule );
  if ( resolver == nullptr )
  {
    throw sc_runtime_error( fmt::format(
        "rl_target_select: registry '{}' names aiming rule '{}' for token '{}' but player '{}' (class plugin '{}') "
        "has no such rule (bind-time refusal)",
        RL_REGISTRY_ID, rule, token, p->name(), plugin.class_name ) );
  }
  return resolver;
}

// What the per-actor binding cache holds, for process_cache_entries() (Pitfall 11 of the Phase 268 research).
std::size_t actor_binding_entries()
{
  std::size_t n = g_actor_binding_cache.size();
  for ( const auto& kv : g_actor_binding_cache )
    n += kv.second.by_action.size();
  return n;
}

actor_binding_t& actor_binding_for( const player_t* p )
{
  auto it = g_actor_binding_cache.find( p );
  if ( it != g_actor_binding_cache.end() )
    return it->second;

  actor_binding_t b;
  b.probe = p->find_action( RL_CHOOSER_PROBE_ACTION );
  if ( b.probe == nullptr )
  {
    throw sc_runtime_error( fmt::format(
        "rl_target_select: registry '{}' names chooser probe action '{}' but player '{}' has no such action "
        "(bind-time refusal)",
        RL_REGISTRY_ID, RL_CHOOSER_PROBE_ACTION, p->name() ) );
  }
  // A missing melee action is tolerated: the chooser then falls back to the hittable list.
  b.melee = p->find_action( RL_CHOOSER_MELEE_ACTION );
  b.feature_sources = build_feature_sources( p );  // refuses a feature no source can serve, here, at arise
  b.rule_rows.reserve( RL_RULE_PREF_COUNT );
  for ( std::size_t k = 0; k < RL_RULE_PREF_COUNT; ++k )
    b.rule_rows.emplace_back( RL_RULE_PREFS[ k ].token, resolve_rule_name( p, RL_RULE_PREFS[ k ].token, RL_RULE_PREFS[ k ].rule ) );
  return g_actor_binding_cache.emplace( p, std::move( b ) ).first->second;
}

}  // namespace

bool rl_can_be_hit( const player_t* p, const player_t* t )
{
  if ( t == nullptr || !rl_counts_as_enemy( t ) )
    return false;
  if ( t->is_sleeping() )
    return false;
  if ( t->debuffs.invulnerable && t->debuffs.invulnerable->check() )
    return false;
  if ( t->sim->is_untargetable_enemy( t ) )
    return false;

  const action_t* probe = actor_binding_for( p ).probe;
  // generic_filter takes a non-const candidate (it is the same function the per-spell selector calls);
  // it only reads from it.
  return generic_filter( probe, const_cast<player_t*>( t ), /*harmful=*/true );
}

// 2026-10-02, 266-09 (research R5, owner F9): in a funnel-mode fight the RL actor's own target and both weapon
// swings sit on the tag. Writes exactly what retarget() writes (the player's target and the two swings, never
// any other action), so the 261-09 dead-swing guard's invariant (swings stay on the player's target) holds.
// With the flag off this does nothing at all (R8).
static void pin_to_tag( player_t* p )
{
  if ( !p->sim->solver_funnel_mode )
    return;
  player_t* tag = p->rl_chosen_enemy;
  if ( tag == nullptr )
    return;
  if ( p->target != tag )
    p->target = tag;
  if ( p->main_hand_attack && p->main_hand_attack->target != tag )
    p->main_hand_attack->set_target( tag );
  if ( p->off_hand_attack && p->off_hand_attack->target != tag )
    p->off_hand_attack->set_target( tag );
}

void refresh_chosen( player_t* p )
{
  // The chooser is entered only by the RL actor. Refuse by name any other player, in
  // decision_dump.cpp:1408's own refusing form, so a call site whose RL-actor gate is missing or wrong
  // aborts the fight (the identity fight has pets) instead of silently tagging for a pet or another
  // class's player. The test is the fork's one RL-actor test (solver_control.cpp:144): exact name, never
  // a prefix, never a pet (CR-05: rl_target_select.hpp:360-372).
  if ( std::strcmp( p->name(), RL_ACTOR_NAME ) != 0 || p->is_pet() )
  {
    throw sc_runtime_error( fmt::format(
        "rl_target_select::refresh_chosen: player '{}' is not the RL actor ('{}') -- the chosen enemy is "
        "picked for the RL actor only, and every call site must carry the RL-actor gate itself",
        p->name(), RL_ACTOR_NAME ) );
  }

  // Bind this actor's header-driven actions and aiming rules now (its arise), so a header that names a
  // probe action the actor lacks, or a rule no class plugin knows, is refused before any decision.
  const actor_binding_t& binding = actor_binding_for( p );

  // Keep the tag while it can still be hit.
  // tstl-sylvanas 264-05 (O4): ... unless the sheet declares it do-not-hit right now and another enemy that can be hit is not
  // do-not-hit; then the tag is dropped here and re-picked below.
  bool dnh_tag_must_move = false;
  if ( p->rl_chosen_enemy != nullptr && sheet_fight_do_not_hit( p->sim, p->rl_chosen_enemy ) &&
       rl_can_be_hit( p, p->rl_chosen_enemy ) )
  {
    for ( player_t* t : p->sim->target_non_sleeping_list )
      if ( t->is_enemy() && t != p->rl_chosen_enemy && !sheet_fight_do_not_hit( p->sim, t ) && rl_can_be_hit( p, t ) )
      {
        dnh_tag_must_move = true;
        break;
      }
  }
  if ( !dnh_tag_must_move && p->rl_chosen_enemy != nullptr && rl_can_be_hit( p, p->rl_chosen_enemy ) )
  {
    pin_to_tag( p );  // 266-09 (R5): flag on only; keeps target and swings on a tag that can still be hit
    return;
  }

  // Otherwise pick among the enemies that can be hit AND are legal for this player's Stormstrike; failing
  // that, among those that can be hit at all (R3). With the random option off the pick is the first in the
  // list order. With it on (266-09, R7) and two or more eligible, the pick is uniform at random, one draw
  // from the chooser's own per-fight stream (solver_target_rng): never the exploration stream, never the
  // engine's. With fewer than two eligible there is no draw. It acts whatever the funnel flag is.
  const action_t*        melee = binding.melee;
  std::vector<player_t*> hittable;
  std::vector<player_t*> melee_legal;
  for ( player_t* t : p->sim->target_non_sleeping_list )
  {
    if ( !t->is_enemy() || !rl_can_be_hit( p, t ) )
      continue;
    hittable.push_back( t );
    if ( melee != nullptr && generic_filter( melee, t, /*harmful=*/true ) )
      melee_legal.push_back( t );
  }
  // tstl-sylvanas 264-05 (O4) + 261006-dnh-F1 (strict): a do-not-hit enemy is never re-picked as the tag. The Stormstrike-legal list
  // is stripped first; when nothing is left of it, the hittable list is stripped instead (R3's fallback, still without a do-not-hit
  // boss). With nothing else hittable the pick is nullptr and the old tag is kept (R2).
  std::vector<player_t*>        dnh_kept;
  std::vector<player_t*>        dnh_kept_hittable;
  const std::vector<player_t*>* eligible_ptr = &melee_legal;
  if ( dnh_split( p->sim, melee_legal, dnh_kept ) )
    eligible_ptr = &dnh_kept;
  if ( eligible_ptr->empty() )
  {
    eligible_ptr = &hittable;
    if ( dnh_split( p->sim, hittable, dnh_kept_hittable ) )
      eligible_ptr = &dnh_kept_hittable;
  }
  const std::vector<player_t*>& eligible = *eligible_ptr;
  player_t*                     pick     = eligible.empty() ? nullptr : eligible.front();
  if ( p->sim->solver_random_chosen_enemy && eligible.size() >= 2 )
  {
    size_t k = static_cast<size_t>( p->sim->solver_target_rng.range( 0.0, static_cast<double>( eligible.size() ) ) );
    if ( k >= eligible.size() )
      k = eligible.size() - 1;
    pick = eligible[ k ];
  }

  // With nothing hittable the old tag is kept (R2).
  if ( pick != nullptr )
  {
    p->rl_chosen_enemy = pick;
    pin_to_tag( p );  // 266-09 (R5): after a re-pick the target and swings follow the tag (flag on only)
  }
}

// tstl-sylvanas 260928-tb8: observation-only non-boss mask for trash pulls. Engine mechanics keep
// player_t::is_boss() untouched (the shaman APL never reads it; the only mechanical use is dungeon-style
// priority damage), so only what the net and the decision dump see changes.
bool obs_is_boss( const player_t* enemy )
{
  return enemy->is_boss() && enemy->sim->fight_style != FIGHT_STYLE_TRASH_PACK;
}

// Phase 268 plan 03 (G268-6, FORK-03, owner D6): declared per-candidate facts. See the declaration in the header for the two
// kinds. Both read the aura the actor itself (`source`) put on the candidate, by its engine name, with the non-creating
// find_dot / buff_t::find idiom (WR-04: get_dot / buff_t::get CREATE the object on a candidate that never had it).
double read_declared_fact( const rl_declared_fact& fact, player_t* source, player_t* candidate )
{
  switch ( fact.kind )
  {
    // WR-04 (268 fix pass): names collide between a dot object and a debuff (lightning_rod is an enhancement action and a
    // debuff; arms colossus_smash is an action and the target debuff). find_dot keeps returning an expired dot object, so a
    // plain "dot found, return its remains" read answered 0 for a live debuff of the same name. Each kind therefore prefers
    // its own object type while that object is ACTIVE, and falls through to the other type when it is not. The float layout
    // of the declared facts is unchanged.
    case rl_fact_kind::dot_remaining:
    {
      double       dot_seconds = 0.0;
      const dot_t* d           = candidate->find_dot( fact.name, source );
      if ( d != nullptr )
        dot_seconds = d->remains().total_seconds();
      if ( dot_seconds > 0.0 )
        return dot_seconds;
      if ( buff_t* b = buff_t::find( candidate, fact.name, source ) )
        return b->remains().total_seconds();
      return dot_seconds;
    }
    case rl_fact_kind::debuff_stacks:
    {
      // check(), never up(): up() mutates benefit bookkeeping (see rl_policy_obs.cpp's note on buff reads).
      double stacks = 0.0;
      if ( buff_t* b = buff_t::find( candidate, fact.name, source ) )
        stacks = static_cast<double>( b->check() );
      if ( stacks > 0.0 )
        return stacks;
      if ( dot_t* d = candidate->find_dot( fact.name, source ) )
        return static_cast<double>( d->current_stack() );
      return stacks;
    }
  }
  return 0.0;
}

std::size_t declared_fact_index_for_feature( const char* feature_name )
{
  for ( std::size_t k = 0; k < RL_DECLARED_FACT_COUNT; ++k )
    if ( std::strcmp( RL_TARGET_FEATURE_NAMES[ RL_DECLARED_FACTS[ k ].slot ], feature_name ) == 0 )
      return k;
  return RL_DECLARED_FACT_COUNT;
}

enemy_fact build_enemy_fact( const action_t* a, player_t* candidate )
{
  enemy_fact f;
  f.candidate    = candidate;
  f.distance     = a->player->get_player_distance( *candidate );
  f.alive        = !candidate->is_sleeping();
  f.immune       = candidate->debuffs.invulnerable && candidate->debuffs.invulnerable->check();
  f.immunity_remaining =
      candidate->debuffs.invulnerable ? candidate->debuffs.invulnerable->remains().total_seconds() : 0.0;

  bool reach_ok = !( a->sim->distance_targeting_enabled && a->range > 0 &&
                      f.distance > a->range + candidate->combat_reach );
  f.in_reach = reach_ok;
  f.in_range = reach_ok;

  // RAW geometry, never gated on sim->facing_enabled -- generic_filter applies that gate
  // separately (D-16: the observation writer needs ground truth, not the gated legality bit).
  f.in_front = a->player->is_in_front( *candidate, 0.0 );

  // WR-10 (260902/cr4): clipped at 600.0s AT THE SOURCE -- ties preference_shortest_time_to_die's
  // 1.0e9 and preference_lava_lash's 2.0e9 dominance offsets to a provable bound rather than one
  // conditional on fixed_time=1 (under fixed_time=1 no candidate's raw time_to_percent(0) exceeds
  // max_time anyway, but an unclipped value is a theoretical hazard the offsets should not have to
  // assume away). Byte identity on a 600s shape confirms this changes nothing today.
  f.time_to_die = std::min( candidate->time_to_percent( 0 ).total_seconds(), 600.0 );
  f.health_pct  = candidate->health_percentage();
  f.is_boss     = obs_is_boss( candidate );  // tstl-sylvanas 260928-tb8
  f.hazard      = candidate->sheet_hazard;  // tstl-sylvanas 262-04 (IN-02, R-6): the actor's own flag, beside is_boss

  // Declared facts (Phase 268 plan 03, G268-6): ONE generic loop over the spec header's RL_DECLARED_FACTS, in place of the eight
  // literal aura reads this function used to hold. Every lookup is the non-creating find / find_dot idiom (WR-04), `source` is
  // always `a->player` -- these are auras THIS actor puts on the candidate, on ANY enemy, not only the current target (P-5's fix point).
  // 0.0 is "absent", matching every other *_remaining field's own convention on this struct.
  for ( std::size_t k = 0; k < RL_DECLARED_FACT_COUNT; ++k )
    f.declared[ k ] = read_declared_fact( RL_DECLARED_FACTS[ k ], a->player, candidate );

  // OBS-03/R-U (tstl-sylvanas 232-02): deterministic geometry, never a target-cache read --
  // count of alive enemies within THIS action's OWN resolved radius of the candidate (a->radius:
  // 10.0 for chain_lightning, 8.0 for tempest -- WHICHEVER action called build_enemy_fact -- never
  // a hardcoded chain_lightning constant, WR-06 260902/cr4). Was previously written into TWO
  // always-equal fields (232-12, LO-01: the pre-232-02 splash-radius and jump-radius neighbour
  // counts); OBS-03 collapsed them to the one field below since no action ever needed the two
  // counts to differ. A shaped spell's true splash geometry is plan 228-03's, not this plan's.
  int neighbours = 0;
  if ( a->radius > 0.0 )
  {
    for ( player_t* other : a->sim->target_non_sleeping_list )
    {
      if ( other == candidate || !other->is_enemy() || !rl_counts_as_enemy( other ) )
        continue;
      if ( candidate->get_player_distance( *other ) <= a->radius + other->combat_reach )
        ++neighbours;
    }
  }
  f.neighbours_within_radius = neighbours;

  f.is_current_target = ( candidate == a->player->target );
  f.actor_index        = candidate->actor_index;
  f.actor_spawn_index  = candidate->actor_spawn_index;

  // The class's own part of the full fact (Phase 268 plan 03, G268-5): the named aura fields completed FROM the declared values
  // above and the class's geometry counts (shaman: chain hop count, Voltaic Blaze new Flame Shocks, Lava Lash spread -- see
  // rl_class_plugins.cpp). Computed for EVERY caller (this is the FULL builder, "every field filled"), unlike
  // build_enemy_fact_for_scoring's own pref-conditional lite computation below. A class with nothing to add has no hook.
  const rl_class_plugin& plugin = rl_class_plugin_for( a->player );
  if ( plugin.fill_class_fields != nullptr )
    plugin.fill_class_fields( a, candidate, f );

  return f;
}

// 228-07 (TGT-07, D-21): the FULL candidate set for one targeted action, as complete enemy_fact
// records -- reuses generic_filter/build_enemy_fact VERBATIM (the exact functions select() itself
// calls below), so this can never enumerate a different candidate set than the one the preference
// actually scores. OBS-01 (232-02) dropped build_enemy_fact's `previous_pick` parameter entirely,
// so there is no local to thread through here any more.
std::vector<enemy_fact> build_candidate_facts( const action_t* a, bool harmful )
{
  std::vector<enemy_fact> out;
  // tstl-sylvanas 264-05 (O4) + 261006-dnh-F1: the set the aim exploration draws from and the dump lists is the SAME set select() blocks (same
  // strict do-not-hit pass, no counting here), so its order and size match the stamped block's slots.
  std::vector<player_t*> gathered, kept;
  for ( player_t* t : a->sim->target_non_sleeping_list )
    if ( t->is_enemy() && rl_counts_as_enemy( t ) && generic_filter( a, t, harmful ) )
      gathered.push_back( t );
  const std::vector<player_t*>& legal = dnh_split( a->sim, gathered, kept ) ? kept : gathered;
  for ( player_t* t : legal )
    out.push_back( build_enemy_fact( a, t ) );
  return out;
}



namespace
{

// BL-01 (232-13): resolves the MODELLED spell's geometry (radius, hop cap) -- never the CALLER's
// own action_t*. Both preference_tempest and preference_chain_lightning are dispatched for the two
// Thorim's-aware melee strikes (windstrike/stormstrike, RULE-01) as well as for the tempest/
// chain_lightning tokens themselves; a melee strike's own action_t* carries neither a radius nor an
// aoe (`stormstrike_base_t`'s ctor sets neither -- `action_t::radius`/`aoe` come only from
// `spelleffect_data.radius_max()`/`max_targets()`, action.cpp:901/921/986, :796-797/:948-950, both
// 0 for a melee weapon strike), so reading `a->radius`/`a->aoe` off the resolved (caller) action
// silently degenerated both branches to "0 neighbours"/"hop cap 1" -- BL-01's whole defect. Named
// by literal string (never a variable holding the spell name) so `find_action("chain_lightning")`/
// `find_action("tempest")` are grep-able, single-source citations, matching
// `preference_for_thorims_aware_strike`'s own `find_action("thorims_invocation")` idiom below.
// Refuses (throws, never silently falls back to the caller's own zero geometry) when the resolved
// source is null or its radius is <= 0.0 -- a structural invariant: a Thorim's-primed decision with
// no Tempest/Chain-Lightning action registered on this player is a configuration error.
// 261001 (MEASURED, decision-path fights): this refusal does NOT fire for a character that lacks the
// Tempest talent. A Totemic character's `tempest` action is still constructed from its priority
// list, is not a background action, and carries Tempest's own 8-yd radius, so the refusal stays
// reserved for a genuinely missing action (a priority list that names no `tempest`/`chain_lightning`
// while the Thorim's branch is primed) -- never loosen it for an untalented character.
struct chain_geometry
{
  double radius = 0.0;
  int    cap    = 1;
};

chain_geometry resolve_thorims_branch_geometry( const action_t* resolved, preference_fn pref )
{
  const bool      is_chain_lightning = ( pref == preference_chain_lightning );
  const action_t* source             = is_chain_lightning
                                            ? resolved->player->find_action( "chain_lightning" )
                                            : resolved->player->find_action( "tempest" );
  if ( !source || source->radius <= 0.0 )
  {
    throw sc_runtime_error( fmt::format(
        "rl_target_select::resolve_thorims_branch_geometry: the Thorim's-aware branch for '{}' "
        "resolved a null or zero-radius geometry source ('{}') -- refusing rather than silently "
        "falling back to the caller's own (zero) radius/aoe (BL-01)",
        resolved->name_str, is_chain_lightning ? "chain_lightning" : "tempest" ) );
  }

  chain_geometry geo;
  geo.radius = source->radius;
  if ( is_chain_lightning )
  {
    geo.cap = source->aoe > 0 ? source->aoe : 1;

    // ME-06/R-AK (232-13): the hop cap follows the ENGINE's own resolved `aoe`. MEASURED this
    // session on the standard `mid2-stormbringer.simc` build (Chaining Storms talented, entryId
    // 135254 present in `spec_talents`): `aoe` resolves to 5, matching the addon's talented cap
    // (`hasTalentChainingStorms ? 5 : 3`, enhancementShamanContext.ts) -- NOT the 3 a static read
    // of Chain Lightning's own talent spell (id 188443, "Chain Targets: 3") would suggest absent
    // any generic effect-merge in `sc_shaman.cpp` (which references Chaining Storms, id 334308,
    // in exactly two places -- neither an `apply_affecting_effects`-style call -- so the +2 must
    // be applied by some OTHER, more generic DBC-level mechanism this session did not trace).
    // 261001 (MEASURED, decision-path fights, threads=1, `target_select_enabled=1`): the
    // WITHOUT-Chaining-Storms case the paragraph above could not measure resolves to 3, exactly the
    // addon's own untalented value, and does NOT refuse here. Receipts: the standard Stormbringer
    // profile (Chaining Storms talented) logs aoe=5; the same profile with the by-name per-fight
    // override `spec_talents+=/chaining_storms:0` after `input=` logs aoe=3 and its `save=` profile's
    // encoded `talents=` string changes -- that override DOES take effect (the earlier "did not
    // take effect" reading was wrong; 261001's own Voltaic Blaze proof relied on the same override
    // form); the Totemic profile (neither Chaining Storms nor Tempest) also logs aoe=3, radius 10.
    // Neither untalented build refused over 5 seeds x {1, 5} targets x 300 s. So {3, 5} stays the
    // full legal set and the check below still catches a genuinely wrong third value.
    if ( geo.cap != 3 && geo.cap != 5 )
    {
      throw sc_runtime_error( fmt::format(
          "rl_target_select::resolve_thorims_branch_geometry: chain_lightning's resolved aoe cap "
          "is {} -- expected 3 or 5 (the addon's own hasTalentChainingStorms ? 5 : 3 ternary); "
          "refusing rather than silently training on an unmeasured cap",
          geo.cap ) );
    }
  }
  return geo;
}

// WR-05 (260902/cr4): the cheap half of select()'s scoring-loop fact build -- see that call
// site's own comment. NOT exposed in the header: build_enemy_fact() (above) is the one and only
// public fact-record builder every OTHER caller (the future observation writer, 228-04 onward)
// uses, with every field filled exactly as before this task. `build_enemy_fact` (the OBSERVATION
// builder) is DELIBERATELY untouched by BL-01's fix below -- the strike blocks' own
// `neighbours_within_radius` obs leaf stays the strike's own (zero) geometry; a schema-semantic
// change there is Phase 233's census (R5-1), not this plan's.
enemy_fact build_enemy_fact_for_scoring( const action_t* a, player_t* candidate, preference_fn pref,
                                          const chain_geometry* geo )
{
  enemy_fact f;
  f.candidate   = candidate;
  f.time_to_die = std::min( candidate->time_to_percent( 0 ).total_seconds(), 600.0 );  // WR-10

  // The scorer-specific part (Phase 268 plan 03, G268-5): only what the DISPATCHED preference reads, filled by the class plugin
  // (WR-05's own "skip what the dispatched preference doesn't need" discipline). Shaman: Flame Shock remaining and the one
  // Voltaic Blaze / Lava Lash geometry count. A class with no class-specific scorer has no hook.
  const rl_class_plugin& plugin = rl_class_plugin_for( a->player );
  if ( plugin.fill_scoring_fields != nullptr )
    plugin.fill_scoring_fields( a, candidate, pref, f );

  // BL-01 (232-13) / ME-4 (232-15b): the neighbour count is computed at the MODELLED spell's own
  // resolved radius -- Tempest's or Chain Lightning's -- never the caller's (`a`'s) own radius,
  // which is 0 for the two Thorim's-aware melee strikes. ME-4 moved the actual
  // resolve_thorims_branch_geometry() call (a player_t::find_action() name walk) OUT of this
  // per-CANDIDATE function and into select()'s once-per-DECISION precompute -- select() is this
  // function's only caller, and it now passes the already-resolved geometry down through `geo`
  // rather than asking this function to re-resolve it once per candidate. The loud refusal on a
  // null-or-zero-radius source still lives at resolve_thorims_branch_geometry's own single call
  // site (select()); this function refuses separately only if it is somehow invoked for a
  // pref that needs geometry without one having been supplied, which would itself be a caller bug.
  if ( pref == preference_tempest || pref == preference_chain_lightning )
  {
    if ( !geo )
    {
      throw sc_runtime_error(
          "rl_target_select::build_enemy_fact_for_scoring: pref requires a precomputed Thorim's "
          "branch geometry but none was supplied -- select() must resolve it once per decision "
          "before calling this function (ME-4)" );
    }
    int neighbours = 0;
    for ( player_t* other : a->sim->target_non_sleeping_list )
    {
      if ( other == candidate || !other->is_enemy() || !rl_counts_as_enemy( other ) )
        continue;
      if ( candidate->get_player_distance( *other ) <= geo->radius + other->combat_reach )
        ++neighbours;
    }
    f.neighbours_within_radius = neighbours;
  }
  else if ( a->radius > 0.0 )
  {
    // Defensive generality only -- no token in RL_TARGETED_TOKENS reaches this branch today (every
    // OTHER registry preference is single-target, so `a->radius` stays 0); kept so a future
    // radius-bearing registry action does not silently skip the neighbour count.
    int neighbours = 0;
    for ( player_t* other : a->sim->target_non_sleeping_list )
    {
      if ( other == candidate || !other->is_enemy() || !rl_counts_as_enemy( other ) )
        continue;
      if ( candidate->get_player_distance( *other ) <= a->radius + other->combat_reach )
        ++neighbours;
    }
    f.neighbours_within_radius = neighbours;
  }

  f.is_current_target = ( candidate == a->player->target );
  f.actor_index       = candidate->actor_index;
  f.actor_spawn_index = candidate->actor_spawn_index;
  return f;
}

// RULE-02 (232-06, R-Z; geometry parameterised 232-13/BL-01): the greedy Chain Lightning hop
// simulation, ported from the addon's enhancementShamanTargeting.ts:933-969 hop loop as pure
// geometry. For EACH candidate in `candidates` treated as the chain's START, greedily walks to the
// nearest not-yet-hit candidate -- from the SAME already-filtered `candidates` set select() just
// built (D-20 forbids a third filter loop: this never re-scans target_non_sleeping_list, re-calls
// generic_filter, or calls build_candidate_facts() a second time) -- within the explicit `radius`
// parameter (`radius + other->combat_reach`, the SAME inequality convention build_enemy_fact's own
// neighbours_within_radius above already uses), until `cap` candidates have been hit including the
// start, or no further hop is available. BOTH `radius` and `cap` are the MODELLED spell's own
// (resolve_thorims_branch_geometry's caller resolves them by name, never a hardcoded literal, never
// read off the calling action `a` -- BL-01's fix). Fills `out` keyed on each START candidate; `out`
// is a plain local the caller stashes, never a module global itself.
//
// R-Z's algebra trap, named here rather than merely in this function's caller: the addon's
// SEPARATE, uninvolved PRODUCTION hop walk (the one this ports is the addon's PURE RULES walk,
// enhancementTargetRules.ts) uses `dist - br <= R`; this walk uses `dist <= R + other_reach` --
// equal only when every actor shares the same reach/br value. 232-05's parity harness sidesteps the
// difference with uniform-reach fixtures.
//
// NEVER calls the engine's own chain-resolution helper in sc_shaman.cpp -- that helper draws from
// the sim's random number stream (FORK-03's own fix exists to keep a read-only selector path from
// ever perturbing that stream); this walk reads only already-resolved positions and combat_reach,
// so it is provably deterministic geometry -- a DETERMINISTIC APPROXIMATION of the engine's own
// randomised chain resolution, never a prediction of it (LO-05/R-D).
void compute_chain_hop_counts( double radius, int cap, const std::vector<player_t*>& candidates,
                                std::unordered_map<const player_t*, int>& out )
{
  std::vector<bool> hit( candidates.size(), false );
  for ( std::size_t start_index = 0; start_index < candidates.size(); ++start_index )
  {
    std::fill( hit.begin(), hit.end(), false );
    hit[ start_index ]    = true;
    player_t* current     = candidates[ start_index ];
    int       hop_count   = 1;
    while ( hop_count < cap )
    {
      int    nearest_index = -1;
      double nearest_dist  = std::numeric_limits<double>::max();
      for ( std::size_t candidate_index = 0; candidate_index < candidates.size(); ++candidate_index )
      {
        if ( hit[ candidate_index ] )
          continue;
        player_t* other = candidates[ candidate_index ];
        double    dist  = current->get_player_distance( *other );
        if ( dist <= radius + other->combat_reach && dist < nearest_dist )
        {
          nearest_dist  = dist;
          nearest_index = static_cast<int>( candidate_index );
        }
      }
      if ( nearest_index < 0 )
        break;
      hit[ static_cast<std::size_t>( nearest_index ) ] = true;
      current   = candidates[ static_cast<std::size_t>( nearest_index ) ];
      ++hop_count;
    }
    out[ candidates[ start_index ] ] = hop_count;
  }
}

// 259-07 (Q2, Q19, R3, R4; fork B3/B4): which of the 23 declared facts read the capability fixed
// value for THIS actor -- a fact whose governing capability's effective bit is 0 (venomfang
// weapon, Unleashed Fire lingering rune, the tier 2-piece: RL_AIM_FACT_DESCS[i].capability).
// Constant for the actor's whole fight (talents, gear and set bonuses are fixed once the actor
// is initialised), so it is computed ONCE per actor, never per slot or per decision, from the
// observation's own memoised capability bits (rl_policy::capability_effective_bit) -- the gated
// facts and the observation's governed columns can therefore never disagree. select() looks the
// gate up once per decision and hands it to every slot's fill.
struct aim_fact_gate
{
  std::array<bool, RL_TARGET_FEATURES> governed_off{};
  // Phase 268 plan 03: the per-index source of each fact, copied once per actor from the actor binding (built, and any unknown
  // feature refused, at the actor's arise). Lives here so the per-decision gate lookup also hands the fill its source table
  // without a second map lookup per candidate; cleared and counted with g_aim_fact_gate_cache.
  feature_sources_t sources{};
};
std::unordered_map<const player_t*, aim_fact_gate> g_aim_fact_gate_cache;

const aim_fact_gate& aim_fact_gate_for( player_t* p )
{
  auto it = g_aim_fact_gate_cache.find( p );
  if ( it != g_aim_fact_gate_cache.end() )
    return it->second;
  aim_fact_gate gate;
  gate.sources = actor_binding_for( p ).feature_sources;
  for ( std::size_t i = 0; i < RL_TARGET_FEATURES; ++i )
  {
    const int capability = RL_AIM_FACT_DESCS[ i ].capability;
    gate.governed_off[ i ] =
        capability >= 0 && !rl_policy::capability_effective_bit( p, static_cast<std::size_t>( capability ) );
  }
  return g_aim_fact_gate_cache.emplace( p, gate ).first->second;
}

// One fact, scaled and gated exactly as the registry declares (and as scripts/rl/
// target_features.py's scale_fact/encode_fact_row do, bit for bit): the raw value rounded to
// float32 (the value production logged), the observation's own arithmetic in float64
// (rl_policy::apply_leaf_scale), the result rounded to float32; a fact whose capability bit is 0
// reads RL_CAPABILITY_FIXED_VALUE whatever the raw value.
float scaled_fact( float raw, std::size_t i, const aim_fact_gate& gate )
{
  if ( gate.governed_off[ i ] )
    return static_cast<float>( RL_CAPABILITY_FIXED_VALUE );
  const rl_aim_fact_desc& d = RL_AIM_FACT_DESCS[ i ];
  return static_cast<float>( rl_policy::apply_leaf_scale( static_cast<double>( raw ), d.kind, d.has_div,
                                                          d.div, d.has_clip_div, d.clip_div ) );
}

// 233.1-03 (R6-13, OV-5): extracted verbatim from the old preference_scorer's own fill loop
// (230-02; folded into run_target_head, 240-05 Task 2) so the rules path can fill its own scratch
// buffer with it too -- pure, no rl_scorer_t dependency.
//
// Phase 268 plan 03 (research Pitfall 6): the fill is BY INDEX from the actor's bind-time source table (gate.sources) -- the
// registry's own declaration order, RL_TARGET_FEATURE_NAMES. Each fact is read as a double (a bool or an int widens exactly)
// from its generic enemy_fact field, its declared value or its class geometry provider, then SCALED into the registry's
// range and capability-gated (see scaled_fact above). The generated fact-list macro in the constants header is generated
// content and no code expands it any more. The rules never read this scaled block (they rank on `enemy_fact`'s raw values --
// build_enemy_fact's 600 s time-to-die cap stays, the 60 s cap lives only in the scaling's clipDiv).
void fill_candidate_features( const action_t*, const enemy_fact& fact, float* out,
                              const aim_fact_gate& gate )
{
  for ( std::size_t i = 0; i < RL_TARGET_FEATURES; ++i )
  {
    const feature_source_t& src = gate.sources[ i ];
    double                  value = 0.0;
    switch ( src.kind )
    {
      case fact_source_kind::generic:
        value = generic_fact_value( fact, src.generic );
        break;
      case fact_source_kind::declared:
        value = fact.declared[ src.declared ];
        break;
      case fact_source_kind::geometry:
        value = src.geometry( fact );
        break;
    }
    out[ i ] = scaled_fact( static_cast<float>( value ), i, gate );
  }
}

// The registry's feature-name list must have exactly the compiled width (replaces the old count check of the generated fact-list macro).
static_assert( sizeof( RL_TARGET_FEATURE_NAMES ) / sizeof( RL_TARGET_FEATURE_NAMES[ 0 ] ) == RL_TARGET_FEATURES,
               "RL_TARGET_FEATURE_NAMES must list exactly RL_TARGET_FEATURES features (268-03)" );

// 240-05 Task 2 (must_haves: "the head's argmax must reuse this, not invent one"): the ONE tie
// ladder both select()'s own rule pick (below) and run_target_head's head pick use -- extracted
// here so there is one definition, two callers, never a second copy. Ladder, unchanged from
// select()'s pre-240-05 inline logic: (1) `!best` -- the first candidate seen always wins so far;
// (2) highest score; (3) among equal scores, the player's CURRENT target; (4) final tie-break on
// the stable (actor_index, actor_spawn_index) identity pair, ascending -- a total order, so a run
// stays reproducible (TGT-02 edge: ordering) regardless of which rung a caller's own candidate
// enumeration order would otherwise have decided.
bool candidate_is_better( double score, player_t* c, double best_score, player_t* best,
                            player_t* current_target )
{
  if ( !best )
    return true;
  if ( score != best_score )
    return score > best_score;
  bool c_is_current    = ( c == current_target );
  bool best_is_current = ( best == current_target );
  if ( c_is_current != best_is_current )
    return c_is_current;
  return std::tie( c->actor_index, c->actor_spawn_index ) <
         std::tie( best->actor_index, best->actor_spawn_index );
}
} // anonymous namespace

player_t* select( action_t* a, bool harmful, preference_fn pref )
{
  // WR-05 (260902/cr4): reuse the file-static buffer instead of allocating a fresh vector every
  // call -- cleared, not reallocated (capacity survives across calls after the first).
  std::vector<player_t*>& candidates = g_candidate_buffer;
  candidates.clear();
  candidates.reserve( a->sim->target_non_sleeping_list.size() );
  for ( player_t* t : a->sim->target_non_sleeping_list )
    if ( t->is_enemy() && rl_counts_as_enemy( t ) && generic_filter( a, t, harmful ) )
      candidates.push_back( t );

  // TGT-02 edge: empty -- no pick, legality bit goes to 0, the unanchored wait stays legal.
  if ( candidates.empty() )
    return nullptr;

  // tstl-sylvanas 264-05 (O4) + 261006-dnh-F1 (strict): the sheet-declared do-not-hit pass. Right after the gather and BEFORE the
  // candidate block is captured or any score is read: every do-not-hit boss leaves the set, even the only one. When that empties
  // the set the answer is the same as for the empty gather above: no pick, the legality bit goes to 0.
  std::vector<player_t*>& unpruned_candidates = g_unpruned_candidate_buffer;
  const bool              dnh_pruned = dnh_prune_candidates( a->sim, candidates, unpruned_candidates );
  if ( candidates.empty() )
    return nullptr;

  // 240-05 Task 2 (D1(a)/D8(a)): BOTH of this function's former overflow refusals (the
  // scorer-declared-slots refusal, and the `!capture_block && pref == preference_scorer` refusal
  // immediately below this comment used to guard) are REMOVED here -- they can never fire any
  // more, because `preference_for` (below) no longer ever returns a scorer preference: the rule
  // runs on EVERY decision unconditionally now, so `pref == preference_scorer` is not merely rare,
  // it is impossible (the symbol itself no longer exists). The refusal they existed for did NOT
  // disappear -- it MOVED to load time (task 3, `rl_policy_net.cpp`'s loader), where a weights
  // file whose declared slot/feature counts disagree with the compiled constants is refused ONCE
  // at startup, by name, rather than aborting an iteration hours into a run. At sixteen slots
  // (240-01/240-02's own re-pin) there is nothing left for a PER-DECISION refusal to catch that
  // the loader hasn't already ruled out.
  //
  // 233.1-03b (R6-13, P233.1-36): the rules path itself still treats RL_TARGET_SLOTS as a
  // DIAGNOSTIC bound, not a gate -- an overflowing decision keeps its FULL candidate set for the
  // pick below (select() still ranks EVERY candidate and returns the identical pick it always
  // would) and records NO candidate block for that decision (the up-front stamp state just below:
  // count 0, mask 0, sentinel chosen slot); run_target_head's own no-block counter is what makes
  // this skip measurable now that the scorer-as-preference path (which used to refuse instead) is
  // gone. scorer_block_equivalence.py counts this by name as rowsOverflowSkipped, never as a
  // mismatch.
  const bool capture_block = candidates.size() <= RL_TARGET_SLOTS;

  // 233.1-03 (R6-13, OV-5): stamp the candidate block table for THIS decision up front, before any
  // early return, so a stale block from an earlier decision can never be read as current (mirrors
  // g_pick_table's own stamp discipline) -- on EVERY preference now, not only the scorer's, so
  // scorer_block_equivalence.py has a real (non-vacuous) row to compare on a rules-only corpus
  // (233-08's own finding: every rules-path decision previously recorded candidate_count == 0).
  // Re-filled below as candidates are actually scored.
  {
    candidate_block_slot& slot = g_candidate_block_table[ a ];
    if ( slot.features.size() != RL_TARGET_SLOTS * RL_TARGET_FEATURES )
      slot.features.assign( RL_TARGET_SLOTS * RL_TARGET_FEATURES, 0.0f );
    else
      std::fill( slot.features.begin(), slot.features.end(), 0.0f );
    // 240-05 (D1(a)): actors sized/cleared in lockstep with features -- see candidate_block_slot's
    // own doc comment for why this parallel array exists and who reads it.
    if ( slot.actors.size() != RL_TARGET_SLOTS )
      slot.actors.assign( RL_TARGET_SLOTS, nullptr );
    else
      std::fill( slot.actors.begin(), slot.actors.end(), nullptr );
    slot.mask        = 0;
    slot.count       = 0;
    slot.chosen_slot   = rl_translog::CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK;
    slot.rules_slot    = rl_translog::CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK;
    slot.observed_slot = rl_translog::CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK;
    slot.stamp        = current_decision_stamp( a->player );
    slot.has_stamp    = true;
  }

  // 233.1-03 (R6-13, OV-5): the single-candidate shortcut that used to return early here (TGT-02
  // edge: single) is REMOVED -- every preference now needs the general loop below to run so its
  // (trivial, one-slot) candidate block is captured, never silently skipped. The general algorithm
  // below reaches the identical PICK either way (stated originally only to keep the edge case
  // visible in code, per the removed comment); this changes 0 picks, only whether the block gets
  // filled for a one-candidate decision.
  if ( g_rules_feature_scratch.size() != RL_TARGET_FEATURES )
    g_rules_feature_scratch.assign( RL_TARGET_FEATURES, 0.0f );
  // 259-07 (R3): the capability gate for the candidate table's facts -- one lookup per decision
  // (constant per actor), handed to every slot's fill below.
  const aim_fact_gate& capture_gate = aim_fact_gate_for( a->player );

  // BL-01 (232-13) / ME-4 (232-15b): resolve the Thorim's-branch geometry ONCE per decision, HERE
  // -- before the per-candidate scoring loop below. `resolve_thorims_branch_geometry` calls
  // `player_t::find_action()`, a name-walk over the action list; calling it once per CANDIDATE (as
  // `build_enemy_fact_for_scoring` used to do directly) repeats that walk N times per decision for
  // the identical `(a, pref)` pair, in the training hot loop -- exactly the extra per-candidate
  // work R-S refused elsewhere. Resolved for BOTH branches that need it (tempest and
  // chain_lightning), not merely chain_lightning's own hop-stash consumer below, so
  // `build_enemy_fact_for_scoring`'s tempest branch gets the SAME once-per-decision value instead
  // of re-resolving it itself.
  chain_geometry precomputed_geo;
  bool           has_precomputed_geo = false;
  if ( pref == preference_tempest || pref == preference_chain_lightning )
  {
    precomputed_geo     = resolve_thorims_branch_geometry( a, pref );
    has_precomputed_geo = true;
  }

  // RULE-02 (232-06, R-Z): compute the Chain Lightning hop-count stash ONCE per decision, HERE --
  // before the scoring loop below calls `pref()` once per candidate. `preference_chain_lightning`
  // cannot see the other candidates itself (`preference_fn` is one-candidate-at-a-time by design),
  // so the geometry must be precomputed and stashed for it to read. Reused, not re-derived: this
  // walks the SAME `candidates` vector the generic-filter loop above already built (D-20).
  if ( pref == preference_chain_lightning )
  {
    chain_hop_slot& hop_slot = g_chain_hop_stash[ a ];
    hop_slot.hop_counts.clear();
    // ME-4 (232-15b): reuses `precomputed_geo` (resolved once, just above) rather than calling
    // resolve_thorims_branch_geometry() a SECOND time for the same decision.
    // tstl-sylvanas 264-05 (O4): the hop walk reads EVERY gathered candidate, do-not-hit ones included: a chain still reaches
    // them, so area geometry is untouched by the rule.
    compute_chain_hop_counts( precomputed_geo.radius, precomputed_geo.cap,
                              dnh_pruned ? unpruned_candidates : candidates, hop_slot.hop_counts );
    hop_slot.stamp         = current_decision_stamp( a->player );
    hop_slot.has_stamp     = true;
    hop_slot.used_fallback = false;
  }

  // CR-03 (260902/cr4, RULING (a) -- PREFERENCE FIRST): the sticky early-return that used to
  // live here is GONE. OBS-01/R5-5 (232-02) then removed the sticky TIE-BREAK too -- the action's
  // own previous pick (`a->target`) is no longer read anywhere in this function, so there is no
  // `previous` local any more. Ladder: (1) preference, highest score wins; (2) among EQUAL scores,
  // the player's CURRENT target (p->target); (3) final tie-break on the stable
  // (actor_index, actor_spawn_index) identity pair, ascending -- a total order, so a run is
  // reproducible (TGT-02 edge: ordering). Consequence: an exact tie between two candidates can now
  // flip when p->target moves for an unrelated reason (the ordering stays a total order because
  // the identity pair is the final rung, so runs remain reproducible).
  player_t*   current_target = a->player->target;
  player_t*   best           = nullptr;
  double      best_score     = 0.0;
  std::size_t best_slot      = 0;  // 230-04: index of `best` within `candidates`, kept in lockstep
  for ( std::size_t slot_index = 0; slot_index < candidates.size(); ++slot_index )
  {
    player_t* c = candidates[ slot_index ];
    // WR-05 (260902/cr4): the SCORING loop drives exactly one of the eight rule preference
    // functions -- none of them read distance/in_reach/in_range/in_front/alive/immune/
    // immunity_remaining/health_pct/is_boss (generic_filter already excluded anything those would
    // have rejected), so this lite build skips them and skips the Flame Shock find_dot() lookup
    // unless the dispatched preference is one of the two that read it. build_enemy_fact() itself
    // is UNCHANGED -- it is what external readers (the observation writer, 228-04 onward) call,
    // with every field filled.
    //
    // 240-05 Task 2: the NINTH preference (the learned scorer, Phase 230-02) that used to need the
    // FULL builder here for SCORING is GONE -- the rule always drives this loop now, so `fact` is
    // always the LITE, geometry-corrected fact every rule preference reads.
    enemy_fact fact  = build_enemy_fact_for_scoring(
        a, c, pref, has_precomputed_geo ? &precomputed_geo : nullptr );
    double     score = pref( a, fact );

    // 233.1-03 (R6-13, OV-5): CAPTURE the candidate block on EVERY preference now (230-04's
    // original SCOR-02 comment scoped this scorer-only; 240-05 Task 2: the scorer-scratch capture
    // branch this comment used to describe is gone along with the scorer-as-preference path --
    // every capture is the rules-path one below now). 233.1-03b (P233.1-36): guarded by
    // `capture_block` -- an overflowing rules-path decision pays NO capture cost (no extra
    // build_enemy_fact, no memcpy) and leaves the up-front-stamped empty block (count 0, mask 0)
    // in place; the pick logic below (`pref(a, fact)` above, `best`/`best_score`/`best_slot` and
    // the tie-break ladder below) is completely outside this guard and therefore untouched.
    if ( capture_block )
    {
      // `fact` above is the LITE, geometry-corrected fact used for SCORING (see the comment above
      // `fact`'s declaration) -- build a fresh, uncorrected FULL fact here, specifically for
      // capture, matching decision_dump.cpp's own build_candidate_facts() exactly (including its
      // own deliberately-uncorrected zero neighbours_within_radius for the two Thorim's-aware
      // melee strikes), and fill it into the rules-path scratch buffer beside g_candidate_buffer.
      enemy_fact capture_fact = build_enemy_fact( a, c );
      fill_candidate_features( a, capture_fact, g_rules_feature_scratch.data(), capture_gate );
      candidate_block_slot& slot = g_candidate_block_table[ a ];
      std::memcpy( slot.features.data() + slot_index * RL_TARGET_FEATURES,
                   g_rules_feature_scratch.data(), RL_TARGET_FEATURES * sizeof( float ) );
      // 240-05 (D1(a)): parallel identity capture, same slot index -- see candidate_block_slot's
      // own doc comment for who reads this and why.
      slot.actors[ slot_index ] = c;
      slot.mask |= static_cast<std::uint16_t>( 1u << slot_index );
      slot.count = static_cast<std::uint8_t>( slot.count + 1 );
    }

    // 240-05 Task 2: the inline tie ladder that used to live here is now candidate_is_better
    // (above, anonymous namespace) -- one definition, two callers (this loop and
    // run_target_head's own argmax, below).
    if ( candidate_is_better( score, c, best_score, best, current_target ) )
    {
      best       = c;
      best_score = score;
      best_slot  = slot_index;
    }
  }

  // 233.1-03 (R6-13, OV-5): stamp the chosen slot on EVERY preference now -- the block itself is
  // captured on every preference above, so a rules-path decision's chosen_slot must be real too
  // (previously scorer_scratch-gated, matching the old scorer-only capture). 233.1-03b
  // (P233.1-36): additionally gated on `capture_block` -- an overflowing decision's block was
  // never captured above, so it must keep the up-front no-pick sentinel here too, or the probe
  // would read a chosen slot into a block of count 0.
  if ( capture_block && best != nullptr )
  {
    candidate_block_slot& chosen_block = g_candidate_block_table[ a ];
    chosen_block.chosen_slot   = static_cast<std::uint8_t>( best_slot );
    // 259-07 (R5): the rules' own pick, kept apart from `chosen_slot` (which a head or the dial
    // may overwrite); the observation describes it until run_target_head says otherwise.
    chosen_block.rules_slot    = static_cast<std::uint8_t>( best_slot );
    chosen_block.observed_slot = static_cast<std::uint8_t>( best_slot );
  }

  return best;
}

namespace
{
// WR-11 (260902/cr4): the single-sim/single-thread precondition this whole module is built on,
// factored into one predicate -- mirrors decision_dump.cpp:~820's own fix for the sibling
// g_action_handle_cache. `threads != 1 || !profileset_map.empty()` now DEGRADES the three module
// globals (no picks filled, no stamps bumped, lookup always reports not-found) instead of merely
// asserting -- an assert compiles out under NDEBUG, silently leaving an unlocked, unkeyed-by-sim
// std::unordered_map to race. The `assert` below is KEPT beside the runtime check as a debug-build
// tripwire (it still fires first in a debug build, before the degrade path is ever reached).
bool multi_sim_or_multi_thread( const sim_t* sim )
{
  return sim->threads != 1 || !sim->profileset_map.empty();
}
} // anonymous namespace

std::uint64_t begin_decision( const player_t* p )
{
  assert( p->sim->threads == 1 && p->sim->profileset_map.empty() &&
          "rl_target_select's decision stamp is single-sim/single-thread by construction (228-02, "
          "mirrors 221-01's action-handle cache)" );
  if ( multi_sim_or_multi_thread( p->sim ) )
    return 0;  // WR-11 degrade: no stamp bump, caller must not fill or trust any pick this "decision".
  return ++g_decision_stamp[ p ];
}

std::uint64_t current_decision_stamp( const player_t* p )
{
  auto it = g_decision_stamp.find( p );
  return it == g_decision_stamp.end() ? 0 : it->second;
}

void fill_pick( action_t* resolved, bool harmful, preference_fn pref )
{
  if ( !resolved )
    return;
  if ( multi_sim_or_multi_thread( resolved->player->sim ) )
    return;  // WR-11 degrade: never fill a pick under threads>1/profileset -- lookup_pick below
             // then reports not-found for it, the same documented fail-open decision_dump.cpp's
             // own action_resolvable/action_ready arrays already use for this precondition.
  std::uint64_t stamp = g_decision_stamp[ resolved->player ];
  // CR-02 (260902/cr4): idempotent WITHIN a decision -- a second fill_pick call for the SAME
  // action at the SAME stamp must never trigger a second select() call (the design invariant this
  // whole module exists to hold): return immediately when the table already carries this
  // decision's stamp for this action.
  auto existing = g_pick_table.find( resolved );
  if ( existing != g_pick_table.end() && existing->second.has_stamp && existing->second.stamp == stamp )
    return;
  player_t* pick = select( resolved, harmful, pref );
  g_pick_table[ resolved ] = pick_slot{ pick, stamp, true };
}

player_t* lookup_pick( const action_t* resolved, bool* out_found )
{
  auto it = g_pick_table.find( resolved );
  if ( it == g_pick_table.end() || !it->second.has_stamp )
  {
    if ( out_found )
      *out_found = false;
    return nullptr;
  }
  auto stamp_it = g_decision_stamp.find( resolved->player );
  std::uint64_t current_stamp = ( stamp_it == g_decision_stamp.end() ) ? 0 : stamp_it->second;
  if ( it->second.stamp != current_stamp )
  {
    // Tampering / staleness guard (T-228-02-02): the stamped pick belongs to an EARLIER decision.
    // Refuse by name at the caller rather than falling back to a fresh select() here -- that
    // fallback is exactly the three-copies failure D-12 exists to prevent.
    if ( out_found )
      *out_found = false;
    return nullptr;
  }
  if ( out_found )
    *out_found = true;
  return it->second.pick;
}

// 230-04 (SCOR-02, R-B): mirrors lookup_pick's own stamp-comparison discipline exactly -- a
// block found in the table but stamped for an EARLIER decision is staleness, refused by name
// (`*out_found = false`) rather than returned, the same D-12 "never fall back to a fresh
// computation" reasoning lookup_pick's own comment states.
candidate_block lookup_candidate_block( const action_t* resolved, bool* out_found )
{
  auto it = g_candidate_block_table.find( resolved );
  if ( it == g_candidate_block_table.end() || !it->second.has_stamp )
  {
    if ( out_found )
      *out_found = false;
    return candidate_block{};
  }
  auto stamp_it = g_decision_stamp.find( resolved->player );
  std::uint64_t current_stamp = ( stamp_it == g_decision_stamp.end() ) ? 0 : stamp_it->second;
  if ( it->second.stamp != current_stamp )
  {
    if ( out_found )
      *out_found = false;
    return candidate_block{};
  }
  if ( out_found )
    *out_found = true;
  candidate_block block;
  block.features    = it->second.features.data();
  block.mask         = it->second.mask;
  block.count        = it->second.count;
  block.chosen_slot  = it->second.chosen_slot;
  block.rules_slot    = it->second.rules_slot;
  block.observed_slot = it->second.observed_slot;
  return block;
}

// 232-04 (OBS-02, R-T): stamps `resolved`'s pre-cast is_current_target for the CURRENT decision.
// Called ONLY from build_obs()'s real-decision path (rl_policy_obs.cpp, gated on
// `rl_state_t::is_decision_boundary`) -- never from decision_dump.cpp's own diagnostic build_obs()
// call, so a dump-time recompute (post accept_cast retarget) can never overwrite the real
// decision's capture. A no-op when `resolved` is null (mirrors fill_pick's own guard).
void stamp_target_fact_is_current_target( const action_t* resolved, bool is_current_target )
{
  if ( !resolved )
    return;
  auto& slot = g_target_fact_snapshot_table[ resolved ];
  slot.is_current_target     = is_current_target;
  slot.has_is_current_target = true;
  slot.stamp                  = current_decision_stamp( resolved->player );
  slot.has_stamp               = true;
}

// 260923-lrc (PLAN.md D11): stamp_target_fact_hit_damage() REMOVED -- it served the eight now-
// deleted action_leaves.*.hit_damage census leaves (R7-4: the live addon can never read a damage
// amount); it was the sole caller into calculate_direct_amount()/target_mitigation() under
// engine/sim/rl_* (rl_policy_obs.cpp).

// Reads the snapshot stamped for `resolved` at the CURRENT decision. `*out_found` is false when
// the stamp is stale or absent (the SAME has_stamp/stamp==current guard lookup_pick/
// lookup_candidate_block already use, T-232-15) -- the caller (decision_dump.cpp) MUST then
// compute fresh and flag the row (target_fact_dump_time_compute), never fabricate a value
// (T-232-14).
target_fact_snapshot lookup_target_fact_snapshot( const action_t* resolved, bool* out_found )
{
  auto it = g_target_fact_snapshot_table.find( resolved );
  if ( it == g_target_fact_snapshot_table.end() || !it->second.has_stamp )
  {
    if ( out_found )
      *out_found = false;
    return target_fact_snapshot{};
  }
  auto stamp_it = g_decision_stamp.find( resolved->player );
  std::uint64_t current_stamp = ( stamp_it == g_decision_stamp.end() ) ? 0 : stamp_it->second;
  if ( it->second.stamp != current_stamp )
  {
    if ( out_found )
      *out_found = false;
    return target_fact_snapshot{};
  }
  if ( out_found )
    *out_found = true;
  target_fact_snapshot out;
  out.has_is_current_target = it->second.has_is_current_target;
  out.is_current_target      = it->second.is_current_target;
  return out;
}

bool apply_head_pick( const action_t* resolved, player_t* replacement,
                       std::uint8_t replacement_slot )
{
  auto it = g_pick_table.find( resolved );
  if ( it == g_pick_table.end() || !it->second.has_stamp )
    return false;
  auto stamp_it = g_decision_stamp.find( resolved->player );
  std::uint64_t current_stamp = ( stamp_it == g_decision_stamp.end() ) ? 0 : stamp_it->second;
  if ( it->second.stamp != current_stamp )
    return false;

  // The row records what happened, never what was intended (must_haves): overwrite the STAMPED
  // pick so accept_cast()'s own lookup_pick() call -- reached AFTER this function returns, per
  // solver_control.cpp's own call ordering -- sees the replacement, not the rule's original.
  it->second.pick = replacement;

  auto block_it = g_candidate_block_table.find( resolved );
  if ( block_it != g_candidate_block_table.end() && block_it->second.has_stamp &&
       block_it->second.stamp == current_stamp )
    block_it->second.chosen_slot = replacement_slot;

  return true;
}

// 240-05 Task 2 (D1(a)): see this function's own doc comment in rl_target_select.hpp for the full
// contract. Implementation note: scores from the candidate block select() ALREADY captured for
// `resolved` this decision (`g_candidate_block_table`, the SAME staleness-checked access
// `lookup_candidate_block` uses) -- never rebuilds a fact, never re-walks target_non_sleeping_list.
// The block's own parallel `actors` array (candidate_block_slot, above) supplies the real
// player_t* each slot names, both for `candidate_is_better`'s final tie-break rung and for the
// `apply_head_pick` call below.
void run_target_head( const action_t* resolved, const float* context, bool observation_follows_head )
{
  if ( !resolved )
    return;
  const sim_t* sim = resolved->player->sim;
  if ( !sim->solver_policy_weights || !sim->solver_policy_weights->has_aim_head ||
       sim->target_scorer_force_rules )
    return;

  // 259-05b: the head scores only the spells its one-hot names (RL_AIM_SPELLS, seven today --
  // primordial_storm is targeted but has no column). A chosen action outside that list keeps the
  // rules' own aim, exactly as if no head were loaded; replaces the old eight-token assert, which
  // could not hold for a seven-spell section.
  std::size_t aim_spell_index = RL_AIM_SPELL_COUNT;
  for ( std::size_t k = 0; k < RL_AIM_SPELL_COUNT; ++k )
  {
    if ( resolved->name_str == RL_AIM_SPELLS[ k ] )
    {
      aim_spell_index = k;
      break;
    }
  }
  if ( aim_spell_index == RL_AIM_SPELL_COUNT )
    return;

  auto it = g_candidate_block_table.find( resolved );
  auto stamp_it = g_decision_stamp.find( resolved->player );
  const std::uint64_t current_stamp = ( stamp_it == g_decision_stamp.end() ) ? 0 : stamp_it->second;
  const bool found = ( it != g_candidate_block_table.end() && it->second.has_stamp &&
                        it->second.stamp == current_stamp );
  if ( !found || it->second.count == 0 )
  {
    // must_haves: no stamped block for this decision (never scored this decision -- e.g. an
    // action a decision boundary never reached -- or an overflowing rules-path decision that
    // captured no block, this file's own capture_block guard in select()) -- the rule's own aim
    // stands. Never throws, never truncates: at sixteen slots plus this counted fallback there is
    // nothing left to refuse mid-fight (the loader-time check, task 3, is what refuses a
    // structurally-wrong blob, once, at startup).
    ++g_target_head_no_block_count;
    return;
  }

  candidate_block_slot& slot = it->second;
  rl_policy::rl_weights_t& w = *resolved->player->sim->solver_policy_weights;
  rl_policy::rl_aim_t&     s = w.aim;
  float* feats = s.feature_scratch.data();
  static_assert( RL_AIM_INPUT_COUNT == RL_TARGET_FEATURES + RL_AIM_CONTEXT_COUNT,
                 "the aim head's per-candidate inputs are the declared facts plus the declared "
                 "context indicators (259-07)" );

  player_t*    current_target = resolved->player->target;
  player_t*    best           = nullptr;
  double       best_score     = 0.0;
  std::uint8_t best_slot      = 0;
  for ( std::size_t slot_index = 0; slot_index < RL_TARGET_SLOTS; ++slot_index )
  {
    // Masked slots are never scored at all -- the practical equivalent of the head's own -inf
    // mask convention (agent/target_scorer.py's docstring: masked slots score literal -inf, never
    // an additive penalty), achieved here by never entering them into the argmax in the first
    // place.
    if ( !( slot.mask & static_cast<std::uint16_t>( 1u << slot_index ) ) )
      continue;
    // Input layout (259-07, R2), the trainer's own order: [RL_TARGET_FEATURES scaled facts of
    // this slot (the very floats the translog logs) | RL_AIM_CONTEXT_COUNT context indicators
    // (0/1, constant across the decision's slots) | RL_AIM_SPELL_COUNT aiming-spell one-hot] --
    // RL_AIM_INPUT_COUNT floats of facts+context, then the one-hot, the same order the loader's
    // first-layer width check pins. One per-slot buffer, written in place; no allocation.
    std::memcpy( feats, slot.features.data() + slot_index * RL_TARGET_FEATURES,
                 RL_TARGET_FEATURES * sizeof( float ) );
    std::memcpy( feats + RL_TARGET_FEATURES, context, RL_AIM_CONTEXT_COUNT * sizeof( float ) );
    std::memset( feats + RL_AIM_INPUT_COUNT, 0, RL_AIM_SPELL_COUNT * sizeof( float ) );
    feats[ RL_AIM_INPUT_COUNT + aim_spell_index ] = 1.0f;
    const double score = static_cast<double>( rl_policy::forward_aim( s, feats ) );
    player_t*    c     = slot.actors[ slot_index ];
    if ( candidate_is_better( score, c, best_score, best, current_target ) )
    {
      best       = c;
      best_score = score;
      best_slot  = static_cast<std::uint8_t>( slot_index );
    }
  }

  if ( best != nullptr && apply_head_pick( resolved, best, best_slot ) && observation_follows_head )
  {
    // 259-07 (R1, R5): in `aim_obs_source = 1` mode the observation built right after this call
    // describes the head's pick -- record it as the block's observed slot BEFORE the random-aim
    // dial can overwrite the chosen slot. `apply_head_pick` just proved the block is current.
    slot.observed_slot = best_slot;
  }
}

std::uint64_t get_target_head_no_block_count()
{
  return g_target_head_no_block_count;
}

void reset( sim_t* )
{
  // WR-11 (260902/cr4): called from sim_t::reset() (sim.cpp), once per iteration -- clears every
  // module global (the original three, plus CR-04's deadlock counter added later the same task,
  // plus 230-04's candidate block table, plus 232-04's target-fact snapshot table, plus 232-06's
  // Chain Lightning hop-count stash, plus Task 2c's own two per-actor geometry/handle caches, plus
  // 240-05's run_target_head no-block counter) so a stale pick, stamp, count or cached geometry
  // from a PRIOR iteration can never leak into the next one. `sim` itself is unused (the tables
  // are keyed on `player_t*`, not sim identity, per this module's own single-sim/single-thread
  // precondition) but is taken by pointer to mirror raid_event_t::reset( sim )'s own signature at
  // the call site.
  g_decision_stamp.clear();
  g_pick_table.clear();
  g_candidate_block_table.clear();  // 230-04
  g_target_fact_snapshot_table.clear();  // 232-04 (OBS-02, R-T)
  g_chain_hop_stash.clear();  // 232-06 (RULE-02, R-Z)
  g_reresolution_counts = reresolution_counts{};
  g_every_targeted_action_illegal = 0;  // CR-04 (260902/cr4)
  g_target_head_no_block_count = 0;  // 240-05 (must_haves)
  // 260914-rbp Task 2c (ME-1): this file's own per-actor VB/Lava Lash geometry cache -- an
  // ACT-02-pattern cache is normally left unset for the life of a fight (the values it resolves
  // never change mid-fight), but leaving it populated ACROSS iterations means a later iteration's
  // player_t* -- reused at the same address once a prior iteration's player is torn down -- could
  // read a STALE geometry from an unrelated earlier actor. Cleared every iteration, same as the
  // five tables above.
  g_vb_lava_lash_geometry_cache.clear();
  // Phase 268 fix pass (WR-01): the per-actor binding cache holds raw action_t* pointers (the chooser probe, the melee
  // action and the by_action keys) that point into the actor's own action list, so an entry must never outlive its actor.
  // A later iteration's player_t* can reuse a torn-down actor's address; clearing here (the same place and for the same
  // reason as the geometry cache above) makes the next lookup rebuild from the live actor. The rebuild is a handful of
  // find_action calls per actor per iteration, off the per-decision path. g_aim_fact_gate_cache copies only the
  // feature_sources value table (indices and plain function pointers, no action pointer), so it does not share this hazard.
  g_actor_binding_cache.clear();
  // 260914-rbp Task 2c (ADD-2): the sibling per-actor find_action() handle cache
  // rl_policy_obs.cpp owns (a DIFFERENT translation unit) -- same stale-address hazard, same
  // per-iteration clear, routed through this file's own reset() since sim.cpp's sim_t::reset()
  // already calls rl_target_select::reset( this ) and there is no separate rl_policy::reset()
  // hook wired into that call site.
  rl_policy::clear_hits_action_handle_cache();
}

// 261005-fight-list (quick 261005-mix, plan 02): the fight-list driver's between-entry hygiene. reset() above
// empties the per-iteration tables every fight; this adds what reset() never touched -- g_aim_fact_gate_cache
// (keyed on a bare player_t*, filled once per actor, otherwise never cleared) and the three scratch vectors
// (whose contents are player_t* / floats from the last decision) -- and routes through reset() for the rest, so
// the two can never drift apart. Returns the count of entries still held (process_cache_entries()).
void clear_process_caches()
{
  reset( nullptr );
  g_aim_fact_gate_cache.clear();
  g_candidate_buffer.clear();
  g_unpruned_candidate_buffer.clear();
  g_rules_feature_scratch.clear();
  g_actor_binding_cache.clear();
}

// How many entries the process-wide state of this file holds right now: the keyed tables, the scratch
// vectors, and the one-fight counters (a non-zero counter counts as one entry).
std::size_t process_cache_entries()
{
  return g_decision_stamp.size() + g_pick_table.size() + g_candidate_block_table.size() +
         g_target_fact_snapshot_table.size() + g_chain_hop_stash.size() + g_vb_lava_lash_geometry_cache.size() +
         g_aim_fact_gate_cache.size() + g_candidate_buffer.size() + g_unpruned_candidate_buffer.size() +
         g_rules_feature_scratch.size() + actor_binding_entries() +
         ( ( g_reresolution_counts.kept_the_pick != 0 || g_reresolution_counts.fell_back_to_player_target != 0 ||
             g_reresolution_counts.left_no_op_boundary != 0 )
               ? 1u
               : 0u ) +
         ( g_every_targeted_action_illegal != 0 ? 1u : 0u ) + ( g_target_head_no_block_count != 0 ? 1u : 0u );
}

bool is_targeted_action( const action_t* resolved )
{
  if ( !resolved )
    return false;
  for ( std::size_t k = 0; k < RL_TARGETED_TOKEN_COUNT; ++k )
    if ( resolved->name_str == RL_TARGETED_TOKENS[ k ] )
      return true;
  return false;
}

// 228-09 (D-23/TGT-08, dump half) -- see rl_target_select.hpp's own doc comment.
std::size_t targeted_action_token_count()
{
  return RL_TARGETED_TOKEN_COUNT;
}

const char* const* targeted_action_tokens()
{
  return RL_TARGETED_TOKENS;
}

// 233.1-02 Task 2 (R6-15, R6-20): the Thorim's-aware strike SUBSTITUTION -- called once per
// decision from preference_for() below, for the two strike tokens only (windstrike, stormstrike).
// primordial_storm and lightning_bolt stay routed to preference_shortest_time_to_die directly by
// preference_for() itself; this function is never consulted for them. Gates, all read through the
// file's existing non-allocating idioms (buff_t::find / player_t::find_action -- never a fresh
// allocation per decision): the talent via find_action("thorims_invocation") being non-null;
// Maelstrom Weapon's stack count greater than zero; for Stormstrike ONLY, Doom Winds present via
// check() (sc_shaman.cpp:2126's own convention -- never up()).
//
// TWO branches once the gates pass, not three (R6-15/R6-20, this task): Tempest up -> best
// splash target (preference_tempest); otherwise -> best Chain Lightning chain start
// (preference_chain_lightning), treating every enemy as a chainable candidate. The former THIRD
// branch (232-05 RULE-01 / P232-29: a "lightning-bolt-primed-or-unprimed" case falling through to
// the base rule, driven by the removed `shaman_thorims_primed_kind` accessor's last-cast primer)
// is GONE -- the engine no longer reads a last-cast primer at all (233.1-02 Task 1:
// `shaman_t::thorims_can_chain` replaces it), so there is no "primed with Lightning Bolt" state
// left to model. With one enemy the chain start IS that enemy, so single-target aiming is
// unchanged.
//
// LO-04 (232-13): a shared modelling divergence, not merely a fork one -- these gates are read at
// DECISION time (this function runs before the cast resolves), while the engine evaluates the
// SAME gates at IMPACT time (`trigger_thorims_invocation`, called from `stormstrike_t::impact`/
// `windstrike_t::impact`). Maelstrom Weapon's stack count or the Tempest buff can change between
// the two, so this function is a close model of `trigger_thorims_invocation`, never an exact one
// -- the addon's own strike-aim rule shares this exact divergence (enhancementTargetRules.ts).
preference_fn preference_for_thorims_aware_strike( const action_t* resolved, bool is_stormstrike )
{
  player_t* p = resolved->player;

  if ( !p->find_action( "thorims_invocation" ) )
    return preference_shortest_time_to_die;

  buff_t* maelstrom_weapon = buff_t::find( p, "maelstrom_weapon" );
  if ( !maelstrom_weapon || maelstrom_weapon->check() == 0 )
    return preference_shortest_time_to_die;

  // Windstrike takes the Thorim's branch whenever the gates above pass; Stormstrike ADDITIONALLY
  // requires Doom Winds. 261002-8rs corrected this comment against the engine (sc_shaman.cpp, each
  // line re-read): Stormstrike's impact fires Thorim's Invocation only while the Doom Winds buff is
  // up (:5884-5888), Windstrike's impact unconditionally (:5926-5931). The Doom Winds buff has three
  // sources, one per variant in `doom_winds_t::execute` (:9298-9322): the Doom Winds BUTTON (pressable
  // only with neither Ascendance nor Deeply Rooted Elements, `ready()` :9338-9346), Ascendance's own
  // execute (the Ascendance variant is built at :11302) and a Deeply Rooted Elements proc (built at
  // :11296). So this branch is reachable on every hero tree, not dead code. (The earlier wording,
  // "triggered from Ascendance's own execute path", named one of the three sources only.)
  if ( is_stormstrike )
  {
    buff_t* doom_winds = buff_t::find( p, "doom_winds" );
    if ( !doom_winds || !doom_winds->check() )
      return preference_shortest_time_to_die;
  }

  buff_t* tempest = buff_t::find( p, "tempest" );
  if ( tempest && tempest->check() )
    return preference_tempest;

  return preference_chain_lightning;
}

// 268-02 (G268-5): the aiming rule of each targeted token is the one the spec header's RL_RULE_PREFS
// names for it, resolved through the generic rules or the actor's class plugin (the bind is above,
// actor_binding_for). The per-spell reasoning for each scorer stays with the scorer's own definition below.
// A resolved action whose name has no row uses current_target (the generic default of contract C-A), so
// this never returns null for a real action.
//
// 261002-8rs (batch 261002-8rq, item 2), kept from the old name chain: the Flame Shock button is aimed by the
// Voltaic Blaze rule (the header row for it names that rule). That rule's score is (new Flame Shocks inside
// Voltaic Blaze's cleave) x 1e12 + (1e9 if the candidate lacks THIS caster's Flame Shock) + time to die. For
// Flame Shock the first term is always 0 (the button is not ready whenever Voltaic Blaze is taken, and without
// the talent the cleave radius stays 0), so what remains is "an enemy without this caster's Flame Shock first,
// the longest-lived first within each group". Rejected: the shortest-time-to-die rule (re-applies on a target
// that already carries it), the Lava Lash rule (prefers carriers), the chain and tempest rules (no Flame Shock
// state read).
//
// Phase 230-02 (SCOR-01, D-01/D-02/R-K) added a run-time switch here -- the PRESENCE of a
// scorer section in the loaded weights used to swap the rule out for a ninth "scorer"
// preference entirely. REMOVED 240-05 Task 2 (D1(a)/D8(a)): the rule now runs UNCONDITIONALLY,
// on every decision, for every arm -- rules, comparator, and both learning arms alike. This is
// what makes the RULES comparator free (the same binary, the same rule, differing only in
// whether a loaded head's own overwrite -- run_target_head, called separately from
// rl_policy_obs.cpp after this rule has already run -- is allowed to touch the pick this
// function's caller stamped). `preference_for` has no branch returning a scorer
// preference; there is no `sim`/`solver_policy_weights` read left in this function.
preference_fn preference_for( const action_t* resolved )
{
  if ( !resolved )
    return nullptr;
  actor_binding_t& binding = actor_binding_for( resolved->player );
  auto             it      = binding.by_action.find( resolved );
  if ( it == binding.by_action.end() )
  {
    rule_resolver_fn resolver = resolve_generic_current_target;
    for ( const rule_row_t& row : binding.rule_rows )
      if ( row.first == resolved->name_str )
      {
        resolver = row.second;
        break;
      }
    it = binding.by_action.emplace( resolved, resolver ).first;
  }
  return it->second( resolved );
}

double preference_current_target( const action_t*, const enemy_fact& )
{
  return 0.0;
}

double preference_shortest_time_to_die( const action_t* a, const enemy_fact& fact )
{
  // OR-1 (owner ruling 2026-09-02, QUESTIONS Q15 alternative (b), 228-04 Task 1 Step 0c):
  // supersedes ledger P228-7 (R-B, "keep D-10 as written") and P228-24. Owner's reasoning
  // verbatim: adds die first, so the single-target spells should finish them rather than parking
  // on the boss. D-15's "outlives the cast" dominance clause is UNCHANGED -- a candidate that
  // will still be alive after this cast lands always outranks one that will not, so an
  // about-to-despawn add never wins over one that will actually survive to be hit. What inverted
  // is the ordering WITHIN each group: a SHORTER time to die now scores higher (subtracted from
  // the dominance offset rather than added to it), so among several valid outliving-the-cast
  // candidates this preference now parks on the shortest-lived one -- exactly the opposite of
  // this function's pre-OR-1 name and behaviour (renamed from the pre-228-04 longest-lived-preferring function of the same shape).
  // R-B's measured consequence INVERTS too: under the rig's fixed_time=1, a boss's time_to_die is
  // the whole fight clock with no per-actor term (research 3.3) -- since 600.0 (D-16's clip) is
  // far larger than any real add's remaining life, the boss's dominance-offset score
  // (1.0e9 - time_to_die) is now always LOWER than any add's, so this preference parks on the
  // shortest-lived valid add and falls back to the boss only when no add passes generic_filter.
  // If NO candidate outlives the cast (every one would be excluded), this still returns a valid,
  // ordered answer via the plain time_to_die term below -- never "invalid" (228-02 ledger row:
  // D-10's own text spells this fallback out explicitly only for Voltaic Blaze; the same
  // reasoning is applied here since a preference must never return null). The dominance offset
  // (1.0e9) stays far larger than any time-to-die the D-16 clip allows (600.0 s) after inversion,
  // so an outliving candidate can never score below a non-outliving one.
  // IN-04 (260902/cr4): stated explicitly, since the two branches order in OPPOSITE directions --
  // the OUTLIVING group (candidates that survive past this cast) orders SHORTEST-time-to-die
  // first (1.0e9 - time_to_die: subtracting less scores higher for a smaller time_to_die); the
  // NON-outliving group (candidates that will not survive to be hit) orders LONGEST-time-to-die
  // first (the bare time_to_die term: a larger raw value scores higher), i.e. closest to actually
  // surviving the cast. Behaviour is UNCHANGED by this comment -- see ledger row for this task.
  double outlives_margin = fact.time_to_die - a->execute_time().total_seconds();
  return ( outlives_margin > 0.0 ) ? ( 1.0e9 - fact.time_to_die ) : fact.time_to_die;
}

double preference_lava_lash( const action_t* a, const enemy_fact& fact )
{
  // NEW (260914-rbp Task 1 Step 6b, rulings Q16/R15): among Flame Shock carriers, the carrier
  // whose OWN 12-yd spread would plant the MOST new Flame Shocks
  // (enemy_fact::lava_lash_spread_within_12yd) wins; tie-break SHORTEST remaining -- kept from
  // the OLD rule (below) so among equally-good spreads the more urgent refresh still wins.
  // Carriers still dominate non-carriers via the SAME 2.0e9 offset the OLD rule used; the extra
  // 1.0e12-per-spread-unit term sits far enough above that offset that ordering is decided by
  // spread count FIRST, remaining SECOND, for any realistic spread count (single digits) --
  // re-checked, not assumed: 1.0e12 * 1 already dwarfs the ENTIRE 2.0e9 carrier range. Non-carrier
  // fallback UNCHANGED (calls the SAME flipped base preference, D-03: no third copy of any rule).
  //
  // OLD rule (superseded 260914-rbp, kept here for the record): among Flame Shock carriers in
  // reach the shortest remaining wins (offset above every non-carrier so carriers are always
  // preferred), copying the STRUCTURE of shortest_duration_target() (sc_shaman.cpp:6877-6911) --
  // `return fact.flame_shock_remaining > 0.0 ? 2.0e9 - fact.flame_shock_remaining :
  // preference_shortest_time_to_die( a, fact );`.
  if ( fact.flame_shock_remaining > 0.0 )
  {
    double primary = static_cast<double>( fact.lava_lash_spread_within_12yd ) * 1.0e12;
    return primary + ( 2.0e9 - fact.flame_shock_remaining );
  }
  return preference_shortest_time_to_die( a, fact );
}

double preference_voltaic_blaze( const action_t*, const enemy_fact& fact )
{
  // NEW (260914-rbp Task 1 Step 6b, rulings Q16/R15): the candidate whose OWN 10-yd cleave would
  // plant the MOST new Flame Shocks (enemy_fact::vb_new_flame_shocks_within_10yd) wins; the OLD
  // rule's own no-carrier-first dominance clause is KEPT as the tie-break's SECOND key (so a
  // fully-dotted pack, where every candidate reads 0 new Flame Shocks, still separates on
  // carrier state rather than collapsing to an arbitrary tie); longest-lived is the tie-break's
  // THIRD/final key. The 1.0e12-per-new-Flame-Shock primary term dwarfs the 1.0e9 dominance
  // clause and the <=600s time_to_die term for any realistic count, so ordering is decided by
  // new-Flame-Shock count FIRST, carrier state SECOND, longest-lived THIRD.
  //
  // OLD rule (superseded 260914-rbp, kept here for the record): without Flame Shock outranks
  // with it (offset), ordered by longest-lived within each group -- if every candidate carries
  // Flame Shock this still returns the longest-lived carrier, never "invalid" (the same reason
  // the always-legal wait exists) -- `return (fact.flame_shock_remaining <= 0.0 ? 1.0e9 : 0.0) +
  // fact.time_to_die;`.
  //
  // 260914-rbp Task 2c (ME-3): Q16 is delivered ONLY with the INCLUDING-centre +1 --
  // `vb_new_flame_shocks_within_10yd` itself already counts the candidate's OWN Flame-Shock
  // absence as one of its "new Flame Shocks" (its own field comment, rl_target_select.hpp:88:
  // "+1 if THIS candidate itself lacks the Flame Shock ... a splash always hits its centre"),
  // never just the secondary cleave hits around it -- the same convention `hits.voltaic_blaze.
  // new_flame_shocks` applies on the training-observation side (HIT-INPUTS-DESIGN.md §2.0).
  double primary   = static_cast<double>( fact.vb_new_flame_shocks_within_10yd ) * 1.0e12;
  double dominance = ( fact.flame_shock_remaining <= 0.0 ? 1.0e9 : 0.0 );
  return primary + dominance + fact.time_to_die;
}

double preference_chain_lightning( const action_t* a, const enemy_fact& fact )
{
  // RULE-02 (232-06, R-Z): reads the greedy hop-count stash `select()` computed just before the
  // scoring loop that calls this function (compute_chain_hop_counts, above), keyed on `a` and this
  // decision's stamp -- REPLACES the retired deterministic neighbour-count approximation this
  // function used before this plan. `preference_tempest` below keeps that retired approximation
  // (R5-20: the CL chain simulation is the FORK's own selector pick; the rules-module/tempest
  // neighbour-count pick stays offline/parity-only for Tempest) -- the two stay two distinct
  // function pointers (R-U) so this substitution touches exactly one dispatch slot.
  auto slot_it = g_chain_hop_stash.find( a );
  if ( slot_it != g_chain_hop_stash.end() && slot_it->second.has_stamp &&
       slot_it->second.stamp == current_decision_stamp( a->player ) )
  {
    auto hop_it = slot_it->second.hop_counts.find( fact.candidate );
    if ( hop_it != slot_it->second.hop_counts.end() )
      return static_cast<double>( hop_it->second ) * 1.0e6 + fact.time_to_die;
    // Defensive (should be unreachable when select() drove this call -- fact.candidate came from
    // the SAME `candidates` vector compute_chain_hop_counts just walked): the stash exists and is
    // current for `a`, but carries no entry for THIS candidate specifically. Flag the fallback
    // visibly on the ALREADY-stamped entry rather than silently reusing the retired approximation
    // below.
    slot_it->second.used_fallback = true;
    return static_cast<double>( fact.neighbours_within_radius ) * 1.0e6 + fact.time_to_die;
  }
  // HI-04 (232-13): reached when no stash entry exists for `a` at all this decision -- e.g. a
  // caller invoking this function directly rather than through select()'s own
  // pref==preference_chain_lightning gate (the only path today that fills the stash). Previously
  // this fell straight through to the neighbour-count expression with NO record of the fallback,
  // so chain_hop_fallback_used() could never report the one case its own doc comment names as
  // reachable -- the getter and this setter now agree. Stamp a fresh entry (empty hop_counts,
  // used_fallback=true) so the getter has something CURRENT to read, mirroring g_pick_table's own
  // "stamp before any early return" discipline (select()'s scorer branch, above). The fallback
  // value returned below is the retired neighbour-count approximation this function used before
  // RULE-02 -- labelled per R-D, never a prediction of the engine's own randomised chain walk
  // (sc_shaman.cpp:982-1057).
  chain_hop_slot& fresh = g_chain_hop_stash[ a ];
  fresh.hop_counts.clear();
  fresh.stamp         = current_decision_stamp( a->player );
  fresh.has_stamp     = true;
  fresh.used_fallback = true;
  return static_cast<double>( fact.neighbours_within_radius ) * 1.0e6 + fact.time_to_die;
}

// RULE-02 (232-06, R-Z): see this function's own declaration (rl_target_select.hpp) for the
// staleness-guard contract -- mirrors lookup_pick's has_stamp/stamp==current discipline exactly.
bool chain_hop_fallback_used( const action_t* resolved )
{
  auto it = g_chain_hop_stash.find( resolved );
  if ( it == g_chain_hop_stash.end() || !it->second.has_stamp )
    return false;
  auto stamp_it = g_decision_stamp.find( resolved->player );
  std::uint64_t current_stamp = ( stamp_it == g_decision_stamp.end() ) ? 0 : stamp_it->second;
  if ( it->second.stamp != current_stamp )
    return false;
  return it->second.used_fallback;
}

double preference_tempest( const action_t*, const enemy_fact& fact )
{
  // BL-01 (232-13): fact.neighbours_within_radius is now always computed at the GEOMETRY SOURCE's
  // own resolved radius (Tempest's, measured 8.0), resolved by name via
  // resolve_thorims_branch_geometry regardless of whether the caller was Tempest itself or a
  // Thorim's-primed melee strike (RULE-01) -- never the caller's own (possibly zero) radius. Same
  // formula as chain_lightning's, applied to Tempest's own resolved radius -- the addon's 8-yard
  // cluster-centre rule.
  return static_cast<double>( fact.neighbours_within_radius ) * 1.0e6 + fact.time_to_die;
}

// ---------------------------------------------------------------------------------------------
// Phase 230-02 (SCOR-01) used to declare the ninth preference, the learned scorer, here --
// REMOVED 240-05 Task 2 (D1(a)/D8(a)): `preference_for()` no longer ever swaps the rule out for
// it, so this pointer would be dead code. Its arithmetic is FOLDED into `run_target_head` (above,
// beside `apply_head_pick`) -- same fill order (`fill_candidate_features`, unchanged), same
// one-hot (`targeted_action_tokens()`'s own order, CK1-1), same `rl_policy::forward_scorer` call
// -- reading the candidate block `select()` already captured instead of a single passed-in
// `fact`, since the head scores every slot at once rather than once per `select()` candidate-loop
// iteration.
// ---------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------
// Shaped spells (228-03). Pure geometry -- plain doubles, no engine pointer beyond what the
// candidate loop below needs to read positions and combat_reach.
// ---------------------------------------------------------------------------------------------

bool crash_lightning_cone_contains( double px, double py, double fx, double fy, double cx, double cy,
                                     double bounding_allowance )
{
  double dx   = cx - px;
  double dy   = cy - py;
  double dist = std::sqrt( dx * dx + dy * dy );
  if ( dist - bounding_allowance > CRASH_LIGHTNING_CONE_RADIUS_YARDS )
    return false;
  if ( dist <= 0.0 )
    return true;  // coincident with the player: no meaningful "behind" for a zero-length vector.
  double dot = ( fx * dx + fy * dy ) / dist;
  return dot >= CRASH_LIGHTNING_CONE_COS_HALF_ANGLE;
}

bool sundering_rect_contains( double px, double py, double fx, double fy, double cx, double cy,
                               double bounding_allowance )
{
  // Facing-axis (fx,fy) and perpendicular (rotate facing 90 degrees: (-fy,fx)) projections. WR-02
  // (260902/cr4): allowance applied ALONG only, dropped from PERP -- convention stated once in
  // rl_target_select.hpp beside the shape constants.
  double dx = cx - px;
  double dy = cy - py;
  double along = dx * fx + dy * fy;
  double perp  = std::fabs( dx * ( -fy ) + dy * fx );
  return along >= -bounding_allowance && along <= SUNDERING_RECT_LENGTH_YARDS + bounding_allowance &&
         perp <= SUNDERING_RECT_HALF_WIDTH_YARDS;
}

// OR-2 (owner ruling 2026-09-02, QUESTIONS Q1): Crash Lightning and Sundering get NO selector
// and never turn the player. Wave 3 (clone a613171c41) wired `preference_shaped_crash_lightning`,
// `preference_shaped_sundering` and the `shaped_score` helper into `select()` under a superseded
// turn-toward-shape default (228-CONTEXT.md D-10's shaped row / ledger section 0 R2-1) -- all
// three are REMOVED here (228-04 Task 1 Step 0b). The geometry predicates immediately below
// (`crash_lightning_cone_contains`, `sundering_rect_contains`) are KEPT as the ONE shared copy:
// `sc_shaman.cpp`'s AoE hit filters and this plan's own descriptive shape facts (228-04 Task 2)
// both read them from the CURRENT facing, never a hypothetical one.

void retarget( action_t* a, player_t* p, player_t* pick )
{
  // WR-07 (260902/cr4): action_t::set_target -- NEVER a raw `a->target = pick` write (it skips the
  // AoE target-cache invalidation, leaving a stale cache and a silently wrong hit set).
  a->set_target( pick );
  // 2026-10-02, 266-09 (research R5, owner F9): in a funnel-mode fight an aimed cast goes where it was aimed
  // (the action's own target above) but does NOT move the player's own target or either weapon swing: they stay
  // on the chosen enemy (the tag), the way the live game keeps them. Flag off: today's writes, unchanged (R8).
  // The mid-cast fallback in action.cpp calls this with the player's own target, so it needs no gate of its own.
  if ( !p->sim->solver_funnel_mode )
  {
    p->target = pick;
    if ( p->main_hand_attack )
      p->main_hand_attack->set_target( pick );
    if ( p->off_hand_attack )
      p->off_hand_attack->set_target( pick );
  }
  // The turn (T2) formerly here -- both callers (accept_cast, the mid-cast re-resolution
  // ladder's fallback arm) turning the player toward `pick` -- is DELETED (R6-8, owner,
  // 2026-09-07). `pick` never reaches this function unless it already passed
  // generic_filter's G4, so there is nothing left to turn toward.
}

void record_reresolution( reresolution_arm arm )
{
  switch ( arm )
  {
    case reresolution_arm::kept_the_pick:              ++g_reresolution_counts.kept_the_pick; break;
    case reresolution_arm::fell_back_to_player_target:  ++g_reresolution_counts.fell_back_to_player_target; break;
    case reresolution_arm::left_no_op_boundary:         ++g_reresolution_counts.left_no_op_boundary; break;
  }
}

reresolution_counts get_reresolution_counts()
{
  return g_reresolution_counts;
}

void record_every_targeted_action_illegal()
{
  ++g_every_targeted_action_illegal;
}

std::uint64_t get_every_targeted_action_illegal_count()
{
  return g_every_targeted_action_illegal;
}

} // namespace rl_target_select
