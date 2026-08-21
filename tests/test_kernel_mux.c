/* tests/test_kernel_mux.c — M17, Step 14. K10 select.
 *
 * THE SUBJECT OF THIS FILE IS THE THIRD OPERAND. The four gates are Bennett's
 * and the sandwich is the driver's; what is new here is an arity the shared
 * sweep cannot drive, and it cannot drive it in a way that LOOKS FINE.
 *
 * cq_kd_case2 — which every sweep at W <= 8 goes through — fills `values[2]`
 * with ZERO (tests/support/kerneldrv.c). So a mux specified as (cond, t, f)
 * and swept the ordinary way runs every exhaustive-width case with `f = 0`,
 * and one specified as (t, f, cond) runs every one of them with `cond = 0` and
 * therefore never selects `t` at all. Either way the sweep is green, prints a
 * six-figure case count, and has tested half the kernel. The masks are not the
 * problem — those still vary, and `cond`'s KIND varies with q[0] bit 0, which
 * is what puts the classical-cond dispatch and the sandwich both under test.
 * The VALUES are the problem. So this file drives cq_kd_case directly with a
 * three-element value array and iterates `cond` explicitly.
 *
 * WHAT THAT BUYS, concretely: at W <= 5 every (cond, t, f) triple against every
 * fixed mask pair, which is exhaustive over the space and not over a slice of
 * it. bd -a0g's lesson one level up — a cheaper assertion hides an expensive
 * one; here a narrower sweep hides a wider one, and prints the same shape of
 * summary line either way.
 *
 * THE ARM SWAP IS THE OTHER THING NO COUNT CAN SEE. `mux(c, t, f)` and
 * `mux(c, f, t)` emit the identical tuple at every width and every mask —
 * K10's four gates are symmetric in `t` and `f` up to which one reaches `r`
 * first — so L4, the palindrome and the pool checks all agree. Only L1 against
 * a reference that was not derived from the kernel can tell them apart, which
 * is the K09 finding (`uge`-meaning-`ule`) in its K10 form.
 */

#include "kernels/mux.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "reg.h"
#include "sink_count.h"

#include "support/bitkinds.h"
#include "support/goldens.h"
#include "support/harness.h"
#include "support/kerneldrv.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"
#include "support/refmodel.h"

#include <stdio.h>

/* THREE SOURCES, AND THE CONDITION IS ONE BIT (bd ckd.15). `w_dst` equals the
 * arm width, so the refusal in shape_of that K9 tripped does not fire here —
 * but n_src is 3, which does, so a `call` adapter is mandatory and the driver
 * says so by name if it is ever dropped. */
static void mux_shape(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->n_src = 3;
    out->w[0]  = 1;          /* cond — `select`'s condition is i1 */
    out->w[1]  = W;          /* t */
    out->w[2]  = W;          /* f */
}

/* `sh->w[1]`, NOT `sh->w_dst`. They are equal for the mux, so this reads like
 * pedantry — it is not. Rule 7's `W` is the OPERAND width, and passing w_dst is
 * exactly the mistake that would have run every compare at W=1 (bd zwh). The
 * two widths agreeing today is a fact about K10, not about the driver. */
static void call_mux(cq_ctx *ctx, cq_bit *dst, const cq_bit *const *src,
                     const cq_kd_shape *sh)
{
    cq_kernel_mux(ctx, dst, src[0], src[1], src[2], sh->w[1]);
}

/* Plain selection, and deliberately NOT `f ^ (c & (t ^ f))` — that is the
 * kernel's own identity, and a reference that shared it could not disagree
 * with a kernel that had the identity right and the wiring wrong. */
static cq_ref_w ref_mux(const cq_ref_w *s, const cq_kd_shape *sh)
{
    return cq_ref_w_bit(s[0], 0) ? cq_ref_w_make(s[1].lo, s[1].hi, sh->w[1])
                                 : cq_ref_w_make(s[2].lo, s[2].hi, sh->w[2]);
}

static const cq_kd_spec MUX = { "mux", NULL, NULL, mux_shape, call_mux, ref_mux };

/* ---- L1 + L2 + L3 + L5: the shared constant sample budget. -------------- */

