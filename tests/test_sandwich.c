/* tests/test_sandwich.c — M09, Step 8. The forward–copyout–reverse driver.
 *
 * Step 8's gate (plan §4): the driver runs compute forwards, copyout, then
 * compute backwards; a synthetic step function's recorded gate stream is a
 * PALINDROME around the copyout; and an I6 violation is caught in Debug (that
 * last one lives in test_sandwich_death.c, since a process that aborts cannot
 * also run the cases here).
 *
 * WHY THE PALINDROME IS THE ASSERTION AND THE VALUE IS NOT. The Prime
 * Directive: the shadow can be exactly right while the circuit is wrong. Risk
 * R8's measured witness is that replaying K12's forward list in reverse under
 * the pre-I6(b) rules yields a DIFFERENT gate multiset with the IDENTICAL
 * total — L1 green, L4 green, scratch dirty. Only an ordered check of the
 * emitted stream, or the pool, can see that. So every case below that could be
 * satisfied by a right value also asserts the stream or the pool.
 *
 * ONE GATE PER STEP is the driver's contract (ckd.14a, plan §0.1) and it is
 * forced, not chosen: the reverse pass re-calls compute(env, s) with the SAME
 * argument, so a step undoes itself only if it is an involution, and a
 * multi-gate block generally is not. The demo kernel here honours it. The two
 * cases that deliberately BREAK it —
 * `a_multi_gate_step_is_not_an_involution_and_the_retire_detector_says_so`
 * (in the death suite) and
 * `the_palindrome_check_has_teeth_where_the_retire_detector_is_blind` (below)
 * — are the ckd.14a witnesses in miniature, and between them they pin exactly
 * which detector covers which surface.
 */

#include "sandwich.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "qubits.h"
#include "reg.h"
#include "scratch.h"
#include "shadow.h"

#include "support/harness.h"
#include "support/mock_sink.h"

typedef struct { cq_ctx ctx; cq_mock mock; cq_sink sink; } fixture;

static void fx_open(fixture *f)
{
    cq_mock_init(&f->mock);
    f->sink = cq_mock_sink(&f->mock);
    cq_ctx_init(&f->ctx, &f->sink);
}

static void fx_close(fixture *f)
{
    cq_ctx_dispose(&f->ctx);
    cq_mock_dispose(&f->mock);
}

/* A quantum operand bit holding a definite classical value: materialise, then
 * X if it should be 1. This is what a kernel's sources look like once anything
 * upstream has tainted them, and it is the only way to get a Q bit whose
 * shadow is determinate — which is what the L1-shaped case below needs. */
static cq_bit q_operand(fixture *f, int value)
{
    cq_bit b = cq_bit_zero();
    cq_materialise(&f->ctx, &b);
    if (value) cq_emit_x(&f->ctx, &b);
    return b;
}

/* ------------------------------------------------------------------------
 * The demo kernel: dst ^= (a & b) ^ a, i.e. dst ^= a & ~b, over W bits.
 *
 * Chosen because it exercises both CCX and CX, needs W scratch bits, and its
 * compute half is 2W steps of exactly one self-inverse gate each — so it is a
 * legitimate one-gate-per-step kernel and its reverse half must cancel.
 * ------------------------------------------------------------------------ */

#define DEMO_W 4

typedef struct {
    const cq_bit *a, *b;
    cq_bit       *scr;
    cq_bit       *dst;
    int           w;
    uint32_t      scr_index[DEMO_W];   /* captured at step 0; see below */
    int           captured;
} demo_env;

static void demo_compute(cq_ctx *ctx, void *env, int s)
{
    demo_env *e = env;

    /* Step 0 is the first thing that runs after pre-materialisation, so it is
     * where the region's qubit indices become observable to the test at all —
     * the driver allocates them and the caller never sees the call. The later
     * cases need them to check that they came back and were retired. */
    if (!e->captured) {
        for (int i = 0; i < e->w; i++)
            e->scr_index[i] = cq_bit_qindex(e->scr[i]);
        e->captured = 1;
    }

    if (s < e->w) cq_emit_ccx(ctx, &e->a[s], &e->b[s], &e->scr[s]);
    else          cq_emit_cx(ctx, &e->a[s - e->w], &e->scr[s - e->w]);
}

