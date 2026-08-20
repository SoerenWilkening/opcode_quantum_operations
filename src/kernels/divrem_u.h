/* src/kernels/divrem_u.h — M19, Step 17. K12 unsigned restoring division.
 *
 * Read docs/constructions/K12.md and PRD §15 D9 before changing anything here.
 *
 * M19 TRANSCRIBES NO GATE LIST AT ALL, AND THAT IS THE HEADLINE. Every gate of
 * the loop body comes out of a sibling module's EXPORTED step block — M16's
 * `cq_ult_step`, M14's `cq_sub_step`, M17's `cq_mux_step` — so what this module
 * actually is, is a step-index map plus a scratch layout plus the L5
 * short-circuit. That is plan §0.4 / PRD §15 D9(e), and Rule 1 is the reason:
 * every re-typing of `lower_sub!` or `lower_mux!` is a fresh chance to put a
 * Toffoli before the two CNOTs that build its control, which is the ckd.14(a)
 * ordering hazard and is invisible to L1.
 *
 * FLAT SCRATCH — ONE SANDWICH, FRESH SCRATCH EVERY ITERATION, NOTHING RECLAIMED
 * MID-KERNEL (D9(a)). That is Bennett's own shape: `lower_divrem!` allocates
 * fresh wires per iteration and frees none, relying on one global
 * forward-copy-reverse wrap (aggregate.jl:119-127, :150-172), and libcqops
 * applies the same wrap once at the kernel boundary (Bennett-in-the-small, PRD
 * §5). It is decided against two alternatives that were BUILT AND MEASURED, not
 * dismissed: nesting alone is `W² + 10W` qubits for `64W² + 5W` gates, and a
 * genuinely LINEAR schedule exists at `13W + 3` qubits for `90W² − 3W` gates
 * (K12.md §4.1, filed as the v2 change). What FLAT costs is not softened
 * anywhere: `131,583` scratch qubits for one i128 `udiv`, against LINEAR's
 * `1,667`. `divrem` DOES ship at i128 (opcode_table.yaml:187-190, all four
 * opcodes, the full 15-variant grid) and that is the largest object in the v1
 * catalogue by 8x. Where D2's pool ceiling bites, `cq_qubits_acquire` fails hard
 * naming the pool, which is correct behaviour and must not be softened here.
 *
 * NOTHING IN THIS KERNEL MAY BE FREED MID-COMPUTE-HALF. The remainder's dropped
 * top bit is provably `0` mathematically (K12.md §2.0) but the two-bit shadow
 * cannot prove it, so a mid-kernel `cqrt_free` would be the Rule 6 hard error
 * firing CORRECTLY. The sandwich reverse is the only thing that returns K12's
 * scratch to |0>.
 */
#ifndef CQOPS_KERNELS_DIVREM_U_H
#define CQOPS_KERNELS_DIVREM_U_H

#include "bit.h"
#include "ctx.h"
#include "scratch.h"

/* K12 unsigned — `dst ^= a / b` and `dst ^= a % b`, truncating, at width W.
 *
 * Sandwiched cost at the all-quantum operand mask, which is what
 * tests/goldens/divrem.counts pins (K12.md §3.2, §3.3):
 *
 *     udiv   4W²+4W X   20W²+5W CX   10W²-4W CCX   = 34W²+5W   8W²+4W-1 qubits
 *     urem   4W²+4W X   20W²+3W CX   10W²-4W CCX   = 34W²+3W   8W²+3W-1
 *
 * so 2216 gates over 543 qubits at i8, and 557,696 over 131,583 at i128.
 *
 * D3 — DIVISION BY ZERO NEVER TRAPS, and the values are INHERITED rather than
 * chosen: `udiv(a, 0) = 2^W - 1` and `urem(a, 0) = a`, straight out of
 * divider.jl:15-18 and :44-46 and pinned upstream by test_salb_div_by_zero.jl.
 * The circuit is a fixed permutation and `b = 0` is just another input — there
 * is no branch, no error path and nothing to fail loud about. This is the one
 * place in libcqops where "fail loud" would be wrong: Rule 6's hard error is
 * about DIRTY RAILS, not about poison values. */
