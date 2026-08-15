/* tests/test_sandwich_death.c — M09's fail-loud paths, Step 8.
 *
 * WHICH CONFIGURATION EACH ONE LIVES IN, and why the line falls where it does.
 *
 * BOTH configurations — the driver's own O(1) premises and the dirty-ancilla
 * detectors. A nested sandwich, a scratch region that is not all CQ_BIT_ZERO
 * on entry, and a compute half that did not cancel are miscompile signatures,
 * not style questions; Rule 17 pins L4 under Release, where a Debug-gated
 * assert is simply absent, and R2's whole value at Step 24 is firing there.
 *
 * DEBUG only — the I6 discipline sweeps. Plan §0.2 assigns the runtime I6
 * checks to CQOPS_DEBUG_INVARIANTS by name ("Run time (debug)"), and they are
 * O(region) rather than O(1). Those cases carry
 * CQ_DEATH_SKIP_WITHOUT_INVARIANTS and report a SKIP in Release, so a Release
 * run never claims to have verified something the configuration compiled out.
 *
 * THE RELEASE BACKSTOP IS NOT NOTHING. With the I6 sweeps compiled out,
 * cq_shadow_retire's "determinate and non-zero" check still fires in Release,
 * and PRD §10 records exactly how far it reaches: a complete detector of a
 * non-cancelling sandwich across the whole rotation-free kernel surface
 * (Steps 10-17), and inert once a rail is rotation-tainted. That is why
 * test_sandwich.c also pins the poisoned-surface case, where only the
 * test-side palindrome check has teeth.
 */

#include "sandwich.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "scratch.h"
#include "shadow.h"
#include "support/death.h"

#include <stdio.h>

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

/* A SINK THAT DISARMS THE DEATH WINDOW, lifted from test_emit_death.c because
 * the same mutation survives here for the same reason. Asserting "it aborts"
 * is not enough when the same guard is armed twice: delete the FIRST sw_arm
 * and an I6(a) violation still aborts — from the second one, on the reverse
 * pass, after the offending gate has already reached the sink. The exit code
 * cannot tell those apart. This can: a gate that escapes disarms the window,
 * so the later abort exits 4 instead of 0 and the case fails.
 *
 * Only usable where the correct behaviour emits NOTHING at all before the
 * abort — which holds for the I6 cases, since pre-materialising a region born
 * BIT_ZERO emits no gates. */
static void leak(const char *op)
{
    fprintf(stderr, "LEAKED GATE: %s reached the sink before the assert\n", op);
    cq_death_disarm();
}
static void lx  (void *u, uint32_t q)                        { (void)u; (void)q; leak("x"); }
static void lcx (void *u, uint32_t c, uint32_t t)            { (void)u; (void)c; (void)t; leak("cx"); }
static void lccx(void *u, uint32_t a, uint32_t b, uint32_t t){ (void)u; (void)a; (void)b; (void)t; leak("ccx"); }

static void setup_gate_leak_detecting(void)
{
    setup();
    g_sink.x = lx; g_sink.cx = lcx; g_sink.ccx = lccx;
}

static void noop_step(cq_ctx *ctx, void *env, int s)
{
    (void)ctx; (void)env; (void)s;
}

static cq_bit q_operand(int value)
{
    cq_bit b = cq_bit_zero();
    cq_materialise(&g_ctx, &b);
    if (value) cq_emit_x(&g_ctx, &b);
    return b;
}

/* --- Both configurations. ------------------------------------------------ */

/* No nested sandwich (plan §0.1, step 0). An inner sandwich releases its own
 * region in the middle of the outer compute half, so the outer's reverse pass
 * re-acquires indices from a free list the forward pass did not see — the
 * stream stops being a palindrome and CQ_ZERO_BY_PALINDROME becomes a
 * laundering site. The depth counter is the one driver premise checked in
 * BOTH configurations because it is O(1); it is also what makes the check
 * survive a nested sandwich attempted from a COPYOUT step, where the I6
 * extent is deliberately disarmed and could not answer. */
static cq_scratch g_inner;

static void nesting_step(cq_ctx *ctx, void *env, int s)
{
    (void)env; (void)s;
    cq_sandwich(ctx, &g_inner, noop_step, 1, NULL, 0, NULL);
}

static void a_nested_sandwich(void)
{
    setup();
    cq_scratch outer;
    cq_scratch_alloc(&outer, 2u);
    cq_scratch_alloc(&g_inner, 2u);

    CQ_EXPECT_ABORT(cq_sandwich(&g_ctx, &outer, nesting_step, 1, NULL, 0, NULL));
}

