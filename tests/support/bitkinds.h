/* tests/support/bitkinds.h — plan §2.2. Registers built to order, and the
 * mask sets L1 sweeps over.
 *
 * L1's real subject is not the value — it is the BIT-KIND MASK. A kernel that
 * is correct on all-quantum operands and wrong on a mixed register is the
 * normal failure here, because the §3 fold table branches on kind and a kernel
 * inherits every one of those branches without naming any of them. So every
 * L1 case is a (value, quantum-mask) pair per operand, and this file is where
 * a mask becomes a register.
 *
 * THE MASK IS A uint64_t AND THE REGISTER IT BUILDS IS NOT. Plan §2.2 spells
 * this file's job as "build a register from (value, quantum-mask)", so the
 * packed scalar is prescribed — and it is a test-side *specification*, never a
 * representation: what comes out is a cq_bit array in the handle table, tested
 * for I1 like any other. See refmodel.h for the same argument at more length,
 * and note the same W ≤ 64 cap applies.
 *
 * MASKS COME IN PAIRS, not one mask applied to both operands. Risk R8's
 * measured witness is "a all Q, b all ZERO" — an ASYMMETRIC pair — and a
 * symmetric-only mask set cannot express it. The two operands of a kernel are
 * independent channels and the fold table treats them so; a suite that varies
 * them together tests the diagonal of the space and calls it the space.
 */
#ifndef CQOPS_TEST_BITKINDS_H
#define CQOPS_TEST_BITKINDS_H

#include "bit.h"
#include "ctx.h"
#include "support/refmodel.h"

#include <stdint.h>

/* Mints a live register of `W` bits holding `value` (masked to W), then
 * materialises exactly the bits set in `qmask` — through cq_materialise, the
 * one sanctioned route (Rule 5), so a bit built here is indistinguishable from
 * one a real program produced. Materialising a constant 1 emits an X, which is
 * why every caller resets its gate counter AFTER building operands and before
 * calling the kernel.
 *
 * Post-condition, and it is what makes L1 an equality rather than an
 * approximation: every bit's *value* is the corresponding bit of `value`,
 * whether it ended up classical or on a qubit whose shadow is known and
 * determinate. Nothing here poisons. */
int32_t cq_bk_reg(cq_ctx *ctx, uint32_t W, uint64_t value, uint64_t qmask);

/* The same, at any width up to 128. THE ONE-WORD FORM IS A WRAPPER FOR THIS,
 * not a parallel implementation — casts reach i80 and i128, and two builders
 * would be two places for an off-by-one at the 64-bit seam, which is exactly
 * where i80 lives. */
int32_t cq_bk_reg_w(cq_ctx *ctx, uint32_t W, cq_ref_w value, cq_ref_w qmask);

/* One L1 mask pair. `name` is carried so a failure names the mask rather than
 * printing a bare hex number the reader has to decode. */
typedef struct {
    cq_ref_w    q[2];
    const char *name;
} cq_bk_pair;

/* The fixed set, per bd bz5: all-classical (which IS L5 — zero gates, zero
 * qubits), all-quantum, alternating, LSB-only, MSB-only, and a
 * one-bit-quantum sweep across all W positions — plus the asymmetric pairs
 * risk R8 names, which no symmetric set contains. Writes at most `cap` pairs
 * and returns how many it wrote; aborts rather than truncating if `cap` is too
 * small, because a silently shortened mask set is coverage a run would still
 * report as complete. Needs W + 12 slots. */
uint32_t cq_bk_fixed_pairs(uint32_t W, cq_bk_pair *out, uint32_t cap);

/* Clears from a pair's masks the bits a kernel's shape requires to be
 * classical. K4's shift amount is the case that needs it: a quantum amount
 * belongs to M12's barrel shifter, so handing one to M11 is a hard error and
 * not a test case. */
void cq_bk_constrain(cq_bk_pair *p, const cq_ref_w *classical, int n_src);

/* Deterministic sampling for the widths where exhaustion is out of reach.
 * xorshift64*, seeded per call site, so a failure is reproducible from the
 * seed printed with it — a time-seeded PRNG would make a red run unrepeatable,
 * which is the one thing a random test must never be. */
typedef struct { uint64_t s; } cq_bk_rng;

void     cq_bk_rng_init(cq_bk_rng *r, uint64_t seed);
uint64_t cq_bk_rng_next(cq_bk_rng *r);
uint64_t cq_bk_rng_below(cq_bk_rng *r, uint64_t bound);   /* [0, bound) */

#endif /* CQOPS_TEST_BITKINDS_H */
