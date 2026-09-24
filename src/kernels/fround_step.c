/* src/kernels/fround_step.c — M38b, K25. THE COSTS AND THE LAYOUT: what a row
 * costs, and where its spans lie inside the caller's region. Operand
 * resolution is fround_operand.c, the M33 hand-off fround_fadd.c and the
 * dispatch fround_emit.c, on the seams fround_int.h records.
 *
 * NOT ONE LINE HERE KNOWS ANY JULIA. Every cost is ASKED of another module —
 * cq_eq_steps, cq_slt_steps, cq_sub_steps, cq_add_steps, cq_mux_steps,
 * cq_barrel_steps, M33's soft_fadd map, and the matching _region functions —
 * or is upstream's own bitwise vocabulary (arith.jl:268-291, :474-478).
 */

#include "kernels/fround_int.h"

#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/kernel.h"
#include "kernels/mux.h"
#include "kernels/shift_var.h"

enum { W64 = CQ_FI_W };

/* `lower_not1!` is CNOT then NOT into a fresh wire — two slots, one bit
 * (arith.jl:474-478). `lower_and!` is one Toffoli per lane (:268-272),
 * `lower_or!` CNOT, CNOT, Toffoli per lane (:274-282) and `lower_xor!` two
 * CNOTs per lane (:284-291), each owning one bit per lane. Transcribed from
 * the pinned source because they are emitted directly next door and no module
 * publishes a cost for them (PRD-v2 §7.10). */
enum { FI_NOT1_STEPS = 2, FI_AND_PER_LANE = 1, FI_OR_PER_LANE = 3,
       FI_XOR_PER_LANE = 2 };

int cq_fi_row_steps(const cq_fround_row *r)
{
    switch (r->op) {
    case CQ_FIOP_VIEW:  return 0;
    case CQ_FIOP_EQ:    return cq_eq_steps(W64);
    case CQ_FIOP_SLT:   return cq_slt_steps(W64);
    case CQ_FIOP_SUB:   return cq_sub_steps(W64);
    case CQ_FIOP_ADD:   return cq_add_steps(W64);
    case CQ_FIOP_MUX:   return cq_mux_steps(W64);
    case CQ_FIOP_OR:    return FI_OR_PER_LANE * W64;
    case CQ_FIOP_AND:   return FI_AND_PER_LANE * W64;
    case CQ_FIOP_XOR:   return FI_XOR_PER_LANE * W64;
    case CQ_FIOP_BSHL:  return cq_barrel_steps(W64, CQ_BARREL_SHL);
    case CQ_FIOP_BLSHR: return cq_barrel_steps(W64, CQ_BARREL_LSHR);
    case CQ_FIOP_NOT1:  return FI_NOT1_STEPS;
    case CQ_FIOP_AND1:  return FI_AND_PER_LANE;
    case CQ_FIOP_OR1:   return FI_OR_PER_LANE;
    case CQ_FIOP_FADD:  return cq_fi_fadd_steps();
    case CQ_FIOP_N_OP:
    default: break;
    }
    cq_kernel_die("fround: the cost of an unknown row op");
    return 0;
}

uint32_t cq_fi_row_region(const cq_fround_row *r)
{
    switch (r->op) {
    case CQ_FIOP_VIEW:  return 0u;
    case CQ_FIOP_EQ:    return (uint32_t)cq_eq_region(W64);
    case CQ_FIOP_SLT:   return (uint32_t)cq_slt_region(W64);
    case CQ_FIOP_SUB:   return (uint32_t)cq_sub_region(W64);
    case CQ_FIOP_ADD:   return (uint32_t)cq_add_region(W64);
    case CQ_FIOP_MUX:   return (uint32_t)cq_mux_region(W64);
    case CQ_FIOP_OR: case CQ_FIOP_AND: case CQ_FIOP_XOR:
        return (uint32_t)W64;
    case CQ_FIOP_BSHL: case CQ_FIOP_BLSHR:
        return (uint32_t)cq_barrel_region(W64);
    case CQ_FIOP_NOT1: case CQ_FIOP_AND1: case CQ_FIOP_OR1:
        return 1u;                               /* one output bit         */
    case CQ_FIOP_FADD:  return cq_fi_fadd_region();
    case CQ_FIOP_N_OP:
    default: break;
    }
    cq_kernel_die("fround: the region of an unknown row op");
    return 0u;
}

