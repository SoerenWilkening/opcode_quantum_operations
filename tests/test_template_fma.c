/* tests/test_template_fma.c — M26's ARITY-3 surface: the eight
 * `cq_template_fma_f64_*` symbols and the `cq_tpl_ternary` sequence behind
 * them. PRD-v2 §6.1's vendoring (bead 9ve.24), §7.11, PRD §15 D7a/D7b, D15,
 * D21.
 *
 * A SEPARATE BINARY FROM test_template_fp, on the same grounds Wave 8 used to
 * split that one off test_template: this family's novelties are things no
 * arity-2 family has — TWO literal lanes, SIX aliasing pairs, a fourth slot in
 * D15's record, and an eighth tag space — and every case below turns on one of
 * them. `test_template_fp.c` stood at 279 counted lines of Rule 12's 300 with
 * three `.inc`s already hanging off it.
 *
 * THE SHARPEST CASE HERE IS THE POISONED ONE AND IT IS THE ONLY DETECTOR FOR
 * THE EFFECT-TABLE WIDENING. `CQ_ROP_TPL_FWD`/`TPL_UNC` had to start reading
 * slot 3 for `c` to exist to the certificate at all; without it a forward, an
 * `X` on `c` and the `_unc` reduce to identity and RELEASE a rail the two
 * halves no longer cancel on. On an UNPOISONED input the shadow answers first
 * and every defect in D15's twin identity is invisible — measured one family
 * over, where dropping `CQ_ROP_TPL_FWD`'s write row left the unpoisoned case
 * green (bd memory the-shadow-masks-every-certificate-defect-off-the-rotation-taint).
 *
 * THERE IS NO D14 CASE IN THIS FILE AND THAT IS A FACT ABOUT THE ABI RATHER
 * THAN AN OMISSION. `intrinsic_table.yaml`'s `fma` row carries variants
 * `[fwd_qqq, fwd_qql, fwd_qlq, fwd_qll, unc_qqq, unc_qql, unc_qlq, unc_qll]`
 * — no `inv` anywhere — so there are no `_inv` symbols to abort and no D14
 * sentence to owe. `tests/test_gen_shim.py`'s census is what pins that: `fma`
 * is absent from the f64 still-aborting set entirely.
 */

#include "cq_shim.h"
#include "cq_shim_ctx.h"
#include "cq_shim_record.h"
#include "cq_template_boundary.h"
#include "cq_runtime_abi.h"

#include "ctx.h"
#include "reg.h"
#include "qubits.h"
#include "sink.h"

#include "kernels/fma.h"
#include "kernels/fmul.h"

#include "cqops/cqops.h"

#include "support/bitkinds.h"
#include "support/fpanchors.h"
#include "support/harness.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"
#include "support/refmodel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* THE EIGHT SYMBOLS, DECLARED HERE because `shim/cq_runtime_abi.h` is
 * CQ_lang's `cqrt_*` core and carries no `cq_template_*` at all. Copied from
 * `tests/abi/cq_intrinsic_templates_abi.txt` — the manifest extracted verbatim
 * from CQ_lang's generated header — so a signature that drifted would fail to
 * compile HERE as well as failing the generator's own comparison. This is the
 * shape every other template suite uses. */
int32_t cq_template_fma_f64(int32_t, int32_t, int32_t);
int32_t cq_template_fma_f64_qql(int32_t, int32_t, double);
int32_t cq_template_fma_f64_qlq(int32_t, double, int32_t);
int32_t cq_template_fma_f64_qll(int32_t, double, double);
void    cq_template_fma_f64_unc(int32_t, int32_t, int32_t, int32_t);
void    cq_template_fma_f64_qql_unc(int32_t, int32_t, int32_t, double);
void    cq_template_fma_f64_qlq_unc(int32_t, int32_t, double, int32_t);
void    cq_template_fma_f64_qll_unc(int32_t, int32_t, double, double);

/* The one sibling the tag-space case needs. */
int32_t cq_template_fmul_f64(int32_t, int32_t);

