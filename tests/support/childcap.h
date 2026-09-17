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
 *   5. EINTR retry on EVERY blocking call — and there are THREE, not two:
 *      read() (cap_step), poll() (drain_both) and waitpid(). This line said
 *      "BOTH read() and waitpid()" until 2026-09-17; it predates `bd ta1`,
 *      which ADDED the poll site, and the paragraph further down had meanwhile
 *      been rewritten to say "poll() and read()". So the file named a
 *      DIFFERENT PAIR in two places and neither pair was the set. Nothing was
 *      wrong in the code — the miscount is the point, and it is why the three
 *      sites now get three separate verdicts rather than one sentence.
 *
 * Plus the rider: the capture buffers are cleared FIRST, so a failed capture
 * can never report a previous call's bytes.
 *
 * WHICH OF THESE A CASE ACTUALLY PINS — `bd 09z`, and THIS PARAGRAPH HAS NOW
 * BEEN WRONG THREE TIMES, which is itself the finding. (1) It said "none of
 * the six is pinned", resting on a battery that never mutated the requirement
 * it was generalising over. (2) It said SEVEN mutants survive, true of the
 * corpus of children that existed on 2026-09-17 and false by the end of that
 * day. (3) It said two of the survivors were UNREACHABLE, which was refuted by
 * someone spending twenty minutes writing the controls it claimed were
 * impossible. Every one of the three was a measured-SOUNDING claim that no test
 * read — `src/angle.h`'s defect, in the one file whose entire job is recording
 * what is and is not pinned. The lesson is not "be careful": it is that a
 * survivor should be re-measured before it is explained, and that an
 * IMPOSSIBILITY claim is the most expensive kind to get wrong, because it stops
 * the next person trying.
 *
 *   REQUIREMENT 1 IS PINNED, and it is the strongest thing here. Ignoring the
 *   stream mask, and swapping the two drains, are both KILLED in both
 *   configurations by `the_d11_rz_refusal_emits_four_gates_per_wire_below_the_
 *   first_constant` (tests/test_runtime_gate_rotate.inc) — its
 *   CHECK_EQ(gates, 4*j) needs stdout and its CHECK(strstr(err, "D11")) needs
 *   stderr, so that one case cannot pass unless both pipes go where they say.
 *
 *   REQUIREMENTS 3 AND 4 ARE PINNED SINCE 2026-09-17 by the CONTROLS in
 *   tests/test_childcap_controls.inc, measured in BOTH configurations. What
 *   closed them was not an assertion but a WIDER CORPUS OF CHILDREN: every
 *   child forked before then either aborted or returned, so `status != 0`,
 *   `WIFEXITED(status) && WEXITSTATUS(status) != 0`, `WIFSIGNALED(status)` and
 *   a hard-wired `return 1` all agreed with the real predicate on every input
 *   it was ever given. A child that prints and _Exit(1)s, and one that prints
 *   and raises SIGTERM, are what make them disagree; a parent that leaves bytes
 *   pending in its own stdout is what makes requirement 4 observable.
 *
 *   THE RIDER AND streams_are_paired() ARE PINNED TOO, since the fix round of
 *   the same day, and the claim they REPLACE is worth keeping visible because
 *   it is this file's third wrong tally. It said the two were "unreachable by a
 *   passing case, not merely unwritten", on the reasoning that their only
 *   effect on a SUCCEEDING call is a cq_h_fail and that a test cannot assert
 *   its own redness. THAT WAS FALSE, and falsified by running code: harness.h
 *   ships cq_h_mute + cq_h_take_failures for exactly this, under a section
 *   headed FALSIFIABILITY that quotes the principle the false claim was
 *   invoking, and tests/test_kerneldrv.c's CQ_EXPECT_CAUGHT / CQ_EXPECT_CLEAN
 *   is the established idiom. tests/test_harness_negative.c is NOT the only
 *   inversion point in this tree; it is only the coarsest.
 *
 *   Measured in both configurations: deleting the rider's clear, removing
 *   streams_are_paired's body, and IGNORING its result are all three RED. The
 *   last of those is the sharp one — a guard whose complaint still prints
 *   passes any control that merely counts the diagnostic, so the control
 *   asserts the buffer is EMPTY afterwards, which a call that went ahead and
 *   captured cannot satisfy.
 *
 *   ONLY THE READ-SIDE EINTR RETRY IS LEFT, and the paragraph this replaces was
 *   this file's FOURTH wrong tally — it said "the two EINTR retries" while
 *   requirement 5 above named a different two. `bd 698` closed the poll side
 *   and the waitpid side on 2026-09-17, in both configurations, in
 *   tests/test_childcap_controls.inc. Note that ta1 ADDED the poll-side one: a
 *   restructure that fixes a hazard can widen the unpinned surface, and saying
 *   so is cheaper than rediscovering it.
 *
 *   THE OLD REASON FOR DECLINING WAS "a timing race, and a flaky control is
 *   worse than a recorded gap". The premise was right and the conclusion did
 *   not follow: an ITIMER IN THE PARENT plus a child made to take ~100 ms puts
 *   ~20 deliveries INSIDE the blocking call per capture, so the event is the
 *   common case rather than a rare one. Measured over 400 runs of the host
 *   binary in Release and 100 in Debug, plus the whole -R gate at `-j 6`
 *   (8 rounds Release, 5 Debug): ZERO failures, while both poll-side mutants
 *   and the waitpid-side mutant die on 100% of runs. `sa_flags = 0` is the
 *   whole mechanism — under SA_RESTART the kernel restarts the call itself and
 *   EINTR never reaches this file.
 *
 *   AND THE READ SIDE IS DECLINED ON A MEASUREMENT, NOT ON FLAKINESS. With a
 *   counter in each of the three EINTR branches (a scratch copy; this file was
 *   never written), driven by a child dribbling 40 separate one-byte writes so
 *   the parent makes 4,000 read() calls, under a 1 ms repeating SIGALRM: the
 *   branch counts were poll 21,489 / wait 4 / read 0 in Release and poll
 *   32,159 / wait 1 / read 0 in Debug. cap_step runs only after poll has
 *   reported readiness or hangup, so the read never sleeps and cannot be
 *   interrupted before it transfers. A control for it would not be flaky, it
 *   would be VACUOUS — which is worse, because it would read as coverage.
 *
 *   WHAT ta1 PINNED is the drain shape itself, in both configurations: making
 *   the two drains sequential again, and making a full buffer end a drain
 *   again, are each KILLED by
 *   `a_child_that_fills_one_pipe_before_finishing_the_other_does_not_deadlock`.
 *
 * REQUIREMENT 3's LINE WAS LOAD-BEARING RATHER THAN UNTESTED ALL ALONG, and
 * only a PAIRED mutation showed the difference. Change the corpus of children
 * so one of them exits non-zero WITHOUT a signal — cq_shim_unsupported's
 * abort() replaced by exit(1), message byte-identical — and in RELEASE the five
 * CHECK_EQ(signalled, 1) in tests/test_shim_ctx_region.inc and the 39 `thunk N
 * did not abort` reports in tests/test_runtime_v2_message.inc all go red while
 * every message check still passes. Pair that with `return 1` here and those
 * two suites go green again; what stays red is tests/test_childcap_controls.inc,
 * which is the whole point of it.
 *
 * MEASURE THAT PAIRING IN RELEASE. Its Debug reading is an ARTEFACT and points
 * the wrong way: the mutated child reaches exit(1), which runs atexit handlers,
 * so LeakSanitizer writes its report into the captured child stderr and the
 * five CHECK_STR_EQ beside those CHECK_EQs fail too — on the appended leak
 * report, not on anything about the abort. A Debug-only reader concludes the
 * message check covers it. It does not. This is the fail-direction twin of
 * `bd remember sanitizer-abort-passes-expect-abort`.
 *
 * AND A BARE BINARY RUN CANNOT SEE THAT ARTEFACT EITHER, because
 * ASAN_OPTIONS=detect_leaks=1 lives in ctest's ENVIRONMENT property
 * (cmake/CqopsTest.cmake) and the shell does not win over it. Measured: the
 * same mutant run directly out of the Debug tree shows five clean
 * CHECK_EQ(signalled, 1) failures and no leak report at all.
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
 * A CAPTURE IS A PREFIX AND THE CALLER IS NOT TOLD SO — a deliberate,
 * MEASURED non-feature since `bd 0on` (2026-09-17), not an omission. Past the
 * cap the bytes are read and dropped, so the child never blocks and the drain
 * always reaches EOF; what is lost is only the notification. Measured across
 * all four consuming suites: 63 live captures, exactly ONE truncates, and that
 * one is the ta1 flood fixture, which does so on purpose and asserts the full
 * prefix length — a stronger claim than the flag anyone would add. The
 * arithmetic, the per-site truncation sensitivity and the two shapes that were
 * REFUSED are in childcap.c, at the comment on cap_step's cap handling.
 *
 * `fn` returning is not an abort: the child then _exit(0)s rather than falling
 * back into the parent's case list, because a forked test binary that resumed
 * would run every remaining case a second time. */
int cq_child_capture(unsigned streams,
                     void (*fn)(void *), void *arg,
                     char *out, size_t out_cap,
                     char *err, size_t err_cap);

#endif /* CQOPS_TEST_CHILDCAP_H */
