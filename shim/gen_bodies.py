#!/usr/bin/env python3
# gen_bodies.py — M27, the EMITTED BODIES half (plan §3's recorded seam:
# `the ABI grid <-> the emitted bodies`).
#
# THIS IS THE ONLY FILE IN M27 THAT NAMES A libcqops OR M26 SYMBOL. gen_shim.py
# knows the frozen ABI and nothing else; this file knows what a body DOES and
# nothing about how the grid is derived. The import is one-way and the Row
# record is the whole interface — which is what lets Step 22's round-trip
# oracle (2479 names AND 2479 signatures diffed against CQ_lang's own
# independently generated header) run from the grid half alone.
#
# THREE BUCKETS, THREE BODIES, and the two abort messages are DISTINCT so that a
# symbol swept into the wrong bucket says which one it landed in:
#
#   wrapper  — a thin call into M26's dispatch surface (shim/cq_shim.h). 992 of
#              them, and every one calls an entry point: "2479 emitted" passes
#              just as well with 992 INERT wrappers.
#   inv      — PRD §15 D14. `_inv` is f-inverse, and f-inverse DOES NOT EXIST
#              for and/or/udiv/trunc or any icmp. Do NOT define it as the
#              forward: for `dst ^= f` it numerically is, and that is the
#              sentence most likely to be quoted back as licence — it would
#              ship and_i32_inv = and_i32, a wrong VALUE.
#   fp       — PRD §1. Floating point is v2, via Bennett's src/softfloat/. A
#              family-width listed in gen_shim.LANDED is NOT in this bucket: its
#              non-`_inv` rows are ordinary wrappers and its `_inv` rows are D14
#              aborts, exactly as an integer family's are. The bucket is
#              gen_shim's to decide; this file only renders it.
#
# THE ABORT IS GROUNDED ON SPECIFIABILITY (D14), never on "it leaks a rail":
# the rail-leak wording would implicitly settle PRD §15 D15 §4's still-open
# abort-vs-strand clause (bd 06t) inside a generated body. Unrelated question.

# PRD §1 fixes the fp message verbatim:
#   cqops: cq_template_sitofp_i32_to_f64 not implemented (fp is v2)
# THE `_inv` REASON MUST BE TRUE OF ALL 603, AND THE FIRST ONE WAS NOT.
# It read "_inv is f-inverse, which does not exist" — and D14 scopes
# non-existence to TIER 1 only: `and`, `or`, `udiv`, `trunc` and every `icmp`.
# For `add`/`sub`/`xor` (84 symbols) and `sext`/`zext` (35) the inverse EXISTS,
# and D14 refuses them on different grounds — tier 2, f⁻¹ is not f, and tier 3,
# a guaranteed rail leak. So the string was a falsehood printed at runtime for
# **119 of the 603**, including `cq_template_add_i32_hl_inv`, which is the very
# symbol D14 uses as its worked example (`lowering_invert_add_i32.ll:25` pins
# `add %a, 2` as lowering to it, i.e. SUBTRACT 2 — an operation this same
# generated file ships a live wrapper for). What replaces it is D14's own
# summary sentence, which is true of the whole bucket: "No uniform definition of
# the data family is possible in ANY version."
#
# AND A ROW MAY CARRY ITS OWN REASON, WHICH IS AN OVERRIDE AND NOT A FOURTH
# BUCKET. `gen_shim.DECLINED` names the ABI rows that are REFUSED rather than
# unported — `uitofp i64 -> f64`, bead 9ve.34 — and sets `row.reason`. The
# bucket stays `fp` (it is an fp-touching row that has not landed), so every
# count and every file banner is unmoved; what changes is the sentence, because
# "fp is v2" would be false for a width v2 already ships.
REASON = {
    "fp": "fp is v2",
    "inv": "_inv is f-inverse, not f; the data family has no uniform definition (PRD 15 D14)",
}

