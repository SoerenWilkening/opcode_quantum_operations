/* src/kernels/shift_var.c — M12, Step 14. The barrel shifter over K10's mux.
 *
 * Read docs/constructions/K10.md §2.2, K04.md §7 and shift_var.h before
 * changing anything. The schedule is Bennett's `lower_var_shl!` /
 * `lower_var_lshr!` / `lower_var_ashr!` (arith.jl:350-400), which differ from
 * each other in exactly one loop — the index shuffle — and share the INIT copy
 * and the mux stage. That is why `shuffle` is the only function below that
 * branches on direction.
 *
 * THE SCHEDULE IS EXPORTED AS `cq_barrel_block` + `cq_barrel_step` AND THE
 * THREE ENTRY POINTS DISPATCH THROUGH IT (plan §0.4, PRD-v2 §7.10). There is
 * one gate list in this module and one step decode; `barrel()` below is a
 * scratch allocation, D8's dispatch and a `cq_sandwich` call over the block.
 * The export is ADDITIVE — no gate moved and no L4 golden moved with it.
 *
 * THE MUX STAGE IS M17's, CALLED, NOT COPIED. Upstream ends each stage with
 * `result = lower_mux!(g, wa, [b[k+1]], shifted, result, W)` (arith.jl:361,
 * :377, :397), so reusing `cq_mux_step` is Rule 1 applied to the call graph.
 * A second transcription of those four gates here would be a second chance to
 * put the Toffoli before the two CNOTs that build `d`.
 *
 * ONE REGION, CARVED. `cur0[W]`, then `(sh_k[W], r_k[W], d_k[W])` for each of
 * the L stages: W(3L+1) bits, ONE `cq_scratch_alloc`, because emit.c's I6(a)
 * check is a pointer RANGE test and a kernel that allocated two regions would
 * have half its targets outside the armed extent. The running value entering
 * stage k is `cur0` at k = 0 and `r_{k-1}` after that — Bennett's `result`
 * being reassigned to the mux output. Since the export, the region is the
 * CALLER's: `cq_barrel_block.scr` + `.off`, exactly as `cq_divrem_block` has
 * it, and M12's own entry points pass `off = 0`.
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

/* --- The block's addressing. --------------------------------------------- */

static cq_bit *span(const cq_barrel_block *k, int off, int len)
{
    return cq_scratch_span(k->scr, k->off + (uint32_t)off, (uint32_t)len);
}

/* sh_k sits at W(1+3k); r_k and d_k follow it. */
static int stage_off(int W, int k) { return W * (1 + 3 * k); }

typedef struct { cq_bit *cur, *sh, *r, *d; } stage_ptrs;

static void stage_ptrs_of(const cq_barrel_block *k, int j, stage_ptrs *p)
{
    int W = k->W, o = stage_off(W, j);

    p->cur = span(k, j == 0 ? 0 : stage_off(W, j - 1) + W, W);
    p->sh  = span(k, o,         W);
    p->r   = span(k, o + W,     W);
    p->d   = span(k, o + 2 * W, W);
}

/* THE ONLY DIRECTION-DEPENDENT CODE, and the asymmetry is upstream's: shl and
 * lshr write W - s of the `sh_k` bits and leave the other s alone, while ashr
 * writes ALL W because its else-branch clamps the source to the sign bit
 * (arith.jl:394). The s bits shl/lshr never write are the shifted-in zeros —
 * they are pre-materialised qubits holding |0> under I6(b), not the free
 * BIT_ZERO constants K10.md §3.2 assumed before ckd.9, which is the whole of
 * bd 84m and worth +2(2^L - 1) CX in the sandwiched total. */
static void shuffle(cq_ctx *ctx, const cq_barrel_block *k, int j, int i,
                    const cq_bit *cur, cq_bit *sh)
{
    int s = 1 << j;
    int src;

    if (k->dir == CQ_BARREL_SHL) {
        cq_emit_cx(ctx, &cur[i], &sh[i + s]);              /* arith.jl:373-375 */
        return;
    }
    if (k->dir == CQ_BARREL_LSHR) {
        cq_emit_cx(ctx, &cur[i + s], &sh[i]);              /* arith.jl:357-359 */
        return;
    }

    src = i + s;                                           /* arith.jl:389-395 */
    if (src > k->W - 1) src = k->W - 1;
    cq_emit_cx(ctx, &cur[src], &sh[i]);
}

/* A PURE FUNCTION OF (W, dir, stage) AND NEVER OF BIT-KINDS. Kinds change
 * during the compute half — the driver materialises the whole region at step 1
 * — but the step INDEX SPACE must not, or the reverse pass would walk a
 * different schedule than the forward. Steps whose gates fold away emit nothing
 * on both passes, which is symmetric and safe.
 *
 * IT IS ALSO THE ONE PLACE THE SHUFFLE'S LENGTH IS WRITTEN. `cq_barrel_steps`
 * sums exactly this over the stages, and `cq_barrel_step` walks exactly this to
 * decode — which is why the decode cannot fall off the end of a step index the
 * range check has already accepted. */
