/* src/kernels/fsqrt_step.c — M40, K21. THE COSTS AND THE LAYOUT: what a row
 * costs, and where its spans lie inside the caller's region. Operand
 * resolution is next door in fsqrt_operand.c and the dispatch in
 * fsqrt_emit.c, on the seams fsqrt_int.h records; the row table is in
 * fsqrt.c, on M36's ROW TABLES <-> STEP MACHINE seam.
 *
 * NOT ONE LINE HERE KNOWS ANY JULIA. Every cost is ASKED of another module —
 * cq_fp_class_steps, cq_eq_steps, cq_ult_steps, cq_add_steps, cq_sub_steps,
 * cq_mux_steps, cq_norm52_steps, cq_round_steps and the matching _region
 * functions — or is upstream's own three-gate `lower_or!`, two-gate
 * `lower_not1!` and one-gate `lower_and!`, and nothing would move if the row
 * table changed.
 *
 * THE PREFIX WALK IS DONE ONCE FOR THE WHOLE PROCESS AND NOT ONCE PER STEP,
 * and fsqrt_int.h argues why that is not a cursor. The row lookup is then a
 * binary search, which is what makes a 1,070-row program drivable 157,108
 * times per compute half without the decode dominating the emission.
 */

#include "kernels/fsqrt_int.h"

#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/fpclass.h"
#include "kernels/fpround.h"
#include "kernels/kernel.h"
#include "kernels/mux.h"

/* `CQ_FS_W` spelled short, for fcmp_step.c's reason: every span expression
 * below carries it two or three times. */
enum { W64 = CQ_FS_W };

/* --- Slot and region arithmetic, ASKED of the owning modules. ------------- */

/* `lower_not1!` is CNOT(w, r) then NOT(r) into a FRESH wire — two slots, one
 * bit (arith.jl:474-478). `lower_and!` is one Toffoli per lane, so one slot
 * and no bit of its own at one lane (:268-272). `lower_or!` is CNOT, CNOT,
 * Toffoli per lane, so three slots and one bit (:274-282). Transcribed from
 * the pinned source because they are emitted directly next door and no module
 * publishes a cost for them (PRD-v2 §7.10). */
enum { FS_NOT1_STEPS = 2, FS_AND1_STEPS = 1, FS_OR_PER_LANE = 3 };

int cq_fs_row_steps(const cq_fsqrt_row *r)
{
    switch (r->op) {
    case CQ_FSOP_VIEW: case CQ_FSOP_SVIEW: case CQ_FSOP_OUT: return 0;
    case CQ_FSOP_CLASS:  return cq_fp_class_steps((cq_fp_class)r->s1);
    case CQ_FSOP_EQ:     return cq_eq_steps(W64);
    case CQ_FSOP_ULT:    return cq_ult_steps(W64);
    case CQ_FSOP_ADD:    return cq_add_steps(W64);
    case CQ_FSOP_SUB:    return cq_sub_steps(W64);
    case CQ_FSOP_MUX:    return cq_mux_steps(W64);
    case CQ_FSOP_OR:     return FS_OR_PER_LANE * W64;
    case CQ_FSOP_NORM52: return cq_norm52_steps();
    case CQ_FSOP_ROUND:  return cq_round_steps();
    case CQ_FSOP_NOT1:   return FS_NOT1_STEPS;
    case CQ_FSOP_AND1:   return FS_AND1_STEPS;
    case CQ_FSOP_N_OP:
    default: break;
    }
    cq_kernel_die("fsqrt: unknown op in the program");
    return 0;
}

uint32_t cq_fs_row_region(const cq_fsqrt_row *r)
{
    switch (r->op) {
    case CQ_FSOP_VIEW: case CQ_FSOP_SVIEW: case CQ_FSOP_OUT: return 0u;
    case CQ_FSOP_CLASS:  return cq_fp_class_region((cq_fp_class)r->s1);
    case CQ_FSOP_EQ:     return (uint32_t)cq_eq_region(W64);
    case CQ_FSOP_ULT:    return (uint32_t)cq_ult_region(W64);
    case CQ_FSOP_ADD:    return (uint32_t)cq_add_region(W64);
    case CQ_FSOP_SUB:    return (uint32_t)cq_sub_region(W64);
    case CQ_FSOP_MUX:    return (uint32_t)cq_mux_region(W64);
    case CQ_FSOP_OR:     return (uint32_t)W64;
    case CQ_FSOP_NORM52: return cq_norm52_region();
    case CQ_FSOP_ROUND:  return cq_round_region();
    case CQ_FSOP_NOT1: case CQ_FSOP_AND1:
        return 1u;                             /* one output bit           */
    case CQ_FSOP_N_OP:
    default: break;
    }
    cq_kernel_die("fsqrt: unknown op in the program");
    return 0u;
}

/* --- The memoised prefix map. -------------------------------------------- */

static cq_fs_map g_map;
static int       g_ready;

const cq_fs_map *cq_fs_map_get(void)
{
    if (!g_ready) {
        const cq_fsqrt_row *rows;
        uint32_t b = 0u;
        int n, s = 0;

        rows = cq_fsqrt_rows(&n);
        if (n > CQ_FS_MAXR)
            cq_kernel_die("fsqrt: the program outgrew the prefix map");
        for (int i = 0; i < n; i++) {
            g_map.slot[i] = s;
            g_map.bit[i]  = b;
            s += cq_fs_row_steps(&rows[i]);
            b += cq_fs_row_region(&rows[i]);
        }
        g_map.slot[n] = s;
        g_map.bit[n]  = b;
        g_map.nslot   = s;
        g_map.nbit    = b;
        g_ready = 1;
    }
    return &g_map;
}

uint32_t cq_fsqrt_region(void) { return cq_fs_map_get()->nbit;  }
int      cq_fsqrt_steps (void) { return cq_fs_map_get()->nslot; }

/* The last `i` with `slot[i] <= u`. A run of zero-slot rows shares one slot
 * value, so this lands on the LAST of the run — which is the emitting row,
 * because `slot[i] < slot[i+1]` holds exactly there. */
int cq_fs_row_at(int u)
{
    const cq_fs_map *m = cq_fs_map_get();
    int lo = 0, hi = cq_fsqrt_n_rows();

    if (u < 0 || u >= m->nslot)
        cq_kernel_die("fsqrt: a slot index outside the program");
    while (hi - lo > 1) {
        int mid = lo + (hi - lo) / 2;

        if (m->slot[mid] <= u) lo = mid; else hi = mid;
    }
    return lo;
}

void cq_fs_arm(const cq_fsqrt_block *k)
{
    const cq_fs_map *m;

    if (k == NULL || k->scr == NULL)
        cq_kernel_die("fsqrt: the block has no region");
    m = cq_fs_map_get();
    if ((uint64_t)k->off + (uint64_t)m->nbit > (uint64_t)cq_scratch_size(k->scr))
        cq_kernel_die("fsqrt: the block's region does not fit at its offset");
}

/* `at` is RELATIVE to this block's base, and this is the one place the base is
 * applied — which is load-bearing rather than tidy: dropping `k->off` slides
 * the whole program inside the caller's region and leaves the value, the
 * palindrome, the pool, every gate count AND the slot scan all correct. Only a
 * SECOND program at a SECOND offset in one region sees it. */
cq_bit *cq_fs_sp(const cq_fsqrt_block *k, uint32_t at, uint32_t len)
{
    return cq_scratch_span(k->scr, k->off + at, len);
}
