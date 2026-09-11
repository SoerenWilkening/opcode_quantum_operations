#!/usr/bin/env python3
"""tools/l6/l6_run.py — Step 24 (L6) driver.

Runs every CQ_lang e2e fixture through `run_slice_cqops.sh` — CQ_lang's own
front end and lowering pass, linked against libcqops instead of CQ_lang's
trace-only runtime stub — and reports LINK / RUN / ABORT per fixture.

THE GATE IS PRD 15 D18: THE FIXTURES LINK, RUN, AND DO NOT ABORT. There is no
diff and there must never be one; read the header of run_slice_cqops.sh for the
two measured reasons the goldens are not an oracle in any column.

THE FIXTURE LIST IS READ FROM CTEST, NOT FROM THE CMakeLists. `ctest
--show-only=json-v1` hands back the fully resolved command line of every
registered e2e entry, generator expressions and toolchain paths included, so the
driver cannot drift from what CQ_lang actually registers and cannot miss an
entry a regex over CMake source would.

CQ_LANG IS NOT PINNED BY THIS REPO AND IT MOVES. The revision is recorded in
the report, by SHA and date, every run. Quote ratios out of it, never counts.

THE VERDICT LIVES IN `l6_report.py` SINCE 2026-09-10 (`bd wgi`), split off at
Rule 12's limit along the seam IMPLEMENTATION_PLAN §3's Layer 6 table recorded
in advance. WHAT IS LEFT HERE BREAKS WITH CQ_LANG AND WITH NOTHING ELSE: the
fixture list, the revision, the two archive members, the four-stage
invocation. What moved breaks with THIS repository: `classify`, the `DEFERRED`
regex, the residue observation. That discriminator -- which repository a line
breaks with -- is the seam; `run` vs `report` sounds like the same cut and is
not, because `--reuse` already makes the report runnable without the run and a
cut there would only move the loop below.
"""
import argparse, json, os, shutil, subprocess, sys

# A stale tools/l6/__pycache__/l6_report.pyc silently serves the PREVIOUS
# verdict logic, and a re-classification that reads a cached `classify` is a
# report about code that never loaded -- `shim/gen_shim.py`'s reason, one
# harness over. Set BEFORE the import below, which is the only thing it can
# still protect: this module's own .pyc is never written, since nothing imports
# it. The ctest registration does NOT set PYTHONDONTWRITEBYTECODE for L6 or L7
# (only the Step-22 shim suites carry it), so this line is the whole defence,
# and it covers a bare `python3 tools/l6/l6_run.py` too.
sys.dont_write_bytecode = True

from l6_report import (DEFERRED, STRAND, classify, first_error, first_line,
                       read, residue, residue_summary, residue_totals)

HERE = os.path.dirname(os.path.abspath(__file__))
RUNNER = os.path.join(HERE, "run_slice_cqops.sh")


def cq_revision(cqdir):
    g = subprocess.run(["git", "log", "-1", "--format=%H %ad", "--date=short"],
                       cwd=cqdir, capture_output=True, text=True)
    dirty = subprocess.run(["git", "status", "--porcelain"], cwd=cqdir,
                           capture_output=True, text=True).stdout.strip()
    return g.stdout.strip() + ("" if not dirty else "  (DIRTY WORKING TREE)")


