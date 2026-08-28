/* src/kernels/addacc.c — M15, Step 15. K8, the Cuccaro in-place accumulator.
 *
 * Read docs/constructions/K08.md and this module's header before changing
 * anything here. Every gate below is `lower_add_cuccaro!`
 * (third_party/bennett/src/adder.jl:64-146) transcribed in emission order.
 *
 * NAMES ARE INVERTED FROM BENNETT: our `acc` is his `b` (the accumulator that
 * receives the sum, adder.jl:62), our `b` is his `a` (the addend, restored),
 * our `x` is his `X[1]`. Read every citation below through that swap.
 *
 * ONE GATE PER STEP, AND IT IS FORCED (bd ckd.14a). K8 has no sandwich of its
 * own — it is clean — but its consumer K11 replays it inside one, and Rule 8's
 * driver reverses STEP order while re-calling compute(env, s) with the same
 * index. A step therefore has to be an involution, and a single X/CX/CCX is.
 * The natural reading — one MAJ or one UMA per step — is not: re-running a MAJ
 * from its own post-state does not restore it.
 *
 * THE W=2 BRANCH IS NOT A SECOND TRANSCRIPTION. adder.jl:84-96 prints it
 * separately, and read next to the W>=3 body at :98-145 it is that body with
 * the two FULL end blocks absent: at W=2 the "last middle MAJ" index W-2 IS 0,
 * so Bennett's "first MAJ, Toffoli dropped" (:84-86) and our block C are the
 * same two gates, and his "last UMA, matching Toffoli dropped" (:93-95) and our
 * block E are the same two. Sharing the blocks is what keeps the two paths from
 * drifting; splitting them would be two places to put the Toffoli in the wrong
 * phase. Verified step for step against both source branches — see the layout
 * table below and tests/test_kernel_addacc.c's stream assertions, which pin the
 * exact gate list at W=2 and W=3 against literals read off adder.jl.
 *
 * I6(a) IS *NOT* SATISFIED BY CONSTRUCTION HERE, UNLIKE EVERY OTHER KERNEL.
 * `b[i]` is a gate TARGET at blocks A, B, C, E, F and G — Bennett stores the
 * carry chain in the addend's wires (adder.jl:100) — so this module cannot lean
 * on cq_emit_*'s `const cq_bit *` controls the way add.c and mux.c do. The
 * Debug scratch-extent check in emit.c is the whole defence, plus
 * cq_addacc_check's refusal of any bit that is not already a qubit.
 */

#include "kernels/addacc.h"

#include "emit.h"
#include "kernels/kernel.h"

/* THE CARRY WIRE BELOW LANE `i`, and the one place the ancilla enters at all.
 * Bennett's chain runs c_0 -> X[1], then c_{i+1} into a[i]; 0-indexed that is
 * "lane i reads its incoming carry from b[i-1], except lane 0, which reads x".
 * Folding the special case in here rather than at six call sites is also what
 * makes the W=2 path fall out of the general blocks: there `W-2 == 0`, so the
 * carry wire the §3.5 blocks reach for IS the ancilla. */
static cq_bit *carry_below(const cq_addacc_block *k, int i)
{
    return i == 0 ? k->x : &k->b[i - 1];
}

/* MAJ — adder.jl:27-29's documented block, one gate at a time:
 *     CNOT(a[i], b[i]);  CNOT(a[i], c);  Toffoli(c, b[i], a[i])
 * taking (c_i, b_i, a_i) to (c_i^a_i, b_i^a_i, c_{i+1}), with the outgoing
 * carry written into the ADDEND's wire. `c` is carry_below(i). */
static void maj(cq_ctx *ctx, const cq_addacc_block *k, int i, int phase)
{
    cq_bit *c = carry_below(k, i);

    switch (phase) {
    case 0:  cq_emit_cx (ctx, &k->b[i],          &k->acc[i]); break;
    case 1:  cq_emit_cx (ctx, &k->b[i],          c);          break;
    default: cq_emit_ccx(ctx, c, &k->acc[i],     &k->b[i]);    break;
    }
}

/* UMA — adder.jl:134-138, and it is MAJ's three phases in reverse order rather
 * than a different block. That is not a coincidence to tidy away: it is why
 * Bennett's construction unwinds the carry chain at all, and it is the reason
 * the §3.5 optimisation can drop a MAJ Toffoli and its UMA partner as a pair. */
static void uma(cq_ctx *ctx, const cq_addacc_block *k, int i, int phase)
{
    cq_bit *c = carry_below(k, i);

    switch (phase) {
    case 0:  cq_emit_ccx(ctx, c, &k->acc[i],     &k->b[i]);    break;
    case 1:  cq_emit_cx (ctx, &k->b[i],          c);           break;
    default: cq_emit_cx (ctx, c,                 &k->acc[i]);  break;
    }
}

