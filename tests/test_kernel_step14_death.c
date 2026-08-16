/* tests/test_kernel_step14_death.c — M17's and M12's fail-loud paths, Step 14.
 *
 * ONE FILE FOR TWO MODULES, like test_kernel_step11_death.c, because between
 * them they add no hard error of their own: both call `cq_kernel_check_n` (via
 * `cq_kernel_check_dst` in M12's case) and everything else they can refuse
 * belongs to M08 or M09, which have their own suites. What this proves is the
 * one thing those suites cannot — that each entry point actually MAKES the
 * call — and, for M12, that it makes it BEFORE it delegates.
 *
 * WHICH ALIAS, AND WHY EACH ONE IS UNGUARDED BENEATH THE KERNEL. The Step 7
 * lesson (a death test can pass on a broken library when two layers guard one
 * condition) says to choose the case that nothing below would catch:
 *
 *   - M17's cases use ALL-CLASSICAL rails with a CLASSICAL `cond`, so the
 *     kernel takes the entry dispatch, never allocates scratch, never enters
 *     cq_sandwich, and emits only `cq_emit_x` on a constant — which carries no
 *     §3 distinctness check. Nothing beneath M17 speaks, in either
 *     configuration.
 *   - M12's cases use a QUANTUM AMOUNT, which is load-bearing: with a classical
 *     amount the barrel DELEGATES to M11, whose own `cq_kernel_check_dst` would
 *     abort, so such a case would stay green with M12's guard deleted — one
 *     module down. A quantum amount takes the sandwich path instead.
 *
 * BUT THE QUANTUM AMOUNT IS NOT ENOUGH ON ITS OWN, AND AN EARLIER DRAFT OF THIS
 * COMMENT CLAIMED IT WAS. It said "nothing speaks" for M12's alias cases.
 * MEASURED, by deleting `cq_kernel_check_dst` from `barrel` and running them:
 * all three `*_var_dst_aliases_its_source` cases still abort, from **M02's
 * `cq_shadow_retire`** — "retire of a determinate NON-ZERO entry — the compute
 * half did not cancel" — in BOTH configurations. Of course they do: copy-out
 * writes into `a`, which the reverse half then replays against as a control, so
 * the sandwich stops cancelling and the epilogue catches it. That is M14's
 * finding (see test_kernel_add_death.c) reaching a case M14 chose to avoid by
 * registering the CLASSICAL alias instead; M12 cannot do that, because a
 * classical amount delegates.
 *
 * SO THE THREE ALIAS CASES CARRY A FAIL_REGULAR_EXPRESSION naming M02's
 * message, and go red if M02 is what caught them. `shift_var_the_value_is_the_
 * amount` (D7b) needs none — measured, it goes red outright with the guard
 * deleted, because two sources aliasing does not corrupt the sandwich, it just
 * emits a malformed Toffoli. See tests/CMakeLists.txt for the width-zero case,
 * which cannot be discriminated at all and says so.
 *
 * M17's six cases were measured the same way and DO stand alone: with
 * `cq_kernel_check_n` deleted from `cq_kernel_mux`, all six go red. A classical
 * `cond` takes the entry dispatch, so the kernel never allocates scratch, never
 * enters `cq_sandwich`, and emits only `cq_emit_x` on a constant — which
 * carries no §3 distinctness check and no palindrome to break.
 *
 * BOTH MODULES ABORT IN BOTH CONFIGURATIONS, so no case carries
 * CQ_DEATH_SKIP_WITHOUT_INVARIANTS: these are D7a/D7b guards and kernel.h
 * records why those are not Debug-gated — risk R2's entire value is firing
 * during the Step 24 fixture run, which Rule 17 pins under Release.
 */

#include "kernels/mux.h"
#include "kernels/shift_var.h"

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

/* --- M17. `cond`, `t`, `f`, all classical. -------------------------------- */

static void mux_dst_aliases_the_true_arm(void)
{
    setup();
    int32_t hc = cq_bk_reg(&g_ctx, 1u, 1u,     0x00u);
    int32_t ht = cq_bk_reg(&g_ctx, 8u, 0xC8u,  0x00u);
    int32_t hf = cq_bk_reg(&g_ctx, 8u, 0x37u,  0x00u);
    cq_bit *t  = cq_reg_bits(&g_ctx.regs, ht);

    CQ_EXPECT_ABORT(cq_kernel_mux(&g_ctx, t, cq_reg_cbits(&g_ctx.regs, hc),
                                  t, cq_reg_cbits(&g_ctx.regs, hf), 8));
}

