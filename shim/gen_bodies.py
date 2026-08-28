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
#   fp       — PRD §1. Floating point is v2, via Bennett's src/softfloat/.
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
REASON = {
    "fp": "fp is v2",
    "inv": "_inv is f-inverse, not f; the data family has no uniform definition (PRD 15 D14)",
}

# The M26 entry point per (kind, shape, axis). A cast and a compare have no
# controlled axis at all (opcode_table.yaml:45-46) and a cast has no classical
# operand, so a generator that applied either axis uniformly would mint phantom
# symbols — which is why this table is explicit and KeyError is the failure.
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
    ("compare", "qq", "fwd"): "cq_shim_icmp_qq",
    ("compare", "hl", "fwd"): "cq_shim_icmp_hl",
    ("compare", "qq", "unc"): "cq_shim_icmp_qq_unc",
    ("compare", "hl", "unc"): "cq_shim_icmp_hl_unc",
    ("cast", "un", "fwd"): "cq_shim_cast",
    ("cast", "un", "unc"): "cq_shim_cast_unc",
}

OPCODE_ENUM = "CQ_SHIM_OP_%s"
PRED_ENUM = "CQ_SHIM_PRED_%s"
CAST_ENUM = "CQ_SHIM_CAST_%s"


# THE CLASSICAL LITERAL IS DECOMPOSED FROM THE WIDTH'S BITS, NEVER FROM
# sizeof(c_type). i80's `_hl` literal is a 128-bit __int128 carrying an 80-bit
# register; i1's is an 8-bit bool carrying a 1-bit one. The C conversion to
# unsigned __int128 is modular, so a negative int8_t arrives sign-extended and
# M26 masks it to `bits` — which is the only place the width is known.
def literal(pname):
    return "CQ_SHIM_LO(%s), CQ_SHIM_HI(%s)" % (pname, pname)


def call_args(row):
    a = []
    if row.kind == "cast":
        a.append(CAST_ENUM % row.opcode.upper())
        a.append(str(row.bits))
        a.append(str(row.to_bits))
    elif row.kind == "compare":
        a.append(PRED_ENUM % row.pred.upper())
        a.append(str(row.bits))
    else:
        a.append(OPCODE_ENUM % row.opcode.upper())
        a.append(str(row.bits))
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
        a.append(literal(pname) if pname.endswith("_classical") else pname)
    return a


def wrapper(row):
    entry = ENTRY[(row.kind, row.shape, row.axis)]
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
    return voids + '    cq_shim_unsupported("%s", "%s");\n' % (row.name, REASON[row.bucket])


def body(row):
    return wrapper(row) if row.bucket == "wrapper" else abort(row)
