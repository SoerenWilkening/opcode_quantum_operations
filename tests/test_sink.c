/* Step 5's gate: M04's sink vtable dispatches, and the env-var default picks.
 *
 * The sink is the one place a gate leaves the library. Emission is a stream,
 * not a structure (Rule 13): nothing in src/ holds a circuit object, a gate
 * list or a statevector, and a gate emitted through one of these six function
 * pointers is gone from our side. What a SINK does with it is the sink's
 * business — which is exactly why tests/support/mock_sink may record the
 * stream, and why that is not a violation of Rule 13.
 *
 * The vtable shape is FROZEN here (Step 0.7). It is PRD §8's six entries and
 * no more: x, cx, ccx, ry, rz, mz. There is no `h`. cqrt_h turned out to be an
 * over-declaration — declared and defined in CQ_lang, emitted by nothing and
 * called by nothing — so it is struck from PRD §1, and §12's Grover builds H
 * out of rotations. Do not add an entry here on a guess.
 *
 * setenv/unsetenv are POSIX rather than C11. That is fine in a test; the
 * library itself reads the environment with getenv only.
 */

#include "sink.h"
#include "support/harness.h"
#include "support/mock_sink.h"

#include <stdlib.h>
#include <string.h>

/* Fresh registry + no explicit sink + no env var, so cases cannot leak into
 * each other through the process-wide selection state. */
static void reset_selection(void)
{
    cq_sink_reset();
    unsetenv("CQOPS_SINK");
}

/* -------------------------------------------------------------------------
 * Dispatch.
 * ------------------------------------------------------------------------- */

CQ_TEST(every_vtable_entry_dispatches)
{
    cq_mock m;
    cq_mock_init(&m);
    cq_sink s = cq_mock_sink(&m);

    cq_sink_x  (&s, 3u);
    cq_sink_cx (&s, 1u, 2u);
    cq_sink_ccx(&s, 4u, 5u, 6u);
    cq_sink_ry (&s, 7u, 0.25);
    cq_sink_rz (&s, 8u, -1.5);
    cq_sink_mz (&s, 9u);

    static const cq_rec want[] = {
        CQ_REC_X(3u), CQ_REC_CX(1u, 2u), CQ_REC_CCX(4u, 5u, 6u),
        CQ_REC_RY(7u, 0.25), CQ_REC_RZ(8u, -1.5), CQ_REC_MZ(9u),
    };
    if (!cq_mock_matches(&m, want, sizeof want / sizeof want[0])) {
        cq_h_fail(__FILE__, __LINE__, "recorded stream != expected");
        cq_mock_dump(&m, "actual");
    }
    CHECK_EQ(cq_mock_count(&m), 6u);

    cq_mock_dispose(&m);
}

CQ_TEST(the_user_pointer_is_threaded_to_the_right_recorder)
{
    /* Two live sinks at once. Nothing in the vtable is global, so a gate sent
     * to one must never land in the other — this is what lets a later suite
     * hold a mock while the printf sink is also configured. */
    cq_mock a, b;
    cq_mock_init(&a);
    cq_mock_init(&b);
    cq_sink sa = cq_mock_sink(&a);
    cq_sink sb = cq_mock_sink(&b);

    cq_sink_x(&sa, 1u);
    cq_sink_x(&sb, 2u);
    cq_sink_x(&sa, 3u);

    CHECK_EQ(cq_mock_count(&a), 2u);
    CHECK_EQ(cq_mock_count(&b), 1u);
    CHECK_EQ(cq_mock_at(&a, 0)->t, 1u);
    CHECK_EQ(cq_mock_at(&a, 1)->t, 3u);
    CHECK_EQ(cq_mock_at(&b, 0)->t, 2u);

    cq_mock_dispose(&a);
    cq_mock_dispose(&b);
}