static void mux_dst_aliases_the_false_arm(void)
{
    setup();
    int32_t hc = cq_bk_reg(&g_ctx, 1u, 0u,     0x00u);
    int32_t ht = cq_bk_reg(&g_ctx, 8u, 0xC8u,  0x00u);
    int32_t hf = cq_bk_reg(&g_ctx, 8u, 0x37u,  0x00u);
    cq_bit *f  = cq_reg_bits(&g_ctx.regs, hf);

    CQ_EXPECT_ABORT(cq_kernel_mux(&g_ctx, f, cq_reg_cbits(&g_ctx.regs, hc),
                                  cq_reg_cbits(&g_ctx.regs, ht), f, 8));
}

/* `dst` overlapping the ONE-BIT condition. NOTE WHAT THIS DOES AND DOES NOT
 * SHOW: it shows that `cond` is checked against `dst` at all, which a guard
 * that only looked at the two arms would miss. It does NOT show that the extent
 * is sized per operand — a guard wrongly using `W` for `cond` would compute the
 * range [d+3, d+11) instead of [d+3, d+4), which still overlaps `dst` and still
 * aborts. A too-WIDE extent can only be caught by a case it wrongly REFUSES,
 * which is a positive control and lives in the suite proper:
 * `a_one_bit_cond_adjacent_to_an_arm_is_accepted` in
 * test_kernel_mux_dispatch.inc. */
static void mux_dst_overlaps_the_condition(void)
{
    setup();
    int32_t hd = cq_bk_reg(&g_ctx, 8u, 0x00u,  0x00u);
    int32_t ht = cq_bk_reg(&g_ctx, 8u, 0xC8u,  0x00u);
    int32_t hf = cq_bk_reg(&g_ctx, 8u, 0x37u,  0x00u);
    cq_bit *d  = cq_reg_bits(&g_ctx.regs, hd);

    /* The condition is bit 3 of the destination register. */
    CQ_EXPECT_ABORT(cq_kernel_mux(&g_ctx, d, &d[3],
                                  cq_reg_cbits(&g_ctx.regs, ht),
                                  cq_reg_cbits(&g_ctx.regs, hf), 8));
}

/* D7b — `select c, x, x`. Legal at the handle boundary (599 occurrences in the
 * corpus), a hard error here, and the remedy is M26's defensive cqrt_copy
 * (bd -493). Reaching a kernel means that copy is missing. */
static void mux_both_arms_are_the_same_register(void)
{
    setup();
    int32_t hd = cq_bk_reg(&g_ctx, 8u, 0x00u,  0x00u);
    int32_t hc = cq_bk_reg(&g_ctx, 1u, 1u,     0x00u);
    int32_t ht = cq_bk_reg(&g_ctx, 8u, 0xC8u,  0x00u);
    const cq_bit *t = cq_reg_cbits(&g_ctx.regs, ht);

    CQ_EXPECT_ABORT(cq_kernel_mux(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                  cq_reg_cbits(&g_ctx.regs, hc), t, t, 8));
}

/* D7b again, and the shape K10.md §2.1 names as "the D7 aliasing corner":
 * `x ? x : y`, where the condition is a bit of one of the arms. */
static void mux_the_condition_is_a_bit_of_an_arm(void)
{
    setup();
    int32_t hd = cq_bk_reg(&g_ctx, 8u, 0x00u,  0x00u);
    int32_t ht = cq_bk_reg(&g_ctx, 8u, 0xC8u,  0x00u);
    int32_t hf = cq_bk_reg(&g_ctx, 8u, 0x37u,  0x00u);
    const cq_bit *t = cq_reg_cbits(&g_ctx.regs, ht);

    CQ_EXPECT_ABORT(cq_kernel_mux(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                  &t[0], t, cq_reg_cbits(&g_ctx.regs, hf), 8));
}

