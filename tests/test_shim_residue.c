/* tests/test_shim_residue.c — `bd c55`: D15 §3's residue split, read from
 * OUTSIDE the archive.
 *
 * THE DEFECT THIS CLOSES IS AN OBSERVABILITY ONE, NOT A BEHAVIOURAL ONE. The
 * split itself has been tested since Step 23 landing 2, at the layer that
 * produces it (tests/test_reg_free.inc:the_residue_splits_by_verdict_although_
 * the_act_does_not) and through the real entry points
 * (tests/test_shim_cert_rules.inc). What was missing was a way for a CQ_lang
 * fixture linked against libcqops.a to ASK: `ctx->stranded_*` and
 * `cq_qubits_stranded` are internal, the strand report on stderr is one-shot by
 * design, and so the strongest sentence an L6 report could make about a fixture
 * was a BOOLEAN — the line fired, or it did not.
 *
 * SO EVERY CASE HERE IS ABOUT THE READ, NOT ABOUT THE SPLIT. What must be true
 * of cqops_read_residue and could be false of a plausible implementation:
 *
 *   1. it is a READ — it does not mint the process context, so asking how much
 *      leaked never installs a sink or resolves CQOPS_SINK;
 *   2. its six fields come from SIX SOURCES, so no field-swap and no
 *      shared-source mutant survives — which is why the fixture below is
 *      arranged to make all six values pairwise distinct;
 *   3. the two GRAINS still disagree after the read, because that disagreement
 *      is the whole content of `bd 06t`'s first obligation and a read that
 *      collapsed it would be reporting a single residue figure again;
 *   4. `stranded_qubits` comes from the POOL and is not `dirty + unproven`,
 *      which src/reg.h states as an assertion a caller must be able to CHECK —
 *      computing it here would make that check a tautology.
 *
 * THE PROOF IS A LOCAL TABLE, NOT THE CERTIFICATE, and that is deliberate. The
 * certificate's verdicts are a property of a call stream and reaching an exact
 * six-way-distinct tuple through it would be an exercise in stream carpentry
 * that tested the reduction all over again. Here the verdict per qubit is
 * DECLARED, so the fixture states its own expected tuple and every assertion
 * below is about the accessor.
 */
#include "cq_shim.h"
#include "cq_shim_ctx.h"

#include "bit.h"
#include "ctx.h"
#include "qubits.h"
#include "reg.h"
#include "sink.h"

#include "cqops/cqops.h"

#include "support/bitkinds.h"
#include "support/harness.h"
#include "support/mock_sink.h"

#include <stdint.h>
#include <stdlib.h>

/* tests/test_shim_cert.c:fresh(), verbatim: the selection and the context must
 * both be known before the first cq_shim_ctx(), which is what latches the sink. */
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
    return cq_shim_ctx();
}

static void close_with(cq_mock *m)
{
    cq_shim_ctx_reset();
    cqops_set_sink(NULL);
    cq_mock_dispose(m);
}

/* --- A proof whose answer is DECLARED PER INDEX. ------------------------- */

#define VERDICTS 32u
static int8_t g_verdict[VERDICTS];

static int proof_table(const cq_ctx *ctx, int32_t h, uint32_t q)
{
    (void)ctx; (void)h;
    return q < VERDICTS ? (int)g_verdict[q] : CQ_PROOF_UNPROVEN;
}

/* A rail of `W` qubits, every lane materialised, with each lane's verdict
 * declared as it is minted. Recycling is OFF for the whole fixture (see
 * `stage`), so the indices this hands out are never reused and the table stays
 * a faithful map for the life of the case. */
static int32_t rail(cq_ctx *ctx, uint32_t W, const int *verdict)
{
    int32_t h = cq_bk_reg(ctx, W, 0u, (W == 64u) ? ~0ull : ((1ull << W) - 1ull));
    const cq_bit *b = cq_reg_cbits(&ctx->regs, h);

    for (uint32_t i = 0; i < W; i++) {
        uint32_t q = cq_bit_qindex(b[i]);
        CHECK(q < VERDICTS);
        g_verdict[q] = (int8_t)verdict[i];
    }
    return h;
}

/* THE ONE FIXTURE EVERY COUNTING CASE USES, and its whole design constraint is
 * that the six fields come out PAIRWISE DISTINCT: 5, 4, 2, 3, 9 and 1. A mutant
 * that reads one counter into another field, or reads the rail grain where the
 * qubit grain was meant, then has nowhere to hide. `strand_reports` is capped at
 * 1 by its own one-shot, so 1 is the one value that cannot be moved out of the
 * way; nothing else in the tuple is 1.
 *
 * RECYCLING IS TURNED OFF FIRST, BEFORE ANY FREE. The clean lanes below really
 * do go back to the pool, and with the LIFO free list on (D4) the next rail's
 * materialise would take one of them back and re-point the verdict table at a
 * lane that has already been decided. cq_qubits_set_recycle refuses to be
 * turned off once the free list is non-empty, which is exactly why this is the
 * first statement. */
