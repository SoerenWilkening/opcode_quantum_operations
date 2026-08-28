/* Step 26's vtable half: M25, the qec sink. PRD §8, §15 D19, D20, D21.
 *
 * REGISTERED ONLY WHEN THE BUILD FOUND THE QEC LIBRARY (-DCQOPS_QEC_DIR=). This
 * repository neither pins nor can build it, so the suite is opt-in for the same
 * reason L6 is: a report that cannot name the revision it ran against is not a
 * report. Without the flag CMake prints a STATUS line rather than registering a
 * suite that silently does nothing.
 *
 * THE THREE THINGS ONLY EXECUTION CAN SETTLE, and each has a case below:
 *
 *   1. THE Ry EMISSION ORDER. Ry(θ) = S·H·Rz(θ)·H·S† is a MATRIX product and a
 *      product applies its RIGHTMOST factor first, so the CIRCUIT is
 *      `sdg; h; rz; h; s`. Emitting `s` first is wrong by 6.858e-01 at θ = π/4
 *      and by 2.0 at θ = π. This project has no instrument that can see a wrong
 *      phase — the shadow models none, L1 compares values, the palindrome is
 *      order-only — so D19 requires the order be pinned by an EXECUTED check
 *      rather than by the identity as written. The instrument is the library's
 *      own trace, whose `# STAGE: logical <G> begin` lines are ordered.
 *   2. WHICH ANGLES ARE FREE. D19's cap has a failure mode at its upper end
 *      that NO VALUE CHECK CAN SEE: a nice angle that stops snapping to its
 *      nice rational round-trips to the identical double and costs a synthesised
 *      rotation instead of a Clifford. Only qec_count(QEC_GATE_T) separates
 *      them, which is the Prime Directive relocated into the angle conversion.
 *   3. THAT WE WRITE NO TEXT. §15 D21 makes the annotation the M26 shim's job
 *      and forbids it here — a `cq_sink` never sees a handle, a width or an
 *      opcode name, and a non-conformant line inside an opted-in trace is a
 *      FATAL parse error for the viewer, not a degraded render.
 */

#include "sink_qec.h"

#include "qubits.h"
#include "sink.h"
#include "support/harness.h"

#include <qec/qec.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* setenv/unsetenv are POSIX rather than C11; fine in a test, which links
 * against a real libc (tests/test_sink.c:16 makes the same call). */

static cq_qubit_pool g_pool;

static const char *TRACE_PATH = "test_sink_qec.trace";

/* Selection is the whole of "one flag", so every case goes through it rather
 * than reaching for the vtable directly. */
static qec_ctx *bind(const char *trace)
{
    setenv("CQOPS_QEC_CONFIG", CQOPS_QEC_TEST_CONFIG, 1);
    if (trace) setenv("CQOPS_QEC_TRACE", trace, 1);
    else       unsetenv("CQOPS_QEC_TRACE");

    cq_sink_qec_register();
    cqops_set_sink(cq_sink_by_name("qec"));

    cq_qubits_init(&g_pool);
    cq_sink_qec_bind(&g_pool);

    qec_ctx *c = (qec_ctx *)cq_sink_qec_handle();
    if (!c) cq_h_fail(__FILE__, __LINE__, "bind produced no qec context");
    return c;
}

static void unbind(void)
{
    cq_sink_qec_teardown();
    cq_qubits_dispose(&g_pool);
    cqops_set_sink(NULL);
    unsetenv("CQOPS_QEC_TRACE");
}

/* The ordered instrument: the `# STAGE: logical <G> begin` labels, in the order
 * the library wrote them. A gate COUNT cannot express an order, which is why
 * this reads the trace rather than qec_count. */
