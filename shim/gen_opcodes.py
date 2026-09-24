# shim/gen_opcodes.py — the FIRST expansion path: CQ_lang's
# `opcode_table.yaml` (2479 symbols), expanded into gen_shim's `Row` model.
#
# THE RESERVE SEAM gen_shim.py RECORDED ON 2026-09-19 (bead 9ve.24), TAKEN
# 2026-09-24 (bead 9ve.44) WHEN IT BIT. gen_shim.py stood at 292 counted lines
# of Rule 12's 300 and Wave 10's wiring owed it a LANDED block for eleven fp
# opcodes and ten integer ones plus a per-opcode `defer` reason table. The cut
# is the recorded one: THE MODEL AND THE RULES (Row, variant_axes, suffix,
# signature, landed_key, bucket_of, the reason tables, the audit, the renderer)
# STAY in gen_shim.py; the per-schema EXPANSION lives beside its siblings —
# `shim/gen_intrinsics.py` already held the other two schemas.
#
# THE IMPORT IS ONE-WAY AND TAKES NO gen_shim NAME. `expand_opcodes` receives
# gen_shim's Row builder as `emit`, exactly as `gen_intrinsics.expand_intrinsic`
# does, so this file cannot build a row by any rule but gen_shim's — which is
# the whole point of putting three grids behind one model rather than three
# generators. `gen_shim.expand` stays as the one-line entry point every test
# already calls.


def expand_opcodes(table, emit):
    """Expand opcode_table.yaml through `emit`, gen_shim's Row builder."""
    W = table["widths"]
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
