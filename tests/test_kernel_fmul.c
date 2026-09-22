/* tests/test_kernel_fmul.c — M34, K16. `soft_fmul` at f64, the second
 * floating-point KERNEL of libcqops and the first with a `cq_mul_block` in it.
 * PRD-v2 §5, §7; docs/constructions/K16.md.
 *
 * THE SUBJECT OF THIS FILE IS THE SLOT ARITHMETIC AND THE HAND-OFF ENCODINGS.
 * Whether `lower_eq!`, `lower_ult!`, `lower_add!`, `lower_sub!`, `lower_mux!`
 * and `lower_mul!` compute the right bits is M14's, M16's, M17's and M18's
 * suites next door, and whether the four shared softfloat helpers do is M32's.
 * What is new here is (i) which SLOT each of 128 rows occupies, (ii) which
 * BITS each of them owns — four `cq_mul_block`s at four offsets in one region,
 * which K11.md §7.5 says is the shape that detects a dropped `off` — and
 * (iii) whether the five hand-offs cross M32's two encodings the right way
 * round. Every one of the three is invisible to a gate count.
 *
 * SO L1'S ORACLE IS THE HOST DOUBLE MULTIPLY AND SHARES NOTHING WITH THE PORT
 * — EXCEPT ON THE CELLS IEEE LEAVES UNSPECIFIED, WHICH ARE PINNED BY TABLE.
 * That is PRD-v2 §7.4's instruction and this kernel is where it stops being a
 * formality. MEASURED on this box while writing this file, Apple clang 17,
 * `-std=c11 -ffp-contract=off`: the expression `bits(dbl(x) * dbl(y))` over an
 * array of pairs returns the FIRST operand's NaN payload at `-O0` and the
 * SECOND at `-O1` and `-O2`, in one program and with no source change. `*` is
 * commutative for the standard's purposes and the surviving payload is
 * unspecified, so the compiler is free to emit `mulsd` either way round and
 * clang does — which makes the bare host operator NOT A WELL-DEFINED ORACLE
 * for a two-NaN multiply. `ref_fmul` below therefore answers the NaN rows and
 * the `Inf x 0` row from a TABLE, exactly as §7.4 says, and calls the host
 * only where IEEE specifies the result. The table's content is upstream's
 * first-operand rule and fphost.h's INDEF, which are two independent
 * statements of the same numbers and not a reading of src/kernels/fmul.c.
 *
 * SLOTS AND GATES COME APART, AS THEY DO IN M31, M32 AND M36 (D-K18-6). 35 of
 * the 128 rows are VIEWS and seven constant spans sit inside block operands,
 * so the §3 fold table elides gates at EVERY operand mask including the
 * all-quantum one L4 pins. Every composition identity below is over SLOTS, and
 * the slot scan's prediction is four-valued — NONE / X / CX / CCX — walked
 * with a separate stream cursor. Deleting the constant rows from the
 * prediction turns the instrument off rather than simplifying it.
 *
 * WIDTH 64 AND NOTHING ELSE, DELIBERATELY, AND IT IS A REFUSAL RATHER THAN A
 * GAP. PRD-v2 §1 scopes v2 to `f64` and `soft_fmul` is `(UInt64, UInt64)`, so
 * a second width would be a fiction; the kernel hard-errors on one in both
 * configurations and tests/test_kernel_fmul_death.c drives that.
 *
 * ONE CIRCUIT CASE IS LARGE: 163,300 compute-half slots at ~300k gates per
 * kernel call. The L1 sweep keeps the shared 32-case budget and uses eight
 * representative anchors; the complete table stays in the cheap classical
 * oracle case.
 */

#include "kernels/fmul.h"
#include "kernels/fmul_int.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/fpclass.h"
#include "kernels/fpfield.h"
#include "kernels/fpround.h"
#include "kernels/mul.h"
#include "kernels/mux.h"
#include "reg.h"
#include "scratch.h"
#include "shadow.h"
#include "sink_count.h"

#include "support/bitkinds.h"
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

CQ_TEST(the_cached_prefix_map_matches_a_linear_dispatch_at_every_slot)
{
    const cq_fm_map *m = cq_fm_map_get();
    const cq_fmul_row *rows;
    int nr, row = 0, start = 0;

    rows = cq_fmul_rows(&nr);
    for (int u = 0; u < m->steps; u++) {
        int got, local, n;

        for (;;) {
            n = cq_fm_row_steps(&rows[row]);
            if (n > 0 && u < start + n) break;
            start += n;
            row++;
        }
        got = cq_fm_row_at(m, u, &local);
        CHECK_EQ(got, row);
        CHECK_EQ(local, u - start);
    }
    CHECK_EQ(m->n, nr);
}

