/* src/kernels/add.c — M14, Step 12. K6 add, K7 sub. THE FIRST SANDWICH USERS.
 *
 * Read docs/constructions/K06.md and K07.md before changing anything here. The
 * carry recurrence is Bennett's (`lower_add!`, adder.jl:8-16) and `lower_sub!`
 * (:162-170) is that same body with `not_b` substituted for `b`, which is why
 * `ripple` below is called by both and why the two share one env.
 *
 * ONE GATE PER STEP, AND IT IS FORCED (K06.md §5 D1, bd ckd.14a). `cq_sandwich`
 * runs the reverse pass by re-calling `compute(env, s)` with the SAME index, so
 * a step undoes itself only if it is an INVOLUTION. Taking a step to be the
 * whole 5-gate loop body is the natural reading and it is wrong: re-running
 * g1..g5 from the post-state leaves c_{i+1} = c_i·(a_i ⊕ b_i ⊕ 1), dirty
 * whenever c_i = 1 and a_i = b_i. The correct reverse is g5,g4,g3,g2,g1, and
 * one gate per step is what makes the driver's index reversal BE the gate
 * reversal. I6(b) does not rescue a multi-gate step — pre-materialisation
 * fixes WHICH gates a step emits and says nothing about their ORDER.
 *
 * I6(a) IS SATISFIED WITH NOTHING LEFT TO CHECK. Every target below is `t[i]`,
 * `c[i]` or `nb[i]`; `a` and `b` reach the emitter only through `const cq_bit *`
 * control parameters, so a source cannot be materialised here by construction.
 * `dst` is touched exclusively in `copyout`, which the driver runs with the
 * extent deliberately disarmed because `dst` is outside scratch by definition.
 *
 * THE ALL-CLASSICAL SHORT-CIRCUIT IS MANDATORY, NOT AN OPTIMISATION (risk R9,
 * plan §0.2 consequence 2). Pre-materialisation is unconditional, so without
 * the check at the top of each entry point a fully classical add would take 2W
 * qubits from the pool for an operation with no quantum input at all, and L5's
 * "zero gates and zero qubits fully-classical" would be false.
 */

#include "kernels/add.h"

#include "emit.h"
#include "kernels/kernel.h"
#include "sandwich.h"
#include "scratch.h"

/* `y` is THE ADDEND THE CARRY CHAIN READS — `b` for K6, `nb` for K7 — which is
 * the whole of the sharing. `b` stays separate because K7's phase 1 needs it as
 * the source of the complement, and K6 never looks at `y` and `b` as two
 * things. `nb` is NULL for K6, where no complement region exists. */
typedef struct {
    cq_bit       *dst;
    const cq_bit *a, *b, *y;
    cq_bit       *nb, *t, *c;
    int           W;
} adder_env;

/* Bennett's carry recurrence, one gate per step, 5W-2 steps.
 *
 *   t[i] ^= a[i];  t[i] ^= y[i]                    -> t[i] = a_i ⊕ y_i
 *   c[i+1] ^= a[i]·y[i];  c[i+1] ^= t[i]·c[i]      -> c[i+1] = MAJ(a_i, y_i, c_i)
 *   t[i] ^= c[i]                                   -> t[i] = the sum bit
 *
 * THE ORDER IS LOAD-BEARING: the MAJ Toffoli at j=3 consumes `t[i]` while it
 * still holds a_i ⊕ y_i, BEFORE j=4 turns it into the sum bit. `c[0]` is never
 * a target — the loop writes c[i+1] for i < W-1 only — and is read at i=0 as
 * the carry-in. Its VALUE is 0 for add and 1 for sub; its KIND is CQ_BIT_Q in
 * both, because the driver pre-materialises the whole region and the fold table
 * dispatches on kind, never on shadow value. That single fact is the whole
 * difference between the pre-I6(b) 11W-8 golden and today's 11W-4. */
static void ripple(cq_ctx *ctx, const adder_env *e, int u)
{
    int W = e->W;

    if (u < 5 * (W - 1)) {
        int i = u / 5;

        switch (u % 5) {
        case 0:  cq_emit_cx (ctx, &e->a[i],             &e->t[i]);     break;
        case 1:  cq_emit_cx (ctx, &e->y[i],             &e->t[i]);     break;
        case 2:  cq_emit_ccx(ctx, &e->a[i], &e->y[i],   &e->c[i + 1]); break;
        case 3:  cq_emit_ccx(ctx, &e->t[i], &e->c[i],   &e->c[i + 1]); break;
        default: cq_emit_cx (ctx, &e->c[i],             &e->t[i]);     break;
        }
        return;
    }

    /* The top bit produces no carry-out — Bennett's `if i < W` guard — so the
     * last stage is three CNOTs and no Toffoli. At W=1 the loop above is empty
     * and this is the entire construction, which is why the closed forms need
     * no W >= 2 dagger. */
    switch (u - 5 * (W - 1)) {
    case 0:  cq_emit_cx(ctx, &e->a[W - 1], &e->t[W - 1]); break;
    case 1:  cq_emit_cx(ctx, &e->y[W - 1], &e->t[W - 1]); break;
    default: cq_emit_cx(ctx, &e->c[W - 1], &e->t[W - 1]); break;
    }
}

static void k6_compute(cq_ctx *ctx, void *env, int s)
{
    ripple(ctx, (const adder_env *)env, s);
}

