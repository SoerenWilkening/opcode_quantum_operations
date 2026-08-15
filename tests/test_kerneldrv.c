/* tests/test_kerneldrv.c — the Phase-B driver's own assertions, made
 * falsifiable. Step 10.
 *
 * THIS SUITE TESTS THE TEST INFRASTRUCTURE, and it exists because a mutation
 * battery over Step 10 measured the exact failure mode the project has already
 * refused once. Mutating each of these to always-true left every test green:
 *
 *     poolcheck.c  cq_pc_same            -> return 1     SURVIVED
 *     poolcheck.c  cq_pc_live_is_exactly -> return 1     SURVIVED
 *     poolcheck.c  cq_pc_indices_are_free-> return 1     SURVIVED
 *     kerneldrv.c  the source-kind loop  -> never runs   SURVIVED
 *     kerneldrv.c  the L5 zero-gate check-> disabled     SURVIVED
 *
 * They all work — injecting a real fault fires them — but nothing in the suite
 * had ever seen one fire, so nothing would have noticed if one stopped. These
 * are precisely the checks the Prime Directive says carry the weight, and
 * poolcheck.h's own header says "a suite that reports L1 green and never calls
 * in here has verified nothing that matters". The same argument applies one
 * level up: a suite that calls in here and never watches it refuse has
 * verified nothing about the refusing.
 *
 * IT IS test_harness_negative's ARGUMENT, GENERALISED. That file exists
 * because "a CHECK that could not fail would make every suite in the project
 * vacuously green, so that path is tested rather than assumed". So is this.
 *
 * TWO SHAPES, and the split is by whether the fault CHECKs or ABORTS. A fault
 * that makes the driver record a harness failure is provoked here and the
 * failure is asserted. A fault that makes the library abort — a dirty free,
 * an operand alias — cannot be caught by a running process and belongs in
 * tests/test_kernel_bitwise_death.c instead.
 */

#include "kernels/bitwise.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "qubits.h"
#include "reg.h"
#include "shadow.h"
#include "sink_count.h"

#include "support/bitkinds.h"
#include "support/harness.h"
#include "support/kerneldrv.h"
#include "support/poolcheck.h"
#include "support/refmodel.h"

/* Runs `stmt`, which is expected to make the driver record at least one
 * harness failure, and fails THIS case if it did not.
 *
 * The mute is turned off on every path, including the failing one, and the
 * count is taken before the failure is reported — so a provocation that
 * records nothing still leaves the harness in a working state, and a mute can
 * never be left on to silence the rest of the run. */
#define CQ_EXPECT_CAUGHT(what, stmt)                                          \
    do {                                                                      \
        cq_h_mute(1);                                                         \
        (void)cq_h_take_failures();                                           \
        stmt;                                                                 \
        int cq_n_ = cq_h_take_failures();                                     \
        cq_h_mute(0);                                                         \
        if (cq_n_ == 0)                                                       \
            cq_h_fail(__FILE__, __LINE__,                                     \
                      "THE DRIVER DID NOT CATCH: %s — the assertion that "    \
                      "should have fired is now vacuous", what);              \
    } while (0)

/* And the negative control for the macro itself: something that must NOT be
 * caught. Without this, a CQ_EXPECT_CAUGHT that counted the wrong thing —
 * or a driver that failed every case — would look like perfect coverage. */
#define CQ_EXPECT_CLEAN(what, stmt)                                           \
    do {                                                                      \
        cq_h_mute(1);                                                         \
        (void)cq_h_take_failures();                                           \
        stmt;                                                                 \
        int cq_n_ = cq_h_take_failures();                                     \
        cq_h_mute(0);                                                         \
        if (cq_n_ != 0)                                                       \
            cq_h_fail(__FILE__, __LINE__,                                     \
                      "the driver rejected a CORRECT kernel (%s): %d "        \
                      "failure(s)", what, cq_n_);                             \
    } while (0)

/* ---- Deliberately broken kernels, one property violated each. ----------- */

