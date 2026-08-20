/* tests/test_kernel_cmp.c — M16, Step 13. K9: `icmp`, all ten predicates.
 *
 * THE SUBJECT OF THIS FILE IS THE DERIVATION TABLE, not the comparator. The
 * three primitives are 16 lines of Bennett each; what a reader will get wrong
 * — and what no gate count can see — is a row that says `uge` and means `ule`.
 * `lower_icmp!` (arith.jl:409-418) puts the operand swap on FOUR of the ten
 * (`ugt`, `ule`, `sgt`, `sle`) and the trailing negation on a DIFFERENT four
 * plus two (`ne`, `ule`, `uge`, `sle`, `sge`), and the two sets overlap in two
 * places. Every predicate costs within one X of its sibling, so an L4 golden
 * would happily pin the wrong one. Only L1 sees it, and only against a
 * reference derived independently — which is why cq_ref_icmp transcribes the
 * ten rows a second time and w_slt branches on sign bits rather than repeating
 * the kernel's bias flip (refmodel.h).
 *
 * THREE THINGS HERE THAT THE ADD SUITE DID NOT NEED:
 *
 *   - `dst` IS ONE BIT. K9 is the only kernel that keeps Rule 7's single-`W`
 *     signature while producing a result of a different width (K09.md §5
 *     delta 10; K5's casts have two widths but name both), so the driver is
 *     told through
 *     cq_kd_shape's `w_dst` and every call adapter passes `sh->w[0]` — the
 *     SOURCE width — as the kernel's W. A harness that passed `w_dst` would
 *     run every compare at W=1 and pass.
 *   - THE PER-MASK FOLD FORMULA. K09.md §3.3.1 gives the exact cost of a
 *     partially-classical operand, and it is asymmetric: a classical ZERO in
 *     `ult`'s `a` kills TWO compute steps (Phase C j==0 and j==2) and in its
 *     `b` only one, while a classical ONE removes NOTHING — it turns a CX into
 *     an X and a CCX into a CX, one gate either way. That formula is asserted
 *     here rather than assumed, because "partially classical is strictly
 *     cheaper" is false and was written down three times before it was caught.
 *   - i80. `icmp` ships at {i1,i8,i16,i32,i64,i80} (opcode_table.yaml:222) and
 *     cq_kd_sweep's ladder stops at 64, so 80 is driven explicitly. It is the
 *     width where the reference's 64-bit word seam falls inside a register.
 */

#include "kernels/cmp.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "reg.h"
#include "sink_count.h"

#include "support/bitkinds.h"
#include "support/goldens.h"
#include "support/harness.h"
#include "support/kerneldrv.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"
#include "support/refmodel.h"

#include <stdio.h>

/* Which of the three constructions carries the predicate, and how. */
enum { P_EQ, P_ULT, P_SLT };
enum { KEEP  = 0, INVERT = 1 };   /* does copy-out negate the raw flag?      */
enum { AB    = 0, BA     = 1 };   /* operand order handed to the primitive   */
enum { DERIV = 0, PRIM   = 1 };   /* a primitive gets the full value ladder  */

/* K9 is the only kernel with |dst| != W. Everything else is the default. */
static void cmp_shape(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->w_dst = 1;
}

/* THE TEN ROWS OF `lower_icmp!`, AS ONE TABLE — deliberately laid out like
 * K09.md §1.1 so the two can be read side by side. Every per-predicate
 * artefact this file needs is generated from it: the call adapter, the
 * reference wrapper, the cq_kd_spec, and the (primitive, swap, invert) triple
 * that L4's closed form and the per-mask fold formula both consume. Thirty
 * hand-copied bodies would be thirty independent chances at the one mistake
 * this suite exists to catch. */
#define CQ_CMP_ROWS(X)                                                         \
    X(eq,  CQ_ICMP_EQ,  P_EQ,  AB, INVERT, PRIM )                              \
    X(ne,  CQ_ICMP_NE,  P_EQ,  AB, KEEP,   DERIV)                              \
    X(ult, CQ_ICMP_ULT, P_ULT, AB, INVERT, PRIM )                              \
    X(ugt, CQ_ICMP_UGT, P_ULT, BA, INVERT, DERIV)                              \
    X(ule, CQ_ICMP_ULE, P_ULT, BA, KEEP,   DERIV)                              \
    X(uge, CQ_ICMP_UGE, P_ULT, AB, KEEP,   DERIV)                              \
    X(slt, CQ_ICMP_SLT, P_SLT, AB, INVERT, PRIM )                              \
    X(sgt, CQ_ICMP_SGT, P_SLT, BA, INVERT, DERIV)                              \
    X(sle, CQ_ICMP_SLE, P_SLT, BA, KEEP,   DERIV)                              \
    X(sge, CQ_ICMP_SGE, P_SLT, AB, KEEP,   DERIV)