enum { W64 = 64 };

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

/* A `double` carrying a chosen BIT PATTERN. A memcpy, never a union and never
 * a pointer cast; this file does no host fp arithmetic. */
static double f64_of(uint64_t u)
{
    double d;
    memcpy(&d, &u, sizeof d);
    return d;
}

static int32_t qreg(cq_ctx *ctx, uint32_t W, uint64_t v)
{
    return cq_bk_reg(ctx, W, v, cq_ref_mask((int)W));
}

/* A POISONED WIRE RAIL BUILT THROUGH THE ABI, AND THE DIFFERENCE FROM `qreg`
 * IS WHAT MAKES THE CERTIFICATE CASES MEAN ANYTHING.
 *
 * MEASURED WHILE WRITING THIS FILE. `cq_bk_reg` mints straight into M07's
 * table, so `cq_rec_mint` never runs and the handle has NO recorded history at
 * all — and `pair_operands_unchanged` opens each read slot with
 * `if (!cq_rec_hist(c1->h[i])) continue;`, which SKIPS it. So a source built
 * with `qreg` is invisible to the operand-stability check: an intervening
 * write to it is never examined, the forward/`_unc` pair is accepted, and the
 * result rail reduces to identity and RELEASES. The first draft of
 * `an_fma_unc_whose_third_source_moved_is_refused_by_the_certificate` used
 * `qreg` and reported a green release — the library was right and the FIXTURE
 * was disarmed.
 *
 * `cqrt_alloc_f64` records the mint and `cqrt_ry_f64` at §7's GENERAL row both
 * MATERIALISES all 64 lanes and poisons the shadow (PRD §15 D12), which is the
 * pair of properties every case below needs: a real history for the engine and
 * an unproven shadow so the certificate is what answers. */
static int32_t wire(uint64_t v)
{
    int32_t h = cqrt_alloc_f64(f64_of(v));

    cqrt_ry_f64(h, 0.5);
    return h;
}

/* §7.12's cells as OPERAND TRIPLES, each here because some branch of
 * `soft_fma` reaches it and nothing cheaper does. The first is the Kahan
 * single-rounding witness — `x*x` exactly, minus its own rounded value. */
#define FT_X          0x3ff0000000000001ull   /* 1 + 2^-52              */
#define FT_NEG_X2_RND 0xbff0000000000002ull   /* -fl(x*x)               */

static const uint64_t FMA_TRIPLES[][3] = {
    { FT_X, FT_X, FT_NEG_X2_RND },                              /* residue  */
    { CQ_F64_ONE, 0x4000000000000000ull, 0x4008000000000000ull },
    { CQ_F64_POS_ZERO, CQ_F64_NEG_ZERO, CQ_F64_NEG_ZERO },      /* -0 rules */
    { CQ_F64_QNAN_A, CQ_F64_QNAN_B, CQ_F64_ONE },               /* a wins   */
    { CQ_F64_ONE, CQ_F64_QNAN_B, CQ_F64_QNAN_A },               /* b wins   */
    { CQ_F64_POS_INF, CQ_F64_POS_ZERO, CQ_F64_ONE },            /* INDEF    */
    { CQ_F64_POS_INF, CQ_F64_ONE, CQ_F64_NEG_INF },             /* clash    */
    { 0x0000000000000001ull, CQ_F64_ONE, CQ_F64_MIN_NORMAL }    /* subnormal*/
};
#define FMA_N (sizeof FMA_TRIPLES / sizeof FMA_TRIPLES[0])

/* ---- (1) Every wrapper agrees with the kernel's own classical row. ------- */

/* ALL EIGHT SYMBOLS, AND THE FOUR SHAPES ARE THE POINT. `qqq` reads three
 * handles; `qql`, `qlq` and `qll` put a literal in one or both of the lanes
 * the ABI allows one in, and each literal lane needs its OWN two-word pair —
 * a single shared pair would make `fma(a, 2.0, 3.0)` compute `fma(a, 3.0,
 * 3.0)`, which is the same shape, the same gate count and the wrong value.
 *
 * ALL-CLASSICAL RAILS, so this is also L5 at the handle boundary: a triple
 * with no quantum input costs ZERO gates and ZERO qubits even though the
 * kernel behind it is 241,083 slots. */
