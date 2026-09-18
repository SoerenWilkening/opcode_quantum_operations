/* tests/support/childcap.c — the one fork-and-capture helper (`bd ula`).
 *
 * Extracted verbatim in behaviour from the three copies the bead names:
 * tests/test_shim_ctx_region.inc (stderr, void* callback),
 * tests/test_runtime_gate_rotate.inc (BOTH streams, two pipes) and
 * tests/test_runtime_v2_message.inc (stderr, nullary callback). Every comment
 * below that states a measured fact came with the copy that measured it.
 */
#include "support/childcap.h"

#include "support/harness.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

/* `bd 7b5` — REQUIREMENT 5'S COUNT IS EXECUTED RATHER THAN ASSERTED IN PROSE.
 *
 * CQOPS_CHILDCAP_EINTR_SITES is counted OUT OF THIS FILE at configure time
 * (tests/CMakeLists.txt), so the left side below is what the code has and the
 * right side is what childcap.h's requirement 5 claims it has. The two are
 * independent, which is the whole point: `bd ta1` added the poll() retry and
 * requirement 5 went on naming a pair, in a green tree, because nothing in this
 * project reads a comment. Adding or deleting a retry site now fails to
 * COMPILE, and the fix is to re-read requirement 5 and the three-verdict table
 * in tests/test_childcap_controls.inc before touching the 3.
 *
 * The #error arm carries as much as the assert: a drift gate that skips when
 * its input is missing is a gate that is off. */
#ifndef CQOPS_CHILDCAP_EINTR_SITES
#error "CQOPS_CHILDCAP_EINTR_SITES undefined: tests/CMakeLists.txt counts it out of this file and must be what compiles it (`bd 7b5`)."
#endif
_Static_assert(CQOPS_CHILDCAP_EINTR_SITES == 3,
               "childcap.c's retry sites are no longer the THREE that "
               "childcap.h's requirement 5 enumerates (read, poll, waitpid). "
               "Re-measure requirement 5 and the three verdicts in "
               "tests/test_childcap_controls.inc, then change the count.");

/* One capture in flight. `fd < 0` means this stream has ended and is out of
 * the poll set; a stream never selected is born that way. */
typedef struct {
    int    fd;
    char  *buf;
    size_t cap;
    size_t n;
} cq_cap;

static void cap_open(cq_cap *s, int fd, char *buf, size_t cap)
{
    s->fd = fd; s->buf = buf; s->cap = cap; s->n = 0u;
    if (fd >= 0) buf[0] = '\0';       /* an UNSELECTED stream may be NULL */
}

/* REQUIREMENT 5's FIRST SITE. EOF is the only end: EINTR is a signal arriving
 * mid-read, not the child finishing, and treating it as one truncates the
 * capture to whatever had arrived.
 *
 * IT IS ALSO THE ONE SITE OF THE THREE THAT IS UNPINNED, and `bd 698` declined
 * it on a MEASUREMENT rather than on flakiness. This read runs only after
 * poll() has reported the fd readable or hung up, so it never sleeps and cannot
 * be interrupted before transferring. Instrumented in a scratch copy: 0 entries
 * of this branch across 4,000 read() calls under 21,489 signal deliveries
 * (Release; 32,159 in Debug). Both mutants of this line survive in both
 * configurations. A control here would be vacuous, not flaky — childcap.h has
 * the full statement.
 *
 * AND A FULL BUFFER IS NOT AN END EITHER (`bd ta1`). This used to `break` at
 * the cap, which leaves the child blocked in write() on a pipe nobody is
 * emptying — so a SMALL capture buffer made the hazard WORSE, not safer, which
 * is the counter-intuitive half of that bead. Past the cap the bytes are read
 * and DROPPED: the stored prefix is byte-for-byte what the old shape stored,
 * and the child always gets to finish. Confirmed directly (`bd 0on`, probe
 * below): the flood fixture stores 1,023 and drops 261,121, which sum to
 * exactly its 256 KiB — every byte was read, so the drain does reach EOF.
 *
 * NOTHING TELLS THE CALLER ITS BUFFER WAS SHORT, AND `bd 0on` MEASURED THAT
 * THIS COSTS NOTHING — a sentence that used to read as an open TODO and kept
 * attracting the same proposal. Instrumented on 2026-09-17 (a scratch copy;
 * this file was never left modified) with a per-capture (cap, stored, dropped)
 * line, across all four consuming suites: 63 live captures, of which EXACTLY
 * ONE truncates — the ta1 flood fixture, which truncates ON PURPOSE and
 * already asserts it by `CHECK_EQ(strlen(err), sizeof err - 1u)` plus the
 * first and last stored byte, which is STRICTLY STRONGER than the boolean the
 * proposal would add. The other 62 have at least 8x headroom; the tightest
 * real margin is 190 bytes stored into a 512 buffer, and the largest message
 * anywhere is 357 into 4,096.
 *
 * AND TRUNCATION IS NOT SILENT AT MOST OF THE SITES ANYWAY. Of the five real
 * consumer call sites, three go RED on a short capture without any new
 * machinery: test_runtime_v2_message.inc's parse_said requires the message to
 * END with ")\n" (`e[2] == '\0'`), test_shim_ctx_region.inc's message cases use
 * CHECK_STR_EQ against the whole string, and test_runtime_gate_rotate.inc
 * counts newlines in `out`. Only the two substring-shaped checks could pass on
 * a prefix, and both sit ~8-11x under their buffers. So the hazard has zero
 * live instances and cannot acquire one quietly at the majority of sites.
 *
 * WHAT WAS REFUSED, so it is not re-proposed a third time: making truncation a
 * cq_h_fail (it converts a documented, harmless prefix into a NEW hard failure
 * across every call site, and it collides head-on with the flood fixture — the
 * ta1 detector — which must overflow); and widening the signature with an
 * out-parameter (17 call sites today, not the four the bead budgeted). If a
 * future consumer ever does need this, the shape to add is a SEPARATE QUERY,
 * which costs zero call-site edits at any count. */
