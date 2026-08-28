// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// In-process RL transport, Stage 1 (read_state) and Stage 2 (build_obs/
// build_mask/build_wait) -- tstl-sylvanas phase 210, plans 210-04/210-05R/
// 210-CR-FIX. See rl_policy.hpp's own header comment for the
// NORMATIVE-source note: this file is an implementation of
// `scripts/rl/obs.py` and `scripts/rl/mask.py` (this repo), never the
// other way around.
//
// The "TWO DELIBERATE GAPS IN THIS SLICE" this comment used to describe
// (plan 210-04's objective) were closed by 210-05R (commit 1b66cde740):
// build_obs is REAL over all three lookup_status outcomes
// (present/permanent/absent), not merely "absent" for every non-derived
// field. build_mask/build_wait/cd_ready_now were already real as of
// 210-04. Nothing in this file is stubbed.

#include "sim/rl_policy.hpp"

#include "action/attack.hpp"
#include "buff/buff.hpp"
#include "player/consumable.hpp"
#include "player/player.hpp"
#include "sim/cooldown.hpp"
#include "sim/decision_dump.hpp"
#include "sim/event.hpp"
#include "sim/expressions.hpp"
#include "sim/raid_event.hpp"
#include "sim/sim.hpp"
#include "util/io.hpp"

#include "fmt/format.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <set>
#include <unordered_map>

