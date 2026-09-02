/* Step 28 (v1.1, PRD §15 D23, bd 1za): the TAPE surface — shim/cq_runtime_tape.c,
 * the 11 `cqrt_tape_*` symbols: one token mint and two write families at five
 * widths.
 *
 * WHAT A TAPE WRITE IS, so the reader knows what the cases are FOR. cq_runtime.h
 * (READ-ONLY; CQ_lang is unpinned) defines `cqrt_tape_write_<W>(tape, src) ->
 * out` as a basis copy of `src` into a fresh |0> rail that is KEPT — never
 * freed, never uncomputed — and returns that rail's handle; the `_controlled`
 * form commits the content under an i1 flag, exactly as
 * `cqrt_copy_<W>_controlled`. So the SPECIFICATION is the copy's: `out ^= src`
 * with `out` born zero, `src` preserved, one CX per source WIRE, zero gates on
 * a classical source. There is no construction and nothing for Rule 1 to look
 * up. What is NEW is the token — `cqrt_tape_alloc() -> t<N>` is a CLASSICAL
 * handle owning zero qubits, drawn from the SAME D5 counter as every rail
 * (plan §0.5) — and the RECORD ROW, which must say the write READS `src`.
 *
 * THE ROW THAT MATTERS IS THE SOURCE'S FREE. Every sweep case brackets the
 * tape write between a self-adjoint `cqrt_copy` pair and then FREES the
 * source. That free is CLEAN only if the recorder modelled the write as a read
 * of `src`: a write recorded against `src` sits unpaired between the two
 * copies, the reduction cannot reach the first copy past it, and the source
 * STRANDS. L1 is green either way — the kept rail holds the right value under
 * both models — so the value check alone would pass the slot transposition
 * that D15 §6(ii) names as the write model's characteristic bug.
 *
 * L2 IS THE SET, TAKEN TWICE: after the write (`a`, `src`, `out`, and the flag
 * where there is one) and after `cqrt_free(src)` (`a`, `out` — the kept rail is
 * still live and still holds the value). A count sees neither the net-zero
 * swap nor a kept rail that quietly lost a lane.
 *
 * THE D21 ANNOTATION IS NOT HERE. `cq_trace_op` is inert under every sink but
 * qec, so "the kept rail gets a `#REGISTER` line and `cqrt_tape_alloc` opens no
 * bracket" lives in tests/test_shim_trace.c, behind -DCQOPS_QEC_DIR=.
 *
 * The seeds print with the count and the pool, so a red case reproduces from a
 * bare re-run (Rule 10's L1 row). CQOPS_L1_SAMPLES overrides the budget.
 *
 * RULE 12. Split on the seam recorded in IMPLEMENTATION_PLAN §3 (Step 28 row)
 * and TAKEN ON THE FIRST MEASUREMENT — the file landed at 367 against the 300
 * wall: `the SWEEP ↔ the ROWS`. tests/test_runtime_tape_sweep.inc carries the
 * ABI dispatch, the seeded draw and the two sweep cases; this file keeps the
 * token, the L5 rows, the promotion tuple, the recorder and the corpus shape.
 */

#include "cq_runtime_abi.h"
#include "cq_shim.h"
#include "cq_shim_ctx.h"
#include "cq_shim_proof.h"
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* tests/test_runtime_rail.c's preamble, verbatim and for the same reasons. */
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

/* -------------------------------------------------------------------------
 * 1. The token.
 * ------------------------------------------------------------------------- */

/* A TAPE HANDLE IS A NUMBER OUT OF THE SHARED COUNTER AND NOTHING ELSE: zero
 * gates, zero qubits, not live, width 0 — and the NEXT rail is `t + 1`, which
 * is what every golden shows (`cqrt_tape_alloc() -> t0`, then `-> h1`). The
 * counter clause is the one that cannot be relaxed: `tape` and `src` are both
 * int32_t, so a token drawn from anywhere else would collide with a rail's
 * number and a rail handed as a tape would be undetectable (D23). */
CQ_TEST(tape_alloc_mints_a_zero_qubit_token_out_of_the_shared_counter)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const cq_pc_snap before = cq_pc_take(ctx);

    const int32_t t0 = cqrt_tape_alloc();
    const int32_t h1 = cqrt_alloc_i32(4);
    const int32_t t2 = cqrt_tape_alloc();

    CHECK_EQ(cq_mock_count(&m), 0);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    CHECK_EQ(h1, t0 + 1);                        /* one counter, D5          */
    CHECK_EQ(t2, h1 + 1);
    CHECK_EQ(cq_reg_is_live(&ctx->regs, t0), 0); /* not a rail               */
    CHECK_EQ(cq_reg_width(&ctx->regs, t0), 0);   /* and no lanes to be one   */
    CHECK_EQ(cq_reg_is_live(&ctx->regs, h1), 1);
    CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ h1 }, 1u));
    cq_reg_audit(ctx);                           /* the sweep skips a token  */

    close_with(&m);
}

