/* tests/test_kernel_bitwise.c — M10, Step 10. K1 xor, K2 and, K3 or.
 *
 * The first kernels, and therefore the first exercise of the shared Phase-B
 * driver itself. K1-K3 are NATURALLY CLEAN — no cq_sandwich, no scratch, no
 * ancilla (docs/constructions/K01.md, K02.md, K03.md; PRD §5) — which is
 * exactly why they go first: the driver's L1-L4 machinery gets validated here,
 * on kernels with nothing to leak, before Step 12 hands it a sandwich kernel
 * that has.
 *
 * THE PRIME DIRECTIVE, restated for this file: L1 green proves the
 * permutation. It says nothing about a leaked ancilla or a materialised
 * source. Every case therefore runs L1 AND L2 AND L3 together, in
 * cq_kd_case — and L4 separately, because a matching gate count is the one
 * assertion that can be green while the circuit is wrong.
 *
 * WHAT IS PINNED WHERE:
 *   L1/L2/L3/L5   cq_kd_sweep, per kernel — every (a,b) x every mask pair
 *   L4            tests/goldens/bitwise.counts, forward and unc SEPARATELY
 *   L5, by hand   the three per-lane fold walkthroughs the K-docs derive
 */

#include "kernels/bitwise.h"

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

/* The trailing shape/call/refn members are deliberately absent: zero means
 * "the ordinary shape" (arity 2, every width W, no constraint, called and
 * referenced directly). Spelled with explicit NULLs rather than left off
 * because the build is -Werror -Wmissing-field-initializers. */
static const cq_kd_spec K1 = { "xor", cq_kernel_xor, cq_ref_xor, NULL, NULL, NULL };
static const cq_kd_spec K2 = { "and", cq_kernel_and, cq_ref_and, NULL, NULL, NULL };
static const cq_kd_spec K3 = { "or",  cq_kernel_or,  cq_ref_or,  NULL, NULL, NULL };

/* ---- L1 + L2 + L3 + L5, the whole sweep, one case at a time. ------------ */

CQ_TEST(k1_xor_sweep) { cq_kd_sweep(&K1); }
CQ_TEST(k2_and_sweep) { cq_kd_sweep(&K2); }
CQ_TEST(k3_or_sweep)  { cq_kd_sweep(&K3); }

/* ---- L4: the goldens, cross-checked against the Bennett formula. -------- */

/* The closed forms, read off the three Julia bodies (K01.md §3, K02.md §3,
 * K03.md §3) and NOT off our own emitter. That is the whole point of having
 * them next to the golden: the file catches drift, the formula catches a
 * golden that was regenerated from a broken kernel and pinned the breakage.
 * Both have to agree, or the run is red. */
static void formula(const char *kernel, int W, uint64_t *x, uint64_t *cx,
                    uint64_t *ccx)
{
    *x = 0u;   /* no NOTGate appears in any of the three functions */

    if (cq_h_streq(kernel, "xor")) { *cx = 2u * (uint64_t)W; *ccx = 0u; }
    else if (cq_h_streq(kernel, "and")) { *cx = 0u; *ccx = (uint64_t)W; }
    else { *cx = 2u * (uint64_t)W; *ccx = (uint64_t)W; }   /* or */
}

static const int GOLDEN_W[] = { 1, 2, 4, 8, 16, 32, 64 };

static void check_counts(cq_gold *g, const cq_kd_spec *k, int W)
{
    cq_counter fwd, unc;
    uint64_t x, cx, ccx;

    cq_kd_measure(k, W, &fwd, &unc);

    formula(k->name, W, &x, &cx, &ccx);

    /* The formula, against the forward measurement. */
    CHECK_GATES(fwd.x, fwd.cx, fwd.ccx, x, cx, ccx);

    /* The golden, against both passes. Pinned SEPARATELY and never compared to
     * each other: `unc == forward` is NOT an invariant (Rule 14, PRD §10), and
     * asserting it is risk R6 by name. They happen to agree for K1-K3 because
     * nothing here can materialise a source between the two calls; that is a
     * fact about these kernels, not a rule. */
    cq_gold_check(g, k->name, "forward", W, fwd.x, fwd.cx, fwd.ccx);
    cq_gold_check(g, k->name, "unc",     W, unc.x, unc.cx, unc.ccx);

    /* Neither Bennett circuit contains a rotation or a measurement, so the
     * counter's other three fields must be untouched — cq_count_total ignores
     * them by design, so a stray Ry would otherwise be invisible to L4. */
    CHECK_EQ(fwd.ry + fwd.rz + fwd.mz, 0);
    CHECK_EQ(unc.ry + unc.rz + unc.mz, 0);
}

