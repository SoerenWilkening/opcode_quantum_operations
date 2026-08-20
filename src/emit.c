/* src/emit.c — M05. The PRD §3 fold table, and nothing else. */

#include "emit.h"

#include "controlled.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(CQOPS_DEBUG_INVARIANTS) && CQOPS_DEBUG_INVARIANTS
#  define CQ_EMIT_DEBUG 1
#else
#  define CQ_EMIT_DEBUG 0
#endif

static void cq_emit_die(const char *what)
{
    fprintf(stderr, "libcqops: FATAL: emit: %s\n", what);
    abort();
}

/* --- The two Debug-gated checks (plan §2.1). ----------------------------- */

/* §3: "Operand distinctness is asserted, not assumed — a coincident operand is
 * a meaningless channel and a real miscompile signature." Note what the check
 * can compare: distinctness is a property of BITS, not of kinds. Two different
 * bits may both be Q unknown while holding different qubit indices, which is a
 * legal ordinary pair. So this fires only on CQ_BIT_Q operands and cannot fire
 * on two constants — correct, because two constant bits are genuinely
 * independent channels. */
static void check_distinct(const cq_bit *a, const cq_bit *b, const char *pair)
{
#if CQ_EMIT_DEBUG
    if (cq_bit_coincident(a, b)) cq_emit_die(pair);
#else
    (void)a; (void)b; (void)pair;
#endif
}

/* I6, target side (plan §0.2). Only armed while a sandwich is running. */
static void check_target(const cq_ctx *ctx, const cq_bit *t)
{
#if CQ_EMIT_DEBUG
    if (ctx->scratch_lo && (t < ctx->scratch_lo || t >= ctx->scratch_hi))
        cq_emit_die("I6: gate target is outside the sandwich scratch region");
#else
    (void)ctx; (void)t;
#endif
}

/* --- Materialisation (Rule 5): the one place a data qubit is allocated. --- */

void cq_materialise(cq_ctx *ctx, cq_bit *b)
{
    if (cq_bit_is_qubit(*b))
        cq_emit_die("materialise: bit is already a qubit");

    int was_one = cq_bit_value(*b);
    uint32_t q = cq_ctx_fresh_qubit(ctx);   /* |0> by I3; shadow born known-0 */
    *b = cq_bit_qubit(q);

    /* THE X GOES STRAIGHT TO THE SINK, AND SINCE STEP 20 THAT IS A DECISION
     * RATHER THAN A DEFERRAL (bd skh, resolved as UNPROMOTED). M06 hooks
     * cq_emit_x/cx/ccx and nothing else, so materialisation is untouched by the
     * controlled axis — which is the CORRECT answer, not a happy accident of
     * layering, and PRD §15 D11's constant column is stated to depend on it.
     *
     * The algebra, with `b` the rail's classical value before the region, `c` an
     * inner control and `k` the control branch. The required semantics is
     * `b ⊕ (k ∧ c)`. Unpromoted, the fresh qubit holds `b` unconditionally and
     * the promoted CX→CCX gives exactly that. Promoted, the fresh qubit would be
     * `k ∧ b` and the result `k ∧ (b ⊕ c)` — which disagrees in exactly one
     * cell, `b = 1, k = 0`, the branch on which the rail must still read its old
     * value. Materialisation changes a bit's ENCODING (constant → qubit, I4),
     * never its VALUE, and an encoding is not conditional on anything.
     *
     * There IS a shipped witness, so this is not hypothetical:
     * CQ_lang's slice_control_cond_onward_phase fixture copies h5 (born 10)
     * into h1 (born 3) under a quantum control, and `10 & 3` has bit 1 set in
     * both — a constant-ONE target materialised inside a controlled region. */
    if (was_one) {
        cq_sink_x(ctx->sink, q);
        cq_shadow_x(&ctx->shadow, q);
    }
}

/* --- The physical tail (M06 calls these; see emit.h). ------------------- */

void cq_emit_cx_phys(cq_ctx *ctx, const cq_bit *c, cq_bit *t)
{
    if (cq_bit_is_const(*t)) cq_materialise(ctx, t);     /* c = Q, t constant */

    uint32_t cq_ = cq_bit_qindex(*c), tq = cq_bit_qindex(*t);
    cq_sink_cx(ctx->sink, cq_, tq);
    cq_shadow_cx(&ctx->shadow, cq_, tq);
}