static int cap_step(cq_cap *s)
{
    char    drop[512];
    char   *dst  = s->buf + s->n;
    size_t  room = s->cap - 1u - s->n;
    ssize_t got;

    if (room == 0u) { dst = drop; room = sizeof drop; }
    got = read(s->fd, dst, room);
    if (got == 0) return 0;                        /* EOF: the child is gone  */
    if (got < 0) {
        if (errno == EINTR) return 1;              /* a signal, not an end    */
        cq_h_fail(__FILE__, __LINE__, "read failed: errno %d", errno);
        return 0;
    }
    if (dst != drop) {
        s->n += (size_t)got;
        s->buf[s->n] = '\0';
    }
    return 1;
}

/* `bd ta1` — THE SELECTED STREAMS ARE DRAINED TOGETHER, and the sequential
 * shape this replaces could DEADLOCK. With both streams selected the parent
 * read stdout to EOF and only then stderr; EOF on stdout arrives when the child
 * exits, so a child that filled the stderr pipe before finishing stdout blocked
 * in write() while the parent blocked in read(). It presents as a ctest
 * TIMEOUT, not a red assertion, which is why nothing would have named it.
 *
 * IT COULD NOT FIRE ON THE TREE AS IT STOOD, measured 2026-09-17: the one
 * both-streams caller is the D11 characterisation and it emits tens of bytes
 * against a pipe that holds at least 16 KiB. This is HARDENING, so it needs
 * its own detector, and that is
 * `a_child_that_fills_one_pipe_before_finishing_the_other_does_not_deadlock`
 * in tests/test_childcap_controls.inc — whose child arms an alarm so a
 * REGRESSION is a bounded red rather than an unbounded hang.
 *
 * poll() rather than a second fork: one process, nothing extra to reap. A
 * POLLHUP with no POLLIN still gets its read(), which returns 0 and ends the
 * stream.
 *
 * THIS ADDED A THIRD EINTR SITE RATHER THAN REUSING ONE OF REQUIREMENT 5's TWO,
 * and the header said otherwise until `bd 698` counted them. It is also the
 * ONLY one of the three a signal can realistically land on — measured, this is
 * where 21,489 of 21,493 interruptions arrived — so its retry is pinned by
 * `a_signal_arriving_while_the_parent_blocks_in_poll_does_not_end_the_drain`
 * in tests/test_childcap_controls.inc. The `break` spelling of this mutant is
 * SILENT, which is why that case asserts the captured BYTES and not only the
 * verdict. */
static void drain_both(cq_cap *a, cq_cap *b)
{
    for (;;) {
        struct pollfd  pfd[2];
        cq_cap        *who[2];
        nfds_t         nf = 0u;
        nfds_t         i;

        if (a->fd >= 0) { pfd[nf].fd = a->fd; pfd[nf].events  = POLLIN;
                          pfd[nf].revents = 0; who[nf] = a; nf++; }
        if (b->fd >= 0) { pfd[nf].fd = b->fd; pfd[nf].events  = POLLIN;
                          pfd[nf].revents = 0; who[nf] = b; nf++; }
        if (nf == 0u) break;

        if (poll(pfd, nf, -1) < 0) {
            if (errno == EINTR) continue;          /* a signal, not an end    */
            cq_h_fail(__FILE__, __LINE__, "poll failed: errno %d", errno);
            break;
        }
        for (i = 0u; i < nf; i++) {
            if (pfd[i].revents == 0) continue;
            if (!cap_step(who[i])) who[i]->fd = -1;
        }
    }
}

static void close_pair(int fd[2])
{
    (void)close(fd[0]);
    (void)close(fd[1]);
}

/* The pairing check REQUIREMENT 1 needs to be safe. `out`/`out_cap` and
 * `err`/`err_cap` are adjacent arguments of the same types, so a transposed
 * call is the mistake available here; without this it would capture the wrong
 * stream into the wrong buffer, or nothing at all, and report the silence as
 * "the refusal printed nothing". A capacity below 1 has no room for the NUL
 * that every path below writes, and `cap - 1u - n` would wrap. */
