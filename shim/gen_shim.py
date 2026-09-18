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
    "bucket",    # wrapper | inv | fp
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
def variant_axes(variant):
    t = variant.split("_")
    shape = "hl" if "hl" in t else "lh" if "lh" in t else "qq" if "qq" in t else "un"
    axis = "unc" if "unc" in t else "controlled" if "controlled" in t else "fwd"
    return shape, axis, ("inv" in t)


def suffix(shape, axis, inv):
    s = "_hl" if shape == "hl" else "_lh" if shape == "lh" else ""
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
def signature(kind, shape, axis, ctype):
    ps = []
    if axis == "unc":
        ps.append(("int32_t", "out_handle"))
    elif axis == "controlled":
        ps.append(("int32_t", "ctrl_flag"))
    if kind in ("binary", "compare"):
        if shape == "hl":
            ps += [("int32_t", "a_handle"), (ctype, "b_classical")]
        elif shape == "lh":
            ps += [(ctype, "a_classical"), ("int32_t", "b_handle")]
        else:
            ps += [("int32_t", "a_handle"), ("int32_t", "b_handle")]
    else:
        ps.append(("int32_t", "a_handle"))
    return ("void" if axis == "unc" else "int32_t"), ps


# --- What has LANDED (PRD-v2 §1, §5, §7.15) ----------------------------------
# ONE ENTRY PER (yaml FAMILY, yaml WIDTH) WHOSE KERNEL AND M26 ENTRY POINT BOTH
# EXIST. That pair's rows then take the ORDINARY bucket rule below — a live
# wrapper, or a D14 `_inv` abort — and every other fp family-width keeps its
# `"fp is v2"` abort. Adding the next family is ONE LINE here plus its dispatch
# row; nothing else in this file moves.
#
# THE KEY IS A PAIR AND NOT A FAMILY, and that is PRD §15 D16's "family first,
# WIDTH second" arriving on the template grid. `fp_compare` is in scope; f16,
# f32 and f80 are not (PRD-v2 §1a), and `cqrt_alloc_f<W>` still aborts at those
# three widths, so an f16 rail cannot exist to hand a live wrapper.
#
# A CAST TOUCHES TWO WIDTHS AND THE RULE BELOW REQUIRES **ALL** OF ITS fp
# WIDTHS TO BE LANDED. That is the conservative direction: a cross-domain
# `sitofp_i32_to_f64` stays an abort until `int_to_fp`/`f64` is listed here,
# rather than going live because one of its two widths is.
LANDED = frozenset((
    ("fp_compare", "f64"),      # M36 / K18, bead 9ve.20 — 14 preds x 6 variants
))                              # = 84 symbols: 56 wrappers + 28 D14 `_inv`


# --- Buckets (PRD §1 + §15 D14 + PRD-v2 §1) ----------------------------------
def bucket_of(widths, inv, domains, family):
    fp = [w for w in widths if domains[w] == "fp"]
    if fp and not all((family, w) in LANDED for w in fp):
        return "fp"
    return "inv" if inv else "wrapper"


# --- Expansion ---------------------------------------------------------------
def expand(table):
    W = table["widths"]
    dom = {k: v["domain"] for k, v in W.items()}
    preds = table["predicates"]
    rows = []

    def emit(base, kind, family, opcode, pred, widths, variant, bits, to_bits):
        shape, axis, inv = variant_axes(variant)
        ctype = W[widths[0]]["c_type"]
        ret, params = signature(kind, shape, axis, ctype)
        rows.append(Row(
            name="cq_template_%s%s" % (base, suffix(shape, axis, inv)),
            kind=kind, family=family, opcode=opcode, pred=pred,
            widths=tuple(widths), bits=bits, to_bits=to_bits, ctype=ctype,
            domain=dom[widths[0]],
            variant=variant, shape=shape, axis=axis, inv=inv,
            bucket=bucket_of(widths, inv, dom, family), ret=ret, params=params))

    for kind, key in (("binary", "binary_opcodes"), ("unary", "unary_opcodes")):
        for e in table[key]:
            for w in e["widths"]:
                for v in e["variants"]:
                    emit("%s_%s" % (e["opcode"], w), kind, e["family"], e["opcode"],
                         "", [w], v, W[w]["bits"], 0)
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
EXPECTED = {"total": 2479, "wrapper": 1048, "inv": 631, "fp": 800}
EXPECTED_BY_DOMAIN = {"int_wrapper": 992, "int_inv": 603,
                      "fp_wrapper": 56, "fp_inv": 28, "fp_abort": 800}


def _is_fp(r):
    return r.bucket == "fp" or any(w.startswith("f") for w in r.widths)


