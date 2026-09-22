/* tests/test_kernel_fpfield.c — M31, K22. The IEEE binary64 field views and
 * the four class predicates. PRD-v2 §3.1, §5, §7; docs/constructions/K22.md.
 *
 * THE SUBJECT OF THIS FILE IS THE SLOT ARITHMETIC AND THE POLARITY, not the
 * comparator. Whether `lower_eq!` computes the right bit is M16's suite next
 * door. What is new here, and what PRD-v2 §7.1 names as the thing a kernel of
 * this shape actually gets wrong, is (i) which SLOT each block occupies, (ii)
 * which lanes of `a` each view addresses, and (iii) whether a source's `==`
 * got its `lower_not1!`. All three are invisible to a gate count: an exponent
 * view that swallowed the sign bit emits the identical tuple, keeps the
 * palindrome and leaves scratch clean, and so does reading `cq_eq_flag` as
 * "equal". Only L1 against an oracle that shares nothing with the port can see
 * them, which is why the reference here is the HOST — memcpy the bits into a
 * `double` and call `isnan` — and not a second transcription of the four rows.
 *
 * SLOTS AND GATES COME APART, AND M31 IS THE FIRST MODULE WHERE THEY DO. Every
 * `eq` block runs at W = 64 over an operand with 53 (exponent) or 12
 * (fraction) CONSTANT lanes and against a CONSTANT span, so the §3 fold table
 * elides gates at EVERY mask including the all-quantum one L4 pins. Every
 * composition identity below is therefore over SLOTS, and the slot scan's
 * prediction is four-valued — NONE / X / CX / CCX — walked with a separate
 * stream cursor. Deleting the constant rows from the prediction turns the
 * instrument off rather than simplifying it.
 *
 * WIDTH 64 AND NOTHING ELSE, DELIBERATELY. PRD-v2 §1 scopes v2 to `f64`, so
 * there is no ladder to sweep: the module names no width parameter at all and
 * a second width would be a fiction. That is the one place this suite is
 * thinner than a v1 kernel's, and it is thinner because the surface is.
 */

#include "kernels/fpclass.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/cmp.h"
#include "kernels/fpfield.h"
#include "reg.h"
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

/* ---- The four kernels, as one table. ------------------------------------ */

static int fp_circuit_anchors(int W, int i, cq_ref_w *v)
{
    const int available = cq_fp_anchors_unary(W, -1, NULL);
    const int picked = cq_fp_representative_unary_index(available, i);

    if (i < 0) return cq_fp_representative_count(available);
    return picked >= 0 ? cq_fp_anchors_unary(W, picked, v) : 0;
}

/* K9's shape: `dst` is one bit while the source is 64, so the driver is told
 * through `w_dst` and the call adapter passes nothing — these kernels are
 * unary and name their operand, so there is no `W` to hand over at all.
 *
 * `anchors` IS WHY THIS SHAPE EXISTS AT ALL. At 32 samples over a 64-bit value
 * space a uniform draw reaches a NaN, an infinity or a subnormal with
 * probability ~0, and every one of those is a branch of the predicate. The
 * provider forces §7.12's singles into the draw. */
static void fp_shape(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->n_src   = 1;
    out->w[0]    = CQ_FP64_W;
    out->w_dst   = 1;
    out->anchors = fp_circuit_anchors;
}

/* L1's ORACLE IS THE HOST, AND IT SHARES NOTHING WITH THE PORT. `fpclass.c`
 * evaluates its own four rows over `uint64_t` for the classical short-circuit
 * (PRD-v2 §7.4); if this reference did the same it would agree with a wrong
 * row rather than catch it — the Step 18 finding, in a new column. So the bits
 * go into a `double` through fphost's memcpy and the verdict comes from
 * <math.h>. The host is SAFE to ask HERE and refused inside the library:
 * §7.4's five IEEE-unspecified cells are all about arithmetic RESULTS, and
 * classification is not one of them — `isnan` is exact on every host. */
static int host_class(uint64_t bits, cq_fp_class cls)
{
    double d = cq_fphost_from_bits(bits);

    if (cls == CQ_FP_IS_NAN)  return isnan(d) != 0;
    if (cls == CQ_FP_IS_INF)  return isinf(d) != 0;
    if (cls == CQ_FP_IS_ZERO) return d == 0.0;
    return fpclassify(d) == FP_SUBNORMAL;
}

#define CQ_FP_ROWS(X)                                                          \
    X(is_nan,       CQ_FP_IS_NAN)                                              \
    X(is_inf,       CQ_FP_IS_INF)                                              \
    X(is_zero,      CQ_FP_IS_ZERO)                                             \
    X(is_subnormal, CQ_FP_IS_SUBNORMAL)