#include "test_runtime_tape_sweep.inc"

/* -------------------------------------------------------------------------
 * 3. L5 — the classical short-circuit, under every control row.
 * ------------------------------------------------------------------------- */

/* A CLASSICAL SOURCE COSTS ZERO GATES AND ZERO QUBITS UNDER NO CONTROL AND
 * UNDER A CLASSICAL ONE; under a QUANTUM flag it costs one CX per SET bit and
 * that is the correct answer rather than a regression (CLAUDE.md, "the
 * controlled axis makes L5's zero false"): the lane has to become a wire driven
 * by the flag, because rewriting a constant in place would run on both
 * branches. Q0 leaves the kept rail's VALUE at 0 and Q1 at the source's. */
CQ_TEST(a_classical_source_costs_nothing_and_a_quantum_flag_one_cx_per_set_bit)
{
    static const struct { cq_kd_ctrl mode; uint64_t want; } ROWS[] = {
        { CQ_KD_CTRL_NONE, 107u }, { CQ_KD_CTRL_ZERO, 0u },
        { CQ_KD_CTRL_ONE,  107u }, { CQ_KD_CTRL_Q0,   0u },
        { CQ_KD_CTRL_Q1,   107u },
    };

    for (size_t i = 0; i < sizeof ROWS / sizeof ROWS[0]; i++) {
        cq_mock m; cq_sink s;
        cq_ctx *ctx = open_with(&m, &s);
        const int32_t t   = cqrt_tape_alloc();
        const int32_t src = cqrt_alloc_i8(107);       /* 0b0110_1011, five set */
        int32_t flag = CQ_REG_NONE, out;
        int32_t hs[3];

        switch (ROWS[i].mode) {
        case CQ_KD_CTRL_ZERO: flag = cq_bk_reg(ctx, 1u, 0u, 0u); break;
        case CQ_KD_CTRL_ONE:  flag = cq_bk_reg(ctx, 1u, 1u, 0u); break;
        case CQ_KD_CTRL_Q0:   flag = cq_bk_reg(ctx, 1u, 0u, 1u); break;
        case CQ_KD_CTRL_Q1:   flag = cq_bk_reg(ctx, 1u, 1u, 1u); break;
        default: break;
        }
        cq_mock_reset(&m);
        const uint32_t live0 = cq_qubits_live(&ctx->pool);

        out = (flag == CQ_REG_NONE) ? cqrt_tape_write_i8(t, src)
                                    : cqrt_tape_write_i8_controlled(flag, t, src);

        CHECK_EQ(cq_pc_value(ctx, out), ROWS[i].want);
        CHECK_EQ(cq_pc_value(ctx, src), 107u);
        CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, src), 0);
        CHECK_EQ(cq_mock_count_op(&m, CQ_OP_X),   0);
        CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CCX), 0);
        if (ROWS[i].mode == CQ_KD_CTRL_Q0 || ROWS[i].mode == CQ_KD_CTRL_Q1) {
            CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CX), 5);
            CHECK_EQ(cq_qubits_live(&ctx->pool) - live0, 5);
            CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, out), 5);
        } else {
            CHECK_EQ(cq_mock_count(&m), 0);                    /* zero gates  */
            CHECK_EQ(cq_qubits_live(&ctx->pool) - live0, 0);   /* zero qubits */
            CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, out), 0);
        }
        CHECK_EQ(cq_ctrl_depth(ctx), 0);
        hs[0] = src; hs[1] = out; hs[2] = flag;
        CHECK(cq_pc_live_is_exactly(ctx, hs, flag == CQ_REG_NONE ? 2u : 3u));
        cq_reg_audit(ctx);
        close_with(&m);
    }
}

/* THE PROMOTION IDENTITY AT THE ALL-QUANTUM MASK: (0, W, 0) -> (0, 0, W). Pinned
 * here as a tuple rather than left to the sweep's per-draw counts because this
 * is the one mask where the whole write is gates and nothing folds. */
