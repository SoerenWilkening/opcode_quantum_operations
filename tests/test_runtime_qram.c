/* tests/test_runtime_qram.c — Step 27 (v1.2, PRD §15 D24, bd 9zq): the QRAM
 * surface — shim/cq_runtime_qram.c over shim/cq_shim_qram.[ch], the 63
 * cqrt_qram_* symbols at nine widths.
 *
 * WHAT THE CASES ARE FOR, level by level, at the ABI:
 *   the TOKEN   — `a`, then cells a+1 … a+count, live at width W, zero qubits
 *                 and zero gates at alloc, the counter shared with the rails.
 *   L1          — the sweep .inc: load reads the cell at the truncated index,
 *                 store writes it and the slot holds the old value, over random
 *                 (idx, contents, val, masks) at cq_kd_samples() per width.
 *   L2          — the SET, including every cell and (while pushed) the slot.
 *   L3          — load → load_unc → out is 0 → cqrt_free(out) releases CLEAN;
 *                 store → store_unc restores every cell AND the slot is released
 *                 CLEAN — by the CERTIFICATE when the index is rotation-poisoned
 *                 and the shadow cannot see (D24 (c)'s whole reason to exist).
 *   the RECORD  — a store between a load and its _unc STRANDS `out`: the one
 *                 direction that would be UNSOUND with a history-less token.
 *   LIFO        — two nested stores pop in order and both slots clear.
 *   §9          — the controlled store under ZERO / ONE / Q0 / Q1.
 *   the CORPUS  — the six integer fixtures' call sequences replayed with real
 *                 `cqrt_ry` indices: they run to completion with zero residue.
 *
 * The D21 annotation is not here: `cq_trace_op` is inert under every sink but
 * qec, so "qram_alloc opens no bracket and a load names idx in and out out"
 * lives in tests/test_shim_trace.c, behind -DCQOPS_QEC_DIR=.
 *
 * RULE 12. Seam, recorded before the file was written and TAKEN on the first
 * measurement: `the SWEEP ↔ the ROWS` — the width-dispatched ABI table, the
 * seeded draw and the sweep cases in tests/test_runtime_qram_sweep.inc; the
 * token, the certificate rows, LIFO, §9 and the corpus shapes here.
 */

#include "cq_runtime_abi.h"
#include "cq_shim.h"
#include "cq_shim_ctx.h"
#include "cq_shim_proof.h"
#include "cq_shim_qram.h"
#include "cq_shim_record.h"

#include "bit.h"
#include "controlled.h"
#include "ctx.h"
#include "qubits.h"
#include "reg.h"
#include "sink.h"

#include "cqops/cqops.h"

#include "support/bitkinds.h"
#include "support/harness.h"
#include "support/kerneldrv.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"
#include "support/refmodel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fresh(void)
{
    cq_sink_reset();
    unsetenv("CQOPS_SINK");
    cq_shim_ctx_reset();
}

static cq_ctx *open_with(cq_mock *m, cq_sink *s)
{
    cq_mock_init(m);
    *s = cq_mock_sink(m);
    fresh();
    cqops_set_sink(s);
    return cq_shim_ctx();
}

static void close_with(cq_mock *m)
{
    cq_shim_ctx_reset();
    cqops_set_sink(NULL);
    cq_mock_dispose(m);
}

#include "test_runtime_qram_sweep.inc"

/* -------------------------------------------------------------------------
 * 1. The token, and the cells behind it.
 * ------------------------------------------------------------------------- */

CQ_TEST(alloc_mints_a_token_and_count_cells_out_of_the_shared_counter)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const cq_pc_snap before = cq_pc_take(ctx);
    int32_t hs[8];

    const int32_t a  = cqrt_qram_alloc_i32(8);
    const int32_t h9 = cqrt_alloc_i32(4);

    CHECK_EQ(cq_mock_count(&m), 0);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    CHECK_EQ(cq_reg_is_live(&ctx->regs, a), 0);
    CHECK_EQ(cq_reg_width(&ctx->regs, a), 0);
    CHECK_EQ(cq_reg_state(&ctx->regs, a), CQ_SLOT_TOKEN);
    CHECK(cq_qram_find(a) != NULL);
    CHECK_EQ(cq_qram_find(a)->count, 8);
    CHECK_EQ(cq_qram_find(a)->width, 32u);
    for (int j = 0; j < 8; j++) {
        hs[j] = cq_qram_cell(cq_qram_find(a), j);
        CHECK_EQ(hs[j], a + 1 + j);
        CHECK_EQ(cq_reg_is_live(&ctx->regs, hs[j]), 1);
        CHECK_EQ(cq_reg_width(&ctx->regs, hs[j]), 32u);
        CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, hs[j]), 0);    /* I4 */
    }
    CHECK_EQ(h9, a + 9);                          /* one counter, D5 */
    CHECK(cq_rec_hist(a) != NULL);                /* D24 (c): a HISTORY */
    CHECK(cq_rec_hist(hs[0]) == NULL);            /* and the cells have none */
    cq_reg_audit(ctx);
    close_with(&m);
}