/* Correct, as the control. */
static void k_good(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, const cq_bit *b,
                   int W)
{
    cq_kernel_xor(ctx, dst, a, b, W);
}

/* L1: drops b's contribution, so dst = a rather than a ^ b. Chosen because it
 * disturbs NOTHING else — the pool, the sources and the uncompute round trip
 * are all still perfect, so L1 is the only assertion that can see it. */
static void k_wrong_value(cq_ctx *ctx, cq_bit *dst, const cq_bit *a,
                          const cq_bit *b, int W)
{
    (void)b;
    for (int i = 0; i < W; i++) cq_emit_cx(ctx, &a[i], &dst[i]);
}

/* L2: correct value, correct uncompute, one qubit taken and never returned.
 * This is the leaked ancilla — the one unforgivable bug (NORTH_STAR §3) — and
 * no value-shaped assertion anywhere can see it. */
static void k_leaks_an_ancilla(cq_ctx *ctx, cq_bit *dst, const cq_bit *a,
                               const cq_bit *b, int W)
{
    cq_kernel_xor(ctx, dst, a, b, W);
    (void)cq_ctx_fresh_qubit(ctx);
}

/* The source-kind check: materialises a source bit. The value is still right
 * and the qubit is still owned by the source register, so L1 and L2 both stay
 * green — only the kind comparison sees it. Casting away `const` is exactly
 * what emit.h's signature makes impossible for real code (Rule 8), which is
 * why the fault has to be manufactured here. */
static void k_materialises_a_source(cq_ctx *ctx, cq_bit *dst, const cq_bit *a,
                                    const cq_bit *b, int W)
{
    /* BOTH sources are searched, and that is not defensive padding — the first
     * draft searched only `a` and the case went red, because under the mask it
     * was called with (`a` quantum, `b` classical) `a` had no constant bit to
     * take and the fault was never injected at all. A provocation that fails
     * to provoke reports the assertion as vacuous, which is the right answer to
     * the question it asked and the wrong diagnosis of the code. */
    cq_bit *va = (cq_bit *)(uintptr_t)a;
    cq_bit *vb = (cq_bit *)(uintptr_t)b;

    for (int i = 0; i < W; i++) {
        if (cq_bit_is_const(va[i])) { cq_materialise(ctx, &va[i]); break; }
        if (cq_bit_is_const(vb[i])) { cq_materialise(ctx, &vb[i]); break; }
    }

    cq_kernel_xor(ctx, dst, a, b, W);
}

/* THE FAULT ONLY L2's SET CHECK CAN SEE, and the one this suite was missing.
 * A mutation battery deleted ALL THREE cq_pc_live_is_exactly calls at once and
 * the suite stayed green — including the case named l2_catches_a_leaked_ancilla,
 * which was in fact being satisfied by L3's cq_pc_same. The reason is that
 * k_leaks_an_ancilla moves the live COUNT, and a count is all cq_pc_same is.
 *
 * This one nets to zero: it acquires one ancilla AND releases one qubit
 * belonging to a SOURCE. `live` is unchanged, every value is right, the
 * uncompute round trip is perfect — and the source register now names an index
 * sitting on the free list while an unowned ancilla is live. I2 and I3 are both
 * lies at that instant. The description was already written in kerneldrv.c's
 * own comment; it had just never been instantiated. */
static void k_swaps_an_ancilla_for_a_source(cq_ctx *ctx, cq_bit *dst,
                                            const cq_bit *a, const cq_bit *b,
                                            int W)
{
    cq_kernel_xor(ctx, dst, a, b, W);

    for (int i = 0; i < W; i++)
        if (cq_bit_is_qubit(a[i]) &&
            cq_shadow_known_zero(&ctx->shadow, cq_bit_qindex(a[i]))) {
            (void)cq_ctx_fresh_qubit(ctx);                  /* +1 live */
            cq_ctx_release_qubit(ctx, cq_bit_qindex(a[i]), 1);  /* -1 live */
            return;
        }
}