/* THE THIRD OPERAND NO LONGER NEEDS A BESPOKE DRIVER, and that is a structural
 * change rather than a shortcut (2026-08-20).
 *
 * This file used to drive cq_kd_case directly, for the reason its header still
 * states at length: cq_kd_case2 fills `values[2]` with ZERO, so a mux swept
 * through it ran every exhaustive-width case with one arm pinned at 0 — green,
 * a six-figure case count, and half a kernel tested. THAT HAZARD IS GONE AT ITS
 * ROOT. cq_kd_sample_at generates a value PER OPERAND AT ITS OWN WIDTH from the
 * spec's shape, so `cond` gets its one bit and both arms get W, and cq_kd_case2
 * is not on the path at all. Verified by reading tests/support/kernelsweep.c,
 * not assumed.
 *
 * The old header paragraph is kept deliberately: the trap was real, it was
 * measured, and a future reader restoring a case2-based sweep here would
 * re-acquire it. What changed is the driver, not the danger.
 *
 * The budget is now cq_kd_samples() cases per width — a small constant, default
 * 32 — each drawing a mask pair and a value triple jointly from one seeded RNG,
 * with the all-classical pair (which IS L5), the all-quantum pair (which is what
 * L4 pins) and the four value corners taken first, inside the budget rather than
 * on top of it. Every width prints its count, its pool and its seed.
 *
 * WHAT THAT GAVE UP HERE: `cond` is no longer iterated explicitly over {0, 1} at
 * every mask — it is one drawn bit per case, so roughly half the cases select
 * each arm. Over 32 cases a width the probability that either arm is never
 * selected is ~5e-10, and the ARM SWAP the header calls the fault no count can
 * see is caught by any case that selects the arm the kernel wired wrong. */

CQ_TEST(k10_sweep_exhaustive_widths)
{
    for (int W = 1; W <= 5; W++) cq_kd_sample_at(&MUX, W);
}

/* Step 20 — the same four levels under PRD §9's four regions, plus §9's
 * gate-tuple transform at every shipped width. The sweep body is this suite's
 * OWN, at its cheap widths only: the promotion is per gate and width-
 * independent, so what the axis adds is its interaction with the §3 fold table.
 * Every shipped width is still covered by cq_kd_check_promotion, at two kernel
 * calls apiece. */
static void mux_narrow(void)
{
    for (int W = 1; W <= 4; W++) cq_kd_sample_at(&MUX, W);
}

CQ_TEST(controlled)
{
    uint64_t reached = 0u;

    /* K10 has THREE sources and a one-bit `cond`, so this is the only place the
     * promotion meets an operand narrower than the result. i80 is excluded, as
     * it is from the sweep: opcode_table.yaml:88 says i80 is never a
     * control-merged data value, which is what a select's arms are. */
    static const int widths[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64, 128 };

    cq_kd_for_each_region("mux", mux_narrow);

    for (size_t w = 0; w < sizeof widths / sizeof widths[0]; w++)
        reached += cq_kd_check_promotion(&MUX, widths[w]);
    /* NOT VACUOUS: the identity above is an equality between two measurements,
     * and it holds trivially where the uncontrolled tuple is empty. K4 makes
     * that a real case rather than a hypothetical — its all-ones L4 fixture
     * saturates under D8 at every non-power-of-two width — so the ladder has to
     * say it reached something. */
    CHECK(reached > 0u);
}

/* i80 IS EXCLUDED ON PURPOSE and i128 is not: opcode_table.yaml:88 says i80 "is
 * NEVER a control-merged data value", which is precisely what a select's arms
 * are, while i128 is a shipped mux width and is swept here. Every width on this
 * ladder gets the same constant budget the narrow ones do — cq_kd_samples()
 * cases, printed with its seed — even though a mux case at W=128 emits 2304
 * gates over 256 scratch qubits, because the budget is a count and not a
 * product. The L4 goldens below pin 128 as a single measurement. */
CQ_TEST(k10_sweep_wide_widths)
{
    static const int WS[] = { 8, 16, 32, 64, 128 };

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++)
        cq_kd_sample_at(&MUX, WS[i]);
}

/* ---- L4: the goldens, at the all-quantum mask. -------------------------- */

