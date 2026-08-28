/* Step 23, landing 1, step 4: M26's rail surface — shim/cq_runtime_rail.c, the
 * 32 `cqrt_*` symbols that create, read, destroy and mutate a rail.
 *
 * WHAT THIS FILE IS FOR, subject by subject, and each subject is here because
 * something else would be green without it:
 *
 *   1. THE VENDORED ABI HEADER (tests/test_runtime_abi.inc).
 *      `shim/cq_runtime_abi.h` is a verified copy of CQ_lang's 173 declarations
 *      and is the ONLY compiler-checkable statement of the contract these 62
 *      definitions satisfy — `-Wmissing-prototypes` is not in this project's
 *      flag set, so without it a wrong return type compiles clean and is not a
 *      link error either. Its counts AND its names are checked against
 *      docs/cqrt_census.txt, which is independent of it.
 *
 *   2. THE LIFETIME: alloc / measure / free. Three slot-state transitions on a
 *      handle CQ_lang NAMED, and that qualifier is load-bearing: `rail_addc`
 *      reaches the same D15 free path twice, for two rails it mints and
 *      tombstones itself, so "the only part of the shim that touches D15's
 *      disposition" is false and was measured false.
 *      The free cases assert the VERDICT and the ACT separately, because under
 *      D15 §4 proven-dirty and unproven take the SAME act and a case asserting
 *      only "it stranded" cannot tell a refusal from ignorance.
 *
 *   3. THE CONTENTS: copy / copy_controlled / cswap / addc / xorc — in
 *      tests/test_runtime_rail_write.inc, on the seam IMPLEMENTATION_PLAN §3
 *      records for shim/cq_runtime_rail.c itself, `the rail's LIFETIME <-> the
 *      rail's CONTENTS`. Taken here before the .c needed it, on
 *      tests/test_shim_ctx.c's precedent: that both sides want the same cut is
 *      the argument for the seam.
 *
 * THE ARGUMENT-ORDER TRAP IS WHY
 * `copy_xors_the_source_into_the_destination_in_the_abis_own_order` EXISTS. The
 * ABI is `cqrt_copy_<W>(src, dst)` and libcqops is
 * `cq_reg_xor_into(ctx, dst, src)` — src/reg.h flags the reversal in prose —
 * and BOTH ARE int32_t. C ignores parameter names, so the vendored header
 * cannot see a transposition and neither can the link. It is pinned
 * BEHAVIOURALLY instead: which rail gained the wires.
 *
 * SUBJECT 1 LIVES IN tests/test_runtime_abi.inc, taken at the 300-line hard
 * limit; see that file for why it is a subject cut and not a size cut.
 */

#include "cq_runtime_abi.h"
#include "cq_shim_ctx.h"
#include "cq_shim_proof.h"

#include "bit.h"
#include "controlled.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/addacc.h"
#include "reg.h"
#include "rotate.h"
#include "sink.h"

#include "cqops/cqops.h"

#include "support/bitkinds.h"
#include "support/harness.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Every case starts from a known SELECTION state and a known CONTEXT, and both
 * must precede the first cq_shim_ctx() — that call is what latches the sink.
 * tests/test_shim_ctx.c:fresh(), verbatim, for the same three reasons. */
static void fresh(void)
{
    cq_sink_reset();
    unsetenv("CQOPS_SINK");
    cq_shim_ctx_reset();
}

/* The whole preamble of an ordinary case: a fresh context recording into `m`. */
static cq_ctx *open_with(cq_mock *m, cq_sink *s)
{
    cq_mock_init(m);
    *s = cq_mock_sink(m);
    fresh();
    cqops_set_sink(s);
    return cq_shim_ctx();
}

static void close_with(cq_mock *m)
{
    cq_shim_ctx_reset();
    cqops_set_sink(NULL);
    cq_mock_dispose(m);
}

/* -------------------------------------------------------------------------
 * 1. The vendored ABI header.
 * ------------------------------------------------------------------------- */

#include "test_runtime_abi.inc"

/* -------------------------------------------------------------------------
 * 2. alloc — the mint, and I4.
 * ------------------------------------------------------------------------- */

