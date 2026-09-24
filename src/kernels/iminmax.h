/* src/kernels/iminmax.h — M42, K27. The integer intrinsics `abs`, `smin`,
 * `smax`, `umin`, `umax` (PRD-v2 §7.14 — v1-SHAPED integer kernels, not fp).
 *
 * Read docs/constructions/K27.md before changing anything here.
 *
 * THERE IS NO CONSTRUCTION OF OUR OWN IN THIS MODULE. Bennett has no
 * `lower_umax!`: its extractor EXPANDS each intrinsic into ordinary IR
 * (third_party/bennett/src/extract/instructions.jl:4782-4825), and each of
 * those IR instructions is lowered by a function M14/M16/M17 already ported:
 *
 *   llvm.umax  IRICmp :uge (:4786) + IRSelect (:4787)
 *   llvm.umin  IRICmp :ule (:4794) + IRSelect (:4795)
 *   llvm.smax  IRICmp :sge (:4802) + IRSelect (:4803)
 *   llvm.smin  IRICmp :sle (:4810) + IRSelect (:4811)
 *   llvm.abs   IRBinOp :sub(iconst(0), x) (:4821) + IRICmp :sge(x, iconst(0))
 *              (:4822) + IRSelect(cmp, x, neg) (:4823)
 *
 * So each kernel is ONE BLOCK PER IR INSTRUCTION, in the IR's own order, over
 * the exported step blocks of M16 (`cq_ult_step`, `cq_slt_step`), M17
 * (`cq_mux_step`) and M14 (`cq_sub_step`), all inside ONE `cq_sandwich`
 * (Rule 8; composite kernels call step functions, never kernels).
 *
 * THE COMPARE IS LOWERED EXACTLY AS OUR M16 KERNEL LOWERS THAT PREDICATE, read
 * off cmp.c's own table (`lower_icmp!`, arith.jl:409-418): `uge` is `ult(a,b)`'s
 * raw carry-out copied unchanged; `ule` is `ult(b,a)`'s, SWAPPED and
 * unchanged; `sge`/`sle` the same over `slt`. All four predicates this module
 * needs are in the KEEP set (no trailing negation) — only two of them (`ule`,
 * `sle`) are in the SWAP set. Getting that wrong is `max` silently meaning
 * `min`, which no count, palindrome or pool check can see (K09's
 * uge-meaning-ule; only L1 against an independent reference catches it).
 *
 * THE CONSTANT 0 OF `abs` IS A SOURCE, NEVER SCRATCH (PRD-v2 §7.3): a const
 * array of CQ_BIT_ZERO, so every gate it controls folds away at every mask.
 * `abs(INT_MIN) == INT_MIN` (the `sub` wraps) — the total function, which is
 * what the ABI's `_unc` needs (intrinsic_table.yaml's `is_int_min_poison`
 * immarg is dropped at lowering).
 */
#ifndef CQOPS_KERNELS_IMINMAX_H
#define CQOPS_KERNELS_IMINMAX_H

#include "bit.h"
#include "ctx.h"
#include "scratch.h"

#include <stdint.h>

/* The five programs. Numbered and pinned: the step dispatch reads it. */
typedef enum {
    CQ_IMM_UMAX = 0,
    CQ_IMM_UMIN,
    CQ_IMM_SMAX,
    CQ_IMM_SMIN,
    CQ_IMM_ABS,
    CQ_IMM_N_OP
} cq_imm_op;

/* --- The block: the whole compute half, exported for a consumer that wants
 * it inside its OWN sandwich (and for the composition fixtures).
 *
 * `a`, `b` are W bits, CONTROLS ONLY, and may be scratch views; `b` is not
 * read by CQ_IMM_ABS. The block owns `cq_imm_region(op, W)` bits of `scr`
 * starting at `off`, laid out row by row in IR order:
 *
 *   min/max:  [compare: ult 3W+1 | slt 5W+1]  [mux: r (W) ++ d (W)]
 *   abs:      [sub: nb ++ d ++ c, 3W]  [slt: 5W+1]  [mux: r ++ d]
 *
 * THE BLOCK ALLOCATES NOTHING and has no R9 fold — the all-classical
 * short-circuit lives at the KERNEL entry, before any scratch is taken. */
typedef struct {
    const cq_bit *a, *b;
    cq_scratch   *scr;
    uint32_t      off;
    int           op;      /* a cq_imm_op */
    int           W;
} cq_imm_block;

/* The slot count, ASKED of M14/M16/M17 and never written out here:
 *   u*: cq_ult_steps(W) + cq_mux_steps(W)   s*: cq_slt_steps(W) + cq_mux_steps(W)
 *   abs: cq_sub_steps(W) + cq_slt_steps(W) + cq_mux_steps(W)
 * Hard error on an unknown op, W <= 0, or `abs` above CQ_REG_WIDTH_MAX (its
 * constant-zero source is a fixed array of that length). */
int      cq_imm_steps (int op, int W);

/* The bits the block owns — the same sum over the rows' regions. Same guard. */
uint32_t cq_imm_region(int op, int W);

/* One gate of the program, `u` in [0, cq_imm_steps(op, W)). Out of range, or
 * a region that does not fit `scr` at `off`, is a hard error in BOTH
 * configurations. */
void     cq_imm_step  (cq_ctx *ctx, const cq_imm_block *k, int u);

/* The result: the mux row's `r`, W bits, inside the caller's region. */
const cq_bit *cq_imm_out(const cq_imm_block *k);

/* --- The kernels. Rule 7: `dst ^= f(sources)`, sources unchanged, every
 * scratch qubit back at |0>. Uncompute is the same call with `dst = out`.
 * Tie (`a == b`): the compare is true and the mux selects `a` (ops[1]) —
 * irrelevant to the value, recorded in K27.md. */
void cq_kernel_umax(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_umin(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_smax(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_smin(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W);

/* UNARY (a departure Rule 7 sanctions, M13's precedent): dst ^= |a| mod 2^W. */
void cq_kernel_abs (cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W);

#endif /* CQOPS_KERNELS_IMINMAX_H */