#define CQ_CMP_DEFINE(p, PRED, prim, swap, inv, deep)                          \
    static void call_##p(cq_ctx *ctx, cq_bit *dst, const cq_bit *const *src,   \
                         const cq_kd_shape *sh)                                \
    { cq_kernel_##p(ctx, dst, src[0], src[1], sh->w[0]); }                     \
    static cq_ref_w ref_##p(const cq_ref_w *s, const cq_kd_shape *sh)          \
    { return cq_ref_w_make((uint64_t)cq_ref_icmp(PRED, s[0], s[1], sh->w[0]),  \
                           0u, 1); }
CQ_CMP_ROWS(CQ_CMP_DEFINE)

typedef struct {
    cq_kd_spec   spec;
    cq_icmp_pred pred;
    int          prim, swap, invert, primitive;
} cmp_row;

/* `kernel` is set as well as `call`, and the driver ignores it — call_kernel
 * prefers the adapter (tests/support/kerneldrv.c). It is carried so the
 * derivation cases can reach the entry point DIRECTLY, without going through
 * a cq_kd_shape, which is what lets them run two predicates into one context
 * and compare the streams qubit for qubit. */
#define CQ_CMP_ROW(p, PRED, prim, swap, inv, deep)                             \
    { { #p, cq_kernel_##p, NULL, cmp_shape, call_##p, ref_##p },               \
      PRED, prim, swap, inv, deep },

static const cmp_row ROWS[] = { CQ_CMP_ROWS(CQ_CMP_ROW) };
enum { N_ROWS = 10 };

/* `_IR_ICMP_PREDS` is exactly ten (ir_types.jl:8-10) and `lower_icmp!` throws
 * on anything else. A row added without a golden width, or lost to a macro
 * edit, must break the build rather than quietly shrink the sweep. */
_Static_assert(sizeof ROWS / sizeof ROWS[0] == (size_t)N_ROWS,
               "icmp has exactly ten predicates");

/* K09.md §2: the compute half's step count, and §4's scratch region. Both are
 * a function of W alone once I6(b) pre-materialises the region. */
static int n_compute_of(int prim, int W)
{
    if (prim == P_EQ)  return 5 * W - 3;
    if (prim == P_ULT) return 6 * W + 1;
    return 8 * W + 3;
}

static uint32_t scratch_of(int prim, int W)
{
    if (prim == P_EQ)  return (uint32_t)(2 * W - 1);
    if (prim == P_ULT) return (uint32_t)(3 * W + 1);
    return (uint32_t)(5 * W + 1);
}

/* ---- L1 + L2 + L3 + L5, the whole sweep. -------------------------------- */

/* WHAT EACH PREDICATE ACTUALLY GETS, said here and printed by every run,
 * because a cap nobody can see reads as coverage.
 *
 * All ten get the FULL CROSS PRODUCT at W <= 5 — every value pair against
 * every mask pair, which is exhaustive over the space and not over a diagonal
 * of it — plus sampling at each shipped width including i80. The three
 * PRIMITIVES additionally get cq_kd_sweep's W=8 stage: structured arithmetic
 * corners plus seeded sampling, crossed with every mask pair. That stage used
 * to be value-EXHAUSTIVE and was 63% of this suite for no coverage — see
 * kernelsweep.c:structured_pairs, and note the plan §4 row that asked for it
 * was corrected rather than quietly narrowed.
 *
 * The seven derived predicates are the SAME CIRCUIT as their primitive: the
 * same step function over the same scratch, differing only in which operand
 * arrives first and whether copy-out appends one X. That is asserted
 * structurally, on the recorded gate stream, in
 * `each_derived_predicate_is_its_primitives_stream` — which is a stronger
 * statement about them than more values at one more width would be. */
static void sweep_pred(const cmp_row *r)
{
    if (r->primitive) {
        cq_kd_sweep(&r->spec);                     /* {1..5} x {8} x {16,32,64} */
    } else {
        for (int W = 1; W <= 5; W++) cq_kd_sweep_at(&r->spec, W, 1);
        cq_kd_sweep_at(&r->spec, 8,  0);
        cq_kd_sweep_at(&r->spec, 16, 0);
        cq_kd_sweep_at(&r->spec, 32, 0);
        cq_kd_sweep_at(&r->spec, 64, 0);
    }
    cq_kd_sweep_at(&r->spec, 80, 0);               /* shipped, and above 64 */
}

CQ_TEST(k9_eq_family_sweep)
{
    sweep_pred(&ROWS[0]); sweep_pred(&ROWS[1]);
}

CQ_TEST(k9_unsigned_family_sweep)
{
    for (int i = 2; i <= 5; i++) sweep_pred(&ROWS[i]);
}

CQ_TEST(k9_signed_family_sweep)
{
    for (int i = 6; i <= 9; i++) sweep_pred(&ROWS[i]);
}

/* Step 20 — the same four levels under PRD §9's four regions, plus §9's
 * gate-tuple transform at every shipped width. The sweep body is this suite's
 * OWN, at its cheap widths only: the promotion is per gate and width-
 * independent, so what the axis adds is its interaction with the §3 fold table,
 * which is exhausted where the value cross product is. Every shipped width is
 * still covered by cq_kd_check_promotion, at two kernel calls apiece. */
static void cmp_narrow(void)
{
    /* ALL TEN PREDICATES, because a derived row differs from its primitive by
     * an operand swap or a trailing X and only L1 can tell them apart — the
     * finding that made `each_derived_predicate_is_its_primitives_stream`
     * necessary. Narrowing to the three primitives here would drop exactly the
     * seven rows that are hardest to see. */
    for (int i = 0; i < N_ROWS; i++)
        for (int W = 1; W <= 4; W++) cq_kd_sweep_at(&ROWS[i].spec, W, 1);
}

CQ_TEST(controlled)
{
    uint64_t reached = 0u;

    /* K9's `dst` IS ONE BIT, so the promotion has a shape here it has nowhere
     * else: a single-lane result driven by a compare's whole scratch region.
     * i80 is in the ladder because icmp ships there (opcode_table.yaml:222) and
     * i128 is not, because it does not (the mirror image of add/sub's fence). */
    static const int widths[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64, 80 };

    cq_kd_for_each_region("icmp", cmp_narrow);

    for (int i = 0; i < N_ROWS; i++)
        for (size_t w = 0; w < sizeof widths / sizeof widths[0]; w++)
            reached += cq_kd_check_promotion(&ROWS[i].spec, widths[w]);
    /* NOT VACUOUS: the identity above is an equality between two measurements,
     * and it holds trivially where the uncontrolled tuple is empty. K4 makes
     * that a real case rather than a hypothetical — its all-ones L4 fixture
     * saturates under D8 at every non-power-of-two width — so the ladder has to
     * say it reached something. */
    CHECK(reached > 0u);
}

/* ---- L4: the goldens, at the all-quantum mask. -------------------------- */

/* K09.md §3.2, from the compute half outward:
 *
 *      eq   0 X       4W-2 CX   W-1 CCX     n_compute = 5W-3
 *      ult  W+1 X     3W   CX   2W  CCX     n_compute = 6W+1
 *      slt  W+3 X     5W   CX   2W  CCX     n_compute = 8W+3
 *
 * sandwiched = 2 x that + copy-out, and copy-out is 1 CX always plus 1 X only
 * where the raw scratch flag is the NEGATION of the answer. The raw flags are
 * `a != b`, `a >=u b` and `a >=s b`, so the five predicates that invert are
 * eq / ult / ugt / slt / sgt and the five that do not are ne / ule / uge /
 * sle / sge — the latter cost exactly one X less, which is Bennett's double
 * negation folding into the copy-out (K09.md §5 delta 2). */
static void closed_form(const cmp_row *r, int W,
                        uint64_t *x, uint64_t *cx, uint64_t *ccx)
{
    uint64_t hx, hcx, hccx;

    if (r->prim == P_EQ) {
        hx = 0u;                 hcx = (uint64_t)(4 * W - 2); hccx = (uint64_t)(W - 1);
    } else if (r->prim == P_ULT) {
        hx = (uint64_t)(W + 1);  hcx = (uint64_t)(3 * W);     hccx = (uint64_t)(2 * W);
    } else {
        hx = (uint64_t)(W + 3);  hcx = (uint64_t)(5 * W);     hccx = (uint64_t)(2 * W);
    }

    *x   = 2u * hx   + (uint64_t)(r->invert ? 1 : 0);
    *cx  = 2u * hcx  + 1u;
    *ccx = 2u * hccx;
}

/* ALL-QUANTUM IS THE MASK, for Rule 14's reason: with no demotion (D6) an
 * operand mask can only drift TOWARDS Q between a forward call and its
 * uncompute, so all-quantum is the fixed point and the one mask at which the
 * two passes emit the same tuple. The two are still measured and pinned
 * SEPARATELY — asserting `unc == forward` is the R6 mistake by name. */
static void check_counts(cq_gold *g, const cmp_row *r, int W)
{
    cq_counter fwd, unc;
    uint64_t x, cx, ccx;

    closed_form(r, W, &x, &cx, &ccx);
    cq_kd_measure(&r->spec, W, &fwd, &unc);

    CHECK_GATES(fwd.x, fwd.cx, fwd.ccx, x, cx, ccx);
    CHECK_GATES(unc.x, unc.cx, unc.ccx, x, cx, ccx);
    CHECK_EQ(fwd.ry + fwd.rz + fwd.mz, 0);
    CHECK_EQ(unc.ry + unc.rz + unc.mz, 0);

    cq_gold_check(g, r->spec.name, "forward", W, fwd.x, fwd.cx, fwd.ccx);
    cq_gold_check(g, r->spec.name, "unc",     W, unc.x, unc.cx, unc.ccx);
}

CQ_TEST(l4_goldens)
{
    /* The shipped icmp ladder plus 2 and 4, which are where the eq OR-prefix
     * first has an interior iteration and where W=1's empty `or` region is
     * left behind. */
    static const int WS[] = { 1, 2, 4, 8, 16, 32, 64, 80 };
    cq_gold g;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/cmp.counts",
                      "M16 kernels/cmp.c — K9 icmp, all ten predicates "
                      "(sandwiched, all-quantum operands)", CQOPS_BENNETT_COMMIT))
        return;

    for (int i = 0; i < N_ROWS; i++)
        for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++)
            check_counts(&g, &ROWS[i], WS[j]);

    CHECK(cq_gold_close(&g));
}

