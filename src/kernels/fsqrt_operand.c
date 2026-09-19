/* src/kernels/fsqrt_operand.c — M40, K21. OPERAND RESOLUTION: what each
 * operand code resolves to. The costs and the layout are next door in
 * fsqrt_step.c and the dispatch in fsqrt_emit.c, on the seams fsqrt_int.h
 * records; the row table is in fsqrt.c.
 *
 * NOT ONE LINE HERE KNOWS ANY JULIA EITHER, with one exception it states
 * rather than hides: `const_of` carries the seven constant PATTERNS, which are
 * `softfloat_common.jl:8-14` and `fsqrt.jl`'s literals. They are here because
 * a constant is a SOURCE (PRD-v2 §7.3) and a source needs a value.
 *
 * A VIEW CHAIN COLLAPSES TO ONE (shift, mask) PAIR, WHICH IS WHY VIEWS COST
 * NOTHING EVEN WHEN THEY NEST — M32's finding, and K21 nests them SIXTY-FOUR
 * deep: `a_lo` at iteration t is `a_lo << 2` applied t times over
 * `(ma_adj & 0x3F) << 58`. The composition is
 *
 *     (((base >> s1) & m1) >> s2) & m2 == (base >> (s1+s2)) & ((m1 >> s2) & m2)
 *
 * with a NEGATIVE shift meaning a left shift, walked OUTERMOST-IN so the
 * accumulated shift is applied to each row's mask before that row's own shift
 * joins it. `shr64` gives ZERO for |shift| >= 64 rather than C's undefined
 * behaviour, which is exactly what makes `a_lo` come out ALL CONSTANT ZERO
 * from iteration 32 onward — the radicand being consumed, which is what the
 * loop is for. A port that "fixed" that by making `a_lo` a real register per
 * iteration would buy a uniform gate count for 4,096 extra qubits and would
 * not make the value any less provably zero; it would only hide the zero from
 * the fold table, and it would be a re-derivation, because the source spells a
 * constant shift and rule 2 makes a constant shift a view.
 *
 * AN ARITHMETIC-SHIFT VIEW TERMINATES A CHAIN RATHER THAN JOINING ONE. Its
 * vacated lanes are COPIES OF ITS OPERAND'S TOP LANE, not literal
 * `CQ_BIT_ZERO`, so the `(shift, mask)` algebra above cannot express it. Its
 * operand must name a SPAN and not another view; that is a hard error in both
 * configurations, and it is what bounds the recursion below at depth two.
 *
 * THE VIEWS AND THE CONSTANT SPANS ARE REBUILT PER STEP, ON PURPOSE —
 * fpclass.c's, fcmp_step.c's, fpround_step.c's and fmul_step.c's reason,
 * verbatim: a block carrying cached operands would carry state whose
 * initialisation a consumer can forget, and a forgotten bind is a silent wrong
 * circuit rather than a failure. It is also what the sandwich needs: the
 * driver replays indices in reverse and a table rebuilt from mutable state
 * would desynchronise the two halves.
 *
 * I6(a) IS SATISFIED WITH NOTHING LEFT TO CHECK. `cq_fs_sp` is the only thing
 * that hands back a WRITABLE pointer and every one of them is a bit of the
 * caller's region. The rail, the views over it, the constant spans and every
 * block output come back `const`, so a source cannot be materialised by
 * construction.
 */

#include "kernels/fsqrt_int.h"

#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/fpclass.h"
#include "kernels/fpround.h"
#include "kernels/kernel.h"

enum { W64 = CQ_FS_W };

/* softfloat_common.jl:8-14 and fsqrt.jl's own literals. Named rather than
 * inlined so the slot scan's independent copy has something to disagree with:
 * an oracle that shares a constant with the code is blind to that constant. */
static uint64_t const_of(int s)
{
    switch (s) {
    case CQ_FS_K_ZERO:     return UINT64_C(0);
    case CQ_FS_K_ONE:      return UINT64_C(1);
    case CQ_FS_K_BIAS:     return UINT64_C(1023);
    case CQ_FS_K_IMPLICIT: return CQ_FP64_IMPLICIT;
    case CQ_FS_K_INF:      return CQ_FP64_INF_BITS;
    case CQ_FS_K_QUIET:    return CQ_FP64_QUIET_BIT;
    case CQ_FS_K_INDEF:    return CQ_FP64_INDEF;
    default: break;
    }
    cq_kernel_die("fsqrt: unknown 64-lane operand code");
    return 0;
}

