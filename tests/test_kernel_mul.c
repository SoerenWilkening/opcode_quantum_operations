/* tests/test_kernel_mul.c — M18, Step 16. K11 `mul`, shift-add over K8.
 *
 * THE FIRST COMPOSITE KERNEL WHOSE INNER CONSTRUCTION IS ANOTHER MODULE'S, and
 * the assertions here are shaped by that. M12's barrel already called
 * `cq_mux_step`, but a mux stage is four gates and its own suite pins them; K8
 * is `6W − 5` gates with a carry chain, a caller-owned ancilla and a
 * precondition, and K11 runs W of them interleaved with its own partial
 * products in ONE flat step index space. So three things are asserted here that
 * no earlier kernel suite could assert at all:
 *
 *   - THE INTERLEAVING, gate for gate, against a BRUTE-FORCE LINEAR SCAN. mul.c
 *     locates the outer iteration by binary search over a closed form; the test
 *     locates it by walking the blocks. Two derivations of one schedule, and
 *     they are compared per step, per op, per control — see
 *     the_flat_step_schedule_matches_a_brute_force_scan.
 *   - THE COMPOSITION, numerically. K11's compute half must be exactly
 *     `W(W+1)/2` partial-product Toffolis plus W copies of whatever K8 MEASURES
 *     at this width — not plus `W(6W−5)` written as a literal. That binds M18
 *     to M15 rather than to a number both were derived from.
 *   - THE ACCUMULATE IS NOT ONE STEP (bd rhp). K11.md §2b writes it on one line
 *     while budgeting 6W−5 slots for it; made one step, the reverse half would
 *     re-accumulate and leave `accum` and every `pp[j]` dirty AFTER `dst` was
 *     copied out — L1 green, L2/L3 and the palindrome the only detectors. The
 *     palindrome case pins the compute half's exact length for this reason.
 *
 * AND ONE THING IT CANNOT DO. K11's L4 golden is SELF-PINNED: "shift-add over
 * Cuccaro" exists in no Bennett source, is reached by no Bennett dispatch path
 * and is exercised by no Bennett test (K11.md §5 delta 4, §6). There is no
 * `x + 1`-style upstream reconciliation to run here the way test_kernel_add.c
 * does — K11.md Appendix A reconciles the RIPPLE variant at residual zero, and
 * that validates the skeleton and the 2×compute+W wrap arithmetic, not our
 * numbers. What replaces it is the composition check above: each component is
 * verified on its own (the skeleton by Appendix A, the accumulator by
 * test_op6a_cuccaro_gate_count.jl and tests/goldens/addacc.counts) and this
 * suite asserts the product of the two. The accumulator half of that claim is
 * `tests/goldens/addacc.counts`'s business and NOT read here: `addacc_tuple`
 * below MEASURES K8 live at the same width, which is a stronger binding — a
 * change to addacc.c moves both this suite and M15's own golden, and neither
 * can be re-pinned without the other going red.
 *
 * MEASURED, AND IT IS THE PRIME DIRECTIVE STATED AS A NUMBER. A 22-mutant
 * battery over mul.c at Step 16 killed 21 and left all 3 deliberately-equivalent
 * controls alive (the 22nd could not be made to compile in the form first
 * written; rewritten, it is killed by three cases). EXACTLY ONE of the twenty-two
 * left the whole L1/L2/L3/L5 sweep GREEN: shortening each accumulate to skip
 * pp[j]'s provably-zero low j lanes. Right value at every mask and every width,
 * scratch clean, palindrome perfect, I6 intact, cq_addacc_check satisfied — and
 * a SMALLER golden, so it presents as an optimisation. It is caught only by the
 * structural cases in this file, and of those only two are durable: `l4_goldens`
 * is SELF-PINNED and `CQOPS_UPDATE_GOLDENS=1` would quietly bless the reduced
 * counts, whereas the composition check and the schedule scan read no golden at
 * all.
 */

#include "kernels/mul.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/addacc.h"
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
#include <time.h>

/* TWO-WORD, because `mul` ships at i128 (opcode_table.yaml:186) where the
 * one-word reference aborts. Rule 7's canonical shape all through, so no `call`
 * adapter and no `shape` are needed — K11 is the first kernel since K6/K7 that
 * departs from the contract in no way at all. */
