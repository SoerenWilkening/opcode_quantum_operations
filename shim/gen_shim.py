#!/usr/bin/env python3
# gen_shim.py — M27, the ABI GRID half (IMPLEMENTATION_PLAN §3 Layer 5, Step 22).
#
# Reads CQ_lang's pinned tools/opcode_table.yaml and expands its own naming
# rules (:35-59) into the cq_template_* symbol grid, then emits one .gen.c per
# opcode family plus BOTH abort buckets.
#
# THE SPLIT SEAM IS `the ABI grid <-> the emitted bodies` and it was RECORDED in
# plan §3 before a line was written (Rule 12: a split is scheduled, never
# improvised). This file keeps everything the FROZEN ABI dictates — the yaml
# load and its sha, the naming-rule expander, the row model, signature(), each
# symbol's v1 bucket, PRD §1's partition audit, R7's per-family bucketing, the
# banner and --check. shim/gen_bodies.py takes a row and returns the text
# between the braces, and is the ONLY file in M27 that names a libcqops or M26
# symbol. Import is one-way; the Row record is the whole interface.
#
# THE PARTITION IS PRD §1 + §15 D14 + PRD-v2 §1, and it is asserted rather than
# assumed. It was 992 / 603 / 884 until v2's FIRST fp family landed
# (2026-09-18, bead 9ve.28); the live figures are EXPECTED below and the
# arithmetic of the move is in LANDED's own comment. Do not quote a number from
# this paragraph — quote EXPECTED, which the audit enforces.
#   2479 cq_template_* from opcode_table.yaml
#   = wrapper  thin WRAPPERS (integer, plus every LANDED fp family-width)
#   + inv      `_inv` ABORT bodies   (D14: _inv is f-inverse, and f-inverse does
#                                     not exist for and/or/udiv/trunc or ANY
#                                     compare — icmp and fcmp alike)
#   + fp       fp-touching ABORT bodies for every family-width NOT yet landed
#                                     (§1/PRD-v2 §1: the rest of fp is still v2)
# Not 1455 (that excludes i80, which IS in scope), not the 1474 phantom, not
# 878 (the fp count at revision ce3837bc, stale by the two i80 increments).
#
# SCOPE IS opcode_table.yaml ONLY (PRD §1, decided 2026-08-14). CQ_lang's
# intrinsic_table.yaml (389) and libm_table.yaml (12) emit cq_template_* into
# the SAME link namespace and stay CQ_lang's, which is why Step 23's gate reads
# "no undefined cq_template_* FROM THE OPCODE GRID".

import argparse
import hashlib
import os
import sys

# A stale shim/__pycache__/gen_bodies.pyc silently serves the PREVIOUS bodies, so
# a mutation battery over gen_bodies.py reports SURVIVED for a mutant that never
# loaded. That is the Step-9 `mv`-mtime leak and Step-21's same-second `touch`
# leak in Python form, and the failure direction is false-KILL as readily as
# false-SURVIVE. This removes the hazard at its root; the ctest registration sets
# PYTHONDONTWRITEBYTECODE=1 too, which does NOT cover a bare
# `python3 shim/gen_shim.py`.
sys.dont_write_bytecode = True

import yaml

import gen_bodies
import gen_intrinsics

# RESERVE SEAM, RECORDED 2026-09-19 (bead 9ve.24) BEFORE IT BITES. This file is
# at 292 counted lines of Rule 12's 300 after PRD-v2 6.1's vendoring, and the
# next family to land is one LANDED line plus nothing — but the next SCHEMA
# would be another expander. The cut is already drawn and is the one this
# landing took for the tables: THE MODEL AND THE RULES (Row, variant_axes,
# suffix, signature, landed_key, bucket_of, the reason tables) STAY HERE; the
# per-schema EXPANSION moves out, as shim/gen_intrinsics.py already has. The
# next thing to move is `expand` itself, to shim/gen_opcodes.py, which is ~35
# counted lines and would leave this file at ~257. Do NOT instead split the
# audit or the renderer: they are what every expander answers to, and two
# copies of either is two readings of one frozen ABI.

