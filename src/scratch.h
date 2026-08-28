/* src/scratch.h — M08: the scratch region's cq_bit array. PRD §5, plan §3.
 *
 * M08 OWNS THE ARRAY AND NOTHING ELSE. It does not own the qubits, does not
 * materialise, and SHIPS NO RELEASE A KERNEL CAN CALL. M09's `cq_sandwich`
 * owns the scratch qubits end to end: it pre-materialises the region at step 0
 * (I6(b)) and releases it in its own epilogue. That split is not tidiness —
 * PRD §10 lets a `proven_zero` constant exist only where the code that RUNS a
 * construction can assert that construction's premises. `CQ_ZERO_BY_PALINDROME`
 * is M09's, resting on three (one involution per step, I6(a), I6(b)); Step 20
 * added M06's `CQ_ZERO_BY_CTRL_UNCOMPUTE` for §9's shared ancilla, resting on
 * three of its own. There are two, and there is no third. An M08 release would
 * be a stamping site with no premises at all — which is the point of the rule,
 * not an exception to it.
 *
 * NO cq_ctx ANYWHERE IN THIS HEADER, and that is the enforcement rather than
 * the convention. With no pool in reach M08 CANNOT allocate a qubit, so "a
 * scratch region costs zero qubits until the driver pre-materialises it" is a
 * fact of the type system — the same move by which `cq_reg_alloc_zero` taking
 * the table rather than the context makes I4 one (reg.h), and by which
 * emit.h's `const cq_bit *` controls make I6(a) one.
 *
 * THE REGION IS ONE CONTIGUOUS EXTENT. emit.c's I6(a) check is a pointer RANGE
 * test over cq_bit addresses (ctx.h), so contiguity is what lets it answer
 * "is this target in scratch?" at all. A kernel carves the region into named
 * sub-arrays with cq_scratch_span; it never allocates two regions.
 */
#ifndef CQOPS_SCRATCH_H
#define CQOPS_SCRATCH_H

#include <stdint.h>

#include "bit.h"

typedef struct {
    cq_bit  *bits;
    uint32_t n;
} cq_scratch;

/* `n` bits, every one CQ_BIT_ZERO. A zero-width region is a hard error: it
 * makes every premise the driver asserts vacuous, and a construction needing
 * no scratch needs no sandwich (PRD §5 lists and/or/xor as naturally clean). */
void cq_scratch_alloc(cq_scratch *s, uint32_t n);

/* ASSERTS EVERY BIT IS BACK TO CQ_BIT_ZERO — a KIND check, never a shadow
 * read, and a hard error in BOTH configurations.
 *
 * The kind is the only thing M08 can honestly see. A literal Rule-6 shadow
 * check is unimplementable here: §3's CX rule makes poison sticky, so after
 * any compute half over a tainted operand every scratch entry reads `unknown`
 * even though the bit is provably back to |0>, and the check would fire on
 * every legitimate kernel that ever met a rotation (bd ckd.17, closed; what
 * cqrt_free reads instead is PRD §15 D15; K09.md:603; K11.md:677 retracts the
 * `cq_scratch_free` that would have done it). The scope of "unimplementable"
 * is that tainted case: on the rotation-free surface the shadow is exact.
 *
 * What the kind check does catch is the leak M09 cannot: a kernel that grabs a
 * region and materialises into it WITHOUT going through the driver. Nothing
 * else in the project would notice — M03 is never told, M07 never sees this
 * array — so unlike M07's free there is no layer underneath to mask a
 * mutation of this line.
 *
 * Leaves the struct usable as if freshly init'd, matching M03 and M07, so a
 * kernel may allocate/sandwich/dispose in a loop. Idempotent. */
void cq_scratch_dispose(cq_scratch *s);

uint32_t cq_scratch_size(const cq_scratch *s);

/* Bounds-checked base pointer for the `len` bits at `off` — how a kernel
 * carves one region into K6's carry chain, K9's `nb`, and so on. Checking the
 * whole span turns an off-by-one in a kernel's layout into an abort at the
 * point the mistake was made rather than at some later gate. `off == n` with
 * `len == 0` is legal and yields the one-past-the-end pointer. */
cq_bit *cq_scratch_span(cq_scratch *s, uint32_t off, uint32_t len);

#endif /* CQOPS_SCRATCH_H */
