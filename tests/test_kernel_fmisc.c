/* tests/test_kernel_fmisc.c — M38, K24: the sign ops (`fneg`, `fabs`,
 * `copysign`) and `fmin` / `fmax` at f64. PRD-v2 §5, §7;
 * docs/constructions/K24.md.
 *
 * TWO SUBJECTS, ONE SUITE, ON PRD-v2 §5's SEAM. The sign ops are one upstream
 * block each, written straight onto `dst`; what can go wrong there is WHICH
 * LANE a constant lands on and whether a Toffoli survives the fold, and
 * test_kernel_fmisc_sign.inc owns that. `fmin` / `fmax` are 17-row programs
 * over five modules' step blocks; what can go wrong there is the SLOT
 * ARITHMETIC, the LAYOUT and — thinnest of all — the OPERAND ORDER of the two
 * rows where fmax differs from fmin, which no count, palindrome or pool check
 * can see (test_kernel_fmisc_rows.inc, _scan.inc).
 *
 * L1'S ORACLE SHARES NOTHING WITH THE PORT. tests/support/fmiscref.c answers
 * minNum/maxNum from the IEEE rules and the docstring's two extra ones, with
 * the host `<` for the ordered case, and the sign ops from host bit
 * arithmetic. `cq_fmin_eval` — the library's C transcription of fmin.jl — is
 * checked AGAINST it, never used as it.
 *
 * WIDTH 64 AND NOTHING ELSE, for all five; another width is a hard error in
 * both configurations and tests/test_kernel_fmisc_death.c drives it.
 */

#include "kernels/fmisc.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/cmp.h"
#include "kernels/fcmp.h"
#include "kernels/fpclass.h"
#include "kernels/fpfield.h"
#include "kernels/mux.h"
#include "reg.h"
#include "scratch.h"
#include "shadow.h"
#include "sink_count.h"

#include "support/bitkinds.h"
#include "support/fmiscref.h"
#include "support/fpanchors.h"
#include "support/fphost.h"
#include "support/goldens.h"
#include "support/harness.h"
#include "support/kerneldrv.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"
#include "support/refmodel.h"

#include <stdio.h>
#include <string.h>

enum { W64 = CQ_FP64_W };

#include "test_kernel_fmisc_anchors.inc"

/* ---- The five specs. ---------------------------------------------------- */

static void shape_unary(int W, cq_kd_shape *out, cq_kd_anchor_fn anchors)
{
    cq_kd_default_shape(W, out);
    out->n_src = 1; out->w[0] = W64; out->w_dst = W64;
    out->anchors = anchors;
}

static void shape_binary(int W, cq_kd_shape *out, cq_kd_anchor_fn anchors)
{
    cq_kd_default_shape(W, out);
    out->n_src = 2; out->w[0] = W64; out->w[1] = W64; out->w_dst = W64;
    out->anchors = anchors;
}

static void fneg_shape(int W, cq_kd_shape *o)     { shape_unary(W, o, fneg_anchors); }
static void fabs_shape(int W, cq_kd_shape *o)     { shape_unary(W, o, fabs_anchors); }
static void copysign_shape(int W, cq_kd_shape *o) { shape_binary(W, o, copysign_anchors); }
static void fminmax_shape(int W, cq_kd_shape *o)  { shape_binary(W, o, fminmax_anchors); }

/* UNARY, ONE WIDTH (PRD-v2 §7.11): `cq_kd_shape_of` refuses a narrow or unary
 * shape without a `call` adapter, because the default path reads `src[1]`. */
static void call_fneg(cq_ctx *c, cq_bit *d, const cq_bit *const *s,
                      const cq_kd_shape *sh)
{ (void)sh; cq_kernel_fneg(c, d, s[0], W64); }
static void call_fabs(cq_ctx *c, cq_bit *d, const cq_bit *const *s,
                      const cq_kd_shape *sh)
{ (void)sh; cq_kernel_fabs(c, d, s[0], W64); }