/* Phase 2 — the high sum bit, carrying the Toffoli §3.5 RELOCATED out of the
 * top MAJ (adder.jl:123-125). The two CNOTs contribute b_W ^ a_W ^ a_{W-1} and
 * the Toffoli adds MAJ(a_{W-1}, b_{W-1}, c_{W-1}) ^ a_{W-1}, whose spurious
 * a_{W-1} term cancels the one the CNOTs left — so what lands is c_W. This is
 * the block that makes the wires a[W-1] (our b[W-1]) keep their input value
 * across the boundary instead of briefly holding the top carry. */
static void high_sum(cq_ctx *ctx, const cq_addacc_block *k, int phase)
{
    int W = k->W;

    switch (phase) {
    case 0:  cq_emit_cx (ctx, &k->b[W - 1], &k->acc[W - 1]); break;
    case 1:  cq_emit_cx (ctx, &k->b[W - 2], &k->acc[W - 1]); break;
    default: cq_emit_ccx(ctx, carry_below(k, W - 2), &k->acc[W - 2],
                              &k->acc[W - 1]);               break;
    }
}

/* The number of FULL middle blocks. Bennett's `for i in 2:(W-2)` and
 * `for i in (W-2):-1:2` both run W-3 times and are empty at W=3 — checked at
 * both degenerate edges (W=3 gives the empty ranges 2:1 and 1:-1:2, W=4 gives
 * one iteration each). At W<=2 there is no middle at all. */
static int middles(int W)
{
    return W >= 3 ? W - 3 : 0;
}

int cq_addacc_steps(int W)
{
    if (W <= 0) cq_kernel_die("addacc: width is not positive");

    /* 6W-5 at every W >= 1 — but at W=1 that is one CX and not the closed
     * form's (4W-2, 2W-3) split, which would be (2, -1). See the header. */
    return 6 * W - 5;
}

void cq_addacc_step(cq_ctx *ctx, const cq_addacc_block *k, int u)
{
    int W = k->W, m = middles(W), ends = W >= 3 ? 3 : 0;

    if (u < 0 || u >= cq_addacc_steps(W))
        cq_kernel_die("addacc: step index out of range");

    /* THE PRECONDITION IS CHECKED HERE AND NOWHERE ELSE, and the placement is
     * the point. A sandwiched caller never calls cq_kernel_addacc — it drives
     * steps — so a check at the whole-call entry point would be absent on the
     * one path that matters. Checking at u == 0 costs O(W) per accumulate
     * against O(W) gates, is unskippable, and still holds on the reverse pass,
     * where u == 0 arrives last and kinds are monotone (D6). */
    if (u == 0) cq_addacc_check(k);

    /* W=1 IS A RE-DERIVATION, NOT A PORT (K08.md §5 D1). adder.jl:66 sends
     * W<=1 to the OUT-OF-PLACE lower_add!, which allocates a fresh result plus
     * a carry wire, emits 3 CNOTs and returns a new register — that is not an
     * accumulator, and Bennett's own test says the formulas do not apply there.
     * In place, `acc += b mod 2` is `acc[0] ^= b[0]`: one CX, no ancilla. */
    if (W == 1) { cq_emit_cx(ctx, &k->b[0], &k->acc[0]); return; }

    /* Block layout, emission order. `m` = W-3, `ends` = 3 at W>=3 and 0 at
     * W=2, where blocks A and G are exactly what §3.5 removed.
     *
     *   A  ends      first MAJ at lane 0, FULL                    :102-104
     *   B  3m        middle MAJs, lanes 1..W-3, FULL              :107-111
     *   C  2         MAJ at lane W-2, Toffoli DROPPED (§3.5)      :116-117
     *   D  3         high sum, with the relocated Toffoli         :123-125
     *   E  2         UMA at lane W-2, Toffoli DROPPED (§3.5)      :130-131
     *   F  3m        middle UMAs, lanes W-3..1, FULL, DESCENDING  :134-138
     *   G  ends      last UMA at lane 0, FULL                     :141-143
     *
     * 2*ends + 6m + 7 = 6W-5 at W>=3, and 7 at W=2. */
    if (u < ends)               { maj(ctx, k, 0, u); return; }
    u -= ends;

    if (u < 3 * m)              { maj(ctx, k, 1 + u / 3, u % 3); return; }
    u -= 3 * m;

    /* C and E are the §3.5 pair: the dropped MAJ Toffoli is phase 2, and its
     * partner is the UMA's phase 0, so C runs phases {0,1} and E runs {1,2}. */
    if (u < 2)                  { maj(ctx, k, W - 2, u); return; }
    u -= 2;

    if (u < 3)                  { high_sum(ctx, k, u); return; }
    u -= 3;

    if (u < 2)                  { uma(ctx, k, W - 2, u + 1); return; }
    u -= 2;

    /* DESCENDING, and it is the half a transcription gets wrong silently: the
     * UMA chain must unwind the carries in the reverse of the order the MAJ
     * chain built them, or the wires it reads still hold a later carry. */
    if (u < 3 * m)              { uma(ctx, k, m - u / 3, u % 3); return; }
    u -= 3 * m;

    uma(ctx, k, 0, u);
}

