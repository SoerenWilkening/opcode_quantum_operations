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
#include "scratch.h"
#include "support/bitkinds.h"
#include "support/death.h"

static cq_sink    g_sink;
static cq_ctx     g_ctx;
static cq_scratch g_scr;

static void setup(void)
{
    g_sink = cq_death_null_sink();
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

/* --- K6's EXPORTED STEP BLOCK (bd 9ve.30). ------------------------------
 *
 * THE OTHER HALF OF THE FILE'S ARGUMENT, and a different one. The two cases
 * above prove an ENTRY POINT makes a call it could omit. These three prove the
 * two guards the export itself added, whose caller is a DIFFERENT MODULE
 * mapping its own index space onto `[0, cq_add_steps(W))` — M32/M33/M39 at
 * W = 64 — where an off-by-one does not fail. It lands in `ripple`'s tail and
 * emits a real, plausible gate from the wrong stage. `cq_sub_step`'s three
 * cases in tests/test_kernel_divrem_death.c are the template.
 *
 * MEASURED, AND IT IS NOT WHAT THE SHAPE SUGGESTS: neither index case reaches
 * out of bounds even with the range check deleted. At `u == cq_add_steps(W)`
 * the tail's `u - 5*(W-1)` is 3, which takes the `default:` arm and reads
 * `c[W-1]`/`t[W-1]`; at `u == -1`, C's truncating division gives `i = 0` and
 * `u % 5 == -1`, which takes the same arm at index 0. Both are INSIDE the
 * region. So the conviction is the death harness's own SURVIVED path — the
 * exit code — in both configurations, exactly as bd 9ve.1 measured for
 * `cq_eq_step`. The message pins in tests/CMakeLists.txt are there for the
 * OTHER direction: `cq_add_steps`' width guard runs first inside the same
 * function, and an index case allowed to pass on ITS message would leave the
 * range check surviving mutation to always-true.
 *
 * THE REGION MATERIALISES NOTHING, deliberately: every bit is CQ_BIT_ZERO, so
 * the fold table kills each gate before its target is read and no sanitizer
 * can speak for these cases either. */
static cq_bit *region(int n)
{
    cq_scratch_alloc(&g_scr, (uint32_t)n);
    return g_scr.bits;
}

static void bind(cq_add_block *k, int W)
{
    cq_bit *r = region(4 * W);

    k->a = r;
    k->b = r + W;
    k->c = r + 2 * W;      /* `c ++ t`, the opposite of add.c's own layout */
    k->t = r + 3 * W;
    k->W = W;
}

static void add_steps_called_directly_with_a_zero_width(void)
{
    CQ_EXPECT_ABORT((void)cq_add_steps(0));
}

static void add_step_index_past_the_end(void)
{
    cq_add_block k;

    setup();
    bind(&k, 4);

    CQ_EXPECT_ABORT(cq_add_step(&g_ctx, &k, cq_add_steps(4)));
}

static void add_step_index_is_negative(void)
{
    cq_add_block k;

    setup();
    bind(&k, 4);

    CQ_EXPECT_ABORT(cq_add_step(&g_ctx, &k, -1));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(add_dst_aliases_a_classical_source),
    CQ_DEATH_CASE(sub_dst_aliases_a_classical_source),
    CQ_DEATH_CASE(add_steps_called_directly_with_a_zero_width),
    CQ_DEATH_CASE(add_step_index_past_the_end),
    CQ_DEATH_CASE(add_step_index_is_negative)
)