static cq_ref_w refn_fneg(const cq_ref_w *s, const cq_kd_shape *sh)
{ (void)sh; return cq_ref_w_make(cq_ref_fneg(s[0].lo), 0u, W64); }
static cq_ref_w refn_fabs(const cq_ref_w *s, const cq_kd_shape *sh)
{ (void)sh; return cq_ref_w_make(cq_ref_fabs(s[0].lo), 0u, W64); }
static cq_ref_w refn_copysign(const cq_ref_w *s, const cq_kd_shape *sh)
{ (void)sh; return cq_ref_w_make(cq_ref_copysign(s[0].lo, s[1].lo), 0u, W64); }
static cq_ref_w refn_fmin(const cq_ref_w *s, const cq_kd_shape *sh)
{ (void)sh; return cq_ref_w_make(cq_ref_fmin(s[0].lo, s[1].lo), 0u, W64); }
static cq_ref_w refn_fmax(const cq_ref_w *s, const cq_kd_shape *sh)
{ (void)sh; return cq_ref_w_make(cq_ref_fmax(s[0].lo, s[1].lo), 0u, W64); }

static const cq_kd_spec SPEC_FNEG = { "fneg", NULL, NULL,
                                      fneg_shape, call_fneg, refn_fneg };
static const cq_kd_spec SPEC_FABS = { "fabs", NULL, NULL,
                                      fabs_shape, call_fabs, refn_fabs };
static const cq_kd_spec SPEC_COPYSIGN = { "copysign", cq_kernel_copysign, NULL,
                                          copysign_shape, NULL, refn_copysign };
static const cq_kd_spec SPEC_FMIN = { "fmin", cq_kernel_fmin, NULL,
                                      fminmax_shape, NULL, refn_fmin };
static const cq_kd_spec SPEC_FMAX = { "fmax", cq_kernel_fmax, NULL,
                                      fminmax_shape, NULL, refn_fmax };

static const cq_kd_spec *const ALL5[] = {
    &SPEC_FNEG, &SPEC_FABS, &SPEC_COPYSIGN, &SPEC_FMIN, &SPEC_FMAX
};
enum { N_ALL5 = 5 };

/* ---- Shared helpers for the .inc files. -------------------------------- */

/* A span's VALUE through the kind for a constant lane and the shadow for a
 * qubit one. `unknown` is a failure: the rotation-free shadow is exact. */
static uint64_t fm_read(cq_ctx *ctx, const cq_bit *v)
{
    uint64_t got = 0u;

    for (int i = 0; i < W64; i++) {
        cq_shadow sv;

        if (cq_bit_is_const(v[i])) {
            if (cq_bit_value(v[i])) got |= UINT64_C(1) << (unsigned)i;
            continue;
        }
        sv = cq_shadow_get(&ctx->shadow, cq_bit_qindex(v[i]));
        if (sv.unknown)
            cq_h_fail(__FILE__, __LINE__, "lane %d's shadow is UNKNOWN", i);
        if (sv.value) got |= UINT64_C(1) << (unsigned)i;
    }
    return got;
}

/* One forward call of `spec` into a mock, at chosen values and masks. `vb` /
 * `qb` are ignored for a unary spec. Returns dst's value; `dst_q`, when not
 * NULL, receives each dst lane's qubit index (UINT32_MAX for a constant). */