# --- Row model ---------------------------------------------------------------
# One record per emitted symbol. This is the whole interface to gen_bodies.
ROW_FIELDS = (
    "name",      # cq_template_...
    "kind",      # binary | unary | compare | cast
    "family",    # int_arith | int_bitwise | int_compare | int_width | fp_* ...
    "opcode",    # add | icmp | sext | fadd ...
    "pred",      # icmp/fcmp predicate, or ""
    "widths",    # every symbolic width the symbol TOUCHES (casts touch two)
    "bits",      # operand width in BITS, from the yaml's own widths map
    "to_bits",   # cast destination width in bits, else 0
    "ctype",     # C type of the classical operand, from the yaml's widths map
    "domain",    # the OPERAND width's domain, verbatim from the yaml: int | fp
    "variant",   # the yaml's own variant token: fwd_qq, controlled_inv_hl, ...
    "shape",     # qq | hl | lh | un   (un = arity-1: casts and fneg)
    "axis",      # fwd | unc | controlled
    "inv",       # bool — the `_inv` axis
    "source",    # opcode | intrinsic | libm — WHICH yaml this row came from
    "bucket",    # wrapper | inv | fp | defer
    "reason",    # None, or an OVERRIDE for this row's abort text (DECLINED)
    "ret",       # int32_t | void
    "params",    # [(ctype, name), ...] in ABI order
)


class Row(object):
    __slots__ = ROW_FIELDS

    def __init__(self, **kw):
        for f in ROW_FIELDS:
            setattr(self, f, kw[f])

    def decl(self):
        args = ", ".join("%s %s" % p for p in self.params)
        return "%s %s(%s)" % (self.ret, self.name, args)


# --- The naming rule (opcode_table.yaml:35-59) -------------------------------
# DERIVED from the yaml's own sentences rather than kept as an 18-entry lookup
# table, because a hand-kept table is a second transcription of the ABI:
#   "`_controlled` follows the shape suffix, `_inv` stays last"
#   "Only the `_hl`/`_lh` shape tokens are spelled, then `_unc` last; QQ is the
#    BARE base symbol + `_unc`"
# THE INTRINSIC TABLE ADDS THREE SHAPE TOKENS AND ONE MORE BARE ONE. `fma` is
# `arity: ternary` with `qqq`/`qql`/`qlq`/`qll`, and `fshl`/`fshr` are
# `arity: funnel` with `qql`; the header shows `qqq` is the BARE base symbol
# exactly as `qq` is, and the other three are spelled. Membership is EXACT
# rather than substring, so `qqq` is never read as `qq`.
SHAPE_TOKENS = ("hl", "lh", "qq", "qqq", "qql", "qlq", "qll")
BARE_SHAPES = ("qq", "qqq", "un")


def variant_axes(variant):
    t = variant.split("_")
    shape = "un"
    for tok in SHAPE_TOKENS:
        if tok in t:
            shape = tok
            break
    axis = "unc" if "unc" in t else "controlled" if "controlled" in t else "fwd"
    return shape, axis, ("inv" in t)


def suffix(shape, axis, inv):
    s = "" if shape in BARE_SHAPES else "_" + shape
    if axis == "controlled":
        s += "_controlled"
    if inv:
        s += "_inv"
    if axis == "unc":
        s += "_unc"
    return s


# --- Signature (PRD §14; validated against CQ_lang's generated header) --------
# The classical operand's C type is read out of the yaml's OWN widths map, never
# a hand-kept table — and the wrapper body's width comes from `bits`, never from
# sizeof(c_type): i80's _hl literal is __int128 (128 bits) carrying an 80-bit
# register, and i1's is bool (8 bits) carrying a 1-bit one.
# THE SHAPE TOKEN IS READ ONE CHARACTER PER OPERAND, which is what lets ONE
# rule serve arities 1, 2 and 3. `l` is a literal and EVERY other letter is a
# handle: `hl` is (handle, literal), `qq` two handles, `qql` two handles and a
# literal, `qlq` handle-literal-handle. `un` is the only token that is not an
# operand list and stays a special case — it is arity 1 by name, not by
# spelling. `kind` is kept in the signature because that is what the monkeypatch
# in tests/test_gen_bodies_run.py replaces.
def signature(kind, shape, axis, ctype):
    ps = []
    if axis == "unc":
        ps.append(("int32_t", "out_handle"))
    elif axis == "controlled":
        ps.append(("int32_t", "ctrl_flag"))
    if shape == "un":
        ps.append(("int32_t", "a_handle"))
    else:
        for i, ch in enumerate(shape):
            nm = "abc"[i]
            if ch == "l":
                ps.append((ctype, nm + "_classical"))
            else:
                ps.append(("int32_t", nm + "_handle"))
    return ("void" if axis == "unc" else "int32_t"), ps


