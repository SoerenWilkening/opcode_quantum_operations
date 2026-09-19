/* src/kernels/fma_step.c — M39, K20. THE COSTS AND THE LAYOUT: what a row
 * costs and where its spans lie inside the caller's region. Operand
 * resolution is next door in fma_operand.c and the dispatch in fma_emit.c, on
 * the seams fma_int.h records; the row table is in fma_rows.inc.
 *
 * NOT ONE LINE HERE KNOWS ANY JULIA. Every cost is ASKED of another module —
 * cq_eq_steps, cq_ult_steps, cq_slt_steps, cq_add_steps, cq_sub_steps,
 * cq_mux_steps, cq_mul_steps, cq_barrel_steps, cq_fp_class_steps,
 * cq_norm52_steps, cq_subnorm_steps, cq_round_steps and the matching _region
 * functions — or is upstream's own four-gate bitwise vocabulary, and nothing
 * would move if the row table changed.
 *
 * THE BARREL'S COST TAKES A DIRECTION AND THAT IS FORCED (shift_var.h):
 * `shl`/`lshr` emit `W - 2^k` shuffle CNOTs per stage because the
 * out-of-range source index does not exist, while `ashr`'s else-branch clamps
 * to the sign bit and writes all `W`. K20 uses only `shl` and `lshr`, but the
 * direction still has to reach `cq_barrel_steps` — asking for the wrong one
 * would move the slot count by `64` per stage per barrel, over twelve
 * barrels, and a slot count that disagrees with the dispatch is a wrong gate
 * rather than a failure. `cq_fu_barrel_dir_of` is the ONE place the mapping
 * lives; fma_emit.c calls the same function.
 *
 * THE PREFIX-OFFSET WALK IS DONE ONCE PER STEP AND NOT ONCE PER LOOKUP. A row
 * reference resolves to a span, and a span needs the sum of every earlier
 * row's region; doing that per operand would make the step O(n^2) in a
 * 395-row program driven a quarter of a million times per compute half.
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

/* `lower_not1!` is CNOT(w, r) then NOT(r) into a FRESH wire — two slots, one
 * bit (arith.jl:474-478). `lower_and!` is one Toffoli per lane, so one slot
 * and no bit of its own at one lane (:268-272). `lower_or!` is CNOT, CNOT,
 * Toffoli per lane, so three slots and one bit (:274-282); `lower_xor!` is
 * CNOT, CNOT, so two slots per lane and W bits (:284-291). Transcribed from
 * the pinned source because they are emitted directly next door and no module
 * publishes a cost for them (PRD-v2 §7.10). */
enum { FA_NOT1_STEPS = 2, FA_AND1_STEPS = 1, FA_OR1_STEPS = 3,
       FA_AND_PER_LANE = 1, FA_OR_PER_LANE = 3, FA_XOR_PER_LANE = 2 };

int cq_fu_barrel_dir_of(int op)
{
    if (op == CQ_FUOP_BSHL)  return (int)CQ_BARREL_SHL;
    if (op == CQ_FUOP_BLSHR) return (int)CQ_BARREL_LSHR;
    cq_kernel_die("fma: a barrel direction asked of a row that is no barrel");
    return 0;
}

int cq_fu_row_steps(const cq_fma_row *r)
{
    switch (r->op) {
    case CQ_FUOP_VIEW: case CQ_FUOP_OUT: return 0;
    case CQ_FUOP_CLASS:   return cq_fp_class_steps((cq_fp_class)r->s1);
    case CQ_FUOP_EQ:      return cq_eq_steps(W64);
    case CQ_FUOP_ULT:     return cq_ult_steps(W64);
    case CQ_FUOP_SLT:     return cq_slt_steps(W64);
    case CQ_FUOP_ADD:     return cq_add_steps(W64);
    case CQ_FUOP_SUB:     return cq_sub_steps(W64);
    case CQ_FUOP_MUX:     return cq_mux_steps(W64);
    case CQ_FUOP_AND:     return FA_AND_PER_LANE * W64;
    case CQ_FUOP_OR:      return FA_OR_PER_LANE  * W64;
    case CQ_FUOP_XOR:     return FA_XOR_PER_LANE * W64;
    case CQ_FUOP_MUL:     return cq_mul_steps(W64);
    case CQ_FUOP_BSHL: case CQ_FUOP_BLSHR:
        return cq_barrel_steps(W64,
                               (cq_barrel_dir)cq_fu_barrel_dir_of(r->op));
    case CQ_FUOP_NORM52:  return cq_norm52_steps();
    case CQ_FUOP_SUBNORM: return cq_subnorm_steps();
    case CQ_FUOP_ROUND:   return cq_round_steps();
    case CQ_FUOP_NOT1:    return FA_NOT1_STEPS;
    case CQ_FUOP_AND1:    return FA_AND1_STEPS;
    case CQ_FUOP_OR1:     return FA_OR1_STEPS;
    case CQ_FUOP_N_OP:
    default: break;
    }
    cq_kernel_die("fma: the cost of an unknown row op");
    return 0;
}

