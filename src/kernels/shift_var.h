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
#include "scratch.h"

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

/* --- The barrel as a block, so a composite kernel can compose it. --------
 *
 * ADDITIVE, AND IT IS THE SCHEDULE THAT IS EXPORTED, NOT NEW GATES. The three
 * entry points above now DISPATCH through `cq_barrel_step`, so there is exactly
 * one gate list in this module and the L4 goldens did not move. This is plan
 * §0.4 / PRD §15 D9(e)'s shape — `cq_ult_block` (kernels/cmp.h), `cq_sub_block`
 * (kernels/add.h), `cq_mux_block` (kernels/mux.h), `cq_divrem_block`
 * (kernels/divrem_u.h) — and the first of the three step exports PRD-v2 §7.10
 * records that v1 owes the fp port before its first kernel: `soft_fadd`'s
 * alignment shift, `fptosi`/`sitofp`'s normalisation and `fround`'s masking are
 * all variable-amount shifts at `W = 64`.
 *
 * WHY A CONSUMER MUST CALL cq_barrel_step AND NEVER cq_kernel_shl_var:
 * `cq_kernel_shl_var` is a whole `cq_sandwich`, and `cq_sandwich` refuses
 * nesting in both configurations — so calling it from inside another compute
 * half aborts before allocating anything. Same rule M12 itself obeys towards
 * M17 (`kernels/mux.h`, "A COMPOSITE KERNEL CALLS THE OTHER KERNEL'S STEP
 * FUNCTION, NEVER THE KERNEL").
 *
 * D8 IS UNCHANGED AND IS STRUCTURAL HERE TOO. Only `b[0 .. S-1]` are ever mux
 * controls, with `S = cq_shift_stages(W)` (`arith.jl:348`), so the MASK half is
 * "those bits are never looked at" rather than an arithmetic reduction; the
 * SATURATE half falls out of each stage zero-filling or sign-filling what it
 * shifts in. A consumer needing the amount's high bits to mean something must
 * handle them ITSELF, above this block — see K04.md §7 for the worked W = 64
 * table the fp port reads.
 */

typedef enum {
    CQ_BARREL_SHL  = 0,    /* lower_var_shl!   arith.jl:366-380 */
    CQ_BARREL_LSHR = 1,    /* lower_var_lshr!  arith.jl:350-364 */
    CQ_BARREL_ASHR = 2     /* lower_var_ashr!  arith.jl:382-400 */
} cq_barrel_dir;

/* THE BLOCK ALLOCATES NOTHING — plan §0.4 obligation 2. `scr` is the CALLER's
 * one contiguous region and `off` is where this barrel's
 * `cq_barrel_region(W)` bits start inside it, exactly as `cq_divrem_block` has
 * it; M12's own entry points pass `off = 0`.
 *
 * `a` AND `b` ARE CONTROLS ONLY and may be anything the caller can name — its
 * operands (M12) or a scratch span an earlier step wrote (plan §0.4 obligation
 * 4). Neither is ever a gate target, so I6(a) holds for this block by
 * construction and an operand view that overlaps an earlier intermediate is
 * legal, as K12's shifted remainder is (K12.md §2.1a).
 *
 * THE REGION'S LAYOUT, because a consumer has to be able to point other blocks
 * at these spans — `S = cq_shift_stages(W)` stages, `W(3S+1)` bits:
 *
 *     [0, W)                       cur0    Bennett's `result` before stage 0
 *     [W(1+3k), W(2+3k))           sh_k    stage k's shuffled copy
 *     [W(2+3k), W(3+3k))           r_k     stage k's mux output  <- the value
 *     [W(3+3k), W(4+3k))           d_k     stage k's mux scratch (t ^ f)
 *
 * THE FINAL SHIFTED VALUE IS `r_{S-1}`, i.e. the span at `W(3S-1)`, and at
 * `S == 0` (W <= 1, `arith.jl:348`'s `W <= 1 ? 0` guard) it is `cur0` — which
 * is what `cq_barrel_result` returns and is the ONLY correct way to find it.
 * Do not re-derive that offset at a call site: `r_k` is the running value that
 * Bennett reassigns `result` to, so the last stage's `r` is the answer and
 * every other span is an intermediate the reverse half has to unwind. */
typedef struct {
    const cq_bit *a;       /* the value, W bits; controls only        */
    const cq_bit *b;       /* the amount; only b[0 .. S-1] are read    */
    cq_scratch   *scr;     /* the caller's region — nothing allocated  */
    uint32_t      off;     /* where this barrel's region starts        */
    int           W;
    cq_barrel_dir dir;
} cq_barrel_block;

/* `W(3S+1)` bits. A hard error at `W <= 0` in both configurations, and its
 * message is deliberately DISJOINT from `cq_barrel_steps`'s identical guard so
 * a death case can name which of the two spoke (M15's `cq_addacc_check`
 * precedent — see kernels/shift_var.c). */
int cq_barrel_region(int W);

/* `W + Σ_{k<S} (shuffle_k + 4W)`, with `shuffle_k = W - 2^k` for shl/lshr and
 * `W` for ashr — so `5WS + W - 2^S + 1` and `5WS + W` respectively, and `W` at
 * `S == 0`. One gate per step, so at the ALL-QUANTUM operand mask this is also
 * the compute half's gate count (K10.md §2.2); at any other mask an operand
 * fold can make a step emit nothing, and a caller that needs the gate count
 * must MEASURE it.
 *
 * IT TAKES THE DIRECTION, AND THAT IS FORCED RATHER THAN CHOSEN. `shl`/`lshr`
 * emit `W - 2^k` shuffle CNOTs per stage because the out-of-range source index
 * does not exist (`arith.jl:359`, `:375`), while `ashr`'s else-branch clamps to
 * the sign bit and writes all `W` (`:394`). A direction-free step count would
 * mean padding shl/lshr with slots that emit nothing, which breaks "every
 * structural step emits exactly one gate" — the property K10.md §2.2 states
 * and the one a palindrome's head length rests on. `cq_divrem_steps(W, with_q)`
 * is the same shape for the same reason. */
int cq_barrel_steps(int W, cq_barrel_dir dir);

/* One gate, `u` in `[0, cq_barrel_steps(W, dir))`. Out of range is a hard error
 * in BOTH configurations, for `cq_ult_step`'s reason: a consumer maps a
 * contiguous run of its own step indices onto this range, and an off-by-one
 * would land inside the stage loop and emit a plausible wrong gate rather than
 * fail. */
void cq_barrel_step(cq_ctx *ctx, const cq_barrel_block *k, int u);

/* The shifted value, as a writable `W`-bit view into the caller's region —
 * `r_{S-1}`, or `cur0` when `S == 0`. Writable because a consumer copies out of
 * it, exactly as `cq_divrem_remainder` is. */
cq_bit *cq_barrel_result(const cq_barrel_block *k);

#endif /* CQOPS_KERNELS_SHIFT_VAR_H */
