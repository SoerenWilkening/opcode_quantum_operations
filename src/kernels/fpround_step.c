/* src/kernels/fpround_step.c — M32, K23. THE LAYOUT AND THE OPERANDS: what a
 * row costs, where its spans lie inside the caller's region, and what each
 * operand code resolves to. The dispatch and the four public blocks are next
 * door in fpround_emit.c, on the seam fpround_int.h records (D-K23-10); the
 * four row tables are in fpround.c, on M36's own ROW TABLES <-> STEP MACHINE
 * seam (K18.md D-K18-7).
 *
 * NOT ONE LINE HERE KNOWS ANY JULIA. Every cost is ASKED of another module —
 * cq_eq_steps, cq_ult_steps, cq_slt_steps, cq_sub_steps, cq_add_steps,
 * cq_mux_steps, cq_barrel_steps and the matching _region functions, all of
 * which M32 is the reason `9ve.19` added — or is upstream's own three-gate
 * bitwise vocabulary, and nothing would move if a row table changed.
 *
 * A VIEW CHAIN COLLAPSES TO ONE (shift, mask) PAIR, WHICH IS WHY VIEWS COST
 * NOTHING EVEN WHEN THEY NEST. `guard << 2` (softfloat_common.jl:209) is a
 * view over `(wr >> 2) & 1` (:204), itself a view over `wr >> 2`: three rows,
 * three operator occurrences, ONE addressing computation. The composition is
 *
 *     (((base >> s1) & m1) >> s2) & m2 == (base >> (s1+s2)) & ((m1 >> s2) & m2)
 *
 * with a NEGATIVE shift meaning a left shift, and it is walked OUTERMOST-IN so
 * the accumulated shift is applied to each row's mask before that row's own
 * shift joins it.
 *
 * THE VIEWS AND THE CONSTANT SPANS ARE REBUILT PER STEP, ON PURPOSE —
 * fpclass.c's and fcmp_step.c's reason, verbatim: a block carrying cached
 * operands would carry state whose initialisation a consumer can forget, and a
 * forgotten bind is a silent wrong circuit rather than a failure.
 *
 * THE PREFIX-OFFSET WALK IS DONE ONCE PER STEP AND NOT ONCE PER LOOKUP. A row
 * reference resolves to a span, and a span needs the sum of every earlier
 * row's region; doing that per operand would make the step O(n^2) in a 48-row
 * program driven 9,165 times per call.
 *
 * I6(a) IS SATISFIED WITH NOTHING LEFT TO CHECK. `cq_fpr_row_out` and
 * `cq_fpr_sp` are the only things here that hand back a WRITABLE pointer, and
 * every one of them is a bit of the caller's region at `off + <span>`. The
 * three inputs, the views over them and the constant spans come back `const`,
 * so a source cannot be materialised by construction.
 */

#include "kernels/fpround_int.h"

#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/kernel.h"
#include "kernels/mux.h"
#include "kernels/shift_var.h"

_Static_assert(CQ_FPR_MAXR >= 48, "_sf_normalize_to_bit52 is 48 rows");

/* `CQ_FPR_W` spelled short, for the same reason fcmp_step.c spells it `W64`:
 * every span expression below carries it two or three times. */
enum { W64 = CQ_FPR_W };

cq_fpr_ctx cq_fpr_ctx_of(cq_fpround_id id, const cq_bit *a, const cq_bit *b,
                         const cq_bit *c, cq_scratch *scr, uint32_t off)
{
    cq_fpr_ctx x;

    x.in[0] = a; x.in[1] = b; x.in[2] = c;
    x.scr = scr; x.off = off; x.id = id;
    x.rows = cq_fpround_rows(id, &x.n);
    return x;
}

/* --- Slot and region arithmetic, ASKED of the owning modules. ------------- */

/* `lower_not1!` is CNOT(w, r) then NOT(r) into a FRESH wire — two slots, one
 * bit (arith.jl:474-478). `lower_and!` is one Toffoli per lane, so W slots and
 * W bits (:268-272). `lower_or!` is CNOT, CNOT, Toffoli per lane, so 3W slots
 * and W bits (:274-282). Transcribed from the pinned source because they are
 * emitted directly here and no module publishes a cost for them (§7.10). */