/* THE CASE THAT DISTINGUISHES M09'S ENTRY CHECK FROM EVERYTHING BELOW IT.
 * A scratch bit holding a constant ONE owns no qubit, so nothing under M09
 * notices: cq_materialise accepts a constant, emits an X, and hands back a
 * qubit in |1>. The region would then be pre-materialised to |1> and I6(b)'s
 * "scratch is born BIT_ZERO, so materialisation emits no X" would be false —
 * silently, with a plausible trace. Delete M09's entry check and this is the
 * single case that goes red (plan §2, the Step 7 lesson). */
static void a_scratch_bit_is_one_on_entry(void)
{
    setup();
    cq_scratch scr;
    cq_scratch_alloc(&scr, 3u);
    cq_emit_x(&g_ctx, cq_scratch_span(&scr, 1u, 1u));    /* ZERO -> ONE, 0 gates */

    CQ_EXPECT_ABORT(cq_sandwich(&g_ctx, &scr, noop_step, 1, NULL, 0, NULL));
}

/* The other half of the same check, and the one WITH a layer underneath:
 * cq_materialise aborts on an already-quantum bit all by itself. The CTest
 * FAIL_REGULAR_EXPRESSION on M05's message is what makes this case assert
 * that M09 caught it FIRST, rather than merely that something did. */
static void a_scratch_bit_is_already_a_qubit_on_entry(void)
{
    setup();
    cq_scratch scr;
    cq_scratch_alloc(&scr, 3u);
    cq_materialise(&g_ctx, cq_scratch_span(&scr, 2u, 1u));

    CQ_EXPECT_ABORT(cq_sandwich(&g_ctx, &scr, noop_step, 1, NULL, 0, NULL));
}

/* ckd.14a, the K06 trap in miniature. Applying
 *   CCX(a, p, t) ; CX(b, p)
 * twice leaves t ^= a&b: the block is not an involution, so replaying the step
 * index in reverse does not undo it. With a = b = 1 and DETERMINATE shadows,
 * t comes back holding 1, and cq_shadow_retire refuses to certify it — in both
 * configurations, because it is the only thing standing between a dirty
 * ancilla and the free list when the I6 sweeps are compiled out. */
typedef struct { const cq_bit *a, *b; cq_bit *p, *t; } bad_env;

static void bad_two_gate_step(cq_ctx *ctx, void *env, int s)
{
    bad_env *e = env;
    (void)s;
    cq_emit_ccx(ctx, e->a, e->p, e->t);
    cq_emit_cx(ctx, e->b, e->p);
}

static void a_multi_gate_step_is_not_an_involution(void)
{
    setup();
    cq_scratch scr;
    cq_scratch_alloc(&scr, 2u);

    cq_bit a = q_operand(1), b = q_operand(1);
    bad_env e;
    e.a = &a; e.b = &b;
    e.p = cq_scratch_span(&scr, 0u, 1u);
    e.t = cq_scratch_span(&scr, 1u, 1u);

    CQ_EXPECT_ABORT(cq_sandwich(&g_ctx, &scr, bad_two_gate_step, 1, NULL, 0, &e));
}

static void a_null_compute_step_function(void)
{
    setup();
    cq_scratch scr;
    cq_scratch_alloc(&scr, 2u);
    CQ_EXPECT_ABORT(cq_sandwich(&g_ctx, &scr, NULL, 1, NULL, 0, NULL));
}

/* A copyout count with no copyout function is the same mistake in the other
 * slot, and it is the likelier one: `NULL, 0` is the legitimate spelling for
 * "no copy-out", so only the count distinguishes them. */
static void a_copyout_count_with_no_copyout_function(void)
{
    setup();
    cq_scratch scr;
    cq_scratch_alloc(&scr, 2u);
    CQ_EXPECT_ABORT(cq_sandwich(&g_ctx, &scr, noop_step, 1, NULL, 2, NULL));
}

static void a_negative_step_count(void)
{
    setup();
    cq_scratch scr;
    cq_scratch_alloc(&scr, 2u);
    CQ_EXPECT_ABORT(cq_sandwich(&g_ctx, &scr, noop_step, -1, NULL, 0, NULL));
}

/* A step that writes a CONSTANT over a scratch bit — the shape that erases a
 * qubit index irrecoverably, since a constant carries a canonical q == 0. It
 * aborts in BOTH configurations, but from DIFFERENT lines, and that is the
 * point of running both: in Debug the region fingerprint catches it after the
 * forward half; in Release, where the fingerprint is compiled out, the
 * epilogue's kind check catches it before cq_bit_qindex can read the canonical
 * zero and release someone else's qubit. Delete either line and one of the two
 * configurations goes red — which is the only reason both lines exist. */
typedef struct { cq_bit *r; } churn_env;

static void zero_a_scratch_bit_step(cq_ctx *ctx, void *env, int s)
{
    churn_env *e = env;
    (void)ctx; (void)s;
    e->r[0] = cq_bit_zero();
}