/* A FRESH RAIL COSTS NOTHING, AND THAT IS INVARIANT I4 RATHER THAN AN
 * OPTIMISATION: an all-constant register owns ZERO qubits, which is what keeps
 * the corpus's classical loop counters free at their free with no evidence at
 * all (PRD §15 D15's 40 addc/xorc rails). Zero gates AND zero qubits, at every
 * width, for every value including all-ones. */
CQ_TEST(alloc_mints_a_rail_of_the_symbols_own_width_at_zero_cost)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const cq_pc_snap before = cq_pc_take(ctx);

    const int32_t h1  = cqrt_alloc_i1(true);
    const int32_t h8  = cqrt_alloc_i8(0x7F);
    const int32_t h16 = cqrt_alloc_i16(-1);
    const int32_t h32 = cqrt_alloc_i32(123456789);
    const int32_t h64 = cqrt_alloc_i64(INT64_MIN);

    CHECK_EQ(cq_mock_count(&m), 0);                    /* zero gates          */
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));        /* zero qubits (I4)    */

    CHECK_EQ(cq_reg_width(&ctx->regs, h1),  1);
    CHECK_EQ(cq_reg_width(&ctx->regs, h8),  8);
    CHECK_EQ(cq_reg_width(&ctx->regs, h16), 16);
    CHECK_EQ(cq_reg_width(&ctx->regs, h32), 32);
    CHECK_EQ(cq_reg_width(&ctx->regs, h64), 64);

    CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, h64), 0);

    /* The I2 sweep, which needs no handle list — the five handles here are
     * enough to make it non-trivial and I4 keeps the pool empty, so the SET
     * check would only restate I4. */
    cq_reg_audit(ctx);

    close_with(&m);
}

/* THE ABI HANDS US A SIGNED VALUE AND THE REGISTER IS A BIT PATTERN, so the
 * conversion is `(uint64_t)(uintW_t)v` — reinterpret at the SAME width, then
 * widen zero-extended. The naive `(uint64_t)v` SIGN-EXTENDS: measured, int8_t
 * -56 becomes 0xFFFFFFFFFFFFFFC8 rather than 0xC8. It is harmless today only
 * because cq_bits_from_words reads exactly `width` bits — a load-bearing
 * accident (bd 216 (B)8) — so the mask is explicit here and the case pins the
 * bits rather than the total, at the widths where the two spellings differ. */
CQ_TEST(alloc_reads_a_negative_literal_as_its_own_widths_bit_pattern)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    CHECK_EQ(cq_pc_value(ctx, cqrt_alloc_i8(-56)),  0xC8);
    CHECK_EQ(cq_pc_value(ctx, cqrt_alloc_i8(-1)),   0xFF);
    CHECK_EQ(cq_pc_value(ctx, cqrt_alloc_i16(-2)),  0xFFFE);
    CHECK_EQ(cq_pc_value(ctx, cqrt_alloc_i32(-1)),  0xFFFFFFFFu);
    CHECK_EQ(cq_pc_value(ctx, cqrt_alloc_i1(false)), 0);
    CHECK_EQ(cq_pc_value(ctx, cqrt_alloc_i1(true)),  1);

    /* i64 is the one width where the two spellings AGREE, which is exactly why
     * it cannot stand in for the others. Pinned so the row is covered. */
    CHECK(cq_pc_value(ctx, cqrt_alloc_i64(-1)) == UINT64_C(0xFFFFFFFFFFFFFFFF));

    CHECK_EQ(cq_mock_count(&m), 0);
    close_with(&m);
}

/* -------------------------------------------------------------------------
 * 3. measure — the value, the gates, and the terminality.
 * ------------------------------------------------------------------------- */

/* THE RETURN TYPE IS THE ABI'S SIGNED READING OF A BIT PATTERN, not a
 * sign-extension: `cqrt_measure_i8` returns int8_t, which cannot hold one.
 * CQ_lang's own header settles it — "Width-only ABI: the runtime sees widths,
 * not signedness ... signedness is implicit in the LLVM opcode at the call
 * site" — and its trace stub cannot, because every measure body returns a
 * literal 0. So the round trip is the assertion: what alloc took in, measure
 * gives back, at the ABI's own type. */
