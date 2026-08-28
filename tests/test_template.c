/* Step 23, landing 1, step 6: M26's OPCODE surface — the FIFTEEN `cq_shim_*`
 * entry points that `shim/cq_template_impl.c` and `shim/cq_template_dispatch.c`
 * define, and that all 992 of M28's generated integer wrappers call.
 *
 * WHAT THIS FILE IS FOR, subject by subject:
 *
 *   1. ONE CASE PER SHAPE. The ABI has five families — forward, `_unc`,
 *      `_controlled`, `icmp` and `cast` — crossed with three operand shapes
 *      (`qq`, `_hl`, `_lh`), and the wrappers carry NO logic, so a shape that
 *      resolves its handles wrongly is wrong for every one of its ~66 symbols
 *      at once. The value oracle is the register's VALUE against an
 *      independent reference, never "the shadow": under an all-classical
 *      operand mask every bit of the result is a constant and has no shadow.
 *
 *   2. THE DISPATCH TABLES, EXHAUSTIVELY. 13 opcodes, 10 predicates, 3 cast
 *      kinds. A table whose rows are transposed emits a perfectly well-formed
 *      circuit for the wrong function, and NOTHING structural can see it —
 *      `uge` spelled as `ule` emits the same tuple, keeps the palindrome and
 *      leaves scratch clean (K09's recorded trap). Only L1 against a reference
 *      that is not the kernel's own construction tells them apart, which is
 *      why the predicate rows go through `cq_ref_icmp` and why THE TWO ENUMS
 *      ARE IN DIFFERENT ORDERS: `cq_shim_pred` is `opcode_table.yaml`'s and
 *      `cq_icmp_pred` is the reference's, and a case that assumed they matched
 *      would test four rows against each other's answers.
 *
 *   3. THE RESULT WIDTH IS NOT THE OPERAND WIDTH for two of the three shapes,
 *      and the arity-2 signature actively suggests otherwise: `icmp` mints ONE
 *      BIT at any operand width and a cast mints `to_bits`. A `dst` minted at
 *      the operand width would still compare equal on the low lane and would
 *      leak `W-1` qubits per compare across a corpus that emits 21,323 of them.
 *
 *   4. THE HANDLE BOUNDARY — D7a's refusal and D7b's defensive copy — is in
 *      tests/test_template_d7.inc, on the seam IMPLEMENTATION_PLAN §3 recorded
 *      before this file existed: `the OPCODE SURFACE <-> the HANDLE BOUNDARY`.
 *      The discriminator is whether a case would still exist if every source
 *      handle were distinct. D7a itself is a hard error and lives in
 *      tests/test_template_death.c, which is a separate binary rather than a
 *      seam: `CQ_DEATH_MAIN` needs its own translation unit.
 */

#include "cq_shim.h"
#include "cq_shim_ctx.h"
#include "cq_shim_proof.h"

#include "cq_runtime_abi.h"

#include "bit.h"
#include "controlled.h"
#include "ctx.h"
#include "qubits.h"
#include "reg.h"
#include "rotate.h"
#include "sink.h"

#include "cqops/cqops.h"

#include "support/bitkinds.h"
#include "support/harness.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"
#include "support/refmodel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* tests/test_runtime_rail.c:fresh(), verbatim and for the same three reasons:
 * the context latches its sink at construction, so both the SELECTION and the
 * CONTEXT must be reset before the first cq_shim_ctx() of a case. */
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

/* An ALL-QUANTUM rail, which is the mask every gate-count claim in this project
 * is pinned at and the one mask a fold cannot move (D6: no demotion, so a mask
 * only ever drifts towards Q). */
static int32_t qreg(cq_ctx *ctx, uint32_t W, uint64_t v)
{
    return cq_bk_reg(ctx, W, v, cq_ref_mask((int)W));
}

/* THE REFERENCE IS THE PROJECT'S OWN `cq_ref_w`, not a re-derivation, and that
 * is the right call HERE and would be the wrong one inside a kernel suite: what
 * this file is testing is the DISPATCH — that `CQ_SHIM_OP_UDIV` reaches
 * `cq_kernel_udiv` and not `cq_kernel_urem` — and for that the reference only
 * has to be an independent statement of WHICH FUNCTION each row names. The
 * kernels' own agreement with `cq_ref_w` is M14/M18/M19/M20's business and is
 * already asserted, exhaustively, in their suites. */