/* L5: emits a gate even when every operand bit is classical. The value stays
 * right — the extra qubit is allocated, flipped twice and left at zero — so
 * this is the R9 shape, a classical operation that quietly costs qubits. */
static void k_costs_on_the_classical_path(cq_ctx *ctx, cq_bit *dst,
                                          const cq_bit *a, const cq_bit *b,
                                          int W)
{
    cq_bit scratch = cq_bit_zero();

    cq_materialise(ctx, &scratch);
    cq_emit_x(ctx, &scratch);
    cq_emit_x(ctx, &scratch);
    cq_ctx_release_qubit(ctx, cq_bit_qindex(scratch), 1);

    cq_kernel_xor(ctx, dst, a, b, W);
}

/* W=2 masks, spelled two-word because that is what a mask is at every width
 * now (bitkinds.h). The high word is 0 below 64 bits. */
#define M2(lo) { (uint64_t)(lo), 0u }
static const cq_bk_pair QQ = { { M2(0x3), M2(0x3) }, "all-quantum" };
static const cq_bk_pair CC = { { M2(0x0), M2(0x0) }, "all-classical" };
static const cq_bk_pair QC = { { M2(0x3), M2(0x0) }, "a-quantum/b-classical" };

/* ---- The driver refuses each fault, and accepts a correct kernel. ------- */

CQ_TEST(the_driver_accepts_a_correct_kernel)
{
    const cq_kd_spec good = { "good", k_good, cq_ref_xor, NULL, NULL, NULL };

    CQ_EXPECT_CLEAN("all-quantum",   cq_kd_case2(&good, 2, 0x2u, 0x1u, &QQ));
    CQ_EXPECT_CLEAN("all-classical", cq_kd_case2(&good, 2, 0x2u, 0x1u, &CC));
    CQ_EXPECT_CLEAN("mixed",         cq_kd_case2(&good, 2, 0x2u, 0x1u, &QC));
}

CQ_TEST(l1_catches_a_wrong_value)
{
    const cq_kd_spec bad = { "wrong-value", k_wrong_value, cq_ref_xor, NULL, NULL, NULL };

    CQ_EXPECT_CAUGHT("dst = a instead of a ^ b, all-quantum",
                     cq_kd_case2(&bad, 2, 0x2u, 0x1u, &QQ));
    CQ_EXPECT_CAUGHT("dst = a instead of a ^ b, all-classical",
                     cq_kd_case2(&bad, 2, 0x2u, 0x1u, &CC));
}

CQ_TEST(l2_catches_a_leaked_ancilla)
{
    const cq_kd_spec bad = { "leaky", k_leaks_an_ancilla, cq_ref_xor, NULL, NULL, NULL };

    /* Right value, right uncompute, one qubit stranded. */
    CQ_EXPECT_CAUGHT("a qubit taken and never returned",
                     cq_kd_case2(&bad, 2, 0x2u, 0x1u, &QQ));
}

/* The provocation that discriminates L2 from L3. `a` is all-quantum with value
 * 0, so one of its qubits is provably |0> and can be released without the
 * pool's own guard firing — which is what makes the swap invisible to every
 * count in the driver. */
CQ_TEST(l2_catches_a_leak_that_nets_to_zero)
{
    const cq_kd_spec bad = { "swapper", k_swaps_an_ancilla_for_a_source,
                             cq_ref_xor, NULL, NULL, NULL };

    CQ_EXPECT_CAUGHT("an ancilla swapped for a source's qubit — live is "
                     "UNCHANGED, so only the set check can see it",
                     cq_kd_case2(&bad, 2, 0x0u, 0x1u, &QQ));
}

CQ_TEST(the_kind_check_catches_a_materialised_source)
{
    const cq_kd_spec bad = { "eats-a-source", k_materialises_a_source, cq_ref_xor, NULL, NULL, NULL };

    /* b is classical here, so there is a constant bit to materialise. */
    CQ_EXPECT_CAUGHT("a source bit turned into a qubit",
                     cq_kd_case2(&bad, 2, 0x2u, 0x1u, &QC));
}