static int streams_are_paired(unsigned streams,
                              const char *out, size_t out_cap,
                              const char *err, size_t err_cap)
{
    const unsigned known = CQ_CHILD_STDOUT | CQ_CHILD_STDERR;

    if (streams == 0u || (streams & ~known) != 0u) {
        cq_h_fail(__FILE__, __LINE__,
                  "cq_child_capture: bad stream mask %u", streams);
        return 0;
    }
    if (((streams & CQ_CHILD_STDOUT) != 0u) != (out != NULL && out_cap > 1u)) {
        cq_h_fail(__FILE__, __LINE__,
                  "cq_child_capture: stdout buffer and mask disagree");
        return 0;
    }
    if (((streams & CQ_CHILD_STDERR) != 0u) != (err != NULL && err_cap > 1u)) {
        cq_h_fail(__FILE__, __LINE__,
                  "cq_child_capture: stderr buffer and mask disagree");
        return 0;
    }
    return 1;
}

int cq_child_capture(unsigned streams,
                     void (*fn)(void *), void *arg,
                     char *out, size_t out_cap,
                     char *err, size_t err_cap)
{
    const int want_out = (streams & CQ_CHILD_STDOUT) != 0u;
    const int want_err = (streams & CQ_CHILD_STDERR) != 0u;
    int fo[2], fe[2];

    /* THE RIDER. Cleared before anything can fail, so a capture that never
     * happened reports an empty string rather than the bytes of the previous
     * call — which matters because the v1-boundary sweep calls this in a loop,
     * once per deferred symbol, into one reused buffer. */
    if (out != NULL && out_cap > 0u) out[0] = '\0';
    if (err != NULL && err_cap > 0u) err[0] = '\0';

    if (!streams_are_paired(streams, out, out_cap, err, err_cap)) return 0;

    if (want_out && pipe(fo) != 0) {
        cq_h_fail(__FILE__, __LINE__, "pipe failed");
        return 0;
    }
    if (want_err && pipe(fe) != 0) {
        if (want_out) close_pair(fo);
        cq_h_fail(__FILE__, __LINE__, "pipe failed");
        return 0;
    }

    /* REQUIREMENT 4, AND IT IS NOT HYGIENE. The harness writes its TAP
     * diagnostics to stdout with printf and flushes at the end of a case
     * (tests/support/harness.c); under ctest stdout is a pipe and therefore
     * fully buffered, so a child forked mid-case inherits every line the case
     * has already printed and RE-EMITS them when it aborts. Measured before
     * the flush was added: three `# FAIL` lines for two failed checks. */
    fflush(NULL);

    const pid_t pid = fork();
    if (pid < 0) {
        if (want_out) close_pair(fo);
        if (want_err) close_pair(fe);
        cq_h_fail(__FILE__, __LINE__, "fork failed");
        return 0;
    }

    if (pid == 0) {
        if (want_out) {
            (void)close(fo[0]);
            (void)dup2(fo[1], STDOUT_FILENO);
            (void)close(fo[1]);
        }
        if (want_err) {
            (void)close(fe[0]);
            (void)dup2(fe[1], STDERR_FILENO);
            (void)close(fe[1]);
        }
        fn(arg);
        _exit(0);                     /* fn returned: not an abort, and the
                                       * child must not resume the suite */
    }

    /* CONCURRENT, not stdout-then-stderr (`bd ta1`). The two pipes are
     * independent and either can fill; the write ends are closed FIRST so EOF
     * means the child and nothing else. */
    cq_cap co, ce;
    if (want_out) (void)close(fo[1]);
    if (want_err) (void)close(fe[1]);
    cap_open(&co, want_out ? fo[0] : -1, out, want_out ? out_cap : 0u);
    cap_open(&ce, want_err ? fe[0] : -1, err, want_err ? err_cap : 0u);
    drain_both(&co, &ce);
    if (want_out) (void)close(fo[0]);
    if (want_err) (void)close(fe[0]);

    /* REQUIREMENT 5's THIRD SITE, then REQUIREMENT 3. A child that RETURNED
     * from `fn` reached `_exit(0)` above and did not abort; a child that exited
     * non-zero did not abort either. The v1-boundary suite's entire claim is
     * that every deferred body ABORTS, so "the status is not zero" would
     * accept a body rewritten to `return 1;`.
     *
     * THE RETRY IS PINNED BY A CHILD THAT CLOSES ITS PIPE BEFORE IT DIES
     * (`bd 698`). In every other child here the drain ends BECAUSE the child
     * exited, so the status is already available and this call never blocks —
     * 4 EINTRs per 100 captures, a real race and a useless control. Separating
     * EOF from exit took that to 1,997 per 2,000 deliveries. The child must
     * also ABORT: without the retry waitpid returns -1 and `status` keeps its
     * initial 0, so the verdict is 0, which AGREES with the truth for every
     * child that does not abort. */
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}
