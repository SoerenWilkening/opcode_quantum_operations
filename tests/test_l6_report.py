#!/usr/bin/env python3
# test_l6_report.py — tools/l6/l6_report.py's VERDICT, on synthetic inputs
# (bd p2y5, 2026-10-04). Nothing here runs CQ_lang, a compiler or a fixture:
# `classify` is a pure function of (ran, rc, stderr, provenance), so every row
# of it can be pinned without the corpus. L6 itself stays opt-in.
#
# THE MAINTAINER'S RULING IS THE SPEC: "l6_run.py gives a nonzero rc with no
# signal and no abort/FATAL marker its OWN verdict class, counted and reported
# apart from OK, with abort detection untouched." Three things follow, and
# each has a case below:
#
#   1. EXIT_STATUS exists, and a D15 §3 strand line does not disqualify it.
#   2. Everything that is NOT the program's own status stays RUN_FAIL — a
#      libcqops fatal, a sanitizer report (which exits 1 with no signal when
#      abort_on_error is off), the shell's and loader's reserved 126/127, and
#      an unreadable .rc.
#   3. ABORT / DEFERRED / BUILD_FAIL / PROVENANCE / OK are unchanged.
#
# AND THE RULING'S PREMISE IS ASSERTED, NOT ASSUMED: EXIT_STATUS is only safe
# while no libcqops fail path can exit with a status of its own. The last two
# cases read src/, include/ and shim/ (generated bodies included) and go red
# the day one does, or the day a stderr prefix appears that the classifier has
# not been taught.
#
# NEXT SEAM: none recorded — well under Rule 12's limit.

import os
import re
import sys

sys.dont_write_bytecode = True   # tools/l6/'s own reason: no stale .pyc verdicts
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(HERE, "support"))
sys.path.insert(0, os.path.join(ROOT, "tools", "l6"))
import shimcheck as sc  # noqa: E402
import l6_report as lr  # noqa: E402

# The library's OWN texts, spelled as it prints them (src/reg.c, shim/cq_shim_ctx.c).
STRAND_LINE = ("libcqops: STRANDED: qubit q3 of handle h7 is not provably |0> at "
               "its free — never released, never on the free list, counted (PRD "
               "§15 D15 §3; set CQOPS_FREE_ABORT=1 to stop here instead). "
               "Further strands are not reported.\n")
FATAL_LINE = "libcqops: FATAL: reg: measure of a rail that is not live (1, 3)\n"
DEFER_LINE = "cqops: cq_template_fadd_f32 not implemented (fp is v2)\n"

# One sample per SANITIZER alternative, each carrying ONLY its own marker, so
# deleting any one alternative turns exactly its sample green.
SANITIZED = {
    "asan": "==41==ERROR: AddressSanitizer: heap-use-after-free on address 0x6\n",
    "lsan": "==41==ERROR: LeakSanitizer: detected memory leaks\n",
    "tsan": "WARNING: ThreadSanitizer: data race (pid=41)\n",
    "msan": "==41==WARNING: MemorySanitizer: use-of-uninitialized-value\n",
    "ubsan_summary": "SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior\n",
    "ubsan_line": "src/reg.c:12:5: runtime error: signed integer overflow\n",
}


def run(rc, err="", ran=True, prov=False):
    return lr.classify(ran, rc, err, prov)


def a_nonzero_status_with_no_marker_is_exit_status():
    # e2e-slice-condtree-classical-leaf, measured 2026-09-28: rc 7, stderr empty.
    for rc in (7, 1, 2, 125):
        sc.check(run(rc) == "EXIT_STATUS", "rc=%d, empty stderr: %s" % (rc, run(rc)))
    sc.check(run(7, "the fixture's own chatter\n") == "EXIT_STATUS",
             "rc=7 with unmarked stderr is not EXIT_STATUS")


def a_strand_report_alone_does_not_disqualify():
    # D15 §3: a strand is reported and the program CONTINUES, by design.
    sc.check(lr.STRAND.search(STRAND_LINE), "the sample is not a strand report")
    sc.check(run(7, STRAND_LINE) == "EXIT_STATUS",
             "rc=7 + strand only: %s" % run(7, STRAND_LINE))


def a_libcqops_fatal_stays_run_fail():
    sc.check(run(1, FATAL_LINE) == "RUN_FAIL", "rc=1 + FATAL: %s" % run(1, FATAL_LINE))
    mixed = STRAND_LINE + "fixture output\n" + FATAL_LINE
    sc.check(run(7, mixed) == "RUN_FAIL", "a FATAL after a strand: %s" % run(7, mixed))


def a_sanitizer_report_stays_run_fail():
    # The trap the class could fall into: a sanitizer's own _exit(1) is a
    # nonzero status with NO signal, exactly the shape EXIT_STATUS admits.
    for what, text in sorted(SANITIZED.items()):
        got = run(1, text)
        sc.check(got == "RUN_FAIL", "rc=1 + %s report: %s" % (what, got))
    sc.check(run(23, SANITIZED["lsan"]) == "RUN_FAIL",
             "standalone LSan's exitcode 23 + its report is not RUN_FAIL")


def the_shell_reserved_and_unreadable_statuses_stay_run_fail():
    # 126/127: the runner could not START the binary — not program data.
    for rc in (126, 127):
        sc.check(run(rc) == "RUN_FAIL", "rc=%d: %s" % (rc, run(rc)))
    # an empty .rc reads as -1 in l6_run.py; it is no status at all.
    sc.check(run(-1) == "RUN_FAIL", "rc=-1: %s" % run(-1))


