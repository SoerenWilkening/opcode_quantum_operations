/* instance.h (n=20 QTG sweep) -- a twenty-item knapsack in the shape of the
 * paper's KP_4 example, so the only things that change against the n=4 and n=6
 * rows are n and the register width.
 *
 *   w = (7,5,9,13,8,6,10,2,14,8,3,9,3,11,5,8,4,4,8,8)   sum 145
 *   p = (2,4,9, 8,5,11, 4,3,10,8,8,2,9, 3,7,3,5,3,6,10) sum 120
 *   c = 72  (half the total weight, the standard knapsack regime)
 *
 * REGISTER WIDTHS ARE Eq. (2), |a_bin| = floor(log2 a) + 1, and BOTH COME OUT
 * AT 7 WITHOUT WIDENING -- unlike the n=4 build, which widened |c_bin| 3 -> 4
 * to make the two rails one word:
 *   H1 path      n                = 20
 *   H2 capacity  |72_bin|         = 7
 *   H3 profit    |120_bin|        = 7   (sum p = 120 < 128, cannot overflow)
 *   Ha ancilla   max(n, 7, 7)     = 20
 *
 * GREEDY x' is Section IV's: sort by decreasing density p/w and pack.  It takes
 * 13 of the 20 items, fills the sack EXACTLY (capacity left 0) for profit 85,
 * against an exact-DP optimum of 87 -- a 2.3 % gap.  That gap is the point: a
 * greedy that is already optimal makes Eq. (3)'s bias uninformative, and one
 * that is far off makes the biased sampling worse than uniform.
 *
 * THE INDEX-ORDER WALK IS FEASIBLE, which is what classical_check() needs: x'
 * is a feasible set, so every prefix of it weighs <= c and the capacity rail
 * never underflows on the greedy branch.
 *
 * Reproducible from gen_instance.py, seed 20260904.
 */
#ifndef CQOPS_QTG_INSTANCE_H
#define CQOPS_QTG_INSTANCE_H

#define QTG_N   20
#define QTG_WC  7
#define QTG_WP  7

static const unsigned qtg_w[QTG_N] =
    { 7, 5, 9, 13, 8, 6, 10, 2, 14, 8, 3, 9, 3, 11, 5, 8, 4, 4, 8, 8 };
static const unsigned qtg_p[QTG_N] =
    { 2, 4, 9,  8, 5, 11, 4, 3, 10, 8, 8, 2, 9,  3, 7, 3, 5, 3, 6, 10 };
static const unsigned qtg_c        = 72;
static const unsigned qtg_xprime[QTG_N] =
    { 1, 1, 1,  0, 0,  1, 0, 1,  0, 1, 1, 0, 1,  0, 1, 0, 1, 1, 1,  1 };

#define QTG_GREEDY_CAP    0u    /* 72 - 85's worth of weight: the sack fills exactly */
#define QTG_GREEDY_PROFIT 85u

#define QTG_BIAS ((double)QTG_N / 4.0)
#define QTG_RY_EPSILON 0.125

#endif /* CQOPS_QTG_INSTANCE_H */