# The M26 entry point per (key, shape, axis). A cast and a compare have no
# controlled axis at all (opcode_table.yaml:45-46) and a cast has no classical
# operand, so a generator that applied either axis uniformly would mint phantom
# symbols — which is why this table is explicit and KeyError is the failure.
#
# THE KEY IS THE OPCODE FOR A COMPARE AND THE KIND FOR EVERYTHING ELSE, and that
# is forced rather than tidy: `icmp` and `fcmp` share `kind == "compare"` and
# reach DIFFERENT M26 entry points over DIFFERENT predicate enums, whose names
# collide in four rows (`ult`/`ugt`/`ule`/`uge` are LLVM mnemonics in both
# lists). A single "compare" key would dispatch every fcmp through the integer
# comparator — right shape, right width, wrong operation, and `nm` cannot see it.
#
# AND THE DOMAIN SPLITS `binary` AND `cast` FOR THE SAME REASON ONE STEP OUT
# (2026-09-19, bead 9ve.36). An fp `fadd` and an integer `add` share
# `kind == "binary"` and reach DIFFERENT entry points over DIFFERENT selector
# enums; a cross-domain `sitofp` and an integer `zext` share `kind == "cast"`
# and reach different ones again. The discriminator for a BINARY row is the
# yaml's own `domain` for the operand width; for a CAST it is the yaml's own
# cast `kind`, because a cross-domain cast's operand domain is `int` on one
# direction and `fp` on the other and neither answer is about the family.
def entry_family(row):
    if row.kind == "compare":
        return row.opcode                       # icmp | fcmp
    if row.kind == "cast":
        return "fcast" if row.family in ("int_to_fp", "fp_to_int") else "cast"
    if row.kind == "cross_unary":
        return "libm"                           # never LANDED; no entry point
    if row.kind in ("rotate", "funnel"):
        return "shift_intrinsic"                # bead 9ve.29; no entry point
    return ("f" if row.domain == "fp" else "") + row.kind   # binary | fbinary
                                                            # unary  | funary
                                                            # ternary| fternary


def entry_key(row):
    return (entry_family(row), row.shape, row.axis)


ENTRY = {
    ("binary", "qq", "fwd"): "cq_shim_bin_qq",
    ("binary", "hl", "fwd"): "cq_shim_bin_hl",
    ("binary", "lh", "fwd"): "cq_shim_bin_lh",
    ("binary", "qq", "unc"): "cq_shim_bin_qq_unc",
    ("binary", "hl", "unc"): "cq_shim_bin_hl_unc",
    ("binary", "lh", "unc"): "cq_shim_bin_lh_unc",
    ("binary", "qq", "controlled"): "cq_shim_bin_qq_ctrl",
    ("binary", "hl", "controlled"): "cq_shim_bin_hl_ctrl",
    ("binary", "lh", "controlled"): "cq_shim_bin_lh_ctrl",
    ("icmp", "qq", "fwd"): "cq_shim_icmp_qq",
    ("icmp", "hl", "fwd"): "cq_shim_icmp_hl",
    ("icmp", "qq", "unc"): "cq_shim_icmp_qq_unc",
    ("icmp", "hl", "unc"): "cq_shim_icmp_hl_unc",
    ("fcmp", "qq", "fwd"): "cq_shim_fcmp_qq",
    ("fcmp", "hl", "fwd"): "cq_shim_fcmp_hl",
    ("fcmp", "qq", "unc"): "cq_shim_fcmp_qq_unc",
    ("fcmp", "hl", "unc"): "cq_shim_fcmp_hl_unc",
    ("cast", "un", "fwd"): "cq_shim_cast",
    ("cast", "un", "unc"): "cq_shim_cast_unc",
    ("fbinary", "qq", "fwd"): "cq_shim_fbin_qq",
    ("fbinary", "hl", "fwd"): "cq_shim_fbin_hl",
    ("fbinary", "lh", "fwd"): "cq_shim_fbin_lh",
    ("fbinary", "qq", "unc"): "cq_shim_fbin_qq_unc",
    ("fbinary", "hl", "unc"): "cq_shim_fbin_hl_unc",
    ("fbinary", "lh", "unc"): "cq_shim_fbin_lh_unc",
    ("fbinary", "qq", "controlled"): "cq_shim_fbin_qq_ctrl",
    ("fbinary", "hl", "controlled"): "cq_shim_fbin_hl_ctrl",
    ("fbinary", "lh", "controlled"): "cq_shim_fbin_lh_ctrl",
    ("fcast", "un", "fwd"): "cq_shim_fcast",
    ("fcast", "un", "unc"): "cq_shim_fcast_unc",
    # --- intrinsic_table.yaml, vendored 2026-09-19 (PRD-v2 §6.1, 9ve.24).
    #
    # `funary` EXISTS NOW AND `fneg` STILL DOES NOT REACH IT. The door was
    # built at Wave 8 for M40's `fsqrt` and was unreachable from the grid only
    # because `sqrt` lives in a yaml M27 did not read; the vendoring is what
    # connects them. `fneg` is `fp_arith`'s unary opcode in the OTHER yaml, its
    # kernel is bead 9ve.27's, and `LANDED` does not carry it — so it takes the
    # bucket rule and aborts, and these two rows are never reached for it.
    ("funary", "un", "fwd"): "cq_shim_fun",
    ("funary", "un", "unc"): "cq_shim_fun_unc",
    # `fternary` IS `fma` AND ONLY `fma` — the yaml's one ternary opcode. Four
    # shapes because two of the three operands may be literals; `qqq` is the
    # bare base symbol exactly as `qq` is.
    ("fternary", "qqq", "fwd"): "cq_shim_fma_qqq",
    ("fternary", "qql", "fwd"): "cq_shim_fma_qql",
    ("fternary", "qlq", "fwd"): "cq_shim_fma_qlq",
    ("fternary", "qll", "fwd"): "cq_shim_fma_qll",
    ("fternary", "qqq", "unc"): "cq_shim_fma_qqq_unc",
    ("fternary", "qql", "unc"): "cq_shim_fma_qql_unc",
    ("fternary", "qlq", "unc"): "cq_shim_fma_qlq_unc",
    ("fternary", "qll", "unc"): "cq_shim_fma_qll_unc",
    # THERE IS NO `("unary", ...)`, `("shift_intrinsic", ...)` OR `("libm", ...)`
    # ROW, AND EACH ABSENCE IS A DOCUMENTED FAILURE. The integer intrinsics
    # (`ctpop`, `ctlz`, `cttz`, `bswap`, `bitreverse`, `abs`, the four min/max
    # and the four saturating adds, plus `fshl`/`fshr` in both their arities)
    # are bead 9ve.29's and `lrint`/`llrint` are PRD-v2 §7.9's; none is in
    # `LANDED`, so none reaches `wrapper` and none reaches this table. Landing
    # any of them means a row HERE as well as a `LANDED` line, and until then
    # the KeyError is what says so.
}