static void a_compute_step_writes_a_constant_into_scratch(void)
{
    setup();
    cq_scratch scr;
    cq_scratch_alloc(&scr, 2u);
    churn_env e = { cq_scratch_span(&scr, 0u, 2u) };

    CQ_EXPECT_ABORT(cq_sandwich(&g_ctx, &scr, zero_a_scratch_bit_step, 1, NULL, 0, &e));
}

/* --- Debug only: the I6 discipline sweeps (plan §0.2). ------------------- */

/* I6(a), risk R1, and the reason emit.c's extent check exists at all. A
 * compute-half target outside scratch is a bit whose kind the reverse pass may
 * find changed — the forward materialises it with an X (2 gates), the reverse
 * sees a qubit (1 gate), and the X is never undone. */
typedef struct { cq_bit *outside; } stray_env;

static void stray_target_step(cq_ctx *ctx, void *env, int s)
{
    stray_env *e = env;
    (void)s;
    cq_emit_x(ctx, e->outside);
}

static void a_compute_step_targets_a_bit_outside_scratch(void)
{
    setup_gate_leak_detecting();
    cq_scratch scr;
    cq_scratch_alloc(&scr, 2u);

    cq_bit outside = q_operand(0);        /* value 0, so materialising emits nothing */
    stray_env e = { &outside };

    CQ_DEATH_SKIP_WITHOUT_INVARIANTS("I6(a) extent check is Debug-gated (plan §0.2)");
    CQ_EXPECT_ABORT(cq_sandwich(&g_ctx, &scr, stray_target_step, 1, NULL, 0, &e));
}

/* THE SECOND sw_arm, which the case above cannot reach. A compute function is
 * called on both passes, so a stray target normally fires on the FORWARD one
 * and the reverse arming is never the thing that catches it — delete it and
 * every I6(a) case above stays green. A step whose behaviour depends on how
 * many times it has been called is the shape that distinguishes them, and it
 * is not a contrived one: it is a ckd.14a violation of exactly the kind the
 * arm exists to catch, and it is invisible to the forward pass. */
typedef struct { cq_bit *outside; int calls, n_forward; } late_stray_env;

static void stray_on_the_reverse_pass_step(cq_ctx *ctx, void *env, int s)
{
    late_stray_env *e = env;
    (void)s;
    if (++e->calls > e->n_forward) cq_emit_x(ctx, e->outside);
}

static void the_reverse_pass_targets_a_bit_outside_scratch(void)
{
    setup();
    cq_scratch scr;
    cq_scratch_alloc(&scr, 2u);

    cq_bit outside = q_operand(0);
    late_stray_env e = { &outside, 0, 1 };

    CQ_DEATH_SKIP_WITHOUT_INVARIANTS("I6(a) extent check is Debug-gated (plan §0.2)");
    CQ_EXPECT_ABORT(cq_sandwich(&g_ctx, &scr, stray_on_the_reverse_pass_step,
                                1, NULL, 0, &e));
}

/* The region fingerprint, and why it is ORDER-SENSITIVE. Swapping two scratch
 * bits leaves the multiset of (kind, index) pairs identical, emits nothing,
 * and keeps every bit CQ_BIT_Q — so an unordered checksum, a count, or an
 * all-Q sweep would all stay green. What it breaks is the correspondence
 * between a step index and the gate that step emits, which is precisely the
 * premise replay-in-reverse rests on. */
static void swap_region_step(cq_ctx *ctx, void *env, int s)
{
    churn_env *e = env;
    (void)ctx; (void)s;
    cq_bit tmp = e->r[0];
    e->r[0] = e->r[1];
    e->r[1] = tmp;
}

static void a_compute_step_rewrites_the_scratch_region(void)
{
    setup();
    cq_scratch scr;
    cq_scratch_alloc(&scr, 2u);
    churn_env e = { cq_scratch_span(&scr, 0u, 2u) };

    CQ_DEATH_SKIP_WITHOUT_INVARIANTS("the region fingerprint is Debug-gated (plan §0.2)");
    CQ_EXPECT_ABORT(cq_sandwich(&g_ctx, &scr, swap_region_step, 1, NULL, 0, &e));
}

/* THE HOLE THE DISARMED EXTENT LEAVES, and the reason the fingerprint is
 * checked after the copyout loop as well as after the two compute halves. The
 * I6(a) extent is deliberately off during copyout (copyout targets `dst`,
 * outside scratch), so emit.c cannot see a copyout step reaching back into the
 * region — and a scratch bit disturbed between the two compute halves breaks
 * the palindrome exactly as one disturbed inside them does. Delete the
 * SW_VERIFY after step 3 and this is the only case that goes red. */
