/* src/kernels/fconv_step.c — M37, K19. THE LAYOUT AND THE OPERANDS: what a row
 * costs, how many bits it owns, where its spans lie inside the caller's
 * region, and what each operand code resolves to. The dispatch and the surface
 * are next door in fconv_emit.c, on the seam fconv_int.h records; the three
 * row tables are in fconv.c, on M36's ROW TABLES <-> STEP MACHINE seam
 * (K18.md D-K18-7).
 *
 * NOT ONE LINE HERE KNOWS ANY JULIA. Every cost is ASKED of another module —
 * cq_eq_steps, cq_ult_steps, cq_sub_steps, cq_add_steps, cq_mux_steps,
 * cq_barrel_steps, cq_fsub_steps and the matching _region functions — or is
 * upstream's own three-gate bitwise vocabulary, and nothing would move if a
 * row table changed.
 *
 * A VIEW CHAIN COLLAPSES TO ONE (shift, mask) PAIR, WHICH IS WHY VIEWS COST
 * NOTHING EVEN WHEN THEY NEST — M32's and M33's mechanism, verbatim. `exp`
 * (fptosi.jl:24) is a view over `a >> 52`: two rows, two operator occurrences,
 * ONE addressing computation. The composition is
 *
 *     (((base >> s1) & m1) >> s2) & m2 == (base >> (s1+s2)) & ((m1 >> s2) & m2)
 *
 * with a NEGATIVE shift meaning a left shift, and it is walked OUTERMOST-IN so
 * the accumulated shift is applied to each row's mask before that row's own
 * shift joins it.
 *
 * A VIEW OVER A *SCRATCH* SPAN CARRIES THAT SPAN'S HIGH LANES AS QUBITS, NOT
 * AS CONSTANTS, AND K19 IS THE FIRST MODULE WHERE THAT BITES (K19.md §2.0).
 * `is_normal << 52` (fptosi.jl:29) is a view over a MUX output — 64
 * pre-materialised scratch bits under I6(b), of which lanes 1..63 provably
 * hold |0> and are nonetheless CQ_BIT_Q — so the `or` at :29 emits on lanes
 * 52..63 exactly as it does on lane 52, and none of it folds. Contrast the
 * views over the RAIL, whose vacated lanes are literal CQ_BIT_ZERO and whose
 * consumers' gates DO fold. A cost model that assumes "a shift view folds" is
 * wrong here in the direction that under-predicts gates.
 *
 * THE VIEWS AND THE CONSTANT SPANS ARE REBUILT PER STEP, ON PURPOSE —
 * fpclass.c's, fcmp_step.c's and fadd_operand.c's reason, verbatim: a block
 * carrying cached operands would carry state whose initialisation a consumer
 * can forget, and a forgotten bind is a silent wrong circuit rather than a
 * failure.
 *
 * I6(a) IS SATISFIED WITH NOTHING LEFT TO CHECK. `cq_fv_sp` and
 * `cq_fv_row_out` are the only things here that hand back a WRITABLE pointer,
 * and every one of them is a bit of the caller's region at `off + <span>`. The
 * rail, the views over it, the constant spans and the zext all come back
 * `const`, so a source cannot be materialised by construction.
 */

#include "kernels/fconv_int.h"

#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/fadd.h"
#include "kernels/kernel.h"
#include "kernels/mux.h"
#include "kernels/shift_var.h"

_Static_assert(CQ_FV_MAXR >= 74, "fptoui's program is 74 rows");

/* `CQ_FV_W` spelled short, for fcmp_step.c's and fadd_step.c's reason: every
 * span expression below carries it two or three times. */
enum { W64 = CQ_FV_W };

/* `lower_not1!` is CNOT(w, r) then NOT(r) into a FRESH wire — two slots, one
 * bit (arith.jl:474-478). `lower_and!` is one Toffoli per lane, so W slots and
 * W bits (:268-272). `lower_or!` is CNOT, CNOT, Toffoli per lane, so 3W slots
 * and W bits (:274-282). `lower_xor!` is CNOT, CNOT per lane, so 2W slots and
 * W bits (:284-291). Transcribed from the pinned source because they are
 * emitted directly in fconv_emit.c and no module publishes a cost for them
 * (PRD-v2 §7.10). */
enum { FV_NOT1_STEPS = 2, FV_AND1_STEPS = 1 };