enum { FR_NOT1_STEPS = 2, FR_AND1_STEPS = 1, FR_OR1_STEPS = 3 };

/* 1 for a row whose output is a Bool, 64 for the rest. A VIEW is 64 lanes. */
int cq_fpr_op_width(int op)
{
    switch (op) {
    case CQ_FROP_EQ: case CQ_FROP_ULT: case CQ_FROP_SLT:
    case CQ_FROP_NOT1: case CQ_FROP_AND1: case CQ_FROP_OR1:
        return 1;
    default: break;
    }
    return W64;
}

int cq_fpr_row_steps(const cq_fpround_row *r)
{
    switch (r->op) {
    case CQ_FROP_VIEW:  return 0;
    case CQ_FROP_EQ:    return cq_eq_steps(W64);
    case CQ_FROP_ULT:   return cq_ult_steps(W64);
    case CQ_FROP_SLT:   return cq_slt_steps(W64);
    case CQ_FROP_SUB:   return cq_sub_steps(W64);
    case CQ_FROP_ADD:   return cq_add_steps(W64);
    case CQ_FROP_MUX:   return cq_mux_steps(W64);
    case CQ_FROP_AND:   return W64;
    case CQ_FROP_OR:    return 3 * W64;
    case CQ_FROP_BSHL:  return cq_barrel_steps(W64, CQ_BARREL_SHL);
    case CQ_FROP_BLSHR: return cq_barrel_steps(W64, CQ_BARREL_LSHR);
    case CQ_FROP_NOT1:  return FR_NOT1_STEPS;
    case CQ_FROP_AND1:  return FR_AND1_STEPS;
    case CQ_FROP_OR1:   return FR_OR1_STEPS;
    default: break;
    }
    cq_kernel_die("fpround: unknown op in the program");
    return 0;
}

static uint32_t row_region(const cq_fpround_row *r)
{
    switch (r->op) {
    case CQ_FROP_VIEW:  return 0u;
    case CQ_FROP_EQ:    return (uint32_t)cq_eq_region(W64);
    case CQ_FROP_ULT:   return (uint32_t)cq_ult_region(W64);
    case CQ_FROP_SLT:   return (uint32_t)cq_slt_region(W64);
    case CQ_FROP_SUB:   return (uint32_t)cq_sub_region(W64);
    case CQ_FROP_ADD:   return (uint32_t)cq_add_region(W64);
    case CQ_FROP_MUX:   return (uint32_t)cq_mux_region(W64);
    case CQ_FROP_AND: case CQ_FROP_OR:
        return (uint32_t)W64;
    case CQ_FROP_BSHL: case CQ_FROP_BLSHR:
        return (uint32_t)cq_barrel_region(W64);
    default: break;
    }
    return 1u;                        /* not1, and1, or1: one output bit */
}

uint32_t cq_fpr_region_of(const cq_fpr_ctx *x)
{
    uint32_t bits = 0u;

    for (int i = 0; i < x->n; i++) bits += row_region(&x->rows[i]);
    return bits;
}

int cq_fpr_steps_of(const cq_fpr_ctx *x)
{
    int slots = 0;

    for (int i = 0; i < x->n; i++) slots += cq_fpr_row_steps(&x->rows[i]);
    return slots;
}

/* The prefix-offset walk, which also returns the region total — so the fit
 * check and the offsets come out of ONE pass. The walk runs once per STEP and
 * not once per operand lookup: a row reference resolves to a span, and a span
 * needs the sum of every earlier row's region, so doing it per operand would
 * make the step O(n^2) in a 48-row program driven 9,165 times per call. */
static uint32_t walk(const cq_fpr_ctx *x, uint32_t *off)
{
    uint32_t o = x->off;

    for (int i = 0; i < x->n; i++) {
        off[i] = o;
        o += row_region(&x->rows[i]);
    }
    return o - x->off;
}

/* The region check is the BLOCK's, and it has to be: without it the first
 * out-of-region span aborts in M08 naming the REGION rather than the consumer
 * that mis-sized its offset, and the two-blocks-in-one-region shape then has
 * no diagnostic of its own. Hard error in both configurations. */
