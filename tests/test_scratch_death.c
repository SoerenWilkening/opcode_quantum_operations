/* tests/test_scratch_death.c — M08's fail-loud paths, Step 8.
 *
 * ALL OF THESE ABORT IN BOTH CONFIGURATIONS. None carries
 * CQ_DEATH_SKIP_WITHOUT_INVARIANTS, and that is deliberate: plan §2.1 gates
 * the I2 owner map, the I6 extent check and the §3 distinctness asserts on
 * CQOPS_DEBUG_INVARIANTS, and none of these is one of those. A scratch region
 * disposed with a live qubit still in it is a leaked ancilla — the one
 * unforgivable bug (NORTH_STAR §3) — and Rule 17 pins L4 under Release, where
 * a Debug-gated assert is simply absent.
 *
 * THE DISPOSE CHECK IS A KIND CHECK, NEVER A SHADOW READ. That is not a
 * shortcut: after any sandwich half on a tainted operand every scratch bit
 * reads `unknown`, because §3's CX rule makes poison sticky, so a literal
 * Rule-6 shadow check would fire on every legitimate kernel (bd ckd.17,
 * K09.md:603, K11.md:677). The kind is the thing M08 can actually see.
 */

#include "scratch.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "support/death.h"

static cq_sink g_sink;
static cq_ctx  g_ctx;

/* A sink that records nothing and accepts everything: these cases are about
 * M08's own state, not about what reached the sink. */
static void nx  (void *u, uint32_t q)                        { (void)u; (void)q; }
static void ncx (void *u, uint32_t c, uint32_t t)            { (void)u; (void)c; (void)t; }
static void nccx(void *u, uint32_t a, uint32_t b, uint32_t t){ (void)u; (void)a; (void)b; (void)t; }
static void nry (void *u, uint32_t q, double th)             { (void)u; (void)q; (void)th; }
static void nrz (void *u, uint32_t q, double ph)             { (void)u; (void)q; (void)ph; }
static void nmz (void *u, uint32_t q)                        { (void)u; (void)q; }

static void setup(void)
{
    g_sink.x = nx; g_sink.cx = ncx; g_sink.ccx = nccx;
    g_sink.ry = nry; g_sink.rz = nrz; g_sink.mz = nmz;
    g_sink.user = NULL;
    cq_ctx_init(&g_ctx, &g_sink);
}

/* THE case that goes red if M08's dispose check is deleted. A kernel that
 * grabs a region and materialises into it without going through the driver
 * leaks a qubit: M09's epilogue is the only thing that returns one, and it
 * never ran. Nothing else in the project would notice — M03 is never told,
 * M07 never sees this array — so there is no layer below to mask the
 * mutation, unlike M07's free (plan §2, deviation 5). */
static void dispose_with_a_materialised_bit(void)
{
    setup();
    cq_scratch scr;
    cq_scratch_alloc(&scr, 4u);

    cq_materialise(&g_ctx, cq_scratch_span(&scr, 2u, 1u));

    CQ_EXPECT_ABORT(cq_scratch_dispose(&scr));
}

/* The second half of "back to CQ_BIT_ZERO". A region left holding a constant
 * ONE owns no qubit, so nothing leaks — but the next allocation of that region
 * would pre-materialise it to |1⟩, and I6(b)'s "scratch is born BIT_ZERO, so
 * materialisation emits no X" would be false. Caught here rather than three
 * steps later inside a kernel. */
static void dispose_with_a_constant_one_bit(void)
{
    setup();
    cq_scratch scr;
    cq_scratch_alloc(&scr, 4u);

    cq_emit_x(&g_ctx, cq_scratch_span(&scr, 0u, 1u));   /* ZERO -> ONE, 0 gates */

    CQ_EXPECT_ABORT(cq_scratch_dispose(&scr));
}

/* A zero-width region makes every premise the driver asserts vacuous, and no
 * kernel wants one: a construction with no scratch needs no sandwich (PRD §5
 * lists and/or/xor as naturally clean). Refused at the point the mistake is
 * made. */
static void alloc_of_a_zero_width_region(void)
{
    setup();
    cq_scratch scr;
    CQ_EXPECT_ABORT(cq_scratch_alloc(&scr, 0u));
}

static void span_starts_past_the_end(void)
{
    setup();
    cq_scratch scr;
    cq_scratch_alloc(&scr, 4u);
    CQ_EXPECT_ABORT((void)cq_scratch_span(&scr, 5u, 1u));
}

/* The off-by-one a kernel's layout arithmetic actually produces: the offset is
 * in range and the length walks off the end. */
static void span_length_runs_off_the_end(void)
{
    setup();
    cq_scratch scr;
    cq_scratch_alloc(&scr, 4u);
    CQ_EXPECT_ABORT((void)cq_scratch_span(&scr, 3u, 2u));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(dispose_with_a_materialised_bit),
    CQ_DEATH_CASE(dispose_with_a_constant_one_bit),
    CQ_DEATH_CASE(alloc_of_a_zero_width_region),
    CQ_DEATH_CASE(span_starts_past_the_end),
    CQ_DEATH_CASE(span_length_runs_off_the_end)
)
