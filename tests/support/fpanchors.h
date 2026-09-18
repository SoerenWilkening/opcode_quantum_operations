/* tests/support/fpanchors.h — PRD-v2 §7.12's anchor list, as BIT PATTERNS.
 *
 * WHY AN ANCHOR AND NOT A POOL ROW. Since 2026-08-21 L1 is a constant sample
 * budget (32 per kernel per width), so a named value is one the draw MAY reach
 * — and at 32 samples over a 64-bit space it reaches a named 64-bit pattern
 * with probability ~0. For the integer catalogue that is a deliberate trade:
 * the §3 fold table dispatches on a bit's KIND and never on a qubit's VALUE
 * (D6), so at the all-quantum mask the emitted circuit is byte-for-byte
 * identical across every value pair, and values reach the circuit only through
 * classical lanes, one bit per lane.
 *
 * FOR fp THAT ARGUMENT INVERTS, AND IT IS THE WHOLE OF bd hkg. An fp value
 * space has STRUCTURE the integers do not — ±0, ±Inf, quiet and signalling
 * NaNs with payloads, the subnormal/normal boundary, the tie-to-even cases,
 * overflow and underflow — and every one of those is a distinct BRANCH of the
 * ported `ifelse` tree (PRD-v2 §7.2's literal transcription). All of it is
 * reached through the CLASSICAL lanes, which is exactly where values DO matter.
 * So the anchors are FORCED INTO EVERY DRAW and the budget is NOT raised:
 * widen the anchors, do not raise the budget.
 *
 * NO `double` ARITHMETIC IN THIS FILE, AND NONE ANYWHERE UNDER tests/support/
 * FOR fp. PRD-v2 §7.4: the library's classical short-circuit is a C
 * transcription of the Julia body over `uint64_t` precisely because the host
 * `double` operator's answers on the IEEE-unspecified cells are x86's choices
 * (`Inf − Inf` and `0 · Inf` are `fff8…` here; ARM differs on all of them).
 * These are LITERALS, so the table reads the same on every host; a constant
 * computed as `1.0 / 0.0` would not be a table, it would be a host probe.
 *
 * THE CONSTANTS ARE f64 ONLY. v2 is `f64` only (PRD-v2 §1), `cqrt_alloc_f32`
 * aborts, and `fpext`/`fptrunc` stay aborts for want of an `f32` rail (§7.9).
 * cq_fp_anchors_binary therefore declares ZERO anchors at every width but 64,
 * which is what lets one provider ride a whole width ladder unchanged.
 */
#ifndef CQOPS_TEST_FPANCHORS_H
#define CQOPS_TEST_FPANCHORS_H

#include "support/refmodel.h"
#include "support/fphost.h"   /* the NaN cells live THERE (bd 9ve.4); aliased below */

#include <stdint.h>

/* --- The f64 fields, so a reader can check a constant rather than trust it.
 * Asserted against the constants below in tests/test_kerneldrv_anchors.inc:
 * a table of hand-written bit patterns needs an instrument, exactly as PRD §15
 * D11's hand-derived phase table did. */
#define CQ_F64_SIGN_MASK  0x8000000000000000ull
#define CQ_F64_EXP_MASK   0x7ff0000000000000ull
#define CQ_F64_MANT_MASK  0x000fffffffffffffull
#define CQ_F64_QUIET_BIT  CQ_FPHOST_QUIET_BIT   /* mantissa MSB: 1 == quiet */

/* Flips the sign bit. A BIT OPERATION, not a negation — `-x` on a double is a
 * host operation and this file does not do host operations. */
#define CQ_F64_NEG(u)     ((uint64_t)(u) ^ CQ_F64_SIGN_MASK)

/* --- §7.12's named values. ----------------------------------------------- */

#define CQ_F64_POS_ZERO       0x0000000000000000ull
#define CQ_F64_NEG_ZERO       0x8000000000000000ull
#define CQ_F64_POS_INF        0x7ff0000000000000ull
#define CQ_F64_NEG_INF        0xfff0000000000000ull

/* The DEFAULT NaN is x86's `INDEF` and Bennett's (`softfloat_common.jl:14`,
 * Intel SDM Vol 1 §4.8.3.7) — NEGATIVE, quiet, payload zero. ARM's default NaN
 * is positive, which is one of the three cells PRD-v2 §7.4 pins by table. */
#define CQ_F64_DEFAULT_NAN    CQ_FPHOST_INDEF       /* 0xfff8000000000000 */

/* Two quiet NaNs with DISTINGUISHABLE payloads, so "which operand's payload
 * came out" is an observable rather than a coincidence — the measured rule is
 * that a two-NaN operation returns the FIRST operand, and a table whose two
 * NaNs were equal could not see a swap.
 *
 * THE VALUES ARE fphost's, NOT FREELY CHOSEN. main's tests/support/fphost.h
 * (bd 9ve.4) already names CQ_FPHOST_QNAN_A / _B at exactly these patterns and
 * the two files must agree, or an anchor and the oracle that judges it would
 * be talking about different NaNs. */
#define CQ_F64_QNAN_A         CQ_FPHOST_QNAN_A      /* 0x7ff8000000000001 */
#define CQ_F64_QNAN_B         CQ_FPHOST_QNAN_B      /* 0x7ff8000000000002 */

