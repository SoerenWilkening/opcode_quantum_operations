/* Step 23, landing 1, step 3: M26's foundation — shim/cq_shim_ctx.[ch].
 *
 * FOUR SUBJECTS, and each one is here because something ELSE would otherwise be
 * green for the wrong reason:
 *
 *   1. THE PROCESS-GLOBAL cq_ctx AND ITS RESET. cq_ctx_init borrows the sink
 *      ONCE (src/ctx.c), so a second case's cqops_set_sink() is ignored by an
 *      already-latched context and its gates land in the FIRST case's recorder.
 *      `a_second_case_records_into_its_own_sink` is the case that fails without
 *      cq_shim_ctx_reset, and it asserts that mock2 SAW the gate rather than
 *      that it saw none. That is not a stylistic preference: measured in a
 *      throwaway program against a latch with no reset, "mock2 recorded zero
 *      gates" PASSES — the gate went to the other recorder — while "mock2 saw
 *      exactly one gate" goes red. The zero-gates spelling is the one a reader
 *      reaches for, because it is the shape Rule 10's L5 row uses for the
 *      classical short-circuit; it is the wrong shape here and there is
 *      deliberately no such assertion in this file.
 *
 *   2. SINK INSTALLATION (bd utk). cq_sink_printf_register and
 *      cq_sink_counter_register had NO caller in the shipping library, so any
 *      process that had not called cqops_set_sink() hard-errored inside
 *      cq_ctx_init with "no default sink registered". M26 is the library's first
 *      real entry point and installs them — checked per (re-)init rather than
 *      once per process, because cq_sink_reset() unregisters "printf" and a
 *      once-per-process install then aborts on the next context.
 *
 *   3. THE ONE PRD §9 REGION BRACKET (bd d6m fix (a)). Three arms: balance over
 *      row 0's kinds; a CHARACTERISATION pinning that a freed control rail is
 *      caught by NOTHING today; and a LOCATION assertion that the bracket exists
 *      in exactly one shim file.
 *
 *   4. cq_shim_unsupported (PRD §1). Its message is pinned test-side in Python
 *      against a stub tests/test_gen_bodies.py writes for itself; this is the
 *      first thing that reads the REAL definition's bytes.
 *
 * SPLIT ON THE MODULE'S OWN SEAM, recorded in IMPLEMENTATION_PLAN §3 before a
 * line was written and taken when this file reached 410 of 300: subjects 3 and
 * 4 — `the BOUNDARY VOCABULARY` — live in tests/test_shim_ctx_region.inc, and
 * what stays here is `the process CONTEXT`. Not a size cut; the same seam is
 * shim/cq_shim_ctx.c's own reserve seam.
 *
 * POSIX IN A TEST, TWICE, for the same reason tests/test_sink.c uses setenv.
 * fork/pipe/waitpid, because subject 4's assertion is about a process that
 * ABORTS and the only way to read its stderr is from another process; and
 * opendir/readdir/stat, because subject 3's third arm is a claim about WHICH
 * FILES exist and a hard-coded list would go silently vacuous for every file
 * Steps 23.4-23.7 add. The LIBRARY uses nothing beyond C11 plus getenv
 * (PRD §14); this is the test side.
 */

#include "cq_shim.h"
#include "cq_shim_ctx.h"

#include "bit.h"
#include "controlled.h"
#include "emit.h"
#include "reg.h"
#include "sink.h"
#include "sink_count.h"

#include "support/bitkinds.h"
#include "support/harness.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"

#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* Every case starts from a known SELECTION state and a known CONTEXT. The two
 * are independent and the order between them does not matter: nothing is
 * latched until the next cq_shim_ctx(), which is what resolves the sink. What
 * matters is that BOTH precede that call. */
static void fresh(void)
{
    cq_sink_reset();
    unsetenv("CQOPS_SINK");
    cq_shim_ctx_reset();
}

/* A rail of `W` bits whose bit 0 carries `value` on a qubit if `q` is set. The
 * four handle-expressible row-0 control kinds are exactly cq_bk_reg at W = 1:
 * (0,0) ZERO, (1,0) ONE, (0,1) Q with shadow 0, (1,1) Q with shadow 1 — the
 * same four spellings tests/support/kernelctrl.c mints for CQ_KD_CTRL_*. */
static int32_t flag(cq_ctx *ctx, uint64_t value, uint64_t q)
{
    return cq_bk_reg(ctx, 1u, value, q);
}

/* -------------------------------------------------------------------------
 * 1. The process-global context, and the reset that makes it testable.
 * ------------------------------------------------------------------------- */

