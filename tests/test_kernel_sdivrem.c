/* tests/test_kernel_sdivrem.c — M20, Step 17. K12 `sdiv` and `srem`.
 *
 * THIS SUITE IS WHERE K12's SIGNED PATH STOPPED BEING ARITHMETIC. K12.md
 * §6.0a's 2026-08-16 measurement pass covered the whole unsigned core at nine
 * widths in both configurations, and its "what is still NOT executed" list has
 * exactly one entry: *the entire signed path — §2.3's prefix and suffix, §3.4's
 * closed forms, §4's signed qubit rows, and the signed `b = 0` values.* Every
 * figure this file pins was DERIVED and had never been run before Step 17.
 *
 * Three things follow from that, and they shape what is asserted here.
 *
 *   - THE WRAPPER'S COST IS CHECKED AGAINST ITS PARTS, NOT ONLY AGAINST §3.4.
 *     `compute = W·C_iter + 2W + 3·C_condneg + (2 for sdiv)`, with `C_iter`
 *     MEASURED from M16's, M14's and M17's exported blocks exactly as the M19
 *     suite measures it. So a signed golden cannot be regenerated into
 *     agreement with a wrapper that lost a condneg or ran the core at the wrong
 *     arity.
 *   - THE 2-CX PORT DELTA IS ASSERTED, NOT ASSUMED. Bennett copies each sign
 *     bit into a fresh wire first (aggregate.jl:76-77) because its conditional
 *     negate would clobber its own control; we do not, because ours are sources
 *     used only as controls. A literal gate-for-gate reading of upstream gives
 *     `11W+7` / `11W+5` per compute half where §3.4 has `11W+5` / `11W+3`, and
 *     K12.md records that an independent reader DID reach the upstream numbers
 *     and had to be told which was which. The wrapper-cost case below is what
 *     decides it by execution.
 *   - THE UNREPRESENTABLE CASES ARE PINNED AND LABELLED INHERITED. `b = 0`,
 *     `typemin / -1`, and the whole of i1 — where the signed values are {0,-1}
 *     and `sdiv(-1,-1) = 1` has no encoding. i1 IS a shipped sdiv/srem width
 *     (opcode_table.yaml:187,189) and upstream never meets the case because it
 *     widens to 64 first, so there is nothing to port and nothing to compare
 *     to: D3's deterministic-but-unspecified posture is the answer, and what
 *     the construction returns is written down here.
 */

#include "kernels/divrem_s.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/divrem_u.h"
#include "kernels/mux.h"
#include "reg.h"
#include "scratch.h"
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

static cq_ref_w refn_sdiv(const cq_ref_w *s, const cq_kd_shape *sh)
{ return cq_ref_w_sdiv(s[0], s[1], sh->w_dst); }

static cq_ref_w refn_srem(const cq_ref_w *s, const cq_kd_shape *sh)
{ return cq_ref_w_srem(s[0], s[1], sh->w_dst); }

static const cq_kd_spec SDIV = { "sdiv", cq_kernel_sdiv, NULL, NULL, NULL, refn_sdiv };
static const cq_kd_spec SREM = { "srem", cq_kernel_srem, NULL, NULL, NULL, refn_srem };

#include "test_kernel_divrem_common.inc"

/* `_cond_negate_inplace!`, aggregate.jl:174-191: W conditional flips, one carry
 * seed, then W (Toffoli, CNOT) pairs — `2W+1` CX and `W` CCX, `3W+1` gates.
 * The ONE closed form in K12 that no sibling module ships (K12.md §3.0), which
 * is why it is written down here and measured against nothing but itself. */
static int condneg_steps(int W) { return 3 * W + 1; }

/* ---- The wrapper is the core plus a fixed prefix and suffix. ------------- */

static void compute_half(const cq_kd_spec *k, int W, cq_counter *out)
{
    cq_counter fwd, unc;
    uint64_t w = (uint64_t)W;

    cq_kd_measure(k, W, &fwd, &unc);

    CHECK_EQ(fwd.x % 2u, 0);
    CHECK_EQ((fwd.cx - w) % 2u, 0);
    CHECK_EQ(fwd.ccx % 2u, 0);
    CHECK_EQ(fwd.ry + fwd.rz + fwd.mz, 0);
    CHECK_EQ(unc.ry + unc.rz + unc.mz, 0);

    cq_count_reset(out);
    out->x   = fwd.x / 2u;
    out->cx  = (fwd.cx - w) / 2u;
    out->ccx = fwd.ccx / 2u;
}

