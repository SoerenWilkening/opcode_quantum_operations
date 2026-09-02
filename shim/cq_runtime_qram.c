/* shim/cq_runtime_qram.c — v1.2's QRAM surface (PRD §15 D24, `bd 9zq`, plan
 * §0.6, Step 27; 2026-09-02). The 63 `cqrt_qram_*` symbols: nine widths × {alloc,
 * load, load_unc, store, store_unc, store_controlled, store_controlled_unc}.
 * Until this file existed they were loud aborts in cq_runtime_v2.c.
 *
 * AN fp WIDTH IS A BIT-PATTERN CELL, on the ABI's own words (cq_runtime.h,
 * READ-ONLY, `bd 38t2`: "fp-as-bitpattern cell"): `cqrt_qram_alloc_f64` is an
 * array of 64-bit registers and nothing in it is an angle. What keeps those
 * symbols unreachable on the corpus is v2's fp CORE — a `cqrt_alloc_f64` rail
 * cannot exist — so the fixtures that hit them first move from `qram is v2` to
 * `fp is v2`, and this file has no fp arm at all (D16: buckets by FAMILY first).
 *
 * WHAT EACH ENTRY POINT IS, from cq_runtime.h:500-660 and PRD §15 D24:
 *   alloc  — D23's token plus `count` cells (shim/cq_shim_qram.c); NO bracket
 *            (the D21 alloc exemption, on the same STATIC ground as
 *            `cqrt_alloc_i<W>`: the token and the cells are minted through the
 *            register TABLE and cannot reach a `qec_*` call) and no record
 *            beyond the token's mint.
 *   load   — mints `out` at birth 0, runs K13 (`out ^= mem[idx]`), records
 *            QRAM_LOAD; `_unc` is the SAME kernel with dst = out (Rule 7) and
 *            records the twin. CQ_lang then frees `out`, and D15's certificate
 *            releases it CLEAN iff the array was not written in between.
 *   store  — mints the tape SLOT at birth 0, pushes it, records QRAM_STORE,
 *            runs K14's push; `_unc` verifies the top of the tape against its
 *            own operands, runs K14's pop, records the twin, and FREES the slot
 *            through cq_shim_free_proof — D15's disposition, in one place:
 *            CLEAN iff the pair reduces, STRANDED otherwise, never a literal.
 *   the two `_controlled` families are those inside `cq_shim_region` (Rule 9).
 *
 * THE ORDER INSIDE THE POP IS LOAD-BEARING TWICE. The record is pushed BEFORE
 * the free — the certificate needs the twin to exist — and the history is
 * retired AFTER it, since cq_reg_free consults the proof twice per qubit
 * (cq_runtime_rail.c's `cqrt_free`, same reason).
 *
 * REFUSALS, ALL HARD ERRORS IN BOTH CONFIGURATIONS. A RAIL or a TAPE token in
 * the array slot (this file's, by state and by the payload table); an array of
 * another element width than the symbol names; an `out` / `val` rail that is
 * not the width the symbol names; an index rail that is not i32 (the ABI: "the
 * index handle is ALWAYS i32"); a pop on an empty tape or one that does not
 * match the last unpopped store (PRD-7.5 §2.8(ii)). A token anywhere a rail is
 * expected is M07's, through its two funnels, and nothing here re-implements
 * it. The messages are DISJOINT from cq_runtime_rail.c's and cq_runtime_tape.c's
 * on purpose: the CMake pins discriminate on the message.
 *
 * D7b: NO DEFENSIVE COPY. Measured over all 31 qram fixtures at CQ_lang
 * `893b769` (dirty), 0 of the 16 store-family calls alias any two of
 * (pred, idx, val), and a load's `out` is minted fresh. An alias reaching the
 * kernel is `cq_qram_check`'s hard error.
 *
 * D21: a load's bracket names `idx` in and `out` out; a store's names `idx`,
 * `val` (and `pred`) in and NOTHING out — the array is a token (`in=` would
 * spell it h<N>) and the cells and the slot are handles CQ_lang never
 * received, rule 3, so their gates appear under this bracket unregistered, as
 * `cqrt_addc`'s transients do.
 *
 * RULE 12. Budget 220, recorded in IMPLEMENTATION_PLAN §3 before the file
 * existed. Seam: `the LOAD family ↔ the STORE families` →
 * shim/cq_runtime_qram_store.c; discriminator: a line that touches the tape
 * stack is the store's. Trigger 240.
 */

#include "cq_runtime_abi.h"

#include "cq_shim_ctx.h"
#include "cq_shim_proof.h"
#include "cq_shim_qram.h"
#include "cq_shim_record.h"
#include "cq_shim_trace.h"

#include "bit.h"
#include "ctx.h"
#include "kernels/qrom.h"
#include "kernels/qstore.h"
#include "reg.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cq_qram_die(const char *what, int32_t h, long k)
{
    fprintf(stderr, "libcqops: FATAL: shim: %s (h%d, %ld)\n", what, h, k);
    abort();
}

/* --- operand resolution --------------------------------------------------- */

