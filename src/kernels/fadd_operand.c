/* src/kernels/fadd_operand.c — M33, K15. OPERAND RESOLUTION: what each operand
 * code of a row resolves to. The THIRD seam, COSTS-AND-LAYOUT <-> OPERAND
 * RESOLUTION, taken at implementation off fadd_step.c because the two halves
 * together measured 326 of Rule 12's 300 lines; fadd_int.h records it with the
 * other two.
 *
 * NOT ONE LINE HERE KNOWS ANY JULIA, and not one knows what a row COSTS.
 *
 * A VIEW CHAIN COLLAPSES TO ONE (shift, mask) PAIR, WHICH IS WHY VIEWS COST
 * NOTHING EVEN WHEN THEY NEST — M32's mechanism, verbatim. `ea` (fadd.jl:21)
 * is a view over `a >> 52`: two rows, two operator occurrences, ONE addressing
 * computation. The composition is
 *
 *     (((base >> s1) & m1) >> s2) & m2 == (base >> (s1+s2)) & ((m1 >> s2) & m2)
 *
 * with a NEGATIVE shift meaning a left shift, and it is walked OUTERMOST-IN so
 * the accumulated shift is applied to each row's mask before that row's own
 * shift joins it.
 *
 * A PICK IS RESOLVED BY ASKING M32, NEVER BY SPELLING ITS LAYOUT. `cq_clz_wr`,
 * `cq_subnorm_ftz`, `cq_round_normal` and the rest are pure addressing inside
 * the caller's region, and a consumer that wrote "the wr output is at
 * off + 39 * something" would be transcribing fpround.c's own out-row table a
 * second time — which is exactly the number a boundary error moves. The one
 * output that is a VIEW rather than a span, `flushed_result`, comes back
 * through `cq_subnorm_flushed`, which FILLS a caller array (D-K23-8) — and a
 * consumer that treats it as an ordinary all-quantum scratch span has 63 lanes
 * wrong, which K15.md §0.4 records as measured.
 *
 * THE VIEWS AND THE CONSTANT SPANS ARE REBUILT PER STEP, ON PURPOSE —
 * fpclass.c's and fcmp_step.c's reason, verbatim: a block carrying cached
 * operands would carry state whose initialisation a consumer can forget, and a
 * forgotten bind is a silent wrong circuit rather than a failure.
 *
 * I6(a) IS SATISFIED WITH NOTHING LEFT TO CHECK. `cq_fa_row_out` is the only
 * thing here that hands back a WRITABLE pointer, and it is always a bit of the
 * caller's region. The two rails, the views over them, the constant spans and
 * every M32 output come back `const`, so a source cannot be materialised by
 * construction.
 */

#include "kernels/fadd_int.h"

#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/fpclass.h"
#include "kernels/kernel.h"
#include "kernels/shift_var.h"

enum { W64 = CQ_FA_W };

/* --- Operand resolution. -------------------------------------------------- */

/* fadd.jl:17's `SIGN_MASK = UInt64(0x8000000000000000)` — a local constant
 * because it is the ONE of upstream's IEEE constants that softfloat_common.jl
 * does not export and M31 therefore does not carry (fpfield.h has the other
 * seven). fneg.jl:6 is its only other use. */
#define FA_SIGN_MASK  UINT64_C(0x8000000000000000)

_Static_assert(FA_SIGN_MASK == ~(UINT64_C(0x7FFFFFFFFFFFFFFF)),
               "SIGN_MASK and ~SIGN_MASK are complements; fadd.jl:47-48 masks "
               "with the second and fneg.jl:6 xors with the first");

static uint64_t const_of(int s)
{
    switch (s) {
    case CQ_FA_K_ZERO:     return UINT64_C(0);
    case CQ_FA_K_ONE:      return UINT64_C(1);
    case CQ_FA_K_56:       return UINT64_C(56);
    case CQ_FA_K_63:       return UINT64_C(63);
    case CQ_FA_K_64:       return UINT64_C(64);
    case CQ_FA_K_7FF:      return CQ_FP64_EXP_ALL;
    case CQ_FA_K_IMPLICIT: return CQ_FP64_IMPLICIT;
    case CQ_FA_K_INDEF:    return CQ_FP64_INDEF;
    case CQ_FA_K_QUIET:    return CQ_FP64_QUIET_BIT;
    case CQ_FA_K_SIGN:     return FA_SIGN_MASK;
    default: break;
    }
    cq_kernel_die("fadd: unknown 64-lane operand code");
    return 0;
}

/* A block's own output span. Every 64-lane row puts its result at the front of
 * its region except `sub`, whose `d` lies between `nb` and `c` (add.h), and
 * the barrel, whose running value is the LAST stage's mux output and is only
 * findable through cq_barrel_result (shift_var.h). */
