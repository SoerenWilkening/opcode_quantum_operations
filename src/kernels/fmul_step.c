/* src/kernels/fmul_step.c — M34, K16. THE LAYOUT AND THE OPERANDS: what a row
 * costs, where its spans lie inside the caller's region, and what each operand
 * code resolves to. The dispatch, the public block and the Rule 7 kernel are
 * next door in fmul_emit.c, on the seam fmul_int.h records; the row table is
 * in fmul.c, on M36's ROW TABLES <-> STEP MACHINE seam.
 *
 * NOT ONE LINE HERE KNOWS ANY JULIA. Every cost is ASKED of another module —
 * cq_eq_steps, cq_ult_steps, cq_add_steps, cq_sub_steps, cq_mux_steps,
 * cq_mul_steps, cq_norm52_steps, cq_clz_steps, cq_subnorm_steps,
 * cq_round_steps and the matching _region functions — or is upstream's own
 * four-gate bitwise vocabulary, and nothing would move if the row table
 * changed.
 *
 * A VIEW CHAIN COLLAPSES TO ONE (shift, mask) PAIR, WHICH IS WHY VIEWS COST
 * NOTHING EVEN WHEN THEY NEST — M32's finding, and K16 nests them four deep in
 * places: `(prod_hi_final & ((1<<42)-1)) << 14` (fmul.jl:174) is two rows and
 * one addressing computation. The composition is
 *
 *     (((base >> s1) & m1) >> s2) & m2 == (base >> (s1+s2)) & ((m1 >> s2) & m2)
 *
 * with a NEGATIVE shift meaning a left shift, and it is walked OUTERMOST-IN so
 * the accumulated shift is applied to each row's mask before that row's own
 * shift joins it.
 *
 * THE VIEWS AND THE CONSTANT SPANS ARE REBUILT PER STEP, ON PURPOSE —
 * fpclass.c's, fcmp_step.c's and fpround_step.c's reason, verbatim: a block
 * carrying cached operands would carry state whose initialisation a consumer
 * can forget, and a forgotten bind is a silent wrong circuit rather than a
 * failure. It is also what the sandwich needs: the driver replays indices in
 * reverse and a table rebuilt from mutable state would desynchronise the two
 * halves (K16.md §2.5).
 *
 * THE PREFIX-OFFSET WALK IS DONE ONCE PER STEP AND NOT ONCE PER LOOKUP. A row
 * reference resolves to a span, and a span needs the sum of every earlier
 * row's region; doing that per operand would make the step O(n^2) in a 150-row
 * program driven 163,300 times per compute half.
 *
 * I6(a) IS SATISFIED WITH NOTHING LEFT TO CHECK. `cq_fm_sp` is the only thing
 * here that hands back a WRITABLE pointer and every one of them is a bit of
 * the caller's region at `off + <span>`. The two rails, the views over them,
 * the constant spans and every block output come back `const`, so a source
 * cannot be materialised by construction.
 */

#include "kernels/fmul_int.h"

#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/fpclass.h"
#include "kernels/fpround.h"
#include "kernels/kernel.h"
#include "kernels/mul.h"
#include "kernels/mux.h"

/* `CQ_FM_W` spelled short, for the same reason fcmp_step.c spells it `W64`:
 * every span expression below carries it two or three times. */
enum { W64 = CQ_FM_W };

/* --- Slot and region arithmetic, ASKED of the owning modules. ------------- */

/* `lower_not1!` is CNOT(w, r) then NOT(r) into a FRESH wire — two slots, one
 * bit (arith.jl:474-478). `lower_and!` is one Toffoli per lane, so one slot
 * and no bit of its own at one lane (:268-272). `lower_or!` is CNOT, CNOT,
 * Toffoli per lane, so three slots and one bit (:274-282); `lower_xor!` is
 * CNOT, CNOT, so two slots per lane and W bits (:284-291). Transcribed from
 * the pinned source because they are emitted directly next door and no module
 * publishes a cost for them (PRD-v2 §7.10). */
