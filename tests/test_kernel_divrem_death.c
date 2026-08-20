/* tests/test_kernel_divrem_death.c — the fail-loud paths of Step 17.
 *
 * FOUR MODULES' WORTH OF GUARDS IN ONE BINARY, because Step 17 adds guards in
 * four places at once: M19's and M20's own, and the two EXPORTS that plan §0.4
 * required of M14 and M16. The exports are the interesting half. `cq_sub_step`
 * and `cq_ult_step` are the first step functions in the project whose caller is
 * a DIFFERENT MODULE mapping its own index space onto theirs, and an off-by-one
 * in that mapping does not fail — it emits a real, plausible gate from the
 * wrong stage of the recurrence. `cq_addacc_step` established the shape at Step
 * 15 and these follow it.
 *
 * ALL-CLASSICAL OPERANDS FOR EVERY ALIAS CASE, DELIBERATELY, and it is the only
 * alias with no layer beneath it — the argument M14, M16 and M18 all record.
 * With every operand bit a constant the kernel takes the R9 short-circuit: it
 * never allocates scratch, never enters `cq_sandwich`, never reaches
 * `cq_ult_step`'s or `cq_sub_step`'s own guards, and its only emission is
 * `cq_emit_x` on a constant, which carries no distinctness check. Delete
 * `cq_kernel_check_dst` and each of these returns a plausible answer with no
 * diagnostic in BOTH configurations.
 *
 * `the_two_sources_are_the_same_register` IS THE SHARPEST, and it is not
 * hypothetical: `udiv(dst, a, a)` is 1 for every non-zero a, and D7b is LEGAL
 * at the handle boundary — 599 occurrences over the corpus, 10 on v1's integer
 * surface — with M26's defensive `cqrt_copy` (bd -493) as the remedy, in one
 * place. With the guard deleted this returns the RIGHT ANSWER.
 *
 * WIDTH GUARDS WITH DISJOINT MESSAGES, for the Step-15 reason. Every entry
 * point calls `cq_kernel_check_dst` first, so M19's, M20's, M14's and M16's own
 * width refusals are all unreachable THROUGH THE KERNEL and would survive
 * mutation to always-true — the masked-by-an-EARLIER-copy shape the addacc
 * battery found. Each message differs and tests/CMakeLists.txt pins which one
 * every case must hear.
 *
 * ALL ABORT IN BOTH CONFIGURATIONS, so none carries
 * CQ_DEATH_SKIP_WITHOUT_INVARIANTS: `cq_kernel_check_dst` is a D7a guard and
 * kernel.h records why that one is not Debug-gated — risk R2's whole value is
 * firing during the Step 24 fixture run, which Rule 17 pins under Release.
 */

#include "kernels/divrem_s.h"
#include "kernels/divrem_u.h"

#include "bit.h"
#include "ctx.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "reg.h"
#include "scratch.h"
#include "support/bitkinds.h"
#include "support/death.h"

static cq_sink g_sink;
static cq_ctx  g_ctx;

static void nx  (void *u, uint32_t q)                        { (void)u; (void)q; }
static void ncx (void *u, uint32_t c, uint32_t t)            { (void)u; (void)c; (void)t; }
static void nccx(void *u, uint32_t a, uint32_t b, uint32_t t){ (void)u; (void)a; (void)b; (void)t; }
static void nry (void *u, uint32_t q, double th)             { (void)u; (void)q; (void)th; }
static void nrz (void *u, uint32_t q, double ph)             { (void)u; (void)q; (void)ph; }
static void nmz (void *u, uint32_t q)                        { (void)u; (void)q; }

static void setup(void)
{
    g_sink.x = nx; g_sink.cx = ncx; g_sink.ccx = nccx;
    g_sink.ry = nry; g_sink.rz = nrz; g_sink.mz = nmz;
    g_sink.user = NULL;
    cq_ctx_init(&g_ctx, &g_sink);
}

/* ---- D7a: `dst` is one of the sources. ---------------------------------- */

static void udiv_dst_aliases_a_classical_source(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0xC8u, 0x00u);   /* all-classical */
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x07u, 0x00u);
    cq_bit *a  = cq_reg_bits(&g_ctx.regs, ha);

    CQ_EXPECT_ABORT(cq_kernel_udiv(&g_ctx, a, a,
                                   cq_reg_cbits(&g_ctx.regs, hb), 8));
}

static void urem_dst_aliases_the_second_source(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0xC8u, 0x00u);
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x07u, 0x00u);
    cq_bit *b  = cq_reg_bits(&g_ctx.regs, hb);

    CQ_EXPECT_ABORT(cq_kernel_urem(&g_ctx, b, cq_reg_cbits(&g_ctx.regs, ha),
                                   b, 8));
}

