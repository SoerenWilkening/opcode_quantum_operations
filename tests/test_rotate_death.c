/* tests/test_rotate_death.c — M22's hard errors, Step 19.
 *
 * FOUR FAMILIES, AND THEY ARE NOT THE SAME KIND OF THING.
 *
 * (1) A ROTATION INSIDE A SANDWICH COMPUTE HALF. M22's own guard, and the only
 *     one it owns outright. It is needed because M22 is the first module in
 *     src/ that emits without going through cq_emit_*, so NEITHER of the two
 *     mechanisms enforcing I6 covers it: not the `const cq_bit *` control
 *     typing, and not the Debug scratch-extent check, which is armed only from
 *     inside cq_emit_*. The hazard is real and specific — `Ry(θ)` is not an
 *     involution, so `cq_sandwich`'s reverse pass re-emits it and the halves
 *     compose to `Ry(2θ)` instead of cancelling. That is risk R1 with every
 *     detector blind: L1 green, the palindrome PERFECT (the stream really is a
 *     palindrome), scratch dirty. Both configurations, because ctx->
 *     sandwich_depth is not Debug-gated.
 *
 * (2) A GENERAL-Ry RAIL CANNOT BE FREED — `bd ckd.18`, CLOSED 2026-08-22 into
 *     PRD §15 D15. **THE PREDICTION THIS HEADER CARRIED CAME TRUE AT STEP 23,
 *     AND BOTH OF THE TWO THINGS IT NAMED AS LIVE ARE NOW SETTLED.** It said
 *     abort-vs-strand for a proven-dirty rail was unconfirmed: D15 §4's last
 *     clause was CONFIRMED AS (b) STRAND on 2026-08-22, so proven-dirty and
 *     unproven take the same act and neither aborts by default. It also said a
 *     unit test emits no call stream, so this rail may land in the UNPROVEN row
 *     rather than the dirty one: that is exactly where it lands, and the reason
 *     is worth keeping — it is born |0> with ONE UNCANCELLED Ry, while the
 *     corpus's ckd.18 rails are born from a non-zero literal and carry a
 *     cancelling pair, which is what D15 §4 convicts on.
 *
 *     SO THE CASE WAS KEPT AS THE SHADOW-EVIDENCE REFUSAL IT ACTUALLY IS, which
 *     is `bd 06t`'s option (a), taken deliberately rather than by default — the
 *     bead demanded a choice between that and giving the fixture the corpus's
 *     shape. What it now runs under is CQOPS_FREE_ABORT, the flag PRD §15 D15
 *     §3 requires anyway, so nothing was deleted and the refusal keeps its
 *     detector. The DEFAULT disposition for the same fixture — it STRANDS, by
 *     index — is pinned in the ordinary suite as
 *     tests/test_rotate.c:a_general_ry_rail_strands_rather_than_aborting.
 *     Re-read both when the certificate lands; do not delete either.
 *
 * (3) THE ORDINARY LIFETIME GUARDS, which are M07's rather than M22's — a
 *     freed handle, a measured rail — reached through M22's entry points so
 *     that M22 is shown to route to them rather than to have its own copy.
 *
 * (4) MEASUREMENT IS TERMINAL: a free after a measure, and a second measure.
 *
 * NO CQ_DEATH_SKIP_WITHOUT_INVARIANTS ANYWHERE IN THIS FILE, and that is
 * checked rather than assumed: every abort below is a both-configuration one.
 *
 * WHAT NO REGEX CAN SEPARATE, said out loud rather than dressed up.
 * `ckd18_a_general_ry_rail_cannot_be_freed` and
 * `a_null_proof_still_refuses_a_materialised_rail` abort at the SAME site with
 * the SAME message, because cq_reg_clean's `!proof ||` short-circuits at the
 * first qubit-carrying bit exactly as a failing proof does. The
 * FAIL_REGULAR_EXPRESSION they carry is therefore a NEGATIVE one — it pins that
 * the abort came from M07's two-pass check and NOT from M03's release guard,
 * which is the Step 7 discriminator and is the distinction that matters.
 */

#include "rotate.h"

#include "angle.h"
#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "reg.h"
#include "sandwich.h"
#include "scratch.h"
#include "shadow.h"

#include "cqops/cqops.h"

#include "support/death.h"
#include "support/poolcheck.h"

#include <stdint.h>
#include <stdio.h>