# --- What has LANDED (PRD-v2 §1, §5, §7.15) ----------------------------------
# ONE ENTRY PER ABI ROW WHOSE KERNEL AND M26 ENTRY POINT BOTH EXIST. That row
# then takes the ORDINARY bucket rule below — a live wrapper, or a D14 `_inv`
# abort — and every other fp-touching row keeps its `"fp is v2"` abort. Adding
# the next family is a LINE here plus its dispatch row; nothing else in this
# file moves.
#
# THE KEY IS `(opcode, width)` AND NOT `(family, width)`, AND THE CHANGE WAS
# FORCED (2026-09-19, bead 9ve.36). `fp_arith` is SIX opcodes: `fadd`, `fsub`,
# `fmul` and `fdiv` have kernels, `frem` has none, and `fneg` — the yaml's ONE
# unary opcode — is in that family too and has none either. A family-grained key
# takes all six together, which for `frem` means a live wrapper emitting
# `CQ_SHIM_FOP_FREM`, an enumerator that does not exist, and for `fneg` a
# `KeyError` out of `gen_bodies.ENTRY`. The finer key is also STRICTLY more
# conservative: it can only ever land fewer rows than the coarse one.
#
# A CAST'S KEY NAMES BOTH WIDTHS, `(opcode, from, to)`, which is the same
# tightening one step further. `uitofp i64 -> f64` is the row it exists for:
# every other `uitofp` source width ships and that one is DECLINED below, so no
# coarser key could express the set at all.
#
# THE CONSERVATIVE DIRECTION IS `bucket_of`'s: a row touching ANY fp width is an
# abort unless its key is listed here. It is a whitelist, never a filter.
LANDED = frozenset((
    ("fcmp", "f64"),            # M36 / K18, bead 9ve.20 — 14 preds x 6 variants
    ("fadd", "f64"),            # M33 / K15, bead 9ve.21
    ("fsub", "f64"),            # M33 / K15, bead 9ve.21
    ("fmul", "f64"),            # M34 / K16, bead 9ve.22
    ("fdiv", "f64"),            # M35 / K17, bead 9ve.25
    # M37 / K19, bead 9ve.23 — the seventeen shipped conversion pairs. The
    # narrow rows are a COMPOSITION at the shim on upstream's own shape, not a
    # kernel each; see shim/cq_template_fparith.c.
    ("fptosi", "f64", "i8"),  ("fptosi", "f64", "i16"),
    ("fptosi", "f64", "i32"), ("fptosi", "f64", "i64"),
    ("fptoui", "f64", "i1"),  ("fptoui", "f64", "i8"),
    ("fptoui", "f64", "i16"), ("fptoui", "f64", "i32"),
    ("fptoui", "f64", "i64"),
    ("sitofp", "i8", "f64"),  ("sitofp", "i16", "f64"),
    ("sitofp", "i32", "f64"), ("sitofp", "i64", "f64"),
    ("uitofp", "i1", "f64"),  ("uitofp", "i8", "f64"),
    ("uitofp", "i16", "f64"), ("uitofp", "i32", "f64"),
    # ("uitofp", "i64", "f64") IS DELIBERATELY ABSENT — bead 9ve.34; see below.
    #
    # --- intrinsic_table.yaml, vendored 2026-09-19 (PRD-v2 §6.1, bead 9ve.24).
    ("fma", "f64"),             # M39 / K20 — 4 shapes x fwd/unc, NO `_inv`
    ("sqrt", "f64"),            # M40 / K21 — reached through cq_shim_fun
    # NOTHING ELSE FROM THAT TABLE LANDS HERE. `fneg`/`fabs`/`copysign`/
    # `fmin`/`fmax` and the five rounding opcodes are M38's (bead 9ve.27) and
    # the thirteen integer intrinsics are bead 9ve.29's; each keeps an abort
    # whose reason is TRUE of it, which for the integer half is not "fp is v2".
))