cq_bit *cq_fa_row_out(const cq_fa_ctx *x, const uint32_t *off, int i)
{
    uint32_t o = off[i];
    int op = x->rows[i].op;

    if (op == CQ_FAOP_SUB)
        return cq_fa_sp(x, o + (uint32_t)W64, (uint32_t)W64);
    if (op == CQ_FAOP_BSHL || op == CQ_FAOP_BLSHR) {
        cq_barrel_block b;

        b.a = NULL; b.b = NULL; b.scr = x->scr; b.off = o; b.W = W64;
        b.dir = (op == CQ_FAOP_BSHL) ? CQ_BARREL_SHL : CQ_BARREL_LSHR;
        return cq_barrel_result(&b);
    }
    return cq_fa_sp(x, o, (uint32_t)W64);
}

/* --- The three M32 hand-offs. -------------------------------------------- */

cq_clz_block cq_fa_clz_of(const cq_fa_ctx *x, const uint32_t *off, int i,
                          cq_fa_bufs *bf)
{
    cq_clz_block k;

    k.wr         = cq_fa_op64(x, off, x->rows[i].s0, bf->v[0], bf->v[2]);
    k.result_exp = cq_fa_op64(x, off, x->rows[i].s1, bf->v[1], bf->v[3]);
    k.scr = x->scr; k.off = off[i];
    return k;
}

cq_subnorm_block cq_fa_subnorm_of(const cq_fa_ctx *x, const uint32_t *off,
                                  int i, cq_fa_bufs *bf)
{
    cq_subnorm_block k;

    k.wr          = cq_fa_op64(x, off, x->rows[i].s0, bf->v[0], bf->v[3]);
    k.result_exp  = cq_fa_op64(x, off, x->rows[i].s1, bf->v[1], bf->v[3]);
    k.result_sign = cq_fa_op64(x, off, x->rows[i].s2, bf->v[2], bf->v[3]);
    k.scr = x->scr; k.off = off[i];
    return k;
}

cq_round_block cq_fa_round_of(const cq_fa_ctx *x, const uint32_t *off, int i,
                              cq_fa_bufs *bf)
{
    cq_round_block k;

    k.wr          = cq_fa_op64(x, off, x->rows[i].s0, bf->v[0], bf->v[3]);
    k.result_exp  = cq_fa_op64(x, off, x->rows[i].s1, bf->v[1], bf->v[3]);
    k.result_sign = cq_fa_op64(x, off, x->rows[i].s2, bf->v[2], bf->v[3]);
    k.scr = x->scr; k.off = off[i];
    return k;
}

/* Row `i`'s Bool, as a one-bit span inside the caller's region. Read through
 * the owning module's accessor wherever there is one — `cq_eq_flag` is
 * `orr[W-2]` in general and `diff[0]` at W == 1, and a consumer spelling that
 * rule inline reads `orr[-1]` at the bottom of the ladder (cmp.h).
 *
 * A 1-BIT OPERAND MUST BE A ROW OF THIS PROGRAM AND MUST BE A BOOL ROW, AND
 * NOTHING BELOW M33 KNOWS EITHER. `cq_fa_op64` refuses a code it does not
 * recognise; these are the mirror refusals — a `not1`, `and1`, `or1` or `mux`
 * cond handed one of the negative codes would index the row table out of
 * bounds, and one handed a 64-lane row would read a wire that is not a flag.
 * Release has no other detector: the read succeeds, the gate is plausible and
 * the value is wrong. */
const cq_bit *cq_fa_flag_of(const cq_fa_ctx *x, const uint32_t *off, int i)
{
    const cq_fadd_row *r;
    uint32_t o;

    if (i < 0 || i >= x->n)
        cq_kernel_die("fadd: a one-bit operand is not a row of this program");
    if (cq_fa_op_width(x, i) != 1)
        cq_kernel_die("fadd: a one-bit operand names a 64-lane row");

    r = &x->rows[i];
    o = off[i];

    if (r->op == CQ_FAOP_PICK) {
        cq_fa_bufs bf;
        int b = r->s0;

        if (x->rows[b].op == CQ_FAOP_SUBNORM) {
            cq_subnorm_block s = cq_fa_subnorm_of(x, off, b, &bf);

            return (r->shift == CQ_FA_PICK_FLAG) ? cq_subnorm_flag(&s)
                                                 : cq_subnorm_ftz(&s);
        }
        {
            cq_round_block rb = cq_fa_round_of(x, off, b, &bf);

            return (r->shift == CQ_FA_PICK_EXPOVF)
                 ? cq_round_exp_overflow(&rb) : cq_round_exp_overflow_aft(&rb);
        }
    }
    if (r->op == CQ_FAOP_CLASS) {
        cq_fp_class_block c;
        cq_bit buf[CQ_FP64_W], tmp[CQ_FP64_W];

        c.a = cq_fa_op64(x, off, r->s0, buf, tmp);
        c.scr = x->scr; c.off = o; c.cls = cq_fa_class_of(r);
        return cq_fp_class_flag(&c);
    }
    if (r->op == CQ_FAOP_EQ) {
        cq_eq_block e;

        e.a = NULL; e.b = NULL; e.W = W64;
        e.diff = cq_fa_sp(x, o, (uint32_t)W64);
        e.orr  = cq_fa_sp(x, o + (uint32_t)W64, (uint32_t)W64 - 1u);
        return cq_eq_flag(&e);
    }
    /* `carry[W]` IS `a >=u b` (cmp.h), and carry starts one vector in. */
    if (r->op == CQ_FAOP_ULT) return cq_fa_sp(x, o + (uint32_t)(2 * W64), 1u);
    return cq_fa_sp(x, o, 1u);               /* not1, and1, or1 sit at rel 0 */
}

