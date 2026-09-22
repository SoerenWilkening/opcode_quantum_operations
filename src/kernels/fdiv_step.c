/* src/kernels/fdiv_step.c — M35, K17. THE COSTS AND THE LAYOUT: what a row
 * costs, where its spans lie inside the caller's region, and which row a slot
 * index falls in. Operand resolution is next door in fdiv_operand.c and the
 * dispatch in fdiv_emit.c, on the seams fdiv_int.h records; the row tables are
 * in fdiv.c.
 *
 * NOT ONE LINE HERE KNOWS ANY JULIA. Every cost is ASKED of another module —
 * cq_eq_steps, cq_ult_steps, cq_add_steps, cq_sub_steps, cq_mux_steps,
 * cq_fp_class_steps, cq_norm52_steps, cq_clz_steps, cq_subnorm_steps,
 * cq_round_steps and the matching _region functions — or is upstream's own
 * four-gate bitwise vocabulary, and nothing would move if the row table
 * changed.
 *
 * THE BASE-ZERO MAP IS BUILT ONCE AND CONTAINS IMMUTABLE INTEGER METADATA.
 * A block invocation copies and rebases it once, never caching operands or
 * circuit state. Segmentation keeps both that preparation and lookup compact:
 * every iteration of fdiv.jl:91 is the same seven rows, so its offsets are
 * arithmetic instead of a 478-entry flat table.
 *
 * I6(a) IS SATISFIED WITH NOTHING LEFT TO CHECK. `cq_fd_sp` is the only thing
 * here that hands back a WRITABLE pointer and every one of them is a bit of
 * the caller's region at `off + <span>`.
 */

#include "kernels/fdiv_int.h"

#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/fpclass.h"
#include "kernels/fpround.h"
#include "kernels/kernel.h"
#include "kernels/mux.h"

/* `CQ_FD_W` spelled short, for fcmp_step.c's reason: every span expression
 * below carries it two or three times. */
enum { W64 = CQ_FD_W };

/* --- Slot and region arithmetic, ASKED of the owning modules. ------------- */

/* `lower_not1!` is CNOT(w, r) then NOT(r) into a FRESH wire — two slots, one
 * bit (arith.jl:474-478). `lower_and!` is one Toffoli per lane, so one slot
 * and no bit of its own at one lane (:268-272). `lower_or!` is CNOT, CNOT,
 * Toffoli per lane, so three slots and one bit (:274-282); `lower_xor!` is
 * CNOT, CNOT, so two slots per lane and W bits (:284-291). Transcribed from
 * the pinned source because they are emitted directly next door and no module
 * publishes a cost for them (PRD-v2 §7.10). */
enum { FD_NOT1_STEPS = 2, FD_AND1_STEPS = 1, FD_OR1_STEPS = 3,
       FD_OR_PER_LANE = 3, FD_XOR_PER_LANE = 2 };

int cq_fd_row_steps(const cq_fdiv_row *r)
{
    switch (r->op) {
    case CQ_FDOP_VIEW: case CQ_FDOP_OUT: return 0;
    case CQ_FDOP_CLASS:   return cq_fp_class_steps((cq_fp_class)r->s1);
    case CQ_FDOP_EQ:      return cq_eq_steps(W64);
    case CQ_FDOP_ULT:     return cq_ult_steps(W64);
    case CQ_FDOP_ADD:     return cq_add_steps(W64);
    case CQ_FDOP_SUB:     return cq_sub_steps(W64);
    case CQ_FDOP_MUX:     return cq_mux_steps(W64);
    case CQ_FDOP_OR:      return FD_OR_PER_LANE * W64;
    case CQ_FDOP_XOR:     return FD_XOR_PER_LANE * W64;
    case CQ_FDOP_NORM52:  return cq_norm52_steps();
    case CQ_FDOP_CLZ:     return cq_clz_steps();
    case CQ_FDOP_SUBNORM: return cq_subnorm_steps();
    case CQ_FDOP_ROUND:   return cq_round_steps();
    case CQ_FDOP_NOT1:    return FD_NOT1_STEPS;
    case CQ_FDOP_AND1:    return FD_AND1_STEPS;
    case CQ_FDOP_OR1:     return FD_OR1_STEPS;
    case CQ_FDOP_N_OP:
    default: break;
    }
    cq_kernel_die("fdiv: unknown op in the program");
    return 0;
}

