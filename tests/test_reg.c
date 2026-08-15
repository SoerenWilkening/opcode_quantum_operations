/* tests/test_reg.c — M07, Step 7. The handle table, D5, tombstones, I4, the
 * free path, the I2 audit and the D7a/D7b operand split.
 *
 * The Prime Directive applies here as everywhere: a green run proves the table
 * bookkeeping, not the circuit. The assertions that actually bite are the ones
 * on the POOL (qubits live, minted, free) and on the audit — the value-shaped
 * ones would stay green through a leaked qubit. */

#include "reg.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "qubits.h"
#include "shadow.h"

#include "support/harness.h"
#include "support/mock_sink.h"

/* THE ONLY ZERO-PROOF IN THE TREE, and it lives here rather than in src/ on
 * purpose. bd ckd.17 (P0, OPEN) establishes that the two-bit shadow CANNOT be
 * the free-time oracle: §3's CX rule makes poison sticky, so after any
 * sandwich kernel on a tainted operand every bit of a result rail reads
 * unknown and this predicate would hard-error on every legitimate program.
 *
 * It is sound ONLY while no kernel exists — i.e. exactly at Step 7 — because
 * the only qubits any of these cases can produce come from cq_materialise on a
 * fresh index, whose shadow is born known-0. The moment M09's sandwich lands,
 * this stops being a proof and becomes a laundering device. It is static, so
 * nothing outside this file can reach it, and the library defines no proof at
 * all: NULL means "no evidence" and fails loud. */
static int proof_shadow_pre_kernel_only(const cq_ctx *ctx, int32_t h, uint32_t q)
{
    (void)h;
    return cq_shadow_known_zero(&ctx->shadow, q);
}

/* Every case needs a context wired to a recording sink, so that "emitted
 * nothing" is an assertion rather than an assumption. */
typedef struct { cq_ctx ctx; cq_mock mock; cq_sink sink; } fixture;

static void fx_open(fixture *f)
{
    cq_mock_init(&f->mock);
    f->sink = cq_mock_sink(&f->mock);
    cq_ctx_init(&f->ctx, &f->sink);
}

static void fx_close(fixture *f)
{
    cq_ctx_dispose(&f->ctx);
    cq_mock_dispose(&f->mock);
}

/* Materialises bit `i` of `h` by CX-ing an external quantum control into it —
 * the only route a bit becomes a qubit (Rule 5), and deliberately NOT a direct
 * write, so these cases exercise the same path a kernel will. Returns the
 * qubit index the bit ends up on.
 *
 * THE CONTROL'S SHADOW DECIDES THE TARGET'S, and getting that backwards is
 * easy: §3's CX rule is `t.unknown |= c.unknown` and then `t.value ^= c.value`,
 * so a CX from a FRESH control (born known-0) leaves the target a qubit whose
 * shadow is still known-0 — provably clean, not dirty. A dirty bit needs a
 * poisoned control, which is what a real rotation on a source produces. */
enum { CLEAN_CTRL = 0, POISONED_CTRL = 1 };

static uint32_t materialise_bit(fixture *f, int32_t h, uint32_t i, int poison)
{
    uint32_t c = cq_ctx_fresh_qubit(&f->ctx);
    if (poison) cq_shadow_rotate(&f->ctx.shadow, c);
    cq_bit ctrl = cq_bit_qubit(c);
    cq_emit_cx(&f->ctx, &ctrl, &cq_reg_bits(&f->ctx.regs, h)[i]);
    return cq_bit_qindex(cq_reg_cbits(&f->ctx.regs, h)[i]);
}

/* --- D5: monotonic, never reused. ---------------------------------------- */

