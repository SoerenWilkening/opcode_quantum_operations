#!/usr/bin/env python3
# test_cqlang_drivers.py — tools/l6/l6_run.py's and tools/l7/l7_run.py's
# CONTACT WITH THE CQ_LANG CHECKOUT (bd oqp8, 2026-10-05), on FAKES.
#
# NOTHING HERE TOUCHES CQ_LANG, AND THAT IS ENFORCED RATHER THAN HOPED: each
# case runs a driver as a subprocess whose PATH is a scratch directory holding
# ONLY a fake `git`, `ctest` and `bash`, with --cqlang an empty scratch
# directory. No real git, ctest, compiler or runner is reachable. L6's fake
# ctest registers no fixture, so its corpus loop never runs; L7's fake `bash`
# stands in for run_slice_cqops.sh and builds nothing, so L7 reports DID NOT
# BUILD — a path that still writes the JSON, which is what is under test.
#
#   1. --cqlang is REQUIRED by both drivers. L6 defaulted it to a machine path
#      that went stale when CQ_lang moved, so a bare hand run read nothing.
#   2. Every git call into CQ_lang carries --no-optional-locks: the checkout
#      is another session's tree (bd memory
#      read-another-sessions-checkout-without-touching-it).
#   3. A run that STRADDLES a CQ_lang commit keeps the START as `cq_revision`,
#      records the end as `cq_revision_end`, and flags the move in the JSON
#      and on stdout. The fake git answers SHA1 to the first `log` and SHA2 to
#      every later one, which is what a commit landing mid-run looks like.
#   4. A run that does not straddle is not flagged.
#
# ONE FAKE FILE PER PROCESS, NOT THREE PER drive() (bd ifoy (f), 2026-10-05).
# On this box the FIRST exec of a freshly written file waits ~0.4 s on Apple's
# XprotectService (its CPU, not ours; the Avast agent barely moves), measured
# at load 4 against ~11 ms for a re-exec, ~18 ms for a symlink or hard link in
# a fresh dir to an already-exec'd file, ~13 ms for `/bin/sh <fresh file>`.
# Writing git/ctest/bash afresh per drive() paid it 15 times a run: ~7 s at
# load 4, 36-1014 s at load 15-130; one file per process: 3 s at load 11,
# 5-13 s at load 34-305 (interleaved with the old ones). So the fake
# is ONE script, written once, reached through three symlinks whose name it
# dispatches on. Each drive() still gets its own $FAKE_DIR: every bit of state
# (git.log, log.n, ctest.json, bash.log) lives there, none in the script.
#
# NEXT SEAM: none recorded — well under Rule 12's limit.

import json
import os
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(HERE, "support"))
import shimcheck as sc  # noqa: E402

DRIVERS = {"l6": os.path.join(ROOT, "tools", "l6", "l6_run.py"),
           "l7": os.path.join(ROOT, "tools", "l7", "l7_run.py")}
SHA1, SHA2 = "1" * 40, "2" * 40
FULL = ["--cqlang", "{cq}", "--out", "{out}", "--json", "{json}"]

# git: every invocation is logged, one argv per line, BEFORE anything else
# runs. ctest: `exec`, so a missing ctest.json still exits non-zero. A name
# with no arm is a loud 127, never a silent pass.
FAKE = r'''#!/bin/sh
case "${0##*/}" in
  git)
    printf '%s\n' "$*" >> "$FAKE_DIR/git.log"
    case " $* " in
      *" log "*)
        n=$(/bin/cat "$FAKE_DIR/log.n" 2>/dev/null || echo 0)
        n=$((n + 1))
        echo "$n" > "$FAKE_DIR/log.n"
        if [ "$n" -eq 1 ]; then echo "$FAKE_SHA1 2026-10-04"
        else echo "$FAKE_SHA2 2026-10-04"; fi ;;
    esac ;;
  ctest) exec /bin/cat "$FAKE_DIR/ctest.json" ;;
  bash) printf '%s\n' "$*" >> "$FAKE_DIR/bash.log" ;;
  *) echo "fake: no arm for ${0##*/}" >&2; exit 127 ;;
esac
exit 0
'''
TOOLS = ("git", "ctest", "bash")
_BIN = []  # [TemporaryDirectory], built by the first drive(), kept to exit

# What each driver needs from `ctest --show-only=json-v1`: L6 nothing (no
# fixture, no runner); L7 one single-file slice, for its toolchain read.
CTEST = {"l6": {"tests": []},
         "l7": {"tests": [{"name": "e2e-fake", "command": [
             "/bin/bash", "/fake/run_slice.sh", "CLANG", "OPT", "PASS", "INC",
             "SRC"]}]}}


def lines(path):
    return open(path).read().splitlines() if os.path.exists(path) else []


