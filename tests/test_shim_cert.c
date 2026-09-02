/* tests/test_shim_cert.c — D15's OBSERVED UNDO CERTIFICATE, Step 23 landing 2.
 *
 * THE SEAM WAS RECORDED IN IMPLEMENTATION_PLAN §3 BEFORE THIS FILE'S FIRST CASE
 * WAS WRITTEN (Rule 12: a split is scheduled, never improvised, and the test
 * side of landing 2 had no seam recorded anywhere): `the ENGINE ↔ the ENTRY
 * CONDITIONS` → tests/test_shim_cert_rules.inc. THIS half is the engine — it
 * drives shim/cq_shim_reduce.c on synthesised histories and names no `cqrt_*`
 * entry point. The discriminator is whether a case would survive D15 losing
 * U1/U2/U3 and keeping only "the write history reduces to the identity": these
 * would; the `.inc`'s are exactly the ones that would not.
 *
 * WHY THE ENGINE IS TESTED SEPARATELY AT ALL, rather than only through real
 * calls. The engine's failures are ORDER failures, and the corpus does not
 * contain the orders that break it. D15's headline example is `A B B A`
 * reordered to `A B A B`: every quantity a PARITY rule inspects is
 * bit-identical between them and the second leaves the rail at `h1 ⊕ h2`.
 * Upstream had that exact bug and fixed it on 2026-08-07 with four dedicated
 * guards after a live miscompile. A suite that only drove `cq_template_*`
 * forward/`_unc` pairs would never build either shape, so it would test the
 * entry conditions and call that testing the engine.
 *
 * THESE CASES DRIVE THE RECORD DIRECTLY. That is deliberate and it is the one
 * place in the tree where a `cq_call_rec` is built by hand: the entry points cannot
 * produce an `A B A B` and cannot produce a paired call under two different §9
 * regions, because the shim refuses to nest and CQ_lang emits no controlled
 * template call at all. Everything the entry points CAN produce is in the
 * `.inc`, driven through the real symbols. */
#include "cq_shim.h"
#include "cq_shim_ctx.h"
#include "cq_shim_proof.h"
#include "cq_shim_record.h"
#include "cq_shim_reduce.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "reg.h"
#include "rotate.h"
#include "sink.h"

#include "cqops/cqops.h"
#include "cq_runtime_abi.h"

#include "support/harness.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"

#include <stdlib.h>
#include <string.h>

/* tests/test_runtime_rail.c:fresh(), verbatim and for the same three reasons —
 * the selection and the context must both be known before the first
 * cq_shim_ctx(), which is what latches the sink. */
static void fresh(void)
{
    cq_sink_reset();
    unsetenv("CQOPS_SINK");
    cq_shim_ctx_reset();
}

static cq_ctx *open_with(cq_mock *m, cq_sink *s)
{
    cq_mock_init(m);
    *s = cq_mock_sink(m);
    fresh();
    cqops_set_sink(s);
    cq_reduce_reset_stats();
    return cq_shim_ctx();
}

static void close_with(cq_mock *m)
{
    cq_shim_ctx_reset();
    cqops_set_sink(NULL);
    cq_mock_dispose(m);
}

/* --- driving the record by hand ------------------------------------------ */

static void push(cq_rop op, int32_t h0, int32_t h1, int32_t h2,
                 uint64_t imm, double angle, int32_t ctrl, uint32_t tag)
{
    cq_call_rec c;
    memset(&c, 0, sizeof c);
    c.op = (uint16_t)op;
    c.h[0] = h0; c.h[1] = h1; c.h[2] = h2; c.h[3] = CQ_REG_NONE;
    c.imm = imm; c.ctrl = ctrl; c.tag = tag;
    memcpy(&c.angle, &angle, sizeof c.angle);
    cq_rec_push(&c);
}

/* Mint `h` at |0> and return the position the reduction should start from. */
static uint32_t born(int32_t h)
{
    cq_rec_mint(h, 8u, 0u, 0u, 0);
    return cq_rec_hist(h)->birth_pos;
}