/* K10.md §3.1. The compute half is Bennett's single loop — 3 CNOT and 1
 * Toffoli per bit, no NOT anywhere in `lower_mux!` — and the sandwich is two
 * copies of it around a W-CNOT copy-out:
 *
 *      compute   0 X   3W CX   W CCX          n_compute = 4W
 *      total     0 X   7W CX   2W CCX
 *
 * UNAFFECTED BY I6(b), unlike the barrel next door. bd 84m is about scratch
 * bits that are read as controls before anything writes them; K10 has none —
 * every one of `r` and `d` is written before it is read — so pre-materialising
 * the region changes which QUBITS are taken and not which gates are emitted.
 * The barrel's shl/lshr stages are where the elision died. */
static void closed_form(int W, uint64_t *x, uint64_t *cx, uint64_t *ccx)
{
    *x   = 0u;
    *cx  = (uint64_t)(7 * W);
    *ccx = (uint64_t)(2 * W);
}

CQ_TEST(l4_goldens)
{
    static const int WS[] = { 1, 2, 4, 8, 16, 32, 64, 128 };
    cq_gold g;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/mux.counts",
                      "M17 kernels/mux.c — K10 select (sandwiched, all-quantum "
                      "operands including cond)", CQOPS_BENNETT_COMMIT))
        return;

    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
        cq_counter fwd, unc;
        uint64_t x, cx, ccx;
        int W = WS[j];

        closed_form(W, &x, &cx, &ccx);
        cq_kd_measure(&MUX, W, &fwd, &unc);

        CHECK_GATES(fwd.x, fwd.cx, fwd.ccx, x, cx, ccx);
        CHECK_GATES(unc.x, unc.cx, unc.ccx, x, cx, ccx);
        CHECK_EQ(fwd.ry + fwd.rz + fwd.mz, 0);
        CHECK_EQ(unc.ry + unc.rz + unc.mz, 0);

        cq_gold_check(&g, "mux", "forward", W, fwd.x, fwd.cx, fwd.ccx);
        cq_gold_check(&g, "mux", "unc",     W, unc.x, unc.cx, unc.ccx);
    }

    CHECK(cq_gold_close(&g));
}

/* K10.md §3.1's evaluated table, transcribed as literals. The goldens above
 * are generated from `closed_form`, so on their own they would pin whatever it
 * happens to compute. These are the numbers the DOCUMENT claims. */
CQ_TEST(the_evaluated_table_in_k10_matches_what_is_emitted)
{
    static const struct { int W; uint64_t cx, ccx; } ROWS[] = {
        {  1,   7u,   2u }, {  8,  56u,  16u }, { 16, 112u,  32u },
        { 32, 224u,  64u }, { 64, 448u, 128u }
    };

    for (size_t i = 0; i < sizeof ROWS / sizeof ROWS[0]; i++) {
        cq_counter fwd, unc;
        uint64_t x, cx, ccx;

        closed_form(ROWS[i].W, &x, &cx, &ccx);
        CHECK_GATES(x, cx, ccx, 0u, ROWS[i].cx, ROWS[i].ccx);

        cq_kd_measure(&MUX, ROWS[i].W, &fwd, &unc);
        CHECK_GATES(fwd.x, fwd.cx, fwd.ccx, 0u, ROWS[i].cx, ROWS[i].ccx);
        CHECK_GATES(unc.x, unc.cx, unc.ccx, 0u, ROWS[i].cx, ROWS[i].ccx);
    }
}

/* ---- The stream, and the scratch. --------------------------------------- */

/* Runs one explicit case into a mock and returns the recorded stream. */
/* `cond` is ALWAYS quantum here, and the parameter that used to say so is gone:
 * a classical cond takes the entry dispatch, which is not a sandwich and has no
 * palindrome. Its own assertions live in the dispatch .inc. */
