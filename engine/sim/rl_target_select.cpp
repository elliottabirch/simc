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
#include "player/player.hpp"
#include "sim/rl_policy.hpp"
#include "sim/rl_translog.hpp"
#include "sim/sim.hpp"
#include "util/util.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace rl_target_select
{
namespace
{

// The eight targeted registry tokens this plan governs (228-02-PLAN.md's TARGETED constant,
// re-derived from scripts/rl/specs/enhancement.json's specs.enhancement.actions[] at planning
// time). SHAPED (crash_lightning, sundering) is plan 228-03's, deliberately absent here.
//
// 230-02 (CK1-1, owner ruling Q1): this is also the target head's own one-hot registry (240-05
// Task 2) -- run_target_head (below) walks this SAME array for its aiming-spell one-hot, so the
// shaped pair can never reach the head either. Settled once, not a run-time discovery: Crash
// Lightning and Sundering cast in the current facing direction only, have no selector and never
// turn, so they have no pick to score and no column to occupy.
constexpr const char* TARGETED_TOKENS[] = {
  "stormstrike", "lightning_bolt", "chain_lightning", "tempest",
  "windstrike",  "lava_lash",      "voltaic_blaze",   "primordial_storm",
};

// 259-05b (fork open question 3): every spell the aim head's one-hot names (RL_AIM_SPELLS, the
// registry-generated list) must be one of the targeted tokens above -- the head can only ever
// aim a spell this module resolves a pick for. Proved at COMPILE time (strictly stronger than a
// load-time assert: a build that violates it does not exist). The loader separately pins the
// file's declared count and registry sha against the same generated tables.
constexpr bool aim_token_equal( const char* a, const char* b )
{
  while ( *a != '\0' && *a == *b )
  {
    ++a;
    ++b;
  }
  return *a == *b;
}
constexpr bool aim_spells_all_targeted()
{
  for ( std::size_t k = 0; k < RL_AIM_SPELL_COUNT; ++k )
  {
    bool found = false;
    for ( const char* token : TARGETED_TOKENS )
      found = found || aim_token_equal( RL_AIM_SPELLS[ k ], token );
    if ( !found )
      return false;
  }
  return true;
}
static_assert( aim_spells_all_targeted(),
               "every RL_AIM_SPELLS token must appear in TARGETED_TOKENS (259-05b)" );

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
    if ( other == candidate || !other->is_enemy() )
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
    if ( other == candidate || !other->is_enemy() )
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

bool generic_filter( const action_t* a, player_t* candidate, bool harmful )
{
  // G1 -- alive.
  if ( candidate->is_sleeping() )
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

// tstl-sylvanas 260928-tb8: observation-only non-boss mask for trash pulls. Engine mechanics keep
// player_t::is_boss() untouched (the shaman APL never reads it; the only mechanical use is dungeon-style
// priority damage), so only what the net and the decision dump see changes.
bool obs_is_boss( const player_t* enemy )
{
  return enemy->is_boss() && enemy->sim->fight_style != FIGHT_STYLE_TRASH_PACK;
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

  // WR-04 (260902/cr4): `find_dot` -- a non-allocating scan of the candidate's existing dot_list --
  // instead of `get_dot`, which CREATES a dot_t on every candidate that has never been Flame
  // Shocked, growing `dot_list` on what this function's own callers treat as a read-only query.
  // Flame Shock on an ARBITRARY enemy, not only the current target (P-5's fix point).
  dot_t* fs = candidate->find_dot( "flame_shock", a->player );
  f.flame_shock_remaining = fs ? fs->remains().total_seconds() : 0.0;

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
      if ( other == candidate || !other->is_enemy() )
        continue;
      if ( candidate->get_player_distance( *other ) <= a->radius + other->combat_reach )
        ++neighbours;
    }
  }
  f.neighbours_within_radius = neighbours;

  f.is_current_target = ( candidate == a->player->target );
  f.actor_index        = candidate->actor_index;
  f.actor_spawn_index  = candidate->actor_spawn_index;

  // 228-10 Task 1 Step 1 (D-03/D-16): the missing per-enemy facts, added ONCE here and read from
  // here by the dump (and later the observation) -- never a second copy. Every lookup uses the
  // non-allocating `find`/`find_dot` idiom (WR-04's own convention above): buff_t::find() scans
  // buff_list without creating; player_t::find_dot() scans dot_list without creating. `source`
  // is always `a->player` -- these are debuffs THIS actor puts on the candidate, mirroring
  // flame_shock_remaining's own `a->player` source above.
  if ( buff_t* bc = buff_t::find( candidate, "burning_core", a->player ) )
    f.burning_core_remaining = bc->remains().total_seconds();
  if ( buff_t* lr = buff_t::find( candidate, "lightning_rod", a->player ) )
  {
    f.lightning_rod_stacks    = lr->check();
    f.lightning_rod_remaining = lr->remains().total_seconds();
  }
  if ( dot_t* vf = candidate->find_dot( "venomfang", a->player ) )
    f.venomfang_remaining = vf->remains().total_seconds();
  if ( buff_t* vfd = buff_t::find( candidate, "venomfang_debuff", a->player ) )
  {
    f.venomfang_debuff_stacks    = vfd->check();
    f.venomfang_debuff_remaining = vfd->remains().total_seconds();
  }
  if ( dot_t* ruf = candidate->find_dot( "rune_of_unleashed_fire_lingering", a->player ) )
    f.rune_of_unleashed_fire_lingering_remaining = ruf->remains().total_seconds();

  // 260914-rbp Task 1 Step 6 (R15, rulings Q16): the targeting-lens fields -- see enemy_fact's
  // own per-field comments (rl_target_select.hpp) for exactly what each counts and why the
  // ordering is fixed. Computed for EVERY caller (this is the FULL builder, "every field
  // filled" per this function's own header comment), unlike build_enemy_fact_for_scoring's own
  // pref-conditional lite computation below.
  // 260914-rbp Task 2c (NOTE-1): the stash lookup itself is the gate -- chain_hop_count_for_start
  // already returns 0 when `a` has no stash entry or the entry's stamp is stale (its own doc
  // comment above), so the `a->name_str == "chain_lightning"` name check this used to require was
  // a second, redundant gate that additionally (silently) zeroed a Thorim's-routed melee strike's
  // real hop count -- compute_chain_hop_counts fills the stash keyed on whatever action_t* select()
  // resolved geometry for (line ~797), not only literal chain_lightning casts.
  f.chain_hop_count = chain_hop_count_for_start( a, candidate );
  {
    const vb_lava_lash_geometry_t& geo = resolve_vb_lava_lash_geometry( a->player );
    // Task 2b (Q17 review, A2): VB's cleave always hits (and can Flame-Shock) its own pick
    // (include_pick=true); Lava Lash's spread pick is the SOURCE carrier, never a spread target
    // (include_pick=false, unchanged).
    f.vb_new_flame_shocks_within_10yd =
        count_new_flame_shock_neighbours( a->player, candidate, geo.vb_radius, geo.vb_cap, true );
    f.lava_lash_spread_within_12yd = count_new_flame_shock_neighbours(
        a->player, candidate, geo.lava_lash_radius, geo.lava_lash_cap, false );
  }

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
  for ( player_t* t : a->sim->target_non_sleeping_list )
    if ( t->is_enemy() && generic_filter( a, t, harmful ) )
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
    // Attempts to reproduce a WITHOUT-Chaining-Storms baseline by re-asserting a stripped
    // `spec_talents=` line LATER in the same profile (including an entirely EMPTY one) did NOT
    // change either this cap or the unrelated, independently-observable
    // `has_talent_thorims_invocation` dump field -- i.e. that override methodology itself does
    // not take effect for an `input=`-included profile in this build, so this session could NOT
    // conclusively measure the WITHOUT-talent case. Given that, this refuses on anything OTHER
    // than the two values either side of the addon's own ternary ({3, 5}) rather than asserting
    // a single unconfirmed number -- protects against a genuinely wrong cap (e.g. a future
    // spell-data change moving it to some third value) without overclaiming a with/without-talent
    // causal link this session did not establish. See the addon-parity todo for the open
    // question and how to close it properly (a real in-game or clean-profile measurement).
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

  if ( pref == preference_lava_lash || pref == preference_voltaic_blaze )
  {
    dot_t* fs = candidate->find_dot( "flame_shock", a->player );  // WR-04
    f.flame_shock_remaining = fs ? fs->remains().total_seconds() : 0.0;

    // 260914-rbp Task 1 Step 6 (R15, rulings Q16): only the ONE new targeting-lens field the
    // DISPATCHED preference actually reads -- WR-05's own "skip what the dispatched preference
    // doesn't need" discipline, applied here exactly like flame_shock_remaining just above.
    // 260914-rbp Task 2c (LO-3): named `vb_ll_geo`, not `geo` -- this function's own parameter
    // (`const chain_geometry* geo`, the Thorim's branch geometry above) is a DIFFERENT type this
    // local used to shadow.
    const vb_lava_lash_geometry_t& vb_ll_geo = resolve_vb_lava_lash_geometry( a->player );
    // Task 2b (Q17 review, A2): same include_pick convention as build_enemy_fact above.
    if ( pref == preference_voltaic_blaze )
      f.vb_new_flame_shocks_within_10yd = count_new_flame_shock_neighbours(
          a->player, candidate, vb_ll_geo.vb_radius, vb_ll_geo.vb_cap, true );
    else  // preference_lava_lash
      f.lava_lash_spread_within_12yd = count_new_flame_shock_neighbours(
          a->player, candidate, vb_ll_geo.lava_lash_radius, vb_ll_geo.lava_lash_cap, false );
  }

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
      if ( other == candidate || !other->is_enemy() )
        continue;
      if ( candidate->get_player_distance( *other ) <= geo->radius + other->combat_reach )
        ++neighbours;
    }
    f.neighbours_within_radius = neighbours;
  }
  else if ( a->radius > 0.0 )
  {
    // Defensive generality only -- no token in TARGETED_TOKENS reaches this branch today (every
    // OTHER registry preference is single-target, so `a->radius` stays 0); kept so a future
    // radius-bearing registry action does not silently skip the neighbour count.
    int neighbours = 0;
    for ( player_t* other : a->sim->target_non_sleeping_list )
    {
      if ( other == candidate || !other->is_enemy() )
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
};
std::unordered_map<const player_t*, aim_fact_gate> g_aim_fact_gate_cache;

const aim_fact_gate& aim_fact_gate_for( player_t* p )
{
  auto it = g_aim_fact_gate_cache.find( p );
  if ( it != g_aim_fact_gate_cache.end() )
    return it->second;
  aim_fact_gate gate;
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
// 259-07: the fill order is now the registry's own declaration, `RL_TARGET_FACT_LIST` (generated
// into rl_policy_constants.h): expanding it below makes the compiler prove every declared fact is
// a real `enemy_fact` member, in declared order -- a reorder or a rename in the registry fails to
// compile here rather than silently mis-ordering a wire. Each fact is SCALED into the registry's
// range and capability-gated (see scaled_fact above); the rules never read this scaled block (they
// rank on `enemy_fact`'s raw values -- build_enemy_fact's 600 s time-to-die cap stays, the 60 s
// cap lives only in the scaling's clipDiv).
void fill_candidate_features( const action_t*, const enemy_fact& fact, float* out,
                              const aim_fact_gate& gate )
{
  std::size_t i = 0;
#define RL_FILL_TARGET_FACT( NAME )                                              \
  out[ i ] = scaled_fact( static_cast<float>( fact.NAME ), i, gate );           \
  ++i;
  RL_TARGET_FACT_LIST( RL_FILL_TARGET_FACT )
#undef RL_FILL_TARGET_FACT

  assert( i == RL_TARGET_FEATURES &&
          "fill_candidate_features's fill order does not match RL_TARGET_FEATURES" );
}

// The number of facts the registry's X-macro list declares must equal the compiled width.
constexpr std::size_t aim_fact_list_count()
{
  std::size_t n = 0;
#define RL_COUNT_TARGET_FACT( NAME ) ++n;
  RL_TARGET_FACT_LIST( RL_COUNT_TARGET_FACT )
#undef RL_COUNT_TARGET_FACT
  return n;
}
static_assert( aim_fact_list_count() == RL_TARGET_FEATURES,
               "RL_TARGET_FACT_LIST must declare exactly RL_TARGET_FEATURES facts (259-07)" );

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
    if ( t->is_enemy() && generic_filter( a, t, harmful ) )
      candidates.push_back( t );

  // TGT-02 edge: empty -- no pick, legality bit goes to 0, the unanchored wait stays legal.
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
    compute_chain_hop_counts( precomputed_geo.radius, precomputed_geo.cap, candidates, hop_slot.hop_counts );
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
  // 260914-rbp Task 2c (ADD-2): the sibling per-actor find_action() handle cache
  // rl_policy_obs.cpp owns (a DIFFERENT translation unit) -- same stale-address hazard, same
  // per-iteration clear, routed through this file's own reset() since sim.cpp's sim_t::reset()
  // already calls rl_target_select::reset( this ) and there is no separate rl_policy::reset()
  // hook wired into that call site.
  rl_policy::clear_hits_action_handle_cache();
}

bool is_targeted_action( const action_t* resolved )
{
  if ( !resolved )
    return false;
  for ( const char* token : TARGETED_TOKENS )
    if ( resolved->name_str == token )
      return true;
  return false;
}

// 228-09 (D-23/TGT-08, dump half) -- see rl_target_select.hpp's own doc comment.
std::size_t targeted_action_token_count()
{
  return sizeof( TARGETED_TOKENS ) / sizeof( TARGETED_TOKENS[ 0 ] );
}

const char* const* targeted_action_tokens()
{
  return TARGETED_TOKENS;
}

namespace
{
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
  // requires Doom Winds -- it is triggered from Ascendance's own execute path and Ascendance IS an
  // agent action (232-RESEARCH.md Addendum C4), so this branch is reachable in production, not
  // dead code.
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
} // anonymous namespace

preference_fn preference_for( const action_t* resolved )
{
  if ( !resolved )
    return nullptr;
  const std::string& n = resolved->name_str;
  preference_fn rule = nullptr;
  if ( n == "stormstrike" || n == "windstrike" )
    rule = preference_for_thorims_aware_strike( resolved, n == "stormstrike" );
  else if ( n == "primordial_storm" || n == "lightning_bolt" )
    rule = preference_shortest_time_to_die;
  else if ( n == "lava_lash" )
    rule = preference_lava_lash;
  else if ( n == "voltaic_blaze" )
    rule = preference_voltaic_blaze;
  else if ( n == "chain_lightning" )
    rule = preference_chain_lightning;
  else if ( n == "tempest" )
    rule = preference_tempest;
  else
    return nullptr;

  // Phase 230-02 (SCOR-01, D-01/D-02/R-K) added a run-time switch here -- the PRESENCE of a
  // scorer section in the loaded weights used to swap the rule out for a ninth "scorer"
  // preference entirely. REMOVED 240-05 Task 2 (D1(a)/D8(a)): the rule now runs UNCONDITIONALLY,
  // on every decision, for every arm -- rules, comparator, and both learning arms alike. This is
  // what makes the RULES comparator free (the same binary, the same rule, differing only in
  // whether a loaded head's own overwrite -- run_target_head, called separately from
  // rl_policy_obs.cpp after this rule has already run -- is allowed to touch the pick this
  // function's caller stamped). `preference_for` no longer has any branch returning a scorer
  // preference; there is no `sim`/`solver_policy_weights` read left in this function.
  return rule;
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
  p->target = pick;
  if ( p->main_hand_attack )
    p->main_hand_attack->set_target( pick );
  if ( p->off_hand_attack )
    p->off_hand_attack->set_target( pick );
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