static void sdiv_dst_aliases_a_classical_source(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0xF9u, 0x00u);
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x02u, 0x00u);
    cq_bit *a  = cq_reg_bits(&g_ctx.regs, ha);

    CQ_EXPECT_ABORT(cq_kernel_sdiv(&g_ctx, a, a,
                                   cq_reg_cbits(&g_ctx.regs, hb), 8));
}

static void srem_dst_aliases_a_classical_source(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0xF9u, 0x00u);
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x02u, 0x00u);
    cq_bit *a  = cq_reg_bits(&g_ctx.regs, ha);

    CQ_EXPECT_ABORT(cq_kernel_srem(&g_ctx, a, a,
                                   cq_reg_cbits(&g_ctx.regs, hb), 8));
}

/* `dst` inside a source without sharing a base pointer — the case that proves
 * the guard compares RANGES and not base pointers, which is the shape a
 * composite kernel's sub-array views make representable (kernel.h). */
static void udiv_dst_overlaps_a_source_without_sharing_a_base(void)
{
    setup();
    int32_t hr = cq_bk_reg(&g_ctx, 8u, 0xC8u, 0x00u);
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x07u, 0x00u);
    cq_bit *r  = cq_reg_bits(&g_ctx.regs, hr);

    CQ_EXPECT_ABORT(cq_kernel_udiv(&g_ctx, &r[0], &r[2],
                                   cq_reg_cbits(&g_ctx.regs, hb), 4));
}

/* D7b, and the sharpest case here: `udiv(dst, a, a)` is 1 for every non-zero
 * `a`, so with the guard deleted it returns the right answer in silence. */
static void the_two_sources_are_the_same_register(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0xC8u, 0x00u);
    int32_t hd = cq_reg_alloc_zero(&g_ctx.regs, 8u);
    const cq_bit *a = cq_reg_cbits(&g_ctx.regs, ha);

    CQ_EXPECT_ABORT(cq_kernel_udiv(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                   a, a, 8));
}

/* ---- Widths, at each of the four layers that refuses one. ---------------- */

static void udiv_a_width_of_zero(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0xC8u, 0x00u);
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x07u, 0x00u);
    int32_t hd = cq_reg_alloc_zero(&g_ctx.regs, 8u);

    CQ_EXPECT_ABORT(cq_kernel_udiv(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                   cq_reg_cbits(&g_ctx.regs, ha),
                                   cq_reg_cbits(&g_ctx.regs, hb), 0));
}

static void sdiv_a_negative_width(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0xC8u, 0x00u);
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x07u, 0x00u);
    int32_t hd = cq_reg_alloc_zero(&g_ctx.regs, 8u);

    CQ_EXPECT_ABORT(cq_kernel_sdiv(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                   cq_reg_cbits(&g_ctx.regs, ha),
                                   cq_reg_cbits(&g_ctx.regs, hb), -8));
}

static void divrem_steps_called_directly_with_a_zero_width(void)
{
    CQ_EXPECT_ABORT((void)cq_divrem_steps(0, 1));
}

static void divrem_region_called_directly_with_a_zero_width(void)
{
    CQ_EXPECT_ABORT((void)cq_divrem_region(0, 1));
}

static void sdivrem_steps_called_directly_with_a_zero_width(void)
{
    CQ_EXPECT_ABORT((void)cq_sdivrem_steps(0, 1));
}

static void sdivrem_region_called_directly_with_a_zero_width(void)
{
    CQ_EXPECT_ABORT((void)cq_sdivrem_region(0, 1));
}

/* ---- The exported step blocks (plan §0.4). ------------------------------- */

/* A block over a region wide enough for it, with nothing materialised: these
 * cases never emit a gate, because the index guard is the first thing
 * `cq_sub_step` and `cq_ult_step` do. */
static cq_scratch g_scr;

static cq_bit *region(int n)
{
    cq_scratch_alloc(&g_scr, (uint32_t)n);
    return g_scr.bits;
}

static void sub_steps_called_directly_with_a_zero_width(void)
{
    CQ_EXPECT_ABORT((void)cq_sub_steps(0));
}

static void sub_step_index_past_the_end(void)
{
    cq_sub_block k;
    cq_bit *r;

    setup();
    r = region(5 * 4);
    k.a = r; k.b = r + 4; k.nb = r + 8; k.d = r + 12; k.c = r + 16; k.W = 4;

    CQ_EXPECT_ABORT(cq_sub_step(&g_ctx, &k, cq_sub_steps(4)));
}

