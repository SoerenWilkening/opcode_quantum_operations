/* tests/test_kernel_divrem.c — M19, Step 17. K12 `udiv` and `urem`.
 *
 * THE COMPOSITION CHECK IS FIRST IN THIS FILE BECAUSE IT WAS FIRST IN TIME.
 * K12.md §6.0's box says to build it BEFORE M19, and the reason is measured
 * rather than stylistic: the one K11 mutant that left the whole L1/L2/L3/L5
 * sweep green was "shorten each inner loop", and K12 offers the identical trade
 * with a quadratic region behind it. After `t` iterations the remainder is
 * provably below `2^t` (K12.md §2.0), so on iteration `t` the high bits of
 * `r_in[t]` are provably zero and the comparator, subtractor and mux could all
 * be narrowed towards `t + 2` bits — taking the kernel from ~17W² towards
 * ~8.5W². L4 is NOT a durable detector for that: the golden is self-pinned and
 * the documented way to make a red L4 green is `CQOPS_UPDATE_GOLDENS=1`, which
 * would bless the reduction as an improvement. What survives is
 *
 *     compute(W) = W · (2 + C_ult(W) + C_sub(W) + C_mux(W))
 *
 * with each `C` obtained by ASKING M16, M14 and M17 what their exported block
 * costs at this width — never by writing 6W+1 / 7W-1 / 4W down. Two cases below
 * carry it: the blocks-compose-to-the-tuple case, which runs no kernel at all,
 * and the compute-half case, which reads no golden.
 *
 * WHAT THIS SUITE PROVES THAT NO EARLIER ONE COULD. K12 is the first kernel
 * whose ENTIRE loop body belongs to other modules — M16's `cq_ult_step`, M14's
 * `cq_sub_step`, M17's `cq_mux_step` — so what M19 can get wrong is the SLOT
 * ARITHMETIC and the LAYOUT, not the gates. Hence the phase-boundary scan
 * (test_kernel_divrem_schedule.inc), which re-derives the op-kind of all
 * `17W²+2W` compute-half slots from first principles and compares them to the
 * recorded stream, and the peak-qubit case, which pins `8W²+4W-1` as a function
 * of W alone.
 *
 * AND ONE THING IT CANNOT DO. There is no upstream number to reconcile against:
 * `BENCHMARKS.md` has zero hits for udiv/sdiv/urem/srem,
 * `test_gate_count_regression.jl` pins no division count, and
 * `test_y56a_division_paths.jl` asserts values only. Bennett could not be
 * compared to anyway — it widens both operands to 64 and runs 64 iterations for
 * every source width (aggregate.jl:52-66, :128-131), where libcqops runs W at
 * width W. The goldens here are SELF-PINNED, and the composition identity is
 * what stands in for an upstream reconciliation.
 */

#include "kernels/divrem_u.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
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

/* TWO-WORD, because `divrem` ships at i128 (opcode_table.yaml:187-190, all four
 * opcodes, the full 15-variant grid including the bare `qq` shape — K12.md said
 * the opposite until 2026-08-16 and it was a Rule 16 trap). Rule 7's canonical
 * shape otherwise — arity 2, one width, |dst| = W — so no `shape` and no `call`
 * adapter are needed, as for K11. */
static cq_ref_w refn_udiv(const cq_ref_w *s, const cq_kd_shape *sh)
{ return cq_ref_w_udiv(s[0], s[1], sh->w_dst); }

static cq_ref_w refn_urem(const cq_ref_w *s, const cq_kd_shape *sh)
{ return cq_ref_w_urem(s[0], s[1], sh->w_dst); }

static const cq_kd_spec UDIV = { "udiv", cq_kernel_udiv, NULL, NULL, NULL, refn_udiv };
static const cq_kd_spec UREM = { "urem", cq_kernel_urem, NULL, NULL, NULL, refn_urem };

#include "test_kernel_divrem_common.inc"
#include "test_kernel_divrem_sweep.inc"

/* ---- The blocks compose to K12's per-iteration tuple. -------------------- */

