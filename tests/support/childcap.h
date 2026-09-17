/* tests/support/childcap.h — fork a child, capture what it wrote, and report
 * whether it died on SIGABRT (`bd ula`).
 *
 * THREE SUITES HAD WRITTEN THIS, AND THE COPIES HAD ALREADY DIVERGED. It is
 * needed wherever the subject is a _Noreturn refusal whose MESSAGE is the
 * contract: a hard error cannot be read in-process, and a ctest
 * PASS_REGULAR_EXPRESSION cannot do it either — that property DISPLACES the
 * exit-code check, trading a death test's own contract for a substring
 * (tests/CMakeLists.txt records this), and it cannot tell stdout from stderr,
 * which is half of what these suites pin.
 *
 * WHY THIS IS NOT tests/support/death.[ch]. A death binary's one native claim
 * is "it aborted": it catches SIGABRT in-process, `argv[1]` selects the case,
 * and every exit path is `_Exit()`. That is the right shape when one process
 * is one case. These callers are ORDINARY suites — they need the abort AND its
 * bytes AND to keep running afterwards, once per deferred symbol in a loop in
 * one of them — so the child is spawned rather than being the test process.
 *
 * WHAT THE EXTRACTED SHAPE HAS TO CARRY. Five requirements, each recorded
 * because it is invisible in a green run and losing it is a silent regression;
 * the WHY of each is in childcap.c, at the line that implements it.
 *
 *   1. WHICH STREAMS TO REDIRECT IS A PARAMETER, NOT A CONSTANT — the D11
 *      partial-emission characterisation needs BOTH, on separate pipes.
 *   2. The callback takes a `void *`; a nullary caller passes NULL.
 *   3. SIGABRT SPECIFICALLY, never merely a non-zero exit.
 *   4. fflush(NULL) BEFORE the fork.
 *   5. EINTR retry on BOTH read() and waitpid().
 *
 * Plus the rider: the capture buffers are cleared FIRST, so a failed capture
 * can never report a previous call's bytes.
 *
 * WHICH OF THESE A CASE ACTUALLY PINS — `bd 09z`, RE-MEASURED 2026-09-17 in
 * BOTH configurations, and the first version of this paragraph got it wrong in
 * both directions. It said "none of the six is pinned", which was the
 * `src/angle.h` shape: a measured-sounding claim resting on a battery that
 * never mutated the requirement it was generalising over.
 *
 *   REQUIREMENT 1 IS PINNED, and it is the strongest thing here. Ignoring the
 *   stream mask, and swapping the two drains, are both KILLED in both
 *   configurations by `the_d11_rz_refusal_emits_four_gates_per_wire_below_the_
 *   first_constant` (tests/test_runtime_gate_rotate.inc) — its
 *   CHECK_EQ(gates, 4*j) needs stdout and its CHECK(strstr(err, "D11")) needs
 *   stderr, so that one case cannot pass unless both pipes go where they say.
 *
 *   REQUIREMENTS 3, 4, 5 AND THE RIDER ARE NOT PINNED. Seven mutants survive
 *   both configurations: requirement 3 three ways (`status != 0`, a hard-wired
 *   `return 1`, and WIFSIGNALED without the SIGABRT test), requirement 5's
 *   read-side EINTR retry turned into a break, requirement 4's fflush deleted,
 *   the rider's buffer clear deleted, and streams_are_paired() forced true.
 *
 * BUT REQUIREMENT 3's LINE IS LOAD-BEARING RATHER THAN UNTESTED, and only a
 * PAIRED mutation shows the difference. Change the corpus of children so one
 * of them exits non-zero WITHOUT a signal — cq_shim_unsupported's abort()
 * replaced by exit(1), message byte-identical — and five CHECK_EQ(signalled, 1)
 * in tests/test_shim_ctx_region.inc go red in Release while every message check
 * still passes. Pair that with `return 1` here and the whole thing goes green
 * again. So this line is the SOLE Release detector of "the refusal printed
 * correctly and did not abort", and the three survivors above are EQUIVALENT
 * MUTANTS on the children that exist today, not dead code.
 *
 * THE CONTROL THAT CLOSES IT IS NOT THE OBVIOUS ONE. This paragraph used to
 * prescribe "a callback that prints and RETURNS"; measured against a probe
 * linking this archive, a returning child reaches _exit(0), so the baseline and
 * `status != 0` BOTH report 0 and that control kills only the hard-wired
 * `return 1`. What discriminates: a child that prints and _exit(1)s (kills
 * `status != 0` AND `return 1`), plus a child that prints and raises a
 * non-SIGABRT signal (kills WIFSIGNALED-without-SIGABRT). Two controls, and
 * 09z carries the measured table.
 */
#ifndef CQOPS_TEST_CHILDCAP_H
#define CQOPS_TEST_CHILDCAP_H

#include <stddef.h>

/* REQUIREMENT 1. A mask, so a caller states which streams it is capturing and
 * an omission is a visible argument rather than a default nobody chose. The
 * two are independent: one caller takes stderr alone (the refusal), one takes
 * both (what reached the SINK on stdout, and WHICH refusal on stderr). */
enum {
    CQ_CHILD_STDOUT = 1u,
    CQ_CHILD_STDERR = 2u
};

/* Runs `fn(arg)` in a forked child with the selected streams on pipes, and
 * returns 1 iff the child died on SIGABRT — never merely "exited non-zero"
 * (REQUIREMENT 3).
 *
 * A stream named in `streams` must be given a buffer, and a buffer must have
 * its stream named; the pairing is checked rather than assumed, because the
 * two buffer pairs are adjacent arguments of the same type and a transposed
 * call would otherwise capture nothing and report it as an empty message. A
 * stream NOT named is left alone and inherited, which is what a caller that
 * wants the child's stdout to reach ctest relies on.
 *
 * Buffers are NUL-terminated and cleared before anything can fail, so a
 * harness fault (pipe, fork, read) yields an empty capture and 0, never the
 * previous call's bytes. Such a fault is reported through cq_h_fail, so it
 * reddens the calling case rather than passing quietly as "did not abort".
 *
 * `fn` returning is not an abort: the child then _exit(0)s rather than falling
 * back into the parent's case list, because a forked test binary that resumed
 * would run every remaining case a second time. */
int cq_child_capture(unsigned streams,
                     void (*fn)(void *), void *arg,
                     char *out, size_t out_cap,
                     char *err, size_t err_cap);

#endif /* CQOPS_TEST_CHILDCAP_H */
