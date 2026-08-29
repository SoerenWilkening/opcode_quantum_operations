/* tests/test_kernel_addacc.c — M15, Step 15. K8, the Cuccaro accumulator.
 *
 * THIS SUITE IS HAND-ROLLED AND THAT IS NOT A CHOICE. Every kernel since Step
 * 10 has run through cq_kd_sweep, which exists so a kernel step is "write the
 * port, add three lines to the suite". K8 cannot use it, and the reason is the
 * whole subject of the module: the shared driver's four levels are stated for
 * Rule 7's contract — `dst ^= f(a,b)`, sources unchanged, uncompute is a
 * SECOND CALL of the same kernel — and K8 satisfies none of the three. It is
 * `acc += b`: in place, destructive in its first operand, transiently
 * destructive in its second, and its inverse is the REVERSE CIRCUIT rather
 * than a re-run (a second call gives `acc + 2b`, K08.md §5 point 2). Handing it
 * to cq_kd_case would not fail informatively; it would allocate a `dst` the
 * kernel does not have and compare it against a reference for a function K8
 * does not compute.
 *
 * SO EVERY LEVEL IS RESTATED HERE, AND THE RESTATEMENTS ARE NOT THE OBVIOUS ONES:
 *
 *   L1  acc == (acc + b) mod 2^W, AND `b` is restored bit for bit, AND the
 *       ancilla is back to |0>. The second and third are not decoration — a
 *       Cuccaro whose UMA chain is mis-transcribed computes the right SUM and
 *       leaves the addend dirty, which is a silent miscompile in K11 (the
 *       partial product is read again by the reverse pass) and invisible to a
 *       value-only oracle.
 *   L2  K8 allocates NOTHING — `x` is the caller's — so the live set is exactly
 *       what the three registers own, before and after, and `live` is equal.
 *   L3  is REVERSE REPLAY, not a second call. Running the steps at descending
 *       indices restores acc, b and x. That is precisely how K11's sandwich
 *       will undo it, so this is the level that tests the sanctioned use.
 *   L4  (0, 4W-2, 2W-3) at W >= 2 and (0, 1, 0) at W = 1, pinned in
 *       tests/goldens/addacc.counts.
 *   L5  DOES NOT APPLY, and saying so is a finding rather than an omission.
 *       Every other kernel folds an all-classical operand to zero gates and
 *       zero qubits because CQ_lang emits that opcode on constants. K8 has no
 *       cqrt_* symbol at all, is never entered from the shim, and its only v1
 *       caller hands it pre-materialised scratch — so a classical operand is a
 *       caller bug, and an ACTIVE hazard besides (K08.md §2 consequence 3,
 *       K11.md §2b's R8 trace). It is refused, loudly, and the refusals are
 *       tests/test_kernel_addacc_death.c.
 *
 *   §9  HAS NO CASE HERE EITHER, and that is Rule 9 rather than a gap. The
 *       controlled axis is an EMITTER MODE: K8 emits through cq_emit_x/cx/ccx
 *       like everything else, so every gate cq_addacc_step produces is promoted
 *       by M06 without this module knowing the axis exists — and there is no
 *       `_controlled` variant of a step function, which plan §0.4 forbids by
 *       name. What would be needed is a case exercising K8's gates INSIDE a
 *       region, and there already is one: K11's `controlled` case sweeps the
 *       multiplier under all four of §9's regions, and W of the accumulates in
 *       every one of those cases are K8's. Adding a bespoke region case here
 *       would test M06 a second time and K8 not at all.
 *
 * THE MASK DIMENSION IS GONE FOR THE SAME REASON, and it is replaced rather
 * than dropped. L1 elsewhere crosses values with bit-kind mask PAIRS because
 * the §3 fold table branches on kind. Here exactly one mask is legal, so the
 * cross would be a single column — and the coverage that would have bought is
 * moved into the death suite, which asserts each illegal kind is refused, and
 * into `the_shadow_and_the_fold_table_both_matter_here_and_neither_moves_the_count`
 * below.
 */