/* RUNS NO KERNEL AT ALL. It asserts that M16's, M14's and M17's shipped step
 * blocks add up to K12.md §3.2's per-iteration tuple, which is what makes
 * `17W² + 2W` a CONSEQUENCE of the three sibling modules rather than an
 * independent claim about K12.
 *
 * Deliberately separate from the case that measures the kernel. If a sibling's
 * cost moves, this goes red and names which of the three blocks moved; the
 * kernel case then goes red for a reason the reader already has in front of
 * them. */
CQ_TEST(the_three_inner_blocks_compose_to_k12s_per_iteration_tuple)
{
    static const int WS[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64, 128 };

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        uint64_t w = (uint64_t)W;
        cq_counter u, s, m, it;

        dr_ult_cost(W, &u);
        dr_sub_cost(W, &s);
        dr_mux_cost(W, &m);

        /* K12.md §3.1, one row at a time. These are the only closed forms in
         * the whole chain and they are the PINNED PRIMITIVE COSTS, hand-counted
         * off the loop bodies quoted verbatim in K12.md §1 at commit 980805de;
         * everything downstream is arithmetic over what was just measured. */
        CHECK_GATES(u.x, u.cx, u.ccx, w + 1u, 3u * w, 2u * w);           /* 6W+1 */
        CHECK_GATES(s.x, s.cx, s.ccx, w + 1u, 4u * w, 2u * w - 2u);      /* 7W-1 */
        CHECK_GATES(m.x, m.cx, m.ccx, 0u,     3u * w, w);                /* 4W   */

        /* One gate per step in all three, which is plan §0.4 obligation 1 and
         * is what makes the driver's index reversal BE gate reversal. */
        CHECK_EQ(cq_count_total(&u), (uint64_t)cq_ult_steps(W));
        CHECK_EQ(cq_count_total(&s), (uint64_t)cq_sub_steps(W));
        CHECK_EQ(cq_count_total(&m), (uint64_t)(CQ_MUX_STEPS_PER_BIT * W));

        /* C_iter = 1 + C_ult + C_sub + C_mux + 1 = 17W + 2 (K12.md §3.2). */
        dr_iter_cost(W, 1, &it);
        CHECK_GATES(it.x, it.cx, it.ccx, 2u * w + 2u, 10u * w + 2u, 5u * w - 2u);
        CHECK_EQ(cq_count_total(&it), 17u * w + 2u);

        /* urem drops P4 and nothing else. */
        dr_iter_cost(W, 0, &it);
        CHECK_EQ(cq_count_total(&it), 17u * w + 1u);
    }
}

/* ---- The kernel is W of those iterations, and nothing else. -------------- */

/* THE ASSERTION THAT SURVIVES `CQOPS_UPDATE_GOLDENS=1`. The compute half is
 * recovered from the sandwiched measurement by the driver's own structure —
 * `sandwiched = 2 x compute + W copy-out CX`, which the palindrome case pins
 * independently — and is then required to equal W copies of a MEASURED
 * iteration. A K12 that narrowed its inner blocks, or ran W-1 iterations, or
 * made a whole `ult` one step, cannot satisfy this however its golden is
 * regenerated. */
static void compute_half(const cq_kd_spec *k, int W, cq_counter *out)
{
    cq_counter fwd, unc;
    uint64_t w = (uint64_t)W;

    cq_kd_measure(k, W, &fwd, &unc);

    /* The copy-out is W CX and lives between the two halves, so the CX column
     * is the only one it touches. An odd residue anywhere means the stream is
     * not [compute][W copy-out][compute] at all. */
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

CQ_TEST(the_compute_half_is_w_measured_inner_iterations)
{
    static const int WS[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64, 128 };

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        uint64_t w = (uint64_t)W;
        cq_counter c, it;

        compute_half(&UDIV, W, &c);
        dr_iter_cost(W, 1, &it);
        CHECK_GATES(c.x, c.cx, c.ccx, w * it.x, w * it.cx, w * it.ccx);

        /* And the SLOT count agrees with the gate count at the all-quantum
         * mask, which is what "one slot, one gate" means and what makes a
         * single golden per (kernel, W) sound. */
        CHECK_EQ(cq_count_total(&c), (uint64_t)cq_divrem_steps(W, 1));

        compute_half(&UREM, W, &c);
        dr_iter_cost(W, 0, &it);
        CHECK_GATES(c.x, c.cx, c.ccx, w * it.x, w * it.cx, w * it.ccx);
        CHECK_EQ(cq_count_total(&c), (uint64_t)cq_divrem_steps(W, 0));
    }
}