void cq_kernel_udiv(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_urem(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W);

/* --- The unsigned core as a block, so M20's signed wrapper composes it. ---
 *
 * The block owns no memory: `scr` is the CALLER's one contiguous region and
 * `off` is where this core's `cq_divrem_region(W, with_q)` bits start inside
 * it. M19's own entry points pass `off = 0`; M20 puts its magnitude registers,
 * its three `condneg` carry chains and its result-sign wire ABOVE the core and
 * passes `off = 0` as well, which is why the accessors below are all relative.
 *
 * `a` and `b` ARE CONTROLS ONLY and may be anything — the caller's operands
 * (M19) or scratch registers an earlier step wrote (M20's `sa`/`sb`). Both
 * bindings are plan §0.4 obligation 4. */
typedef struct {
    const cq_bit *a, *b;   /* dividend and divisor; controls only */
    cq_scratch   *scr;     /* the caller's region — the block allocates nothing */
    uint32_t      off;     /* where this core's sub-region starts */
    int           W;
    int           with_q;  /* 1 keeps the quotient register and P4 (udiv/sdiv) */
} cq_divrem_block;

/* `8W² + 4W - 1` with the quotient, `8W² + 3W - 1` without. Counted in K12.md
 * §2.1a and §4: the remainder tape is `W² + 2W - 1`, the `W` per-iteration
 * inner blocks are `7W + 1` each, and the quotient is `W`. */
int cq_divrem_region(int W, int with_q);

/* `W(17W + 2)` with the quotient, `W(17W + 1)` without — one gate per step, so
 * at the all-quantum mask this is also the compute half's gate count. */
int cq_divrem_steps(int W, int with_q);

/* One gate, `u` in [0, cq_divrem_steps(W, with_q)). Out of range is a hard
 * error in BOTH configurations. */
void cq_divrem_step(cq_ctx *ctx, const cq_divrem_block *k, int u);

/* The two results, as W-bit views into the caller's region. `quotient` is a
 * hard error on a block with `with_q == 0`; `remainder` is `rnext[W-1]`, the
 * last mux output, and is valid either way. Both are writable, because M20's
 * suffix conditionally negates whichever one it is returning. */
cq_bit *cq_divrem_quotient (const cq_divrem_block *k);
cq_bit *cq_divrem_remainder(const cq_divrem_block *k);

/* --- The classical fold's arithmetic, shared with M20. -------------------
 *
 * RISK R9's SHORT-CIRCUIT IS MANDATORY HERE, NOT AN OPTIMISATION, and this
 * kernel has more to lose by it than any other. Pre-materialisation is
 * unconditional once cq_sandwich is entered, so a fully classical `udiv`
 * without the fold would draw `8W² + 4W - 1` qubits — 33,023 at i64 — for an
 * operation with no quantum input at all, and L5's "zero gates and zero qubits
 * fully-classical" would be false (plan §0.2 consequence 2, K12.md §4).
 *
 * A `uint64_t` remainder here would be an I5 violation AND wrong: `divrem`
 * ships at i128, where one would silently truncate. So the fold is bit-serial
 * over a per-bit array, which is the same shape a `cq_bit` array has, and the
 * bound below is the REGISTER cap rather than a divrem-local choice — asserted
 * against CQ_REG_WIDTH_MAX at compile time in divrem_u.c so the two cannot
 * drift apart. */
enum { CQ_DIVREM_MAX_W = 128 };

/* `out[0..W-1]` receives each bit's classical VALUE. Every bit must be
 * constant; a caller checks that first. */
void cq_divrem_read(const cq_bit *v, int W, unsigned char *out);

/* Two's complement negate in place — `~v + 1`, the value form of what M20's
 * `condneg` emits as gates. */
void cq_divrem_negate(unsigned char *v, int W);

/* Restoring division on plain bit arrays: `q` and `r` receive W entries each.
 * `b == 0` yields `q` all ones and `r == a`, which is D3, inherited.
 *
 * IT SHARES THE CIRCUIT'S RECURRENCE, deliberately and unlike M18's fold, and
 * the reason it is safe to is that the DIFFERENTIAL still has an independent
 * third party: L1 compares this against tests/support/refmodel.c, whose own
 * two-word divider is cross-checked against the hardware `/` and `%` at every
 * width up to 64 (tests/test_kernel_divrem_refmodel.inc). A shared mistake
 * between the fold and the circuit would still disagree with the reference. The
 * alternative — a second, different classical division algorithm in `src/` —
 * would be a construction Bennett does not ship (Rule 1). */
void cq_divrem_classical(const unsigned char *a, const unsigned char *b, int W,
                         unsigned char *q, unsigned char *r);

#endif /* CQOPS_KERNELS_DIVREM_U_H */