# --- Rows that are REFUSED rather than unported ------------------------------
# A KEY HERE IS NOT LANDED AND CARRIES ITS OWN ABORT TEXT. The default `"fp is
# v2"` would be a FALSEHOOD PRINTED AT RUNTIME for these: f64 IS v2 and is
# already shipping for the other seventeen conversion pairs, so a caller told to
# wait for the fp release is being pointed at a release that has happened.
#
# ALL THREE OF THE ROW'S SYMBOLS TAKE THIS REASON, INCLUDING `_inv`. D14's
# sentence attaches to a LANDED family-width — it is what an `_inv` of a family
# whose forward SHIPS is refused for — and this pair has not landed at all, so
# its `_inv` is not a D14 row. The reason a body prints must be TRUE of that
# body (gen_bodies' own recorded lesson, which cost 119 symbols a falsehood),
# and the bead is true of all three.
# --- Rows that abort for a reason that is NEITHER D14 NOR "fp is v2" ---------
# THE VENDORING EXPOSED A POPULATION THE THREE OLD BUCKETS COULD NOT DESCRIBE,
# and printing `"fp is v2"` at `cq_template_ctpop_i32` would be a falsehood of
# exactly the kind that cost 119 symbols one already: `ctpop` is an INTEGER
# opcode at an INTEGER width and no amount of floating-point work reaches it.
# PRD-v2 §6.1 measured that six L6 fixtures are blocked today by symbols with
# no fp in them at all.
#
# SO `defer` IS A FOURTH BUCKET RATHER THAN A FOURTH REASON INSIDE `fp`. The
# bucket is what the audit counts and what the .gen.c banner prints, so folding
# these rows into `fp` would make every published fp figure wrong by the 389
# minus the ten that landed — the "a count is not an identification" failure
# applied to the buckets themselves.
DEFER_REASON = {
    "intrinsic": "the integer llvm.* intrinsics are a v1-shaped port that has "
                 "not run yet — Bennett expands ctpop/ctlz/bswap/fshl in its "
                 "extractor, so Rule 1 is satisfiable (bead 9ve.29, "
                 "PRD-v2 7.14)",
    "libm": "lrint/llrint have no soft_lrint upstream and ZERO corpus calls; "
            "they are composable as soft_round then soft_fptosi the day a "
            "caller appears (PRD-v2 7.9)",
}

DECLINED = {
    ("uitofp", "i64", "f64"):
        "uitofp i64 -> f64 is REFUSED, not unported: upstream routes UIToFP to "
        "soft_sitofp with no bias correction at this width, so every u >= 2^63 "
        "would convert as a negative number (bead 9ve.34, PRD-v2 7.9)",
}


# --- Buckets (PRD §1 + §15 D14 + PRD-v2 §1) ----------------------------------
# ONE RULE, NO PER-SYMBOL SPECIAL CASE: a row touching an fp width is an abort
# unless its key is LANDED; otherwise it is D14's `_inv` abort or a wrapper.
# `DECLINED` changes only the TEXT of an abort, never which bucket it is in.
def landed_key(kind, opcode, widths):
    if kind == "cast":
        return (opcode, widths[0], widths[1])
    return (opcode, widths[0])


# `source` DECIDES WHICH RULE APPLIES, and it has to: a row from
# `opcode_table.yaml` that touches no fp width is a live wrapper by definition,
# while a row from `intrinsic_table.yaml` that touches no fp width is bead
# 9ve.29's and must NOT be. Reading that off the widths alone is impossible —
# `cq_template_add_i32` and `cq_template_ctpop_i32` are indistinguishable by
# domain — which is why the field exists rather than being derived.
def bucket_of(key, widths, inv, domains, source):
    if key in LANDED:
        return "inv" if inv else "wrapper"
    if source == "libm":
        # EVERY libm ROW DEFERS, AND ITS fp WIDTH IS NOT THE REASON. `lrint
        # f64 -> i64` touches f64, which SHIPS — so "fp is v2" would point a
        # caller at a release that has already happened. What is actually
        # missing is a construction: there is no `soft_lrint` upstream at any
        # width (PRD-v2 §7.9). The by-domain split below keeps these visible
        # as `fp_defer` rather than losing them among the integer ones.
        return "defer"
    if source == "intrinsic":
        # An UNLANDED intrinsic row. An fp one is honestly "fp is v2"; an
        # integer one is not, and `defer` is where it goes.
        return "fp" if any(domains[w] == "fp" for w in widths) else "defer"
    if any(domains[w] == "fp" for w in widths):
        return "fp"
    return "inv" if inv else "wrapper"