CQ_TEST(l4_goldens)
{
    cq_gold g;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/bitwise.counts",
                      "M10 kernels/bitwise.c — K1 xor, K2 and, K3 or",
                      CQOPS_BENNETT_COMMIT))
        return;

    for (size_t i = 0; i < sizeof GOLDEN_W / sizeof GOLDEN_W[0]; i++) {
        check_counts(&g, &K1, GOLDEN_W[i]);
        check_counts(&g, &K2, GOLDEN_W[i]);
        check_counts(&g, &K3, GOLDEN_W[i]);
    }

    CHECK(cq_gold_close(&g));
}

/* ---- L5 by hand: the per-lane fold walkthroughs, gate for gate. --------- */

/* cq_kd_sweep already proves the all-classical row costs nothing. These pin
 * the MIXED lanes, where one operand is classical and the other is a qubit —
 * the cases each K-doc works out by hand in its §5 "deltas from upstream", and
 * the cases where a plausible-looking emitter change would silently cost a
 * gate or a qubit. An exact stream, not a count: only an ordered comparison
 * distinguishes them. */
typedef struct {
    cq_ctx  ctx;
    cq_mock mock;
    cq_sink sink;
    int32_t ha, hb, hd;
} lane_fx;

/* `a` is always all-quantum — these cases exist to watch what a CLASSICAL `b`
 * lane does to the fold — and `b`'s mask is a parameter so the same fixture
 * serves the all-quantum measurement below. Returns the qubit indices `a`
 * landed on via `qa`. */
static void lane_open(lane_fx *f, int W, uint64_t va, uint64_t qb, uint64_t vb,
                      uint32_t *qa)
{
    cq_mock_init(&f->mock);
    f->sink = cq_mock_sink(&f->mock);
    cq_ctx_init(&f->ctx, &f->sink);

    f->ha = cq_bk_reg(&f->ctx, (uint32_t)W, va, cq_ref_mask(W));   /* quantum */
    f->hb = cq_bk_reg(&f->ctx, (uint32_t)W, vb, qb);
    f->hd = cq_reg_alloc_zero(&f->ctx.regs, (uint32_t)W);

    for (int i = 0; i < W; i++)
        qa[i] = cq_bit_qindex(cq_reg_cbits(&f->ctx.regs, f->ha)[i]);

    cq_mock_reset(&f->mock);   /* the operand X's are not the kernel's gates */
}

static void lane_run(lane_fx *f, const cq_kd_spec *k, int W)
{
    k->kernel(&f->ctx, cq_reg_bits(&f->ctx.regs, f->hd),
              cq_reg_cbits(&f->ctx.regs, f->ha),
              cq_reg_cbits(&f->ctx.regs, f->hb), W);
}

static void lane_close(lane_fx *f)
{
    cq_ctx_dispose(&f->ctx);
    cq_mock_dispose(&f->mock);
}

/* K03.md §5 delta 2, the row that was WRONG in the first draft of that doc and
 * was corrected by adversarial review on 2026-08-14. `a | 1` on a quantum `a`
 * does NOT collapse to a lone X on dst: the step-1 and step-3 CX(a,dst) pair
 * is mathematically redundant, but v1 forbids gate-level cancellation
 * (PRD §1, "Out of scope for v1": gate-level optimisation — cancellation,
 * commutation, peephole fusion), so all three gates are emitted and the lane
 * still costs a
 * qubit. Pinning the exact stream is what stops someone "optimising" it back
 * into the wrong claim. */