CQ_TEST(measure_round_trips_every_width_at_the_abis_own_signed_type)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    (void)ctx;

    CHECK_EQ(cqrt_measure_i1 (cqrt_alloc_i1 (true)),  1);
    CHECK_EQ(cqrt_measure_i1 (cqrt_alloc_i1 (false)), 0);
    CHECK_EQ(cqrt_measure_i8 (cqrt_alloc_i8 (-56)),   -56);
    CHECK_EQ(cqrt_measure_i8 (cqrt_alloc_i8 (127)),   127);
    CHECK_EQ(cqrt_measure_i16(cqrt_alloc_i16(-2)),    -2);
    CHECK_EQ(cqrt_measure_i32(cqrt_alloc_i32(-1)),    -1);
    CHECK_EQ(cqrt_measure_i32(cqrt_alloc_i32(123456789)), 123456789);
    CHECK(cqrt_measure_i64(cqrt_alloc_i64(INT64_MIN)) == INT64_MIN);

    /* An all-constant rail owns no wire, so there is nothing to measure and no
     * `mz` to emit (I4). Eight measures above, zero gates. */
    CHECK_EQ(cq_mock_count(&m), 0);
    close_with(&m);
}

/* ONE `mz` PER QUBIT AND NONE PER CONSTANT, and the rail becomes MEASURED —
 * terminal, per PRD §7. The mixed rail is the fixture that discriminates: a
 * wrapper that emitted per BIT rather than per QUBIT would be green on a
 * uniform one. */
CQ_TEST(measure_emits_one_mz_per_qubit_and_makes_the_rail_terminal)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    /* Width 8, because the symbol says i8 and the rail must be that wide.
     * Value 0b1010, wires on lanes 1 and 3 — two wires and six constants, which
     * is the fixture that discriminates per-QUBIT from per-BIT. */
    const int32_t h = cq_bk_reg(ctx, 8u, 0xAu, 0xAu);
    CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, h), 2);
    cq_mock_reset(&m);

    const int8_t got = cqrt_measure_i8(h);
    (void)got;   /* the value is the previous case's subject, not this one's */

    CHECK_EQ(cq_mock_count(&m), 2);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_MZ), 2);
    CHECK_EQ(cq_reg_state(&ctx->regs, h), CQ_SLOT_MEASURED);

    /* MEASUREMENT IS TERMINAL AND THE RAIL STILL OWNS ITS INDICES — nothing is
     * reclaimed (Rule 6), so a measure that quietly released them would leave
     * `h` naming free indices. A MEASURED slot is readable and is swept. */
    CHECK(cq_pc_live_is_exactly(ctx, &h, 1u));
    cq_reg_audit(ctx);

    close_with(&m);
}

/* -------------------------------------------------------------------------
 * 4. free — D15's three-valued verdict and its two-valued act.
 * ------------------------------------------------------------------------- */

/* THE 40 LOOP COUNTERS. An all-constant rail owns no qubit (I4), so it frees
 * with no evidence reaching the proof at all — and PRD §15 D15 measured that
 * path carrying 40 of the corpus's 45 I4 frees. `cqrt_free` passes a real proof
 * rather than NULL, so this case is not testing the NULL carve-out; it is
 * testing that the free of a classical rail is free. */
CQ_TEST(free_of_an_all_constant_rail_costs_nothing_and_strands_nothing)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const cq_pc_snap before = cq_pc_take(ctx);
    const int32_t h = cqrt_alloc_i32(-1);

    cqrt_free(h);

    CHECK_EQ(cq_mock_count(&m), 0);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0);
    CHECK_EQ(cq_reg_state(&ctx->regs, h), CQ_SLOT_DEAD);
    cq_reg_audit(ctx);

    close_with(&m);
}

/* THE CLEAN ROW, BY INDEX. The indices must be named BEFORE the free, because
 * cq_reg_free tombstones the rail and they are gone afterwards — src/qubits.h's
 * "a count is not an identification", applied. */
