/* tests/test_childcap_controls.inc — the CONTROLS for tests/support/childcap.c
 * (`bd 09z`). The subject is the HELPER, not this suite's own subject; it is
 * hosted here because tests/test_runtime_gate_rotate.inc is already where
 * childcap's requirement 1 is pinned, and because an .inc adds no ctest entry
 * and no line to tools/check_loc.sh's walk.
 *
 * WHAT A CONTROL IS FOR HERE. childcap.h enumerates five requirements and a
 * rider, and a mutation battery measured (2026-09-17, both configurations)
 * that only requirement 1 was pinned: the whole of requirement 3 — SIGABRT
 * SPECIFICALLY, never merely a non-zero exit — survived being weakened three
 * ways, because every child the corpus forks today either aborts or returns,
 * so `status != 0`, `WIFSIGNALED(status)` and a hard-wired `return 1` all agree
 * with the real predicate on every input it is ever given. They are EQUIVALENT
 * MUTANTS on that corpus, not dead code: the paired mutation in childcap.h's
 * last paragraph shows the line is the sole Release detector of "the refusal
 * printed correctly and did NOT abort". A control is the instrument that turns
 * an equivalent mutant back into a falsifiable one — it widens the corpus of
 * children rather than adding an assertion about the ones that exist.
 *
 * THE OBVIOUS SINGLE CONTROL DOES NOT WORK, and childcap.h records why: a
 * child that prints and RETURNS reaches _exit(0), so the true predicate and
 * `status != 0` BOTH report 0 and only `return 1` dies. Measured, per cell, as
 * the value cq_child_capture returns:
 *
 *      child                  true   status!=0   WIFEXITED&&code!=0   return 1   WIFSIGNALED
 *      prints, RETURNS           0           0                    0          1             0
 *      prints, _Exit(1)          0           1                    1          1             0
 *      prints, raise(SIGTERM)    0           1                    0          1             1
 *
 * so no ONE row discriminates all four weakenings and the two rows below do.
 * Both spellings of "non-zero exit" are covered because the bead named the
 * mutant as `status != 0` while its table read like the WIFEXITED one, and a
 * control that covers only the spelling someone wrote down is a control
 * scoped to one author's imagination.
 *
 * EVERY CONTROL ASSERTS THE BYTES AS WELL AS THE VERDICT. A capture that
 * silently did nothing also returns 0, so the verdict alone passes vacuously —
 * which is the same defect as an implication-shaped assertion against a module
 * that never folds.
 *
 * WHAT IS STILL NOT PINNED, stated plainly so the next reader does not take
 * this file for more than it is: requirement 5's read-side EINTR retry, the
 * rider's buffer clear, and streams_are_paired() itself. The reasons are at
 * their cases' absence below.
 */

/* `raise`/SIGTERM for the requirement-3 controls, and `alarm`/`write` for the
 * ta1 one. The host .c needs none of them; an .inc that brings its own is more
 * honest than widening the suite's include block for three cases. */
#include <signal.h>
#include <unistd.h>

/* The child's bytes. Verbatim through CHECK_STR_EQ, so a capture that dropped
 * or appended anything is a red case rather than a substring that still hits. */
#define CQ_CC_SAYS "childcap control child wrote this\n"

/* -------------------------------------------------------------------------
 * REQUIREMENT 3 — SIGABRT specifically.
 * ------------------------------------------------------------------------- */

/* _Exit, not exit: exit() runs atexit handlers, and in Debug LeakSanitizer is
 * one of them — its report would land on this child's stderr and be compared
 * against CQ_CC_SAYS. tests/support/death.c takes the same care for the same
 * reason. fflush is belt-and-braces: stderr is unbuffered by C's own rule, and
 * _Exit would not flush it if it were not. */
static void cc_prints_and_exits_one(void *p)
{
    (void)p;
    fputs(CQ_CC_SAYS, stderr);
    fflush(stderr);
    _Exit(1);
}

/* SIGTERM because it is NOT SIGABRT and its default disposition terminates.
 * Nothing in either configuration handles it: ASan takes SIGSEGV/SIGBUS, and
 * the harness installs nothing. */
static void cc_prints_and_raises_sigterm(void *p)
{
    (void)p;
    fputs(CQ_CC_SAYS, stderr);
    fflush(stderr);
    raise(SIGTERM);
    _Exit(0);                      /* unreachable; a return here would be a
                                    * SECOND way for the case to read 0 */
}