# THE SELECTOR PER OPCODE, AS A TABLE WHOSE KeyError IS THE FAILURE — the same
# posture `ENTRY` has, and it is what stops a `LANDED` key widened back to
# `(family, width)` from emitting `CQ_SHIM_FOP_FREM` or `CQ_SHIM_FCAST_FPEXT`.
# Those are enumerators that do not exist, so the defect would surface as a C
# compile error in a GENERATED file — three steps too late, and in a file whose
# header says never to edit it — rather than as a generator refusal naming the
# symbol.
SELECTOR = {
    "binary": {op: "CQ_SHIM_OP_%s" % op.upper() for op in (
        "add", "sub", "mul", "sdiv", "udiv", "srem", "urem",
        "and", "or", "xor", "shl", "lshr", "ashr")},
    "fbinary": {op: "CQ_SHIM_FOP_%s" % op.upper() for op in (
        "fadd", "fsub", "fmul", "fdiv")},
    "cast": {k: "CQ_SHIM_CAST_%s" % k.upper() for k in ("sext", "zext", "trunc")},
    "fcast": {k: "CQ_SHIM_FCAST_%s" % k.upper() for k in (
        "fptosi", "fptoui", "sitofp", "uitofp")},
    # --- intrinsic_table.yaml (PRD-v2 §6.1, bead 9ve.24). ONE ENTRY EACH, and
    # the one-entry-ness is the point: `funary` is the whole of M40's surface
    # from that table and `fternary` is the whole of M39's, so a second opcode
    # arriving in either family is a KeyError here rather than an enumerator
    # that does not exist in a generated file.
    "funary": {"sqrt": "CQ_SHIM_FUN_FSQRT"},
    "fternary": {"fma": "CQ_SHIM_FMA_FMA"},
}

# Two predicate enums, and the prefixes are DELIBERATELY not a shared one: the
# two lists overlap in `ult`/`ugt`/`ule`/`uge`, so `CQ_SHIM_PRED_ULT` for both
# would be one enumerator meaning two predicates in two different orders. The
# same holds one family over for `CQ_SHIM_OP_` versus `CQ_SHIM_FOP_` and for
# `CQ_SHIM_CAST_` versus `CQ_SHIM_FCAST_`.
PRED_ENUM = {"icmp": "CQ_SHIM_PRED_%s", "fcmp": "CQ_SHIM_FPRED_%s"}


