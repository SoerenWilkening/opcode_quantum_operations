/* src/sink_qec_angle.c — M25b. See the header for D19, the cap and the band. */

#include "sink_qec_angle.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* The house shape, one layer per prefix. */
static void cq_qa_die(const char *what, double theta, long v)
{
    fprintf(stderr, "libcqops: FATAL: qec angle: %s (theta=%a, %ld)\n",
            what, theta, v);
    abort();
}

/* π to double-double: PI_HI is the nearest double to π — the same one
 * src/angle.h uses — and PI_LO carries the next 53 bits of π − PI_HI, giving π
 * to a relative ~9.5e-34. Copied deliberately rather than included from
 * angle.h: M21 classifies and this converts, they share no code path, and
 * angle.h's constant is the one the CLASSIFIER rounds to. */
#define PI_HI 3.14159265358979323846264338327950288
#define PI_LO 1.2246467991473531772e-16

/* |hi| ≥ |lo| is the caller's obligation; the two-sum then renormalises. */
typedef struct { double hi, lo; } cq_dd;

static cq_dd dd_norm(double a, double b)
{
    cq_dd r;
    r.hi = a + b;
    r.lo = b - (r.hi - a);
    return r;
}

/* x − a, for `a` an integer whose magnitude does not exceed x.hi's. The
 * subtraction is exact — x.hi and floor(x.hi) are multiples of the same ulp —
 * so the only rounding is the renormalisation. */
static cq_dd dd_sub_int(cq_dd x, double a)
{
    return dd_norm(x.hi - a, x.lo);
}

/* 1/x, by one Newton correction on the leading reciprocal:
 *   e = 1 − x·r   computed as (1 − x.hi·r) − x.lo·r, the first term by fma so
 *                 the product's rounding is recovered rather than lost
 *   1/x ≈ r·(1 + e) */
static cq_dd dd_recip(cq_dd x)
{
    double r = 1.0 / x.hi;
    double e = fma(-x.hi, r, 1.0) - x.lo * r;
    return dd_norm(r, r * e);
}

/* floor of a double-double: floor(hi) is right unless the pair sits just BELOW
 * an integer, which is exactly what θ = k·π gives (the quotient is k − 3.9e-17,
 * whose hi rounds to k). Getting this wrong would put every folding row of §7
 * one integer too high. */
static double dd_floor(cq_dd x, cq_dd *frac)
{
    double a = floor(x.hi);
    cq_dd  f = dd_sub_int(x, a);
    if (f.hi < 0.0 || (f.hi == 0.0 && f.lo < 0.0)) {
        a -= 1.0;
        f = dd_sub_int(x, a);
    }
    *frac = f;
    return a;
}

/* θ/π as a double-double. The residual of the leading quotient is recovered
 * with fma against PI_HI and then corrected by the PI_LO term; dividing by
 * PI_HI alone is what reinjects the drift (see the header). */
static cq_dd theta_over_pi(double theta)
{
    double q0 = theta / PI_HI;
    double r  = fma(-q0, PI_HI, theta) - q0 * PI_LO;
    return dd_norm(q0, r / PI_HI);
}

/* Every convergent is guarded in DOUBLE before it is formed in `long`, so the
 * overflow test never itself overflows. 9.0e18 is under LONG_MAX (9.223e18) on
 * every platform with 64-bit longs, and on a 32-bit long the `long` cast below
 * would be the narrowing the -Wconversion build refuses — hence the explicit
 * range test rather than a cast and a hope. */
#define CQ_QA_LONG_LIMIT 9.0e18

void cq_qec_ratio(double theta, long cap, long *p, long *q)
{
    if (!isfinite(theta)) cq_qa_die("angle is not finite", theta, cap);
    if (cap < 1)          cq_qa_die("denominator cap below 1", theta, cap);

    cq_dd x = theta_over_pi(theta);
    cq_dd frac;
    double a = dd_floor(x, &frac);

    if (fabs(a) > CQ_QA_LONG_LIMIT)
        cq_qa_die("angle too large to express as an integer multiple of pi",
                  theta, cap);

    long p_prev = 1, p_cur = (long)a;
    long q_prev = 0, q_cur = 1;

    /* The expansion terminates on its own for a rational quotient and is cut by
     * the cap otherwise; the iteration bound is a backstop against a pathology
     * in the double-double residual, not part of the mathematics. */
    for (int step = 0; step < 64; step++) {
        if (frac.hi == 0.0 && frac.lo == 0.0) break;

        x = dd_recip(frac);
        a = dd_floor(x, &frac);
        if (!(a >= 1.0)) break;               /* also catches a non-finite a */

        double qn = a * (double)q_cur + (double)q_prev;
        double pn = a * (double)p_cur + (double)p_prev;
        if (qn > (double)cap || fabs(pn) > CQ_QA_LONG_LIMIT) break;

        long a_l = (long)a;
        long p_new = a_l * p_cur + p_prev;
        long q_new = a_l * q_cur + q_prev;
        p_prev = p_cur; p_cur = p_new;
        q_prev = q_cur; q_cur = q_new;
    }

    *p = p_cur;
    *q = q_cur;
}