/* THE ASSERTION THAT SURVIVES `CQOPS_UPDATE_GOLDENS=1`, in its signed form.
 * The core's share is W MEASURED iterations of M16's + M14's + M17's blocks;
 * the wrapper's share is `2W` copies plus three conditional negates plus, for
 * sdiv, the two CX that build `sign(a) ^ sign(b)`.
 *
 * `+2` AND NOT `+4` IS THE PORT DELTA, DECIDED HERE BY EXECUTION. Upstream
 * spends two more CX copying the sign bits into fresh wires; we read them as
 * controls off the sources. If this case ever reads `+4`, someone has restored
 * upstream's copies and §3.4's goldens are the ones that need changing — not
 * the other way round. */
CQ_TEST(the_compute_half_is_the_core_plus_a_fixed_wrapper)
{
    static const int WS[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64, 128 };

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        uint64_t w = (uint64_t)W, cn = (uint64_t)condneg_steps(W);
        cq_counter c, it;

        /* sdiv: the udiv core, 2W copies, three condnegs and the `rs` pair. */
        compute_half(&SDIV, W, &c);
        dr_iter_cost(W, 1, &it);
        CHECK_GATES(c.x, c.cx, c.ccx,
                    w * it.x,
                    w * it.cx + 2u * w + 3u * (2u * w + 1u) + 2u,
                    w * it.ccx + 3u * w);
        CHECK_EQ(cq_count_total(&c), (uint64_t)cq_sdivrem_steps(W, 1));
        CHECK_EQ(cq_sdivrem_steps(W, 1),
                 W * (17 * W + 2) + 2 * W + 3 * condneg_steps(W) + 2);

        /* srem: the urem core, the same prefix, one condneg on the remainder
         * and no `rs` — the remainder's sign is the dividend's. */
        compute_half(&SREM, W, &c);
        dr_iter_cost(W, 0, &it);
        CHECK_GATES(c.x, c.cx, c.ccx,
                    w * it.x,
                    w * it.cx + 2u * w + 3u * (2u * w + 1u),
                    w * it.ccx + 3u * w);
        CHECK_EQ(cq_count_total(&c), (uint64_t)cq_sdivrem_steps(W, 0));
        CHECK_EQ(cq_sdivrem_steps(W, 0),
                 W * (17 * W + 1) + 2 * W + 3 * condneg_steps(W));

        /* And the three condnegs really are 3W+1 gates each, which is what
         * makes the wrapper's budget add up. */
        CHECK_EQ(cn, 3u * w + 1u);
    }
}

/* ---- L4: the goldens. --------------------------------------------------- */

/* K12.md §3.4, at the ALL-QUANTUM operand mask:
 *
 *      sdiv   4W²+4W X   20W²+21W+10 CX   10W²+2W CCX   = 34W²+27W+10
 *      srem   4W²+4W X   20W²+19W+6  CX   10W²+2W CCX   = 34W²+25W+6
 *
 * The CCX column gains `3W` over the unsigned core and NOT `3W-3`: Bennett's
 * dead top Toffoli at `i == W` (aggregate.jl:186-188) writes `ncar[W]`, which
 * nothing reads, and K12.md §5 delta 7 keeps it faithfully. Dropping it is a
 * gate-level optimisation inside a port (Rule 1); if it is ever dropped the
 * CCX column becomes `10W²+2W-6` and the qubit rows drop by 3. */
static void closed_form(int W, int want_q, uint64_t *x, uint64_t *cx,
                        uint64_t *ccx)
{
    uint64_t w = (uint64_t)W;

    *x   = 4u * w * w + 4u * w;
    *cx  = 20u * w * w + (want_q ? 21u * w + 10u : 19u * w + 6u);
    *ccx = 10u * w * w + 2u * w;
}

