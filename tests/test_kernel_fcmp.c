/* tests/test_kernel_fcmp.c — M36, K18. `fcmp`, all fourteen LLVM predicates at
 * f64, and the FIRST floating-point kernel of libcqops. PRD-v2 §5, §7;
 * docs/constructions/K18.md.
 *
 * THE SUBJECT OF THIS FILE IS THE PREDICATE TABLE AND THE SLOT ARITHMETIC.
 * Whether `lower_eq!`, `lower_ult!`, `lower_sub!` and `lower_mux!` compute the
 * right bits is M14's, M16's and M17's suites next door, and whether
 * `(ea == 0x7FF) & (fa != 0)` is the right NaN test is M31's. What is new here
 * is (i) which of the ten `soft_fcmp_*` bodies each of the fourteen LLVM rows
 * routes to and whether it swaps its operands, (ii) which SLOT each of up to
 * 42 blocks occupies, and (iii) whether each `==`, `<` and `>` occurrence got
 * its `lower_not1!`. Every one of the three is invisible to a gate count: K9's
 * measured `uge`-meaning-`ule` emitted the identical tuple, kept the
 * palindrome and left scratch clean.
 *
 * SO L1'S ORACLE IS THE HOST DOUBLE COMPARISON AND SHARES NOTHING WITH THE
 * PORT. fcmp_pred.c evaluates its own transcription of the Julia bodies over
 * `uint64_t` for the classical short-circuit (PRD-v2 §7.4); if this reference
 * did the same it would agree with a wrong row rather than catch it — the
 * Step 18 finding, in a new column. The bits go into a `double` through
 * fphost's memcpy and the verdict comes from C's `<`, `==` and `isunordered`,
 * which are LLVM's own definitions of the fourteen predicates.
 *
 * AND ASKING THE HOST IS SAFE HERE IN A WAY IT IS NOT FOR `fadd`. PRD-v2 §7.4's
 * five IEEE-unspecified cells are all about arithmetic RESULTS — which NaN
 * payload comes out — and a COMPARISON has none: IEEE 754 specifies the result
 * of every one of the fourteen predicates on every input, NaNs included, so
 * the oracle reads the same on x86 and on arm64. That is a real difference
 * from M33 and nobody should import `fadd`'s pinned-by-table machinery here.
 * The one row this file still pins as LITERALS is the unordered one, because
 * it is what separates the `o*` family from the `u*` family and a host that
 * agreed with a wrong family split would be a host, not an oracle.
 *
 * SLOTS AND GATES COME APART, AS THEY DO IN M31 (D-K18-6). A block operand is
 * a view over `a` with a constant fill, or a constant span, so the §3 fold
 * table elides gates at EVERY operand mask including the all-quantum one L4
 * pins. Every composition identity below is over SLOTS, and the slot scan's
 * prediction is four-valued — NONE / X / CX / CCX — walked with a separate
 * stream cursor. Deleting the constant rows from the prediction turns the
 * instrument off rather than simplifying it.
 *
 * WIDTH 64 AND NOTHING ELSE, DELIBERATELY, AND IT IS A REFUSAL RATHER THAN A
 * GAP. PRD-v2 §1 scopes v2 to `f64` and every `soft_fcmp_*` is
 * `(UInt64, UInt64)`, so a second width would be a fiction; the kernel hard-
 * errors on one in both configurations and tests/test_kernel_fcmp_death.c
 * drives that for two predicates.
 */

#include "kernels/fcmp.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/fpclass.h"
#include "kernels/fpfield.h"
#include "kernels/mux.h"
#include "reg.h"
#include "shadow.h"
#include "scratch.h"
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

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- L1's oracle: LLVM's fourteen definitions, over the host operator. --- */

/* NOTHING HERE IS DERIVED FROM THE PORT. `ult` is spelled `unordered || a < b`
 * — LLVM's definition — and NOT rebuilt as `uno | olt` out of the same pieces
 * fcmp_pred.c composes, which is the Step 18 trap: an oracle that shares a
 * decomposition with the implementation is blind to exactly what that
 * decomposition gets wrong. `ord`/`uno` go through `isunordered` rather than
 * through a second NaN test of our own. */