#define RED(h, lo)  cq_reduce_to_identity((h), (lo), cq_rec_len())

/* -------------------------------------------------------------------------
 * 1. Pairing: what cancels, what does not, and the ORDER claim.
 * ------------------------------------------------------------------------- */

CQ_TEST(a_self_inverse_pair_on_unchanged_operands_reduces)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    uint32_t b;
    (void)ctx;

    b = born(1);
    push(CQ_ROP_X, 1, -1, -1, 0u, 0.0, -1, 0u);
    push(CQ_ROP_X, 1, -1, -1, 0u, 0.0, -1, 0u);
    CHECK(RED(1, b));

    /* AND AN ODD NUMBER DOES NOT. An unpaired write is PUSHED, never dropped —
     * a non-empty stack at the end is a decline, and this is exactly where a
     * deleted uncompute fails. */
    push(CQ_ROP_X, 1, -1, -1, 0u, 0.0, -1, 0u);
    CHECK(!RED(1, b));

    close_with(&m);
}

CQ_TEST(abba_reduces_and_abab_does_not_although_every_parity_agrees)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    uint32_t b1, b2;
    (void)ctx;

    /* THIS IS THE CASE THE WHOLE ENGINE EXISTS FOR, and it is the one D15 §2
     * names: "reordering a reverse pass from A B B A to A B A B leaves every
     * quantity the parity rule inspects bit-identical while leaving the rail at
     * h1 ⊕ h2". Both streams below contain exactly two copies from h2 and two
     * from h3 into the rail; every count, every multiset and every per-operand
     * parity is identical. Only the ORDER differs.
     *
     * The nesting is what makes A B B A reduce: the inner B B pair cancels
     * first (the scan is nearest-adjoint-first), and the outer A A pair is then
     * adjacent. In A B A B neither pair is ever adjacent, and the intervening
     * write does not commute — a copy carries no control, so `commutes` has no
     * flag to find complementary branches of. */
    b1 = born(1);
    push(CQ_ROP_COPY, 2, 1, -1, 0u, 0.0, -1, 0u);   /* A */
    push(CQ_ROP_COPY, 3, 1, -1, 0u, 0.0, -1, 0u);   /* B */
    push(CQ_ROP_COPY, 3, 1, -1, 0u, 0.0, -1, 0u);   /* B */
    push(CQ_ROP_COPY, 2, 1, -1, 0u, 0.0, -1, 0u);   /* A */
    CHECK(RED(1, b1));

    b2 = born(4);
    push(CQ_ROP_COPY, 2, 4, -1, 0u, 0.0, -1, 0u);   /* A */
    push(CQ_ROP_COPY, 3, 4, -1, 0u, 0.0, -1, 0u);   /* B */
    push(CQ_ROP_COPY, 2, 4, -1, 0u, 0.0, -1, 0u);   /* A */
    push(CQ_ROP_COPY, 3, 4, -1, 0u, 0.0, -1, 0u);   /* B */
    CHECK(!RED(4, b2));

    close_with(&m);
}

CQ_TEST(a_source_that_moved_between_the_halves_refuses_the_pair)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    uint32_t b;
    (void)ctx;

    /* UPSTREAM'S T2, ARRIVING AS A CONSEQUENCE RATHER THAN AS A RULE: "a gate
     * acts on its live sources; if one moved in between, the second call is not
     * the adjoint of the first." The engine gets it from
     * pair_operands_unchanged's READS loop, which recurses into the SOURCE's
     * own history — so the check is the same reduction, one level down.
     *
     * The source's intervening write is a single `x`, which cannot pair with
     * anything, so its own history does not reduce and the outer pair is
     * rejected. Note the failure is a REJECTION of the pair and not a crash:
     * the scan continues looking for another adjoint deeper in the stack, finds
     * none, and the two copies are left unpaired. */
    (void)born(2);
    b = born(1);
    push(CQ_ROP_COPY, 2, 1, -1, 0u, 0.0, -1, 0u);
    push(CQ_ROP_X,    2, -1, -1, 0u, 0.0, -1, 0u);   /* the source MOVES */
    push(CQ_ROP_COPY, 2, 1, -1, 0u, 0.0, -1, 0u);
    CHECK(!RED(1, b));

    close_with(&m);
}