static cq_ref_w ref_bin(cq_shim_op op, cq_ref_w a, cq_ref_w b, int W)
{
    switch (op) {
    case CQ_SHIM_OP_ADD:  return cq_ref_w_add (a, b, W);
    case CQ_SHIM_OP_SUB:  return cq_ref_w_sub (a, b, W);
    case CQ_SHIM_OP_MUL:  return cq_ref_w_mul (a, b, W);
    case CQ_SHIM_OP_SDIV: return cq_ref_w_sdiv(a, b, W);
    case CQ_SHIM_OP_UDIV: return cq_ref_w_udiv(a, b, W);
    case CQ_SHIM_OP_SREM: return cq_ref_w_srem(a, b, W);
    case CQ_SHIM_OP_UREM: return cq_ref_w_urem(a, b, W);
    case CQ_SHIM_OP_AND:  return cq_ref_w_and(a, b);
    case CQ_SHIM_OP_OR:   return cq_ref_w_or (a, b);
    /* `a ^ b` as `(a | b) & ~(a & b)`: `refmodel.h` ships no wide XOR, and
     * spelling it from the two it does ship keeps this a REFERENCE rather than
     * a second call into the thing under test. */
    case CQ_SHIM_OP_XOR:  return cq_ref_w_andnot(cq_ref_w_or(a, b),
                                                 cq_ref_w_and(a, b));
    case CQ_SHIM_OP_SHL:  return cq_ref_w_shl (a, (int)b.lo, W);
    case CQ_SHIM_OP_LSHR: return cq_ref_w_lshr(a, (int)b.lo, W);
    default:              return cq_ref_w_ashr(a, (int)b.lo, W);
    }
}

/* The thirteen opcodes with an operand PAIR that is legal for every one of them
 * — no division by zero (D3 makes that deterministic-but-unspecified and this
 * file is not where it is pinned) and a shift amount inside K4's mask at W = 8,
 * so `b` reads as 3 both as a value and as an amount. */
#define TPL_W   8u
#define TPL_A   0xB5u
#define TPL_B   0x03u

static const cq_shim_op ALL_OPS[] = {
    CQ_SHIM_OP_ADD, CQ_SHIM_OP_SUB,  CQ_SHIM_OP_MUL,  CQ_SHIM_OP_SDIV,
    CQ_SHIM_OP_UDIV, CQ_SHIM_OP_SREM, CQ_SHIM_OP_UREM, CQ_SHIM_OP_AND,
    CQ_SHIM_OP_OR,  CQ_SHIM_OP_XOR,  CQ_SHIM_OP_SHL,  CQ_SHIM_OP_LSHR,
    CQ_SHIM_OP_ASHR
};

/* `cq_shim_pred` is opcode_table.yaml's order and `cq_icmp_pred` is the
 * reference's, AND THEY ARE DIFFERENT ORDERS. Four rows would silently swap
 * signed for unsigned without this map, and at operands that are equal or that
 * share a sign bit those four agree with each other — which is exactly the
 * shape that passes. */
static const struct { cq_shim_pred shim; cq_icmp_pred ref; } PRED_MAP[] = {
    { CQ_SHIM_PRED_EQ,  CQ_ICMP_EQ  }, { CQ_SHIM_PRED_NE,  CQ_ICMP_NE  },
    { CQ_SHIM_PRED_SLT, CQ_ICMP_SLT }, { CQ_SHIM_PRED_SGT, CQ_ICMP_SGT },
    { CQ_SHIM_PRED_SLE, CQ_ICMP_SLE }, { CQ_SHIM_PRED_SGE, CQ_ICMP_SGE },
    { CQ_SHIM_PRED_ULT, CQ_ICMP_ULT }, { CQ_SHIM_PRED_UGT, CQ_ICMP_UGT },
    { CQ_SHIM_PRED_ULE, CQ_ICMP_ULE }, { CQ_SHIM_PRED_UGE, CQ_ICMP_UGE }
};