# --- Expansion ---------------------------------------------------------------
# ONE Row BUILDER, THREE YAMLS. `emit` is closed over the row list and takes the
# per-table width map, so `gen_intrinsics.py` builds rows through exactly the
# naming, signature and bucket rules `opcode_table.yaml` goes through — which is
# the whole point of putting the second grid behind the same model rather than
# a second generator.
def _emitter(rows, W):
    def emit(base, kind, family, opcode, pred, widths, variant, bits, to_bits,
             dom=None, source="opcode"):
        if dom is None:
            dom = {k: v["domain"] for k, v in W.items()}
        shape, axis, inv = variant_axes(variant)
        ctype = W[widths[0]]["c_type"]
        ret, params = signature(kind, shape, axis, ctype)
        key = landed_key(kind, opcode, widths)
        bucket = bucket_of(key, widths, inv, dom, source)
        if bucket == "fp":
            reason = DECLINED.get(key)
        elif bucket == "defer":
            reason = DEFER_REASON[source]
        else:
            reason = None
        rows.append(Row(
            name="cq_template_%s%s" % (base, suffix(shape, axis, inv)),
            kind=kind, family=family, opcode=opcode, pred=pred,
            widths=tuple(widths), bits=bits, to_bits=to_bits, ctype=ctype,
            domain=dom[widths[0]], source=source,
            variant=variant, shape=shape, axis=axis, inv=inv,
            bucket=bucket, reason=reason, ret=ret, params=params)
        )
    return emit


def expand(table):
    W = table["widths"]
    rows = []
    emit = _emitter(rows, W)
    preds = table["predicates"]

    for kind, key in (("binary", "binary_opcodes"), ("unary", "unary_opcodes")):
        for e in table[key]:
            for w in e["widths"]:
                for v in e["variants"]:
                    emit("%s_%s" % (e["opcode"], w), kind, e["family"],
                         e["opcode"], "", [w], v, W[w]["bits"], 0)
    for e in table["compare_opcodes"]:
        for p in preds[e["predicate_set"]]:
            for w in e["widths"]:
                for v in e["variants"]:
                    emit("%s_%s_%s" % (e["opcode"], p, w), "compare", e["family"],
                         e["opcode"], p, [w], v, W[w]["bits"], 0)
    for e in table["cast_opcodes"]:
        for pr in e["pairs"]:
            for v in e["variants"]:
                emit("%s_%s_to_%s" % (e["opcode"], pr["from"], pr["to"]), "cast",
                     e["kind"], e["opcode"], "", [pr["from"], pr["to"]], v,
                     W[pr["from"]]["bits"], W[pr["to"]]["bits"])
    return rows


# --- PRD §1's partition audit ------------------------------------------------
# A COUNT IS NOT AN IDENTIFICATION (this repo's own callout): 20 same-size
# variant transpositions leave every one of these figures exact while swapping a
# live family for an aborting one. The counts are the cheap guard; the SET
# assertions live in tests/test_gen_shim.py, where the oracle is CQ_lang's own
# independently generated header rather than this file's reading of the rules.
#
# THE THREE CODE BUCKETS DO NOT SEPARATE INTEGER FROM fp, SO THE AUDIT CARRIES A
# SECOND, FINER SPLIT. Once a family-width lands, `wrapper` holds integer AND fp
# wrappers and `inv` holds integer AND fp D14 aborts, so the three coarse
# figures can stay exact while a family moves domain — which is precisely the
# "a count is not an identification" failure one line down. The five-way split
# is what a reader and a reviewer actually need, and it is what the bead reports.
BUCKETS = ("wrapper", "inv", "fp", "defer")

EXPECTED = {"total": 2880, "wrapper": 1122, "inv": 668, "fp": 841,
            "defer": 249}