def audit(rows):
    names = [r.name for r in rows]
    if len(names) != len(set(names)):
        raise SystemExit("gen_shim: duplicate symbol names in the expansion")
    got = {"total": len(rows)}
    for b in ("wrapper", "inv", "fp"):
        got[b] = sum(1 for r in rows if r.bucket == b)
    if got["wrapper"] + got["inv"] + got["fp"] != got["total"]:
        raise SystemExit("gen_shim: buckets do not partition the grid")
    fine = {"int_wrapper": 0, "int_inv": 0, "fp_wrapper": 0, "fp_inv": 0,
            "fp_abort": 0}
    for r in rows:
        if r.bucket == "fp":
            fine["fp_abort"] += 1
        else:
            fine[("fp_" if _is_fp(r) else "int_") + r.bucket] += 1
    if got != EXPECTED or fine != EXPECTED_BY_DOMAIN:
        raise SystemExit("gen_shim: partition audit FAILED\n"
                         "  expected %r / %r\n  got      %r / %r\n"
                         "  (PRD §1 + §15 D14 + PRD-v2 §1; re-pinning "
                         "opcode_table.yaml or extending LANDED is a tracked "
                         "act — update both dicts deliberately)"
                         % (EXPECTED, EXPECTED_BY_DOMAIN, got, fine))
    got.update(fine)
    return got


# --- Emission (R7: one .gen.c per opcode family) -----------------------------
# Four of the ten families are MIXED (int_arith 342 wrap / 228 abort,
# int_bitwise 300/200, int_compare 240/120, int_width 110/55), which is exactly
# why "wrappers <-> aborts" is NOT the file seam. FIVE since 2026-09-18:
# `fp_compare` is 56 wrap / 28 D14-inv / 252 still-fp-abort, which also makes it
# the first family carrying all THREE buckets at once.
BANNER = ("/* AUTOGENERATED by shim/gen_shim.py — do not edit.\n"
          " * Source: third_party/cq_lang/opcode_table.yaml\n"
          " * yaml-sha256: %s\n"
          " * family: %s (%d symbols: %d wrapper, %d _inv abort, %d fp abort)\n"
          " */\n")


def render(family, rows, sha):
    rows = sorted(rows, key=lambda r: r.name)
    n = {b: sum(1 for r in rows if r.bucket == b) for b in ("wrapper", "inv", "fp")}
    out = [BANNER % (sha, family, len(rows), n["wrapper"], n["inv"], n["fp"])]
    out.append('#include <stdbool.h>\n#include <stdint.h>\n\n#include "cq_shim.h"\n\n')
    out.append("/* Prototypes — the ABI this file satisfies, in CQ_lang's own order. */\n")
    out += ["%s;\n" % r.decl() for r in rows]
    out.append("\n")
    for r in rows:
        out.append("%s {\n%s}\n\n" % (r.decl(), gen_bodies.body(r)))
    return "".join(out)


def files(rows, sha):
    fams = sorted({r.family for r in rows})
    return {"cq_template_%s.gen.c" % f: render(f, [r for r in rows if r.family == f], sha)
            for f in fams}


def load(path):
    raw = open(path, "rb").read()
    return yaml.safe_load(raw), hashlib.sha256(raw).hexdigest()


def main(argv=None):
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="Generate the libcqops cq_template_* shim.")
    ap.add_argument("--yaml", default=os.path.join(
        here, os.pardir, "third_party", "cq_lang", "opcode_table.yaml"))
    ap.add_argument("--output-dir", default=os.path.join(here, "generated"))
    ap.add_argument("--check", action="store_true",
                    help="regenerate in memory and diff against disk; CI's drift gate")
    ap.add_argument("--list-symbols", action="store_true",
                    help="print '<bucket> <declaration>' per symbol and exit")
    a = ap.parse_args(argv)

    table, sha = load(a.yaml)
    rows = expand(table)
    counts = audit(rows)

    if a.list_symbols:
        for r in sorted(rows, key=lambda r: r.name):
            sys.stdout.write("%s %s;\n" % (r.bucket, r.decl()))
        return 0

    want = files(rows, sha)
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
    sys.stdout.write("gen_shim: %(total)d symbols = %(wrapper)d wrappers + %(inv)d _inv aborts"
                     " + %(fp)d fp aborts\n"
                     "          (int %(int_wrapper)d wrap / %(int_inv)d inv;"
                     " fp %(fp_wrapper)d wrap / %(fp_inv)d inv / %(fp_abort)d abort)\n"
                     % counts)
    return 0


if __name__ == "__main__":
    sys.exit(main())