CQ_TEST(an_all_quantum_source_promotes_one_cx_per_lane_to_one_ccx)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t t    = cqrt_tape_alloc();
    const int32_t src  = cq_bk_reg(ctx, 8u, 0xA5u, 0xFFu);
    const int32_t flag = cq_bk_reg(ctx, 1u, 1u, 1u);
    int32_t o1, o2;

    cq_mock_reset(&m);
    o1 = cqrt_tape_write_i8(t, src);
    CHECK_GATES(cq_mock_count_op(&m, CQ_OP_X), cq_mock_count_op(&m, CQ_OP_CX),
                cq_mock_count_op(&m, CQ_OP_CCX), 0, 8, 0);

    cq_mock_reset(&m);
    o2 = cqrt_tape_write_i8_controlled(flag, t, src);
    CHECK_GATES(cq_mock_count_op(&m, CQ_OP_X), cq_mock_count_op(&m, CQ_OP_CX),
                cq_mock_count_op(&m, CQ_OP_CCX), 0, 0, 8);

    CHECK_EQ(cq_pc_value(ctx, o1), 0xA5u);
    CHECK_EQ(cq_pc_value(ctx, o2), 0xA5u);
    CHECK_EQ(cq_pc_value(ctx, src), 0xA5u);
    CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ src, flag, o1, o2 }, 4u));
    cq_reg_audit(ctx);

    close_with(&m);
}

/* -------------------------------------------------------------------------
 * 4. The recorder: the write is MODELLED, and modelled as a read of `src`.
 * ------------------------------------------------------------------------- */

/* THIS IS THE CASE THAT WAS RED BEFORE D23 FOR THE OPPOSITE REASON. Until the
 * two tape rows existed, a tape call reaching the recorder would have tripped
 * `cq_rec_push`'s refusal of an unmodelled opcode — the hard error
 * tests/test_shim_cert_death.c pins. It must now NOT fire, the table must still
 * be complete (every row but the mint writes something), and the row's SHAPE
 * must be the one D23 states: `src` read, `out` written from birth 0, the flag
 * a control. The token has no history at all. */
CQ_TEST(the_recorder_models_the_write_as_a_read_of_the_source)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t t    = cqrt_tape_alloc();
    const int32_t src  = cq_bk_reg(ctx, 8u, 0x3Cu, 0x0Fu);
    const int32_t flag = cq_bk_reg(ctx, 1u, 1u, 1u);
    const cq_call_rec *r;
    const cq_hist *h;
    uint32_t nw_src, pos;
    int32_t out;

    (void)ctx;
    CHECK(cq_rec_table_is_complete());
    nw_src = cq_rec_hist(src) ? cq_rec_hist(src)->nw : 0u;

    out = cqrt_tape_write_i8(t, src);
    pos = cq_rec_len() - 1u;
    r = cq_rec_at(pos);
    CHECK(r != NULL);
    if (r) {
        CHECK_EQ(r->op, (uint16_t)CQ_ROP_TAPE_WRITE);
        CHECK_EQ(r->h[0], src);
        CHECK_EQ(r->h[1], out);
        CHECK_EQ(r->h[2], CQ_REG_NONE);
        CHECK_EQ(r->ctrl, CQ_REG_NONE);
        CHECK_EQ(cq_rec_effect(r->op)->reads,  1u);        /* slot 0: src  */
        CHECK_EQ(cq_rec_effect(r->op)->writes, 2u);        /* slot 1: out  */
        CHECK_EQ(cq_rec_effect(r->op)->controls, 0u);
        /* A TAPE WRITE PAIRS WITH NOTHING, and the flags say so. Pinned here
         * because the battery found `self_adjoint = 1` on this row to be
         * INERT — every write mints a fresh `out`, so no two records ever share
         * their handles and adjoint_matches cannot fire — which makes the row's
         * declared intent the only thing that can carry the claim. */
        CHECK_EQ(cq_rec_effect(r->op)->self_adjoint, 0u);
        CHECK_EQ(cq_rec_effect(r->op)->neg_angle,    0u);
        CHECK_EQ(cq_rec_effect(r->op)->neg_imm,      0u);
        CHECK_EQ(cq_rec_effect(r->op)->twin,         0u);
    }
    h = cq_rec_hist(out);
    CHECK(h != NULL);
    if (h) {
        CHECK_EQ(h->nw, 1);                 /* born, then written once       */
        CHECK_EQ(h->birth_lo, 0);           /* from |0>                      */
        CHECK_EQ(h->width, 8);
    }
    h = cq_rec_hist(src);
    /* `src` came from the test-side builder and has no mint; its row is what
     * the recorder created for it on first touch, and that touch was a READ. */
    CHECK_EQ(cq_rec_writes_in(src, 0u, cq_rec_len(), NULL, 0u), nw_src);
    CHECK(cq_rec_hist(t) == NULL);         /* a token has no history        */

    out = cqrt_tape_write_i8_controlled(flag, t, src);
    r = cq_rec_at(cq_rec_len() - 1u);
    CHECK(r != NULL);
    if (r) {
        CHECK_EQ(r->op, (uint16_t)CQ_ROP_TAPE_WRITE_CTRL);
        CHECK_EQ(r->h[0], flag);
        CHECK_EQ(r->h[1], src);
        CHECK_EQ(r->h[2], out);
        CHECK_EQ(r->ctrl, flag);
        CHECK_EQ(cq_rec_effect(r->op)->reads,    3u);      /* flag, src    */
        CHECK_EQ(cq_rec_effect(r->op)->writes,   4u);      /* out          */
        CHECK_EQ(cq_rec_effect(r->op)->controls, 1u);      /* the flag     */
    }
    CHECK_EQ(cq_rec_writes_in(src, 0u, cq_rec_len(), NULL, 0u), nw_src);
    CHECK(cq_rec_table_is_complete());

    close_with(&m);
}

