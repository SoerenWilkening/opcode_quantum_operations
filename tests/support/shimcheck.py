#!/usr/bin/env python3
# shimcheck.py — the Python side of tests/support/, for Step 22's two suites.
#
# tests/support/harness.c's job, in Python: a TAP-13 runner whose FAILURE PATH
# is itself asserted (see the negative controls in both suites — a CHECK that
# cannot fail makes every suite vacuously green, which is exactly what
# tests/test_harness_negative.c exists to rule out on the C side).
#
# It also owns the two things both suites need: loading the INDEPENDENT ABI
# manifest (tests/abi/cq_templates_abi.txt) and parsing definitions back out of
# the emitted .gen.c files. Nothing here knows how the grid is derived; that
# is shim/gen_shim.py's, and keeping the two apart is what makes the manifest
# an oracle rather than a mirror.

import hashlib
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHIM = os.path.join(ROOT, "shim")
GENERATED = os.path.join(SHIM, "generated")
YAML = os.path.join(ROOT, "third_party", "cq_lang", "opcode_table.yaml")
MANIFEST = os.path.join(ROOT, "tests", "abi", "cq_templates_abi.txt")

# A width token in a symbol NAME, which is how the fp/integer partition is
# derived on the test side — from the ABI's own spelling, never from
# gen_shim.bucket_of, whose input is the yaml's `widths` map. Two independent
# routes to the same 884/1595, which is the point.
FP_WIDTH = re.compile(r"(?:^|_)(f16|f32|f64|f80)(?:_|$)")
DECL = re.compile(r"^(int32_t|void) (cq_template_[A-Za-z0-9_]+)\(([^)]*)\);$")
DEFN = re.compile(r"^(int32_t|void) (cq_template_[A-Za-z0-9_]+)\(([^)]*)\) \{$")


class Fail(Exception):
    pass


def check(cond, msg):
    if not cond:
        raise Fail(msg)


def diff_sets(got, want, what):
    # A COUNT IS NOT AN IDENTIFICATION. Both directions, and the message names
    # symbols rather than a number, so a provocation says WHAT moved.
    missing, extra = sorted(want - got), sorted(got - want)
    check(not missing and not extra,
          "%s: %d missing %s, %d extra %s"
          % (what, len(missing), missing[:4], len(extra), extra[:4]))


def manifest_yaml_sha():
    for line in open(MANIFEST):
        m = re.match(r"^#\s+yaml-sha256\s*:\s*([0-9a-f]{64})$", line)
        if m:
            return m.group(1)
    raise Fail("manifest carries no yaml-sha256 provenance line")


def manifest_decls():
    # name -> the full declaration, verbatim as CQ_lang generated it.
    #
    # THE PROVENANCE CHECK RUNS AT THE LOAD, NOT ONLY AS A CASE, and what it
    # proves is narrow enough to be worth stating exactly: sha256(the pinned
    # yaml) equals the sha CQ_lang's generator stamped into the header these
    # lines came from, so the manifest expands THE GRID WE SHIP. It proves
    # nothing about the 2479 lines themselves — a hand-edited declaration would
    # pass it — which is why the count and uniqueness are checked here too, and
    # why the file says never to hand-edit one. Gating the LOAD is what makes
    # the R3 comparison honest: the L4 goldens' COMMIT check hard-errors before
    # a golden is read, and a check that only sits in a case lets every other
    # case run against an unverified oracle.
    sha = hashlib.sha256(open(YAML, "rb").read()).hexdigest()
    recorded = manifest_yaml_sha()
    check(sha == recorded,
          "tests/abi manifest was extracted from a DIFFERENT opcode_table.yaml:\n"
          "  pinned yaml : %s\n  manifest    : %s\n"
          "  Re-pin, then re-extract from CQ_lang's regenerated header." % (sha, recorded))
    out = {}
    for line in open(MANIFEST):
        m = DECL.match(line.rstrip("\n"))
        if m:
            check(m.group(2) not in out, "duplicate declaration in the manifest: %s" % m.group(2))
            out[m.group(2)] = line.rstrip("\n").rstrip(";")
    check(len(out) == 2479, "manifest holds %d declarations, expected 2479" % len(out))
    return out


def is_fp(name):
    return FP_WIDTH.search(name) is not None


