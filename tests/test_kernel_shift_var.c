/* tests/test_kernel_shift_var.c — M12, Step 14. The barrel shifter.
 *
 * THE SUBJECT OF THIS FILE IS D8 AGREEMENT ACROSS TWO MODULES. M11 landed at
 * Step 11 implementing `dst ^= sat_shift(a, k mod 2^S)` by reading the low S
 * bits of the amount and then saturating; M12 implements the same formula by
 * running L mux stages that each zero-fill or sign-fill what they shift in.
 * Nothing in either module's own suite can compare them — each checks itself
 * against a reference that applies the same reduction — so the cross-check
 * below is the assertion bd -mf4 filed this step for, and it is the only place
 * the two implementations ever meet.
 *
 * IT IS NOT HYPOTHETICAL. bd ckd.16 recorded three factual claims about these
 * two paths and all three were wrong, in both directions: the variable path
 * DOES saturate (each stage's `shifted` is a fresh all-zero vector and the
 * out-of-range copy is simply not emitted), the conflict is a POWER-OF-TWO
 * artefact so i80 is the width where the paths agree furthest rather than the
 * deciding case, and the "free" `k &= W-1` fix is exact only when W is a power
 * of two. The corners that pin all of that are at the bottom of this file.
 *
 * THE OTHER SUBJECT IS bd 84m. K10.md was written before ckd.9 decided
 * PRE-MATERIALISE, and its §3.2 claims the shl/lshr stages elide `2^L - 1`
 * CNOTs because the never-written `sh_k` bits "stay CQ_BIT_ZERO". Under I6(b)
 * they do not: the driver materialises the whole region at step 1, the fold
 * table dispatches on KIND and never on shadow value (D6), so those CNOTs are
 * emitted and the sandwiched golden is `2(2^L - 1)` CX HIGHER than the shipped
 * document says. That is 14 CX at W=8 and 254 at W=128, and it is a number the
 * suite measures rather than a claim it repeats.
 */

#include "kernels/shift_var.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/shift_const.h"
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

enum { DIR_SHL = 0, DIR_LSHR, DIR_ASHR, N_DIRS };

/* THE REDUCTION GOES THROUGH THE LIBRARY's cq_shift_stages, exactly as M11's
 * suite does it and for the same reason: D8's whole content is that ONE
 * reduction is shared by three implementations, and a reference that computed
 * its own ceil(log2 W) would turn the thing under test into the thing assumed. */
static int eff_k(const cq_ref_w *src, int W)
{
    int s = cq_shift_stages(W);

    return (int)(src[1].lo & ((s >= 64) ? ~(uint64_t)0
                                        : (((uint64_t)1 << s) - 1u)));
}

static cq_ref_w ref_shl (const cq_ref_w *s, const cq_kd_shape *sh)
{ return cq_ref_w_shl (s[0], eff_k(s, sh->w_dst), sh->w_dst); }
static cq_ref_w ref_lshr(const cq_ref_w *s, const cq_kd_shape *sh)
{ return cq_ref_w_lshr(s[0], eff_k(s, sh->w_dst), sh->w_dst); }
static cq_ref_w ref_ashr(const cq_ref_w *s, const cq_kd_shape *sh)
{ return cq_ref_w_ashr(s[0], eff_k(s, sh->w_dst), sh->w_dst); }

/* NO SHAPE FUNCTION, AND THAT IS THE HEADLINE DIFFERENCE FROM M11's SUITE.
 * K4 constrains operand 1 to be classical (`classical[1] = ~0`) because a
 * quantum amount is a hard error there. Here the quantum amount is the whole
 * point, so the driver hands these kernels every mask pair it has, and roughly
 * half of them leave the amount classical — which exercises the delegation to
 * M11 on the same cases that exercise the barrel on the other half. */
static const cq_kd_spec SPEC[N_DIRS] = {
    { "shl_var",  cq_kernel_shl_var,  NULL, NULL, NULL, ref_shl  },
    { "lshr_var", cq_kernel_lshr_var, NULL, NULL, NULL, ref_lshr },
    { "ashr_var", cq_kernel_ashr_var, NULL, NULL, NULL, ref_ashr }
};