/* The two M32 blocks, rebuilt at a row's own offset. Their inputs are NULL
 * because every accessor K21 reaches reads only `scr` and `off` — neither
 * `cq_norm52_m`/`_e` nor `cq_round_normal`/`_overflow_result` is a view over
 * an operand, which is the asymmetry M33 measured on `cq_subnorm_flushed` and
 * which K21 does not inherit because `soft_fsqrt` never calls
 * `_sf_handle_subnormal`. */
#define FS_N52(k, o)  ((cq_norm52_block){ NULL, NULL, (k)->scr, (o) })
#define FS_RND(k, o)  ((cq_round_block){ NULL, NULL, NULL, (k)->scr, (o) })

/* A projection of a hand-off's tuple, 64 lanes. Every one of the four is a
 * real span inside the caller's region. */
static const cq_bit *out64(const cq_fsqrt_block *k, const cq_fsqrt_row *rows,
                           int i)
{
    const cq_fsqrt_row *r = &rows[i];
    uint32_t o = cq_fs_map_get()->bit[r->s0] + k->off;
    int w = r->s1;

    if (rows[r->s0].op == CQ_FSOP_NORM52) {
        cq_norm52_block b = FS_N52(k, o);

        return (w == CQ_FS_OUT_M) ? cq_norm52_m(&b) : cq_norm52_e(&b);
    }
    {
        cq_round_block b = FS_RND(k, o);

        return (w == CQ_FS_OUT_NORMAL) ? cq_round_normal(&b)
                                       : cq_round_overflow_result(&b);
    }
}

/* Row `i`'s 64-lane value: a projection, or a block's own output span. Every
 * 64-lane block puts its result at the front of its region except `sub`, whose
 * `d` lies between `nb` and `c` (add.h). */
const cq_bit *cq_fs_val64(const cq_fsqrt_block *k, int i, cq_bit *buf)
{
    const cq_fsqrt_row *rows;
    uint32_t o;
    int n;

    (void)buf;
    rows = cq_fsqrt_rows(&n);
    if (i < 0 || i >= n)
        cq_kernel_die("fsqrt: an operand names a row outside the program");
    if (cq_fsqrt_row_width(rows, n, i) != W64)
        cq_kernel_die("fsqrt: a 64-lane operand names a row of another width");

    if (rows[i].op == CQ_FSOP_OUT) return out64(k, rows, i);
    o = cq_fs_map_get()->bit[i];
    if (rows[i].op == CQ_FSOP_SUB) return cq_fs_sp(k, o + (uint32_t)W64,
                                                  (uint32_t)W64);
    return cq_fs_sp(k, o, (uint32_t)W64);
}

/* Row `i`'s Bool, as a one-bit span inside the caller's region. Read through
 * the owning module's accessor wherever there is one — `cq_eq_flag` is
 * `orr[W-2]` in general and `diff[0]` at W == 1, and a consumer spelling that
 * rule inline reads `orr[-1]` at the bottom of a ladder (cmp.h).
 *
 * A 1-BIT OPERAND MUST BE A ROW OF THIS PROGRAM AND MUST BE A BOOL ROW, AND
 * NOTHING BELOW M40 KNOWS EITHER. `cq_fs_val64` refuses a code it does not
 * recognise; these are the mirror refusals, and Release has no other detector:
 * the read succeeds, the gate is plausible and the value is wrong. */
const cq_bit *cq_fs_flag_of(const cq_fsqrt_block *k, int i)
{
    const cq_fsqrt_row *rows;
    uint32_t o;
    int n;

    rows = cq_fsqrt_rows(&n);
    if (i < 0 || i >= n)
        cq_kernel_die("fsqrt: a one-bit operand is not a row of this program");
    if (cq_fsqrt_row_width(rows, n, i) != 1)
        cq_kernel_die("fsqrt: a one-bit operand names a row of another width");

    o = cq_fs_map_get()->bit[i];
    if (rows[i].op == CQ_FSOP_CLASS) {
        cq_fp_class_block c;

        c.a = NULL; c.scr = k->scr; c.off = o + k->off;
        c.cls = (cq_fp_class)rows[i].s1;
        return cq_fp_class_flag(&c);
    }
    if (rows[i].op == CQ_FSOP_OUT) {
        cq_round_block b = FS_RND(k, cq_fs_map_get()->bit[rows[i].s0] + k->off);

        return (rows[i].s1 == CQ_FS_OUT_EXPOVF) ? cq_round_exp_overflow(&b)
                                                : cq_round_exp_overflow_aft(&b);
    }
    if (rows[i].op == CQ_FSOP_EQ) {
        cq_eq_block e;

        e.a = NULL; e.b = NULL; e.W = W64;
        e.diff = cq_fs_sp(k, o, (uint32_t)W64);
        e.orr  = cq_fs_sp(k, o + (uint32_t)W64, (uint32_t)W64 - 1u);
        return cq_eq_flag(&e);
    }
    /* `carry[W]` IS `a >=u b` (cmp.h), and carry starts one vector in. */
    if (rows[i].op == CQ_FSOP_ULT)
        return cq_fs_sp(k, o + (uint32_t)(2 * W64), 1u);
    return cq_fs_sp(k, o, 1u);               /* not1 and and1 sit at rel 0 */
}

