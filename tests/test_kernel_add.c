/* tests/test_kernel_add.c — M14, Step 12. K6 add and K7 sub.
 *
 * THE FIRST KERNELS WITH SOMETHING TO LEAK. K1-K5 target `dst` and nothing
 * else; these two write a carry chain into scratch and leave it dirty by
 * construction, so this is where the shared Phase-B driver first meets
 * Bennett-in-the-small. Three things are therefore asserted here that no
 * earlier kernel suite could assert at all:
 *
 *   - THE PALINDROME. cq_mock_is_palindrome over the recorded stream, at the
 *     all-quantum mask AND at risk R8's measured mixed-kind witness
 *     (W=3, a = b = {Q, ZERO, ZERO}). A gate COUNT cannot see an R8
 *     divergence — K12's reversed forward list has a different multiset with
 *     the identical total — so only an ORDERED comparison has teeth here.
 *   - THE PEAK. cq_kd_peak measures the high-water mark DURING the call, which
 *     is the only instrument that can see scratch that was taken and tidily
 *     given back. L2 looks after the call and cannot.
 *   - R9's SHORT-CIRCUIT. Pre-materialisation is unconditional, so without a
 *     kernel-entry check a fully classical add would take 2W qubits it never
 *     needed. That is a REQUIREMENT, not an optimisation (plan §0.2
 *     consequence 2, risk R9), and L5 is what catches its absence.
 *
 * AND THIS IS THE ONE PLACE THE PORT IS VALIDATED AGAINST UPSTREAM rather than
 * against itself (plan §4, Step 12's extra gate). K1-K5, K9, K10 and K12 have
 * no published Bennett counts at all; `x + 1` does, at four widths. Read the
 * §3.8 trap in K06.md before touching those cases: the TOTALS agree at every
 * width and the TUPLES do not, and the agreement is a coincidence of two
 * unrelated W-independent deltas that cancel. Those cases live in
 * test_kernel_add_upstream.inc, split on that same against-upstream /
 * against-ourselves boundary once this file reached the Rule 12 limit.
 */

#include "kernels/add.h"

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

typedef cq_ref_w (*wide_ref)(cq_ref_w, cq_ref_w, int);

/* TWO-WORD, because add and sub ship at i128 (opcode_table.yaml:184-185) and
 * a one-word reference aborts above 64. There is NO one-word sibling: it could
 * only ever be crossed against this pair below 64, where the two are provably
 * the same expression. The real cross-check is the bit-serial ripple at the
 * foot of this file. */
static cq_ref_w refn_add(const cq_ref_w *s, const cq_kd_shape *sh)
{ return cq_ref_w_add(s[0], s[1], sh->w_dst); }
static cq_ref_w refn_sub(const cq_ref_w *s, const cq_kd_shape *sh)
{ return cq_ref_w_sub(s[0], s[1], sh->w_dst); }

static const cq_kd_spec ADD = { "add", cq_kernel_add, NULL, NULL, NULL, refn_add };
static const cq_kd_spec SUB = { "sub", cq_kernel_sub, NULL, NULL, NULL, refn_sub };

/* ---- L1 + L2 + L3 + L5, the whole sweep. -------------------------------- */

/* The standard ladder, then i128. i128 is not decoration: it is a shipped
 * width for both opcodes, it is the only one where the reference's 64-bit seam
 * is exercised, and the standard ladder stops at 64 because that is where a
 * one-word value model stopped. i80 is deliberately absent — the opcode table
 * excludes it from add/sub explicitly (opcode_table.yaml:85). */
static void sweep(const cq_kd_spec *k)
{
    cq_kd_sweep(k);
    cq_kd_sweep_at(k, 128, 0);
}

CQ_TEST(k6_add_sweep) { sweep(&ADD); }
CQ_TEST(k7_sub_sweep) { sweep(&SUB); }

/* Step 20 — the same four levels under PRD §9's four regions, plus §9's
 * gate-tuple transform at every shipped width. The sweep body is this suite's
 * OWN, at its cheap widths only: the promotion is per gate and width-
 * independent, so what the axis adds is its interaction with the §3 fold table,
 * which is exhausted where the value cross product is. Every shipped width is
 * still covered by cq_kd_check_promotion, at two kernel calls apiece. */