/* a - b = a + ~b + 1. Phase 1 builds ~b into scratch (2W steps), phase 2 sets
 * the carry-in (1 step), phase 3 is `ripple` unchanged (5W-2 steps).
 *
 * THE COMPLEMENT REGION IS MANDATORY. The tempting shortcuts — inverted
 * controls, or bracketing the adder in X(b[i]) ... X(b[i]) — are both
 * forbidden: Rule 4 admits only X/CX/CCX so there is no negative-control gate
 * to fold into, and targeting `b` would mutate a source, which violates Rule 7
 * and does not even compile against `cq_emit_*`'s const controls (K07.md D8). */
static void k7_compute(cq_ctx *ctx, void *env, int s)
{
    const adder_env *e = (const adder_env *)env;
    int W = e->W;

    if (s < 2 * W) {
        int i = s / 2;

        if (s % 2 == 0) cq_emit_cx(ctx, &e->b[i], &e->nb[i]);   /* copy  */
        else            cq_emit_x (ctx,           &e->nb[i]);   /* flip  */
        return;
    }

    /* The +1 of two's complement. Under I6(b) this is a REAL X on a qubit, not
     * the free constant flip it was before pre-materialisation — and that one
     * bit is the entire 15W-4 -> 15W-2 and 3W-1 -> 3W delta (K07.md §5 D2b). */
    if (s == 2 * W) { cq_emit_x(ctx, &e->c[0]); return; }

    ripple(ctx, e, s - (2 * W + 1));
}

/* The "^=" of Rule 7's contract, and the only place `dst` is written on the
 * sandwich path. W CX either way: `t[k]` is a scratch qubit, and `dst[k]` is a
 * fresh BIT_ZERO on the forward path (materialises at zero gates, since the
 * constant is 0) or already a qubit on the uncompute path. */
static void copyout(cq_ctx *ctx, void *env, int k)
{
    const adder_env *e = (const adder_env *)env;

    cq_emit_cx(ctx, &e->t[k], &e->dst[k]);
}

static int all_const(const cq_bit *v, int W)
{
    for (int i = 0; i < W; i++)
        if (!cq_bit_is_const(v[i])) return 0;
    return 1;
}

/* Risk R9's short-circuit: `dst ^= (a + y + carry_in) mod 2^W` with every input
 * bit a constant. Zero gates and zero qubits when `dst` is classical too, which
 * is L5; a real X per set bit of the sum when `dst` already sits on qubits,
 * which is what makes this a correct implementation of `^=` and not merely a
 * cheap one.
 *
 * BIT-SERIAL, NOT PACKED, AND THAT IS I5 RATHER THAN TASTE. A `uint64_t sum`
 * here would cap the kernel at 64 bits — and add and sub ship at i128
 * (opcode_table.yaml:184) — while the whole reason there is no packed scalar
 * anywhere is that a width-generic loop over `cq_bit` costs nothing extra. */
static void fold_constant(cq_ctx *ctx, cq_bit *dst, const cq_bit *a,
                          const cq_bit *b, int W, int invert_b)
{
    int carry = invert_b;      /* the +1 of two's complement, or 0 for add */

    for (int i = 0; i < W; i++) {
        int x = cq_bit_value(a[i]);
        int y = cq_bit_value(b[i]) ^ invert_b;

        if (x ^ y ^ carry) cq_emit_x(ctx, &dst[i]);
        carry = (x & y) | (carry & (x ^ y));
    }
}

void cq_kernel_add(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W)
{
    cq_scratch scr;
    adder_env e;

    cq_kernel_check_dst(dst, a, b, W);

    if (all_const(a, W) && all_const(b, W)) {
        fold_constant(ctx, dst, a, b, W, 0);
        return;
    }

    /* t ++ c, 2W bits. ONE CONTIGUOUS REGION, because emit.c's I6(a) check is a
     * pointer RANGE test over cq_bit addresses — a kernel carves a region into
     * named sub-arrays, it never allocates two. */
    cq_scratch_alloc(&scr, (uint32_t)(2 * W));

    e.dst = dst;
    e.a   = a;
    e.b   = b;
    e.nb  = NULL;
    e.t   = cq_scratch_span(&scr, 0u,          (uint32_t)W);
    e.c   = cq_scratch_span(&scr, (uint32_t)W, (uint32_t)W);
    e.y   = b;
    e.W   = W;

    cq_sandwich(ctx, &scr, k6_compute, 5 * W - 2, copyout, W, &e);
    cq_scratch_dispose(&scr);
}

void cq_kernel_sub(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W)
{
    cq_scratch scr;
    adder_env e;

    cq_kernel_check_dst(dst, a, b, W);

    if (all_const(a, W) && all_const(b, W)) {
        fold_constant(ctx, dst, a, b, W, 1);
        return;
    }

    cq_scratch_alloc(&scr, (uint32_t)(3 * W));          /* nb ++ t ++ c */

    e.dst = dst;
    e.a   = a;
    e.b   = b;
    e.nb  = cq_scratch_span(&scr, 0u,              (uint32_t)W);
    e.t   = cq_scratch_span(&scr, (uint32_t)W,     (uint32_t)W);
    e.c   = cq_scratch_span(&scr, (uint32_t)(2*W), (uint32_t)W);
    e.y   = e.nb;
    e.W   = W;

    cq_sandwich(ctx, &scr, k7_compute, 7 * W - 1, copyout, W, &e);
    cq_scratch_dispose(&scr);
}
