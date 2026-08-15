/* Step 9's second half: M24 counts, and counts the right thing into the right
 * bucket.
 *
 * This module is the mechanism behind every L4 gate-count golden in Phase B
 * (bd 6n0), which makes a wrong count here a wrong golden in eleven kernels at
 * once — and a wrong golden does not look wrong. So the cases below are chosen
 * against one question: which single edit to sink_count.c does this case, and
 * only this case, catch?
 *
 *   - SIX DISTINCT TOTALS. Every op is emitted a different number of times, so
 *     swapping any two buckets changes at least one field. Equal counts would
 *     let a `c->cx++` in the ccx entry pass silently, which is the exact bug
 *     that produces a plausible L4 golden for a circuit nobody built.
 *   - `total` EXCLUDES ROTATIONS. Nothing else in the suite can see it: the
 *     six fields are all correct in a build where cq_count_total sums all six.
 *   - RESET CLEARS ALL SIX. Checking `total` after a reset would miss ry, rz
 *     and mz entirely — the fields total deliberately does not read.
 *
 * The cross-check against mock_sink is bd 6n0's literal acceptance criterion
 * ("counter totals match the mock sink's stream"). It is worth having and it
 * is worth being honest about: the two count the same stream by independent
 * routes — mock_sink buckets a recorded cq_rec after the fact, M24 increments
 * at the call — so it catches a mis-wired vtable, and it cannot catch a
 * misconception both files share.
 */

#include "sink.h"
#include "sink_count.h"
#include "support/harness.h"
#include "support/mock_sink.h"

#include <stdlib.h>

/* Six DIFFERENT counts, so no permutation of the buckets survives. */
#define N_X   6u
#define N_CX  40u
#define N_CCX 12u
#define N_RY  3u
#define N_RZ  5u
#define N_MZ  2u

static void emit_the_mixed_stream(const cq_sink *s)
{
    for (uint32_t i = 0; i < N_X;   i++) cq_sink_x  (s, i);
    for (uint32_t i = 0; i < N_CX;  i++) cq_sink_cx (s, i, i + 100u);
    for (uint32_t i = 0; i < N_CCX; i++) cq_sink_ccx(s, i, i + 50u, i + 100u);
    for (uint32_t i = 0; i < N_RY;  i++) cq_sink_ry (s, i, 0.25);
    for (uint32_t i = 0; i < N_RZ;  i++) cq_sink_rz (s, i, -0.5);
    for (uint32_t i = 0; i < N_MZ;  i++) cq_sink_mz (s, i);
}

CQ_TEST(every_entry_counts_into_its_own_bucket)
{
    cq_counter c;
    cq_count_reset(&c);
    cq_sink s = cq_sink_counter(&c);

    emit_the_mixed_stream(&s);

    CHECK_EQ(c.x,   N_X);
    CHECK_EQ(c.cx,  N_CX);
    CHECK_EQ(c.ccx, N_CCX);
    CHECK_EQ(c.ry,  N_RY);
    CHECK_EQ(c.rz,  N_RZ);
    CHECK_EQ(c.mz,  N_MZ);
}

CQ_TEST(total_is_the_bennett_triple_and_excludes_rotations)
{
    /* Bennett's gate_count is (total, NOT, CNOT, Toffoli) with total the
     * redundant sum of the three (diagnostics.jl:21-26). Bennett circuits
     * contain no Ry, Rz or Mz at all, so folding ours into the sum would break
     * the very comparison PRD §8 asks this sink to make possible — and would
     * break it only once §7 rotations start firing, i.e. long after the
     * goldens were pinned. */
    cq_counter c;
    cq_count_reset(&c);
    cq_sink s = cq_sink_counter(&c);

    emit_the_mixed_stream(&s);

    CHECK_EQ(cq_count_total(&c), N_X + N_CX + N_CCX);
    CHECK(cq_count_total(&c) != N_X + N_CX + N_CCX + N_RY + N_RZ + N_MZ);

    /* And the three-tuple itself, through the assertion Phase B will use. */
    CHECK_GATES(c.x, c.cx, c.ccx, N_X, N_CX, N_CCX);
}

CQ_TEST(t_count_is_seven_per_toffoli)
{
    /* Verbatim from Bennett's t_count (diagnostics.jl:122): NOT and CNOT are
     * Clifford and contribute nothing. */
    cq_counter c;
    cq_count_reset(&c);
    cq_sink s = cq_sink_counter(&c);

    emit_the_mixed_stream(&s);
    CHECK_EQ(cq_count_t(&c), 7u * N_CCX);

    cq_count_reset(&c);
    CHECK_EQ(cq_count_t(&c), 0u);

    cq_sink_x(&s, 0u);
    cq_sink_cx(&s, 0u, 1u);
    CHECK_EQ(cq_count_t(&c), 0u);            /* Clifford contributes nothing */
}

