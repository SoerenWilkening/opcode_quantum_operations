/* src/kernels/shift_var.c — M12, Step 14. The barrel shifter over K10's mux.
 *
 * Read docs/constructions/K10.md §2.2 and shift_var.h before changing anything.
 * The schedule is Bennett's `lower_var_shl!` / `lower_var_lshr!` /
 * `lower_var_ashr!` (arith.jl:350-400), which differ from each other in exactly
 * one loop — the index shuffle — and share the INIT copy and the mux stage.
 * That is why `shuffle` is the only function below that branches on direction.
 *
 * THE MUX STAGE IS M17's, CALLED, NOT COPIED. Upstream ends each stage with
 * `result = lower_mux!(g, wa, [b[k+1]], shifted, result, W)` (arith.jl:361,
 * :377, :397), so reusing `cq_mux_step` is Rule 1 applied to the call graph.
 * A second transcription of those four gates here would be a second chance to
 * put the Toffoli before the two CNOTs that build `d`.
 *
 * ONE REGION, CARVED. `cur0[W]`, then `(sh_k[W], r_k[W], d_k[W])` for each of
 * the L stages: W(3L+1) bits, one `cq_scratch_alloc`, because emit.c's I6(a)
 * check is a pointer RANGE test and a kernel that allocated two regions would
 * have half its targets outside the armed extent. The running value entering
 * stage k is `cur0` at k = 0 and `r_{k-1}` after that — Bennett's `result`
 * being reassigned to the mux output.
 *
 * WHAT THIS COSTS, SO NOBODY IS SURPRISED BY IT AT W=64: W(3L+1) scratch qubits
 * — 1216 at i64, 2816 at i128 — every one of them pre-materialised by I6(b)
 * and returned by the reverse half. Nothing in Bennett reduces it (Bennett
 * never frees inside `lower_*`), and the driver cannot free mid-compute-half
 * without breaking the replay, so a reduction is a genuine design change
 * (staged or pebbled scratch) and belongs in a bead, not in an improvisation
 * here. K10.md §5 delta 13 records the one cheaper equivalent form that is
 * known and is deliberately NOT v1.
 */

#include "kernels/shift_var.h"

#include "emit.h"
#include "kernels/kernel.h"
#include "kernels/mux.h"
#include "kernels/shift_const.h"
#include "sandwich.h"
#include "scratch.h"

typedef enum { DIR_SHL = 0, DIR_LSHR, DIR_ASHR } barrel_dir;

typedef struct {
    cq_bit       *dst;
    const cq_bit *a, *b;
    cq_scratch   *scr;
    int           W, L;
    barrel_dir    dir;
} barrel_env;

static cq_bit *span(const barrel_env *e, int off)
{
    return cq_scratch_span(e->scr, (uint32_t)off, (uint32_t)e->W);
}

/* sh_k sits at W(1 + 3k); r_k and d_k follow it. */
static int stage_off(const barrel_env *e, int k) { return e->W * (1 + 3 * k); }

typedef struct { cq_bit *cur, *sh, *r, *d; } stage_ptrs;

static void stage_ptrs_of(const barrel_env *e, int k, stage_ptrs *p)
{
    int o = stage_off(e, k);

    p->cur = span(e, k == 0 ? 0 : stage_off(e, k - 1) + e->W);
    p->sh  = span(e, o);
    p->r   = span(e, o + e->W);
    p->d   = span(e, o + 2 * e->W);
}

/* THE ONLY DIRECTION-DEPENDENT CODE, and the asymmetry is upstream's: shl and
 * lshr write W - s of the `sh_k` bits and leave the other s alone, while ashr
 * writes ALL W because its else-branch clamps the source to the sign bit
 * (arith.jl:394). The s bits shl/lshr never write are the shifted-in zeros —
 * they are pre-materialised qubits holding |0> under I6(b), not the free
 * BIT_ZERO constants K10.md §3.2 assumed before ckd.9, which is the whole of
 * bd 84m and worth +2(2^L - 1) CX in the sandwiched total. */
static void shuffle(cq_ctx *ctx, const barrel_env *e, int k, int j,
                    const cq_bit *cur, cq_bit *sh)
{
    int s = 1 << k;
    int src;

    if (e->dir == DIR_SHL)  { cq_emit_cx(ctx, &cur[j],     &sh[j + s]); return; }
    if (e->dir == DIR_LSHR) { cq_emit_cx(ctx, &cur[j + s], &sh[j]);     return; }

    src = j + s;
    if (src > e->W - 1) src = e->W - 1;
    cq_emit_cx(ctx, &cur[src], &sh[j]);
}

static int shuffle_steps(const barrel_env *e, int k)
{
    return e->dir == DIR_ASHR ? e->W : e->W - (1 << k);
}

/* A PURE FUNCTION OF (W, L, dir) AND NEVER OF BIT-KINDS. Kinds change during
 * the compute half — the driver materialises the whole region at step 1 — but
 * the step INDEX SPACE must not, or the reverse pass would walk a different
 * schedule than the forward. Steps whose gates fold away emit nothing on both
 * passes, which is symmetric and safe. */