/* ---- L4: the goldens. --------------------------------------------------- */

/* K12.md §3.2 and §3.3, at the ALL-QUANTUM operand mask:
 *
 *      udiv   4W²+4W X   20W²+5W CX   10W²-4W CCX   = 34W²+5W
 *      urem   4W²+4W X   20W²+3W CX   10W²-4W CCX   = 34W²+3W
 *
 * ALL-QUANTUM IS THE MASK, for Rule 14's reason: because we never demote (D6)
 * an operand mask can only drift TOWARDS Q between a forward call and its
 * uncompute, so all-quantum is the fixed point of that drift and the one mask
 * at which the two passes emit the same number. Under I6(b) the whole SCRATCH
 * side is a function of W alone; the residual mask dependence is on the source
 * side only — P0's `CX(a[i] -> z[t])` and the two complement loops' `CX(b[k] ->
 * ~b[k])` — and that is what the palindrome case exercises.
 *
 * NO W=1 SPECIAL CASE, unlike K11's. K12.md §3.1a's last paragraph: with no
 * folds the closed forms are exact at W = 1, verified by hand-executing all
 * four loop bodies there. 39 gates over 11 qubits. */
static void closed_form(int W, int want_q, uint64_t *x, uint64_t *cx,
                        uint64_t *ccx)
{
    uint64_t w = (uint64_t)W;

    *x   = 4u * w * w + 4u * w;
    *cx  = 20u * w * w + (want_q ? 5u : 3u) * w;
    *ccx = 10u * w * w - 4u * w;
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

/* THE SHIPPED LADDER IS {i1, i8, i16, i32, i64, i128} — not 8/16/32/64, which
 * K12.md said until 2026-08-16 and which was a Rule 16 trap.
 * opcode_table.yaml:187-190 gives all four opcodes at `[i1,i8,i16,i32,i64,i128]`
 * with the full 15-variant grid, and docs/cqrt_census.txt:498-501 counts them
 * 6 x 15 = 90 symbols each on that basis. i80 is NOT a divrem width (yaml :85,
 * :133-135) — the mirror image of `icmp`'s fence. The exhaustive L1 widths are
 * pinned too, so every width the sweep runs is also a golden. */
CQ_TEST(l4_goldens)
{
    static const int WS[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64, 128 };
    cq_gold g;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/divrem_u.counts",
                      "M19 kernels/divrem_u.c — K12 udiv/urem (flat restoring "
                      "division over M16/M14/M17's exported step blocks, "
                      "sandwiched, all-quantum operands). SELF-PINNED: upstream "
                      "publishes no division gate count and could not be "
                      "compared to anyway — it widens to 64 bits",
                      CQOPS_BENNETT_COMMIT))
        return;

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        check_counts(&g, &UDIV, WS[i], 1);
        check_counts(&g, &UREM, WS[i], 0);
    }

    CHECK(cq_gold_close(&g));
}

#include "test_kernel_divrem_schedule.inc"

/* ---- The palindrome, and the compute half's exact length. ---------------- */

/* THE COMPUTE HALF'S GATE COUNT AT AN ARBITRARY MASK, WHICH IS NOT ITS SLOT
 * COUNT. Under I6(b) every scratch bit is CQ_BIT_Q from step 0, so nothing on
 * the scratch side can fold; the only operand bits that reach the emitter as
 * raw source controls are
 *
 *      P0            CX(a[i]  -> z[t])        once per iteration, one per bit of a
 *      P1 phase 1    CX(b[k]  -> nb[t][k])    W per iteration
 *      P2 phase 1    CX(b[k]  -> nsb[t][k])   W per iteration
 *
 * and everything else is scratch-controlled. A classical ZERO removes its gate;
 * a classical ONE removes NONE — `CX(ONE,t)` folds to `X(t)`, one gate for one
 * gate. That asymmetry is K09.md §3.3.1's rule, which had to be corrected three
 * times, and it is executed here rather than argued.
 *
 * NOTE WHAT THIS DELIBERATELY DOES NOT DO: derive the head from the recorded
 * total as `(n - W)/2`. That would assume the very structure the palindrome is
 * supposed to prove, and would still pass on a compute half that emitted the
 * wrong number of gates in a mirror-symmetric way. */