CQ_TEST(free_of_a_determinate_zero_rail_releases_every_index_by_name)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t h = cq_bk_reg(ctx, 4u, 0x0u, 0xFu);   /* four wires, all |0> */
    uint32_t idx[4];
    const uint32_t n = cq_pc_indices(ctx, h, idx, 4u);
    CHECK_EQ(n, 4);

    CHECK_EQ(cq_reg_disposition(ctx, h, cq_shim_shadow_proof), CQ_PROOF_CLEAN);

    /* BEFORE the free — afterwards `h` is a tombstone and the SET check would
     * abort inside cq_reg_readable rather than fail. */
    CHECK(cq_pc_live_is_exactly(ctx, &h, 1u));

    cqrt_free(h);

    CHECK(cq_pc_indices_are_free(ctx, idx, n));
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0);
    cq_reg_audit(ctx);

    close_with(&m);
}

/* THE PROVEN-DIRTY ROW, AND IT ASSERTS THE VERDICT SEPARATELY FROM THE ACT.
 * Under D15 §4's last clause proven-dirty and unproven take the SAME act, so a
 * case that asserted only "it stranded" could not tell a CONVICTION from
 * IGNORANCE — which is precisely what landing 2's negative control exists to
 * distinguish. The shadow can convict here because nothing has rotated: it is
 * EXACT on a determinate entry, so a determinate 1 really is not |0>. */
CQ_TEST(free_of_a_determinate_one_rail_is_convicted_and_strands_by_name)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    /* EVERY lane must convict, and that is not pedantry: with value 101 the
     * middle lane is a determinate ZERO, which the same proof calls CLEAN and
     * RELEASES — so a rail of mixed verdicts would pass a "the rail is dirty"
     * assertion while stranding two of three. That row has its own case below;
     * this one is the uniform conviction. */
    const int32_t h = cq_bk_reg(ctx, 3u, 0x7u, 0x7u);   /* wires holding 111 */
    uint32_t idx[3];
    const uint32_t n = cq_pc_indices(ctx, h, idx, 3u);
    CHECK_EQ(n, 3);

    CHECK_EQ(cq_reg_disposition(ctx, h, cq_shim_shadow_proof), CQ_PROOF_DIRTY);
    CHECK(cq_pc_live_is_exactly(ctx, &h, 1u));

    cqrt_free(h);

    /* Never released, never on the free list, counted, reported ONCE. */
    for (uint32_t i = 0; i < n; i++) {
        CHECK(cq_qubits_is_stranded(&ctx->pool, idx[i]));
        CHECK(!cq_qubits_is_free(&ctx->pool, idx[i]));
    }
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 3);
    CHECK_EQ(cq_reg_strand_reports(ctx), 1);

    /* The sweep is green THROUGH a strand: a stranded index is not on the free
     * list and the DEAD slot is skipped, so clause (d) does not fire. */
    cq_reg_audit(ctx);

    close_with(&m);
}

/* THE UNPROVEN ROW, AND IT MUST ASSERT A DIFFERENT VERDICT FROM THE ROW ABOVE.
 * A general `Ry` is the one thing in the library that poisons (PRD §15 D12), so
 * this rail is one the shadow cannot reach — the residue D15's certificate
 * exists to discharge at landing 2, when this case's verdict changes and its
 * act does not. */
CQ_TEST(free_of_a_rotation_tainted_rail_is_unproven_and_strands_by_name)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t h = cq_bk_reg(ctx, 8u, 0x0u, 0xFFu);
    uint32_t idx[8];
    const uint32_t n = cq_pc_indices(ctx, h, idx, 8u);
    CHECK_EQ(n, 8);

    cqrt_ry_i8(h, 0.5);                     /* PRD §7's general row: poison */

    CHECK_EQ(cq_reg_disposition(ctx, h, cq_shim_shadow_proof), CQ_PROOF_UNPROVEN);
    CHECK(cq_pc_live_is_exactly(ctx, &h, 1u));

    cqrt_free(h);

    for (uint32_t i = 0; i < n; i++) {
        CHECK(cq_qubits_is_stranded(&ctx->pool, idx[i]));
        CHECK(!cq_qubits_is_free(&ctx->pool, idx[i]));
    }
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 8);
    cq_reg_audit(ctx);

    close_with(&m);
}