void cq_fpr_arm(const cq_fpr_ctx *x, uint32_t *off)
{
    if (x->scr == NULL) cq_kernel_die("fpround: the block has no region");
    if ((uint64_t)x->off + walk(x, off) > (uint64_t)cq_scratch_size(x->scr))
        cq_kernel_die("fpround: the block's region does not fit at its offset");
}

/* `at` is ABSOLUTE inside the region: the prefix walk has already added
 * `x->off`, which is the one place this block's base is applied. */
cq_bit *cq_fpr_sp(const cq_fpr_ctx *x, uint32_t at, uint32_t len)
{
    return cq_scratch_span(x->scr, at, len);
}

/* --- Operand resolution. -------------------------------------------------- */

static uint64_t const_of(int s)
{
    switch (s) {
    case CQ_FR_K_ZERO:     return UINT64_C(0);
    case CQ_FR_K_ONE:      return UINT64_C(1);
    case CQ_FR_K_FOUR:     return UINT64_C(4);
    case CQ_FR_K_56:       return UINT64_C(56);
    case CQ_FR_K_63:       return UINT64_C(63);
    case CQ_FR_K_7FE:      return UINT64_C(0x7FE);
    case CQ_FR_K_7FF:      return CQ_FP64_EXP_ALL;
    case CQ_FR_K_D32:      return UINT64_C(32);
    case CQ_FR_K_D16:      return UINT64_C(16);
    case CQ_FR_K_D8:       return UINT64_C(8);
    case CQ_FR_K_D4:       return UINT64_C(4);
    case CQ_FR_K_D2:       return UINT64_C(2);
    case CQ_FR_K_D1:       return UINT64_C(1);
    case CQ_FR_K_IMPLICIT: return CQ_FP64_IMPLICIT;
    case CQ_FR_K_INF:      return CQ_FP64_INF_BITS;
    default: break;
    }
    cq_kernel_die("fpround: unknown 64-lane operand code");
    return 0;
}

/* A block's own output span. Every 64-lane block puts its result at the front
 * of its region except `sub`, whose `d` lies between `nb` and `c` (add.h), and
 * the barrel, whose running value is the LAST stage's mux output and is only
 * findable through cq_barrel_result (shift_var.h). */
cq_bit *cq_fpr_row_out(const cq_fpr_ctx *x, const uint32_t *off, int i)
{
    uint32_t o = off[i];

    if (x->rows[i].op == CQ_FROP_SUB) return cq_fpr_sp(x, o + (uint32_t)W64, (uint32_t)W64);
    if (x->rows[i].op == CQ_FROP_BSHL || x->rows[i].op == CQ_FROP_BLSHR) {
        cq_barrel_block b;

        b.a = NULL; b.b = NULL; b.scr = x->scr; b.off = o; b.W = W64;
        b.dir = (x->rows[i].op == CQ_FROP_BSHL) ? CQ_BARREL_SHL : CQ_BARREL_LSHR;
        return cq_barrel_result(&b);
    }
    return cq_fpr_sp(x, o, (uint32_t)W64);
}

/* Row `i`'s Bool, as a one-bit span inside the caller's region. Read through
 * the owning module's accessor wherever there is one — `cq_eq_flag` and
 * `cq_slt_flag` are pure addressing and a consumer spelling their rule inline
 * gets it wrong at the bottom of a ladder (cmp.h). `cq_ult_block` publishes no
 * accessor, so `carry[W]` is spelled from its documented layout.
 *
 * A 1-BIT OPERAND MUST BE A ROW OF THIS PROGRAM AND MUST BE A BOOL ROW, AND
 * NOTHING BELOW M32 KNOWS EITHER. `cq_fpr_op64` refuses a code it does not recognise;
 * these are the mirror refusals — a `not1`, `and`, `or` or `mux` cond handed
 * one of the negative codes would index the row table out of bounds, and one
 * handed a 64-lane row would read a wire that is not a flag. Release has no
 * other detector: the read succeeds, the gate is plausible, the value wrong. */
