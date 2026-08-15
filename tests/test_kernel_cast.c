/* tests/test_kernel_cast.c — M13, Step 11. K5 sext / zext / trunc.
 *
 * THE FIRST KERNEL THAT DOES NOT FIT THE RULE 7 SHAPE, and the first whose
 * values do not fit a uint64_t. A cast is unary with TWO widths, so it reaches
 * the shared driver through a call adapter and a shape function rather than
 * through cq_kernel_fn; and casts are the ONLY way an i128 register exists at
 * all — no cqrt_alloc_i128, no cqrt_measure_i128 — so 25 of the 55 cast pairs
 * CQ_lang ships involve i80 or i128 and a one-word reference could check 30 of
 * them. Both generalisations landed in the driver rather than being forked
 * here, which is what Steps 12-17 inherit.
 *
 * WIDTH PAIRS ARE THE CASE SPACE, so this file drives cq_kd_sweep_at per pair
 * instead of cq_kd_sweep's standard ladder. That is the split kerneldrv.c and
 * kernelsweep.c were separated for, and it needed no change to any assertion.
 */

#include "kernels/cast.h"

#include "bit.h"
#include "ctx.h"
#include "reg.h"
#include "sink_count.h"

#include "support/bitkinds.h"
#include "support/goldens.h"
#include "support/harness.h"
#include "support/kerneldrv.h"
#include "support/poolcheck.h"
#include "support/refmodel.h"

#include <stdio.h>

/* The width pair under test. A cast's shape depends on (F,T), which the driver
 * hands in as a single W — so the pair is carried here and the shape function
 * reads it. Set before each sweep; single-threaded by construction, like the
 * harness's own current-case pointer. */
static int g_F, g_T;

static void cast_shape(int W, cq_kd_shape *out)
{
    (void)W;
    cq_kd_default_shape(g_F, out);
    out->n_src = 1;
    out->w[0]  = g_F;
    out->w_dst = g_T;
}

static void call_zext(cq_ctx *ctx, cq_bit *dst, const cq_bit *const *src,
                      const cq_kd_shape *sh)
{
    cq_kernel_zext(ctx, dst, src[0], sh->w[0], sh->w_dst);
}
static void call_sext(cq_ctx *ctx, cq_bit *dst, const cq_bit *const *src,
                      const cq_kd_shape *sh)
{
    cq_kernel_sext(ctx, dst, src[0], sh->w[0], sh->w_dst);
}
static void call_trunc(cq_ctx *ctx, cq_bit *dst, const cq_bit *const *src,
                       const cq_kd_shape *sh)
{
    cq_kernel_trunc(ctx, dst, src[0], sh->w[0], sh->w_dst);
}

static cq_ref_w ref_zext(const cq_ref_w *s, const cq_kd_shape *sh)
{ return cq_ref_w_zext(s[0], sh->w[0], sh->w_dst); }
static cq_ref_w ref_sext(const cq_ref_w *s, const cq_kd_shape *sh)
{ return cq_ref_w_sext(s[0], sh->w[0], sh->w_dst); }
static cq_ref_w ref_trunc(const cq_ref_w *s, const cq_kd_shape *sh)
{ return cq_ref_w_trunc(s[0], sh->w[0], sh->w_dst); }

static const cq_kd_spec ZEXT  = { "zext",  NULL, NULL, cast_shape, call_zext,  ref_zext  };
static const cq_kd_spec SEXT  = { "sext",  NULL, NULL, cast_shape, call_sext,  ref_sext  };
static const cq_kd_spec TRUNC = { "trunc", NULL, NULL, cast_shape, call_trunc, ref_trunc };

/* CQ_lang's shipped width ladder. i80 has no ashr but does have casts, and
 * i128 exists ONLY through these — see cast.h. */
static const int LADDER[] = { 1, 8, 16, 32, 64, 80, 128 };
enum { N_LADDER = 7 };

/* ---- L1 + L2 + L3 + L5, over the width PAIRS. --------------------------- */

/* Every widening pair (F < T) and every narrowing pair (T < F), plus the
 * degenerate T == F, which the construction accepts as an identity copy
 * (cast.c records that as a decision rather than an accident). At the small
 * widths the sweep is exhaustive; above 8 it samples — and above 64 the driver
 * carries the values in two words, which is the whole reason it was
 * generalised. */