#include "kernels/addacc.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "reg.h"
#include "sandwich.h"
#include "scratch.h"
#include "shadow.h"
#include "sink_count.h"

#include "support/bitkinds.h"
#include "support/goldens.h"
#include "support/harness.h"
#include "support/kerneldrv.h"   /* cq_kd_samples — the shared L1 budget */
#include "support/mock_sink.h"
#include "support/poolcheck.h"
#include "support/refmodel.h"

#include <stdio.h>

/* ---- The fixture: three all-quantum registers and a counting sink. ------- */

typedef struct {
    cq_ctx     ctx;
    cq_counter cnt;
    cq_sink    sink;
    int32_t    h_acc, h_b, h_x;
    cq_addacc_block k;
} k8_fix;

/* Builds acc, b and x at the ONE legal mask — all-quantum — and resets the
 * counter afterwards, because materialising a set bit emits an X and what L4
 * measures is the KERNEL. */
static void k8_open(k8_fix *f, int W, cq_ref_w va, cq_ref_w vb)
{
    cq_ref_w all = cq_ref_w_ones(W);

    cq_count_reset(&f->cnt);
    f->sink = cq_sink_counter(&f->cnt);
    cq_ctx_init(&f->ctx, &f->sink);

    f->h_acc = cq_bk_reg_w(&f->ctx, (uint32_t)W, va, all);
    f->h_b   = cq_bk_reg_w(&f->ctx, (uint32_t)W, vb, all);
    f->h_x   = cq_bk_reg_w(&f->ctx, 1u, cq_ref_w_zero(), cq_ref_w_ones(1));

    f->k.acc = cq_reg_bits(&f->ctx.regs, f->h_acc);
    f->k.b   = cq_reg_bits(&f->ctx.regs, f->h_b);
    f->k.x   = cq_reg_bits(&f->ctx.regs, f->h_x);
    f->k.W   = W;

    cq_count_reset(&f->cnt);
}

static void k8_close(k8_fix *f)
{
    /* No frees, matching kerneldrv.c's fixture: a rail holding a non-zero value
     * on materialised qubits is genuinely not |0>, and refusing to free it is
     * Rule 6 working. cq_ctx_dispose returns nothing to the pool. */
    cq_ctx_dispose(&f->ctx);
}

/* Every step at descending index — how K11's sandwich will undo this call, and
 * the ONLY thing that does. A second forward call gives `acc + 2b`. */
static void k8_reverse(k8_fix *f)
{
    for (int u = cq_addacc_steps(f->k.W) - 1; u >= 0; u--)
        cq_addacc_step(&f->ctx, &f->k, u);
}

/* ---- L1 + L2 + L3, one case. -------------------------------------------- */