uint32_t cq_fd_row_region(const cq_fdiv_row *r)
{
    switch (r->op) {
    case CQ_FDOP_VIEW: case CQ_FDOP_OUT: return 0u;
    case CQ_FDOP_CLASS:   return cq_fp_class_region((cq_fp_class)r->s1);
    case CQ_FDOP_EQ:      return (uint32_t)cq_eq_region(W64);
    case CQ_FDOP_ULT:     return (uint32_t)cq_ult_region(W64);
    case CQ_FDOP_ADD:     return (uint32_t)cq_add_region(W64);
    case CQ_FDOP_SUB:     return (uint32_t)cq_sub_region(W64);
    case CQ_FDOP_MUX:     return (uint32_t)cq_mux_region(W64);
    case CQ_FDOP_OR: case CQ_FDOP_XOR: return (uint32_t)W64;
    case CQ_FDOP_NORM52:  return cq_norm52_region();
    case CQ_FDOP_CLZ:     return cq_clz_region();
    case CQ_FDOP_SUBNORM: return cq_subnorm_region();
    case CQ_FDOP_ROUND:   return cq_round_region();
    case CQ_FDOP_NOT1: case CQ_FDOP_AND1: case CQ_FDOP_OR1:
        return 1u;                       /* one output bit                */
    case CQ_FDOP_N_OP:
    default: break;
    }
    cq_kernel_die("fdiv: unknown op in the program");
    return 0u;
}

/* --- The segmented prefix map. ------------------------------------------- */

/* One segment's prefixes. `first` is the absolute row index of its first row,
 * `n` its length, `base` the absolute bit offset it starts at and `slot0` the
 * slot index it starts at. Fills n+1 entries of each array and returns the
 * segment's totals through `*bits` / `*slots`. */
static void seg(int first, int n, uint32_t base, int slot0,
                uint32_t *off, int *sl, uint32_t *bits, int *slots)
{
    uint32_t o = base;
    int s = slot0;

    for (int i = 0; i < n; i++) {
        cq_fdiv_row r;

        cq_fdiv_row_at(first + i, &r);
        off[i] = o;
        sl[i]  = s;
        o += cq_fd_row_region(&r);
        s += cq_fd_row_steps(&r);
    }
    off[n] = o;
    sl[n]  = s;
    *bits  = o - base;
    *slots = s - slot0;
}

void cq_fd_map_build(cq_fd_map *m, uint32_t base)
{
    uint32_t bits;
    int slots;

    m->n_pre  = cq_fdiv_loop_base_row();
    m->n_post = cq_fdiv_n_rows() - cq_fdiv_post_base_row();
    if (m->n_pre > CQ_FD_MAXSEG || m->n_post > CQ_FD_MAXSEG)
        cq_kernel_die("fdiv: a program segment outgrew the prefix map");

    seg(0, m->n_pre, base, 0, m->pre, m->pre_s, &bits, &slots);
    m->loop_off  = m->pre[m->n_pre];
    m->loop_slot = m->pre_s[m->n_pre];

    /* ONE ITERATION, RELATIVE TO ITSELF. Every instantiation of fdiv.jl:91's
     * body has the same seven ops, so its internal layout is the same seven
     * offsets at every `t`; only the base moves. That is the arithmetic the
     * whole segmented decode rests on, and it is also exactly what a
     * K12-shaped slide corrupts — see the span scan. */
    seg(cq_fdiv_loop_base_row(), CQ_FDIV_ITER_ROWS, 0u, 0,
        m->iter, m->iter_s, &m->iter_bits, &m->iter_steps);

    m->post_off  = m->loop_off  + (uint32_t)CQ_FDIV_N_ITERS * m->iter_bits;
    m->post_slot = m->loop_slot + CQ_FDIV_N_ITERS * m->iter_steps;
    seg(cq_fdiv_post_base_row(), m->n_post, m->post_off, m->post_slot,
        m->post, m->post_s, &bits, &slots);
}

const cq_fd_map *cq_fd_map_get(void)
{
    static cq_fd_map m;
    static int ready;

    if (!ready) {
        cq_fd_map_build(&m, 0u);
        ready = 1;
    }
    return &m;
}