# THE CLASSICAL LITERAL IS DECOMPOSED FROM THE WIDTH'S BITS, NEVER FROM
# sizeof(c_type). i80's `_hl` literal is a 128-bit __int128 carrying an 80-bit
# register; i1's is an 8-bit bool carrying a 1-bit one. The C conversion to
# unsigned __int128 is modular, so a negative int8_t arrives sign-extended and
# M26 masks it to `bits` — which is the only place the width is known.
#
# AND AN fp LITERAL TAKES A DIFFERENT MACRO PAIR, WHICH IS THE SHARPEST TRAP IN
# THIS FILE. `CQ_SHIM_LO(x)` is `(uint64_t)(unsigned __int128)(x)` — a NUMERIC
# CONVERSION. On a `double` that compiles clean, is silent under -Wconversion
# because the cast is explicit, and turns `3.5` into `3`: the register would
# hold the integer 3 rather than the IEEE pattern 0x400C000000000000. The fp
# pair memcpys instead (PRD-v2 §7.4 — the pattern, never the value), and the
# discriminator is the yaml's OWN `domain` for the operand width, not the C type
# and not the spelling of the width token.
def literal(pname, row):
    if row.domain != "fp":
        return "CQ_SHIM_LO(%s), CQ_SHIM_HI(%s)" % (pname, pname)
    if row.bits != 64:
        raise SystemExit(
            "gen_shim: %s has a LANDED fp classical operand at %d bits, and "
            "only f64 has a bit-pattern macro (PRD-v2 §1 is f64-only). Landing "
            "%s means adding CQ_SHIM_F%d_LO/HI to shim/cq_shim.h first; do NOT "
            "let it fall back to CQ_SHIM_LO, which is a numeric conversion and "
            "would compile clean."
            % (row.name, row.bits, row.widths[0], row.bits))
    return "CQ_SHIM_F64_LO(%s), CQ_SHIM_F64_HI(%s)" % (pname, pname)


def call_args(row):
    a = []
    fam = entry_family(row)
    if fam in ("icmp", "fcmp"):
        a.append(PRED_ENUM[fam] % row.pred.upper())
        a.append(str(row.bits))
    else:
        a.append(SELECTOR[fam][row.opcode])
        a.append(str(row.bits))
        if row.kind == "cast":
            a.append(str(row.to_bits))
    if row.axis == "unc":
        a.append("out_handle")
    elif row.axis == "controlled":
        a.append("ctrl_flag")
    for _ctype, pname in row.params:
        if pname in ("out_handle", "ctrl_flag"):
            continue
        # THE DISCRIMINATOR IS THE OPERAND'S ROLE, NEVER ITS C TYPE. At i32 the
        # classical operand's c_type IS int32_t — the same type a handle rides —
        # so `ctype == "int32_t"` silently passes the i32 `_hl`/`_lh` literals as
        # handles. MEASURED: 83 bodies (21 int_arith hl + 15 lh, 18 int_bitwise hl
        # + 9 lh, 20 icmp hl). This comment said "190" until an adversarial pass
        # measured it — an unmeasured number in shipped prose, which is the
        # src/angle.h failure mode this repo tracks by name, reproduced inside the
        # comment written to record a fix. Caught by the compile check on the first
        # run (5 args offered, 4 given); INVISIBLE to any name-set or count
        # assertion.
        a.append(literal(pname, row) if pname.endswith("_classical") else pname)
    return a


def wrapper(row):
    entry = ENTRY[entry_key(row)]
    call = "%s(%s)" % (entry, ", ".join(call_args(row)))
    # A `_controlled` body is EXACTLY ONE call, over all 214 of them. That is a
    # NECESSARY condition for bd d6m's preferred fix (a) — the ABI offers M26 no
    # way to bracket more than one kernel invocation per controlled symbol — and
    # nothing more: what M26 does between cq_ctrl_push and cq_ctrl_pop is
    # unconstrained by anything emitted here, and d6m is OPEN. An earlier draft
    # of this comment said the shape "asserted" the fix. It does not.
    return "    %s%s;\n" % ("" if row.ret == "void" else "return ", call)


def abort(row):
    # Every parameter is silenced explicitly: -Wextra makes an unused parameter
    # an error under -Werror, and C11 forbids omitting a parameter's name in a
    # definition, so there is no quieter spelling.
    voids = "".join("    (void)%s;\n" % p[1] for p in row.params)
    # cq_shim_unsupported is _Noreturn, so a value-returning body needs no
    # return statement and cannot fall off the end.
    why = row.reason if row.reason else REASON[row.bucket]
    return voids + '    cq_shim_unsupported("%s", "%s");\n' % (row.name, why)


def body(row):
    return wrapper(row) if row.bucket == "wrapper" else abort(row)
