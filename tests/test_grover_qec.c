/* tests/test_grover_qec.c — L7, Step 25. The OTHER half of PRD §12(3)'s
 * T-count, per PRD §15 D22 and `bd qi9`.
 *
 * §12(3) asks the counter sink for a Toffoli count and a T-count. `cq_count_t`
 * gives `7 × ccx`, ported verbatim from Bennett's `t_count`, and that is EXACT
 * only while nothing emits a rotation. Quantum-mode Grover emits nothing BUT
 * rotations to get off the classical surface — a general `Ry` is the only route
 * there is — so `cq_count_t` is a LOWER BOUND for exactly the program §12 uses
 * it on. D22 puts the missing term where §7 already puts angle representation:
 * the QEC sink, `qec_count(ctx, QEC_GATE_T)`.
 *
 * THE CLAIM THIS FILE MAKES IS NARROW AND IT IS THE ONE THAT MATTERS: the
 * missing term is ZERO for §12's program, so the pinned T-count is `7 × ccx`
 * AFTER measurement rather than by assumption. That is not a general fact about
 * rotations — `Rz(0.1)` costs 1,200 physical T on this config — it is a fact
 * about the two angles this program actually emits, and the negative control
 * below is what keeps the distinction visible.
 *
 * IT TAKES ITS ALPHABET FROM `tests/test_grover.c`, WHICH IS THE HALF THIS ONE
 * CANNOT MAKE. A `cq_sink` is handed one rotation at a time and never sees the
 * program, so nothing here can know that `{Ry(π/2), Rz(π)}` is the whole set;
 * `the_rotation_alphabet_is_exactly_ry_half_pi_and_rz_pi` is where that is
 * pinned, bitwise, including the `*_alien` counters that turn "the first angle"
 * into "every angle". Delete either file and the T-count silently becomes an
 * assumption again — which is the state `bd qi9` filed.
 *
 * WHY THE WHOLE PROGRAM IS NOT RUN THROUGH THIS SINK. `qec_n_logical` is a HARD
 * ceiling (D20) and the shipped configs carry 1..10 logical qubits, while §12's
 * W = 8 oracle peaks near a hundred; one logical Toffoli at d = 3 is 562,564
 * physical gates. NORTH_STAR condition 4 asks for a gate stream and a
 * classical-mode value, not a fault-tolerant run — condition 5 was the qec one
 * and was met at Step 26. So this file feeds the SINK the alphabet, which is
 * exactly the composition the two halves need and costs milliseconds.
 *
 * OPT-IN behind `-DCQOPS_QEC_DIR=`, for L6's reason: this repository neither
 * pins nor can build the QEC library, and a suite registered without it would
 * be a green run claiming a link it never made.
 */

#include "sink_qec.h"

#include "qubits.h"
#include "sink.h"
#include "support/harness.h"

#include <qec/qec.h>

#include <stdlib.h>

static cq_qubit_pool g_pool;

/* π to more digits than a double holds, so the constant is the compiler's
 * rounding of π and not of something else. `M_PI` is not in C11's <math.h>
 * without an extension, and test_sink_qec.c spells it out for the same reason. */
static const double PI = 3.14159265358979323846264338327950288;

static qec_ctx *bind(void)
{
    setenv("CQOPS_QEC_CONFIG", CQOPS_QEC_TEST_CONFIG, 1);
    unsetenv("CQOPS_QEC_TRACE");

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
}

/* -------------------------------------------------------------------------
 * 1. The rotation term of §12(3)'s T-count is zero.
 * ------------------------------------------------------------------------- */

/* EVERY ARM IS PAIRED WITH A "THE GATE ACTUALLY HAPPENED" CHECK, because a
 * T-count of zero is what a DELETED rotation also reports — and §7's own
 * folding rows plus D10's tolerance make silently deleting a rotation a real,
 * documented failure mode rather than a hypothetical one. `qec_total` growing
 * is what separates "Clifford" from "gone".
 *
 * `Ry` is D19's Clifford conjugation `sdg; h; rz; h; s` around the same `Rz`,
 * so its T-count is its inner `Rz`'s and its four gadgets are free — which is
 * why the two arms below are not one arm twice. */