static int host_fcmp(uint64_t ab, uint64_t bb, cq_fcmp_pred p)
{
    double x = cq_fphost_from_bits(ab), y = cq_fphost_from_bits(bb);
    int un = isunordered(x, y) != 0;

    switch (p) {
    case CQ_FCMP_OEQ: return !un && x == y;
    case CQ_FCMP_OGT: return !un && x >  y;
    case CQ_FCMP_OGE: return !un && x >= y;
    case CQ_FCMP_OLT: return !un && x <  y;
    case CQ_FCMP_OLE: return !un && x <= y;
    case CQ_FCMP_ONE: return !un && x != y;
    case CQ_FCMP_ORD: return !un;
    case CQ_FCMP_UNO: return  un;
    case CQ_FCMP_UEQ: return  un || x == y;
    case CQ_FCMP_UGT: return  un || x >  y;
    case CQ_FCMP_UGE: return  un || x >= y;
    case CQ_FCMP_ULT: return  un || x <  y;
    case CQ_FCMP_ULE: return  un || x <= y;
    default:          return  un || x != y;         /* UNE */
    }
}

/* ---- §7.12's anchors, composed. ----------------------------------------- */

#include "test_kernel_fcmp_anchors.inc"

/* ---- The fourteen kernels, as one table. -------------------------------- */

static int fcmp_circuit_anchors(int W, int i, cq_ref_w *v)
{
    const int available = fcmp_anchors(W, -1, NULL);
    const int picked = cq_fp_representative_binary_index(available, i);

    if (i < 0) return cq_fp_representative_count(available);
    return picked >= 0 ? fcmp_anchors(W, picked, v) : 0;
}

/* K9's shape: `dst` is one bit while the operands are 64, so the driver is
 * told through `w_dst` and the call adapter passes `sh->w[0]`. Without the
 * adapter `shape_of` REFUSES the narrow shape — which it must, because the
 * default call path would run every case at W = 1 and pass. */
static void fcmp_shape(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->n_src   = 2;
    out->w[0]    = CQ_FP64_W;
    out->w[1]    = CQ_FP64_W;
    out->w_dst   = 1;
    out->anchors = fcmp_circuit_anchors;
}

#define CQ_FCMP_ROWS(X)                                                        \
    X(oeq, OEQ) X(ogt, OGT) X(oge, OGE) X(olt, OLT) X(ole, OLE)                \
    X(one, ONE) X(ord, ORD) X(uno, UNO) X(ueq, UEQ) X(ugt, UGT)                \
    X(uge, UGE) X(ult, ULT) X(ule, ULE) X(une, UNE)

