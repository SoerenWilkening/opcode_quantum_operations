/* tests/test_shim_cert_death.c — the certificate's hard errors.
 *
 * A SEPARATE BINARY RATHER THAN A SEAM, on tests/test_template_death.c's and
 * tests/test_runtime_rail_death.c's precedent: `CQ_DEATH_MAIN` needs its own
 * translation unit, so this is not a Rule 12 split of tests/test_shim_cert.c.
 * IMPLEMENTATION_PLAN §3 records it as its own artefact with its own reserve
 * seam — the same `the ENGINE ↔ the ENTRY CONDITIONS` cut, to
 * tests/test_shim_cert_death_rules.inc.
 *
 * IT EXISTS BECAUSE A MUTANT SURVIVED. Turning `cq_rec_push`'s refusal of an
 * unmodelled opcode from an `abort()` into a `return` left the whole suite
 * green, because nothing in the tree ever pushed one. That refusal is the
 * single most load-bearing line in the recorder: a silently ignored call makes
 * EVERY rule wider — the rail's history loses a write it really took — and a
 * wider rule DISCHARGES frees it should not, which is the unsound direction.
 * It is upstream's own posture ("an unclassified symbol is a loud stop, never a
 * skipped check") and the reason to keep it is the same there and here. */
#include "cq_shim_ctx.h"
#include "cq_shim_record.h"

#include "reg.h"
#include "sink.h"

#include "cqops/cqops.h"

#include "support/death.h"
#include "support/harness.h"

#include <stdlib.h>
#include <string.h>

static void fresh(void)
{
    cq_sink_reset();
    unsetenv("CQOPS_SINK");
    cq_shim_ctx_reset();
}

/* An opcode the effect table does not model. `CQ_ROP_NONE` is the reachable
 * spelling of it and is also the ZERO row, which is why the record's own
 * `_Static_assert` pins `CQ_ROP_NONE == 0`: a caller who forgets to assign
 * `op` gets the refusal rather than an `x` recorded against handle 0. */
static void an_unmodelled_opcode_is_a_hard_error(void)
{
    cq_call_rec c;

    fresh();
    (void)cq_shim_ctx();
    memset(&c, 0, sizeof c);
    c.h[0] = 0; c.h[1] = -1; c.h[2] = -1; c.h[3] = -1;
    c.ctrl = -1;
    /* op left at CQ_ROP_NONE, which is what a memset gives and what a caller
     * who forgot to set it would produce. */
    CQ_EXPECT_ABORT(cq_rec_push(&c));
}

/* THE OUT-OF-RANGE ROW, which is the same refusal reached by the other door:
 * `cq_rec_effect` returns NULL above the enumerator range, and a v2 that adds a
 * `cqrt_qram_*` opcode without adding its row lands exactly here. */
static void an_opcode_past_the_table_is_a_hard_error(void)
{
    cq_call_rec c;

    fresh();
    (void)cq_shim_ctx();
    memset(&c, 0, sizeof c);
    c.op   = (uint16_t)CQ_ROP__N;   /* one past the last modelled row */
    c.h[0] = 0; c.h[1] = -1; c.h[2] = -1; c.h[3] = -1;
    c.ctrl = -1;
    CQ_EXPECT_ABORT(cq_rec_push(&c));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(an_unmodelled_opcode_is_a_hard_error),
    CQ_DEATH_CASE(an_opcode_past_the_table_is_a_hard_error)
)