static const cq_kd_spec *const ADDS[] = { &ADD, &SUB };

static void add_narrow(void)
{
    for (size_t i = 0; i < sizeof ADDS / sizeof ADDS[0]; i++)
        for (int W = 1; W <= 5; W++) cq_kd_sweep_at(ADDS[i], W, 1);
}

CQ_TEST(controlled)
{
    uint64_t reached = 0u;

    /* THE FIRST SANDWICH USERS UNDER THE AXIS, so this is where the composition
     * is exercised at scale: I6's extent, the region fingerprint,
     * CQ_ZERO_BY_PALINDROME and §9's promotion in one call. i80 is excluded from
     * add and sub (opcode_table.yaml:85), as it is from the sweep. */
    static const int widths[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64, 128 };

    cq_kd_for_each_region("add/sub", add_narrow);

    for (size_t i = 0; i < sizeof ADDS / sizeof ADDS[0]; i++)
        for (size_t w = 0; w < sizeof widths / sizeof widths[0]; w++)
            reached += cq_kd_check_promotion(ADDS[i], widths[w]);
    /* NOT VACUOUS: the identity above is an equality between two measurements,
     * and it holds trivially where the uncontrolled tuple is empty. K4 makes
     * that a real case rather than a hypothetical — its all-ones L4 fixture
     * saturates under D8 at every non-power-of-two width — so the ladder has to
     * say it reached something. */
    CHECK(reached > 0u);
}

/* ---- One run at an EXPLICIT operand mask, with the stream recorded. ------ */

/* Both passes, each into its own mock, so their counts are pinned separately
 * (Rule 14: their equality is not an invariant). The value is checked on the
 * way through — a count case that never looks at the answer would happily pin
 * the gate profile of a wrong circuit. */
static void run_masked(const cq_kd_spec *k, wide_ref ref, int W,
                       cq_ref_w va, cq_ref_w qa, cq_ref_w vb, cq_ref_w qb,
                       cq_mock *fwd, cq_mock *unc)
{
    cq_ctx ctx;
    cq_sink s_fwd = cq_mock_sink(fwd);
    cq_sink s_unc = cq_mock_sink(unc);
    cq_ref_w want, got;

    va = cq_ref_w_make(va.lo, va.hi, W);
    vb = cq_ref_w_make(vb.lo, vb.hi, W);
    want = ref(va, vb, W);

    cq_mock_reset(fwd);
    cq_mock_reset(unc);
    cq_ctx_init(&ctx, &s_fwd);

    int32_t ha = cq_bk_reg_w(&ctx, (uint32_t)W, va, qa);
    int32_t hb = cq_bk_reg_w(&ctx, (uint32_t)W, vb, qb);
    int32_t hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W);
    cq_bit       *dst = cq_reg_bits (&ctx.regs, hd);
    const cq_bit *a   = cq_reg_cbits(&ctx.regs, ha);
    const cq_bit *b   = cq_reg_cbits(&ctx.regs, hb);

    /* Building the operands materialises their set bits, which emits X. Drop
     * that, so what is recorded is the KERNEL. */
    cq_mock_reset(fwd);
    k->kernel(&ctx, dst, a, b, W);

    got = cq_pc_value_w(&ctx, hd);
    if (!cq_ref_w_eq(got, want))
        cq_h_fail(__FILE__, __LINE__,
                  "%s W=%d: dst = 0x%llx%016llx, want 0x%llx%016llx", k->name,
                  W, (unsigned long long)got.hi,  (unsigned long long)got.lo,
                  (unsigned long long)want.hi, (unsigned long long)want.lo);

    ctx.sink = &s_unc;
    k->kernel(&ctx, dst, a, b, W);

    if (!cq_ref_w_is_zero(cq_pc_value_w(&ctx, hd)))
        cq_h_fail(__FILE__, __LINE__, "%s W=%d: dst is not 0 after uncompute",
                  k->name, W);

    cq_ctx_dispose(&ctx);
}