def fixtures(cqdir, builddir):
    """Every registered e2e slice, as (name, kind, argv-slice)."""
    r = subprocess.run(["ctest", "--test-dir", builddir, "--show-only=json-v1"],
                       cwd=cqdir, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("ctest --show-only failed:\n" + r.stderr)
    out = []
    for t in json.loads(r.stdout)["tests"]:
        c = t["command"]
        if len(c) > 1 and c[1].endswith("run_slice.sh"):
            # bash sh CLANG OPT PASS INC SRC RT TPL GOLDEN
            out.append((t["name"], "single", dict(
                clang=c[2], opt=c[3], llvmlink="", pas=c[4], inc=c[5],
                src=c[6], libsrcs="")))
        elif len(c) > 1 and c[1].endswith("run_slice_multi.sh"):
            # bash sh CLANG OPT LLVMLINK PASS INC CQSRC LIBSRCS RT TPL GOLDEN
            out.append((t["name"], "multi", dict(
                clang=c[2], opt=c[3], llvmlink=c[4], pas=c[5], inc=c[6],
                src=c[7], libsrcs=c[8])))
    return out


def extract_intrinsics(cqdir, builddir, out):
    """`ar x` the two members we DO take, fresh, on every run.

    FRESH IS THE POINT. An objdir that persists between runs is a cache of
    whatever CQ_lang's archive held when it was last extracted, and this project
    has already been bitten twice by a stale copy standing in for the tree (the
    Step 9 `mv`-mtime leak, and `bd 18j`'s backup that captured a mutant).

    AND THE MEMBERS ARE NAMED, NEVER GLOBBED. `libcq_templates.a` holds THREE
    objects and the third is `cq_templates.c.o` -- the trace-only stub with a
    second definition of all 2479 grid symbols. Taking the archive, or extracting
    it wholesale, is `bd 216` checklist item 20's silent failure: measured
    2026-08-27, the same two archives in opposite order both link cleanly and
    behave differently, with CQ_lang's stub serving every call in one of them.
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
    for m in members:
        if not os.path.exists(os.path.join(d, m)):
            sys.exit("ar x did not produce " + m)
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cqlang", default="/Users/sorenwilkening/Desktop/CQ_lang")
    ap.add_argument("--cqbuild", default="build")
    ap.add_argument("--archive", default="build-release/libcqops.a")
    ap.add_argument("--objdir", default="",
                    help="where to put cq_intrinsic_templates.c.o and "
                         "cq_libm_templates.c.o; extracted fresh with `ar x` on "
                         "every run unless the directory is supplied ready-made")
    ap.add_argument("--out", required=True)
    ap.add_argument("--sink", default="counter")
    ap.add_argument("--only", default="")
    ap.add_argument("--json", default="")
    ap.add_argument("--reuse", action="store_true")
    ap.add_argument("--gate", action="store_true",
                    help="exit non-zero unless every fixture LINKED and RAN")
    ap.add_argument("--min-ok", type=int, default=0,
                    help="floor on the OK count. A FLOOR, not a pin: CQ_lang is "
                         "not pinned by this repo and its corpus grows, so an "
                         "exact count would churn on every upstream commit while "
                         "a floor still catches an in-scope fixture regressing "
                         "into the casualty list")
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    archive = os.path.abspath(a.archive)
    objdir = (os.path.abspath(a.objdir) if a.objdir
              else extract_intrinsics(a.cqlang, a.cqbuild, a.out))

    print("CQ_lang @", cq_revision(a.cqlang))
    print("archive  ", archive)
    print("sink     ", a.sink)

    env = dict(os.environ, CQOPS_SINK=a.sink)
    rows, counts = [], {}
    for name, kind, f in fixtures(a.cqlang, a.cqbuild):
        if a.only and a.only not in name:
            continue
        stem = os.path.join(a.out, name)
        # --reuse RE-READS a completed run's artifacts instead of re-running the
        # pipeline. It exists because the classification is the cheap half and
        # the four-stage pipeline is the expensive one: correcting how a verdict
        # is read must not cost another full corpus pass, and re-running would
        # also silently re-measure against whatever the tree holds NOW.
        if not (a.reuse and os.path.exists(stem + ".rc")):
            for ext in (".rc", ".bin", ".err", ".out", ".link", ".map",
                        ".prov", ".residue"):
                if os.path.exists(stem + ext):
                    os.remove(stem + ext)   # a stale .rc reports a stale run
            r = subprocess.run(
                ["bash", RUNNER, f["clang"], f["opt"], f["llvmlink"], f["pas"],
                 f["inc"], f["src"], f["libsrcs"], archive, objdir, stem],
                capture_output=True, text=True, env=env)
            extra = r.stderr
        else:
            extra = ""
        err = read(stem + ".err")
        link = read(stem + ".link") + extra
        ran = os.path.exists(stem + ".rc")
        rc = int(read(stem + ".rc").strip() or -1) if ran else -1
        verdict = classify(ran, rc, err, os.path.exists(stem + ".prov"))
        counts[verdict] = counts.get(verdict, 0) + 1
        d = DEFERRED.search(err)
        res = residue(stem + ".residue")
        rows.append(dict(name=name, kind=kind, verdict=verdict, rc=rc,
                         src=os.path.basename(f["src"]),
                         strand=bool(STRAND.search(err)),
                         residue=res,
                         deferred=d.group(1) if d else "",
                         reason=d.group(2) if d else "",
                         err=first_line(err), link=first_error(link)))
        # The split, only where there IS one. A fixture whose certificate
        # discharged every rail prints nothing extra, which keeps the casualty
        # list readable and makes a residue line mean something.
        tail = ""
        if res and res["stranded_qubits"]:
            tail = (f"   residue: {res['stranded_dirty']} convicted + "
                    f"{res['stranded_unproven']} unproven qubits over "
                    f"{res['frees_dirty']} dirty + {res['frees_unproven']} "
                    f"unproven frees")
        print(f"  {verdict:11s} {name}{tail}")

    print("\n--- L6 summary ---")
    for k in sorted(counts):
        print(f"  {k:11s} {counts[k]}")
    print(residue_summary(rows))
    if a.json:
        json.dump(dict(cq_revision=cq_revision(a.cqlang), archive=archive,
                       sink=a.sink, counts=counts,
                       residue=residue_totals(rows), rows=rows),
                  open(a.json, "w"), indent=1)

    if not a.gate:
        return 0
    # THE GATE IS PRD §15 D18's, AND `DEFERRED` IS GREEN ON PURPOSE: a fixture
    # reaching a v1-deferred symbol aborts loudly and BY DESIGN (PRD §1). What is
    # red is a fixture that failed to build or link, ran and aborted for a reason
    # we do not own, or linked against a CQ_lang archive.
    bad = sum(counts.get(k, 0)
              for k in ("BUILD_FAIL", "ABORT", "RUN_FAIL", "PROVENANCE"))
    ok = counts.get("OK", 0)
    for r in rows:
        if r["verdict"] not in ("OK", "DEFERRED"):
            print(f"  L6 FAILURE {r['verdict']:11s} {r['name']}  "
                  f"rc={r['rc']}  {r['err'] or r['link']}")
    if ok < a.min_ok:
        print(f"  L6 FAILURE: {ok} fixtures ran to completion, floor is "
              f"{a.min_ok} — an in-scope fixture has regressed into the "
              f"casualty list")
    return 1 if (bad or ok < a.min_ok) else 0


sys.exit(main())