CQ_TEST(the_rotation_alphabet_grovers_program_emits_costs_no_t_gates)
{
    qec_ctx       *c = bind();
    const cq_sink *s = cq_sink_active();
    uint64_t       t0;

    t0 = qec_total(c);
    cq_sink_ry(s, 0, PI / 2.0);                 /* cq_theta, every lane */
    CHECK(qec_total(c) > t0);
    CHECK_EQ(qec_count(c, QEC_GATE_T), 0u);

    t0 = qec_total(c);
    cq_sink_rz(s, 0, PI);                       /* cq_phi, every lane   */
    CHECK(qec_total(c) > t0);
    CHECK_EQ(qec_count(c, QEC_GATE_T), 0u);

    unbind();
}

/* -------------------------------------------------------------------------
 * 2. The negative control — a rotation OUTSIDE the alphabet does cost T.
 * ------------------------------------------------------------------------- */

/* WITHOUT THIS, CASE 1 PASSES AGAINST A BUILD WHOSE T COUNTER IS DEAD, and the
 * acceptance gate would report `7 × ccx` as exact for every program. The
 * separation is invisible to any value check: both angles round-trip to the
 * identical double through M25b's rational conversion, so `qec_count` is the
 * only instrument that can tell them apart — the Prime Directive's "a right
 * answer is not a right circuit", relocated into the T-count.
 *
 * It is also the reason this file does not simply assert `cq_count_t == 7·ccx`
 * and stop: that is an identity of the counter, true by construction, and it
 * says nothing at all about the program. */
CQ_TEST(a_rotation_outside_that_alphabet_is_not_free_so_the_counter_is_alive)
{
    qec_ctx       *c = bind();
    const cq_sink *s = cq_sink_active();

    CHECK_EQ(qec_count(c, QEC_GATE_T), 0u);
    cq_sink_rz(s, 0, 0.1);
    CHECK(qec_count(c, QEC_GATE_T) > 0u);

    unbind();
}

/* -------------------------------------------------------------------------
 * 3. The Toffoli IS the program's only T source, which is what `7 × ccx` says.
 * ------------------------------------------------------------------------- */

/* THE THIRD OPERAND OF D22's ARGUMENT. Case 1 says the rotations contribute
 * nothing; this says the Toffolis contribute something, so `7 × ccx` is not
 * accidentally right by everything being zero. `x` and `cx` are Clifford and
 * cost nothing, which is the other half of Bennett's `t_count` formula and is
 * asserted rather than assumed for the same reason.
 *
 * The number is NOT pinned here and must not be: it is a property of the loaded
 * config's distillation, not of this project, and pinning it would make a
 * libcqops suite red when the QEC repo re-tunes a factory. What is pinned is
 * the SIGN — zero for the Cliffords, non-zero for the Toffoli. */
CQ_TEST(the_toffoli_is_the_only_thing_in_the_program_that_costs_t)
{
    qec_ctx       *c = bind();
    const cq_sink *s = cq_sink_active();

    cq_sink_x(s, 0);
    cq_sink_cx(s, 0, 1);
    CHECK_EQ(qec_count(c, QEC_GATE_T), 0u);

    cq_sink_ccx(s, 0, 1, 2);
    CHECK(qec_count(c, QEC_GATE_T) > 0u);

    unbind();
}

CQ_TEST_MAIN(
    CQ_CASE(the_rotation_alphabet_grovers_program_emits_costs_no_t_gates),
    CQ_CASE(a_rotation_outside_that_alphabet_is_not_free_so_the_counter_is_alive),
    CQ_CASE(the_toffoli_is_the_only_thing_in_the_program_that_costs_t)
)