/* K09.md §3.4's evaluated table, W=8 column, transcribed as literals. This is
 * what cross-checks `closed_form` itself — the goldens above are generated
 * from it, so on their own they would pin whatever it happens to compute. */
CQ_TEST(the_w8_column_matches_k09s_evaluated_table)
{
    static const uint64_t W8[N_ROWS][3] = {
        {  1u, 61u, 14u },   /* eq   76  = 10W-4 */
        {  0u, 61u, 14u },   /* ne   75  = 10W-5 */
        { 19u, 49u, 32u },   /* ult  100 = 12W+4 */
        { 19u, 49u, 32u },   /* ugt  100 — the swap is free */
        { 18u, 49u, 32u },   /* ule  99  = 12W+3 */
        { 18u, 49u, 32u },   /* uge  99 */
        { 23u, 81u, 32u },   /* slt  136 = 16W+8 */
        { 23u, 81u, 32u },   /* sgt  136 */
        { 22u, 81u, 32u },   /* sle  135 = 16W+7 */
        { 22u, 81u, 32u }    /* sge  135 */
    };

    for (int i = 0; i < N_ROWS; i++) {
        cq_counter fwd, unc;
        uint64_t x, cx, ccx;

        closed_form(&ROWS[i], 8, &x, &cx, &ccx);
        CHECK_GATES(x, cx, ccx, W8[i][0], W8[i][1], W8[i][2]);

        cq_kd_measure(&ROWS[i].spec, 8, &fwd, &unc);
        CHECK_GATES(fwd.x, fwd.cx, fwd.ccx, W8[i][0], W8[i][1], W8[i][2]);
        CHECK_GATES(unc.x, unc.cx, unc.ccx, W8[i][0], W8[i][1], W8[i][2]);
    }
}