enum { FM_NOT1_STEPS = 2, FM_AND1_STEPS = 1, FM_OR1_STEPS = 3,
       FM_OR_PER_LANE = 3, FM_XOR_PER_LANE = 2 };

int cq_fm_row_steps(const cq_fmul_row *r)
{
    switch (r->op) {
    case CQ_FMOP_VIEW: case CQ_FMOP_OUT: return 0;
    case CQ_FMOP_CLASS:   return cq_fp_class_steps((cq_fp_class)r->s1);
    case CQ_FMOP_EQ:      return cq_eq_steps(W64);
    case CQ_FMOP_ULT:     return cq_ult_steps(W64);
    case CQ_FMOP_ADD:     return cq_add_steps(W64);
    case CQ_FMOP_SUB:     return cq_sub_steps(W64);
    case CQ_FMOP_MUX:     return cq_mux_steps(W64);
    case CQ_FMOP_OR:      return FM_OR_PER_LANE * W64;
    case CQ_FMOP_XOR:     return FM_XOR_PER_LANE * W64;
    case CQ_FMOP_MUL:     return cq_mul_steps(W64);
    case CQ_FMOP_NORM52:  return cq_norm52_steps();
    case CQ_FMOP_CLZ:     return cq_clz_steps();
    case CQ_FMOP_SUBNORM: return cq_subnorm_steps();
    case CQ_FMOP_ROUND:   return cq_round_steps();
    case CQ_FMOP_NOT1:    return FM_NOT1_STEPS;
    case CQ_FMOP_AND1:    return FM_AND1_STEPS;
    case CQ_FMOP_OR1:     return FM_OR1_STEPS;
    case CQ_FMOP_N_OP:
    default: break;
    }
    cq_kernel_die("fmul: unknown op in the program");
    return 0;
}

uint32_t cq_fm_row_region(const cq_fmul_row *r)
{
    switch (r->op) {
    case CQ_FMOP_VIEW: case CQ_FMOP_OUT: return 0u;
    case CQ_FMOP_CLASS:   return cq_fp_class_region((cq_fp_class)r->s1);
    case CQ_FMOP_EQ:      return (uint32_t)cq_eq_region(W64);
    case CQ_FMOP_ULT:     return (uint32_t)cq_ult_region(W64);
    case CQ_FMOP_ADD:     return (uint32_t)cq_add_region(W64);
    case CQ_FMOP_SUB:     return (uint32_t)cq_sub_region(W64);
    case CQ_FMOP_MUX:     return (uint32_t)cq_mux_region(W64);
    case CQ_FMOP_OR: case CQ_FMOP_XOR: return (uint32_t)W64;
    case CQ_FMOP_MUL:     return (uint32_t)cq_mul_region(W64);
    case CQ_FMOP_NORM52:  return cq_norm52_region();
    case CQ_FMOP_CLZ:     return cq_clz_region();
    case CQ_FMOP_SUBNORM: return cq_subnorm_region();
    case CQ_FMOP_ROUND:   return cq_round_region();
    case CQ_FMOP_NOT1: case CQ_FMOP_AND1: case CQ_FMOP_OR1:
        return 1u;                       /* one output bit                */
    case CQ_FMOP_N_OP:
    default: break;
    }
    cq_kernel_die("fmul: unknown op in the program");
    return 0u;
}

uint32_t cq_fmul_region(void)
{
    const cq_fmul_row *rows;
    uint32_t bits = 0u;
    int n;

    rows = cq_fmul_rows(&n);
    for (int i = 0; i < n; i++) bits += cq_fm_row_region(&rows[i]);
    return bits;
}

int cq_fmul_steps(void)
{
    const cq_fmul_row *rows;
    int slots = 0, n;

    rows = cq_fmul_rows(&n);
    for (int i = 0; i < n; i++) slots += cq_fm_row_steps(&rows[i]);
    return slots;
}

