/* tests/test_kernel_mul_death.c — M18's fail-loud paths, Step 16.
 *
 * M18 ADDS ONE HARD ERROR OF ITS OWN and inherits the rest. `cq_mul_steps`
 * refuses a non-positive width; everything else it can refuse belongs to
 * `cq_kernel_check_dst` (kernels/kernel.h), to M08/M09, or to M15's
 * `cq_addacc_check`, each of which has its own suite. What this file proves is
 * what those suites cannot: that `cq_kernel_mul` actually MAKES the D7a/D7b
 * call, and that M18's own guard is reachable.
 *
 * ALL-CLASSICAL OPERANDS, DELIBERATELY, and it is the only alias with no layer
 * beneath it — the same argument M14 and M16 record. With every operand bit a
 * constant the kernel takes the R9 short-circuit at the top of
 * `cq_kernel_mul`: it never allocates scratch, never enters `cq_sandwich`,
 * never reaches `cq_addacc_check`, and its only emission is `cq_emit_x` on a
 * constant, which carries no distinctness check. Delete
 * `cq_kernel_check_dst` and each of these returns a plausible answer with no
 * diagnostic in BOTH configurations.
 *
 * `the_two_sources_are_the_same_register` IS THE SHARPEST OF THEM, and it is
 * not hypothetical. `mul(dst, a, a)` is x², which CQ_lang genuinely emits —
 * `cq_template_mul_i32(h10, h10)` is one of the 599 D7b occurrences measured
 * over the corpus, 10 of them on v1's integer surface. It is LEGAL at the
 * handle boundary and must not abort there; the remedy is M26's defensive
 * `cqrt_copy` (bd -493), in one place. With the guard deleted this case returns
 * the RIGHT ANSWER — a·a is a·a — while, on the quantum path, phase P would
 * emit Toffolis whose two controls are one physical qubit.
 *
 * A WIDTH GUARD WITH A DISJOINT MESSAGE, for the Step-15 reason. Both of M18's
 * width refusals would fire on `cq_kernel_mul(..., 0)`, and
 * `cq_kernel_check_dst` gets there first — so `cq_mul_steps`' own guard would
 * survive mutation to always-true, which is precisely the masked-by-an-EARLIER-
 * copy shape the addacc battery found. The two messages differ and
 * tests/CMakeLists.txt pins which one each case must hear.
 *
 * ALL ABORT IN BOTH CONFIGURATIONS, so none carries
 * CQ_DEATH_SKIP_WITHOUT_INVARIANTS: `cq_kernel_check_dst` is a D7a guard and
 * kernel.h records why that one is not Debug-gated — risk R2's whole value is
 * firing during the Step 24 fixture run, which Rule 17 pins under Release.
 */

#include "kernels/mul.h"

#include "bit.h"
#include "ctx.h"
#include "reg.h"
#include "support/bitkinds.h"
#include "support/death.h"

#include <stdio.h>
#include <stdlib.h>

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

static void mul_dst_aliases_a_classical_source(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0x0Cu, 0x00u);   /* all-classical */
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x15u, 0x00u);
    cq_bit *a  = cq_reg_bits(&g_ctx.regs, ha);

    CQ_EXPECT_ABORT(cq_kernel_mul(&g_ctx, a, a,
                                  cq_reg_cbits(&g_ctx.regs, hb), 8));
}

static void mul_dst_aliases_the_second_source(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0x0Cu, 0x00u);
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x15u, 0x00u);
    cq_bit *b  = cq_reg_bits(&g_ctx.regs, hb);

    CQ_EXPECT_ABORT(cq_kernel_mul(&g_ctx, b, cq_reg_cbits(&g_ctx.regs, ha),
                                  b, 8));
}

/* SUB-ARRAYS ARE THE SANCTIONED CALLING SHAPE (cq_scratch_span), so the guard
 * compares RANGES and not base pointers — and this is the case that says so.
 * `dst` and `a` share no base address and overlap in two lanes. */
static void mul_dst_overlaps_a_source_without_sharing_a_base(void)
{
    setup();
    int32_t hr = cq_bk_reg(&g_ctx, 8u, 0x3Cu, 0x00u);
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x15u, 0x00u);
    cq_bit *r  = cq_reg_bits(&g_ctx.regs, hr);

    CQ_EXPECT_ABORT(cq_kernel_mul(&g_ctx, &r[0], &r[2],
                                  cq_reg_cbits(&g_ctx.regs, hb), 4));
}

/* x², which CQ_lang ships. See the file header. */
static void mul_the_two_sources_are_the_same_register(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0x0Cu, 0x00u);
    int32_t hd = cq_reg_alloc_zero(&g_ctx.regs, 8u);
    const cq_bit *a = cq_reg_cbits(&g_ctx.regs, ha);

    CQ_EXPECT_ABORT(cq_kernel_mul(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                  a, a, 8));
}

static void mul_a_width_of_zero(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0x0Cu, 0x00u);
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x15u, 0x00u);
    int32_t hd = cq_reg_alloc_zero(&g_ctx.regs, 8u);

    CQ_EXPECT_ABORT(cq_kernel_mul(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                  cq_reg_cbits(&g_ctx.regs, ha),
                                  cq_reg_cbits(&g_ctx.regs, hb), 0));
}

