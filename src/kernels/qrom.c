/* src/kernels/qrom.c — M29, Step 27. K13 QROM read; the tree block K14 shares.
 * See qrom.h and docs/constructions/K13.md.
 *
 * RULE 12. Budget 160, recorded in IMPLEMENTATION_PLAN §3 before this file was
 * written. Seam: `the TREE ↔ the LOAD` → kernels/qtree.[ch]; discriminator: a
 * line that names `dst` or a cell is the load's, a line that names only flags
 * and `idx` is the tree's. Trigger 240, the house figure.
 */

#include "kernels/qrom.h"

#include "emit.h"
#include "kernels/kernel.h"
#include "sandwich.h"
#include "scratch.h"

#include <stdlib.h>

/* --- the tree ------------------------------------------------------------ */

int cq_qtree_depth(int count)
{
    int n = 0;

    if (count <= 0)
        cq_kernel_die("qram: count is not positive");
    if (count > CQ_QRAM_COUNT_MAX)
        cq_kernel_die("qram: count exceeds CQ_QRAM_COUNT_MAX — the sandwich's "
                      "step index is an int and the tree has 3·(2^n − 1) + 1 "
                      "compute steps");
    while ((1 << n) < count) n++;
    return n;
}

int cq_qtree_nodes(int n) { return (2 << n) - 1; }
int cq_qtree_steps(int n) { return 1 + 3 * ((1 << n) - 1); }

/* Node k sits at depth d iff 2^d − 1 <= k < 2^(d+1) − 1. */
static int depth_of(int k)
{
    int d = 0;
    while ((2 << d) - 1 <= k) d++;
    return d;
}

void cq_qtree_step(cq_ctx *ctx, const cq_qtree_block *b, int u)
{
    int k, lane;
    cq_bit *left, *right;

    if (u == 0) { cq_emit_x(ctx, &b->flags[0]); return; }   /* root := 1 */

    k     = (u - 1) / 3;
    lane  = b->n - 1 - depth_of(k);
    left  = &b->flags[2 * k + 1];
    right = &b->flags[2 * k + 2];

    /* qrom.jl:110-118, in order: right := parent ∧ idx_lane; left := parent;
     * left ^= right. Phase 2 must follow phase 0 (it reads `right`) and phase 1
     * (it writes `left` after it); nothing else here is order-sensitive. */
    switch ((u - 1) % 3) {
    case 0:  cq_emit_ccx(ctx, &b->flags[k], &b->idx[lane], right); break;
    case 1:  cq_emit_cx (ctx, &b->flags[k],                left);  break;
    default: cq_emit_cx (ctx, right,                       left);  break;
    }
}

const cq_bit *cq_qtree_leaf(const cq_qtree_block *b, int j)
{
    return &b->flags[(1 << b->n) - 1 + j];
}

int cq_qtree_index_is_const(const cq_bit *idx, int n, int *j)
{
    int v = 0;

    for (int lane = 0; lane < n; lane++) {
        if (!cq_bit_is_const(idx[lane])) return 0;
        if (cq_bit_value(idx[lane])) v |= 1 << lane;
    }
    *j = v;
    return 1;
}

/* --- the operand check --------------------------------------------------- */

static int cmp_addr(const void *x, const void *y)
{
    uintptr_t a = (uintptr_t)*(const cq_bit *const *)x;
    uintptr_t b = (uintptr_t)*(const cq_bit *const *)y;
    return a < b ? -1 : a > b;
}

static void check_against(const cq_bit *p, int wp, const char *what,
                          const cq_bit *idx, int n_idx, const cq_bit *val,
                          const cq_bit *tape, const cq_bit *const *cells,
                          int count, int W)
{
    if (!p) return;
    if (idx  && idx  != p && cq_kernel_overlap2(p, wp, idx,  n_idx)) cq_kernel_die(what);
    if (val  && val  != p && cq_kernel_overlap2(p, wp, val,  W))     cq_kernel_die(what);
    if (tape && tape != p && cq_kernel_overlap2(p, wp, tape, W))     cq_kernel_die(what);
    for (int j = 0; j < count; j++)
        if (cq_kernel_overlap2(p, wp, cells[j], W)) cq_kernel_die(what);
}