/* THE FULL TUPLE, EVERY FIELD MEASURED. The total is read back independently
 * and compared against the sum, which is what forces ry / rz / mz to zero —
 * Rule 4 admits only X, CX and CCX on this surface. An earlier Step-11 helper
 * passed literal 0 for the NOT and Toffoli columns and compared them against
 * literal 0; nothing here may repeat that. */
static void gates_of(const cq_mock *m, uint64_t *x, uint64_t *cx, uint64_t *ccx)
{
    *x   = (uint64_t)cq_mock_count_op(m, CQ_OP_X);
    *cx  = (uint64_t)cq_mock_count_op(m, CQ_OP_CX);
    *ccx = (uint64_t)cq_mock_count_op(m, CQ_OP_CCX);

    CHECK_EQ(cq_mock_count(m), *x + *cx + *ccx);
}

/* ---- L4: the goldens, at the all-quantum mask. -------------------------- */

/* K06.md §3.4 and K07.md §3.5, both re-derived under I6(b):
 *
 *      add   0     X   7W CX   4W-4 CCX   = 11W - 4
 *      sub   2W+2  X   9W CX   4W-4 CCX   = 15W - 2
 *
 * ALL-QUANTUM IS THE MASK, and the reason is Rule 14's: because we never
 * demote (D6) an operand mask can only drift TOWARDS Q between a forward call
 * and its uncompute, so all-quantum is the fixed point of that drift and the
 * one mask at which the two passes emit the same number. That is what makes a
 * single stable golden per (kernel, W) possible. The two passes are still
 * checked against the closed form SEPARATELY and are never compared to each
 * other — asserting `unc == forward` is exactly the R6 mistake Rule 14
 * forbids, and it happens to hold here only because of the mask. */
static void closed_form(const cq_kd_spec *k, int W,
                        uint64_t *x, uint64_t *cx, uint64_t *ccx)
{
    int is_sub = cq_h_streq(k->name, "sub");

    *x   = is_sub ? (uint64_t)(2 * W + 2) : 0u;
    *cx  = (uint64_t)(is_sub ? 9 * W : 7 * W);
    *ccx = (uint64_t)(4 * W - 4);
}

static void check_counts(cq_gold *g, const cq_kd_spec *k, int W)
{
    cq_counter fwd, unc;
    uint64_t x, cx, ccx;

    closed_form(k, W, &x, &cx, &ccx);
    cq_kd_measure(k, W, &fwd, &unc);

    CHECK_GATES(fwd.x, fwd.cx, fwd.ccx, x, cx, ccx);
    CHECK_GATES(unc.x, unc.cx, unc.ccx, x, cx, ccx);
    CHECK_EQ(fwd.ry + fwd.rz + fwd.mz, 0);
    CHECK_EQ(unc.ry + unc.rz + unc.mz, 0);

    cq_gold_check(g, k->name, "forward", W, fwd.x, fwd.cx, fwd.ccx);
    cq_gold_check(g, k->name, "unc",     W, unc.x, unc.cx, unc.ccx);
}

CQ_TEST(l4_goldens)
{
    static const int WS[] = { 1, 2, 4, 8, 16, 32, 64, 128 };
    cq_gold g;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/add.counts",
                      "M14 kernels/add.c — K6 add, K7 sub (ripple-carry, "
                      "sandwiched, all-quantum operands)", CQOPS_BENNETT_COMMIT))
        return;

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        check_counts(&g, &ADD, WS[i]);
        check_counts(&g, &SUB, WS[i]);
    }

    CHECK(cq_gold_close(&g));
}

/* K07.md §3.6 asks L4 to assert the IDENTITY and not only the bare numbers,
 * so that a change to K6 cannot silently desynchronise K7. `lower_sub!`'s body
 * at adder.jl:162-170 IS `lower_add!`'s at :8-16 with `not_b` for `b`, and the
 * whole delta is phase 1 (W CX + W X for ¬b) plus phase 2's carry-in X —
 * 2W + 1 per compute half, doubled by the sandwich because copyout is W CX in
 * both and does not scale. */