/* The prefix-offset walk plus the fit check, in ONE pass.
 *
 * The region check is the BLOCK's, and it has to be: without it the first
 * out-of-region span aborts in M08 naming the REGION rather than the consumer
 * that mis-sized its offset, and the two-programs-in-one-region shape then has
 * no diagnostic of its own. Hard error in both configurations. */
void cq_fm_arm(const cq_fmul_block *k, uint32_t *off)
{
    const cq_fmul_row *rows;
    uint32_t o;
    int n;

    if (k == NULL || k->scr == NULL)
        cq_kernel_die("fmul: the block has no region");
    rows = cq_fmul_rows(&n);
    o = k->off;
    for (int i = 0; i < n; i++) {
        off[i] = o;
        o += cq_fm_row_region(&rows[i]);
    }
    if ((uint64_t)o > (uint64_t)cq_scratch_size(k->scr))
        cq_kernel_die("fmul: the block's region does not fit at its offset");
}

/* `at` is ABSOLUTE inside the region: the prefix walk has already added
 * `k->off`, which is the one place this block's base is applied. */
cq_bit *cq_fm_sp(const cq_fmul_block *k, uint32_t at, uint32_t len)
{
    return cq_scratch_span(k->scr, at, len);
}

/* --- Operand resolution. -------------------------------------------------- */

static uint64_t const_of(int s)
{
    switch (s) {
    case CQ_FM_K_ZERO:     return UINT64_C(0);
    case CQ_FM_K_ONE:      return UINT64_C(1);
    case CQ_FM_K_7FF:      return CQ_FP64_EXP_ALL;
    case CQ_FM_K_BIAS:     return UINT64_C(1023);
    case CQ_FM_K_IMPLICIT: return CQ_FP64_IMPLICIT;
    case CQ_FM_K_INF:      return CQ_FP64_INF_BITS;
    case CQ_FM_K_QUIET:    return CQ_FP64_QUIET_BIT;
    case CQ_FM_K_INDEF:    return CQ_FP64_INDEF;
    default: break;
    }
    cq_kernel_die("fmul: unknown 64-lane operand code");
    return 0;
}

/* The four M32 blocks and M18's, rebuilt at a row's own offset. Their inputs
 * are NULL because every accessor below reads only `scr` and `off` — with the
 * ONE exception `cq_subnorm_flushed`, which is a VIEW over `result_sign` and
 * is handled by its own arm in `cq_fm_val64`. That asymmetry is M32's measured
 * finding 2 (bd a-discarded-arm-and-an-output-only-view-are-invisible-to-l1):
 * `flushed_result` is an operand of NOTHING, so driving the block with
 * `result_sign = NULL` completes normally and only the accessor sees it. */
#define FM_N52(k, o)  ((cq_norm52_block){ NULL, NULL, (k)->scr, (o) })
#define FM_CLZ(k, o)  ((cq_clz_block){ NULL, NULL, (k)->scr, (o) })
#define FM_SUB(k, o)  ((cq_subnorm_block){ NULL, NULL, NULL, (k)->scr, (o) })
#define FM_RND(k, o)  ((cq_round_block){ NULL, NULL, NULL, (k)->scr, (o) })

/* A projection of a hand-off's tuple, 64 lanes. `flushed_result` is assembled
 * into `buf`; every other output is a span inside the caller's region. */