/* THE ARRAY SLOT IS CHECKED BY STATE AND THEN BY PAYLOAD: an out-of-range
 * handle is M07's diagnostic, a rail is this file's, and a TAPE token — the
 * same state, the other owner — is this file's too. */
static cq_qram_array *qram_arr(cq_ctx *ctx, int32_t arr, uint32_t w)
{
    cq_qram_array *a;

    if (cq_reg_state(&ctx->regs, arr) != CQ_SLOT_TOKEN)
        cq_qram_die("the array operand is not a qram array token: the ABI's "
                    "array operand is the a<N> cqrt_qram_alloc returned, and "
                    "this handle is a rail (live, freed or measured)", arr, 0);
    a = cq_qram_find_mut(arr);
    if (!a)
        cq_qram_die("the array operand is a TAPE token, not a qram array",
                    arr, 0);
    if (a->width != w)
        cq_qram_die("the array's element width is not the width its "
                    "cqrt_qram symbol names", arr, (long)a->width);
    return a;
}

/* Resolved through cq_reg_cbits FIRST so that a token or a tombstone is
 * M07's diagnostic; then the width. */
static const cq_bit *qram_src(cq_ctx *ctx, int32_t h, uint32_t w,
                              const char *what)
{
    const cq_bit *b = cq_reg_cbits(&ctx->regs, h);
    uint32_t got = cq_reg_width(&ctx->regs, h);
    if (got != w) cq_qram_die(what, h, (long)got);
    return b;
}

static const cq_bit *qram_idx(cq_ctx *ctx, int32_t idx)
{
    return qram_src(ctx, idx, 32u, "the index rail is not i32: the ABI's index "
                    "handle is ALWAYS i32");
}

/* The cells as `count` mutable pointers — K13 reads them, K14 writes them. */
static cq_bit **qram_cells(cq_ctx *ctx, const cq_qram_array *a)
{
    cq_bit **cells = malloc((size_t)a->count * sizeof *cells);
    if (!cells) cq_qram_die("out of memory", a->token, 0);
    for (int32_t j = 0; j < a->count; j++)
        cells[j] = cq_reg_bits(&ctx->regs, cq_qram_cell(a, j));
    return cells;
}

static void rec(cq_rop op, int32_t h0, int32_t h1, int32_t h2, int32_t h3,
                int32_t ctrl)
{
    cq_call_rec c;
    memset(&c, 0, sizeof c);
    c.op   = (uint16_t)op;
    c.h[0] = h0; c.h[1] = h1; c.h[2] = h2; c.h[3] = h3;
    c.ctrl = ctrl;
    cq_rec_push(&c);
}

/* --- load ----------------------------------------------------------------- */

static void load_body(cq_ctx *ctx, const cq_qram_array *a, int32_t out,
                      int32_t idx)
{
    cq_bit **cells = qram_cells(ctx, a);
    cq_kernel_qload(ctx, cq_reg_bits(&ctx->regs, out), qram_idx(ctx, idx), 32,
                    (const cq_bit *const *)cells, a->count, (int)a->width);
    free(cells);
}

static int32_t qram_load(uint32_t w, int32_t arr, int32_t idx)
{
    cq_ctx *ctx = cq_shim_ctx();
    const cq_qram_array *a = qram_arr(ctx, arr, w);
    int32_t out;

    (void)qram_idx(ctx, idx);
    out = cq_reg_alloc_zero(&ctx->regs, w);
    cq_rec_mint(out, w, 0u, 0u, 0);
    cq_trace_op("qram_load", idx, CQ_REG_NONE, CQ_REG_NONE, out, CQ_REG_NONE);
    load_body(ctx, a, out, idx);
    cq_trace_end();
    rec(CQ_ROP_QRAM_LOAD, out, arr, idx, CQ_REG_NONE, CQ_REG_NONE);
    return out;
}

static void qram_load_unc(uint32_t w, int32_t out, int32_t arr, int32_t idx)
{
    cq_ctx *ctx = cq_shim_ctx();
    const cq_qram_array *a = qram_arr(ctx, arr, w);

    (void)cq_reg_bits(&ctx->regs, out);   /* M07 refuses a token or a tombstone */
    (void)qram_src(ctx, out, w, "the out rail is not the width its "
                   "cqrt_qram_load_unc symbol names");
    cq_trace_op("qram_load_unc", idx, CQ_REG_NONE, CQ_REG_NONE, out, CQ_REG_NONE);
    load_body(ctx, a, out, idx);
    cq_trace_end();
    rec(CQ_ROP_QRAM_LOAD_UNC, out, arr, idx, CQ_REG_NONE, CQ_REG_NONE);
}

/* --- store ---------------------------------------------------------------- */

typedef struct {
    cq_ctx *ctx;
    const cq_qram_array *a;
    int32_t idx, val, slot;
    int     pop;
} store_args;