CQ_TEST(handles_are_monotonic_and_start_at_zero)
{
    fixture f; fx_open(&f);
    CHECK_EQ(cq_reg_count(&f.ctx.regs), 0);
    /* Handle 0 is VALID and live — CQ_lang's counter post-increments from 0
     * (runtime/cq_runtime.c:64,67) and every golden opens `-> h0`. A design
     * that reserved 0 as its sentinel fails on this line. */
    CHECK_EQ(cq_reg_alloc_zero(&f.ctx.regs, 32), 0);
    CHECK_EQ(cq_reg_alloc_zero(&f.ctx.regs, 8),  1);
    CHECK_EQ(cq_reg_alloc_zero(&f.ctx.regs, 1),  2);
    CHECK_EQ(cq_reg_count(&f.ctx.regs), 3);
    CHECK(CQ_REG_NONE < 0);
    fx_close(&f);
}

CQ_TEST(a_freed_handle_is_never_reissued)
{
    fixture f; fx_open(&f);
    int32_t h0 = cq_reg_alloc_zero(&f.ctx.regs, 8);
    cq_reg_free(&f.ctx, h0, NULL);              /* all-constant: no evidence needed */
    /* This is the assertion that fails the moment anyone adds a handle free
     * list "to save memory". D5 is what makes the h<N> trace convention and
     * every L6 diff stable. */
    CHECK_EQ(cq_reg_alloc_zero(&f.ctx.regs, 8), 1);
    CHECK_EQ(cq_reg_count(&f.ctx.regs), 2);     /* never decreases across a free */
    fx_close(&f);
}

CQ_TEST(a_tombstone_keeps_its_width_for_diagnostics)
{
    fixture f; fx_open(&f);
    int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 16);
    CHECK_EQ(cq_reg_state(&f.ctx.regs, h), CQ_SLOT_LIVE);
    cq_reg_free(&f.ctx, h, NULL);
    CHECK_EQ(cq_reg_state(&f.ctx.regs, h), CQ_SLOT_DEAD);
    CHECK_EQ(cq_reg_width(&f.ctx.regs, h), 16);
    fx_close(&f);
}

CQ_TEST(is_live_is_a_predicate_and_never_aborts)
{
    fixture f; fx_open(&f);
    int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 8);
    CHECK(cq_reg_is_live(&f.ctx.regs, h));
    /* Out of range, negative and the sentinel all answer 0 rather than dying —
     * cq_qubits_is_free's shape. The ACCESSORS abort on the same inputs; that
     * asymmetry is the point, and the death suite pins the other half. */
    CHECK(!cq_reg_is_live(&f.ctx.regs, 999));
    CHECK(!cq_reg_is_live(&f.ctx.regs, -1));
    CHECK(!cq_reg_is_live(&f.ctx.regs, CQ_REG_NONE));
    cq_reg_free(&f.ctx, h, NULL);
    CHECK(!cq_reg_is_live(&f.ctx.regs, h));
    fx_close(&f);
}

CQ_TEST(tombstones_and_bits_survive_table_growth)
{
    fixture f; fx_open(&f);
    int32_t h0 = cq_reg_alloc_zero(&f.ctx.regs, 8);
    int32_t h1 = cq_reg_alloc_zero(&f.ctx.regs, 16);
    cq_reg_free(&f.ctx, h1, NULL);

    /* A cq_bit * must survive a realloc of the SLOT array: the bits array is
     * its own allocation, which is what lets Rule 7 hand a kernel a raw
     * cq_bit * that stays valid while the shim mints more handles. */
    cq_bit *p = cq_reg_bits(&f.ctx.regs, h0);
    p[3] = cq_bit_one();

    for (int i = 0; i < 300; i++) (void)cq_reg_alloc_zero(&f.ctx.regs, 4);

    CHECK(cq_reg_bits(&f.ctx.regs, h0) == p);
    CHECK_EQ(p[3].kind, CQ_BIT_ONE);
    /* A grow that re-initialised the whole slot array instead of only the new
     * tail resurrects this tombstone, and a use-after-free becomes a
     * successful read. */
    CHECK_EQ(cq_reg_state(&f.ctx.regs, h1), CQ_SLOT_DEAD);
    CHECK_EQ(cq_reg_width(&f.ctx.regs, h1), 16);
    CHECK_EQ(cq_reg_count(&f.ctx.regs), 302);
    fx_close(&f);
}