int cq_fv_row_steps(const cq_fconv_row *r)
{
    switch (r->op) {
    case CQ_FVOP_VIEW:  return 0;
    case CQ_FVOP_EQ:    return cq_eq_steps(W64);
    case CQ_FVOP_ULT:   return cq_ult_steps(W64);
    case CQ_FVOP_SUB:   return cq_sub_steps(W64);
    case CQ_FVOP_ADD:   return cq_add_steps(W64);
    case CQ_FVOP_MUX:   return cq_mux_steps(W64);
    case CQ_FVOP_AND:   return W64;
    case CQ_FVOP_OR:    return 3 * W64;
    case CQ_FVOP_XOR:   return 2 * W64;
    case CQ_FVOP_NOT1:  return FV_NOT1_STEPS;
    case CQ_FVOP_AND1:  return FV_AND1_STEPS;
    case CQ_FVOP_BSHL:  return cq_barrel_steps(W64, CQ_BARREL_SHL);
    case CQ_FVOP_BLSHR: return cq_barrel_steps(W64, CQ_BARREL_LSHR);
    case CQ_FVOP_FSUB:  return cq_fsub_steps();
    default: break;
    }
    cq_kernel_die("fconv: unknown op in the program");
    return 0;
}

static uint32_t row_region(const cq_fconv_row *r)
{
    switch (r->op) {
    case CQ_FVOP_VIEW:  return 0u;
    case CQ_FVOP_EQ:    return (uint32_t)cq_eq_region(W64);
    case CQ_FVOP_ULT:   return (uint32_t)cq_ult_region(W64);
    case CQ_FVOP_SUB:   return (uint32_t)cq_sub_region(W64);
    case CQ_FVOP_ADD:   return (uint32_t)cq_add_region(W64);
    case CQ_FVOP_MUX:   return (uint32_t)cq_mux_region(W64);
    case CQ_FVOP_AND: case CQ_FVOP_OR: case CQ_FVOP_XOR:
        return (uint32_t)W64;
    case CQ_FVOP_BSHL: case CQ_FVOP_BLSHR:
        return (uint32_t)cq_barrel_region(W64);
    case CQ_FVOP_FSUB:  return cq_fsub_region();
    default:            return 1u;         /* not1, and1: one output bit */
    }
}

int cq_fv_op_width(const cq_fv_ctx *x, int i)
{
    if (i < 0 || i >= x->n)
        cq_kernel_die("fconv: an operand names a row outside the program");
    switch (x->rows[i].op) {
    case CQ_FVOP_EQ: case CQ_FVOP_ULT: case CQ_FVOP_NOT1: case CQ_FVOP_AND1:
        return 1;
    default: break;
    }
    return W64;
}

void cq_fv_check_program(const cq_fconv_row *rows, int n)
{
    if (rows == NULL || n <= 0 || n > CQ_FV_MAXR)
        cq_kernel_die("fconv: the program is empty or longer than "
                      "CQ_FCONV_MAX_ROWS");
}

uint32_t cq_fv_region_of(const cq_fv_ctx *x)
{
    uint32_t bits = 0u;

    cq_fv_check_program(x->rows, x->n);
    for (int i = 0; i < x->n; i++) bits += row_region(&x->rows[i]);
    return bits;
}

int cq_fv_steps_of(const cq_fv_ctx *x)
{
    int slots = 0;

    cq_fv_check_program(x->rows, x->n);
    for (int i = 0; i < x->n; i++) slots += cq_fv_row_steps(&x->rows[i]);
    return slots;
}

const cq_fv_map *cq_fv_map_get(cq_fconv_prog p)
{
    static cq_fv_map maps[3];
    static int ready[3];
    cq_fv_map *m;
    uint32_t bits = 0u;
    int slots = 0, pi = (int)p;

    if (pi < (int)CQ_FCONV_PROG_FPTOSI || pi > (int)CQ_FCONV_PROG_SITOFP)
        cq_kernel_die("fconv: program outside the prefix-map table");
    m = &maps[pi];
    if (ready[pi]) return m;
    m->n = cq_fconv_program(p, m->rows);
    if (m->n > CQ_FV_MAXR)
        cq_kernel_die("fconv: the program outgrew the prefix map");
    cq_fv_check_program(m->rows, m->n);
    for (int i = 0; i < m->n; i++) {
        m->rel[i]  = bits;
        m->slot[i] = slots;
        bits  += row_region(&m->rows[i]);
        slots += cq_fv_row_steps(&m->rows[i]);
    }
    m->slot[m->n] = slots;
    m->region = bits;
    m->steps  = slots;
    ready[pi] = 1;
    return m;
}

int cq_fv_row_at(const cq_fv_map *m, int u, int *within)
{
    int lo = 0, hi = m->n;

    if (u < 0 || u >= m->steps)
        cq_kernel_die("fconv: a slot index outside the prefix map");
    while (hi - lo > 1) {
        int mid = lo + (hi - lo) / 2;

        if (m->slot[mid] <= u) lo = mid; else hi = mid;
    }
    *within = u - m->slot[lo];
    return lo;
}