static void check_counts(cq_gold *g, const cq_kd_spec *k, int W, int want_q)
{
    cq_counter fwd, unc;
    uint64_t x, cx, ccx;

    closed_form(W, want_q, &x, &cx, &ccx);
    cq_kd_measure(k, W, &fwd, &unc);

    CHECK_GATES(fwd.x, fwd.cx, fwd.ccx, x, cx, ccx);
    CHECK_GATES(unc.x, unc.cx, unc.ccx, x, cx, ccx);

    cq_gold_check(g, k->name, "forward", W, fwd.x, fwd.cx, fwd.ccx);
    cq_gold_check(g, k->name, "unc",     W, unc.x, unc.cx, unc.ccx);
}

CQ_TEST(l4_goldens)
{
    static const int WS[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64, 128 };
    cq_gold g;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/divrem_s.counts",
                      "M20 kernels/divrem_s.c — K12 sdiv/srem (sign-magnitude "
                      "over M19, sandwiched). SELF-PINNED and FIRST EXECUTED AT "
                      "STEP 17: K12.md §6.1 listed the whole signed path as "
                      "derived-but-not-run",
                      "ALL-QUANTUM on both operands, at ctrl_depth 0",
                      CQOPS_BENNETT_COMMIT))
        return;

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        check_counts(&g, &SDIV, WS[i], 1);
        check_counts(&g, &SREM, WS[i], 0);
    }

    CHECK(cq_gold_close(&g));
}

/* ---- The palindrome. ---------------------------------------------------- */

/* The source-controlled gates of the SIGNED wrapper, which are a different set
 * from the unsigned core's — and the difference is the whole point of running
 * this case rather than reusing M19's. The core divides `sa` and `sb`, which
 * are SCRATCH, so no operand fold reaches it at all: everything mask-dependent
 * is in the prefix and the suffix.
 *
 *      prefix copies       CX(a[i] -> sa[i]), CX(b[i] -> sb[i])   one per bit
 *      condneg(sa, a[MSB]) its FIRST W+1 gates, controlled by a's SIGN BIT
 *      condneg(sb, b[MSB]) its first W+1 gates, controlled by b's sign bit
 *      sdiv suffix         CX(a[MSB] -> rs), CX(b[MSB] -> rs)
 *      srem suffix         condneg(rem, a[MSB]), its first W+1 gates
 *
 * so a classical-ZERO sign bit removes W+1 gates of a conditional negate, while
 * a classical ONE removes none — K09.md §3.3.1's rule in its sharpest form.
 *
 * `W+1` AND NOT `3W+1`, WHICH IS WHERE THE FIRST DRAFT OF THIS FUNCTION WAS
 * WRONG AND THE SUITE CAUGHT IT. A conditional negate is `3W+1` gates but only
 * its first `W+1` — the W conditional flips and the carry seed — are controlled
 * by `cond`. The remaining `2W` are the `(Toffoli, CNOT)` pairs of the carry
 * chain, whose controls are `val[c]` and `ncar[c]`, BOTH SCRATCH and therefore
 * both CQ_BIT_Q from step 0 under I6(b). They are emitted whatever `cond` is,
 * and with `cond = 0` they act on an all-|0> carry chain and do nothing — which
 * is the correct behaviour and not a waste to optimise away, because the fold
 * table sees each gate alone (mux.c makes the same argument at more length).
 *
 * The other correction the suite forced: an ALL-CLASSICAL mask pair is not a
 * palindrome case at all. It takes R9's short-circuit, never enters
 * cq_sandwich, and emits nothing — which is L5, and is asserted as L5 by
 * r9_all_classical_operands_never_enter_the_sandwich. Every mask below leaves
 * at least one operand quantum for that reason. */
static size_t sdiv_head(int W, int want_q, cq_ref_w va, cq_ref_w qa,
                        cq_ref_w vb, cq_ref_w qb)
{
    size_t n = (size_t)cq_sdivrem_steps(W, want_q);
    int a_sign_zero = dr_zero_bit(va, qa, W - 1);
    int b_sign_zero = dr_zero_bit(vb, qb, W - 1);

    for (int i = 0; i < W; i++) {
        if (dr_zero_bit(va, qa, i)) n -= 1u;
        if (dr_zero_bit(vb, qb, i)) n -= 1u;
    }

    if (a_sign_zero) n -= (size_t)(W + 1);          /* condneg(sa) */
    if (b_sign_zero) n -= (size_t)(W + 1);          /* condneg(sb) */

    if (want_q) {
        if (a_sign_zero) n -= 1u;                   /* CX(a MSB -> rs) */
        if (b_sign_zero) n -= 1u;                   /* CX(b MSB -> rs) */
    } else if (a_sign_zero) {
        n -= (size_t)(W + 1);                       /* condneg(remainder) */
    }
    return n;
}