/* THE SIX ROWS, PINNED AS DECLARED INTENT (D24 (c)). Every mask below is a
 * claim about `cq_runtime.h`, and one field is unreachable by any stream the
 * shim can produce: the pop row's `twin_of`. The engine scans EARLIER against
 * LATER and reads the earlier record's row, so the pop's own `twin_of` would be
 * consulted only if a pop preceded its push — which the LIFO check makes
 * impossible. Measured SURVIVED by the Step 27 battery (E3) and pinned here
 * rather than left as a field nothing reads, exactly as the tape row's
 * `self_adjoint` was. */
CQ_TEST(the_six_record_rows_are_what_d24_declares)
{
    static const struct {
        cq_rop op; uint8_t reads, writes, twin_of;
    } ROWS[] = {
        { CQ_ROP_QRAM_LOAD,           0x6, 0x1, CQ_ROP_QRAM_LOAD_UNC },
        { CQ_ROP_QRAM_LOAD_UNC,       0x6, 0x1, CQ_ROP_QRAM_LOAD },
        { CQ_ROP_QRAM_STORE,          0x7, 0x9, CQ_ROP_QRAM_STORE_UNC },
        { CQ_ROP_QRAM_STORE_UNC,      0xF, 0x9, CQ_ROP_QRAM_STORE },
        { CQ_ROP_QRAM_STORE_CTRL,     0x7, 0x9, CQ_ROP_QRAM_STORE_CTRL_UNC },
        { CQ_ROP_QRAM_STORE_CTRL_UNC, 0xF, 0x9, CQ_ROP_QRAM_STORE_CTRL },
    };

    for (size_t i = 0; i < sizeof ROWS / sizeof ROWS[0]; i++) {
        const cq_reff *e = cq_rec_effect((uint16_t)ROWS[i].op);
        CHECK(e != NULL);
        if (!e) continue;
        CHECK_EQ(e->reads,    ROWS[i].reads);
        CHECK_EQ(e->writes,   ROWS[i].writes);
        CHECK_EQ(e->controls, 0);
        CHECK_EQ(e->twin,     1);
        CHECK_EQ(e->twin_of,  ROWS[i].twin_of);
        CHECK_EQ(e->self_adjoint + e->neg_angle + e->neg_imm + e->diagonal, 0);
    }
    CHECK(cq_rec_table_is_complete());
}

/* -------------------------------------------------------------------------
 * 2. The certificate: the slot and `out` under a POISONED index.
 * ------------------------------------------------------------------------- */

/* THE CLAUSE D24 (c) EXISTS FOR. `cqrt_ry_i32` poisons every lane of the
 * index; after the store the slot's lanes are unknown to the shadow and after
 * the pop still unknown — so the shadow says UNPROVEN, and only the CERTIFICATE
 * (push and pop as twins, idx/val/array unchanged between) releases it. */