static void stage(cq_ctx *ctx)
{
    static const int m_clean_unproven[2] = { CQ_PROOF_CLEAN, CQ_PROOF_UNPROVEN };
    static const int m_unproven[1]       = { CQ_PROOF_UNPROVEN };
    static const int m_dirty4[4]         = { CQ_PROOF_DIRTY, CQ_PROOF_DIRTY,
                                             CQ_PROOF_DIRTY, CQ_PROOF_DIRTY };
    static const int m_mixed[2]          = { CQ_PROOF_UNPROVEN, CQ_PROOF_DIRTY };
    static const int m_clean[1]          = { CQ_PROOF_CLEAN };

    cq_qubits_set_recycle(&ctx->pool, 0);

    /* Three frees land on the rail-level UNPROVEN row and strand four qubits
     * between them — the first of them also proving a CLEAN lane of an
     * otherwise unproven rail is released rather than dragged down with it. */
    cq_reg_free(ctx, rail(ctx, 2u, m_clean_unproven), proof_table);
    cq_reg_free(ctx, rail(ctx, 1u, m_unproven),       proof_table);
    cq_reg_free(ctx, rail(ctx, 1u, m_unproven),       proof_table);

    /* Two land on the rail-level DIRTY row and strand five qubits, one of which
     * is UNPROVEN rather than convicted — that is the mixed rail, and it is the
     * shape the two grains disagree about. */
    cq_reg_free(ctx, rail(ctx, 4u, m_dirty4), proof_table);
    cq_reg_free(ctx, rail(ctx, 2u, m_mixed),  proof_table);

    /* AND A WHOLLY CLEAN FREE, WHICH IS COUNTED NOWHERE. The rail rows
     * deliberately do not sum to the number of frees (src/reg.h): counting this
     * one would make the denominator mean "frees" in one reading and "frees
     * that owned a qubit" in another. It is here so that "nowhere" is asserted
     * rather than assumed. */
    cq_reg_free(ctx, rail(ctx, 1u, m_clean), proof_table);
}

/* -------------------------------------------------------------------------
 * 1. It is a READ.
 * ------------------------------------------------------------------------- */

CQ_TEST(a_read_before_the_context_exists_is_zero_and_mints_nothing)
{
    cqops_residue r;

    fresh();
    cqops_set_sink(NULL);

    /* bd utk's state, and tests/test_shim_ctx.c's spelling for it: the built-in
     * sinks are installed by cq_shim_ctx() and by nothing else, so their absence
     * is the observable that says no context has been constructed. */
    CHECK(cq_sink_by_name("printf") == NULL);

    cqops_read_residue(&r);

    /* THE CLAIM. A read that reached for cq_shim_ctx() would register the
     * built-ins, resolve CQOPS_SINK and build a pool — and would turn
     * `CQOPS_SINK=nonesuch` into a hard error raised by a diagnostic. */
    CHECK(cq_sink_by_name("printf") == NULL);
    CHECK(cq_sink_by_name("counter") == NULL);

    /* With no context there has been no free, so zero is the truth rather than
     * a placeholder. */
    CHECK_EQ(r.stranded_dirty, 0u);
    CHECK_EQ(r.stranded_unproven, 0u);
    CHECK_EQ(r.frees_dirty, 0u);
    CHECK_EQ(r.frees_unproven, 0u);
    CHECK_EQ(r.stranded_qubits, 0u);
    CHECK_EQ(r.strand_reports, 0u);
}

/* -------------------------------------------------------------------------
 * 2. Six fields, six sources.
 * ------------------------------------------------------------------------- */

CQ_TEST(every_field_comes_from_its_own_source)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    cqops_residue r;

    stage(ctx);
    cqops_read_residue(&r);

    /* PAIRWISE DISTINCT BY CONSTRUCTION — see stage(). Any two of these read
     * from one source is a red case here and is invisible to a total. */
    CHECK_EQ(r.stranded_dirty, 5u);
    CHECK_EQ(r.stranded_unproven, 4u);
    CHECK_EQ(r.frees_dirty, 2u);
    CHECK_EQ(r.frees_unproven, 3u);
    CHECK_EQ(r.stranded_qubits, 9u);
    CHECK_EQ(r.strand_reports, 1u);

    /* AND IT IS THE SAME CONTEXT THE LIBRARY IS COUNTING IN. A read of a
     * second, private cq_ctx would satisfy every line above only by accident;
     * these four say the public surface and the internal one are one subject. */
    CHECK_EQ(r.stranded_dirty, cq_reg_stranded_dirty(ctx));
    CHECK_EQ(r.stranded_unproven, cq_reg_stranded_unproven(ctx));
    CHECK_EQ(r.frees_dirty, cq_reg_frees_dirty(ctx));
    CHECK_EQ(r.frees_unproven, cq_reg_frees_unproven(ctx));
    CHECK_EQ(r.stranded_qubits, cq_qubits_stranded(&ctx->pool));
    CHECK_EQ(r.strand_reports, cq_reg_strand_reports(ctx));

    close_with(&m);
}

