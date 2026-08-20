/* src/kernels/divrem_s.c — M20, Step 17. K12 sdiv/srem.
 *
 * Read docs/constructions/K12.md §2.3 and this module's header before changing
 * anything. `condneg_step` below is the only gate list in K12 that does not
 * come from a sibling module: `_cond_negate_inplace!`, aggregate.jl:174-191,
 * transcribed in order. Everything else is M19's, which is M16's, M14's and
 * M17's.
 *
 * ONE FLAT STEP SPACE — prefix, unsigned core, suffix — over ONE region, so the
 * driver's index reversal unwinds the conditional negates as well as the
 * division. There is no nesting and none is wanted: cq_sandwich refuses it, and
 * an inner region's release would break the outer palindrome.
 *
 * I6(a) IS SATISFIED WITH NOTHING LEFT TO CHECK. Every target is `sa`, `sb`,
 * `ncar`, `rs`, or a bit of M19's sub-region; `a` and `b` reach the emitter only
 * through `const cq_bit *` control parameters, so a source cannot be
 * materialised here by construction. `a[W-1]` and `b[W-1]` are read as CONTROLS
 * and are never negated in place — see the header on why that is the most
 * dangerous line in the extraction.
 */

#include "kernels/divrem_s.h"

#include "emit.h"
#include "kernels/divrem_u.h"
#include "kernels/kernel.h"
#include "sandwich.h"
#include "scratch.h"

/* --- `_cond_negate_inplace!`, aggregate.jl:174-191. ---------------------- */

/* Two's complement negate under a one-bit control: flip every bit, then add the
 * control as a carry. `3W + 1` steps, one gate each — W conditional flips, the
 * carry seed, then W (Toffoli, CNOT) pairs.
 *
 * `ncar` IS W+1 BITS AND `ncar[W]` IS WRITTEN AND NEVER READ. That is Bennett's
 * dead top Toffoli (aggregate.jl:186-188, K12.md §5 delta 7) and it is carried
 * FAITHFULLY: dropping it would save 1 CCX and 1 ancilla per call, 3 per signed
 * op, and would put a gate-level optimisation in a port (Rule 1). If it is ever
 * dropped, sdiv/srem CCX becomes `10W²+2W-6` sandwiched and the qubit rows drop
 * by 3.
 *
 * THE ORDER IS LOAD-BEARING in the same way `lower_sub!`'s is: the Toffoli at
 * `v % 2 == 0` reads `val[c]` BEFORE the CNOT that adds the carry into it, so
 * `next_carry = val[c] & carry` uses the pre-add value. Reordering the pair
 * gives a carry chain that is wrong from bit 1 up, and the reverse half still
 * cancels — so only L1 would see it. */
static void condneg_step(cq_ctx *ctx, const cq_bit *ctrl, cq_bit *val,
                         cq_bit *ncar, int W, int u)
{
    int v, c;

    if (u <  W) { cq_emit_cx(ctx, ctrl, &val[u]);  return; }
    if (u == W) { cq_emit_cx(ctx, ctrl, &ncar[0]); return; }

    v = u - (W + 1);
    c = v / 2;

    if (v % 2 == 0) cq_emit_ccx(ctx, &val[c], &ncar[c], &ncar[c + 1]);
    else            cq_emit_cx (ctx, &ncar[c],          &val[c]);
}

static int condneg_steps(int W) { return 3 * W + 1; }

/* --- The layout. --------------------------------------------------------- */

/* M19's sub-region sits at offset 0 so its own accessors need no adjustment;
 * the wrapper's registers go above it. `sa` and `sb` are the magnitudes, `ncar`
 * is three condneg carry chains of W+1, and `rs` (sdiv only) holds
 * `sign(a) ^ sign(b)`. */
typedef struct {
    cq_bit         *dst;
    const cq_bit   *a, *b;
    cq_scratch     *scr;
    cq_divrem_block core;
    int             W, want_q;
} sdivrem_env;

static int core_bits(const sdivrem_env *e)
{
    return cq_divrem_region(e->W, e->want_q);
}

static cq_bit *sa_of(const sdivrem_env *e)
{
    return cq_scratch_span(e->scr, (uint32_t)core_bits(e), (uint32_t)e->W);
}

static cq_bit *sb_of(const sdivrem_env *e)
{
    return cq_scratch_span(e->scr, (uint32_t)(core_bits(e) + e->W),
                           (uint32_t)e->W);
}

static cq_bit *ncar_of(const sdivrem_env *e, int c)
{
    int W = e->W;

    return cq_scratch_span(e->scr,
                           (uint32_t)(core_bits(e) + 2 * W + c * (W + 1)),
                           (uint32_t)(W + 1));
}

static cq_bit *rs_of(const sdivrem_env *e)
{
    int W = e->W;

    return cq_scratch_span(e->scr,
                           (uint32_t)(core_bits(e) + 2 * W + 3 * (W + 1)), 1u);
}

int cq_sdivrem_region(int W, int want_q)
{
    if (W <= 0) cq_kernel_die("sdivrem: width is not positive");
    return cq_divrem_region(W, want_q) + 2 * W + 3 * (W + 1) + (want_q ? 1 : 0);
}

int cq_sdivrem_steps(int W, int want_q)
{
    if (W <= 0) cq_kernel_die("sdivrem: width is not positive");

    /* 2W copies + two magnitude condnegs + the core + (sdiv: 2 CX for `rs`) +
     * one result condneg. 17W²+13W+5 for sdiv, 17W²+12W+3 for srem. */
    return 2 * W + 2 * condneg_steps(W) + cq_divrem_steps(W, want_q)
             + (want_q ? 2 : 0) + condneg_steps(W);
}