static int stage_labels(const char *path, char out[][8], int cap)
{
    FILE *f = fopen(path, "r");
    if (!f) { cq_h_fail(__FILE__, __LINE__, "cannot read %s", path); return 0; }

    char line[512];
    int  n = 0;
    while (n < cap && fgets(line, (int)sizeof line, f)) {
        const char *m = strstr(line, "# STAGE: logical ");
        if (!m || !strstr(line, "begin")) continue;
        m += strlen("# STAGE: logical ");
        int k = 0;
        while (k < 7 && m[k] && m[k] != ' ') { out[n][k] = m[k]; k++; }
        out[n][k] = '\0';
        n++;
    }
    fclose(f);
    return n;
}

/* -------------------------------------------------------------------------
 * The four entries that map 1:1.
 * ------------------------------------------------------------------------- */

/* x, cx, ccx and mz go straight onto qec_x, qec_cx, qec_ccx and qec_mz, all
 * uint32_t. The assertion is on the library's OWN counters rather than on a
 * recorder of ours: what is under test is that the call arrived, and a mock
 * would only prove that our vtable calls our vtable.
 *
 * A CCX AT d = 3 IS 562,564 PHYSICAL GATES against a CX's 1,830, and that gap
 * is the whole of D20's scope note — it is magic-state distillation, not a
 * defect, and it is why a Toffoli-free region at small width is inspectable
 * through the QEC repo's drawer and anything with Toffolis is not. */
CQ_TEST(the_four_direct_entries_reach_their_qec_counterparts)
{
    qec_ctx *c = bind(NULL);
    const cq_sink *s = cq_sink_active();

    uint64_t t0 = qec_total(c);
    cq_sink_x(s, 0);
    uint64_t after_x = qec_total(c);
    CHECK(after_x > t0);

    cq_sink_cx(s, 0, 1);
    uint64_t after_cx = qec_total(c);
    CHECK(after_cx - after_x > after_x - t0);

    uint64_t before_ccx = after_cx;
    cq_sink_ccx(s, 0, 1, 2);
    uint64_t ccx_cost = qec_total(c) - before_ccx;
    /* The 7-T decomposition, so it must be enormously more than a CX and must
     * be the only thing so far that produced a T. */
    CHECK(ccx_cost > 100u * (after_cx - after_x));
    CHECK(qec_count(c, QEC_GATE_T) > 0u);

    uint64_t before_mz = qec_total(c);
    cq_sink_mz(s, 0);
    CHECK(qec_total(c) > before_mz);
    CHECK(qec_count(c, QEC_GATE_MZ) > 0u);

    unbind();
}

/* THE CASE D19 ASKS FOR BY NAME. Two arms, and the second is the sharper one.
 *
 * With θ folding to the identity the Rz emits NOTHING (p = 0, and the library's
 * degenerate branch), so the trace holds exactly the four Clifford gadgets and
 * the sequence is unambiguous: Sdg, H, H, S. Emitting `s` first would give
 * S, H, H, Sdg — same multiset, same count, reversed order, and the ONLY thing
 * that can see the difference is this list.
 *
 * With θ = π/2 the Rz is itself a Clifford S, so the sequence gains a middle
 * element: Sdg, H, S, H, S. That is what pins the Rz to the INSIDE of the
 * conjugation rather than to either end. */
CQ_TEST(the_ry_conjugation_is_emitted_in_the_reverse_of_the_matrix_product)
{
    char labels[16][8];

    qec_ctx *c = bind(TRACE_PATH);
    (void)c;
    cq_sink_ry(cq_sink_active(), 0, 1e-300);   /* Rz folds away entirely */
    unbind();

    int n = stage_labels(TRACE_PATH, labels, 16);
    if (n != 4)
        cq_h_fail(__FILE__, __LINE__, "identity-Rz arm: %d stage labels, want 4", n);
    CHECK_STR_EQ(labels[0], "Sdg");
    CHECK_STR_EQ(labels[1], "H");
    CHECK_STR_EQ(labels[2], "H");
    CHECK_STR_EQ(labels[3], "S");

    c = bind(TRACE_PATH);
    (void)c;
    cq_sink_ry(cq_sink_active(), 0, 3.14159265358979323846 / 2.0);
    unbind();

    n = stage_labels(TRACE_PATH, labels, 16);
    if (n != 5)
        cq_h_fail(__FILE__, __LINE__, "Clifford-Rz arm: %d stage labels, want 5", n);
    CHECK_STR_EQ(labels[0], "Sdg");
    CHECK_STR_EQ(labels[1], "H");
    CHECK_STR_EQ(labels[2], "S");      /* the Rz, inside the conjugation */
    CHECK_STR_EQ(labels[3], "H");
    CHECK_STR_EQ(labels[4], "S");

    remove(TRACE_PATH);
}

