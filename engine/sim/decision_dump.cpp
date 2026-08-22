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
#include "sim/sim.hpp"
#include "util/concurrency.hpp"
#include "util/io.hpp"

#include <map>
#include <stdexcept>

#include <set>
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
void write_state_fields( std::ostream& out, player_t* p, action_t* chosen, bool solver_reply_gated )
{
  sim_t* sim = p->sim;

  // GCD remaining - same formula as action.cpp's gcd_remains_expr_t, read
  // directly since we have no action anchor when `chosen` is null (idle/wait
  // decision).
  out << ",\"gcd_remains\":" << clamp_nonneg( ( p->gcd_ready - sim->current_time() ).total_seconds() );

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

  // Swing timer (main-hand only - ret paladin has no meaningful OH state).
  if ( p->main_hand_attack && p->main_hand_attack->execute_event )
    out << ",\"swing_mh_remains\":" << p->main_hand_attack->execute_event->remains().total_seconds();
  else
    out << ",\"swing_mh_remains\":null";

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
}

void record( player_t* p, action_t* chosen, execute_type et )
{
  sim_t* sim = p->sim;
  if ( sim->decision_dump_file_str.empty() )
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
  write_state_fields( line, p, chosen, !sim->solver_control_str.empty() );

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
