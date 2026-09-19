/* src/kernels/fma_operand.c — M39, K20. THE OPERAND RESOLUTION: what each
 * operand code resolves to. The costs and the layout are next door in
 * fma_step.c and the dispatch in fma_emit.c, on the seams fma_int.h records.
 *
 * M33's fourth seam, COSTS-AND-LAYOUT <-> OPERAND RESOLUTION, taken here for
 * M33's own measured reason: resolution grows with the number of DISTINCT ops
 * and with how many of them resolve something that is not a span, while the
 * cost side is one switch per op and stays flat. K20 has 21 ops, view chains,
 * constant spans, 1-bit flags, 64-lane block outputs, barrel results and
 * three destructured tuples reached through projection rows.
 *
 * A VIEW CHAIN COLLAPSES TO ONE (shift, mask) PAIR, WHICH IS WHY VIEWS COST
 * NOTHING EVEN WHEN THEY NEST — M32's finding, and K20 nests them three deep
 * in `_shr128jam_by1` and in the CLZ ladder. The composition is
 *
 *     (((base >> s1) & m1) >> s2) & m2 == (base >> (s1+s2)) & ((m1 >> s2) & m2)
 *
 * with a NEGATIVE shift meaning a left shift, and it is walked OUTERMOST-IN so
 * the accumulated shift is applied to each row's mask before that row's own
 * shift joins it.
 *
 * THE VIEWS AND THE CONSTANT SPANS ARE REBUILT PER STEP, ON PURPOSE —
 * fpclass.c's, fcmp_step.c's, fpround_step.c's and fmul_step.c's reason,
 * verbatim: a block carrying cached operands would carry state whose
 * initialisation a consumer can forget, and a forgotten bind is a silent wrong
 * circuit rather than a failure. It is also what the sandwich needs: the
 * driver replays indices in reverse and a table rebuilt from mutable state
 * would desynchronise the two halves.
 *
 * I6(a) IS SATISFIED WITH NOTHING LEFT TO CHECK. `cq_fu_sp` is the only thing
 * that hands back a WRITABLE pointer and every one of them is a bit of the
 * caller's region at `off + <span>`. The three rails, the views over them, the
 * constant spans and every block output come back `const`, so a source cannot
 * be materialised by construction.
 */

#include "kernels/fma_int.h"

#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/fpclass.h"
#include "kernels/fpround.h"
#include "kernels/kernel.h"
#include "kernels/mul.h"
#include "kernels/mux.h"
#include "kernels/shift_var.h"

enum { W64 = CQ_FU_W };

static uint64_t const_of(int s)
{
    switch (s) {
    case CQ_FU_K_ZERO:     return UINT64_C(0);
    case CQ_FU_K_ONE:      return UINT64_C(1);
    case CQ_FU_K_ONES:     return ~UINT64_C(0);
    case CQ_FU_K_2:        return UINT64_C(2);
    case CQ_FU_K_4:        return UINT64_C(4);
    case CQ_FU_K_8:        return UINT64_C(8);
    case CQ_FU_K_16:       return UINT64_C(16);
    case CQ_FU_K_32:       return UINT64_C(32);
    case CQ_FU_K_63:       return UINT64_C(63);
    case CQ_FU_K_64:       return UINT64_C(64);
    case CQ_FU_K_127:      return UINT64_C(127);
    case CQ_FU_K_128:      return UINT64_C(128);
    case CQ_FU_K_3FE:      return UINT64_C(0x3FE);
    case CQ_FU_K_2P61:     return UINT64_C(0x2000000000000000);
    case CQ_FU_K_IMPLICIT: return CQ_FP64_IMPLICIT;
    case CQ_FU_K_QUIET:    return CQ_FP64_QUIET_BIT;
    case CQ_FU_K_INF:      return CQ_FP64_INF_BITS;
    case CQ_FU_K_INDEF:    return CQ_FP64_INDEF;
    default: break;
    }
    cq_kernel_die("fma: unknown 64-lane operand code");
    return 0;
}