static void a_copyout_step_rewrites_the_scratch_region(void)
{
    setup();
    cq_scratch scr;
    cq_scratch_alloc(&scr, 2u);
    churn_env e = { cq_scratch_span(&scr, 0u, 2u) };

    CQ_DEATH_SKIP_WITHOUT_INVARIANTS("the region fingerprint is Debug-gated (plan §0.2)");
    CQ_EXPECT_ABORT(cq_sandwich(&g_ctx, &scr, noop_step, 1,
                                swap_region_step, 1, &e));
}

/* THE THIRD FINGERPRINT CALL, which neither case above can reach. Measured:
 * the driver checks the fingerprint after each of its three loops, and with
 * only the two cases above, DELETING ANY ONE OF THE THREE left all 65 tests
 * green — a later call caught what the deleted one would have. That is the
 * defence-in-depth trap this project has now hit three times (plan §0, Step 6;
 * §2 deviation 5, Step 7). It is closed two ways: the two cases above carry a
 * CTest FAIL_REGULAR_EXPRESSION naming the loop that must catch them, and this
 * case reaches the last check, which has nothing after it to lean on.
 *
 * n_compute is 1 so the swap happens ONCE, on the reverse pass. With two steps
 * it would happen twice and cancel — which is worth knowing, because it is the
 * same self-inverse accident that hides the ckd.14a trap in K01. */
typedef struct { cq_bit *r; int calls, n_forward; } late_churn_env;

static void rewrite_on_the_reverse_pass_step(cq_ctx *ctx, void *env, int s)
{
    late_churn_env *e = env;
    (void)ctx; (void)s;
    if (++e->calls > e->n_forward) {
        cq_bit tmp = e->r[0];
        e->r[0] = e->r[1];
        e->r[1] = tmp;
    }
}

static void the_reverse_pass_rewrites_the_scratch_region(void)
{
    setup();
    cq_scratch scr;
    cq_scratch_alloc(&scr, 2u);
    late_churn_env e = { cq_scratch_span(&scr, 0u, 2u), 0, 1 };

    CQ_DEATH_SKIP_WITHOUT_INVARIANTS("the region fingerprint is Debug-gated (plan §0.2)");
    CQ_EXPECT_ABORT(cq_sandwich(&g_ctx, &scr, rewrite_on_the_reverse_pass_step,
                                1, NULL, 0, &e));
}

/* --- cq_ctx_release_qubit: the order IS the certificate (PRD §10). ------- */

/* RELEASE FIRST, THEN RETIRE, and this case is what makes the order
 * observable rather than merely commented. The governing rule is that a
 * certificate may only be written on a qubit that has ALREADY left data use;
 * reversed, the shadow entry of a still-live qubit would be cleared before the
 * pool ever got a chance to refuse the release — and a certified-but-live
 * qubit read as a control hits `t.unknown |= c.unknown`, so the poison stops
 * propagating and the shadow starts claiming determinate downstream of a real
 * superposition.
 *
 * The qubit here is determinate |1> and the release is UNPROVEN, so both
 * layers have something to say and only the order decides which speaks. The
 * CTest FAIL_REGULAR_EXPRESSION on M02's message is the discriminator: under
 * the correct order M03 refuses first and M02 is never reached. */
static void an_unproven_release_is_refused_before_anything_is_retired(void)
{
    setup();
    cq_bit b = q_operand(1);                 /* fresh qubit, shadow determinate 1 */

    CQ_EXPECT_ABORT(cq_ctx_release_qubit(&g_ctx, cq_bit_qindex(b), 0));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(a_nested_sandwich),
    CQ_DEATH_CASE(an_unproven_release_is_refused_before_anything_is_retired),
    CQ_DEATH_CASE(a_copyout_step_rewrites_the_scratch_region),
    CQ_DEATH_CASE(a_scratch_bit_is_one_on_entry),
    CQ_DEATH_CASE(a_scratch_bit_is_already_a_qubit_on_entry),
    CQ_DEATH_CASE(a_multi_gate_step_is_not_an_involution),
    CQ_DEATH_CASE(a_null_compute_step_function),
    CQ_DEATH_CASE(a_copyout_count_with_no_copyout_function),
    CQ_DEATH_CASE(a_negative_step_count),
    CQ_DEATH_CASE(a_compute_step_writes_a_constant_into_scratch),
    CQ_DEATH_CASE(a_compute_step_targets_a_bit_outside_scratch),
    CQ_DEATH_CASE(the_reverse_pass_targets_a_bit_outside_scratch),
    CQ_DEATH_CASE(a_compute_step_rewrites_the_scratch_region),
    CQ_DEATH_CASE(the_reverse_pass_rewrites_the_scratch_region)
)