static size_t udiv_head(int W, int want_q, cq_ref_w va, cq_ref_w qa,
                        cq_ref_w vb, cq_ref_w qb)
{
    size_t n = (size_t)cq_divrem_steps(W, want_q);

    for (int i = 0; i < W; i++) {
        if (dr_zero_bit(va, qa, i)) n -= 1u;
        if (dr_zero_bit(vb, qb, i)) n -= 2u * (size_t)W;
    }
    return n;
}

/* [compute] [copy-out] [compute reversed], with the head length computed from
 * the mask rather than read off the stream.
 *
 * A GATE COUNT IS NOT A DEFENCE AGAINST R8 AND K12 IS THE MEASURED WITNESS FOR
 * THAT (K12.md §2.4b): replaying K12's forward list in reverse under the
 * pre-I6(b) rules, with `a` all quantum and `b` all CQ_BIT_ZERO, gives a
 * DIFFERENT gate multiset with the IDENTICAL total — 816 at W=8. L1 green, L4
 * green, circuit wrong, scratch dirty. Only an ORDERED comparison sees it,
 * which is this. The `b` all-ZERO row below is that exact mask. */
CQ_TEST(the_stream_is_a_palindrome_around_the_copyout)
{
    static const int WS[] = { 1, 2, 3, 4, 5, 8 };
    const cq_kd_spec *KS[2] = { &UDIV, &UREM };
    cq_mock m;

    cq_mock_init(&m);

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++)
        for (int q = 0; q < 2; q++) {
            const cq_kd_spec *k = KS[q];
            int W = WS[i], want_q = 1 - q;
            cq_ref_w all = cq_ref_w_ones(W);
            cq_ref_w none = cq_ref_w_zero();
            cq_ref_w seven = cq_ref_w_make(7u, 0u, W);
            size_t steps = (size_t)cq_divrem_steps(W, want_q);
            size_t head;

            /* All-quantum: no fold can fire, so gates == slots. This is the
             * mask L4 pins and the one place the two counts must agree. */
            dr_run_masked(k, W, all, all, seven, all, &m);
            head = udiv_head(W, want_q, all, all, seven, all);
            CHECK_EQ(head, steps);
            CHECK(cq_mock_is_palindrome(&m, head, (size_t)W));
            CHECK_EQ(cq_mock_count(&m), 2u * head + (size_t)W);

            /* `a` all quantum, `b` all CQ_BIT_ZERO — risk R8's mandated
             * asymmetric witness, which no symmetric mask set contains, and the
             * mask K12.md §2.4b names. Both complement loops vanish entirely:
             * 2W CX per iteration, 2W^2 over the kernel. */
            dr_run_masked(k, W, all, all, none, none, &m);
            head = udiv_head(W, want_q, all, all, none, none);
            CHECK_EQ(head, steps - 2u * (size_t)W * (size_t)W);
            CHECK(cq_mock_is_palindrome(&m, head, (size_t)W));
            CHECK_EQ(cq_mock_count(&m), 2u * head + (size_t)W);

            /* `b` all-ones and all-CLASSICAL keeps every gate, rewriting each
             * CX as an X rather than folding it away — so this head is the
             * all-quantum head exactly. An L5 assertion written with `<` fails
             * here, which is K09.md §3.3.1's corrected rule. */
            dr_run_masked(k, W, all, all, all, none, &m);
            head = udiv_head(W, want_q, all, all, all, none);
            CHECK_EQ(head, steps);
            CHECK(cq_mock_is_palindrome(&m, head, (size_t)W));

            /* A narrow quantum value in a wide register — the typical CQ_lang
             * shape. `a` classical zero above the LSB removes W-1 shift-in CXs. */
            dr_run_masked(k, W, cq_ref_w_setbit(0), cq_ref_w_setbit(0),
                          seven, all, &m);
            head = udiv_head(W, want_q, cq_ref_w_setbit(0), cq_ref_w_setbit(0),
                             seven, all);
            CHECK_EQ(head, steps - (size_t)(W - 1));
            CHECK(cq_mock_is_palindrome(&m, head, (size_t)W));
        }

    cq_mock_dispose(&m);
}