#define CQ_FP_DEFINE(p, CLS)                                                   \
    static void call_##p(cq_ctx *ctx, cq_bit *dst, const cq_bit *const *src,   \
                         const cq_kd_shape *sh)                                \
    { (void)sh; cq_kernel_fp_##p(ctx, dst, src[0]); }                          \
    static cq_ref_w ref_##p(const cq_ref_w *s, const cq_kd_shape *sh)          \
    { (void)sh;                                                                \
      return cq_ref_w_make((uint64_t)host_class(s[0].lo, CLS), 0u, 1); }
CQ_FP_ROWS(CQ_FP_DEFINE)

typedef struct {
    cq_kd_spec  spec;
    cq_fp_class cls;
} fp_krow;

#define CQ_FP_ROW(p, CLS)                                                      \
    { { "fp_" #p, NULL, NULL, fp_shape, call_##p, ref_##p }, CLS },

static const fp_krow ROWS[] = { CQ_FP_ROWS(CQ_FP_ROW) };
enum { N_FP_ROWS = 4 };

/* A row lost to a macro edit must break the build rather than quietly shrink
 * the sweep — a missing predicate fails by not existing. */
_Static_assert(sizeof ROWS / sizeof ROWS[0] == (size_t)N_FP_ROWS,
               "fpclass.h declares four class predicates");

/* ---- L1 + L2 + L3 + L5. ------------------------------------------------- */

CQ_TEST(l1_sweep)
{
    for (int i = 0; i < N_FP_ROWS; i++) cq_kd_sweep_at(&ROWS[i].spec, CQ_FP64_W);
}

/* ---- Step 20's four regions, and §9's gate-tuple transform. -------------- */

static void fp_narrow(void)
{
    for (int i = 0; i < N_FP_ROWS; i++) cq_kd_sweep_at(&ROWS[i].spec, CQ_FP64_W);
}

CQ_TEST(controlled)
{
    uint64_t reached = 0u;

    /* THE ALL-CLASSICAL ROW AT CQ_KD_CTRL_Q0 IS THE ONE THAT MATTERS, and it
     * is the only fixture shape that can see a controlled region silently made
     * unconditional: the R9 short-circuit writes `dst` through cq_emit_x,
     * whose constant row would run on BOTH branches if M06 did not intercept
     * it. The sweep's slot 0 IS that row. */
    cq_kd_for_each_region("fp class predicates", fp_narrow);

    for (int i = 0; i < N_FP_ROWS; i++)
        reached += cq_kd_check_promotion(&ROWS[i].spec, CQ_FP64_W);

    /* NOT VACUOUS: the promotion identity holds trivially where the
     * uncontrolled tuple is empty, so the caller owns the non-vacuity claim
     * (cq_kd_check_promotion returns the uncontrolled total for this reason). */
    CHECK(reached > 0u);
}

/* ---- L4: the goldens, at the all-quantum mask. -------------------------- */

/* ALL-QUANTUM ON `a` IS THE MASK, for Rule 14's reason: with no demotion (D6)
 * a mask can only drift TOWARDS Q, so all-quantum is the fixed point and the
 * one mask at which forward and `_unc` emit the same tuple. They are still
 * measured and pinned SEPARATELY — asserting `unc == forward` is the R6
 * mistake by name.
 *
 * AND THE MASK DOES NOT MAKE THE COUNTS FOLD-FREE HERE, which is what parts
 * M31 from every v1 kernel. 53 lanes of the exponent view and 12 of the
 * fraction view are CQ_BIT_ZERO whatever `a`'s mask is, and the constant spans
 * are constant by construction. The golden is what those folds cost; the slot
 * scan is what says the folds landed where the lane table says. */
static void check_counts(cq_gold *g, const fp_krow *r)
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

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/fpfield.counts",
                      "M31 kernels/fpclass.c — K22 IEEE binary64 class "
                      "predicates (sandwiched)",
                      "ALL-QUANTUM on `a`, whose width is the W column (f64 "
                      "only); dst is ONE bit; at ctrl_depth 0. The views' "
                      "constant lanes and the constant spans fold at EVERY "
                      "mask, so these are well below the slot count",
                      CQOPS_BENNETT_COMMIT))
        return;

    for (int i = 0; i < N_FP_ROWS; i++) check_counts(&g, &ROWS[i]);

    CHECK(cq_gold_close(&g));
}

/* ---- dst owns one qubit and the scratch comes back. --------------------- */

CQ_TEST(dst_owns_one_qubit_and_the_scratch_comes_back)
{
    for (int i = 0; i < N_FP_ROWS; i++) {
        uint32_t peak = 0u;
        uint32_t owned = cq_kd_peak(&ROWS[i].spec, CQ_FP64_W, &peak);

        CHECK_EQ(owned, 1u);
        /* The whole region plus dst's one bit. L2 looks after the call and
         * cannot see scratch that was taken and tidily released, so the
         * high-water mark is the only instrument for it. */
        CHECK_EQ(peak, cq_fp_class_region(ROWS[i].cls) + 1u);
    }
}

#include "test_kernel_fpfield_views.inc"
#include "test_kernel_fpfield_slots.inc"
#include "test_kernel_fpfield_block.inc"

CQ_TEST_MAIN_ARGV(
    CQ_CASE(a_view_emits_nothing_and_allocates_nothing),
    CQ_CASE(a_view_addresses_the_lanes_the_source_names),
    CQ_CASE(a_view_over_a_classical_rail_is_all_constant),
    CQ_CASE(the_constant_spans_are_the_softfloat_patterns),
    CQ_CASE(the_full_anchor_table_stays_classical_and_the_circuit_set_is_constant),
    CQ_CASE(the_eq_block_costs_what_m16_says_it_costs),
    CQ_CASE(each_predicate_is_two_eq_blocks_a_not1_per_eq_and_one_and),
    CQ_CASE(the_slot_boundaries_match_an_independent_four_valued_scan),
    CQ_CASE(the_blocks_five_spans_are_pairwise_disjoint),
    CQ_CASE(l1_sweep),
    CQ_CASE(controlled),
    CQ_CASE(l4_goldens),
    CQ_CASE(dst_owns_one_qubit_and_the_scratch_comes_back),
    CQ_CASE(the_class_block_is_a_palindrome_and_gives_the_pool_back),
    CQ_CASE(two_blocks_in_one_region_do_not_collide),
    CQ_CASE(the_blocks_flag_is_the_predicate_not_its_negation),
    CQ_CASE(the_kernels_are_the_block_twice_plus_their_copyout)
)
