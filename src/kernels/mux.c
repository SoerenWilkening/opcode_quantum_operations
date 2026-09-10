/* src/kernels/mux.c — M17, Step 14. K10 mux (select).
 *
 * Read docs/constructions/K10.md before changing anything here. The four gates
 * of `cq_mux_step` are Bennett's `lower_mux!` (arith.jl:522-532) in order; the
 * sandwich around them is Rule 2, and the entry dispatch is ours.
 *
 * I6(a) IS SATISFIED WITH NOTHING LEFT TO CHECK. Every target in the compute
 * half is `r[i]` or `d[i]`, both scratch; `cond`, `t` and `f` reach the emitter
 * only through `const cq_bit *` control parameters, so a source cannot be
 * materialised here by construction. `dst` is touched exclusively in `copyout`,
 * which the driver runs with the extent deliberately disarmed.
 *
 * ONE GATE PER STEP, AND K10 IS THE WITNESS THAT FORCED IT (bd ckd.14a,
 * K10.md §2.0). Taking a step to be the whole 4-gate per-bit block is the
 * natural reading and it is wrong: from the post-state `d = t^f, r = f ^
 * c(t^f)`, re-running the block gives `d = 0` but `r = c(t^f)` — dirty
 * whenever `cond` is set and the arms differ. The correct reverse is g4..g1,
 * and one gate per step is what makes the driver's index reversal BE the gate
 * reversal.
 */

#include "kernels/mux.h"

#include "emit.h"
#include "kernels/kernel.h"
#include "sandwich.h"
#include "scratch.h"

/* THE ORDER IS LOAD-BEARING IN EXACTLY ONE PLACE, and it is worth being precise
 * about which: phase 3's Toffoli reads `d[i]` and therefore must come after
 * BOTH phases 1 and 2, which is what makes it read `t[i] ^ f[i]`. Everything
 * else commutes — phases 1 and 2 are two CX into the same target, and phase 0
 * targets `r[i]`, which nothing before phase 3 reads. So the only reordering
 * that breaks this block is one that moves phase 3 earlier. */
void cq_mux_step(cq_ctx *ctx, const cq_mux_block *b, int u)
{
    int i = u / CQ_MUX_STEPS_PER_BIT;

    switch (u % CQ_MUX_STEPS_PER_BIT) {
    case 0:  cq_emit_cx (ctx, &b->f[i],              &b->r[i]); break;
    case 1:  cq_emit_cx (ctx, &b->t[i],              &b->d[i]); break;
    case 2:  cq_emit_cx (ctx, &b->f[i],              &b->d[i]); break;
    default: cq_emit_ccx(ctx, &b->cond[0], &b->d[i], &b->r[i]); break;
    }
}

typedef struct {
    cq_bit      *dst;
    cq_mux_block b;
} mux_env;

static void compute(cq_ctx *ctx, void *env, int s)
{
    cq_mux_step(ctx, &((const mux_env *)env)->b, s);
}

/* The "^=" of the contract, and the only place `dst` is written on the sandwich
 * path. Bennett RETURNS `r` as the SSA result and lives with `diff` staying
 * dirty; we use `r` as the copy-out source instead and reverse both. */
static void copyout(cq_ctx *ctx, void *env, int i)
{
    const mux_env *e = (const mux_env *)env;

    cq_emit_cx(ctx, &e->b.r[i], &e->dst[i]);
}

/* A CLASSICAL `cond` TAKES THE OTHER PATH, AND THIS IS A DECISION RATHER THAN A
 * PORT (K10.md §5 delta 4, settled 2026-08-16 with the rest of Step 14).
 *
 * It is not merely cheaper, it is REQUIRED. Risk R9: pre-materialisation is
 * unconditional, so without a short-circuit above the sandwich a fully
 * classical select would take 2W qubits from the pool for an operation with no
 * quantum input at all, and L5's "zero gates and zero qubits fully-classical"
 * would be false. The all-classical case is a strict subset of this one, so
 * dispatching on `cond` alone covers R9 and covers more: with a constant
 * condition and live arms the answer is one arm, and copying it costs W CX
 * against the sandwich's 7W CX + 2W CCX.
 *
 * PRECEDENTED ON BOTH SIDES. PRD §2.1 already specifies `cqrt_cswap` with a
 * constant control as ZERO gates — "swap the two `cq_bit` arrays, 0 gates"
 * (PRD-v1.md:265 @ 961905f) — and §0's provenance table names it as a v1
 * obligation, "incl. the 0-gate constant-control case"
 * (PRD-v1.md:56 @ 961905f). Bennett itself dispatches
 * constant vs. variable shifts at arith.jl:185-198. It is a dispatch on operand
 * KIND at kernel entry — not a peephole, not gate-level fusion, and not
 * something the emitter could do for us: the fold table sees each gate alone
 * and cannot know that 6W of them are about to cancel.
 *
 * THE COPY IS STILL PHYSICAL. `cq_emit_cx` from a quantum arm materialises
 * `dst[i]` and emits a real CX — I2 forbids aliasing the arm's qubits into
 * `dst`, which is exactly what makes cqrt_free sound (shift_const.c makes the
 * same argument at more length). A classical arm bit folds: ONE becomes an X,
 * ZERO becomes nothing, which is where L5's zero comes from.
 *
 * `_unc` STILL CANCELS ACROSS A KIND CHANGE. If `cond` is classical at forward
 * time and a rotation materialises it before the uncompute, the second call
 * takes the sandwich path and emits a completely different, larger circuit —
 * and the XOR still cancels, because both realise the same `f(cond, t, f)` on
 * the same values. That asymmetry is Rule 14 and it is deliberate. */
void cq_kernel_mux(cq_ctx *ctx, cq_bit *dst, const cq_bit *cond,
                   const cq_bit *t, const cq_bit *f, int W)
{
    const cq_bit *src[3] = { cond, t, f };
    int w[3] = { 1, W, W };
    cq_scratch scr;
    mux_env e;

    cq_kernel_check_n(dst, W, src, w, 3);

    if (cq_bit_is_const(cond[0])) {
        const cq_bit *arm = cq_bit_value(cond[0]) ? t : f;

        for (int i = 0; i < W; i++) cq_emit_cx(ctx, &arm[i], &dst[i]);
        return;
    }

    /* r ++ d. ONE CONTIGUOUS REGION, because emit.c's I6(a) check is a pointer
     * RANGE test over cq_bit addresses — a kernel carves a region into named
     * sub-arrays, it never allocates two. */
    cq_scratch_alloc(&scr, (uint32_t)(2 * W));

    e.dst    = dst;
    e.b.cond = cond;
    e.b.t    = t;
    e.b.f    = f;
    e.b.r    = cq_scratch_span(&scr, 0u,          (uint32_t)W);
    e.b.d    = cq_scratch_span(&scr, (uint32_t)W, (uint32_t)W);

    cq_sandwich(ctx, &scr, compute, CQ_MUX_STEPS_PER_BIT * W, copyout, W, &e);
    cq_scratch_dispose(&scr);
}
