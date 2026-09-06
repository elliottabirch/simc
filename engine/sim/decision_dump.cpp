// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "decision_dump.hpp"

#include "action/action.hpp"
#include "action/attack.hpp"
#include "action/dot.hpp"
#include "action/sequence.hpp"
#include "buff/buff.hpp"
#include "item/item.hpp"
#include "player/player.hpp"
#include "sim/cooldown.hpp"
#include "sim/event.hpp"
#include "sim/gain.hpp"
#include "sim/rl_policy.hpp"
#include "sim/rl_target_select.hpp"
#include "sim/sim.hpp"
#include "util/concurrency.hpp"
#include "util/io.hpp"

#include <cstring>
#include <algorithm>
#include <map>
#include <stdexcept>

#include <set>
#include <vector>
#include <sstream>

namespace
{
// Resolves the actual sub-action about to execute at this decision boundary,
// unwrapping a sequence_t/strict_sequence_t wrapper action to its real next
// sub-action rather than the wrapper's own generic name_str ("default"/
// "strict_sequence") -- the exact gotcha P3-findings.md found under the
// spike's own `sequence`-driven replay, where `chosen` collapsed to the
// sequence wrapper's name ("plan") on every decision (116-02). Both
// sequence_t and strict_sequence_t track their next-to-execute sub-action
// index in a public `current_action` member which is only advanced inside
// their own schedule_execute() -- called AFTER this decision boundary -- so
// reading it here always names the sub-action about to fire, never one
// already committed. Returns nullptr only when `a` itself is null (idle/wait
// decision).
action_t* resolve_current_action( action_t* a )
{
  if ( !a )
    return nullptr;

  if ( auto* strict = dynamic_cast<strict_sequence_t*>( a ) )
  {
    if ( strict->current_action < strict->sub_actions.size() )
      return strict->sub_actions[ strict->current_action ];
    return a;
  }

  if ( auto* seq = dynamic_cast<sequence_t*>( a ) )
  {
    if ( seq->current_action >= 0 && static_cast<size_t>( seq->current_action ) < seq->sub_actions.size() )
      return seq->sub_actions[ seq->current_action ];
    return a;
  }

  return a;
}

// ---------------------------------------------------------------------------
// resolved_spell_id identity resolution (2026-08-01 id0 investigation --
// .planning/research/2026-08-01-simc-action-identity-id0.md and
// -id0-census.md in the consuming project). Measured fact: a plain
// `action_t::data().id()` read collides at 0 for FOUR distinct action names
// observed at real decision boundaries -- auto_attack, potion,
// use_item_algethar_puzzle_box, wait -- because all four are constructed
// with `spell_data_t::nil()`, which value-initializes `_id` to 0 by design
// (action.hpp:642-650). The tiered resolver below recovers a real id where
// one genuinely exists (potion/flask/food/augmentation -- consumable.cpp
// populates `s_data_reporting`, never `s_data`) and assigns a stable,
// per-action-name sentinel where no id exists anywhere in the engine's own
// model (auto_attack, wait, run_action_list, ...).

// Tier-4a sentinel band for engine actions that carry NO spell id anywhere --
// not in data(), not in data_reporting(), not in the plain `id` member -- AND
// have a fixed, literal name_str that a static table can key on. Reserved
// band: 9,000,000-9,000,999. (Tier 4b, immediately below, reserves its own
// non-overlapping 9,100,000-9,899,999 sub-band for the use_item family,
// whose name_str is dynamically item-suffixed and cannot live in this static
// table; RESOLVED_ID_UNMAPPED reserves 9,999,999.)
//
// Why the 9,000,000+ space specifically:
//  - Every real Blizzard spell id observed against this fork's ret paladin
//    dataset tops out at 1,261,562 (SHIELD_OF_VENGEANCE,
//    2026-08-01-our-side-action-identity.md); nine million is comfortably
//    outside any spell id space Blizzard has allocated to date.
//  - The TSTL project that consumes this dump (tstl-sylvanas-solver-v2)
//    already has ITS OWN sentinel convention in the 999,000-999,999 band
//    (DEC-013, e.g. `POTION = 999001` in
//    src/ext_rotation_paladin_ret_bb/spellIds.ts), but that band is
//    per-rotation and explicitly NOT globally unique -- 999001 means POTION
//    in the ret paladin rotation and TRINKET_1_USE in the frost mage
//    rotation (same research doc, section 1.2). Reusing 999xxx here, a
//    SimC-fork-level identity with no rotation scoping at all, would risk
//    exactly the kind of silent cross-namespace aliasing that band's own
//    design tolerates internally but this dump must not reproduce. A
//    completely different band (9,000,000+) makes that conflation
//    structurally impossible rather than merely unlikely.
//
// Keyed by the action's own name_str (SimC's stable APL/report token), NOT
// its C++ class -- two distinct id-less action TYPES that happen to share a
// sentinel would silently reintroduce the exact injectivity bug this table
// exists to close. Conversely, `wait` and `wait_until_ready` intentionally
// resolve to the SAME sentinel: `wait_until_ready_t` is constructed via
// `wait_fixed_t`'s own ctor and therefore inherits the literal name "wait"
// (player.cpp:9939-9945) -- they really are the same name-identity, so
// sharing a sentinel here is correct, not a collision.
struct sentinel_entry_t
{
  const char* name;
  unsigned id;
};

constexpr sentinel_entry_t RESOLVED_ID_SENTINELS[] = {
  // Melee/swing actions -- constructed with spell_data_t::nil() explicitly
  // (sc_paladin.cpp:985,1073); SimC's swing math is hardcoded off the
  // weapon table, never spell-record-driven, on every class checked.
  { "auto_attack", 9000001u },
  { "melee", 9000002u },
  // Wait family -- action_t(..., spell_data_t::nil()) explicit
  // (player.cpp:9839-9845). Pure engine bookkeeping, no in-game spell
  // correlate.
  { "wait", 9000010u },
  { "wait_for_cooldown", 9000011u },
  // Control-flow actions -- no spell data passed to the ctor at all
  // (action.cpp:4634,4682,4728). call_action_list is structurally excluded
  // from ever being `chosen` (player.cpp select_action() recurses through
  // it transparently) but is included here for completeness/defense.
  { "call_action_list", 9000020u },
  { "run_action_list", 9000021u },
  { "swap_action_list", 9000022u },
  // Bookkeeping/utility actions -- action_t(ACTION_OTHER, name, p), no
  // spell data (snapshot_stats.cpp:17, player.cpp:10785). variable is
  // structurally excluded from ever being `chosen` (consumed inline by
  // select_action()) but included for the same defensive reason as
  // call_action_list.
  { "snapshot_stats", 9000030u },
  { "pool_resource", 9000040u },
  { "variable", 9000050u },
  // mana_potion_t is, unlike potion_t/flask_t/food_t/augmentation_t, NOT a
  // dbc_consumable_base_t subclass -- it never calls initialize_consumable()
  // and so never gets a driver()-populated id or s_data_reporting
  // (consumable.cpp:186-230). Not reachable by the ret paladin (mana-resource
  // classes only) but included defensively per the id0 research doc's note.
  { "mana_potion", 9000060u },
};

// Marker for an action that reached Tier 5: no id anywhere, no item, and no
// registered Tier-4 sentinel -- i.e. a genuinely NEW id-less action class
// this table has never seen. Distinct from (and far outside) both the real
// spell-id space and the 9,000,000-9,000,999 Tier-4 band above, so it can
// never be confused with either. This is deliberately a SINGLE shared value
// across all unmapped names, not a per-name sentinel -- unlike Tier 4, this
// is a "go add this to the table" alarm, not a stable identity two
// unmapped actions could be safely compared against each other with.
constexpr unsigned RESOLVED_ID_UNMAPPED = 9999999u;

const sentinel_entry_t* find_sentinel( std::string_view name )
{
  for ( const auto& entry : RESOLVED_ID_SENTINELS )
  {
    if ( name == entry.name )
      return &entry;
  }
  return nullptr;
}

// Tiered resolution of a stable, non-colliding numeric identity for
// `resolved`. See the comment block above for the full rationale.
unsigned resolve_spell_id( action_t* resolved, sim_t* sim )
{
  // Tier 1: data_reporting() returns *s_data_reporting when that pointer is
  // non-nil, otherwise falls back to *s_data (action.cpp:4851-4856) -- so
  // this read is a strict superset of the old data().id() read: identical
  // result for every ordinary spell-backed action (s_data_reporting stays
  // nil() for those, action.cpp:340), and additionally recovers the real
  // triggered-spell id for potion/flask/food/augmentation, which
  // consumable.cpp:951-953 populates into s_data_reporting (and the `id`
  // member below) but deliberately never into s_data.
  if ( unsigned id = resolved->data_reporting().id() )
    return id;

  // Tier 2: belt-and-braces for any action that populated action_t::id
  // directly without going through s_data_reporting. Currently redundant
  // with Tier 1 for every action type this fork's research covered (potion
  // sets both id and s_data_reporting from the same driver()), but this is
  // strictly cheap insurance against a future/unaudited action type that
  // sets one and not the other.
  if ( resolved->id != 0 )
    return resolved->id;

  // Tier 3 (item ids) is intentionally NOT folded in here -- see the
  // separate resolved_item_id field emitted by the caller. Item ids and
  // spell ids are different DBC content-id namespaces; a shared numeric
  // space risks a genuine (if currently unobserved) collision between an
  // item id and a real spell id.

  // Tier 4a: genuinely id-less engine actions with a fixed, literal
  // name_str.
  if ( const sentinel_entry_t* entry = find_sentinel( resolved->name() ) )
    return entry->id;

  // Tier 4b: use_item family. use_item_t's name_str is NOT a fixed literal
  // -- it is item-name-suffixed at construction time
  // (player.cpp:10011,10037,10064: "use_item" + "_" + item_name), so a
  // static name->sentinel table (Tier 4a) can never cover it; measured
  // directly ("use_item_algethar_puzzle_box" fell through to Tier 5 on the
  // first verification run of this fix). use_item_t has no spell id
  // anywhere (bare `action_t(ACTION_OTHER, "use_item", player)`,
  // player.cpp:9989) but DOES have a resolved item id reachable via
  // `used_item()` -- a read-only virtual accessor (action.hpp) that
  // use_item_t overrides to expose its own item pointer WITHOUT writing to
  // the base `action_t::item` member (that member feeds real damage-scaling
  // decisions elsewhere in the engine -- action.cpp's `item_scaling` check
  // -- for OTHER action types; see action_t::used_item()'s own doc comment
  // for why a reporting-only need should not repurpose a gameplay-affecting
  // field even where doing so is inert today). Derive a stable, per-item sentinel
  // from that item id instead of a per-name table entry -- this still gives
  // each DISTINCT trinket its own distinct value (two different
  // `use_item,name=...` actions in the same run cannot alias each other),
  // matching the granularity of every other Tier 4 entry (auto_attack IS a
  // specific action, not a generic "no-id" bucket).
  //
  // Reserved sub-band: 9,100,000-9,899,999 (800,000 slots), computed as
  // 9,100,000 + item_id. Chosen to sit inside the same "SimC-fork sentinel"
  // 9,000,000+ space as Tier 4a/RESOLVED_ID_UNMAPPED but in its own
  // non-overlapping slice; current DBC item ids are well under 800,000 (the
  // two trinkets seen in this fork's ret paladin profiles are 193701 and
  // 249343), guarded below rather than assumed.
  if ( const item_t* used = resolved->used_item() )
  {
    unsigned item_id = used->parsed.data.id;
    if ( item_id > 0 && item_id < 800000u )
      return 9100000u + item_id;
  }

  // Tier 5: reached here with no id, no Tier 4a/4b sentinel -- a NEW
  // id-less action this table has never seen (or a use_item action whose
  // item id fell outside the guarded Tier 4b range). Do not silently
  // rejoin the id-0 collision bucket. Surface it loudly (once per unique
  // unmapped name, not every decision tick) via sim_t::errorf, which prints
  // directly to stderr (sim.cpp:3302-3311) -- visible in-run, not buried in
  // a JSONL file no one reads until later.
  static std::set<std::string> warned_unmapped_names;
  std::string name = resolved->name();
  if ( warned_unmapped_names.insert( name ).second )
  {
    sim->errorf(
        "decision_dump: action '%s' has no spell id, no reporting id, no registered Tier-4a name "
        "sentinel, and no usable Tier-4b item id in decision_dump.cpp -- resolved_spell_id is "
        "falling back to the RESOLVED_ID_UNMAPPED marker (%u). Add '%s' to RESOLVED_ID_SENTINELS "
        "or investigate why its item id (if any) fell outside the guarded range.",
        name.c_str(), RESOLVED_ID_UNMAPPED, name.c_str() );
  }
  return RESOLVED_ID_UNMAPPED;
}
} // anonymous namespace