static void run_masked(int W, cq_ref_w vt, cq_ref_w vf,
                       cq_mock *fwd, cq_mock *unc)
{
    cq_ctx ctx;
    cq_sink s_fwd = cq_mock_sink(fwd);
    cq_sink s_unc = cq_mock_sink(unc);
    cq_ref_w all = cq_ref_w_ones(W);
    cq_ref_w one = cq_ref_w_setbit(0);

    cq_mock_reset(unc);
    cq_ctx_init(&ctx, &s_fwd);

    int32_t hc = cq_bk_reg_w(&ctx, 1u, one, one);
    int32_t ht = cq_bk_reg_w(&ctx, (uint32_t)W, vt, all);
    int32_t hf = cq_bk_reg_w(&ctx, (uint32_t)W, vf, all);
    int32_t hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W);

    /* Building the operands materialises their set bits, which emits X. Drop
     * that, so what is recorded is the KERNEL. */
    cq_mock_reset(fwd);
    cq_kernel_mux(&ctx, cq_reg_bits(&ctx.regs, hd), cq_reg_cbits(&ctx.regs, hc),
                  cq_reg_cbits(&ctx.regs, ht), cq_reg_cbits(&ctx.regs, hf), W);

    /* cond is 1 in both runs, so the answer is `t` whichever path was taken. */
    if (!cq_ref_w_eq(cq_pc_value_w(&ctx, hd), cq_ref_w_make(vt.lo, vt.hi, W)))
        cq_h_fail(__FILE__, __LINE__, "mux W=%d: dst is not t", W);

    ctx.sink = &s_unc;
    cq_kernel_mux(&ctx, cq_reg_bits(&ctx.regs, hd), cq_reg_cbits(&ctx.regs, hc),
                  cq_reg_cbits(&ctx.regs, ht), cq_reg_cbits(&ctx.regs, hf), W);

    if (!cq_ref_w_is_zero(cq_pc_value_w(&ctx, hd)))
        cq_h_fail(__FILE__, __LINE__, "mux W=%d: dst is not 0 after uncompute", W);

    cq_ctx_dispose(&ctx);
}

/* [compute half] [copy-out] [compute half reversed], head = 4W, mid = W.
 * A gate COUNT cannot see a divergence here — R8's measured witness has a
 * different multiset with an identical total — so the ordered check is the only
 * one with teeth, and the only detector that survives Step 19 (PRD §10). */
CQ_TEST(the_stream_is_a_palindrome_around_the_copyout)
{
    static const int WS[] = { 1, 2, 3, 5, 8 };
    cq_mock fwd, unc;

    cq_mock_init(&fwd);
    cq_mock_init(&unc);

    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
        int W = WS[j];

        run_masked(W, cq_ref_w_make(0x5Au, 0u, W),
                   cq_ref_w_make(0xA3u, 0u, W), &fwd, &unc);

        CHECK(cq_mock_is_palindrome(&fwd, (size_t)(4 * W), (size_t)W));
        CHECK(cq_mock_is_palindrome(&unc, (size_t)(4 * W), (size_t)W));
    }

    cq_mock_dispose(&fwd);
    cq_mock_dispose(&unc);
}

/* K10.md §4: 2W scratch slots, and under I6(b) all 2W become qubits whatever
 * the operand mask is. `owned` is W and `peak` is 3W; they differ, and for a
 * sandwich kernel they must — L2 looks after the call and cannot see scratch
 * that was taken and tidily released. */
CQ_TEST(the_scratch_is_2w_and_it_all_comes_back)
{
    static const int WS[] = { 1, 2, 4, 8, 16, 32, 64, 128 };

    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
        uint32_t peak = 0;
        uint32_t owned = cq_kd_peak(&MUX, WS[j], &peak);

        CHECK_EQ(owned, (uint32_t)WS[j]);
        CHECK_EQ(peak, (uint32_t)(3 * WS[j]));
    }
}

#include "test_kernel_mux_dispatch.inc"

CQ_TEST_MAIN_ARGV(
    CQ_CASE(k10_sweep_exhaustive_widths),
    CQ_CASE(controlled),
    CQ_CASE(k10_sweep_wide_widths),
    CQ_CASE(l4_goldens),
    CQ_CASE(the_evaluated_table_in_k10_matches_what_is_emitted),
    CQ_CASE(the_stream_is_a_palindrome_around_the_copyout),
    CQ_CASE(the_scratch_is_2w_and_it_all_comes_back),
    CQ_CASE(a_classical_cond_never_enters_the_sandwich),
    CQ_CASE(a_classical_cond_still_writes_a_real_gate_into_a_quantum_dst),
    CQ_CASE(r9_all_classical_operands_cost_nothing_at_all),
    CQ_CASE(a_one_bit_cond_adjacent_to_an_arm_is_accepted),
    CQ_CASE(a_quantum_cond_with_classical_arms_is_a_shape_no_sweep_reaches),
    CQ_CASE(the_arms_are_not_interchangeable),
    CQ_CASE(the_condition_is_read_from_bit_zero_only)
)