CQ_TEST(the_same_two_copies_reduce_when_the_source_is_left_alone)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    uint32_t b;
    (void)ctx;

    /* THE NEGATIVE CONTROL FOR THE CASE ABOVE, and it is not decoration: the
     * two streams differ by exactly one `x` on a rail that is not the target,
     * so without this case "the pair was refused" is indistinguishable from
     * "the engine refuses every pair of copies". */
    (void)born(2);
    b = born(1);
    push(CQ_ROP_COPY, 2, 1, -1, 0u, 0.0, -1, 0u);
    push(CQ_ROP_COPY, 2, 1, -1, 0u, 0.0, -1, 0u);
    CHECK(RED(1, b));

    close_with(&m);
}

CQ_TEST(a_rotation_pairs_only_with_its_exact_negation_bitwise)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    uint32_t b;
    (void)ctx;

    b = born(1);
    push(CQ_ROP_RY, 1, -1, -1, 0u,  0.7, -1, 0u);
    push(CQ_ROP_RY, 1, -1, -1, 0u, -0.7, -1, 0u);
    CHECK(RED(1, b));

    /* NOT A NEARBY ANGLE. The comparison is BITWISE — the sign bit flipped and
     * nothing else — which is this project's standing convention for angles
     * (mock_sink.h) and is a deliberate divergence from upstream, which parses
     * to a float and would call (0.0, -0.0) a negation. Here it does not. */
    (void)born(2);
    push(CQ_ROP_RY, 2, -1, -1, 0u,  0.7, -1, 0u);
    push(CQ_ROP_RY, 2, -1, -1, 0u, -0.7000000000000001, -1, 0u);
    CHECK(!RED(2, cq_rec_hist(2)->birth_pos));

    (void)born(3);
    push(CQ_ROP_RY, 3, -1, -1, 0u,  0.0, -1, 0u);
    push(CQ_ROP_RY, 3, -1, -1, 0u, -0.0, -1, 0u);
    CHECK(RED(3, cq_rec_hist(3)->birth_pos));   /* -0.0 IS the sign flip */

    close_with(&m);
}

CQ_TEST(a_diagonal_rotation_never_enters_a_write_history_at_all)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    uint32_t b;
    (void)ctx;

    /* PRD §15 D12, and upstream's `if not e.diagonal`, are the same fact: a
     * diagonal gate maps |v> to a phase times |v> and cannot move a
     * computational-basis value, so it cannot make a rail non-zero. An ODD
     * number of them therefore still reduces — which is the assertion, because
     * a model that recorded them would decline here. */
    b = born(1);
    push(CQ_ROP_RZ, 1, -1, -1, 0u, 0.3, -1, 0u);
    CHECK(RED(1, b));

    /* And it is the ROW that is diagonal, not the angle: the same lone Ry at
     * the same angle declines. */
    (void)born(2);
    push(CQ_ROP_RY, 2, -1, -1, 0u, 0.3, -1, 0u);
    CHECK(!RED(2, cq_rec_hist(2)->birth_pos));

    close_with(&m);
}