CQ_TEST(a_poisoned_index_store_unc_releases_the_slot_clean_by_the_certificate)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    uint32_t slot_idx[64], out_idx[64], n_slot, n_out;
    int32_t slot1, slot2, out;

    const int32_t a   = cqrt_qram_alloc_i32(8);
    const int32_t idx = cqrt_alloc_i32(0);
    cqrt_ry_i32(idx, 0.5);                            /* the corpus's shape */
    const int32_t vq  = cq_bk_reg(ctx, 32u, 0x5Au, 0xFFFFFFFFu);
    const int32_t val = cqrt_alloc_i32(7);

    /* The FIRST store finds every cell |0>, so its slot captures a constant;
     * the SECOND finds cell[idx] holding quantum lanes entangled with the
     * index, and its slot is a real, poisoned rail. That is the one the
     * certificate has to clear. */
    cqrt_qram_store_i32(a, idx, vq);
    slot1 = cq_qram_top(cq_qram_find(a))->slot;
    CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, slot1), 0);
    cqrt_qram_store_i32(a, idx, val);
    slot2 = cq_qram_top(cq_qram_find(a))->slot;
    CHECK(cq_reg_owned_qubits(&ctx->regs, slot2) > 0);
    n_slot = cq_pc_indices(ctx, slot2, slot_idx, 64u);

    out = cqrt_qram_load_i32(a, idx);
    n_out = cq_pc_indices(ctx, out, out_idx, 64u);
    cqrt_qram_load_i32_unc(out, a, idx);
    cqrt_free(out);
    CHECK(cq_pc_indices_are_free(ctx, out_idx, n_out));
    CHECK_EQ(cq_reg_strand_reports(ctx), 0);

    cqrt_qram_store_i32_unc(a, idx, val);
    CHECK_EQ(cq_reg_is_live(&ctx->regs, slot2), 0);
    CHECK(cq_pc_indices_are_free(ctx, slot_idx, n_slot));
    CHECK_EQ(cq_reg_strand_reports(ctx), 0);
    cqrt_qram_store_i32_unc(a, idx, vq);
    CHECK(cq_qram_top(cq_qram_find(a)) == NULL);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0);
    CHECK_EQ(cq_reg_stranded_unproven(ctx) + cq_reg_stranded_dirty(ctx), 0);
    cq_reg_audit(ctx);
    close_with(&m);
}

/* THE UNSOUND FACE, ASSERTED AS A STRAND: with no history on the token this
 * would RELEASE `out` dirty. The store between the pair writes the array; the
 * load pair's operand check reduces the array's writes over the window, finds
 * an unpaired one, and refuses — UNPROVEN, stranded, reported. */
CQ_TEST(a_store_between_a_load_and_its_unc_strands_the_out_rail)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    uint32_t out_idx[64], n_out;
    int32_t out;

    /* (i) A determinate index: the shadow SEES the dirt and convicts. */
    const int32_t a   = cqrt_qram_alloc_i32(4);
    const int32_t idx = cq_bk_reg(ctx, 32u, 2u, 0xFFFFFFFFu);
    const int32_t val = cq_bk_reg(ctx, 32u, 9u, 0xFFFFFFFFu);
    const int32_t v2  = cq_bk_reg(ctx, 32u, 3u, 0xFFFFFFFFu);

    cqrt_qram_store_i32(a, idx, val);
    out = cqrt_qram_load_i32(a, idx);
    CHECK_EQ(cq_pc_value(ctx, out), 9u);
    cqrt_qram_store_i32(a, idx, v2);                 /* the intervening write */
    cqrt_qram_load_i32_unc(out, a, idx);
    CHECK_EQ(cq_pc_value(ctx, out), 9u ^ 3u);        /* genuinely dirty      */
    n_out = cq_pc_indices(ctx, out, out_idx, 64u);
    cqrt_free(out);
    CHECK_EQ(cq_reg_strand_reports(ctx), 1);
    CHECK_EQ(cq_reg_stranded_dirty(ctx), 2);
    /* THE ACT IS PER QUBIT (D15 §3): 9 ^ 3 = 0b1010, so the shadow convicts
     * exactly two lanes and releases the thirty it can prove |0>. */
    CHECK(cq_pc_indices_settled(ctx, out_idx, n_out, 2u));
    close_with(&m);

    /* (ii) A POISONED index: the shadow cannot see, and only the CERTIFICATE's
     * refusal — an unpaired write to the ARRAY inside the load's window —
     * stands between `out` and the free list. UNPROVEN, stranded. */
    ctx = open_with(&m, &s);
    {
        const int32_t a2  = cqrt_qram_alloc_i32(4);
        const int32_t ip  = cqrt_alloc_i32(0);
        const int32_t vq  = cq_bk_reg(ctx, 32u, 9u, 0xFFFFFFFFu);
        const int32_t vq2 = cq_bk_reg(ctx, 32u, 3u, 0xFFFFFFFFu);
        cqrt_ry_i32(ip, 0.5);
        cqrt_qram_store_i32(a2, ip, vq);
        out = cqrt_qram_load_i32(a2, ip);
        cqrt_qram_store_i32(a2, ip, vq2);
        cqrt_qram_load_i32_unc(out, a2, ip);
        n_out = cq_pc_indices(ctx, out, out_idx, 64u);
        cqrt_free(out);
        CHECK_EQ(cq_reg_strand_reports(ctx), 1);
        CHECK(cq_reg_stranded_unproven(ctx) > 0);
        CHECK(cq_pc_indices_settled(ctx, out_idx, n_out, n_out));
    }
    close_with(&m);
}

