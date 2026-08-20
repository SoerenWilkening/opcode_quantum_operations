/* src/sandwich.c — M09. The driver, plan §0.1 step for step. See sandwich.h. */

#include "sandwich.h"

#include "controlled.h"

#include "bit.h"
#include "emit.h"

#include <stdio.h>
#include <stdlib.h>

#if defined(CQOPS_DEBUG_INVARIANTS) && CQOPS_DEBUG_INVARIANTS
#  define CQ_SW_DEBUG 1
#else
#  define CQ_SW_DEBUG 0
#endif

/* THE SCRATCH REGION'S `proven_zero` CONSTANT, and it is irreducible (PRD §10).
 * Rule 13 forbids the library holding the gate stream that would let it
 * COMPUTE the answer, so "a sandwich cleans its own scratch" is asserted
 * exactly once, by the code that owns the construction.
 *
 * IT WAS THE SOLE ONE IN src/ UNTIL STEP 20. M06's CQ_ZERO_BY_CTRL_UNCOMPUTE is
 * the second and the last: §9's shared Toffoli ancilla and its nested AND flag
 * are cleaned by a construction M09 does not own and cannot see. PRD §10's rule
 * is not "one constant" — it is that a constant may exist only where the code
 * that RUNS a construction can assert that construction's premises, which is
 * exactly why M08 ships no release a kernel could call.
 *
 * IT RESTS ON THREE PREMISES, AND DELETING ANY ONE MAKES IT A LAUNDERING SITE:
 *
 *   1. ONE GATE PER STEP (ckd.14a). The reverse pass re-calls compute with the
 *      same index, so a multi-gate step must be an involution and generally is
 *      not — K06.md:566-586, K10.md:153-171.
 *   2. I6(a), target side. Every compute-half gate target is a scratch bit, so
 *      no source can be materialised behind the driver's back. Enforced by
 *      emit.h's `const cq_bit *` controls and by the extent armed below.
 *   3. I6(b), control side. Every scratch bit is CQ_BIT_Q for the whole
 *      compute half, because step 1 pre-materialises the region — so no fold
 *      on a scratch bit can fire and the two halves are identical by
 *      construction (risk R8).
 *
 * Under all three, step s emits the identical gate sequence forwards and
 * backwards, X/CX/CCX are each self-inverse, and the region is back to |0>.
 * NOT the _unc epilogue, NOT M07, NOT M08 (which ships no release a kernel can
 * call), and NOT a kernel — there is no API through which one could. */
#define CQ_ZERO_BY_PALINDROME 1

/* Hard errors in BOTH configurations, except where the Debug gate below says
 * otherwise. Every one of them is a miscompile signature. */
static void cq_sw_die(const char *what, long a, long b)
{
    fprintf(stderr, "libcqops: FATAL: sandwich: %s (%ld, %ld)\n", what, a, b);
    abort();
}

/* --- The I6 extent (plan §0.2, run time, Debug). ------------------------- */

static void sw_arm(cq_ctx *ctx, const cq_scratch *scr)
{
#if CQ_SW_DEBUG
    ctx->scratch_lo = scr->bits;
    ctx->scratch_hi = scr->bits + scr->n;
#else
    (void)ctx; (void)scr;
#endif
}

static void sw_disarm(cq_ctx *ctx)
{
#if CQ_SW_DEBUG
    ctx->scratch_lo = NULL;
    ctx->scratch_hi = NULL;
#else
    (void)ctx;
#endif
}

/* --- The region fingerprint (Debug). ------------------------------------- */

#if CQ_SW_DEBUG
/* Walks the region once and establishes two things at once, because they are
 * two views of one premise. It asserts I6(b) — every bit CQ_BIT_Q — and
 * returns an ORDER-SENSITIVE hash of the (kind, index) sequence.
 *
 * ORDER-SENSITIVE IS THE POINT. Swapping two scratch bits leaves the multiset
 * of pairs identical, emits nothing, and keeps every bit CQ_BIT_Q, so a count,
 * an unordered checksum or a bare all-Q sweep would all stay green. What a
 * swap breaks is the correspondence between a step INDEX and the gate that
 * step emits — precisely the premise replay-in-reverse rests on.
 *
 * There is deliberately no second all-Q sweep on entry to the reverse half:
 * the baseline was taken all-Q, and an unchanged fingerprint carries that
 * forward. A check no case can distinguish is a check that survives its own
 * deletion (plan §2, the Step 7 lesson). */
static uint64_t sw_fingerprint(const cq_scratch *scr, const char *where)
{
    uint64_t h = 1469598103934665603ull;      /* FNV-1a, 64-bit */

    for (uint32_t i = 0; i < scr->n; i++) {
        const cq_bit *b = &scr->bits[i];
        if (!cq_bit_is_qubit(*b))
            cq_sw_die(where, (long)i, (long)b->kind);
        h = (h ^ (uint64_t)b->kind) * 1099511628211ull;
        h = (h ^ (uint64_t)b->q)    * 1099511628211ull;
    }
    return h;
}

static void sw_verify(const cq_scratch *scr, uint64_t want, const char *where)
{
    if (sw_fingerprint(scr, where) != want)
        cq_sw_die(where, (long)scr->n, 0);
}

#  define SW_BASELINE(scr) \
       sw_fingerprint((scr), "I6(b): a scratch bit is not CQ_BIT_Q after "     \
                             "pre-materialisation (index, kind)")
