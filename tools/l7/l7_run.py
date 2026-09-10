#!/usr/bin/env python3
"""tools/l7/l7_run.py — Step 25 (L7), PRD §12(1) and the through-CQ_lang half
of §12(2).

Compiles `tools/l7/grover.cq.c` with CQ_lang's front end, lowers it with
CQ_lang's `cq-lowering` pass, links it against `libcqops` and NO CQ_lang runtime
archive, and RUNS IT TWICE -- once in quantum mode and once in classical mode --
through libcqops' printf sink.

WHAT IT ASSERTS, and each clause is here because something else cannot make it:

  1. IT LOWERS.        §12's listing as printed does NOT (PRD §15 D22); read
                       grover.cq.c's header for the two upstream declines and
                       why the accepted spelling is not a workaround.
  2. IT LINKS, AGAINST THIS ARCHIVE ALONE. The provenance arm is
                       run_slice_cqops.sh's (`bd 216` item 20): no object on the
                       link map may come from a CQ_lang archive, because a
                       silently-shadowed grid symbol looks exactly like a
                       successful link from the outside.
  3. IT RUNS AND DOES NOT ABORT.   That much is D18's L6 gate.
  4. IT EMITS A NON-EMPTY GATE STREAM.   THIS IS THE CLAUSE L6 DOES NOT HAVE,
                       and it is the whole reason L7(1) is not subsumed by L6: a
                       program that folded entirely away links, runs, and does
                       not abort. Asserted as a gate MIX -- x, cx, ccx, ry, rz
                       and mz must all appear -- rather than as a line count,
                       because "the oracle built a circuit" is a claim about
                       Toffolis and a claim about rotations, and a count of
                       either alone can be satisfied by the other.
  5. CLASSICAL MODE EMITS NO GATE AT ALL, and measures what plain C computes.
                       §12(2)'s zero-cost half, delivered through the real front
                       end instead of through the frozen ABI. `tests/
                       test_grover.c` makes the same claim about a bigger
                       program; this one makes it about a program CQ_lang
                       compiled, which no unit test can.
  6. NO PHASE LANDS ON THE MEASURED RAIL.   Added 2026-09-10 (`bd 2tm`), and it
                       is here because clauses 1-5 WERE ALL GREEN against a
                       circuit that was the IDENTITY on the marked branch. The
                       retired `cq_phi(x, θ)` spelling emitted a TENSORED
                       `cqrt_rz_<W>` -- Rz on every qubit of the data register
                       -- whose marked phase is (-1)^popcount(target) and which,
                       being diagonal per bit, FIXES |0..0>, so the reflection
                       about |0> could never put -1 there (CQ_lang's own
                       finding, `include/CQ.h` at `cq_phi`). A tensored Rz emits
                       MORE `rz` lines than the kickback form, not fewer, so
                       clause 4's MIX cannot see the difference and neither can
                       any count: an assertion that COUNTS is not an assertion
                       that IDENTIFIES. What separates the two circuits is WHICH
                       QUBIT, and the robust form of that is a DISJOINTNESS --
                       every `rz` target must be outside the set of `mz`
                       targets. It reads no golden and survives any amount of
                       upstream re-lowering that keeps the phase on a flag.

THE TWO RUNS SHARE ONE LOWERED ARTEFACT. The angle is read off `argc`, so
"replace M_PI/2 with M_PI" is two runs of one binary rather than two
compilations -- which is what makes "the same program in two modes" a fact about
the circuit rather than about two circuits that resemble each other.

NO GOLDEN, DELIBERATELY. The gate-count pin lives in `tests/test_grover.c`,
against a program whose shape this repo controls; CQ_lang is NOT pinned here and
its lowering moves, so a count pinned against its output would churn on every
upstream commit -- risk R5, one layer up. What is pinned here is the MIX and the
zero, both of which survive any amount of upstream re-lowering that still emits
a circuit.

OPT-IN behind -DCQOPS_CQLANG_DIR=, for L6's reason: this repository neither pins
nor can build CQ_lang, and a report that cannot name the revision it ran against
is not a report.
"""
import argparse, json, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
RUNNER = os.path.join(os.path.dirname(HERE), "l6", "run_slice_cqops.sh")
SOURCE = os.path.join(HERE, "grover.cq.c")

# M23's spelling, and it is NOT cqrt_x / h<N>: our gate stream is one level below
# the handle-level trace CQ_lang's goldens record, and a sink is handed a raw
# qubit index it can only print as q<N> (PRD §8; CLAUDE.md's M23 callout).
GATE = re.compile(r"^(x|cx|ccx|ry|rz|mz)\(", re.M)
REQUIRED = ("x", "cx", "ccx", "ry", "rz", "mz")