CQ_TEST(k7_is_k6_plus_two_w_plus_one_per_compute_half)
{
    static const int WS[] = { 1, 2, 4, 8, 16, 32, 64, 128 };

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        cq_counter a_f, a_u, s_f, s_u;

        cq_kd_measure(&ADD, W, &a_f, &a_u);
        cq_kd_measure(&SUB, W, &s_f, &s_u);

        CHECK_EQ(cq_count_total(&s_f) - cq_count_total(&a_f), 4 * W + 2);
        CHECK_EQ(cq_count_total(&s_u) - cq_count_total(&a_u), 4 * W + 2);
        CHECK_EQ(s_f.ccx, a_f.ccx);     /* the carry guard `i < W` is shared */
    }
}

#include "test_kernel_add_upstream.inc"

/* ---- The palindrome. --------------------------------------------------- */

/* WHAT THIS IS AND IS NOT LOAD-BEARING FOR, measured rather than asserted. A
 * 13-mutant battery over src/kernels/add.c at Step 12 killed every real mutant
 * (and left both deliberately-equivalent controls alive), and NOT ONE of them
 * was caught by this case alone — every divergence a mutant could produce also
 * moved a value, so L1 got there first. Two things still earn its keep, and
 * neither is "it is the detector today":
 *
 *   (i)  It pins the COMPUTE-HALF LENGTH independently of the total. The x+1
 *        case uses that to pin K06.md §3.8's (1, 17, 7) structurally instead of
 *        by halving the sandwiched tuple, which would be circular.
 *   (ii) It is the only detector that SURVIVES STEP 19. On the rotation-free
 *        surface a non-cancelling compute half is caught by cq_shadow_retire's
 *        determinate-non-zero check in M02 — measured here, in BOTH
 *        configurations, by merging two gates into one step (the D1 violation):
 *        "shadow: retire of a determinate NON-ZERO entry". Once M22 taints a
 *        rail that check goes inert (PRD §10) and an ordered stream comparison
 *        is what is left.
 *
 * Also measured, and worth knowing before anyone "simplifies" M09: removing
 * pre-materialisation from cq_sandwich does NOT reach this case either. The
 * driver's own release-epilogue kind check fires first, in Release — so I6(b)
 * has a both-configuration backstop that is not the Debug fingerprint. */
CQ_TEST(the_stream_is_a_palindrome_around_the_copyout)
{
    static const int WS[] = { 1, 2, 3, 5, 8 };
    cq_mock fwd, unc;

    cq_mock_init(&fwd);
    cq_mock_init(&unc);

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        cq_ref_w all = cq_ref_w_ones(W);

        run_masked(&ADD, cq_ref_w_add, W, all, all, all, all, &fwd, &unc);
        CHECK(cq_mock_is_palindrome(&fwd, (size_t)(5 * W - 2), (size_t)W));
        CHECK(cq_mock_is_palindrome(&unc, (size_t)(5 * W - 2), (size_t)W));

        run_masked(&SUB, cq_ref_w_sub, W, all, all, all, all, &fwd, &unc);
        CHECK(cq_mock_is_palindrome(&fwd, (size_t)(7 * W - 1), (size_t)W));
        CHECK(cq_mock_is_palindrome(&unc, (size_t)(7 * W - 1), (size_t)W));
    }

    cq_mock_dispose(&fwd);
    cq_mock_dispose(&unc);
}

/* RISK R8's MEASURED WITNESS, and the reason I6(b) exists. W=3 with
 * a = b = {Q, ZERO, ZERO} — two narrow quantum values in a wide register,
 * which is the TYPICAL CQ_lang shape and not a corner case. Pre-decision the
 * forward pass folded the j=3 CCX at i=1 to nothing (t[1] was still BIT_ZERO)
 * while the reverse pass emitted it (t[1] had become CQ_BIT_Q) and allocated a
 * qubit for c[2]: 4 gates forward against 5 reverse, scratch left dirty, and
 * L1 GREEN throughout. Under I6(b) every scratch bit is CQ_BIT_Q from step 0
 * and the halves mirror exactly.
 *
 * The counts are K06.md §3.7's own hand-fold — compute half 8, sandwiched 19 —
 * so this pins a MIXED-mask golden at its own number, which §3.7 says any
 * mixed-mask pin must do. A count alone could not see the pre-decision bug;
 * the palindrome can, and that is why both are here. */