/* ---- L1's oracle: the host `*`, with §7.4's cells pinned by table. ------- */

static int fm_is_nan(uint64_t u)
{
    return ((u >> 52) & CQ_FP64_EXP_ALL) == CQ_FP64_EXP_ALL
        && (u & CQ_F64_MANT_MASK) != 0u;
}

static int fm_is_inf(uint64_t u)
{
    return ((u >> 52) & CQ_FP64_EXP_ALL) == CQ_FP64_EXP_ALL
        && (u & CQ_F64_MANT_MASK) == 0u;
}

static int fm_is_zero(uint64_t u) { return (u & ~CQ_F64_SIGN_MASK) == 0u; }

/* NOTHING HERE IS DERIVED FROM src/kernels/fmul_eval.c. The three pinned rows
 * are literals — upstream's `_sf_propagate_nan2` rule (softfloat_common.jl:
 * 23-24, "first-operand NaN, x86 SSE") and fphost.h's measured INDEF — and
 * everything else is the host operator, which IEEE fully specifies. An oracle
 * that re-transcribed the Julia would agree with a wrong port rather than
 * catch it (the Step 18 trap). */
static uint64_t ref_fmul(uint64_t a, uint64_t b, int W)
{
    (void)W;
    if (fm_is_nan(a)) return a | CQ_F64_QUIET_BIT;
    if (fm_is_nan(b)) return b | CQ_F64_QUIET_BIT;
    if ((fm_is_inf(a) && fm_is_zero(b)) || (fm_is_zero(a) && fm_is_inf(b)))
        return CQ_FPHOST_INDEF;
    return cq_fphost_bits(cq_fphost_from_bits(a) * cq_fphost_from_bits(b));
}

/* ---- §7.12's anchors, composed. ----------------------------------------- */

#include "test_kernel_fmul_anchors.inc"

/* ---- The spec. ---------------------------------------------------------- */

static int fmul_circuit_anchors(int W, int i, cq_ref_w *v)
{
    const int available = fmul_anchors(W, -1, NULL);
    const int picked = cq_fp_representative_binary_index(available, i);

    if (i < 0) return cq_fp_representative_count(available);
    return picked >= 0 ? fmul_anchors(W, picked, v) : 0;
}

/* Rule 7's canonical shape unchanged — arity 2, `w_dst == w[0]` — so the
 * driver's DEFAULT call and reference paths serve it with no adapter, which
 * `cq_kd_shape_of` checks rather than assumes. */
static void fmul_shape(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->n_src   = 2;
    out->w[0]    = W64;
    out->w[1]    = W64;
    out->w_dst   = W64;
    out->anchors = fmul_circuit_anchors;
}

static const cq_kd_spec SPEC = { "fmul", cq_kernel_fmul, ref_fmul,
                                 fmul_shape, NULL, NULL };

/* One run of the kernel into a mock, at a chosen value pair and mask pair.
 * Returns `dst`'s value. Shared by the slot scan, the span scan and the
 * palindrome. */
static uint64_t fmul_run(uint64_t va, uint64_t vb, cq_ref_w qa, cq_ref_w qb,
                         cq_mock *m)
{
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    int32_t ha, hb, hd;
    uint64_t got;

    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);
    ha = cq_bk_reg_w(&ctx, (uint32_t)W64, cq_ref_w_make(va, 0u, W64), qa);
    hb = cq_bk_reg_w(&ctx, (uint32_t)W64, cq_ref_w_make(vb, 0u, W64), qb);
    hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W64);

    cq_mock_reset(m);                   /* drop the rails' materialising Xs */
    cq_kernel_fmul(&ctx, cq_reg_bits(&ctx.regs, hd),
                   cq_reg_cbits(&ctx.regs, ha), cq_reg_cbits(&ctx.regs, hb),
                   W64);
    got = cq_pc_value(&ctx, hd);
    cq_ctx_dispose(&ctx);
    return got;
}

/* ---- The classical row against the oracle, on every anchor. ------------- */

/* THE ONLY CASE THAT RUNS NO CIRCUIT AND STILL CATCHES A PORT ERROR. L1 drives
 * the all-classical mask row for every anchor too, but it does so through
 * `cq_kd_case`, and this one says what went wrong in bits rather than through
 * the driver's framing. It is also the only place the pinned cells and the
 * transcription meet without the circuit in between. */