/* -------------------------------------------------------------------------
 * 1. The three FORWARD shapes.
 * ------------------------------------------------------------------------- */

/* EVERY ONE OF THE THIRTEEN OPCODES, through the `qq` door, against a reference
 * that is not the kernel. This is the case a transposed dispatch row dies on
 * and the only one that can: a wrong row emits a well-formed circuit for a
 * different function, so gate counts, the palindrome, the pool and the shadow
 * all stay green. */
CQ_TEST(every_opcode_dispatches_to_its_own_kernel_through_the_qq_door)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    for (size_t i = 0; i < sizeof ALL_OPS / sizeof *ALL_OPS; i++) {
        const cq_pc_snap before = cq_pc_take(ctx);
        const int32_t a = qreg(ctx, TPL_W, TPL_A);
        const int32_t b = qreg(ctx, TPL_W, TPL_B);
        const int32_t r = cq_shim_bin_qq(ALL_OPS[i], (int)TPL_W, a, b);
        const int32_t live[3] = { a, b, r };

        CHECK_EQ(cq_reg_width(&ctx->regs, r), TPL_W);
        CHECK(cq_ref_w_eq(cq_pc_value_w(ctx, r),
                          ref_bin(ALL_OPS[i],
                                  cq_ref_w_make(TPL_A, 0u, (int)TPL_W),
                                  cq_ref_w_make(TPL_B, 0u, (int)TPL_W),
                                  (int)TPL_W)));

        /* L2 over the three rails this call left live: an ancilla the kernel
         * leaked, or a scratch rail M26 minted and forgot, is an index nobody
         * owns and there is no other detector for it here. */
        CHECK(cq_pc_live_is_exactly(ctx, live, 3u));

        cqrt_free(a); cqrt_free(b); cqrt_free(r);
        CHECK(cq_pc_same(cq_pc_take(ctx), before));
    }
    close_with(&m);
}

/* THE LITERAL LANE, BOTH WAYS ROUND, AND `sub` IS THE OPERAND-ORDER DETECTOR.
 * `_hl` is `f(rail, literal)` and `_lh` is `f(literal, rail)`; for a commutative
 * opcode the two are the same function and a transposition is invisible, which
 * is why this drives `sub` and `lshr` rather than `add` and `and`. */
CQ_TEST(the_hl_and_lh_doors_put_the_literal_on_the_side_the_symbol_names)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t a  = qreg(ctx, TPL_W, TPL_A);
    const int32_t hl = cq_shim_bin_hl(CQ_SHIM_OP_SUB, (int)TPL_W, a, TPL_B, 0u);
    const int32_t lh = cq_shim_bin_lh(CQ_SHIM_OP_SUB, (int)TPL_W, TPL_B, 0u, a);

    CHECK_EQ(cq_pc_value(ctx, hl), (TPL_A - TPL_B) & cq_ref_mask((int)TPL_W));
    CHECK_EQ(cq_pc_value(ctx, lh), (TPL_B - TPL_A) & cq_ref_mask((int)TPL_W));

    /* THE SHIFTS TAKE THE BARREL THROUGH BOTH DOORS (bd 216 checklist 12), and
     * `_lh` — a classical VALUE with a QUANTUM amount — is the half that
     * genuinely needs it: M11 cannot serve a quantum amount at all. */
    {
        const int32_t amt = qreg(ctx, TPL_W, 3u);
        const int32_t sv  = cq_shim_bin_lh(CQ_SHIM_OP_LSHR, (int)TPL_W,
                                           0xF0u, 0u, amt);
        CHECK_EQ(cq_pc_value(ctx, sv), 0xF0u >> 3);
        cqrt_free(amt); cqrt_free(sv);
    }
    cqrt_free(a); cqrt_free(hl); cqrt_free(lh);
    close_with(&m);
}

/* THE TWO-WORD LITERAL IS LITTLE-ENDIAN AND BOTH WORDS ARE `uint64_t`, so C
 * accepts a transposition in silence — `shim/cq_shim.h` says so and pins the
 * generated argument list; this is the other end of that pin, at the only two
 * widths where the high word is reachable at all. */