static void sweep_pairs(const cq_kd_spec *k, int widening)
{
    int pairs = 0;

    for (int i = 0; i < N_LADDER; i++)
        for (int j = 0; j < N_LADDER; j++) {
            int F = LADDER[i], T = LADDER[j];

            if (widening ? (T < F) : (T > F)) continue;

            g_F = F; g_T = T;
            /* The driver's `W` is the source width — the shape overrides both
             * anyway; passing F keeps the sweep's own value spans right. */
            cq_kd_sweep_at(k, F, F <= 8);
            pairs++;
        }

    printf("# %s: %d width pairs swept over the shipped ladder\n", k->name, pairs);
    fflush(stdout);
}

CQ_TEST(k5_zext_sweep)  { sweep_pairs(&ZEXT,  1); }
CQ_TEST(k5_sext_sweep)  { sweep_pairs(&SEXT,  1); }
CQ_TEST(k5_trunc_sweep) { sweep_pairs(&TRUNC, 0); }

/* ---- The wide pairs, by hand, where the reference earns its keep. ------- */

/* Runs one cast at one value and returns dst's two-word value. */
static cq_ref_w run_cast_full(const cq_kd_spec *k, int F, int T, cq_ref_w a,
                              cq_counter *fwd, cq_counter *unc)
{
    cq_ctx ctx; cq_counter cnt; cq_sink sink;
    cq_kd_shape sh;

    cq_count_reset(&cnt);
    sink = cq_sink_counter(&cnt);
    cq_ctx_init(&ctx, &sink);

    g_F = F; g_T = T;
    cast_shape(F, &sh);

    int32_t ha = cq_bk_reg_w(&ctx, (uint32_t)F, a, cq_ref_w_ones(F));
    int32_t hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)T);
    const cq_bit *src[1] = { cq_reg_cbits(&ctx.regs, ha) };

    cq_count_reset(&cnt);
    k->call(&ctx, cq_reg_bits(&ctx.regs, hd), src, &sh);
    if (fwd) *fwd = cnt;

    cq_ref_w v = cq_pc_value_w(&ctx, hd);

    /* The UNCOMPUTE pass, measured SEPARATELY — Rule 14 forbids asserting that
     * it equals the forward, and pinning only one of the two was how M13's
     * first draft left half of L4 unmeasured. */
    if (unc) {
        cq_count_reset(&cnt);
        k->call(&ctx, cq_reg_bits(&ctx.regs, hd), src, &sh);
        *unc = cnt;
    }

    cq_ctx_dispose(&ctx);
    return v;
}

/* The value-only form the by-hand cases use. */
static cq_ref_w run_cast(const cq_kd_spec *k, int F, int T, cq_ref_w a,
                         uint32_t *cx_out)
{
    cq_counter fwd;
    cq_ref_w v = run_cast_full(k, F, T, a, &fwd, NULL);
    if (cx_out) *cx_out = (uint32_t)fwd.cx;
    return v;
}

/* THE CASE THE ONE-WORD REFERENCE COULD NOT HAVE CHECKED. A sign extension
 * from i64 to i128 has to fill 64 bits ABOVE the low word — the exact place a
 * shift-based reference and a shift-based kernel would agree while both were
 * wrong. Stated by hand rather than taken from the model. */
CQ_TEST(wide_sext_fills_the_high_word)
{
    cq_ref_w neg = cq_ref_w_make(~(uint64_t)0, 0u, 64);      /* -1 at i64 */
    cq_ref_w pos = cq_ref_w_make(0x7FFFFFFFFFFFFFFFull, 0u, 64);
    cq_ref_w r;

    r = run_cast(&SEXT, 64, 128, neg, NULL);
    CHECK_EQ(r.lo, ~(uint64_t)0);
    CHECK_EQ(r.hi, ~(uint64_t)0);            /* every high bit filled */

    r = run_cast(&SEXT, 64, 128, pos, NULL);
    CHECK_EQ(r.lo, 0x7FFFFFFFFFFFFFFFull);
    CHECK_EQ(r.hi, 0u);

    /* zext must NOT fill — and must not allocate for the high word either. */
    r = run_cast(&ZEXT, 64, 128, neg, NULL);
    CHECK_EQ(r.lo, ~(uint64_t)0);
    CHECK_EQ(r.hi, 0u);
}