/* The three M32 blocks, M18's and M12's, rebuilt at a row's own offset. Their
 * inputs are NULL because every accessor below reads only `scr` and `off` —
 * with the ONE exception `cq_subnorm_flushed`, which is a VIEW over
 * `result_sign` and is handled by its own arm in `cq_fu_val64`. That asymmetry
 * is M32's measured finding 2 (bd
 * a-discarded-arm-and-an-output-only-view-are-invisible-to-l1):
 * `flushed_result` is an operand of NOTHING, so driving the block with
 * `result_sign = NULL` completes normally and only the accessor sees it. */
#define FA_N52(k, o)  ((cq_norm52_block){ NULL, NULL, (k)->scr, (o) })
#define FA_SUB(k, o)  ((cq_subnorm_block){ NULL, NULL, NULL, (k)->scr, (o) })
#define FA_RND(k, o)  ((cq_round_block){ NULL, NULL, NULL, (k)->scr, (o) })

/* M16's two comparators, assembled from the spans this module lays out. The
 * flag is read through the owning module's accessor and never by the rule:
 * `cq_eq_flag` is `orr[W-2]` in general and `diff[0]` at W == 1, and
 * `cq_slt_flag` is `carry[W]` of an inner comparator whose base a consumer
 * must not re-derive (cmp.h). */
static cq_eq_block eq_at(const cq_fma_block *k, uint32_t o)
{
    cq_eq_block e;

    e.a = NULL; e.b = NULL; e.W = W64;
    e.diff = cq_fu_sp(k, o, (uint32_t)W64);
    e.orr  = cq_fu_sp(k, o + (uint32_t)W64, (uint32_t)W64 - 1u);
    return e;
}

static cq_slt_block slt_at(const cq_fma_block *k, uint32_t o)
{
    cq_slt_block c;

    c.a = NULL; c.b = NULL; c.W = W64;
    c.af    = cq_fu_sp(k, o,                            (uint32_t)W64);
    c.bf    = cq_fu_sp(k, o + (uint32_t)W64,            (uint32_t)W64);
    c.nb    = cq_fu_sp(k, o + (uint32_t)(2 * W64),      (uint32_t)W64);
    c.carry = cq_fu_sp(k, o + (uint32_t)(3 * W64),      (uint32_t)W64 + 1u);
    c.axnb  = cq_fu_sp(k, o + (uint32_t)(4 * W64) + 1u, (uint32_t)W64);
    return c;
}

/* A projection of a hand-off's tuple, 64 lanes. `flushed_result` is assembled
 * into `buf`; every other output is a span inside the caller's region. */
static const cq_bit *out64(const cq_fma_block *k, const uint32_t *off,
                           const cq_fma_row *rows, int i, cq_bit *buf)
{
    const cq_fma_row *r = &rows[i];
    uint32_t o = off[r->s0];
    int w = r->s1;

    if (rows[r->s0].op == CQ_FUOP_NORM52) {
        cq_norm52_block b = FA_N52(k, o);

        return (w == CQ_FU_OUT_M) ? cq_norm52_m(&b) : cq_norm52_e(&b);
    }
    if (rows[r->s0].op == CQ_FUOP_SUBNORM) {
        cq_subnorm_block b = FA_SUB(k, o);

        if (w == CQ_FU_OUT_WR)  return cq_subnorm_wr(&b);
        if (w == CQ_FU_OUT_EXP) return cq_subnorm_exp(&b);
        /* `flushed_result = result_sign << 63` (softfloat_common.jl:183). The
         * accessor ASSEMBLES it, so the block needs its real third input. */
        {
            cq_bit s0[CQ_FU_W], s1[CQ_FU_W];

            b.result_sign = cq_fu_op64(k, off, rows[r->s0].s2, s0, s1);
            cq_subnorm_flushed(&b, buf);
            return buf;
        }
    }
    {
        cq_round_block b = FA_RND(k, o);

        return (w == CQ_FU_OUT_NORMAL) ? cq_round_normal(&b)
                                       : cq_round_overflow_result(&b);
    }
}