CQ_TEST(the_two_word_literal_reaches_a_rail_above_bit_63)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t a = cq_reg_alloc_zero(&ctx->regs, 128u);
    const int32_t r = cq_shim_bin_hl(CQ_SHIM_OP_XOR, 128,
                                     a, 0x0123456789ABCDEFu, 0xFEDCBA9876543210u);
    const cq_ref_w got = cq_pc_value_w(ctx, r);

    CHECK_EQ(got.lo, 0x0123456789ABCDEFu);
    CHECK_EQ(got.hi, 0xFEDCBA9876543210u);

    /* i80 truncates by construction: `cq_bits_from_words` reads exactly `width`
     * bits, so the top 48 bits of the high word never arrive and NO separate
     * mask is wanted here (bd 216 checklist 13). */
    {
        const int32_t a80 = cq_reg_alloc_zero(&ctx->regs, 80u);
        const int32_t r80 = cq_shim_bin_hl(CQ_SHIM_OP_XOR, 80,
                                           a80, ~0u, 0xFFFFFFFFFFFFFFFFu);
        CHECK_EQ(cq_pc_value_w(ctx, r80).hi, 0xFFFFu);
        cqrt_free(a80); cqrt_free(r80);
    }
    cqrt_free(a); cqrt_free(r);
    close_with(&m);
}

/* -------------------------------------------------------------------------
 * 2. `_unc` — `out ^= f(sources)`, and NOT a reclaim.
 * ------------------------------------------------------------------------- */

/* THE SAME KERNEL AT A DIFFERENT REPRESENTATION (Rule 14), so the assertion is
 * on VALUES and never on kinds or on counts. All three operand shapes, because
 * `_unc` is where D7a can arise and where a shape that resolved `out` as a
 * source would be caught by nothing else. */
CQ_TEST(every_unc_shape_xors_the_same_function_back_out)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const cq_pc_snap before = cq_pc_take(ctx);

    const int32_t a = qreg(ctx, TPL_W, TPL_A);
    const int32_t b = qreg(ctx, TPL_W, TPL_B);

    const int32_t qq = cq_shim_bin_qq(CQ_SHIM_OP_ADD, (int)TPL_W, a, b);
    cq_shim_bin_qq_unc(CQ_SHIM_OP_ADD, (int)TPL_W, qq, a, b);
    CHECK_EQ(cq_pc_value(ctx, qq), 0u);

    const int32_t hl = cq_shim_bin_hl(CQ_SHIM_OP_SUB, (int)TPL_W, a, TPL_B, 0u);
    cq_shim_bin_hl_unc(CQ_SHIM_OP_SUB, (int)TPL_W, hl, a, TPL_B, 0u);
    CHECK_EQ(cq_pc_value(ctx, hl), 0u);

    const int32_t lh = cq_shim_bin_lh(CQ_SHIM_OP_SUB, (int)TPL_W, TPL_B, 0u, a);
    cq_shim_bin_lh_unc(CQ_SHIM_OP_SUB, (int)TPL_W, lh, TPL_B, 0u, a);
    CHECK_EQ(cq_pc_value(ctx, lh), 0u);

    const int32_t f = cq_shim_icmp_qq(CQ_SHIM_PRED_ULT, (int)TPL_W, b, a);
    CHECK_EQ(cq_pc_value(ctx, f), 1u);
    cq_shim_icmp_qq_unc(CQ_SHIM_PRED_ULT, (int)TPL_W, f, b, a);
    CHECK_EQ(cq_pc_value(ctx, f), 0u);

    const int32_t g = cq_shim_icmp_hl(CQ_SHIM_PRED_EQ, (int)TPL_W, a, TPL_A, 0u);
    CHECK_EQ(cq_pc_value(ctx, g), 1u);
    cq_shim_icmp_hl_unc(CQ_SHIM_PRED_EQ, (int)TPL_W, g, a, TPL_A, 0u);
    CHECK_EQ(cq_pc_value(ctx, g), 0u);

    /* `_unc` RECLAIMS NOTHING (PRD §10): `cqrt_free` is the sole deallocator,
     * so all five rails are still live here and every index comes back only
     * because this case frees them. */
    cqrt_free(a); cqrt_free(b); cqrt_free(qq); cqrt_free(hl);
    cqrt_free(lh); cqrt_free(f); cqrt_free(g);
    CHECK(cq_pc_same(cq_pc_take(ctx), before));
    close_with(&m);
}