def generate(outdir=None):
    # Run the generator exactly as CI would, into a scratch directory by
    # default — a test must never write into the tree it is checking.
    tmp = outdir or tempfile.mkdtemp(prefix="cqops-shim-")
    subprocess.check_output([sys.executable, os.path.join(SHIM, "gen_shim.py"),
                             "--yaml", YAML, "--output-dir", tmp],
                            stderr=subprocess.STDOUT)
    return tmp


def parse_definitions(files):
    # {filename: text} -> {name: (declaration-without-semicolon, [body lines])}
    out = {}
    for fn in sorted(files):
        if not fn.endswith(".gen.c"):
            continue
        lines = files[fn].split("\n")
        i = 0
        while i < len(lines):
            m = DEFN.match(lines[i])
            if m:
                decl, body = lines[i].rstrip(" {"), []
                i += 1
                while lines[i] != "}":
                    body.append(lines[i])
                    i += 1
                out[m.group(2)] = (decl, body)
            i += 1
    return out


def read_dir(directory):
    return {fn: open(os.path.join(directory, fn)).read()
            for fn in sorted(os.listdir(directory)) if fn.endswith(".gen.c")}


# THE BUCKET IS READ BACK OUT OF THE EMITTED BODY, not asked of gen_shim. What
# ships is what is tested, and the two abort reasons have to be distinguishable
# from the outside or "the abort set is exactly the 603" cannot be stated.
FP_REASON = "fp is v2"
INV_REASON = "_inv is f-inverse, not f; the data family has no uniform definition (PRD 15 D14)"


def bucket_of_body(body):
    text = "\n".join(body)
    if "cq_shim_unsupported(" not in text:
        return "wrapper"
    if '"%s"' % FP_REASON in text:
        return "fp"
    if '"%s"' % INV_REASON in text:
        return "inv"
    return "abort-with-an-unrecognised-reason"


def run(cases, argv=None):
    # TAP 13, mirroring tests/support/harness.c so a ctest run reads the same
    # whichever language the suite is in.
    argv = sys.argv[1:] if argv is None else argv
    if argv:
        # A typo'd case name selected NOTHING and exited 0 — a run that cannot
        # fail, which is precisely what tests/test_harness_negative.c rules out
        # on the C side.
        missing = sorted(set(argv) - {c.__name__ for c in cases})
        if missing:
            print("Bail out! no such case(s): %s" % ", ".join(missing))
            return 2
        cases = [c for c in cases if c.__name__ in argv]
    print("TAP version 13")
    print("1..%d" % len(cases))
    failed = 0
    for n, case in enumerate(cases, 1):
        try:
            case()
            print("ok %d - %s" % (n, case.__name__))
        except Exception as e:  # noqa: BLE001 — a case may fail any way at all
            failed += 1
            print("not ok %d - %s" % (n, case.__name__))
            for line in ("%s: %s" % (type(e).__name__, e)).split("\n"):
                print("# %s" % line)
    if failed:
        print("# %d of %d case(s) FAILED" % (failed, len(cases)))
    return 1 if failed else 0


def abi_header(tmp):
    # CQ_lang's 2479 declarations, verbatim, as a forcible -include. Putting
    # them AHEAD of a .gen.c is what makes a signature divergence a
    # `conflicting types` error instead of a silent link-time mismatch that
    # `nm` cannot see.
    p = os.path.join(tmp, "cq_templates_abi.h")
    open(p, "w").write("#include <stdbool.h>\n#include <stdint.h>\n" + "\n".join(
        l.rstrip("\n") for l in open(MANIFEST) if not l.startswith("#") and l.strip()))
    return p


def compile_gen(directory, tmp=None):
    # Returns (rc, output) for the first file that fails, or (0, "").
    tmp = tmp or tempfile.mkdtemp(prefix="cqops-shim-cc-")
    cc = os.environ.get("CQOPS_CC", "cc")
    abi = abi_header(tmp)
    for fn in sorted(os.listdir(directory)):
        if not fn.endswith(".gen.c"):
            continue
        r = subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wconversion",
                            "-ffp-contract=off", "-I" + SHIM, "-include", abi, "-c",
                            os.path.join(directory, fn), "-o", os.path.join(tmp, fn + ".o")],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if r.returncode != 0:
            return r.returncode, "%s: %s" % (fn, r.stdout.decode())
    return 0, ""