/* THE SLOT'S CERTIFICATE HAS TEETH: a pop whose INDEX moved in between (a
 * `cqrt_xorc` on the same handle — the LIFO check compares handles, and this
 * is the same handle) runs the reverse circuit at the wrong cell, the slot is
 * genuinely not |0>, and the certificate must REFUSE the pair: `idx` has an
 * unpaired write inside the window. UNPROVEN, stranded, reported — and every
 * one of the slot's qubits stays off the free list. */
CQ_TEST(a_pop_whose_index_moved_in_between_strands_the_slot)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    uint32_t sl_idx[64], n_sl;
    int32_t slot;

    const int32_t a   = cqrt_qram_alloc_i32(4);
    const int32_t idx = cqrt_alloc_i32(0);
    cqrt_ry_i32(idx, 0.5);
    const int32_t vq  = cq_bk_reg(ctx, 32u, 0x5Au, 0xFFFFFFFFu);
    const int32_t val = cq_bk_reg(ctx, 32u, 0x33u, 0xFFFFFFFFu);

    cqrt_qram_store_i32(a, idx, vq);                 /* the cell is quantum */
    cqrt_qram_store_i32(a, idx, val);
    slot = cq_qram_top(cq_qram_find(a))->slot;
    n_sl = cq_pc_indices(ctx, slot, sl_idx, 64u);
    CHECK(n_sl > 0);
    cqrt_xorc_i32(idx, 1);                           /* the index MOVES     */
    cqrt_qram_store_i32_unc(a, idx, val);
    CHECK_EQ(cq_reg_strand_reports(ctx), 1);
    CHECK(cq_reg_stranded_unproven(ctx) > 0);
    CHECK(cq_pc_indices_settled(ctx, sl_idx, n_sl, n_sl));
    CHECK_EQ(cq_qram_find(a)->n_tape, 1u);
    close_with(&m);
}

/* -------------------------------------------------------------------------
 * 3. LIFO: two nested stores, and both slots clear.
 * ------------------------------------------------------------------------- */

CQ_TEST(nested_stores_pop_in_lifo_order_and_every_slot_clears)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    uint32_t s1_idx[64], s2_idx[64], n1, n2;
    int32_t slot1, slot2;

    const int32_t a  = cqrt_qram_alloc_i8(4);
    const int32_t i1 = cq_bk_reg(ctx, 32u, 1u, 0xFFFFFFFFu);
    const int32_t i2 = cq_bk_reg(ctx, 32u, 3u, 0xFFFFFFFFu);
    const int32_t v1 = cq_bk_reg(ctx, 8u, 0x5Au, 0xFFu);
    const int32_t v2 = cq_bk_reg(ctx, 8u, 0xC3u, 0xFFu);

    cqrt_qram_store_i8(a, i1, v1);
    slot1 = cq_qram_top(cq_qram_find(a))->slot;
    cqrt_qram_store_i8(a, i2, v2);
    slot2 = cq_qram_top(cq_qram_find(a))->slot;
    CHECK(slot1 != slot2);
    CHECK_EQ(cq_qram_find(a)->n_tape, 2u);
    CHECK_EQ(cq_pc_value(ctx, cq_qram_cell(cq_qram_find(a), 1)), 0x5Au);
    CHECK_EQ(cq_pc_value(ctx, cq_qram_cell(cq_qram_find(a), 3)), 0xC3u);
    n1 = cq_pc_indices(ctx, slot1, s1_idx, 64u);
    n2 = cq_pc_indices(ctx, slot2, s2_idx, 64u);

    cqrt_qram_store_i8_unc(a, i2, v2);
    CHECK_EQ(cq_qram_find(a)->n_tape, 1u);
    CHECK(cq_pc_indices_are_free(ctx, s2_idx, n2));
    cqrt_qram_store_i8_unc(a, i1, v1);
    CHECK_EQ(cq_qram_find(a)->n_tape, 0u);
    CHECK(cq_pc_indices_are_free(ctx, s1_idx, n1));
    for (int j = 0; j < 4; j++)
        CHECK_EQ(cq_pc_value(ctx, cq_qram_cell(cq_qram_find(a), j)), 0u);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0);
    cq_reg_audit(ctx);
    close_with(&m);
}

