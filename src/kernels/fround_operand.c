/* src/kernels/fround_operand.c — M38b, K25. OPERAND RESOLUTION: what each
 * operand code resolves to. Costs and layout are fround_step.c, the M33
 * hand-off fround_fadd.c and the dispatch fround_emit.c.
 *
 * NOT ONE LINE HERE KNOWS ANY JULIA, with one exception it states: `const_of`
 * carries the constant PATTERNS (softfloat_common.jl:8-14 and fround.jl's
 * literals), because a constant is a SOURCE (PRD-v2 §7.3) and a source needs a
 * value.
 *
 * A VIEW CHAIN COLLAPSES TO ONE (shift, mask) PAIR:
 *
 *     (((base >> s1) & m1) >> s2) & m2 == (base >> (s1+s2)) & ((m1 >> s2) & m2)
 *
 * with a NEGATIVE shift meaning a left shift, walked OUTERMOST-IN — M32's
 * finding, and K25 nests them two deep (`(abs_a >> 52) & 0x7ff` over
 * `a & 0x7FFF...`).
 *
 * THE VIEWS AND THE CONSTANT SPANS ARE REBUILT PER STEP, ON PURPOSE: a block
 * carrying cached operands would carry state whose initialisation a consumer
 * can forget, and the driver replays indices in reverse.
 *
 * I6(a) IS SATISFIED WITH NOTHING LEFT TO CHECK: `cq_fi_sp` is the only
 * source of a WRITABLE pointer and every one is a bit of the caller's region;
 * the rail, the views, the constants and every block output come back
 * `const`.
 */

#include "kernels/fround_int.h"

#include "kernels/cmp.h"
#include "kernels/kernel.h"
#include "kernels/shift_var.h"

enum { W64 = CQ_FI_W };

/* softfloat_common.jl:8-14 and fround.jl's own literals. Named rather than
 * inlined so the slot scan's independent copy has something to disagree with.
 * `-1.0` is fround.jl:69-70's `one_bits | UInt64(0x8000000000000000)`, an
 * expression over two literals and so a constant-folded constant (K15 §2.0
 * rule 3). */
static uint64_t const_of(int s)
{
    switch (s) {
    case CQ_FI_K_ZERO:      return UINT64_C(0);
    case CQ_FI_K_ONE:       return UINT64_C(1);
    case CQ_FI_K_7FF:       return CQ_FP64_EXP_ALL;
    case CQ_FI_K_QUIET:     return CQ_FP64_QUIET_BIT;
    case CQ_FI_K_1022:      return UINT64_C(1022);
    case CQ_FI_K_1023:      return UINT64_C(1023);
    case CQ_FI_K_1075:      return UINT64_C(1075);
    case CQ_FI_K_52:        return UINT64_C(52);
    case CQ_FI_K_ONES:      return ~UINT64_C(0);
    case CQ_FI_K_IMPLICIT:  return CQ_FP64_IMPLICIT;
    case CQ_FI_K_ONE_F:     return UINT64_C(0x3FF0000000000000);
    case CQ_FI_K_NEG_ONE_F: return UINT64_C(0xBFF0000000000000);
    default: break;
    }
    cq_kernel_die("fround: unknown 64-lane operand code");
    return 0;
}

static const cq_fround_row *rows_of(const cq_fround_block *k, int *n)
{
    return cq_fround_rows(k->prog, n);
}

/* Row `i`'s 64-lane value: a block's own output span. Every 64-lane block puts
 * its result at the front of its region except `sub`, whose `d` lies between
 * `nb` and `c` (add.h), the barrel, whose answer is its LAST stage's `r` and
 * is found only through `cq_barrel_result` (shift_var.h), and M33's fadd,
 * found only through M33. */
const cq_bit *cq_fi_val64(const cq_fround_block *k, int i)
{
    const cq_fround_row *rows;
    uint32_t o;
    int n;

    rows = rows_of(k, &n);
    if (i < 0 || i >= n)
        cq_kernel_die("fround: an operand names a row outside the program");
    if (cq_fround_row_width(rows, n, i) != W64 || rows[i].op == CQ_FIOP_VIEW)
        cq_kernel_die("fround: a 64-lane operand names a row that is not a "
                      "64-lane block");

    o = cq_fi_map_get(k->prog)->bit[i];
    switch (rows[i].op) {
    case CQ_FIOP_SUB:
        return cq_fi_sp(k, o + (uint32_t)W64, (uint32_t)W64);
    case CQ_FIOP_BSHL: case CQ_FIOP_BLSHR: {
        cq_barrel_block b;

        b.a = NULL; b.b = NULL; b.scr = k->scr; b.off = cq_fi_base(k, o);
        b.W = W64;
        b.dir = (rows[i].op == CQ_FIOP_BSHL) ? CQ_BARREL_SHL : CQ_BARREL_LSHR;
        return cq_barrel_result(&b); }
    case CQ_FIOP_FADD:
        return cq_fi_fadd_result(NULL, NULL, k->scr, cq_fi_base(k, o));
    default:
        return cq_fi_sp(k, o, (uint32_t)W64);
    }
}

