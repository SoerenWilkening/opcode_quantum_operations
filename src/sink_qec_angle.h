/* src/sink_qec_angle.h — M25b: the double → (p, q_denom) conversion. PRD §15 D19.
 *
 * A SPLIT SEAM RECORDED IN ADVANCE (IMPLEMENTATION_PLAN §3, Rule 12). This is
 * a self-contained numeric concern with its own suite and nothing to do with a
 * vtable, and it is deliberately compiled WHETHER OR NOT the QEC library is
 * present: it depends on nothing but <math.h>, so the half of Step 26 that can
 * be tested on any box is tested on every box.
 *
 * WHY A RATIONAL AT ALL. `qec_rz(ctx, q, long p, long q_denom, int precision)`
 * means θ = π·p/q_denom EXACTLY, with ε = 2^−precision. It is not a clumsy
 * spelling of a double: p and q_denom reach the Ross-Selinger gridsynth driver
 * as integers at 4·precision + 96 bits of working precision and are never
 * divided, so `(1, 4)` is exactly π/4 — which no double is. The angle is in
 * UNITS OF π, so the conversion is a best-rational approximation of θ/π.
 *
 * THE DENOMINATOR CAP IS D10'S TOLERANCE WEARING A DIFFERENT HAT, and it fails
 * at BOTH ends. Too small and the corpus's own `3.14` becomes `1/1` — a silent
 * 1.593e-03 rad rewrite to π, which is precisely the miscompile src/angle.h
 * spends a page refusing. Too large and a nice angle stops snapping to its nice
 * rational: fl(π/4) becomes 365555973729107575/1462223894916430357, the same
 * double and the same round-trip, but a denominator no synthesiser can shorten.
 * BOTH SURVIVE ANY VALUE CHECK — the round-trip to the input double is bit-exact
 * on either side — so the Prime Directive applies here too: a right answer is
 * not a right circuit, and only the emitted gate stream can tell them apart.
 *
 * MEASURED 2026-08-28, and one half of D19's account did not survive contact
 * with the library. π/2, π and 0 DO take gridsynth's degenerate branch — 0 T
 * gates at every precision — and those are exactly the rows §7 folds to, so the
 * cheap angles stay cheap. But fl(π/4) at cap 2^40 converts to `1/4` and STILL
 * costs 1,200 physical T at precision 20 on `config_ccx.json`, the same as the
 * 2^62 convergent: this implementation's Lemma-7.2 branch catches multiples of
 * π/2 and not π/4. D19's "ONE T gate" for π/4 is an inference from the paper,
 * not a measurement of this driver. The BAND is unaffected — it was established
 * by bisection on the CONVERSION, which is what this module owns — and the
 * lower end remains a real miscompile, so nothing here changes.
 *
 * THE QUOTIENT IS FORMED AGAINST π AT MORE THAN DOUBLE PRECISION, and that is
 * not fastidiousness. `theta / CQ_ANGLE_PI` divides by the ROUNDED π, which
 * reinjects the |θ|·3.9e-17 drift `src/angle.h` fights: at |θ| = 1e12 that is
 * 1.2e-05 in the quotient, i.e. 3.7e-05 rad of angle, which the continued
 * fraction would then faithfully approximate. The technique is
 * tests/test_angle_oracles.inc's PI_HI + PI_LO, moved library-side.
 */
#ifndef CQOPS_SINK_QEC_ANGLE_H
#define CQOPS_SINK_QEC_ANGLE_H

/* The shipped cap. D19's measured safe band is [2^32, 2^48]: π/4, π/8 and π
 * snap to 1/4, 1/8, 1/1 for every cap up to 2^53.5 (3π/4 to 2^51.9), while
 * arbitrary doubles round-trip exactly from 2^27–2^30 up. 2^40 is the middle,
 * with two decades of margin on each side. The snapping is a MECHANISM rather
 * than luck: a double near a nice rational has a huge partial quotient, so the
 * convergent holds across an enormous range of caps. */
#define CQ_QEC_DENOM_CAP (1L << 40)

/* Writes the best rational p/q with 1 ≤ q ≤ cap approximating θ/π, so that
 * π·p/q ≈ theta. `q` is always positive and the sign rides on `p`.
 *
 * "Best" in the continued-fraction sense: the last convergent whose denominator
 * fits under the cap. Semiconvergents are not searched — they can only improve
 * the bound by a constant factor, and every angle this project emits is either
 * an exact multiple of π (§7's folding rows, which land on q = 1 by the first
 * convergent) or a general double, where the cap is two decades wider than the
 * accuracy needed.
 *
 * HARD ERRORS, both configurations: a non-finite theta, a cap below 1, and an
 * angle so large that |p| would not fit in a `long` — the last is the honest
 * answer rather than a silent wrap, since `qec_rz` takes a `long` and cannot
 * represent it either. */
void cq_qec_ratio(double theta, long cap, long *p, long *q);

#endif /* CQOPS_SINK_QEC_ANGLE_H */