CQ_TEST(r8_the_mixed_kind_witness_mirrors_exactly_at_w3)
{
    cq_ref_w lsb = cq_ref_w_setbit(0);
    cq_mock fwd, unc;
    uint64_t x, cx, ccx;

    cq_mock_init(&fwd);
    cq_mock_init(&unc);

    run_masked(&ADD, cq_ref_w_add, 3, lsb, lsb, lsb, lsb, &fwd, &unc);
    gates_of(&fwd, &x, &cx, &ccx);
    CHECK_GATES(x, cx, ccx, 0, 13, 6);              /* 2*(5 CX + 3 CCX) + 3 */
    CHECK(cq_mock_is_palindrome(&fwd, 8, 3));

    run_masked(&SUB, cq_ref_w_sub, 3, lsb, lsb, lsb, lsb, &fwd, &unc);
    gates_of(&fwd, &x, &cx, &ccx);
    CHECK_GATES(x, cx, ccx, 8, 19, 6);              /* 2*(4 X + 8 CX + 3 CCX) + 3 */
    CHECK(cq_mock_is_palindrome(&fwd, 15, 3));

    cq_mock_dispose(&fwd);
    cq_mock_dispose(&unc);
}

/* ---- R9: the all-classical short-circuit. ------------------------------- */

/* Not an optimisation. Pre-materialisation is UNCONDITIONAL, so a kernel that
 * entered the sandwich here would take 2W (add) or 3W (sub) qubits for an
 * operation with no quantum input at all, and L5's "zero gates and zero
 * qubits" would be false (plan §0.2 consequence 2, risk R9).
 *
 * THE HIGH-WATER MARK, NOT `live`, IS THE INSTRUMENT. A sandwich that took
 * scratch and gave it back leaves `live` exactly where it found it; only a
 * monotone counter records that it was ever entered. `minted` is that counter,
 * and asserting `peak` as well would be the same claim twice — src/qubits.h
 * records the identity `peak == minted` and src/qubits.c enforces it, so one
 * line is the whole assertion. (test_qubits owns the identity itself.)
 *
 * WHAT THIS CASE IS NOT: unique. cq_kd_case already applies L5 on the
 * all-classical mask pair at every swept width, on the identical trigger, and
 * the Step-12 mutation battery confirmed both fire together. It is kept
 * because it names R9 at worked two's-complement corners — a wrap, a
 * borrow-out, and 0 - 1 — rather than because it catches something L5 misses. */
static void classical_case(cq_kernel_fn kern, uint64_t va, uint64_t vb,
                           uint64_t want)
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

    kern(&ctx, cq_reg_bits(&ctx.regs, hd), cq_reg_cbits(&ctx.regs, ha),
         cq_reg_cbits(&ctx.regs, hb), 8);

    cq_pc_snap after = cq_pc_take(&ctx);

    CHECK_EQ(cq_count_total(&cnt), 0);
    CHECK_EQ(after.minted, before.minted);
    CHECK_EQ(cq_pc_value(&ctx, hd), want);

    cq_ctx_dispose(&ctx);
}

CQ_TEST(r9_all_classical_operands_never_enter_the_sandwich)
{
    classical_case(cq_kernel_add, 200u, 55u, 255u);
    classical_case(cq_kernel_add, 200u, 200u, 144u);      /* wraps mod 256 */
    classical_case(cq_kernel_sub, 200u, 55u, 145u);
    classical_case(cq_kernel_sub, 55u, 200u, 111u);       /* borrows out */
    classical_case(cq_kernel_sub, 0u, 1u, 255u);
}

