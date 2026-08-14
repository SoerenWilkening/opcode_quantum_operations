/* Step 6's remaining 4 of 159, plus the I6 mechanism M05 owns.
 *
 * PRD §3 spells out FOUR distinctness constraints, one per operand pair,
 * "because c1 != c2 != t is prose shorthand and not valid C semantics":
 *
 *     CX (c,t)        c != t
 *     CCX(c1,c2,t)    c1 != c2,  c1 != t,  c2 != t
 *
 * c1 == t is exactly as much a miscompile signature as c1 == c2, which is why
 * there are four and not two. A coincident operand is a meaningless channel:
 * CX(q,q) computes q ^= q = 0 in the shadow while emitting a gate no hardware
 * accepts.
 *
 * EVERY CASE USES Q OPERANDS. Distinctness is a property of BITS, not of
 * kinds, so the check compares qubit indices — it can only ever fire on
 * CQ_BIT_Q operands and cannot fire on two constants. That is correct rather
 * than a gap: two constant bits are genuinely independent channels, and the
 * fold table folds them away before any index exists. (An earlier
 * cq_bit_coincident carried a pointer-identity clause that broke this; Step 6
 * removed it — see src/bit.h.)
 *
 * WHY THE SINK DISARMS THE DEATH WINDOW. Asserting "it aborts" is not enough
 * here, and mutation testing proved it: delete M05's distinctness check and
 * these cases STILL abort, because M02's cq_shadow_cx carries its own c != t
 * assert one layer down. Defence in depth is good, but it made the mutation
 * survive — the test could not tell which layer caught it. What M05 must do is
 * reject BEFORE anything reaches the sink; M02 only fires after cq_sink_cx has
 * already pushed an invalid gate. So the sink below disarms the window on any
 * emission: a gate that escapes turns the later abort into exit 4 instead of
 * exit 0, and the case fails.
 *
 * THE DISTINCTNESS CASES ARE DEBUG-ONLY, deliberately. Plan §2.1 assigns them
 * to CQOPS_DEBUG_INVARIANTS along with the I2 owner map and the I6 check. In
 * Release each reports a skip rather than a pass, so a Release run never
 * claims to have verified something the configuration compiled out (Rule 17).
 */

#include "ctx.h"
#include "emit.h"
#include "support/death.h"

#include <stdio.h>

/* A sink that fails the case if it is ever reached. Disarming is what turns
 * "a gate escaped, then something else aborted" into a failure. */
static void leak(void *u, const char *op)
{
    (void)u;
    fprintf(stderr, "LEAKED GATE: %s reached the sink before the assert\n", op);
    cq_death_disarm();
}
static void lx  (void *u, uint32_t q)                       { (void)q; leak(u, "x"); }
static void lcx (void *u, uint32_t c, uint32_t t)           { (void)c; (void)t; leak(u, "cx"); }
static void lccx(void *u, uint32_t a, uint32_t b, uint32_t t){ (void)a; (void)b; (void)t; leak(u, "ccx"); }
static void lry (void *u, uint32_t q, double th)            { (void)q; (void)th; leak(u, "ry"); }
static void lrz (void *u, uint32_t q, double ph)            { (void)q; (void)ph; leak(u, "rz"); }
static void lmz (void *u, uint32_t q)                       { (void)q; leak(u, "mz"); }

static cq_sink g_sink;
static cq_ctx  g_ctx;

static void setup(void)
{
    g_sink.x = lx; g_sink.cx = lcx; g_sink.ccx = lccx;
    g_sink.ry = lry; g_sink.rz = lrz; g_sink.mz = lmz;
    g_sink.user = NULL;
    cq_ctx_init(&g_ctx, &g_sink);
}

/* Two distinct cq_bit objects naming the SAME qubit. This is the shape the
 * assert has to catch — not `&a == &b`, which a caller would rarely write. */