static cq_ref_w refn_mul(const cq_ref_w *s, const cq_kd_shape *sh)
{ return cq_ref_w_mul(s[0], s[1], sh->w_dst); }

static const cq_kd_spec MUL = { "mul", cq_kernel_mul, NULL, NULL, NULL, refn_mul };

#include "test_kernel_mul_sweep.inc"

/* ---- The closed forms, in one place. ------------------------------------ */

/* K11.md §3, re-derived under I6(b) and pinned at the ALL-QUANTUM operand mask:
 *
 *      compute half   0 X   (4W² − 2W) CX   (5W² − 5W)/2 CCX   = (13W² − 9W)/2
 *      sandwiched     0 X   (8W² − 3W) CX   (5W² − 5W)   CCX   = 13W² − 8W
 *
 * ALL-QUANTUM IS THE MASK, for Rule 14's reason: because we never demote (D6)
 * an operand mask can only drift TOWARDS Q between a forward call and its
 * uncompute, so all-quantum is the fixed point of that drift and the one mask
 * at which the two passes emit the same number. Under I6(b) the whole SCRATCH
 * side is a function of W alone; the one residual mask dependence is on the
 * source side, in phase P's `CCX(a[k], b[j] → pp[j][d])`, whose two controls
 * are the kernel's operands (K11.md §3's last table).
 *
 * W = 1 IS NOT THIS FORMULA and the trap is that the TOTAL agrees. The closed
 * form gives 5 = (0 X, 5 CX, 0 CCX); the uniform Cuccaro path would emit 5 as
 * (0, 3, 2); the kernel is K2 with one Toffoli, (0, 0, 1). Pinning the closed
 * form there would pin a wrong golden that happens to sum right — exactly the
 * K08.md D1 failure mode one level up. */
static void closed_form(int W, uint64_t *x, uint64_t *cx, uint64_t *ccx)
{
    uint64_t w = (uint64_t)W;

    *x = 0u;
    if (W == 1) { *cx = 0u; *ccx = 1u; return; }

    *cx  = 8u * w * w - 3u * w;
    *ccx = 5u * w * w - 5u * w;
}

/* ---- L4: the goldens. --------------------------------------------------- */

static void check_counts(cq_gold *g, int W)
{
    cq_counter fwd, unc;
    uint64_t x, cx, ccx;

    closed_form(W, &x, &cx, &ccx);
    cq_kd_measure(&MUL, W, &fwd, &unc);

    CHECK_GATES(fwd.x, fwd.cx, fwd.ccx, x, cx, ccx);
    CHECK_GATES(unc.x, unc.cx, unc.ccx, x, cx, ccx);
    CHECK_EQ(fwd.ry + fwd.rz + fwd.mz, 0);
    CHECK_EQ(unc.ry + unc.rz + unc.mz, 0);

    cq_gold_check(g, "mul", "forward", W, fwd.x, fwd.cx, fwd.ccx);
    cq_gold_check(g, "mul", "unc",     W, unc.x, unc.cx, unc.ccx);
}

/* The shipped ladder (i1, i8, i16, i32, i64, i128 — opcode_table.yaml:186) plus
 * the exhaustive L1 widths, so every width the sweep runs is also pinned. */
CQ_TEST(l4_goldens)
{
    static const int WS[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64, 128 };
    cq_gold g;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/mul.counts",
                      "M18 kernels/mul.c — K11 mul (shift-add over K8 Cuccaro, "
                      "sandwiched, all-quantum operands). SELF-PINNED: this "
                      "composition exists nowhere in Bennett", CQOPS_BENNETT_COMMIT))
        return;

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) check_counts(&g, WS[i]);

    CHECK(cq_gold_close(&g));
}

/* ---- The composition, against M15's own measured cost. ------------------ */

/* K8's tuple at this width, MEASURED rather than quoted. Three all-quantum
 * registers — the accumulator, the addend and the one-bit ancilla — which is
 * the only mask cq_addacc_check accepts (K8 refuses a classical operand rather
 * than folding it; that refusal IS K8's L5, tests/test_kernel_addacc_death.c). */