/* SIGNALLING: exponent all ones, quiet bit CLEAR, payload non-zero (a zero
 * payload with the quiet bit clear is an infinity, not a NaN).
 *
 * THE RULE IS "THE FIRST OPERAND WINS, QUIETENED IF SIGNALLING" — upstream's
 * `_sf_propagate_nan2` (softfloat_common.jl:23-24), and re-measured on this box
 * under both compilers by bd 9ve.4. PRD-v2 §7.4's table row said "the qNaN"
 * and was WRONG, for a reason this file has to encode rather than restate:
 * the original measurement paired this sNaN against CQ_F64_QNAN_A, and
 *
 *     quieten(CQ_F64_SNAN) == CQ_F64_SNAN_QUIET == CQ_F64_QNAN_A
 *
 * so on THAT pair the two readings print the same bits. A DEGENERATE PAIR IS
 * NOT A DISCRIMINATOR, and the whole point of an anchor is to discriminate —
 * so the sNaN rows in fpanchors.c pair the sNaN against QNAN_**B**, where
 * "first operand quietened" gives …0001 and "the qNaN" gives …0002. The
 * identity is asserted in tests/test_kerneldrv_anchors.inc so that a future
 * edit which re-introduces the degenerate pair cannot do it quietly. */
#define CQ_F64_SNAN           CQ_FPHOST_SNAN        /* 0x7ff0000000000001 */
#define CQ_F64_SNAN_QUIET     CQ_FPHOST_SNAN_QUIET  /* 0x7ff8000000000001 */

#define CQ_F64_MAX_SUBNORMAL  0x000fffffffffffffull
#define CQ_F64_MIN_SUBNORMAL  0x0000000000000001ull
#define CQ_F64_MIN_NORMAL     0x0010000000000000ull   /* 1 << 52 */
#define CQ_F64_MAX            0x7fefffffffffffffull
#define CQ_F64_ONE            0x3ff0000000000000ull

/* 1 + 2^-52 — the neighbour above 1.0, and the one with an ODD trailing
 * significand bit. It is the second tie-to-even witness's first operand. */
#define CQ_F64_ONE_PLUS_ULP   0x3ff0000000000001ull

/* 2^-53: exactly HALF an ulp of 1.0, so `1 + this` is an exact tie. Biased
 * exponent 1023 − 53 = 970 = 0x3CA, significand zero. */
#define CQ_F64_HALF_ULP_OF_1  0x3ca0000000000000ull

/* --- The generic binary provider. ---------------------------------------- */

/* Writes v[0] and v[1] and NOTHING ELSE, over a tuple the sampler has already
 * drawn (kerneldrv.h's contract), so a three-source kernel composing this one
 * still gets a sampled third operand rather than the pinned zero that is
 * cq_kd_case2's recorded trap.
 *
 *   cq_fp_anchors_binary(W, -1, NULL)  -> the count; 0 unless W == 64
 *   cq_fp_anchors_binary(W,  i, v)     -> fills v[0..1], returns 1
 *
 * ORDERED PAIRS, both operand positions and both orders wherever §7.12 says
 * so: the first-operand payload rule, the sNaN-against-qNaN row and the
 * ±0 combinations are all asymmetric, and a table that gave only one order
 * would be blind to an operand swap — K9's `uge`-meaning-`ule` in a new column.
 *
 * WHAT IT DOES NOT COVER, BY DESIGN. It names no per-kernel special case: the
 * `a_nan` / `a_inf` / `a_zero` / `swap` / `d ≥ 56` predicates of a particular
 * Julia source are that kernel's own provider's job, composing this one
 * (see the composition pattern in tests/test_kerneldrv_anchors.inc). fcmp's
 * "one unordered pair per predicate" is likewise per-kernel — the predicate
 * set lives in the spec, not here — although every row involving a NaN below
 * IS unordered against every predicate. */
int cq_fp_anchors_binary(int W, int i, cq_ref_w *v);

/* How many rows cq_fp_anchors_binary has at f64. Exposed so a composing
 * provider can offset its own indices without calling with i < 0 twice. */
int cq_fp_anchors_binary_count(void);

/* --- The generic UNARY provider (bd 9ve.18, M31's class predicates). ------
 *
 * Writes v[0] and NOTHING ELSE, over a tuple the sampler has already drawn, so
 * a composing provider's other operands stay sampled rather than pinned at 0.
 * Same two-call contract, same "f64 only" refusal.
 *
 * §7.12's SINGLES, not its pairs: ±0, ±Inf, the default NaN, a payload NaN of
 * each flavour, an sNaN, the largest and smallest subnormal, the smallest
 * normal, the largest finite, and 1.0. A unary predicate has no operand order
 * to get wrong, so the ordered-pair argument does not apply — but the SIGN
 * does, and that is why the two negative rows are here rather than left to the
 * draw. M31's exponent view is lanes 52..62 and NOT 52..63; a view that
 * swallowed the sign bit computes `ea == 0x7FF` correctly for every POSITIVE
 * operand and wrongly for every negative one, so −Inf, −0 and a NEGATIVE
 * subnormal are the rows that separate the two readings. A table of positive
 * specials cannot see it, which is the degenerate-pair finding one column
 * over.
 *
 *   cq_fp_anchors_unary(W, -1, NULL)  -> the count; 0 unless W == 64
 *   cq_fp_anchors_unary(W,  i, v)     -> fills v[0], returns 1
 */
int cq_fp_anchors_unary(int W, int i, cq_ref_w *v);

/* How many rows cq_fp_anchors_unary has at f64. */
int cq_fp_anchors_unary_count(void);

#endif /* CQOPS_TEST_FPANCHORS_H */
