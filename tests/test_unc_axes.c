/* tests/test_unc_axes.c — bd 4jj. THE UNCOMPUTE AXIS COMPOSED WITH TWO OTHERS.
 *
 * tests/test_unc.c owns the axis at ONE route and with NO region open: every
 * kernel it drives is on the same construction at the forward and at the
 * uncompute, and the drift between them is always uncontrolled. The Step 21
 * adversarial review named three compositions that fixture cannot reach, and
 * this binary is them:
 *
 *   (a) M12's barrel re-evaluates its classical short-circuit on EVERY call
 *       (src/kernels/shift_var.c), so a drift on an amount lane the barrel
 *       reads is a ROUTE CHANGE rather than a delta: the forward is M11's
 *       W-k CX with no scratch, and the uncompute opens a W(3L+1) scratch
 *       region and a cq_sandwich the forward never entered.
 *       -> tests/test_unc_axes_route.inc
 *
 *   (b) M11 reached DIRECTLY with that drifted amount is a hard error. It has
 *       no ABI route (the shim sends every shift to M12, which re-scans and
 *       takes (a)'s sandwich instead), so it is a death case beside M11's own:
 *       tests/test_kernel_step11_death.c, and one dated sentence in PRD §10.
 *
 *   (c) The drift's OCCURRENCE is conditional on a region: cq_rotate_ry_bit
 *       opens with §9 row 0's skip. So PRD §10 consequence (ii) — `unc ==
 *       forward` is not an invariant — is a FUNCTION of the enclosing region,
 *       and in the one row where a ZERO control makes the drift provably
 *       absent, the equality is the claim rather than the error.
 *       -> tests/test_unc_axes_region.inc
 *
 * WHY A SEPARATE BINARY. tests/test_unc.c stood at 294 of Rule 12's 300 when
 * this landed, and its CQ_TEST_MAIN list is code. The seam is the one named
 * above — the axis COMPOSED with a route or a region, against the axis at a
 * fixed route — so nothing here reaches into test_unc.c's catalogue, and the
 * fixture below is a deliberate small copy of its `ux_*` helpers rather than a
 * shared include: each file reads top to bottom on its own.
 *
 * THE INSTRUMENT IS test_unc_asym.inc's DIFFERENTIAL, AND THE SAME TWO
 * PREMISES ARE ASSERTED HERE. A general Ry poisons the drifted lane, the
 * poison reaches `dst` through the control edge, and nothing in this project
 * can then read `dst` (cq_pc_value fails by design; Rule 13 forbids a
 * simulator). So every row is run twice — once with the Ry pair, once with a
 * CX pair that materialises the same lane and keeps its shadow — and the two
 * uncompute streams must agree gate for gate. The CX route's `dst` is
 * readable and zero; the Ry route's is the same circuit on the same input.
 * The premises: the Ry pair really is an inverse pair (its recorded angles
 * cancel), and the CX pair really restores the lane's value.
 *
 * NO ASSERTION HERE EQUATES A FORWARD AND AN UNCOMPUTE ACROSS A DRIFT (Rule
 * 14). The durable identity is the one test_unc_asym.inc established:
 * count(unc) == count(a FRESH forward at the drifted mask, under the same
 * region as the uncompute).
 */

#include "support/harness.h"

#include "controlled.h"
#include "ctx.h"
#include "emit.h"
#include "reg.h"
#include "rotate.h"
#include "shadow.h"
#include "support/bitkinds.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"
#include "support/refmodel.h"

#include "kernels/add.h"
#include "kernels/kernel.h"
#include "kernels/shift_var.h"

#include <stdio.h>
#include <string.h>

enum { AX_STREAM_CAP = 4096 };

/* A row's failure messages carry the row; CHECK's carry only the line. */
#define AX_EXPECT(cond, ...)                                                  \
    do {                                                                      \
        if (!(cond)) cq_h_fail(__FILE__, __LINE__, __VA_ARGS__);              \
    } while (0)

/* --- the fixture --------------------------------------------------------- */

typedef struct {
    cq_ctx  ctx;
    cq_mock mock;
    cq_sink sink;
} ax_fix;

static void ax_open(ax_fix *f)
{
    cq_mock_init(&f->mock);
    f->sink = cq_mock_sink(&f->mock);
    cq_ctx_init(&f->ctx, &f->sink);
}

/* Frees nothing it was not told to: a rail the shadow cannot prove is left
 * live, which is Rule 6 working (PRD §10's intended safe leak). */
static void ax_close(ax_fix *f)
{
    cq_ctx_dispose(&f->ctx);
    cq_mock_dispose(&f->mock);
}

/* 1 iff every bit of `h` is determinate; the value is written either way. A
 * caller that ignores the return value has written a vacuous assertion — a
 * poisoned bit contributes 0 to `*out`. */
static int ax_read(const cq_ctx *ctx, int32_t h, uint64_t *out)
{
    const cq_bit *b = cq_reg_cbits(&ctx->regs, h);
    uint32_t      W = cq_reg_width(&ctx->regs, h);
    uint64_t      v = 0u;
    int           determinate = 1;

    for (uint32_t i = 0; i < W; i++) {
        if (cq_bit_is_const(b[i])) {
            v |= (uint64_t)cq_bit_value(b[i]) << i;
            continue;
        }

        cq_shadow s = cq_shadow_get(&ctx->shadow, cq_bit_qindex(b[i]));
        if (s.unknown) { determinate = 0; continue; }
        v |= (uint64_t)(s.value != 0) << i;
    }

    *out = v;
    return determinate;
}