void cq_qram_check(const cq_bit *dst, int w_dst,
                   const cq_bit *idx, int n_idx,
                   const cq_bit *val, const cq_bit *tape,
                   const cq_bit *const *cells, int count, int W)
{
    const int n = cq_qtree_depth(count);
    const cq_bit **sorted;

    if (W <= 0)            cq_kernel_die("qram: cell width is not positive");
    if (!cells)            cq_kernel_die("qram: no cells");
    if (!idx || n_idx <= 0) cq_kernel_die("qram: no index rail");
    if (n_idx < n)
        cq_kernel_die("qram: the index rail has fewer lanes than the tree "
                      "needs (qrom.jl:172 — L requires ceil(log2 L) index bits)");
    for (int j = 0; j < count; j++)
        if (!cells[j]) cq_kernel_die("qram: a cell is NULL");

    /* dst against everything (D7a); idx, val and tape against each other and
     * against every cell (D7b, kernel.h's grounds). */
    check_against(dst, w_dst, "D7a — dst overlaps a source of a qram kernel",
                  idx, n_idx, val, tape, cells, count, W);
    check_against(idx, n_idx, "D7b — the index rail overlaps another qram "
                  "operand; an alias reaching a kernel is a missing copy",
                  NULL, 0, val, tape, cells, count, W);
    check_against(val, W, "D7b — the value rail overlaps another qram operand",
                  NULL, 0, NULL, tape, cells, count, W);
    check_against(tape, W, "D7b — the tape slot overlaps a cell",
                  NULL, 0, NULL, NULL, cells, count, W);

    /* The cells against each other: sorted by base address, adjacent pairs.
     * If any two overlap then, after sorting by start, some ADJACENT pair
     * overlaps — so this finds an alias among `count` cells without the
     * quadratic scan the shim's largest arrays could not afford. */
    sorted = malloc((size_t)count * sizeof *sorted);
    if (!sorted) cq_kernel_die("qram: out of memory in the operand check");
    for (int j = 0; j < count; j++) sorted[j] = cells[j];
    qsort(sorted, (size_t)count, sizeof *sorted, cmp_addr);
    for (int j = 1; j < count; j++)
        if (cq_kernel_overlap2(sorted[j - 1], W, sorted[j], W)) {
            free(sorted);
            cq_kernel_die("D7b — two cells of one array overlap");
        }
    free(sorted);
}

/* --- K13 ------------------------------------------------------------------ */

typedef struct {
    cq_bit               *dst;
    const cq_bit *const  *cells;
    int                   W;
    cq_qtree_block        t;
} qload_env;

static void compute(cq_ctx *ctx, void *env, int s)
{
    cq_qtree_step(ctx, &((const qload_env *)env)->t, s);
}

/* The "^=" of the contract: dst[i] ^= leaf_j ∧ cell_j[i], one Toffoli per
 * (cell, lane). A padded leaf owns no cell and is never reached here. */
static void copyout(cq_ctx *ctx, void *env, int s)
{
    const qload_env *e = (const qload_env *)env;
    const int j = s / e->W, i = s % e->W;

    cq_emit_ccx(ctx, cq_qtree_leaf(&e->t, j), &e->cells[j][i], &e->dst[i]);
}

void cq_kernel_qload(cq_ctx *ctx, cq_bit *dst,
                     const cq_bit *idx, int n_idx,
                     const cq_bit *const *cells, int count, int W)
{
    const int n = cq_qtree_depth(count);
    cq_scratch scr;
    qload_env  e;

    cq_qram_check(dst, W, idx, n_idx, NULL, NULL, cells, count, W);

    /* A KNOWN INDEX TAKES THE OTHER PATH — Bennett's Case 1 (qrom.jl:151-160)
     * and, at n == 0, his L == 1 branch (:69-79), which is the same thing with
     * no lanes to read: a plain copy of the selected cell, no flag, no
     * sandwich (a region of zero bits is M08's hard error, and a construction
     * needing no scratch needs no driver). A padded index selects nothing. The
     * copy is still PHYSICAL — a quantum cell lane materialises dst[i] and
     * emits a real CX (I2); a constant lane folds, which is where L5's zero
     * comes from. */
    {
        int j;
        if (cq_qtree_index_is_const(idx, n, &j)) {
            if (j < count)
                for (int i = 0; i < W; i++)
                    cq_emit_cx(ctx, &cells[j][i], &dst[i]);
            return;
        }
    }

    cq_scratch_alloc(&scr, (uint32_t)cq_qtree_nodes(n));
    e.dst     = dst;
    e.cells   = cells;
    e.W       = W;
    e.t.flags = cq_scratch_span(&scr, 0u, (uint32_t)cq_qtree_nodes(n));
    e.t.idx   = idx;
    e.t.n     = n;

    cq_sandwich(ctx, &scr, compute, cq_qtree_steps(n), copyout, count * W, &e);
    cq_scratch_dispose(&scr);
}
