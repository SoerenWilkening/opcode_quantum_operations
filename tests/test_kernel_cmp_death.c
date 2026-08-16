/* tests/test_kernel_cmp_death.c — M16's fail-loud paths, Step 13.
 *
 * M16 adds no hard error of its own. It calls `cq_kernel_check_n` (with
 * `w_dst = 1`, because `icmp` is `i1`) and everything else it can refuse
 * belongs to M08 or M09, which have their own suites. What this file proves is
 * the one thing those suites cannot: that the shared body behind the ten entry
 * points actually MAKES that call, and makes it with the right widths.
 *
 * CLASSICAL OPERANDS, DELIBERATELY, AND FOR K9 THE ARGUMENT IS SHARPER THAN IT
 * WAS FOR M14. An all-classical compare takes the R9 short-circuit, so it
 * never allocates scratch and never enters cq_sandwich; its only emission is
 * `cq_emit_x` on a constant, which carries no distinctness check. Nothing
 * below M16 speaks, in either configuration — and for `the_two_sources_are_
 * the_same_register` nothing above it would either, because `eq(a,a)` is
 * *true*: delete the guard and the case returns the RIGHT ANSWER, silently.
 * That is the whole reason it is registered.
 *
 * D7b IS THE ONE CQ_LANG ACTUALLY SHIPS FOR THIS OPCODE.
 * `tests/e2e/slice_select_rail_alias_cond.expected.log:4` is
 * `cq_template_icmp_slt_i32(h0, h0)` — legal at the HANDLE boundary, where a
 * blanket abort would fail a fixture CQ_lang ships as correct, and a hard error
 * here, where it means M26's defensive `cqrt_copy` (bd -493) is missing. Both
 * halves of that are in kernels/kernel.h; this file is where the kernel half
 * is observed to fire.
 *
 * BOTH CONFIGURATIONS, so no case carries CQ_DEATH_SKIP_WITHOUT_INVARIANTS:
 * `cq_kernel_check_n` is a D7a/D7b guard and kernel.h records why that one is
 * not Debug-gated — risk R2's entire value is firing during the Step 24
 * fixture run, which Rule 17 pins under Release.
 */

#include "kernels/cmp.h"

#include "bit.h"
#include "ctx.h"
#include "reg.h"
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

/* Two all-classical 8-bit rails, and a one-bit `dst` that overlaps the first.
 * `dst` is one cq_bit, so the overlap is with `a[0]` alone — which is exactly
 * the case a base-pointer comparison would catch and a range comparison also
 * catches, and it is the shape K9's one-bit result makes ordinary. */
static void alias_case(void (*k)(cq_ctx *, cq_bit *, const cq_bit *,
                                 const cq_bit *, int))
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0xC8u, 0x00u);
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x37u, 0x00u);
    cq_bit *a  = cq_reg_bits(&g_ctx.regs, ha);

    CQ_EXPECT_ABORT(k(&g_ctx, a, a, cq_reg_cbits(&g_ctx.regs, hb), 8));
}

/* One per PRIMITIVE — the three distinct scratch layouts and the three
 * distinct short-circuit branches — plus one SWAPPED predicate, because the
 * guard runs before the swap and a version that ran after it would still have
 * to name the caller's pointers. */
static void eq_dst_aliases_a_classical_source (void) { alias_case(cq_kernel_eq);  }
static void ult_dst_aliases_a_classical_source(void) { alias_case(cq_kernel_ult); }
static void slt_dst_aliases_a_classical_source(void) { alias_case(cq_kernel_slt); }
static void ugt_dst_aliases_a_classical_source(void) { alias_case(cq_kernel_ugt); }

static void the_two_sources_are_the_same_register(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0xC8u, 0x00u);
    int32_t hd = cq_reg_alloc_zero(&g_ctx.regs, 1u);
    const cq_bit *a = cq_reg_cbits(&g_ctx.regs, ha);

    CQ_EXPECT_ABORT(cq_kernel_slt(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                  a, a, 8));
}

static void a_width_of_zero(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0xC8u, 0x00u);
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x37u, 0x00u);
    int32_t hd = cq_reg_alloc_zero(&g_ctx.regs, 1u);

    CQ_EXPECT_ABORT(cq_kernel_eq(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                 cq_reg_cbits(&g_ctx.regs, ha),
                                 cq_reg_cbits(&g_ctx.regs, hb), 0));
}

static void a_negative_width(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0xC8u, 0x00u);
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x37u, 0x00u);
    int32_t hd = cq_reg_alloc_zero(&g_ctx.regs, 1u);

    CQ_EXPECT_ABORT(cq_kernel_uge(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                  cq_reg_cbits(&g_ctx.regs, ha),
                                  cq_reg_cbits(&g_ctx.regs, hb), -3));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(eq_dst_aliases_a_classical_source),
    CQ_DEATH_CASE(ult_dst_aliases_a_classical_source),
    CQ_DEATH_CASE(slt_dst_aliases_a_classical_source),
    CQ_DEATH_CASE(ugt_dst_aliases_a_classical_source),
    CQ_DEATH_CASE(the_two_sources_are_the_same_register),
    CQ_DEATH_CASE(a_width_of_zero),
    CQ_DEATH_CASE(a_negative_width)
)