namespace rl_policy
{
// ---------------------------------------------------------------------------
// rl_state_t lookups -- absent is a distinct state, never a zero default.
// ---------------------------------------------------------------------------

const buff_reading* rl_state_t::find_buff( const char* name ) const
{
  for ( const buff_reading& b : buffs )
  {
    if ( b.name == name )
      return &b;
  }
  return nullptr;
}

const cooldown_reading* rl_state_t::find_cooldown( const char* name ) const
{
  for ( const cooldown_reading& c : cooldowns )
  {
    if ( c.name == name )
      return &c;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Stage 1: read_state -- needs the engine, not exercised by the standalone
// test executable (plan 210-07).
// ---------------------------------------------------------------------------

rl_state_t read_state( const player_t* p, bool boundary_is_foreground )
{
  rl_state_t s;
  sim_t* sim = p->sim;

  s.t = sim->current_time().total_seconds();
  s.boundary_is_foreground = boundary_is_foreground;

  // Same formula as decision_dump.cpp:355's gcd_remains -- shared helper,
  // not a re-derived expression, so the two can never drift.
  s.gcd_remains = decision_dump::clamp_nonneg( ( p->gcd_ready - sim->current_time() ).total_seconds() );
  s.has_gcd_remains = true;

  // Same source decision_dump.cpp's swing-timer emitter reads.
  if ( p->main_hand_attack && p->main_hand_attack->execute_event )
  {
    s.swing_mh_remains = p->main_hand_attack->execute_event->remains().total_seconds();
    s.has_swing_mh_remains = true;
  }

  // Off-hand swing timer (quick task 260826-38t, D-2R slot 8). Mirrors the
  // main-hand read above exactly; guarded the same way decision_dump.cpp's
  // (new) off-hand emitter is guarded. The off-hand genuinely swings as of
  // this fork HEAD (commit "fix(solver_control): start the OFF-HAND
  // swinging too, not just the main hand") -- previously there was nothing
  // here to read.
  if ( p->off_hand_attack && p->off_hand_attack->execute_event )
  {
    s.swing_oh_remains = p->off_hand_attack->execute_event->remains().total_seconds();
    s.has_swing_oh_remains = true;
  }

  // Same active_enemies derivation decision_dump.cpp's emitter uses:
  // a single-target fight with no adds/pull raid events collapses to the
  // constant 1, otherwise the live sim_t::active_enemies counter.
  {
    int active_enemies = 1;
    if ( !( sim->target_list.size() == 1U && !sim->has_raid_event( "adds" ) && !sim->has_raid_event( "pull" ) ) )
      active_enemies = sim->active_enemies;
    s.active_enemies = static_cast<double>( active_enemies );
    s.has_active_enemies = true;
  }

  // Buffs -- same walk decision_dump's emitter uses, skipping
  // buff->check() <= 0 exactly as it does. `permanent` cannot currently be
  // produced by this live walk for this spec (R4 SS1.4) -- always false
  // here. Documentation fix (210-CR-FIX, per 210-VERIFICATION.md's own
  // finding at rl_policy_obs.cpp:97-100): this comment used to point at
  // plan 210-07's hand-written 20-case corpus as "what exercises the
  // `permanent` leaf" -- 210-07 was SUPERSEDED outright under ruling N-8
  // (210-05R-PLAN.md frontmatter) and will never be built, so that named a
  // non-existent future exerciser. The `permanent` leg is DELIBERATELY
  // unexercised for this MVP: `absent`/`present` are exercised by every
  // live decision boundary (200-616 per smoke seed), but nothing in this
  // phase or planned after it currently drives a buff row with
  // `permanent == true` through this walk. See 210-VERIFICATION.md's
  // `behavior_unverified_items` for the full paper trail; this is a
  // recorded gap, not an oversight.
  //
  // WR-02 fix, cheap half (210-CR-FIX): reserve() against the actor's full
  // buff_list size (an upper bound -- not every entry passes the
  // buff->check() <= 0 filter below) so push_back() below never reallocates
  // on this per-boundary hot path.
  s.buffs.reserve( p->buff_list.size() );
  for ( buff_t* buff : p->buff_list )
  {
    if ( buff->check() <= 0 )
      continue;
    buff_reading b;
    b.name = buff->name();
    b.present = true;
    b.stacks = static_cast<double>( buff->check() );
    b.has_stacks = true;
    // remains (quick task 260826-38t, D-2R slot 6) -- same source and same
    // permanent-sentinel convention decision_dump.cpp's write_buff_remains
    // uses: timespan_t::min() means "no scheduled expiry", encoded here as
    // has_remains=false + permanent=true, never as a huge negative number.
    {
      const timespan_t remains = buff->remains();
      if ( remains == timespan_t::min() )
      {
        b.has_remains = false;
        b.permanent = true;
      }
      else
      {
        b.remains = remains.total_seconds();
        b.has_remains = true;
        b.permanent = false;
      }
    }
    s.buffs.push_back( std::move( b ) );
  }

  // Cooldowns -- same walk decision_dump's emitter uses (every named
  // cooldown with a nonzero base recharge; skips the zero-duration
  // bookkeeping cooldowns SimC creates internally).
  //
  // WR-04 fix (210-CR-FIX): decision_dump::write_state_fields
  // (decision_dump.cpp:429/451) gates `charges_fractional` on
  // `emit_maelstrom_ext = p->resources.is_active( RESOURCE_MAELSTROM )` --
  // the SC-5 byte-neutrality contract. This walk used to write it
  // unconditionally, so obs slot 1 (`cooldown.strike.charges_fractional`,
  // missing=2.0, div=2) could read the live value for a non-maelstrom
  // actor where the FIFO transport's wire request would have omitted the
  // key entirely (-> `absent` -> `1.0`), a second cross-transport
  // divergence source under the same green fingerprints. Mirror the same
  // predicate here so `read_state` is honest about "the same walk
  // decision_dump's emitter uses" for every field it claims to share, not
  // just the ones CR-01's actor filter happens to make reachable today.
  // WR-02 fix, cheap half: same reserve() treatment as buffs above.
  const bool emit_maelstrom_ext = p->resources.is_active( RESOURCE_MAELSTROM );
  s.cooldowns.reserve( p->cooldown_list.size() );
  for ( cooldown_t* cd : p->cooldown_list )
  {
    if ( cd->duration <= timespan_t::zero() && cd->base_duration <= timespan_t::zero() )
      continue;
    cooldown_reading c;
    c.name = cd->name_str;
    c.present = true;
    c.remains = cd->remains().total_seconds();
    c.has_remains = true;
    c.recharge_time = cd->recharge_event ? cd->recharge_event->remains().total_seconds() : 0.0;
    c.has_recharge_time = true;
    c.charges = static_cast<double>( cd->current_charge );
    c.has_charges = true;
    if ( emit_maelstrom_ext )
    {
      c.charges_fractional = cd->charges_fractional();
      c.has_charges_fractional = true;
    }
    c.max_charges = cd->charges;
    s.cooldowns.push_back( std::move( c ) );
  }

  // Deliberately NOT read (each is out of scope for this schema or belongs
  // to a later phase, not an oversight): solver_damage_so_far (the reward,
  // Phase 212's log), holy_power, target_debuffs, target_time_to_die, dots,
  // gcd_length, auto_attack_interval, resolved_action.

  return s;
}

// ---------------------------------------------------------------------------
// Stage 2 helpers -- PURE arithmetic, mirroring obs.py's
// _apply_scale / _encode_bucket_ordinal exactly. Retargeted (phase 220,
// plan 220-04) from the retired per-field `rl_obs_field` to the per-leaf
// `rl_leaf_desc` -- the scale/bucket fields these two functions read are
// identical between the two structs, so this is a type swap only.
// ---------------------------------------------------------------------------

namespace
{
double apply_scale( double raw, rl_kind kind, const rl_leaf_desc& f )
{
  switch ( kind )
  {
    case rl_kind::k_int:
    case rl_kind::k_float:
    {
      const double div = f.has_div ? f.div : 1.0;
      return raw / div;
    }
    case rl_kind::k_seconds:
    {
      if ( f.has_clip_div )
      {
        const double clipped = std::max( std::min( raw, f.clip_div ), 0.0 );
        return clipped / f.clip_div;
      }
      const double div = f.has_div ? f.div : 1.0;
      return raw / div;
    }
    case rl_kind::k_bucket:
      // Bucket fields are dispatched by the caller before this function is
      // ever reached (mirrors obs.py's own `if kind == "bucket"` branch,
      // which never calls _apply_scale) -- unreachable in practice.
      return raw;
  }
  return raw;
}

double encode_bucket_ordinal( double raw, const double* buckets, std::size_t n )
{
  // Floor-matched to the largest bucket boundary <= raw (index 0 if raw is
  // below every boundary): scan ascending, keep the LAST boundary
  // satisfied with >=, no break -- mirrors obs.py:207-215 exactly.
  std::size_t idx = 0;
  for ( std::size_t i = 0; i < n; ++i )
  {
    if ( raw >= buckets[ i ] )
      idx = i;
  }
  const double denom = static_cast<double>( n > 1 ? n - 1 : 1 );
  return static_cast<double>( idx ) / denom;
}

enum class lookup_status { absent, present, permanent };
} // anonymous namespace

// ---------------------------------------------------------------------------
// Slot binding (phase 220, plan 220-04, OBS-02/OBS-07): ONE census walk
// resolves every family member to an engine handle ONCE per actor AND
// composes that slot's name from the SAME family/member/leaf tables --
// so the vector build_obs fills and the name list a caller can request via
// `rl_obs_names_out=` share one source of truth by construction (this is
// what makes T-220-04-01's fingerprint check non-vacuous: a names file
// that only echoed RL_OBS_NAMES back would compare the generator to
// itself; this walk composes its own names independently and reports a
// divergence if it ever disagrees with the baked table).
//
// Resolved for real in this task: `player_buffs` (buff) and the `scalars`
// pseudo-family's `fight_remains`/`active_enemies`/`t`/`maelstrom` leaves
// (`raid_event_next_in` stays unresolved -- plan 220-05 binds it). Every
// other family resolves `unresolved` here; plans 220-05/220-06 turn those
// into real bindings by adding cases below, with NO change to this
// structure (the slot_binding/slot_table shape, the bind-once contract,
// the name-composition walk).
// ---------------------------------------------------------------------------

// These three enums (and slot_binding/slot_table below) are deliberately
// AT NAMESPACE SCOPE, not inside an anonymous namespace -- slot_table is
// forward-declared in rl_policy.hpp with external linkage (rl_policy::
// slot_table), so its member type slot_binding, and slot_binding's own
// member types, must not carry internal linkage themselves.
enum class slot_binding_kind
{
  buff,
  cooldown,
  expression,
  action_expression,
  enemy_slot,
  direct,
  unresolved
};

enum class buff_leaf_kind { stacks, remains };

// The `scalars` pseudo-family's `direct`-kind leaves this task resolves,
// PLUS (220-05 Task 2) the `stats`/`swing_cast`/`position` families' own
// `direct`-kind leaves and the `sim_auras` family's single `skyfury` member
// (declared `rl_family_kind::expression` in the generated table alongside
// deck/pets/items/raid_events, but bound `direct` here -- `sim->auras.*` is
// a raid-wide buff_t* with no `sim.auras.<name>` APL expression form at all,
// verified empirically; a direct engine read is both correct and cheaper
// than round-tripping through a nonexistent expression string).
// `fight_remains` needs no entry here -- it is `derived` (rl_leaf_desc::
// derived), and build_obs' derived-first dispatch short-circuits before
// ever consulting a binding's `direct` id, exactly as the retired
// per-field walk did for the same field.
enum class direct_id
{
  active_enemies, t, maelstrom,
  // stats
  stats_attack_haste, stats_attack_crit_chance, stats_mastery_value,
  stats_damage_versatility, stats_attack_power,
  // swing_cast
  swing_cast_auto_attack_interval, swing_cast_casting_remains, swing_cast_gcd_length,
  swing_cast_gcd_remains, swing_cast_swing_mh_remains, swing_cast_swing_oh_remains,
  // position
  position_current_distance, position_distance_to_move, position_movement_speed,
  position_x, position_y,
  // sim_auras
  sim_aura_skyfury,
  // scalars (Task 3)
  raid_event_next_in
};

// rl_family_kind::cooldown's own `direct`-style dispatch (220-05 Task 2):
// which cooldown_t accessor a leaf means, decided once at bind time -- never
// a strcmp inside build_obs. `full_recharge_time` is NOT here: it is the
// only leaf in this schema resolved through the ACTION rather than the raw
// cooldown_t* (see resolve_cooldown_member's own comment), so it binds
// `slot_binding_kind::expression` instead.
enum class cooldown_leaf_kind { remains, charges, charges_fractional, recharge_time, max_charges };

struct slot_binding
{
  slot_binding_kind kind = slot_binding_kind::unresolved;
  const rl_leaf_desc* leaf = nullptr;   // this slot's own descriptor -- resolved once, indexed per decision
  buff_t* buff = nullptr;               // kind == buff
  buff_leaf_kind buff_leaf = buff_leaf_kind::stacks;   // kind == buff
  direct_id direct = direct_id::t;      // kind == direct
  expr_t* expr = nullptr;               // kind == expression -- non-owning; owned by slot_table::owned_expressions
  cooldown_t* cooldown = nullptr;                        // kind == cooldown
  cooldown_leaf_kind cooldown_leaf = cooldown_leaf_kind::remains;  // kind == cooldown
  // enemy-slot descriptor handles are added by plan 220-06 alongside the
  // `enemy_slot`/`action_expression` resolution cases below -- no
  // structural change needed here.
};

struct slot_table
{
  std::vector<slot_binding> bindings;        // size RL_OBS_DIM, slot order
  std::vector<std::string>  composed_names;  // size RL_OBS_DIM -- the WALK's own names
  int first_name_divergence = -1;            // -1 == the walk agreed with RL_OBS_NAMES at every slot
  // 220-05 OBS-04: expr_t objects returned by player_t::create_expression are
  // owned here (kept alive for the actor's whole run, same lifetime as the
  // slot_table itself in g_slot_table_cache) -- slot_binding::expr is a
  // non-owning raw pointer into this vector, never re-created per decision.
  std::vector<std::unique_ptr<expr_t>> owned_expressions;
};

namespace
{
std::unordered_map<const player_t*, slot_table> g_slot_table_cache;
bool g_names_out_written = false;   // guards the ONE names-file write per process

std::string compose_slot_name( const rl_obs_family& fam, const rl_obs_member& mem, const rl_leaf_desc& leaf )
{
  // header_contract (220-04-PLAN.md): `bare_name` families (only `scalars`)
  // compose the bare leaf name; every other family composes
  // "<family.name>.<member>.<leaf>".
  if ( fam.bare_name )
    return leaf.leaf;
  return fmt::format( "{}.{}.{}", fam.name, mem.member, leaf.leaf );
}

// The census artifact merges several consumable buffs into ONE generic
// member name per role (`flask`, `food`, `voidtouched` == augmentation,
// `potion`) -- INCLUDED.tsv's own "merged, one row" convention (220-02's
// importer, `_MULTI_INSTANCE_ROWS`/`_RENAME_ROWS`). The underlying buff
// object's REAL name is the specific item (e.g. a flask token, not the
// literal string "flask"), so a plain `p->buff_list` name-string scan can
// never match these four -- `player_t::consumables` exists for exactly
// this reason (flask/food/augmentation are direct `buff_t*` pointers,
// populated at init regardless of which concrete item the profile chose).
// `potion` has no equivalent direct pointer; its action ("potion",
// `dbc_consumable_base_t`) exposes the SAME generic seam through its own
// public `consumable_buff` member.
buff_t* resolve_generic_consumable_buff( player_t* p, const std::string& member )
{
  if ( member == "flask" )
    return p->consumables.flask;
  if ( member == "food" )
    return p->consumables.food;
  if ( member == "voidtouched" )
    return p->consumables.augmentation;
  if ( member == "potion" )
  {
    if ( action_t* a = p->find_action( "potion" ) )
    {
      if ( auto* consumable = dynamic_cast<dbc_consumable_base_t*>( a ) )
        return consumable->consumable_buff;
    }
    return nullptr;
  }
  return nullptr;   // not a generic-consumable member -- caller falls through to a name scan
}

// rl_family_kind::buff resolution: `stacks`/`remains` are the only two
// leaves this schema's census artifact declares for this family -- an
// UNRECOGNISED leaf name binds `unresolved` (a generator/artifact
// mismatch, not a per-fight data fact). A recognised leaf ALWAYS binds
// `kind::buff`, even when `member_buff` is null -- a member whose handle
// this run simply never created (an untalented proc buff, a raid-event-
// gated buff under a fight style that never injects that event) is a
// RUNTIME absence build_obs already encodes correctly (buff==nullptr ->
// absent -> `missing`), not an unimplemented resolution strategy. Binding
// it `unresolved` instead would conflate "this fight has no such buff"
// with "plans 220-05/220-06 still owe this family a binding" -- exactly
// the distinction the acceptance criteria (player_buffs fully resolved,
// `unresolved[]` names only families this task defers) requires.
slot_binding resolve_buff_leaf( buff_t* member_buff, const rl_leaf_desc& leaf )
{
  slot_binding b;
  b.leaf = &leaf;
  b.buff = member_buff;
  if ( std::strcmp( leaf.leaf, "stacks" ) == 0 )
  {
    b.kind = slot_binding_kind::buff;
    b.buff_leaf = buff_leaf_kind::stacks;
  }
  else if ( std::strcmp( leaf.leaf, "remains" ) == 0 )
  {
    b.kind = slot_binding_kind::buff;
    b.buff_leaf = buff_leaf_kind::remains;
  }
  else
  {
    b.kind = slot_binding_kind::unresolved;
  }
  return b;
}

// rl_family_kind::scalar resolution (the `scalars` pseudo-family). Every
// leaf this task resolves reads a value `build_obs` already has to hand
// with NO per-decision engine lookup of its own: `active_enemies`/`t` come
// straight from `rl_state_t` (already populated by `read_state`, untouched
// by this plan); `maelstrom` is read directly off `p->resources` at build
// time -- there is no POD field for it (deliberately: "do NOT touch
// read_state's POD population"), which is exactly what the new
// `build_obs(player_t*, ...)` signature exists to allow. Where the RETIRED
// per-field walk gated an engine-direct read like this on
// `p->resources.is_active(RESOURCE_MAELSTROM)` (the SC-5 cross-transport
// byte-neutrality contract, `cooldowns.strike.charges_fractional`'s old
// gate), that gate is unnecessary for any read this new walk performs: the
// FIFO transport's own byte-neutrality concern was about the WIRE PROTOCOL
// omitting a key the request never asked for, not about whether the engine
// itself can answer the read -- and CR-01's actor filter means only the
// shaman ever reaches this path, so there is no other actor's absent
// resource to accidentally read as zero. Applies identically to the
// cooldowns family's own `charges_fractional` when plan 220-05 binds it.
slot_binding resolve_scalar_leaf( const rl_leaf_desc& leaf )
{
  slot_binding b;
  b.leaf = &leaf;
  if ( leaf.derived )
  {
    // Never actually read -- build_obs' derived-first dispatch short-
    // circuits before consulting `direct` for a derived leaf. Kept a
    // defined value rather than left default-constructed so a future
    // reader never has to wonder whether "direct_id::t" here means
    // anything.
    b.kind = slot_binding_kind::direct;
    b.direct = direct_id::t;
    return b;
  }
  if ( std::strcmp( leaf.leaf, "active_enemies" ) == 0 )
  {
    b.kind = slot_binding_kind::direct;
    b.direct = direct_id::active_enemies;
    return b;
  }
  if ( std::strcmp( leaf.leaf, "t" ) == 0 )
  {
    b.kind = slot_binding_kind::direct;
    b.direct = direct_id::t;
    return b;
  }
  if ( std::strcmp( leaf.leaf, "maelstrom" ) == 0 )
  {
    b.kind = slot_binding_kind::direct;
    b.direct = direct_id::maelstrom;
    return b;
  }
  if ( std::strcmp( leaf.leaf, "raid_event_next_in" ) == 0 )
  {
    // 220-05 Task 3 (RIG-05): computed from the public sim->raid_events +
    // until_next() in build_obs, never through create_expression -- see
    // that switch arm's own comment for the missing-substitution rule.
    b.kind = slot_binding_kind::direct;
    b.direct = direct_id::raid_event_next_in;
    return b;
  }
  // anything else this task does not bind.
  b.kind = slot_binding_kind::unresolved;
  return b;
}

// Shared plumbing for every `expression`-kind binding (220-05 OBS-04/Task 2):
// a resolved (possibly null) expr_t is moved into `table.owned_expressions`
// (kept alive for the actor's whole run -- eval() is called once PER
// DECISION from build_obs, never re-created there) and the binding holds
// only a non-owning raw pointer into that vector. A null `expr` (whether
// because create_expression legitimately returned nullptr, or because the
// caller already caught a throw) binds `unresolved`.
slot_binding bind_expression_result( std::unique_ptr<expr_t> expr, const rl_leaf_desc& leaf,
                                      slot_table& table )
{
  slot_binding b;
  b.leaf = &leaf;
  if ( !expr )
  {
    b.kind = slot_binding_kind::unresolved;
    return b;
  }
  table.owned_expressions.push_back( std::move( expr ) );
  b.kind = slot_binding_kind::expression;
  b.expr = table.owned_expressions.back().get();
  return b;
}

// rl_family_kind::expression resolution (220-05 OBS-04, Pitfall 5): the
// class-agnostic seam -- every deck/pet/item/raid-event member is an
// expression NAME that arrives from the census artifact's `engine_token`,
// never a spell token core RL code names itself (ruling R-4).
// `player_t::create_expression` throws `sc_invalid_apl_argument` for an
// unrecognised name (player.cpp:12263/12281/12596) -- wrapped in try/catch
// HERE, at bind time, so a failure becomes an `unresolved` binding (named in
// the names file) rather than aborting the run mid-fight with a confusing
// APL-argument message.
slot_binding resolve_expression_leaf( player_t* p, const std::string& engine_token,
                                       const rl_leaf_desc& leaf, slot_table& table )
{
  std::unique_ptr<expr_t> expr;
  try
  {
    expr = p->create_expression( engine_token );
  }
  catch ( const std::exception& )
  {
    expr = nullptr;
  }
  return bind_expression_result( std::move( expr ), leaf, table );
}

// Same as resolve_expression_leaf, but resolved through an ACTION's own
// create_expression rather than the player's -- 220-05 Task 2's
// `full_recharge_time` leaf (the cooldown family's `strike` member) needs
// this: `cooldown.<name>.full_recharge_time` is not a recognised generic
// cooldown expression (only `cooldown.<name>.remains` and friends are), but
// `action.<name>.full_recharge_time` reaches the action's own real cooldown
// object directly. A null `action` (find_action failed to resolve the
// engine_token->action-name mapping) binds `unresolved` without ever
// calling create_expression on a null pointer.
slot_binding resolve_action_expression_leaf( action_t* action, const std::string& expr_name,
                                              const rl_leaf_desc& leaf, slot_table& table )
{
  std::unique_ptr<expr_t> expr;
  if ( action != nullptr )
  {
    try
    {
      expr = action->create_expression( expr_name );
    }
    catch ( const std::exception& )
    {
      expr = nullptr;
    }
  }
  return bind_expression_result( std::move( expr ), leaf, table );
}

// rl_family_kind::cooldown resolution (220-05 Task 2): the named cooldown_t*
// is resolved ONCE per member (bind_slots' own member-level cache, mirroring
// the buff family's member_buff cache) and every leaf but one dispatches to
// a `cooldown`-kind binding read directly off that handle in build_obs --
// never through create_expression, since a raw pointer read is both cheaper
// (OBS-07's hot-path concern) and simpler than round-tripping an expression
// string for the SAME accessor read_state already used pre-220-04
// (rl_policy_obs.cpp:189-200's retired walk).
//
// `full_recharge_time` is the ONE exception, per this task's own action
// text: it is resolved through the ACTION, not the cooldown_t*, because
// `cooldown.<name>.full_recharge_time` is not a real generic-cooldown
// expression (verified empirically) and a cooldown NAME does not always
// equal its action's name -- in this schema `full_recharge_time` appears
// ONLY on the `strike` member (the shared Stormstrike/Windstrike cooldown
// row), whose real action is named "stormstrike", not "strike"; that one
// mapping is hardcoded here rather than assumed identical to every other
// cooldown member (which never need it -- no OTHER member in this census
// declares a `full_recharge_time` leaf).
//
// Deliberately does NOT gate `charges_fractional` on
// `p->resources.is_active(RESOURCE_MAELSTROM)` (the SC-5 byte-neutrality
// contract 220-04 preserved for the `maelstrom` scalar) -- that gate exists
// for the retired FIFO transport's wire-omission concern, not for whether
// the engine can answer the read, and CR-01's actor filter means only the
// shaman ever reaches this path (resolve_scalar_leaf's own 220-04 comment
// makes the identical argument for `maelstrom`).
slot_binding resolve_cooldown_leaf( player_t* p, cooldown_t* cd, const std::string& cooldown_token,
                                     const rl_leaf_desc& leaf, slot_table& table )
{
  if ( std::strcmp( leaf.leaf, "full_recharge_time" ) == 0 )
  {
    const std::string action_token = ( cooldown_token == "strike" ) ? "stormstrike" : cooldown_token;
    return resolve_action_expression_leaf( p->find_action( action_token ), "full_recharge_time", leaf, table );
  }

  slot_binding b;
  b.leaf = &leaf;
  if ( cd == nullptr )
  {
    b.kind = slot_binding_kind::unresolved;
    return b;
  }
  b.cooldown = cd;
  b.kind = slot_binding_kind::cooldown;
  if ( std::strcmp( leaf.leaf, "remains" ) == 0 )
    b.cooldown_leaf = cooldown_leaf_kind::remains;
  else if ( std::strcmp( leaf.leaf, "charges" ) == 0 )
    b.cooldown_leaf = cooldown_leaf_kind::charges;
  else if ( std::strcmp( leaf.leaf, "charges_fractional" ) == 0 )
    b.cooldown_leaf = cooldown_leaf_kind::charges_fractional;
  else if ( std::strcmp( leaf.leaf, "recharge_time" ) == 0 )
    b.cooldown_leaf = cooldown_leaf_kind::recharge_time;
  else if ( std::strcmp( leaf.leaf, "max_charges" ) == 0 )
    b.cooldown_leaf = cooldown_leaf_kind::max_charges;
  else
    b.kind = slot_binding_kind::unresolved;   // artifact/generator mismatch, not a runtime fact
  return b;
}

// rl_family_kind::direct resolution for `stats`/`swing_cast`/`position`
// (220-05 Task 2) -- a small closed mapping from (family id, member name) to
// a `direct_id`, decided once at bind time. Each member in these three
// families carries exactly one leaf (always named "value" in this schema),
// so no per-leaf dispatch is needed beyond the member-name match itself.
slot_binding resolve_stats_swing_cast_position_leaf( rl_family fam_id, const std::string& member,
                                                      const rl_leaf_desc& leaf )
{
  slot_binding b;
  b.leaf = &leaf;
  b.kind = slot_binding_kind::direct;

  if ( fam_id == rl_family::stats )
  {
    if ( member == "attack_power" )      b.direct = direct_id::stats_attack_power;
    else if ( member == "crit" )         b.direct = direct_id::stats_attack_crit_chance;
    else if ( member == "damage_versatility" ) b.direct = direct_id::stats_damage_versatility;
    else if ( member == "haste" )        b.direct = direct_id::stats_attack_haste;
    else if ( member == "mastery_value" ) b.direct = direct_id::stats_mastery_value;
    else b.kind = slot_binding_kind::unresolved;
  }
  else if ( fam_id == rl_family::swing_cast )
  {
    if ( member == "auto_attack_interval" ) b.direct = direct_id::swing_cast_auto_attack_interval;
    else if ( member == "casting_remains" ) b.direct = direct_id::swing_cast_casting_remains;
    else if ( member == "gcd_length" )      b.direct = direct_id::swing_cast_gcd_length;
    else if ( member == "gcd_remains" )     b.direct = direct_id::swing_cast_gcd_remains;
    else if ( member == "swing_mh_remains" ) b.direct = direct_id::swing_cast_swing_mh_remains;
    else if ( member == "swing_oh_remains" ) b.direct = direct_id::swing_cast_swing_oh_remains;
    else b.kind = slot_binding_kind::unresolved;
  }
  else if ( fam_id == rl_family::position )
  {
    if ( member == "current_distance" )    b.direct = direct_id::position_current_distance;
    else if ( member == "distance_to_move" ) b.direct = direct_id::position_distance_to_move;
    else if ( member == "movement_speed" ) b.direct = direct_id::position_movement_speed;
    else if ( member == "x" )              b.direct = direct_id::position_x;
    else if ( member == "y" )              b.direct = direct_id::position_y;
    else b.kind = slot_binding_kind::unresolved;
  }
  else
  {
    b.kind = slot_binding_kind::unresolved;
  }
  return b;
}

// rl_family_kind::expression resolution for the `pets` family (220-05 Task
// 2). Two sub-strategies, both class-agnostic in NAME (the wiring lives
// here, in a shared file, never in sc_shaman.cpp -- ruling R-4 is about core
// RL code naming no spell/pet token; this switch is the census's own
// resolution logic, exactly like raid_events' "adds"/"movement" -> "raid_event.*"
// composition below):
//   - "n_active_pets"/"active" leaves compose "pet.<token>.active", which
//     resolves through player_t::create_expression's pet_spawner_t branch to
//     a live COUNT (pet_spawner_t::create_expression, "active" ->
//     n_active_pets()) for a multi-pet spawner, or a 0/1 liveness bit for a
//     single find_pet() match -- either way a correct numeric encoding.
//   - "remains" composes "pet.<token>.remains" (spawner: soonest-to-expire
//     active pet; single pet: its own expiration).
//   - "pulse_event_remains" (searing_totem/surging_totem only) uses the two
//     new shaman_t-level expressions this task's fork edit added, NOT
//     "pet.<token>.pulse_event_remains" -- that string throws (verified
//     empirically: no existing APL expression reaches a totem's raw
//     pulse_event timer through the spawner path).
// "the wolves" (all_wolves/fire_wolves/lightning_wolves) route through the
// shaman's own soft-failing `feral_spirit.active`/`feral_spirit.remains`
// forms instead of `pet.<token>.*` -- there is no per-color wolf split in
// this fork (one Feral Spirit cast spawns an undifferentiated wolf pack), so
// all three members read the SAME feral_spirit.* value; 220-07's liveness
// census prunes the resulting cross-slot duplication by its own
// "redundant-with:<slot>" rule (CONTEXT.md ruling), not by special-casing it
// away here.
slot_binding resolve_pets_leaf( player_t* p, const std::string& member, const std::string& engine_token,
                                 const rl_leaf_desc& leaf, slot_table& table )
{
  static const std::set<std::string> wolves{ "all_wolves", "fire_wolves", "lightning_wolves" };

  if ( wolves.count( member ) )
  {
    if ( std::strcmp( leaf.leaf, "n_active_pets" ) == 0 )
      return resolve_expression_leaf( p, "feral_spirit.active", leaf, table );
    if ( std::strcmp( leaf.leaf, "remains" ) == 0 )
      return resolve_expression_leaf( p, "feral_spirit.remains", leaf, table );
    return bind_expression_result( nullptr, leaf, table );   // artifact mismatch -> unresolved
  }

  if ( std::strcmp( leaf.leaf, "pulse_event_remains" ) == 0 )
  {
    if ( member == "surging_totem" )
      return resolve_expression_leaf( p, "surging_totem_pulse_remains", leaf, table );
    if ( member == "searing_totem" )
      return resolve_expression_leaf( p, "searing_totem_pulse_remains", leaf, table );
    return bind_expression_result( nullptr, leaf, table );
  }

  std::string suffix;
  if ( std::strcmp( leaf.leaf, "n_active_pets" ) == 0 || std::strcmp( leaf.leaf, "active" ) == 0 )
    suffix = "active";
  else if ( std::strcmp( leaf.leaf, "remains" ) == 0 )
    suffix = "remains";
  else
    return bind_expression_result( nullptr, leaf, table );   // artifact mismatch -> unresolved

  return resolve_expression_leaf( p, "pet." + engine_token + "." + suffix, leaf, table );
}

// rl_family_kind::expression resolution for `items` (220-05 Task 2). Two
// members, two different real expression forms -- neither is the bare
// census token, exactly like raid_events/pets above.
//   - "midnight_season_2_4pc" -> "set_bonus.midnight_season_2_4pc" (a
//     player-scoped set-bonus check, real regardless of talent state).
//   - "trinket_has_use_buff" -> the OR of BOTH trinket slots'
//     `trinket.<N>.has_use_buff` (the census declares ONE aggregate member
//     for what the game exposes as two independently-equipped slots; a
//     composite `make_fn_expr` over up to two resolved sub-expressions is
//     the simplest correct aggregation -- max() over {0,1} boolean-ish
//     reads is OR). A slot with no trinket present is expected to throw
//     nothing here (verified empirically both trinket slots resolve
//     cleanly for this profile); either sub-expression failing to resolve
//     still lets the composite bind through the other.
slot_binding resolve_items_leaf( player_t* p, const std::string& member, const rl_leaf_desc& leaf,
                                  slot_table& table )
{
  if ( member == "midnight_season_2_4pc" )
    return resolve_expression_leaf( p, "set_bonus.midnight_season_2_4pc", leaf, table );

  if ( member == "trinket_has_use_buff" )
  {
    expr_t* e1 = nullptr;
    expr_t* e2 = nullptr;
    try
    {
      if ( auto e = p->create_expression( "trinket.1.has_use_buff" ) )
      {
        table.owned_expressions.push_back( std::move( e ) );
        e1 = table.owned_expressions.back().get();
      }
    }
    catch ( const std::exception& ) { }
    try
    {
      if ( auto e = p->create_expression( "trinket.2.has_use_buff" ) )
      {
        table.owned_expressions.push_back( std::move( e ) );
        e2 = table.owned_expressions.back().get();
      }
    }
    catch ( const std::exception& ) { }

    if ( e1 == nullptr && e2 == nullptr )
      return bind_expression_result( nullptr, leaf, table );

    auto composite = make_fn_expr( "trinket_has_use_buff", [ e1, e2 ]() {
      double v = 0.0;
      if ( e1 != nullptr ) v = std::max( v, e1->eval() );
      if ( e2 != nullptr ) v = std::max( v, e2->eval() );
      return v;
    } );
    return bind_expression_result( std::move( composite ), leaf, table );
  }

  return bind_expression_result( nullptr, leaf, table );   // artifact mismatch -> unresolved
}

// rl_family_kind::expression resolution for `sim_auras` (220-05 Task 2),
// bound `direct` (not `expression`) -- see direct_id's own comment above for
// why: `sim->auras.skyfury` is a raid-wide buff_t* with no matching APL
// expression string at all (verified empirically), so the correct and
// cheapest read is direct engine access, exactly like the `maelstrom`
// scalar.
slot_binding resolve_sim_auras_leaf( const std::string& member, const rl_leaf_desc& leaf )
{
  slot_binding b;
  b.leaf = &leaf;
  if ( member == "skyfury" )
  {
    b.kind = slot_binding_kind::direct;
    b.direct = direct_id::sim_aura_skyfury;
  }
  else
  {
    b.kind = slot_binding_kind::unresolved;
  }
  return b;
}

// Writes the `rl_obs_names_out=` file ONCE per process (T-220-04-02's
// rundir-scoped discipline, mirroring rl_translog='s open_and_write_header:
// eager, no parent directories created, refuses loudly on a bad path
// rather than silently skipping). Called from bind_slots() -- the ONLY
// place a fresh slot_table (and therefore a fresh set of walk-composed
// names) is ever produced.
void write_names_out_if_requested( const player_t* p, const slot_table& table )
{
  if ( g_names_out_written )
    return;
  const std::string& path = p->sim->rl_obs_names_out_str;
  if ( path.empty() )
    return;
  g_names_out_written = true;

  io::ofstream stream;
  stream.open( path, std::ios::out | std::ios::trunc );
  if ( !stream.is_open() )
  {
    throw sc_runtime_error(
        fmt::format( "rl_obs_names_out=: unable to open '{}' for writing.", path ) );
  }

  stream << "{\"schemaSha\":\"" << decision_dump::json_escape( RL_OBS_SCHEMA_SHA ) << "\",";
  stream << "\"width\":" << RL_OBS_DIM << ",";
  stream << "\"names\":[";
  for ( std::size_t i = 0; i < RL_OBS_DIM; ++i )
  {
    if ( i > 0 )
      stream << ",";
    stream << "\"" << decision_dump::json_escape( table.composed_names[ i ] ) << "\"";
  }
  stream << "],\"unresolved\":[";
  bool first = true;
  for ( std::size_t i = 0; i < RL_OBS_DIM; ++i )
  {
    if ( table.bindings[ i ].kind != slot_binding_kind::unresolved )
      continue;
    if ( !first )
      stream << ",";
    first = false;
    stream << "\"" << decision_dump::json_escape( table.composed_names[ i ] ) << "\"";
  }
  stream << "],\"firstNameDivergence\":" << table.first_name_divergence << "}";
  stream.flush();
}
} // anonymous namespace

const slot_table& bind_slots( player_t* p )
{
  auto cached = g_slot_table_cache.find( p );
  if ( cached != g_slot_table_cache.end() )
    return cached->second;

  slot_table table;
  table.bindings.reserve( RL_OBS_DIM );
  table.composed_names.reserve( RL_OBS_DIM );

  std::size_t counter = 0;
  for ( std::size_t fi = 0; fi < RL_OBS_FAMILY_COUNT; ++fi )
  {
    const rl_obs_family& fam = RL_OBS_FAMILIES[ fi ];
    for ( std::size_t mi = 0; mi < fam.n_members; ++mi )
    {
      const rl_obs_member& mem = fam.members[ mi ];

      // Buff family: resolve the handle ONCE per member (shared across
      // that member's leaves) -- never re-scan p->buff_list per leaf.
      buff_t* member_buff = nullptr;
      bool member_buff_scanned = false;

      // Cooldown family (220-05 Task 2): same one-scan-per-member discipline
      // as the buff family above, over p->cooldown_list instead of
      // p->buff_list.
      cooldown_t* member_cooldown = nullptr;
      bool member_cooldown_scanned = false;

      for ( std::size_t li = 0; li < mem.n_leaves; ++li )
      {
        const rl_leaf_desc& leaf = mem.leaves[ li ];
        std::string composed = compose_slot_name( fam, mem, leaf );

        slot_binding binding;
        switch ( fam.kind )
        {
          case rl_family_kind::buff:
          {
            if ( !member_buff_scanned )
            {
              member_buff = resolve_generic_consumable_buff( p, mem.member );
              if ( member_buff == nullptr )
              {
                for ( buff_t* buff : p->buff_list )
                {
                  if ( buff->name_str == mem.member )
                  {
                    member_buff = buff;
                    break;
                  }
                }
              }
              member_buff_scanned = true;
            }
            binding = resolve_buff_leaf( member_buff, leaf );
            break;
          }
          case rl_family_kind::scalar:
            binding = resolve_scalar_leaf( leaf );
            break;
          case rl_family_kind::expression:
            // 220-05 Task 1 proved the seam on `deck`; Task 2 adds
            // `pets`/`items`/`raid_events` (Task 3) and (bound `direct`,
            // not `expression` -- see resolve_sim_auras_leaf's own comment)
            // `sim_auras`.
            if ( fam.id == rl_family::deck )
            {
              binding = resolve_expression_leaf( p, mem.engine_token, leaf, table );
            }
            else if ( fam.id == rl_family::pets )
            {
              binding = resolve_pets_leaf( p, mem.member, mem.engine_token, leaf, table );
            }
            else if ( fam.id == rl_family::items )
            {
              binding = resolve_items_leaf( p, mem.member, leaf, table );
            }
            else if ( fam.id == rl_family::sim_auras )
            {
              binding = resolve_sim_auras_leaf( mem.member, leaf );
            }
            else if ( fam.id == rl_family::raid_events )
            {
              // 220-05 Task 3 (RIG-05): "raid_event.<token>.<leaf>" --
              // member.engine_token is "adds"/"movement" (the census's own
              // engineToken override, chosen to avoid the genericity-token
              // collision "adds" has against source.split('.')[1] elsewhere
              // -- CONTEXT.md's own ruling). Constant-folded at creation time
              // under a single-shape fight (sim.cpp:3824-3840) -- correct,
              // per this task's own action text; the census must see these
              // move under HecticAddCleave/LightMovement instead.
              binding = resolve_expression_leaf(
                p, "raid_event." + std::string( mem.engine_token ) + "." + leaf.leaf, leaf, table );
            }
            else
            {
              binding.kind = slot_binding_kind::unresolved;
              binding.leaf = &leaf;
            }
            break;
          case rl_family_kind::cooldown:
          {
            if ( !member_cooldown_scanned )
            {
              for ( cooldown_t* cd : p->cooldown_list )
              {
                if ( cd->name_str == mem.member )
                {
                  member_cooldown = cd;
                  break;
                }
              }
              member_cooldown_scanned = true;
            }
            binding = resolve_cooldown_leaf( p, member_cooldown, mem.member, leaf, table );
            break;
          }
          case rl_family_kind::direct:
            // 220-05 Task 2: stats/swing_cast/position -- see
            // resolve_stats_swing_cast_position_leaf's own comment.
            binding = resolve_stats_swing_cast_position_leaf( fam.id, mem.member, leaf );
            break;
          case rl_family_kind::enemy_slot:
          case rl_family_kind::action_expression:
          default:
            // Deferred to plan 220-06 -- see this function's own header
            // comment.
            binding.kind = slot_binding_kind::unresolved;
            binding.leaf = &leaf;
            break;
        }

        if ( counter >= RL_OBS_DIM )
        {
          throw sc_runtime_error( fmt::format(
              "rl_policy::bind_slots: the family table walk produced more than RL_OBS_DIM ({}) "
              "slots at family='{}' member='{}' leaf='{}' -- this is a generator/engine table "
              "mismatch (a long table), not a runtime data problem",
              RL_OBS_DIM, fam.name, mem.member, leaf.leaf ) );
        }

        // Assert the composed name equals RL_OBS_NAMES[i]. On a mismatch,
        // do NOT abort -- record the FIRST divergent slot and still emit
        // the WALK's own composed name (never a copy of RL_OBS_NAMES) so
        // obs_fingerprint.py can see the divergence instead of the engine
        // hiding it (T-220-04-01).
        if ( table.first_name_divergence < 0 && composed != RL_OBS_NAMES[ counter ] )
          table.first_name_divergence = static_cast<int>( counter );

        table.bindings.push_back( binding );
        table.composed_names.push_back( std::move( composed ) );
        ++counter;
      }
    }
  }

  if ( counter != RL_OBS_DIM )
  {
    throw sc_runtime_error( fmt::format(
        "rl_policy::bind_slots: the family table walk produced {} slots, expected RL_OBS_DIM={} "
        "-- this is a generator bug (a short table), not a runtime data problem",
        counter, RL_OBS_DIM ) );
  }

  write_names_out_if_requested( p, table );

  auto [ inserted, ok ] = g_slot_table_cache.emplace( p, std::move( table ) );
  ( void )ok;   // emplace on a key just proven absent by the find() above always succeeds
  return inserted->second;
}

// ---------------------------------------------------------------------------
// build_obs -- REAL for the families this task resolves (player_buffs,
// scalars); every other family's slots read their bound `unresolved`
// status (-> `missing`) until plans 220-05/220-06 add real bindings, with
// NO change to this dispatch. Order mirrors obs.py:232-287 and the retired
// per-field walk exactly: derived fields force `present`; otherwise the
// binding decides a tri-state status (absent/present/permanent); `kind ==
// bucket` is tested BEFORE the `permanent` branch (a permanent bucket is a
// registry misconfiguration, not a value to saturate); non-bucket
// `permanent` saturates to RL_PERMANENT_SATURATION; everything else goes
// through apply_scale. NO string comparison anywhere in this function's
// body -- every slot's resolution (which accessor to call, which leaf a
// buff binding means) was already decided once, at bind time, above.
// ---------------------------------------------------------------------------

void build_obs( const player_t* p, const rl_state_t& s, const slot_table& t, float out_obs[ RL_OBS_DIM ] )
{
  for ( std::size_t slot = 0; slot < RL_OBS_DIM; ++slot )
  {
    const slot_binding& b = t.bindings[ slot ];
    const rl_leaf_desc& f = *b.leaf;

    double raw = 0.0;
    lookup_status status;

    if ( f.derived )
    {
      // Only fight_remains is derived in this schema: episode.maxTime - t,
      // floored at 0 (obs.py:236-243).
      raw = std::max( RL_EPISODE_MAX_TIME - s.t, 0.0 );
      status = lookup_status::present;
    }
    else
    {
      switch ( b.kind )
      {
        case slot_binding_kind::buff:
        {
          buff_t* buff = b.buff;
          if ( buff == nullptr || buff->check() <= 0 )
          {
            // Not up (or unresolved) -- absent for BOTH leaves of this
            // member, matching the retired walk's own `s.buffs` filter
            // (a not-up buff was skipped entirely, so find_buff() would
            // return nullptr for it there too).
            status = lookup_status::absent;
          }
          else if ( b.buff_leaf == buff_leaf_kind::stacks )
          {
            raw = static_cast<double>( buff->check() );
            status = lookup_status::present;
          }
          else   // remains
          {
            const timespan_t remains = buff->remains();
            if ( remains == timespan_t::min() )
            {
              status = lookup_status::permanent;
            }
            else
            {
              raw = remains.total_seconds();
              status = lookup_status::present;
            }
          }
          break;
        }
        case slot_binding_kind::direct:
        {
          switch ( b.direct )
          {
            case direct_id::active_enemies:
              if ( s.has_active_enemies )
              {
                raw = s.active_enemies;
                status = lookup_status::present;
              }
              else
              {
                status = lookup_status::absent;
              }
              break;
            case direct_id::t:
              raw = s.t;
              status = lookup_status::present;
              break;
            case direct_id::maelstrom:
              // Read directly off the engine, not off rl_state_t -- there
              // is no POD field for it (see resolve_scalar_leaf's own
              // comment on why that gate is unnecessary here).
              raw = p->resources.current[ RESOURCE_MAELSTROM ];
              status = lookup_status::present;
              break;

            // 220-05 Task 2: stats -- always well-defined, never absent.
            case direct_id::stats_attack_haste:
              raw = p->cache.attack_haste();
              status = lookup_status::present;
              break;
            case direct_id::stats_attack_crit_chance:
              raw = p->cache.attack_crit_chance();
              status = lookup_status::present;
              break;
            case direct_id::stats_mastery_value:
              raw = p->cache.mastery_value();
              status = lookup_status::present;
              break;
            case direct_id::stats_damage_versatility:
              raw = p->cache.damage_versatility();
              status = lookup_status::present;
              break;
            case direct_id::stats_attack_power:
              raw = p->cache.attack_power();
              status = lookup_status::present;
              break;

            // 220-05 Task 2: swing_cast. gcd_remains/swing_mh_remains/
            // swing_oh_remains read the SAME rl_state_t POD read_state
            // already populates (has_* flags -> absent, exactly like
            // active_enemies above) -- never re-derived here. gcd_length and
            // auto_attack_interval mirror decision_dump.cpp's own
            // player-scoped formulas (this task's own read_first); both are
            // unconditionally well-defined (0.0 is a real "no main-hand
            // weapon" reading for auto_attack_interval, not an absence).
            case direct_id::swing_cast_gcd_remains:
              if ( s.has_gcd_remains )
              {
                raw = s.gcd_remains;
                status = lookup_status::present;
              }
              else
              {
                status = lookup_status::absent;
              }
              break;
            case direct_id::swing_cast_swing_mh_remains:
              if ( s.has_swing_mh_remains )
              {
                raw = s.swing_mh_remains;
                status = lookup_status::present;
              }
              else
              {
                status = lookup_status::absent;
              }
              break;
            case direct_id::swing_cast_swing_oh_remains:
              if ( s.has_swing_oh_remains )
              {
                raw = s.swing_oh_remains;
                status = lookup_status::present;
              }
              else
              {
                status = lookup_status::absent;
              }
              break;
            case direct_id::swing_cast_gcd_length:
            {
              timespan_t player_gcd = p->base_gcd * p->cache.attack_haste();
              if ( player_gcd < p->min_gcd )
                player_gcd = p->min_gcd;
              raw = player_gcd.total_seconds();
              status = lookup_status::present;
              break;
            }
            case direct_id::swing_cast_auto_attack_interval:
              raw = p->main_hand_attack
                ? ( p->main_hand_weapon.swing_time * p->cache.auto_attack_speed() ).total_seconds()
                : 0.0;
              status = lookup_status::present;
              break;
            case direct_id::swing_cast_casting_remains:
              if ( p->executing && p->executing->execute_event )
              {
                raw = p->executing->execute_event->remains().total_seconds();
                status = lookup_status::present;
              }
              else if ( p->channeling && p->channeling->execute_event )
              {
                raw = p->channeling->execute_event->remains().total_seconds();
                status = lookup_status::present;
              }
              else
              {
                status = lookup_status::absent;   // not currently casting
              }
              break;

            // 220-05 Task 2: position. Always well-defined.
            case direct_id::position_current_distance:
              raw = p->current.distance;
              status = lookup_status::present;
              break;
            case direct_id::position_distance_to_move:
              raw = p->current.distance_to_move;
              status = lookup_status::present;
              break;
            case direct_id::position_movement_speed:
              raw = p->composite_movement_speed();
              status = lookup_status::present;
              break;
            case direct_id::position_x:
              raw = p->x_position;
              status = lookup_status::present;
              break;
            case direct_id::position_y:
              raw = p->y_position;
              status = lookup_status::present;
              break;

            // 220-05 Task 2: sim_auras.skyfury -- constructed unconditionally
            // (sim.cpp:2825), so ->check() is always a safe read.
            case direct_id::sim_aura_skyfury:
              raw = p->sim->auras.skyfury->check();
              status = lookup_status::present;
              break;

            // 220-05 Task 3 (RIG-05): the soonest-to-fire raid event across
            // the whole sim, computed from the PUBLIC sim->raid_events
            // (sim.hpp:218) + raid_event_t::until_next() (raid_event.hpp:78)
            // -- never through raid_event_t::get_next_raid_event, which is
            // file-local to raid_event.cpp's unnamed namespace and therefore
            // NOT LINKABLE from here; a later reader must not "simplify"
            // this loop into a call to that function. timespan_t::max()
            // (nothing pending, or an empty raid_events vector) substitutes
            // the SAME value the fight_remains scalar computes above
            // (max(RL_EPISODE_MAX_TIME - s.t, 0)), never the raw ~1e12
            // saturation sentinel (Pitfall 7).
            case direct_id::raid_event_next_in:
            {
              timespan_t min_until = timespan_t::max();
              for ( const auto& re : p->sim->raid_events )
              {
                if ( !re )
                  continue;
                const timespan_t u = re->until_next();
                if ( u < min_until )
                  min_until = u;
              }
              raw = ( min_until == timespan_t::max() )
                ? std::max( RL_EPISODE_MAX_TIME - s.t, 0.0 )
                : min_until.total_seconds();
              status = lookup_status::present;
              break;
            }

            default:
              status = lookup_status::absent;
              break;
          }
          break;
        }
        case slot_binding_kind::expression:
        {
          // 220-05 OBS-04: eval() is called ONCE per decision here -- the
          // expr_t itself was resolved once at bind time (resolve_expression_leaf).
          // A null b.expr (should not happen for a binding of this kind, but
          // guarded defensively) reads absent rather than dereferencing null.
          if ( b.expr == nullptr )
          {
            status = lookup_status::absent;
          }
          else
          {
            raw = b.expr->eval();
            status = lookup_status::present;
          }
          break;
        }
        case slot_binding_kind::cooldown:
        {
          // 220-05 Task 2: a direct cooldown_t* read, resolved once at bind
          // time (resolve_cooldown_leaf); no create_expression round-trip.
          if ( b.cooldown == nullptr )
          {
            status = lookup_status::absent;
            break;
          }
          switch ( b.cooldown_leaf )
          {
            case cooldown_leaf_kind::remains:
              raw = b.cooldown->remains().total_seconds();
              break;
            case cooldown_leaf_kind::charges:
              raw = static_cast<double>( b.cooldown->current_charge );
              break;
            case cooldown_leaf_kind::charges_fractional:
              raw = b.cooldown->charges_fractional();
              break;
            case cooldown_leaf_kind::recharge_time:
              raw = b.cooldown->recharge_event
                ? b.cooldown->recharge_event->remains().total_seconds() : 0.0;
              break;
            case cooldown_leaf_kind::max_charges:
              raw = static_cast<double>( b.cooldown->charges );
              break;
          }
          status = lookup_status::present;
          break;
        }
        case slot_binding_kind::action_expression:
        case slot_binding_kind::enemy_slot:
        case slot_binding_kind::unresolved:
        default:
          // Deferred to plan 220-06 (enemy_slot/action_expression) -- an
          // unresolved binding yields `absent` (header_contract), never a
          // special value.
          status = lookup_status::absent;
          break;
      }
    }

    if ( status == lookup_status::absent )
      raw = f.missing;
    // status == present: `raw` was already written above.
    // status == permanent: `raw` is left unused below, matching obs.py's
    // `raw = None` for this branch -- neither the bucket-absent path nor
    // apply_scale() is ever reached with a permanent status.

    double encoded;
    if ( f.kind == rl_kind::k_bucket )
    {
      // obs.py:253-278: an "absent" bucket leaf encodes its `missing`
      // value DIRECTLY, bypassing the ordinal floor-match entirely
      // (WR-03 -- -1.0 must stay distinct from every legitimate ordinal
      // 0.0/0.5/1.0). A "permanent" bucket leaf is an ERROR in Python
      // (obs.py:281, raises naming the slot) -- no bucket field in this
      // schema describes a buff, so this is a loud registry-misconfiguration
      // refusal on both sides, never a silent saturation.
      if ( status == lookup_status::absent )
      {
        encoded = f.missing;
      }
      else if ( status == lookup_status::permanent )
      {
        throw sc_runtime_error( fmt::format(
            "rl_policy::build_obs: observation slot={} leaf='{}' is kind=bucket but its binding "
            "resolved to status=permanent -- no bucket field can legitimately be permanent "
            "(permanence is a buff-expiry concept); this is a registry/binding misconfiguration, "
            "not a valid build_obs() input",
            slot, f.leaf ) );
      }
      else
      {
        encoded = encode_bucket_ordinal( raw, f.buckets, f.n_buckets );
      }
    }
    else if ( status == lookup_status::permanent )
    {
      encoded = RL_PERMANENT_SATURATION;
    }
    else
    {
      encoded = apply_scale( raw, f.kind, f );
    }

    // Arithmetic in double throughout; assign to the float output only at
    // the very end (AP-7).
    out_obs[ slot ] = static_cast<float>( encoded );
  }
}

// Sanity check (comment only, not executable): with `player_buffs` fully
// bound and every OTHER family still `unresolved` (this task's own scope),
// an unresolved-cooldown slot (e.g. `cooldowns.strike.charges_fractional`,
// missing=2.0, div=2) encodes via the absent branch to `2.0/2 = 1.0`, never
// `0.0` -- and an always-up buff (`permanent == true`, its `remains` leaf)
// encodes to `RL_PERMANENT_SATURATION == 1.0`, never to its `missing`
// value. Both traps (P-1/P-2) are closed by the dispatch above, not by a
// special case.

// ---------------------------------------------------------------------------
// cd_ready_now -- ported COMPLETE, including the fourth line, from
// simc_channel.py:269-298. THE TRAP THAT DOCSTRING NAMES: this function's
// body is FOUR lines, and the fourth line -- the plain single-charge
// `return remains <= 0.0` fallback -- is the ENTIRE single-charge contract,
// not a default to trim. Losing it makes every single-charge spell read as
// permanently castable.
// ---------------------------------------------------------------------------

namespace
{
bool cd_ready_now( const cooldown_reading& row )
{
  // A JSON null `remains` coerces to 0.0, i.e. ready -- so an absent
  // has_remains reads as ready, matching `.get("remains", 0.0) or 0.0`.
  const double remains = row.has_remains ? row.remains : 0.0;
  if ( row.max_charges > 1 )
  {
    const double charges = row.has_charges ? row.charges : 0.0;
    return charges > 0.0 || remains <= 0.0;
  }
  return remains <= 0.0;
}
} // anonymous namespace

// ---------------------------------------------------------------------------
// build_mask -- REAL, all four rules, mirroring mask.py:133-172 exactly.
// ---------------------------------------------------------------------------

void build_mask( const rl_state_t& s, std::uint8_t out_mask[ RL_ACTION_DIM ] )
{
  for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
  {
    const rl_action_desc& a = RL_ACTIONS[ i ];

    // R1: a wait action is legal ONLY at a foreground boundary. A `wait`
    // reply at any other boundary hard-aborts the engine
    // (protocol_abort) -- the mask is the only thing between the agent
    // and that FATAL.
    if ( a.kind == rl_action_kind::wait )
    {
      out_mask[ i ] = s.boundary_is_foreground ? 1 : 0;
      continue;
    }

    // R2: buff gate (mask.py's maskRules.buffGates). FAIL-CLOSED,
    // evaluated FIRST -- and deliberately the OPPOSITE polarity to R3's
    // absent-cooldown-row rule below. mask.py's own capitalized warning,
    // reproduced verbatim in substance: an unsatisfied buff gate means
    // "the engine will refuse this cast" (restrictive is correct), while
    // an absent cooldown row means "this action has no cooldown"
    // (permissive is correct). DO NOT HARMONISE THE TWO.
    bool denied = false;
    for ( std::size_t g = 0; g < RL_BUFF_GATE_COUNT; ++g )
    {
      const rl_buff_gate& gate = RL_BUFF_GATES[ g ];
      if ( a.token == nullptr || std::strcmp( gate.action_token, a.token ) != 0 )
        continue;
      const buff_reading* b = s.find_buff( gate.buff_name );
      const bool present = ( b != nullptr && b->present );
      if ( gate.forbidden )
      {
        // forbiddenBuff: deny when the named buff IS present.
        if ( present )
          denied = true;
      }
      else
      {
        // requiresBuff: deny when the named buff is ABSENT.
        if ( !present )
          denied = true;
      }
    }
    if ( denied )
    {
      out_mask[ i ] = 0;
      continue;
    }

    // R3/R4: cooldown-row legality. "A cast action WITHOUT a cooldownRow,
    // OR whose named row is ABSENT from request['cooldowns']: legal."
    // Deliberate FAIL-OPEN exception to this repo's usual fail-closed
    // mask convention (mask.py:22-32) -- do not harmonise with R2 above.
    if ( a.cooldown_row == nullptr )
    {
      out_mask[ i ] = 1;
      continue;
    }
    const cooldown_reading* row = s.find_cooldown( a.cooldown_row );
    if ( row == nullptr )
    {
      out_mask[ i ] = 1;
      continue;
    }
    out_mask[ i ] = cd_ready_now( *row ) ? 1 : 0;
  }
}

// ---------------------------------------------------------------------------
// build_wait -- REAL, mirroring mask.py:175-243 exactly (next_event_wait_detail).
// ---------------------------------------------------------------------------

namespace
{
struct wait_candidate
{
  double seconds;
  bool is_cooldown;
  const std::string* row_name;   // valid only if is_cooldown
  const char* literal_source;    // valid only if !is_cooldown
};
} // anonymous namespace

wait_result build_wait( const rl_state_t& s )
{
  std::vector<wait_candidate> candidates;

  // Append order IS the tie-break rule (P-3): every cooldown row's
  // candidates, in RL_ACTIONS declaration order (a row's `remains`
  // candidate before its `recharge_time` candidate), THEN swing_mh, THEN
  // gcd. Zero-valued clocks are DROPPED (> 0, never >= 0).
  for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
  {
    const rl_action_desc& a = RL_ACTIONS[ i ];
    if ( a.cooldown_row == nullptr )
      continue;
    const cooldown_reading* row = s.find_cooldown( a.cooldown_row );
    if ( row == nullptr )
      continue;
    if ( row->has_remains && row->remains > 0.0 )
      candidates.push_back( { row->remains, true, &row->name, nullptr } );
    if ( row->has_recharge_time && row->recharge_time > 0.0 )
      candidates.push_back( { row->recharge_time, true, &row->name, nullptr } );
  }

  if ( s.has_swing_mh_remains && s.swing_mh_remains > 0.0 )
    candidates.push_back( { s.swing_mh_remains, false, nullptr, "swing_mh" } );

  if ( s.has_gcd_remains && s.gcd_remains > 0.0 )
    candidates.push_back( { s.gcd_remains, false, nullptr, "gcd" } );

  if ( candidates.empty() )
    return wait_result{ RL_WAIT_FLOOR_SECONDS, "floor", true };

  // Scan with STRICT < so the FIRST-seen minimum wins on a tie (P-3) --
  // never re-derived or reshuffled elsewhere.
  std::size_t best = 0;
  for ( std::size_t i = 1; i < candidates.size(); ++i )
  {
    if ( candidates[ i ].seconds < candidates[ best ].seconds )
      best = i;
  }

  const wait_candidate& winner = candidates[ best ];

  // WR-05 fix (210-CR-FIX): `source` is a `std::string` in `wait_result`
  // now, so a cooldown-row label ("cooldown:<row>") is built directly into
  // the return value -- no thread_local buffer, no lifetime invariant tying
  // this to "exactly one wait_result alive at a time, consumed
  // synchronously". One allocation on the wait arm only, not the hot path.
  std::string source;
  if ( winner.is_cooldown )
    source = "cooldown:" + *winner.row_name;
  else
    source = winner.literal_source;

  const bool floored = winner.seconds < RL_WAIT_FLOOR_SECONDS;
  const double seconds = std::max( winner.seconds, RL_WAIT_FLOOR_SECONDS );
  return wait_result{ seconds, source, floored };
}
} // namespace rl_policy
