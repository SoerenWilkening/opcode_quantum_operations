/* src/kernels/fpclass.h — M31, K22, the PREDICATES half. PRD-v2 §5's
 * recorded seam ("VIEWS ↔ CLASS PREDICATES … Split at the first predicate").
 *
 * Read docs/constructions/K22.md before changing anything here. Every gate is
 * a port: `lower_eq!` (arith.jl:424-447) through M16's exported block,
 * `lower_not1!` (arith.jl:474-478) and `lower_and!` (arith.jl:268-272) emitted
 * directly one gate per slot, which is PRD-v2 §7.10's disposition for bitwise
 * ops ("one gate per bit and are emitted directly in the fp step function").
 * The four predicates themselves are transcribed from
 *
 *      a_nan       = (ea == UInt64(0x7FF)) & (fa != UInt64(0))   fadd.jl:29
 *      a_inf       = (ea == UInt64(0x7FF)) & (fa == UInt64(0))   fadd.jl:31
 *      a_zero      = (ea == UInt64(0))     & (fa == UInt64(0))   fadd.jl:33
 *      a_subnormal = (ea == UInt64(0))     & (fa != UInt64(0))   flog.jl:262
 *
 * and nothing about their shape is ours: four rows of one table, `==` against
 * `!=` and `0x7FF` against `0`.
 *
 * ONE BLOCK TYPE WITH A CLASS FIELD, NOT FOUR — AND THAT IS K9's OWN ANSWER TO
 * THE SAME QUESTION. cmp.c reaches ten predicates through one shared `cmp()`
 * body because "thirty hand-copied bodies would be thirty independent chances
 * at the one mistake this suite exists to catch" (tests/test_kernel_cmp.c).
 * The four class predicates differ ONLY in two constants and two polarities,
 * so four copies of the step dispatch would be four chances at an off-by-one
 * in the slot arithmetic — which PRD-v2 §7.1 names as the thing a kernel of
 * this shape actually gets wrong. Rule 7's "no predicate parameter" governs
 * `cq_kernel_fn`, the KERNEL contract, and is honoured: the four kernels below
 * are four separate entry points with no class argument between them.
 *
 * THE BLOCK IS A STEP BLOCK BECAUSE A CONSUMER CANNOT CALL THE KERNEL.
 * `cq_kernel_fp_is_nan` is a whole sandwich and `cq_sandwich` refuses nesting
 * in both configurations, so M36 `fcmp` and M33 `fadd` — which need `a_nan`,
 * `a_inf` and `a_zero` inside their own compute halves — reach this
 * construction through `cq_fp_class_step` and nothing else. That is the
 * composite-kernels-call-the-step-function rule M12 is the standing witness
 * for.
 *
 * THE BLOCK ALLOCATES NOTHING. The caller supplies `scr` and `off`, and every
 * span this block touches is `off`-relative — which is load-bearing rather
 * than tidy: a single block cannot see its own offset, so dropping `off` from
 * the span helper slides the whole block inside the caller's region and leaves
 * the value, the palindrome and the pool all correct. Only a SECOND block at a
 * second offset in the same region detects it, and
 * tests/test_kernel_fpfield_block.inc carries that case.
 *
 * SLOTS AND GATES COME APART HERE, AND M31 IS THE FIRST MODULE WHERE THEY DO.
 * A block operand is a view (53 constant lanes for the exponent test, 12 for
 * the fraction test) or a constant span, so the §3 fold table elides gates at
 * EVERY operand mask including the all-quantum one L4 pins. Consequence,
 * stated once and relied on everywhere: `cq_fp_class_steps` is a SLOT count
 * and is never a gate count, and every composition identity over this module
 * is over slots. That is D-K18-6, written for K18 and true here first.
 */
#ifndef CQOPS_KERNELS_FPCLASS_H
#define CQOPS_KERNELS_FPCLASS_H

#include "bit.h"
#include "ctx.h"
#include "kernels/fpfield.h"
#include "scratch.h"

#include <stdint.h>

/* The numbering is load-bearing in one direction only — `CQ_FP_N_CLASS` is the
 * table's length and the guards compare against it — so it is asserted rather
 * than commented, on CQ_BIT_ZERO's and CQ_ANGLE_GENERAL's precedent. */