static const cq_bit *out64(const cq_fmul_block *k, const uint32_t *off,
                           const cq_fmul_row *rows, int i, cq_bit *buf)
{
    const cq_fmul_row *r = &rows[i];
    uint32_t o = off[r->s0];
    int w = r->s1;

    switch (rows[r->s0].op) {
    case CQ_FMOP_NORM52: {
        cq_norm52_block b = FM_N52(k, o);

        return (w == CQ_FM_OUT_M) ? cq_norm52_m(&b) : cq_norm52_e(&b); }
    case CQ_FMOP_CLZ: {
        cq_clz_block b = FM_CLZ(k, o);

        return (w == CQ_FM_OUT_WR) ? cq_clz_wr(&b) : cq_clz_exp(&b); }
    case CQ_FMOP_SUBNORM: {
        cq_subnorm_block b = FM_SUB(k, o);

        if (w == CQ_FM_OUT_WR)  return cq_subnorm_wr(&b);
        if (w == CQ_FM_OUT_EXP) return cq_subnorm_exp(&b);
        /* `flushed_result = result_sign << 63` (softfloat_common.jl:183). The
         * accessor ASSEMBLES it, so the block needs its real third input. */
        {
            cq_bit s0[CQ_FM_W], s1[CQ_FM_W];

            b.result_sign = cq_fm_op64(k, off, rows[r->s0].s2, s0, s1);
            cq_subnorm_flushed(&b, buf);
            return buf;
        } }
    default: break;
    }
    {
        cq_round_block b = FM_RND(k, o);

        return (w == CQ_FM_OUT_NORMAL) ? cq_round_normal(&b)
                                       : cq_round_overflow_result(&b);
    }
}

/* Row `i`'s 64-lane value: a view, a projection, or a block's own output span.
 * Every 64-lane block puts its result at the front of its region except `sub`,
 * whose `d` lies between `nb` and `c` (add.h), and `mul`, whose product is
 * `accum` and is read through cq_mul_product (mul.h) rather than spelled. */
const cq_bit *cq_fm_val64(const cq_fmul_block *k, const uint32_t *off, int i,
                          cq_bit *buf)
{
    const cq_fmul_row *rows;
    int n;

    rows = cq_fmul_rows(&n);
    if (i < 0 || i >= n)
        cq_kernel_die("fmul: an operand names a row outside the program");
    if (cq_fmul_row_width(rows, n, i) != W64)
        cq_kernel_die("fmul: a 64-lane operand names a row of another width");

    if (rows[i].op == CQ_FMOP_OUT) return out64(k, off, rows, i, buf);
    if (rows[i].op == CQ_FMOP_SUB)
        return cq_fm_sp(k, off[i] + (uint32_t)W64, (uint32_t)W64);
    if (rows[i].op == CQ_FMOP_MUL) {
        cq_mul_block b;

        b.a = NULL; b.b = NULL; b.scr = k->scr; b.off = off[i]; b.W = W64;
        return cq_mul_product(&b);
    }
    return cq_fm_sp(k, off[i], (uint32_t)W64);
}

/* Row `i`'s Bool, as a one-bit span inside the caller's region. Read through
 * the owning module's accessor wherever there is one — `cq_eq_flag` is
 * `orr[W-2]` in general and `diff[0]` at W == 1, and a consumer spelling that
 * rule inline reads `orr[-1]` at the bottom of a ladder (cmp.h).
 *
 * A 1-BIT OPERAND MUST BE A ROW OF THIS PROGRAM AND MUST BE A BOOL ROW, AND
 * NOTHING BELOW M34 KNOWS EITHER. `cq_fm_val64` refuses a code it does not
 * recognise; these are the mirror refusals — a `not1`, `and1`, `or1` or `mux`
 * cond handed one of the negative codes would index the row table out of
 * bounds and hand the emitter whatever it read, and one handed a 64-lane row
 * would read a wire that is not a flag. Release has no other detector: the
 * read succeeds, the gate is plausible and the value is wrong. */