static void sub_step_index_is_negative(void)
{
    cq_sub_block k;
    cq_bit *r;

    setup();
    r = region(5 * 4);
    k.a = r; k.b = r + 4; k.nb = r + 8; k.d = r + 12; k.c = r + 16; k.W = 4;

    CQ_EXPECT_ABORT(cq_sub_step(&g_ctx, &k, -1));
}

static void ult_steps_called_directly_with_a_zero_width(void)
{
    CQ_EXPECT_ABORT((void)cq_ult_steps(0));
}

static void ult_step_index_past_the_end(void)
{
    cq_ult_block k;
    cq_bit *r;

    setup();
    r = region(5 * 4 + 1);
    k.a = r; k.b = r + 4; k.nb = r + 8; k.carry = r + 12; k.axnb = r + 17;
    k.W = 4;

    CQ_EXPECT_ABORT(cq_ult_step(&g_ctx, &k, cq_ult_steps(4)));
}

static void ult_step_index_is_negative(void)
{
    cq_ult_block k;
    cq_bit *r;

    setup();
    r = region(5 * 4 + 1);
    k.a = r; k.b = r + 4; k.nb = r + 8; k.carry = r + 12; k.axnb = r + 17;
    k.W = 4;

    CQ_EXPECT_ABORT(cq_ult_step(&g_ctx, &k, -1));
}

static void divrem_step_index_past_the_end(void)
{
    cq_divrem_block k;

    setup();
    (void)region(cq_divrem_region(4, 1));
    k.a = g_scr.bits; k.b = g_scr.bits; k.scr = &g_scr; k.off = 0u;
    k.W = 4; k.with_q = 1;

    CQ_EXPECT_ABORT(cq_divrem_step(&g_ctx, &k, cq_divrem_steps(4, 1)));
}

/* `cq_divrem_quotient` on a remainder-only block. M20's srem path reads the
 * remainder and never the quotient, so a wrapper that asked for the wrong one
 * would otherwise index `W` bits past the end of the region — which
 * cq_scratch_span WOULD catch, but with a message about a span rather than
 * about the mistake. */
static void the_quotient_of_a_remainder_only_block(void)
{
    cq_divrem_block k;

    setup();
    (void)region(cq_divrem_region(4, 0));
    k.a = g_scr.bits; k.b = g_scr.bits; k.scr = &g_scr; k.off = 0u;
    k.W = 4; k.with_q = 0;

    CQ_EXPECT_ABORT((void)cq_divrem_quotient(&k));
}

/* The classical fold's per-bit arrays are bounded by the REGISTER cap, and
 * divrem_u.c asserts at compile time that the two are the same number. This is
 * the run-time half: a width above it is refused rather than overrunning. */
static void the_classical_fold_above_the_register_cap(void)
{
    unsigned char a[4] = { 0, 0, 0, 0 }, b[4] = { 0, 0, 0, 0 };
    unsigned char q[4], r[4];

    CQ_EXPECT_ABORT(cq_divrem_classical(a, b, CQ_DIVREM_MAX_W + 1, q, r));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(udiv_dst_aliases_a_classical_source),
    CQ_DEATH_CASE(urem_dst_aliases_the_second_source),
    CQ_DEATH_CASE(sdiv_dst_aliases_a_classical_source),
    CQ_DEATH_CASE(srem_dst_aliases_a_classical_source),
    CQ_DEATH_CASE(udiv_dst_overlaps_a_source_without_sharing_a_base),
    CQ_DEATH_CASE(the_two_sources_are_the_same_register),
    CQ_DEATH_CASE(udiv_a_width_of_zero),
    CQ_DEATH_CASE(sdiv_a_negative_width),
    CQ_DEATH_CASE(divrem_steps_called_directly_with_a_zero_width),
    CQ_DEATH_CASE(divrem_region_called_directly_with_a_zero_width),
    CQ_DEATH_CASE(sdivrem_steps_called_directly_with_a_zero_width),
    CQ_DEATH_CASE(sdivrem_region_called_directly_with_a_zero_width),
    CQ_DEATH_CASE(sub_steps_called_directly_with_a_zero_width),
    CQ_DEATH_CASE(sub_step_index_past_the_end),
    CQ_DEATH_CASE(sub_step_index_is_negative),
    CQ_DEATH_CASE(ult_steps_called_directly_with_a_zero_width),
    CQ_DEATH_CASE(ult_step_index_past_the_end),
    CQ_DEATH_CASE(ult_step_index_is_negative),
    CQ_DEATH_CASE(divrem_step_index_past_the_end),
    CQ_DEATH_CASE(the_quotient_of_a_remainder_only_block),
    CQ_DEATH_CASE(the_classical_fold_above_the_register_cap)
)