/* ---- The scratch is taken, and it is given back. ------------------------- */

/* K12.md §4: `8W²+4W-1` with the quotient and `8W²+3W-1` without — the
 * remainder tape (W²+2W-1), W inner blocks of 7W+1, and the quotient register.
 * A function of W ALONE, because pre-materialisation does not consult the
 * operand kinds, which is exactly what makes it assertable as a closed form.
 *
 * `owned` and `peak` MUST differ here, and that is the point: cq_kd_peak
 * measures the high-water mark DURING the call, which is the only instrument
 * that can see scratch that was taken and tidily given back. L2 looks after the
 * call and cannot.
 *
 * THIS IS ALSO WHERE THE COST OF D9(a) IS ON THE RECORD. 33,023 qubits at i64
 * and 131,583 at i128 — the largest object in the v1 catalogue by 8x, against
 * the LINEAR schedule's 1,667, which was built and measured and deliberately
 * not taken in v1 (K12.md §4.1, bd 5zn). */
CQ_TEST(the_sandwich_takes_its_scratch_and_gives_it_back)
{
    static const int WS[] = { 1, 2, 3, 4, 8, 16, 32, 64, 128 };

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        uint64_t w = (uint64_t)W;
        uint32_t peak = 0;

        CHECK_EQ(cq_kd_peak(&UDIV, W, &peak), (uint32_t)W);
        CHECK_EQ(peak, (uint32_t)(8u * w * w + 5u * w - 1u));   /* region + dst */
        CHECK_EQ(cq_divrem_region(W, 1), (int)(8u * w * w + 4u * w - 1u));

        CHECK_EQ(cq_kd_peak(&UREM, W, &peak), (uint32_t)W);
        CHECK_EQ(peak, (uint32_t)(8u * w * w + 4u * w - 1u));
        CHECK_EQ(cq_divrem_region(W, 0), (int)(8u * w * w + 3u * w - 1u));
    }
}

/* ---- D3: division by zero. ---------------------------------------------- */

/* NEVER TRAPS, AND THE VALUES ARE INHERITED RATHER THAN CHOSEN.
 * `_soft_udiv_compile(a, 0) = typemax` because the trial `r >= 0` always
 * succeeds, and `_soft_urem_compile(a, 0) = a` because every trial subtract is
 * a no-op — divider.jl:15-18 and :44-46, pinned upstream by
 * test_salb_div_by_zero.jl:33-34 and :52-66.
 *
 * THIS IS THE ONE PLACE IN libcqops WHERE "FAIL LOUD" WOULD BE WRONG. The
 * circuit is a fixed permutation and `b = 0` is just another input: there is no
 * branch, no error path and nothing to fail about. Rule 6's hard error is about
 * DIRTY RAILS, not about poison values; D3 is deterministic-but-unspecified,
 * which is a value contract rather than an error contract.
 *
 * Driven on QUANTUM operands, not through the classical fold, so it is the
 * CIRCUIT that is asserted to reproduce the inherited values. */
CQ_TEST(d3_division_by_zero_never_traps_and_returns_the_inherited_values)
{
    static const int WS[] = { 1, 2, 3, 4, 8, 16 };
    cq_mock m;

    cq_mock_init(&m);

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        cq_ref_w all = cq_ref_w_ones(W);
        cq_ref_w zero = cq_ref_w_zero();

        /* dr_run_masked checks dst against the reference, and the reference
         * carries the same inherited contract — so this states the VALUES
         * explicitly too, rather than only that the two models agree. */
        for (uint64_t a = 0; a < 4u; a++) {
            cq_ref_w va = cq_ref_w_make(a, 0u, W);

            CHECK(cq_ref_w_eq(cq_ref_w_udiv(va, zero, W), all));
            CHECK(cq_ref_w_eq(cq_ref_w_urem(va, zero, W), va));

            dr_run_masked(&UDIV, W, va, all, zero, all, &m);
            dr_run_masked(&UREM, W, va, all, zero, all, &m);
        }
    }

    cq_mock_dispose(&m);
}