CQ_TEST(the_stream_is_a_palindrome_around_the_copyout)
{
    static const int WS[] = { 1, 2, 3, 4, 5, 8 };
    const cq_kd_spec *KS[2] = { &SDIV, &SREM };
    cq_mock m;

    cq_mock_init(&m);

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++)
        for (int q = 0; q < 2; q++) {
            const cq_kd_spec *k = KS[q];
            int W = WS[i], want_q = 1 - q;
            cq_ref_w all = cq_ref_w_ones(W);
            cq_ref_w none = cq_ref_w_zero();
            cq_ref_w three = cq_ref_w_make(3u, 0u, W);
            size_t steps = (size_t)cq_sdivrem_steps(W, want_q);
            size_t head;

            /* All-quantum: nothing folds, so gates == slots. */
            dr_run_masked(k, W, all, all, three, all, &m);
            head = sdiv_head(W, want_q, all, all, three, all);
            CHECK_EQ(head, steps);
            CHECK(cq_mock_is_palindrome(&m, head, (size_t)W));
            CHECK_EQ(cq_mock_count(&m), 2u * head + (size_t)W);

            /* `a` CLASSICAL, `b` quantum. At W >= 3 the value 3 has a zero sign
             * bit, so condneg(sa)'s controlled half folds away; at W <= 2 it
             * does not, and the same formula covers both because it reads the
             * actual bits rather than assuming a sign. */
            dr_run_masked(k, W, three, none, three, all, &m);
            head = sdiv_head(W, want_q, three, none, three, all);
            CHECK(cq_mock_is_palindrome(&m, head, (size_t)W));
            CHECK_EQ(cq_mock_count(&m), 2u * head + (size_t)W);

            /* `a` all quantum, `b` all CQ_BIT_ZERO — risk R8's mandated
             * asymmetric witness, which no symmetric mask set contains. b's
             * sign bit is a classical zero, so condneg(sb)'s controlled half
             * folds; a's is a qubit, so condneg(sa) is emitted in full. */
            dr_run_masked(k, W, all, all, none, none, &m);
            head = sdiv_head(W, want_q, all, all, none, none);
            CHECK(cq_mock_is_palindrome(&m, head, (size_t)W));
            CHECK_EQ(cq_mock_count(&m), 2u * head + (size_t)W);

            /* `a` all-ones and all-CLASSICAL — i.e. -1, whose sign bit is a
             * classical ONE — against a quantum `b`. Every gate is still
             * emitted, CX rewritten as X and CCX as CX, so the head is the
             * all-quantum head EXACTLY. An L5 assertion written with `<` fails
             * right here, which is K09.md §3.3.1's corrected rule. */
            dr_run_masked(k, W, all, none, three, all, &m);
            head = sdiv_head(W, want_q, all, none, three, all);
            CHECK_EQ(head, steps);
            CHECK(cq_mock_is_palindrome(&m, head, (size_t)W));
            CHECK_EQ(cq_mock_count(&m), 2u * head + (size_t)W);
        }

    cq_mock_dispose(&m);
}

/* ---- The scratch. ------------------------------------------------------- */

/* K12.md §4: `udiv + 2W (sa,sb) + 3(W+1) (condneg) + 1 (rs)` = `8W²+9W+3`, and
 * `urem + 2W + 3(W+1)` = `8W²+8W+2`. DERIVED THERE, MEASURED HERE — §4's signed
 * rows say so explicitly ("the sdiv/srem rows are derived, not measured"). */
