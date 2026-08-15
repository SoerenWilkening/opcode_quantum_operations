/* tests/support/refmodel.h — plan §2.2. Plain-C reference semantics.
 *
 * The other half of L1's differential: the kernel computes f(a,b) as a
 * reversible circuit, this file computes it as ordinary C, and the suite
 * asserts they agree. Keeping the reference in a separate file from the kernel
 * is the whole point — a reference derived from the kernel proves nothing.
 *
 * A PACKED SCALAR HERE IS NOT AN I5 VIOLATION, and the distinction is worth
 * stating once because Rule 3 is otherwise absolute. I5 forbids a packed
 * uint64_t in the REPRESENTATION — in a register, a peephole or a kernel —
 * because it caps width at 64 and forces a two-word split plus a 128-bit
 * variant of every fold. This is neither: it is the *reference*, and PRD §11
 * defines L1 as "compare shadow result against the C operator", which requires
 * a C scalar by construction. Plan §2.2 budgets 150 lines for exactly that.
 * The cap it does impose is real and bounded: W ≤ 64, which covers every width
 * L1 tests ({1,2,4,8} exhaustive, {16,32,64} sampled). An i80 or i128 kernel
 * would need a two-word reference, and cq_ref_mask aborts rather than silently
 * truncating if anyone asks for one.
 *
 * Two's complement and masking live here rather than in each suite, because
 * "with correct masking and two's-complement edge cases" is where a reference
 * model usually goes wrong: an unmasked reference agrees with a kernel that
 * has the same overflow bug.
 */
#ifndef CQOPS_TEST_REFMODEL_H
#define CQOPS_TEST_REFMODEL_H

#include <stdint.h>

/* The low `W` bits set. W is validated: 0 and anything above 64 are hard
 * errors, not a shifted-by-64 undefined behaviour that UBSan would catch only
 * in Debug. */
uint64_t cq_ref_mask(int W);

/* `v` reduced to W bits, and its two's-complement sign extension to 64 bits.
 * cq_ref_sext is what the signed kernels (K9's slt, K20's sdiv) will compare
 * against; it lands now so the arithmetic exists in one place from the start. */
uint64_t cq_ref_trunc(uint64_t v, int W);
int64_t  cq_ref_sext (uint64_t v, int W);

/* K1, K2, K3. Signature shape is the reference counterpart of Rule 7's kernel
 * contract: two operands and a width, no state. */
uint64_t cq_ref_xor(uint64_t a, uint64_t b, int W);
uint64_t cq_ref_and(uint64_t a, uint64_t b, int W);
uint64_t cq_ref_or (uint64_t a, uint64_t b, int W);

/* K4, at the effective amount. D8's reduction is NOT applied here — the caller
 * applies it once, through cq_shift_stages/cq_shift_amount, so that the kernel
 * and the reference cannot disagree about the reduction itself. These take the
 * already-reduced `k` and implement only `sat_shift`. */
uint64_t cq_ref_shl (uint64_t a, int k, int W);
uint64_t cq_ref_lshr(uint64_t a, int k, int W);
uint64_t cq_ref_ashr(uint64_t a, int k, int W);

/* --- The two-word half, for the widths a single uint64_t cannot reach. ----
 *
 * K5's casts are the ONLY way an i128 register exists at all, and 25 of the 55
 * cast pairs CQ_lang ships involve i80 or i128. A one-word reference can check
 * 30 of 55 and would have to say so; this checks all of them. Chosen
 * deliberately over the cheaper "L4 counts only for the wide pairs", because
 * L1 and L5 are the two levels that actually catch bugs (Rule 10) and the wide
 * pairs are exactly where a sign-extension or a masking error would live.
 *
 * `hi` holds bits 64..127. For W <= 64 `hi` is always 0 and these agree with
 * the one-word functions bit for bit — asserted in the suite rather than
 * assumed, since two models that never meet are two chances to be wrong. */
typedef struct { uint64_t lo, hi; } cq_ref_w;

cq_ref_w cq_ref_w_make (uint64_t lo, uint64_t hi, int W);  /* masked to W */
int      cq_ref_w_eq   (cq_ref_w a, cq_ref_w b);
int      cq_ref_w_bit  (cq_ref_w v, int i);
cq_ref_w cq_ref_w_zero (void);

/* Enough bit algebra for a mask set, and no more. These exist so the bit-kind
 * masks can be one type at every width rather than a uint64_t below 64 and
 * something else above it — two mask representations would be two places for
 * an off-by-one at the 64-bit seam, which is precisely where i80 lives. */
cq_ref_w cq_ref_w_ones  (int W);            /* the low W bits set */
cq_ref_w cq_ref_w_setbit(int i);            /* just bit i */
cq_ref_w cq_ref_w_and   (cq_ref_w a, cq_ref_w b);
cq_ref_w cq_ref_w_or    (cq_ref_w a, cq_ref_w b);
cq_ref_w cq_ref_w_andnot(cq_ref_w a, cq_ref_w b);
int      cq_ref_w_is_zero(cq_ref_w a);
uint32_t cq_ref_w_popcount(cq_ref_w a);

/* K4, width-generic to 128, at the ALREADY-REDUCED amount (D8's reduction is
 * the caller's, so kernel and reference cannot disagree about it). Needed
 * because i80 carries 47% of the corpus's shift calls AND is the only shipped
 * width where D8's saturating half can fire at all — at every power-of-two W
 * the interval [W, 2^ceil(log2 W)) is empty. */
cq_ref_w cq_ref_w_shl (cq_ref_w a, int k, int W);
cq_ref_w cq_ref_w_lshr(cq_ref_w a, int k, int W);
cq_ref_w cq_ref_w_ashr(cq_ref_w a, int k, int W);

/* K5, width-generic to 128. `F` is the source width, `T` the destination. */
cq_ref_w cq_ref_w_zext (cq_ref_w a, int F, int T);
cq_ref_w cq_ref_w_sext (cq_ref_w a, int F, int T);
cq_ref_w cq_ref_w_trunc(cq_ref_w a, int F, int T);

#endif /* CQOPS_TEST_REFMODEL_H */