CQ_TEST(every_fma_f64_wrapper_agrees_with_its_kernels_own_eval)
{
    for (size_t i = 0; i < FMA_N; i++) {
        cq_mock m; cq_sink s;
        cq_ctx *ctx = open_with(&m, &s);
        const uint64_t a = FMA_TRIPLES[i][0], b = FMA_TRIPLES[i][1],
                       c = FMA_TRIPLES[i][2];
        const uint64_t want = cq_fma_eval(a, b, c);
        uint32_t live0 = cq_qubits_live(&ctx->pool);
        int32_t ha, hb, hc, r;

        ha = cqrt_alloc_f64(f64_of(a));
        hb = cqrt_alloc_f64(f64_of(b));
        hc = cqrt_alloc_f64(f64_of(c));

        r = cq_template_fma_f64(ha, hb, hc);
        CHECK_EQ(cq_reg_width(&ctx->regs, r), (uint32_t)W64);
        CHECK_EQ(cq_pc_value(ctx, r), want);

        CHECK_EQ(cq_pc_value(ctx, cq_template_fma_f64_qql(ha, hb, f64_of(c))),
                 want);
        CHECK_EQ(cq_pc_value(ctx, cq_template_fma_f64_qlq(ha, f64_of(b), hc)),
                 want);
        CHECK_EQ(cq_pc_value(ctx,
                             cq_template_fma_f64_qll(ha, f64_of(b), f64_of(c))),
                 want);

        /* L5 AT THE HANDLE BOUNDARY: no quantum input, so no gate and no
         * qubit, four calls deep. */
        CHECK_EQ(cq_mock_count(&m), 0);
        CHECK_EQ(cq_qubits_live(&ctx->pool), live0);

        cq_reg_audit(ctx);
        close_with(&m);
    }
}

/* ---- (2) The literal lanes carry the PATTERN, not the value. ------------ */

/* `CQ_SHIM_LO(x)` IS A NUMERIC CONVERSION AND WOULD TURN 3.5 INTO 3. It
 * compiles clean, is silent under -Wconversion because the cast inside it is
 * explicit, and only a test that reads the RAIL'S BITS can see it. The two
 * lanes are asserted SEPARATELY and with DIFFERENT values, because one shared
 * literal buffer would make both lanes carry whichever arrived last. */
CQ_TEST(the_fma_literal_lanes_carry_the_ieee_pattern_on_the_side_they_name)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const uint64_t THREE_HALVES = 0x400C000000000000ull;   /* 3.5 */
    const uint64_t SEVEN        = 0x401C000000000000ull;   /* 7.0 */
    const uint64_t TWO          = 0x4000000000000000ull;   /* 2.0 */
    int32_t one = cqrt_alloc_f64(f64_of(CQ_F64_ONE));
    int32_t two = cqrt_alloc_f64(f64_of(TWO));

    /* 3.5 IS 0x400C000000000000 AND NOT 3. If the literal arrived as the
     * integer 3 the result would be `fma(1, 3, 0)`'s bits, which is a
     * different number entirely. */
    CHECK_EQ(cq_pc_value(ctx, cq_template_fma_f64_qlq(one, f64_of(THREE_HALVES),
                                                      cqrt_alloc_f64(0.0))),
             cq_fma_eval(CQ_F64_ONE, THREE_HALVES, CQ_F64_POS_ZERO));

    /* THE TWO LANES ARE DISTINCT, AND THE DISCRIMINATOR HAD TO BE CHOSEN
     * RATHER THAN ASSUMED. `fma(1, b, c)` is `b + c`, which is SYMMETRIC in
     * the two literal lanes — so the first draft of this case swapped 3.5 and
     * 7 with `a = 1` and asserted a difference that does not exist. Caught by
     * execution. With `a = 2` the product lane is scaled and the two
     * orderings separate: `fma(2, 3.5, 7)` is 14 and `fma(2, 7, 3.5)` is
     * 17.5. The inequality is asserted FIRST, so a future edit that made them
     * agree again reddens the premise rather than silently making the two
     * value checks vacuous. */
    CHECK(cq_fma_eval(TWO, THREE_HALVES, SEVEN)
          != cq_fma_eval(TWO, SEVEN, THREE_HALVES));
    CHECK_EQ(cq_pc_value(ctx, cq_template_fma_f64_qll(two, f64_of(THREE_HALVES),
                                                      f64_of(SEVEN))),
             cq_fma_eval(TWO, THREE_HALVES, SEVEN));
    CHECK_EQ(cq_pc_value(ctx, cq_template_fma_f64_qll(two, f64_of(SEVEN),
                                                      f64_of(THREE_HALVES))),
             cq_fma_eval(TWO, SEVEN, THREE_HALVES));

    cq_reg_audit(ctx);
    close_with(&m);
}