/* Row `i`'s Bool, as a one-bit span inside the caller's region. Read through
 * the owning module's accessor wherever there is one. THE MIRROR REFUSALS of
 * `cq_fi_val64`: Release has no other detector — the read succeeds, the gate
 * is plausible and the value is wrong. */
const cq_bit *cq_fi_flag_of(const cq_fround_block *k, int i)
{
    const cq_fround_row *rows;
    uint32_t o;
    int n;

    rows = rows_of(k, &n);
    if (i < 0 || i >= n)
        cq_kernel_die("fround: a one-bit operand is not a row of this program");
    if (cq_fround_row_width(rows, n, i) != 1)
        cq_kernel_die("fround: a one-bit operand names a row of another width");

    o = cq_fi_map_get(k->prog)->bit[i];
    if (rows[i].op == CQ_FIOP_EQ) {
        cq_eq_block e;

        e.a = NULL; e.b = NULL; e.W = W64;
        e.diff = cq_fi_sp(k, o, (uint32_t)W64);
        e.orr  = cq_fi_sp(k, o + (uint32_t)W64, (uint32_t)W64 - 1u);
        return cq_eq_flag(&e);
    }
    if (rows[i].op == CQ_FIOP_SLT) {
        cq_slt_block c;

        c.a = NULL; c.b = NULL; c.W = W64;
        c.af    = cq_fi_sp(k, o,                            (uint32_t)W64);
        c.bf    = cq_fi_sp(k, o + (uint32_t)W64,            (uint32_t)W64);
        c.nb    = cq_fi_sp(k, o + (uint32_t)(2 * W64),      (uint32_t)W64);
        c.carry = cq_fi_sp(k, o + (uint32_t)(3 * W64),      (uint32_t)W64 + 1u);
        c.axnb  = cq_fi_sp(k, o + (uint32_t)(4 * W64) + 1u, (uint32_t)W64);
        return cq_slt_flag(&c);
    }
    return cq_fi_sp(k, o, 1u);            /* not1, and1 and or1 sit at rel 0 */
}

/* `(v >> s)`, a NEGATIVE `s` meaning a left shift and |s| >= 64 giving 0
 * rather than C's undefined behaviour. */
static uint64_t shr64(uint64_t v, int s)
{
    if (s >= 64 || s <= -64) return UINT64_C(0);
    if (s >= 0) return v >> (unsigned)s;
    return v << (unsigned)(-s);
}

int cq_fi_view_collapse(cq_fround_prog p, int s, int *shift, uint64_t *mask)
{
    const cq_fround_row *rows;
    int guard = 0, n;

    rows = cq_fround_rows(p, &n);
    *shift = 0;
    *mask  = ~UINT64_C(0);
    while (s >= 0 && s < n && rows[s].op == CQ_FIOP_VIEW) {
        *mask   = shr64(rows[s].mask, *shift) & *mask;
        *shift += rows[s].shift;
        s       = rows[s].s0;
        if (++guard > n) cq_kernel_die("fround: a view chain does not terminate");
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

/* A NON-view 64-lane operand: the rail, a constant span, or a row's value. */
static const cq_bit *base64(const cq_fround_block *k, int s, cq_bit *buf)
{
    if (s >= 0) return cq_fi_val64(k, s);
    if (s == CQ_FI_A) {
        if (k->a == NULL) cq_kernel_die("fround: the block has no operand `a`");
        return k->a;
    }
    cq_fp_const(buf, const_of(s));
    return buf;
}

const cq_bit *cq_fi_op64(const cq_fround_block *k, int s, cq_bit *buf,
                         cq_bit *tmp)
{
    const cq_fround_row *rows;
    int n;

    rows = rows_of(k, &n);
    if (s >= 0 && s < n && rows[s].op == CQ_FIOP_VIEW) {
        uint64_t mask;
        int shift;
        int b = cq_fi_view_collapse(k->prog, s, &shift, &mask);

        view_fill(base64(k, b, tmp), shift, mask, buf);
        return buf;
    }
    return base64(k, s, buf);
}