void cq_addacc_check(const cq_addacc_block *k)
{
    int W = k->W;

    /* A DIFFERENT MESSAGE FROM cq_addacc_steps'S, DELIBERATELY. Both entry
     * points call cq_addacc_steps before they ever reach here, so on those
     * paths that guard fires first and this one is unreachable — a mutation
     * battery found this line surviving mutation to always-true for exactly
     * that reason, which is CLAUDE.md's "a guard a later (here: EARLIER) copy
     * of itself catches is a guard nobody has tested". The wording differs so a
     * death test can say WHICH layer spoke (the Step 7 FAIL_REGULAR_EXPRESSION
     * discriminator).
     *
     * CORRECTED 2026-08-16 (Step 16): this used to justify itself with "K11
     * calls cq_addacc_check directly, once per accumulate". M18 SHIPPED AND IT
     * DOES NOT — it reaches K8 only through cq_addacc_step, which runs the check
     * at u == 0, i.e. exactly once per accumulate anyway. Adding a second call
     * in mul.c would be a guard whose deletion turns no test red, which is the
     * shape this file elsewhere says to refuse. So the direct entry point has no
     * production caller today; it is kept as the K11-shaped API K08.md §4
     * specifies and is exercised by
     * tests/test_kernel_addacc_death.c:check_called_directly_with_a_zero_width. */
    if (W <= 0) cq_kernel_die("addacc: cq_addacc_check received a "
                              "non-positive width");
    if (!k->acc || !k->b || !k->x) cq_kernel_die("addacc: null operand");

    /* RANGES, NOT BASE POINTERS (kernels/kernel.h's argument). cq_scratch_span
     * hands out sub-arrays of one region, so a K11 layout that overlapped the
     * accumulator with a partial product by two lanes would pass a pointer
     * comparison. acc and b are BOTH targets here, so an overlap is two carry
     * chains sharing wires — not merely undefined, but silently wrong. */
    if (cq_kernel_overlap2(k->acc, W, k->b, W) ||
        cq_kernel_overlap2(k->acc, W, k->x, 1) ||
        cq_kernel_overlap2(k->b,   W, k->x, 1))
        cq_kernel_die("addacc: acc, b and x must be disjoint — the accumulator, "
                      "the addend and the ancilla are three distinct registers");

    /* EVERY BIT ALREADY A QUBIT, hard error in both configurations, and this is
     * the I6(b) precondition rather than a tidiness check. A classical b[i] is
     * a TARGET here and the fold table would materialise it mid-sequence — an X
     * on the forward pass that the reverse never emits, since kinds are
     * monotone (D6). A classical ZERO x is read as a CONTROL at block A phase 1
     * and folds to nothing, then is materialised at block G phase 1, so the
     * reverse emits what the forward folded away: that is risk R8's antecedent,
     * traced gate by gate in K11.md §2b. Both are silent — right value, dirty
     * scratch, green L1 — which is why this refuses rather than folds.
     *
     * THERE IS NO L5 SHORT-CIRCUIT HERE AND ADDING ONE WOULD BE A REGRESSION.
     * L5 is about opcodes CQ_lang emits, and K8's L5 lives in M26's WRAPPER
     * rather than here (PRD §15 D17): `shim/cq_runtime_rail.c`'s `rail_addc` is
     * reached from `cqrt_addc_i<W>`, folds an all-classical rail at zero cost,
     * and materialises every operand before calling in. (This comment said K8
     * "has no cqrt_* symbol, is never entered from the shim" until 2026-08-23,
     * when that caller landed.) A classical operand arriving HERE is a caller
     * bug, not a cheap case. */
    for (int i = 0; i < W; i++) {
        if (!cq_bit_is_qubit(k->acc[i]) || !cq_bit_is_qubit(k->b[i]))
            cq_kernel_die("addacc: every bit of acc and b must already be a "
                          "qubit — K8 targets its own addend, so a classical "
                          "bit materialises mid-construction and the reverse "
                          "replay stops cancelling (K08.md §2, risk R8)");
    }

    /* AT EVERY W, INCLUDING W=1 WHERE NO GATE TOUCHES IT. The uniform demand is
     * the cheaper rule: K11 carves one x[j] per Cuccaro call out of its region
     * whatever the width (K11.md §4), so nothing is being asked for that a
     * caller does not already have, and a W-dependent precondition is one more
     * edge for M18 to get wrong. */
    if (!cq_bit_is_qubit(k->x[0]))
        cq_kernel_die("addacc: the ancilla x must already be a qubit — it is "
                      "read as a control before it is ever written, so a "
                      "classical x folds those gates away on the forward pass "
                      "only (K11.md §2b's R8 witness)");
}

/* No cq_addacc_check here, DELIBERATELY. Step 0 makes it, and a second call
 * would be a guard whose deletion turns no test red — the shape CLAUDE.md says
 * to ask about before adding one. */
void cq_kernel_addacc(cq_ctx *ctx, const cq_addacc_block *k)
{
    int n = cq_addacc_steps(k->W);

    for (int u = 0; u < n; u++) cq_addacc_step(ctx, k, u);
}