/* `(v >> s)`, with a NEGATIVE `s` meaning a left shift and |s| >= 64 giving 0
 * rather than C's undefined behaviour. */
static uint64_t shr64(uint64_t v, int s)
{
    if (s >= 64 || s <= -64) return UINT64_C(0);
    if (s >= 0) return v >> (unsigned)s;
    return v << (unsigned)(-s);
}

/* Walk a chain of VIEW rows outermost-in, accumulating one (shift, mask), and
 * return the first non-view operand. */
static int view_collapse(const cq_fa_ctx *x, int s, int *shift, uint64_t *mask)
{
    int guard = 0;

    *shift = 0;
    *mask  = ~UINT64_C(0);
    while (s >= 0 && s < x->n && x->rows[s].op == CQ_FAOP_VIEW) {
        *mask   = shr64(x->rows[s].mask, *shift) & *mask;
        *shift += x->rows[s].shift;
        s       = x->rows[s].s0;
        if (++guard > x->n)
            cq_kernel_die("fadd: a view chain does not terminate");
    }
    return s;
}

static void view_fill(const cq_bit *base, int shift, uint64_t mask, cq_bit *out)
{
    for (int i = 0; i < W64; i++) {
        int j = i + shift;

        out[i] = (((mask >> (unsigned)i) & UINT64_C(1)) != 0u && j >= 0 && j < W64)
               ? base[j] : cq_bit_zero();
    }
}

/* A PICK of a 64-lane output. `flushed_result` is the one output that is a
 * VIEW over an operand rather than a span, so M32 ASSEMBLES it (D-K23-8). */
static const cq_bit *pick64(const cq_fa_ctx *x, const uint32_t *off, int i,
                            cq_bit *buf)
{
    const cq_fadd_row *r = &x->rows[i];
    cq_fa_bufs bf;
    int b = r->s0;

    if (b < 0 || b >= x->n)
        cq_kernel_die("fadd: a pick names a row outside the program");

    if (x->rows[b].op == CQ_FAOP_CLZ) {
        cq_clz_block k = cq_fa_clz_of(x, off, b, &bf);

        return (r->shift == CQ_FA_PICK_WR) ? cq_clz_wr(&k) : cq_clz_exp(&k);
    }
    if (x->rows[b].op == CQ_FAOP_SUBNORM) {
        cq_subnorm_block k = cq_fa_subnorm_of(x, off, b, &bf);

        if (r->shift == CQ_FA_PICK_FLUSHED) {
            cq_subnorm_flushed(&k, buf);
            return buf;
        }
        return (r->shift == CQ_FA_PICK_WR) ? cq_subnorm_wr(&k)
                                           : cq_subnorm_exp(&k);
    }
    if (x->rows[b].op == CQ_FAOP_ROUND) {
        cq_round_block k = cq_fa_round_of(x, off, b, &bf);

        return (r->shift == CQ_FA_PICK_NORMAL) ? cq_round_normal(&k)
                                               : cq_round_overflow_result(&k);
    }
    cq_kernel_die("fadd: a pick names a row that returns no tuple");
    return NULL;
}

/* A NON-view 64-lane operand: one of the two rails, a constant span, a picked
 * output, or an emitting row's own output. */
static const cq_bit *base64(const cq_fa_ctx *x, const uint32_t *off, int s,
                            cq_bit *buf)
{
    if (s >= 0) {
        if (s >= x->n)
            cq_kernel_die("fadd: an operand names a row outside the program");
        if (cq_fa_op_width(x, s) != W64)
            cq_kernel_die("fadd: a 64-lane operand names a one-bit row");
        if (x->rows[s].op == CQ_FAOP_PICK) return pick64(x, off, s, buf);
        if (x->rows[s].op == CQ_FAOP_CLZ || x->rows[s].op == CQ_FAOP_SUBNORM
            || x->rows[s].op == CQ_FAOP_ROUND)
            cq_kernel_die("fadd: a hand-off row is read through a pick, never "
                          "directly");
        return cq_fa_row_out(x, off, s);
    }
    if (s == CQ_FA_A || s == CQ_FA_B) {
        const cq_bit *p = (s == CQ_FA_A) ? x->a : x->b;

        if (p == NULL) cq_kernel_die("fadd: this block has no such input");
        return p;
    }
    cq_fp_const(buf, const_of(s));
    return buf;
}

const cq_bit *cq_fa_op64(const cq_fa_ctx *x, const uint32_t *off, int s,
                         cq_bit *buf, cq_bit *tmp)
{
    if (s >= 0 && s < x->n && x->rows[s].op == CQ_FAOP_VIEW) {
        int shift;
        uint64_t mask;
        int b = view_collapse(x, s, &shift, &mask);

        view_fill(base64(x, off, b, tmp), shift, mask, buf);
        return buf;
    }
    return base64(x, off, s, buf);
}
