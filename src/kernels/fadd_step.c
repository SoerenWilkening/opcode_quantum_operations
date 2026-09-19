/* src/kernels/fadd_step.c — M33, K15. THE LAYOUT: what a row costs, how many
 * bits it owns, and where its spans lie inside the caller's region. What each
 * operand code RESOLVES to is next door in fadd_operand.c, on a THIRD seam —
 * COSTS-AND-LAYOUT <-> OPERAND RESOLUTION — taken at implementation because the
 * two halves together measured 326 of Rule 12's 300 non-blank non-comment
 * lines. That is one cut more than M32 needed, and the reason is in the numbers:
 * M32's machine drives 154 rows over fourteen ops and M33's drives 133 over
 * NINETEEN, three of which resolve a DESTRUCTURED TUPLE rather than a span. The
 * dispatch and the surface are in fadd_emit.c, on the seam fadd_int.h records;
 * the two row tables are in fadd.c, on M36's ROW TABLES <-> STEP MACHINE seam
 * (K18.md D-K18-7).
 *
 * NOT ONE LINE HERE KNOWS ANY JULIA. Every cost is ASKED of another module —
 * cq_eq_steps, cq_ult_steps, cq_sub_steps, cq_add_steps, cq_mux_steps,
 * cq_barrel_steps, cq_fp_class_steps, cq_clz_steps, cq_subnorm_steps,
 * cq_round_steps and the matching _region functions — or is upstream's own
 * three-gate bitwise vocabulary, and nothing would move if a row table changed.
 *
 * THE PREFIX-OFFSET WALK IS DONE ONCE PER STEP AND NOT ONCE PER LOOKUP. A row
 * reference resolves to a span, and a span needs the sum of every earlier
 * row's region; doing that per operand would make the step O(n^2) in a 133-row
 * program driven ~48,000 times per call.
 *
 * I6(a) IS SATISFIED WITH NOTHING LEFT TO CHECK. `cq_fa_sp` is the only thing
 * here that hands back a WRITABLE pointer, and every one of them is a bit of
 * the caller's region at `off + <span>`.
 */

#include "kernels/fadd_int.h"

#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/fpclass.h"
#include "kernels/kernel.h"
#include "kernels/mux.h"
#include "kernels/shift_var.h"

_Static_assert(CQ_FA_MAXR >= 144, "fsub's program is 133 rows");

/* `CQ_FA_W` spelled short, for the same reason fcmp_step.c spells it `W64`:
 * every span expression below carries it two or three times. */
enum { W64 = CQ_FA_W };

/* `lower_not1!` is CNOT(w, r) then NOT(r) into a FRESH wire — two slots, one
 * bit (arith.jl:474-478). `lower_and!` is one Toffoli per lane, so W slots and
 * W bits (:268-272). `lower_or!` is CNOT, CNOT, Toffoli per lane, so 3W slots
 * and W bits (:274-282). `lower_xor!` is CNOT, CNOT per lane, so 2W slots and
 * W bits (:284-291). Transcribed from the pinned source because they are
 * emitted directly in fadd_emit.c and no module publishes a cost for them
 * (PRD-v2 §7.10). */
enum { FA_NOT1_STEPS = 2, FA_AND1_STEPS = 1, FA_OR1_STEPS = 3 };

cq_fp_class cq_fa_class_of(const cq_fadd_row *r)
{
    if (r->shift < 0 || r->shift >= (short)CQ_FP_N_CLASS)
        cq_kernel_die("fadd: a class row names no cq_fp_class");
    return (cq_fp_class)r->shift;
}

int cq_fa_row_steps(const cq_fadd_row *r)
{
    switch (r->op) {
    case CQ_FAOP_VIEW: case CQ_FAOP_PICK: return 0;
    case CQ_FAOP_CLASS:   return cq_fp_class_steps(cq_fa_class_of(r));
    case CQ_FAOP_EQ:      return cq_eq_steps(W64);
    case CQ_FAOP_ULT:     return cq_ult_steps(W64);
    case CQ_FAOP_SUB:     return cq_sub_steps(W64);
    case CQ_FAOP_ADD:     return cq_add_steps(W64);
    case CQ_FAOP_MUX:     return cq_mux_steps(W64);
    case CQ_FAOP_AND:     return W64;
    case CQ_FAOP_OR:      return 3 * W64;
    case CQ_FAOP_XOR:     return 2 * W64;
    case CQ_FAOP_BSHL:    return cq_barrel_steps(W64, CQ_BARREL_SHL);
    case CQ_FAOP_BLSHR:   return cq_barrel_steps(W64, CQ_BARREL_LSHR);
    case CQ_FAOP_NOT1:    return FA_NOT1_STEPS;
    case CQ_FAOP_AND1:    return FA_AND1_STEPS;
    case CQ_FAOP_OR1:     return FA_OR1_STEPS;
    case CQ_FAOP_CLZ:     return cq_clz_steps();
    case CQ_FAOP_SUBNORM: return cq_subnorm_steps();
    case CQ_FAOP_ROUND:   return cq_round_steps();
    default: break;
    }
    cq_kernel_die("fadd: unknown op in the program");
    return 0;
}

