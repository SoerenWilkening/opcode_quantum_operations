/* src/kernels/divrem_u.c — M19, Step 17. K12 udiv/urem, PRD §15 D9.
 *
 * Read docs/constructions/K12.md and this module's header before changing
 * anything. There is no gate list here to read against Bennett: the loop body
 * is M16's, M14's and M17's exported step blocks, and what follows is the slot
 * arithmetic (§2.2) and the scratch layout (§2.1a) that bind them.
 *
 * ONE GATE PER STEP, INHERITED THREE TIMES OVER. Each of the three inner blocks
 * is already one gate per index (that is plan §0.4 obligation 1, and ckd.14(a)
 * before it), and this module's own two slots — the shift-in and the quotient
 * bit — are one CX each. So the driver's index reversal IS gate reversal, with
 * nothing left to check per phase.
 *
 * THE SHIFT COSTS NOTHING, AND THAT IS THE LAYOUT'S DOING. `r_in[t]` is a VIEW
 * over `z[t] ++ rnext[t-1]`, so `r << 1` is a relabelling of cq_bit array
 * entries — 0 gates, 0 qubits (Rule 3: a cq_bit is a value, not a wire). Laying
 * `z[t]` immediately BELOW `rnext[t-1]` is what makes that view CONTIGUOUS,
 * which is what lets a reused step function be handed it at all (K12.md §2.1a).
 * The view deliberately ALIASES the previous iteration's mux output; that is
 * legal because every use of it after the shift-in is a CONTROL.
 *
 * THE SHIFT OFFSET IS RECOMPUTED FROM `t`, NEVER CARRIED IN MUTABLE STATE. The
 * driver replays step indices in reverse, so every index a step function uses
 * has to be a pure function of `s`.
 *
 * I6(a) IS SATISFIED WITH NOTHING LEFT TO CHECK. Every target below is a bit of
 * the region — `z[t]`, the inner block, `rnext[t]`, `q[i]` — and `a` and `b`
 * reach the emitter only through `const cq_bit *` control parameters, so a
 * source cannot be materialised here by construction. `dst` is touched
 * exclusively in `copyout`, which the driver runs with the extent disarmed.
 *
 * I6(b) IS WHAT MAKES K12 CORRECT, NOT MERELY STABLE, AND K12 IS THE KERNEL
 * WHERE THAT IS TRUE. `lower_sub!` reads `diff[k]` as a Toffoli CONTROL one gate
 * before targeting it again (adder.jl:151-154); with a classical `b` both of the
 * CNOTs that build `diff[k]` fold, the Toffoli folds too, and the very next CX
 * materialises it — so the reverse pass emits a Toffoli the forward never did.
 * The halves stop mirroring and L1 stays green. Smallest witness: W = 3, `a` all
 * quantum, `b` all CQ_BIT_ZERO (K12.md §2.4b). Pre-materialisation is what
 * closes it; do not exempt this kernel from it.
 */

#include "kernels/divrem_u.h"

#include "emit.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/kernel.h"
#include "kernels/mux.h"
#include "reg.h"
#include "sandwich.h"

/* The fold's per-bit arrays are bounded by the REGISTER cap, not by a
 * divrem-local choice — a kernel is handed cq_bit arrays that came from a
 * register, and reg.c refuses a wider one. Asserted rather than commented so
 * the two cannot drift. */
_Static_assert(CQ_DIVREM_MAX_W == (int)CQ_REG_WIDTH_MAX,
               "the divrem fold's bound must be the register width cap");

/* --- The layout (K12.md §2.1a). ------------------------------------------ */

/* The remainder tape, in bits: z (W) + rnext (W²) + r0_hi (W-1). The
 * contiguity that makes `r_in[t]` one run is free, not paid for. */
static int tape_bits(int W) { return W * W + 2 * W - 1; }