namespace decision_dump
{
// Boundary-kind label (phase 200-04, FORK-01/R-8 -- widened control
// surface). Shared with solver_control.cpp's own request-line builder (see
// decision_dump.hpp's doc comment) so the wire and the dump can never
// disagree about the name for the same execute_type value.
std::string boundary_name( execute_type et )
{
  switch ( et )
  {
    case execute_type::FOREGROUND: return "foreground";
    case execute_type::OFF_GCD: return "off_gcd";
    case execute_type::CAST_WHILE_CASTING: return "cast_while_casting";
  }
  // WR-06 (2026-08-21): an unhandled execute_type must NEVER silently
  // re-label as "foreground" -- that would fold an un-comparable new
  // boundary INTO criterion 2's boundary=="foreground" projection instead
  // of out of it, turning a real divergence into an unexplained hash
  // mismatch. Fail loudly instead: this switch is exhaustive over
  // execute_type's three current values (sc_enums.hpp), so reaching here
  // means execute_type gained a value this helper was not updated for.
  throw std::runtime_error(
      "decision_dump::boundary_name: unhandled execute_type -- add a case "
      "here (and to solver_control.cpp's own boundary handling) before "
      "adding a new execute_type value" );
}

// Minimal JSON-string escaping - decision-boundary identifiers (action/buff/
// cooldown name_str) are simc internal snake_case tokens; the only
// real-world tokens seen with punctuation are spell display names, which
// this dump does not emit (name_str, not spell_name()), so this covers the
// one practical case (a stray quote) defensively rather than exhaustively.
std::string json_escape( std::string_view s )
{
  std::string out;
  out.reserve( s.size() );
  for ( char c : s )
  {
    if ( c == '"' || c == '\\' )
      out.push_back( '\\' );
    out.push_back( c );
  }
  return out;
}

double clamp_nonneg( double v )
{
  return v < 0.0 ? 0.0 : v;
}

namespace
{
// Defect (c) fix (2026-07-29 fix bundle): a buff with no scheduled expiration
// event (an indefinite/permanent buff, e.g. blessing_of_the_bronze,
// lights_deliverance) reports `buff_t::remains() == timespan_t::min()` --
// SimC's internal "no known expiry" sentinel -- which serializes as a huge
// negative number (timespan_t::min().total_seconds(), ~-9223370000000000.0)
// if dumped raw. Owner-decided wire representation: `remains:null` plus an
// explicit `permanent:true` marker instead. Finite remains are unchanged.
void write_buff_remains( std::ostream& out, timespan_t remains )
{
  if ( remains == timespan_t::min() )
    out << "null,\"permanent\":true";
  else
    out << remains.total_seconds();
}
} // anonymous namespace

// Shared decision-boundary state block -- see decision_dump.hpp. Reused
// verbatim by solver_control's "decision" request line (phase 116,
// simc-offline-evaluation-pipeline) so the two hooks can never drift.
//
// Shaman-scoped additions (tstl-sylvanas phase 165-01, elemental shaman
// parity judge): `maelstrom`/`maelstrom_max` (top-level), `charges_fractional`
// (per cooldown bucket), `duration`/`refreshable` (per dot bucket), and
// `enemy_debuff_counts` (top-level) are ALL additive and gated on the single
// predicate `p->resources.is_active( RESOURCE_MAELSTROM )` -- true only for
// actors whose primary resource is Maelstrom (shaman). Byte-neutrality
// contract (SC-5): every non-maelstrom actor's record bytes are UNCHANGED --
// ret (Holy Power) and arms (Rage) records are byte-identical before and
// after this patch; verified this session via `diff -rq` against both
// committed fixture corpora plus RUN_ARMS_DRIFT=1 replay. Four consumers:
// `maelstrom.deficit`, `cooldown.lava_burst.charges_fractional`,
// `dot.flame_shock.refreshable`, `lightning_rod` (via enemy_debuff_counts).
void write_state_fields( std::ostream& out, player_t* p, action_t* chosen, bool solver_reply_gated,
                          bool boundary_is_foreground, bool is_decision_boundary )
{
  sim_t* sim = p->sim;

  // GCD remaining - same formula as action.cpp's gcd_remains_expr_t, read
  // directly since we have no action anchor when `chosen` is null (idle/wait
  // decision).
  out << ",\"gcd_remains\":" << clamp_nonneg( ( p->gcd_ready - sim->current_time() ).total_seconds() );

  // 221-03 (ACT-05/ACT-06, Task 2 point 4) -- fight_remains/raid_event_next_in
  // as TOP-LEVEL scalars, DISTINCT from the observation vector's own
  // fight_remains/raid_event_next_in leaves (which reach the FIFO client
  // only after the whole observation encoding round-trip). The FIFO arm's
  // mask.py mirror (anchored_wait_seconds) needs both directly on this
  // wire request to apply the SAME clamp accept_wait() applies engine-side
  // -- Phase 220 deliberately did not widen this wire for that purpose
  // (see its own plan-07 follow-up todo). Computed through the SAME
  // rl_policy::next_raid_event_in()/fight_remains definitions read_state()
  // and accept_wait() use -- never a third independent walk of
  // sim->raid_events. raid_event_next_in is emitted as null (never a
  // saturation number) when nothing is pending.
  {
    const double fight_remaining =
        clamp_nonneg( ( sim->expected_iteration_time - sim->current_time() ).total_seconds() );
    out << ",\"fight_remains\":" << fight_remaining;
    double raid_event_bound = 0.0;
    if ( rl_policy::next_raid_event_in( sim, raid_event_bound ) )
      out << ",\"raid_event_next_in\":" << raid_event_bound;
    else
      out << ",\"raid_event_next_in\":null";
  }

  // solver_damage_so_far (FORK-03, tstl-sylvanas phase 205, owner ruling
  // OQ-1 2026-08-22) -- cumulative kit damage (pets included, D-05) since
  // the start of the current iteration, read exactly (no fractional bucket
  // apportionment) as the RL reward source at each decision boundary;
  // timeline_dmg apportionment demotes to this field's probe cross-check.
  // Additive; absent on every pre-change record, which every downstream
  // reader treats as 0 (no damage attributable since the previous
  // reward-source read). Emitted unconditionally -- universal across every
  // actor, unlike the resource-scoped fields below.
  out << ",\"solver_damage_so_far\":" << p->solver_damage_so_far;

  // solver_damage_expected_so_far (260901-pb1 Task 3, D-3/D-4, Lever B):
  // the crit-expectation-corrected sibling of solver_damage_so_far above --
  // same population, same pet routing, same reset cadence -- ADDITIVE on
  // this wire exactly like solver_damage_so_far itself was (absent on every
  // pre-change record, which every downstream reader treats as 0). Emitted
  // unconditionally, same rule as above.
  out << ",\"solver_damage_expected_so_far\":" << p->solver_damage_expected_so_far;

  // Swing timer, main-hand.
  if ( p->main_hand_attack && p->main_hand_attack->execute_event )
    out << ",\"swing_mh_remains\":" << p->main_hand_attack->execute_event->remains().total_seconds();
  else
    out << ",\"swing_mh_remains\":null";

  // Swing timer, off-hand (quick task 260826-38t, D-2R slot 8). Previously
  // omitted with the comment "ret paladin has no meaningful OH state" --
  // the enhancement shaman genuinely dual-wields and this fork HEAD started
  // the off-hand swinging (commit "fix(solver_control): start the OFF-HAND
  // swinging too, not just the main hand"), so the omission no longer
  // holds. Mirrors the main-hand emitter's two-branch shape exactly:
  // obs.py distinguishes absent (null) from zero.
  if ( p->off_hand_attack && p->off_hand_attack->execute_event )
    out << ",\"swing_oh_remains\":" << p->off_hand_attack->execute_event->remains().total_seconds();
  else
    out << ",\"swing_oh_remains\":null";

  // Resources - holy_power is the only resource the ret solver models;
  // dumped by enum value directly rather than iterating RESOURCE_MAX to
  // keep the line small and the field self-describing for the spike.
  out << ",\"holy_power\":" << p->resources.current[ RESOURCE_HOLY_POWER ];

  // Rage (143 D-09/D-10, arms-clean-room) - additive, scoped to
  // rage-primary actors only (p->primary_resource() == RESOURCE_RAGE) so
  // paladin/holy_power records stay byte-identical to the pre-change
  // output (SC-5). A future generic resources-block emission (iterating
  // RESOURCE_MAX like `holy_power` does not) is the more durable long-term
  // shape but would change ret's record bytes too -- deliberately deferred.
  if ( p->primary_resource() == RESOURCE_RAGE )
  {
    out << ",\"rage\":" << p->resources.current[ RESOURCE_RAGE ];

    // Per-named-gain ledger (143 D-09) - cumulative actual/overflow/count
    // for the actor's primary resource, one bucket per named gain_t SimC
    // already maintains (gain.melee_main_hand, gain.melee_crit, etc). Same
    // rage-primary scoping predicate as the `rage` field above. Between-
    // record deltas on this ledger give the smoke exact swing counts split
    // by crit outcome plus overcap, with no swing-timer reconstruction and
    // no crit inference (see 143-RESEARCH.md "Recommended resolution").
    // All-zero buckets are skipped to keep the line small.
    out << ",\"rage_gains\":{";
    bool first_gain = true;
    for ( gain_t* g : p->gain_list )
    {
      double actual = g->actual[ RESOURCE_RAGE ];
      double overflow = g->overflow[ RESOURCE_RAGE ];
      double count = g->count[ RESOURCE_RAGE ];
      if ( actual == 0.0 && overflow == 0.0 && count == 0.0 )
        continue;
      if ( !first_gain )
        out << ",";
      first_gain = false;
      out << "\"" << json_escape( g->name_str ) << "\":{";
      out << "\"actual\":" << actual;
      out << ",\"overflow\":" << overflow;
      out << ",\"count\":" << count;
      out << "}";
    }
    out << "}";
  }

  // Maelstrom (phase 165-01, tstl-sylvanas elemental shaman parity judge) -
  // additive, scoped to actors with an active Maelstrom resource via
  // p->resources.is_active( RESOURCE_MAELSTROM ). Deliberately NOT the
  // player's own declared primary-resource accessor (shaman_t overrides
  // that accessor to report RESOURCE_MANA -- sc_shaman.cpp:2167-2170 -- so
  // comparing it against RESOURCE_MAELSTROM would silently emit nothing for
  // every shaman actor) so ret/arms/every non-maelstrom actor's record
  // bytes stay byte-identical (SC-5). `charges_fractional` on the cooldown
  // block and `duration`/`refreshable` on the dot block below share this same
  // predicate for the same reason -- consumers: `maelstrom.deficit`,
  // `cooldown.lava_burst.charges_fractional`, `dot.flame_shock.refreshable`,
  // `lightning_rod` (enemy_debuff_counts, emitted after the dots block).
  const bool emit_maelstrom_ext = p->resources.is_active( RESOURCE_MAELSTROM );
  if ( emit_maelstrom_ext )
  {
    out << ",\"maelstrom\":" << p->resources.current[ RESOURCE_MAELSTROM ];
    out << ",\"maelstrom_max\":" << p->resources.max[ RESOURCE_MAELSTROM ];
  }

  // Cooldowns - every named cooldown with a nonzero base recharge (skips
  // the zero-duration bookkeeping cooldowns SimC creates internally).
  out << ",\"cooldowns\":{";
  bool first_cd = true;
  for ( cooldown_t* cd : p->cooldown_list )
  {
    if ( cd->duration <= timespan_t::zero() && cd->base_duration <= timespan_t::zero() )
      continue;
    if ( !first_cd )
      out << ",";
    first_cd = false;
    out << "\"" << json_escape( cd->name_str ) << "\":{";
    out << "\"remains\":" << cd->remains().total_seconds();
    out << ",\"charges\":" << cd->current_charge;
    out << ",\"max_charges\":" << cd->charges;
    out << ",\"recharge_time\":" << ( cd->recharge_event ? cd->recharge_event->remains().total_seconds() : 0.0 );
    if ( emit_maelstrom_ext )
      out << ",\"charges_fractional\":" << cd->charges_fractional();
    out << "}";
  }
  out << "}";

  // Buffs on the actor - active (stack > 0) only, to keep the line focused
  // on what a decision boundary actually needs to read.
  out << ",\"buffs\":{";
  bool first_buff = true;
  for ( buff_t* buff : p->buff_list )
  {
    if ( buff->check() <= 0 )
      continue;
    if ( !first_buff )
      out << ",";
    first_buff = false;
    out << "\"" << json_escape( buff->name() ) << "\":{";
    out << "\"stacks\":" << buff->check();
    out << ",\"remains\":";
    write_buff_remains( out, buff->remains() );
    out << "}";
  }
  out << "}";

  // Target debuffs - same active-only convention, read off the actor's
  // current target (single-target Patchwerk profile - see spike plan §7).
  out << ",\"target_debuffs\":{";
  if ( p->target )
  {
    bool first_debuff = true;
    for ( buff_t* buff : p->target->buff_list )
    {
      if ( buff->source != p || buff->check() <= 0 )
        continue;
      if ( !first_debuff )
        out << ",";
      first_debuff = false;
      out << "\"" << json_escape( buff->name() ) << "\":{";
      out << "\"stacks\":" << buff->check();
      out << ",\"remains\":";
      write_buff_remains( out, buff->remains() );
      out << "}";
    }
    out << "},\"target_time_to_die\":" << p->target->time_to_percent( 0 ).total_seconds();
  }
  else
  {
    out << "},\"target_time_to_die\":null";
  }

  // active_enemies - mirrors sim_t's own "active_enemies" APL-expression
  // semantics exactly (sim.cpp's expression-creation branch): a single-
  // target fight with no adds/pull raid events collapses to the constant 1,
  // otherwise the live sim_t::active_enemies counter (116-02, previously
  // unwired -- P2-state-mapping.md §5).
  {
    int active_enemies = 1;
    if ( !( sim->target_list.size() == 1U && !sim->has_raid_event( "adds" ) && !sim->has_raid_event( "pull" ) ) )
      active_enemies = sim->active_enemies;
    out << ",\"active_enemies\":" << active_enemies;
  }

  // DoT family - name-keyed like buffs, but "ticking" (not stack>0) is the
  // active predicate since dot_t has no stack-based inactive convention.
  // Filtered to this actor's own DoTs on its current target, mirroring the
  // target_debuffs source==p convention above. This is what supplies the
  // Expurgation family (116-02, previously unwired -- P2-state-mapping.md
  // §5): consumers read `dots.expurgation.{ticking,remains}`.
  out << ",\"dots\":{";
  if ( p->target )
  {
    bool first_dot = true;
    for ( dot_t* dot : p->target->dot_list )
    {
      if ( dot->source != p || !dot->is_ticking() )
        continue;
      if ( !first_dot )
        out << ",";
      first_dot = false;
      out << "\"" << json_escape( dot->name() ) << "\":{";
      out << "\"ticking\":" << ( dot->is_ticking() ? "true" : "false" );
      out << ",\"remains\":" << dot->remains().total_seconds();
      if ( emit_maelstrom_ext )
      {
        // duration/refreshable (phase 165-01) - the same dot_refreshable()
        // call the "refreshable" APL expression makes (dot.cpp:458-478),
        // evaluated against the dot's OWN current snapshot state
        // (dot->current_action, dot->state) instead of freshly re-snapshotting
        // a throwaway action_state_t the way the live expression does -- a
        // single documented approximation, acceptable because decision_dump
        // records a boundary, not a live re-snapshot.
        out << ",\"duration\":" << dot->duration().total_seconds();
        bool dot_refreshable_val = dot->current_action != nullptr &&
            dot->current_action->dot_refreshable( dot, dot->current_action->composite_dot_duration( dot->state ) );
        out << ",\"refreshable\":" << ( dot_refreshable_val ? "true" : "false" );
      }
      out << "}";
    }
  }
  out << "}";

  // Enemy debuff census (phase 165-01) - counts, across every actor in
  // sim->target_list, how many carry an active (check()>0) buff/debuff this
  // player (p) is the source of. Answers "how many enemies have my X up"
  // (e.g. lightning_rod) without needing a per-enemy target_debuffs block.
  // Deterministic std::map ordering keeps output byte-stable across runs.
  if ( emit_maelstrom_ext )
  {
    out << ",\"enemy_debuff_counts\":{";
    std::map<std::string, int> debuff_counts;
    for ( player_t* t : sim->target_list )
    {
      for ( buff_t* b : t->buff_list )
      {
        if ( b->source != p || b->check() <= 0 )
          continue;
        debuff_counts[ b->name() ]++;
      }
    }
    bool first_edc = true;
    for ( const auto& kv : debuff_counts )
    {
      if ( !first_edc )
        out << ",";
      first_edc = false;
      out << "\"" << json_escape( kv.first ) << "\":" << kv.second;
    }
    out << "}";
  }

  // Full GCD length (distinct from gcd_remains above, which is time-until-
  // ready, not the total duration) - PLAYER-scoped (2026-07-29 fix bundle,
  // defect (d)): the actor's own current haste-scaled standard GCD
  // (`player_t::base_gcd`/`min_gcd`, ATTACK_HASTE-scaled), matching what the
  // live TSTL context builder's `currentGcdInMs` means
  // (`resources.getCurrentGcdInMs()` reads a live PLAYER-scoped GCD via the
  // Sylvanas API's `core.spell_book.get_global_cooldown()`, not a specific
  // spell's own gcd()). Previously action-scoped (`chosen->gcd()`), which
  // degenerated to 0 whenever `chosen` was the zero-trigger_gcd auto_attack
  // placeholder (the entire solver_control path before the (a) fix, since
  // auto_attack never stopped being "ready") or null (any idle/wait
  // decision) -- no longer reads `chosen` at all.
  {
    timespan_t player_gcd = p->base_gcd * p->cache.attack_haste();
    if ( player_gcd < p->min_gcd )
      player_gcd = p->min_gcd;
    out << ",\"gcd_length\":" << player_gcd.total_seconds();
  }

  // Full auto-attack swing interval (distinct from swing_mh_remains above,
  // which is time-until-next-swing, not the full period) - PLAYER-scoped
  // (2026-07-29 fix bundle, defect (d)): computed directly from the main-hand
  // weapon's swing time and the actor's current auto-attack-speed haste,
  // rather than `attack_t::execute_time()` on the actual swing-timer action
  // (`melee_t` in the paladin module) -- that action class deliberately
  // special-cases its OWN execute_time() to 0 before the first swing has
  // actually landed and to 10ms before combat starts (see
  // `melee_t::execute_time()`, sc_paladin.cpp), which describes "time until
  // the next scheduled swing" (swing_mh_remains already answers that), not
  // "how long is a swing" -- the field this key is documented to mean.
  out << ",\"auto_attack_interval\":"
      << ( p->main_hand_attack ? ( p->main_hand_weapon.swing_time * p->cache.auto_attack_speed() ).total_seconds()
                                : 0.0 );

  // 228-10 Task 1 Step 6(b) (Q18): the immunity-remaining wire field mask.py's own mirror reads
  // reaches this SAME wire (write_state_fields is shared by both the JSONL dump append and the
  // FIFO request-line build, exactly like gcd_length/auto_attack_interval above) via the single
  // top-level "immunity_remaining" key emitted later in this function, alongside "immunity_in"
  // (the D-16 aggregate block) -- ONE key serves both the aggregate reader and Q18's wait mirror,
  // never two independently-emitted copies of the same number under the same name.

  // Resolved action identity (116-02; solver-path gating added 2026-07-29 fix
  // bundle, defect (b)) - the SimC-internal name_str of the action actually
  // about to execute at this boundary, unwrapping a sequence/strict_sequence
  // wrapper to its real next sub-action. Authoritative; the `chosen`
  // top-level key emitted by record() below is kept unchanged for backward
  // compatibility with the spike's own artefacts but is unreliable under a
  // sequence-driven run -- see PROTOCOL.md.
  //
  // `solver_reply_gated` (decision_dump::record()'s own call only, never
  // solver_control's wire-request-building call) forces `resolved_action:null`
  // plus an explicit `solver_reply_type` marker whenever the immediately-
  // preceding solver_control reply was NOT a "cast" (i.e. "wait"/"default"/
  // "abstain" -- none of those name a real cast action, so reporting
  // whatever `chosen` happens to resolve to there is exactly the
  // pre-resolution-placeholder bug this fix closes).
  //
  // `resolved_spell_id` (owner-ratified decision chain, step 2, 2026-08-01) -
  // added alongside `resolved_action` because the name alone is not a stable
  // comparison key: SimC renames the action's own `name_str` at construction
  // time based on live talent state, so the STRING is talent-dependent while
  // the spell id carries the real identity (see sc_paladin_retribution.cpp:663-668
  // -- `templars_verdict_t` picks between `find_spell(383328)` and
  // `find_specialization_spell("Templar's Verdict")` (85256) based on
  // `p->talents.final_verdict->ok()`, so `action_t::data().id()` reliably
  // discriminates Templar's Verdict vs Final Verdict even where the name_str
  // does not, or where a future rename changes the string without changing
  // the id). This is exactly why three alias tables exist today purely to
  // paper over the name-string instability: `NAME_STR_RENAME_ALIASES`
  // (solver_control.cpp:107), `RUNTIME_NAME_ALIASES`
  // (scripts/simc-eval/equivalence-check.py:205), and the rename entries in
  // scripts/simc-eval/name-map.json. This commit only EMITS the id field so
  // it can be measured; deciding whether the comparison key should switch to
  // id (and retiring/collapsing those alias tables) is step 3 of the chain,
  // deliberately NOT done here. Emitted in all three exits below (mirroring
  // `resolved_action`'s null in exit 1) so the field is never silently
  // absent -- null wherever `resolved_action` is null, a JSON number
  // (unquoted) wherever it is a string.
  //
  // 2026-08-01 tiering update: `resolved_spell_id` was measured to still
  // collide at 0 across FOUR distinct action names (auto_attack, potion,
  // use_item_algethar_puzzle_box, wait -- see 2026-08-01-id0-census.md), a
  // direct violation of the "unique, non-colliding identity" goal this field
  // exists for. `resolve_spell_id()` (top of this file) replaces the bare
  // `data().id()` read with a 5-tier resolution (data_reporting() -> plain
  // `id` member -> [item id handled separately below] -> per-name sentinel
  // -> loud unmapped marker) that recovers potion/flask/food/augmentation's
  // real id and gives every other genuinely id-less action its own distinct,
  // reserved-band sentinel so no two DIFFERENT action names can ever share a
  // value again.
  //
  // `resolved_item_id` (added alongside, same commit) - the resolved
  // action's backing item id (`used_item()->parsed.data.id`), for actions
  // whose `used_item()` (action.hpp -- a read-only virtual accessor, see its
  // own doc comment for why it exists instead of the base `action_t::item`
  // member) returns non-null (trinket/use_item actions). Deliberately a
  // SEPARATE field, not folded into `resolved_spell_id`: item ids and spell
  // ids are different DBC namespaces, and a numeric collision between a real
  // item id and a real spell id is possible in principle. null for every
  // action that isn't item-backed. Paired with `resolved_action` exactly
  // like `resolved_spell_id` is (null iff `resolved_action` is null).
  if ( solver_reply_gated && sim->solver_control_last_reply_type != "cast" )
  {
    out << ",\"resolved_action\":null";
    out << ",\"resolved_spell_id\":null";
    out << ",\"resolved_item_id\":null";
    out << ",\"solver_reply_type\":\"" << json_escape( sim->solver_control_last_reply_type ) << "\"";
  }
  else
  {
    if ( action_t* resolved = resolve_current_action( chosen ) )
    {
      out << ",\"resolved_action\":\"" << json_escape( resolved->name() ) << "\"";
      out << ",\"resolved_spell_id\":" << resolve_spell_id( resolved, sim );
      if ( const item_t* used = resolved->used_item() )
        out << ",\"resolved_item_id\":" << used->parsed.data.id;
      else
        out << ",\"resolved_item_id\":null";
    }
    else
    {
      out << ",\"resolved_action\":null";
      out << ",\"resolved_spell_id\":null";
      out << ",\"resolved_item_id\":null";
    }
    if ( solver_reply_gated )
      out << ",\"solver_reply_type\":\"" << json_escape( sim->solver_control_last_reply_type ) << "\"";
  }

  // 221-03 (ACT-05/ACT-06, Task 2 point 4) -- requested_wait_sec/wait_anchor:
  // the value HANDED TO accept_wait() before its own fight-end/raid-event
  // clamp, and the chosen wait's registry label. Gated the SAME way
  // solver_reply_type immediately above is (solver_reply_gated) -- these
  // fields describe what THIS boundary's solver reply chose, so they
  // belong only on decision_dump::record()'s post-reply call, never on the
  // wire-request-building call (which describes the UPCOMING boundary,
  // before any reply exists for it -- emitting a stale prior-boundary
  // value there would be exactly the pre-resolution-placeholder class of
  // bug resolved_action's own gating fixed, above). null on every
  // non-"wait" reply and whenever the FIFO arm's own reply carried no
  // anchor identity (wait_anchor only, PROTOCOL.md's "wait" shape has no
  // anchor field).
  if ( solver_reply_gated )
  {
    if ( sim->solver_control_last_reply_type == "wait" && sim->solver_control_has_requested_wait_sec )
      out << ",\"requested_wait_sec\":" << sim->solver_control_last_requested_wait_sec;
    else
      out << ",\"requested_wait_sec\":null";
    if ( sim->solver_control_last_reply_type == "wait" && !sim->solver_control_last_wait_anchor_label.empty() )
      out << ",\"wait_anchor\":\"" << json_escape( sim->solver_control_last_wait_anchor_label ) << "\"";
    else
      out << ",\"wait_anchor\":null";
    // 260902/cr2 (CR-03/WR-08's fix): wait_source -- which clock produced
    // this wait's seconds ("cooldown:<row>" / "swing_mh" / "gcd" /
    // "reask_cap" / "floor", or an anchored-wait source), plumbed the
    // EXACT same way wait_anchor immediately above is: gated on
    // solver_reply_gated + reply_type=="wait", null otherwise, and null on
    // the FIFO transport (the wire "wait" reply carries no source
    // identity -- PROTOCOL.md's `{"sec": float}` shape -- so
    // solver_control_last_wait_source stays empty there, same reason
    // wait_anchor stays empty on that transport). Never re-derived --
    // carried straight from the wait_result build_wait() returned.
    if ( sim->solver_control_last_reply_type == "wait" && !sim->solver_control_last_wait_source.empty() )
      out << ",\"wait_source\":\"" << json_escape( sim->solver_control_last_wait_source ) << "\"";
    else
      out << ",\"wait_source\":null";
  }

  // 221-01 (ACT-02, Pattern 3) -- the engine-truth legality layer, emitted
  // as RL_ACTION_DIM-long integer arrays in action-id order, filled by
  // calling rl_policy::read_action_gate_bits() -- the SAME computation
  // read_state() calls to fill its rl_state_t POD for the in-process arm
  // (never a second, independent walk here; see that function's own doc
  // comment for why two computations would be the exact drift class this
  // design closes). This function has exactly two call sites --
  // decision_dump::record()'s JSONL append below and solver_control.cpp's
  // FIFO request line -- so this ONE edit reaches both transports, which
  // is what lets mask.py AND the same bits on the wire that build_mask
  // ANDs in-process. Wait entries emit 0 (they have no action_t* to
  // resolve; read_action_gate_bits() already encodes that).
  //
  // WR-05 (221-08 code review): read_action_gate_bits()'s g_action_handle_
  // cache is a file-static, unlocked std::unordered_map keyed on a bare
  // const player_t* -- correct only under the single-sim/single-thread
  // precondition its own assert states, which is COMPILED OUT under
  // NDEBUG. Before 221 that precondition belonged only to the in-process
  // arm (single-threaded by construction); 221 reaches this SAME function
  // from BOTH call sites named above, including the FIFO request line and
  // decision_dump=<file> runs with no threading requirement of their own
  // -- a decision_dump=/threads>1 combination (previously legal; nothing
  // in the dump path required single-threading) now races on the cache in
  // a release build with no diagnostic. The precondition is now ENFORCED
  // here rather than merely asserted: a multi-threaded/profileset run
  // emits `null` for both arrays instead of touching the cache at all --
  // mask.py's `_engine_truth_denies` already treats an absent-or-null
  // array as "no engine truth on this wire" (a documented fail-open, not
  // a crash), so this degrades a multi-threaded dump into that existing,
  // named behaviour instead of undefined behaviour.
  if ( p->sim->threads == 1 && p->sim->profileset_map.empty() )
  {
    std::uint8_t action_resolvable[ RL_ACTION_DIM ];
    std::uint8_t action_ready[ RL_ACTION_DIM ];
    // 260902/cr4 (CR-02): threads the boundary distinction through -- see rl_policy.hpp's own doc
    // comment on read_action_gate_bits for the full mechanism. `used_dump_time_compute` is true
    // only on the non-boundary call when no cache existed (a scripted actor with decision_dump=
    // and no solver arm never took the boundary path this decision, or ever) -- named on the dump
    // line below so a reader can tell the arrays were computed AT DUMP TIME, off the action's
    // current target, rather than cached from the decision that was actually made.
    bool used_dump_time_compute = false;
    rl_policy::read_action_gate_bits( p, action_resolvable, action_ready, is_decision_boundary,
                                       &used_dump_time_compute );
    if ( used_dump_time_compute )
      out << ",\"action_gate_dump_time_compute\":true";
    out << ",\"action_resolvable\":[";
    for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
    {
      if ( i > 0 )
        out << ",";
      out << static_cast<int>( action_resolvable[ i ] );
    }
    out << "]";
    out << ",\"action_ready\":[";
    for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
    {
      if ( i > 0 )
        out << ",";
      out << static_cast<int>( action_ready[ i ] );
    }
    out << "]";
  }
  else
  {
    out << ",\"action_resolvable\":null";
    out << ",\"action_ready\":null";
  }

  // 228-09 (D-23/TGT-08, dump half) -- per-decision picked-target recording. For each of the
  // eight TARGETED registry actions, and for the CHOSEN action's own pick, record the actor
  // index / spawn index PAIR (the STICKY_KEY, R-C/P228-8 -- never the actor index alone) plus
  // the candidate's fact record. Every value is READ from
  // `rl_target_select::lookup_pick()`/`build_enemy_fact()` -- no `select()` call anywhere in
  // this block (D-12). Emitted HERE, immediately after this function's own
  // `read_action_gate_bits` call above and BEFORE the obs block below -- this is the ONLY safe
  // point in this function to read a pick: `read_state()`'s OWN internal call to
  // `read_action_gate_bits` (rl_policy_obs.cpp) is unconditionally `is_decision_boundary=true`,
  // independent of THIS function's own `is_decision_boundary` parameter, so the obs block a few
  // lines below re-bumps the per-player decision stamp and re-fills every targeted action's pick
  // via a FRESH `select()` call of its own -- a pre-existing behaviour of this shared function
  // (260901-od1's obs-block addition, unchanged by 260902/cr4's is_decision_boundary widening,
  // and out of THIS plan's scope to alter). Reading the pick here, before that nested call runs,
  // is what keeps this block's own picks equal to the ones `accept_cast` actually cast on;
  // reading after the obs block would read a re-computed pick from a hidden extra decision
  // instead. Gated the SAME way action_resolvable/action_ready above are (WR-12
  // single-sim/single-thread cache-safety precondition) -- `null` under threads>1/profileset,
  // matching the existing fail-open convention.
  if ( p->sim->threads == 1 && p->sim->profileset_map.empty() )
  {
    out << ",\"targeted_picks\":{";
    const std::size_t n_targeted = rl_target_select::targeted_action_token_count();
    const char* const* targeted_tokens = rl_target_select::targeted_action_tokens();
    for ( std::size_t i = 0; i < n_targeted; ++i )
    {
      if ( i > 0 )
        out << ",";
      const char* token = targeted_tokens[ i ];
      out << "\"" << token << "\":";
      action_t* a = p->find_action( token );
      if ( a == nullptr )
      {
        out << "null";
        continue;
      }
      bool      found = false;
      player_t* pick  = rl_target_select::lookup_pick( a, &found );
      if ( !found || pick == nullptr )
      {
        out << "{\"found\":false}";
        continue;
      }
      const rl_target_select::enemy_fact fact = rl_target_select::build_enemy_fact( a, pick );
      out << "{\"found\":true"
          << ",\"actor_index\":" << fact.actor_index
          << ",\"actor_spawn_index\":" << fact.actor_spawn_index
          << ",\"distance\":" << fact.distance
          << ",\"in_front\":" << ( fact.in_front ? "true" : "false" )
          << ",\"in_reach\":" << ( fact.in_reach ? "true" : "false" )
          << ",\"in_range\":" << ( fact.in_range ? "true" : "false" )
          << ",\"alive\":" << ( fact.alive ? "true" : "false" )
          << ",\"immune\":" << ( fact.immune ? "true" : "false" )
          << ",\"immunity_remaining\":" << fact.immunity_remaining
          << ",\"time_to_die\":" << fact.time_to_die
          << ",\"health_pct\":" << fact.health_pct
          << ",\"is_boss\":" << ( fact.is_boss ? "true" : "false" )
          << ",\"flame_shock_remaining\":" << fact.flame_shock_remaining
          // 228-10 Task 1 Step 1 (D-03/D-16): the per-enemy facts new to this plan --
          // Burning Core, Lightning Rod, the trinket (venomfang) and omnium
          // (rune_of_unleashed_fire_lingering) debuffs already on the wire via the old
          // enemy-slot family, now also on the per-targeted-action pick record.
          << ",\"burning_core_remaining\":" << fact.burning_core_remaining
          << ",\"lightning_rod_stacks\":" << fact.lightning_rod_stacks
          << ",\"lightning_rod_remaining\":" << fact.lightning_rod_remaining
          << ",\"venomfang_remaining\":" << fact.venomfang_remaining
          << ",\"venomfang_debuff_stacks\":" << fact.venomfang_debuff_stacks
          << ",\"venomfang_debuff_remaining\":" << fact.venomfang_debuff_remaining
          << ",\"rune_of_unleashed_fire_lingering_remaining\":" << fact.rune_of_unleashed_fire_lingering_remaining
          << ",\"neighbours_within_radius\":" << fact.neighbours_within_radius
          << ",\"is_current_target\":" << ( fact.is_current_target ? "true" : "false" )
          << "}";
    }
    out << "}";

    // The CHOSEN action's own pick (D-23) -- named again at the top level (rather than making a
    // reader cross-reference `chosen` against the map above by hand) so the equivalence probe
    // and the translog round-trip (228-09 Task 2) can read one fixed key regardless of which
    // token was cast this decision. `null` when the chosen action is untargeted, a wait, or a
    // SHAPED action (Crash Lightning/Sundering never carry a pick -- OR-2, NO_SHAPED_PICK_ON_WIRE).
    if ( chosen != nullptr && rl_target_select::is_targeted_action( chosen ) )
    {
      bool      found = false;
      player_t* pick  = rl_target_select::lookup_pick( chosen, &found );
      if ( found && pick != nullptr )
      {
        const rl_target_select::enemy_fact fact =
            rl_target_select::build_enemy_fact( chosen, pick );
        out << ",\"chosen_pick\":{\"found\":true"
            << ",\"actor_index\":" << fact.actor_index
            << ",\"actor_spawn_index\":" << fact.actor_spawn_index
            << "}";
      }
      else
      {
        out << ",\"chosen_pick\":{\"found\":false}";
      }
    }
    else
    {
      out << ",\"chosen_pick\":null";
    }
  }
  else
  {
    out << ",\"targeted_picks\":null";
    out << ",\"chosen_pick\":null";
  }

  // 228-07 (TGT-07, D-21) -- the parity harness's engine-sourced corpus. For each of the eight
  // TARGETED registry actions, the FULL candidate set (every enemy `generic_filter` would pass,
  // via `rl_target_select::build_candidate_facts` -- the SAME function select() itself consults,
  // D-20: no re-derivation) as complete fact records, keyed by the stable
  // (actor_index, actor_spawn_index) identity pair so an external reimplementation of the eight
  // preferences (spec/helpers/enhancementTargetRulesReplay.ts) can be handed the SAME numbers the
  // fork's own preference sees and asked to reproduce `targeted_picks[token]` above -- never a
  // record rebuilt from any OTHER key on this row (T-228-07-02). Gated identically to
  // targeted_picks/chosen_pick above (WR-12): `null` under threads>1/profileset. `x`/`y` are the
  // candidate's absolute world position (`x_position`/`y_position`) -- read here for the first
  // time on this row because only a per-candidate record (not the single-pick summary above) has
  // a use for two candidates' positions relative to each other (chain_lightning/tempest's own
  // neighbour count already exists as `neighbours_within_radius` below (OBS-03, 232-02);
  // `x`/`y` let an external reimplementation cross-check that count independently, matching the
  // hand-written fixture corpus's own planar-position schema, spec/fixtures/selector-parity/).
  if ( p->sim->threads == 1 && p->sim->profileset_map.empty() )
  {
    out << ",\"candidate_facts\":{";
    const std::size_t n_targeted = rl_target_select::targeted_action_token_count();
    const char* const* targeted_tokens = rl_target_select::targeted_action_tokens();
    for ( std::size_t i = 0; i < n_targeted; ++i )
    {
      if ( i > 0 )
        out << ",";
      const char* token = targeted_tokens[ i ];
      out << "\"" << token << "\":";
      action_t* a = p->find_action( token );
      if ( a == nullptr )
      {
        out << "null";
        continue;
      }
      const std::vector<rl_target_select::enemy_fact> candidates =
          rl_target_select::build_candidate_facts( a, /*harmful=*/true );
      // `cast_time_ms` -- `a->execute_time()`, the SAME call
      // `preference_shortest_time_to_die`'s own outlives-the-cast dominance clause reads (D-15,
      // unchanged by OR-1). Per-token, not per-candidate (it does not depend on which candidate is
      // scored) -- exported here so an external reimplementation of that clause is handed the
      // EXACT cast time the fork's own preference used, never a guessed or hardcoded per-spell
      // constant (T-228-07-02's "no re-derivation" reasoning, applied to this scalar too).
      out << "{\"cast_time_ms\":" << ( a->execute_time().total_seconds() * 1000.0 )
          << ",\"candidates\":[";
      for ( std::size_t ci = 0; ci < candidates.size(); ++ci )
      {
        if ( ci > 0 )
          out << ",";
        const rl_target_select::enemy_fact& fact = candidates[ ci ];
        out << "{\"actor_index\":" << fact.actor_index
            << ",\"actor_spawn_index\":" << fact.actor_spawn_index
            << ",\"x\":" << ( fact.candidate ? fact.candidate->x_position : 0.0 )
            << ",\"y\":" << ( fact.candidate ? fact.candidate->y_position : 0.0 )
            << ",\"distance\":" << fact.distance
            << ",\"in_front\":" << ( fact.in_front ? "true" : "false" )
            << ",\"in_reach\":" << ( fact.in_reach ? "true" : "false" )
            << ",\"in_range\":" << ( fact.in_range ? "true" : "false" )
            << ",\"alive\":" << ( fact.alive ? "true" : "false" )
            << ",\"immune\":" << ( fact.immune ? "true" : "false" )
            << ",\"immunity_remaining\":" << fact.immunity_remaining
            << ",\"time_to_die\":" << fact.time_to_die
            << ",\"health_pct\":" << fact.health_pct
            << ",\"is_boss\":" << ( fact.is_boss ? "true" : "false" )
            << ",\"flame_shock_remaining\":" << fact.flame_shock_remaining
            << ",\"neighbours_within_radius\":" << fact.neighbours_within_radius
            << ",\"is_current_target\":" << ( fact.is_current_target ? "true" : "false" )
            // 230-04 (SCOR-02, Task 2): the seven 228-10 gap-fill fields, previously present on
            // `targeted_picks` (the CHOSEN pick's own record, above) but missing here on the FULL
            // candidate list -- scorer_block_equivalence.py needs every one of the 22 declared
            // RL_TARGET_FEATURE_NAMES for EVERY candidate, not only the one that was picked, to
            // re-derive the transition log's own candidate block and compare it against the
            // engine's independent fact record (the probe's whole reason for existing).
            << ",\"burning_core_remaining\":" << fact.burning_core_remaining
            << ",\"lightning_rod_stacks\":" << fact.lightning_rod_stacks
            << ",\"lightning_rod_remaining\":" << fact.lightning_rod_remaining
            << ",\"venomfang_remaining\":" << fact.venomfang_remaining
            << ",\"venomfang_debuff_stacks\":" << fact.venomfang_debuff_stacks
            << ",\"venomfang_debuff_remaining\":" << fact.venomfang_debuff_remaining
            << ",\"rune_of_unleashed_fire_lingering_remaining\":"
            << fact.rune_of_unleashed_fire_lingering_remaining
            << "}";
      }
      out << "]}";
    }
    out << "}";
  }
  else
  {
    out << ",\"candidate_facts\":null";
  }

  // 228-07 (TGT-07, D-21) -- the SHAPE half of the parity harness's engine-sourced corpus.
  // `shape_facts` below (228-10/228-11) is the fork's own SUMMARY (enemies_hit,
  // summed_remaining_life, long_lived_count); an external reimplementation needs the per-enemy
  // geometry that summary was folded from, plus the player's own current position/facing, to
  // recompute it independently and compare. Never gated on WR-12 (same reasoning as
  // target_aggregates below: a stateless read of target_non_sleeping_list/x_position/y_position/
  // facing_x/facing_y, safe under any threading configuration) -- and NOT restricted to any one
  // action's own generic_filter (crash_lightning_cone_contains/sundering_rect_contains iterate
  // every enemy on target_non_sleeping_list with no immunity/range/harmful filter at all, D-16 --
  // reusing candidate_facts above would silently drop an immune or out-of-range enemy the shape
  // math still counts).
  {
    out << ",\"player_position\":{\"x\":" << p->x_position << ",\"y\":" << p->y_position
        << ",\"facing_x\":" << p->facing_x << ",\"facing_y\":" << p->facing_y << "}";
    out << ",\"all_enemies\":[";
    bool first_enemy = true;
    for ( player_t* t : p->sim->target_non_sleeping_list )
    {
      if ( !t->is_enemy() )
        continue;
      if ( !first_enemy )
        out << ",";
      first_enemy = false;
      const double ttd = std::min( t->time_to_percent( 0 ).total_seconds(), 600.0 );  // WR-10 clip
      out << "{\"actor_index\":" << t->actor_index << ",\"actor_spawn_index\":" << t->actor_spawn_index
          << ",\"x\":" << t->x_position << ",\"y\":" << t->y_position
          << ",\"combat_reach\":" << t->combat_reach << ",\"time_to_die\":" << ttd
          << ",\"is_boss\":" << ( t->is_boss() ? "true" : "false" ) << "}";
    }
    out << "]";
  }

  // 228-10 Task 1 Steps 3/4/5 (D-16/D-17/TGT-05/TGT-06) -- the identity-free aggregates, the
  // fight-wide immunity timers and the two shaped-spell hit-set facts, every one an ADDITIVE
  // key (no existing key's shape changes). None of these three computations touch
  // rl_target_select's file-static pick/stamp tables, so unlike targeted_picks/chosen_pick above
  // they are NOT gated on the WR-12 single-sim/single-thread precondition -- each is a plain,
  // stateless read of live engine state (target_non_sleeping_list, sim->raid_events,
  // player_t::facing_x/y), safe under any threading configuration.
  {
    const rl_policy::fight_wide_aggregates_t agg = rl_policy::compute_fight_wide_aggregates( p );
    out << ",\"target_aggregates\":{";
    out << "\"enemies_total\":" << agg.enemies_total;
    out << ",\"enemies_in_melee\":" << agg.enemies_in_melee;
    out << ",\"enemies_within_8yd\":" << agg.enemies_within_8yd;
    out << ",\"enemies_within_40yd\":" << agg.enemies_within_40yd;
    out << ",\"enemies_in_front\":" << agg.enemies_in_front;
    out << ",\"flame_shock_carrier_count\":" << agg.flame_shock_carrier_count;
    out << ",\"soonest_time_to_die\":";
    if ( agg.has_soonest_time_to_die ) out << agg.soonest_time_to_die; else out << "null";
    out << ",\"longest_time_to_die\":";
    if ( agg.has_longest_time_to_die ) out << agg.longest_time_to_die; else out << "null";
    out << ",\"dying_within_5s\":" << agg.dying_within_5s;
    out << ",\"dying_within_15s\":" << agg.dying_within_15s;
    out << ",\"nearest_enemy_distance\":";
    if ( agg.has_nearest_enemy_distance ) out << agg.nearest_enemy_distance; else out << "null";
    out << "}";
  }

  // TGT-06/D-17/Q18 -- the fight-wide immunity timers, both from the ONE
  // compute_invulnerability_window() read (D-12: read once, use twice -- the SAME function
  // read_state() calls to fill rl_state_t::immunity_remaining for Q18's wait candidate). Nothing
  // pending/active encodes as 0.0 (never a saturation sentinel, never null) -- the value this
  // plan's receipt records as the wire's "nothing pending" convention.
  {
    const rl_policy::invulnerability_window_t iw = rl_policy::compute_invulnerability_window( sim );
    out << ",\"immunity_in\":" << ( iw.has_next ? iw.next_in : 0.0 );
    out << ",\"immunity_remaining\":" << ( iw.active ? iw.remaining : 0.0 );
  }

  // D-16 shaped-spell block (TGT-04, OR-2) -- Crash Lightning and Sundering carry no pick (never
  // in targeted_picks above), but the observation still needs "what would this shape hit from
  // here" -- computed from the CURRENT facing only, through the ONE shared geometry copy
  // (rl_target_select::crash_lightning_cone_contains / sundering_rect_contains).
  {
    const rl_policy::shape_hit_result_t cl = rl_policy::compute_crash_lightning_shape( p );
    out << ",\"shape_facts\":{";
    out << "\"crash_lightning\":{";
    out << "\"enemies_hit\":" << cl.enemies_hit;
    out << ",\"summed_remaining_life\":" << cl.summed_remaining_life;
    out << ",\"long_lived_count\":" << cl.long_lived_count;
    out << "}";

    const rl_policy::shape_hit_result_t su = rl_policy::compute_sundering_shape( p );
    out << ",\"sundering\":{";
    out << "\"enemies_hit\":" << su.enemies_hit;
    out << ",\"summed_remaining_life\":" << su.summed_remaining_life;
    out << ",\"long_lived_count\":" << su.long_lived_count;
    out << "}";
    out << "}";
  }

  // 260901-od1 (kill-the-dual-encoder-seam) -- the engine-encoded
  // observation vector + legality mask, run through the SAME
  // bind_slots -> read_state -> build_mask -> build_obs pipeline (same
  // order) solver_control.cpp's in-process transport uses
  // (solver_control.cpp's own read_state -> build_mask -> [allow-list AND]
  // -> [all-illegal refusal] -> build_obs comment, ~line 590) -- minus the
  // allow-list AND and the all-illegal refusal, both arm-subset training
  // concepts with no meaning for a dump-only run (see this function's own
  // doc comment in decision_dump.hpp for the full rationale). No
  // solver_policy= is read anywhere in this pipeline -- bind_slots is
  // registry-constants-driven (rl_policy_constants.h), not policy-driven --
  // so this is available on a bare APL-driven decision_dump=<file> run.
  // Gated identically to action_resolvable/action_ready immediately above
  // (WR-12 single-sim/single-thread cache-safety precondition).
  // 260901-od1 post-landing fix (real HecticAddCleave capture, seed 101):
  // measured via a temporary stderr trace that `bind_slots`/`read_state`/
  // `build_mask` all complete cleanly for EVERY actor type that reaches
  // this function (the primary shaman, its own pets, AND enemy/target
  // actors -- decision_dump::record() fires from player_t::execute_action()
  // unconditionally, for every actor in the sim, not just the RL-relevant
  // one) but `build_obs()` itself segfaults the FIRST time it is called
  // for a non-shaman actor (measured crash: an enemy target actor named
  // "Fluffy_Pillow_1" under HecticAddCleave). `build_obs`'s only
  // pre-260901-od1 caller (solver_control.cpp's in-process transport) is
  // gated to the ONE actor `solver_policy=` names -- it was NEVER
  // exercised against an enemy/pillow-dummy actor before this task wired
  // it into decision_dump's all-actor hook, so whatever assumption inside
  // build_obs breaks for that actor class was latent, not introduced here.
  // Scoped the SAME way every other shaman-specific decision_dump
  // addition already is (phase 165-01's `emit_maelstrom_ext` gate,
  // maelstrom/charges_fractional/duration/refreshable/enemy_debuff_counts)
  // -- `p->resources.is_active( RESOURCE_MAELSTROM )` is this codebase's
  // established "is this actor the shaman this schema targets" predicate,
  // deliberately NOT the player's own primary-resource accessor (shaman_t
  // overrides that to report RESOURCE_MANA). CORRECTED 260902/FORK-05
  // (D-13 amended -- the sentence this replaced was wrong for pets, and a
  // comment that contradicts itself in one sentence sent two prior readers
  // to the wrong actor set): this resource predicate does NOT exclude the
  // wolf pet -- `resources_t::active_resource` defaults to `true` for
  // every `resource_e`, so a friendly `lightning_wolf` pet also reads
  // `is_active(RESOURCE_MAELSTROM) == true` and reaches `build_obs`
  // (measured: the pet actor completed `build_obs` without crashing every
  // time it was reached). What actually excludes pets (and every
  // raid-event add) from the dump today is the OUTER actor-identity gate
  // at the top of `record()` (260902/FORK-03+FORK-05): its `p->is_pet()`
  // conjunct is TRUE not only for the wolf (`PLAYER_PET`) but also for
  // `ENEMY_ADD` and `ENEMY_ADD_BOSS` (`player.hpp:968`), so it strips every
  // raid-event add's rows as well. `demos.rows_from_dump`'s consumer --
  // `project_fight`'s actor_name filter -- never read a pet's or an
  // enemy's own dump rows anyway, so this scoping has zero effect on the
  // schema's actual target and removes the untested actor classes from
  // ever reaching build_obs at all.
  // SECOND crash, found the same session (real gdb backtrace, RelWithDebInfo
  // build): the Maelstrom-active check ALONE did not exclude an enemy actor
  // (`resources_t::active_resource` defaults to `true` for every resource_e
  // and an enemy actor was measured still reading is_active(MAELSTROM)==true)
  // -- that enemy actor then reached build_obs() and segfaulted inside the
  // `movement.remains` expression's lambda (`buffs.movement->remains()`,
  // player.cpp -- `buffs.movement` is constructed ONLY for non-enemy actors,
  // permanently null for every enemy). `!p->is_enemy()` (player.hpp's own
  // `_is_enemy(type)` -- ENEMY/ENEMY_ADD/ENEMY_ADD_BOSS/TANK_DUMMY) is an
  // EXPLICIT, unambiguous actor-class exclusion, added rather than trusting
  // the resource predicate alone to be sufficient for every actor type this
  // hook is called with. See decision_dump.hpp's own "ACTOR SCOPE" doc
  // comment for the full writeup of both crashes.
  if ( p->sim->threads == 1 && p->sim->profileset_map.empty() && !p->is_enemy() &&
       p->resources.is_active( RESOURCE_MAELSTROM ) )
  {
    const rl_policy::slot_table& table = rl_policy::bind_slots( p );
    // 228-11 Task 2 (closing a gap 228-09 disclosed but did not fix, see the targeted_picks
    // comment above): threads THIS function's own is_decision_boundary parameter (always false
    // here -- record() is never the decision boundary, per this file's own comment at its call
    // site) through to read_state(), instead of read_state() unconditionally assuming true. This
    // makes read_state()'s own internal read_action_gate_bits call read the SAME cached,
    // non-re-bumping pick/legality state this function's own adjacent call (above) already
    // does -- the dump's obs field and the translog's engine-built vector can no longer disagree
    // on which pick a target_fact leaf reads.
    const rl_policy::rl_state_t state = rl_policy::read_state( p, boundary_is_foreground, is_decision_boundary );
    std::uint8_t obs_mask[ RL_ACTION_DIM ];
    rl_policy::build_mask( state, obs_mask );
    float obs[ RL_OBS_DIM ];
    rl_policy::build_obs( p, state, table, obs_mask, obs );

    out << ",\"obs\":[";
    for ( std::size_t i = 0; i < RL_OBS_DIM; ++i )
    {
      if ( i > 0 )
        out << ",";
      out << obs[ i ];
    }
    out << "]";

    out << ",\"obs_mask\":[";
    for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
    {
      if ( i > 0 )
        out << ",";
      out << static_cast<int>( obs_mask[ i ] );
    }
    out << "]";
  }
  else
  {
    out << ",\"obs\":null";
    out << ",\"obs_mask\":null";
  }
}

void record( player_t* p, action_t* chosen, execute_type et )
{
  sim_t* sim = p->sim;
  if ( sim->decision_dump_file_str.empty() )
    return;

  // 260902/FORK-03+FORK-05 -- this hook fires from player_t::execute_action()
  // (player.cpp:207 and :7538) for EVERY actor in the sim, unconditionally --
  // monsters, adds, pets, the registered agent, on every boundary. Below this
  // point, write_state_fields() and the action_resolvable/action_ready and
  // obs/obs_mask blocks all call into rl_policy::read_action_gate_bits /
  // build_obs, and build_obs' shared-action-leaf lambda
  // (rl_policy_obs.cpp:1925-1930) sets `a->target_cache.is_valid = false`
  // and rebuilds `a->target_list()` for every multi-target action it
  // touches -- a WRITE into engine state from what is supposed to be a
  // read-only diagnostic. With one enemy the rebuilt list is trivially
  // identical to the stale one (invisible); with several it perturbs the
  // random stream downstream of it (measured: an add-bearing fight's DPS
  // mean moved from 343514.6 to 349746.6 at the same seed with only a
  // decision_dump= line added). Mirror solver_control.cpp:459's own
  // actor-identity gate -- the same exact strcmp against RL_ACTOR_NAME,
  // never a prefix match (a prefix over the actor's name also matches
  // every one of its pet records). 260902/cr2 (WR-06's fix): corrected
  // from a prior "mirror :421 exactly" citation -- :421 is the
  // auto-attack re-arm block, not an actor gate, and the two gates are
  // NOT identical predicates. Two deliberate deltas from :459: (a) this
  // gate is unconditional, while :459's `sim->solver_control_str.empty()
  // &&` conjunct applies only on the in-process transport (the FIFO arm
  // filters which actor's rows it sees on the Python side instead); (b)
  // `p->is_pet()` is added here as an explicit second conjunct
  // (belt-and-braces, D-13 amended) even though the name gate alone
  // already excludes the pet -- `is_pet()` is ALSO true for ENEMY_ADD and
  // ENEMY_ADD_BOSS (player.hpp:968), so this makes "no pet rows, no
  // raid-event-add rows" an explicit, named property of the gate rather
  // than an accident of the actor-name check alone.
  if ( std::strcmp( p->name(), RL_ACTOR_NAME ) != 0 || p->is_pet() )
    return;

  // The stream and its mutex live on the ROOT sim only (see sim.hpp) -- with
  // threads>1 every worker sim_t inherits the same decision_dump_file_str via
  // setup(parent->control), so opening one stream per sim_t would mean N
  // handles each opened (out|trunc) against the same path, silently discarding
  // most of the dump. Walk to the root and share a single writer instead.
  sim_t* root = sim;
  while ( root->parent )
    root = root->parent;

  // Line is built into a local buffer FIRST, so the locked region is one
  // string write: holding the mutex across ~20 individual operator<< calls
  // (several of which walk buff/cooldown/dot lists) would serialize every
  // worker on the full state-collection cost, not just on the write.
  std::ostringstream line;

  line << "{";
  line << "\"t\":" << sim->current_time().total_seconds();
  // Iteration + thread identity: `t` restarts every iteration, so once several
  // workers share one file a consumer cannot otherwise tell which decisions
  // belong to the same combat. Emitted unconditionally to keep the schema
  // stable between threads=1 and threads>1 runs.
  line << ",\"iteration\":" << sim->current_iteration;
  line << ",\"thread\":" << sim->thread_index;
  // Boundary kind (phase 200-04, FORK-01/R-8, R-9) -- additive; absent on
  // every pre-change record, which every downstream reader treats as
  // "foreground". See boundary_name()'s own doc comment above.
  line << ",\"boundary\":\"" << boundary_name( et ) << "\"";
  line << ",\"actor\":\"" << json_escape( p->name() ) << "\"";
  line << ",\"chosen\":" << ( chosen ? ( "\"" + json_escape( chosen->name() ) + "\"" ) : std::string( "null" ) );

  // solver_reply_gated=true only when solver_control is active for this sim
  // (2026-07-29 fix bundle, defect (b)) -- see write_state_fields' own doc
  // comment above and decision_dump.hpp. `chosen` here is already the
  // POST-solver_control-resolution action: player.cpp's execute_action() now
  // calls solver_control::choose() BEFORE decision_dump::record() (reordered
  // for exactly this fix), so `sim->solver_control_last_reply_type` reflects
  // THIS boundary, not the previous one.
  // Widened 210-05R Task 1: gate on EITHER transport being active, not
  // just the FIFO one -- left unwidened, an in-process dump emits a
  // PRE-reply `resolved_action` with no `solver_reply_type`, and every
  // downstream reader (including this phase's own smoke receipt) breaks
  // silently. Nothing else in THIS function (record()) changes.
  //
  // UPDATED 221-01: the claim that `write_state_fields` itself "stays
  // byte-frozen" was a Phase 210 scoping statement about that specific
  // patch, never an invariant of the function -- 221-01 extends it with
  // the `action_resolvable`/`action_ready` arrays (see that function's own
  // doc comment). The extension is additive and every reader (this file's
  // own JSONL consumers, `mask.py`'s `request.get(key)`) tolerates an
  // unknown/absent key, so record()'s own byte-shape claim above is
  // unaffected -- only write_state_fields' output grows.
  // 260902/cr4 (CR-02): record() is NEVER the decision boundary -- player.cpp's execute_action()
  // calls solver_control::choose() (which has already retargeted/turned the player for this
  // decision, via accept_cast) BEFORE calling decision_dump::record(). Recomputing the pick here
  // would read the state the cast already mutated, not the state the decision was made from.
  write_state_fields( line, p, chosen, !sim->solver_control_str.empty() || !sim->solver_policy_str.empty(),
                       et == execute_type::FOREGROUND, /*is_decision_boundary=*/false );

  line << "}\n";

  {
    auto_lock_t lock( root->decision_dump_mutex );

    // Lazy open stays INSIDE the lock: two workers reaching a first decision
    // boundary concurrently would otherwise both see a null unique_ptr and
    // both construct/open, with the loser's stream destructing under the
    // winner's writes.
    if ( !root->decision_dump_stream )
    {
      root->decision_dump_stream = std::make_unique<io::ofstream>();
      root->decision_dump_stream->open( root->decision_dump_file_str );
    }
    if ( !root->decision_dump_stream->is_open() )
      return;

    *root->decision_dump_stream << line.str();
    root->decision_dump_stream->flush();
  }
}
} // namespace decision_dump