CQ_TEST(the_context_is_one_object_for_the_life_of_the_process)
{
    cq_mock m;
    cq_mock_init(&m);
    cq_sink s = cq_mock_sink(&m);

    fresh();
    cqops_set_sink(&s);

    cq_ctx *a = cq_shim_ctx();
    cq_ctx *b = cq_shim_ctx();
    CHECK(a != NULL);
    CHECK(a == b);                    /* not a fresh context per call */

    /* And a second touch does not re-init: state survives it. */
    (void)cq_bk_reg(a, 4u, 0xAu, 0xFu);
    const uint32_t live = cq_qubits_live(&cq_shim_ctx()->pool);
    CHECK_EQ(live, 4);

    cq_shim_ctx_reset();
    cqops_set_sink(NULL);
    cq_mock_dispose(&m);
}

CQ_TEST(the_reset_hands_back_an_empty_pool_and_an_empty_table)
{
    cq_mock m;
    cq_mock_init(&m);
    cq_sink s = cq_mock_sink(&m);

    fresh();
    cqops_set_sink(&s);

    cq_ctx *ctx = cq_shim_ctx();
    (void)cq_bk_reg(ctx, 8u, 0xFFu, 0xFFu);
    CHECK_EQ(cq_qubits_live(&ctx->pool), 8);
    CHECK(cq_reg_count(&ctx->regs) > 0);

    cq_shim_ctx_reset();

    ctx = cq_shim_ctx();
    CHECK_EQ(cq_qubits_live(&ctx->pool), 0);
    CHECK_EQ(cq_qubits_minted(&ctx->pool), 0);
    CHECK_EQ(cq_reg_count(&ctx->regs), 0);
    CHECK_EQ(cq_ctrl_depth(ctx), 0);

    cq_shim_ctx_reset();
    cqops_set_sink(NULL);
    cq_mock_dispose(&m);
}

/* THE CASE THAT IS RED WITHOUT cq_shim_ctx_reset, and the reason the reset is
 * not optional decoration. src/ctx.c resolves the sink at construction:
 *
 *     ctx->sink = sink ? sink : cq_sink_active();
 *
 * so a latched process-global context keeps case 1's recorder for the life of
 * the process. Measured against a latch with no reset, identically in Debug,
 * Release and a from-source ASan build: `after case 2: mock1.n=2 mock2.n=0`.
 *
 * THE ASSERTION IS L4-SHAPED ON PURPOSE. Asked the L5 way — "mock2 recorded
 * ZERO gates", which is the shape Rule 10's L5 row mandates for a classical
 * short-circuit — the broken context PASSES, because the gates went somewhere
 * else entirely. Only requiring mock2 to have SEEN the gate can tell "the
 * short-circuit worked" from "the recorder was never connected". */
CQ_TEST(a_second_case_records_into_its_own_sink)
{
    cq_mock m1, m2;
    cq_mock_init(&m1);
    cq_mock_init(&m2);
    cq_sink s1 = cq_mock_sink(&m1);
    cq_sink s2 = cq_mock_sink(&m2);

    /* Case 1. */
    fresh();
    cqops_set_sink(&s1);
    (void)cq_bk_reg(cq_shim_ctx(), 1u, 1u, 1u);   /* materialise a ONE -> one X */
    CHECK_EQ(cq_mock_count(&m1), 1);
    CHECK_EQ(cq_mock_count(&m2), 0);

    /* Case 2, with its own recorder — and its own pool. */
    cqops_set_sink(&s2);
    cq_shim_ctx_reset();
    CHECK_EQ(cq_qubits_live(&cq_shim_ctx()->pool), 0);   /* no residue from case 1 */

    (void)cq_bk_reg(cq_shim_ctx(), 1u, 1u, 1u);
    CHECK_EQ(cq_mock_count(&m2), 1);                     /* L4-shaped: it SAW it */
    CHECK_EQ(cq_mock_count(&m1), 1);                     /* and case 1 is untouched */

    cq_shim_ctx_reset();
    cqops_set_sink(NULL);
    cq_mock_dispose(&m1);
    cq_mock_dispose(&m2);
}

/* The other half of the reset's contract: it must NOT clear the selection.
 * cq_sink_reset() clears the registry AND the cqops_set_sink override, so a
 * reset that called it would silently discard a test's deliberate sink and send
 * the gates to stdout with no diagnostic. */