/* ---- One run at an EXPLICIT operand mask, with the stream recorded. ------ */

/* Both passes into their own mock, so the counts are pinned separately. The
 * value is checked on the way through: a count case that never looks at the
 * answer would happily pin the gate profile of a wrong circuit. */
static void run_masked(const cmp_row *r, int W, cq_ref_w va, cq_ref_w qa,
                       cq_ref_w vb, cq_ref_w qb, cq_mock *fwd, cq_mock *unc)
{
    cq_ctx ctx;
    cq_sink s_fwd = cq_mock_sink(fwd);
    cq_sink s_unc = cq_mock_sink(unc);
    cq_kd_shape sh;
    uint64_t want, got;

    cmp_shape(W, &sh);
    va = cq_ref_w_make(va.lo, va.hi, W);
    vb = cq_ref_w_make(vb.lo, vb.hi, W);
    want = (uint64_t)cq_ref_icmp(r->pred, va, vb, W);

    cq_mock_reset(fwd);
    cq_mock_reset(unc);
    cq_ctx_init(&ctx, &s_fwd);

    int32_t ha = cq_bk_reg_w(&ctx, (uint32_t)W, va, qa);
    int32_t hb = cq_bk_reg_w(&ctx, (uint32_t)W, vb, qb);
    int32_t hd = cq_reg_alloc_zero(&ctx.regs, 1u);
    const cq_bit *src[2] = { cq_reg_cbits(&ctx.regs, ha),
                             cq_reg_cbits(&ctx.regs, hb) };

    /* Building the operands materialises their set bits, which emits X. Drop
     * that, so what is recorded is the KERNEL. */
    cq_mock_reset(fwd);
    r->spec.call(&ctx, cq_reg_bits(&ctx.regs, hd), src, &sh);

    got = cq_pc_value(&ctx, hd);
    if (got != want)
        cq_h_fail(__FILE__, __LINE__, "%s W=%d: dst = %llu, want %llu",
                  r->spec.name, W, (unsigned long long)got,
                  (unsigned long long)want);

    ctx.sink = &s_unc;
    r->spec.call(&ctx, cq_reg_bits(&ctx.regs, hd), src, &sh);

    if (cq_pc_value(&ctx, hd) != 0u)
        cq_h_fail(__FILE__, __LINE__, "%s W=%d: dst is not 0 after uncompute",
                  r->spec.name, W);

    cq_ctx_dispose(&ctx);
}