void cq_emit_ccx_phys(cq_ctx *ctx, const cq_bit *c1, const cq_bit *c2, cq_bit *t)
{
    if (cq_bit_is_const(*t)) cq_materialise(ctx, t);

    uint32_t a = cq_bit_qindex(*c1), b = cq_bit_qindex(*c2);
    uint32_t tq = cq_bit_qindex(*t);
    cq_sink_ccx(ctx->sink, a, b, tq);
    cq_shadow_ccx(&ctx->shadow, a, b, tq);
}

/* --- The fold table. PRD §3, row for row, under PRD §9 row 0. ----------- */

/* THE ORDER OF THE THREE CLAUSES IN EACH FUNCTION IS LOAD-BEARING, and
 * src/controlled.h states the rule at length: row 0's SKIP first, then the §3
 * fold on the CONTROLS (a control fold is semantic and survives any control),
 * then §9's promotion — which comes BEFORE any fold that reads the TARGET,
 * because a target fold is a representation choice and rewriting a constant in
 * place is unconditional. Getting that last one backwards is a controlled
 * region silently made unconditional, and it is invisible at the all-quantum
 * operand mask. */

void cq_emit_x(cq_ctx *ctx, cq_bit *t)
{
    check_target(ctx, t);

    if (cq_ctrl_skipping(&ctx->ctrl)) return;            /* §9 row 0          */
    if (cq_ctrl_wire(&ctx->ctrl)) { cq_ctrl_promote_x(ctx, t); return; }

    /* Row 1: t constant -> flip the constant in place, 0 gates, 0 qubits.
     * This single line is the whole classical short-circuit for X, and the
     * reason `int a = 0; a ^= 1;` costs nothing. */
    if (cq_bit_is_const(*t)) { cq_bit_flip_const(t); return; }

    /* Row 2: t qubit -> sink.x, 1 gate; shadow flips if known. */
    uint32_t q = cq_bit_qindex(*t);
    cq_sink_x(ctx->sink, q);
    cq_shadow_x(&ctx->shadow, q);
}

void cq_emit_cx(cq_ctx *ctx, const cq_bit *c, cq_bit *t)
{
    check_distinct(c, t, "§3 distinctness: CX control == target");
    check_target(ctx, t);

    if (cq_ctrl_skipping(&ctx->ctrl)) return;            /* §9 row 0          */

    if (cq_bit_is_zero(*c)) return;                      /* c = ZERO: nothing */
    if (cq_bit_is_one(*c)) { cq_emit_x(ctx, t); return; }/* c = ONE:  X(t)    */

    /* c = Q from here. */
    if (cq_ctrl_wire(&ctx->ctrl)) { cq_ctrl_promote_cx(ctx, c, t); return; }

    cq_emit_cx_phys(ctx, c, t);
}

void cq_emit_ccx(cq_ctx *ctx, const cq_bit *c1, const cq_bit *c2, cq_bit *t)
{
    check_distinct(c1, c2, "§3 distinctness: CCX controls coincide");
    check_distinct(c1, t,  "§3 distinctness: CCX control 1 == target");
    check_distinct(c2, t,  "§3 distinctness: CCX control 2 == target");
    check_target(ctx, t);

    if (cq_ctrl_skipping(&ctx->ctrl)) return;            /* §9 row 0          */

    /* Either control ZERO: nothing. */
    if (cq_bit_is_zero(*c1) || cq_bit_is_zero(*c2)) return;

    /* CCX is SYMMETRIC IN ITS CONTROLS, so the c1 = ONE and c2 = ONE rows are
     * one rule written twice. Implement it as a swap before dispatch, not as
     * two branches — an earlier PRD draft carried only the c1 row, which left
     * (c1 = Q, c2 = ONE), 15 of the 125 combinations, matched by no row at
     * all. A swap cannot develop that hole. */
    if (cq_bit_is_one(*c2)) { const cq_bit *s = c1; c1 = c2; c2 = s; }
    if (cq_bit_is_one(*c1)) { cq_emit_cx(ctx, c2, t); return; }

    /* Both controls Q. */
    if (cq_ctrl_wire(&ctx->ctrl)) { cq_ctrl_promote_ccx(ctx, c1, c2, t); return; }

    cq_emit_ccx_phys(ctx, c1, c2, t);
}