/* THE CORPUS SHAPE, END TO END: a template result written onto the tape, then
 * `_unc`'d and FREED (slice_io_controlled: `icmp_sgt` -> `tape_write_controlled`
 * -> `icmp_sgt_unc` -> `free`). The certificate pairs the forward with its
 * twin across the tape write and the free is CLEAN. A write model that put the
 * tape write on `src` would strand this rail — and it would strand it in six
 * shipped fixtures. */
CQ_TEST(a_template_result_written_to_the_tape_still_uncomputes_and_frees_clean)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t t = cqrt_tape_alloc();
    const int32_t a = cq_bk_reg(ctx, 8u, 0x5Au, 0xFFu);
    const int32_t b = cq_bk_reg(ctx, 8u, 0x0Fu, 0xF0u);
    const int32_t x = cq_shim_bin_qq(CQ_SHIM_OP_XOR, 8, a, b);
    uint32_t idx[8], n;
    int32_t out;

    out = cqrt_tape_write_i8(t, x);
    CHECK_EQ(cq_pc_value(ctx, out), 0x5Au ^ 0x0Fu);
    cq_shim_bin_qq_unc(CQ_SHIM_OP_XOR, 8, x, a, b);
    CHECK_EQ(cq_pc_value(ctx, x), 0);
    n = cq_pc_indices(ctx, x, idx, 8u);
    CHECK(n > 0u);
    cqrt_free(x);
    CHECK(cq_pc_indices_are_free(ctx, idx, n));
    CHECK_EQ(cq_reg_strand_reports(ctx), 0);
    CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ a, b, out }, 3u));
    CHECK_EQ(cq_pc_value(ctx, out), 0x5Au ^ 0x0Fu);
    cq_reg_audit(ctx);

    close_with(&m);
}

/* AND THE KEPT RAIL ITSELF IS NOT FREEABLE BY ARGUMENT — a caller who frees it
 * anyway gets D15's strand, never a release: its history is one lone write
 * that pairs with nothing, and on a lane the shadow can see is 1 the verdict
 * is a CONVICTION. Nothing reaches the free list. */
CQ_TEST(a_kept_rail_freed_by_a_caller_strands_rather_than_releases)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t t   = cqrt_tape_alloc();
    const int32_t src = cq_bk_reg(ctx, 8u, 0xFFu, 0xFFu);
    const int32_t out = cqrt_tape_write_i8(t, src);
    uint32_t idx[8], n;

    n = cq_pc_indices(ctx, out, idx, 8u);
    CHECK_EQ(n, 8);
    CHECK(cq_reg_disposition(ctx, out, cq_shim_free_proof) < 0);   /* convicted */
    cqrt_free(out);                          /* prints the one-shot STRANDED */
    CHECK(cq_pc_indices_settled(ctx, idx, n, n));   /* all eight stranded  */
    CHECK_EQ(cq_reg_strand_reports(ctx), 1);
    CHECK_EQ(cq_reg_stranded_dirty(ctx), 8);

    close_with(&m);
}

CQ_TEST_MAIN(
    CQ_CASE(tape_alloc_mints_a_zero_qubit_token_out_of_the_shared_counter),
    CQ_CASE(the_kept_rail_holds_the_sources_value_over_random_masks),
    CQ_CASE(the_controlled_write_holds_the_sources_value_under_a_set_quantum_flag),
    CQ_CASE(a_classical_source_costs_nothing_and_a_quantum_flag_one_cx_per_set_bit),
    CQ_CASE(an_all_quantum_source_promotes_one_cx_per_lane_to_one_ccx),
    CQ_CASE(the_recorder_models_the_write_as_a_read_of_the_source),
    CQ_CASE(a_template_result_written_to_the_tape_still_uncomputes_and_frees_clean),
    CQ_CASE(a_kept_rail_freed_by_a_caller_strands_rather_than_releases)
)