static uint32_t row_region(const cq_fadd_row *r)
{
    switch (r->op) {
    case CQ_FAOP_VIEW: case CQ_FAOP_PICK: return 0u;
    case CQ_FAOP_CLASS:   return cq_fp_class_region(cq_fa_class_of(r));
    case CQ_FAOP_EQ:      return (uint32_t)cq_eq_region(W64);
    case CQ_FAOP_ULT:     return (uint32_t)cq_ult_region(W64);
    case CQ_FAOP_SUB:     return (uint32_t)cq_sub_region(W64);
    case CQ_FAOP_ADD:     return (uint32_t)cq_add_region(W64);
    case CQ_FAOP_MUX:     return (uint32_t)cq_mux_region(W64);
    case CQ_FAOP_AND: case CQ_FAOP_OR: case CQ_FAOP_XOR:
        return (uint32_t)W64;
    case CQ_FAOP_BSHL: case CQ_FAOP_BLSHR:
        return (uint32_t)cq_barrel_region(W64);
    case CQ_FAOP_CLZ:     return cq_clz_region();
    case CQ_FAOP_SUBNORM: return cq_subnorm_region();
    case CQ_FAOP_ROUND:   return cq_round_region();
    default:              return 1u;    /* not1, and1, or1: one output bit */
    }
}

/* Which of the picked outputs are ONE bit, in fpround.h's declaration order.
 * Asked here rather than in fadd.c because it is a fact about M32's surface
 * and not about the Julia. */
int cq_fa_pick_is_flag(int block_op, int which)
{
    if (block_op == CQ_FAOP_SUBNORM)
        return which == CQ_FA_PICK_FLAG || which == CQ_FA_PICK_FTZ;
    if (block_op == CQ_FAOP_ROUND)
        return which == CQ_FA_PICK_EXPOVF || which == CQ_FA_PICK_EXPOVFA;
    return 0;                                   /* clz returns two 64s */
}

int cq_fa_op_width(const cq_fa_ctx *x, int i)
{
    const cq_fadd_row *r;

    if (i < 0 || i >= x->n)
        cq_kernel_die("fadd: an operand names a row outside the program");
    r = &x->rows[i];
    switch (r->op) {
    case CQ_FAOP_CLASS: case CQ_FAOP_EQ:   case CQ_FAOP_ULT:
    case CQ_FAOP_NOT1:  case CQ_FAOP_AND1: case CQ_FAOP_OR1:
        return 1;
    case CQ_FAOP_PICK:
        if (r->s0 < 0 || r->s0 >= x->n)
            cq_kernel_die("fadd: a pick names a row outside the program");
        return cq_fa_pick_is_flag(x->rows[r->s0].op, r->shift) ? 1 : W64;
    default: break;
    }
    return W64;
}

void cq_fa_check_program(const cq_fadd_row *rows, int n)
{
    if (rows == NULL || n <= 0 || n > CQ_FA_MAXR)
        cq_kernel_die("fadd: the program is empty or longer than "
                      "CQ_FADD_MAX_ROWS");
}

uint32_t cq_fa_region_of(const cq_fa_ctx *x)
{
    uint32_t bits = 0u;

    cq_fa_check_program(x->rows, x->n);
    for (int i = 0; i < x->n; i++) bits += row_region(&x->rows[i]);
    return bits;
}

int cq_fa_steps_of(const cq_fa_ctx *x)
{
    int slots = 0;

    cq_fa_check_program(x->rows, x->n);
    for (int i = 0; i < x->n; i++) slots += cq_fa_row_steps(&x->rows[i]);
    return slots;
}

/* The prefix-offset walk, which also returns the region total — so the fit
 * check and the offsets come out of ONE pass. */
static uint32_t walk(const cq_fa_ctx *x, uint32_t *off)
{
    uint32_t o = x->off;

    for (int i = 0; i < x->n; i++) {
        off[i] = o;
        o += row_region(&x->rows[i]);
    }
    return o - x->off;
}

/* The region check is the PROGRAM's, and it has to be: without it the first
 * out-of-region span aborts in M08 naming the REGION rather than the consumer
 * that mis-sized its offset, and the two-programs-in-one-region shape then has
 * no diagnostic of its own. Hard error in both configurations. */
void cq_fa_arm(const cq_fa_ctx *x, uint32_t *off)
{
    cq_fa_check_program(x->rows, x->n);
    if (x->scr == NULL) cq_kernel_die("fadd: the block has no region");
    if ((uint64_t)x->off + walk(x, off) > (uint64_t)cq_scratch_size(x->scr))
        cq_kernel_die("fadd: the block's region does not fit at its offset");
}

/* `at` is ABSOLUTE inside the region: the prefix walk has already added
 * `x->off`, which is the one place this program's base is applied. */
cq_bit *cq_fa_sp(const cq_fa_ctx *x, uint32_t at, uint32_t len)
{
    return cq_scratch_span(x->scr, at, len);
}