def abort_deferred_build_provenance_and_ok_are_unchanged():
    sc.check(run(134) == "ABORT", "rc=134: %s" % run(134))
    sc.check(run(134, FATAL_LINE) == "ABORT", "rc=134 + FATAL")
    sc.check(run(128) == "ABORT" and run(255) == "ABORT", "rc 128/255 not ABORT")
    sc.check(run(134, DEFER_LINE) == "DEFERRED", "rc=134 + deferral")
    sc.check(run(7, DEFER_LINE) == "DEFERRED", "rc=7 + deferral")
    sc.check(run(-1, ran=False) == "BUILD_FAIL", "never ran")
    sc.check(run(0, prov=True) == "PROVENANCE", "provenance with rc 0")
    sc.check(run(-1, ran=False, prov=True) == "PROVENANCE", "provenance, never ran")
    sc.check(run(0) == "OK" and run(0, STRAND_LINE) == "OK", "rc=0 not OK")


def the_gate_is_green_on_exactly_three_classes():
    sc.check(set(lr.GATE_GREEN) == {"OK", "DEFERRED", "EXIT_STATUS"},
             "GATE_GREEN is %r" % (lr.GATE_GREEN,))
    for red in ("BUILD_FAIL", "ABORT", "RUN_FAIL", "PROVENANCE"):
        sc.check(red not in lr.GATE_GREEN, "%s is green" % red)


def every_exit_status_row_is_listed_with_its_rc():
    rows = [dict(name="f-ok", verdict="OK", rc=0),
            dict(name="f-seven", verdict="EXIT_STATUS", rc=7),
            dict(name="f-def", verdict="DEFERRED", rc=134),
            dict(name="f-three", verdict="EXIT_STATUS", rc=3),
            dict(name="f-bad", verdict="RUN_FAIL", rc=1)]
    lines = lr.exit_status_lines(rows)
    sc.check(len(lines) == 2, "expected 2 EXIT_STATUS lines, got %r" % lines)
    for (name, rc), line in zip((("f-seven", 7), ("f-three", 3)), lines):
        sc.check(name in line and ("rc=%d " % rc) in line, "line %r" % line)


def c_text(roots, exts=(".c", ".h")):
    # Comment-stripped C under the library's three roots. Block comments and
    # WHOLE-LINE // comments only, so a trailing `// exit(` stays visible —
    # over-reporting is the safe direction for this scan.
    for root in roots:
        for d, _sub, files in os.walk(os.path.join(ROOT, root)):
            for f in sorted(files):
                if f.endswith(exts):
                    p = os.path.join(d, f)
                    text = re.sub(r"/\*.*?\*/", " ", open(p, errors="replace").read(),
                                  flags=re.S)
                    yield os.path.relpath(p, ROOT), re.sub(r"(?m)^\s*//.*$", "", text)


EXITS = re.compile(r"(?<![A-Za-z0-9_.])(exit|_exit|_Exit|quick_exit|at_quick_exit|"
                   r"on_exit|pthread_exit|signal|sigaction|raise|kill|longjmp|"
                   r"siglongjmp)\s*\(")


def the_library_has_no_status_exit_and_no_signal_handling():
    # THE RULING'S PREMISE: every libcqops fail path is abort() (rc >= 128).
    seen, hits = 0, []
    for path, text in c_text(("src", "include", "shim")):
        seen += 1
        hits += ["%s: %s(" % (path, m.group(1)) for m in EXITS.finditer(text)]
    sc.check(seen > 150, "scanned only %d files — the roots moved?" % seen)
    sc.check(not hits, "a libcqops path can exit with a status: %s" % hits[:6])


STDERR = re.compile(r'fprintf\(\s*stderr\s*,\s*"([^"]*)"')
BENIGN = ("libcqops: STRANDED:", "libcqops: qec trace:")


def every_library_stderr_prefix_is_one_the_classifier_knows():
    fatal, other = 0, []
    for path, text in c_text(("src", "include", "shim")):
        for m in STDERR.finditer(text):
            s = m.group(1)
            if s.startswith("libcqops: FATAL:"):
                fatal += 1
                sc.check(lr.FATAL.search(s), "%s: FATAL not matched: %r" % (path, s))
            elif s.startswith(BENIGN) or lr.DEFERRED.match(s.replace("%s", "x")):
                sc.check(not lr.FATAL.search(s), "%s: benign line matches FATAL" % path)
            else:
                other.append("%s: %r" % (path, s[:40]))
    sc.check(fatal >= 20, "only %d FATAL prints found — the scan is blind" % fatal)
    sc.check(not other, "stderr prints the classifier has not ruled on: %s" % other)


CASES = [
    a_nonzero_status_with_no_marker_is_exit_status,
    a_strand_report_alone_does_not_disqualify,
    a_libcqops_fatal_stays_run_fail,
    a_sanitizer_report_stays_run_fail,
    the_shell_reserved_and_unreadable_statuses_stay_run_fail,
    abort_deferred_build_provenance_and_ok_are_unchanged,
    the_gate_is_green_on_exactly_three_classes,
    every_exit_status_row_is_listed_with_its_rc,
    the_library_has_no_status_exit_and_no_signal_handling,
    every_library_stderr_prefix_is_one_the_classifier_knows,
]


if __name__ == "__main__":
    sys.exit(sc.run(CASES))