CQ_TEST(the_sandwich_takes_its_scratch_and_gives_it_back)
{
    static const int WS[] = { 1, 2, 3, 4, 8, 16, 32, 64, 128 };

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        uint64_t w = (uint64_t)W;
        uint32_t peak = 0;

        CHECK_EQ(cq_kd_peak(&SDIV, W, &peak), (uint32_t)W);
        CHECK_EQ(peak, (uint32_t)(8u * w * w + 10u * w + 3u));   /* region + dst */
        CHECK_EQ(cq_sdivrem_region(W, 1), (int)(8u * w * w + 9u * w + 3u));

        CHECK_EQ(cq_kd_peak(&SREM, W, &peak), (uint32_t)W);
        CHECK_EQ(peak, (uint32_t)(8u * w * w + 9u * w + 2u));
        CHECK_EQ(cq_sdivrem_region(W, 0), (int)(8u * w * w + 8u * w + 2u));
    }
}

/* ---- D3 and the unrepresentable cases. ---------------------------------- */

/* Runs the CIRCUIT on quantum operands and states the value explicitly, rather
 * than only asserting that the kernel and the reference agree — these are
 * inherited constants and the point is to have them written down. */
static void pin(const cq_kd_spec *k, int W, int64_t a, int64_t b, int64_t want,
                cq_mock *m)
{
    cq_ref_w va = cq_ref_w_make((uint64_t)a, 0u, W);
    cq_ref_w vb = cq_ref_w_make((uint64_t)b, 0u, W);
    cq_ref_w got = (k == &SDIV) ? cq_ref_w_sdiv(va, vb, W)
                                : cq_ref_w_srem(va, vb, W);

    CHECK_EQ(cq_ref_sext(got.lo, W), want);
    dr_run_masked(k, W, va, cq_ref_w_ones(W), vb, cq_ref_w_ones(W), m);
}

CQ_TEST(d3_signed_division_by_zero_and_the_unrepresentable_cases)
{
    cq_mock m;

    cq_mock_init(&m);

    /* b = 0. `|0| = 0` and `b_sign = 0`, so the unsigned core returns 2^W-1 and
     * the sign fix is `sign(a) ^ 0`: -1 for a >= 0, +1 for a < 0. The remainder
     * is `condneg(|a|, sign(a))`, which is `a` again. K12.md §5 D3. */
    pin(&SDIV, 8,  100, 0, -1,  &m);
    pin(&SDIV, 8, -100, 0,  1,  &m);
    pin(&SDIV, 8,    0, 0, -1,  &m);
    pin(&SREM, 8,  100, 0,  100, &m);
    pin(&SREM, 8, -100, 0, -100, &m);

    /* typemin / -1 wraps to typemin, and reproduces at width W without
     * upstream's 64-bit widening: |typemin| is typemin again in two's
     * complement, dividing by 1 leaves it, and the sign fix is a no-op because
     * sign(a) ^ sign(b) = 1 ^ 1 = 0. Upstream pins this only for its widened
     * path (test_salb_div_by_zero.jl:69-76). */
    pin(&SDIV, 8, -128, -1, -128, &m);
    pin(&SREM, 8, -128, -1,    0, &m);
    pin(&SDIV, 4,   -8, -1,   -8, &m);
    pin(&SREM, 4,   -8, -1,    0, &m);

    /* i1 IS a shipped sdiv/srem width (opcode_table.yaml:187,189) and NOTHING
     * is representable there: the two values are {0,-1} and sdiv(-1,-1) = 1 has
     * no i1 encoding. Upstream never meets the case because it widens to 64
     * first, so there is nothing to port and nothing to compare against. What
     * the construction returns is pinned here and is INHERITED, exactly as the
     * b = 0 row is — |−1| = 1 as an unsigned i1, so udiv(1,1) = 1, the sign fix
     * is 1^1 = 0, and the quotient bit pattern 1 reads back as -1. */
    pin(&SDIV, 1, -1, -1, -1, &m);
    pin(&SREM, 1, -1, -1,  0, &m);
    pin(&SDIV, 1,  0, -1,  0, &m);
    /* b = 0 at i1: |a| = 1, |b| = 0, udiv(1,0) = 1, and the sign fix negates
     * it — which at W = 1 leaves the bit pattern 1, i.e. -1. */
    pin(&SDIV, 1, -1,  0, -1, &m);
    pin(&SREM, 1, -1,  0, -1, &m);

    /* And the ordinary rows, so the pinned oddities are not the only evidence
     * the wrapper works: C truncates toward zero and so does sign-magnitude. */
    pin(&SDIV, 8,  7,  2,  3, &m);
    pin(&SDIV, 8, -7,  2, -3, &m);
    pin(&SDIV, 8,  7, -2, -3, &m);
    pin(&SDIV, 8, -7, -2,  3, &m);
    pin(&SREM, 8,  7,  2,  1, &m);
    pin(&SREM, 8, -7,  2, -1, &m);
    pin(&SREM, 8,  7, -2,  1, &m);
    pin(&SREM, 8, -7, -2, -1, &m);

    cq_mock_dispose(&m);
}