void cq_fd_arm(const cq_fdiv_block *k, cq_fd_map *m)
{
    const cq_fd_map *base;

    if (k == NULL || k->scr == NULL)
        cq_kernel_die("fdiv: the block has no region");
    base = cq_fd_map_get();
    *m = *base;
    for (int i = 0; i <= m->n_pre; i++)  m->pre[i]  += k->off;
    for (int i = 0; i <= m->n_post; i++) m->post[i] += k->off;
    m->loop_off += k->off;
    m->post_off += k->off;
    if ((uint64_t)m->post[m->n_post] > (uint64_t)cq_scratch_size(k->scr))
        cq_kernel_die("fdiv: the block's region does not fit at its offset");
}

uint32_t cq_fd_off(const cq_fd_map *m, int i)
{
    int base = cq_fdiv_loop_base_row();

    if (i < 0 || i >= cq_fdiv_n_rows())
        cq_kernel_die("fdiv: an operand names a row outside the program");
    if (i < m->n_pre)                 return m->pre[i];
    if (i >= cq_fdiv_post_base_row()) return m->post[i - cq_fdiv_post_base_row()];
    {
        int u = i - base;
        int t = u / CQ_FDIV_ITER_ROWS, j = u % CQ_FDIV_ITER_ROWS;

        return m->loop_off + (uint32_t)t * m->iter_bits + m->iter[j];
    }
}

/* A PREFIX SUM, THEN A MODULUS, THEN A PREFIX SUM (K17.md §2.7). Each segment
 * uses a last-boundary binary search; the loop body has only seven rows. */
static int last_boundary(const int *slot, int n, int u)
{
    int lo = 0, hi = n;

    while (hi - lo > 1) {
        int mid = lo + (hi - lo) / 2;

        if (slot[mid] <= u) lo = mid; else hi = mid;
    }
    return lo;
}

int cq_fd_row_of_slot(const cq_fd_map *m, int u, int *local)
{
    /* THE MESSAGE IS DISJOINT FROM `cq_fdiv_step`'s ON PURPOSE, and it is the
     * recorded "a guard is untested if a later copy of itself catches it"
     * shape (bd a-guard-is-untested-if-a-later-copy-of-itself-catches-it,
     * measured on M15's width guard). The public entry point checks the range
     * too; if both said the same thing, deleting either would leave every
     * death case green. */
    if (u < 0 || u >= m->post_s[m->n_post])
        cq_kernel_die("fdiv: a slot index outside the segmented map");

    if (u < m->loop_slot) {
        int i = last_boundary(m->pre_s, m->n_pre, u);

        *local = u - m->pre_s[i];
        return i;
    }
    if (u >= m->post_slot) {
        int i = last_boundary(m->post_s, m->n_post, u);

        *local = u - m->post_s[i];
        return cq_fdiv_post_base_row() + i;
    }
    {
        int v = u - m->loop_slot;
        int t = v / m->iter_steps, j = v % m->iter_steps;
        int i = last_boundary(m->iter_s, CQ_FDIV_ITER_ROWS, j);

        *local = j - m->iter_s[i];
        return cq_fdiv_loop_base_row() + t * CQ_FDIV_ITER_ROWS + i;
    }
}

cq_bit *cq_fd_sp(const cq_fdiv_block *k, uint32_t at, uint32_t len)
{
    return cq_scratch_span(k->scr, at, len);
}

/* --- The public totals. -------------------------------------------------- */

uint32_t cq_fdiv_region(void)
{
    const cq_fd_map *m = cq_fd_map_get();

    return m->post[m->n_post];
}

int cq_fdiv_steps(void)
{
    const cq_fd_map *m = cq_fd_map_get();

    return m->post_s[m->n_post];
}

/* `loop_K` — the slots one iteration of fdiv.jl:91-98 costs. A wrong one is a
 * wrong step decode at 56 places at once, which is why it is exported and
 * asserted against `C_ult + C_sub + 2*C_mux + C_or` on its own. */
int cq_fdiv_iter_steps(void)
{
    return cq_fd_map_get()->iter_steps;
}

uint32_t cq_fdiv_iter_region(void)
{
    return cq_fd_map_get()->iter_bits;
}