/* The M11 counterpart of each row, for the cross-check. `cq_kernel_fn` rather
 * than a local re-typedef of the same five parameters — both modules' entry
 * points are Rule 7's canonical shape, which is what lets one loop drive them. */
static const cq_kernel_fn CONST_PATH[N_DIRS] = {
    cq_kernel_shl, cq_kernel_lshr, cq_kernel_ashr
};


/* ---- L4: the goldens, at the all-quantum mask. -------------------------- */

/* K10.md §3.2 RE-ISSUED UNDER I6(b) (bd 84m). Per stage k, s = 2^k:
 *
 *      INIT              W CX                              (once)
 *      SHIFT  shl/lshr   W - s CX      ashr  W CX
 *      MUX               3W CX + W CCX                     (K10 §3.1)
 *
 * and the MUX row is where the shipped document is wrong. It claims
 * `3W - s` CX for shl/lshr, because the `s` bits of `sh_k` that receive no
 * source "stay CQ_BIT_ZERO" and CX from a ZERO control emits nothing. Under
 * I6(b) the driver pre-materialises the WHOLE region at step 1, so those bits
 * are CQ_BIT_Q holding |0> and the fold — which reads kind, never shadow —
 * does not fire. Summing with sum(2^k, k<L) = 2^L - 1:
 *
 *      shl/lshr   compute  4WL + W - 2^L + 1 CX,  WL CCX
 *      ashr       compute  4WL + W            CX,  WL CCX
 *
 * sandwiched = 2 x compute + the W-CNOT copy-out. That leaves libcqops and a
 * raw Bennett gate count IDENTICAL for the barrel, which they were not before:
 * K10.md §3.2's own "raw-Bennett comparison" line gives `4WL + W - 2^L + 1`
 * for the shl/lshr compute half, and that is now our number too.
 *
 * W = 1 IS THE DELEGATED ROW. L is 0, no bit of the amount is read, the
 * barrel never runs and M11 answers with the identity — W CX, no scratch. The
 * closed form above would say 3 CX; the kernel says 1, and the kernel is
 * right. */
static void closed_form(int dir, int W, uint64_t *x, uint64_t *cx,
                        uint64_t *ccx)
{
    long long L = cq_shift_stages(W);
    long long compute;

    *x = 0u;

    if (L == 0) {                       /* delegated to M11: dst ^= a */
        *cx  = (uint64_t)W;
        *ccx = 0u;
        return;
    }

    compute = 4LL * W * L + W;
    if (dir != DIR_ASHR) compute -= (1LL << L) - 1LL;

    *cx  = (uint64_t)(2LL * compute + W);
    *ccx = (uint64_t)(2LL * W * L);
}

static int golden_widths(int dir, const int **out)
{
    static const int ALL[]     = { 1, 2, 3, 4, 5, 8, 16, 32, 64, 80, 128 };
    static const int NO_I80[]  = { 1, 2, 3, 4, 5, 8, 16, 32, 64, 128 };

    *out = dir == DIR_ASHR ? NO_I80 : ALL;
    return dir == DIR_ASHR ? (int)(sizeof NO_I80 / sizeof NO_I80[0])
                           : (int)(sizeof ALL / sizeof ALL[0]);
}