const cq_bit *cq_fm_flag_of(const cq_fmul_block *k, const uint32_t *off, int i)
{
    const cq_fmul_row *rows;
    uint32_t o;
    int n;

    rows = cq_fmul_rows(&n);
    if (i < 0 || i >= n)
        cq_kernel_die("fmul: a one-bit operand is not a row of this program");
    if (cq_fmul_row_width(rows, n, i) != 1)
        cq_kernel_die("fmul: a one-bit operand names a row of another width");

    o = off[i];
    if (rows[i].op == CQ_FMOP_CLASS) {
        cq_fp_class_block c;

        c.a = NULL; c.scr = k->scr; c.off = o; c.cls = (cq_fp_class)rows[i].s1;
        return cq_fp_class_flag(&c);
    }
    if (rows[i].op == CQ_FMOP_OUT) {
        int w = rows[i].s1;
        uint32_t po = off[rows[i].s0];

        if (rows[rows[i].s0].op == CQ_FMOP_SUBNORM) {
            cq_subnorm_block b = FM_SUB(k, po);

            return (w == CQ_FM_OUT_SUBNORMAL) ? cq_subnorm_flag(&b)
                                              : cq_subnorm_ftz(&b);
        }
        {
            cq_round_block b = FM_RND(k, po);

            return (w == CQ_FM_OUT_EXPOVF) ? cq_round_exp_overflow(&b)
                                           : cq_round_exp_overflow_aft(&b);
        }
    }
    if (rows[i].op == CQ_FMOP_EQ) {
        cq_eq_block e;

        e.a = NULL; e.b = NULL; e.W = W64;
        e.diff = cq_fm_sp(k, o, (uint32_t)W64);
        e.orr  = cq_fm_sp(k, o + (uint32_t)W64, (uint32_t)W64 - 1u);
        return cq_eq_flag(&e);
    }
    /* `carry[W]` IS `a >=u b` (cmp.h), and carry starts one vector in. */
    if (rows[i].op == CQ_FMOP_ULT) return cq_fm_sp(k, o + (uint32_t)(2 * W64), 1u);
    return cq_fm_sp(k, o, 1u);            /* not1, and1, or1 sit at rel 0 */
}

/* `(v >> s)`, with a NEGATIVE `s` meaning a left shift and |s| >= 64 giving 0
 * rather than C's undefined behaviour. */
static uint64_t shr64(uint64_t v, int s)
{
    if (s >= 64 || s <= -64) return UINT64_C(0);
    if (s >= 0) return v >> (unsigned)s;
    return v << (unsigned)(-s);
}

int cq_fm_view_collapse(const cq_fmul_block *k, int s, int *shift,
                        uint64_t *mask)
{
    const cq_fmul_row *rows;
    int guard = 0, n;

    (void)k;
    rows = cq_fmul_rows(&n);
    *shift = 0;
    *mask  = ~UINT64_C(0);
    while (s >= 0 && s < n && rows[s].op == CQ_FMOP_VIEW) {
        *mask   = shr64(rows[s].mask, *shift) & *mask;
        *shift += rows[s].shift;
        s       = rows[s].s0;
        if (++guard > n) cq_kernel_die("fmul: a view chain does not terminate");
    }
    return s;
}

void cq_fm_view_fill(const cq_bit *base, int shift, uint64_t mask, cq_bit *out)
{
    for (int i = 0; i < W64; i++) {
        int j = i + shift;

        out[i] = (((mask >> (unsigned)i) & UINT64_C(1)) != 0u && j >= 0 && j < W64)
               ? base[j] : cq_bit_zero();
    }
}

/* A NON-view 64-lane operand: a rail, a constant span, or a row's value. */
static const cq_bit *base64(const cq_fmul_block *k, const uint32_t *off, int s,
                            cq_bit *buf)
{
    if (s >= 0) return cq_fm_val64(k, off, s, buf);
    if (s == CQ_FM_A) {
        if (k->a == NULL) cq_kernel_die("fmul: the block has no operand `a`");
        return k->a;
    }
    if (s == CQ_FM_B) {
        if (k->b == NULL) cq_kernel_die("fmul: the block has no operand `b`");
        return k->b;
    }
    cq_fp_const(buf, const_of(s));
    return buf;
}

const cq_bit *cq_fm_op64(const cq_fmul_block *k, const uint32_t *off, int s,
                         cq_bit *buf, cq_bit *tmp)
{
    const cq_fmul_row *rows;
    int n;

    rows = cq_fmul_rows(&n);
    if (s >= 0 && s < n && rows[s].op == CQ_FMOP_VIEW) {
        int shift;
        uint64_t mask;
        int b = cq_fm_view_collapse(k, s, &shift, &mask);

        cq_fm_view_fill(base64(k, off, b, tmp), shift, mask, buf);
        return buf;
    }
    return base64(k, off, s, buf);
}
