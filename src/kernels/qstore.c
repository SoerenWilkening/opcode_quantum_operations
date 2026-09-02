/* src/kernels/qstore.c — M30, Step 27. K14 shadow store at a quantum index.
 * See qstore.h and docs/constructions/K14.md.
 *
 * RULE 12. Budget 140, recorded in IMPLEMENTATION_PLAN §3 before this file was
 * written. Seam: `the SWEEP STEP ↔ the ENTRY` → kernels/qstore_step.c; the
 * step decode answers to shadow_memory.jl, the entry to D24's contract (the
 * operand check, the sandwich call, the direction). Trigger 240.
 */

#include "kernels/qstore.h"

#include "emit.h"
#include "kernels/kernel.h"
#include "kernels/qrom.h"
#include "sandwich.h"
#include "scratch.h"

int cq_qstore_sweep_steps(int count, int W) { return 3 * count * W; }

typedef struct {
    cq_bit *const  *cells;
    const cq_bit   *val;
    cq_bit         *tape;
    int             count, W, n_copy, reverse;
    cq_qtree_block  t;
} qstore_env;

static void compute(cq_ctx *ctx, void *env, int s)
{
    cq_qtree_step(ctx, &((const qstore_env *)env)->t, s);
}

/* One sweep gate: shadow_memory.jl:109-120 with pred_wire = leaf_j. Sweep-
 * major, then cell, then lane. Sweep 0 must precede sweep 1 (it fills the slot
 * sweep 1 reads) and sweep 1 must precede sweep 2 (it clears the cell sweep 2
 * writes); within a sweep every gate commutes with every other. */
static void sweep_gate(cq_ctx *ctx, const qstore_env *e, int s)
{
    const int cw = e->count * e->W;
    const int sweep = s / cw, j = (s % cw) / e->W, i = s % e->W;
    const cq_bit *leaf = cq_qtree_leaf(&e->t, j);
    cq_bit *cell = &e->cells[j][i];

    switch (sweep) {
    case 0:  cq_emit_ccx(ctx, leaf, cell,         &e->tape[i]); break;
    case 1:  cq_emit_ccx(ctx, leaf, &e->tape[i],  cell);        break;
    default: cq_emit_ccx(ctx, leaf, &e->val[i],   cell);        break;
    }
}

/* THE WHOLE DIFFERENCE BETWEEN PUSH AND POP. The pop runs the same gates at
 * descending indices; each is its own inverse, so the composite is the exact
 * reverse circuit (K14.md §2.2). */
static void copyout(cq_ctx *ctx, void *env, int s)
{
    const qstore_env *e = (const qstore_env *)env;
    sweep_gate(ctx, e, e->reverse ? e->n_copy - 1 - s : s);
}

/* `emit_shadow_store!` unguarded on ONE cell (shadow_memory.jl:38-55): the
 * known-index path, and Bennett's L == 1 shape. */
static void one_cell(cq_ctx *ctx, cq_bit *cell, const cq_bit *val,
                     cq_bit *tape, int W, int reverse)
{
    const int n = 3 * W;

    for (int s = 0; s < n; s++) {
        const int g = reverse ? n - 1 - s : s;
        const int i = g % W;
        switch (g / W) {
        case 0:  cq_emit_cx(ctx, &cell[i], &tape[i]); break;
        case 1:  cq_emit_cx(ctx, &tape[i], &cell[i]); break;
        default: cq_emit_cx(ctx, &val[i],  &cell[i]); break;
        }
    }
}

static void store(cq_ctx *ctx, cq_bit *const *cells, int count,
                  const cq_bit *idx, int n_idx, const cq_bit *val,
                  cq_bit *tape, int W, int reverse)
{
    const int n = cq_qtree_depth(count);
    cq_scratch scr;
    qstore_env e;

    if (!val)  cq_kernel_die("qstore: no value rail");
    if (!tape) cq_kernel_die("qstore: no tape slot");
    cq_qram_check(NULL, 0, idx, n_idx, val, tape,
                  (const cq_bit *const *)cells, count, W);

    /* THE SLOT IS BORN |0> ON THE PUSH, and that is asserted on KIND (M08's
     * posture, never a shadow read): a slot holding a qubit before the push
     * is a slot some earlier store did not pop, and sweep 0 would XOR the old
     * cell into whatever it holds. The pop makes no such claim — its slot
     * legitimately holds the displaced value. */
    if (!reverse)
        for (int i = 0; i < W; i++)
            if (tape[i].kind != CQ_BIT_ZERO)
                cq_kernel_die("qstore: the tape slot is not all CQ_BIT_ZERO on "
                              "a push — a slot is fresh per store");

    /* A KNOWN INDEX TAKES THE OTHER PATH (K13's dispatch, shared): Bennett's
     * unguarded `emit_shadow_store!` on the one selected cell, no tree, no
     * scratch; at n == 0 the index is trivially known and this IS his L == 1
     * shape. A padded index selects nothing and the slot stays |0>. */
    {
        int j;
        if (cq_qtree_index_is_const(idx, n, &j)) {
            if (j < count) one_cell(ctx, cells[j], val, tape, W, reverse);
            return;
        }
    }

    cq_scratch_alloc(&scr, (uint32_t)cq_qtree_nodes(n));
    e.cells   = cells;
    e.val     = val;
    e.tape    = tape;
    e.count   = count;
    e.W       = W;
    e.n_copy  = cq_qstore_sweep_steps(count, W);
    e.reverse = reverse;
    e.t.flags = cq_scratch_span(&scr, 0u, (uint32_t)cq_qtree_nodes(n));
    e.t.idx   = idx;
    e.t.n     = n;

    cq_sandwich(ctx, &scr, compute, cq_qtree_steps(n), copyout, e.n_copy, &e);
    cq_scratch_dispose(&scr);
}

void cq_kernel_qstore_push(cq_ctx *ctx, cq_bit *const *cells, int count,
                           const cq_bit *idx, int n_idx,
                           const cq_bit *val, cq_bit *tape, int W)
{
    store(ctx, cells, count, idx, n_idx, val, tape, W, 0);
}

void cq_kernel_qstore_pop(cq_ctx *ctx, cq_bit *const *cells, int count,
                          const cq_bit *idx, int n_idx,
                          const cq_bit *val, cq_bit *tape, int W)
{
    store(ctx, cells, count, idx, n_idx, val, tape, W, 1);
}
