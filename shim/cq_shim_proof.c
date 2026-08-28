/* shim/cq_shim_proof.c — see cq_shim_proof.h for why this exists, why it is
 * here rather than in src/, and what its three rows mean. */

#include "cq_shim_proof.h"

#include "cq_shim_record.h"
#include "cq_shim_reduce.h"
#include "reg.h"
#include "shadow.h"

int cq_shim_shadow_proof(const cq_ctx *ctx, int32_t h, uint32_t q)
{
    cq_shadow s = cq_shadow_get(&ctx->shadow, q);

    /* Per-qubit evidence; the rail plays no part until landing 2's certificate,
     * which is keyed by handle. See the header. */
    (void)h;

    if (s.unknown) return CQ_PROOF_UNPROVEN;
    return s.value == 0 ? CQ_PROOF_CLEAN : CQ_PROOF_DIRTY;
}

/* ------------------------------------------------------------------------- */
/* D15's OBSERVED UNDO CERTIFICATE. The engine is shim/cq_shim_reduce.c, ported
 * from the pinned third_party/cq_free_pairing/free_pairing_check.py; this is
 * the entry-condition layer and the birth-value decision. */

/* Is the rail's whole classical birth value zero? An i128 register reaches this
 * through the template surface, so both words are read; `cq_reg_alloc_const`
 * takes two and the certificate must not look at only the low one. */
static int birth_is_zero(const cq_hist *r)
{
    return r->birth_lo == 0u && r->birth_hi == 0u;
}

/* U3, the classical-immediate fast path: "born cqrt_alloc_W(L), sum of addc
 * immediates is -L, no other write". Stated arithmetically rather than as a sum
 * so that `xorc` joins `addc` in the same walk — upstream's A1 admits both, and
 * separating them would make the same rail's verdict depend on which classical
 * opcode CQ_lang happened to emit.
 *
 * IT IS AN ENTRY CONDITION, NOT A RULE. Returning 0 here means "not this
 * shape"; the caller falls through to the reduction, exactly as upstream's A1
 * falls through to A2. Only `*val` is an answer. */
static int classical_only(int32_t h, const cq_hist *r, uint32_t now,
                          uint64_t *val)
{
    uint32_t pos[64];
    uint32_t n = cq_rec_writes_in(h, r->birth_pos, now, pos, 64u);
    uint64_t mask;

    if (r->birth_hi != 0u) return 0;      /* the arithmetic is one word */
    if (n == 0u || n > 64u) return 0;
    mask = r->width >= 64u ? ~(uint64_t)0u : ((uint64_t)1u << r->width) - 1u;

    *val = r->birth_lo & mask;
    for (uint32_t i = 0; i < n; i++) {
        const cq_call_rec *c = cq_rec_at(pos[i]);
        if (c->op == CQ_ROP_ADDC)      *val = (*val + c->imm) & mask;
        else if (c->op == CQ_ROP_XORC) *val = (*val ^ c->imm) & mask;
        else return 0;
    }
    return 1;
}

int cq_shim_certificate(const cq_ctx *ctx, int32_t h, uint32_t q)
{
    const cq_hist *r = cq_rec_hist(h);
    uint32_t now = cq_rec_len();
    uint64_t val;

    /* PER-RAIL EVIDENCE. `q` is unused here for the mirror-image reason the
     * shadow proof ignores `h`: the certificate reasons about a HANDLE's
     * observed history, and every qubit of one rail shares it. */
    (void)ctx; (void)q;

    /* A HANDLE THE RECORDER NEVER SAW IS UNPROVEN, NOT CLEAN. Not every rail in
     * the process comes through a recorded entry point — a test driving M07
     * directly does not, and neither does anything a future caller adds — and
     * answering CLEAN for a stream we have no history of is precisely the
     * "always yes" degeneration `bd 06t` names a negative control against. */
    if (!r) return CQ_PROOF_UNPROVEN;

    /* THE EFFECT TABLE'S COMPLETENESS IS NOT RE-ASKED HERE, AND THAT IS A
     * DELIBERATE REMOVAL RATHER THAN AN OMISSION. A draft consulted
     * `cq_rec_table_is_complete()` on every free. A mutant deleting that call
     * survived, correctly and permanently: the predicate is TRUE for every
     * table this codebase can compile, so the branch can never be taken and no
     * test can ever reach it. An untestable runtime guard is worse than none —
     * it reads as coverage.
     *
     * THE CLAIM IS REAL AND IT MOVED TO WHERE IT CAN FAIL: the function stays
     * (`shim/cq_shim_record.c`) and `tests/test_shim_cert.c` asserts it
     * directly, which is a test that goes RED the moment a row is added with no
     * write — the widening direction. */

    /* UPSTREAM'S R1, REPLACED BY A PAST-ONLY WITNESS (see cq_shim_reduce.h,
     * divergence (b)). A rail carrying a non-diagonal rotation that was READ
     * after it is entangled with the reader and the reduction cannot see it:
     * the engine reasons over WRITES to a rail and never asks what read it.
     * Strictly stronger than upstream's correlation closure, and it closes the
     * one hole D15 §5 demonstrates upstream leaving open. */
    if (r->first_rot >= 0 && r->last_read > r->first_rot)
        return CQ_PROOF_UNPROVEN;

    /* U3. */
    if (classical_only(h, r, now, &val))
        return val == 0u ? CQ_PROOF_CLEAN : CQ_PROOF_DIRTY;

    /* U1 and U2 — the same call into the same engine. */
    if (!cq_reduce_to_identity(h, r->birth_pos, now))
        return CQ_PROOF_UNPROVEN;

    /* THE BIRTH VALUE DECIDES THE SIGN. A reduced history returns the rail to
     * what it was MINTED holding — upstream's obligation stops here and ours
     * does not. This line is D15 §4's carve-out and D15 §3's residue split at
     * the same time: a template rail is born |0> and clears, and ckd.18's
     * `alloc_i32(5); ry(θ); ry(−θ)` reduces just as cleanly and is CONVICTED. */
    return birth_is_zero(r) ? CQ_PROOF_CLEAN : CQ_PROOF_DIRTY;
}

int cq_shim_free_proof(const cq_ctx *ctx, int32_t h, uint32_t q)
{
    int cert = cq_shim_certificate(ctx, h, q);
    int shad = cq_shim_shadow_proof(ctx, h, q);

    if (cert < 0 || shad < 0) return CQ_PROOF_DIRTY;
    if (cert > 0 || shad > 0) return CQ_PROOF_CLEAN;
    return CQ_PROOF_UNPROVEN;
}