CQ_TEST(l4_goldens)
{
    cq_gold g;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/shift_var.counts",
                      "M12 kernels/shift_var.c — the barrel shifter "
                      "(sandwiched, all-quantum value AND amount)",
                      CQOPS_BENNETT_COMMIT))
        return;

    for (int dir = 0; dir < N_DIRS; dir++) {
        const int *ws;
        int nw = golden_widths(dir, &ws);

        for (int j = 0; j < nw; j++) {
            cq_counter fwd, unc;
            uint64_t x, cx, ccx;

            closed_form(dir, ws[j], &x, &cx, &ccx);
            cq_kd_measure(&SPEC[dir], ws[j], &fwd, &unc);

            CHECK_GATES(fwd.x, fwd.cx, fwd.ccx, x, cx, ccx);
            CHECK_GATES(unc.x, unc.cx, unc.ccx, x, cx, ccx);
            CHECK_EQ(fwd.ry + fwd.rz + fwd.mz, 0);
            CHECK_EQ(unc.ry + unc.rz + unc.mz, 0);

            cq_gold_check(&g, SPEC[dir].name, "forward", ws[j],
                          fwd.x, fwd.cx, fwd.ccx);
            cq_gold_check(&g, SPEC[dir].name, "unc", ws[j],
                          unc.x, unc.cx, unc.ccx);
        }
    }

    CHECK(cq_gold_close(&g));
}

/* THE W=8 COLUMN AS LITERALS, AND IT IS THE bd 84m WITNESS. K10.md §3.2 ships
 * 0 / 188 / 48 for shl and lshr; under I6(b) it is 0 / 202 / 48, higher by
 * exactly 2(2^L - 1) = 14. ashr is unchanged at 0 / 216 / 48 because its
 * else-branch writes every `sh_k` bit, so it never had an elision to lose. */
CQ_TEST(the_w8_column_is_k10s_table_corrected_for_i6b)
{
    static const uint64_t W8[N_DIRS][2] = {
        { 202u, 48u },      /* shl  */
        { 202u, 48u },      /* lshr */
        { 216u, 48u }       /* ashr */
    };
    uint64_t stale_shl_lshr = 188u;

    for (int dir = 0; dir < N_DIRS; dir++) {
        cq_counter fwd, unc;
        uint64_t x, cx, ccx;

        closed_form(dir, 8, &x, &cx, &ccx);
        CHECK_GATES(x, cx, ccx, 0u, W8[dir][0], W8[dir][1]);

        cq_kd_measure(&SPEC[dir], 8, &fwd, &unc);
        CHECK_GATES(fwd.x, fwd.cx, fwd.ccx, 0u, W8[dir][0], W8[dir][1]);
        CHECK_GATES(unc.x, unc.cx, unc.ccx, 0u, W8[dir][0], W8[dir][1]);
    }

    /* Stated as an inequality so the correction cannot be quietly undone. */
    CHECK(W8[DIR_SHL][0] == stale_shl_lshr + 2u * ((1u << 3) - 1u));
}

/* ---- The stream, and the scratch. --------------------------------------- */

static int n_compute_of(int dir, int W)
{
    int L = cq_shift_stages(W);
    int n = W;

    for (int k = 0; k < L; k++)
        n += (dir == DIR_ASHR ? W : W - (1 << k)) + 4 * W;
    return n;
}

static uint32_t scratch_of(int W)
{
    return (uint32_t)(W * (3 * cq_shift_stages(W) + 1));
}

/* One run with an explicit amount mask, recorded into a mock. */
/* The amount is ALWAYS quantum here, and the parameter that used to say so is
 * gone. A classical amount delegates to M11, which is not a sandwich and has no
 * palindrome to check — so the only value this helper was ever called with was
 * the only one it can be called with. */
static void run_masked(int dir, int W, cq_ref_w va, cq_ref_w vk,
                       cq_mock *fwd, cq_mock *unc)
{
    cq_ctx ctx;
    cq_sink s_fwd = cq_mock_sink(fwd);
    cq_sink s_unc = cq_mock_sink(unc);
    cq_ref_w all = cq_ref_w_ones(W);

    cq_mock_reset(unc);
    cq_ctx_init(&ctx, &s_fwd);

    int32_t ha = cq_bk_reg_w(&ctx, (uint32_t)W, va, all);
    int32_t hb = cq_bk_reg_w(&ctx, (uint32_t)W, vk, all);
    int32_t hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W);

    cq_mock_reset(fwd);
    SPEC[dir].kernel(&ctx, cq_reg_bits(&ctx.regs, hd),
                     cq_reg_cbits(&ctx.regs, ha),
                     cq_reg_cbits(&ctx.regs, hb), W);

    ctx.sink = &s_unc;
    SPEC[dir].kernel(&ctx, cq_reg_bits(&ctx.regs, hd),
                     cq_reg_cbits(&ctx.regs, ha),
                     cq_reg_cbits(&ctx.regs, hb), W);

    if (!cq_ref_w_is_zero(cq_pc_value_w(&ctx, hd)))
        cq_h_fail(__FILE__, __LINE__,
                  "%s W=%d: dst is not 0 after uncompute", SPEC[dir].name, W);

    cq_ctx_dispose(&ctx);
}