/* ---- R9: the all-classical short-circuit. ------------------------------- */

CQ_TEST(r9_all_classical_operands_never_enter_the_sandwich)
{
    dr_classical_case(&UDIV, 8, 0u,   0u,   255u);   /* D3: a/0 = 2^W-1     */
    dr_classical_case(&UREM, 8, 0u,   0u,   0u);     /* D3: a%0 = a         */
    dr_classical_case(&UDIV, 8, 200u, 0u,   255u);
    dr_classical_case(&UREM, 8, 200u, 0u,   200u);
    dr_classical_case(&UDIV, 8, 200u, 7u,   28u);
    dr_classical_case(&UREM, 8, 200u, 7u,   4u);
    dr_classical_case(&UDIV, 8, 255u, 255u, 1u);
    dr_classical_case(&UREM, 8, 255u, 255u, 0u);
    dr_classical_case(&UDIV, 8, 7u,   200u, 0u);     /* a < b               */
    dr_classical_case(&UREM, 8, 7u,   200u, 7u);
    dr_classical_case(&UDIV, 8, 128u, 1u,   128u);   /* the top lane alone  */
    dr_classical_case(&UREM, 8, 128u, 128u, 0u);
    dr_classical_case(&UDIV, 1, 1u,   1u,   1u);
    dr_classical_case(&UREM, 1, 1u,   0u,   1u);
}

/* THE OTHER SIDE OF THE SAME FOLD, AND NO SWEEP CASE REACHES IT. The driver
 * always mints `dst` all-BIT_ZERO, so every all-classical case above lands on
 * cq_bit_flip_const and emits nothing. With `dst` already on qubits the same
 * path must emit a REAL X per set bit of the result — which is what makes the
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

    int32_t ha = cq_bk_reg_w(&ctx, 8u, cq_ref_w_make(200u, 0u, 8), cq_ref_w_zero());
    int32_t hb = cq_bk_reg_w(&ctx, 8u, cq_ref_w_make(7u, 0u, 8),   cq_ref_w_zero());
    /* |0> on eight qubits — a rail a previous kernel could well have left. */
    int32_t hd = cq_bk_reg_w(&ctx, 8u, cq_ref_w_zero(), cq_ref_w_ones(8));

    cq_count_reset(&cnt);
    cq_kernel_udiv(&ctx, cq_reg_bits(&ctx.regs, hd), cq_reg_cbits(&ctx.regs, ha),
                   cq_reg_cbits(&ctx.regs, hb), 8);

    CHECK_EQ(cq_pc_value(&ctx, hd), 28u);            /* 200 / 7 */
    CHECK_GATES(cnt.x, cnt.cx, cnt.ccx, 3, 0, 0);    /* popcount(28) = 3 */

    cq_ctx_dispose(&ctx);
}

#include "test_kernel_divrem_refmodel.inc"

CQ_TEST_MAIN_ARGV(
    CQ_CASE(the_three_inner_blocks_compose_to_k12s_per_iteration_tuple),
    CQ_CASE(the_compute_half_is_w_measured_inner_iterations),
    CQ_CASE(l4_goldens),
    CQ_CASE(the_phase_boundaries_match_an_independent_slot_scan),
    CQ_CASE(the_stream_is_a_palindrome_around_the_copyout),
    CQ_CASE(the_sandwich_takes_its_scratch_and_gives_it_back),
    CQ_CASE(d3_division_by_zero_never_traps_and_returns_the_inherited_values),
    CQ_CASE(r9_all_classical_operands_never_enter_the_sandwich),
    CQ_CASE(the_classical_fold_writes_real_gates_into_a_quantum_dst),
    CQ_CASE(l1s_oracle_agrees_with_the_hardware_divider),
    CQ_CASE(k12_unsigned_sweep)
)