/* Row `i`'s 64-lane value: a view, a projection, or a block's own output span.
 * Every 64-lane block puts its result at the front of its region except `sub`,
 * whose `d` lies between `nb` and `c` (add.h); `mul`, whose product is `accum`
 * and is read through cq_mul_product (mul.h); and the barrel, whose value is
 * `r_{S-1}` and is read through cq_barrel_result (shift_var.h, which says in
 * so many words not to re-derive that offset at a call site). */
const cq_bit *cq_fu_val64(const cq_fma_block *k, const uint32_t *off, int i,
                          cq_bit *buf)
{
    const cq_fma_row *rows;
    int n;

    rows = cq_fma_rows(&n);
    if (i < 0 || i >= n)
        cq_kernel_die("fma: an operand names a row outside the program");
    if (cq_fma_row_width(rows, n, i) != W64)
        cq_kernel_die("fma: a 64-lane operand names a row of another width");

    if (rows[i].op == CQ_FUOP_OUT) return out64(k, off, rows, i, buf);
    if (rows[i].op == CQ_FUOP_SUB)
        return cq_fu_sp(k, off[i] + (uint32_t)W64, (uint32_t)W64);
    if (rows[i].op == CQ_FUOP_MUL) {
        cq_mul_block b;

        b.a = NULL; b.b = NULL; b.scr = k->scr; b.off = off[i]; b.W = W64;
        return cq_mul_product(&b);
    }
    if (rows[i].op == CQ_FUOP_BSHL || rows[i].op == CQ_FUOP_BLSHR) {
        cq_barrel_block b;

        b.a = NULL; b.b = NULL; b.scr = k->scr; b.off = off[i]; b.W = W64;
        b.dir = (cq_barrel_dir)cq_fu_barrel_dir_of(rows[i].op);
        return cq_barrel_result(&b);
    }
    return cq_fu_sp(k, off[i], (uint32_t)W64);
}

/* Row `i`'s Bool, as a one-bit span inside the caller's region.
 *
 * A 1-BIT OPERAND MUST BE A ROW OF THIS PROGRAM AND MUST BE A BOOL ROW, AND
 * NOTHING BELOW M39 KNOWS EITHER. `cq_fu_val64` refuses a code it does not
 * recognise; these are the mirror refusals — a `not1`, `and1`, `or1` or `mux`
 * cond handed one of the negative codes would index the row table out of
 * bounds and hand the emitter whatever it read, and one handed a 64-lane row
 * would read a wire that is not a flag. Release has no other detector: the
 * read succeeds, the gate is plausible and the value is wrong. */