static void addacc_tuple(int W, uint64_t *x, uint64_t *cx, uint64_t *ccx)
{
    cq_ctx ctx;
    cq_counter cnt;
    cq_sink sink;
    cq_addacc_block k;
    cq_ref_w ones = cq_ref_w_ones(W);

    cq_count_reset(&cnt);
    sink = cq_sink_counter(&cnt);
    cq_ctx_init(&ctx, &sink);

    int32_t ha = cq_bk_reg_w(&ctx, (uint32_t)W, ones, ones);
    int32_t hb = cq_bk_reg_w(&ctx, (uint32_t)W, ones, ones);
    int32_t hx = cq_bk_reg_w(&ctx, 1u, cq_ref_w_zero(), cq_ref_w_ones(1));

    k.acc = cq_reg_bits(&ctx.regs, ha);
    k.b   = cq_reg_bits(&ctx.regs, hb);
    k.x   = cq_reg_bits(&ctx.regs, hx);
    k.W   = W;

    cq_count_reset(&cnt);                    /* drop the materialising Xs */
    cq_kernel_addacc(&ctx, &k);

    *x = cnt.x; *cx = cnt.cx; *ccx = cnt.ccx;
    cq_ctx_dispose(&ctx);
}

/* THE CHECK THAT REPLACES AN UPSTREAM RECONCILIATION. K11's compute half is the
 * skeleton's `W(W+1)/2` partial-product Toffolis plus W accumulates, and the
 * accumulate's cost is asked of M15 rather than written down as `6W−5`. So a
 * change to addacc.c that moved its tuple would fail HERE as well as in its own
 * suite, and a K11 that quietly stopped calling K8 W times — or called it W−1
 * times, or made the whole accumulate one step — cannot satisfy this.
 *
 * The compute half is recovered from the sandwiched measurement by the driver's
 * own structure, `sandwiched = 2 × compute + W copy-out CX`, which the
 * palindrome case below pins independently. */
CQ_TEST(the_compute_half_is_the_skeleton_plus_w_measured_k8_accumulates)
{
    static const int WS[] = { 2, 3, 4, 5, 8, 16, 32, 64 };

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        uint64_t w = (uint64_t)W;
        uint64_t ax, acx, accx, mx, mcx, mccx;
        cq_counter fwd, unc;

        addacc_tuple(W, &ax, &acx, &accx);
        cq_kd_measure(&MUL, W, &fwd, &unc);

        /* sandwiched = 2 x compute + W copy-out CX */
        mx   = fwd.x / 2u;
        mcx  = (fwd.cx - w) / 2u;
        mccx = fwd.ccx / 2u;

        CHECK_EQ(fwd.x % 2u, 0);
        CHECK_EQ((fwd.cx - w) % 2u, 0);
        CHECK_EQ(fwd.ccx % 2u, 0);

        /* The skeleton contributes Toffolis only; the accumulator contributes
         * W copies of everything K8 emits. */
        CHECK_EQ(mccx, w * (w + 1u) / 2u + w * accx);
        CHECK_EQ(mcx,  w * acx);
        CHECK_EQ(mx,   w * ax);

        /* And the accumulate really is 6W-5 gates, which is what makes the step
         * budget below add up. Asserted against M15's own step count, not a
         * literal, for the same reason. */
        CHECK_EQ(ax + acx + accx, (uint64_t)cq_addacc_steps(W));
    }
}

/* THE TRUNCATION IS STRUCTURAL, NOT A SLICE (K11.md §2). multiplier.jl:26 drops
 * every partial-product Toffoli of weight >= W before it is emitted, so the
 * skeleton costs W(W+1)/2 and not W². A kernel that emitted all W² would need a
 * 2W-bit accumulator and would strand the high half dirty — Bennett's own
 * `:qcla_tree` comment at arith.jl:222-224 calls that "STRANDS the high W as
 * dirty ancillae" (upstream's sense of the word, not PRD §15 D15's), which
 * here is what Rule 6 refuses — under D15 §3 those qubits strand rather than
 * return. The difference is visible
 * in the Toffoli column alone, and this is the case that looks at it. */