/* --- The memoised prefix maps, one per program. -------------------------- */

static cq_fi_map g_map[CQ_FROUND_N_PROG];
static int       g_ready[CQ_FROUND_N_PROG];

const cq_fi_map *cq_fi_map_get(cq_fround_prog p)
{
    const cq_fround_row *rows;
    cq_fi_map *m;
    uint32_t b = 0u;
    int n, s = 0;

    rows = cq_fround_rows(p, &n);          /* refuses an id outside the five */
    m = &g_map[p];
    if (g_ready[p]) return m;
    if (n > CQ_FI_MAXR)
        cq_kernel_die("fround: the program outgrew the prefix map");
    for (int i = 0; i < n; i++) {
        m->slot[i] = s;
        m->bit[i]  = b;
        s += cq_fi_row_steps(&rows[i]);
        b += cq_fi_row_region(&rows[i]);
    }
    m->slot[n] = s;
    m->bit[n]  = b;
    m->n = n;
    m->nslot = s;
    m->nbit = b;
    g_ready[p] = 1;
    return m;
}

uint32_t cq_fround_region(cq_fround_prog p) { return cq_fi_map_get(p)->nbit;  }
int      cq_fround_steps (cq_fround_prog p) { return cq_fi_map_get(p)->nslot; }

/* The LAST `i` with `slot[i] <= u`. A run of zero-slot views shares one slot
 * value, so a first-match spelling stops on a VIEW and dispatches the wrong op
 * with the wrong operands — bd a-395-row-flat-table-needs-the-memoised-
 * prefix-map's one silent trap; the dispatch also refuses a zero-slot row. */
int cq_fi_row_at(cq_fround_prog p, int u)
{
    const cq_fi_map *m = cq_fi_map_get(p);
    int lo = 0, hi = m->n;

    if (u < 0 || u >= m->nslot)
        cq_kernel_die("fround: a slot index outside the program");
    while (hi - lo > 1) {
        int mid = lo + (hi - lo) / 2;

        if (m->slot[mid] <= u) lo = mid; else hi = mid;
    }
    return lo;
}

void cq_fi_arm(const cq_fround_block *k)
{
    const cq_fi_map *m;

    if (k == NULL || k->scr == NULL)
        cq_kernel_die("fround: the block has no region");
    if ((unsigned)k->prog >= (unsigned)CQ_FROUND_N_PROG)
        cq_kernel_die("fround: the block names a program outside the five");
    m = cq_fi_map_get(k->prog);
    if ((uint64_t)k->off + (uint64_t)m->nbit > (uint64_t)cq_scratch_size(k->scr))
        cq_kernel_die("fround: the block's region does not fit at its offset");
}

/* `at` is RELATIVE to this block's base, and `cq_fi_base` is the one place the
 * base is applied — every span AND every inner region-owning block's `off`
 * (the barrel, M33's fadd) goes through it: dropping `k->off` slides the whole
 * program inside the caller's region and leaves the value, the palindrome, the
 * pool, every count and the slot scan all correct. Only a SECOND program at a
 * SECOND offset sees it. */
uint32_t cq_fi_base(const cq_fround_block *k, uint32_t at)
{
    return k->off + at;
}

cq_bit *cq_fi_sp(const cq_fround_block *k, uint32_t at, uint32_t len)
{
    return cq_scratch_span(k->scr, cq_fi_base(k, at), len);
}