/* -------------------------------------------------------------------------
 * 3. The grains still disagree on the other side of the read.
 * ------------------------------------------------------------------------- */

CQ_TEST(the_read_preserves_the_disagreement_between_the_two_grains)
{
    static const int m_mixed[2] = { CQ_PROOF_UNPROVEN, CQ_PROOF_DIRTY };
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    cqops_residue a, b;

    cq_qubits_set_recycle(&ctx->pool, 0);
    cqops_read_residue(&a);

    /* ONE FREE, ONE MIXED RAIL: one unproven lane and one convicted lane. */
    cq_reg_free(ctx, rail(ctx, 2u, m_mixed), proof_table);
    cqops_read_residue(&b);

    /* THE QUBIT GRAIN SAYS ONE OF EACH ... */
    CHECK_EQ(b.stranded_dirty - a.stranded_dirty, 1u);
    CHECK_EQ(b.stranded_unproven - a.stranded_unproven, 1u);

    /* ... AND THE RAIL GRAIN SAYS DIRTY, AND ONLY DIRTY, because the
     * disposition's lattice makes dirty absorbing. Neither grain is derivable
     * from the other, and a read that reported one of them would discard
     * exactly the rail the fold's no-early-return was written to reach. */
    CHECK_EQ(b.frees_dirty - a.frees_dirty, 1u);
    CHECK_EQ(b.frees_unproven - a.frees_unproven, 0u);

    close_with(&m);
}

/* -------------------------------------------------------------------------
 * 4. The pool total is the POOL's, not a sum of the two rows above it.
 * ------------------------------------------------------------------------- */

CQ_TEST(the_pool_total_is_read_from_the_pool_and_can_disagree)
{
    static const int m_unproven[1] = { CQ_PROOF_UNPROVEN };
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    cqops_residue r;
    int32_t leaked;

    cq_qubits_set_recycle(&ctx->pool, 0);
    cq_reg_free(ctx, rail(ctx, 1u, m_unproven), proof_table);

    /* THE SUM HOLDS ON THE FREE PATH — src/reg.h states it as an assertion
     * about that path, because every increment sits beside the strand it
     * describes. */
    cqops_read_residue(&r);
    CHECK_EQ(r.stranded_qubits, r.stranded_dirty + r.stranded_unproven);

    /* AND THE CALLER MUST BE ABLE TO CHECK IT, WHICH MEANS IT CANNOT BE HOW THE
     * FIELD IS COMPUTED. A qubit stranded by something that did not go through
     * cq_reg_free is precisely the drift the assertion exists to catch; against
     * a field spelled `dirty + unproven` it would be invisible. The rail is
     * deliberately never freed afterwards — a second strand of the same index
     * is a hard error, and an unfreed rail is PRD §10's intended safe leak. */
    leaked = rail(ctx, 1u, m_unproven);
    cq_qubits_strand(&ctx->pool,
                     cq_bit_qindex(cq_reg_cbits(&ctx->regs, leaked)[0]));

    cqops_read_residue(&r);
    CHECK_EQ(r.stranded_qubits, r.stranded_dirty + r.stranded_unproven + 1u);

    close_with(&m);
}

/* -------------------------------------------------------------------------
 * 5. The counters are the CONTEXT's, so the reset clears them.
 * ------------------------------------------------------------------------- */

CQ_TEST(the_reset_clears_the_residue_because_it_belongs_to_the_context)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    cqops_residue r;

    stage(ctx);
    cqops_read_residue(&r);
    CHECK(r.stranded_qubits > 0u);

    /* A SECOND CASE IN A TEST BINARY MUST NOT INHERIT THE FIRST ONE'S RESIDUE,
     * for cq_shim_ctx_reset's own reason one subject over: the counters live in
     * the context, and the reset builds a fresh one. Read AFTER the reset and
     * before the next cq_shim_ctx(), which is the window where there is no
     * context at all. */
    cq_shim_ctx_reset();
    cqops_read_residue(&r);
    CHECK_EQ(r.stranded_dirty, 0u);
    CHECK_EQ(r.stranded_unproven, 0u);
    CHECK_EQ(r.frees_dirty, 0u);
    CHECK_EQ(r.frees_unproven, 0u);
    CHECK_EQ(r.stranded_qubits, 0u);
    CHECK_EQ(r.strand_reports, 0u);

    cqops_set_sink(NULL);
    cq_mock_dispose(&m);
}

CQ_TEST_MAIN(
    CQ_CASE(a_read_before_the_context_exists_is_zero_and_mints_nothing),
    CQ_CASE(every_field_comes_from_its_own_source),
    CQ_CASE(the_read_preserves_the_disagreement_between_the_two_grains),
    CQ_CASE(the_pool_total_is_read_from_the_pool_and_can_disagree),
    CQ_CASE(the_reset_clears_the_residue_because_it_belongs_to_the_context)
)