# Clause 6's two operand readings. `rz` carries `(q<N>, <angle>)` and `mz` a bare
# `(q<N>)`; both are M23's spelling, and the qubit INDEX is all a sink is ever
# handed (PRD §8) -- which is exactly enough here, because the claim is a
# disjointness between two index sets and not an identification of a rail.
RZ_TARGET = re.compile(r"^rz\((q\d+),", re.M)
MZ_TARGET = re.compile(r"^mz\((q\d+)\)", re.M)


def cq_revision(cqdir):
    g = subprocess.run(["git", "log", "-1", "--format=%H %ad", "--date=short"],
                       cwd=cqdir, capture_output=True, text=True)
    dirty = subprocess.run(["git", "status", "--porcelain"], cwd=cqdir,
                           capture_output=True, text=True).stdout.strip()
    return g.stdout.strip() + ("" if not dirty else "  (DIRTY WORKING TREE)")


def toolchain(cqdir, builddir):
    """CLANG / OPT / PASS / INC, read out of CTEST rather than out of CMake.

    Same reason l6_run.py does it: `ctest --show-only=json-v1` hands back the
    fully resolved command line of a registered e2e entry, generator expressions
    and toolchain paths included, so the driver cannot drift from what CQ_lang
    actually uses and cannot miss a path a regex over CMake source would.
    """
    r = subprocess.run(["ctest", "--test-dir", builddir, "--show-only=json-v1"],
                       cwd=cqdir, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("ctest --show-only failed:\n" + r.stderr)
    for t in json.loads(r.stdout)["tests"]:
        c = t["command"]
        if len(c) > 1 and c[1].endswith("run_slice.sh"):
            # bash sh CLANG OPT PASS INC SRC ...
            return dict(clang=c[2], opt=c[3], pas=c[4], inc=c[5])
    sys.exit("no single-file e2e slice registered in " + builddir)


def extract_intrinsics(cqdir, builddir, out):
    """The two objects the link DOES take, extracted FRESH on every run.

    Named, never globbed, and the archive itself is never on the link line:
    `libcq_templates.a`'s third member is CQ_lang's trace-only stub with a second
    definition of all 2479 grid symbols, and measured, the same two archives in
    opposite order both link cleanly and behave differently. `bd 216` item 20;
    `tools/l6/run_slice_cqops.sh`'s header carries the full statement.
    """
    src = os.path.join(cqdir, builddir, "runtime", "libcq_templates.a")
    if not os.path.exists(src):
        sys.exit("no cq_templates archive at " + src)
    d = os.path.join(os.path.abspath(out), "_intrinsics")
    if os.path.exists(d):
        shutil.rmtree(d)
    os.makedirs(d)
    members = ["cq_intrinsic_templates.c.o", "cq_libm_templates.c.o"]
    r = subprocess.run(["ar", "x", src] + members, cwd=d, capture_output=True,
                       text=True)
    if r.returncode != 0:
        sys.exit("ar x failed: " + r.stderr)
    return d


def read(p):
    try:
        with open(p, "r", errors="replace") as fh:
            return fh.read()
    except OSError:
        return ""


def gate_mix(text):
    mix = {}
    for m in GATE.finditer(text):
        mix[m.group(1)] = mix.get(m.group(1), 0) + 1
    return mix


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cqlang", required=True)
    ap.add_argument("--cqbuild", default="build")
    ap.add_argument("--archive", default="build-release/libcqops.a")
    ap.add_argument("--out", required=True)
    ap.add_argument("--json", default="")
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    archive = os.path.abspath(a.archive)
    tc = toolchain(a.cqlang, a.cqbuild)
    objdir = extract_intrinsics(a.cqlang, a.cqbuild, a.out)
    stem = os.path.join(a.out, "grover")

    print("L7 — PRD §12(1), Grover through CQ_lang")
    print("  CQ_lang @", cq_revision(a.cqlang))
    print("  archive  ", archive)

    # `.residue` is written by the l6_residue.c object the shared runner puts on
    # every link line (`bd c55`). L7 does not read it — the residue is L6's
    # corpus-scale observation — but a stale one left beside a fresh run would
    # be a stale artefact claiming to describe it.
    for ext in (".rc", ".bin", ".err", ".out", ".link", ".map", ".prov",
                ".residue"):
        if os.path.exists(stem + ext):
            os.remove(stem + ext)          # a stale .rc reports a stale run

    # Stages 1-4, the same four the L6 corpus goes through. Its stage 4 runs the
    # binary once with no argument, which IS the quantum arm; the classical arm
    # is the second run below.
    r = subprocess.run(
        ["bash", RUNNER, tc["clang"], tc["opt"], "", tc["pas"], tc["inc"],
         SOURCE, "", archive, objdir, stem],
        capture_output=True, text=True,
        env=dict(os.environ, CQOPS_SINK="printf"))

    fails = []
    if os.path.exists(stem + ".prov"):
        fails.append("PROVENANCE: a CQ_lang archive is on the link map")
    if not os.path.exists(stem + ".rc"):
        # Front end, lowering or LINK, and the three write to different places:
        # the pass declines on `r.stderr`, the linker writes `.link`. Reporting
        # only the first would have said "(no message)" for the commonest real
        # failure here -- a missing $CQOPS_L6_LDLIBS, i.e. the archive's own qec
        # dependencies absent from the line (measured 2026-08-28, running this
        # driver by hand instead of through ctest).
        why = [l for l in (r.stderr.strip().splitlines() +
                           [l for l in read(stem + ".link").splitlines()
                            if "was built for newer" not in l])
               if l.strip()]
        fails.append("DID NOT BUILD: " + (why[0].strip() if why
                                          else "(no message)"))
        report(fails, {}, {}, "", "", a)
        return 1

    rc = int(read(stem + ".rc").strip() or -1)
    q_out = read(stem + ".out")
    q_mix = gate_mix(q_out)
    if rc != 0:
        fails.append(f"QUANTUM RUN rc={rc}: " +
                     (read(stem + ".err").strip().splitlines() or [""])[0])

    # Clause 4 — the gate MIX, not a line count.
    for g in REQUIRED:
        if not q_mix.get(g):
            fails.append(f"QUANTUM STREAM has no `{g}` — §12(1)'s "
                         f"'emits a gate stream' is not satisfied by a program "
                         f"that folded away")

    # Clause 6 — the phase is on a FLAG, not on the register that gets measured.
    #
    # Stated as a disjointness rather than as "rz appears twice" on purpose: the
    # count is what a re-lowering legitimately moves, and the placement is what a
    # semantic regression moves. Under kickback the phase rides a minted i1 flag
    # the compare's `_unc` returns to |0> and `cqrt_free` reclaims, so no `rz`
    # target can also be an `mz` target; under the retired tensored spelling
    # EVERY `rz` target was one.
    #
    # PROVOKED 2026-09-10, both directions, because an assertion nobody has seen
    # fail is an assertion nobody has tested: silent on the real stream (`rz` on
    # {q22, q23}, `mz` on {q0..q7}), and firing and naming `q0` on the same
    # stream with one `rz` target rewritten to a measured lane.
    rz_on, mz_on = set(RZ_TARGET.findall(q_out)), set(MZ_TARGET.findall(q_out))
    if rz_on & mz_on:
        fails.append("PHASE ON THE MEASURED RAIL " +
                     repr(sorted(rz_on & mz_on)) + " — a branch phase lowers by "
                     "KICKBACK onto a minted flag; an `rz` on a rail that is "
                     "later measured is the TENSORED spelling, which is the "
                     "identity on |0…0> and marks nothing")

    # Clause 5 — the classical arm, same binary, one argument.
    c = subprocess.run([stem + ".bin", "classical"], capture_output=True,
                       text=True, env=dict(os.environ, CQOPS_SINK="printf"))
    c_mix = gate_mix(c.stdout)
    if c.returncode != 0:
        fails.append(f"CLASSICAL RUN rc={c.returncode}: " +
                     (c.stderr.strip().splitlines() or [""])[0])
    if c_mix:
        fails.append("CLASSICAL MODE EMITTED GATES " + repr(c_mix) +
                     " — §12(2)'s zero-cost half is L5 at program scale and "
                     "this is where it would break")

    q_val = tail_value(q_out)
    c_val = tail_value(c.stdout)
    if c_val is None:
        fails.append("CLASSICAL RUN printed no `grover -> <n>` line")

    report(fails, q_mix, c_mix, q_val, c_val, a)
    return 1 if fails else 0


def tail_value(text):
    for line in reversed(text.splitlines()):
        m = re.match(r"^grover -> (\d+)$", line.strip())
        if m:
            return int(m.group(1))
    return None


def report(fails, q_mix, c_mix, q_val, c_val, a):
    print("  quantum   gates:", dict(sorted(q_mix.items())), "measured:", q_val)
    print("  classical gates:", dict(sorted(c_mix.items())), "measured:", c_val)
    if a.json:
        json.dump(dict(cq_revision=cq_revision(a.cqlang),
                       quantum=dict(gates=q_mix, measured=q_val),
                       classical=dict(gates=c_mix, measured=c_val),
                       failures=fails),
                  open(a.json, "w"), indent=1)
    for f in fails:
        print("  L7 FAILURE:", f)
    print("  L7", "FAILED" if fails else "OK")


if __name__ == "__main__":
    sys.exit(main())
