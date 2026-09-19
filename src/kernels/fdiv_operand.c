/* src/kernels/fdiv_operand.c — M35, K17. OPERAND RESOLUTION: what each operand
 * code resolves to. The costs and the layout are next door in fdiv_step.c and
 * the dispatch in fdiv_emit.c, on the seams fdiv_int.h records.
 *
 * THE FOURTH SEAM, AND IT IS M33's RATHER THAN A LINE COUNT (bd a-row-table-fp-
 * kernel-needs-three-seams-not-two): "what drives it is the OP VOCABULARY, not
 * the row count … operand RESOLUTION grows with the number of distinct ops and
 * with how many of them resolve something that is not a span". M35 has view
 * CHAINS, constant spans, 1-bit flags, 64-lane block outputs and FOUR
 * destructured tuples reached through projection rows; the cost side, by
 * contrast, is one switch per op and stays flat.
 *
 * A VIEW CHAIN COLLAPSES TO ONE (shift, mask) PAIR, WHICH IS WHY VIEWS COST
 * NOTHING EVEN WHEN THEY NEST. The composition is
 *
 *     (((base >> s1) & m1) >> s2) & m2 == (base >> (s1+s2)) & ((m1 >> s2) & m2)
 *
 * with a NEGATIVE shift meaning a left shift, walked OUTERMOST-IN so the
 * accumulated shift is applied to each row's mask before that row's own shift
 * joins it. In K17 the chains are at most two deep — `(a >> 52) & 0x7FF` and
 * the loop's `r << 1` over the previous iteration's mux output — but the walk
 * is the general one because `r_in(t)` is a view over a row an EARLIER
 * ITERATION wrote, which is plan §0.4 obligations 3 and 4 and K12 §2.1a.
 *
 * `flushed_result` IS AN ASSEMBLED VIEW AND NOT A SPAN, AND MODELLING IT AS A
 * SPAN IS WRONG ON 63 OF 64 LANES (bd a-picked-output-can-itself-be-a-view,
 * measured on M33). `_sf_handle_subnormal` returns `result_sign << 63`
 * (softfloat_common.jl:183), an operand of NOTHING inside M32, so it has no
 * home in the caller's region and `cq_subnorm_flushed` ASSEMBLES it into a
 * caller array. The library side is right by construction because it calls the
 * accessor; what has to know is any INDEPENDENT lane model, which is the test
 * side's problem and is recorded there too.
 *
 * THE VIEWS AND THE CONSTANT SPANS ARE REBUILT PER STEP, ON PURPOSE —
 * fpclass.c's, fcmp_step.c's, fpround_step.c's and fmul_step.c's reason,
 * verbatim: a block carrying cached operands would carry state whose
 * initialisation a consumer can forget, and a forgotten bind is a silent wrong
 * circuit rather than a failure.
 *
 * I6(a): everything this file hands back is `const`, so a source cannot be
 * materialised by construction. The one writable pointer in the module is
 * `cq_fd_sp`, next door.
 */

#include "kernels/fdiv_int.h"

#include "kernels/cmp.h"
#include "kernels/fpclass.h"
#include "kernels/fpround.h"
#include "kernels/kernel.h"

enum { W64 = CQ_FD_W };

/* The constant vocabulary. `0x7FF` is NOT here — in `soft_fdiv` it occurs only
 * as a view mask, because every `ea == 0x7FF` lives inside M31's class block
 * (fdiv.h). */
static uint64_t const_of(int s)
{
    switch (s) {
    case CQ_FD_K_ZERO:     return UINT64_C(0);
    case CQ_FD_K_ONE:      return UINT64_C(1);
    case CQ_FD_K_BIAS:     return UINT64_C(1023);
    case CQ_FD_K_IMPLICIT: return CQ_FP64_IMPLICIT;
    case CQ_FD_K_INF:      return CQ_FP64_INF_BITS;
    case CQ_FD_K_QUIET:    return CQ_FP64_QUIET_BIT;
    case CQ_FD_K_INDEF:    return CQ_FP64_INDEF;
    default: break;
    }
    cq_kernel_die("fdiv: unknown 64-lane operand code");
    return 0;
}

/* The four M32 blocks, rebuilt at a row's own offset. Their inputs are NULL
 * because every accessor below reads only `scr` and `off` — with the ONE
 * exception `cq_subnorm_flushed`, which is a VIEW over `result_sign` and is
 * handled by its own arm in `out64`. */