typedef struct { uint64_t x, cx, ccx; } ax_tuple;

static ax_tuple ax_tuple_of(const cq_mock *m)
{
    ax_tuple t;

    t.x   = cq_mock_count_op(m, CQ_OP_X);
    t.cx  = cq_mock_count_op(m, CQ_OP_CX);
    t.ccx = cq_mock_count_op(m, CQ_OP_CCX);
    return t;
}

static uint64_t ax_total(ax_tuple t) { return t.x + t.cx + t.ccx; }

/* --- the two drift routes ------------------------------------------------ */

typedef enum { AX_RY = 0, AX_CX = 1 } ax_route;
static const char *const AX_ROUTE[2] = { "Ry", "CX" };

/* Perturb `lane` and restore its value. The Ry pair is §7's one cell that
 * materialises AND poisons; 0.7 rad is CQ_ANGLE_GENERAL at every legal
 * tolerance (test_unc_asym.inc has the arithmetic). The CX pair materialises
 * the same lane through the fold table's `c = Q, t constant` row and keeps the
 * shadow. Both run under whatever region the caller has open. */
static void ax_drift(cq_ctx *ctx, cq_bit *lane, const cq_bit *poke,
                     ax_route route)
{
    if (route == AX_RY) {
        cq_rotate_ry_bit(ctx, lane,  0.7);
        cq_rotate_ry_bit(ctx, lane, -0.7);
        return;
    }
    cq_emit_cx(ctx, poke, lane);
    cq_emit_cx(ctx, poke, lane);
}

/* THE INVERSE-PAIR PREMISE, from the recorded window. Emission reads bit
 * kinds only, so `Ry(0.7); Ry(0.7)` is byte-identical to the inverse pair
 * everywhere except the `angle` field — which the route comparison omits. The
 * count is 2 uncontrolled, 4 under a promoting region (§9's R(θ/2); CX;
 * R(−θ/2); CX, twice), and 0 when row 0 skipped it. The sum is exact: every
 * angle here is ±0.7 or ±0.35, and x + (−x) is 0 in binary floating point.
 * No zero-angle guard: θ = 0 is §7's IDENTITY row at every tolerance and never
 * reaches the sink, so a guard on a recorded 0.0 could not be made to fail —
 * the COUNT is what sees a drift that folded away. */
static void ax_check_ry_window(const cq_mock *m, size_t want_ry,
                               const char *row)
{
    size_t n_ry = 0u;
    double sum = 0.0;

    for (size_t i = 0; i < cq_mock_count(m); i++) {
        const cq_rec *r = cq_mock_at(m, i);

        if (r->op != CQ_OP_RY) continue;
        n_ry++;
        sum += r->angle;
    }
    AX_EXPECT(n_ry == want_ry, "%s: the drift emitted %zu Ry, want %zu", row,
              n_ry, want_ry);
    AX_EXPECT(sum == 0.0, "%s: the drift's angles sum to %a, so it is not an "
              "inverse pair and the oracle has no licence", row, sum);
}

/* The uncompute stream, kept for the cross-route comparison. */
static size_t ax_keep_stream(const cq_mock *m, cq_rec *out, const char *row)
{
    size_t n = cq_mock_count(m);

    if (n > AX_STREAM_CAP) {
        cq_h_fail(__FILE__, __LINE__, "%s: %zu gates will not fit the stream "
                  "buffer — a TRUNCATED comparison would silently agree", row,
                  n);
        return 0u;
    }
    for (size_t i = 0; i < n; i++) out[i] = *cq_mock_at(m, i);
    return n;
}

/* Gate for gate, operands included. Returns the first differing index, or
 * `n` when the two agree. */
static size_t ax_first_diff(const cq_rec *a, const cq_rec *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (a[i].op != b[i].op || a[i].a != b[i].a || a[i].b != b[i].b ||
            a[i].t != b[i].t)
            return i;
    return n;
}

/* L3 on a rail the shadow CAN prove: free it, then the pool AS A SET — every
 * live index is owned by one of `rest`, and every index `hd` held is back on
 * the free list. Counts (`minted`, `peak`, free-list length) are monotone and
 * are not compared. Returns 1 iff both hold. */
static int ax_free_l3(ax_fix *f, int32_t hd, const int32_t *rest,
                      uint32_t n_rest)
{
    uint32_t held[128];
    uint32_t n = cq_pc_indices(&f->ctx, hd, held, 128u);

    cq_reg_free(&f->ctx, hd, cq_pc_zero_proof_rotation_free);
    return cq_pc_live_is_exactly(&f->ctx, rest, n_rest) &&
           cq_pc_indices_are_free(&f->ctx, held, n);
}

#include "test_unc_axes_route.inc"
#include "test_unc_axes_region.inc"

CQ_TEST_MAIN(
    /* (a) — tests/test_unc_axes_route.inc */
    CQ_CASE(a_drift_on_an_amount_lane_the_barrel_reads_changes_the_route),
    CQ_CASE(a_drift_on_an_amount_lane_the_barrel_never_reads_changes_nothing),

    /* (c) — tests/test_unc_axes_region.inc */
    CQ_CASE(a_drifted_uncompute_inside_its_own_region),
    CQ_CASE(a_drift_inside_a_region_happens_only_if_the_region_runs),
    CQ_CASE(nested_regions_across_the_pair_gate_the_drift_and_the_kernel_apart)
)
