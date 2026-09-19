# shim/gen_intrinsics.py — the SECOND and THIRD expansion paths: CQ_lang's
# `intrinsic_table.yaml` (389 symbols) and `libm_table.yaml` (12), vendored
# 2026-09-19 under PRD-v2 §6.1 and expanded into the SAME `Row` model
# `opcode_table.yaml` uses.
#
# WHY A SEPARATE MODULE AND NOT MORE OF gen_shim.py. Rule 12 counts
# non-blank, non-comment lines and `gen_shim.py` stood at 233 of 300 before
# this landed. The seam is the one the three sources already draw for
# themselves — THE OPCODE GRID <-> THE INTRINSIC AND LIBM GRIDS — and it moves
# no logic: the Row model, the naming rule, the signature rule, the bucket rule
# and the audit all stay in one place next door, because two copies of any of
# them is two readings of one frozen ABI.
#
# THE SCHEMAS ARE NOT `opcode_table.yaml`'s, AND THAT IS THE WHOLE REASON THIS
# FILE EXISTS RATHER THAN THREE MORE LOOPS IN `expand`.
#
#   intrinsic_table.yaml   top-level `widths` (NINE: i1..i64, f16..f80 — no
#                          i80, no i128) and ONE flat `intrinsics:` list. An
#                          entry is {op, llvm_id, class, reversibility, arity,
#                          commutative?, widths, variants}. THERE IS NO
#                          `family` KEY, so the .gen.c split is assigned here
#                          rather than read; and `arity` is FIVE values, three
#                          of which the opcode table has never seen — `rotate`,
#                          `funnel` and `ternary`.
#   libm_table.yaml        top-level `widths` (FIVE) and a `libm:` list whose
#                          entries are {op, kind, froms, to} — a LIST of source
#                          widths and ONE target, with no `variants` key at
#                          all.
#
# `fshl` AND `fshr` EACH APPEAR TWICE, and reading the list as keyed by `op`
# loses half of each. The class-1 row is a `rotate` with `[fwd_hl, inv_hl,
# unc_hl]` and the class-2 row is a `funnel` with `[fwd_qql, unc_qql]`; the
# unique key is `(op, arity)`. The two rows produce DISJOINT symbol names
# because their variant tokens differ, so nothing collides — but an expander
# that de-duplicated by `op` would emit 12 of each opcode's 20 symbols and the
# link gate would name the other 8.
#
# `cross_unary`'s VARIANTS ARE NOT IN THE YAML and are hard-coded below, which
# is the one place this file reads a sentence rather than a field:
# `libm_table.yaml` says `cross_unary` is "NON-injective (round) -> fwd+unc",
# so there is no `_inv` and there are exactly two variants per (from, to) pair.
# 2 ops x 3 froms x 2 variants = 12, which is the count `COMMIT.intrinsics`
# records and CQ_lang's own generated header carries.

# --- The .gen.c family, assigned rather than read ----------------------------
# THE SPLIT IS BY DOMAIN, because that is the split a reader and the audit
# both need: the integer intrinsics are a v1-shaped gap (PRD-v2 §7.14, bead
# 9ve.29) and the fp ones are v2's, and mixing them into one file would put
# two different abort reasons behind one banner with nothing saying which rows
# take which.
def _family(dom, w):
    return "intrinsic_fp" if dom[w] == "fp" else "intrinsic_int"


def expand_intrinsic(table, emit):
    """Expand intrinsic_table.yaml through `emit`, gen_shim's Row builder."""
    W = table["widths"]
    dom = {k: v["domain"] for k, v in W.items()}

    for e in table["intrinsics"]:
        for w in e["widths"]:
            for v in e["variants"]:
                emit(base="%s_%s" % (e["op"], w), kind=e["arity"],
                     family=_family(dom, w), opcode=e["op"], pred="",
                     widths=[w], variant=v, bits=W[w]["bits"], to_bits=0,
                     dom=dom, source="intrinsic")


# `cross_unary` is fwd + unc and nothing else — libm_table.yaml's own
# "NON-injective (round) -> fwd+unc". Spelled as a constant here so an added
# variant upstream is a COUNT mismatch in the audit rather than a silent drop.
CROSS_UNARY_VARIANTS = ("fwd", "unc")


def expand_libm(table, emit):
    """Expand libm_table.yaml through `emit`, gen_shim's Row builder."""
    W = table["widths"]
    dom = {k: v["domain"] for k, v in W.items()}

    for e in table["libm"]:
        if e["kind"] != "cross_unary":
            raise SystemExit(
                "gen_intrinsics: libm_table.yaml grew a `%s` kind; this "
                "expander knows only `cross_unary`, whose variant list is "
                "hard-coded from the yaml's own prose. Read the new kind's "
                "rule before adding it." % e["kind"])
        for f in e["froms"]:
            for v in CROSS_UNARY_VARIANTS:
                # THE OPERAND WIDTH IS `from` AND THE ROW IS THEREFORE fp.
                # `to` is always i64 here, so a row keyed on the TARGET's
                # domain would call every libm symbol an integer one.
                emit(base="%s_%s_to_%s" % (e["op"], f, e["to"]),
                     kind=e["kind"], family="libm", opcode=e["op"], pred="",
                     widths=[f, e["to"]], variant=v, bits=W[f]["bits"],
                     to_bits=W[e["to"]]["bits"], dom=dom, source="libm")