/* Where tape block `b` starts. Block 0 is `z[0] ++ r0_hi` (W slots); block b in
 * [1,W) is `z[b] ++ rnext[b-1]` (W+1 slots); block W is `rnext[W-1]` alone
 * (W slots). At W = 1 this degenerates cleanly: r0_hi is empty, block 0 is
 * `z[0]` and block 1 is `rnext[0]`, so the tape is 2 bits and no special case
 * is needed anywhere. */
static int tape_off(int W, int b) { return b == 0 ? 0 : W + (b - 1) * (W + 1); }

static cq_bit *span(const cq_divrem_block *k, int off, int len)
{
    return cq_scratch_span(k->scr, k->off + (uint32_t)off, (uint32_t)len);
}

/* The shifted remainder entering iteration `t` — the first W slots of block t,
 * i.e. `z[t] ++ rnext[t-1][0..W-2]`. `rnext[t-1][W-1]`, the dropped top bit, is
 * the LAST slot of block t and nothing reads it again; it is provably zero
 * (K12.md §2.0) and the sandwich reverse cleans it. */
static cq_bit *r_in(const cq_divrem_block *k, int t)
{
    return span(k, tape_off(k->W, t), k->W);
}

static cq_bit *r_next(const cq_divrem_block *k, int t)
{
    int W = k->W;

    return span(k, tape_off(W, t + 1) + (t + 1 < W ? 1 : 0), W);
}

/* Iteration t's private 7W+1 bits: nb, ucar (W+1), axnb, nsb, diff, scar,
 * mdiff. `ucar[W]` IS `fits[t]` — the comparator's carry-out, read directly,
 * with no `ult` wire and no `not1` (D9(b), and it is what M16 itself ships). */
static cq_bit *inner(const cq_divrem_block *k, int t, int off, int len)
{
    int W = k->W;

    return span(k, tape_bits(W) + t * (7 * W + 1) + off, len);
}

static cq_bit *fits_of(const cq_divrem_block *k, int t)
{
    return inner(k, t, k->W, k->W + 1) + k->W;
}

cq_bit *cq_divrem_quotient(const cq_divrem_block *k)
{
    if (!k->with_q) cq_kernel_die("divrem: this block has no quotient register");
    return span(k, tape_bits(k->W) + k->W * (7 * k->W + 1), k->W);
}

cq_bit *cq_divrem_remainder(const cq_divrem_block *k)
{
    return r_next(k, k->W - 1);
}

int cq_divrem_region(int W, int with_q)
{
    if (W <= 0) cq_kernel_die("divrem: width is not positive");
    return tape_bits(W) + W * (7 * W + 1) + (with_q ? W : 0);
}

/* Compute-half slots per division iteration: 1 shift-in + C_ult + C_sub +
 * C_mux + 1 quotient bit. ASKED OF THE THREE MODULES rather than written down,
 * which is what makes the L4 golden a consequence of M14/M16/M17 (K12.md §3.0
 * — the one assertion that survives CQOPS_UPDATE_GOLDENS=1). */
static int per_iter(int W, int with_q)
{
    return 1 + cq_ult_steps(W) + cq_sub_steps(W) + CQ_MUX_STEPS_PER_BIT * W
             + (with_q ? 1 : 0);
}

int cq_divrem_steps(int W, int with_q)
{
    if (W <= 0) cq_kernel_die("divrem: width is not positive");
    return W * per_iter(W, with_q);
}

/* --- The step function (K12.md §2.2). ------------------------------------ */