CQ_TEST(l5_or_lane_with_a_constant_one)
{
    lane_fx f;
    uint32_t qa[1];

    lane_open(&f, 1, 1u, 0u, 1u, qa);
    lane_run(&f, &K3, 1);

    uint32_t qd = cq_bit_qindex(cq_reg_cbits(&f.ctx.regs, f.hd)[0]);
    const cq_rec want[] = {
        CQ_REC_CX(qa[0], qd),   /* step 1: materialise dst, then CX from a   */
        CQ_REC_X(qd),           /* step 2: b is ONE, so CX folds to X        */
        CQ_REC_CX(qa[0], qd),   /* step 3: CCX with a ONE control folds to CX */
    };

    if (!cq_mock_matches(&f.mock, want, 3)) {
        cq_h_fail(__FILE__, __LINE__,
                  "L5 or: a|1 must emit CX,X,CX — 2 CX + 1 X and one qubit "
                  "(K03.md §5 delta 2). A lone X would mean the emitter has "
                  "grown the cancellation peephole PRD §1 rules out of v1");
        cq_mock_dump(&f.mock, "or a|1");
    }

    CHECK_EQ(cq_qubits_live(&f.ctx.pool), 2);      /* a's qubit and dst's */
    CHECK_EQ(cq_pc_value(&f.ctx, f.hd), 1);        /* a | 1 == 1, always */
    lane_close(&f);
}

/* The other row of the same table: a ZERO lane DOES collapse, to a single CX
 * from the other operand. This is also the shift-free half of PRD §11's L5
 * example, `int a = 0; a |= b << 3` — exactly 1 qubit and 1 CX per live lane.
 * The shift itself is K4 and arrives at Step 11. */
CQ_TEST(l5_or_lane_with_a_constant_zero)
{
    lane_fx f;
    uint32_t qa[1];

    lane_open(&f, 1, 1u, 0u, 0u, qa);
    lane_run(&f, &K3, 1);

    uint32_t qd = cq_bit_qindex(cq_reg_cbits(&f.ctx.regs, f.hd)[0]);
    const cq_rec want[] = { CQ_REC_CX(qa[0], qd) };

    if (!cq_mock_matches(&f.mock, want, 1)) {
        cq_h_fail(__FILE__, __LINE__, "L5 or: a|0 must emit exactly one CX");
        cq_mock_dump(&f.mock, "or a|0");
    }

    CHECK_EQ(cq_qubits_live(&f.ctx.pool), 2);
    CHECK_EQ(cq_pc_value(&f.ctx, f.hd), 1);
    lane_close(&f);
}

/* Bennett's own test expression, `x & 0x0f` at W=8 (test/test_bitwise.jl:2),
 * and K02.md §5 delta 2's sharp case. Our side: the §3 fold table decides per
 * bit, so the four clear lanes emit NOTHING AND ALLOCATE NOTHING and the four
 * set lanes fold CCX(a, ONE, dst) to a CX — 4 CX, zero Toffoli, 4 qubits, not
 * 8. THREE QUARTERS OF THE REGISTER NEVER BECOMES QUANTUM, which is the whole
 * claim of the per-bit design and is what this pins.
 *
 * DO NOT re-add the upstream comparison K02.md prints here. Measured
 * 2026-08-15: that comparison ("Bennett pays 4 NOT + 8 Toffoli") is a
 * fold_constants=false figure, and Bennett runs `_fold_constants` BY DEFAULT
 * (src/Bennett.jl:146 `fold_constants::Bool = true`, applied at
 * src/lowering/driver.jl:375-377), which drops the clear-bit Toffolis and
 * reduces the set-bit ones to CNOTs. At default options upstream also pays
 * zero Toffoli. The per-bit win over upstream is real but smaller than the
 * K-doc says; our own numbers below are unaffected, and they are what L5 is
 * for. */
CQ_TEST(l5_and_against_a_partial_constant)
{
    lane_fx f;
    uint32_t qa[8];

    lane_open(&f, 8, 0xFFu, 0u, 0x0Fu, qa);
    lane_run(&f, &K2, 8);

    CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_CCX), 0);
    CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_CX), 4);
    CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_X), 0);
    CHECK_EQ(cq_qubits_live(&f.ctx.pool), 8 + 4);   /* a's eight, dst's four */
    CHECK_EQ(cq_reg_owned_qubits(&f.ctx.regs, f.hd), 4);
    CHECK_EQ(cq_pc_value(&f.ctx, f.hd), 0x0F);
    lane_close(&f);
}

