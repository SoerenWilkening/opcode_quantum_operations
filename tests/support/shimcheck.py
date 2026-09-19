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
# THREE (yaml, manifest) PAIRS SINCE PRD-v2 §6.1's VENDORING (bead 9ve.24), and
# they are a LIST OF PAIRS rather than two parallel lists for the reason the
# CMake side gives at more length: three manifests and three yamls is nine
# pairings, of which three are right, and an arm that checked the intrinsic
# manifest against the opcode yaml would be red on a correct tree.
SOURCES = (
    (os.path.join(ROOT, "third_party", "cq_lang", "opcode_table.yaml"),
     os.path.join(ROOT, "tests", "abi", "cq_templates_abi.txt"), 2479),
    (os.path.join(ROOT, "third_party", "cq_lang", "intrinsic_table.yaml"),
     os.path.join(ROOT, "tests", "abi", "cq_intrinsic_templates_abi.txt"), 389),
    (os.path.join(ROOT, "third_party", "cq_lang", "libm_table.yaml"),
     os.path.join(ROOT, "tests", "abi", "cq_libm_templates_abi.txt"), 12),
)
N_GRID = sum(n for _, _, n in SOURCES)          # 2880

YAML = SOURCES[0][0]
MANIFEST = SOURCES[0][1]

# A width token in a symbol NAME, which is how the fp/integer partition is
# derived on the test side — from the ABI's own spelling, never from
# gen_shim.bucket_of, whose input is the yaml's `widths` map. Two independent
# routes to the same 884/1595, which is the point.
FP_WIDTH = re.compile(r"(?:^|_)(f16|f32|f64|f80)(?:_|$)")

# THE LANDED fp ROWS, READ OFF THE NAME — the test side's INDEPENDENT route to
# shim/gen_shim.py's LANDED, whose input is the yaml's `widths` map and the
# yaml's own opcode and cast-pair lists. Neither can reach the other, which is
# the whole point: if gen_shim landed a row by mistake the grid would go live
# and this predicate would still say it had not, so every bucket set below goes
# red and NAMES the symbols.
#
# ONE ALTERNATIVE PER LANDED FAMILY, added the same day its gen_shim entry is,
# and the WIDTH LISTS ARE SPELLED OUT rather than written `i\d+`: `uitofp` ships
# from i1, i8, i16 and i32 and NOT from i64 (bead 9ve.34), and a lazy `i\d+`
# would declare the one row this repo refuses to be live.
LANDED_NAME = re.compile(
    r"^cq_template_(?:"
    r"fcmp_[a-z]+_f64"
    r"|f(?:add|sub|mul|div)_f64"
    r"|fptosi_f64_to_i(?:64|32|16|8)"
    r"|fptoui_f64_to_i(?:64|32|16|8|1)"
    r"|sitofp_i(?:64|32|16|8)_to_f64"
    r"|uitofp_i(?:32|16|8|1)_to_f64"
    # --- intrinsic_table.yaml, vendored 2026-09-19 (bead 9ve.24). The four
    # `fma` shapes at f64 and `sqrt` at f64; NOTHING at f16/f32/f80 and no
    # other opcode from that table.
    r"|fma_f64"
    r"|sqrt_f64"
    r")(?:_|$)")

# THE ROWS THAT ARE REFUSED RATHER THAN UNPORTED — gen_shim.DECLINED's test-side
# twin, again by NAME. They are NOT landed and they are NOT `"fp is v2"`: f64 is
# in v2's scope and seventeen sibling conversion pairs already ship, so that
# sentence would be a falsehood printed at runtime.
DECLINED_NAME = re.compile(r"^cq_template_uitofp_i64_to_f64(?:_|$)")

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