/* Kills `status != 0`, `WIFEXITED(status) && WEXITSTATUS(status) != 0`, and the
 * hard-wired `return 1`. Blind to WIFSIGNALED-without-SIGABRT, which is the
 * next case. */
CQ_TEST(a_child_that_prints_and_exits_nonzero_did_not_abort)
{
    char err[256];
    const int signalled = cq_child_capture(CQ_CHILD_STDERR,
                                           cc_prints_and_exits_one, NULL,
                                           NULL, 0u, err, sizeof err);

    CHECK_EQ(signalled, 0);
    CHECK_STR_EQ(err, CQ_CC_SAYS);
}

/* Kills WIFSIGNALED-without-the-SIGABRT-test, the raw `status != 0`, and
 * `return 1`. Blind to the WIFEXITED spelling, which the case above covers. */
CQ_TEST(a_child_that_prints_and_dies_on_a_non_sigabrt_signal_did_not_abort)
{
    char err[256];
    const int signalled = cq_child_capture(CQ_CHILD_STDERR,
                                           cc_prints_and_raises_sigterm, NULL,
                                           NULL, 0u, err, sizeof err);

    CHECK_EQ(signalled, 0);
    CHECK_STR_EQ(err, CQ_CC_SAYS);
}

/* -------------------------------------------------------------------------
 * REQUIREMENT 4 — fflush(NULL) BEFORE the fork.
 * ------------------------------------------------------------------------- */

/* Returns rather than _Exit()ing, so this child is also the "prints, RETURNS"
 * row of the table above and reaches childcap's own `_exit(0)`. The explicit
 * flush is REQUIRED: _exit does not flush, and without it the mutant and the
 * real thing would both capture nothing. */
static void cc_prints_on_stdout(void *p)
{
    (void)p;
    fputs(CQ_CC_SAYS, stdout);
    fflush(stdout);
}

/* THE FORK DUPLICATES THE PARENT'S STDIO BUFFER, so bytes the parent has
 * written and not flushed are written a SECOND time — by the child, into the
 * child's stdout, which here is the capture pipe. childcap.c measured the
 * symptom in the other direction (three `# FAIL` TAP lines for two failed
 * checks) and added fflush(NULL); nothing then asserted it.
 *
 * THE MARKER CARRIES NO TRAILING NEWLINE ON PURPOSE. Under ctest stdout is a
 * pipe and fully buffered, so anything would stay pending; run directly on a
 * tty it is LINE buffered, and a '\n'-terminated marker would have flushed
 * itself before the fork and left this case vacuously green in exactly the
 * configuration a maintainer reaches for by hand. The leading "\n# " and the
 * terminator after the capture keep the log a well-formed TAP comment. */
CQ_TEST(the_parents_pending_stdout_does_not_reappear_in_the_childs_capture)
{
    char out[512];

    fputs("\n# cqops: childcap requirement 4 marker", stdout);

    const int signalled = cq_child_capture(CQ_CHILD_STDOUT,
                                           cc_prints_on_stdout, NULL,
                                           out, sizeof out, NULL, 0u);

    fputs("\n", stdout);

    CHECK_EQ(signalled, 0);
    CHECK_STR_EQ(out, CQ_CC_SAYS);
}

/* -------------------------------------------------------------------------
 * THE REFUSAL PATH — the rider, and streams_are_paired().
 *
 * THESE TWO WERE REPORTED "UNREACHABLE BY A PASSING CASE" AND THAT WAS FALSE,
 * falsified by execution on 2026-09-17. The claim rested on "a test cannot
 * assert its own redness", which ignores tests/support/harness.h's own
 * FALSIFIABILITY section: cq_h_mute + cq_h_take_failures exist for exactly
 * this, under a heading that quotes the principle the false claim was invoking.
 * tests/test_kerneldrv.c's CQ_EXPECT_CAUGHT / CQ_EXPECT_CLEAN is the
 * established in-suite idiom and the macro below is that shape, local because
 * that file owns its copy privately.
 *
 * It is worth naming the failure mode rather than just fixing it: a comment in
 * a file whose ENTIRE PURPOSE is recording what is and is not pinned asserted a
 * measured-sounding fact that no test read. That is `src/angle.h`, and
 * childcap.h had already said the same paragraph had been wrong twice.
 * ------------------------------------------------------------------------- */

/* CQ_EXPECT_CAUGHT's shape, verbatim in the property that matters: the mute is
 * turned off on EVERY path including the failing one, and the count is taken
 * before anything is reported, so a provocation that records nothing still
 * leaves the harness working and a mute can never be left on to silence the
 * rest of the run. */