CQ_TEST(every_slot_reads_a_valid_state_after_growth)
{
    fixture f; fx_open(&f);
    for (int i = 0; i < 200; i++) (void)cq_reg_alloc_zero(&f.ctx.regs, 2);
    /* Reads go through the validating accessor, so a 0xAA-poisoned entry
     * inside [0, n) would abort here rather than answer a plausible state. */
    for (int32_t h = 0; h < cq_reg_count(&f.ctx.regs); h++) {
        int s = cq_reg_state(&f.ctx.regs, h);
        CHECK(s == CQ_SLOT_LIVE || s == CQ_SLOT_DEAD || s == CQ_SLOT_MEASURED);
    }
    fx_close(&f);
}

/* --- I4: an all-constant register owns zero qubits. ---------------------- */

CQ_TEST(i4_allocation_costs_no_qubits_at_any_width)
{
    static const uint32_t widths[] = { 1, 8, 16, 32, 64, 80, 128 };
    fixture f; fx_open(&f);
    for (size_t k = 0; k < sizeof widths / sizeof widths[0]; k++) {
        int32_t h = cq_reg_alloc_zero(&f.ctx.regs, widths[k]);
        CHECK_EQ(cq_reg_width(&f.ctx.regs, h), widths[k]);
        CHECK_EQ(cq_reg_owned_qubits(&f.ctx.regs, h), 0);
        const cq_bit *b = cq_reg_cbits(&f.ctx.regs, h);
        for (uint32_t i = 0; i < widths[k]; i++) {
            CHECK(cq_bit_is_zero(b[i]));
            CHECK(cq_bit_valid(b[i]));
        }
    }
    /* 80 and 128 are in that list deliberately: a width WHITELIST taken from
     * PRD §2.2's old "1, 8, 16, 32, 64 or 128" comment rejects every i80 rail,
     * which is why the check is a range. */
    CHECK_EQ(cq_qubits_live(&f.ctx.pool), 0);
    CHECK_EQ(cq_qubits_minted(&f.ctx.pool), 0);
    CHECK_EQ(cq_mock_count(&f.mock), 0);
    fx_close(&f);
}

CQ_TEST(a_literal_register_is_constants_and_owns_nothing)
{
    fixture f; fx_open(&f);
    int32_t h = cq_reg_alloc_const(&f.ctx.regs, 32, 5u, 0u);
    const cq_bit *b = cq_reg_cbits(&f.ctx.regs, h);
    CHECK(cq_bit_is_one(b[0]));  CHECK(cq_bit_is_zero(b[1]));
    CHECK(cq_bit_is_one(b[2]));  CHECK(cq_bit_is_zero(b[31]));
    for (uint32_t i = 0; i < 32; i++) CHECK_EQ(b[i].q, 0);   /* canonical */
    CHECK_EQ(cq_reg_owned_qubits(&f.ctx.regs, h), 0);
    CHECK_EQ(cq_qubits_minted(&f.ctx.pool), 0);
    fx_close(&f);
}

CQ_TEST(the_literal_is_two_words_because_the_abi_reaches_128_bits)
{
    fixture f; fx_open(&f);
    /* cq_template_and_i80_hl(int32_t, __int128) is literally CQ_lang
     * runtime/cq_templates.h:127, so a uint64_t value parameter would silently
     * truncate every i80 and i128 literal above bit 63. This is the case such
     * a signature cannot pass. */
    int32_t w128 = cq_reg_alloc_const(&f.ctx.regs, 128, 0u, 0x8000000000000000u);
    const cq_bit *b = cq_reg_cbits(&f.ctx.regs, w128);
    CHECK(cq_bit_is_one(b[127]));
    CHECK(cq_bit_is_zero(b[126]));
    CHECK(cq_bit_is_zero(b[0]));

    int32_t w80 = cq_reg_alloc_const(&f.ctx.regs, 80, 0u, 0xFFFFu);
    const cq_bit *c = cq_reg_cbits(&f.ctx.regs, w80);
    for (uint32_t i = 64; i < 80; i++) CHECK(cq_bit_is_one(c[i]));
    CHECK(cq_bit_is_zero(c[63]));
    fx_close(&f);
}

