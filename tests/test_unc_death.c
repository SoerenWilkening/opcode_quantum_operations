/* tests/test_unc_death.c — Step 21. The axis's two free-time hard errors.
 *
 * bd 5vd's acceptance criteria name "cqrt_free of a dirty rail is a hard
 * error" as part of this step, and Rule 6 is why it is a death test and not a
 * predicate: a free of a rail that is not provably clean is the exact
 * signature of a silent state collapse, so it must refuse rather than warn.
 * tests/test_unc.c asserts the PREDICATE (cq_reg_clean before and after the
 * uncompute); this file asserts that the refusal really fires.
 *
 * THAT ACCEPTANCE LINE'S *ACT* IS SUPERSEDED AND ITS REFUSAL IS NOT. PRD §15
 * D15 §4's last clause, confirmed 2026-08-22 and shipped at Step 23, makes both
 * non-clean rows STRAND by default; termination is CQOPS_FREE_ABORT, which both
 * cases below set in C, per case. So each case asserts the VERDICT first, as a
 * value, and only then the act.
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
 *       `_unc` IS NOT SUFFICIENT — for the SHADOW. This is bd 2cf, closed
 *       2026-08-22 as PRD §15 D15.
 *
 * HONEST ABOUT WHICH ONE IS A DETECTOR. The first reaches a guard that
 * tests/test_reg_death.c:free_dirty_rail already provokes by hand, so deleting
 * cq_reg_clean's call site turns that case red too; what this one adds is the
 * kernel-shaped provocation and the pairing. The second is the only place in
 * the tree where the freed rail was never itself rotated — the poison arrived
 * from a source that appeared ONLY as a control — which is what makes it the
 * sharpest such fixture here and why it is a regression marker rather than a
 * defect being tolerated.
 *
 * ITS PREDICTION WAS FULFILLED AND THEN MEASURED WRONG IN ITS LAST CLAUSE, AND
 * THE CASE STILL DOES NOT GET DELETED. This paragraph used to say that `bd 2cf`
 * was an OPEN P2 and that deciding `bd ckd.17b` at Step 23 would turn the case
 * red on purpose. Both beads closed 2026-08-22 into PRD §15 D15, which supplies
 * exactly that other evidence: an observed undo certificate over the CALL
 * STREAM. This fixture is the shape of a positive case for its U1 rule — one
 * forward, the same kernel again on the same handles, and the only intervening
 * writes a cancelling rotation pair.
 *
 * WHAT THE PARAGRAPH THEN PREDICTED — "once M26 carries the certificate this
 * death stops happening" — IS FALSE, and landing 2 (2026-08-27) is what
 * measured it. The certificate reads a call stream recorded at the M26 HANDLE
 * boundary; both cases below drive `cq_kernel_and` and `cq_rotate_ry_bit`
 * DIRECTLY, so no `cqrt_*` entry point runs, nothing is recorded, and
 * `cq_shim_certificate` answers UNPROVEN for want of a history. Neither case
 * moved. Each case's own comment names where its shape WAS reproduced at the
 * layer the certificate runs; read those rather than restating D15 here.
 *
 * WHY NEITHER CARRIES CQ_DEATH_SKIP_WITHOUT_INVARIANTS. Both aborts are in
 * src/reg.c outside any #if, because Rule 17 pins L4 under Release and
 * CQOPS_FREE_ABORT is a RUNTIME flag rather than a build one — a Debug-gated
 * assert simply is not there. Note the flag is NOT what Step 24 runs under: PRD
 * §15 D18's "the fixtures link, run, and do not abort" is unreachable by
 * construction with it armed, so L6 sees the default (stranding) instead.
 */

