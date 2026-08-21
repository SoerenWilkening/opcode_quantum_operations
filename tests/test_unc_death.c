/* tests/test_unc_death.c — Step 21. The axis's two free-time hard errors.
 *
 * bd 5vd's acceptance criteria name "cqrt_free of a dirty rail is a hard
 * error" as part of this step, and Rule 6 is why it is a death test and not a
 * predicate: a free of a rail that is not provably clean is the exact
 * signature of a silent state collapse, so it must abort rather than warn.
 * tests/test_unc.c asserts the PREDICATE (cq_reg_clean before and after the
 * uncompute); this file asserts that the abort really fires.
 *
 * TWO CASES, AND THEY ARE THE SAME GUARD REACHED BY OPPOSITE ROUTES. That is
 * the point of having both:
 *
 *   `free_without_the_uncompute`   — the rail holds f(a,b) on determinate
 *       wires. It IS dirty, the shadow says so, and the abort is correct.
 *       `_unc` IS NECESSARY.
 *
 *   `free_of_a_rotation_tainted_uncomputed_rail` — the rail is physically
 *       |0>, the uncompute DID run, and the abort fires anyway because a
 *       general Ry on a source poisoned it through the control edge.
 *       `_unc` IS NOT SUFFICIENT. This is bd 2cf.
 *
 * HONEST ABOUT WHICH ONE IS A DETECTOR. The first reaches a guard that
 * tests/test_reg_death.c:free_dirty_rail already provokes by hand, so deleting
 * cq_reg_clean's call site turns that case red too; what this one adds is the
 * kernel-shaped provocation and the pairing. The second is the only place in
 * the tree where the freed rail was never itself rotated — the poison arrived
 * from a source that appeared ONLY as a control — and it is a regression
 * marker for an OPEN P2 bead rather than a defect being tolerated: when
 * bd ckd.17b is decided at Step 23 and gives cqrt_free some evidence other
 * than the shadow, this case is expected to go red, on purpose, and its
 * failure is the signal to re-read it rather than to delete it.
 *
 * WHY NEITHER CARRIES CQ_DEATH_SKIP_WITHOUT_INVARIANTS. Both aborts are in
 * src/reg.c outside any #if, because Rule 17 pins L4 and the Step 24 fixture
 * run under Release and a dirty free must fail there too.
 */

#include "support/death.h"
#include "support/harness.h"

#include "ctx.h"
#include "emit.h"
#include "reg.h"
#include "rotate.h"
#include "sink_count.h"
#include "support/bitkinds.h"
#include "support/poolcheck.h"

#include "kernels/bitwise.h"

enum { UD_W = 4 };

static cq_ctx     ctx;
static cq_counter cnt;
static cq_sink    sink;

/* `a` all quantum, `b` all classical — the mixed mask the whole axis lives on;
 * at an all-quantum mask nothing can drift (see test_unc_asym.inc). Returns
 * dst's handle and writes the two source handles. */
static int32_t setup(int32_t *ha, int32_t *hb)
{
    cq_count_reset(&cnt);
    sink = cq_sink_counter(&cnt);
    cq_ctx_init(&ctx, &sink);

    *ha = cq_bk_reg(&ctx, (uint32_t)UD_W, 0xBu, ~0ull);
    *hb = cq_bk_reg(&ctx, (uint32_t)UD_W, 0x6u, 0ull);
    return cq_reg_alloc_zero(&ctx.regs, (uint32_t)UD_W);
}

static void run_kernel(int32_t hd, int32_t ha, int32_t hb)
{
    cq_kernel_and(&ctx, cq_reg_bits(&ctx.regs, hd),
                  cq_reg_cbits(&ctx.regs, ha),
                  cq_reg_cbits(&ctx.regs, hb), UD_W);
}

/* `_unc` IS NECESSARY. `and(0xB, 0x6) = 2`, so the rail holds a live value on
 * a materialised wire; the shadow can see it and refuses. */
static void free_without_the_uncompute(void)
{
    int32_t ha, hb;
    int32_t hd = setup(&ha, &hb);

    run_kernel(hd, ha, hb);
    CQ_EXPECT_ABORT(cq_reg_free(&ctx, hd, cq_pc_zero_proof_rotation_free));
}

/* `_unc` IS NOT SUFFICIENT — bd 2cf, reproduced.
 *
 * The uncompute runs, `dst` is physically |0>, and the free aborts anyway. The
 * poison never touched `dst` directly: cq_shadow_rotate marked ONE bit of the
 * source `b`, and cq_shadow_cx/ccx carry `unknown` from a control into its
 * target with no clearing path, which is precisely the direction a kernel's
 * sources travel. Ry(-0.7)Ry(0.7) = I exactly, so the source's STATE is
 * restored and the XOR still cancels; what is gone is our certificate.
 *
 * That the rail really is zero is not asserted here — nothing in this project
 * can read a poisoned rail (cq_pc_value fails by design; rt_value and
 * cq_measure return 0 for one, which would make any such assertion vacuous;
 * Rule 13 forbids a simulator). It is established in test_unc_asym.inc, by a
 * second route that emits the identical gate stream and keeps its shadow. */
static void free_of_a_rotation_tainted_uncomputed_rail(void)
{
    int32_t ha, hb;
    int32_t hd = setup(&ha, &hb);
    cq_bit *bb;

    run_kernel(hd, ha, hb);

    bb = cq_reg_bits(&ctx.regs, hb);
    cq_rotate_ry_bit(&ctx, &bb[0],  0.7);   /* CQ_ANGLE_GENERAL: the one §7 */
    cq_rotate_ry_bit(&ctx, &bb[0], -0.7);   /* cell that materialises        */

    run_kernel(hd, ha, hb);

    CQ_EXPECT_ABORT(cq_reg_free(&ctx, hd, cq_pc_zero_proof_rotation_free));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(free_without_the_uncompute),
    CQ_DEATH_CASE(free_of_a_rotation_tainted_uncomputed_rail)
)
