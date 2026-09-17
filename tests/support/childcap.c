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

/* REQUIREMENT 5, half one. EOF is the only end: EINTR is a signal arriving
 * mid-read, not the child finishing, and treating it as one truncates the
 * capture to whatever had arrived.
 *
 * AND A FULL BUFFER IS NOT AN END EITHER (`bd ta1`). This used to `break` at
 * the cap, which leaves the child blocked in write() on a pipe nobody is
 * emptying — so a SMALL capture buffer made the hazard WORSE, not safer, which
 * is the counter-intuitive half of that bead. Past the cap the bytes are read
 * and DROPPED: the stored prefix is byte-for-byte what the old shape stored,
 * and the child always gets to finish. Nothing tells the caller its buffer was
 * short; see childcap.h's note on what is still not pinned. */
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
 * poll() rather than a second fork: one process, nothing extra to reap, and the
 * EINTR retry stays in the two places requirement 5 already names. A POLLHUP
 * with no POLLIN still gets its read(), which returns 0 and ends the stream. */
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

    /* REQUIREMENT 5, half two, then REQUIREMENT 3. A child that RETURNED from
     * `fn` reached `_exit(0)` above and did not abort; a child that exited
     * non-zero did not abort either. The v1-boundary suite's entire claim is
     * that every deferred body ABORTS, so "the status is not zero" would
     * accept a body rewritten to `return 1;`. */
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}
