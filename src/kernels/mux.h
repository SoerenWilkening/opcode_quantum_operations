/* src/kernels/mux.h — M17, Step 14. K10 mux (select).
 *
 * THREE SOURCES AGAINST RULE 7's TWO, AND THAT IS SANCTIONED RATHER THAN
 * SMUGGLED (bd ckd.15, resolved 2026-08-16). Rule 7's
 *
 *     void kernel(cq_ctx*, cq_bit *dst, const cq_bit *a, const cq_bit *b, int W)
 *
 * is the shape of the CANONICAL two-source kernel and the type `cq_kernel_fn`;
 * what the rule actually fixes is the SEMANTICS — `dst ^= f(sources)`, sources
 * unchanged, every internal ancilla back at |0> — and those are what the three
 * axes rest on. A kernel whose arity or operand widths differ declares its own
 * signature, names every operand explicitly, and satisfies the semantics
 * unchanged. Two such kernels shipped before this one, and they depart
 * differently: M13's casts are unary with two widths, so they leave the
 * parameter list; M16's compares KEEP it and the `cq_kernel_fn` type exactly,
 * breaking only the unwritten assumption that |dst| == W. K10 leaves the list
 * outright, which is why it cannot be stored in a `cq_kernel_fn` at all.
 * `cq_kernel_check_n` (kernels/kernel.h) is already N-ary for exactly this
 * reason, and the test driver already carries CQ_KD_MAX_SRC = 3 and a
 * shape/call/refn triple, so nothing below or above had to move.
 *
 * WHAT IS *NOT* PERMITTED is inventing a fourth parameter to carry state, or
 * adding a `_controlled` or `_unc` variant. The extra operand is a parameter
 * and, inside the sandwich, a member of the kernel's own `env` struct — the
 * same escape hatch M14's adder uses for its complement region.
 *
 * SANDWICH, because Bennett's `lower_mux!` (arith.jl:522-532) allocates TWO
 * W-wide vectors and leaves the second one dirty: at return `diff[i] = t[i] ^
 * f[i]`, and nothing in `src/lowering/` ever calls `free!`. Bennett relies on
 * its one global forward-copy-reverse wrap; we have none, so K10 gets
 * Bennett-in-the-small (Rule 2, PRD §5) and pays 2x for it.
 *
 * A CLASSICAL `cond` DOES NOT FOLD AWAY BY ITSELF, which is why there is an
 * entry dispatch below. With `cond = CQ_BIT_ZERO` the W Toffolis vanish by the
 * §3 fold table but the 6W CX of compute+reverse remain, so a constant select
 * would cost 7W CX to compute `dst ^= f`. See the dispatch note in mux.c.
 */
#ifndef CQOPS_KERNELS_MUX_H
#define CQOPS_KERNELS_MUX_H

#include "bit.h"
#include "ctx.h"

/* K10 — dst ^= (cond ? t : f), i.e. dst ^= f ^ (cond & (t ^ f)).
 *
 * `cond` IS ONE BIT, not W. Bennett's `lower_mux!` takes `cond` as a vector and
 * reads only `cond[1]` (arith.jl:529); the barrel passes the singleton
 * `[b[k+1]]` (:361, :377, :397). `select`'s condition is `i1` in the IR, so a
 * W-wide parameter here would be W-1 bits nobody reads.
 *
 * Sandwiched cost at all-quantum operands: 7W CX + 2W CCX, 0 X, and 2W scratch
 * qubits taken and returned. With `cond` classical: W CX and no scratch at all.
 */
void cq_kernel_mux(cq_ctx *ctx, cq_bit *dst, const cq_bit *cond,
                   const cq_bit *t, const cq_bit *f, int W);

/* --- The block, shared with M12's barrel shifter. ------------------------ */

/* THE BARREL IS L COPIES OF THIS BLOCK, AND UPSTREAM SAYS SO LITERALLY:
 * `lower_var_shl!`/`lshr!`/`ashr!` each end their stage loop with
 * `result = lower_mux!(g, wa, [b[k+1]], shifted, result, W)` (arith.jl:361,
 * :377, :397). So M12 reusing this step function is Rule 1 applied to the call
 * graph as well as to the gates — the alternative is transcribing `lower_mux!`
 * a second time, which is the mistake bd -4tt was filed about in the other
 * direction (there the premise was false and K9 genuinely had its own upstream
 * function; here it is true).
 *
 * `r` and `d` are the two scratch vectors — Bennett's `r` and `diff`. `r` is
 * the block's OUTPUT, and for M12 it is the next stage's `f`; `d` is pure
 * scratch. Both must lie inside the caller's sandwich region: every gate below
 * targets one of them and nothing else, which is I6(a) discharged by
 * construction rather than by inspection.
 */
typedef struct {
    const cq_bit *cond;    /* one bit; only cond[0] is read */
    const cq_bit *t, *f;   /* the two arms, W bits each     */
    cq_bit       *r, *d;   /* scratch: the result, and t^f  */
} cq_mux_block;

/* FOUR STEPS PER BIT, ONE GATE EACH, AND THE SPLIT IS FORCED (bd ckd.14a).
 * K10 is one of the two worked witnesses that made one-gate-per-step a driver
 * invariant: the natural 4-gate block is NOT self-inverse — re-running it from
 * its own post-state leaves `d = 0` but `r = cond & (t ^ f)`, so a sandwich
 * built on multi-gate steps silently stops cancelling (K10.md §2.0). The
 * driver reverses STEP order only, so a step must be an involution, and a
 * single X/CX/CCX always is. */
enum { CQ_MUX_STEPS_PER_BIT = 4 };

/* One gate of the block: `u` in [0, 4W), bit `u / 4`, phase `u % 4`. */
void cq_mux_step(cq_ctx *ctx, const cq_mux_block *b, int u);

#endif /* CQOPS_KERNELS_MUX_H */