/* -------------------------------------------------------------------------
 * 4. §9: the controlled store under the four flag rows.
 * ------------------------------------------------------------------------- */

CQ_TEST(the_controlled_store_follows_the_flag_and_its_slot_still_clears)
{
    static const struct { uint64_t v, q; int writes; } ROWS[] = {
        { 0u, 0u, 0 }, { 1u, 0u, 1 }, { 0u, 1u, 0 }, { 1u, 1u, 1 },
    };

    for (size_t r = 0; r < sizeof ROWS / sizeof ROWS[0]; r++) {
        cq_mock m; cq_sink s;
        cq_ctx *ctx = open_with(&m, &s);
        uint32_t sl_idx[64], n_sl;
        int32_t slot, hs[12];

        const int32_t a    = cqrt_qram_alloc_i8(4);
        const int32_t idx  = cq_bk_reg(ctx, 32u, 2u, 0xFFFFFFFFu);
        const int32_t val  = cq_bk_reg(ctx, 8u, 0x3Cu, 0xFFu);
        const int32_t pred = cq_bk_reg(ctx, 1u, ROWS[r].v, ROWS[r].q);
        cq_mock_reset(&m);

        cqrt_qram_store_i8_controlled(pred, a, idx, val);
        slot = cq_qram_top(cq_qram_find(a))->slot;
        CHECK_EQ(cq_pc_value(ctx, cq_qram_cell(cq_qram_find(a), 2)),
                 ROWS[r].writes ? 0x3Cu : 0u);
        if (ROWS[r].q == 0u && ROWS[r].v == 0u) CHECK_EQ(cq_mock_count(&m), 0);
        if (ROWS[r].q == 1u) CHECK_EQ(cq_mock_count_op(&m, CQ_OP_X), 0);
        for (int j = 0; j < 4; j++) hs[j] = cq_qram_cell(cq_qram_find(a), j);
        hs[4] = idx; hs[5] = val; hs[6] = pred; hs[7] = slot;
        CHECK(cq_pc_live_is_exactly(ctx, hs, 8u));
        CHECK_EQ(cq_ctrl_depth(ctx), 0);
        n_sl = cq_pc_indices(ctx, slot, sl_idx, 64u);

        cqrt_qram_store_i8_controlled_unc(pred, a, idx, val);
        CHECK_EQ(cq_pc_value(ctx, cq_qram_cell(cq_qram_find(a), 2)), 0u);
        CHECK(cq_pc_indices_are_free(ctx, sl_idx, n_sl));
        CHECK_EQ(cq_reg_strand_reports(ctx), 0);
        CHECK(cq_pc_live_is_exactly(ctx, hs, 7u));
        cq_reg_audit(ctx);
        close_with(&m);
    }
}

#include "test_runtime_qram_corpus.inc"

CQ_TEST_MAIN(
    CQ_CASE(alloc_mints_a_token_and_count_cells_out_of_the_shared_counter),
    CQ_CASE(the_six_record_rows_are_what_d24_declares),
    CQ_CASE(the_load_reads_the_cell_at_the_truncated_index_over_random_draws),
    CQ_CASE(the_store_moves_the_old_cell_onto_the_slot_and_the_pop_restores_it),
    CQ_CASE(a_poisoned_index_store_unc_releases_the_slot_clean_by_the_certificate),
    CQ_CASE(a_store_between_a_load_and_its_unc_strands_the_out_rail),
    CQ_CASE(a_pop_whose_index_moved_in_between_strands_the_slot),
    CQ_CASE(nested_stores_pop_in_lifo_order_and_every_slot_clears),
    CQ_CASE(the_controlled_store_follows_the_flag_and_its_slot_still_clears),
    CQ_CASE(corpus_qram_store_and_qram_read_run_to_completion),
    CQ_CASE(corpus_qram_local_uncompute_frees_clean),
    CQ_CASE(corpus_guarded_store_multi_and_swapped_free_clean)
)