/* -------------------------------------------------------------------------
 * 3. `_controlled` — PRD §9 row 0, through the shim's ONE bracket.
 * ------------------------------------------------------------------------- */

/* ROW 0 IS A DECISION AND NOT A CIRCUIT: a CQ_BIT_ZERO flag skips the body
 * entirely — 0 gates AND 0 qubits — and a CQ_BIT_ONE flag emits it VERBATIM.
 * The zero-qubit half needs `cq_sandwich`'s own short-circuit as well as the
 * emitter's, because the driver pre-materialises its whole scratch region
 * before any gate is emitted (I6(b)); this is the one place in the shim's
 * opcode surface that drives a real kernel through it. */
CQ_TEST(a_controlled_call_obeys_row_zero_on_a_classical_flag)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t a = qreg(ctx, TPL_W, TPL_A);
    const int32_t b = qreg(ctx, TPL_W, TPL_B);

    const int32_t zero = cq_reg_alloc_const(&ctx->regs, 1u, 0u, 0u);
    const cq_pc_snap pre = cq_pc_take(ctx);
    cq_mock_reset(&m);
    const int32_t rz = cq_shim_bin_qq_ctrl(CQ_SHIM_OP_ADD, (int)TPL_W, zero, a, b);

    CHECK_EQ((int)cq_mock_count(&m), 0);
    CHECK_EQ(cq_pc_value(ctx, rz), 0u);
    /* `live` moved by nothing: the result rail is all-constant, so by I4 it
     * owns no qubit at all. A region that emitted its body and then folded
     * would move it by W. */
    CHECK(cq_pc_same(cq_pc_take(ctx), pre));

    const int32_t one = cq_reg_alloc_const(&ctx->regs, 1u, 1u, 0u);
    cq_mock_reset(&m);
    const int32_t ro = cq_shim_bin_qq_ctrl(CQ_SHIM_OP_ADD, (int)TPL_W, one, a, b);
    const size_t promoted = cq_mock_count(&m);

    cq_mock_reset(&m);
    const int32_t rp = cq_shim_bin_qq(CQ_SHIM_OP_ADD, (int)TPL_W, a, b);
    CHECK_EQ((int)promoted, (int)cq_mock_count(&m));
    CHECK_EQ(cq_pc_value(ctx, ro), cq_pc_value(ctx, rp));

    cqrt_free(a); cqrt_free(b); cqrt_free(zero); cqrt_free(one);
    cqrt_free(rz); cqrt_free(ro); cqrt_free(rp);
    close_with(&m);
}

/* A QUANTUM FLAG PROMOTES, AND THE IDENTITY IS SCOPED TO THE ALL-QUANTUM MASK:
 * `(x, cx, ccx) -> (0, x, cx + 3*ccx)`. Asserted through `cq_kd`'s own
 * arithmetic rather than a golden, so `CQOPS_UPDATE_GOLDENS=1` cannot bless a
 * region that silently stopped promoting. */
CQ_TEST(a_quantum_flag_promotes_every_gate_the_kernel_emits)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t a = qreg(ctx, TPL_W, TPL_A);
    const int32_t b = qreg(ctx, TPL_W, TPL_B);

    cq_mock_reset(&m);
    const int32_t plain = cq_shim_bin_qq(CQ_SHIM_OP_AND, (int)TPL_W, a, b);
    const size_t p_x   = cq_mock_count_op(&m, CQ_OP_X);
    const size_t p_cx  = cq_mock_count_op(&m, CQ_OP_CX);
    const size_t p_ccx = cq_mock_count_op(&m, CQ_OP_CCX);
    CHECK(p_ccx > 0u);   /* the identity is VACUOUS on an empty tuple */

    const int32_t flag = qreg(ctx, 1u, 1u);
    cq_mock_reset(&m);
    const int32_t got = cq_shim_bin_qq_ctrl(CQ_SHIM_OP_AND, (int)TPL_W, flag, a, b);

    CHECK_EQ((int)cq_mock_count_op(&m, CQ_OP_X),   0);
    CHECK_EQ((int)cq_mock_count_op(&m, CQ_OP_CX),  (int)p_x);
    CHECK_EQ((int)cq_mock_count_op(&m, CQ_OP_CCX), (int)(p_cx + 3u * p_ccx));
    CHECK_EQ(cq_pc_value(ctx, got), cq_pc_value(ctx, plain));

    cqrt_free(a); cqrt_free(b); cqrt_free(flag);
    cqrt_free(plain); cqrt_free(got);
    close_with(&m);
}