static uint64_t fm_run(const cq_kd_spec *spec, uint64_t va, uint64_t vb,
                       cq_ref_w qa, cq_ref_w qb, cq_mock *m, uint32_t *dst_q)
{
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    cq_kd_shape sh;
    const cq_bit *src[2];
    int32_t h[2], hd;
    uint64_t got;

    spec->shape(W64, &sh);
    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);
    h[0] = cq_bk_reg_w(&ctx, (uint32_t)W64, cq_ref_w_make(va, 0u, W64), qa);
    src[0] = cq_reg_cbits(&ctx.regs, h[0]);
    src[1] = NULL;
    if (sh.n_src == 2) {
        h[1] = cq_bk_reg_w(&ctx, (uint32_t)W64, cq_ref_w_make(vb, 0u, W64), qb);
        src[1] = cq_reg_cbits(&ctx.regs, h[1]);
    }
    hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W64);

    cq_mock_reset(m);                   /* drop the rails' materialising Xs */
    if (spec->call) spec->call(&ctx, cq_reg_bits(&ctx.regs, hd), src, &sh);
    else spec->kernel(&ctx, cq_reg_bits(&ctx.regs, hd), src[0], src[1], W64);

    got = cq_pc_value(&ctx, hd);
    if (dst_q)
        for (int i = 0; i < W64; i++) {
            cq_bit b = cq_reg_bits(&ctx.regs, hd)[i];
            dst_q[i] = cq_bit_is_qubit(b) ? cq_bit_qindex(b) : UINT32_MAX;
        }
    cq_ctx_dispose(&ctx);
    return got;
}

#include "test_kernel_fmisc_sign.inc"
#include "test_kernel_fmisc_rows.inc"
#include "test_kernel_fmisc_scan.inc"

/* ---- L1 + L2 + L3 + L5, all five. -------------------------------------- */

CQ_TEST(l1_sweep)
{
    for (int i = 0; i < N_ALL5; i++) cq_kd_sweep_at(ALL5[i], W64);
}

/* ---- Step 20's regions, and §9's gate-tuple transform. ------------------ */

/* The sign ops are the cheap body — three whole sweeps — and fmin/fmax ride
 * along at the two rows that matter under a control: the ALL-CLASSICAL one,
 * where CQ_KD_CTRL_Q0 is the only fixture that sees a short-circuit made
 * unconditional, and the ALL-QUANTUM one L4 pins. */
static void fm_region_body(void)
{
    static const cq_kd_spec *const MM[] = { &SPEC_FMIN, &SPEC_FMAX };

    for (int i = 0; i < 3; i++) cq_kd_sweep_at(ALL5[i], W64);
    for (int k = 0; k < 2; k++)
        for (int row = 0; row < 2; row++) {
            cq_bk_pair m;
            cq_ref_w v[CQ_KD_MAX_SRC];

            memset(v, 0, sizeof v);
            v[0] = cq_ref_w_make(CQ_F64_NEG_ZERO, 0u, W64);
            v[1] = cq_ref_w_make(CQ_F64_ONE, 0u, W64);
            m.q[0] = row ? cq_ref_w_ones(W64) : cq_ref_w_zero();
            m.q[1] = m.q[0];
            m.name = row ? "all-quantum" : "all-classical";
            cq_kd_case(MM[k], W64, v, &m);
        }
}

CQ_TEST(controlled)
{
    uint64_t reached = 0u;

    cq_kd_for_each_region("fmisc sign ops + fmin/fmax two rows", fm_region_body);
    for (int i = 0; i < N_ALL5; i++)
        reached += cq_kd_check_promotion(ALL5[i], W64);
    CHECK(reached > 0u);            /* the promotion identity is not vacuous */
}

/* ---- L4: the goldens, at the all-quantum mask. -------------------------- */

