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

/* ---- L1 + L2 + L3 + L5: the sweep, with `cond` driven explicitly. -------- */

enum { MUX_MAX_PAIRS = 128 + 12 };

/* The same 1024/W law kernelsweep.c uses, and for its reason: the fixed mask
 * set grows linearly in W and a case costs O(W), so a flat sample count makes
 * the sweep quadratic. Duplicated rather than exported because samples_for is
 * kernelsweep.c's private tuning knob and this file's ladder is not its ladder. */
static int mux_samples(int W)
{
    int n = 1024 / W;

    if (n > 64) n = 64;
    if (n < 8)  n = 8;
    return n;
}

static void mux_case(int W, int cond, cq_ref_w vt, cq_ref_w vf,
                     const cq_bk_pair *m)
{
    cq_ref_w v[CQ_KD_MAX_SRC];

    v[0] = cq_ref_w_make((uint64_t)cond, 0u, 1);
    v[1] = cq_ref_w_make(vt.lo, vt.hi, W);
    v[2] = cq_ref_w_make(vf.lo, vf.hi, W);
    cq_kd_case(&MUX, W, v, m);
}

static void mux_full_cross(int W)
{
    cq_bk_pair pairs[MUX_MAX_PAIRS];
    uint32_t np = cq_bk_fixed_pairs((uint32_t)W, pairs, MUX_MAX_PAIRS);
    uint64_t span = cq_ref_mask(W) + 1u;      /* W <= 5 here, cannot wrap */
    uint64_t cases = 0;

    for (uint32_t p = 0; p < np; p++)
        for (int c = 0; c < 2; c++)
            for (uint64_t vt = 0; vt < span; vt++)
                for (uint64_t vf = 0; vf < span; vf++) {
                    mux_case(W, c, cq_ref_w_make(vt, 0u, W),
                             cq_ref_w_make(vf, 0u, W), &pairs[p]);
                    cases++;
                }

    printf("# mux W=%2d FULL CROSS: %u mask pairs x 2 conds x %llu arm pairs = "
           "%llu cases\n", W, np, (unsigned long long)(span * span),
           (unsigned long long)cases);
    fflush(stdout);
}

/* Corners plus a seeded tail, at both values of `cond`, crossed with every
 * fixed mask pair. Two-word throughout so the ladder can reach i128 without a
 * second code path at the 64-bit seam. */
static void mux_sampled(int W)
{
    cq_bk_pair pairs[MUX_MAX_PAIRS];
    uint32_t np = cq_bk_fixed_pairs((uint32_t)W, pairs, MUX_MAX_PAIRS);
    cq_ref_w corner[4];
    cq_bk_rng rng;
    uint64_t cases = 0;

    corner[0] = cq_ref_w_zero();
    corner[1] = cq_ref_w_ones(W);
    corner[2] = cq_ref_w_setbit(W - 1);       /* the sign lane */
    corner[3] = cq_ref_w_setbit(0);

    cq_bk_rng_init(&rng, 0x1D10Full ^ (uint64_t)W);

    for (uint32_t p = 0; p < np; p++)
        for (int c = 0; c < 2; c++) {
            /* Every ordered corner pair: t and f must differ for the selection
             * to be observable at all, and (ones, zero) is the pair where a
             * swapped arm is loudest. */
            for (int i = 0; i < 4; i++)
                for (int j = 0; j < 4; j++) {
                    mux_case(W, c, corner[i], corner[j], &pairs[p]);
                    cases++;
                }

            for (int s = 0; s < mux_samples(W); s++) {
                cq_ref_w vt = cq_ref_w_make(cq_bk_rng_next(&rng),
                                            cq_bk_rng_next(&rng), W);
                cq_ref_w vf = cq_ref_w_make(cq_bk_rng_next(&rng),
                                            cq_bk_rng_next(&rng), W);
                mux_case(W, c, vt, vf, &pairs[p]);
                cases++;
            }
        }

    printf("# mux W=%3d sampled: %u mask pairs x 2 conds x (16 corner pairs + "
           "%d sampled) = %llu cases (seed 0x1D10F^W)\n",
           W, np, mux_samples(W), (unsigned long long)cases);
    fflush(stdout);
}

CQ_TEST(k10_sweep_exhaustive_widths)
{
    for (int W = 1; W <= 5; W++) mux_full_cross(W);
}

/* Step 20 — the same four levels under PRD §9's four regions, plus §9's
 * gate-tuple transform at every shipped width. The sweep body is this suite's
 * OWN, at its cheap widths only: the promotion is per gate and width-
 * independent, so what the axis adds is its interaction with the §3 fold table,
 * which is exhausted where the value cross product is. Every shipped width is
 * still covered by cq_kd_check_promotion, at two kernel calls apiece. */
static void mux_narrow(void)
{
    /* mux_full_cross, NOT cq_kd_sweep_at: cq_kd_case2 fills values[2] with
     * ZERO, so the ordinary sweep would run every exhaustive case with one arm
     * pinned at 0 — green, and half a kernel. The region hook takes this
     * suite's own body for exactly that reason. */
    for (int W = 1; W <= 4; W++) mux_full_cross(W);
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

/* THE LADDER STOPS AT 64 FOR THE SWEEP AND THE CAP IS DELIBERATE, so it is
 * printed rather than implied. i80 is excluded on purpose — opcode_table.yaml
 * :88 says i80 "is NEVER a control-merged data value", which is precisely what
 * a select's arms are — and i128 is swept here but at the same reduced sample
 * depth every wide width gets, because a mux case at W=128 emits 2304 gates
 * and takes 256 scratch qubits. The L4 goldens below DO pin 128. */
CQ_TEST(k10_sweep_wide_widths)
{
    mux_sampled(8);
    mux_sampled(16);
    mux_sampled(32);
    mux_sampled(64);
    mux_sampled(128);
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
