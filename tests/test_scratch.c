/* tests/test_scratch.c — M08, Step 8. The scratch region's cq_bit array.
 *
 * M08 IS DELIBERATELY ALMOST NOTHING, and the almost is the point. It owns the
 * `cq_bit` array and its lifetime; it does NOT own the qubits, does not
 * materialise, and ships no release a kernel could call (plan §3). M09's
 * driver owns the qubits end to end. So the assertions here are about the
 * array and about what M08 CANNOT do:
 *
 *   - the API takes no cq_ctx, so it cannot reach the pool. That makes "a
 *     scratch region costs zero qubits until the driver pre-materialises it"
 *     a fact of the type system rather than a promise, exactly as
 *     cq_reg_alloc_zero taking the table rather than the context makes I4 one
 *     (reg.h). The case below observes it anyway, because a type-system fact
 *     that nothing checks is a claim.
 *   - dispose asserts every bit is back to CQ_BIT_ZERO. A KIND check, never a
 *     shadow read: poison is sticky, so a literal shadow check would fire on
 *     every legitimate sandwich kernel (bd ckd.17, K09.md:603).
 *
 * WHICH SINGLE CASE GOES RED IF THE DISPOSE CHECK IS DELETED (plan §2, the
 * Step 7 lesson): test_scratch_death.dispose_with_a_materialised_bit. Nothing
 * else in the project guards it — M03 never sees these qubits unless M09
 * releases them, and M07 never sees this array at all — so unlike M07's free
 * there is no layer underneath to mask a mutation here.
 */

#include "scratch.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "qubits.h"

#include "support/harness.h"
#include "support/mock_sink.h"

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

/* ------------------------------------------------------------------------ */

CQ_TEST(alloc_gives_a_region_of_valid_zero_bits)
{
    cq_scratch scr;
    cq_scratch_alloc(&scr, 9u);

    CHECK_EQ(cq_scratch_size(&scr), 9u);
    for (uint32_t i = 0; i < 9u; i++) {
        cq_bit *b = cq_scratch_span(&scr, i, 1u);
        CHECK(cq_bit_valid(*b));
        CHECK(cq_bit_is_zero(*b));
        CHECK_EQ(b->q, 0u);
    }

    cq_scratch_dispose(&scr);
}

/* The extent check in emit.c is a pointer RANGE test (ctx.h: "the range is
 * over cq_bit ADDRESSES"), so contiguity is not decoration — it is what makes
 * I6(a) able to answer "is this target in scratch?" at all. */
CQ_TEST(the_region_is_one_contiguous_extent)
{
    cq_scratch scr;
    cq_scratch_alloc(&scr, 6u);

    cq_bit *base = cq_scratch_span(&scr, 0u, 6u);
    for (uint32_t i = 0; i < 6u; i++)
        CHECK(cq_scratch_span(&scr, i, 1u) == base + i);

    cq_scratch_dispose(&scr);
}

/* Kernels carve one region into named arrays — K6's carry chain, K9's `nb`.
 * The whole span is bounds-checked at carve time rather than at gate time,
 * which turns an off-by-one in a kernel's layout into an abort at the point
 * the mistake was made. */
CQ_TEST(span_carves_named_sub_regions)
{
    cq_scratch scr;
    cq_scratch_alloc(&scr, 12u);

    cq_bit *lo = cq_scratch_span(&scr, 0u, 4u);
    cq_bit *mid = cq_scratch_span(&scr, 4u, 4u);
    cq_bit *hi = cq_scratch_span(&scr, 8u, 4u);

    CHECK(mid == lo + 4);
    CHECK(hi == lo + 8);
    CHECK(cq_scratch_span(&scr, 12u, 0u) == lo + 12);   /* empty tail span */

    cq_scratch_dispose(&scr);
}

/* I4's shape one level down. M08 has no pool in reach, so this is a fact of
 * the type system; the case exists so a signature that grows a cq_ctx *
 * argument has to break something. */
CQ_TEST(alloc_costs_zero_qubits_and_zero_gates)
{
    fixture f; fx_open(&f);

    cq_scratch scr;
    cq_scratch_alloc(&scr, 64u);

    CHECK_EQ(cq_qubits_live(&f.ctx.pool), 0u);
    CHECK_EQ(cq_qubits_minted(&f.ctx.pool), 0u);
    CHECK_EQ(cq_mock_count(&f.mock), 0u);

    cq_scratch_dispose(&scr);
    fx_close(&f);
}

/* Dispose "leaves it usable, as if init'd" — the house rule M03 and M07 both
 * follow — so a kernel that allocates, sandwiches and disposes in a loop does
 * not need a second lifetime concept. */
CQ_TEST(dispose_returns_the_struct_to_a_reusable_state)
{
    cq_scratch scr;

    cq_scratch_alloc(&scr, 3u);
    cq_scratch_dispose(&scr);
    CHECK_EQ(cq_scratch_size(&scr), 0u);
    CHECK(scr.bits == NULL);

    cq_scratch_dispose(&scr);            /* idempotent: no double free */
    CHECK_EQ(cq_scratch_size(&scr), 0u);

    cq_scratch_alloc(&scr, 5u);          /* and re-usable */
    CHECK_EQ(cq_scratch_size(&scr), 5u);
    cq_scratch_dispose(&scr);
}

/* The check discriminates rather than aborting on everything: a bit taken to
 * ONE and back to ZERO disposes cleanly. Without this the death cases would
 * be satisfied by an unconditional abort. */
CQ_TEST(a_bit_flipped_and_flipped_back_disposes_cleanly)
{
    fixture f; fx_open(&f);

    cq_scratch scr;
    cq_scratch_alloc(&scr, 2u);

    cq_bit *b = cq_scratch_span(&scr, 1u, 1u);
    cq_emit_x(&f.ctx, b);                 /* ZERO -> ONE, 0 gates (const fold) */
    CHECK(cq_bit_is_one(*b));
    cq_emit_x(&f.ctx, b);                 /* ONE  -> ZERO */
    CHECK(cq_bit_is_zero(*b));

    CHECK_EQ(cq_mock_count(&f.mock), 0u);
    cq_scratch_dispose(&scr);
    fx_close(&f);
}

CQ_TEST_MAIN(
    CQ_CASE(alloc_gives_a_region_of_valid_zero_bits),
    CQ_CASE(the_region_is_one_contiguous_extent),
    CQ_CASE(span_carves_named_sub_regions),
    CQ_CASE(alloc_costs_zero_qubits_and_zero_gates),
    CQ_CASE(dispose_returns_the_struct_to_a_reusable_state),
    CQ_CASE(a_bit_flipped_and_flipped_back_disposes_cleanly)
)
