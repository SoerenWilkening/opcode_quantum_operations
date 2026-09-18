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
 * this file for more than it is: requirement 5's READ-side EINTR retry, and
 * that one alone. The rider's buffer clear and streams_are_paired() were
 * closed by the fix round of the same day; the POLL-side and WAITPID-side
 * EINTR retries were closed on 2026-09-17 by the two cases at the end of this
 * file (`bd 698`). The read-side reason is at its case's absence there, and it
 * is a MEASUREMENT rather than an apology.
 */

/* `raise`/SIGTERM for the requirement-3 controls, `alarm`/`write` for the ta1
 * one, and `setitimer`/`nanosleep` for the two EINTR ones. The host .c needs
 * none of them; an .inc that brings its own is more honest than widening the
 * suite's include block. */
#include <signal.h>
#include <sys/time.h>
#include <time.h>
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

/* -------------------------------------------------------------------------
 * REQUIREMENT 5 — the EINTR retries (`bd 698`).
 *
 * THERE ARE THREE EINTR SITES IN childcap.c, NOT TWO: read() (cap_step),
 * poll() (drain_both, ADDED by `bd ta1`) and waitpid().
 *
 * AND THIS PARAGRAPH WAS ITSELF STALE FOR A WHILE, WHICH IS WHY THE COUNT IS
 * NOW A BUILD ERROR (`bd 7b5`). It used to report, in the present tense, that
 * childcap.h's requirement 5 SAID "read() and waitpid()" while a later
 * paragraph of the same file SAID poll() and read() — three documents naming
 * three different pairs out of a set of three. `bd 698` corrected both of those
 * sentences and this one went on describing their old wording, so the defect
 * reproduced itself one level up in the act of being written down. The COUNT is
 * now measured out of childcap.c at configure time and _Static_asserted there;
 * what no build can check is REACHABILITY, which is what the three verdicts
 * below are about. They are NOT equally reachable, and the difference is
 * structural rather than statistical:
 *
 *   poll()    — where the parent actually blocks. Pinned below.
 *   waitpid() — reachable whenever the child outlives its own output. Pinned
 *               below, and it needs a DIFFERENT child: the drain must reach
 *               EOF while the process is still alive.
 *   read()    — NOT pinned, and not for want of trying. cap_step runs only
 *               after poll() has reported the fd readable or hung up, so the
 *               read never sleeps, so it cannot be interrupted before it
 *               transfers. MEASURED rather than argued (2026-09-17, both
 *               configurations, a scratch build of childcap.c with a counter
 *               in each of the three EINTR branches): over 100 captures of a
 *               child dribbling 40 separate one-byte writes — 4,000 read()
 *               calls — under a 1 ms repeating SIGALRM, the branch counts were
 *               poll 21,489 / wait 4 / read 0 in Release and poll 32,159 /
 *               wait 1 / read 0 in Debug. Both read-side mutants (EINTR as
 *               EOF, and EINTR falling through to cq_h_fail) SURVIVE 100/100
 *               in both configurations. A control for it would not be flaky;
 *               it would be VACUOUS, which is worse, so the gap is recorded.
 *
 * THE INSTRUMENT IS AN ITIMER IN THE PARENT, NOT A SIGNAL FROM THE CHILD, and
 * that is what makes these two cases deterministic rather than a timing race.
 * The child is made to take ~100 ms; a 5 ms repeating SIGALRM therefore fires
 * ~20 times INSIDE the one blocking call the case is aiming at. Measured over
 * 100 runs per mode per configuration: 0 failures on the real childcap.c, and
 * 100/100 kills of the matching mutant.
 *
 * sa_flags = 0 IS THE WHOLE MECHANISM. With SA_RESTART the kernel restarts the
 * interrupted call itself and EINTR never reaches our code, so the case would
 * be green against any childcap at all.
 * ------------------------------------------------------------------------- */

static volatile sig_atomic_t cc_ticks;
static void cc_count_a_tick(int sig) { (void)sig; cc_ticks++; }

static struct sigaction cc_old_alrm;

static void cc_nap_ms(long ms)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(ms / 1000);
    ts.tv_nsec = (long)((ms % 1000) * 1000000L);
    (void)nanosleep(&ts, NULL);
}

static void cc_arm_interrupter(long period_ms)
{
    struct sigaction sa;
    struct itimerval it;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = cc_count_a_tick;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;                   /* NO SA_RESTART -- see the note above */
    if (sigaction(SIGALRM, &sa, &cc_old_alrm) != 0)
        cq_h_fail(__FILE__, __LINE__, "sigaction(SIGALRM) failed");

    cc_ticks = 0;
    it.it_value.tv_sec  = 0;
    it.it_value.tv_usec = (suseconds_t)(period_ms * 1000L);
    it.it_interval = it.it_value;
    if (setitimer(ITIMER_REAL, &it, NULL) != 0)
        cq_h_fail(__FILE__, __LINE__, "setitimer(ITIMER_REAL) failed");
}

