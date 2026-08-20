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
 * cq_ref_sext is what the signed kernels (K9's slt, K12's sdiv/srem — module
 * M20, which is what "K20" here used to say and there is no such kernel) will
 * compare against; it lands now so the arithmetic exists in one place from the
 * start. Step 17 adds the divrem references beside it, including D3's
 * b == 0 rows (udiv -> 2^W - 1, urem -> a; K12.md §5 D3). */
uint64_t cq_ref_trunc(uint64_t v, int W);
int64_t  cq_ref_sext (uint64_t v, int W);

/* K1, K2, K3. Signature shape is the reference counterpart of Rule 7's kernel
 * contract: two operands and a width, no state. */
uint64_t cq_ref_xor(uint64_t a, uint64_t b, int W);
uint64_t cq_ref_and(uint64_t a, uint64_t b, int W);
uint64_t cq_ref_or (uint64_t a, uint64_t b, int W);

/* K6 and K7 have NO one-word form. They ship at i128 (opcode_table.yaml:184),
 * so the two-word pair below is the oracle, and a one-word sibling would only
 * ever be compared to it below 64 — where the two are provably the same
 * expression. See the note in refmodel.c; do not add one. */

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

/* K6, K7, width-generic to 128 — THE model L1 drives for add and sub, because
 * both ship at i128 (opcode_table.yaml:184-185) where the one-word pair
 * aborts. Word arithmetic with an explicit carry across the 64-bit seam, not a
 * bit-serial ripple: the kernel under test IS a ripple-carry circuit, and a
 * reference that shared its recurrence would share its mistakes. */
cq_ref_w cq_ref_w_add(cq_ref_w a, cq_ref_w b, int W);
cq_ref_w cq_ref_w_sub(cq_ref_w a, cq_ref_w b, int W);

/* K11, width-generic to 128 — `mul` ships at i128 (opcode_table.yaml:186), and
 * i128 is the only width where a reference's 64-bit seam is exercised at all.
 * SAME-WIDTH: only the low W bits of the product exist, here and in the kernel,
 * because there is no widening-multiply opcode in the table.
 *
 * Limb multiplication with an explicit high word, sharing no recurrence with
 * either the circuit (bit-serial shift-add over a Cuccaro carry chain) or
 * mul.c's classical fold (column accumulation). Three derivations of one
 * function; a reference that shared the kernel's algorithm would share its
 * mistakes, which is this file's opening warning. */
cq_ref_w cq_ref_w_mul(cq_ref_w a, cq_ref_w b, int W);

/* K9, width-generic to 128, and the ONE-BIT result is returned as a plain int
 * because `icmp` is `i1` — the suite wraps it into a 1-bit cq_ref_w.
 *
 * NOT BY THE BIAS FLIP THE KERNEL USES. K9's `slt` copies both operands,
 * inverts their sign bits and runs the unsigned comparator on the biased
 * values (arith.jl:465-472). A reference doing the same would share the
 * construction under test and could not disagree with it — this file's opening
 * warning, applied to the one kernel where the temptation is a one-liner.
 * cq_ref_icmp branches on the two sign bits instead, which is different
 * reasoning reaching the same answer. Verified against a third model in
 * tests/test_kernel_cmp.c: cq_ref_sext plus a plain C `<` on int64_t, at every
 * width a 64-bit signed integer can hold. */
typedef enum {
    CQ_ICMP_EQ = 0, CQ_ICMP_NE,  CQ_ICMP_ULT, CQ_ICMP_UGT, CQ_ICMP_ULE,
    CQ_ICMP_UGE,    CQ_ICMP_SLT, CQ_ICMP_SGT, CQ_ICMP_SLE, CQ_ICMP_SGE
} cq_icmp_pred;

int cq_ref_icmp(cq_icmp_pred p, cq_ref_w a, cq_ref_w b, int W);

/* K12, width-generic to 128 — `divrem` ships at i128 (opcode_table.yaml:187-190,
 * all four opcodes, the full 15-variant grid).
 *
 * SHIFT-SUBTRACT OVER cq_ref_w_sub, WHICH DOES SHARE THE KERNEL'S RECURRENCE,
 * AND THAT IS SAID OUT LOUD RATHER THAN GLOSSED. Every other reference in this
 * file is deliberately a different algorithm from the circuit it checks; this
 * one is not, because a second, genuinely different 128-bit division algorithm
 * (Knuth D on 32-bit limbs) is a large piece of error-prone arithmetic whose
 * own correctness would then need a third model. What replaces the
 * independence is a cross-check with real teeth: at every width up to 64 these
 * four are compared against the HARDWARE `/` and `%` — and, for the signed
 * pair, against C's `int64_t` division — over the full value cross product at
 * small widths and thousands of samples above it
 * (tests/test_kernel_divrem_refmodel.inc). Everything that changes above 64 is
 * the 64-bit word seam inside cq_ref_w_sub and w_ult, which have their own
 * coverage. So the recurrence is verified independently even though it is
 * shared.
 *
 * D3 IS BUILT IN AND INHERITED, NOT CHOSEN: `udiv(a, 0)` is `2^W - 1` and
 * `urem(a, 0)` is `a`, straight out of divider.jl:15-18 and :44-46 — they fall
 * out of the loop, since `r >= 0` always succeeds. The signed pair inherits
 * that through the sign-magnitude wrapper.
 *
 * SIGNED IS SIGN-MAGNITUDE, which IS the kernel's shape (aggregate.jl:69-117)
 * and is also exactly C's truncating division — so the cross-check against
 * `int64_t` is a genuinely independent derivation wherever C defines the
 * answer. It is not defined for `typemin / -1`, which the kernel computes as
 * `typemin` (wrap, not poison); the suite pins that case separately. */
cq_ref_w cq_ref_w_udiv(cq_ref_w a, cq_ref_w b, int W);
cq_ref_w cq_ref_w_urem(cq_ref_w a, cq_ref_w b, int W);
cq_ref_w cq_ref_w_sdiv(cq_ref_w a, cq_ref_w b, int W);
cq_ref_w cq_ref_w_srem(cq_ref_w a, cq_ref_w b, int W);

/* K5, width-generic to 128. `F` is the source width, `T` the destination. */
cq_ref_w cq_ref_w_zext (cq_ref_w a, int F, int T);
cq_ref_w cq_ref_w_sext (cq_ref_w a, int F, int T);
cq_ref_w cq_ref_w_trunc(cq_ref_w a, int F, int T);

#endif /* CQOPS_TEST_REFMODEL_H */