/* A MIXED RAIL IS THE ONE FIXTURE THAT CAN SEE A PER-RAIL ACT. D15 §3 says the
 * act is PER QUBIT: a rail whose lanes disagree must release the provable ones
 * and strand the rest. Every uniform fixture above passes against a per-rail
 * implementation; this one does not. */
CQ_TEST(a_mixed_rail_releases_its_provable_lanes_and_strands_the_rest)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    /* Lane 0 a wire holding 0 (clean); lane 1 a wire holding 1 (convicted). */
    const int32_t h = cq_bk_reg(ctx, 2u, 0x2u, 0x3u);
    uint32_t idx[2];
    CHECK_EQ(cq_pc_indices(ctx, h, idx, 2u), 2);
    CHECK(cq_pc_live_is_exactly(ctx, &h, 1u));

    cqrt_free(h);

    CHECK(cq_qubits_is_free(&ctx->pool, idx[0]));
    CHECK(!cq_qubits_is_free(&ctx->pool, idx[1]));
    CHECK(cq_qubits_is_stranded(&ctx->pool, idx[1]));
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 1);
    cq_reg_audit(ctx);

    close_with(&m);
}

#include "test_runtime_rail_write.inc"

CQ_TEST_MAIN(
    CQ_CASE(the_vendored_abi_header_declares_the_whole_frozen_surface),
    CQ_CASE(alloc_mints_a_rail_of_the_symbols_own_width_at_zero_cost),
    CQ_CASE(alloc_reads_a_negative_literal_as_its_own_widths_bit_pattern),
    CQ_CASE(measure_round_trips_every_width_at_the_abis_own_signed_type),
    CQ_CASE(measure_emits_one_mz_per_qubit_and_makes_the_rail_terminal),
    CQ_CASE(free_of_an_all_constant_rail_costs_nothing_and_strands_nothing),
    CQ_CASE(free_of_a_determinate_zero_rail_releases_every_index_by_name),
    CQ_CASE(free_of_a_determinate_one_rail_is_convicted_and_strands_by_name),
    CQ_CASE(free_of_a_rotation_tainted_rail_is_unproven_and_strands_by_name),
    CQ_CASE(a_mixed_rail_releases_its_provable_lanes_and_strands_the_rest),
    CQ_CASE(copy_xors_the_source_into_the_destination_in_the_abis_own_order),
    CQ_CASE(copy_into_a_written_destination_is_an_xor_and_not_a_copy),
    CQ_CASE(a_measured_rail_is_a_legal_copy_source_and_an_illegal_destination),
    CQ_CASE(copy_controlled_row_0_skips_on_zero_and_is_verbatim_on_one),
    CQ_CASE(copy_controlled_under_a_quantum_flag_is_one_ccx_per_source_wire),
    CQ_CASE(cswap_under_a_constant_control_costs_nothing_in_both_rows),
    CQ_CASE(cswap_under_a_quantum_control_is_a_fredkin_per_bit),
    CQ_CASE(cswap_at_the_corpus_shape_pins_the_fredkins_operand_order),
    CQ_CASE(copy_controlled_at_the_corpus_source_shape_is_one_cx_per_set_bit),
    CQ_CASE(the_three_wrong_cswap_routes_give_the_right_value_and_the_wrong_cost),
    CQ_CASE(xorc_is_one_x_per_set_immediate_bit_and_free_on_a_classical_rail),
    CQ_CASE(addc_folds_on_an_all_classical_rail_at_zero_cost),
    CQ_CASE(addc_short_circuits_a_zero_immediate_and_a_one_bit_rail),
    CQ_CASE(addc_on_a_qubit_owning_rail_is_cuccaro_in_place),
    CQ_CASE(addcs_transients_are_certified_by_the_construction_and_come_back),
    CQ_CASE(addc_survives_cqops_free_abort_on_a_rotation_tainted_rail),
    CQ_CASE(addc_is_width_generic_and_a_negative_immediate_is_the_expensive_half)
)