static void k8_case(int W, cq_ref_w va, cq_ref_w vb)
{
    k8_fix f;
    int32_t hs[3];
    cq_pc_snap before, after;
    cq_ref_w want;

    k8_open(&f, W, va, vb);
    hs[0] = f.h_acc; hs[1] = f.h_b; hs[2] = f.h_x;
    before = cq_pc_take(&f.ctx);

    cq_kernel_addacc(&f.ctx, &f.k);

    /* L1, all three clauses. `b` restored and `x` clean are as load-bearing as
     * the sum: the carry chain LIVES in b's wires, so a mis-transcribed UMA
     * gives the right acc and a dirty addend. */
    want = cq_ref_w_add(va, vb, W);
    if (!cq_ref_w_eq(cq_pc_value_w(&f.ctx, f.h_acc), want))
        cq_h_fail(__FILE__, __LINE__, "addacc W=%d: acc != (acc+b) "
                  "(acc=%llx:%llx b=%llx:%llx)", W,
                  (unsigned long long)va.hi, (unsigned long long)va.lo,
                  (unsigned long long)vb.hi, (unsigned long long)vb.lo);

    if (!cq_ref_w_eq(cq_pc_value_w(&f.ctx, f.h_b), cq_ref_w_make(vb.lo, vb.hi, W)))
        cq_h_fail(__FILE__, __LINE__, "addacc W=%d: the addend was not restored", W);

    if (!cq_ref_w_is_zero(cq_pc_value_w(&f.ctx, f.h_x)))
        cq_h_fail(__FILE__, __LINE__, "addacc W=%d: the ancilla is not |0>", W);

    /* L2. K8 takes no scratch of its own, so this is stronger than it is for a
     * sandwich kernel: not "it tidied up" but "it never allocated". */
    after = cq_pc_take(&f.ctx);
    if (!cq_pc_same(before, after))
        cq_h_fail(__FILE__, __LINE__, "addacc W=%d: live moved %u -> %u; K8 "
                  "allocates nothing", W, before.live, after.live);
    cq_pc_live_is_exactly(&f.ctx, hs, 3u);

    /* L3 — reverse replay, the real inverse. */
    k8_reverse(&f);

    if (!cq_ref_w_eq(cq_pc_value_w(&f.ctx, f.h_acc), cq_ref_w_make(va.lo, va.hi, W)))
        cq_h_fail(__FILE__, __LINE__, "addacc W=%d: the reverse replay did not "
                  "restore acc", W);
    if (!cq_ref_w_eq(cq_pc_value_w(&f.ctx, f.h_b), cq_ref_w_make(vb.lo, vb.hi, W)))
        cq_h_fail(__FILE__, __LINE__, "addacc W=%d: the reverse replay dirtied b", W);
    if (!cq_ref_w_is_zero(cq_pc_value_w(&f.ctx, f.h_x)))
        cq_h_fail(__FILE__, __LINE__, "addacc W=%d: the reverse replay dirtied x", W);

    after = cq_pc_take(&f.ctx);
    if (!cq_pc_same(before, after))
        cq_h_fail(__FILE__, __LINE__, "addacc W=%d: the reverse replay moved the pool", W);
    cq_pc_live_is_exactly(&f.ctx, hs, 3u);

    k8_close(&f);
}

/* ---- The sweeps: the shared constant sample budget. --------------------- */

/* K8 HAS EXACTLY ONE LEGAL MASK, so unlike every other kernel its sample is
 * over VALUES ALONE. A classical operand is REFUSED, not folded (K08.md §5 D7):
 * K8 writes its own addend, so a classical b[i] would be materialised
 * mid-construction and the reverse replay would stop cancelling while L1 stayed
 * green. cq_addacc_check enforces that in both configurations, and
 * tests/test_kernel_addacc_death.c IS K8's L5. There is therefore no
 * all-classical anchor to force here — forcing one would drive a case the
 * kernel hard-errors on.
 *
 * THE BUDGET IS cq_kd_samples(), THE SAME CONSTANT THE SHARED DRIVER USES
 * (2026-08-20), even though K8 cannot go through that driver: it is acc += b,
 * in place and destructive, so cq_kd_case's contract (a fresh zero dst, sources
 * unchanged, L3 as a second call giving acc + 2b) does not hold and this file
 * restates every level by hand with a descending-index replay for L3. Sharing
 * the CONSTANT rather than the driver is the most this kernel can share.
 *
 * THE CORNERS ARE THE CARRY CASES and they are forced first, inside the budget:
 * all-ones plus one is the full-length carry ripple, and it is exactly the case
 * a dropped §3.5 Toffoli gets wrong while every low-weight value stays right.
 * This used to be the full value cross product at W <= 5 and 25 corner pairs
 * plus a 1024/W tail above; the ripple is preserved, the enumeration is not. */

enum { K8_CORNERS = 6 };

