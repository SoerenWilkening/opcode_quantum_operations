/* tests/support/poolcheck.h — plan §2.2. The automatic L2 and L3 assertions.
 *
 * THE PRIME DIRECTIVE'S ENFORCEMENT ARM. L1 compares a value and can be
 * exactly right while the circuit is wrong: a leaked scratch qubit, a reverse
 * half that did not cancel, a rail freed while dirty — none of them move a
 * value. Everything in this file asserts the POOL instead, which is the half
 * that can see them. A suite that reports L1 green and never calls in here has
 * verified nothing that matters.
 */
#ifndef CQOPS_TEST_POOLCHECK_H
#define CQOPS_TEST_POOLCHECK_H

#include "ctx.h"
#include "support/refmodel.h"

#include <stdint.h>

/* Everything the pool knows, at one instant. */
typedef struct {
    uint32_t live, minted, n_free;
    uint32_t peak;
} cq_pc_snap;

cq_pc_snap cq_pc_take(const cq_ctx *ctx);

/* `live` equal, and ONLY `live` — the other three are monotone or derived, and
 * an earlier draft of this file compared all of them, which cannot hold and
 * failed 1.28 million cases on its first run.
 *
 * `minted` and `peak` never decrease (src/qubits.h: "minted == live + free"
 * and "peak == minted"), so a round trip that allocates dst's qubits and hands
 * them back necessarily leaves minted HIGHER and n_free higher by the same
 * amount. Requiring them to match would be requiring the kernel never to
 * allocate. What "the pool is restored" actually means is that nothing stayed
 * live — which is `live` — plus the far sharper claim that the specific
 * indices came back, which is cq_pc_indices_are_free below and not a count at
 * all. */
int cq_pc_same(cq_pc_snap a, cq_pc_snap b);

/* Collects the qubit indices a register currently holds, so L3 can name them
 * BEFORE the free tombstones the rail and check them after. Returns how many
 * were written; aborts rather than truncating if `cap` is too small. */
uint32_t cq_pc_indices(const cq_ctx *ctx, int32_t h, uint32_t *out, uint32_t cap);

/* L3's real assertion: every one of these indices is back on the free list.
 *
 * STRONGER THAN ANY COUNT, and that is the point. A free that released the
 * wrong index, or released one of dst's twice and none of another, restores
 * `live` exactly and passes every count comparison — this does not. Reports
 * through the harness naming the index that did not come back. */
int cq_pc_indices_are_free(const cq_ctx *ctx, const uint32_t *idx, uint32_t n);

/* L1's read side. The VALUE of a register: constant bits contribute their
 * kind, qubit-carrying bits their shadow value.
 *
 * A qubit whose shadow reads `unknown` records a harness failure and
 * contributes 0. That is not a fudge — on the rotation-free surface (Steps
 * 10-17) it cannot happen, because cq_shadow_rotate is the ONLY producer of
 * `unknown` in the library and M22 does not exist yet. If it ever fires, the
 * suite has found something real, and failing loudly beats returning a number
 * that quietly means nothing. */
uint64_t cq_pc_value(const cq_ctx *ctx, int32_t h);

/* The same at any width up to 128 — and the one-word form is a WRAPPER for
 * this, not a parallel implementation. Casts reach i80 and i128, and two
 * readers would be two places for an off-by-one at the 64-bit seam. */
cq_ref_w cq_pc_value_w(const cq_ctx *ctx, int32_t h);

/* L2, as a SET rather than a count. Is the pool's live-index set exactly the
 * union of the indices held by these `n` registers?
 *
 * PRD §11 words L2 as "the live-qubit set equals exactly dst's qubits", which
 * is literally false whenever a and b are quantum and still live — they own
 * their indices and are nobody's leak. The operative claim is the one this
 * function makes: NO INDEX IS LIVE THAT NO NAMED REGISTER OWNS, and every
 * index a named register owns is live. A leaked ancilla shows up as an extra
 * live index owned by nothing; a double-released one as an owned index sitting
 * on the free list.
 *
 * A count comparison would be weaker in exactly the way that matters: leak one
 * index and mistakenly hand another back and the totals still agree. Returns 1
 * on success; on failure it records a harness failure naming the offending
 * index, so a red run says WHICH qubit. */
int cq_pc_live_is_exactly(const cq_ctx *ctx, const int32_t *hs, uint32_t n);

/* L3's evidence, and read the scope before reusing it. A cq_zero_proof valid
 * ONLY on the rotation-free surface — which is Steps 10 through 17, every
 * kernel and no rotation.
 *
 * Why it is sound there and nowhere else: `unknown` has exactly one producer,
 * cq_shadow_rotate (src/shadow.c:121), so in a program containing no rotation
 * every shadow entry is determinate and tracks the true computational-basis
 * state exactly — X, CX and CCX on determinate inputs ARE the classical
 * permutation. On that surface cq_shadow_known_zero is not conservative, it is
 * exact, and it is a genuine proof.
 *
 * The moment M22 lands (Step 19), a rotation anywhere upstream poisons the
 * rail and this predicate starts refusing legitimate frees — bd ckd.17b and
 * ckd.18 are that problem, and they are OPEN. It is NOT the answer to them,
 * and it must never be promoted into src/: the library defines no proof at
 * all, and NULL meaning "no evidence, fail loud" is the correct posture for an
 * unresolved P0 (src/reg.h). Its predecessor lives as a static in
 * tests/test_reg.c under a name saying it was valid only while no kernel
 * existed; this one is the Step 10 successor, with the wider but still bounded
 * scope that a kernel-but-no-rotation surface allows. */
int cq_pc_zero_proof_rotation_free(const cq_ctx *ctx, int32_t h, uint32_t q);

#endif /* CQOPS_TEST_POOLCHECK_H */