/* A DISCARDING sink, the same shape test_reg_death.c uses and for the same
 * reason: most cases here emit real gates while building the rail they then
 * misuse, so a sink that treated emission as a failure would fail the setup
 * rather than the assertion. Passing NULL to cq_ctx_init is not an option — it
 * resolves CQOPS_SINK, and "no default sink registered" is itself a hard error,
 * outside the armed window. (bd cue tracks consolidating these; this is the ELEVENTH copy — the same
 * stub set is in ten other death suites.) */
static void nx(void *u, uint32_t q) { (void)u; (void)q; }
static void ncx(void *u, uint32_t c, uint32_t t) { (void)u; (void)c; (void)t; }
static void nccx(void *u, uint32_t a, uint32_t b, uint32_t t)
{ (void)u; (void)a; (void)b; (void)t; }
static void nry(void *u, uint32_t q, double th) { (void)u; (void)q; (void)th; }
static void nrz(void *u, uint32_t q, double ph) { (void)u; (void)q; (void)ph; }
/* THE MZ TRIPWIRE. `measure_of_a_measured_rail` needs to assert not just that a
 * second measure aborts, but that it aborts having emitted NOTHING — which is
 * what cq_measure's mark-before-read order buys and what a refactor moving the
 * mark below the loop would silently cost. A gate count cannot see it (the
 * process is gone), so the sink itself reports, and ctest's
 * FAIL_REGULAR_EXPRESSION turns that report into a failure. The flag is armed
 * only after the LEGITIMATE first measure, which of course emits `mz` on every
 * qubit — without it the marker would fire on the correct behaviour. */
static int g_mz_forbidden = 0;
static void nmz(void *u, uint32_t q)
{
    (void)u; (void)q;
    if (g_mz_forbidden) fprintf(stderr, "MZ-EMITTED-PAST-THE-ABORT-POINT\n");
}

static cq_sink g_sink;

static void open_ctx(cq_ctx *ctx)
{
    g_sink.x  = nx;  g_sink.cx = ncx; g_sink.ccx = nccx;
    g_sink.ry = nry; g_sink.rz = nrz; g_sink.mz  = nmz;
    g_sink.user = NULL;
    cq_ctx_init(ctx, &g_sink);
}

/* Every case runs this first, OUTSIDE the armed window, so that an abort from
 * setup cannot masquerade as the death under test. It exercises rows that must
 * NOT abort — a fold on a constant, an Rz on a constant, and a general Ry on a
 * QUBIT — so a guard that fired on perfectly good input is caught here rather
 * than counted as a pass.
 *
 * The general-Ry call is the one that matters: it is the only row that
 * materialises and the only row that poisons, so it is the row a
 * hair-trigger guard would most plausibly reject. Its rail is deliberately
 * NOT freed — a poisoned rail cannot be, which is ckd.18 and is the subject of
 * a case below rather than of the setup. */
static void preflight(cq_ctx *ctx)
{
    int32_t ok = cq_reg_alloc_zero(&ctx->regs, 2);
    cq_rotate_ry(ctx, ok, 0.0);                    /* identity: nothing        */
    cq_rotate_rz(ctx, ok, 1.25);                   /* constant column: nothing */
    if (cq_reg_owned_qubits(&ctx->regs, ok) != 0u)
        fprintf(stderr, "PREFLIGHT-FOLD-ALLOCATED\n");
    cq_reg_free(ctx, ok, NULL);                    /* all-constant: no evidence needed */

    int32_t q = cq_reg_alloc_zero(&ctx->regs, 2);
    cq_bit *qb = cq_reg_bits(&ctx->regs, q);
    cq_materialise(ctx, &qb[0]);
    cq_rotate_ry(ctx, q, 0.5);                     /* general, on a qubit      */
    if (cq_reg_owned_qubits(&ctx->regs, q) != 2u)
        fprintf(stderr, "PREFLIGHT-GENERAL-RY-DID-NOT-MATERIALISE\n");
}

/* --- (1) a rotation inside a sandwich compute half ----------------------- */

typedef struct { cq_scratch *scr; int what; int32_t h; } sw_env;

enum { SW_RY = 0, SW_RZ = 1, SW_MEASURE = 2 };

static void rotating_step(cq_ctx *ctx, void *env, int step)
{
    sw_env *e = (sw_env *)env;
    cq_bit *b = cq_scratch_span(e->scr, 0, 1);
    (void)step;

    if (e->what == SW_MEASURE) {
        uint64_t lo, hi;
        cq_measure(ctx, e->h, &lo, &hi);
    } else if (e->what == SW_RZ) {
        cq_rotate_rz_bit(ctx, b, 0.5);
    } else {
        cq_rotate_ry_bit(ctx, b, 0.5);
    }
}