static void k8_sampled(int W)
{
    cq_ref_w ones = cq_ref_w_ones(W);
    cq_ref_w one  = cq_ref_w_setbit(0);
    cq_ref_w msb  = cq_ref_w_setbit(W - 1);
    cq_ref_w zero = cq_ref_w_zero();
    const cq_ref_w ca[K8_CORNERS] = { zero, ones, ones, one,  msb,  ones };
    const cq_ref_w cb[K8_CORNERS] = { zero, ones, one,  ones, msb,  zero };
    int n = cq_kd_samples();
    cq_bk_rng rng;

    cq_bk_rng_init(&rng, 0xC0CCA20ull ^ (uint64_t)W);

    for (int s = 0; s < n; s++) {
        cq_ref_w va, vb;

        if (s < K8_CORNERS) {
            va = ca[s];
            vb = cb[s];
        } else {
            va = cq_ref_w_make(cq_bk_rng_next(&rng), cq_bk_rng_next(&rng), W);
            vb = cq_ref_w_make(cq_bk_rng_next(&rng), cq_bk_rng_next(&rng), W);
        }
        k8_case(W, va, vb);
    }

    printf("# addacc W=%3d SAMPLED: %d cases (%d carry corners forced, %d drawn) "
           "at the one legal mask (all-quantum), seed 0xC0CCA20^W — a CONSTANT "
           "budget; see tests/support/kernelsweep.c\n",
           W, n, K8_CORNERS, n > K8_CORNERS ? n - K8_CORNERS : 0);
    fflush(stdout);
}

CQ_TEST(k8_exhaustive_widths)
{
    for (int W = 1; W <= 5; W++) k8_sampled(W);
}

/* i128 IS IN SCOPE AND IT IS NOT A COURTESY: `mul` ships at i128
 * (opcode_table.yaml:186), so K11's inner accumulator runs there and K8 must be
 * width-generic to 128 like every other kernel. Nothing here is packed — the
 * VALUES are two-word cq_ref_w on the reference side only (refmodel.h). */
CQ_TEST(k8_wide_widths)
{
    static const int WS[] = { 8, 16, 32, 64, 128 };

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) k8_sampled(WS[i]);
}

/* ---- L4: the goldens. --------------------------------------------------- */

/* K08.md §3. `lower_add_cuccaro!` pushes no NOTGate at any width, 4W-2 CNOT and
 * 2W-3 Toffoli — counted block by block from adder.jl:98-145, with both loop
 * ranges checked at their degenerate edges (W=3 empty, W=4 one iteration), and
 * the W=2 branch at :84-96 counted directly as 6 CNOT + 1 Toffoli, which the
 * closed form also gives.
 *
 * W=1 IS OUTSIDE THE CLOSED FORM AND IS PINNED SEPARATELY. `4W-2 = 2` and
 * `2W-3 = -1` there — a negative gate count. The total `6W-5 = 1` happens to be
 * right, and relying on that coincidence is exactly the wrong golden waiting to
 * happen (K08.md §5 D1). */
static void closed_form(int W, uint64_t *x, uint64_t *cx, uint64_t *ccx)
{
    *x = 0u;

    if (W == 1) { *cx = 1u; *ccx = 0u; return; }

    *cx  = (uint64_t)(4 * W - 2);
    *ccx = (uint64_t)(2 * W - 3);
}

/* Measures one width at the all-quantum mask — the only mask K8 accepts, which
 * makes the usual "the golden names the mask it was taken at" caveat trivially
 * satisfied here and worth saying anyway. Returns the forward and the reverse
 * replay SEPARATELY. */
static void k8_measure(int W, cq_counter *fwd, cq_counter *rev)
{
    k8_fix f;

    k8_open(&f, W, cq_ref_w_ones(W), cq_ref_w_ones(W));

    cq_kernel_addacc(&f.ctx, &f.k);
    *fwd = f.cnt;

    cq_count_reset(&f.cnt);
    k8_reverse(&f);
    *rev = f.cnt;

    k8_close(&f);
}

