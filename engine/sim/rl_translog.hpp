// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================
//
// Flight recorder for the in-process RL transport -- tstl-sylvanas phase
// 212, plan 212-01 (TLOG-01/02/03). One binary file per process carrying a
// fixed-size row for every decision the in-process policy makes, a row for
// every fight's close-out built from that fight's own live damage counter
// (never from report.json -- D-07), and a summary row at the end proving
// the run reached the end (D-15).
//
// NORMATIVE SOURCE: the row and header layouts below are ruling 212-G1 --
// 212-RESEARCH.md M-1's compiled, asserted ordering, verbatim, AS AMENDED
// by quick task 260826-38t's version-2 relayout (see immediately below).
// The reader refuses on an exact version mismatch, so any field addition
// or reordering is a version bump that orphans every file written under
// the prior version. There is no layout generator and none is being built
// (D-05 dropped under N-8, MVP replan
// .planning/notes/v29-mvp-replan-2026-08-25.md) -- both sides of this
// layout are hand-written ONCE and proved by the compile-time assertions at
// the bottom of this file (D-20): a disagreeing layout cannot compile.
//
// Quick task 260826-38t (D-2R, superseding the same task's own prior
// truncating fix): version 1 froze a 48-byte row carrying exactly five
// float32 obs slots. That predates `RL_OBS_DIM` widening 5 -> 9
// (scripts/rl/specs/enhancement.json's observation schema); the interim
// fix decoupled the row's frozen capacity from the live dimension and
// copied only the first five of nine floats into the row, which let the
// build compile but truncated every obs vector recorded to disk --
// training read a 5-wide observation out of a 9-wide network input and
// crashed. That truncation is the bug this version-2 layout exists to
// fix: the row now carries the full, LIVE `RL_OBS_DIM` floats directly
// (no second, independently-frozen width literal -- the whole defect was
// two numbers that could drift out of lockstep), which grows every row
// from 48 to 64 bytes. Version-1 files are orphaned by design (the reader
// refuses on an exact version mismatch, unchanged) -- acceptable here
// because every prior checkpoint is already invalidated by the action/obs
// widening this same quick task's earlier commit made.
//
// Version 3 (tstl-sylvanas trainer-upgrades session, Task 2): adds
// `top_q` -- the best LEGAL Q value at this decision (the net's own
// estimate of how much more discounted damage is still coming from here,
// before the engine picks an action), or a quiet NaN when no action was
// legal. `q_margin` (best minus second-best legal Q) already lived in
// this row and answers "how sure was the net between its top two
// choices"; `top_q` answers a different question entirely -- "what does
// the net think this fight is still worth from here" -- which is what a
// trainer needs to compare the net's own prediction against what actually
// happened afterwards (predicted-vs-realized remaining reward, computed
// Python-side in `scripts/rl/pred_vs_realized.py`, never in this file).
// Adding it grows every row from 64 to 72 bytes; version-2 files are
// orphaned by design (the reader refuses on an exact version mismatch,
// same rule as every prior bump) since there is no `top_q` to backfill
// into an old row.
//
// Version 4 (tstl-sylvanas phase 220, plan 220-03, OBS-06): a pure RELAYOUT
// at an unchanged observation width (`RL_OBS_DIM` is still 9 when this
// version lands) -- no field is added or removed, only reordered and one
// field's TYPE widened, so this bump exists purely to make the layout
// closed-form in the observation width `W` (`RL_OBS_DIM`) ahead of plan
// 220-04's actual widen. Two changes: (1) `mask` moves from a `uint8`
// sitting between `action` and `iteration` (one byte, 5 actions) to a
// `uint32` sitting immediately after `seq` (32 bits, up to 32 actions) --
// widened NOW, before the observation width moves, because Phase 221 will
// need a 24-slot action set and re-widening the mask together with the
// observation width would have forced two relayouts instead of one
// (locked cross-phase amendment, 220-CONTEXT.md); placing it immediately
// after `seq` costs zero bytes of internal padding under natural alignment
// (an ordering that instead kept `iteration` before `mask` would cost 2
// padding bytes at every width). (2) every field offset below `obs` is
// now stated as a function of `W` rather than a per-width hand-typed
// constant, so the NEXT width move (220-04) changes exactly one number
// (`RL_OBS_DIM`) and every downstream offset falls out of the formula
// instead of being re-typed by hand -- the exact defect class (a
// hand-typed offset silently left stale) that the 5->9 truncation bug
// this file's version-2 history describes above came from. At `W = 9`,
// `RECORD_SIZE` is unchanged from version 3 (`roundup8(35 + 4*9) = 72`):
// this bump changes field ORDER and the `mask` field's WIDTH, never the
// row's SIZE. Version-3 files are orphaned by design (the reader refuses
// on an exact version mismatch, same rule as every prior bump) -- their
// one-byte `mask` cannot be losslessly reinterpreted as the new `uint32`
// slot at a different offset.
//
// Version 5 (260901-pb1 Task 3, Lever B: expectation-corrected reward):
// inserts one new `double` field into decision_record (`damage_expected`,
// immediately after `damage`) and into close_record
// (`final_damage_expected_total`, immediately after `final_damage_total`) --
// the crit-expectation-corrected sibling of each realized-damage field,
// D-3/D-4. Both insertions are exactly 8 bytes, at the same relative
// position (right after the first 8-byte field), so every offset from `t`/
// `fight_length` onward in those two structs shifts by a uniform `+8`.
// footer_record gets NO new named field (T3 emits its expected-total
// footer counterpart nowhere in this file -- the reward source's terminal
// reconciliation reads `final_damage_expected_total` off the LAST close
// row directly, not off a run-level footer aggregate) -- its `zero40`
// padding array instead grows by 2 more `uint32` elements (also +8 bytes)
// so its own `iteration`/`zero_action`/`flags`/`thread`/`kind`/`reserved`
// tail lands at the SAME shifted offsets D-02 requires across all three
// row kinds. `RECORD_SIZE(W)` moves from `roundup8(35 + 4*W)` to
// `roundup8(43 + 4*W)` (`1024` -> `1032` at `W = 246`, the width Task 2
// landed in this same commit series). Version-4 files are orphaned by
// design (the reader refuses on an exact version mismatch, same rule as
// every prior bump) -- there is no `damage_expected` to backfill into an
// old row.
//
// A `RECORD_SIZE(W)` row shape carries all three kinds (decision,
// fight-close, footer) and the `kind` byte sits at the same offset
// (`41 + 4*W`, at `W = 246` that is `41 + 984 = 1025`) in every one of them
// (D-02), so a reader can classify a row before it interprets it.
//
// Version 6 (tstl-sylvanas phase 228, plan 228-09, D-23/TGT-08 -- dump-half
// counterpart): inserts one new `std::uint16_t chosen_target_actor_index`
// field into `decision_record`, immediately after `iteration` -- the CHOSEN
// action's own per-decision pick (`rl_target_select::lookup_pick()`, READ
// never recomputed, D-12), as a candidate's `player_t::actor_index`
// (declared `size_t` engine-side, but no realistic fight composition
// approaches 65535 distinct actor_index values, so a 16-bit field is exact,
// never a truncation -- this is why the field is a FULL sixteen bits and not
// a spare byte, which WOULD truncate above 255 actors, exactly the failure
// this version exists to avoid). `CHOSEN_TARGET_SENTINEL_NO_PICK` (0xFFFFu,
// defined below) marks a wait, an untargeted cast, or a targeted cast with
// no stamped pick (an all-illegal decision) -- it cannot collide with a real
// actor_index for the same reason a 16-bit field is exact rather than
// merely sufficient. `close_record`/`footer_record` gain a matching
// zero-written `std::uint16_t` at the SAME relative offset (right after
// their own `iteration` field) purely to keep `kind`/`reserved` landing at
// the SAME offset across all three row shapes (D-02) -- neither row kind
// gets a NAMED counterpart field, mirroring version 5's own
// `final_damage_expected_total`-has-no-footer-counterpart precedent.
// `RECORD_SIZE(W)` moves from `roundup8(43 + 4*W)` to `roundup8(45 + 4*W)`
// (`1032` at both `43` and `45` when `W = 246`, since `43+984=1027` and
// `45+984=1029` both round up to `1032` -- the two extra bytes fit inside
// version 5's own trailing round-up slack, so the ON-DISK row size does not
// change even though the logical field layout does). Version-5 files are
// orphaned by design (the reader refuses on an exact version mismatch, same
// rule as every prior bump) -- there is no `chosen_target_actor_index` to
// backfill into an old row. This is the ONE transition-log layout bump for
// Phase 228 (D-18) -- plan 228-11 moves only `RL_OBS_DIM` (and therefore the
// derived `RECORD_SIZE`) inside THIS layout; it does not bump
// `FORMAT_VERSION` again.
//
// Version 7 (tstl-sylvanas phase 230, plan 230-04, SCOR-02 -- R-B): the learner needs three
// things per decision that no record carried through version 6 -- the facts the scorer looked at
// for every candidate, which of those slots were real, and which one was taken (must_haves R-B).
// Four new fields land on `decision_record`, grouped by size class the SAME way every prior
// field on this struct already is (largest-alignment-first, so the whole struct still closes
// under natural alignment with ZERO internal padding -- see this file's own "Row layouts"
// section comment below):
//   - `candidate_features[RL_TARGET_SLOTS * RL_TARGET_FEATURES]` (float array, grouped with
//     `obs` -- inserted immediately after it, pushing every field from `q_margin` onward down by
//     `4 * RL_TARGET_SLOTS * RL_TARGET_FEATURES` bytes): the per-slot feature block
//     `rl_target_select::preference_scorer` actually scored this decision, CAPTURED at the
//     moment it scored it (never recomputed after the action was chosen -- a recompute would
//     silently disagree with the candidate exploration draw this same plan adds and with any
//     state that moved between the fill and the write). Slot-major: slot 0's
//     `RL_TARGET_FEATURE_NAMES` features first, in that array's own declared order.
//   - `candidate_mask` (uint16, grouped with the other 2-byte fields, immediately after `mask`):
//     one bit per slot, set for every slot this decision actually held a real candidate.
//   - `candidate_count` / `chosen_candidate_slot` (two uint8 fields, grouped with the other
//     1-byte fields, immediately before `kind`): the live candidate count for this decision, and
//     the SLOT (an index into this row's own `candidate_features` block, NOT the chosen enemy's
//     `player_t::actor_index` -- that is `chosen_target_actor_index` above, a different field
//     answering a different question) the action actually took. `CHOSEN_CANDIDATE_SLOT_SENTINEL`
//     (0xFFu, defined below) marks a wait, an untargeted cast, or any decision where the scorer
//     did not score candidates this boundary -- it can never collide with a real slot because
//     `RL_TARGET_SLOTS` (8) is far below 0xFF and the overflow refusal in
//     `rl_target_select.cpp` already keeps the live candidate count at or below it.
// `close_record`/`footer_record` gain matching zero-written fields at the SAME relative offsets
// (mirroring version 5's `final_damage_expected_total`-has-no-footer-counterpart precedent and
// version 6's `chosen_target_actor_index`-has-no-footer-counterpart precedent) so `kind`/
// `reserved` keep landing at the SAME offset across all three row shapes (D-02) -- no row kind
// gets a NAMED close/footer counterpart for any of the four new fields.
// `RECORD_SIZE(W)` moves from `roundup8(45 + 4*W)` to
// `roundup8(45 + 4*W + 4*RL_TARGET_SLOTS*RL_TARGET_FEATURES + 4)` -- the existing round-up rule
// with the four new field sizes added (`4*RL_TARGET_SLOTS*RL_TARGET_FEATURES` for the feature
// array, `2` for `candidate_mask`, `1` each for `candidate_count`/`chosen_candidate_slot`). At
// `RL_OBS_DIM=372`, `RL_TARGET_SLOTS=8`, `RL_TARGET_FEATURES=22`: `roundup8(45 + 1488 + 704 + 4)
// = roundup8(2241) = 2248`. Version-6 files are orphaned by design (the reader refuses on an
// exact version mismatch, same rule as every prior bump) -- there is no candidate block to
// backfill into an old row; `230-ROW-RECEIPT.md` enumerates every transition log in this tree
// written at version 6 that this bump orphans.
//
// Version 9 (tstl-sylvanas quick task 260917-pcn, "GPL rung 3" -- format version 8 is RESERVED
// by the unmerged `260916-dual-head` branch's own incompatible candidate-table relayout, so this
// bump skips straight to 9): a 160-byte per-proc-mechanic counter block, `rl_proc::counters_t`
// (struct-of-arrays: `attempts`/`successes`/`chance_sum`, 8 bytes per registered mechanic across
// 20 registry slots defined in `rl_proc_counters.hpp`), appended to every row kind. The block is
// CUMULATIVE per fight -- `decision_record`'s own copy is the running total at that decision
// boundary, `close_record`'s is the fight's final total -- exactly the same convention `damage`
// already uses, so a reader wanting one window's counts takes a difference between two
// consecutive rows' copies, never a bare read of one row. The intent is to name WHICH proc
// mechanics carry the unpriced in-window credit spread the noise-by-bucket probe (260917-vnp3)
// measured on the exploration-free corpus, something the existing `damage`/`obs` fields cannot
// answer on their own.
//
// The block is placed at the very END of every row -- immediately after `reserved`, at
// `PROC_BLOCK_OFFSET` (defined below; numerically equal to the version-7 `RECORD_SIZE`, i.e. the
// row size before this bump) -- specifically so every version-7 field, `kind` and `reserved`
// included, keeps its version-7 offset unchanged: a reader decoding only the version-7-shaped
// PREFIX of a version-9 row (ignoring the trailing bytes) gets byte-identical results to decoding
// an actual version-7 row. `close_record`/`footer_record` gain the SAME three field names as
// `decision_record` (not zero-written twins under different names, unlike every prior version's
// convention for a decision-only field) because both are legitimate populated values -- a
// fight's own final counts -- except `footer_record`, which owns no fight and writes the block
// zero (`zero_proc_block`, matching every other footer zero-fill).
//
// `RECORD_SIZE(W)` moves from `roundup8(45 + 4*W + 4*RL_TARGET_SLOTS*RL_TARGET_FEATURES + 4)` to
// that same expression plus `8*rl_proc::COUNT` -- at `RL_OBS_DIM=317`, `RL_TARGET_SLOTS=8`,
// `RL_TARGET_FEATURES=23`, `rl_proc::COUNT=20`: `2056 + 160 = 2216`. Version-7/version-8 files
// are orphaned by design (the reader refuses on an exact version mismatch, same rule as every
// prior bump) -- there is no proc-counter history to backfill into an old row, since this stage
// (A of 4) does not yet hook any roll site: every row this binary writes carries an all-zero
// block until stage B wires the roll sites named in the registry. The Python reader keeps
// versions 7 AND 9 both readable (two known-good shapes); this C++ writer only ever writes 9.