CQ_TEST(owned_qubits_counts_what_the_emitter_actually_allocated)
{
    fixture f; fx_open(&f);
    int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 8);
    (void)materialise_bit(&f, h, 0, CLEAN_CTRL);
    (void)materialise_bit(&f, h, 5, CLEAN_CTRL);
    /* Two rail bits materialised, plus the two external controls = 4 live.
     * The count is a LOOP over the bits, never a cached field: a cache would
     * need M05 to notify M07 and would drift the first time a kernel ran. */
    CHECK_EQ(cq_reg_owned_qubits(&f.ctx.regs, h), 2);
    CHECK_EQ(cq_qubits_live(&f.ctx.pool), 4);
    cq_reg_audit(&f.ctx);
    fx_close(&f);
}

/* --- The free path, and the ckd.17 refusal. ------------------------------ */

CQ_TEST(freeing_an_all_constant_rail_needs_no_evidence)
{
    fixture f; fx_open(&f);
    /* `int x = 5;` going out of scope. By I4 the rail owns zero qubits, so
     * nothing can reach the free list and nothing can collapse — which is why
     * Rule 6 is scoped to QUBIT-CARRYING bits. Read literally ("every bit is
     * BIT_ZERO or a known-zero qubit") this line aborts, and L5's zero-cost
     * classical path becomes unreachable. PRD §10 is amended to match. */
    int32_t h = cq_reg_alloc_const(&f.ctx.regs, 32, 5u, 0u);
    CHECK(cq_reg_clean(&f.ctx, h, NULL));
    cq_reg_free(&f.ctx, h, NULL);
    CHECK_EQ(cq_reg_state(&f.ctx.regs, h), CQ_SLOT_DEAD);
    CHECK_EQ(cq_qubits_live(&f.ctx.pool), 0);
    CHECK_EQ(cq_qubits_minted(&f.ctx.pool), 0);
    CHECK_EQ(cq_qubits_free(&f.ctx.pool), 0);
    fx_close(&f);
}

CQ_TEST(free_returns_exactly_the_indices_the_rail_held)
{
    fixture f; fx_open(&f);
    int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 4);
    uint32_t held[4]; uint32_t n = 0;

    for (uint32_t i = 0; i < 4; i++)
        held[n++] = materialise_bit(&f, h, i, CLEAN_CTRL);
    uint32_t before = cq_qubits_free(&f.ctx.pool);

    cq_reg_free(&f.ctx, h, proof_shadow_pre_kernel_only);

    CHECK_EQ(cq_qubits_free(&f.ctx.pool), before + n);
    /* The SET matters, not just the count: clearing a bit before releasing its
     * index erases the index (a constant carries a canonical q == 0) and leaks
     * the qubit in silence, and a count-only assertion stays green. */
    for (uint32_t i = 0; i < n; i++) CHECK(cq_qubits_is_free(&f.ctx.pool, held[i]));
    fx_close(&f);
}