CQ_TEST(l4_goldens)
{
    cq_gold g;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/fmisc.counts",
                      "M38 kernels/fmisc*.c — K24 the sign ops (unsandwiched, "
                      "one block onto dst) and soft_fmin / soft_fmax "
                      "(sandwiched)",
                      "ALL-QUANTUM on every source at W = 64 (f64 only), "
                      "ctrl_depth 0. The sign ops have no scratch and no "
                      "Toffoli; fmin/fmax's views and constant spans fold at "
                      "every mask, so they sit below twice their slot count",
                      CQOPS_BENNETT_COMMIT))
        return;

    for (int i = 0; i < N_ALL5; i++) {
        cq_counter fwd, unc;

        cq_kd_measure(ALL5[i], W64, &fwd, &unc);
        CHECK_EQ(fwd.ry + fwd.rz + fwd.mz, 0);
        CHECK_EQ(unc.ry + unc.rz + unc.mz, 0);
        cq_gold_check(&g, ALL5[i]->name, "forward", W64, fwd.x, fwd.cx, fwd.ccx);
        cq_gold_check(&g, ALL5[i]->name, "unc",     W64, unc.x, unc.cx, unc.ccx);
    }
    CHECK(cq_gold_close(&g));
}

/* ---- What dst owns, and the scratch high-water mark. -------------------- */

CQ_TEST(dst_ownership_and_the_scratch_high_water_mark)
{
    uint32_t peak = 0u, owned;

    /* The sign ops take NO scratch: the peak IS what dst ends up owning. fabs
     * owns 63 qubits, not 64 — lane 63 is `a[63] & 0`, a constant ZERO view,
     * so its copy-out CNOT folds away and the lane is never materialised. */
    owned = cq_kd_peak(&SPEC_FNEG, W64, &peak);
    CHECK_EQ(owned, (uint32_t)W64);       CHECK_EQ(peak, owned);
    owned = cq_kd_peak(&SPEC_FABS, W64, &peak);
    CHECK_EQ(owned, (uint32_t)W64 - 1u);  CHECK_EQ(peak, owned);
    owned = cq_kd_peak(&SPEC_COPYSIGN, W64, &peak);
    CHECK_EQ(owned, (uint32_t)W64);       CHECK_EQ(peak, owned);

    /* fmin/fmax: FLAT scratch (PRD §15 D9), so the peak is the whole region
     * plus dst's 64 bits. L2 looks after the call and cannot see scratch
     * that was taken and tidily released; the high-water mark can. */
    owned = cq_kd_peak(&SPEC_FMIN, W64, &peak);
    CHECK_EQ(owned, (uint32_t)W64);
    CHECK_EQ(peak, cq_fmisc_region(CQ_FMISC_MIN) + (uint32_t)W64);
    owned = cq_kd_peak(&SPEC_FMAX, W64, &peak);
    CHECK_EQ(owned, (uint32_t)W64);
    CHECK_EQ(peak, cq_fmisc_region(CQ_FMISC_MAX) + (uint32_t)W64);
}

CQ_TEST_MAIN_ARGV(
    CQ_CASE(the_sign_ops_cost_their_upstream_blocks_slot_for_slot),
    CQ_CASE(the_sign_ops_match_an_independent_lane_scan_and_write_only_dst),
    CQ_CASE(the_sign_ops_have_a_true_l5_row_on_every_classical_anchor),
    CQ_CASE(the_sign_ops_are_zero_toffoli_at_every_scanned_mask),
    CQ_CASE(the_programs_are_the_source_line_for_line),
    CQ_CASE(the_programs_are_well_formed),
    CQ_CASE(the_blocks_cost_what_their_modules_say_they_cost),
    CQ_CASE(each_program_is_the_sum_of_its_blocks),
    CQ_CASE(the_classical_row_agrees_with_the_oracle),
    CQ_CASE(the_named_cells_separate_each_row_from_its_neighbour),
    CQ_CASE(the_slot_boundaries_match_an_independent_four_valued_scan),
    CQ_CASE(the_rows_spans_are_pairwise_disjoint),
    CQ_CASE(the_operands_are_wired_lane_for_lane),
    CQ_CASE(the_compute_half_is_a_palindrome_at_an_asymmetric_mask),
    CQ_CASE(two_programs_in_one_region_do_not_collide),
    CQ_CASE(l1_sweep),
    CQ_CASE(controlled),
    CQ_CASE(l4_goldens),
    CQ_CASE(dst_ownership_and_the_scratch_high_water_mark)
)