/* THE ONLY INSTRUMENT THAT CAN SEE THE DENOMINATOR CAP'S UPPER END. §7's
 * folding rows are exact multiples of π and convert to (k, 1), which the
 * library's own Ross-Selinger degenerate branch turns into Cliffords: MEASURED
 * 2026-08-28, π·1/1 and π·1/2 cost ZERO T gates at every precision. A general
 * angle does not, and the difference is invisible to any value check because
 * both round-trip to the identical double.
 *
 * AND ONE HALF OF D19 DID NOT SURVIVE CONTACT WITH THE IMPLEMENTATION, recorded
 * here because the decision's own text asserts otherwise: fl(π/4) converts to
 * `1/4` and STILL costs 1,200 physical T at precision 20 on config_ccx.json,
 * the same as the 2^62 convergent. This driver's Lemma-7.2 branch catches
 * multiples of π/2, not of π/4. The cap band is unaffected — it was measured on
 * the CONVERSION, which is M25b's — but "π/4 becomes ONE T gate" is an
 * inference from the paper rather than a reading of this library, so the case
 * asserts only what was measured. */
CQ_TEST(section_7s_folding_rows_cost_no_t_gates_and_a_general_angle_does)
{
    qec_ctx *c = bind(NULL);
    const cq_sink *s = cq_sink_active();
    const double PI = 3.14159265358979323846264338327950288;

    cq_sink_rz(s, 0, 0.0);
    cq_sink_rz(s, 0, PI);
    cq_sink_rz(s, 0, -PI);
    cq_sink_rz(s, 0, 2.0 * PI);
    cq_sink_rz(s, 0, PI / 2.0);
    CHECK_EQ(qec_count(c, QEC_GATE_T), 0u);

    cq_sink_rz(s, 0, 0.1);
    CHECK(qec_count(c, QEC_GATE_T) > 0u);

    unbind();
}

/* D20 and D21 (b): the install hook sets BOTH modes, and it sets them from the
 * config rather than from a constant. n_logical is a CONFIG quantity whose
 * value RAISES THE DERIVED CODE DISTANCE, so a hard-coded ceiling would either
 * over-commit a fabric that cannot be built or refuse one that can. */
CQ_TEST(the_install_hook_sets_both_pool_modes_from_the_config)
{
    qec_ctx *c = bind(NULL);

    CHECK_EQ(cq_qubits_ceiling(&g_pool), qec_n_logical(c));
    CHECK(qec_n_logical(c) > 0u);
    CHECK(!cq_qubits_recycles(&g_pool));

    unbind();

    /* And a pool that never met this sink is untouched: the mode is the sink's,
     * not a change to D4. */
    cq_qubit_pool plain;
    cq_qubits_init(&plain);
    CHECK(cq_qubits_recycles(&plain));
    CHECK_EQ(cq_qubits_ceiling(&plain), 0u);
    cq_qubits_dispose(&plain);
}

/* §15 D21's prohibition, executed. Every line in the trace is the LIBRARY's —
 * gate lines, #PATCH, its own stage fences — and none is ours. The hazard is
 * concrete and cheap to hit: pointing M23's printf sink at this same stream
 * would inject `cx(q0, q1)`, which is neither a conformant gate line (those are
 * spelled `CX 0 17`) nor a conformant annotation, and the viewer's parser makes
 * an unrecognised line a FATAL error — no JSON, no HTML, no degraded render.
 * So the check is for M23's format specifically. */