uint32_t cq_fu_row_region(const cq_fma_row *r)
{
    switch (r->op) {
    case CQ_FUOP_VIEW: case CQ_FUOP_OUT: return 0u;
    case CQ_FUOP_CLASS:   return cq_fp_class_region((cq_fp_class)r->s1);
    case CQ_FUOP_EQ:      return (uint32_t)cq_eq_region(W64);
    case CQ_FUOP_ULT:     return (uint32_t)cq_ult_region(W64);
    case CQ_FUOP_SLT:     return (uint32_t)cq_slt_region(W64);
    case CQ_FUOP_ADD:     return (uint32_t)cq_add_region(W64);
    case CQ_FUOP_SUB:     return (uint32_t)cq_sub_region(W64);
    case CQ_FUOP_MUX:     return (uint32_t)cq_mux_region(W64);
    case CQ_FUOP_AND: case CQ_FUOP_OR: case CQ_FUOP_XOR:
        return (uint32_t)W64;
    case CQ_FUOP_MUL:     return (uint32_t)cq_mul_region(W64);
    case CQ_FUOP_BSHL: case CQ_FUOP_BLSHR:
        return (uint32_t)cq_barrel_region(W64);
    case CQ_FUOP_NORM52:  return cq_norm52_region();
    case CQ_FUOP_SUBNORM: return cq_subnorm_region();
    case CQ_FUOP_ROUND:   return cq_round_region();
    case CQ_FUOP_NOT1: case CQ_FUOP_AND1: case CQ_FUOP_OR1:
        return 1u;                       /* one output bit                */
    case CQ_FUOP_N_OP:
    default: break;
    }
    cq_kernel_die("fma: the region of an unknown row op");
    return 0u;
}

/* BUILT ONCE, FROM TWO COMPILE-TIME CONSTANTS. See fma_int.h for why this is
 * not the cached state Rule 13 forbids. The library is single-threaded — there
 * is no thread anywhere in it — so a plain one-shot flag is the whole of the
 * initialisation. */
const cq_fu_map *cq_fu_map_get(void)
{
    static cq_fu_map m;
    static int built = 0;
    const cq_fma_row *rows;
    uint32_t bits = 0u;
    int slots = 0, n;

    if (built) return &m;
    rows = cq_fma_rows(&n);
    for (int i = 0; i < n; i++) {
        m.rel[i]   = bits;
        m.step0[i] = slots;
        bits  += cq_fu_row_region(&rows[i]);
        slots += cq_fu_row_steps(&rows[i]);
    }
    m.region = bits;
    m.steps  = slots;
    m.n      = n;
    built    = 1;
    return &m;
}

/* A BINARY SEARCH, WHICH IS WHAT THE MEMOISATION BUYS. `step0` is
 * non-decreasing and a row may own ZERO slots (73 views and 15 projections
 * do), so the search must land on the LAST row whose `step0 <= u` — the
 * first-match spelling would stop at a zero-width view and dispatch the wrong
 * op with the wrong operands.
 *
 * THE LAST-MATCH POSTCONDITION IS THE SEARCH'S OWN AND MUST NOT BE REPAIRED
 * AFTERWARDS. The first draft followed this loop with
 * `while (best + 1 < n && step0[best + 1] <= u) best++;`, which is DEAD —
 * recording `best = mid` only on the `<=` arm and then moving `lo` right makes
 * the final `best` the greatest index satisfying the predicate already. Dead
 * was not the problem: it REPAIRED the first-match mutant (`hi = mid - 1` in
 * place of `lo = mid + 1`) in silence, so the one spelling this module's own
 * banner names as the trap of the landing left every test green — MEASURED,
 * 17 of 17 cases green in Release with the walk in place and the mutant
 * applied. Removed, and `tests/test_kernel_fma_map.inc`'s
 * `the_prefix_map_names_the_last_row_at_a_zero_slot_boundary` drives this
 * function directly against a linear-scan oracle at every boundary; with the
 * walk gone the same mutant is named at slot 0. */
int cq_fu_row_at(const cq_fu_map *m, int u, int *within)
{
    int lo = 0, hi = m->n - 1, best = 0;

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;

        if (m->step0[mid] <= u) { best = mid; lo = mid + 1; }
        else                    { hi = mid - 1; }
    }
    *within = u - m->step0[best];
    return best;
}

uint32_t cq_fma_region(void) { return cq_fu_map_get()->region; }

int cq_fma_steps(void) { return cq_fu_map_get()->steps; }

/* The prefix-offset walk plus the fit check, in ONE pass.
 *
 * The region check is the BLOCK's, and it has to be: without it the first
 * out-of-region span aborts in M08 naming the REGION rather than the consumer
 * that mis-sized its offset, and the two-programs-in-one-region shape then has
 * no diagnostic of its own. Hard error in both configurations. */
void cq_fu_arm(const cq_fma_block *k, uint32_t *off)
{
    const cq_fu_map *m = cq_fu_map_get();

    if (k == NULL || k->scr == NULL)
        cq_kernel_die("fma: the block has no region");
    /* THE ONE PLACE `k->off` IS APPLIED. The map is base-relative; this loop
     * is the whole of the rebasing, which is what keeps a dropped `off` a
     * single-line mutation that only a second program in one region sees. */
    for (int i = 0; i < m->n; i++) off[i] = k->off + m->rel[i];
    if ((uint64_t)k->off + (uint64_t)m->region
        > (uint64_t)cq_scratch_size(k->scr))
        cq_kernel_die("fma: the block's region does not fit at its offset");
}

/* `at` is ABSOLUTE inside the region: the prefix walk has already added
 * `k->off`, which is the one place this block's base is applied. */
cq_bit *cq_fu_sp(const cq_fma_block *k, uint32_t at, uint32_t len)
{
    return cq_scratch_span(k->scr, at, len);
}