const cq_bit *cq_fpr_flag_of(const cq_fpr_ctx *x, const uint32_t *off, int i)
{
    uint32_t o;

    if (i < 0 || i >= x->n)
        cq_kernel_die("fpround: a one-bit operand is not a row of this program");
    if (cq_fpr_op_width(x->rows[i].op) != 1)
        cq_kernel_die("fpround: a one-bit operand names a 64-lane row");

    o = off[i];
    if (x->rows[i].op == CQ_FROP_EQ) {
        cq_eq_block e;

        e.a = NULL; e.b = NULL; e.W = W64;
        e.diff = cq_fpr_sp(x, o, (uint32_t)W64);
        e.orr  = cq_fpr_sp(x, o + (uint32_t)W64, (uint32_t)W64 - 1u);
        return cq_eq_flag(&e);
    }
    if (x->rows[i].op == CQ_FROP_SLT) {
        cq_slt_block c;

        c.a = NULL; c.b = NULL; c.W = W64;
        c.af    = cq_fpr_sp(x, o,                            (uint32_t)W64);
        c.bf    = cq_fpr_sp(x, o + (uint32_t)W64,            (uint32_t)W64);
        c.nb    = cq_fpr_sp(x, o + (uint32_t)(2 * W64),      (uint32_t)W64);
        c.carry = cq_fpr_sp(x, o + (uint32_t)(3 * W64),      (uint32_t)W64 + 1u);
        c.axnb  = cq_fpr_sp(x, o + (uint32_t)(4 * W64) + 1u, (uint32_t)W64);
        return cq_slt_flag(&c);
    }
    /* `carry[W]` IS `a >=u b` (cmp.h), and carry starts one vector in. */
    if (x->rows[i].op == CQ_FROP_ULT) return cq_fpr_sp(x, o + (uint32_t)(2 * W64), 1u);
    return cq_fpr_sp(x, o, 1u);              /* not1, and1, or1 sit at rel 0 */
}

/* A NON-view 64-lane operand: one of the three inputs, a constant span, or an
 * emitting row's output. */
const cq_bit *cq_fpr_base64(const cq_fpr_ctx *x, const uint32_t *off, int s,
                            cq_bit *buf)
{
    if (s >= 0) {
        if (s >= x->n)
            cq_kernel_die("fpround: an operand names a row outside the program");
        if (cq_fpr_op_width(x->rows[s].op) != W64)
            cq_kernel_die("fpround: a 64-lane operand names a one-bit row");
        return cq_fpr_row_out(x, off, s);
    }
    if (s >= CQ_FR_IN2) {                          /* IN0 = -1 … IN2 = -3 */
        const cq_bit *p = x->in[-s - 1];

        if (p == NULL) cq_kernel_die("fpround: this block has no such input");
        return p;
    }
    cq_fp_const(buf, const_of(s));
    return buf;
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
int cq_fpr_view_collapse(const cq_fpr_ctx *x, int s, int *shift, uint64_t *mask)
{
    int guard = 0;

    *shift = 0;
    *mask  = ~UINT64_C(0);
    while (s >= 0 && s < x->n && x->rows[s].op == CQ_FROP_VIEW) {
        *mask   = shr64(x->rows[s].mask, *shift) & *mask;
        *shift += x->rows[s].shift;
        s       = x->rows[s].s0;
        if (++guard > x->n)
            cq_kernel_die("fpround: a view chain does not terminate");
    }
    return s;
}

void cq_fpr_view_fill(const cq_bit *base, int shift, uint64_t mask, cq_bit *out)
{
    for (int i = 0; i < W64; i++) {
        int j = i + shift;

        out[i] = (((mask >> (unsigned)i) & UINT64_C(1)) != 0u && j >= 0 && j < W64)
               ? base[j] : cq_bit_zero();
    }
}

/* A 64-lane operand. `buf` receives a view or a constant; `tmp` is the second
 * buffer a view over a constant needs, and the two must be distinct arrays. */
const cq_bit *cq_fpr_op64(const cq_fpr_ctx *x, const uint32_t *off, int s,
                          cq_bit *buf, cq_bit *tmp)
{
    if (s >= 0 && s < x->n && x->rows[s].op == CQ_FROP_VIEW) {
        int shift;
        uint64_t mask;
        int b = cq_fpr_view_collapse(x, s, &shift, &mask);

        cq_fpr_view_fill(cq_fpr_base64(x, off, b, tmp), shift, mask, buf);
        return buf;
    }
    return cq_fpr_base64(x, off, s, buf);
}