static int n_compute_of(const barrel_env *e)
{
    int n = e->W;

    for (int k = 0; k < e->L; k++)
        n += shuffle_steps(e, k) + CQ_MUX_STEPS_PER_BIT * e->W;
    return n;
}

static void compute(cq_ctx *ctx, void *env, int s)
{
    const barrel_env *e = (const barrel_env *)env;
    int W = e->W;

    if (s < W) {                                  /* INIT: cur0 ^= a */
        cq_emit_cx(ctx, &e->a[s], &span(e, 0)[s]);
        return;
    }
    s -= W;

    for (int k = 0; k < e->L; k++) {
        stage_ptrs p;
        int ns = shuffle_steps(e, k);

        stage_ptrs_of(e, k, &p);

        if (s < ns) { shuffle(ctx, e, k, s, p.cur, p.sh); return; }
        s -= ns;

        if (s < CQ_MUX_STEPS_PER_BIT * W) {
            /* t = the SHIFTED value, f = the unshifted one: upstream passes
             * `lower_mux!(..., shifted, result, W)`, so bit k of the amount
             * selects the shift. Swapping them inverts the whole shifter and
             * no gate count can see it. */
            cq_mux_block m;

            m.cond = &e->b[k];
            m.t    = p.sh;
            m.f    = p.cur;
            m.r    = p.r;
            m.d    = p.d;
            cq_mux_step(ctx, &m, s);
            return;
        }
        s -= CQ_MUX_STEPS_PER_BIT * W;
    }

    cq_kernel_die("shift_var: compute step past the end of the schedule");
}

/* The answer is r_{L-1}, and L >= 1 HERE BY CONSTRUCTION: at L = 0 the amount
 * test below is vacuously true, so the barrel delegates and this driver never
 * runs. An `L == 0 ? cur0 : ...` guard would be a branch no test can reach,
 * which is worse than no guard — cq_scratch_span bounds-checks the result
 * anyway, so an arithmetic slip aborts at the point the mistake was made. */
static void copyout(cq_ctx *ctx, void *env, int i)
{
    const barrel_env *e = (const barrel_env *)env;
    cq_bit *res = span(e, stage_off(e, e->L - 1) + e->W);

    cq_emit_cx(ctx, &res[i], &e->dst[i]);
}

/* Only the bits the construction READS. Bits at or above L are structurally
 * invisible to the barrel — never mux controls — so their kind is not this
 * module's business, exactly as in cq_shift_amount. At W=1, L is 0 and this is
 * vacuously true, which is how a width-1 variable shift becomes the identity. */
static int amount_is_classical(const cq_bit *b, int L)
{
    for (int i = 0; i < L; i++)
        if (!cq_bit_is_const(b[i])) return 0;
    return 1;
}

/* `cq_kernel_fn` (kernels/kernel.h), not a local re-typedef: M11's three entry
 * points have exactly Rule 7's canonical shape, which is the whole reason the
 * delegation below is a plain call and not an adapter. */
static void barrel(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, const cq_bit *b,
                   int W, barrel_dir dir, cq_kernel_fn constant_path)
{
    cq_scratch scr;
    barrel_env e;
    int L;

    /* THE GUARD IS FIRST, INCLUDING BEFORE cq_shift_stages. Nothing below cares
     * about the order today — cq_shift_stages answers 0 for a zero or negative
     * width rather than misbehaving — but the death test's whole claim is that
     * a malformed call is refused before anything looks at it, and a reader
     * checking that claim should not have to reason about a function call in an
     * initialiser to confirm it. */
    cq_kernel_check_dst(dst, a, b, W);

    L = cq_shift_stages(W);

    /* Risk R9's short-circuit, and D8's agreement made structural: the constant
     * path is not an approximation of this one, it is the same reduction with
     * the mux stages evaluated at compile time. */
    if (amount_is_classical(b, L)) { constant_path(ctx, dst, a, b, W); return; }

    cq_scratch_alloc(&scr, (uint32_t)(W * (3 * L + 1)));

    e.dst = dst;
    e.a   = a;
    e.b   = b;
    e.scr = &scr;
    e.W   = W;
    e.L   = L;
    e.dir = dir;

    cq_sandwich(ctx, &scr, compute, n_compute_of(&e), copyout, W, &e);
    cq_scratch_dispose(&scr);
}

void cq_kernel_shl_var(cq_ctx *ctx, cq_bit *dst,
                       const cq_bit *a, const cq_bit *b, int W)
{
    barrel(ctx, dst, a, b, W, DIR_SHL, cq_kernel_shl);
}

void cq_kernel_lshr_var(cq_ctx *ctx, cq_bit *dst,
                        const cq_bit *a, const cq_bit *b, int W)
{
    barrel(ctx, dst, a, b, W, DIR_LSHR, cq_kernel_lshr);
}

void cq_kernel_ashr_var(cq_ctx *ctx, cq_bit *dst,
                        const cq_bit *a, const cq_bit *b, int W)
{
    barrel(ctx, dst, a, b, W, DIR_ASHR, cq_kernel_ashr);
}