/* i80 is the width where the 64-bit seam falls INSIDE the register, which is
 * where an off-by-one in a two-word reference lives. Bit 79 is the sign. */
CQ_TEST(wide_i80_crosses_the_word_seam)
{
    cq_ref_w v = cq_ref_w_make(0u, (uint64_t)1 << 15, 80);   /* bit 79 set */
    cq_ref_w r;

    CHECK_EQ(cq_ref_w_bit(v, 79), 1);
    CHECK_EQ(cq_ref_w_bit(v, 78), 0);

    r = run_cast(&SEXT, 80, 128, v, NULL);
    /* NOT all-ones, and the first draft of this line said it was. The sign bit
     * is 79, so the high word holds bits 64..78 of the SOURCE (all clear here)
     * and only bits 80..127 are filled — 0xFFFF...8000, with bit 15 being the
     * sign bit itself. A high word of ~0 would mean the fill had started 15
     * bits too low, which is precisely the off-by-one that lives at the seam. */
    CHECK_EQ(r.hi, ~(uint64_t)0 << 15);
    CHECK_EQ(r.lo, 0u);

    r = run_cast(&TRUNC, 128, 80, cq_ref_w_make(~0ull, ~0ull, 128), NULL);
    CHECK_EQ(r.lo, ~(uint64_t)0);
    CHECK_EQ(r.hi, ((uint64_t)1 << 16) - 1u);  /* exactly 16 bits of the high word */
}

/* AN INDEPENDENT ORACLE FOR sext, which is the one cast whose reference shares
 * its shape with the kernel. cq_ref_w_sext is a bit loop with the same bounds
 * and the same sign index F-1 as cq_kernel_sext — so an error made once and
 * repeated in the reference is invisible to L1, which is exactly what
 * refmodel.h's own "a reference derived from the kernel proves nothing" warns
 * against. MEASURED: a mutant reading the wrong sign bit passed all eight
 * cases in both configurations before this went in.
 *
 * cq_ref_sext (refmodel.c) is genuinely different arithmetic — the shift-free
 * identity (t ^ sign) - sign, no loop and no bit index — and it was dead code
 * with no caller anywhere. This is its caller. */
CQ_TEST(sext_against_an_independently_derived_oracle)
{
    static const int FS[] = { 1, 8, 16, 32 };

    for (size_t i = 0; i < sizeof FS / sizeof FS[0]; i++) {
        int F = FS[i];
        uint64_t hi_val = cq_ref_mask(F);

        for (uint64_t v = 0; v <= hi_val && v < 512u; v++) {
            /* Sign-extend F -> 64 two ways: the kernel, and the identity. */
            cq_ref_w got = run_cast(&SEXT, F, 64,
                                    cq_ref_w_make(v, 0u, F), NULL);
            uint64_t want = (uint64_t)cq_ref_sext(v, F);

            if (got.lo != want || got.hi != 0u)
                cq_h_fail(__FILE__, __LINE__,
                          "sext F=%d v=0x%llx: kernel gave 0x%llx%016llx, the "
                          "(t^sign)-sign identity gives 0x%llx", F,
                          (unsigned long long)v, (unsigned long long)got.hi,
                          (unsigned long long)got.lo,
                          (unsigned long long)want);
        }
    }
}

/* The two value models must also agree on masking wherever both are defined —
 * otherwise the wide half could drift and nothing below 64 would notice. */
CQ_TEST(the_two_reference_models_agree_below_64)
{
    for (int W = 1; W <= 64; W *= 2) {
        for (uint64_t v = 0; v < 8; v++) {
            uint64_t x = v * 0x0123456789ABCDEFull;
            cq_ref_w w = cq_ref_w_make(x, 0u, W);

            CHECK_EQ(w.lo, cq_ref_trunc(x, W));
            CHECK_EQ(cq_ref_w_trunc(w, W, W).lo, cq_ref_trunc(x, W));
            CHECK_EQ(w.hi, 0);

            /* And sext, through the independent identity. */
            CHECK_EQ(cq_ref_w_sext(w, W, 64).lo, (uint64_t)cq_ref_sext(x, W));
        }
    }
}