/* THE OTHER SIDE OF THE SAME FOLD, AND NO SWEEP CASE REACHES IT. The driver
 * always mints `dst` all-BIT_ZERO, so every all-classical case above lands on
 * `cq_bit_flip_const` and emits nothing. With `dst` already on qubits the same
 * code path must emit a REAL X per set bit of the sum — which is what makes
 * the short-circuit a correct implementation of `dst ^= f(a,b)` rather than
 * merely a cheap one. Delete the emit and only this case goes red. */
CQ_TEST(the_classical_fold_writes_real_gates_into_a_quantum_dst)
{
    cq_ctx ctx;
    cq_counter cnt;
    cq_sink sink;

    cq_count_reset(&cnt);
    sink = cq_sink_counter(&cnt);
    cq_ctx_init(&ctx, &sink);

    int32_t ha = cq_bk_reg_w(&ctx, 8u, cq_ref_w_make(200u, 0u, 8), cq_ref_w_zero());
    int32_t hb = cq_bk_reg_w(&ctx, 8u, cq_ref_w_make(55u, 0u, 8), cq_ref_w_zero());
    /* |0> on eight qubits — a rail a previous kernel could well have left. */
    int32_t hd = cq_bk_reg_w(&ctx, 8u, cq_ref_w_zero(), cq_ref_w_ones(8));

    cq_count_reset(&cnt);
    cq_kernel_add(&ctx, cq_reg_bits(&ctx.regs, hd), cq_reg_cbits(&ctx.regs, ha),
                  cq_reg_cbits(&ctx.regs, hb), 8);

    CHECK_EQ(cq_pc_value(&ctx, hd), 255u);
    CHECK_GATES(cnt.x, cnt.cx, cnt.ccx, 8, 0, 0);   /* one X per set bit of 255 */

    cq_ctx_dispose(&ctx);
}

/* ---- The scratch is taken, and it is given back. ------------------------ */

/* K06.md §4: peak is 2W scratch + W for dst. K07.md §4: 3W + W. Both are a
 * function of W ALONE — pre-materialisation does not consult the operand
 * kinds — which is exactly what makes them assertable as a closed form.
 *
 * `owned` is what dst ends up holding, and it differs from `peak` here, which
 * is the whole point: for K1-K5 the two are equal and that IS the "zero
 * ancillae" claim. A sandwich kernel is the case where they must not be. */
CQ_TEST(the_sandwich_takes_its_scratch_and_gives_it_back)
{
    static const int WS[] = { 1, 4, 8, 16 };

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        uint32_t peak = 0;

        CHECK_EQ(cq_kd_peak(&ADD, W, &peak), (uint32_t)W);
        CHECK_EQ(peak, (uint32_t)(3 * W));

        CHECK_EQ(cq_kd_peak(&SUB, W, &peak), (uint32_t)W);
        CHECK_EQ(peak, (uint32_t)(4 * W));
    }
}

/* ---- L1's oracle, crossed against a model that shares nothing with it. --- */

/* THE FIRST VERSION OF THIS CASE COULD NOT FAIL, and the reason is worth
 * keeping because it is the shape this project has now shipped three times.
 * It crossed cq_ref_w_add against a one-word `(a + b) & cq_ref_mask(W)` at
 * widths 1..64 and asserted `.hi == 0`. Both halves are structurally vacuous
 * there: refmodel.c's `hi_mask(W)` returns 0 for every W <= 64, so `.hi` is
 * zeroed whatever the carry does, and `lo_mask` and `cq_ref_mask` are provably
 * the same function on 1..64, so the `.lo` comparison reduces to
 * `(a+b) & m == (a+b) & m`. Deleting the cross-seam carry term from
 * cq_ref_w_add left it green. TWO MODELS THAT SHARE AN IDIOM CROSS-CHECK
 * NOTHING; the one-word pair has been removed rather than kept as decoration.
 *
 * What replaces it is a BIT-SERIAL RIPPLE, and it is deliberately NOT in
 * refmodel.c. L1's oracle must not share the kernel's algorithm — the kernel
 * IS a ripple-carry circuit — so cq_ref_w_add stays word arithmetic and this
 * ripple lives here, as the cross-check only. Between them the two share no
 * operation: one propagates a carry bit by bit, the other adds two 64-bit
 * words and tests for wrap.
 *
 * AND IT SPANS THE SEAM. Widths 63, 64, 65, 80, 100, 127 and 128 put the carry
 * on both sides of bit 64, which is the one thing the old case could not
 * reach and the only place cq_ref_w_add's `(uint64_t)(lo < a.lo)` term does
 * any work. Verified falsifiable: deleting that term turns this case red. */