static cq_bit same_rail_as(const cq_bit *b) { return cq_bit_qubit(cq_bit_qindex(*b)); }

static void cx_control_equals_target(void)
{
    setup();
    cq_bit a = cq_bit_qubit(cq_ctx_fresh_qubit(&g_ctx));
    cq_bit t = same_rail_as(&a);

    CQ_DEATH_SKIP_WITHOUT_INVARIANTS("§3 distinctness is Debug-gated (plan §2.1)");
    CQ_EXPECT_ABORT(cq_emit_cx(&g_ctx, &a, &t));
}

static void ccx_controls_are_the_same_qubit(void)
{
    setup();
    cq_bit a = cq_bit_qubit(cq_ctx_fresh_qubit(&g_ctx));
    cq_bit b = same_rail_as(&a);
    cq_bit t = cq_bit_qubit(cq_ctx_fresh_qubit(&g_ctx));

    CQ_DEATH_SKIP_WITHOUT_INVARIANTS("§3 distinctness is Debug-gated (plan §2.1)");
    CQ_EXPECT_ABORT(cq_emit_ccx(&g_ctx, &a, &b, &t));
}

static void ccx_first_control_equals_target(void)
{
    setup();
    cq_bit a = cq_bit_qubit(cq_ctx_fresh_qubit(&g_ctx));
    cq_bit b = cq_bit_qubit(cq_ctx_fresh_qubit(&g_ctx));
    cq_bit t = same_rail_as(&a);

    CQ_DEATH_SKIP_WITHOUT_INVARIANTS("§3 distinctness is Debug-gated (plan §2.1)");
    CQ_EXPECT_ABORT(cq_emit_ccx(&g_ctx, &a, &b, &t));
}

static void ccx_second_control_equals_target(void)
{
    setup();
    cq_bit a = cq_bit_qubit(cq_ctx_fresh_qubit(&g_ctx));
    cq_bit b = cq_bit_qubit(cq_ctx_fresh_qubit(&g_ctx));
    cq_bit t = same_rail_as(&b);

    CQ_DEATH_SKIP_WITHOUT_INVARIANTS("§3 distinctness is Debug-gated (plan §2.1)");
    CQ_EXPECT_ABORT(cq_emit_ccx(&g_ctx, &a, &b, &t));
}

/* I6, the half M05 owns (plan §3 lists "I6 check" against this module). The
 * extent is set here by hand because M09 — which sets it for real — is Step 8;
 * without this case the mechanism would sit untested for two more steps, and
 * a mutation deleting it survives the whole suite. The OTHER half of I6 needs
 * no test because it is enforced by the type system: cq_emit_* takes controls
 * as `const cq_bit *`, so a source cannot be materialised at all. */
static void i6_target_outside_the_scratch_region(void)
{
    setup();
#if defined(CQOPS_DEBUG_INVARIANTS) && CQOPS_DEBUG_INVARIANTS
    cq_bit scratch[4];
    for (int i = 0; i < 4; i++) scratch[i] = cq_bit_zero();
    cq_bit outside = cq_bit_qubit(cq_ctx_fresh_qubit(&g_ctx));

    g_ctx.scratch_lo = &scratch[0];
    g_ctx.scratch_hi = &scratch[4];

    /* Inside the extent is fine — proves the check discriminates rather than
     * simply aborting on everything once armed. Done before arming. */
    cq_emit_x(&g_ctx, &scratch[2]);

    CQ_EXPECT_ABORT(cq_emit_x(&g_ctx, &outside));
#else
    cq_death_skip("I6 scratch-extent check is Debug-gated (plan §2.1)");
#endif
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(cx_control_equals_target),
    CQ_DEATH_CASE(ccx_controls_are_the_same_qubit),
    CQ_DEATH_CASE(ccx_first_control_equals_target),
    CQ_DEATH_CASE(ccx_second_control_equals_target),
    CQ_DEATH_CASE(i6_target_outside_the_scratch_region)
)
