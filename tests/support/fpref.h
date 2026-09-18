/* tests/support/fpref.h — L1's ORACLE FOR M32, and PRD-v2 §7.16's decision.
 *
 * WRITTEN FROM IEEE 754-2019's RULES, NEVER FROM THE JULIA. That sentence is
 * the whole point of this file and is a claim about how it was produced: the
 * four bodies in fpref.c were written from the standard's statement of
 * roundTiesToEven (§4.3.1), of the binary64 interchange encoding (§3.4) and of
 * underflow to a subnormal (§7.5), with `third_party/bennett/src/softfloat/
 * softfloat_common.jl` NOT open. They were then compared against the circuit,
 * which is where any disagreement had to be adjudicated.
 *
 * WHY NOT THE OBVIOUS THING. M32 has no host operator to differential against
 * — there is no C `_sf_round_and_pack` — so PRD-v2 §7.4's "the host operator,
 * with the IEEE-unspecified cells pinned by TABLE" names an operator that does
 * not exist at this layer (K23 §6.5 risk 1). The two candidates that were
 * REFUSED: a second C transcription of the Julia alone is the Step 18 trap
 * (an oracle sharing shape with the implementation is blind to exactly what it
 * gets wrong), and deferring to M33/M35's kernel-level host differential alone
 * would ship M32 untested with a wrong helper presenting as a kernel bug one
 * wave later. `src/kernels/fpround_eval.c` IS that second transcription and is
 * checked AGAINST this file rather than with it.
 *
 * WHAT INDEPENDENCE LOOKS LIKE HERE, concretely, so a later reader can check
 * it rather than take it:
 *
 *   - the rounding decision is `G && ((R|S) || (frac odd))` — the standard's
 *     "nearer, and on a tie the even neighbour" read off the three discarded
 *     bits — where the port packs them into `grs` and compares against 4;
 *   - normalisation is a BOUNDED SHIFT LOOP counting places, where the port is
 *     a six-stage branchless binary search;
 *   - the subnormal shift is one explicit right shift with an OR-reduced
 *     sticky, where the port clamps, muxes and barrel-shifts;
 *   - every selection is an ordinary `if`, where the port is branchless
 *     `ifelse` chains.
 *
 * THE ONE PLACE THIS MODEL IS NOT THE STANDARD, SAID PLAINLY. The flush-to-
 * zero arm fires when the subnormal shift would move the entire 56-bit working
 * value out, i.e. at a shift of 56 or more. Upstream's own review flagged the
 * boundary case at EXACTLY 56 as theoretically dropping an RTNE round-up to
 * the smallest subnormal and recorded the disposition as investigated and
 * declined (softfloat_common.jl:148-172, `Bennett-xiqt`). Rule 1 settles what
 * libcqops does — port the line — so this model deliberately draws the same
 * boundary. Deviating would test a construction we do not ship.
 *
 * DOMAINS, BECAUSE A REFERENCE WITH NO DOMAIN IS A REFERENCE THAT DISAGREES
 * SOMEWHERE AND CANNOT SAY WHERE. `fpref_norm52` wants `m` with no bit above
 * 52 or `m == 0` (softfloat_common.jl:46-48 is upstream's own precondition,
 * and every caller satisfies it); `fpref_clz` wants a leading 1 at or below
 * bit 55, or zero. Outside those the binary-search ladder and a counting loop
 * genuinely differ, and a test that draws such an input is testing neither.
 */
#ifndef CQOPS_TEST_FPREF_H
#define CQOPS_TEST_FPREF_H

#include <stdint.h>

/* Normalise `*m` so its leading 1 sits at bit 52, decrementing `*e` by the
 * number of places moved. `m == 0` is returned unchanged with `*e` untouched.
 * Domain: no bit of `m` above 52. */
void fpref_norm52(uint64_t *m, int64_t *e);

/* Normalise `*wr` so its leading 1 sits at bit 55, decrementing `*rexp` by the
 * number of places moved. `wr == 0` shifts the full 63 places the six-stage
 * ladder can move and stays zero. Domain: leading 1 at or below bit 55. */
void fpref_clz(uint64_t *wr, int64_t *rexp);

/* Underflow to a subnormal (IEEE 754-2019 §7.5): when the biased exponent has
 * fallen to 0 or below the significand is right-shifted by `1 - rexp` with the
 * discarded bits OR-reduced into bit 0, and the stored exponent becomes 0.
 * `*ftz` is the arm where the whole 56-bit working value would go, and
 * `*flushed` is the signed zero the caller's select chain then substitutes. */
void fpref_subnormal(uint64_t *wr, int64_t *rexp, uint64_t rsign,
                     uint64_t *flushed, int *subnormal, int *ftz);

/* Round the G/R/S working format to nearest, ties to even (§4.3.1), and pack
 * it into the binary64 interchange encoding (§3.4).
 *
 * `wr` is `bit 55 = implicit 1, bits 54..3 = the 52-bit stored fraction,
 * bits 2/1/0 = guard/round/sticky` (fadd.jl:13-14). `rexp` is the BIASED
 * exponent. `*normal` is the packed result with the exponent field saturated
 * into [0, 0x7FE]; `*overflow` is the signed infinity; `*exp_ovf` and
 * `*exp_ovf_after` are the overflow tests before and after the rounding step's
 * carry-out, and the caller's select chain uses them to replace `*normal` in
 * both saturating directions — which is what makes the saturation
 * observationally free rather than a rounding mode of its own. */
void fpref_round_and_pack(uint64_t wr, int64_t rexp, uint64_t rsign,
                          uint64_t *normal, uint64_t *overflow,
                          int *exp_ovf, int *exp_ovf_after);

#endif /* CQOPS_TEST_FPREF_H */
