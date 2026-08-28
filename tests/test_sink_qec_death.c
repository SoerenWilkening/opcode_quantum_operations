/* M25's fail-loud paths, Step 26.
 *
 * ALL FOUR ABORT IN BOTH CONFIGURATIONS, and the first is the one with teeth.
 *
 * `qec_ccx` REFUSES COINCIDENT OPERANDS ITSELF — "a, b, c must be three
 * DISTINCT logical qubits (Rule 1: a coincident operand is a degenerate,
 * meaningless channel — fail loud)" — while OUR §3 distinctness asserts are
 * Debug-gated. CLAUDE.md records a MEASURED Release run in which a D7b alias
 * produced `ccx q0 q0 q2`, a Toffoli whose two controls are one physical qubit,
 * with a right value and no diagnostic anywhere. Under this sink that stops
 * being silent — but ONLY because the −1 return is checked. Every qec_* returns
 * int where our vtable returns void, and dropping that int would put the
 * refusal back to silent in exactly the configuration where nothing else is
 * looking.
 */

#include "sink_qec.h"

#include "qubits.h"
#include "sink.h"

#include "support/death.h"

#include <stdlib.h>

static cq_qubit_pool g_pool;

static const cq_sink *bind(void)
{
    setenv("CQOPS_QEC_CONFIG", CQOPS_QEC_TEST_CONFIG, 1);
    unsetenv("CQOPS_QEC_TRACE");
    cq_sink_qec_register();
    cqops_set_sink(cq_sink_by_name("qec"));
    cq_qubits_init(&g_pool);
    cq_sink_qec_bind(&g_pool);
    return cq_sink_active();
}

static void a_coincident_toffoli(void)
{
    const cq_sink *s = bind();
    CQ_EXPECT_ABORT(cq_sink_ccx(s, 0, 0, 2));
}

/* A gate reaching the vtable before the install hook ran. The `user` pointer is
 * NULL then, and jumping through it would be undefined behaviour rather than a
 * diagnostic — src/sink.h's own reason for dispatching through a function. */
static void a_gate_before_the_bind(void)
{
    cq_sink_qec_register();
    const cq_sink *s = cq_sink_by_name("qec");
    CQ_DEATH_REQUIRE(s != NULL);
    CQ_EXPECT_ABORT(cq_sink_x(s, 0));
}

/* No config to create a context from. Refusing here rather than substituting a
 * default is M04's posture: a sink quietly standing in for another hands the
 * caller a circuit they did not ask for, and a default config would silently
 * pick a code distance and an n_logical. */
static void a_bind_without_a_config(void)
{
    unsetenv("CQOPS_QEC_CONFIG");
    cq_sink_qec_register();
    cqops_set_sink(cq_sink_by_name("qec"));
    cq_qubits_init(&g_pool);
    CQ_EXPECT_ABORT(cq_sink_qec_bind(&g_pool));
}

/* ε = 2^−precision must stay well above the CONVERSION's own floor of about
 * π/CQ_QEC_DENOM_CAP ≈ 2.9e-12 rad, or the accuracy would be set by our
 * rational approximation rather than by the synthesis — silently, since both
 * produce a perfectly ordinary gate string. Refused rather than delivered. */
static void a_precision_above_the_conversions_floor(void)
{
    setenv("CQOPS_QEC_CONFIG", CQOPS_QEC_TEST_CONFIG, 1);
    setenv("CQOPS_QEC_PRECISION", "48", 1);
    cq_sink_qec_register();
    cqops_set_sink(cq_sink_by_name("qec"));
    cq_qubits_init(&g_pool);
    CQ_EXPECT_ABORT(cq_sink_qec_bind(&g_pool));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(a_coincident_toffoli),
    CQ_DEATH_CASE(a_gate_before_the_bind),
    CQ_DEATH_CASE(a_bind_without_a_config),
    CQ_DEATH_CASE(a_precision_above_the_conversions_floor)
)
