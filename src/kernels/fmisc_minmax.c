/* src/kernels/fmisc_minmax.c — M38, K24, the SURFACE of `fmin` / `fmax`: the
 * classical row and the two Rule 7 kernels. The rows are fmisc_rows.c and the
 * machine fmisc_step.c, on the seams fmisc_int.h records.
 */

#include "kernels/fmisc_int.h"

#include "emit.h"
#include "kernels/fpclass.h"
#include "kernels/kernel.h"
#include "sandwich.h"

enum { W64 = CQ_FP64_W };

/* --- Risk R9's classical row (PRD-v2 §7.4). ------------------------------ */

/* THE SAME JULIA BODY OVER `uint64_t`, NEVER THE HOST `fmin()` — whose
 * two-NaN answer is one of the payloads where upstream's is the canonical
 * 0x7FF8000000000000, and whose ±0 answer C leaves unspecified. It composes
 * M31's and M36's own classical rows for the two callees, exactly as the
 * circuit composes their step blocks; each line cites the one it transcribes.
 * A `Bool` intermediate is an `int` and a `UInt64` one a `uint64_t`
 * (D-K18-1). `fmax` passes `max = 1`, which moves exactly the two operand
 * orders fmin.jl:128 and :130 move. */
static uint64_t minmax_eval(uint64_t a, uint64_t b, int max)
{
    uint64_t abs_a = a & UINT64_C(0x7FFFFFFFFFFFFFFF);            /* :87  */
    uint64_t abs_b = b & UINT64_C(0x7FFFFFFFFFFFFFFF);            /* :88  */
    int a_nan = cq_fp_class_eval(a, CQ_FP_IS_NAN);                /* :93  */
    int b_nan = cq_fp_class_eval(b, CQ_FP_IS_NAN);                /* :94  */
    int both_nan = a_nan & b_nan;                                 /* :95  */
    int both_zero = (abs_a == UINT64_C(0)) & (abs_b == UINT64_C(0)); /* :97 */
    int a_neg = (a & UINT64_C(0x8000000000000000)) != UINT64_C(0);   /* :98 */
    int lt = max ? cq_fcmp_eval(b, a, CQ_FCMP_OLT) != UINT64_C(0)    /* :128 */
                 : cq_fcmp_eval(a, b, CQ_FCMP_OLT) != UINT64_C(0);   /* :100 */
    uint64_t base = lt ? a : b;                                   /* :101 */
    uint64_t res;

    base = both_zero ? (max ? (a_neg ? b : a)                     /* :130 */
                            : (a_neg ? a : b))                    /* :102 */
                     : base;
    res = a_nan ? b : (b_nan ? a : base);                         /* :104 */
    return both_nan ? UINT64_C(0x7FF8000000000000) : res;         /* :105 */
}

uint64_t cq_fmin_eval(uint64_t a, uint64_t b) { return minmax_eval(a, b, 0); }
uint64_t cq_fmax_eval(uint64_t a, uint64_t b) { return minmax_eval(a, b, 1); }

/* --- The two kernels: Bennett-in-the-small over one flat region. ---------- */

typedef struct {
    cq_fmisc_block k;
    cq_bit        *dst;
} fmisc_env;

static void fmisc_compute(cq_ctx *ctx, void *env, int s)
{
    cq_fmisc_step(ctx, &((const fmisc_env *)env)->k, s);
}

/* The "^=" of Rule 7's contract, and the only place `dst` is written on the
 * sandwich path — which is why the driver runs it with the I6 extent
 * DISARMED. One CX per lane from row 16's `r`: Bennett RETURNS `r` as the SSA
 * result, and the copy is its `_emit_copy_out!` shape (arith.jl:89-96). */
static void fmisc_copyout(cq_ctx *ctx, void *env, int i)
{
    const fmisc_env *e = (const fmisc_env *)env;

    cq_emit_cx(ctx, &cq_fmisc_result(&e->k)[i], &e->dst[i]);
}

static void minmax(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, const cq_bit *b,
                   int W, cq_fmisc_prog p)
{
    cq_scratch scr;
    fmisc_env e;

    /* PRD-v2 §1 scopes v2 to f64, and upstream's own `llvm.minnum` /
     * `llvm.maxnum` arms refuse any other width (instructions.jl:5117-5120,
     * :5127-5130), so another width is a fiction rather than a gap. */
    if (W != W64)
        cq_kernel_die("fmisc: W must be 64 — v2 is f64 only and soft_fmin / "
                      "soft_fmax take two UInt64");
    cq_kernel_check_dst(dst, a, b, W);

    /* RISK R9, AND IT IS NOT AN OPTIMISATION (plan §0.2 consequence 2).
     * Pre-materialisation is unconditional and a block has no fold of its
     * own, so without this an all-classical `fmin` would draw its whole
     * region from the pool for an operation with no quantum input. The value
     * comes from the SAME Julia body the circuit runs (PRD-v2 §7.4). */
    if (cq_bits_all_const(a, W) && cq_bits_all_const(b, W)) {
        uint64_t v = minmax_eval(cq_fp_pack(a), cq_fp_pack(b),
                                 p == CQ_FMISC_MAX);

        for (int i = 0; i < W; i++)
            if (((v >> (unsigned)i) & UINT64_C(1)) != 0u) cq_emit_x(ctx, &dst[i]);
        return;
    }

    /* ONE CONTIGUOUS REGION: emit.c's I6(a) check is a pointer RANGE test. */
    cq_scratch_alloc(&scr, cq_fmisc_region(p));
    e.k.a = a; e.k.b = b; e.k.scr = &scr; e.k.off = 0u; e.k.prog = p;
    e.dst = dst;

    cq_sandwich(ctx, &scr, fmisc_compute, cq_fmisc_steps(p),
                fmisc_copyout, W, &e);
    cq_scratch_dispose(&scr);
}

void cq_kernel_fmin(cq_ctx *ctx, cq_bit *dst, const cq_bit *a,
                    const cq_bit *b, int W)
{
    minmax(ctx, dst, a, b, W, CQ_FMISC_MIN);
}

void cq_kernel_fmax(cq_ctx *ctx, cq_bit *dst, const cq_bit *a,
                    const cq_bit *b, int W)
{
    minmax(ctx, dst, a, b, W, CQ_FMISC_MAX);
}