#include "cqops/cqops.h"

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
 * a materialised wire; the shadow can see it and refuses.
 *
 * THE VERDICT IS ASSERTED, AND IT IS THE POINT OF THE CASE (Step 23). This is
 * the ONLY PROVEN-DIRTY fixture in the tree, and IMPLEMENTATION_PLAN.md's Step
 * 23 row names it BY PATH as landing 2's mandatory negative control — the thing
 * that stops D15's certificate degenerating into "always yes". A case asserting
 * only that the free aborts could not tell a REFUSAL from IGNORANCE: the
 * unproven row aborts identically under CQOPS_FREE_ABORT, and that is exactly
 * what its sibling below does. So the conviction is checked first, as a value,
 * before the act is checked at all.
 *
 * The disposition is < 0 rather than 0 because the rail's wires are
 * determinate: nothing rotated, so the shadow is EXACT here and a non-zero
 * entry is a proof of dirtiness rather than a guess (poolcheck.h has the scope
 * argument).
 *
 * LANDING 2 LANDED AND THIS CASE DID NOT MOVE — DELIBERATELY, and the reason is
 * a fact about WHERE the certificate can see anything rather than a decision to
 * leave a case alone. D15's evidence is an OBSERVED CALL STREAM at the M26
 * handle boundary; this fixture drives `cq_kernel_and` DIRECTLY, so no
 * `cqrt_*` or `cq_shim_*` entry point runs, no record is made, and
 * `cq_shim_certificate` answers UNPROVEN for want of a history — which is the
 * correct answer for a rail it never saw, and is asserted as such in
 * tests/test_shim_cert_rules.inc:a_handle_the_recorder_never_saw_is_unproven_
 * never_clean. The shadow, which IS exact here, keeps the conviction.
 *
 * SO THE NEGATIVE CONTROL WAS REPRODUCED AT THE LAYER THE CERTIFICATE RUNS, not
 * transplanted into this one: tests/test_shim_cert_rules.inc:u1_refuses_the_
 * same_forward_with_no_uncompute builds the identical shape through
 * `cq_shim_bin_qq` with no `_unc`, and its sibling one case up discharges the
 * same call WITH the `_unc`. The two differ by exactly one call, which is the
 * property `bd 06t` asks for.
 *
 * IT REFUSES WITH `== 0`, NOT `< 0`, AND THAT IS NOT A WEAKER REPRODUCTION OF
 * THIS LINE. Upstream escalates a minted rail with no reversal to a VIOLATION
 * because it "demonstrably still holds f(args)" — true of ITS obligation, a
 * known classical basis state, and NOT a claim the rail is non-zero. Ours is:
 * `and(a, b)` with both operands zero really is |0>. The port therefore
 * declines to inherit that escalation (shim/cq_shim_reduce.h, divergence (f)),
 * because a conviction the library cannot see would corrupt the residue split
 * `bd 06t` exists to produce. The act is identical either way. */
static void free_without_the_uncompute(void)
{
    int32_t ha, hb;
    int32_t hd = setup(&ha, &hb);

    run_kernel(hd, ha, hb);

    /* PROVEN DIRTY — the verdict, not the act. */
    CQ_DEATH_REQUIRE(cq_reg_disposition(&ctx, hd,
                                        cq_pc_zero_proof_rotation_free) < 0);

    cqops_set_free_abort(1);
    CQ_EXPECT_ABORT(cq_reg_free(&ctx, hd, cq_pc_zero_proof_rotation_free));
}

/* `_unc` IS NOT SUFFICIENT — bd 2cf (closed; PRD §15 D15), reproduced.
 *
 * The uncompute runs, `dst` is physically |0>, and the free aborts anyway. The
 * poison never touched `dst` directly: cq_shadow_rotate marked ONE bit of the
 * source `b`, and cq_shadow_cx/ccx carry `unknown` from a control into its
 * target with no clearing path, which is precisely the direction a kernel's
 * sources travel. Ry(-0.7)Ry(0.7) = I exactly, so the source's STATE is
 * restored and the XOR still cancels; what is gone is the SHADOW's evidence.
 * D15's certificate reads the call stream and discharges this rail under U1.
 *
 * LANDING 2 LANDED AND THIS CASE DID NOT MOVE EITHER, for the SAME reason as
 * its sibling above and not for a different one: this fixture drives
 * `cq_kernel_and` and `cq_rotate_ry_bit` directly, so nothing is recorded and
 * `cq_shim_certificate` has no history to read. `bd 06t` asks that this case be
 * INVERTED into U1's positive row, and it was — at the layer the certificate
 * actually runs, where the same shape can be built through real symbols:
 * tests/test_shim_cert_rules.inc:u1_discharges_a_template_forward_and_its_
 * uncompute takes a forward whose SOURCE was rotated, uncomputes it, and
 * asserts the free RELEASES every index by name; and
 * ckd18s_cancelled_rotation_pair_is_a_conviction_not_an_absence's second half
 * discharges a rail that was ITSELF rotated, from a zero birth literal.
 *
 * WHAT STAYS HERE IS WHAT ONLY HERE CAN SAY IT: this is the only place in the
 * tree where the freed rail was never itself rotated and the poison arrived
 * ENTIRELY THROUGH THE CONTROL EDGE — `cq_shadow_cx` is `t.unknown |=
 * c.unknown` with no clearing path — and it is the shadow's unproven row
 * against its sibling's proven-dirty row, one file apart. Deleting it would
 * remove the pair that makes the two rows a discriminator.
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

    /* UNPROVEN, NOT DIRTY, and the contrast with the case above is the whole
     * of D15 §3's residue split. The rail is physically |0>; what is missing is
     * our evidence, not the zero. Asserting the verdict here is what makes the
     * pair a discriminator rather than two spellings of "it aborts". */
    CQ_DEATH_REQUIRE(cq_reg_disposition(&ctx, hd,
                                        cq_pc_zero_proof_rotation_free) == 0);

    cqops_set_free_abort(1);
    CQ_EXPECT_ABORT(cq_reg_free(&ctx, hd, cq_pc_zero_proof_rotation_free));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(free_without_the_uncompute),
    CQ_DEATH_CASE(free_of_a_rotation_tainted_uncomputed_rail)
)
