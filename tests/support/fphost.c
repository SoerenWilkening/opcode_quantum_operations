/* tests/support/fphost.c — the ten arms. See fphost.h for why any of this
 * exists and for the correction to §7.4's sNaN row.
 *
 * TWO CALLERS, ONE COPY, AND THAT IS DELIBERATE. cmake/CqopsFpHost.cmake
 * compiles THIS FILE at configure time against a generated main(), rather than
 * carrying its own transcription of the arms. A second transcription is the
 * defect this repository keeps paying for — a comment, a K-doc or a probe that
 * drifts from the code it describes and is caught by nothing, because no test
 * reads a comment. What the two callers do NOT share is the compile line: the
 * probe builds with no -O flag and no sanitizer, the test binary with the
 * configuration's own. test_skeleton asserts they agree, so an optimisation
 * level that moved a cell is caught rather than assumed.
 *
 * EVERY OPERAND IS `volatile`, AND THAT IS THE MEASUREMENT. Without it the
 * compiler folds `inf - inf` and `DBL_MIN / 2` at translation time, under ITS
 * model of the target, and the probe reports on the compiler instead of on the
 * machine the tests will run on. -ffp-contract=off (root CMakeLists.txt) is the
 * other half of the same posture.
 */

#include "support/fphost.h"

#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The check reads the rounding mode and does arithmetic that a caller may have
 * changed the mode under (test_skeleton's negative control does exactly that).
 * Guarded on __clang__: gcc answers this pragma with "not supported, ignoring"
 * and -Werror turns that into a build failure. */
#if defined(__clang__)
#  pragma STDC FENV_ACCESS ON
#endif

uint64_t cq_fphost_bits(double d)
{
    uint64_t u;
    memcpy(&u, &d, sizeof u);
    return u;
}

double cq_fphost_from_bits(uint64_t u)
{
    double d;
    memcpy(&d, &u, sizeof d);
    return d;
}

static int cq_fphost_arms;

int cq_fphost_arms_run(void) { return cq_fphost_arms; }

/* Records the first failing arm and returns 0, so every arm below is one line
 * of the form `if (bad) return no(...)`. */
static int no(char *why, size_t cap, const char *fmt, ...)
{
    va_list ap;

    if (why != NULL && cap > 0) {
        va_start(ap, fmt);
        (void)vsnprintf(why, cap, fmt, ap);
        va_end(ap);
    }
    return 0;
}

/* One §7.4 cell: `got` must be `want`, bitwise. Bitwise is the whole point —
 * every NaN compares unequal to itself with ==, and two NaNs with different
 * payloads are the SAME value to C and different bit patterns to the oracle. */
static int cell(char *why, size_t cap, const char *what,
                uint64_t got, uint64_t want)
{
    if (got == want) {
        cq_fphost_arms++;
        return 1;
    }
    return no(why, cap, "nan cell: %s = %016llx, want %016llx "
                        "(this host is not the x86 of PRD-v2 7.4)",
              what, (unsigned long long)got, (unsigned long long)want);
}

int cq_fphost_check(char *why, size_t cap)
{
    volatile double inf, zero, neg1, tiny, half;
    volatile double qa, qb, sn;

    cq_fphost_arms = 0;
    if (why != NULL && cap > 0) why[0] = '\0';

    /* Arm 1 — the rounding mode. Every anchor in §7.12's list that lands on a
     * tie (round bit set, sticky clear) reads differently under any other mode,
     * and the port's round-and-pack is ties-to-even by construction. */
    if (fegetround() != FE_TONEAREST)
        return no(why, cap, "rounding: fegetround() = %d, want FE_TONEAREST = %d",
                  fegetround(), FE_TONEAREST);
    cq_fphost_arms++;

    /* Arms 2 and 3 — subnormals survive, on BOTH sides of an operator, and the
     * two are SEPARATE MXCSR bits that this pair keeps separate.
     *
     * THE OBVIOUS SPELLING OF ARM 2 IS `DBL_TRUE_MIN * 1.0`, AND IT MASKS ARM 3
     * ENTIRELY — measured 2026-09-18 by setting each bit through _mm_setcsr.
     * That product's RESULT is subnormal too, so flush-to-zero alone zeroes it;
     * arm 2 then fires for either bit, arm 3 can never be the first to fail,
     * and the message names the wrong register. It is CLAUDE.md's masked-guard
     * shape with the masking copy EARLIER in the function.
     *
     * Arm 2 therefore scales a subnormal operand up to a NORMAL result
     * (2^-1074 * 2^60 = 2^-1014, exact, above DBL_MIN), so it fires on
     * denormals-are-zero and on nothing else; arm 3 takes a NORMAL operand to a
     * subnormal result, exact in every rounding mode, so it fires on
     * flush-to-zero and on nothing else. Provoked one bit at a time, each arm
     * fires alone. §7.12's "largest and smallest subnormal" anchors are
     * unreachable with either bit set. */
    tiny = DBL_TRUE_MIN;
    if (tiny * 0x1p+60 == 0.0)
        return no(why, cap, "subnormal operand: DBL_TRUE_MIN * 0x1p+60 == 0 "
                            "(denormals-are-zero is ON)");
    cq_fphost_arms++;

    half = DBL_MIN;
    if (half / 2.0 == 0.0)
        return no(why, cap, "subnormal result: DBL_MIN / 2 == 0 "
                            "(flush-to-zero is ON)");
    cq_fphost_arms++;

    /* Arms 4-6 — the three cells that produce Intel's indefinite QNaN. */
    inf  = INFINITY;
    zero = 0.0;
    neg1 = -1.0;

    if (!cell(why, cap, "Inf - Inf", cq_fphost_bits(inf - inf), CQ_FPHOST_INDEF))
        return 0;
    if (!cell(why, cap, "0 * Inf", cq_fphost_bits(zero * inf), CQ_FPHOST_INDEF))
        return 0;
    if (!cell(why, cap, "sqrt(-1)", cq_fphost_bits(sqrt(neg1)), CQ_FPHOST_INDEF))
        return 0;

    /* Arms 7-8 — two quiet NaNs: the FIRST operand's payload survives, in both
     * orders. Both orders are asserted because one order alone is consistent
     * with "the larger payload wins" and with several other rules. */
    qa = cq_fphost_from_bits(CQ_FPHOST_QNAN_A);
    qb = cq_fphost_from_bits(CQ_FPHOST_QNAN_B);
    sn = cq_fphost_from_bits(CQ_FPHOST_SNAN);

    if (!cell(why, cap, "qNaN_A + qNaN_B", cq_fphost_bits(qa + qb), CQ_FPHOST_QNAN_A))
        return 0;
    if (!cell(why, cap, "qNaN_B + qNaN_A", cq_fphost_bits(qb + qa), CQ_FPHOST_QNAN_B))
        return 0;

    /* Arms 9-10 — quiet against signalling, AT THE PAYLOAD PAIR THAT SEPARATES
     * THE TWO READINGS. qNaN_A is deliberately not used here: quietening
     * CQ_FPHOST_SNAN gives exactly qNaN_A's bits, so an A-against-sNaN row is
     * degenerate and agrees with "the qNaN always wins" — which is false. The B
     * row is the one that fixes the convention, and it says the first operand
     * wins and is quietened if it was signalling. */
    if (!cell(why, cap, "qNaN_B + sNaN", cq_fphost_bits(qb + sn), CQ_FPHOST_QNAN_B))
        return 0;
    if (!cell(why, cap, "sNaN + qNaN_B", cq_fphost_bits(sn + qb), CQ_FPHOST_SNAN_QUIET))
        return 0;

    return 1;
}