/* ---- (3) A round trip on wires: the value, L2, and the pool. ------------ */

CQ_TEST(an_fma_f64_round_trip_on_wires_is_the_value_l2_and_the_pool)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const uint64_t a = FT_X, b = FT_X, c = FT_NEG_X2_RND;
    int32_t ha = qreg(ctx, (uint32_t)W64, a);
    int32_t hb = qreg(ctx, (uint32_t)W64, b);
    int32_t hc = qreg(ctx, (uint32_t)W64, c);
    uint32_t idx[64];
    cq_pc_snap before;
    int32_t out;

    before = cq_pc_take(ctx);
    out = cq_template_fma_f64(ha, hb, hc);
    CHECK_EQ(cq_pc_value(ctx, out), cq_fma_eval(a, b, c));
    /* THE SOURCES ARE UNCHANGED — Rule 7's contract, read at the ABI. */
    CHECK_EQ(cq_pc_value(ctx, ha), a);
    CHECK_EQ(cq_pc_value(ctx, hb), b);
    CHECK_EQ(cq_pc_value(ctx, hc), c);
    {
        const int32_t named[] = { ha, hb, hc, out };

        CHECK(cq_pc_live_is_exactly(ctx, named, 4));
    }

    cq_template_fma_f64_unc(out, ha, hb, hc);
    CHECK_EQ(cq_pc_value(ctx, out), 0u);
    {
        const int32_t named[] = { ha, hb, hc, out };

        CHECK(cq_pc_live_is_exactly(ctx, named, 4));
    }

    /* NAME THE INDICES BEFORE THE FREE — afterwards the rail is a tombstone —
     * and assert each is back on the free list. A COUNT cannot see a free that
     * released the wrong index. */
    for (int i = 0; i < W64; i++)
        idx[i] = cq_bit_qindex(cq_reg_cbits(&ctx->regs, out)[i]);
    cqrt_free(out);
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0u);
    CHECK(cq_pc_indices_are_free(ctx, idx, (uint32_t)W64));
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));

    cq_reg_audit(ctx);
    close_with(&m);
}

#include "test_template_fma_cert.inc"

CQ_TEST_MAIN(
    CQ_CASE(every_fma_f64_wrapper_agrees_with_its_kernels_own_eval),
    CQ_CASE(the_fma_literal_lanes_carry_the_ieee_pattern_on_the_side_they_name),
    CQ_CASE(an_fma_f64_round_trip_on_wires_is_the_value_l2_and_the_pool),
    CQ_CASE(an_fma_unc_whose_third_source_moved_is_refused_by_the_certificate),
    CQ_CASE(an_fma_unc_on_a_poisoned_source_is_discharged_by_the_certificate),
    CQ_CASE(an_fma_unc_does_not_pair_with_an_fmul_forward_on_the_same_handles),
    CQ_CASE(d7b_on_every_aliasing_shape_gives_the_unaliased_answer),
    CQ_CASE(the_fma_record_names_all_four_slots_and_pairs_on_them)
)