CQ_TEST(the_partial_products_are_truncated_and_the_high_half_never_exists)
{
    static const int WS[] = { 2, 3, 4, 8, 16 };

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        uint64_t w = (uint64_t)W;
        uint64_t ax, acx, accx;
        cq_counter fwd, unc;

        addacc_tuple(W, &ax, &acx, &accx);
        cq_kd_measure(&MUL, W, &fwd, &unc);

        CHECK_EQ(fwd.ccx / 2u - w * accx, w * (w + 1u) / 2u);
        CHECK(fwd.ccx / 2u - w * accx < w * w);      /* strictly, for W >= 2 */
    }
}

#include "test_kernel_mul_schedule.inc"

/* ---- The palindrome, and the compute half's exact length. --------------- */

/* Runs one mul at an explicit mask with the stream recorded, checks the value
 * on the way through, and hands the mock back. A count case that never looked
 * at the answer would happily pin the gate profile of a wrong circuit. */
static void run_masked(int W, cq_ref_w va, cq_ref_w qa, cq_ref_w vb,
                       cq_ref_w qb, cq_mock *m)
{
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    cq_ref_w want, got;

    va = cq_ref_w_make(va.lo, va.hi, W);
    vb = cq_ref_w_make(vb.lo, vb.hi, W);
    want = cq_ref_w_mul(va, vb, W);

    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);

    int32_t ha = cq_bk_reg_w(&ctx, (uint32_t)W, va, qa);
    int32_t hb = cq_bk_reg_w(&ctx, (uint32_t)W, vb, qb);
    int32_t hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W);

    cq_mock_reset(m);                        /* drop the materialising Xs */
    cq_kernel_mul(&ctx, cq_reg_bits(&ctx.regs, hd), cq_reg_cbits(&ctx.regs, ha),
                  cq_reg_cbits(&ctx.regs, hb), W);

    got = cq_pc_value_w(&ctx, hd);
    if (!cq_ref_w_eq(got, want))
        cq_h_fail(__FILE__, __LINE__,
                  "mul W=%d: dst = 0x%llx%016llx, want 0x%llx%016llx", W,
                  (unsigned long long)got.hi,  (unsigned long long)got.lo,
                  (unsigned long long)want.hi, (unsigned long long)want.lo);

    cq_ctx_dispose(&ctx);
}

/* THE COMPUTE HALF'S GATE COUNT AT AN ARBITRARY MASK, which is NOT its step
 * count — and the difference is the whole of K11.md §3's "one residual mask
 * dependence". Under I6(b) every scratch bit is CQ_BIT_Q from step 0, so all
 * `W(6W−5)` accumulate steps emit unconditionally; phase P's controls are the
 * kernel's OPERANDS, whose kinds I6(b) does not touch, so the fold table still
 * dispatches on them:
 *
 *      either control classically ZERO   nothing
 *      both classically ONE              1 X       (the target is a qubit)
 *      one ONE, one Q                    1 CX
 *      both Q                            1 CCX
 *
 * so exactly one gate per phase-P step unless a control is a classical ZERO,
 * and never more. The step COUNT is a function of W alone; the gate count is
 * not, and a suite that fed cq_mul_steps to cq_mock_is_palindrome at a mixed
 * mask would be comparing a head of the wrong length and failing on a correct
 * kernel — measured here first, before the expected head was computed properly.
 *
 * NOTE WHAT THIS DELIBERATELY DOES NOT DO: derive the head from the recorded
 * total as `(n − W)/2`. That would assume the very structure the palindrome is
 * supposed to prove, and would still pass on a compute half that emitted the
 * wrong number of gates in a mirror-symmetric way. */
static size_t compute_half_gates(int W, cq_ref_w va, cq_ref_w qa,
                                 cq_ref_w vb, cq_ref_w qb)
{
    size_t n = (size_t)W * (size_t)(6 * W - 5);

    for (int j = 0; j < W; j++) {
        if (!cq_ref_w_bit(qb, j) && !cq_ref_w_bit(vb, j)) continue;

        for (int k = 0; k + j < W; k++)
            if (cq_ref_w_bit(qa, k) || cq_ref_w_bit(va, k)) n++;
    }
    return n;
}