#  define SW_VERIFY(scr, fp, where) sw_verify((scr), (fp), (where))
#else
#  define SW_BASELINE(scr)          ((void)(scr), 0u)
#  define SW_VERIFY(scr, fp, where) ((void)(scr), (void)(fp))
#endif

/* --- The driver. --------------------------------------------------------- */

void cq_sandwich(cq_ctx *ctx, cq_scratch *scr,
                 cq_step_fn compute, int n_compute,
                 cq_step_fn copyout, int n_copyout,
                 void *env)
{
    /* 0. The driver's own premises. */
    if (!scr || scr->n == 0u)
        cq_sw_die("no scratch region", 0, 0);
    if (!compute)
        cq_sw_die("compute step function is NULL", n_compute, 0);
    if (n_compute < 0 || n_copyout < 0)
        cq_sw_die("negative step count (compute, copyout)", n_compute, n_copyout);
    if (n_copyout > 0 && !copyout)
        cq_sw_die("copyout steps with no copyout function", n_copyout, 0);
    if (ctx->sandwich_depth != 0)
        cq_sw_die("nested sandwich — the inner region's release would break "
                  "the outer palindrome", ctx->sandwich_depth, 0);

    ctx->sandwich_depth = 1;

    /* 0b. PRD §9 ROW 0: A SKIPPED REGION COSTS ZERO QUBITS, NOT ONLY ZERO
     * GATES, and this is the only place that can deliver the second half.
     * Every cq_emit_* already returns immediately under a CQ_BIT_ZERO control,
     * so the gates are gone — but step 1 below takes the whole scratch region
     * from the pool before any gate is emitted, and §9's row 0 says "0 gates,
     * 0 qubits". Returning here is what makes that literally true through the
     * kernel surface: `dst` is untouched, which is exactly right for a region
     * that does not run, and the caller's cq_scratch_dispose still finds every
     * bit at CQ_BIT_ZERO because nothing materialised one.
     *
     * The entry checks above have ALREADY run, deliberately: a malformed call
     * is a caller bug whether or not its region is skipped, and a guard that
     * switches off under a control is a guard with a hole in it. */
    if (cq_ctrl_skipping(&ctx->ctrl)) { ctx->sandwich_depth = 0; return; }

    /* 1. Pre-materialise the whole region (I6(b), risk R8).
     *
     * VERIFY EVERY BIT BEFORE MATERIALISING ANY, which is M07's two-pass free
     * one module over: a region that fails halfway would already have taken
     * qubits nothing is going to give back. The check ALSO has to be here
     * rather than left to cq_materialise, and a CQ_BIT_ONE bit is why —
     * cq_materialise accepts a constant, emits an X and hands back a qubit in
     * |1>, so I6(b)'s "scratch is born BIT_ZERO, so materialisation emits no
     * X" would be silently false. (An already-quantum bit WOULD be caught one
     * layer down; the ONE case is the one with nothing underneath it.) */
    for (uint32_t i = 0; i < scr->n; i++) {
        if (!cq_bit_is_zero(scr->bits[i]))
            cq_sw_die("scratch bit is not CQ_BIT_ZERO on entry (index, kind)",
                      (long)i, (long)scr->bits[i].kind);
    }
    for (uint32_t i = 0; i < scr->n; i++)
        cq_materialise(ctx, &scr->bits[i]);   /* born 0, so zero gates */

    uint64_t fp = SW_BASELINE(scr);

    /* 2. Forward compute half, extent armed. */
    sw_arm(ctx, scr);
    for (int s = 0; s < n_compute; s++) compute(ctx, env, s);
    SW_VERIFY(scr, fp, "the forward compute half rewrote the scratch region");

    /* 3. Copy out, extent DISARMED — copyout targets `dst`, outside scratch.
     *    See sandwich.h for why widening the extent instead is the wrong fix.
     *    The fingerprint still runs afterwards, so a copyout step that reaches
     *    into scratch is caught even though the extent cannot see it. */
    sw_disarm(ctx);
    for (int s = 0; s < n_copyout; s++) copyout(ctx, env, s);
    SW_VERIFY(scr, fp, "a copyout step rewrote the scratch region");

    /* 4. Reverse compute half — the SAME step function at descending indices.
     *    Reversal is structural: no kernel writes it, so no kernel can get it
     *    wrong. */
    sw_arm(ctx, scr);
    for (int s = n_compute - 1; s >= 0; s--) compute(ctx, env, s);
    SW_VERIFY(scr, fp, "the reverse compute half rewrote the scratch region");
    sw_disarm(ctx);

    /* 5. Release, IN REVERSE INDEX ORDER. The free list is LIFO (D4), so
     *    pushing n-1 .. 0 leaves index 0 on top and the next sandwich acquires
     *    the same indices in the same ascending order — two identical kernels
     *    emit an identical stream, which keeps an L6 trace diff quiet and an
     *    L4 golden reproducible.
     *
     *    The kind check is a both-configuration hard error because in Release
     *    the fingerprint is compiled out and cq_bit_qindex would happily read
     *    a constant's canonical q == 0 and release someone else's qubit. */
    for (uint32_t i = scr->n; i-- > 0; ) {
        cq_bit *b = &scr->bits[i];
        if (!cq_bit_is_qubit(*b))
            cq_sw_die("scratch bit is not a qubit at release (index, kind)",
                      (long)i, (long)b->kind);
        cq_ctx_release_qubit(ctx, cq_bit_qindex(*b), CQ_ZERO_BY_PALINDROME);
        *b = cq_bit_zero();
    }

    ctx->sandwich_depth = 0;
}