typedef enum {
    CQ_FP_IS_NAN = 0,
    CQ_FP_IS_INF,
    CQ_FP_IS_ZERO,
    CQ_FP_IS_SUBNORMAL,
    CQ_FP_N_CLASS
} cq_fp_class;

_Static_assert(CQ_FP_IS_NAN == 0 && CQ_FP_N_CLASS == 4,
               "the four rows of fpclass.c's table are indexed by this enum; "
               "adding one is a deliberate act and must break a build");

/* `a` is CQ_FP64_W lanes and is CONTROLS ONLY — it may be a rail, a scratch
 * span, or a view that overlaps a region an earlier step wrote (plan §0.4
 * obligations 2, 3 and 4), sound for `cq_eq_block`'s reason: it reaches the
 * emitter only through `cq_emit_*`'s `const cq_bit *` parameters, so it can
 * never be a target, and guards compare RANGES rather than base pointers. */
typedef struct {
    const cq_bit *a;      /* 64 lanes, controls only                        */
    cq_scratch   *scr;    /* the CALLER's region; this block allocates none */
    uint32_t      off;    /* this block's base offset inside `scr`          */
    cq_fp_class   cls;
} cq_fp_class_block;

/* How many bits of the caller's region this block owns, starting at `off`.
 * 2·S_eq(64) + one bit per `==` occurrence (lower_not1!'s fresh wire) + the
 * flag — never written as a number here or in a test. */
uint32_t cq_fp_class_region(cq_fp_class cls);

/* The SLOT count: cq_eq_steps(64) twice, plus 2 per `==` occurrence, plus the
 * one `and`. ONE INVOLUTION PER SLOT, which is what makes cq_sandwich's index
 * reversal be gate reversal. NOT a gate count — see the header note above. */
int cq_fp_class_steps(cq_fp_class cls);

/* One gate of the block, `u` in [0, cq_fp_class_steps(cls)). Out of range is a
 * hard error in BOTH configurations, for cq_eq_step's reason: a consumer maps
 * a contiguous run of its own indices onto this range and an off-by-one lands
 * inside an `eq` block's OR-prefix, emitting a plausible wrong gate rather
 * than failing. */
void cq_fp_class_step(cq_ctx *ctx, const cq_fp_class_block *k, int u);

/* The predicate's wire, inside the caller's own region. Unlike `cq_eq_flag`
 * this is the value ITSELF and not its negation: the polarity of each `==`
 * occurrence is settled inside the block by upstream's own `lower_not1!`, so a
 * consumer copies this out unchanged. */
const cq_bit *cq_fp_class_flag(const cq_fp_class_block *k);

/* Risk R9's classical row: the SAME four rows evaluated in C over `uint64_t`
 * (PRD-v2 §7.4), never the host's `isnan`/`isinf`/`fpclassify`. Exported
 * because it is what L5 asserts and what a consumer's own short-circuit
 * composes. */
int cq_fp_class_eval(uint64_t a, cq_fp_class cls);

/* --- The four Rule 7 kernels. -------------------------------------------- */

/* `dst[0] ^= is_<class>(a)`, with `a` 64 bits and unchanged and every internal
 * ancilla back at |0>.
 *
 * UNARY, AND `dst` IS ONE BIT. Rule 7 fixes the SEMANTICS and not the
 * parameter list (ckd.15), so these name their operands explicitly as M13's
 * casts do; `dst` being one bit while the source is 64 is K9's shape, so a
 * cq_kd_spec needs `w_dst = 1` and a `call` adapter, and the guard is
 * cq_kernel_check_n(dst, 1, src, w, 1) whose D7b leg is vacuous at arity 1.
 * There is no `_unc` entry point and no `_controlled` variant: uncompute is
 * the same call, and the controlled axis is an emitter mode. */
void cq_kernel_fp_is_nan      (cq_ctx *ctx, cq_bit *dst, const cq_bit *a);
void cq_kernel_fp_is_inf      (cq_ctx *ctx, cq_bit *dst, const cq_bit *a);
void cq_kernel_fp_is_zero     (cq_ctx *ctx, cq_bit *dst, const cq_bit *a);
void cq_kernel_fp_is_subnormal(cq_ctx *ctx, cq_bit *dst, const cq_bit *a);

#endif /* CQOPS_KERNELS_FPCLASS_H */