CQ_TEST(the_reset_leaves_the_sink_selection_alone)
{
    cq_mock m;
    cq_mock_init(&m);
    cq_sink s = cq_mock_sink(&m);

    fresh();
    cqops_set_sink(&s);
    (void)cq_shim_ctx();

    cq_shim_ctx_reset();

    CHECK(cq_shim_ctx()->sink == &s);
    (void)cq_bk_reg(cq_shim_ctx(), 1u, 1u, 1u);
    CHECK_EQ(cq_mock_count(&m), 1);

    cq_shim_ctx_reset();
    cqops_set_sink(NULL);
    cq_mock_dispose(&m);
}

/* THE RESET REALLY DISPOSES, AND NOTHING ELSE IN THE FILE CAN SEE THAT.
 * Every other assertion about the reset is taken through a RE-FETCH, and
 * cq_ctx_init re-initialises all four members by assigning NULL over their
 * allocations WITHOUT freeing them — so a reset that only cleared the latch
 * produces a byte-identical empty context and stays green. Measured: with the
 * cq_ctx_dispose call deleted, 14/14 + 5/5 remained green in both
 * configurations while 20,000 reset cycles took peak RSS from 1.3 MB to 344 MB.
 * NO SANITIZER WOULD HAVE SAID SO AT THE TIME, AND ONE WOULD NOW -- which is
 * why this case is kept rather than retired. bd 6wg made Debug's ASan live and
 * bd kfi turned LeakSanitizer on with it (Debug only, probed, ASAN_OPTIONS=
 * detect_leaks=1 written into every test's CTest ENVIRONMENT property, since
 * that property WINS over the shell). LSan would now catch the deleted dispose
 * as a leak. It would NOT catch what this case catches: LSan speaks at process
 * exit about heap blocks, and says nothing about WHICH statement was supposed to
 * free them or about the two OBSERVABLE consequences asserted below -- an empty
 * handle table and a NULLed sink. It is also silent in Release, where this case
 * runs. Two detectors for one defect, neither subsuming the other.
 *
 * Reading the SAME cq_ctx object BETWEEN the reset and the next cq_shim_ctx()
 * is what discriminates: `sink` is NULLed by cq_ctx_dispose and the table is
 * emptied by it, and neither happens if the dispose is gone. The pointer stays
 * valid — g_ctx is a file-static that is never freed — so this is a legal read
 * of a disposed object, not a use-after-free. */
CQ_TEST(the_reset_disposes_the_context_rather_than_only_dropping_the_latch)
{
    cq_mock m;
    cq_mock_init(&m);
    cq_sink s = cq_mock_sink(&m);

    fresh();
    cqops_set_sink(&s);

    cq_ctx *c = cq_shim_ctx();
    (void)cq_reg_alloc_zero(&c->regs, 4u);
    CHECK_EQ(cq_reg_count(&c->regs), 1);
    CHECK(c->sink == &s);

    cq_shim_ctx_reset();

    CHECK_EQ(cq_reg_count(&c->regs), 0);   /* the table was disposed */
    CHECK(c->sink == NULL);                /* src/ctx.c's last statement */

    cqops_set_sink(NULL);
    cq_mock_dispose(&m);
}

/* -------------------------------------------------------------------------
 * 2. Sink installation (bd utk).
 * ------------------------------------------------------------------------- */

/* bd utk's case. With no override and no CQOPS_SINK, cq_ctx_init resolves the
 * documented default name "printf" — which resolved to NOTHING until M26 called
 * cq_sink_printf_register, and hard-errored `libcqops: FATAL: sink: no default
 * sink registered (-)`. Reaching the second line of this case IS the assertion;
 * the registry checks say which sinks got there. */
CQ_TEST(the_first_touch_installs_the_built_in_sinks)
{
    fresh();
    cqops_set_sink(NULL);

    CHECK(cq_sink_by_name("printf") == NULL);     /* the state bd utk describes */
    CHECK(cq_sink_by_name("counter") == NULL);

    cq_ctx *ctx = cq_shim_ctx();                  /* would abort before Step 23 */

    CHECK(ctx != NULL);
    CHECK(cq_sink_by_name("printf") != NULL);
    CHECK(cq_sink_by_name("counter") != NULL);
    CHECK(ctx->sink == cq_sink_by_name("printf"));

    cq_shim_ctx_reset();
}

/* INSTALLATION IS PER (RE-)INIT, NOT ONCE PER PROCESS, and that is forced
 * rather than tidy: cq_sink_reset() sets n_registered = 0, so a shim that
 * installed once would find "printf" gone and abort on the next context. The
 * registry replaces by name (src/sink.c), so re-installing is idempotent —
 * measured: same address, and the counter is not zeroed by a second install. */