CQ_TEST(the_stream_is_a_palindrome_around_the_copyout)
{
    static const int WS[] = { 2, 3, 5, 8, 16 };
    cq_mock fwd, unc;

    cq_mock_init(&fwd);
    cq_mock_init(&unc);

    for (int dir = 0; dir < N_DIRS; dir++)
        for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
            int W = WS[j];

            run_masked(dir, W, cq_ref_w_make(0x5Au, 0u, W),
                       cq_ref_w_make(0x03u, 0u, W), &fwd, &unc);

            CHECK(cq_mock_is_palindrome(&fwd, (size_t)n_compute_of(dir, W),
                                        (size_t)W));
            CHECK(cq_mock_is_palindrome(&unc, (size_t)n_compute_of(dir, W),
                                        (size_t)W));
        }

    cq_mock_dispose(&fwd);
    cq_mock_dispose(&unc);
}

/* K10.md §4, RE-ISSUED: W(3L+1) slots, and under I6(b) every one of them
 * becomes a qubit for all three directions. The shipped table gives shl/lshr
 * `3WL + W - 2^L + 1` — 73 at W=8 against the region's 80 — on the same
 * pre-materialisation premise its gate counts rested on. */
CQ_TEST(the_scratch_is_the_whole_region_for_every_direction)
{
    /* Up to 64, where the region is 1216 qubits — the widest the goldens pin is
     * 128, but a peak check is ONE call per width, so there is no reason for it
     * to stop as far below the golden ladder as it did. */
    static const int WS[] = { 2, 4, 5, 8, 16, 32, 64 };

    for (int dir = 0; dir < N_DIRS; dir++)
        for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
            uint32_t peak = 0;
            uint32_t owned = cq_kd_peak(&SPEC[dir], WS[j], &peak);

            CHECK_EQ(owned, (uint32_t)WS[j]);
            CHECK_EQ(peak, scratch_of(WS[j]) + (uint32_t)WS[j]);
        }

    /* The stale figure, named so it cannot come back: 73 was the shl/lshr
     * qubit count K10.md §4 tabulates at W=8, and the region is 80. */
    CHECK_EQ(scratch_of(8), 80u);
}

#include "test_kernel_shift_var_sweep.inc"
#include "test_kernel_shift_var_d8.inc"

CQ_TEST_MAIN_ARGV(
    CQ_CASE(k10_barrel_shl_sweep),
    CQ_CASE(k10_barrel_lshr_sweep),
    CQ_CASE(k10_barrel_ashr_sweep),
    CQ_CASE(l4_goldens),
    CQ_CASE(the_w8_column_is_k10s_table_corrected_for_i6b),
    CQ_CASE(the_stream_is_a_palindrome_around_the_copyout),
    CQ_CASE(the_scratch_is_the_whole_region_for_every_direction),
    CQ_CASE(m11_and_m12_agree_at_every_width_amount_and_value),
    CQ_CASE(d8_the_barrel_saturates_without_being_asked_to),
    CQ_CASE(d8_a_shift_by_exactly_the_width_is_the_identity),
    CQ_CASE(d8_a_width_of_one_ignores_the_amount_entirely),
    CQ_CASE(a_quantum_bit_above_the_stages_does_not_force_the_barrel),
    CQ_CASE(r9_a_classical_amount_never_enters_the_sandwich)
)