static void mux_a_width_of_zero(void)
{
    setup();
    int32_t hd = cq_bk_reg(&g_ctx, 8u, 0x00u, 0x00u);
    int32_t hc = cq_bk_reg(&g_ctx, 1u, 1u,    0x00u);
    int32_t ht = cq_bk_reg(&g_ctx, 8u, 0xC8u, 0x00u);
    int32_t hf = cq_bk_reg(&g_ctx, 8u, 0x37u, 0x00u);

    CQ_EXPECT_ABORT(cq_kernel_mux(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                  cq_reg_cbits(&g_ctx.regs, hc),
                                  cq_reg_cbits(&g_ctx.regs, ht),
                                  cq_reg_cbits(&g_ctx.regs, hf), 0));
}

/* --- M12. A QUANTUM amount, so the barrel cannot delegate to M11. --------- */

/* One per entry point: the shared `barrel` makes the call, but only three
 * separate cases prove all three entry points reach `barrel` at all. */
#define CQ_SHV_ALIAS_CASE(name, kernel)                                        \
    static void name(void)                                                     \
    {                                                                          \
        setup();                                                               \
        int32_t ha = cq_bk_reg(&g_ctx, 8u, 0xC8u, 0x00u);  /* classical  */    \
        int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x03u, 0xFFu);  /* QUANTUM    */    \
        cq_bit *a  = cq_reg_bits(&g_ctx.regs, ha);                             \
                                                                               \
        CQ_EXPECT_ABORT(kernel(&g_ctx, a, a,                                   \
                               cq_reg_cbits(&g_ctx.regs, hb), 8));             \
    }

CQ_SHV_ALIAS_CASE(shl_var_dst_aliases_its_source,  cq_kernel_shl_var)
CQ_SHV_ALIAS_CASE(lshr_var_dst_aliases_its_source, cq_kernel_lshr_var)
CQ_SHV_ALIAS_CASE(ashr_var_dst_aliases_its_source, cq_kernel_ashr_var)

/* D7b for the barrel: `x << x`. */
static void shift_var_the_value_is_the_amount(void)
{
    setup();
    int32_t hd = cq_bk_reg(&g_ctx, 8u, 0x00u, 0x00u);
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0x03u, 0xFFu);
    const cq_bit *a = cq_reg_cbits(&g_ctx.regs, ha);

    CQ_EXPECT_ABORT(cq_kernel_shl_var(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                      a, a, 8));
}

/* THIS CASE CANNOT DISCRIMINATE M12 FROM M11 AND IS KEPT ANYWAY, WITH THAT SAID
 * OUT LOUD. At W = 0, `cq_shift_stages` returns 0, so `amount_is_classical` is
 * vacuously true whatever the amount's bit-kinds are and the barrel delegates —
 * meaning M11's identical `cq_kernel_check_dst` would abort with the identical
 * message if M12's were deleted. No FAIL_REGULAR_EXPRESSION can tell them
 * apart. What it does pin is that the entry point refuses W = 0 at all rather
 * than computing `1 << 0` scratch bits and walking off a zero-length region.
 *
 * An earlier version of this comment claimed the guard "stops a zero width from
 * reaching cq_shift_stages". It did not: the call sat in `L`'s initialiser and
 * therefore ran first — harmlessly, since the W <= 1 branch answers 0, but the
 * claim was the wrong way round. `barrel` was reordered so the guard genuinely
 * is the first thing that runs, which is what this case is here to pin. */
static void shift_var_a_width_of_zero(void)
{
    setup();
    int32_t hd = cq_bk_reg(&g_ctx, 8u, 0x00u, 0x00u);
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0xC8u, 0x00u);
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x03u, 0xFFu);

    CQ_EXPECT_ABORT(cq_kernel_lshr_var(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                       cq_reg_cbits(&g_ctx.regs, ha),
                                       cq_reg_cbits(&g_ctx.regs, hb), 0));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(mux_dst_aliases_the_true_arm),
    CQ_DEATH_CASE(mux_dst_aliases_the_false_arm),
    CQ_DEATH_CASE(mux_dst_overlaps_the_condition),
    CQ_DEATH_CASE(mux_both_arms_are_the_same_register),
    CQ_DEATH_CASE(mux_the_condition_is_a_bit_of_an_arm),
    CQ_DEATH_CASE(mux_a_width_of_zero),
    CQ_DEATH_CASE(shl_var_dst_aliases_its_source),
    CQ_DEATH_CASE(lshr_var_dst_aliases_its_source),
    CQ_DEATH_CASE(ashr_var_dst_aliases_its_source),
    CQ_DEATH_CASE(shift_var_the_value_is_the_amount),
    CQ_DEATH_CASE(shift_var_a_width_of_zero)
)