def fake_bin():
    """The fake PATH: a dir holding ONLY git, ctest, bash -> one FAKE."""
    if not _BIN:
        _BIN.append(tempfile.TemporaryDirectory(prefix="cqops-fakebin-"))
        script = os.path.join(_BIN[0].name, "fake")
        with open(script, "w") as fh:
            fh.write(FAKE)
        os.chmod(script, 0o755)
        os.mkdir(os.path.join(_BIN[0].name, "bin"))
        for name in TOOLS:
            os.symlink(script, os.path.join(_BIN[0].name, "bin", name))
    return os.path.join(_BIN[0].name, "bin")


def drive(which, argv, sha2=SHA2):
    """Run one driver on fakes: (rc, stdout+stderr, git argv lines, JSON)."""
    with tempfile.TemporaryDirectory(prefix="cqops-drivers-") as tmp:
        fake = fake_bin()
        with open(os.path.join(tmp, "ctest.json"), "w") as fh:
            json.dump(CTEST[which], fh)
        cq = os.path.join(tmp, "cqlang")
        os.mkdir(cq)
        out, js = os.path.join(tmp, "out"), os.path.join(tmp, "run.json")
        args = [a.format(cq=cq, out=out, json=js) for a in argv]
        env = dict(PATH=fake, FAKE_DIR=tmp, FAKE_SHA1=SHA1, FAKE_SHA2=sha2,
                   PYTHONDONTWRITEBYTECODE="1", PYTHONIOENCODING="utf-8")
        r = subprocess.run([sys.executable, DRIVERS[which]] + args, cwd=tmp,
                           env=env, capture_output=True, encoding="utf-8",
                           errors="replace", timeout=120)
        data = json.load(open(js)) if os.path.exists(js) else None
        return (r.returncode, r.stdout + r.stderr,
                lines(os.path.join(tmp, "git.log")), data)


def both_drivers_refuse_a_missing_cqlang():
    for which in sorted(DRIVERS):
        rc, text, log, _ = drive(which, ["--out", "{out}"])
        sc.check(rc == 2 and "--cqlang" in text and "required" in text,
                 "%s without --cqlang: rc=%d, %r" % (which, rc, text[-300:]))
        sc.check(not log, "%s ran git before refusing: %r" % (which, log))


def every_git_call_into_cqlang_skips_optional_locks():
    for which in sorted(DRIVERS):
        rc, text, log, _ = drive(which, FULL)
        verbs = {w for l in log for w in l.split() if w in ("log", "status")}
        # Not vacuous: both of cq_revision's calls must have been SEEN.
        sc.check(verbs == {"log", "status"},
                 "%s: git calls seen %r (rc=%d) %s" % (which, log, rc, text[-300:]))
        bad = [l for l in log if l.split()[:1] != ["--no-optional-locks"]]
        sc.check(not bad, "%s: git without --no-optional-locks: %r" % (which, bad))


def a_run_that_straddles_a_commit_records_both_and_flags_it():
    for which in sorted(DRIVERS):
        rc, text, _, data = drive(which, FULL)
        sc.check(data is not None, "%s wrote no JSON (rc=%d) %s" % (which, rc, text[-300:]))
        sc.check(("CQ_lang @ " + SHA1) in text, "%s: banner is not the START" % which)
        sc.check(str(data.get("cq_revision")).startswith(SHA1),
                 "%s: cq_revision is not the START: %r" % (which, data.get("cq_revision")))
        sc.check(str(data.get("cq_revision_end")).startswith(SHA2),
                 "%s: cq_revision_end is %r" % (which, data.get("cq_revision_end")))
        sc.check(data.get("cq_revision_straddled") is True,
                 "%s: straddle flag is %r" % (which, data.get("cq_revision_straddled")))
        moved = [l for l in text.splitlines() if "MOVED DURING THIS RUN" in l]
        sc.check(len(moved) == 1 and SHA1 in moved[0] and SHA2 in moved[0],
                 "%s: straddle line %r" % (which, moved))


def a_run_that_does_not_straddle_is_not_flagged():
    for which in sorted(DRIVERS):
        rc, text, _, data = drive(which, FULL, sha2=SHA1)
        sc.check(data is not None, "%s wrote no JSON (rc=%d) %s" % (which, rc, text[-300:]))
        sc.check(str(data.get("cq_revision")).startswith(SHA1)
                 and data.get("cq_revision_end") == data.get("cq_revision"),
                 "%s: revisions %r / %r" % (which, data.get("cq_revision"),
                                             data.get("cq_revision_end")))
        sc.check(data.get("cq_revision_straddled") is False,
                 "%s: straddle flag is %r" % (which, data.get("cq_revision_straddled")))
        sc.check("MOVED DURING THIS RUN" not in text, "%s: flagged a still tree" % which)


CASES = [
    both_drivers_refuse_a_missing_cqlang,
    every_git_call_into_cqlang_skips_optional_locks,
    a_run_that_straddles_a_commit_records_both_and_flags_it,
    a_run_that_does_not_straddle_is_not_flagged,
]


if __name__ == "__main__":
    sys.exit(sc.run(CASES))