static void copyout_nothing(cq_ctx *ctx, void *env, int step)
{
    (void)ctx; (void)env; (void)step;
}

static void rotate_in_a_sandwich(int what)
{
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    cq_scratch scr;
    cq_scratch_alloc(&scr, 1);
    sw_env env = { &scr, what, cq_reg_alloc_zero(&ctx.regs, 2) };

    CQ_EXPECT_ABORT(cq_sandwich(&ctx, &scr, rotating_step, 1,
                                copyout_nothing, 0, &env));
}

static void ry_inside_a_sandwich_compute_half(void)      { rotate_in_a_sandwich(SW_RY); }
static void rz_inside_a_sandwich_compute_half(void)      { rotate_in_a_sandwich(SW_RZ); }
static void measure_inside_a_sandwich_compute_half(void) { rotate_in_a_sandwich(SW_MEASURE); }

/* --- (2) bd ckd.18 -------------------------------------------------------- */

/* A rail a general Ry has touched is poisoned, so the evidence THIS SUITE
 * supplies — the two-bit shadow — cannot prove it clean and the free is a hard
 * error. That is Rule 6 working, not failing.
 *
 * TWO CORRECTIONS, BOTH PRD §15 D15, both to sentences this comment used to
 * carry. (i) It said "no evidence can prove it clean". That is an unqualified
 * universal and it is false: the shadow is not the only possible evidence, and
 * D15's certificate reads the CALL STREAM instead. On this fixture the call
 * stream is enough to CONVICT rather than merely to fail to clear: the rail is
 * born |0>, its only write is one general Ry at a non-zero angle, and nothing
 * else touches it — so the stream names the state it is left in, and that
 * state is not |0>. Failing to reduce to the identity would only make it
 * UNPROVEN; it is the positive evidence that puts it in D15 §3's PROVEN-DIRTY
 * row instead. (ii) It said this was
 * "exactly the shape" of the corpus's `ry`-rooted frees. It is not: those are
 * born from a NON-ZERO literal and carry a CANCELLING (θ, −θ) pair, and both
 * differences are what D15 §4 convicts on.
 *
 * WHAT THE EVIDENCE IN THIS TREE ACTUALLY SAYS, at Step 23: UNPROVEN. The only
 * proof available is the shadow, which reports `unknown` for a rotated wire and
 * can never say more than "I cannot tell" — so this fixture reaches D15 §3's
 * unproven row, not its dirty one, and the sentence above describes what the
 * CERTIFICATE would say about a DIFFERENT rail. That distinction is why the
 * case runs under CQOPS_FREE_ABORT rather than asserting a conviction it has no
 * evidence for. D15 §4's disposition clause is CONFIRMED (strand) as of
 * 2026-08-22; this header's paragraph (2) has the whole reckoning. */
static void ckd18_a_general_ry_rail_cannot_be_freed(void)
{
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);
    /* The DEFAULT act for this rail is to strand — pinned by index in
     * tests/test_rotate.c. This case is about the REFUSAL still being
     * reachable, which is what the flag is for. */
    cqops_set_free_abort(1);

    int32_t h = cq_reg_alloc_zero(&ctx.regs, 4);
    cq_rotate_ry(&ctx, h, 0.5);          /* materialises AND poisons all four */

    /* THE PROOF IS SUPPLIED, AND THAT IS THE WHOLE POINT OF THIS CASE. Passing
     * NULL would abort at cq_reg_clean's `!proof ||` short-circuit on the first
     * qubit-carrying bit, without ever reading a shadow — which is what the
     * companion case below asserts, and is a different claim. With the
     * rotation-free proof supplied, the refusal comes from the POISON: the
     * predicate returns 0 only because cq_shadow_known_zero reads `unknown`.
     * Measured: with the proof passed, disabling cq_shadow_rotate in the
     * general-Ry row turns this case red; with NULL it did not. */
    CQ_EXPECT_ABORT(cq_reg_free(&ctx, h, cq_pc_zero_proof_rotation_free));
}

/* The same abort with a proof supplied, so the case does not silently become a
 * test of "NULL means no evidence" instead of a test of the poison. */