/* The region check is the PROGRAM's, and it has to be: without it the first
 * out-of-region span aborts in M08 naming the REGION rather than the consumer
 * that mis-sized its offset, and the two-programs-in-one-region shape then has
 * no diagnostic of its own. Hard error in both configurations. */
void cq_fv_arm(const cq_fv_ctx *x, uint32_t *off, int *slots)
{
    uint32_t o;
    int s = 0;

    cq_fv_check_program(x->rows, x->n);
    if (x->scr == NULL) cq_kernel_die("fconv: the block has no region");

    o = x->off;
    for (int i = 0; i < x->n; i++) {
        off[i] = o;
        o += row_region(&x->rows[i]);
        s  += cq_fv_row_steps(&x->rows[i]);
    }
    if ((uint64_t)o > (uint64_t)cq_scratch_size(x->scr))
        cq_kernel_die("fconv: the block's region does not fit at its offset");
    if (slots != NULL) *slots = s;
}

void cq_fv_arm_map(const cq_fv_ctx *x, const cq_fv_map *m, uint32_t *off)
{
    if (x->scr == NULL) cq_kernel_die("fconv: the block has no region");
    for (int i = 0; i < m->n; i++) off[i] = x->off + m->rel[i];
    if ((uint64_t)x->off + (uint64_t)m->region
        > (uint64_t)cq_scratch_size(x->scr))
        cq_kernel_die("fconv: the block's region does not fit at its offset");
}

/* `at` is ABSOLUTE inside the region: the prefix walk has already added
 * `x->off`, which is the one place this program's base is applied. */
cq_bit *cq_fv_sp(const cq_fv_ctx *x, uint32_t at, uint32_t len)
{
    return cq_scratch_span(x->scr, at, len);
}

/* --- Operand resolution. -------------------------------------------------- */

static uint64_t const_of(int s)
{
    switch (s) {
    case CQ_FV_K_0:     return UINT64_C(0);
    case CQ_FV_K_1:     return UINT64_C(1);
    case CQ_FV_K_2:     return UINT64_C(2);
    case CQ_FV_K_4:     return UINT64_C(4);
    case CQ_FV_K_8:     return UINT64_C(8);
    case CQ_FV_K_16:    return UINT64_C(16);
    case CQ_FV_K_32:    return UINT64_C(32);
    case CQ_FV_K_63:    return UINT64_C(63);
    case CQ_FV_K_3FF:   return UINT64_C(0x3FF);
    case CQ_FV_K_7FF:   return CQ_FP64_EXP_ALL;
    case CQ_FV_K_1075:  return UINT64_C(1075);
    case CQ_FV_K_1086:  return UINT64_C(1086);
    case CQ_FV_K_FRAC:  return CQ_FP64_FRAC_MASK;
    case CQ_FV_K_ONES:  return ~UINT64_C(0);
    case CQ_FV_K_HIBIT: return UINT64_C(0x8000000000000000);
    case CQ_FV_K_BIAS:  return UINT64_C(0x43E0000000000000);
    default: break;
    }
    cq_kernel_die("fconv: unknown 64-lane operand code");
    return 0;
}

/* A block's own output span. Every 64-lane row puts its result at the front of
 * its region except `sub`, whose `d` lies between `nb` and `c` (add.h); the
 * barrel, whose running value is the LAST stage's mux output and is only
 * findable through cq_barrel_result (shift_var.h); and M33's `fsub`, whose
 * result row is deep inside its own 133-row program and is only findable
 * through cq_fsub_result (fadd.h). All three are ASKED rather than spelled:
 * a consumer writing "the result is at off + N" would be transcribing another
 * module's own layout table a second time, which is exactly the number a
 * boundary error moves. */
const cq_bit *cq_fv_row_out(const cq_fv_ctx *x, const uint32_t *off, int i)
{
    uint32_t o = off[i];
    int op = x->rows[i].op;

    if (op == CQ_FVOP_SUB)
        return cq_fv_sp(x, o + (uint32_t)W64, (uint32_t)W64);
    if (op == CQ_FVOP_BSHL || op == CQ_FVOP_BLSHR) {
        cq_barrel_block b;

        b.a = NULL; b.b = NULL; b.scr = x->scr; b.off = o; b.W = W64;
        b.dir = (op == CQ_FVOP_BSHL) ? CQ_BARREL_SHL : CQ_BARREL_LSHR;
        return cq_barrel_result(&b);
    }
    if (op == CQ_FVOP_FSUB) {
        cq_fsub_block k;

        /* The two operands are not read by the accessor — it is pure
         * addressing inside the region — which is the same idiom the barrel
         * line above uses. */
        k.a = NULL; k.b = NULL; k.scr = x->scr; k.off = o;
        return cq_fsub_result(&k);
    }
    return cq_fv_sp(x, o, (uint32_t)W64);
}