const cq_bit *cq_fu_flag_of(const cq_fma_block *k, const uint32_t *off, int i)
{
    const cq_fma_row *rows;
    uint32_t o;
    int n;

    rows = cq_fma_rows(&n);
    if (i < 0 || i >= n)
        cq_kernel_die("fma: a one-bit operand is not a row of this program");
    if (cq_fma_row_width(rows, n, i) != 1)
        cq_kernel_die("fma: a one-bit operand names a row of another width");

    o = off[i];
    if (rows[i].op == CQ_FUOP_CLASS) {
        cq_fp_class_block c;

        c.a = NULL; c.scr = k->scr; c.off = o; c.cls = (cq_fp_class)rows[i].s1;
        return cq_fp_class_flag(&c);
    }
    if (rows[i].op == CQ_FUOP_OUT) {
        int w = rows[i].s1;
        uint32_t po = off[rows[i].s0];

        if (rows[rows[i].s0].op == CQ_FUOP_SUBNORM) {
            cq_subnorm_block b = FA_SUB(k, po);

            return (w == CQ_FU_OUT_SUBNORMAL) ? cq_subnorm_flag(&b)
                                              : cq_subnorm_ftz(&b);
        }
        {
            cq_round_block b = FA_RND(k, po);

            return (w == CQ_FU_OUT_EXPOVF) ? cq_round_exp_overflow(&b)
                                           : cq_round_exp_overflow_aft(&b);
        }
    }
    if (rows[i].op == CQ_FUOP_EQ) {
        cq_eq_block e = eq_at(k, o);

        return cq_eq_flag(&e);
    }
    if (rows[i].op == CQ_FUOP_SLT) {
        cq_slt_block c = slt_at(k, o);

        return cq_slt_flag(&c);
    }
    /* `carry[W]` IS `a >=u b` (cmp.h), and carry starts one vector in. */
    if (rows[i].op == CQ_FUOP_ULT)
        return cq_fu_sp(k, o + (uint32_t)(2 * W64), 1u);
    return cq_fu_sp(k, o, 1u);            /* not1, and1, or1 sit at rel 0 */
}

/* `(v >> s)`, with a NEGATIVE `s` meaning a left shift and |s| >= 64 giving 0
 * rather than C's undefined behaviour. */
static uint64_t shr64(uint64_t v, int s)
{
    if (s >= 64 || s <= -64) return UINT64_C(0);
    if (s >= 0) return v >> (unsigned)s;
    return v << (unsigned)(-s);
}

int cq_fu_view_collapse(const cq_fma_block *k, int s, int *shift,
                        uint64_t *mask)
{
    const cq_fma_row *rows;
    int guard = 0, n;

    (void)k;
    rows = cq_fma_rows(&n);
    *shift = 0;
    *mask  = ~UINT64_C(0);
    while (s >= 0 && s < n && rows[s].op == CQ_FUOP_VIEW) {
        *mask   = shr64(rows[s].mask, *shift) & *mask;
        *shift += rows[s].shift;
        s       = rows[s].s0;
        if (++guard > n) cq_kernel_die("fma: a view chain does not terminate");
    }
    return s;
}

void cq_fu_view_fill(const cq_bit *base, int shift, uint64_t mask,
                     cq_bit *out)
{
    for (int i = 0; i < W64; i++) {
        int j = i + shift;

        out[i] = (((mask >> (unsigned)i) & UINT64_C(1)) != 0u && j >= 0 && j < W64)
               ? base[j] : cq_bit_zero();
    }
}

/* A NON-view 64-lane operand: a rail, a constant span, or a row's value. */
static const cq_bit *base64(const cq_fma_block *k, const uint32_t *off, int s,
                            cq_bit *buf)
{
    if (s >= 0) return cq_fu_val64(k, off, s, buf);
    if (s == CQ_FU_A) {
        if (k->a == NULL) cq_kernel_die("fma: the block has no operand `a`");
        return k->a;
    }
    if (s == CQ_FU_B) {
        if (k->b == NULL) cq_kernel_die("fma: the block has no operand `b`");
        return k->b;
    }
    if (s == CQ_FU_C) {
        if (k->c == NULL) cq_kernel_die("fma: the block has no operand `c`");
        return k->c;
    }
    cq_fp_const(buf, const_of(s));
    return buf;
}

const cq_bit *cq_fu_op64(const cq_fma_block *k, const uint32_t *off, int s,
                         cq_bit *buf, cq_bit *tmp)
{
    const cq_fma_row *rows;
    int n;

    rows = cq_fma_rows(&n);
    if (s >= 0 && s < n && rows[s].op == CQ_FUOP_VIEW) {
        int shift;
        uint64_t mask;
        int b = cq_fu_view_collapse(k, s, &shift, &mask);

        cq_fu_view_fill(base64(k, off, b, tmp), shift, mask, buf);
        return buf;
    }
    return base64(k, off, s, buf);
}