CQ_TEST(the_classical_row_agrees_with_the_oracle_on_every_anchor)
{
    int n = fmul_anchors(W64, -1, NULL);

    CHECK(n > 0);
    for (int i = 0; i < n; i++) {
        cq_ref_w v[CQ_KD_MAX_SRC];
        uint64_t got, want;

        memset(v, 0, sizeof v);
        CHECK_EQ(fmul_anchors(W64, i, v), 1);
        got  = cq_fmul_eval(v[0].lo, v[1].lo);
        want = ref_fmul(v[0].lo, v[1].lo, W64);
        if (got != want)
            cq_h_fail(__FILE__, __LINE__,
                      "anchor %d: soft_fmul(%016llx, %016llx) = %016llx, the "
                      "oracle says %016llx", i,
                      (unsigned long long)v[0].lo, (unsigned long long)v[1].lo,
                      (unsigned long long)got, (unsigned long long)want);
    }

    /* AND THE ORACLE IS NOT THE TRANSCRIPTION, ASSERTED RATHER THAN NARRATED.
     * These three rows are the ones the host operator answers differently at
     * different optimisation levels (see this file's header), so the oracle
     * must be answering them from its table — if it were calling the host, one
     * of the two orders below would come back with the wrong payload in a
     * Release build. */
    CHECK_EQ(ref_fmul(CQ_F64_QNAN_A, CQ_F64_QNAN_B, W64), CQ_F64_QNAN_A);
    CHECK_EQ(ref_fmul(CQ_F64_QNAN_B, CQ_F64_QNAN_A, W64), CQ_F64_QNAN_B);
    CHECK_EQ(ref_fmul(CQ_F64_SNAN, CQ_F64_QNAN_B, W64), CQ_F64_SNAN_QUIET);
    CHECK_EQ(ref_fmul(CQ_F64_QNAN_B, CQ_F64_SNAN, W64), CQ_F64_QNAN_B);
    CHECK_EQ(ref_fmul(CQ_F64_POS_INF, CQ_F64_POS_ZERO, W64), CQ_FPHOST_INDEF);
    CHECK_EQ(ref_fmul(CQ_F64_NEG_ZERO, CQ_F64_POS_INF, W64), CQ_FPHOST_INDEF);

    /* An sNaN IS a NaN to `soft_fmul`, because fmul.jl never consults
     * QUIET_BIT: the exponent is extracted by shift-and-mask and the quiet bit
     * is not read anywhere in the file. A port that "helpfully" tested it
     * would send a signalling NaN down the arithmetic path. */
    CHECK_EQ(cq_fmul_eval(CQ_F64_SNAN, CQ_F64_ONE), CQ_F64_SNAN_QUIET);

    /* AND AN INFINITY IS NOT: `ea == 0x7FF` holds and `fa != 0` does not, so
     * deleting the fraction conjunct turns every Inf into a NaN. */
    CHECK_EQ(cq_fmul_eval(CQ_F64_POS_INF, CQ_F64_ONE), CQ_F64_POS_INF);
}

/* ---- Complete classical table, constant representative circuit set. ----- */

CQ_TEST(the_full_anchor_table_stays_classical_and_the_circuit_set_is_constant)
{
    cq_kd_shape sh;
    int n, circuit_n;

    fmul_shape(W64, &sh);
    n = fmul_anchors(W64, -1, NULL);
    circuit_n = sh.anchors(W64, -1, NULL);

    CHECK_EQ(n, cq_fp_anchors_binary_count() + N_FMUL_PAIRS);
    CHECK_EQ(circuit_n, 8);

    /* The COMPLETE table remains a cheap provider/oracle obligation. Only its
     * representative subset reaches the gate-emitting sampler. */
    for (int i = 0; i < n; i++) {
        cq_ref_w v[CQ_KD_MAX_SRC];

        v[0] = cq_ref_w_make(0xDEADBEEFull, 0u, W64);
        v[1] = cq_ref_w_make(0xDEADBEEFull, 0u, W64);
        v[2] = cq_ref_w_make(0xC0FFEEull, 0u, W64);
        CHECK_EQ(fmul_anchors(W64, i, v), 1);
        CHECK_EQ(v[2].lo, 0xC0FFEEull);       /* untouched, not zero-filled */
    }
}

#include "test_kernel_fmul_slots.inc"
#include "test_kernel_fmul_scan.inc"
#include "test_kernel_fmul_spans.inc"

/* ---- L1 + L2 + L3 + L5. ------------------------------------------------- */

CQ_TEST(l1_sweep)
{
    cq_kd_sweep_at(&SPEC, W64);
}

/* ---- Step 20's four regions, and §9's gate-tuple transform. -------------- */