CQ_TEST(clean_is_false_when_the_proof_refuses)
{
    fixture f; fx_open(&f);
    int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 4);
    (void)materialise_bit(&f, h, 2, POISONED_CTRL);   /* shadow now unknown */

    /* THE MUTATION-PROOF ASSERTION, and it is an ORDINARY test rather than a
     * death test on purpose. If M07's evidence forwarding were deleted, a
     * death test would still see an abort — from M03's own
     * `if (!proven_zero) cq_pool_die(...)` one layer down — and would pass on a
     * broken library. That is the defence-in-depth trap plan §0 recorded when
     * deleting M05's distinctness check still aborted. Nothing aborts here at
     * all, so nothing can mask it. */
    CHECK(!cq_reg_clean(&f.ctx, h, proof_shadow_pre_kernel_only));
    CHECK(!cq_reg_clean(&f.ctx, h, NULL));   /* no evidence => not clean */
    fx_close(&f);
}

CQ_TEST(clean_is_total_over_the_rail_before_anything_is_released)
{
    fixture f; fx_open(&f);
    int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 8);
    for (uint32_t i = 0; i < 8; i++)            /* every bit clean but the last */
        (void)materialise_bit(&f, h, i, i == 7 ? POISONED_CTRL : CLEAN_CTRL);
    CHECK(!cq_reg_clean(&f.ctx, h, proof_shadow_pre_kernel_only));
    /* Nothing was released: the free verifies the WHOLE rail before touching
     * the pool, so a dirty rail is never left half-returned. */
    CHECK_EQ(cq_qubits_free(&f.ctx.pool), 0);
    fx_close(&f);
}

/* --- Rule 5's physical copy. --------------------------------------------- */

CQ_TEST(copying_a_quantum_source_allocates_and_emits_per_bit)
{
    fixture f; fx_open(&f);
    int32_t src = cq_reg_alloc_zero(&f.ctx.regs, 4);
    int32_t dst = cq_reg_alloc_zero(&f.ctx.regs, 4);
    for (uint32_t i = 0; i < 4; i++) (void)materialise_bit(&f, src, i, CLEAN_CTRL);

    cq_mock_reset(&f.mock);
    cq_reg_xor_into(&f.ctx, dst, src);

    /* Copies are PHYSICAL, never aliases (Rule 5) — 4 fresh qubits and 4 CX,
     * which is exactly what makes cqrt_free sound (I2). */
    CHECK_GATES(cq_mock_count_op(&f.mock, CQ_OP_X),
                cq_mock_count_op(&f.mock, CQ_OP_CX),
                cq_mock_count_op(&f.mock, CQ_OP_CCX), 0, 4, 0);
    CHECK_EQ(cq_reg_owned_qubits(&f.ctx.regs, dst), 4);
    for (uint32_t i = 0; i < 4; i++)
        CHECK(cq_bit_qindex(cq_reg_cbits(&f.ctx.regs, dst)[i]) !=
              cq_bit_qindex(cq_reg_cbits(&f.ctx.regs, src)[i]));
    cq_reg_audit(&f.ctx);       /* and no index is now double-owned */
    fx_close(&f);
}

CQ_TEST(copying_a_constant_source_costs_nothing)
{
    fixture f; fx_open(&f);
    int32_t src = cq_reg_alloc_zero(&f.ctx.regs, 64);
    int32_t dst = cq_reg_alloc_zero(&f.ctx.regs, 64);
    cq_reg_xor_into(&f.ctx, dst, src);
    /* L5 in miniature: fully classical is zero gates and zero qubits. */
    CHECK_EQ(cq_mock_count(&f.mock), 0);
    CHECK_EQ(cq_qubits_minted(&f.ctx.pool), 0);
    CHECK_EQ(cq_reg_owned_qubits(&f.ctx.regs, dst), 0);
    fx_close(&f);
}

/* --- Measurement is terminal. -------------------------------------------- */

