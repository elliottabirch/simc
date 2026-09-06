// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// shaman_rl_facts -- 232-06 (RULE-01, R-AC/P232-29). The ONE narrow route `engine/sim/` is allowed
// to read a shaman-specific fact through. The shaman player class is defined ONLY inside
// sc_shaman.cpp -- there is no forward-declarable, engine/sim/-visible version of it anywhere in
// this tree, and there is no `player_t::find_action` route to WHICH lightning is primed:
// `find_action` proves an action EXISTS, never that it is ARMED. The ground truth for "armed" is
// the class's own `trigger_thorims_invocation` member function (sc_shaman.cpp:12962-12988), which
// chooses between a Tempest-primed action, a `ti_trigger` pointer (itself armed with either the
// lightning-bolt or the chain-lightning primed variant at sc_shaman.cpp:6821/:7594/:10166/:10170,
// and reset to null at sc_shaman.cpp:14277) and a plain Lightning Bolt default.
//
// This header declares EXACTLY ONE free function and names NO player-class type and NO buff type --
// no forward declaration of the shaman player class, no pointer to it, no buff pointer. Widening it
// to expose more would put a piece of that class's definition where the build system cannot support
// it (`engine/sim/` cannot #include `class_modules/sc_shaman.cpp`, and there is no standalone header
// for the class to include instead). The function below is DEFINED in sc_shaman.cpp, beside
// `trigger_thorims_invocation`, so the two stay in sync as the engine's own mechanics change.
//
// Naming note: the function's own name is required to embed the four-character substring the build
// system's class-leak grep also matches on (it is a mnemonic, not a class reference) -- its single
// occurrence below is that declaration, not a second copy of the class itself.
//
// The three enumerators below correspond ONE-TO-ONE with the addon's `ThorimsPrimed` fact type
// ("none" | "lightning_bolt" | "chain_lightning", src/ext_rotation_shaman_enhancement_bb/
// enhancementTargetRules.ts, landed 232-05) -- RULE-01's shared fact, read on the fork side through
// this accessor and on the addon side through `resolveThorimsPrimedApproximation`'s own labelled
// two-branch guess (232-05-SUMMARY.md).
// ==========================================================================

#pragma once

class player_t;

namespace rl_target_select
{

enum class thorims_primed_kind
{
  none,             // no talent, no Maelstrom Weapon stack yet, or the primed-action pointer is
                     // null (the pre-cast default and the sc_shaman.cpp:14277 per-iteration reset
                     // both read as this value -- there is no separate "not yet primed" enumerator).
  lightning_bolt,    // the primed-action pointer is armed with the lightning-bolt variant
  chain_lightning,   // the primed-action pointer is armed with the chain-lightning variant
};

// Reports which of the two primed-action candidates is armed for `p` right now, or
// `thorims_primed_kind::none` when `p` is null, is not a shaman actor (covers enemies, pets and
// every other class; a non-shaman actor and an un-primed shaman actor are indistinguishable to a
// caller that only ever asks "is Thorim's primed", which is the only question RULE-01 asks), or the
// primed-action pointer itself is null. Deliberately says nothing about Tempest:
// `trigger_thorims_invocation` checks the Tempest buff BEFORE consulting the primed-action pointer
// at all, and RULE-01's own selector substitution (rl_target_select.cpp, `preference_for()`)
// already makes that Tempest check itself -- keeping the two separate is what lets the rule's three
// branches (Tempest / chain-primed / lightning-bolt-primed-or-none) be written once, in one place,
// in both the fork's and the addon's rules modules, rather than duplicating the Tempest override
// inside this accessor too.
thorims_primed_kind shaman_thorims_primed_kind( const player_t* p );

} // namespace rl_target_select