static cq_ref_w ripple_ref(cq_ref_w a, cq_ref_w b, int W, int invert_b)
{
    cq_ref_w r = cq_ref_w_zero();
    int carry = invert_b;                  /* the +1 of two's complement */

    for (int i = 0; i < W; i++) {
        int x = cq_ref_w_bit(a, i);
        int y = cq_ref_w_bit(b, i) ^ invert_b;

        if (x ^ y ^ carry) r = cq_ref_w_or(r, cq_ref_w_setbit(i));
        carry = (x & y) | (carry & (x ^ y));
    }
    return r;
}

CQ_TEST(l1s_oracle_agrees_with_an_independent_bit_serial_model)
{
    static const int WS[] = { 1, 2, 3, 8, 32, 63, 64, 65, 80, 100, 127, 128 };
    cq_bk_rng rng;

    cq_bk_rng_init(&rng, 0xADDEDull);

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        cq_ref_w ones = cq_ref_w_ones(W);
        cq_ref_w zero = cq_ref_w_zero();

        for (int s = 0; s < 64; s++) {
            /* The corners first — all-ones + all-ones is the carry chain at
             * full length, which is where a dropped seam carry shows. */
            cq_ref_w A = (s < 4) ? ((s & 1) ? ones : zero)
                                 : cq_ref_w_make(cq_bk_rng_next(&rng),
                                                 cq_bk_rng_next(&rng), W);
            cq_ref_w B = (s < 4) ? ((s & 2) ? ones : zero)
                                 : cq_ref_w_make(cq_bk_rng_next(&rng),
                                                 cq_bk_rng_next(&rng), W);
            cq_ref_w got, want;

            got  = cq_ref_w_add(A, B, W);
            want = ripple_ref(A, B, W, 0);
            if (!cq_ref_w_eq(got, want))
                cq_h_fail(__FILE__, __LINE__,
                          "add W=%d: word model 0x%llx%016llx, ripple "
                          "0x%llx%016llx", W,
                          (unsigned long long)got.hi,  (unsigned long long)got.lo,
                          (unsigned long long)want.hi, (unsigned long long)want.lo);

            got  = cq_ref_w_sub(A, B, W);
            want = ripple_ref(A, B, W, 1);
            if (!cq_ref_w_eq(got, want))
                cq_h_fail(__FILE__, __LINE__,
                          "sub W=%d: word model 0x%llx%016llx, ripple "
                          "0x%llx%016llx", W,
                          (unsigned long long)got.hi,  (unsigned long long)got.lo,
                          (unsigned long long)want.hi, (unsigned long long)want.lo);
        }
    }
}

CQ_TEST_MAIN_ARGV(
    CQ_CASE(k6_add_sweep),
    CQ_CASE(k7_sub_sweep),
    CQ_CASE(controlled),
    CQ_CASE(l4_goldens),
    CQ_CASE(k7_is_k6_plus_two_w_plus_one_per_compute_half),
    CQ_CASE(l4_x_plus_one_against_bennetts_published_baseline),
    CQ_CASE(l4_x_minus_one_costs_thirteen_w),
    CQ_CASE(the_stream_is_a_palindrome_around_the_copyout),
    CQ_CASE(r8_the_mixed_kind_witness_mirrors_exactly_at_w3),
    CQ_CASE(r9_all_classical_operands_never_enter_the_sandwich),
    CQ_CASE(the_classical_fold_writes_real_gates_into_a_quantum_dst),
    CQ_CASE(the_sandwich_takes_its_scratch_and_gives_it_back),
    CQ_CASE(l1s_oracle_agrees_with_an_independent_bit_serial_model)
)