#pragma once

#include "sim/rl_policy_constants.h"
#include "sim/rl_proc_counters.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

struct sim_t;
struct player_t;

namespace rl_translog
{

// ---- File identity ----

// The four characters 'R','L','T','L' -- not a NUL-terminated string, so no
// trailing NUL is part of the magic itself.
inline constexpr char MAGIC[ 4 ] = { 'R', 'L', 'T', 'L' };
inline constexpr std::uint32_t ENDIAN_CANARY = 0x01020304u;
inline constexpr std::uint32_t FORMAT_VERSION = 9u;  // 260917-pcn: per-proc counter block
                                                        // appended (see top-of-file version-9
                                                        // comment); 8 is RESERVED by the unmerged
                                                        // dual-head branch
// RECORD_SIZE stays an integer LITERAL, not a computed expression --
// scripts/rl/obs_transport_coupling.selftest.py parses this file's own
// source text for an `ast.Constant`-shaped literal on both sides of the
// language boundary, and a computed expression here would break that
// parse. The static_assert immediately below is what keeps the literal
// honest: RECORD_SIZE(W) = roundup8(45 + 4*W + 4*RL_TARGET_SLOTS*RL_TARGET_FEATURES + 4) +
// 8*rl_proc::COUNT, so a mistyped literal cannot silently drift from the formula and still
// compile (tstl 220-03, OBS-06; formula updated 260901-pb1 Task 3 for version 5, updated again
// 228-09 for version 6, updated again 230-04 for version 7, updated again 260917-pcn for
// version 9 -- see top-of-file comment).
inline constexpr std::uint32_t RECORD_SIZE = 2216u;  // 260915-sti Task 1 (D-317):
                                                       // 2016 -> 2056 -- roundup8(45 + 4*317 +
                                                       // 4*8*23 + 4) = roundup8(2053) = 2056.
                                                       // RL_OBS_DIM moves 306 -> 317 (11 new stat/
                                                       // potion leaves: agility + four secondary-
                                                       // rating leaves needing this task's fork
                                                       // C++, plus crit/cooldowns.potion.remains/
                                                       // four potion buff-remains leaves needing
                                                       // no fork change -- see STAT-INPUTS-MEMO.md
                                                       // (b)). RL_TARGET_FEATURES stays 23
                                                       // (unchanged by this task). RL_TARGET_SLOTS
                                                       // stays 8. Both constants read from
                                                       // rl_policy_constants.h's own regenerated
                                                       // values (Task 2, this same branch), never
                                                       // hand-typed. Confirmed, not assumed, by the
                                                       // static_assert immediately below, which
                                                       // recomputes from the live
                                                       // RL_OBS_DIM/RL_TARGET_SLOTS/
                                                       // RL_TARGET_FEATURES constants at compile
                                                       // time -- NOTE (Task 1 receipt): this task
                                                       // builds against the CURRENT (pre-regen)
                                                       // header, where RL_OBS_DIM is still 306, so
                                                       // this static_assert FAILS here by design
                                                       // until Task 2 regenerates
                                                       // rl_policy_constants.h to RL_OBS_DIM=317.
                                                       // This is a re-pin: one-way,
                                                       // checkpoint-invalidating.
                                                       // 260917-pcn: 2056 -> 2216, the 160-byte
                                                       // proc block.
// 260917-pcn: the version-7 row size (unchanged formula) is where the new proc-counter block
// starts; PROC_BLOCK_SIZE is the block's own byte count (8 bytes per registered mechanic --
// see rl_proc_counters.hpp's counters_t). Declared after RECORD_SIZE's literal so the replaced
// static_assert immediately below can reference them (an assert may only reference constants
// declared above it).
inline constexpr std::uint32_t PROC_BLOCK_OFFSET =
    ( ( 45u + 4u * static_cast<std::uint32_t>( RL_OBS_DIM ) +
        4u * static_cast<std::uint32_t>( RL_TARGET_SLOTS ) * static_cast<std::uint32_t>( RL_TARGET_FEATURES ) +
        4u + 7u ) / 8u ) * 8u;
inline constexpr std::uint32_t PROC_BLOCK_SIZE = 8u * rl_proc::COUNT;
static_assert( RECORD_SIZE == PROC_BLOCK_OFFSET + PROC_BLOCK_SIZE,
               "RECORD_SIZE must be roundup8(45 + 4*RL_OBS_DIM + 4*RL_TARGET_SLOTS*RL_TARGET_FEATURES + 4) + "
               "8*rl_proc::COUNT" );
static_assert( RL_OBS_DIM >= 2, "footer_record's zero40[RL_OBS_DIM-1] needs at least one element" );
inline constexpr std::uint32_t HEADER_SIZE = 256u;

// 228-09 (D-23/TGT-08): the chosen-target sentinel meaning "no pick" --
// a wait decision, an untargeted cast, or a targeted cast whose pick was
// not found (an all-illegal decision, CR-04's deadlock case). 0xFFFFu
// cannot collide with a real `player_t::actor_index` (see this file's own
// top-of-file version-6 comment for why a full 16 bits is exact, never
// merely sufficient, for that field).
inline constexpr std::uint16_t CHOSEN_TARGET_SENTINEL_NO_PICK = 0xFFFFu;

// 230-04 (SCOR-02, R-B): the chosen-CANDIDATE-SLOT sentinel meaning "no candidate block scored
// this decision" -- a wait, an untargeted cast, or a targeted cast the scorer did not score
// (rules path active, or no scorer loaded). An INDEX into this row's own `candidate_features`
// block, never an actor identity (that is `CHOSEN_TARGET_SENTINEL_NO_PICK` above -- a different
// field answering a different question, stated explicitly so a later reader never reads one as a
// copy of the other). 0xFFu cannot collide with a real slot: `RL_TARGET_SLOTS` (8) is far below
// 0xFF, and `rl_target_select.cpp`'s own overflow refusal keeps the live candidate count at or
// below `RL_TARGET_SLOTS` before a slot is ever assigned.
inline constexpr std::uint8_t CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK = 0xFFu;

// Row kind discriminator (offset 62 in every row struct below, D-02). Zero
// is deliberately not a valid kind -- a zero-filled region left by a short
// write (a crashed/truncated file, D-14) must never classify as a valid
// row; it must classify as "not a row at all".
inline constexpr std::uint8_t KIND_DECISION = 1;
inline constexpr std::uint8_t KIND_CLOSE = 2;
inline constexpr std::uint8_t KIND_FOOTER = 3;

// Footer source discriminator (D-07/D-15). The only value this phase ever
// writes -- the footer's per-fight numbers come from the writer's own
// running sum of close-row damage, never from report.json.
inline constexpr std::uint32_t FOOTER_SOURCE_PER_FIGHT_ACCUMULATOR = 1u;

// Decision-row flag bits (std::uint8_t). Bits 0, 1 and 3 are frozen in the
// layout and written ZERO by this phase:
//  - bit 0 (FLAG_TERMINATED) and bit 1 (FLAG_TRUNCATED) distinguish a fight
//    that ended by target death from one that ended by the clock. That is
//    how a trainer defines a fight's end, and that definition belongs to
//    Phase 213, not this plan.
//  - bit 3 (FLAG_EXPLORATORY) marks a random action taken instead of the
//    best one. It has no writer until Phase 213 lands the dice roll in
//    C++ -- Phase 210 currently REFUSES to load a weights blob that asks
//    for any randomness at all.
// Reserving the bit positions now costs nothing; retrofitting one later is
// a version bump.
inline constexpr std::uint8_t FLAG_TERMINATED = 1u << 0;
inline constexpr std::uint8_t FLAG_TRUNCATED = 1u << 1;
inline constexpr std::uint8_t FLAG_WAIT_FLOORED = 1u << 2;
inline constexpr std::uint8_t FLAG_EXPLORATORY = 1u << 3;
inline constexpr std::uint8_t FLAG_COLLECTED = 1u << 4;

// ---- Row layouts (212-RESEARCH M-1, ruling 212-G1, AS RELAID for version 2
// by quick task 260826-38t, AS RELAID AGAIN for version 3 by this session's
// Task 2, AS RELAID A THIRD TIME for version 4 by tstl-sylvanas plan
// 220-03 -- see this file's top-of-file comment for why) ----
//
// The layout closes under NATURAL alignment -- measured by the researcher
// for version 1 (record sizeof=48/alignof=8), re-measured for version 2
// (record sizeof=64/alignof=8), re-measured again for version 3
// (record sizeof=72/alignof=8), and re-measured again for version 4 (still
// sizeof=72/alignof=8 at the still-committed `RL_OBS_DIM == 9` -- version 4
// is a pure reorder, see top-of-file comment; header is unchanged
// throughout, sizeof=256/alignof=8). No struct-packing pragma is used
// anywhere in this file: under byte packing the assertions below stop
// being a test of anything (a misaligned float64 load becomes legal and
// the compiler is no longer proving the offsets the assertions claim), and
// packing would hide exactly the field-ordering mistake those assertions
// exist to catch.
//
// Version 2 grew the row from 48 to 64 bytes solely to carry the full,
// live `RL_OBS_DIM` (9) obs floats instead of version 1's frozen five.
// Version 3 grew it again, 64 to 72 bytes, solely to add `top_q` right
// after `q_margin` in decision_record (see top-of-file comment for what it
// means). Version 4 (this relayout) changes neither the field SET nor the
// row SIZE at `W = 9` -- it moves `mask` to immediately after `seq` and
// widens it from `uint8` to `uint32`, and restates every following offset
// as a function of `W` (`RL_OBS_DIM`) so the next width move (220-04)
// needs to change only `RL_OBS_DIM` itself. Every `// @NN` comment below is
// therefore written as the W-parametrised formula from the closed form,
// with the W=9 value alongside it -- see this file's top-of-file comment
// for the version-4 rationale.

// 230-04 shorthand used only in this file's own @NN offset comments below: S = RL_TARGET_SLOTS
// (8), F = RL_TARGET_FEATURES (22), SF = S*F (176 floats, 704 bytes).
struct decision_record
{
  double damage;              // @0  -- p->solver_damage_so_far at this boundary
  double damage_expected;     // @8  -- version 5 (260901-pb1 Task 3, D-3/D-4): the crit-
                               //         expectation-corrected sibling, p->solver_damage_expected_so_far
                               //         at this boundary -- see top-of-file comment
  float t;                    // @16 -- sim->current_time().total_seconds()
  float obs[ RL_OBS_DIM ];    // @20 -- @20..(19+4W) -- the full, live obs
                               //         vector, copied straight across (version 2: no separate
                               //         frozen-width literal, see top-of-file comment)
  float q_margin;             // @20+4W -- best minus second-best legal Q, or quiet NaN
  float top_q;                 // @24+4W -- version 3: best LEGAL Q (the net's own
                               //         remaining-value estimate at this boundary), or quiet NaN
                               //         if no action was legal -- see top-of-file comment
  float candidate_features[ RL_TARGET_SLOTS * RL_TARGET_FEATURES ];  // @28+4W -- version 7
                               //         (230-04, SCOR-02, R-B): the per-slot feature block
                               //         rl_target_select::preference_scorer actually scored this
                               //         decision, CAPTURED at the moment it scored it (never
                               //         recomputed -- see top-of-file version-7 comment).
                               //         Slot-major, RL_TARGET_FEATURE_NAMES's own order within
                               //         each slot. Zero-filled for any slot candidate_mask does
                               //         not mark real. Placed right after top_q (the point
                               //         close_record's own zero12 array already ends at) so
                               //         close_record only needs a NEW trailing zero-fill, not a
                               //         split of its existing one.
  std::uint32_t seq;          // @28+4W+4SF -- sim->solver_control_seq at this boundary
  std::uint32_t mask;         // @32+4W+4SF -- version 4: one bit per legal action,
                               //         declaration order, now a full uint32 (was a uint8 in
                               //         versions <=3) -- widened to pre-pay Phase 221's 24-slot
                               //         action set (see top-of-file comment)
  std::uint16_t candidate_mask;  // @36+4W+4SF -- version 7 (230-04): one bit per slot, set for
                               //         every slot this decision actually held a real candidate.
  std::uint16_t iteration;    // @38+4W+4SF -- sim->current_iteration
  std::uint16_t chosen_target_actor_index;  // @40+4W+4SF -- version 6 (228-09, D-23/TGT-08): the
                               //         CHOSEN action's own stamped pick (lookup_pick(), never
                               //         recomputed), as a player_t::actor_index, or
                               //         CHOSEN_TARGET_SENTINEL_NO_PICK -- see top-of-file comment
  std::uint8_t action;        // @42+4W+4SF -- chosen action index
  std::uint8_t flags;         // @43+4W+4SF
  std::uint8_t thread;        // @44+4W+4SF -- sim->thread_index
  std::uint8_t candidate_count;  // @45+4W+4SF -- version 7 (230-04): the live candidate count
                               //         this decision (0..RL_TARGET_SLOTS).
  std::uint8_t chosen_candidate_slot;  // @46+4W+4SF -- version 7 (230-04): an INDEX into this
                               //         row's own candidate_features block (never an actor
                               //         identity -- see chosen_target_actor_index above), or
                               //         CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK.
  std::uint8_t kind;          // @47+4W+4SF -- KIND_DECISION
  std::uint8_t reserved;      // @48+4W+4SF -- written zero
  // Version 9 (260917-pcn): the per-fight CUMULATIVE proc-roll observer block, struct-of-arrays,
  // alignas(8) so it starts exactly at PROC_BLOCK_OFFSET (= the version-7 row size) at any width W.
  // A window's counts are consecutive-row differences, exactly like `damage`. Sits AFTER `reserved`
  // so every version-7 field, `kind` and `reserved` included, keeps its version-7 offset (D-02).
  alignas( 8 ) std::uint16_t proc_attempts[ rl_proc::COUNT ];   // @PROC_BLOCK_OFFSET
  std::uint16_t proc_successes[ rl_proc::COUNT ];               // @PROC_BLOCK_OFFSET + 2*COUNT
  float proc_chance_sum[ rl_proc::COUNT ];                      // @PROC_BLOCK_OFFSET + 4*COUNT
};

struct close_record
{
  double final_damage_total;            // @0  -- p->solver_damage_so_far, complete by combat_end
  double final_damage_expected_total;   // @8  -- version 5 (260901-pb1 Task 3, D-3/D-4): the
                                          //         crit-expectation-corrected sibling,
                                          //         p->solver_damage_expected_so_far, complete by
                                          //         combat_end -- see top-of-file comment
  float fight_length;                    // @16 -- p->iteration_fight_length.total_seconds()
  std::uint32_t zero12[ RL_OBS_DIM + 2 ]; // @20..(27+4W) -- written zero
                                          //         (version 4: sized as W+2 to keep decision_count
                                          //         aligned with decision_record's own shifted seq
                                          //         offset, see top-of-file comment)
  float zero_candidate_features[ RL_TARGET_SLOTS * RL_TARGET_FEATURES ];  // @28+4W -- version 7
                                          //         (230-04): written zero, sits immediately after
                                          //         zero12 -- exactly where decision_record's own
                                          //         candidate_features sits (right after top_q) --
                                          //         no close-row counterpart is named, mirroring
                                          //         version 5's own final_damage_expected_total
                                          //         precedent
  std::uint32_t decision_count;          // @28+4W+4SF -- decision rows appended for this fight
  std::uint32_t zero_mask;                // @32+4W+4SF -- version 4: widened to uint32 to sit at
                                          //         decision_record's own mask offset; written zero.
                                          //         Collapses the old zero62/zero63 uint8 PAIR (the mask
                                          //         byte they straddled is now this single uint32)
  std::uint16_t zero_candidate_mask;      // @36+4W+4SF -- version 7 (230-04): written zero, sits
                                          //         at decision_record's own candidate_mask offset
  std::uint16_t iteration;                // @38+4W+4SF
  std::uint16_t zero_chosen_target;       // @40+4W+4SF -- version 6 (228-09): written zero, sits at
                                          //         decision_record's own chosen_target_actor_index
                                          //         offset -- no close-row counterpart is named,
                                          //         mirroring version 5's own
                                          //         final_damage_expected_total precedent
  std::uint8_t zero_action;               // @42+4W+4SF -- written zero, sits at decision_record's
                                          //         own action offset
  std::uint8_t flags;                      // @43+4W+4SF -- FLAG_COLLECTED set per D-09; others zero this phase
  std::uint8_t thread;                      // @44+4W+4SF
  std::uint8_t zero_candidate_count;         // @45+4W+4SF -- version 7 (230-04): written zero,
                                          //         sits at decision_record's own candidate_count offset
  std::uint8_t zero_chosen_candidate_slot;   // @46+4W+4SF -- version 7 (230-04): written zero,
                                          //         sits at decision_record's own chosen_candidate_slot offset
  std::uint8_t kind;                         // @47+4W+4SF -- KIND_CLOSE
  std::uint8_t reserved;                      // @48+4W+4SF -- written zero
  // Version 9 (260917-pcn): the per-fight FINAL proc-roll observer totals -- named the same as
  // decision_record's own fields (not a zero-twin) so a reader gets the fight's closing counts.
  alignas( 8 ) std::uint16_t proc_attempts[ rl_proc::COUNT ];   // @PROC_BLOCK_OFFSET
  std::uint16_t proc_successes[ rl_proc::COUNT ];               // @PROC_BLOCK_OFFSET + 2*COUNT
  float proc_chance_sum[ rl_proc::COUNT ];                      // @PROC_BLOCK_OFFSET + 4*COUNT
};

// 212-CR-FIX BL-01: this footer carries two DIFFERENT populations of fight,
// and the field names below say which is which so a reader never has to
// guess. `engine_run_aggregate` and `mean_collected_fight_length` are the
// engine's OWN run-level numbers (p->collected_data.*.mean()), which only
// ever accumulate COLLECTED fights -- sim.cpp's datacollection_end() guard
// skips the discarded warm-up fight entirely. `fight_count` and
// `summed_close_damage`, by contrast, are the writer's own running totals
// from record_close(), which runs for EVERY fight including the warm-up.
// `collected_fight_count` is the fourth number that makes this checkable:
// it is incremented in record_close() under the exact same predicate that
// sets a close row's FLAG_COLLECTED, so it counts the SAME population
// `engine_run_aggregate`/`mean_collected_fight_length` were computed over.
// A reader wanting `summed_close_damage / fight_count` as a check against
// `engine_run_aggregate` must instead sum only the close rows with
// FLAG_COLLECTED set and divide by `collected_fight_count` -- mixing the
// two populations (as the pre-fix footer silently invited) is guaranteed
// to disagree for any run with iterations > 1, for a reason that has
// nothing to do with a wrong field, a wrong offset or a missed fight.
struct footer_record
{
  double engine_run_aggregate;         // @0  -- p->collected_data.compound_dmg.mean(); COLLECTED fights only
  float mean_collected_fight_length;   // @8  -- p->collected_data.fight_length.mean(); COLLECTED fights only
  std::uint32_t fight_count;           // @12 -- ALL fights, warm-up included
  std::uint32_t row_count;             // @16 -- INCLUDES the footer row itself
  std::uint32_t footer_source;         // @20 -- FOOTER_SOURCE_PER_FIGHT_ACCUMULATOR
  double summed_close_damage;          // @24 -- writer's own running sum of close-row damage; ALL fights, warm-up included
  std::uint32_t collected_fight_count; // @32 -- count of close rows with FLAG_COLLECTED set (excludes warm-up); was the unused `zero32` slot
  std::uint32_t zero36;                 // @36
  std::uint32_t zero40[ RL_OBS_DIM - 1 + RL_TARGET_SLOTS * RL_TARGET_FEATURES ]; // @40..(35+4W+4SF)
                                          //         -- version 7 (230-04): grown by SF more elements
                                          //         (was W-1) purely to absorb decision_record's new
                                          //         candidate_features span -- footer_record gets NO
                                          //         new NAMED field for it (see top-of-file comment);
                                          //         sized W-1+SF to keep the following fields aligned
                                          //         with decision_record's own shifted offsets
  std::uint16_t zero_candidate_mask;     // @36+4W+4SF -- version 7 (230-04): written zero, sits at
                                          //         decision_record's own candidate_mask offset
  std::uint16_t iteration;              // @38+4W+4SF -- written 0xFFFF, a sentinel: no fight owns the footer
  std::uint16_t zero_chosen_target;      // @40+4W+4SF -- version 6 (228-09): written zero, sits at
                                          //         decision_record's own chosen_target_actor_index
                                          //         offset -- see close_record's own comment
  std::uint8_t zero_action;              // @42+4W+4SF -- written zero, collapses the old
                                          //         zero62/zero63 uint8 PAIR (see close_record's
                                          //         own zero_mask comment)
  std::uint8_t flags;                    // @43+4W+4SF -- written zero
  std::uint8_t thread;                   // @44+4W+4SF
  std::uint8_t zero_candidate_count;     // @45+4W+4SF -- version 7 (230-04): written zero, sits at
                                          //         decision_record's own candidate_count offset
  std::uint8_t zero_chosen_candidate_slot; // @46+4W+4SF -- version 7 (230-04): written zero, sits
                                          //         at decision_record's own chosen_candidate_slot offset
  std::uint8_t kind;                     // @47+4W+4SF -- KIND_FOOTER
  std::uint8_t reserved;                 // @48+4W+4SF -- written zero
  // Version 9 (260917-pcn): written zero -- no fight owns the footer.
  alignas( 8 ) std::uint8_t zero_proc_block[ PROC_BLOCK_SIZE ];  // @PROC_BLOCK_OFFSET
};

// The header. The two fields at @20/@24 used to be pure alignment padding
// (`reserved0`/`reserved1`, carrying no data) that kept the three
// fingerprint character arrays eight-aligned starting at offset 32. Phase
// 213 (D-08/213-G17) repurposes them: this is a RENAME plus a TYPE CHANGE,
// never a relayout -- the offsets themselves (20, 24, 28) do not move, so
// every pre-213 log (which wrote both zero) still reads as dial 0.0 /
// round 0, which is the truth about it: the loader refused any other dial
// value until this same phase's commit.
//
// Why 80 and 72, not 64 each: the observation fingerprint is not a bare
// digest -- it is the 74-character prefixed form "rl-obs-v1:<64 hex>"
// (rl_policy_constants.h's RL_OBS_SCHEMA_SHA), and the prefix carries the
// encoder version, so a reader comparing these two strings automatically
// also compares encoder versions. The other two fingerprints are bare
// 64-hex strings. All three are stored as the STRINGS the generated
// constants header already spells, NUL-terminated and zero-filled to the
// end of the array -- that is what makes D-04's "each refusal names which
// config drifted" a direct string comparison on the reader side.
// `alignas(8)`: the struct used to get its natural 8-byte alignment for
// free from the old `std::uint64_t reserved1` member. Splitting that
// member into two `std::uint32_t`s (above) removed the only member that
// forced it, so the requirement is now stated explicitly -- this changes
// nothing about member offsets (all of which stay exactly where the
// comment says), only the compiler-enforced alignment of the type itself.
struct alignas( 8 ) file_header
{
  char magic[ 4 ];                    // @0   -- MAGIC, not NUL-terminated
  std::uint32_t endian_canary;        // @4   -- ENDIAN_CANARY
  std::uint32_t format_version;       // @8   -- FORMAT_VERSION
  std::uint32_t record_size;          // @12  -- RECORD_SIZE
  std::uint32_t header_size;          // @16  -- HEADER_SIZE
  float exploration;                   // @20  -- Phase 213 D-08: the dial value this run was
                                        //         launched with; 0.0 when no policy was loaded (a
                                        //         null solver_policy_weights) -- the truthful value,
                                        //         since the loader refuses anything else at dial 0.0
  std::uint32_t round_index;          // @24  -- Phase 213: the round (generation) index the loaded
                                        //         weights blob declared; 0 when no policy was loaded
  std::uint32_t fight_shape_index;    // @28  -- tstl-sylvanas phase 218, plan 218-02 (RIG-01): the
                                        //         drawn fight shape's 1-based index into the declared
                                        //         fightMix (0 = reserved pre-mix/unknown sentinel,
                                        //         same as every pre-218 log, which wrote this word
                                        //         zero under its old name reserved1)
  char obs_schema_sha[ 80 ];          // @32  -- RL_OBS_SCHEMA_SHA, "rl-obs-v1:<64 hex>"
  char mask_rules_sha[ 72 ];          // @112 -- RL_MASK_RULES_SHA, bare 64 hex
  char action_space_sha[ 72 ];        // @184 -- RL_ACTION_SPACE_SHA, bare 64 hex
};

// ---- Compile-time proof (D-20): the build itself is the proof ----

// 212-CR-FIX WR-01: these four assertions tie RECORD_SIZE/HEADER_SIZE --
// the free-standing literals append_row()'s memcpy actually copies and
// open_and_write_header() stamps into the file -- to the structs' own
// sizeof. Before this fix RECORD_SIZE=48u and HEADER_SIZE=256u were
// separate literals that merely happened to agree with every struct's
// sizeof; nothing tied them together, so a future layout edit that changed
// one struct's size without also updating RECORD_SIZE would compile clean
// while append_row() silently over-read (or truncated) every row of that
// kind. This is the one hole D-20's "a disagreeing layout cannot compile"
// premise did not already close.
static_assert( sizeof( decision_record ) == RECORD_SIZE, "decision_record must be exactly RECORD_SIZE bytes" );
static_assert( alignof( decision_record ) == 8, "decision_record must be 8-aligned" );
static_assert( sizeof( close_record ) == RECORD_SIZE, "close_record must be exactly RECORD_SIZE bytes" );
static_assert( alignof( close_record ) == 8, "close_record must be 8-aligned" );
static_assert( sizeof( footer_record ) == RECORD_SIZE, "footer_record must be exactly RECORD_SIZE bytes" );
static_assert( alignof( footer_record ) == 8, "footer_record must be 8-aligned" );
static_assert( sizeof( file_header ) == HEADER_SIZE, "file_header must be exactly HEADER_SIZE bytes" );
static_assert( alignof( file_header ) == 8, "file_header must be 8-aligned" );

// 230-04: SF = RL_TARGET_SLOTS * RL_TARGET_FEATURES, the byte-count-in-floats the candidate
// block adds -- named once here so every offsetof assertion below states the SAME expression
// (never a hand-typed product) for the "+4SF" term this version's insertion shifts everything
// after `top_q` by.
inline constexpr std::uint32_t TARGET_BLOCK_FLOATS =
    static_cast<std::uint32_t>( RL_TARGET_SLOTS ) * static_cast<std::uint32_t>( RL_TARGET_FEATURES );

// decision_record field offsets -- version 4 restated every offset below `obs` as a function of
// RL_OBS_DIM (W); version 7 (230-04) additionally restates every offset from `candidate_features`
// onward as a function of TARGET_BLOCK_FLOATS (SF) too, so a future feature-list or slot-count
// change is likewise a one-number change.
static_assert( offsetof( decision_record, damage ) == 0 );
static_assert( offsetof( decision_record, damage_expected ) == 8 );
static_assert( offsetof( decision_record, t ) == 16 );
static_assert( offsetof( decision_record, obs ) == 20 );
static_assert( offsetof( decision_record, q_margin ) == 20u + 4u * RL_OBS_DIM );
static_assert( offsetof( decision_record, top_q ) == 24u + 4u * RL_OBS_DIM );
static_assert( offsetof( decision_record, candidate_features ) == 28u + 4u * RL_OBS_DIM );
static_assert( offsetof( decision_record, seq ) == 28u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( decision_record, mask ) == 32u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( decision_record, candidate_mask ) == 36u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( decision_record, iteration ) == 38u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( decision_record, chosen_target_actor_index ) == 40u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( decision_record, action ) == 42u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( decision_record, flags ) == 43u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( decision_record, thread ) == 44u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( decision_record, candidate_count ) == 45u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( decision_record, chosen_candidate_slot ) == 46u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( decision_record, kind ) == 47u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( decision_record, reserved ) == 48u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );

// close_record field offsets -- version 7, same W/SF-parametrised form.
static_assert( offsetof( close_record, final_damage_total ) == 0 );
static_assert( offsetof( close_record, final_damage_expected_total ) == 8 );
static_assert( offsetof( close_record, fight_length ) == 16 );
static_assert( offsetof( close_record, zero12 ) == 20 );
static_assert( offsetof( close_record, zero_candidate_features ) == 28u + 4u * RL_OBS_DIM );
static_assert( offsetof( close_record, decision_count ) == 28u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( close_record, zero_mask ) == 32u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( close_record, zero_candidate_mask ) == 36u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( close_record, iteration ) == 38u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( close_record, zero_chosen_target ) == 40u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( close_record, zero_action ) == 42u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( close_record, flags ) == 43u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( close_record, thread ) == 44u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( close_record, zero_candidate_count ) == 45u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( close_record, zero_chosen_candidate_slot ) == 46u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( close_record, kind ) == 47u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( close_record, reserved ) == 48u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );

// footer_record field offsets -- version 7, same W/SF-parametrised form.
static_assert( offsetof( footer_record, engine_run_aggregate ) == 0 );
static_assert( offsetof( footer_record, mean_collected_fight_length ) == 8 );
static_assert( offsetof( footer_record, fight_count ) == 12 );
static_assert( offsetof( footer_record, row_count ) == 16 );
static_assert( offsetof( footer_record, footer_source ) == 20 );
static_assert( offsetof( footer_record, summed_close_damage ) == 24 );
static_assert( offsetof( footer_record, collected_fight_count ) == 32 );
static_assert( offsetof( footer_record, zero36 ) == 36 );
static_assert( offsetof( footer_record, zero40 ) == 40 );
static_assert( offsetof( footer_record, zero_candidate_mask ) == 36u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( footer_record, iteration ) == 38u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( footer_record, zero_chosen_target ) == 40u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( footer_record, zero_action ) == 42u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( footer_record, flags ) == 43u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( footer_record, thread ) == 44u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( footer_record, zero_candidate_count ) == 45u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( footer_record, zero_chosen_candidate_slot ) == 46u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( footer_record, kind ) == 47u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( footer_record, reserved ) == 48u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );

// file_header field offsets
static_assert( offsetof( file_header, magic ) == 0 );
static_assert( offsetof( file_header, endian_canary ) == 4 );
static_assert( offsetof( file_header, format_version ) == 8 );
static_assert( offsetof( file_header, record_size ) == 12 );
static_assert( offsetof( file_header, header_size ) == 16 );
static_assert( offsetof( file_header, exploration ) == 20 );
static_assert( offsetof( file_header, round_index ) == 24 );
static_assert( offsetof( file_header, fight_shape_index ) == 28 );
static_assert( offsetof( file_header, obs_schema_sha ) == 32 );
static_assert( offsetof( file_header, mask_rules_sha ) == 112 );
static_assert( offsetof( file_header, action_space_sha ) == 184 );

// Cross-kind assertions (D-02's entire premise: a reader can classify a
// row before it interprets it, because `kind` and `reserved` sit at the
// same offset in every row shape).
static_assert( offsetof( decision_record, kind ) == offsetof( close_record, kind ) );
static_assert( offsetof( close_record, kind ) == offsetof( footer_record, kind ) );
static_assert( offsetof( decision_record, kind ) == 47u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
static_assert( offsetof( decision_record, reserved ) == offsetof( close_record, reserved ) );
static_assert( offsetof( close_record, reserved ) == offsetof( footer_record, reserved ) );
static_assert( offsetof( decision_record, reserved ) == 48u + 4u * RL_OBS_DIM + 4u * TARGET_BLOCK_FLOATS );
// 228-09 (version 6): the new chosen-target field also lands at the same offset across all
// three row shapes -- decision_record's own NAMED field, close/footer's zero-written twins.
static_assert( offsetof( decision_record, chosen_target_actor_index ) == offsetof( close_record, zero_chosen_target ) );
static_assert( offsetof( close_record, zero_chosen_target ) == offsetof( footer_record, zero_chosen_target ) );
// 230-04 (version 7): the new candidate-block fields also land at the same offset across all
// three row shapes -- decision_record's own NAMED fields, close/footer's zero-written twins.
static_assert( offsetof( decision_record, candidate_features ) == offsetof( close_record, zero_candidate_features ) );
static_assert( offsetof( decision_record, candidate_mask ) == offsetof( close_record, zero_candidate_mask ) );
static_assert( offsetof( close_record, zero_candidate_mask ) == offsetof( footer_record, zero_candidate_mask ) );
static_assert( offsetof( decision_record, candidate_count ) == offsetof( close_record, zero_candidate_count ) );
static_assert( offsetof( close_record, zero_candidate_count ) == offsetof( footer_record, zero_candidate_count ) );
static_assert( offsetof( decision_record, chosen_candidate_slot ) == offsetof( close_record, zero_chosen_candidate_slot ) );
static_assert( offsetof( close_record, zero_chosen_candidate_slot ) == offsetof( footer_record, zero_chosen_candidate_slot ) );

// 260917-pcn (version 9): the per-proc counter block lands at PROC_BLOCK_OFFSET (the version-7
// row size) in every row shape -- decision_record and close_record's own NAMED fields,
// footer_record's zero-written twin.
static_assert( offsetof( decision_record, proc_attempts ) == PROC_BLOCK_OFFSET );
static_assert( offsetof( decision_record, proc_successes ) == PROC_BLOCK_OFFSET + 2u * rl_proc::COUNT );
static_assert( offsetof( decision_record, proc_chance_sum ) == PROC_BLOCK_OFFSET + 4u * rl_proc::COUNT );
static_assert( offsetof( close_record, proc_attempts ) == PROC_BLOCK_OFFSET );
static_assert( offsetof( close_record, proc_successes ) == PROC_BLOCK_OFFSET + 2u * rl_proc::COUNT );
static_assert( offsetof( close_record, proc_chance_sum ) == PROC_BLOCK_OFFSET + 4u * rl_proc::COUNT );
static_assert( offsetof( footer_record, zero_proc_block ) == PROC_BLOCK_OFFSET );
static_assert( offsetof( decision_record, proc_attempts ) == offsetof( close_record, proc_attempts ) );
static_assert( offsetof( close_record, proc_attempts ) == offsetof( footer_record, zero_proc_block ) );

// Dimension guards.
static_assert( RL_ACTION_DIM <= 32,
               "the legality bitfield is one uint32; an action set wider than 32 must fail the build "
               "(version 4 widened this from <= 8 ahead of Phase 221's 24-slot action set, "
               "tstl-sylvanas plan 220-03)" );
// Quick task 260826-38t (version 2): the row now carries the LIVE
// RL_OBS_DIM directly (decision_record::obs is declared `float[ RL_OBS_DIM
// ]` above) -- there is no second, independently-frozen width literal for
// this guard to compare against; RL_OBS_DIM changing size is caught for
// free by the `sizeof( decision_record ) == RECORD_SIZE` assertion above,
// which fails loudly the moment the two stop agreeing.

// 212-CR-FIX WR-07: the three fingerprint strings are generated
// (rl_policy_constants.h) and NOT under this file's control -- nothing
// stopped them from growing past their fixed-width header fields. Before
// this fix, open_and_write_header()'s std::strncpy truncated silently on
// overflow (guaranteed NUL-terminated by value-initialisation, but
// truncated all the same), which would turn D-04's fingerprint refusal
// into a permanently misleading "config drifted" message naming a config
// that did not actually drift -- just got cut short. `< 80`/`< 72` (not
// `<=`) because strncpy's destination needs room for the trailing NUL.
static_assert( std::string_view( RL_OBS_SCHEMA_SHA ).size() < 80,
               "RL_OBS_SCHEMA_SHA must fit file_header::obs_schema_sha with room for its NUL" );
static_assert( std::string_view( RL_MASK_RULES_SHA ).size() < 72,
               "RL_MASK_RULES_SHA must fit file_header::mask_rules_sha with room for its NUL" );
static_assert( std::string_view( RL_ACTION_SPACE_SHA ).size() < 72,
               "RL_ACTION_SPACE_SHA must fit file_header::action_space_sha with room for its NUL" );

// ---- Writer interface. All five are no-ops when rl_translog= is unset. ----

// Root only -- called once, from sim_t::setup(). Opens the stream and
// writes the 256-byte header.
void open_and_write_header( sim_t* sim );

// Called from inside the in-process arm of solver_control::choose(), once
// per decision boundary that arm answers. Appends only -- no flush; the
// fight's close row flushes the whole fight at once (D-13).
// `exploratory` (Phase 213, D-08): true whenever the random-action branch
// fired, even when the drawn index happened to equal the greedy one -- the
// bit means "a random action fired", not "the action differed".
//
// `obs` is caller-owned and RL_OBS_DIM-sized (the LIVE obs vector) --
// this function's own implementation copies the WHOLE vector into the
// row (version 2, quick task 260826-38t: the row's obs field is itself
// declared `RL_OBS_DIM`-sized now, so there is no narrower prefix to copy
// only part of).
//
// `top_q` (version 3, this session's Task 2): the best LEGAL Q value the
// caller's own masked_argmax pass already computed alongside `q_margin` --
// pass the SAME `best` the margin was derived from, never a re-derived
// value, so the two fields can never silently disagree about what "best"
// meant at this boundary. NaN when no action was legal (mirrors q_margin's
// own NaN-on-insufficient-legal-actions convention).
//
// `chosen_target_actor_index` (version 6, 228-09, D-23/TGT-08): the CHOSEN
// action's own per-decision pick, as a `player_t::actor_index` value --
// caller-resolved via `rl_target_select::lookup_pick()` on the SAME
// `action_index` this call already carries, never recomputed inside this
// function (D-12: this file is a writer, not a decision-maker). The caller
// passes `CHOSEN_TARGET_SENTINEL_NO_PICK` for a wait, an untargeted cast,
// or a targeted cast whose pick was not found this decision.
//
// `candidate_features`/`candidate_mask`/`candidate_count`/`chosen_candidate_slot` (version 7,
// 230-04, SCOR-02, R-B): the per-decision candidate block `rl_target_select` CAPTURED at the
// moment its scorer preference actually scored it (never recomputed here -- D-12's "this file is
// a writer, not a decision-maker" applies to this block exactly as it already does to
// `chosen_target_actor_index`). `candidate_features` is caller-owned and
// `RL_TARGET_SLOTS * RL_TARGET_FEATURES`-sized, slot-major; a nullptr means no candidate block
// was captured this decision (a wait, an untargeted cast, or the rules path was active) -- this
// function then writes an all-zero block, `candidate_mask=0`, `candidate_count=0` and
// `chosen_candidate_slot=CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK` regardless of what the other
// three parameters carry, so a caller does not need to zero them itself.
void record_decision( sim_t* sim, const player_t* p, std::uint64_t seq,
                       const float obs[ RL_OBS_DIM ], const std::uint8_t mask[ RL_ACTION_DIM ],
                       int action_index, float q_margin, float top_q,
                       std::uint16_t chosen_target_actor_index, bool wait_floored,
                       bool exploratory,
                       const float* candidate_features = nullptr, std::uint16_t candidate_mask = 0,
                       std::uint8_t candidate_count = 0,
                       std::uint8_t chosen_candidate_slot = CHOSEN_CANDIDATE_SLOT_SENTINEL_NO_PICK );

// Called from sim_t::combat_end(), after datacollection_end(). Builds and
// appends the close row, then flushes the buffered rows for this fight to
// disk in one write (D-13 -- syscall cost, not crash-loss bounding; a
// crashed file is refused whole by the reader, D-14).
void record_close( sim_t* sim );

// Called from sim_t::execute(), immediately after analyze(), inside the
// existing `if ( success )` guard. Appends the footer row, flushes and
// closes the stream.
void write_footer( sim_t* sim );

// 212-CR-FIX WR-06: called from the in-process arm's cast/wait epilogues
// (solver_control.cpp) when accept_cast()/accept_wait() throws. On that
// path record_close() never runs for the in-flight fight -- the exception
// unwinds past combat_end()'s hook entirely -- so without this call the
// buffered rows, INCLUDING the decision that was made and then refused
// (the one thing the write-before-accept ordering exists to preserve),
// would be lost with the process, and the file that survives has no
// footer, so D-14 makes the reader refuse it whole anyway. Writes whatever
// is currently buffered to disk and clears the buffer; does NOT append a
// close or footer row, because the fight did not actually end. No-op when
// rl_translog= is unset or nothing is buffered.
void flush_pending( sim_t* sim );

} // namespace rl_translog