static void demo_copyout(cq_ctx *ctx, void *env, int s)
{
    demo_env *e = env;
    cq_emit_cx(ctx, &e->scr[s], &e->dst[s]);
}

/* Runs the demo kernel over `a_val`/`b_val`. `poison` makes the sources
 * `unknown`, which is what every real operand downstream of a rotation looks
 * like. Leaves the scratch region allocated-and-disposed and `dst` live. */
static void run_demo(fixture *f, cq_scratch *scr, demo_env *e,
                     cq_bit *a, cq_bit *b, cq_bit *dst,
                     unsigned a_val, unsigned b_val, int poison)
{
    for (int i = 0; i < DEMO_W; i++) {
        a[i] = q_operand(f, (int)((a_val >> i) & 1u));
        b[i] = q_operand(f, (int)((b_val >> i) & 1u));
        if (poison) {
            cq_shadow_rotate(&f->ctx.shadow, cq_bit_qindex(a[i]));
            cq_shadow_rotate(&f->ctx.shadow, cq_bit_qindex(b[i]));
        }
        dst[i] = cq_bit_zero();
    }

    cq_scratch_alloc(scr, (uint32_t)DEMO_W);

    e->a = a; e->b = b; e->dst = dst; e->w = DEMO_W; e->captured = 0;
    e->scr = cq_scratch_span(scr, 0u, (uint32_t)DEMO_W);

    cq_mock_reset(&f->mock);      /* operand setup is not part of the count */
    cq_sandwich(&f->ctx, scr, demo_compute, 2 * DEMO_W, demo_copyout, DEMO_W, e);
}

/* ------------------------------------------------------------------------
 * 1. The driver's control flow.
 * ------------------------------------------------------------------------ */

typedef struct { int seq[32]; int n; } log_env;

static void log_compute(cq_ctx *ctx, void *env, int s)
{
    (void)ctx;
    log_env *e = env;
    e->seq[e->n++] = s;
}

static void log_copyout(cq_ctx *ctx, void *env, int s)
{
    (void)ctx;
    log_env *e = env;
    e->seq[e->n++] = 100 + s;
}

CQ_TEST(compute_forwards_then_copyout_then_compute_backwards)
{
    fixture f; fx_open(&f);
    cq_scratch scr;
    cq_scratch_alloc(&scr, 2u);

    log_env e = { { 0 }, 0 };
    cq_sandwich(&f.ctx, &scr, log_compute, 3, log_copyout, 2, &e);

    static const int want[] = { 0, 1, 2, 100, 101, 2, 1, 0 };
    CHECK_EQ(e.n, (int)(sizeof want / sizeof want[0]));
    for (int i = 0; i < e.n && i < (int)(sizeof want / sizeof want[0]); i++)
        CHECK_EQ(e.seq[i], want[i]);

    cq_scratch_dispose(&scr);
    fx_close(&f);
}

/* I6(b), and the two halves of its cost. Pre-materialisation costs QUBITS and
 * never gates, because scratch is born BIT_ZERO and cq_materialise emits an X
 * only for a constant 1 (PRD §5). A sandwich whose steps emit nothing must
 * therefore emit nothing at all — and still take, and give back, the region. */
CQ_TEST(pre_materialisation_costs_qubits_and_never_gates)
{
    fixture f; fx_open(&f);
    cq_scratch scr;
    cq_scratch_alloc(&scr, 5u);

    log_env e = { { 0 }, 0 };
    cq_sandwich(&f.ctx, &scr, log_compute, 0, log_copyout, 0, &e);

    CHECK_EQ(cq_mock_count(&f.mock), 0u);
    CHECK_EQ(cq_qubits_peak(&f.ctx.pool), 5u);
    CHECK_EQ(cq_qubits_live(&f.ctx.pool), 0u);
    CHECK_EQ(cq_qubits_free(&f.ctx.pool), 5u);

    cq_scratch_dispose(&scr);           /* every bit back to CQ_BIT_ZERO */
    fx_close(&f);
}

/* ------------------------------------------------------------------------
 * 2. The palindrome — Step 8's headline assertion.
 * ------------------------------------------------------------------------ */