/* [compute] [copy-out] [compute reversed], with the head length computed from
 * the mask rather than read off the stream, and pinned to the LITERAL closed
 * form at the all-quantum mask where the two must coincide.
 *
 * THIS IS THE CASE bd rhp ASKS FOR. Transcribe K11.md §2b's one-line accumulate
 * as one step and the head becomes W(W+1)/2 + W instead of W(W+1)/2 + W(6W−5),
 * so the length check fails; and even at the right length, a reverse half that
 * re-accumulated rather than undoing would not mirror, which no gate COUNT can
 * see (Rule 10 L4 note 2 — a reversed forward list can have a different
 * multiset with an identical total).
 *
 * AT MIXED MASKS TOO, and that is risk R8's whole point: the divergence K11.md
 * §2b traces gate by gate is invisible at all-quantum and at all-classical, and
 * lives on masks where a scratch bit would have changed kind mid-half. Under
 * I6(b) it cannot, and this is what says so out loud. */
CQ_TEST(the_stream_is_a_palindrome_around_the_copyout)
{
    static const int WS[] = { 2, 3, 4, 5, 8 };
    cq_mock m;

    cq_mock_init(&m);

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        size_t steps = (size_t)(13 * W * W - 9 * W) / 2u;
        cq_ref_w all = cq_ref_w_ones(W);
        cq_ref_w lsb = cq_ref_w_setbit(0);
        cq_ref_w none = cq_ref_w_zero();
        size_t head;

        CHECK_EQ((size_t)cq_mul_steps(W), steps);

        /* All-quantum: no fold can fire, so gates == steps. This is the mask
         * L4 pins, and the one place the two counts must agree. */
        run_masked(W, all, all, all, all, &m);
        head = compute_half_gates(W, all, all, all, all);
        CHECK_EQ(head, steps);
        CHECK(cq_mock_is_palindrome(&m, head, (size_t)W));
        CHECK_EQ(cq_mock_count(&m), 2u * head + (size_t)W);

        /* Two narrow quantum values in a wide register — the TYPICAL CQ_lang
         * shape, and R8's named witness class. Every phase-P step but (0,0)
         * folds away, so the head is W(6W−5) + 1. */
        run_masked(W, lsb, lsb, lsb, lsb, &m);
        head = compute_half_gates(W, lsb, lsb, lsb, lsb);
        CHECK_EQ(head, (size_t)W * (size_t)(6 * W - 5) + 1u);
        CHECK(cq_mock_is_palindrome(&m, head, (size_t)W));
        CHECK_EQ(cq_mock_count(&m), 2u * head + (size_t)W);

        /* a all Q, b all ZERO: the asymmetric pair risk R8 mandates by name,
         * which no symmetric mask set contains. Phase P vanishes entirely and
         * the accumulates still run in full — W(6W−5) gates to compute x·0,
         * which is I6(b)'s price stated as a number. */
        run_masked(W, all, all, none, none, &m);
        head = compute_half_gates(W, all, all, none, none);
        CHECK_EQ(head, (size_t)W * (size_t)(6 * W - 5));
        CHECK(cq_mock_is_palindrome(&m, head, (size_t)W));
        CHECK_EQ(cq_mock_count(&m), 2u * head + (size_t)W);

        /* A CLASSICAL ONE REMOVES NO GATE — only a classical ZERO does, which
         * is the K09.md §3.3.1 rule that had to be corrected three times. `b`
         * all-ones and all-classical keeps every phase-P step, rewriting each
         * CCX as a CX rather than folding it away, so this head is the
         * all-quantum head exactly. */
        run_masked(W, all, all, all, none, &m);
        head = compute_half_gates(W, all, all, all, none);
        CHECK_EQ(head, steps);
        CHECK(cq_mock_is_palindrome(&m, head, (size_t)W));
        CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CCX),
                 2u * ((size_t)W * (size_t)(2 * W - 3)));   /* K8's only */
    }

    cq_mock_dispose(&m);
}

/* ---- W = 1: the delegation, and the trap. ------------------------------- */

