/* src/sandwich.h — M09: the forward–copyout–reverse driver. Plan §0.1, PRD §5.
 *
 * THE SANDWICH IS A DRIVER, NOT A PER-KERNEL PATTERN (Rule 8). PRD §5 read
 * literally has every sandwich kernel hand-write its gate loop twice, which
 * doubles each kernel and puts the correctness of the reverse half in twelve
 * separate places. Here each kernel exposes its compute half as an INDEXED
 * STEP FUNCTION and this one driver runs it forwards, copies out, and runs it
 * backwards. Reversal is STRUCTURAL and cannot be got wrong per kernel.
 *
 * DO NOT HAND-WRITE A REVERSE PASS IN A KERNEL, and do not add a `_controlled`
 * variant of one: PRD §9's controlled axis is an emitter mode (M06, Step 20),
 * and controlling a sandwich kernel is a one-line change here — promote ONLY
 * the copyout steps — rather than twelve kernel edits.
 */
#ifndef CQOPS_SANDWICH_H
#define CQOPS_SANDWICH_H

#include "ctx.h"
#include "scratch.h"

/* ONE GATE PER STEP. This is bd ckd.14(a), and it is FORCED rather than
 * chosen: the driver re-calls compute(env, s) with the SAME argument on the
 * reverse pass, so a step undoes itself only if it is an INVOLUTION — and a
 * multi-gate block generally is not. Two worked witnesses in the ported specs:
 * K06.md:566-586, where re-running the 5-gate ripple-carry block leaves
 * c_{i+1} = c·(a ⊕ b ⊕ 1), dirty whenever c_i = 1 and a_i = b_i; and
 * K10.md:153-171, where the natural 4-gate mux block leaves r = c·(t ⊕ f).
 * (K01's 2-gate block IS self-inverse — two commuting CXs into one target —
 * which is why the trap does not show up there.)
 *
 * I6(b) does NOT rescue a multi-gate step: pre-materialisation fixes WHICH
 * gates a step emits and says nothing about their ORDER. If this API ever
 * grows a multi-gate step contract it must reverse gate order WITHIN the step
 * too, and until it does, CQ_ZERO_BY_PALINDROME becomes unfounded while every
 * test stays green. */
typedef void (*cq_step_fn)(cq_ctx *ctx, void *env, int step);

/* The driver. Plan §0.1, step for step:
 *
 *   0. no nested sandwich; take the region
 *   1. pre-materialise every bit of `scr`                        <- I6(b)
 *   2. ARM the I6 extent; for s in [0, n_compute):  compute(env, s)
 *   3. DISARM;             for s in [0, n_copyout):  copyout(env, s)
 *   4. RE-ARM;             for s in (n_compute, 0]:  compute(env, s)
 *   5. release `scr`'s qubits                    <- reversal is structural
 *
 * STEP 3 DISARMS, AND THAT IS NOT AN OVERSIGHT. Copyout targets `dst`, which
 * is OUTSIDE scratch by construction (Rule 7: `dst ^= f(a,b)`), so an extent
 * armed across all three loops makes emit.c's I6(a) check fire on every
 * sandwich kernel there is. The plausible wrong fix — widening the extent to
 * cover `dst` — silently disables I6(a) for the compute halves too, which is
 * risk R1 with the detector removed.
 *
 * THE DRIVER OWNS THE SCRATCH QUBITS END TO END: it materialises the region at
 * step 1 and releases it at step 5. It does NOT own the cq_bit array — the
 * kernel allocates that with cq_scratch_alloc and disposes it after this
 * returns, and M08's dispose then re-checks that every bit came back to
 * CQ_BIT_ZERO.
 *
 * WHAT IT DOES NOT DO: there is no shadow call anywhere in this driver
 * (bd ckd.14b). It asserts its OWN premises — no nesting, every scratch bit
 * CQ_BIT_ZERO on entry and CQ_BIT_Q after step 1, and an order-sensitive
 * region fingerprint unchanged across each loop. The shadow entries are
 * retired as a CONSEQUENCE of the qubits going back to the pool, through
 * cq_ctx_release_qubit, the same path cq_reg_free uses.
 *
 * `copyout` may be NULL when `n_copyout` is 0. `n_compute` may be 0. Every
 * other malformed combination is a hard error in both configurations. */
void cq_sandwich(cq_ctx *ctx, cq_scratch *scr,
                 cq_step_fn compute, int n_compute,
                 cq_step_fn copyout, int n_copyout,
                 void *env);

#endif /* CQOPS_SANDWICH_H */
