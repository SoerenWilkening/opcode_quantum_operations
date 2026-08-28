/* tests/test_shim_trace_death.c — the two fail-loud paths of M26's annotation
 * layer (`bd 76r`, PRD §15 D21 (c)).
 *
 * BOTH ARE CONTRACT VIOLATIONS THE VIEWER WOULD CATCH ANYWAY, AND THAT IS WHY
 * THEY ARE HERE. Handoff §9 makes a nested `op begin` and an orphan `op end`
 * fatal parse errors — the pipeline aborts citing the offending LINE, with the
 * producer long gone. Refusing at the moment the bracket is opened names the
 * entry point instead, while the stack that opened it is still standing; the
 * difference is a stack trace versus a line number in a 20,000-line file.
 *
 * (c) IS WHAT MAKES THE NESTING CASE MORE THAN HYGIENE. M06 supports nested §9
 * regions and is tested for it, and every instinct in this repository says to
 * bracket the sandwich's forward / copyout / reverse halves — which is exactly
 * the violation. D21 (c) resolves that the halves are CIRCUIT STRUCTURE, which
 * the LIBRARY narrates with its own `# STAGE: logical CX begin/end` fences, and
 * that our brackets go at the OUTERMOST entry point and nowhere else. This case
 * is that resolution with teeth.
 *
 * EVERY CASE'S FAIL_REGULAR_EXPRESSION NAMES UndefinedBehaviorSanitizer (bd u76)
 * and the layers that must NOT have spoken. CQ_EXPECT_ABORT arms a SIGABRT
 * window and cannot tell whose abort it caught; the qec sink's own die and the
 * pool's ceiling both abort with the same exit code, and both are reachable from
 * this setup.
 */

#include "cq_runtime_abi.h"
#include "cq_shim_ctx.h"
#include "cq_shim_trace.h"

#include "reg.h"
#include "sink_qec.h"

#include "support/death.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* The layer is inert unless the qec sink is bound with a trace open — that is
 * its ONE activation test — so a death case has to bind it for real. The trace
 * goes to a file rather than to stdout because a death binary's stdout is the
 * harness's. */
static void arm(const char *path)
{
    setenv("CQOPS_SINK", "qec", 1);
    setenv("CQOPS_QEC_CONFIG", CQOPS_QEC_TRACE_CONFIG, 1);
    setenv("CQOPS_QEC_PRECISION", "0", 1);
    setenv("CQOPS_QEC_TRACE", path, 1);
    (void)cq_shim_ctx();
    CQ_DEATH_REQUIRE(cq_sink_qec_trace() != NULL);
    CQ_DEATH_REQUIRE(cq_trace_open() == 0);
}

/* `cq_trace_op` is called directly rather than through two `cqrt_*` symbols,
 * and it has to be: no entry point in the shim nests, which is the property
 * under test, so the only way to reach the guard is to violate it by hand. */
static void nested_bracket(void)
{
    arm("test_shim_trace_death_nested.out");
    cq_trace_op("outer", CQ_REG_NONE, CQ_REG_NONE, CQ_REG_NONE, CQ_REG_NONE,
                CQ_REG_NONE);
    CQ_EXPECT_ABORT(cq_trace_op("inner", CQ_REG_NONE, CQ_REG_NONE, CQ_REG_NONE,
                                CQ_REG_NONE, CQ_REG_NONE));
}

/* THE OTHER DIRECTION, AND IT IS NOT THE SAME BUG. A missed `op begin` is what
 * an entry point that returns early gets wrong — `cqrt_cswap` has three exits
 * and `rail_addc` four — and it would otherwise present as the NEXT bracket
 * being closed twice, arbitrarily far downstream. */
static void unopened_bracket(void)
{
    arm("test_shim_trace_death_orphan.out");
    CQ_EXPECT_ABORT(cq_trace_end());
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(nested_bracket),
    CQ_DEATH_CASE(unopened_bracket)
)