CQ_TEST(marking_measured_keeps_the_qubits_and_emits_nothing)
{
    fixture f; fx_open(&f);
    int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 4);
    (void)materialise_bit(&f, h, 1, CLEAN_CTRL);
    uint32_t live = cq_qubits_live(&f.ctx.pool);
    cq_mock_reset(&f.mock);

    cq_reg_mark_measured(&f.ctx.regs, h);

    /* Measured over the 239 goldens: 255 measures, 0 later freed, 0 later
     * referenced — the qubits are DELIBERATELY never reclaimed (PRD §7). The
     * gate and the ABI return value belong to M26 at Step 23, so this call
     * takes the table and cannot emit. */
    CHECK_EQ(cq_reg_state(&f.ctx.regs, h), CQ_SLOT_MEASURED);
    CHECK_EQ(cq_qubits_live(&f.ctx.pool), live);
    CHECK_EQ(cq_reg_owned_qubits(&f.ctx.regs, h), 1);
    CHECK_EQ(cq_mock_count(&f.mock), 0);
    fx_close(&f);
}

#include "test_reg_invariants.inc"

/* --- Lifetime of the table itself. --------------------------------------- */

CQ_TEST(dispose_leaves_the_table_usable_and_is_not_a_free)
{
    fixture f; fx_open(&f);
    int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 8);
    for (uint32_t i = 0; i < 4; i++) (void)materialise_bit(&f, h, i, CLEAN_CTRL);
    CHECK_EQ(cq_reg_owned_qubits(&f.ctx.regs, h), 4);

    cq_reg_table_dispose(&f.ctx.regs);

    /* DISPOSE IS NOT A FREE. The pool is untouched — 8 qubits stay live (4 in
     * the rail, 4 external controls). A rail that is never `cqrt_free`d stays
     * allocated for good: that is the intended Rule-6 safe leak (PRD §10), and
     * a tidy-up sweep here would be releasing rails with no evidence at all,
     * which is the one unforgivable bug. */
    CHECK_EQ(cq_qubits_live(&f.ctx.pool), 8);
    CHECK_EQ(cq_reg_count(&f.ctx.regs), 0);
    CHECK_EQ(cq_reg_alloc_zero(&f.ctx.regs, 8), 0);   /* usable as if init'd */
    fx_close(&f);
}

CQ_TEST_MAIN(
    CQ_CASE(handles_are_monotonic_and_start_at_zero),
    CQ_CASE(a_freed_handle_is_never_reissued),
    CQ_CASE(a_tombstone_keeps_its_width_for_diagnostics),
    CQ_CASE(is_live_is_a_predicate_and_never_aborts),
    CQ_CASE(tombstones_and_bits_survive_table_growth),
    CQ_CASE(every_slot_reads_a_valid_state_after_growth),
    CQ_CASE(i4_allocation_costs_no_qubits_at_any_width),
    CQ_CASE(a_literal_register_is_constants_and_owns_nothing),
    CQ_CASE(the_literal_is_two_words_because_the_abi_reaches_128_bits),
    CQ_CASE(owned_qubits_counts_what_the_emitter_actually_allocated),
    CQ_CASE(freeing_an_all_constant_rail_needs_no_evidence),
    CQ_CASE(free_returns_exactly_the_indices_the_rail_held),
    CQ_CASE(clean_is_false_when_the_proof_refuses),
    CQ_CASE(clean_is_total_over_the_rail_before_anything_is_released),
    CQ_CASE(copying_a_quantum_source_allocates_and_emits_per_bit),
    CQ_CASE(copying_a_constant_source_costs_nothing),
    CQ_CASE(marking_measured_keeps_the_qubits_and_emits_nothing),
    CQ_CASE(the_audit_passes_on_disjoint_registers),
    CQ_CASE(the_audit_sweeps_a_measured_rail_without_a_false_positive),
    CQ_CASE(the_audit_asserts_only_the_weak_form),
    CQ_CASE(the_audit_is_scoped_to_live_and_measured_registers),
    CQ_CASE(distinct_operands_pass_the_d7_check),
    CQ_CASE(d7b_two_aliased_sources_are_legal_and_must_not_abort),
    CQ_CASE(dispose_leaves_the_table_usable_and_is_not_a_free)
)