# SEVEN POPULATIONS, AND THE THREE NEW ONES ARE WHAT MAKES THE VENDORING
# VISIBLE. `int_defer` is bead 9ve.29's integer intrinsics; `fp_defer` is
# libm's twelve, which are fp on their OPERAND width; `fp_abort` is unchanged
# in meaning and grew by the unported fp intrinsics. Without this split the
# four coarse figures stay exact while 389 symbols change domain — the
# failure the five-way split was added for one landing ago.
EXPECTED_BY_DOMAIN = {"int_wrapper": 992, "int_inv": 603, "int_defer": 237,
                      "fp_wrapper": 130, "fp_inv": 65, "fp_abort": 841,
                      "fp_defer": 12}


def _is_fp(r):
    return r.bucket == "fp" or any(w.startswith("f") for w in r.widths)


def audit(rows):
    names = [r.name for r in rows]
    if len(names) != len(set(names)):
        raise SystemExit("gen_shim: duplicate symbol names in the expansion")
    got = {"total": len(rows)}
    for b in BUCKETS:
        got[b] = sum(1 for r in rows if r.bucket == b)
    if sum(got[b] for b in BUCKETS) != got["total"]:
        raise SystemExit("gen_shim: buckets do not partition the grid")
    fine = {k: 0 for k in EXPECTED_BY_DOMAIN}
    for r in rows:
        dom = "fp_" if _is_fp(r) else "int_"
        fine[dom + ("abort" if r.bucket == "fp" else r.bucket)] += 1
    if got != EXPECTED or fine != EXPECTED_BY_DOMAIN:
        raise SystemExit("gen_shim: partition audit FAILED\n"
                         "  expected %r / %r\n  got      %r / %r\n"
                         "  (PRD §1 + §15 D14 + PRD-v2 §1 + §6.1; re-pinning "
                         "any of the three yamls or extending LANDED is a "
                         "tracked act — update both dicts deliberately)"
                         % (EXPECTED, EXPECTED_BY_DOMAIN, got, fine))
    got.update(fine)
    return got


# --- Emission (R7: one .gen.c per opcode family) -----------------------------
# Four of the ten families are MIXED (int_arith 342 wrap / 228 abort,
# int_bitwise 300/200, int_compare 240/120, int_width 110/55), which is exactly
# why "wrappers <-> aborts" is NOT the file seam. FIVE since 2026-09-18:
# `fp_compare` is 56 wrap / 28 D14-inv / 252 still-fp-abort, which also makes it
# the first family carrying all THREE buckets at once.
# THE SOURCE LINE IS PER FAMILY NOW, because three yamls feed this generator
# and a banner naming `opcode_table.yaml` above `cq_template_fma_f64` would be
# a provenance claim that is simply false. `SOURCE_YAML` maps a row's `source`
# to the vendored path and its sha, and `render` asserts a family is fed by
# exactly ONE of them.
BANNER = ("/* AUTOGENERATED by shim/gen_shim.py — do not edit.\n"
          " * Source: third_party/cq_lang/%s\n"
          " * yaml-sha256: %s\n"
          " * family: %s (%d symbols: %d wrapper, %d _inv abort, %d fp abort,"
          " %d deferred)\n"
          " */\n")

SOURCE_YAML = {"opcode": "opcode_table.yaml",
               "intrinsic": "intrinsic_table.yaml",
               "libm": "libm_table.yaml"}


def render(family, rows, shas):
    rows = sorted(rows, key=lambda r: r.name)
    srcs = {r.source for r in rows}
    if len(srcs) != 1:
        raise SystemExit("gen_shim: family `%s` is fed by %d yamls (%s); a "
                         "banner can only name one provenance"
                         % (family, len(srcs), ", ".join(sorted(srcs))))
    src = srcs.pop()
    n = {b: sum(1 for r in rows if r.bucket == b) for b in BUCKETS}
    out = [BANNER % (SOURCE_YAML[src], shas[src], family, len(rows),
                     n["wrapper"], n["inv"], n["fp"], n["defer"])]
    out.append('#include <stdbool.h>\n#include <stdint.h>\n\n#include "cq_shim.h"\n\n')
    out.append("/* Prototypes — the ABI this file satisfies, in CQ_lang's own order. */\n")
    out += ["%s;\n" % r.decl() for r in rows]
    out.append("\n")
    for r in rows:
        out.append("%s {\n%s}\n\n" % (r.decl(), gen_bodies.body(r)))
    return "".join(out)