/* K01.md §5 delta 2: `x ^ 0x0f` at W=8 costs 4 X on the constant-1 lanes plus
 * 8 CX from a — EVERY lane pays the CX and allocates, because xor's first gate
 * has a quantum control and so cannot fold. Pinned next to the AND above
 * precisely because the two differ: the per-bit fold is not a blanket saving,
 * it depends on which gate meets which constant. (K01.md's upstream
 * comparison on this line is a fold_constants=false figure too — see the AND
 * case above.) */
CQ_TEST(l5_xor_against_a_partial_constant)
{
    lane_fx f;
    uint32_t qa[8];

    lane_open(&f, 8, 0xFFu, 0u, 0x0Fu, qa);
    lane_run(&f, &K1, 8);

    CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_CX), 8);
    CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_X), 4);
    CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_CCX), 0);
    CHECK_EQ(cq_reg_owned_qubits(&f.ctx.regs, f.hd), 8);
    CHECK_EQ(cq_pc_value(&f.ctx, f.hd), 0xF0);
    lane_close(&f);
}

/* ---- The emitted ORDER, which no count can see. ------------------------- */

/* THE MUTANTS THAT MADE THIS NECESSARY. A mutation battery over M10 killed 22
 * of 24 library mutants; both survivors were ORDERING changes — swapping
 * `xor`'s two CX, and swapping `or`'s two CCX controls. Both are semantically
 * equivalent (CX gates sharing a target commute; CCX is symmetric in its
 * controls), so L1, L2, L3 and every gate count stay green, and nothing in the
 * suite could tell. They still matter: bitwise.c says in as many words that
 * Bennett's order is kept "so that L6 trace diffs at Step 24 stay
 * attributable", and an unpinned convention is not a convention. An ORDERED
 * stream comparison is the only instrument that sees this — the same reason
 * PRD §10 gives for `cq_mock_is_palindrome`.
 *
 * W=2, all-quantum, so every gate is really emitted and the expected stream is
 * short enough to read. */
CQ_TEST(the_emitted_gate_order_is_bennetts)
{
    uint32_t qa[2];
    lane_fx f;

    /* xor: CX from a, then CX from b, per lane (arith.jl:287-288). */
    lane_open(&f, 2, 0x3u, 0x3u, 0x3u, qa);
    uint32_t qb0 = cq_bit_qindex(cq_reg_cbits(&f.ctx.regs, f.hb)[0]);
    uint32_t qb1 = cq_bit_qindex(cq_reg_cbits(&f.ctx.regs, f.hb)[1]);
    lane_run(&f, &K1, 2);
    uint32_t qd0 = cq_bit_qindex(cq_reg_cbits(&f.ctx.regs, f.hd)[0]);
    uint32_t qd1 = cq_bit_qindex(cq_reg_cbits(&f.ctx.regs, f.hd)[1]);
    const cq_rec want_xor[] = {
        CQ_REC_CX(qa[0], qd0), CQ_REC_CX(qb0, qd0),
        CQ_REC_CX(qa[1], qd1), CQ_REC_CX(qb1, qd1),
    };
    if (!cq_mock_matches(&f.mock, want_xor, 4)) {
        cq_h_fail(__FILE__, __LINE__, "xor: gate order is not Bennett's "
                  "(arith.jl:287-288 is CX from a, then CX from b)");
        cq_mock_dump(&f.mock, "xor W=2 all-quantum");
    }
    lane_close(&f);

    /* and: one CCX per lane, controls (a, b) in that order (arith.jl:270). */
    lane_open(&f, 2, 0x3u, 0x3u, 0x3u, qa);
    qb0 = cq_bit_qindex(cq_reg_cbits(&f.ctx.regs, f.hb)[0]);
    qb1 = cq_bit_qindex(cq_reg_cbits(&f.ctx.regs, f.hb)[1]);
    lane_run(&f, &K2, 2);
    qd0 = cq_bit_qindex(cq_reg_cbits(&f.ctx.regs, f.hd)[0]);
    qd1 = cq_bit_qindex(cq_reg_cbits(&f.ctx.regs, f.hd)[1]);
    const cq_rec want_and[] = {
        CQ_REC_CCX(qa[0], qb0, qd0),
        CQ_REC_CCX(qa[1], qb1, qd1),
    };
    if (!cq_mock_matches(&f.mock, want_and, 2)) {
        cq_h_fail(__FILE__, __LINE__, "and: gate order or control order is not "
                  "Bennett's (arith.jl:270 is ToffoliGate(a[i], b[i], r[i]))");
        cq_mock_dump(&f.mock, "and W=2 all-quantum");
    }
    lane_close(&f);

    /* or: CX a, CX b, CCX(a,b), per lane (arith.jl:277-279). */
    lane_open(&f, 2, 0x3u, 0x3u, 0x3u, qa);
    qb0 = cq_bit_qindex(cq_reg_cbits(&f.ctx.regs, f.hb)[0]);
    qb1 = cq_bit_qindex(cq_reg_cbits(&f.ctx.regs, f.hb)[1]);
    lane_run(&f, &K3, 2);
    qd0 = cq_bit_qindex(cq_reg_cbits(&f.ctx.regs, f.hd)[0]);
    qd1 = cq_bit_qindex(cq_reg_cbits(&f.ctx.regs, f.hd)[1]);
    const cq_rec want_or[] = {
        CQ_REC_CX(qa[0], qd0), CQ_REC_CX(qb0, qd0), CQ_REC_CCX(qa[0], qb0, qd0),
        CQ_REC_CX(qa[1], qd1), CQ_REC_CX(qb1, qd1), CQ_REC_CCX(qa[1], qb1, qd1),
    };
    if (!cq_mock_matches(&f.mock, want_or, 6)) {
        cq_h_fail(__FILE__, __LINE__, "or: gate order or control order is not "
                  "Bennett's (arith.jl:277-279)");
        cq_mock_dump(&f.mock, "or W=2 all-quantum");
    }
    lane_close(&f);
}