CQ_TEST(angles_survive_dispatch_bit_exactly)
{
    /* The Ry sink entry stays `double` all the way down — converting to
     * whatever representation the QEC backend wants is the SINK's problem,
     * not ours (Key Prohibitions). So no rounding may happen in transit, and
     * §7's exact-multiple angle test would be meaningless if it did. */
    cq_mock m;
    cq_mock_init(&m);
    cq_sink s = cq_mock_sink(&m);

    const double angles[] = { 0.0, -0.0, 3.141592653589793, 1e-300, -1e300 };
    for (size_t i = 0; i < sizeof angles / sizeof angles[0]; i++)
        cq_sink_ry(&s, (uint32_t)i, angles[i]);

    for (size_t i = 0; i < sizeof angles / sizeof angles[0]; i++) {
        double got = cq_mock_at(&m, i)->angle;
        /* memcmp, not ==, so -0.0 vs 0.0 is caught rather than compared equal */
        if (memcmp(&got, &angles[i], sizeof got) != 0)
            cq_h_fail(__FILE__, __LINE__,
                      "angle %zu came back %.17g, sent %.17g",
                      i, got, angles[i]);
    }
    cq_mock_dispose(&m);
}

/* -------------------------------------------------------------------------
 * Selection: cqops_set_sink and the env-var default.
 * ------------------------------------------------------------------------- */

CQ_TEST(an_explicitly_set_sink_wins_over_everything)
{
    cq_mock reg, expl;
    cq_mock_init(&reg);
    cq_mock_init(&expl);
    reset_selection();

    cq_sink s_reg  = cq_mock_sink(&reg);
    cq_sink s_expl = cq_mock_sink(&expl);
    cq_sink_register("printf", &s_reg);
    setenv("CQOPS_SINK", "printf", 1);

    cqops_set_sink(&s_expl);
    CHECK(cq_sink_active() == &s_expl);

    /* and unsetting falls back to the env-var default */
    cqops_set_sink(NULL);
    CHECK(cq_sink_active() == &s_reg);

    reset_selection();
    cq_mock_dispose(&reg);
    cq_mock_dispose(&expl);
}

CQ_TEST(the_env_var_chooses_among_registered_sinks)
{
    /* PRD §8: "the default is chosen by environment variable so CQ_lang's
     * existing fixtures need no changes" — i.e. selection must work with no
     * call into the library at all. */
    cq_mock pf, ct;
    cq_mock_init(&pf);
    cq_mock_init(&ct);
    reset_selection();

    cq_sink s_pf = cq_mock_sink(&pf);
    cq_sink s_ct = cq_mock_sink(&ct);
    cq_sink_register("printf",  &s_pf);
    cq_sink_register("counter", &s_ct);

    setenv("CQOPS_SINK", "counter", 1);
    CHECK(cq_sink_active() == &s_ct);

    setenv("CQOPS_SINK", "printf", 1);
    CHECK(cq_sink_active() == &s_pf);

    /* absent: printf is the documented default (PRD §8) */
    unsetenv("CQOPS_SINK");
    CHECK(cq_sink_active() == &s_pf);

    /* empty is treated as absent, not as a sink named "" */
    setenv("CQOPS_SINK", "", 1);
    CHECK(cq_sink_active() == &s_pf);

    reset_selection();
    cq_mock_dispose(&pf);
    cq_mock_dispose(&ct);
}

CQ_TEST(registering_a_name_twice_replaces_it)
{
    cq_mock a, b;
    cq_mock_init(&a);
    cq_mock_init(&b);
    reset_selection();

    cq_sink sa = cq_mock_sink(&a);
    cq_sink sb = cq_mock_sink(&b);
    cq_sink_register("printf", &sa);
    CHECK(cq_sink_by_name("printf") == &sa);

    cq_sink_register("printf", &sb);
    CHECK(cq_sink_by_name("printf") == &sb);
    CHECK(cq_sink_by_name("nosuch") == NULL);

    reset_selection();
    cq_mock_dispose(&a);
    cq_mock_dispose(&b);
}

/* -------------------------------------------------------------------------
 * The recorder itself — the workhorse fixture for Step 6 and all of Phase B.
 * ------------------------------------------------------------------------- */

