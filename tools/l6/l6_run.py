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

A CASUALTY IS NOT A FAILURE. Anything reaching a v1-deferred symbol aborts
loudly and BY DESIGN (PRD 1 and PRD 15 D16), so the driver CLASSIFIES rather
than merely passing or failing, and it matches on our own abort text so a
casualty is never reported as an unexplained abort. Measured 2026-08-27 against
CQ_lang 134e625, the four reasons that actually fire are `fp is v2`, `qram is
v2`, `tape is v2`, and `cqrt_alloc_handle` -- the last being D16's refusal to
mint a register-less handle, which is what stops the purely-INTEGER
intrinsic-bearing fixtures. Not one casualty was an integer `_inv` body.
`tape is v2` STOPPED FIRING on 2026-09-02 (PRD 15 D23 put the tape in scope),
and `qram is v2` stopped later the same day (PRD 15 D24 put QRAM in scope at
all nine widths — the fp-width qram fixtures now stop at `fp is v2` instead);
if either ever appears again, a shim file has regressed, not the corpus.

THE RESIDUE IS READ ALONGSIDE THE GATE AND IS NEVER A GATE (PRD 15 D18). Until
`bd c55` the only thing observable from outside a fixture was whether D15 3's
one-shot strand line had fired -- a BOOLEAN, where the split shipped TWO GRAINS
that genuinely disagree. `cqops_read_residue` is the public read, and
`l6_residue.c` -- the HARNESS's translation unit, on the link line, never the
library's -- writes it beside the run's other artefacts. A fixture that ABORTS
writes none, because abort() runs no destructor; that absence is reported as
absence and never as zeroes.
"""
import argparse, json, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
RUNNER = os.path.join(HERE, "run_slice_cqops.sh")

# OUR OWN LOUD v1 DEFERRAL, and it is matched on the EXACT text
# `cq_shim_unsupported` prints (shim/cq_shim_ctx.c) -- not on a guess. It is
# deliberately NOT the `libcqops: FATAL: <layer>:` house shape every other hard
# error in the library uses, so a regex written from that shape matches nothing
# and every casualty reads as an unexplained ABORT.
DEFERRED = re.compile(r"^cqops: (\S+) not implemented \((.*)\)", re.M)
STRAND = re.compile(r"strand", re.I)


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


RESIDUE_FIELDS = ("stranded_dirty", "stranded_unproven", "frees_dirty",
                  "frees_unproven", "stranded_qubits", "strand_reports")


def residue(path):
    """The six-field read `tools/l6/l6_residue.c` wrote, or None.

    NONE IS NOT ZEROES. A fixture that aborted, or that never linked, ran no
    destructor and so has no final residue at all; reporting it as a row of
    zeroes would say "this program leaked nothing", which is a claim nobody
    made. Every consumer below distinguishes the two.
    """
    text = read(path)
    if not text.strip():
        return None
    out = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0] in RESIDUE_FIELDS:
            try:
                out[parts[0]] = int(parts[1])
            except ValueError:
                return None
    return out if len(out) == len(RESIDUE_FIELDS) else None


def classify(ran, rc, err, prov):
    """LINK / RUN / ABORT, plus the v1-deferred row.

    `ran` is False when the fixture never reached stage 4 -- a front-end,
    lowering or LINK failure, which is a different fact from "it ran and
    aborted" and must never be folded into it. 128+SIGABRT is 134 in a shell's
    status, and that is the one outcome this gate exists to detect.
    """
    if prov:
        return "PROVENANCE"
    if not ran:
        return "BUILD_FAIL"
    if rc == 0:
        return "OK"
    if DEFERRED.search(err):
        return "DEFERRED"        # a v1 boundary, loud and by design
    if rc >= 128:
        return "ABORT"
    return "RUN_FAIL"


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


def residue_totals(rows):
    """Corpus totals over the fixtures that actually reported one."""
    seen = [r["residue"] for r in rows if r["residue"]]
    t = {k: sum(x[k] for x in seen) for k in RESIDUE_FIELDS}
    t["fixtures_reporting"] = len(seen)
    t["fixtures_with_residue"] = sum(1 for x in seen if x["stranded_qubits"])
    return t


def residue_summary(rows):
    """THE SENTENCE D15 3's SPLIT WAS BUILT TO MAKE (`bd c55`).

    IT IS AN OBSERVATION AND NOT A GATE, exactly as PRD 15 D18 requires -- the
    verdict above is LINK / RUN / DO NOT ABORT and nothing here feeds it. What
    it adds is resolution: "the certificate discharged every rail in N of M
    fixtures" was already sayable from the boolean, and "and in the others it
    left X qubits convicted and Y merely unproven" was not.

    THE TWO GRAINS ARE PRINTED SEPARATELY AND ARE NOT ADDED TOGETHER. A mixed
    rail contributes to both qubit rows and to the rail-level DIRTY row alone,
    so a single figure would discard exactly the rail the split exists for.
    """
    t = residue_totals(rows)
    n = t["fixtures_reporting"]
    if not n:
        return ("  residue    no fixture reported one — every run aborted or "
                "failed to link before its reader could write")
    clean = n - t["fixtures_with_residue"]

    # THE ONE THING THE HARNESS CAN CHECK ABOUT ITSELF, and it is src/reg.h's
    # own assertion: every strand increments a qubit row beside the strand it
    # describes, so the two rows sum to the pool's total. A mismatch here is
    # NOT a fixture leaking — it is `l6_residue.c` having mislabelled a field or
    # the library's free path stranding somewhere that did not go through
    # cq_reg_free, and either way this report is lying. It is printed and not
    # gated: PRD §15 D18 is explicit that the residue is read ALONGSIDE the gate
    # and never as one, and the verdict above owns the exit code.
    bad = [r["name"] for r in rows if r["residue"]
           and r["residue"]["stranded_qubits"] !=
               r["residue"]["stranded_dirty"] + r["residue"]["stranded_unproven"]]
    warn = ("" if not bad else
            f"\n             !! the two qubit rows do not sum to the pool total "
            f"in {len(bad)} fixture(s): {', '.join(bad[:4])} — this report is "
            f"not trustworthy")
    return (
        f"  residue    {clean} of {n} fixtures that ran to the end stranded "
        f"nothing\n"
        f"             qubits: {t['stranded_dirty']} convicted, "
        f"{t['stranded_unproven']} merely unproven "
        f"(pool total {t['stranded_qubits']})\n"
        f"             frees:  {t['frees_dirty']} landed on the dirty row, "
        f"{t['frees_unproven']} on the unproven row" + warn)


def read(p):
    try:
        with open(p, "r", errors="replace") as fh:
            return fh.read()
    except OSError:
        return ""


def first_line(s):
    for l in s.splitlines():
        if l.strip():
            return l.strip()[:200]
    return ""


def first_error(s):
    for l in s.splitlines():
        if "error" in l.lower() or "Undefined" in l:
            return l.strip()[:200]
    return ""


sys.exit(main())