CQ_TEST(the_recorded_stream_is_a_palindrome_around_the_copyout)
{
    fixture f; fx_open(&f);
    cq_scratch scr; demo_env e;
    cq_bit a[DEMO_W], b[DEMO_W], dst[DEMO_W];

    run_demo(&f, &scr, &e, a, b, dst, 0xBu, 0x6u, 0);

    /* 2W compute gates, W copyout gates, 2W again. Every one of them is a
     * gate: with all-quantum operands no fold fires, and pre-materialisation
     * emits nothing. */
    CHECK_EQ(cq_mock_count(&f.mock), (size_t)(5 * DEMO_W));
    CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_CCX), (size_t)(2 * DEMO_W));
    CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_CX), (size_t)(3 * DEMO_W));
    CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_X), (size_t)0);

    if (!cq_mock_is_palindrome(&f.mock, (size_t)(2 * DEMO_W), (size_t)DEMO_W)) {
        CHECK(0 && "compute half is not replayed exactly in reverse");
        cq_mock_dump(&f.mock, "sandwich stream");
    }

    cq_scratch_dispose(&scr);
    fx_close(&f);
}

/* L1-shaped, and it is the WEAKEST case in this file — it would stay green
 * through a leaked ancilla (the Prime Directive). It is here because a driver
 * that emitted a perfect palindrome of the WRONG gates would pass everything
 * above. Determinate sources, so the shadow carries the value. */
CQ_TEST(the_kernel_computes_a_and_not_b)
{
    fixture f; fx_open(&f);
    cq_scratch scr; demo_env e;
    cq_bit a[DEMO_W], b[DEMO_W], dst[DEMO_W];

    run_demo(&f, &scr, &e, a, b, dst, 0xBu, 0x6u, 0);

    unsigned got = 0;
    for (int i = 0; i < DEMO_W; i++) {
        CHECK(cq_bit_is_qubit(dst[i]));
        cq_shadow s = cq_shadow_get(&f.ctx.shadow, cq_bit_qindex(dst[i]));
        CHECK_EQ(s.unknown, 0);
        got |= (unsigned)(s.value & 1u) << i;
    }
    CHECK_EQ(got, 0xBu & ~0x6u & 0xFu);      /* 0b1011 & 0b1001 = 0b1001 */

    cq_scratch_dispose(&scr);
    fx_close(&f);
}

#include "test_sandwich_certificate.inc"

/* ------------------------------------------------------------------------
 * 4. The extent is armed for the compute halves ONLY.
 * ------------------------------------------------------------------------ */

/* The hallucination-risk callout in CLAUDE.md, made into a test. Copyout
 * targets `dst`, which is OUTSIDE scratch; an extent armed across all three
 * loops makes emit.c's I6(a) check fire on every sandwich kernel there is.
 * The plausible wrong fix — widening the extent to cover `dst` — silently
 * disables I6(a) for the compute halves too, which is R1 with the detector
 * removed, and is what the death suite's
 * `compute_step_targets_a_bit_outside_scratch` case exists to catch.
 *
 * Every demo case above already depends on this; it is named here so that a
 * regression reports the cause rather than five unrelated failures. */
CQ_TEST(the_copyout_may_target_a_bit_outside_the_scratch_region)
{
    fixture f; fx_open(&f);
    cq_scratch scr; demo_env e;
    cq_bit a[DEMO_W], b[DEMO_W], dst[DEMO_W];

    run_demo(&f, &scr, &e, a, b, dst, 0x5u, 0x0u, 0);   /* survives => armed right */

    for (int i = 0; i < DEMO_W; i++) CHECK(cq_bit_is_qubit(dst[i]));

    cq_scratch_dispose(&scr);
    fx_close(&f);
}

#if defined(CQOPS_DEBUG_INVARIANTS) && CQOPS_DEBUG_INVARIANTS
/* The extent must be disarmed on the way out, or the next unrelated gate in
 * the program aborts. Reads the Debug-only field directly, which is what a
 * test in this repo may do and nothing in src/ outside M09 may. */