CQ_TEST(l5_catches_a_cost_on_the_classical_path)
{
    const cq_kd_spec bad = { "not-free", k_costs_on_the_classical_path, cq_ref_xor, NULL, NULL, NULL };

    CQ_EXPECT_CAUGHT("gates and a qubit on fully classical operands",
                     cq_kd_case2(&bad, 2, 0x2u, 0x1u, &CC));
}

/* ---- The poolcheck helpers refuse, directly. ---------------------------- */

typedef struct { cq_ctx ctx; cq_counter cnt; cq_sink sink; } pc_fx;

static void pc_open(pc_fx *f)
{
    cq_count_reset(&f->cnt);
    f->sink = cq_sink_counter(&f->cnt);
    cq_ctx_init(&f->ctx, &f->sink);
}

CQ_TEST(cq_pc_same_sees_a_changed_live_count)
{
    pc_fx f;
    pc_open(&f);

    cq_pc_snap before = cq_pc_take(&f.ctx);
    int32_t h = cq_bk_reg(&f.ctx, 4u, 0xFu, 0xFu);
    cq_pc_snap after = cq_pc_take(&f.ctx);

    CHECK(!cq_pc_same(before, after));       /* four qubits appeared */
    CHECK(cq_pc_same(after, after));         /* and the trivial case holds */
    CHECK_EQ(cq_reg_owned_qubits(&f.ctx.regs, h), 4);

    cq_ctx_dispose(&f.ctx);
}

CQ_TEST(cq_pc_live_is_exactly_sees_an_unowned_qubit)
{
    pc_fx f;
    pc_open(&f);

    int32_t h = cq_bk_reg(&f.ctx, 4u, 0xFu, 0xFu);
    int32_t hs[1] = { h };

    CQ_EXPECT_CLEAN("every live qubit is owned",
                    CHECK(cq_pc_live_is_exactly(&f.ctx, hs, 1)));

    /* One qubit live that no named register owns — a leaked ancilla, seen from
     * the pool rather than from the kernel. */
    (void)cq_ctx_fresh_qubit(&f.ctx);

    CQ_EXPECT_CAUGHT("an unowned live qubit",
                     CHECK(cq_pc_live_is_exactly(&f.ctx, hs, 1)));

    cq_ctx_dispose(&f.ctx);
}

CQ_TEST(cq_pc_indices_are_free_sees_an_index_that_did_not_come_back)
{
    pc_fx f;
    uint32_t idx[4];
    pc_open(&f);

    int32_t h = cq_bk_reg(&f.ctx, 4u, 0x0u, 0xFu);   /* value 0: freeable */
    uint32_t n = cq_pc_indices(&f.ctx, h, idx, 4u);
    CHECK_EQ(n, 4);

    /* Still live: not one of them is on the free list yet. */
    CQ_EXPECT_CAUGHT("indices that are still live",
                     CHECK(cq_pc_indices_are_free(&f.ctx, idx, n)));

    cq_reg_free(&f.ctx, h, cq_pc_zero_proof_rotation_free);

    CQ_EXPECT_CLEAN("indices returned by the free",
                    CHECK(cq_pc_indices_are_free(&f.ctx, idx, n)));

    cq_ctx_dispose(&f.ctx);
}

CQ_TEST_MAIN(
    CQ_CASE(the_driver_accepts_a_correct_kernel),
    CQ_CASE(l1_catches_a_wrong_value),
    CQ_CASE(l2_catches_a_leaked_ancilla),
    CQ_CASE(l2_catches_a_leak_that_nets_to_zero),
    CQ_CASE(the_kind_check_catches_a_materialised_source),
    CQ_CASE(l5_catches_a_cost_on_the_classical_path),
    CQ_CASE(cq_pc_same_sees_a_changed_live_count),
    CQ_CASE(cq_pc_live_is_exactly_sees_an_unowned_qubit),
    CQ_CASE(cq_pc_indices_are_free_sees_an_index_that_did_not_come_back)
)