/* ---- L4 and the zero-ancilla claim. ------------------------------------- */

/* THE FULL TUPLE, MEASURED, BOTH PASSES. An earlier draft handed CHECK_GATES
 * literal zeros for the measured NOT and Toffoli columns — comparing 0 against
 * 0, an assertion that cannot fail — and wrote those unobserved zeros into the
 * golden. A stray X or CCX inserted into a cast would have passed. */
static void measure_cast(cq_gold *g, const cq_kd_spec *k, int F, int T,
                         cq_ref_w a, uint64_t want_cx)
{
    cq_counter fwd, unc;
    char key[24];

    (void)run_cast_full(k, F, T, a, &fwd, &unc);

    CHECK_GATES(fwd.x, fwd.cx, fwd.ccx, 0, want_cx, 0);
    CHECK_EQ(fwd.ry + fwd.rz + fwd.mz, 0);
    CHECK_EQ(unc.ry + unc.rz + unc.mz, 0);

    snprintf(key, sizeof key, "%s_f%d", k->name, F);
    cq_gold_check(g, key, "forward", T, fwd.x, fwd.cx, fwd.ccx);
    cq_gold_check(g, key, "unc",     T, unc.x, unc.cx, unc.ccx);
}

CQ_TEST(l4_goldens)
{
    cq_gold g;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/cast.counts",
                      "M13 kernels/cast.c — K5 sext/zext/trunc",
                      CQOPS_BENNETT_COMMIT))
        return;

    for (int i = 0; i < N_LADDER; i++)
        for (int j = 0; j < N_LADDER; j++) {
            int F = LADDER[i], T = LADDER[j];
            cq_ref_w all;

            if (T >= F) {
                all = cq_ref_w_ones(F);

                measure_cast(&g, &ZEXT, F, T, all, (uint64_t)F);
                measure_cast(&g, &SEXT, F, T, all, (uint64_t)T);
            }

            if (T <= F) {
                all = cq_ref_w_ones(F);
                measure_cast(&g, &TRUNC, F, T, all, (uint64_t)T);
            }
        }

    CHECK(cq_gold_close(&g));
}

/* zext's high bits cost NOTHING — not a gate and not a qubit. That is the
 * whole per-bit-representation payoff over Bennett, which must allocate all T
 * wires and pin the high ones at |0>, and it is invisible to a gate count
 * alone. */
CQ_TEST(zext_allocates_only_the_source_width)
{
    cq_ctx ctx; cq_counter cnt; cq_sink sink;
    cq_kd_shape sh;

    cq_count_reset(&cnt);
    sink = cq_sink_counter(&cnt);
    cq_ctx_init(&ctx, &sink);

    g_F = 8; g_T = 64;
    cast_shape(8, &sh);

    int32_t ha = cq_bk_reg_w(&ctx, 8u, cq_ref_w_ones(8), cq_ref_w_ones(8));
    int32_t hd = cq_reg_alloc_zero(&ctx.regs, 64u);
    const cq_bit *src[1] = { cq_reg_cbits(&ctx.regs, ha) };

    cq_pc_snap before = cq_pc_take(&ctx);
    call_zext(&ctx, cq_reg_bits(&ctx.regs, hd), src, &sh);
    cq_pc_snap after = cq_pc_take(&ctx);

    CHECK_EQ(cq_reg_owned_qubits(&ctx.regs, hd), 8);   /* not 64 */
    CHECK_EQ(after.peak - before.peak, 8);             /* and no transient */

    cq_ctx_dispose(&ctx);
}

CQ_TEST_MAIN_ARGV(
    CQ_CASE(k5_zext_sweep),
    CQ_CASE(k5_sext_sweep),
    CQ_CASE(k5_trunc_sweep),
    CQ_CASE(wide_sext_fills_the_high_word),
    CQ_CASE(wide_i80_crosses_the_word_seam),
    CQ_CASE(sext_against_an_independently_derived_oracle),
    CQ_CASE(the_two_reference_models_agree_below_64),
    CQ_CASE(l4_goldens),
    CQ_CASE(zext_allocates_only_the_source_width)
)