#define FD_N52(k, o)  ((cq_norm52_block){ NULL, NULL, (k)->scr, (o) })
#define FD_CLZ(k, o)  ((cq_clz_block){ NULL, NULL, (k)->scr, (o) })
#define FD_SUB(k, o)  ((cq_subnorm_block){ NULL, NULL, NULL, (k)->scr, (o) })
#define FD_RND(k, o)  ((cq_round_block){ NULL, NULL, NULL, (k)->scr, (o) })

/* A projection of a hand-off's tuple, 64 lanes. */
static const cq_bit *out64(const cq_fdiv_block *k, const cq_fd_map *m,
                           const cq_fdiv_row *r, cq_bit *buf)
{
    cq_fdiv_row p;
    uint32_t o = cq_fd_off(m, r->s0);
    int w = r->s1;

    cq_fdiv_row_at(r->s0, &p);
    switch (p.op) {
    case CQ_FDOP_NORM52: {
        cq_norm52_block b = FD_N52(k, o);

        return (w == CQ_FD_OUT_M) ? cq_norm52_m(&b) : cq_norm52_e(&b); }
    case CQ_FDOP_CLZ: {
        cq_clz_block b = FD_CLZ(k, o);

        return (w == CQ_FD_OUT_WR) ? cq_clz_wr(&b) : cq_clz_exp(&b); }
    case CQ_FDOP_SUBNORM: {
        cq_subnorm_block b = FD_SUB(k, o);

        if (w == CQ_FD_OUT_WR)  return cq_subnorm_wr(&b);
        if (w == CQ_FD_OUT_EXP) return cq_subnorm_exp(&b);
        {
            cq_bit s0[CQ_FD_W], s1[CQ_FD_W];

            b.result_sign = cq_fd_op64(k, m, p.s2, s0, s1);
            cq_subnorm_flushed(&b, buf);
            return buf;
        } }
    default: break;
    }
    {
        cq_round_block b = FD_RND(k, o);

        return (w == CQ_FD_OUT_NORMAL) ? cq_round_normal(&b)
                                       : cq_round_overflow_result(&b);
    }
}

/* Row `i`'s 64-lane value. Every 64-lane block puts its result at the front of
 * its region except `sub`, whose `d` lies between `nb` and `c` (add.h). */
const cq_bit *cq_fd_val64(const cq_fdiv_block *k, const cq_fd_map *m, int i,
                          cq_bit *buf)
{
    cq_fdiv_row r;

    if (i < 0 || i >= cq_fdiv_n_rows())
        cq_kernel_die("fdiv: an operand names a row outside the program");
    if (cq_fdiv_width_at(i) != W64)
        cq_kernel_die("fdiv: a 64-lane operand names a row of another width");

    cq_fdiv_row_at(i, &r);
    if (r.op == CQ_FDOP_OUT) return out64(k, m, &r, buf);
    if (r.op == CQ_FDOP_SUB)
        return cq_fd_sp(k, cq_fd_off(m, i) + (uint32_t)W64, (uint32_t)W64);
    return cq_fd_sp(k, cq_fd_off(m, i), (uint32_t)W64);
}

/* Row `i`'s Bool, as a one-bit span inside the caller's region. Read through
 * the owning module's accessor wherever there is one — `cq_eq_flag` is
 * `orr[W-2]` in general and `diff[0]` at W == 1, and a consumer spelling that
 * rule inline reads `orr[-1]` at the bottom of a ladder (cmp.h).
 *
 * A 1-BIT OPERAND MUST BE A ROW OF THIS PROGRAM AND MUST BE A BOOL ROW, AND
 * NOTHING BELOW M35 KNOWS EITHER. `cq_fd_val64` refuses a code it does not
 * recognise; this is the mirror refusal, and it is the one that gets forgotten
 * (bd a-table-driven-kernel-needs-both-operand-refusals). Release has no other
 * detector: the read succeeds, the gate is plausible and the value is wrong. */
