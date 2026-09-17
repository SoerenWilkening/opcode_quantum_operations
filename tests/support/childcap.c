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
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

/* REQUIREMENT 5, half one. EOF is the only end: EINTR is a signal arriving
 * mid-read, not the child finishing, and treating it as one truncates the
 * capture to whatever had arrived. */
static void drain(int fd, char *buf, size_t cap)
{
    size_t n = 0;

    buf[0] = '\0';
    for (;;) {
        const ssize_t got = read(fd, buf + n, cap - 1u - n);
        if (got == 0) break;                       /* EOF: the child is gone  */
        if (got < 0) {
            if (errno == EINTR) continue;          /* a signal, not an end    */
            cq_h_fail(__FILE__, __LINE__, "read failed: errno %d", errno);
            break;
        }
        n += (size_t)got;
        if (n + 1u >= cap) break;
    }
    buf[n] = '\0';
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

    /* Sequential, stdout first — inherited from the two-pipe copy, whose
     * children write a bounded trace to stdout and one short line to stderr.
     * It is not safe for a child that can fill a pipe on the stream drained
     * second while the parent blocks on the first (`bd ta1`). */
    if (want_out) (void)close(fo[1]);
    if (want_err) (void)close(fe[1]);
    if (want_out) drain(fo[0], out, out_cap);
    if (want_err) drain(fe[0], err, err_cap);
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