CQ_TEST(l4_goldens)
{
    static const int WS[] = { 1, 2, 3, 4, 8, 16, 32, 64, 128 };
    cq_gold g;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/addacc.counts",
                      "M15 kernels/addacc.c — K8 Cuccaro accumulator (CLEAN, "
                      "no sandwich)",
                      "ALL-QUANTUM on acc and b — the ONLY mask K8 accepts "
                      "(cq_addacc_check refuses a classical bit, K08.md §5 "
                      "D7); hand-rolled measurement, not cq_kd_measure; no "
                      "sandwich, no control region",
                      CQOPS_BENNETT_COMMIT))
        return;

    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
        cq_counter fwd, rev;
        uint64_t x, cx, ccx;
        int W = WS[j];

        closed_form(W, &x, &cx, &ccx);
        k8_measure(W, &fwd, &rev);

        CHECK_GATES(fwd.x, fwd.cx, fwd.ccx, x, cx, ccx);
        CHECK_EQ(fwd.x + fwd.cx + fwd.ccx, (uint64_t)cq_addacc_steps(W));
        CHECK_EQ(fwd.ry + fwd.rz + fwd.mz, 0);

        /* The reverse replay is the SAME gate multiset by construction — it is
         * the same steps at descending indices — so unlike Rule 14's forward
         * vs `_unc` asymmetry these two genuinely must agree, and there is no
         * rotation axis in reach that could make them differ. Asserted rather
         * than assumed, because "by construction" is what a wrong index
         * mapping also looks like. */
        CHECK_GATES(rev.x, rev.cx, rev.ccx, x, cx, ccx);

        cq_gold_check(&g, "addacc", "forward", W, fwd.x, fwd.cx, fwd.ccx);
        cq_gold_check(&g, "addacc", "reverse", W, rev.x, rev.cx, rev.ccx);
    }

    CHECK(cq_gold_close(&g));
}

/* K08.md §3's evaluated table, transcribed as literals. The goldens above are
 * generated from `closed_form`, so on their own they pin whatever it happens to
 * compute; these are the numbers the DOCUMENT claims, and the row upstream
 * pins directly (test_op6a_cuccaro_gate_count.jl asserts the same three for
 * W in {2,3,4,8,16,32,64}). */
CQ_TEST(the_evaluated_table_in_k08_matches_what_is_emitted)
{
    static const struct { int W; uint64_t cx, ccx, total; } ROWS[] = {
        {  2,   6u,   1u,   7u }, {  3,  10u,   3u,  13u },
        {  4,  14u,   5u,  19u }, {  8,  30u,  13u,  43u },
        { 16,  62u,  29u,  91u }, { 32, 126u,  61u, 187u },
        { 64, 254u, 125u, 379u }
    };

    for (size_t i = 0; i < sizeof ROWS / sizeof ROWS[0]; i++) {
        cq_counter fwd, rev;

        CHECK_EQ(ROWS[i].cx + ROWS[i].ccx, ROWS[i].total);
        CHECK_EQ(cq_addacc_steps(ROWS[i].W), (int)ROWS[i].total);

        k8_measure(ROWS[i].W, &fwd, &rev);
        CHECK_GATES(fwd.x, fwd.cx, fwd.ccx, 0u, ROWS[i].cx, ROWS[i].ccx);
    }

    /* W=1's own row, which the table above cannot carry: the document marks it
     * "formula invalid" and D1 pins (0, 1, 0). */
    {
        cq_counter fwd, rev;

        k8_measure(1, &fwd, &rev);
        CHECK_GATES(fwd.x, fwd.cx, fwd.ccx, 0u, 1u, 0u);
    }
}

#include "test_kernel_addacc_upstream.inc"
#include "test_kernel_addacc_sandwich.inc"

CQ_TEST_MAIN_ARGV(
    CQ_CASE(k8_exhaustive_widths),
    CQ_CASE(k8_wide_widths),
    CQ_CASE(l4_goldens),
    CQ_CASE(the_evaluated_table_in_k08_matches_what_is_emitted),
    CQ_CASE(the_gate_stream_is_adder_jl_transcribed),
    CQ_CASE(the_addend_really_is_written_during_the_construction),
    CQ_CASE(the_last_gate_is_a_no_op_that_is_emitted_anyway),
    CQ_CASE(the_ancilla_can_be_freed_after_a_bare_call),
    CQ_CASE(k8_inside_a_sandwich_is_a_palindrome_and_leaves_scratch_clean),
    CQ_CASE(a_sandwiched_k8_computes_the_sum_of_two_real_operands)
)