/* --- The step function (K12.md §2.3). ------------------------------------ */

static void compute(cq_ctx *ctx, void *env, int s)
{
    const sdivrem_env *e = (const sdivrem_env *)env;
    int W = e->W, cn = condneg_steps(W), base = 2 * W + 2 * cn;

    if (s < 0 || s >= cq_sdivrem_steps(W, e->want_q))
        cq_kernel_die("sdivrem: step index outside "
                      "[0, cq_sdivrem_steps(W, want_q))");

    /* Prefix — |a| and |b| start as copies. Bennett needs a fresh wire for each
     * sign bit first (aggregate.jl:76-77); we do not, because ours are sources
     * used only as controls. That is the 2-CX port delta, and it is in §3.4. */
    if (s <     W) { cq_emit_cx(ctx, &e->a[s],     &sa_of(e)[s]);     return; }
    if (s < 2 * W) { cq_emit_cx(ctx, &e->b[s - W], &sb_of(e)[s - W]); return; }

    if (s < 2 * W + cn) {
        condneg_step(ctx, &e->a[W - 1], sa_of(e), ncar_of(e, 0), W, s - 2 * W);
        return;
    }
    if (s < base) {
        condneg_step(ctx, &e->b[W - 1], sb_of(e), ncar_of(e, 1), W,
                     s - (2 * W + cn));
        return;
    }

    /* The core, over the MAGNITUDES rather than the operands. */
    if (s < base + cq_divrem_steps(W, e->want_q)) {
        cq_divrem_step(ctx, &e->core, s - base);
        return;
    }
    s -= base + cq_divrem_steps(W, e->want_q);

    /* Suffix. The quotient's sign is `sign(a) ^ sign(b)`, which needs its own
     * wire; the remainder's sign is the dividend's, which does not. */
    if (!e->want_q) {
        condneg_step(ctx, &e->a[W - 1], cq_divrem_remainder(&e->core),
                     ncar_of(e, 2), W, s);
        return;
    }

    if (s == 0) { cq_emit_cx(ctx, &e->a[W - 1], &rs_of(e)[0]); return; }
    if (s == 1) { cq_emit_cx(ctx, &e->b[W - 1], &rs_of(e)[0]); return; }

    condneg_step(ctx, rs_of(e), cq_divrem_quotient(&e->core), ncar_of(e, 2), W,
                 s - 2);
}

static void copyout(cq_ctx *ctx, void *env, int c)
{
    const sdivrem_env *e = (const sdivrem_env *)env;
    const cq_bit *res = e->want_q ? cq_divrem_quotient(&e->core)
                                  : cq_divrem_remainder(&e->core);

    cq_emit_cx(ctx, &res[c], &e->dst[c]);
}

/* --- The classical fold (risk R9). --------------------------------------- */

static int all_const(const cq_bit *v, int W)
{
    for (int i = 0; i < W; i++)
        if (!cq_bit_is_const(v[i])) return 0;
    return 1;
}

/* The same sign-magnitude wrapper the circuit emits, evaluated rather than
 * emitted — so `b == 0` and `typemin / -1` inherit the unsigned contract here
 * exactly as they do on the sandwich path. */
static void fold_constant(cq_ctx *ctx, cq_bit *dst, const cq_bit *a,
                          const cq_bit *b, int W, int want_q)
{
    unsigned char av[CQ_DIVREM_MAX_W], bv[CQ_DIVREM_MAX_W];
    unsigned char q[CQ_DIVREM_MAX_W], r[CQ_DIVREM_MAX_W];
    unsigned char *res;
    int sa, sb;

    cq_divrem_read(a, W, av);
    cq_divrem_read(b, W, bv);
    sa = av[W - 1];
    sb = bv[W - 1];

    if (sa) cq_divrem_negate(av, W);
    if (sb) cq_divrem_negate(bv, W);

    cq_divrem_classical(av, bv, W, q, r);

    res = want_q ? q : r;
    if (want_q ? (sa ^ sb) : sa) cq_divrem_negate(res, W);

    for (int c = 0; c < W; c++) if (res[c]) cq_emit_x(ctx, &dst[c]);
}

/* --- The kernel. --------------------------------------------------------- */

static void sdivrem(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, const cq_bit *b,
                    int W, int want_q)
{
    cq_scratch scr;
    sdivrem_env e;

    cq_kernel_check_dst(dst, a, b, W);

    if (all_const(a, W) && all_const(b, W)) {
        fold_constant(ctx, dst, a, b, W, want_q);
        return;
    }

    cq_scratch_alloc(&scr, (uint32_t)cq_sdivrem_region(W, want_q));

    e.dst    = dst;
    e.a      = a;
    e.b      = b;
    e.scr    = &scr;
    e.W      = W;
    e.want_q = want_q;

    /* The core divides the MAGNITUDES, which are scratch this module writes —
     * plan §0.4 obligation 4, and the reason `cq_divrem_block.a` is a plain
     * `const cq_bit *` rather than "an operand". */
    e.core.a      = sa_of(&e);
    e.core.b      = sb_of(&e);
    e.core.scr    = &scr;
    e.core.off    = 0u;
    e.core.W      = W;
    e.core.with_q = want_q;

    cq_sandwich(ctx, &scr, compute, cq_sdivrem_steps(W, want_q), copyout, W, &e);
    cq_scratch_dispose(&scr);
}

void cq_kernel_sdiv(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W)
{ sdivrem(ctx, dst, a, b, W, 1); }

void cq_kernel_srem(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W)
{ sdivrem(ctx, dst, a, b, W, 0); }