CQ_TEST(reset_clears_every_field_not_just_the_summed_ones)
{
    cq_counter c;
    cq_count_reset(&c);
    cq_sink s = cq_sink_counter(&c);

    emit_the_mixed_stream(&s);
    cq_count_reset(&c);

    /* Each field named individually. cq_count_total does not read ry, rz or
     * mz, so asserting the total alone would leave three fields unchecked. */
    CHECK_EQ(c.x,   0u);
    CHECK_EQ(c.cx,  0u);
    CHECK_EQ(c.ccx, 0u);
    CHECK_EQ(c.ry,  0u);
    CHECK_EQ(c.rz,  0u);
    CHECK_EQ(c.mz,  0u);
    CHECK_EQ(cq_count_total(&c), 0u);

    cq_sink_x(&s, 1u);                       /* still usable afterwards */
    CHECK_EQ(c.x, 1u);
}

CQ_TEST(two_counters_do_not_share_state)
{
    /* Nothing in the vtable is global; the user pointer is what carries the
     * identity. A counter that incremented a file-static would pass every
     * other case in this file. */
    cq_counter a, b;
    cq_count_reset(&a);
    cq_count_reset(&b);
    cq_sink sa = cq_sink_counter(&a);
    cq_sink sb = cq_sink_counter(&b);

    cq_sink_x(&sa, 1u);
    cq_sink_x(&sb, 2u);
    cq_sink_x(&sa, 3u);
    cq_sink_ccx(&sb, 1u, 2u, 3u);

    CHECK_EQ(a.x, 2u);
    CHECK_EQ(a.ccx, 0u);
    CHECK_EQ(b.x, 1u);
    CHECK_EQ(b.ccx, 1u);
}

CQ_TEST(the_counter_agrees_with_the_mock_sinks_recorded_stream)
{
    /* bd 6n0's acceptance criterion. Two independent routes to the same
     * numbers: mock_sink buckets a recorded cq_rec after the fact, M24
     * increments at the call. */
    cq_counter c;
    cq_mock    m;
    cq_count_reset(&c);
    cq_mock_init(&m);

    cq_sink sc = cq_sink_counter(&c);
    cq_sink sm = cq_mock_sink(&m);

    emit_the_mixed_stream(&sc);
    emit_the_mixed_stream(&sm);

    CHECK_EQ(c.x,   cq_mock_count_op(&m, CQ_OP_X));
    CHECK_EQ(c.cx,  cq_mock_count_op(&m, CQ_OP_CX));
    CHECK_EQ(c.ccx, cq_mock_count_op(&m, CQ_OP_CCX));
    CHECK_EQ(c.ry,  cq_mock_count_op(&m, CQ_OP_RY));
    CHECK_EQ(c.rz,  cq_mock_count_op(&m, CQ_OP_RZ));
    CHECK_EQ(c.mz,  cq_mock_count_op(&m, CQ_OP_MZ));

    /* cq_mock_count is EVERY record, rotations included — which is exactly
     * what cq_count_total is not. The two numbers differing is the point. */
    CHECK_EQ(cq_mock_count(&m), N_X + N_CX + N_CCX + N_RY + N_RZ + N_MZ);
    CHECK_EQ(cq_count_total(&c), N_X + N_CX + N_CCX);

    cq_mock_dispose(&m);
}

CQ_TEST(registering_makes_counter_selectable_by_environment)
{
    cq_sink_reset();
    unsetenv("CQOPS_SINK");

    cq_counter *g = cq_sink_counter_register();
    CHECK(g != NULL);
    CHECK(cq_sink_by_name("counter") != NULL);

    setenv("CQOPS_SINK", "counter", 1);
    const cq_sink *s = cq_sink_active();
    CHECK(s == cq_sink_by_name("counter"));

    cq_count_reset(g);
    cq_sink_ccx(s, 1u, 2u, 3u);
    CHECK_EQ(g->ccx, 1u);
    CHECK_EQ(cq_count_t(g), 7u);

    unsetenv("CQOPS_SINK");
    cq_sink_reset();
}

CQ_TEST_MAIN(
    CQ_CASE(every_entry_counts_into_its_own_bucket),
    CQ_CASE(total_is_the_bennett_triple_and_excludes_rotations),
    CQ_CASE(t_count_is_seven_per_toffoli),
    CQ_CASE(reset_clears_every_field_not_just_the_summed_ones),
    CQ_CASE(two_counters_do_not_share_state),
    CQ_CASE(the_counter_agrees_with_the_mock_sinks_recorded_stream),
    CQ_CASE(registering_makes_counter_selectable_by_environment)
)