/* Row `i`'s Bool, as a one-bit span inside the caller's region. Read through
 * the owning module's accessor wherever there is one — `cq_eq_flag` is
 * `orr[W-2]` in general and `diff[0]` at W == 1, and a consumer spelling that
 * rule inline reads `orr[-1]` at the bottom of the ladder (cmp.h).
 *
 * A 1-BIT OPERAND MUST BE A ROW OF THIS PROGRAM AND MUST BE A BOOL ROW, AND
 * NOTHING BELOW M37 KNOWS EITHER. `cq_fv_op64` refuses a code it does not
 * recognise; these are the mirror refusals — a `not1`, `and1` or `mux` cond
 * handed one of the negative codes would index the row table out of bounds,
 * and one handed a 64-lane row would read a wire that is not a flag. Release
 * has no other detector: the read succeeds, the gate is plausible and the
 * value is wrong. */
const cq_bit *cq_fv_flag_of(const cq_fv_ctx *x, const uint32_t *off, int i)
{
    const cq_fconv_row *r;
    uint32_t o;

    if (i < 0 || i >= x->n)
        cq_kernel_die("fconv: a one-bit operand is not a row of this program");
    if (cq_fv_op_width(x, i) != 1)
        cq_kernel_die("fconv: a one-bit operand names a 64-lane row");

    r = &x->rows[i];
    o = off[i];

    if (r->op == CQ_FVOP_EQ) {
        cq_eq_block e;

        e.a = NULL; e.b = NULL; e.W = W64;
        e.diff = cq_fv_sp(x, o, (uint32_t)W64);
        e.orr  = cq_fv_sp(x, o + (uint32_t)W64, (uint32_t)W64 - 1u);
        return cq_eq_flag(&e);
    }
    /* `carry[W]` IS `a >=u b` (cmp.h), and carry starts one vector in. */
    if (r->op == CQ_FVOP_ULT) return cq_fv_sp(x, o + (uint32_t)(2 * W64), 1u);
    return cq_fv_sp(x, o, 1u);                   /* not1, and1 sit at rel 0 */
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
static int view_collapse(const cq_fv_ctx *x, int s, int *shift, uint64_t *mask)
{
    int guard = 0;

    *shift = 0;
    *mask  = ~UINT64_C(0);
    while (s >= 0 && s < x->n && x->rows[s].op == CQ_FVOP_VIEW) {
        *mask   = shr64(x->rows[s].mask, *shift) & *mask;
        *shift += x->rows[s].shift;
        s       = x->rows[s].s0;
        if (++guard > x->n)
            cq_kernel_die("fconv: a view chain does not terminate");
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

/* A NON-view 64-lane operand: the rail (widened when the source is narrower
 * than 64) or a constant span.
 *
 * THE ZEXT IS THE WHOLE OF `uitofp` AND IT IS WIRING (instructions.jl:7677-79
 * under PRD-v2 §7.3's amendment). Lanes [a_w, 64) are CQ_BIT_ZERO entries of a
 * read-only view: no copy, no scratch, no gate — and the row program above it
 * is `sitofp`'s, unchanged. There is deliberately NO `sext` arm: `sitofp` is
 * shipped at F == 64 only, where instructions.jl:7672-7673 emits no widening
 * cast at all, so an `sext` here would be a widening nothing reaches. */
static const cq_bit *base64(const cq_fv_ctx *x, const uint32_t *off, int s,
                            cq_bit *buf)
{
    if (s >= 0) {
        if (s >= x->n)
            cq_kernel_die("fconv: an operand names a row outside the program");
        if (cq_fv_op_width(x, s) != W64)
            cq_kernel_die("fconv: a 64-lane operand names a one-bit row");
        return cq_fv_row_out(x, off, s);
    }
    if (s == CQ_FV_A) {
        if (x->a == NULL) cq_kernel_die("fconv: this block has no input `a`");
        if (x->a_w <= 0 || x->a_w > W64)
            cq_kernel_die("fconv: the source width is outside (0, 64]");
        if (x->a_w == W64) return x->a;
        for (int i = 0; i < W64; i++)
            buf[i] = (i < x->a_w) ? x->a[i] : cq_bit_zero();
        return buf;
    }
    cq_fp_const(buf, const_of(s));
    return buf;
}

const cq_bit *cq_fv_op64(const cq_fv_ctx *x, const uint32_t *off, int s,
                         cq_bit *buf, cq_bit *tmp)
{
    if (s >= 0 && s < x->n && x->rows[s].op == CQ_FVOP_VIEW) {
        int shift;
        uint64_t mask;
        int b = view_collapse(x, s, &shift, &mask);

        view_fill(base64(x, off, b, tmp), shift, mask, buf);
        return buf;
    }
    return base64(x, off, s, buf);
}