CQ_TEST(the_mock_counts_by_kind_for_check_gates)
{
    /* L4 goldens are a (NOT, CNOT, Toffoli) tuple, and harness.h's CHECK_GATES
     * takes six plain counts — so the mock only has to feed it three. */
    cq_mock m;
    cq_mock_init(&m);
    cq_sink s = cq_mock_sink(&m);

    for (uint32_t i = 0; i < 6u; i++)  cq_sink_x(&s, i);
    for (uint32_t i = 0; i < 40u; i++) cq_sink_cx(&s, i, i + 100u);
    for (uint32_t i = 0; i < 12u; i++) cq_sink_ccx(&s, i, i + 50u, i + 100u);
    cq_sink_ry(&s, 0u, 1.0);          /* rotations are not gate-count kinds */
    cq_sink_mz(&s, 0u);

    CHECK_GATES(cq_mock_count_op(&m, CQ_OP_X),
                cq_mock_count_op(&m, CQ_OP_CX),
                cq_mock_count_op(&m, CQ_OP_CCX),
                6, 40, 12);
    CHECK_EQ(cq_mock_count(&m), 60u);   /* everything, rotations included */

    cq_mock_dispose(&m);
}

CQ_TEST(the_mock_detects_a_mismatch_rather_than_waving_it_through)
{
    /* If cq_mock_matches could not fail, every Phase-B golden would be
     * vacuously green — the same reason test_harness_negative exists. */
    cq_mock m;
    cq_mock_init(&m);
    cq_sink s = cq_mock_sink(&m);

    cq_sink_x(&s, 1u);
    cq_sink_cx(&s, 2u, 3u);

    static const cq_rec right[]     = { CQ_REC_X(1u), CQ_REC_CX(2u, 3u) };
    static const cq_rec wrong_op[]  = { CQ_REC_X(1u), CQ_REC_X(3u) };
    static const cq_rec wrong_arg[] = { CQ_REC_X(1u), CQ_REC_CX(2u, 4u) };
    static const cq_rec too_short[] = { CQ_REC_X(1u) };

    CHECK(cq_mock_matches(&m, right, 2));
    CHECK(!cq_mock_matches(&m, wrong_op, 2));
    CHECK(!cq_mock_matches(&m, wrong_arg, 2));
    CHECK(!cq_mock_matches(&m, too_short, 1));   /* length counts */

    cq_mock_dispose(&m);
}

CQ_TEST(the_mock_compares_angles_bitwise_not_numerically)
{
    /* 0.0 == -0.0 is true in C, but they are different gates to emit and a
     * golden that could not tell them apart would be no golden. This pins the
     * comparison inside cq_mock_matches specifically — reading .angle back
     * through cq_mock_at would not exercise it. */
    cq_mock m;
    cq_mock_init(&m);
    cq_sink s = cq_mock_sink(&m);

    cq_sink_ry(&s, 0u, 0.0);

    static const cq_rec pos[] = { CQ_REC_RY(0u,  0.0) };
    static const cq_rec neg[] = { CQ_REC_RY(0u, -0.0) };
    CHECK(cq_mock_matches(&m, pos, 1));
    CHECK(!cq_mock_matches(&m, neg, 1));

    cq_mock_dispose(&m);
}

CQ_TEST(reset_empties_the_recorder_without_freeing_it)
{
    cq_mock m;
    cq_mock_init(&m);
    cq_sink s = cq_mock_sink(&m);

    for (uint32_t i = 0; i < 300u; i++) cq_sink_x(&s, i);   /* crosses growth */
    CHECK_EQ(cq_mock_count(&m), 300u);
    CHECK_EQ(cq_mock_at(&m, 299)->t, 299u);

    cq_mock_reset(&m);
    CHECK_EQ(cq_mock_count(&m), 0u);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_X), 0u);

    cq_sink_x(&s, 42u);                       /* still usable */
    CHECK_EQ(cq_mock_count(&m), 1u);
    CHECK_EQ(cq_mock_at(&m, 0)->t, 42u);

    cq_mock_dispose(&m);
}

CQ_TEST_MAIN(
    CQ_CASE(every_vtable_entry_dispatches),
    CQ_CASE(the_user_pointer_is_threaded_to_the_right_recorder),
    CQ_CASE(angles_survive_dispatch_bit_exactly),
    CQ_CASE(an_explicitly_set_sink_wins_over_everything),
    CQ_CASE(the_env_var_chooses_among_registered_sinks),
    CQ_CASE(registering_a_name_twice_replaces_it),
    CQ_CASE(the_mock_counts_by_kind_for_check_gates),
    CQ_CASE(the_mock_detects_a_mismatch_rather_than_waving_it_through),
    CQ_CASE(the_mock_compares_angles_bitwise_not_numerically),
    CQ_CASE(reset_empties_the_recorder_without_freeing_it)
)