def manifest_yaml_sha(manifest=None):
    for line in open(manifest or MANIFEST):
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
    out = {}
    for yaml_path, manifest, n_want in SOURCES:
        sha = hashlib.sha256(open(yaml_path, "rb").read()).hexdigest()
        recorded = manifest_yaml_sha(manifest)
        check(sha == recorded,
              "%s was extracted from a DIFFERENT %s:\n"
              "  pinned yaml : %s\n  manifest    : %s\n"
              "  Re-pin, then re-extract from CQ_lang's regenerated header."
              % (os.path.basename(manifest), os.path.basename(yaml_path),
                 sha, recorded))
        n = 0
        for line in open(manifest):
            m = DECL.match(line.rstrip("\n"))
            if m:
                # ACROSS ALL THREE, which is also the DISJOINTNESS claim: the
                # three grids share no symbol name, measured at the vendoring
                # and asserted here on every load.
                check(m.group(2) not in out,
                      "duplicate declaration across the manifests: %s" % m.group(2))
                out[m.group(2)] = line.rstrip("\n").rstrip(";")
                n += 1
        check(n == n_want, "%s holds %d declarations, expected %d"
              % (os.path.basename(manifest), n, n_want))
    check(len(out) == N_GRID,
          "the three manifests hold %d declarations, expected %d"
          % (len(out), N_GRID))
    return out


def is_fp(name):
    return FP_WIDTH.search(name) is not None


def is_landed(name):
    # An fp-touching symbol whose ABI ROW v2 has IMPLEMENTED. Such a symbol is
    # NOT in the `"fp is v2"` bucket: its non-`_inv` rows are live wrappers and
    # its `_inv` rows are D14 aborts, exactly as an integer family's are.
    return LANDED_NAME.match(name) is not None


def is_declined(name):
    # Not landed, and NOT `"fp is v2"` either: a row refused for a reason of its
    # own. It stays in the `fp` BUCKET — the counts and the file banners do not
    # move — and only the sentence differs.
    return DECLINED_NAME.match(name) is not None


def is_deferred_fp(name):
    return is_fp(name) and not is_landed(name)


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
# Bead 9ve.34's row (2026-09-19). A THIRD REASON IN THE SAME `fp` BUCKET — not a
# fourth bucket — so the three coarse counts, the five-way domain split and
# every `.gen.c` banner are unmoved by it.
DECLINED_REASON = (
    "uitofp i64 -> f64 is REFUSED, not unported: upstream routes UIToFP to "
    "soft_sitofp with no bias correction at this width, so every u >= 2^63 "
    "would convert as a negative number (bead 9ve.34, PRD-v2 7.9)")


# THE FOURTH BUCKET (bead 9ve.24). `"fp is v2"` printed at
# `cq_template_ctpop_i32` would be a falsehood — an INTEGER opcode at an
# INTEGER width that no amount of fp work reaches — and `lrint f64 -> i64`
# touches a width that already SHIPS, so neither can honestly take the fp
# reason. Both are transcribed here rather than imported from gen_shim, which
# is what makes this an independent reading of what actually shipped.
DEFER_INTRINSIC_REASON = (
    "the integer llvm.* intrinsics are a v1-shaped port that has not run yet "
    "— Bennett expands ctpop/ctlz/bswap/fshl in its extractor, so Rule 1 is "
    "satisfiable (bead 9ve.29, PRD-v2 7.14)")
DEFER_LIBM_REASON = (
    "lrint/llrint have no soft_lrint upstream and ZERO corpus calls; they are "
    "composable as soft_round then soft_fptosi the day a caller appears "
    "(PRD-v2 7.9)")


def bucket_of_body(body):
    text = "\n".join(body)
    if "cq_shim_unsupported(" not in text:
        return "wrapper"
    if '"%s"' % FP_REASON in text or '"%s"' % DECLINED_REASON in text:
        return "fp"
    if '"%s"' % INV_REASON in text:
        return "inv"
    if ('"%s"' % DEFER_INTRINSIC_REASON in text
            or '"%s"' % DEFER_LIBM_REASON in text):
        return "defer"
    return "abort-with-an-unrecognised-reason"


# A NAME-BASED TWIN FOR THE FOURTH BUCKET, sharing nothing with the yamls. The
# integer intrinsics and the two libm opcodes, by the only thing the test side
# can see from outside: the symbol's own stem.
DEFER_NAME = re.compile(
    r"^cq_template_(?:"
    r"bswap|bitreverse|fshl|fshr|ctpop|ctlz|cttz|abs"
    r"|smin|smax|umin|umax|uadd_sat|sadd_sat|usub_sat|ssub_sat"
    r"|lrint|llrint"
    r")_")


def is_defer(name):
    return bool(DEFER_NAME.match(name))


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