/* ---- R9. ---------------------------------------------------------------- */

CQ_TEST(r9_all_classical_operands_never_enter_the_sandwich)
{
    dr_classical_case(&SDIV, 8, 7u,   2u,   3u);
    dr_classical_case(&SDIV, 8, 249u, 2u,   253u);        /* -7 / 2 = -3   */
    dr_classical_case(&SDIV, 8, 7u,   254u, 253u);        /*  7 / -2 = -3  */
    dr_classical_case(&SDIV, 8, 249u, 254u, 3u);          /* -7 / -2 = 3   */
    dr_classical_case(&SREM, 8, 249u, 2u,   255u);        /* -7 % 2 = -1   */
    dr_classical_case(&SREM, 8, 7u,   254u, 1u);          /*  7 % -2 = 1   */
    dr_classical_case(&SDIV, 8, 128u, 255u, 128u);        /* typemin / -1  */
    dr_classical_case(&SREM, 8, 128u, 255u, 0u);
    dr_classical_case(&SDIV, 8, 100u, 0u,   255u);        /* D3            */
    dr_classical_case(&SREM, 8, 100u, 0u,   100u);
    dr_classical_case(&SDIV, 1, 1u,   1u,   1u);
    dr_classical_case(&SREM, 1, 1u,   1u,   0u);
}

CQ_TEST(the_classical_fold_writes_real_gates_into_a_quantum_dst)
{
    cq_ctx ctx;
    cq_counter cnt;
    cq_sink sink;

    cq_count_reset(&cnt);
    sink = cq_sink_counter(&cnt);
    cq_ctx_init(&ctx, &sink);

    int32_t ha = cq_bk_reg_w(&ctx, 8u, cq_ref_w_make(249u, 0u, 8), cq_ref_w_zero());
    int32_t hb = cq_bk_reg_w(&ctx, 8u, cq_ref_w_make(2u, 0u, 8),   cq_ref_w_zero());
    int32_t hd = cq_bk_reg_w(&ctx, 8u, cq_ref_w_zero(), cq_ref_w_ones(8));

    cq_count_reset(&cnt);
    cq_kernel_sdiv(&ctx, cq_reg_bits(&ctx.regs, hd), cq_reg_cbits(&ctx.regs, ha),
                   cq_reg_cbits(&ctx.regs, hb), 8);

    CHECK_EQ(cq_pc_value(&ctx, hd), 253u);           /* -7 / 2 = -3 = 0xFD */
    CHECK_GATES(cnt.x, cnt.cx, cnt.ccx, 7, 0, 0);    /* popcount(0xFD) = 7 */

    cq_ctx_dispose(&ctx);
}

#include "test_kernel_sdivrem_sweep.inc"

CQ_TEST_MAIN_ARGV(
    CQ_CASE(the_compute_half_is_the_core_plus_a_fixed_wrapper),
    CQ_CASE(l4_goldens),
    CQ_CASE(the_stream_is_a_palindrome_around_the_copyout),
    CQ_CASE(the_sandwich_takes_its_scratch_and_gives_it_back),
    CQ_CASE(d3_signed_division_by_zero_and_the_unrepresentable_cases),
    CQ_CASE(r9_all_classical_operands_never_enter_the_sandwich),
    CQ_CASE(the_classical_fold_writes_real_gates_into_a_quantum_dst),
    CQ_CASE(k12_signed_sweep),
    CQ_CASE(controlled)
)