void cq_divrem_step(cq_ctx *ctx, const cq_divrem_block *k, int u)
{
    int W = k->W, K = per_iter(W, k->with_q);
    int t, j, i;
    cq_bit *r;

    if (u < 0 || u >= cq_divrem_steps(W, k->with_q))
        cq_kernel_die("divrem: step index outside "
                      "[0, cq_divrem_steps(W, with_q))");

    t = u / K;
    j = u % K;
    i = W - 1 - t;
    r = r_in(k, t);

    /* P0 — `r = (r << 1) | ((a >> i) & 1)`. The shift is the layout; only this
     * one CX survives of Bennett's shl + lshr + and (K12.md §5 delta 3). */
    if (j == 0) { cq_emit_cx(ctx, &k->a[i], &r[0]); return; }

    /* P1 — fits[t] = (r >=u b), M16's `lower_ult!` compute half. */
    if (j <= 6 * W + 1) {
        cq_ult_block c;

        c.a     = r;
        c.b     = k->b;
        c.nb    = inner(k, t, 0,         W);
        c.carry = inner(k, t, W,         W + 1);
        c.axnb  = inner(k, t, 2 * W + 1, W);
        c.W     = W;
        cq_ult_step(ctx, &c, j - 1);
        return;
    }

    /* P2 — diff[t] = r - b, M14's `lower_sub!` compute half. */
    if (j <= 13 * W) {
        cq_sub_block s;

        s.a  = r;
        s.b  = k->b;
        s.nb = inner(k, t, 3 * W + 1, W);
        s.d  = inner(k, t, 4 * W + 1, W);
        s.c  = inner(k, t, 5 * W + 1, W);
        s.W  = W;
        cq_sub_step(ctx, &s, j - (6 * W + 2));
        return;
    }

    /* P3 — rnext[t] = fits ? diff : r, M17's `lower_mux!`. `f` is the remainder
     * VIEW, which aliases rnext[t-1]; `r` is the next block. Disjoint at every
     * t, including 0 and W-1 — the view spans [off(t), off(t)+W) and the output
     * starts at off(t+1)+1 >= off(t)+W+1. */
    if (j <= 17 * W) {
        cq_mux_block m;

        m.cond = fits_of(k, t);
        m.t    = inner(k, t, 4 * W + 1, W);      /* diff[t]  */
        m.f    = r;
        m.r    = r_next(k, t);
        m.d    = inner(k, t, 6 * W + 1, W);      /* mdiff[t] */
        cq_mux_step(ctx, &m, j - (13 * W + 1));
        return;
    }

    /* P4 — the quotient bit, ONE CX and not Bennett's `or` + `mux` (D9(c),
     * K12.md §5 delta 4). `q[i]` is provably CQ_BIT_ZERO here and is written by
     * iteration t = W-1-i and no other, so `q[i] ^= fits` IS `q[i] := fits`.
     * The alternative costs `C_or + C_mux = 5W` CX and `2W` CCX PER ITERATION —
     * 7W² extra gates — for the identical permutation. */
    cq_emit_cx(ctx, fits_of(k, t), &cq_divrem_quotient(k)[i]);
}

/* --- The classical arithmetic. ------------------------------------------- */

void cq_divrem_read(const cq_bit *v, int W, unsigned char *out)
{
    if (W <= 0 || W > CQ_DIVREM_MAX_W)
        cq_kernel_die("divrem: width outside [1, CQ_DIVREM_MAX_W]");

    for (int i = 0; i < W; i++) out[i] = (unsigned char)cq_bit_value(v[i]);
}

void cq_divrem_negate(unsigned char *v, int W)
{
    int carry = 1;

    for (int i = 0; i < W; i++) {
        int s = (v[i] ^ 1) + carry;

        v[i]  = (unsigned char)(s & 1);
        carry = s >> 1;
    }
}