/* `mul` at i1 IS in the opcode table (opcode_table.yaml:186), and it is the one
 * width where the pinned construction is out of domain: `lower_add_cuccaro!`
 * delegates to the out-of-place `lower_add!` at W <= 1 (adder.jl:66) and stops
 * being an accumulator, and upstream's own test says the formulas do not apply
 * (test_op6a_cuccaro_gate_count.jl:45-56).
 *
 * THE TRAP IS THAT THE TOTAL AGREES THREE WAYS AND THE SPLIT AGREES NONE. This
 * case states all three tuples so a future reader cannot re-derive the wrong
 * one and find it plausible. It also pins ZERO scratch: the delegation is a
 * clean kernel, so `mul` at i1 allocates nothing at all, where the uniform path
 * would have taken 3 qubits. */
CQ_TEST(w1_is_k2_with_one_toffoli_and_not_the_closed_form)
{
    cq_counter fwd, unc;
    uint32_t peak = 0;

    cq_kd_measure(&MUL, 1, &fwd, &unc);

    CHECK_GATES(fwd.x, fwd.cx, fwd.ccx, 0, 0, 1);       /* K2:            1 */
    CHECK_GATES(unc.x, unc.cx, unc.ccx, 0, 0, 1);
    CHECK(!(fwd.cx == 5u && fwd.ccx == 0u));            /* closed form:   5 */
    CHECK(!(fwd.cx == 3u && fwd.ccx == 2u));            /* uniform path:  5 */

    /* No sandwich, so no compute half and no scratch. */
    CHECK_EQ(cq_mul_steps(1), 0);
    CHECK_EQ(cq_kd_peak(&MUL, 1, &peak), 1u);
    CHECK_EQ(peak, 1u);
}

/* ---- The scratch is taken, and it is given back. ------------------------ */

/* K11.md §4: W² + 2W scratch — `accum` W, `pp[j]` W², `x[j]` W — plus W for
 * dst. A function of W ALONE, because pre-materialisation does not consult the
 * operand kinds, which is exactly what makes it assertable as a closed form.
 *
 * `owned` and `peak` MUST differ here, and that is the point: for K1-K5 they
 * are equal and that IS the "zero ancillae" claim. cq_kd_peak measures the high
 * water mark DURING the call, which is the only instrument that can see scratch
 * that was taken and tidily given back — L2 looks after the call and cannot. */
CQ_TEST(the_sandwich_takes_its_scratch_and_gives_it_back)
{
    static const int WS[] = { 2, 3, 4, 8, 16, 32 };

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        uint32_t peak = 0;

        CHECK_EQ(cq_kd_peak(&MUL, W, &peak), (uint32_t)W);
        CHECK_EQ(peak, (uint32_t)(W * W + 3 * W));
    }
}

/* ---- R9: the all-classical short-circuit. ------------------------------- */

/* NOT AN OPTIMISATION, AND THIS KERNEL HAS THE MOST TO LOSE. Pre-materialisation
 * is unconditional once cq_sandwich is entered, so a fully classical multiply
 * without the fold would draw W² + 2W qubits — 4224 at i64 — for an operation
 * with no quantum input at all (plan §0.2 consequence 2, K11.md §4 point 3).
 *
 * `minted` is the instrument, not `live`: a sandwich that took scratch and gave
 * it back leaves `live` exactly where it found it, and only a monotone counter
 * records that it was ever entered. */
static void classical_case(uint64_t va, uint64_t vb, uint64_t want)
{
    cq_ctx ctx;
    cq_counter cnt;
    cq_sink sink;

    cq_count_reset(&cnt);
    sink = cq_sink_counter(&cnt);
    cq_ctx_init(&ctx, &sink);

    int32_t ha = cq_bk_reg_w(&ctx, 8u, cq_ref_w_make(va, 0u, 8), cq_ref_w_zero());
    int32_t hb = cq_bk_reg_w(&ctx, 8u, cq_ref_w_make(vb, 0u, 8), cq_ref_w_zero());
    int32_t hd = cq_reg_alloc_zero(&ctx.regs, 8u);

    cq_pc_snap before = cq_pc_take(&ctx);
    cq_count_reset(&cnt);

    cq_kernel_mul(&ctx, cq_reg_bits(&ctx.regs, hd), cq_reg_cbits(&ctx.regs, ha),
                  cq_reg_cbits(&ctx.regs, hb), 8);

    cq_pc_snap after = cq_pc_take(&ctx);

    CHECK_EQ(cq_count_total(&cnt), 0);
    CHECK_EQ(after.minted, before.minted);
    CHECK_EQ(cq_pc_value(&ctx, hd), want);

    cq_ctx_dispose(&ctx);
}