def files(rows, shas):
    fams = sorted({r.family for r in rows})
    return {"cq_template_%s.gen.c" % f:
            render(f, [r for r in rows if r.family == f], shas) for f in fams}


def load(path):
    raw = open(path, "rb").read()
    return yaml.safe_load(raw), hashlib.sha256(raw).hexdigest()


def main(argv=None):
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="Generate the libcqops cq_template_* shim.")
    _tp = os.path.join(here, os.pardir, "third_party", "cq_lang")
    ap.add_argument("--yaml", default=os.path.join(_tp, "opcode_table.yaml"))
    ap.add_argument("--intrinsic-yaml",
                    default=os.path.join(_tp, "intrinsic_table.yaml"))
    ap.add_argument("--libm-yaml", default=os.path.join(_tp, "libm_table.yaml"))
    ap.add_argument("--output-dir", default=os.path.join(here, "generated"))
    ap.add_argument("--check", action="store_true",
                    help="regenerate in memory and diff against disk; CI's drift gate")
    ap.add_argument("--list-symbols", action="store_true",
                    help="print '<bucket> <declaration>' per symbol and exit")
    a = ap.parse_args(argv)

    table, sha = load(a.yaml)
    itable, isha = load(a.intrinsic_yaml)
    ltable, lsha = load(a.libm_yaml)
    shas = {"opcode": sha, "intrinsic": isha, "libm": lsha}

    rows = expand(table)
    # THE SECOND AND THIRD GRIDS GO THROUGH THE SAME Row BUILDER (PRD-v2 §6.1).
    # They are appended rather than expanded in place because the three tables
    # carry three different schemas and one loop over all of them would have to
    # branch per field — see shim/gen_intrinsics.py's header.
    iemit = _emitter(rows, itable["widths"])
    gen_intrinsics.expand_intrinsic(itable, iemit)
    lemit = _emitter(rows, ltable["widths"])
    gen_intrinsics.expand_libm(ltable, lemit)
    counts = audit(rows)

    if a.list_symbols:
        for r in sorted(rows, key=lambda r: r.name):
            sys.stdout.write("%s %s;\n" % (r.bucket, r.decl()))
        return 0

    want = files(rows, shas)
    if a.check:
        stale = []
        for fn, text in sorted(want.items()):
            p = os.path.join(a.output_dir, fn)
            if not os.path.exists(p) or open(p).read() != text:
                stale.append(fn)
        # Only .gen.c is ours to account for: a stray .DS_Store, a __pycache__ or
        # an editor swapfile in the output directory is not shim drift, and a
        # drift gate that goes red for those gets turned off.
        on_disk = [f for f in os.listdir(a.output_dir) if f.endswith(".gen.c")] \
            if os.path.isdir(a.output_dir) else []
        extra = sorted(set(on_disk) - set(want))
        if stale or extra:
            sys.stderr.write("gen_shim --check: OUT OF DATE\n  stale: %s\n  extra: %s\n"
                             % (", ".join(stale) or "-", ", ".join(extra) or "-"))
            return 1
        sys.stdout.write("gen_shim --check: up to date (%(total)d symbols)\n" % counts)
        return 0

    if not os.path.isdir(a.output_dir):
        os.makedirs(a.output_dir)
    for fn, text in sorted(want.items()):
        open(os.path.join(a.output_dir, fn), "w").write(text)
    sys.stdout.write("gen_shim: %(total)d symbols = %(wrapper)d wrappers"
                     " + %(inv)d _inv aborts + %(fp)d fp aborts"
                     " + %(defer)d deferred\n"
                     "          (int %(int_wrapper)d wrap / %(int_inv)d inv"
                     " / %(int_defer)d defer;"
                     " fp %(fp_wrapper)d wrap / %(fp_inv)d inv"
                     " / %(fp_abort)d abort / %(fp_defer)d defer)\n"
                     % counts)
    return 0


if __name__ == "__main__":
    sys.exit(main())