#define CQ_CC_EXPECT_REFUSED(what, stmt)                                      \
    do {                                                                      \
        cq_h_mute(1);                                                         \
        (void)cq_h_take_failures();                                           \
        stmt;                                                                 \
        int cq_n_ = cq_h_take_failures();                                     \
        cq_h_mute(0);                                                         \
        if (cq_n_ == 0)                                                       \
            cq_h_fail(__FILE__, __LINE__,                                     \
                      "cq_child_capture DID NOT REFUSE: %s", what);           \
    } while (0)

/* And the negative control for the macro itself, which is not optional: a
 * CQ_CC_EXPECT_REFUSED that counted the wrong thing — or a helper that
 * complained about every call — would read as perfect coverage. */
#define CQ_CC_EXPECT_ACCEPTED(what, stmt)                                     \
    do {                                                                      \
        cq_h_mute(1);                                                         \
        (void)cq_h_take_failures();                                           \
        stmt;                                                                 \
        int cq_n_ = cq_h_take_failures();                                     \
        cq_h_mute(0);                                                         \
        if (cq_n_ != 0)                                                       \
            cq_h_fail(__FILE__, __LINE__,                                     \
                      "a WELL-FORMED call was refused (%s): %d failure(s)",   \
                      what, cq_n_);                                           \
    } while (0)

/* Never reached on the green path — the guard refuses before the fork — so it
 * prints nothing, to keep a mutant's run quiet rather than plausible. */
static void cc_says_nothing(void *p) { (void)p; }

/* THE RIDER. childcap.c clears the buffers BEFORE anything can fail, so a
 * capture that never happened reports an empty string rather than the bytes of
 * the previous call. Nothing read that until now, and the reason it is easy to
 * miss is that cap_open clears again for any stream it actually drains — so the
 * clear is only OBSERVABLE on a call that is refused before reaching a read.
 * That is what the second call here is. */
CQ_TEST(a_refused_capture_reports_no_bytes_from_the_previous_one)
{
    char err[256], spare[8];

    const int signalled = cq_child_capture(CQ_CHILD_STDERR,
                                           cc_prints_and_exits_one, NULL,
                                           NULL, 0u, err, sizeof err);
    CHECK_EQ(signalled, 0);
    CHECK_STR_EQ(err, CQ_CC_SAYS);            /* the buffer now holds bytes */

    /* THE REFUSAL SHAPE IS CHOSEN, NOT THE FIRST ONE THAT REFUSES, and the
     * first draft got this wrong in a way worth recording. It used a mask
     * naming stdout with a NULL stdout buffer, which IS refused — but a mutant
     * that defeats the guard then reaches cap_open with that NULL and
     * SEGFAULTS, so the case "killed" the mutant by taking the binary down and
     * the two cases after this one never ran at all. A control that crashes
     * hides its neighbours exactly as a cheaper assertion hides an expensive
     * one.
     *
     * This shape instead supplies a buffer for a stream the mask does NOT
     * name — the transposed call streams_are_paired's own comment names. Every
     * stream that IS named still has a valid buffer, so a guard-defeating
     * mutant simply proceeds, forks, and captures the child's bytes into
     * `err`; the assertion below then reads them and goes red on an
     * assertion. */
    CQ_CC_EXPECT_REFUSED("a stdout buffer for a stderr-only capture",
        (void)cq_child_capture(CQ_CHILD_STDERR,
                               cc_prints_and_exits_one, NULL,
                               spare, sizeof spare, err, sizeof err));

    /* THE WHOLE CLAIM. Not "err is unchanged" — EMPTY. It is also what says
     * the call was refused rather than merely complained about: a capture that
     * went ahead leaves the child's bytes here. */
    CHECK_STR_EQ(err, "");
}

/* streams_are_paired() ITSELF. Three refusals, chosen so that each is HARMLESS
 * if the guard stops refusing: all three leave every named stream with a valid
 * buffer, so a guard that waved them through would fork and return rather than
 * dereference NULL. That matters — a control whose mutant SEGFAULTs kills the
 * binary and takes every later case with it, which is a worse instrument than a
 * red check even though ctest scores both as a failure. The genuinely
 * dangerous shapes (a named stream with a NULL buffer) are therefore NOT
 * provoked here; the guard is what makes them unreachable and nothing else. */
