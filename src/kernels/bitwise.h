/* src/kernels/bitwise.h — M10, Step 10. K1 xor, K2 and, K3 or.
 *
 * NOT NAMED IN THE MODULE MAP, which prints "M10 `kernels/bitwise.c`" with no
 * header (plan §3, PRD §14) — as it does for every Layer-3 kernel. A header is
 * needed anyway: plan §2.1 is one test binary per module and the suites reach
 * internal headers directly, so `cq_kernel_xor` has to be declared somewhere.
 * The name follows the sibling construction specs, which already print C
 * prototypes in this shape (K05.md:83, K09.md:28), and the `cq_` prefix is
 * PRD §14's rule for internal symbols.
 *
 * ALL THREE ARE NATURALLY CLEAN — no cq_sandwich, no scratch, no ancilla, at
 * any W (PRD §5's "naturally clean already"; K01.md §4, K02.md §4, K03.md §4
 * each derive it from the Julia body). Bennett's `allocate!` in each of the
 * three lower_* functions produces the SSA RESULT, which is our caller-supplied
 * `dst` — not a temporary — so there is no intermediate wire for Bennett's
 * global forward-copy-reverse wrap to be cleaning up on this kernel's behalf,
 * and nothing for Bennett-in-the-small to localise. That is why these three go
 * first: they exercise the shared L1-L4 driver on kernels that have nothing to
 * leak, before Step 12 hands it one that does.
 *
 * EACH IS ITS OWN INVERSE, which is Rule 7 falling out rather than a property
 * anyone arranged: every gate targets `dst` and every control is drawn from
 * `a` or `b`, so calling the same function twice XORs `f(a,b)` in twice and
 * leaves 0. There is no _unc entry point here and there must never be one.
 *
 * SAFE TO CALL FROM INSIDE ANOTHER KERNEL'S SANDWICH COMPUTE HALF with
 * `dst = scratch`: every gate target is a bit of `dst`, so I6(a) is satisfied
 * for the caller automatically. K9 (cmp) and K12 (divrem) will use exactly
 * that.
 */
#ifndef CQOPS_KERNELS_BITWISE_H
#define CQOPS_KERNELS_BITWISE_H

#include "bit.h"
#include "ctx.h"

/* K1 — dst ^= a ^ b.  Bennett lower_xor!, arith.jl:284-291.  2W CNOT. */
void cq_kernel_xor(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W);

/* K2 — dst ^= a & b.  Bennett lower_and!, arith.jl:268-272.  W Toffoli. */
void cq_kernel_and(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W);

/* K3 — dst ^= a | b.  Bennett lower_or!, arith.jl:274-282.
 * 2W CNOT + W Toffoli, via the XOR identity a ^ b ^ (a & b). */
void cq_kernel_or (cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W);

#endif /* CQOPS_KERNELS_BITWISE_H */