/* The `_hl` and `_lh` controlled doors exist too, and each is ~72 of the 214
 * controlled grid symbols. They are one call each because what they add over
 * the `qq` door is only which lane carries the literal — already pinned above
 * — and which handle reaches `cq_reg_check_operands`, which is checklist item
 * 6 and is pinned by its own death case. */
CQ_TEST(the_controlled_literal_doors_reach_the_region_with_the_right_lane)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t a    = qreg(ctx, TPL_W, TPL_A);
    const int32_t one  = cq_reg_alloc_const(&ctx->regs, 1u, 1u, 0u);
    const int32_t hl   = cq_shim_bin_hl_ctrl(CQ_SHIM_OP_SUB, (int)TPL_W,
                                             one, a, TPL_B, 0u);
    const int32_t lh   = cq_shim_bin_lh_ctrl(CQ_SHIM_OP_SUB, (int)TPL_W,
                                             one, TPL_B, 0u, a);

    CHECK_EQ(cq_pc_value(ctx, hl), (TPL_A - TPL_B) & cq_ref_mask((int)TPL_W));
    CHECK_EQ(cq_pc_value(ctx, lh), (TPL_B - TPL_A) & cq_ref_mask((int)TPL_W));
    CHECK_EQ(cq_ctrl_depth(ctx), 0);

    cqrt_free(a); cqrt_free(one); cqrt_free(hl); cqrt_free(lh);
    close_with(&m);
}

/* -------------------------------------------------------------------------
 * 4-5. `icmp` and `cast` — the two families whose RESULT WIDTH is not their
 * operand width — are in tests/test_template_width.inc, on the second seam
 * IMPLEMENTATION_PLAN §3 records for this file. See that file's header.
 * ------------------------------------------------------------------------- */

#include "test_template_width.inc"

/* -------------------------------------------------------------------------
 * 6. The handle boundary — D7a's siblings and D7b. See the .inc's header.
 * ------------------------------------------------------------------------- */

#include "test_template_d7.inc"

CQ_TEST_MAIN(
    CQ_CASE(every_opcode_dispatches_to_its_own_kernel_through_the_qq_door),
    CQ_CASE(the_hl_and_lh_doors_put_the_literal_on_the_side_the_symbol_names),
    CQ_CASE(the_two_word_literal_reaches_a_rail_above_bit_63),
    CQ_CASE(every_unc_shape_xors_the_same_function_back_out),
    CQ_CASE(a_controlled_call_obeys_row_zero_on_a_classical_flag),
    CQ_CASE(a_quantum_flag_promotes_every_gate_the_kernel_emits),
    CQ_CASE(the_controlled_literal_doors_reach_the_region_with_the_right_lane),
    CQ_CASE(every_predicate_dispatches_to_its_own_kernel_and_mints_one_bit),
    CQ_CASE(a_cast_mints_the_to_width_and_each_kind_reaches_its_own_kernel),
    CQ_CASE(d7b_copies_one_aliased_source_and_returns_the_temporary_to_zero),
    CQ_CASE(d7b_costs_exactly_two_w_cnots_and_no_toffoli_over_the_unaliased_call),
    CQ_CASE(d7b_reaches_the_unc_and_icmp_doors_too),
    CQ_CASE(d7b_on_a_rotation_poisoned_source_is_discharged_by_the_certificate),
    CQ_CASE(the_d7b_copy_brackets_the_region_from_outside_on_both_sides)
)