static void mul_a_negative_width(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0x0Cu, 0x00u);
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x15u, 0x00u);
    int32_t hd = cq_reg_alloc_zero(&g_ctx.regs, 8u);

    CQ_EXPECT_ABORT(cq_kernel_mul(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                  cq_reg_cbits(&g_ctx.regs, ha),
                                  cq_reg_cbits(&g_ctx.regs, hb), -8));
}

/* THE ONLY WAY TO REACH M18's OWN GUARD. Through cq_kernel_mul it is
 * unreachable — cq_kernel_check_dst refuses W <= 0 one line earlier — so
 * without this case the line would survive mutation to always-true. The suite
 * calls cq_mul_steps directly for the palindrome's head length, so this is a
 * real entry point and not a contrivance. */
static void mul_steps_called_directly_with_a_zero_width(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_mul_steps(0));
}

/* --- `bd fxz` / PRD §15 D25: D2's ceiling reaches K11's scratch region. ---- */

/* A SINK THAT ABORTS ON ANY GATE, and it is the whole instrument. `bd fxz`'s
 * words are "fail loud rather than half-allocate", and a death test's only
 * native claim is "it aborted" — which a ceiling check moved anywhere later in
 * the kernel would satisfy just as well. What makes the site checkable is that
 * pre-materialisation emits NOTHING: scratch is born CQ_BIT_ZERO, so I6(b)'s
 * step-1 loop takes W² + 2W qubits for zero gates, and the operands below are
 * built at value ZERO so `cq_bk_reg` emits none either. So under a correct
 * library not one gate reaches this sink, and if any does, tests/CMakeLists.txt
 * fails the case on the message rather than on the exit code — a
 * FAIL_REGULAR_EXPRESSION composes with the death binary's own contract where a
 * PASS_REGULAR_EXPRESSION would displace it. */
static void gate_die(const char *op)
{
    fprintf(stderr, "probe: FATAL: a %s was emitted before the ceiling "
                    "refusal\n", op);
    abort();
}

static void dx  (void *u, uint32_t q)                        { (void)u; (void)q; gate_die("x"); }
static void dcx (void *u, uint32_t c, uint32_t t)            { (void)u; (void)c; (void)t; gate_die("cx"); }
static void dccx(void *u, uint32_t a, uint32_t b, uint32_t t){ (void)u; (void)a; (void)b; (void)t; gate_die("ccx"); }

/* THE CEILING IS SET ONE SHORT OF THE SCRATCH REGION, so the refusal lands
 * inside `cq_sandwich`'s step 1 rather than at the copyout's `dst` lanes — the
 * two are different sites and only the first is the one this bead is about.
 * W = 4 makes the region 24 and the operands 8, which is 31 against a device
 * that has 32; at i128 the same arithmetic is 16,640 against a `qec_n_logical`
 * the shipped configs cap at 7 (§15 D20).
 *
 * MEASURED, NOT WRITTEN DOWN, for the reason its positive sibling gives: the
 * closed form is pinned one file over, and a change to it must not silently
 * move this case off the boundary it claims to sit on. The measuring run is a
 * SEPARATE context that is disposed before the bounded one is built. */
static void mul_scratch_exceeds_the_pool_ceiling(void)
{
    uint32_t scratch;

    setup();
    {
        int32_t ma = cq_bk_reg(&g_ctx, 4u, 0u, 0x0Fu);
        int32_t mb = cq_bk_reg(&g_ctx, 4u, 0u, 0x0Fu);
        int32_t md = cq_reg_alloc_zero(&g_ctx.regs, 4u);
        uint32_t before = cq_qubits_live(&g_ctx.pool);

        cq_kernel_mul(&g_ctx, cq_reg_bits(&g_ctx.regs, md),
                      cq_reg_cbits(&g_ctx.regs, ma),
                      cq_reg_cbits(&g_ctx.regs, mb), 4);
        scratch = (cq_qubits_peak(&g_ctx.pool) - before)
                  - cq_reg_owned_qubits(&g_ctx.regs, md);
        cq_ctx_dispose(&g_ctx);
    }
    CQ_DEATH_REQUIRE(scratch == 24u);          /* W² + 2W at W = 4 */

    setup();
    g_sink.x = dx; g_sink.cx = dcx; g_sink.ccx = dccx;
    {
        int32_t ha = cq_bk_reg(&g_ctx, 4u, 0u, 0x0Fu);
        int32_t hb = cq_bk_reg(&g_ctx, 4u, 0u, 0x0Fu);
        int32_t hd = cq_reg_alloc_zero(&g_ctx.regs, 4u);

        CQ_DEATH_REQUIRE(cq_qubits_live(&g_ctx.pool) == 8u);
        cq_qubits_set_ceiling(&g_ctx.pool, 8u + scratch - 1u);

        CQ_EXPECT_ABORT(cq_kernel_mul(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                      cq_reg_cbits(&g_ctx.regs, ha),
                                      cq_reg_cbits(&g_ctx.regs, hb), 4));
    }
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(mul_dst_aliases_a_classical_source),
    CQ_DEATH_CASE(mul_dst_aliases_the_second_source),
    CQ_DEATH_CASE(mul_dst_overlaps_a_source_without_sharing_a_base),
    CQ_DEATH_CASE(mul_the_two_sources_are_the_same_register),
    CQ_DEATH_CASE(mul_a_width_of_zero),
    CQ_DEATH_CASE(mul_a_negative_width),
    CQ_DEATH_CASE(mul_steps_called_directly_with_a_zero_width),
    CQ_DEATH_CASE(mul_scratch_exceeds_the_pool_ceiling)
)
