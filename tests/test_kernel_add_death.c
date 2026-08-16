/* tests/test_kernel_add_death.c — M14's fail-loud path, Step 12.
 *
 * ONE CASE PER ENTRY POINT, AND THAT IS THE WHOLE FILE. M14 adds no hard error
 * of its own: it calls `cq_kernel_check_dst` (kernels/kernel.h) and everything
 * else it can refuse belongs to M08 or M09, which have their own suites. What
 * this file proves is the one thing those suites cannot — that each of the two
 * entry points actually MAKES that call. Delete the line from `cq_kernel_add`
 * and only `add_dst_aliases_a_classical_source` goes red; delete it from
 * `cq_kernel_sub` and only the sub case does. That is the Step 11 lesson
 * applied on purpose rather than after a mutation battery finds it: M11 and
 * M13 shipped four hard errors between them with no death test at all.
 *
 * A CLASSICAL SOURCE, DELIBERATELY, and it is the ONLY alias with no layer
 * beneath it. With `dst == a` all-classical the kernel takes the R9
 * short-circuit, so it never allocates scratch and never enters `cq_sandwich`;
 * the short-circuit's only emission is `cq_emit_x` on a constant, which
 * carries no distinctness check. Nothing below M14 speaks, in either
 * configuration.
 *
 * THE QUANTUM ALIAS IS A DIFFERENT STORY, and an earlier draft of this comment
 * got it wrong in the direction that matters — it claimed this kernel "has no
 * backstop at all". It has one. M05's §3 distinctness indeed cannot fire (on
 * the sandwich path `dst` is touched only by copyout, whose control is a
 * scratch bit genuinely distinct from it), but copyout writing into `a`
 * corrupts a control the reverse half then replays against, so the compute
 * half stops cancelling and `cq_shadow_retire` aborts from M02 — in BOTH
 * configurations, and CLAUDE.md names that check as exactly this detector.
 * So the guard is load-bearing for the classical case and belt-and-braces for
 * the quantum one; only the first is registered here, because only the first
 * is a case no other layer would catch.
 *
 * BOTH ABORT IN BOTH CONFIGURATIONS, so neither carries
 * CQ_DEATH_SKIP_WITHOUT_INVARIANTS: `cq_kernel_check_dst` is a D7a guard and
 * kernel.h records why that one is not Debug-gated — risk R2's entire value is
 * firing during the Step 24 fixture run, which Rule 17 pins under Release.
 */

#include "kernels/add.h"

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

static void add_dst_aliases_a_classical_source(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0xC8u, 0x00u);   /* all-classical */
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x37u, 0x00u);
    cq_bit *a  = cq_reg_bits(&g_ctx.regs, ha);

    CQ_EXPECT_ABORT(cq_kernel_add(&g_ctx, a, a,
                                  cq_reg_cbits(&g_ctx.regs, hb), 8));
}

static void sub_dst_aliases_a_classical_source(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0xC8u, 0x00u);
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x37u, 0x00u);
    cq_bit *a  = cq_reg_bits(&g_ctx.regs, ha);

    CQ_EXPECT_ABORT(cq_kernel_sub(&g_ctx, a, a,
                                  cq_reg_cbits(&g_ctx.regs, hb), 8));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(add_dst_aliases_a_classical_source),
    CQ_DEATH_CASE(sub_dst_aliases_a_classical_source)
)