static void a_null_proof_still_refuses_a_materialised_rail(void)
{
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    int32_t h = cq_reg_alloc_zero(&ctx.regs, 1);
    cq_bit *b = cq_reg_bits(&ctx.regs, h);
    cq_materialise(&ctx, b);             /* a clean qubit, but no evidence     */

    CQ_EXPECT_ABORT(cq_reg_free(&ctx, h, NULL));
}

/* --- (3) lifetime, through M22's entry points ---------------------------- */

static void ry_of_a_freed_handle(void)
{
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    int32_t h = cq_reg_alloc_zero(&ctx.regs, 2);
    cq_reg_free(&ctx, h, NULL);          /* all-constant: legal, returns nothing */

    CQ_EXPECT_ABORT(cq_rotate_ry(&ctx, h, 0.5));
}

static void rz_of_a_freed_handle(void)
{
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    int32_t h = cq_reg_alloc_zero(&ctx.regs, 2);
    cq_reg_free(&ctx, h, NULL);

    CQ_EXPECT_ABORT(cq_rotate_rz(&ctx, h, 0.5));
}

static void ry_of_a_measured_rail(void)
{
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    int32_t h = cq_reg_alloc_zero(&ctx.regs, 2);
    uint64_t lo, hi;
    cq_measure(&ctx, h, &lo, &hi);

    CQ_EXPECT_ABORT(cq_rotate_ry(&ctx, h, 0.5));
}

static void rz_of_a_measured_rail(void)
{
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    int32_t h = cq_reg_alloc_zero(&ctx.regs, 2);
    uint64_t lo, hi;
    cq_measure(&ctx, h, &lo, &hi);

    CQ_EXPECT_ABORT(cq_rotate_rz(&ctx, h, 0.5));
}

/* --- (4) measurement is terminal ----------------------------------------- */

/* A SECOND MEASURE ABORTS, AND ABORTS HAVING EMITTED ZERO GATES.
 *
 * The rail is MATERIALISED on purpose. With an all-constant rail — which is what
 * this case used to build — a second measure emits no `mz` under either
 * ordering, so the case could not tell mark-before-read from mark-after-read and
 * the ordering had no detector at all. Measured: a mutant moving
 * cq_reg_mark_measured below the loop survived the entire suite in both
 * configurations. It matters because the sink is LIVE — the printf sink has
 * already flushed those lines and a QEC driver has already committed them — so
 * "aborts having emitted zero gates" is a promise to the caller, not an
 * internal detail. The tripwire is in the sink; see g_mz_forbidden above. */
static void measure_of_a_measured_rail(void)
{
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    int32_t h = cq_reg_alloc_zero(&ctx.regs, 2);
    cq_bit *b = cq_reg_bits(&ctx.regs, h);
    cq_materialise(&ctx, &b[0]);
    cq_materialise(&ctx, &b[1]);

    uint64_t lo, hi;
    cq_measure(&ctx, h, &lo, &hi);      /* legitimate: two mz, before arming */

    g_mz_forbidden = 1;
    CQ_EXPECT_ABORT(cq_measure(&ctx, h, &lo, &hi));
}

static void measure_of_a_freed_handle(void)
{
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    int32_t h = cq_reg_alloc_zero(&ctx.regs, 2);
    cq_reg_free(&ctx, h, NULL);

    uint64_t lo, hi;
    CQ_EXPECT_ABORT(cq_measure(&ctx, h, &lo, &hi));
}

static void free_after_measure(void)
{
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    int32_t h = cq_reg_alloc_zero(&ctx.regs, 2);
    uint64_t lo, hi;
    cq_measure(&ctx, h, &lo, &hi);

    CQ_EXPECT_ABORT(cq_reg_free(&ctx, h, NULL));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(ry_inside_a_sandwich_compute_half),
    CQ_DEATH_CASE(rz_inside_a_sandwich_compute_half),
    CQ_DEATH_CASE(measure_inside_a_sandwich_compute_half),
    CQ_DEATH_CASE(ckd18_a_general_ry_rail_cannot_be_freed),
    CQ_DEATH_CASE(a_null_proof_still_refuses_a_materialised_rail),
    CQ_DEATH_CASE(ry_of_a_freed_handle),
    CQ_DEATH_CASE(rz_of_a_freed_handle),
    CQ_DEATH_CASE(ry_of_a_measured_rail),
    CQ_DEATH_CASE(rz_of_a_measured_rail),
    CQ_DEATH_CASE(measure_of_a_measured_rail),
    CQ_DEATH_CASE(measure_of_a_freed_handle),
    CQ_DEATH_CASE(free_after_measure)
)