static int shuffle_steps(int W, cq_barrel_dir dir, int j)
{
    return dir == CQ_BARREL_ASHR ? W : W - (1 << j);
}

/* --- The exported block. ------------------------------------------------- */

/* THE TWO WIDTH GUARDS CARRY DISJOINT MESSAGES, AND THAT IS THE ONLY THING THAT
 * MAKES EITHER OF THEM TESTABLE. They are the same condition in two functions a
 * consumer calls in sequence, and `cq_barrel_step`'s range check calls
 * `cq_barrel_steps` FIRST — so with one message a death case could not say which
 * copy spoke, and deleting either would leave every case green on the other.
 * M15's `cq_addacc_check` records the identical finding and the identical
 * remedy: make the messages disjoint and drive each directly. */
int cq_barrel_region(int W)
{
    if (W <= 0) cq_kernel_die("barrel: region width is not positive");
    return W * (3 * cq_shift_stages(W) + 1);
}

int cq_barrel_steps(int W, cq_barrel_dir dir)
{
    int S, n;

    if (W <= 0) cq_kernel_die("barrel: step-count width is not positive");

    S = cq_shift_stages(W);
    n = W;                                       /* INIT, arith.jl:351-352 */
    for (int j = 0; j < S; j++)
        n += shuffle_steps(W, dir, j) + CQ_MUX_STEPS_PER_BIT * W;
    return n;
}

/* THE ANSWER IS `r_{S-1}`, AND `S == 0` IS A REACHABLE ROW HERE WHERE IT IS NOT
 * IN `barrel()` (bd djf). The kernel delegates at `L == 0` before any scratch
 * exists, so its copy-out never sees that width; a CONSUMER may legitimately
 * drive this block at W = 1, where the construction is the INIT copy alone and
 * the result is `cur0`. Written as a branch rather than as arithmetic because
 * `stage_off(W, -1) + W` is `-W`, which `span` casts to uint32_t and M08
 * refuses as (4294967295, W) — an abort at the point the mistake was made,
 * which is exactly why M08 was left alone rather than taught to tolerate the
 * wrapped offset. */
cq_bit *cq_barrel_result(const cq_barrel_block *k)
{
    int S = cq_shift_stages(k->W);

    if (S == 0) return span(k, 0, k->W);
    return span(k, stage_off(k->W, S - 1) + k->W, k->W);
}

void cq_barrel_step(cq_ctx *ctx, const cq_barrel_block *k, int u)
{
    int W = k->W, S = cq_shift_stages(W), s = u;

    /* The consumer's range check, `cq_ult_step`'s (kernels/cmp.c) for
     * `cq_ult_step`'s reason: a composite kernel maps a contiguous run of its
     * own step indices onto [0, cq_barrel_steps(W, dir)), and an off-by-one
     * would land inside the stage loop below and emit a plausible wrong gate
     * rather than fail. It is the ONLY guard in this function: the loop walks
     * the same `shuffle_steps` sum that bound is built from, so an accepted
     * index provably lands in a slot and a second, terminal `cq_kernel_die`
     * would be a line no case could ever reach. */
    if (u < 0 || u >= cq_barrel_steps(W, k->dir))
        cq_kernel_die("barrel: step index outside "
                      "[0, cq_barrel_steps(W, dir))");

    if (s < W) {                                  /* INIT: cur0 ^= a */
        cq_emit_cx(ctx, &k->a[s], &span(k, 0, W)[s]);
        return;
    }
    s -= W;

    for (int j = 0; j < S; j++) {
        stage_ptrs p;
        int ns = shuffle_steps(W, k->dir, j);

        stage_ptrs_of(k, j, &p);

        if (s < ns) { shuffle(ctx, k, j, s, p.cur, p.sh); return; }
        s -= ns;

        if (s < CQ_MUX_STEPS_PER_BIT * W) {
            /* t = the SHIFTED value, f = the unshifted one: upstream passes
             * `lower_mux!(..., shifted, result, W)`, so bit k of the amount
             * selects the shift. Swapping them inverts the whole shifter and
             * no gate count can see it. */
            cq_mux_block m;

            m.cond = &k->b[j];
            m.t    = p.sh;
            m.f    = p.cur;
            m.r    = p.r;
            m.d    = p.d;
            cq_mux_step(ctx, &m, s);
            return;
        }
        s -= CQ_MUX_STEPS_PER_BIT * W;
    }
}

/* --- The three Rule 7 entry points over that block. ---------------------- */

typedef struct {
    cq_bit         *dst;
    cq_barrel_block k;
} barrel_env;

static void compute(cq_ctx *ctx, void *env, int s)
{
    cq_barrel_step(ctx, &((const barrel_env *)env)->k, s);
}