static void store_body(void *p)
{
    const store_args *s = (const store_args *)p;
    cq_ctx *ctx = s->ctx;
    cq_bit **cells = qram_cells(ctx, s->a);
    const cq_bit *idx = qram_idx(ctx, s->idx);
    const cq_bit *val = cq_reg_cbits(&ctx->regs, s->val);
    cq_bit *tape = cq_reg_bits(&ctx->regs, s->slot);
    const int count = s->a->count, W = (int)s->a->width;

    if (s->pop) cq_kernel_qstore_pop (ctx, cells, count, idx, 32, val, tape, W);
    else        cq_kernel_qstore_push(ctx, cells, count, idx, 32, val, tape, W);
    free(cells);
}

static void qram_store(uint32_t w, int32_t pred, int32_t arr, int32_t idx,
                       int32_t val, int pop)
{
    cq_ctx *ctx = cq_shim_ctx();
    cq_qram_array *a = qram_arr(ctx, arr, w);
    const int ctrl = pred != CQ_REG_NONE;
    store_args s;

    (void)qram_idx(ctx, idx);
    (void)qram_src(ctx, val, w, "the value rail is not the width its "
                   "cqrt_qram_store symbol names");

    s.ctx = ctx; s.a = a; s.idx = idx; s.val = val; s.pop = pop;
    if (!pop) {
        cq_qram_entry e;
        s.slot = cq_reg_alloc_zero(&ctx->regs, w);
        cq_rec_mint(s.slot, w, 0u, 0u, 0);
        e.slot = s.slot; e.idx = idx; e.val = val; e.pred = pred;
        cq_qram_push(a, e);
    } else {
        /* PRD-7.5 §2.8(ii): strict LIFO per array, verified rather than
         * trusted. Handles are D5-monotonic, so equality is identity. */
        const cq_qram_entry *top = cq_qram_top(a);
        if (!top)
            cq_qram_die("cqrt_qram_store_unc on an array with no unpopped "
                        "store", arr, (long)idx);
        if (top->idx != idx || top->val != val || top->pred != pred)
            cq_qram_die("cqrt_qram_store_unc does not match the last unpopped "
                        "store on this array (idx, val and pred must be the "
                        "forward's — the tape is strictly LIFO per array)",
                        arr, (long)top->idx);
        s.slot = top->slot;
    }

    cq_trace_op(ctrl ? (pop ? "qram_store_ctrl_unc" : "qram_store_ctrl")
                     : (pop ? "qram_store_unc"      : "qram_store"),
                idx, val, pred, CQ_REG_NONE, CQ_REG_NONE);
    if (ctrl) cq_shim_region(pred, store_body, &s);
    else      store_body(&s);

    rec(ctrl ? (pop ? CQ_ROP_QRAM_STORE_CTRL_UNC : CQ_ROP_QRAM_STORE_CTRL)
             : (pop ? CQ_ROP_QRAM_STORE_UNC      : CQ_ROP_QRAM_STORE),
        arr, idx, val, s.slot, pred);

    if (pop) {
        /* The slot's release is D15's disposition and nothing else: the
         * certificate pairs the push and this pop iff idx, val, pred and the
         * array held still in between. Free, THEN retire (rail.c's order). */
        cq_reg_free(ctx, s.slot, cq_shim_free_proof);
        cq_rec_retire(s.slot);
        cq_qram_pop(a);
    }
    cq_trace_end();
}

/* --- the 63 symbols ------------------------------------------------------- */

#define CQ_QRAM_FAMILY(TOK, W)                                                \
    int32_t cqrt_qram_alloc_##TOK(int32_t count)                              \
    { return cq_qram_alloc(cq_shim_ctx(), W##u, count); }                     \
    int32_t cqrt_qram_load_##TOK(int32_t arr, int32_t idx)                    \
    { return qram_load(W##u, arr, idx); }                                     \
    void cqrt_qram_load_##TOK##_unc(int32_t out, int32_t arr, int32_t idx)    \
    { qram_load_unc(W##u, out, arr, idx); }                                   \
    void cqrt_qram_store_##TOK(int32_t arr, int32_t idx, int32_t val)         \
    { qram_store(W##u, CQ_REG_NONE, arr, idx, val, 0); }                      \
    void cqrt_qram_store_##TOK##_unc(int32_t arr, int32_t idx, int32_t val)   \
    { qram_store(W##u, CQ_REG_NONE, arr, idx, val, 1); }                      \
    void cqrt_qram_store_##TOK##_controlled(int32_t pred, int32_t arr,        \
                                            int32_t idx, int32_t val)         \
    { qram_store(W##u, pred, arr, idx, val, 0); }                             \
    void cqrt_qram_store_##TOK##_controlled_unc(int32_t pred, int32_t arr,    \
                                                int32_t idx, int32_t val)     \
    { qram_store(W##u, pred, arr, idx, val, 1); }

CQ_QRAM_FAMILY(i1,  1)
CQ_QRAM_FAMILY(i8,  8)
CQ_QRAM_FAMILY(i16, 16)
CQ_QRAM_FAMILY(i32, 32)
CQ_QRAM_FAMILY(i64, 64)
CQ_QRAM_FAMILY(f16, 16)
CQ_QRAM_FAMILY(f32, 32)
CQ_QRAM_FAMILY(f64, 64)
CQ_QRAM_FAMILY(f80, 80)
