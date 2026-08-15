/* tests/test_kernel_step11_death.c — M11's and M13's fail-loud paths, Step 11.
 *
 * FILED BY A MUTATION BATTERY, not by design. Step 11 shipped M11 and M13 with
 * four hard errors between them and NO death test for any of them; deleting
 * each in turn left all 80 tests green. Two of the four are the ones that hurt:
 *
 *   shift_const.c  the classical-amount guard — THE M11/M12 BOUNDARY ITSELF.
 *                  Deleting it is silent in Release, where M05's distinctness
 *                  assert is compiled out.
 *   cast.c         the inverted-width guard, whose own comment says it prevents
 *                  "not a wrong answer but a memory error" — a zext with T < F
 *                  writes past the end of dst. Neutering it to `if (0 && ...)`
 *                  left the cast suite 8/8 in BOTH configurations.
 *
 * ALL OF THESE ABORT IN BOTH CONFIGURATIONS. None carries
 * CQ_DEATH_SKIP_WITHOUT_INVARIANTS: none is one of plan §2.1's Debug-gated
 * checks, and cast.c's comment records that the width guard is a both-config
 * error precisely so it survives into the Step 24 fixture run, which Rule 17
 * pins under Release.
 */

#include "kernels/cast.h"
#include "kernels/shift_const.h"

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

/* ---- M11. ---------------------------------------------------------------- */

/* THE M11/M12 BOUNDARY. A quantum shift amount is not a harder case for the
 * constant shifter — it is a different module. The suite cannot reach this by
 * accident: test_kernel_shift.c's shape sets classical[1] = ~0 and the driver
 * subtracts those bits from every mask twice over, so the one path that would
 * provoke this abort was deliberately removed. That is exactly why it needs a
 * test written on purpose. */
static void a_quantum_shift_amount(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0xFFu, 0xFFu);
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x03u, 0x01u);   /* bit 0 is a QUBIT */
    int32_t hd = cq_reg_alloc_zero(&g_ctx.regs, 8u);

    CQ_EXPECT_ABORT(cq_kernel_shl(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                  cq_reg_cbits(&g_ctx.regs, ha),
                                  cq_reg_cbits(&g_ctx.regs, hb), 8));
}

/* A quantum bit ABOVE the stages the construction reads is legal — those bits
 * are structurally invisible to the barrel too, so their kind is not M11's
 * business. This is the NEGATIVE control for the case above: without it, a
 * guard that rejected every quantum bit anywhere in the amount would pass the
 * death test while silently refusing operands M11 can serve. */
static void a_quantum_bit_above_the_stages_is_fine(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0xFFu, 0xFFu);
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x03u, 0x80u);   /* bit 7; S = 3 */
    int32_t hd = cq_reg_alloc_zero(&g_ctx.regs, 8u);

    cq_kernel_shl(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                  cq_reg_cbits(&g_ctx.regs, ha),
                  cq_reg_cbits(&g_ctx.regs, hb), 8);

    /* Reached only if it did NOT abort, which is the assertion. The death
     * harness treats a case that returns as a failure, so this one has to say
     * so itself and exit cleanly. */
    cq_death_skip("a quantum bit above the read stages is accepted, as intended");
}

static void a_shift_dst_overlaps_its_source(void)
{
    setup();
    int32_t hr = cq_bk_reg(&g_ctx, 8u, 0xFFu, 0x00u);
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x02u, 0x00u);
    cq_bit *r = cq_reg_bits(&g_ctx.regs, hr);

    CQ_EXPECT_ABORT(cq_kernel_lshr(&g_ctx, &r[0], &r[2],
                                   cq_reg_cbits(&g_ctx.regs, hb), 4));
}

/* ---- M13. ---------------------------------------------------------------- */

/* A zext with T < F writes dst[i] for i up to F into a T-slot array. Julia
 * raises BoundsError; C does not. Neutering this guard left the cast suite
 * 8/8 in both configurations. */
static void a_widening_cast_that_narrows(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 16u, 0xFFFFu, 0xFFFFu);
    int32_t hd = cq_reg_alloc_zero(&g_ctx.regs, 8u);

    CQ_EXPECT_ABORT(cq_kernel_zext(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                   cq_reg_cbits(&g_ctx.regs, ha), 16, 8));
}

static void a_narrowing_cast_that_widens(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0xFFu, 0xFFu);
    int32_t hd = cq_reg_alloc_zero(&g_ctx.regs, 16u);

    CQ_EXPECT_ABORT(cq_kernel_trunc(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                    cq_reg_cbits(&g_ctx.regs, ha), 8, 16));
}

static void a_sign_extension_that_narrows(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 32u, 0xFFFFFFFFu, 0xFFFFFFFFu);
    int32_t hd = cq_reg_alloc_zero(&g_ctx.regs, 16u);

    CQ_EXPECT_ABORT(cq_kernel_sext(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                   cq_reg_cbits(&g_ctx.regs, ha), 32, 16));
}

/* THE OVERLAP CASE SIZED FOR A CAST, where dst and the source have DIFFERENT
 * widths — the configuration whose byte extents the pre-Step-11 guard computed
 * wrongly, because it sized both ranges with one W. */
static void a_cast_dst_overlaps_its_source(void)
{
    setup();
    int32_t hr = cq_bk_reg(&g_ctx, 32u, 0xFFFFFFFFu, 0x00000000u);
    cq_bit *r = cq_reg_bits(&g_ctx.regs, hr);

    /* dst = r[0..15] (T=16), a = r[8..15] (F=8): an eight-element overlap that
     * no base-pointer comparison sees. */
    CQ_EXPECT_ABORT(cq_kernel_zext(&g_ctx, &r[0], &r[8], 8, 16));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(a_quantum_shift_amount),
    CQ_DEATH_CASE(a_quantum_bit_above_the_stages_is_fine),
    CQ_DEATH_CASE(a_shift_dst_overlaps_its_source),
    CQ_DEATH_CASE(a_widening_cast_that_narrows),
    CQ_DEATH_CASE(a_narrowing_cast_that_widens),
    CQ_DEATH_CASE(a_sign_extension_that_narrows),
    CQ_DEATH_CASE(a_cast_dst_overlaps_its_source)
)