CQ_TEST(a_mask_and_buffer_that_disagree_are_refused_before_anything_forks)
{
    char err[256];

    CQ_CC_EXPECT_REFUSED("an empty stream mask",
        (void)cq_child_capture(0u, cc_says_nothing, NULL,
                               NULL, 0u, err, sizeof err));

    CQ_CC_EXPECT_REFUSED("a stream mask with an unknown bit",
        (void)cq_child_capture(4u, cc_says_nothing, NULL,
                               NULL, 0u, err, sizeof err));

    /* A capacity with no room for the NUL every path writes. `cap - 1u - n`
     * would wrap, so this one is a memory-safety refusal rather than a
     * bookkeeping one. */
    CQ_CC_EXPECT_REFUSED("a stderr buffer with a capacity of 1",
        (void)cq_child_capture(CQ_CHILD_STDERR, cc_says_nothing, NULL,
                               NULL, 0u, err, 1u));

    CQ_CC_EXPECT_ACCEPTED("stderr named, one stderr buffer",
        (void)cq_child_capture(CQ_CHILD_STDERR, cc_prints_and_exits_one, NULL,
                               NULL, 0u, err, sizeof err));
}

/* -------------------------------------------------------------------------
 * `bd ta1` — THE TWO DRAINS RUN TOGETHER.
 * ------------------------------------------------------------------------- */

/* THE HAZARD THIS DETECTS IS A HANG, NOT A WRONG ANSWER, so the detector has
 * to convert it into a bounded red. Under the sequential drain the parent read
 * stdout to EOF first; EOF on stdout only arrives when the child exits; so a
 * child that fills the stderr pipe before writing its stdout blocks in write()
 * while the parent blocks in read(), for ever. A ctest TIMEOUT is 1500 s by
 * default, which is not a test result anyone waits for.
 *
 * THE CHILD CARRIES THE SAFETY, not the parent — the parent IS the subject.
 * alarm(5) is armed before the flood, so a regression kills the child after
 * five seconds, the parent's stdout read gets EOF with no sentinel, and the
 * case goes RED in bounded time. On a correct drain the flood is consumed as
 * it is written, the child reaches its sentinel in milliseconds, and the alarm
 * dies with the process; the green path costs nothing. */
#define CQ_CC_FLOOD_BYTES (256u * 1024u)   /* >> any pipe capacity: Darwin
                                            * STARTS at 16 KiB and grows to
                                            * 64 KiB, Linux defaults to 64 */
#define CQ_CC_SENTINEL "the child got past the flood\n"

static void cc_floods_stderr_then_writes_stdout(void *p)
{
    char chunk[1024];
    unsigned written = 0u;

    (void)p;
    for (size_t i = 0; i < sizeof chunk; i++) chunk[i] = 'e';

    alarm(5);                      /* a regression is a red, not a hang */

    /* write(2), not fputs: stdio would buffer the flood in the CHILD and only
     * the final flush would block, which is the same deadlock reached by a
     * less direct route -- but it would also depend on the child's buffer
     * size, and this case must depend only on the PIPE's. */
    while (written < CQ_CC_FLOOD_BYTES) {
        const ssize_t got = write(STDERR_FILENO, chunk, sizeof chunk);
        if (got <= 0) break;
        written += (unsigned)got;
    }

    fputs(CQ_CC_SENTINEL, stdout);
    fflush(stdout);
}

/* The stderr buffer is deliberately FAR smaller than the flood, which is the
 * bead's counter-intuitive point made executable: under the old shape a small
 * buffer made things WORSE, because the drain stopped reading at the cap and
 * left the child blocked on a pipe nobody was emptying. Here it must not
 * matter -- the overflow is read and dropped, and what is kept is a prefix. */
CQ_TEST(a_child_that_fills_one_pipe_before_finishing_the_other_does_not_deadlock)
{
    char out[256], err[1024];
    const int signalled =
        cq_child_capture(CQ_CHILD_STDOUT | CQ_CHILD_STDERR,
                         cc_floods_stderr_then_writes_stdout, NULL,
                         out, sizeof out, err, sizeof err);

    /* THE SENTINEL IS THE WHOLE CLAIM: it is written AFTER the flood, so it can
     * only arrive if the child was never blocked. */
    CHECK_EQ(signalled, 0);
    CHECK_STR_EQ(out, CQ_CC_SENTINEL);

    /* And the short buffer holds a full prefix rather than nothing -- a drain
     * that gave up on the capped stream would still pass the sentinel check if
     * it gave up AFTER the child finished, so this is the second half. */
    CHECK_EQ(strlen(err), sizeof err - 1u);
    CHECK_EQ(err[0], 'e');
    CHECK_EQ(err[sizeof err - 2u], 'e');
}