/* THE REGION BODY IS TWO HAND-DRIVEN CASES AND NOT THE SWEEP, and that is
 * kerneldrv.h's own instruction applied to a kernel with ONE width and ONE
 * entry point: "`body` should be the suite's sweep at its CHEAP widths only".
 * K16 has no cheap width — every call is ~300k gates — so what this suite can
 * economise on is the CASE COUNT. The two rows kept are the two that matter:
 * the ALL-CLASSICAL one, which at CQ_KD_CTRL_Q0 is the only fixture in the
 * project that can see a controlled region silently made unconditional (the
 * short-circuit writes `dst` through cq_emit_x, whose constant row cannot fire
 * inside a promoted region, so each set lane must become a wire driven by a
 * real CX); and the ALL-QUANTUM one, which is the mask L4 pins. */
static void fmul_two_rows(void)
{
    cq_bk_pair m;
    cq_ref_w v[CQ_KD_MAX_SRC];

    v[0] = cq_ref_w_make(FM_GRS4_A, 0u, W64);
    v[1] = cq_ref_w_make(FM_GRS4_B, 0u, W64);

    m.q[0] = cq_ref_w_zero();  m.q[1] = cq_ref_w_zero();
    m.name = "all-classical";
    cq_kd_case(&SPEC, W64, v, &m);

    m.q[0] = cq_ref_w_ones(W64); m.q[1] = cq_ref_w_ones(W64);
    m.name = "all-quantum";
    cq_kd_case(&SPEC, W64, v, &m);
}

CQ_TEST(controlled)
{
    uint64_t reached;

    cq_kd_for_each_region("fmul all-classical + all-quantum", fmul_two_rows);

    reached = cq_kd_check_promotion(&SPEC, W64);

    /* NOT VACUOUS: the promotion identity holds trivially where the
     * uncontrolled tuple is empty, so the caller owns the non-vacuity claim
     * (cq_kd_check_promotion returns the uncontrolled total for this reason). */
    CHECK(reached > 0u);
}

/* ---- L4: the goldens, at the all-quantum mask. -------------------------- */

CQ_TEST(l4_goldens)
{
    cq_counter fwd, unc;
    cq_gold g;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/fmul.counts",
                      "M34 kernels/fmul.c — K16 IEEE binary64 multiply "
                      "(sandwiched), the whole of soft_fmul",
                      "ALL-QUANTUM on `a` and `b` at W = 64 (f64 only), at "
                      "ctrl_depth 0. 35 of the 150 rows are VIEWS and seven "
                      "constant spans sit inside block operands, so these are "
                      "well below twice the 163,300-slot compute half",
                      CQOPS_BENNETT_COMMIT))
        return;

    cq_kd_measure(&SPEC, W64, &fwd, &unc);

    CHECK_EQ(fwd.ry + fwd.rz + fwd.mz, 0);
    CHECK_EQ(unc.ry + unc.rz + unc.mz, 0);

    cq_gold_check(&g, "fmul", "forward", W64, fwd.x, fwd.cx, fwd.ccx);
    cq_gold_check(&g, "fmul", "unc",     W64, unc.x, unc.cx, unc.ccx);

    CHECK(cq_gold_close(&g));
}

/* ---- dst owns 64 qubits and the scratch comes back. --------------------- */

CQ_TEST(dst_owns_64_qubits_and_the_scratch_comes_back)
{
    uint32_t peak = 0u;
    uint32_t owned = cq_kd_peak(&SPEC, W64, &peak);

    CHECK_EQ(owned, (uint32_t)W64);
    /* The whole program's region plus dst's 64 bits. L2 looks AFTER the call
     * and cannot see scratch that was taken and tidily released, so the
     * high-water mark is the only instrument for it. */
    CHECK_EQ(peak, cq_fmul_region() + (uint32_t)W64);
}

CQ_TEST_MAIN_ARGV(
    CQ_CASE(the_program_is_the_source_line_for_line),
    CQ_CASE(the_blocks_cost_what_their_modules_say_they_cost),
    CQ_CASE(the_program_is_the_sum_of_its_blocks),
    CQ_CASE(the_cached_prefix_map_matches_a_linear_dispatch_at_every_slot),
    CQ_CASE(the_classical_row_agrees_with_the_oracle_on_every_anchor),
    CQ_CASE(the_full_anchor_table_stays_classical_and_the_circuit_set_is_constant),
    CQ_CASE(the_slot_boundaries_match_an_independent_four_valued_scan),
    CQ_CASE(the_rows_spans_are_pairwise_disjoint),
    CQ_CASE(the_four_products_and_the_106_bit_lane_map),
    CQ_CASE(the_compute_half_is_a_palindrome_at_an_asymmetric_mask),
    CQ_CASE(two_programs_in_one_region_do_not_collide),
    CQ_CASE(l1_sweep),
    CQ_CASE(controlled),
    CQ_CASE(l4_goldens),
    CQ_CASE(dst_owns_64_qubits_and_the_scratch_comes_back)
)