/* `(v >> s)`, with a NEGATIVE `s` meaning a left shift and |s| >= 64 giving 0
 * rather than C's undefined behaviour. */
static uint64_t shr64(uint64_t v, int s)
{
    if (s >= 64 || s <= -64) return UINT64_C(0);
    if (s >= 0) return v >> (unsigned)s;
    return v << (unsigned)(-s);
}

int cq_fs_view_collapse(int s, int *shift, uint64_t *mask)
{
    const cq_fsqrt_row *rows;
    int guard = 0, n;

    rows = cq_fsqrt_rows(&n);
    *shift = 0;
    *mask  = ~UINT64_C(0);
    while (s >= 0 && s < n && rows[s].op == CQ_FSOP_VIEW) {
        *mask   = shr64(rows[s].mask, *shift) & *mask;
        *shift += rows[s].shift;
        s       = rows[s].s0;
        if (++guard > n) cq_kernel_die("fsqrt: a view chain does not terminate");
    }
    return s;
}

void cq_fs_view_fill(const cq_bit *base, int shift, uint64_t mask, cq_bit *out)
{
    for (int i = 0; i < W64; i++) {
        int j = i + shift;

        out[i] = (((mask >> (unsigned)i) & UINT64_C(1)) != 0u && j >= 0 && j < W64)
               ? base[j] : cq_bit_zero();
    }
}

/* An ARITHMETIC right shift: lane `i` takes `base[i + shift]`, and every lane
 * the shift vacates takes `base[63]` — the SIGN lane, replicated. That is
 * `fsqrt.jl:60` on an `Int64`, and `:53-56` is upstream's statement of why it
 * has to be one. Spelling it as `cq_fs_view_fill` would zero-fill instead,
 * which is right for every operand at or above 1.0 and wrong for every one
 * below it, at exactly the same cost — so no gate count can see the
 * difference. */
static void sar_fill(const cq_bit *base, int shift, cq_bit *out)
{
    for (int i = 0; i < W64; i++) {
        int j = i + shift;

        out[i] = base[(j < W64) ? j : W64 - 1];
    }
}

/* A NON-view 64-lane operand: the rail, a constant span, an arithmetic-shift
 * view, or a row's value. */
static const cq_bit *base64(const cq_fsqrt_block *k, int s, cq_bit *buf)
{
    const cq_fsqrt_row *rows;
    int n;

    rows = cq_fsqrt_rows(&n);
    if (s >= 0 && s < n && rows[s].op == CQ_FSOP_SVIEW) {
        cq_bit inner[CQ_FS_W];
        int b = rows[s].s0;

        if (b >= 0 && b < n
            && (rows[b].op == CQ_FSOP_VIEW || rows[b].op == CQ_FSOP_SVIEW))
            cq_kernel_die("fsqrt: an arithmetic-shift view must name a span, "
                          "not another view");
        if (rows[s].shift < 0 || rows[s].shift >= W64)
            cq_kernel_die("fsqrt: an arithmetic-shift view outside [0, 64)");
        sar_fill(base64(k, b, inner), rows[s].shift, buf);
        return buf;
    }
    if (s >= 0) return cq_fs_val64(k, s, buf);
    if (s == CQ_FS_A) {
        if (k->a == NULL) cq_kernel_die("fsqrt: the block has no operand `a`");
        return k->a;
    }
    cq_fp_const(buf, const_of(s));
    return buf;
}

const cq_bit *cq_fs_op64(const cq_fsqrt_block *k, int s, cq_bit *buf,
                         cq_bit *tmp)
{
    const cq_fsqrt_row *rows;
    int n;

    rows = cq_fsqrt_rows(&n);
    if (s >= 0 && s < n && rows[s].op == CQ_FSOP_VIEW) {
        uint64_t mask;
        int shift;
        int b = cq_fs_view_collapse(s, &shift, &mask);

        cq_fs_view_fill(base64(k, b, tmp), shift, mask, buf);
        return buf;
    }
    return base64(k, s, buf);
}