CQ_TEST(r9_all_classical_operands_never_enter_the_sandwich)
{
    classical_case(0u, 0u, 0u);
    classical_case(1u, 1u, 1u);
    classical_case(12u, 21u, 252u);
    classical_case(200u, 55u, 248u);     /* 11000 mod 256 — truncation bites  */
    classical_case(255u, 255u, 1u);      /* (-1)*(-1) = 1                      */
    classical_case(16u, 16u, 0u);        /* the whole product is the high half */
    classical_case(128u, 2u, 0u);        /* one carry out of the top lane      */
}

/* THE OTHER SIDE OF THE SAME FOLD, AND NO SWEEP CASE REACHES IT. The driver
 * always mints `dst` all-BIT_ZERO, so every all-classical case above lands on
 * cq_bit_flip_const and emits nothing. With `dst` already on qubits the same
 * path must emit a REAL X per set bit of the product — which is what makes the
 * short-circuit a correct implementation of `dst ^= f(a,b)` rather than merely
 * a cheap one. Delete the emit and only this case goes red. */
CQ_TEST(the_classical_fold_writes_real_gates_into_a_quantum_dst)
{
    cq_ctx ctx;
    cq_counter cnt;
    cq_sink sink;

    cq_count_reset(&cnt);
    sink = cq_sink_counter(&cnt);
    cq_ctx_init(&ctx, &sink);

    int32_t ha = cq_bk_reg_w(&ctx, 8u, cq_ref_w_make(12u, 0u, 8), cq_ref_w_zero());
    int32_t hb = cq_bk_reg_w(&ctx, 8u, cq_ref_w_make(21u, 0u, 8), cq_ref_w_zero());
    /* |0> on eight qubits — a rail a previous kernel could well have left. */
    int32_t hd = cq_bk_reg_w(&ctx, 8u, cq_ref_w_zero(), cq_ref_w_ones(8));

    cq_count_reset(&cnt);
    cq_kernel_mul(&ctx, cq_reg_bits(&ctx.regs, hd), cq_reg_cbits(&ctx.regs, ha),
                  cq_reg_cbits(&ctx.regs, hb), 8);

    CHECK_EQ(cq_pc_value(&ctx, hd), 252u);          /* 12 * 21 */
    CHECK_GATES(cnt.x, cnt.cx, cnt.ccx, 6, 0, 0);   /* popcount(0xFC) = 6 */

    cq_ctx_dispose(&ctx);
}

/* THE REFERENCE MODEL'S OWN CROSS-CHECK — no circuit, no qubit, no kernel.
 * Split out on that seam when this file reached the Rule 12 limit, which is the
 * right cut: everything above tests mul.c, and l1s_oracle tests refmodel.c's
 * cq_ref_w_mul against a fourth derivation. It is also where i128's real value
 * coverage lives, since the sweep can afford only 14 cases there. */
#include "test_kernel_mul_refmodel.inc"

CQ_TEST_MAIN_ARGV(
    CQ_CASE(k11_mul_sweep),
    CQ_CASE(controlled),
    CQ_CASE(l4_goldens),
    CQ_CASE(the_compute_half_is_the_skeleton_plus_w_measured_k8_accumulates),
    CQ_CASE(the_partial_products_are_truncated_and_the_high_half_never_exists),
    CQ_CASE(the_flat_step_schedule_matches_a_brute_force_scan),
    CQ_CASE(the_stream_is_a_palindrome_around_the_copyout),
    CQ_CASE(w1_is_k2_with_one_toffoli_and_not_the_closed_form),
    CQ_CASE(the_sandwich_takes_its_scratch_and_gives_it_back),
    CQ_CASE(r9_all_classical_operands_never_enter_the_sandwich),
    CQ_CASE(the_classical_fold_writes_real_gates_into_a_quantum_dst),
    CQ_CASE(l1s_oracle_agrees_with_an_independent_shift_add)
)