CQ_TEST(the_extent_is_disarmed_when_the_driver_returns)
{
    fixture f; fx_open(&f);
    cq_scratch scr; demo_env e;
    cq_bit a[DEMO_W], b[DEMO_W], dst[DEMO_W];

    CHECK(f.ctx.scratch_lo == NULL);
    run_demo(&f, &scr, &e, a, b, dst, 0x3u, 0x1u, 0);
    CHECK(f.ctx.scratch_lo == NULL);
    CHECK(f.ctx.scratch_hi == NULL);

    /* A gate on a bit that has nothing to do with scratch now goes through. */
    cq_emit_x(&f.ctx, &dst[0]);

    cq_scratch_dispose(&scr);
    fx_close(&f);
}
#endif

/* ------------------------------------------------------------------------
 * 5. Which detector covers which surface (PRD §10).
 * ------------------------------------------------------------------------ */

/* A two-gate step, which is NOT an involution: applying
 *   CCX(a, p, t) ; CX(b, p)
 * twice leaves t ^= a&b rather than t unchanged. This is ckd.14a's K06 trap in
 * miniature, and the death suite pins that cq_shadow_retire catches it when
 * the sources are determinate.
 *
 * HERE THE SOURCES ARE POISONED, and PRD §10 says exactly what follows: the
 * retire detector is "inert on the L6 corpus, where nearly every rail is
 * rotation-tainted, so never report an L6 run as evidence that the certificate
 * held. The ordered-stream palindrome check in mock_sink is the only thing
 * with teeth on the poisoned surface." This case is that sentence, executable:
 * the library accepts the sandwich without a murmur, and the recorded stream
 * is the only thing that knows it did not cancel. */
typedef struct { const cq_bit *a, *b; cq_bit *p, *t; } bad_env;

static void bad_two_gate_step(cq_ctx *ctx, void *env, int s)
{
    bad_env *e = env;
    (void)s;
    cq_emit_ccx(ctx, e->a, e->p, e->t);
    cq_emit_cx(ctx, e->b, e->p);
}

CQ_TEST(the_palindrome_check_has_teeth_where_the_retire_detector_is_blind)
{
    fixture f; fx_open(&f);
    cq_scratch scr;
    cq_scratch_alloc(&scr, 2u);

    cq_bit a = q_operand(&f, 1), b = q_operand(&f, 1);
    cq_shadow_rotate(&f.ctx.shadow, cq_bit_qindex(a));
    cq_shadow_rotate(&f.ctx.shadow, cq_bit_qindex(b));

    bad_env e;
    e.a = &a; e.b = &b;
    e.p = cq_scratch_span(&scr, 0u, 1u);
    e.t = cq_scratch_span(&scr, 1u, 1u);

    cq_mock_reset(&f.mock);
    cq_sandwich(&f.ctx, &scr, bad_two_gate_step, 1, NULL, 0, &e);   /* no abort */

    /* Four gates: CCX, CX forwards and CCX, CX again. A palindrome would need
     * CX, CCX on the way back. Nothing in src/ can tell — this is the test
     * side of the vtable, where Rule 13 permits the recording. */
    CHECK_EQ(cq_mock_count(&f.mock), (size_t)4);
    CHECK(!cq_mock_is_palindrome(&f.mock, (size_t)2, (size_t)0));

    cq_scratch_dispose(&scr);
    fx_close(&f);
}

CQ_TEST_MAIN(
    CQ_CASE(compute_forwards_then_copyout_then_compute_backwards),
    CQ_CASE(pre_materialisation_costs_qubits_and_never_gates),
    CQ_CASE(the_recorded_stream_is_a_palindrome_around_the_copyout),
    CQ_CASE(the_kernel_computes_a_and_not_b),
    CQ_CASE(the_scratch_qubits_come_back_and_the_bits_are_zero_again),
    CQ_CASE(the_released_indices_are_retired_and_the_live_ones_are_not),
    CQ_CASE(retirement_clears_a_frozen_value_byte_not_just_the_poison),
    CQ_CASE(freeing_a_rail_retires_its_shadow_entries_too),
    CQ_CASE(a_second_sandwich_reuses_the_same_indices_in_the_same_order),
    CQ_CASE(the_copyout_may_target_a_bit_outside_the_scratch_region),
#if defined(CQOPS_DEBUG_INVARIANTS) && CQOPS_DEBUG_INVARIANTS
    CQ_CASE(the_extent_is_disarmed_when_the_driver_returns),
#endif
    CQ_CASE(the_palindrome_check_has_teeth_where_the_retire_detector_is_blind)
)