/* The recorded stream is [compute half] [copy-out] [compute half reversed].
 * A gate COUNT cannot see a divergence here — risk R8's measured witness has
 * a different multiset with an identical total — so only the ordered check has
 * teeth, and it is the only detector that survives Step 19 (PRD §10). */
CQ_TEST(the_stream_is_a_palindrome_around_the_copyout)
{
    static const int WS[] = { 1, 2, 3, 5, 8 };
    cq_mock fwd, unc;

    cq_mock_init(&fwd);
    cq_mock_init(&unc);

    for (int i = 0; i < N_ROWS; i++)
        for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
            int W = WS[j];
            cq_ref_w all = cq_ref_w_ones(W);
            size_t head = (size_t)n_compute_of(ROWS[i].prim, W);
            size_t mid  = ROWS[i].invert ? 2u : 1u;

            /* Distinct values, so the raw flag is not the same on both halves
             * of the ladder and a predicate that ignored its operands would
             * still have to produce the right bit. */
            run_masked(&ROWS[i], W, cq_ref_w_make(0x5Au, 0u, W), all,
                       cq_ref_w_make(0xA3u, 0u, W), all, &fwd, &unc);

            CHECK(cq_mock_is_palindrome(&fwd, head, mid));
            CHECK(cq_mock_is_palindrome(&unc, head, mid));
        }

    cq_mock_dispose(&fwd);
    cq_mock_dispose(&unc);
}

/* ---- The scratch is taken, and it is given back. ------------------------ */

/* K09.md §4. `owned` is ONE — the whole result is a flag — and `peak` is the
 * scratch region plus that one bit. They differ, and for a sandwich kernel
 * they must: L2 looks after the call and cannot see scratch that was taken and
 * tidily released, so the high-water mark is the only instrument for it. */
CQ_TEST(dst_owns_one_qubit_and_the_scratch_comes_back)
{
    static const int WS[] = { 1, 4, 8, 16 };

    for (int i = 0; i < N_ROWS; i++)
        for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
            uint32_t peak = 0;
            uint32_t owned = cq_kd_peak(&ROWS[i].spec, WS[j], &peak);

            CHECK_EQ(owned, 1u);
            CHECK_EQ(peak, scratch_of(ROWS[i].prim, WS[j]) + 1u);
        }
}

#include "test_kernel_cmp_derivation.inc"

CQ_TEST_MAIN_ARGV(
    CQ_CASE(k9_eq_family_sweep),
    CQ_CASE(k9_unsigned_family_sweep),
    CQ_CASE(k9_signed_family_sweep),
    CQ_CASE(controlled),
    CQ_CASE(l4_goldens),
    CQ_CASE(the_w8_column_matches_k09s_evaluated_table),
    CQ_CASE(the_stream_is_a_palindrome_around_the_copyout),
    CQ_CASE(dst_owns_one_qubit_and_the_scratch_comes_back),
    CQ_CASE(each_derived_predicate_is_its_primitives_stream),
    CQ_CASE(a_classical_zero_operand_bit_folds_by_k09s_own_formula),
    CQ_CASE(r9_all_classical_operands_never_enter_the_sandwich),
    CQ_CASE(the_classical_fold_writes_a_real_gate_into_a_quantum_dst),
    CQ_CASE(the_signed_oracle_agrees_with_plain_c_on_sign_extended_values)
)