CQ_TEST(the_built_ins_come_back_after_a_sink_reset)
{
    fresh();
    cqops_set_sink(NULL);
    const cq_sink *first = cq_shim_ctx()->sink;

    cq_sink_reset();                       /* unregisters "printf" */
    unsetenv("CQOPS_SINK");
    CHECK(cq_sink_by_name("printf") == NULL);

    cq_shim_ctx_reset();
    CHECK(cq_shim_ctx()->sink == first);   /* re-installed, same instance */
    CHECK(cq_sink_by_name("printf") != NULL);

    cq_shim_ctx_reset();
}

/* Installing must not displace a caller who has already chosen. cq_sink_active
 * short-circuits on the override before it consults the registry, so the two
 * cannot fight — but nothing asserted it, and "the shim quietly took the sink
 * back" is the exact class of bug bd utk warns about in the other direction. */
CQ_TEST(installing_does_not_displace_an_explicit_override)
{
    cq_mock m;
    cq_mock_init(&m);
    cq_sink s = cq_mock_sink(&m);

    fresh();
    cqops_set_sink(&s);

    cq_ctx *ctx = cq_shim_ctx();
    CHECK(ctx->sink == &s);
    CHECK(cq_sink_by_name("printf") != NULL);   /* installed anyway */
    CHECK(cq_sink_by_name("printf") != &s);

    cq_shim_ctx_reset();
    cqops_set_sink(NULL);
    cq_mock_dispose(&m);
}

/* WHAT THIS ADDS, STATED EXACTLY. That the counter is REGISTERED is already
 * pinned four cases above; what is unique here is the whole PRD §8 selection
 * path driven end to end — CQOPS_SINK names it, cq_ctx_init resolves it, and a
 * real gate lands in a real counter — which is how CQ_lang's fixtures choose a
 * sink with no call into the library. (An earlier draft of this comment claimed
 * to be the only case that could see the counter at all; that was false, and it
 * was falsified by execution rather than by reading.) */
CQ_TEST(the_environment_can_select_the_installed_counter)
{
    fresh();
    cqops_set_sink(NULL);
    setenv("CQOPS_SINK", "counter", 1);

    cq_ctx *ctx = cq_shim_ctx();
    CHECK(ctx->sink == cq_sink_by_name("counter"));

    /* THE COUNTER IS REACHED THROUGH THE REGISTRY, NOT BY REGISTERING IT AGAIN.
     * Calling cq_sink_counter_register() here would work — it hands back the
     * same process-global instance every time — but it would also INSTALL the
     * sink, which is the very thing this case exists to check the shim did.
     * Reading `user` off the registered vtable asks the registry for what the
     * SHIM put there, so a shim that installed nothing cannot be rescued by the
     * fixture. */
    cq_counter *c = (cq_counter *)cq_sink_by_name("counter")->user;
    CHECK(c != NULL);
    cq_count_reset(c);
    (void)cq_bk_reg(ctx, 3u, 0x7u, 0x7u);         /* three ONE bits -> three X */
    CHECK_EQ(c->x, 3);
    CHECK_EQ(cq_count_total(c), 3);

    fresh();
}

#include "test_shim_ctx_region.inc"

CQ_TEST_MAIN(
    CQ_CASE(the_context_is_one_object_for_the_life_of_the_process),
    CQ_CASE(the_reset_hands_back_an_empty_pool_and_an_empty_table),
    CQ_CASE(a_second_case_records_into_its_own_sink),
    CQ_CASE(the_reset_leaves_the_sink_selection_alone),
    CQ_CASE(the_reset_disposes_the_context_rather_than_only_dropping_the_latch),
    CQ_CASE(the_first_touch_installs_the_built_in_sinks),
    CQ_CASE(the_built_ins_come_back_after_a_sink_reset),
    CQ_CASE(installing_does_not_displace_an_explicit_override),
    CQ_CASE(the_environment_can_select_the_installed_counter),
    CQ_CASE(every_control_kind_leaves_the_region_balanced),
    CQ_CASE(a_promoted_toffoli_returns_the_regions_shared_ancilla),
    CQ_CASE(a_region_outliving_its_control_rail_is_caught_by_nothing),
    CQ_CASE(the_token_scanner_can_tell_a_call_from_a_mention),
    CQ_CASE(the_region_bracket_lives_in_exactly_one_shim_file),
    CQ_CASE(the_v1_boundary_prints_prd_1s_message_and_aborts),
    CQ_CASE(the_boundary_carries_whatever_reason_its_caller_names),
    CQ_CASE(a_null_symbol_or_reason_is_still_a_well_formed_refusal),
    CQ_CASE(each_region_refusal_names_itself_on_stderr)
)
