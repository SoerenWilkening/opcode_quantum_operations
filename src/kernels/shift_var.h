/* src/kernels/shift_var.h — M12, Step 14. K10's barrel: variable shl/lshr/ashr.
 *
 * Read docs/constructions/K10.md and PRD §15 D8 first. The construction is
 * Bennett's `lower_var_shl!` / `lower_var_lshr!` / `lower_var_ashr!`
 * (arith.jl:350-400): one INIT copy, then L stages, each an index shuffle into
 * a fresh vector followed by a mux between the shuffled and the unshuffled
 * value, selected by bit `k` of the amount.
 *
 * D8 IS SATISFIED BY PORTING FAITHFULLY, NOT BY ADDING ANYTHING. PRD §15 D8
 * fixes one formula for both shift paths — `dst ^= sat_shift(a, k mod 2^S)`
 * with `S = cq_shift_stages(W)` — and the barrel already implements it:
 *
 *   - the MASK half is structural. Only `b[0..S-1]` are ever mux controls, so
 *     bits at or above S are not masked away, they are never looked at. That is
 *     the same sentence shift_const.h makes about M11, and it is why the two
 *     agree by construction rather than by discipline.
 *   - the SATURATE half is free. Each stage zero-fills (shl/lshr) or sign-fills
 *     (ashr) the positions it shifts in — `shifted` is a fresh all-zero vector
 *     and the out-of-range copy is simply not emitted — and saturating shifts
 *     compose additively, so an effective amount in [W, 2^S) annihilates the
 *     payload exactly as the constant path does. Measured at W=80, k=100.
 *
 * SO DO NOT ADD A RANGE CHECK, A CLAMP, OR AN OR-REDUCTION OVER THE HIGH
 * AMOUNT BITS. Making the barrel saturate "properly" was costed at bd ckd.16:
 * +156 gates and +26 qubits at W=32, to reproduce behaviour it already has.
 * Bennett's own `s >= W && break` guard is UNREACHABLE given `_shift_stages`'s
 * bound (checked for every W in [1,4096]) and is deliberately not ported as
 * live logic — `2^(L-1) < W` for every W >= 2.
 *
 * A CLASSICAL AMOUNT SHORT-CIRCUITS TO M11, AND THAT IS MANDATORY, NOT AN
 * OPTIMISATION. Bennett gets the split for free because LLVM hands it a
 * ConstOperand (arith.jl:185-198); our amount arrives as W `cq_bit`s, so this
 * module has to test for it. Without the test, `x << 3` on a tainted `x` costs
 * ~10WL gates and W(3L+1) qubits instead of <= W CX and none, and L5 — whose
 * shipped example is `int a = 0; a |= b << 3` — is false.
 *
 * AT W=1 EVERY VARIABLE SHIFT IS STILL THE IDENTITY, BUT NOT BY THE MECHANISM
 * THIS HEADER USED TO NAME (bd djf). It read "At W=1, S is 0, the test is
 * vacuously true, and every variable shift is the identity." The CONCLUSION is
 * unchanged and no behaviour moved; what is superseded is the reason. S is 0
 * exactly when W <= 1, and the short-circuit now opens with an explicit
 * `S == 0 ||` disjunct, which short-circuits — so at W=1 the classical test is
 * NEVER CALLED rather than called and vacuously true. The disjunct exists
 * because the sandwich's entry condition is S >= 1 (copyout indexes stage
 * S - 1), and resting that on a predicate's ARGUMENT made a wrong count reach
 * a uint32_t underflow. See kernels/shift_var.c.
 */
#ifndef CQOPS_KERNELS_SHIFT_VAR_H
#define CQOPS_KERNELS_SHIFT_VAR_H

#include "bit.h"
#include "ctx.h"

/* These FIT RULE 7's TWO-SOURCE SHAPE EXACTLY, unlike the mux they are built
 * from: `a` is the value, `b` is the amount, both W bits, `dst` W bits. M11's
 * three entry points have the identical signature, which is what lets the
 * cross-check test in tests/test_kernel_shift_var.c drive both from one loop. */
void cq_kernel_shl_var (cq_ctx *ctx, cq_bit *dst,
                        const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_lshr_var(cq_ctx *ctx, cq_bit *dst,
                        const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_ashr_var(cq_ctx *ctx, cq_bit *dst,
                        const cq_bit *a, const cq_bit *b, int W);

/* AND A GREP FOR THOSE THREE NAMES FINDS A CONSUMER IT CANNOT FOLLOW (bd 8u0).
 * Besides the direct calls in tests/, the three are ROWS of a function-pointer
 * table — `cq_tpl_bin_kernel`'s `K[]` in shim/cq_template_dispatch.c. The grep
 * lands on the row and stops there; the table's callers are further hops on and
 * no spelling of that grep reaches them. SO THE BLAST RADIUS OF A CHANGE HERE IS
 * NOT A SYMBOL GREP. Walk the hops instead:
 *
 *     grep -rn cq_kernel_ashr_var src shim tests   # -> the table row; stops
 *     grep -rn cq_tpl_bin_kernel shim              # -> cq_template_impl.c
 *     grep -rn cq_shim_bin_ tests                  # -> that file's public doors
 *
 * THE CHAIN IS WHAT IS DURABLE AND THE NAMES AT ITS END ARE NOT, which is why
 * they are re-derived rather than listed: a suite can be renamed or retired
 * between one reader and the next, and the hop cannot. Today it ends at
 * tests/test_template.c, which drives all three shifts through the `qq` door at
 * W = 8 and the barrel through `_lh` with a quantum amount. That file is not an
 * incidental consumer but the table's own detector — a TRANSPOSED row emits a
 * well-formed circuit for a different function, so gate counts, the palindrome,
 * the pool and the shadow all stay green and only an L1 against an independent
 * reference sees it.
 *
 * NOT CLAIMED HERE: that any suite is green, or what a gate regex should be.
 * Both rot on the next case added; the hop does not. One rider for reading a
 * run — M12's death cases share ONE binary with M17's mux cases, so a total
 * under a `step14` regex is a two-module figure and not M12 coverage. Count per
 * module, and include the template suite.
 */

#endif /* CQOPS_KERNELS_SHIFT_VAR_H */