void cq_divrem_classical(const unsigned char *a, const unsigned char *b, int W,
                         unsigned char *q, unsigned char *r)
{
    unsigned char d[CQ_DIVREM_MAX_W];

    if (W <= 0 || W > CQ_DIVREM_MAX_W)
        cq_kernel_die("divrem: width outside [1, CQ_DIVREM_MAX_W]");

    for (int c = 0; c < W; c++) { q[c] = 0u; r[c] = 0u; }

    for (int t = 0; t < W; t++) {
        int i = W - 1 - t, borrow = 0;

        /* r = (r << 1) | a[i], top-down so no entry is overwritten early. */
        for (int c = W - 1; c > 0; c--) r[c] = r[c - 1];
        r[0] = a[i];

        /* d = r - b, and `fits` is the ABSENCE of a final borrow — the value
         * form of reading the comparator's carry-out (D9(b)). Written as a
         * borrow recurrence rather than as `x - y` so nothing is ever a
         * negative int being masked. */
        for (int c = 0; c < W; c++) {
            int x = r[c], y = b[c];

            d[c]   = (unsigned char)(x ^ y ^ borrow);
            borrow = x < y + borrow;
        }

        if (!borrow) {
            for (int c = 0; c < W; c++) r[c] = d[c];
            q[i] = 1u;
        }
    }
}

static int all_const(const cq_bit *v, int W)
{
    for (int i = 0; i < W; i++)
        if (!cq_bit_is_const(v[i])) return 0;
    return 1;
}

static void fold_constant(cq_ctx *ctx, cq_bit *dst, const cq_bit *a,
                          const cq_bit *b, int W, int want_q)
{
    unsigned char av[CQ_DIVREM_MAX_W], bv[CQ_DIVREM_MAX_W];
    unsigned char q[CQ_DIVREM_MAX_W], r[CQ_DIVREM_MAX_W];
    const unsigned char *res;

    cq_divrem_read(a, W, av);
    cq_divrem_read(b, W, bv);
    cq_divrem_classical(av, bv, W, q, r);

    /* A real X per set bit when `dst` already sits on qubits, and a free
     * constant flip when it does not — which is what makes this a correct
     * implementation of `dst ^=` rather than merely a cheap one. */
    res = want_q ? q : r;
    for (int c = 0; c < W; c++) if (res[c]) cq_emit_x(ctx, &dst[c]);
}

/* --- The kernel. --------------------------------------------------------- */

typedef struct {
    cq_bit         *dst;
    cq_divrem_block k;
} divrem_env;

static void compute(cq_ctx *ctx, void *env, int s)
{
    cq_divrem_step(ctx, &((const divrem_env *)env)->k, s);
}

/* The "^=" of Rule 7's contract, and the only place `dst` is written on the
 * sandwich path — which is why the driver runs it with the I6 extent DISARMED.
 * `urem` reads the last mux output; `udiv` reads the quotient register. */
static void copyout(cq_ctx *ctx, void *env, int c)
{
    const divrem_env *e = (const divrem_env *)env;
    const cq_bit *res = e->k.with_q ? cq_divrem_quotient(&e->k)
                                    : cq_divrem_remainder(&e->k);

    cq_emit_cx(ctx, &res[c], &e->dst[c]);
}

static void divrem(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, const cq_bit *b,
                   int W, int want_q)
{
    cq_scratch scr;
    divrem_env e;

    cq_kernel_check_dst(dst, a, b, W);

    if (all_const(a, W) && all_const(b, W)) {
        fold_constant(ctx, dst, a, b, W, want_q);
        return;
    }

    /* ONE CONTIGUOUS REGION, carved into named sub-arrays, because emit.c's
     * I6(a) check is a pointer RANGE test over cq_bit addresses — a kernel that
     * allocated two regions would put half its targets outside the extent. */
    cq_scratch_alloc(&scr, (uint32_t)cq_divrem_region(W, want_q));

    e.dst      = dst;
    e.k.a      = a;
    e.k.b      = b;
    e.k.scr    = &scr;
    e.k.off    = 0u;
    e.k.W      = W;
    e.k.with_q = want_q;

    cq_sandwich(ctx, &scr, compute, cq_divrem_steps(W, want_q), copyout, W, &e);
    cq_scratch_dispose(&scr);
}

void cq_kernel_udiv(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W)
{ divrem(ctx, dst, a, b, W, 1); }

void cq_kernel_urem(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W)
{ divrem(ctx, dst, a, b, W, 0); }