/* The "^=" of Rule 7's contract, and the only place `dst` is written on the
 * sandwich path. `cq_barrel_result` is asked where the answer is rather than
 * the offset being spelled again here — one arithmetic slip, in one place.
 *
 * STILL NOT TAKEN — an `L == 0 ? cur0 : ...` guard AT THIS CALL SITE. The
 * branch now exists, but it lives inside `cq_barrel_result` where a consumer
 * can reach it; here it remains unreachable, because `barrel()` delegates on
 * `L == 0 ||` outright and at L = 0 producing the identity through a sandwich
 * would cost W scratch qubits where R9's short-circuit produces it for none. */
static void copyout(cq_ctx *ctx, void *env, int i)
{
    const barrel_env *e = (const barrel_env *)env;

    cq_emit_cx(ctx, &cq_barrel_result(&e->k)[i], &e->dst[i]);
}

/* `cq_kernel_fn` (kernels/kernel.h), not a local re-typedef: M11's three entry
 * points have exactly Rule 7's canonical shape, which is the whole reason the
 * delegation below is a plain call and not an adapter. */
static void barrel(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, const cq_bit *b,
                   int W, cq_barrel_dir dir, cq_kernel_fn constant_path)
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
     * the mux stages evaluated at compile time.
     *
     * THE COUNT IS `L`, NOT `W`, AND THAT IS LOAD-BEARING — it is why
     * kernels/kernel.h's shared predicate takes a COUNT and names the parameter
     * `n`. Only the bits the construction READS are scanned: bits at or above L
     * are structurally invisible to the barrel — never mux controls — so their
     * kind is not this module's business, exactly as in cq_shift_amount.
     * Passing `W` here would send a rail whose amount is classical in every
     * lane the barrel reads, but quantum in a lane it never looks at, down the
     * SANDWICH path instead of the constant one: the same value by a different
     * circuit, which no value-level test is obliged to see.
     *
     * `L == 0` IS A SEPARATE DISJUNCT, AND IT IS NOT REDUNDANT THE WAY IT LOOKS
     * (bd djf). This comment used to read "at W=1, L is 0 and the scan is
     * vacuously true, which is how a width-1 variable shift becomes the
     * identity" — true of the SCAN, and it made the sandwich's own precondition
     * depend on the ARGUMENT of a predicate rather than on anything structural.
     * L is 0 exactly when W <= 1 (cq_shift_stages), and at L = 0 the driver
     * below has no last stage. So the entry condition for cq_sandwich is
     * L >= 1, and it is now WRITTEN rather than inferred through
     * cq_bits_all_const(b, 0) being vacuously true.
     *
     * Behaviourally inert on a correct library, deliberately — the scan already
     * answers 1 at n = 0, so no test can separate the two routes, and none is
     * claimed to. What the disjunct buys is that a wrong COUNT in the scan can
     * no longer reach the underflow: measured, with `L` mutated to `W`, W=1
     * delegates instead of aborting and the purpose-built detector in
     * tests/test_kernel_shift_var_d8.inc keeps firing. Its dated numbers are in
     * tests/test_kernel_shift_var.c beside the case-order note.
     *
     * AND THAT SURVIVAL IS STRUCTURAL, NOT A LUCKY MEASUREMENT — which is the
     * durable form of the argument and the one worth keeping. The disjunct can
     * only fire at L == 0, and cq_shift_stages gives L == 0 iff W <= 1. The
     * detector runs at W=8, where L is 3. So the hardening and the detector are
     * DISJOINT BY CONSTRUCTION: no choice of operands can route that case
     * through this clause, and it would keep its teeth against a wrong count at
     * every width above 1 even if the mutant re-run had never been done. The
     * measurement confirms the reasoning; it is not what the claim rests on. */
    if (L == 0 || cq_bits_all_const(b, L)) {
        constant_path(ctx, dst, a, b, W);
        return;
    }

    cq_scratch_alloc(&scr, (uint32_t)cq_barrel_region(W));

    e.dst   = dst;
    e.k.a   = a;
    e.k.b   = b;
    e.k.scr = &scr;
    e.k.off = 0u;
    e.k.W   = W;
    e.k.dir = dir;

    cq_sandwich(ctx, &scr, compute, cq_barrel_steps(W, dir), copyout, W, &e);
    cq_scratch_dispose(&scr);
}

void cq_kernel_shl_var(cq_ctx *ctx, cq_bit *dst,
                       const cq_bit *a, const cq_bit *b, int W)
{
    barrel(ctx, dst, a, b, W, CQ_BARREL_SHL, cq_kernel_shl);
}

void cq_kernel_lshr_var(cq_ctx *ctx, cq_bit *dst,
                        const cq_bit *a, const cq_bit *b, int W)
{
    barrel(ctx, dst, a, b, W, CQ_BARREL_LSHR, cq_kernel_lshr);
}

void cq_kernel_ashr_var(cq_ctx *ctx, cq_bit *dst,
                        const cq_bit *a, const cq_bit *b, int W)
{
    barrel(ctx, dst, a, b, W, CQ_BARREL_ASHR, cq_kernel_ashr);
}