CQ_TEST(the_sink_writes_no_text_of_its_own_into_the_trace)
{
    bind(TRACE_PATH);
    const cq_sink *s = cq_sink_active();
    cq_sink_x(s, 0);
    cq_sink_cx(s, 0, 1);
    cq_sink_mz(s, 1);
    cq_sink_ry(s, 0, 1e-300);
    unbind();

    FILE *f = fopen(TRACE_PATH, "r");
    if (!f) { cq_h_fail(__FILE__, __LINE__, "no trace at %s", TRACE_PATH); return; }

    /* M23's lines are `<op>(<operands>)` with the op spelled exactly like the
     * vtable entry it came from, so the detector is a PREFIX test rather than a
     * substring search. A substring search is what the first draft used and it
     * was wrong in the noisy direction: `strstr(line, "(q")` matches the
     * LIBRARY's own `# STAGE: logical X begin (q=0)`, so the case failed on
     * five conformant lines. */
    static const char *ours[] = { "x(", "cx(", "ccx(", "ry(", "rz(", "mz(" };

    char line[512];
    long n = 0;
    while (fgets(line, (int)sizeof line, f)) {
        n++;
        for (size_t i = 0; i < sizeof ours / sizeof ours[0]; i++)
            if (strncmp(line, ours[i], strlen(ours[i])) == 0)
                cq_h_fail(__FILE__, __LINE__,
                          "line %ld of the trace is in M23's format: %s", n, line);

        /* And the grammar, which is the stronger claim: every line the library
         * writes is an annotation (`#`) or a gate whose name is upper case
         * (`H 17`, `CX 0 17`). Anything we injected would be lower case, so
         * this catches a future line we have not thought of as well as the six
         * we have. */
        if (line[0] != '#' && !(line[0] >= 'A' && line[0] <= 'Z'))
            cq_h_fail(__FILE__, __LINE__,
                      "line %ld is neither an annotation nor a gate: %s", n, line);
    }
    fclose(f);
    CHECK(n > 0);
    remove(TRACE_PATH);
}

/* THE TRACE IS A NAMED `.partial` UNTIL A CLEAN TEARDOWN, so an abort() mid
 * program leaves the partial trace on disk — the same reason M23 flushes per
 * line, and the reason it is not a tmpfile(). Only the rename is conditional on
 * finishing; the bytes are always there. */
CQ_TEST(only_a_clean_teardown_renames_the_partial_trace)
{
    remove(TRACE_PATH);
    remove("test_sink_qec.trace.partial");

    bind(TRACE_PATH);
    cq_sink_x(cq_sink_active(), 0);

    FILE *partial = fopen("test_sink_qec.trace.partial", "r");
    CHECK(partial != NULL);
    if (partial) fclose(partial);
    FILE *final_early = fopen(TRACE_PATH, "r");
    CHECK(final_early == NULL);
    if (final_early) fclose(final_early);

    unbind();

    FILE *final_late = fopen(TRACE_PATH, "r");
    CHECK(final_late != NULL);
    if (final_late) fclose(final_late);
    FILE *partial_late = fopen("test_sink_qec.trace.partial", "r");
    CHECK(partial_late == NULL);
    if (partial_late) fclose(partial_late);

    remove(TRACE_PATH);
}

CQ_TEST_MAIN(
    CQ_CASE(the_four_direct_entries_reach_their_qec_counterparts),
    CQ_CASE(the_ry_conjugation_is_emitted_in_the_reverse_of_the_matrix_product),
    CQ_CASE(section_7s_folding_rows_cost_no_t_gates_and_a_general_angle_does),
    CQ_CASE(the_install_hook_sets_both_pool_modes_from_the_config),
    CQ_CASE(the_sink_writes_no_text_of_its_own_into_the_trace),
    CQ_CASE(only_a_clean_teardown_renames_the_partial_trace)
)