const cq_bit *cq_fd_flag_of(const cq_fdiv_block *k, const cq_fd_map *m, int i)
{
    cq_fdiv_row r;
    uint32_t o;

    if (i < 0 || i >= cq_fdiv_n_rows())
        cq_kernel_die("fdiv: a one-bit operand is not a row of this program");
    if (cq_fdiv_width_at(i) != 1)
        cq_kernel_die("fdiv: a one-bit operand names a row of another width");

    cq_fdiv_row_at(i, &r);
    o = cq_fd_off(m, i);
    if (r.op == CQ_FDOP_CLASS) {
        cq_fp_class_block c;

        c.a = NULL; c.scr = k->scr; c.off = o; c.cls = (cq_fp_class)r.s1;
        return cq_fp_class_flag(&c);
    }
    if (r.op == CQ_FDOP_OUT) {
        cq_fdiv_row p;
        uint32_t po = cq_fd_off(m, r.s0);

        cq_fdiv_row_at(r.s0, &p);
        if (p.op == CQ_FDOP_SUBNORM) {
            cq_subnorm_block b = FD_SUB(k, po);

            return (r.s1 == CQ_FD_OUT_SUBNORMAL) ? cq_subnorm_flag(&b)
                                                 : cq_subnorm_ftz(&b);
        }
        {
            cq_round_block b = FD_RND(k, po);

            return (r.s1 == CQ_FD_OUT_EXPOVF) ? cq_round_exp_overflow(&b)
                                              : cq_round_exp_overflow_aft(&b);
        }
    }
    if (r.op == CQ_FDOP_EQ) {
        cq_eq_block e;

        e.a = NULL; e.b = NULL; e.W = W64;
        e.diff = cq_fd_sp(k, o, (uint32_t)W64);
        e.orr  = cq_fd_sp(k, o + (uint32_t)W64, (uint32_t)W64 - 1u);
        return cq_eq_flag(&e);
    }
    /* `carry[W]` IS `a >=u b` (cmp.h), and carry starts one vector in. THE
     * LOOP READS IT UNNEGATED, because fdiv.jl:93 says `>=`. */
    if (r.op == CQ_FDOP_ULT)
        return cq_fd_sp(k, o + (uint32_t)(2 * W64), 1u);
    return cq_fd_sp(k, o, 1u);            /* not1, and1, or1 sit at rel 0 */
}

/* `(v >> s)`, with a NEGATIVE `s` meaning a left shift and |s| >= 64 giving 0
 * rather than C's undefined behaviour. */
static uint64_t shr64(uint64_t v, int s)
{
    if (s >= 64 || s <= -64) return UINT64_C(0);
    if (s >= 0) return v >> (unsigned)s;
    return v << (unsigned)(-s);
}

int cq_fd_view_collapse(int s, int *shift, uint64_t *mask)
{
    int n = cq_fdiv_n_rows(), guard = 0;

    *shift = 0;
    *mask  = ~UINT64_C(0);
    while (s >= 0 && s < n) {
        cq_fdiv_row r;

        cq_fdiv_row_at(s, &r);
        if (r.op != CQ_FDOP_VIEW) break;
        *mask   = shr64(r.mask, *shift) & *mask;
        *shift += r.shift;
        s       = r.s0;
        if (++guard > n) cq_kernel_die("fdiv: a view chain does not terminate");
    }
    return s;
}

void cq_fd_view_fill(const cq_bit *base, int shift, uint64_t mask, cq_bit *out)
{
    for (int i = 0; i < W64; i++) {
        int j = i + shift;

        out[i] = (((mask >> (unsigned)i) & UINT64_C(1)) != 0u && j >= 0 && j < W64)
               ? base[j] : cq_bit_zero();
    }
}

/* A NON-view 64-lane operand: a rail, a constant span, or a row's value. */
static const cq_bit *base64(const cq_fdiv_block *k, const cq_fd_map *m, int s,
                            cq_bit *buf)
{
    if (s >= 0) return cq_fd_val64(k, m, s, buf);
    if (s == CQ_FD_A) {
        if (k->a == NULL) cq_kernel_die("fdiv: the block has no operand `a`");
        return k->a;
    }
    if (s == CQ_FD_B) {
        if (k->b == NULL) cq_kernel_die("fdiv: the block has no operand `b`");
        return k->b;
    }
    cq_fp_const(buf, const_of(s));
    return buf;
}

const cq_bit *cq_fd_op64(const cq_fdiv_block *k, const cq_fd_map *m, int s,
                         cq_bit *buf, cq_bit *tmp)
{
    if (s >= 0 && s < cq_fdiv_n_rows()) {
        cq_fdiv_row r;

        cq_fdiv_row_at(s, &r);
        if (r.op == CQ_FDOP_VIEW) {
            int shift;
            uint64_t mask;
            int b = cq_fd_view_collapse(s, &shift, &mask);

            cq_fd_view_fill(base64(k, m, b, tmp), shift, mask, buf);
            return buf;
        }
    }
    return base64(k, m, s, buf);
}