CQ_TEST(a_pair_split_across_two_control_regions_is_not_a_pair)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    uint32_t b;
    (void)ctx;

    /* D15 §6(i): an uncontrolled `_unc` after a CONTROLLED forward leaves `dst`
     * at `ctrl · f(a,b)`, not zero. THERE IS NO CORPUS WITNESS — the goldens
     * contain zero `cq_template_*_controlled` calls — so this guard is for
     * callers, not for CQ_lang, and this case is the only thing in the tree
     * that can see it. It is also unbuildable through the entry points: the
     * shim refuses to nest a region and CQ_lang never emits one, which is why
     * it is here rather than in the `.inc`. */
    b = born(1);
    push(CQ_ROP_TPL_FWD, 1, 2, 3, 0u, 0.0,  9, 77u);   /* under region h9 */
    push(CQ_ROP_TPL_UNC, 1, 2, 3, 0u, 0.0, -1, 77u);   /* uncontrolled    */
    CHECK(!RED(1, b));

    /* The negative control: the SAME two calls under the SAME region do pair. */
    (void)born(4);
    push(CQ_ROP_TPL_FWD, 4, 2, 3, 0u, 0.0, 9, 77u);
    push(CQ_ROP_TPL_UNC, 4, 2, 3, 0u, 0.0, 9, 77u);
    CHECK(RED(4, cq_rec_hist(4)->birth_pos));

    close_with(&m);
}

CQ_TEST(a_forward_pairs_only_with_its_own_opcode_s_uncompute)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    uint32_t b;
    (void)ctx;

    /* THE TAG IS WHAT MAKES A FORWARD AND AN `_unc` THE SAME OPERATION rather
     * than merely two calls naming the same rail. `add_i32(a,b) -> out` and
     * `sub_i32_unc(out,a,b)` name identical handles in identical slots; only
     * the tag separates them, and without it the certificate would clear a rail
     * that still holds `a + b`. */
    b = born(1);
    push(CQ_ROP_TPL_FWD, 1, 2, 3, 0u, 0.0, -1, 77u);
    push(CQ_ROP_TPL_UNC, 1, 2, 3, 0u, 0.0, -1, 78u);
    CHECK(!RED(1, b));

    close_with(&m);
}

#include "test_shim_cert_rules.inc"

CQ_TEST_MAIN(
    CQ_CASE(a_self_inverse_pair_on_unchanged_operands_reduces),
    CQ_CASE(abba_reduces_and_abab_does_not_although_every_parity_agrees),
    CQ_CASE(a_source_that_moved_between_the_halves_refuses_the_pair),
    CQ_CASE(the_same_two_copies_reduce_when_the_source_is_left_alone),
    CQ_CASE(a_rotation_pairs_only_with_its_exact_negation_bitwise),
    CQ_CASE(a_diagonal_rotation_never_enters_a_write_history_at_all),
    CQ_CASE(a_pair_split_across_two_control_regions_is_not_a_pair),
    CQ_CASE(a_forward_pairs_only_with_its_own_opcode_s_uncompute),
    CQ_CASE(the_cswap_co_written_slot_is_checked_and_it_is_a_miscompile_fix),
    CQ_CASE(complementary_flag_branches_commute_and_nothing_else_does),
    CQ_CASE(u1_discharges_a_template_forward_and_its_uncompute),
    CQ_CASE(u1_refuses_the_same_forward_with_no_uncompute),
    CQ_CASE(u2_discharges_a_self_inverse_bracket_on_an_alloc_zero_rail),
    CQ_CASE(u3_is_arithmetic_and_convicts_when_the_sum_is_not_zero),
    CQ_CASE(ckd18s_cancelled_rotation_pair_is_a_conviction_not_an_absence),
    CQ_CASE(a_read_after_a_rotation_is_unproven_and_upstream_lets_it_through),
    CQ_CASE(a_handle_the_recorder_never_saw_is_unproven_never_clean),
    CQ_CASE(the_d7b_temporary_is_discharged_rather_than_stranded),
    CQ_CASE(a_gate_on_the_complementary_branch_of_its_flag_is_not_the_adjoint),
    CQ_CASE(cqrt_cnot_controlled_reads_its_inner_control_and_writes_only_arg_two),
    CQ_CASE(a_constant_one_cswap_swaps_the_histories_with_the_contents),
    CQ_CASE(the_birth_value_is_read_at_the_full_register_width),
    CQ_CASE(u3_distinguishes_xorc_from_addc_and_the_two_differ_on_a_carry),
    CQ_CASE(a_fredkin_cswap_is_a_write_and_leaves_the_rail_unproven),
    CQ_CASE(the_effect_table_models_a_write_for_every_opcode)
)