/* ---- The clean-kernel claim itself: ZERO ancillae, at any W. ------------ */

/* PRD §5 lists and/or/xor as "naturally clean already (no sandwich needed)",
 * and each K-doc §4 says "0 ancillae, for every W". THIS IS A PEAK
 * MEASUREMENT, and it has to be: L2 in cq_kd_case looks at the pool AFTER the
 * call, so a kernel that allocated scratch and dutifully released it before
 * returning passes L2 and every gate-count golden — it is clean, it is just
 * not supposed to exist. `peak == minted` (src/qubits.h), so the high-water
 * mark during the call is exactly `minted` after it, and a transient
 * allocation cannot hide.
 *
 * An earlier version of this case asserted `total != sandwiched` — a
 * not-equal-to-one-constant test on a total that check_counts has already
 * pinned exactly, so it could only fail after CHECK_GATES had already failed.
 * It read like an allocation check and never touched the pool. */
CQ_TEST(the_three_kernels_allocate_only_dst)
{
    const cq_kd_spec *ks[3] = { &K1, &K2, &K3 };
    const int W = 8;

    for (int i = 0; i < 3; i++) {
        lane_fx f;
        uint32_t qa[8];

        /* Both operands all-quantum, so no lane can fold and every gate the
         * kernel wants is really emitted. */
        lane_open(&f, W, 0xFFu, cq_ref_mask(W), 0xFFu, qa);

        cq_pc_snap before = cq_pc_take(&f.ctx);   /* operands built, dst empty */
        lane_run(&f, ks[i], W);
        cq_pc_snap after = cq_pc_take(&f.ctx);

        /* dst needs exactly W, and the PEAK says nothing else was ever live —
         * a transient scratch allocation cannot hide behind a tidy release. */
        CHECK_EQ(after.minted - before.minted, (uint32_t)W);
        CHECK_EQ(after.peak   - before.peak,   (uint32_t)W);
        CHECK_EQ(cq_reg_owned_qubits(&f.ctx.regs, f.hd), (uint32_t)W);

        lane_close(&f);
    }
}

CQ_TEST_MAIN_ARGV(
    CQ_CASE(k1_xor_sweep),
    CQ_CASE(k2_and_sweep),
    CQ_CASE(k3_or_sweep),
    CQ_CASE(l4_goldens),
    CQ_CASE(l5_or_lane_with_a_constant_one),
    CQ_CASE(l5_or_lane_with_a_constant_zero),
    CQ_CASE(l5_and_against_a_partial_constant),
    CQ_CASE(l5_xor_against_a_partial_constant),
    CQ_CASE(the_emitted_gate_order_is_bennetts),
    CQ_CASE(the_three_kernels_allocate_only_dst)
)