/* TEARDOWN ORDER IS LOAD-BEARING AND THE OBVIOUS ORDER IS WRONG. Disarming the
 * timer does not discard a SIGALRM that has ALREADY fired, so restoring the old
 * disposition -- SIG_DFL, since nothing else in this tree installs one --
 * delivers it with default action and kills the test binary with "Alarm clock".
 * Measured while building this: two of six scratch runs at a 1 ms period died
 * that way. Setting the disposition to SIG_IGN first DISCARDS the pending
 * instance, so the restore is safe.
 *
 * AND THE RESTORE PROTECTS A CASE THAT IS NOT THIS ONE. The ta1 flood child
 * relies on alarm(5) TERMINATING it if the drain regresses; a forked child
 * inherits the parent's disposition, so leaving cc_count_a_tick installed would
 * turn that watchdog into a counter bump and the bounded red back into an
 * unbounded hang. The read-back below is what makes that an assertion instead
 * of a comment. */
static int cc_disarm_interrupter(void)
{
    struct sigaction ign, now;
    struct itimerval off;

    off.it_value.tv_sec = 0; off.it_value.tv_usec = 0;
    off.it_interval = off.it_value;
    (void)setitimer(ITIMER_REAL, &off, NULL);

    memset(&ign, 0, sizeof ign);
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    ign.sa_flags = 0;
    (void)sigaction(SIGALRM, &ign, NULL);

    (void)sigaction(SIGALRM, &cc_old_alrm, NULL);

    memset(&now, 0, sizeof now);
    (void)sigaction(SIGALRM, NULL, &now);
    CHECK(now.sa_handler == cc_old_alrm.sa_handler);

    return (int)cc_ticks;
}

/* Returns rather than _Exit()ing, so it is the "prints, RETURNS" row of the
 * table at the top of this file with a delay in front. The delay is the point:
 * while it runs, the parent is inside poll() with nothing to read. */
static void cc_sleeps_then_prints_on_stderr(void *p)
{
    (void)p;
    cc_nap_ms(100);
    fputs(CQ_CC_SAYS, stderr);
    fflush(stderr);
}

/* Kills both poll-side mutants: EINTR treated as an end (`break`), and the
 * EINTR branch deleted so it falls through to cq_h_fail. The SECOND assertion
 * is the one with teeth -- the `break` spelling is SILENT, so a case that only
 * checked the verdict would pass while the capture came back empty. */
CQ_TEST(a_signal_arriving_while_the_parent_blocks_in_poll_does_not_end_the_drain)
{
    char err[256];
    int  signalled, ticks;

    cc_arm_interrupter(5);
    signalled = cq_child_capture(CQ_CHILD_STDERR,
                                 cc_sleeps_then_prints_on_stderr, NULL,
                                 NULL, 0u, err, sizeof err);
    ticks = cc_disarm_interrupter();

    /* NON-VACUITY FIRST. With no signal delivered this case asserts nothing
     * about EINTR and would be green against any drain whatsoever -- the same
     * defect as an implication-shaped assertion over a module that never
     * folds. Measured at ~20 (Release) and ~40 (Debug); 2 is a floor. */
    CHECK(ticks >= 2);
    CHECK_EQ(signalled, 0);
    CHECK_STR_EQ(err, CQ_CC_SAYS);
}

/* THE WAITPID WINDOW HAS TO BE MANUFACTURED. In every other child here the
 * drain ends BECAUSE the child exited, so the parent reaches waitpid() with the
 * status already available and the call never blocks -- measured at 4 EINTRs
 * per 100 captures, i.e. a real race and a useless control. Closing the pipe
 * first separates the two events: the drain sees EOF, the parent parks in
 * waitpid(), and the child is still alive for another ~100 ms. That took the
 * count to 1,997 of 2,000 signal deliveries.
 *
 * IT MUST ABORT, NOT EXIT. Without the retry waitpid() returns -1 and `status`
 * keeps its initial 0, so WIFSIGNALED(0) is false and the verdict is 0 -- which
 * AGREES with the true answer for every child that does not abort. Only a child
 * whose correct verdict is 1 can tell the two apart.
 *
 * write(2) rather than fputs: after close(STDERR_FILENO) the stdio stream is
 * attached to a closed descriptor, so the flush has to precede the close and
 * the direct write makes that ordering unmissable. */
static void cc_closes_its_pipe_then_lingers_and_aborts(void *p)
{
    (void)p;
    (void)write(STDERR_FILENO, CQ_CC_SAYS, sizeof CQ_CC_SAYS - 1u);
    (void)close(STDERR_FILENO);        /* the parent's drain now sees EOF */
    cc_nap_ms(100);
    abort();
}

CQ_TEST(a_signal_arriving_while_the_parent_blocks_in_waitpid_keeps_the_verdict)
{
    char err[256];
    int  signalled, ticks;

    cc_arm_interrupter(5);
    signalled = cq_child_capture(CQ_CHILD_STDERR,
                                 cc_closes_its_pipe_then_lingers_and_aborts,
                                 NULL, NULL, 0u, err, sizeof err);
    ticks = cc_disarm_interrupter();

    CHECK(ticks >= 2);
    CHECK_EQ(signalled, 1);
    CHECK_STR_EQ(err, CQ_CC_SAYS);
}