#define CQ_FCMP_DEFINE(nm, UP)                                                 \
    static void call_##nm(cq_ctx *ctx, cq_bit *dst, const cq_bit *const *src,  \
                          const cq_kd_shape *sh)                               \
    { cq_kernel_fcmp_##nm(ctx, dst, src[0], src[1], sh->w[0]); }               \
    static cq_ref_w ref_##nm(const cq_ref_w *s, const cq_kd_shape *sh)         \
    { (void)sh;                                                                \
      return cq_ref_w_make((uint64_t)host_fcmp(s[0].lo, s[1].lo,               \
                                               CQ_FCMP_##UP), 0u, 1); }
CQ_FCMP_ROWS(CQ_FCMP_DEFINE)

typedef struct {
    cq_kd_spec   spec;
    cq_fcmp_pred pred;
} fcmp_krow;

#define CQ_FCMP_ROW(nm, UP)                                                    \
    { { "fcmp_" #nm, NULL, NULL, fcmp_shape, call_##nm, ref_##nm },            \
      CQ_FCMP_##UP },

static const fcmp_krow ROWS[] = { CQ_FCMP_ROWS(CQ_FCMP_ROW) };
enum { N_FCMP_ROWS = 14 };

/* A row lost to a macro edit must break the build rather than quietly shrink
 * the sweep — a missing predicate fails by not existing, and nothing else in
 * this file could observe it. */
_Static_assert(sizeof ROWS / sizeof ROWS[0] == (size_t)N_FCMP_ROWS,
               "instructions.jl:7697-7733 dispatches fourteen LLVM predicates");

/* ---- L1 + L2 + L3 + L5. ------------------------------------------------- */

CQ_TEST(l1_sweep)
{
    for (int i = 0; i < N_FCMP_ROWS; i++)
        cq_kd_sweep_at(&ROWS[i].spec, CQ_FP64_W);
}

/* ---- Step 20's four regions, and §9's gate-tuple transform. -------------- */

/* THE REGION BODY IS THE TWO CHEAPEST PREDICATES AND THAT IS DELIBERATE.
 * kerneldrv.h: "`body` should be the suite's sweep at its CHEAP widths only"
 * — there is only one width here, so the axis this suite can economise on is
 * the PREDICATE. What the region sweep adds over `cq_kd_check_promotion` is
 * the axis's interaction with the §3 fold table across a whole L1 draw,
 * including the all-classical row at CQ_KD_CTRL_Q0 that no other fixture in
 * the project reaches; the promotion itself is per gate and width-independent,
 * and every one of the fourteen is covered by cq_kd_check_promotion below at
 * two kernel calls apiece. `uno` and `ord` are the two smallest programs and
 * between them they carry a body, a trailing `lower_not1!` and the copy-out. */
static void fcmp_narrow(void)
{
    cq_kd_sweep_at(&ROWS[CQ_FCMP_UNO].spec, CQ_FP64_W);
    cq_kd_sweep_at(&ROWS[CQ_FCMP_ORD].spec, CQ_FP64_W);
}

CQ_TEST(controlled)
{
    uint64_t reached = 0u;

    cq_kd_for_each_region("fcmp uno/ord", fcmp_narrow);

    for (int i = 0; i < N_FCMP_ROWS; i++)
        reached += cq_kd_check_promotion(&ROWS[i].spec, CQ_FP64_W);

    /* NOT VACUOUS: the promotion identity holds trivially where the
     * uncontrolled tuple is empty, so the caller owns the non-vacuity claim
     * (cq_kd_check_promotion returns the uncontrolled total for this reason). */
    CHECK(reached > 0u);
}

/* ---- L4: the goldens, at the all-quantum mask. -------------------------- */

static void check_counts(cq_gold *g, const fcmp_krow *r)
{
    cq_counter fwd, unc;

    cq_kd_measure(&r->spec, CQ_FP64_W, &fwd, &unc);

    CHECK_EQ(fwd.ry + fwd.rz + fwd.mz, 0);
    CHECK_EQ(unc.ry + unc.rz + unc.mz, 0);

    cq_gold_check(g, r->spec.name, "forward", CQ_FP64_W, fwd.x, fwd.cx, fwd.ccx);
    cq_gold_check(g, r->spec.name, "unc",     CQ_FP64_W, unc.x, unc.cx, unc.ccx);
}

CQ_TEST(l4_goldens)
{
    cq_gold g;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/fcmp.counts",
                      "M36 kernels/fcmp.c — K18 IEEE binary64 compares, all "
                      "fourteen LLVM predicates (sandwiched)",
                      "ALL-QUANTUM on `a` and `b`, whose width is the W column "
                      "(f64 only); dst is ONE bit; at ctrl_depth 0. The views' "
                      "constant lanes and the constant spans fold at EVERY "
                      "mask, so these are well below twice the slot count",
                      CQOPS_BENNETT_COMMIT))
        return;

    for (int i = 0; i < N_FCMP_ROWS; i++) check_counts(&g, &ROWS[i]);

    CHECK(cq_gold_close(&g));
}

/* ---- dst owns one qubit and the scratch comes back. --------------------- */

CQ_TEST(dst_owns_one_qubit_and_the_scratch_comes_back)
{
    for (int i = 0; i < N_FCMP_ROWS; i++) {
        cq_fcmp_row rows[CQ_FCMP_MAX_ROWS];
        int n = cq_fcmp_program(ROWS[i].pred, rows);
        uint32_t peak = 0u;
        uint32_t owned = cq_kd_peak(&ROWS[i].spec, CQ_FP64_W, &peak);

        CHECK_EQ(owned, 1u);
        /* The whole program's region plus dst's one bit. L2 looks after the
         * call and cannot see scratch that was taken and tidily released, so
         * the high-water mark is the only instrument for it. */
        CHECK_EQ(peak, cq_fcmp_region(rows, n) + 1u);
    }
}

#include "test_kernel_fcmp_pred.inc"
#include "test_kernel_fcmp_slots.inc"
#include "test_kernel_fcmp_spans.inc"

CQ_TEST_MAIN_ARGV(
    CQ_CASE(the_dispatch_is_four_swaps_and_zero_inversions),
    CQ_CASE(the_unordered_row_is_the_literal_o_and_u_family_split),
    CQ_CASE(the_classical_row_agrees_with_the_host_on_every_anchor),
    CQ_CASE(the_full_anchor_table_stays_classical_and_the_circuit_set_is_constant),
    CQ_CASE(the_programs_are_well_formed_and_reach_no_variable_shift),
    CQ_CASE(the_blocks_cost_what_their_modules_say_they_cost),
    CQ_CASE(each_predicate_is_the_sum_of_its_blocks),
    CQ_CASE(the_slot_boundaries_match_an_independent_four_valued_scan),
    CQ_CASE(the_programs_spans_are_pairwise_disjoint),
    CQ_CASE(the_compute_half_is_a_palindrome_at_an_asymmetric_mask),
    CQ_CASE(two_programs_in_one_region_do_not_collide),
    CQ_CASE(the_four_swapped_predicates_are_their_twin_on_exchanged_operands),
    CQ_CASE(l1_sweep),
    CQ_CASE(controlled),
    CQ_CASE(l4_goldens),
    CQ_CASE(dst_owns_one_qubit_and_the_scratch_comes_back)
)
