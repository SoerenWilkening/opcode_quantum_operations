#!/usr/bin/env python3
# test_gen_bodies.py — Step 22's gate, the BODIES half (plan §3's recorded
# seam: `the ABI grid <-> the emitted bodies`).
#
# WHAT THIS FILE EXISTS FOR: "2479 emitted" passes just as well with 992 INERT
# wrappers, and neither Step 23's `nm` nor Step 24's corpus could notice — an
# inert body links, returns, and is never called by a fixture that also calls
# something fp. So every claim here is about what is BETWEEN THE BRACES.
#
# Each check is a function of a definitions dict so that the last cases can hand
# it a deliberately broken one: an assertion nobody has seen fail is an assertion
# nobody has tested, and mutating an assertion cannot fail against a correct
# generator — the instrument is a PROVOCATION (tests/test_kerneldrv.c).
#
# SPLIT SEAM: TAKEN 2026-09-18, exactly where this header recorded it before it
# was needed (Rule 12: a split is scheduled, never improvised). The line was
# `the ASSERTIONS <-> the PROVOCATIONS`, at the "Negative controls" rule below,
# moving to tests/test_gen_bodies_provoked.py — and it fired when PRD-v2's first
# fp family (bead 9ve.28) took the file from 259 to 319 counted lines of a
# 300-line wall. It is the right subject line as well as the right size: the
# provocations are about the SUITE and everything above them is about the
# GENERATOR. Python has no `.inc` escape hatch (check_loc.sh counts every line of
# a multi-line string as code), so the split is a third registered ctest entry
# and not a second file on the same one.
#
# AND THAT NEXT SEAM WAS TAKEN 2026-09-19 (bead 9ve.36), also exactly where
# this header recorded it: `the STATIC reading <-> the EXECUTED witnesses` ->
# tests/test_gen_bodies_run.py, TRIGGER 260, measured at 292 once the fp
# ARITHMETIC surface landed. The cases that need a COMPILER are ~90 counted
# lines of embedded C; what is left here reads text and runs in milliseconds.
# The recorded list named THREE and FOUR moved — see the note beside `CASES`
# below for why `a_signature_that_diverges_from_the_abi_is_caught` went with
# them.
#
# THE NEXT SEAM AFTER THAT, recorded now for the same reason: `the WRAPPER
# claims <-> the ABORT claims` -> tests/test_gen_bodies_abort.py. The
# discriminator is which BUCKET a case reads, and the abort side is what grows
# as PRD-v2 §7.15's remaining families land and the `fp` bucket shrinks —
# `every_abort_body_names_its_own_symbol...`, `the_two_abort_reasons...` and
# the declined rows of `the_new_fp_shapes...`. Trigger 260.

import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "support"))
import shimcheck as sc  # noqa: E402

SHIPPED = sc.parse_definitions(sc.read_dir(sc.GENERATED))
BITS = {"i1": 1, "i8": 8, "i16": 16, "i32": 32, "i64": 64, "i80": 80,
        "i128": 128, "f64": 64}
# The two opcode sets whose NAMES decide which selector enum a live body must
# carry. Spelled out here rather than asked of gen_shim: that file reads the
# yaml's `domain` and its cast `kind`, so these are a genuinely independent
# route to the same answer and the two disagree exactly when one is wrong.
FP_BINOPS = {"fadd", "fsub", "fmul", "fdiv"}
XDOM = {"fptosi", "fptoui", "sitofp", "uitofp"}
CALL = re.compile(r"^    (?:return )?(cq_shim_[a-z_0-9]+)\((.*)\);$")
PARAM = re.compile(r"\b([a-z_]+_(?:handle|classical|flag))\b")
OPCODES = {"add", "sub", "mul", "sdiv", "udiv", "srem", "urem", "and", "or",
           "xor", "shl", "lshr", "ashr", "icmp", "sext", "zext", "trunc",
           "fcmp", "fadd", "fsub", "fmul", "fdiv",
           "fptosi", "fptoui", "sitofp", "uitofp",
           # PRD-v2 §6.1's vendoring (bead 9ve.24): the two opcodes from
           # intrinsic_table.yaml that LANDED.
           "fma", "sqrt"}

# THE TWO INTRINSIC SELECTORS, TRANSCRIBED INDEPENDENTLY. `fma`'s enumerator
# happens to be the opcode upper-cased and `sqrt`'s is NOT — `CQ_SHIM_FUN_FSQRT`
# carries the `f` the ABI's opcode token drops — so a rule that upper-cased the
# stem would emit `CQ_SHIM_FUN_SQRT`, an enumerator that does not exist. The
# mapping is spelled out for exactly that reason.
INTRINSIC_SEL = {"fma": "CQ_SHIM_FMA_FMA", "sqrt": "CQ_SHIM_FUN_FSQRT"}
# The enum prefix a dispatch SELECTOR may carry, and the opcode each implies.
# The four `F*` prefixes are pairwise disjoint from their integer siblings —
# checked in tests/test_gen_shim.py rather than assumed here — so the order of
# this table is not load-bearing and `len(hit) == 1` below is what enforces it.
SELECTORS = (("CQ_SHIM_OP_", None), ("CQ_SHIM_FOP_", None),
             ("CQ_SHIM_PRED_", "icmp"), ("CQ_SHIM_FPRED_", "fcmp"),
             ("CQ_SHIM_CAST_", None), ("CQ_SHIM_FCAST_", None),
             # The two intrinsic prefixes. `fma` takes the None rule (its
             # enumerator IS its opcode); `sqrt` cannot, so it is named.
             ("CQ_SHIM_FMA_", None), ("CQ_SHIM_FUN_", "sqrt"))


def wrappers(defs):
    return {n: b for n, (_d, b) in defs.items() if sc.bucket_of_body(b) == "wrapper"}


def aborts(defs):
    return {n: b for n, (_d, b) in defs.items() if sc.bucket_of_body(b) != "wrapper"}


def one_call(name, body):
    # A wrapper is ONE statement. That is also bd d6m's preferred fix asserted
    # structurally over the whole `_controlled` family rather than left as a
    # comment on generated output: the region a controlled body opens is
    # exactly one kernel call wide because the body IS one call.
    sc.check(len(body) == 1, "%s: wrapper body is %d lines, not 1: %r" % (name, len(body), body))
    m = CALL.match(body[0])
    sc.check(m is not None, "%s: body calls no libcqops entry point: %r" % (name, body[0]))
    return m.group(1), [a.strip() for a in m.group(2).split(",")]


def expected_args(name, decl):
    # THE WHOLE ARGUMENT LIST, DERIVED FROM THE SYMBOL AND ITS DECLARATION AND
    # FROM NOTHING ELSE. This replaces a width-only check that an adversarial
    # review broke four ways at once, every one of them silent: the dispatch
    # SELECTOR was emitted and never compared to the symbol it sits in (so
    # cq_template_sub_i32 could dispatch CQ_SHIM_OP_ADD); the two literal words
    # were checked for MEMBERSHIP, not order, and both are uint64_t so C accepts
    # the transposition; the `qq` operand order was unverified, where the compile
    # check has no teeth because both operands are int32_t; and ctrl_flag could
    # land in a data slot. All four keep the 2479-name set, all 2479 signatures,
    # the 992/603/884 partition, the one-call shape, the -Werror compile and
    # Step 23's `nm` exact. An ordered, complete comparison sees all four.
    stem = name[len("cq_template_"):]
    # THE CROSS-DOMAIN CASTS TAKE A DIFFERENT ENUM FROM THE WIDTH CASTS, and
    # this regex is what splits them: `([if]\d+)` admits both domains, and the
    # OPCODE decides the prefix. A single `CQ_SHIM_CAST_` arm would emit
    # `CQ_SHIM_CAST_SITOFP`, an enumerator that does not exist — caught by the
    # compile, but only after the generator had written it.
    m = re.match(r"([a-z]+)_([if]\d+)_to_([if]\d+)", stem)
    if m:
        pre = "CQ_SHIM_FCAST_" if m.group(1) in XDOM else "CQ_SHIM_CAST_"
        head = [pre + m.group(1).upper(),
                str(BITS[m.group(2)]), str(BITS[m.group(3)])]
    elif stem.startswith("icmp_"):
        m = re.match(r"icmp_([a-z]+)_(i\d+)", stem)
        head = ["CQ_SHIM_PRED_%s" % m.group(1).upper(), str(BITS[m.group(2)])]
    elif stem.startswith("fcmp_"):
        # A DIFFERENT ENUM AND A DIFFERENT ORDER, AND THIS BRANCH EXISTS TO SAY
        # SO. `ult`/`ugt`/`ule`/`uge` are mnemonics in BOTH predicate lists, so
        # an `fcmp` body emitting `CQ_SHIM_PRED_ULT` would compile, link, and
        # dispatch the integer comparator on an IEEE pattern. Folding this into
        # the `icmp` branch is how that ships.
        m = re.match(r"fcmp_([a-z]+)_(f\d+)", stem)
        head = ["CQ_SHIM_FPRED_%s" % m.group(1).upper(), str(BITS[m.group(2)])]
    else:
        # AND THE SAME SPLIT FOR THE BINARY OPCODES. An fp `fadd` and an integer
        # `add` reach different entry points over different enums; the opcode
        # token is the discriminator here, where gen_bodies uses the yaml's
        # `domain`, so the two routes are independent and disagree exactly when
        # one of them is wrong.
        m = re.match(r"([a-z_]+?)_([if]\d+)", stem)
        if m.group(1) in INTRINSIC_SEL:
            # THE TWO LANDED INTRINSICS, whose selector is a NAMED constant
            # rather than a derived one — see INTRINSIC_SEL.
            head = [INTRINSIC_SEL[m.group(1)], str(BITS[m.group(2)])]
        else:
            pre = "CQ_SHIM_FOP_" if m.group(1) in FP_BINOPS else "CQ_SHIM_OP_"
            head = [pre + m.group(1).upper(), str(BITS[m.group(2)])]
    # Declaration order IS ABI order: `out_handle`/`ctrl_flag` is argument 0
    # (opcode_table.yaml:43-55), so walking the declaration pins their position
    # in the call too.
    #
    # AND THE LITERAL'S MACRO PAIR IS CHOSEN BY THE DECLARED C TYPE, which is the
    # ONE place in this file where reading the type is right rather than the
    # recorded trap: gen_bodies picks by the yaml's `domain`, so a type-driven
    # expectation here is a genuinely independent route to the same answer, and
    # the two disagree exactly when one of them is wrong. `CQ_SHIM_LO` on a
    # `double` is a NUMERIC CONVERSION — `3.5` arrives as `3` — and it compiles
    # clean under -Wconversion because the cast inside it is explicit, so
    # nothing but this comparison can see it.
    m2 = re.search(r"(?:\(|, )([A-Za-z_][A-Za-z_0-9 ]*?) [a-z_]+_classical", decl)
    ctype = m2.group(1) if m2 else ""
    if ctype in ("float", "double", "long double", "_Float16"):
        sc.check(ctype == "double",
                 "%s carries a %s literal; only f64 has a bit-pattern macro "
                 "(PRD-v2 §1a), so this body should not be a wrapper at all"
                 % (name, ctype))
        lo, hi = "CQ_SHIM_F64_LO(%s)", "CQ_SHIM_F64_HI(%s)"
    else:
        lo, hi = "CQ_SHIM_LO(%s)", "CQ_SHIM_HI(%s)"
    tail = []
    for p in PARAM.findall(decl.split("(", 1)[1]):
        tail += [lo % p, hi % p] if p.endswith("_classical") else [p]
    return head + tail


# --- The cases ---------------------------------------------------------------

def no_wrapper_is_inert(defs=None):
    # 992 until PRD-v2's first fp family landed (2026-09-18, bead 9ve.28) and
    # 1048 until bead 9ve.36 added the fp ARITHMETIC surface: 30 `fp_arith`
    # bodies at f64 and 34 cross-domain conversion bodies, wrappers on exactly
    # the same terms as an integer one, ONE CALL EACH.
    # AND 1122 SINCE bead 9ve.24's VENDORING: `fma` x8 and `sqrt` x2 at f64.
    w = wrappers(defs or SHIPPED)
    sc.check(len(w) == 1122, "expected 1122 wrappers, got %d" % len(w))
    for name, body in sorted(w.items()):
        one_call(name, body)


def the_wrapper_calls_cover_all_twenty_six_dispatched_opcodes(defs=None):
    # READS THE CALL, NOT THE NAME. Until an adversarial review said so this
    # walked the symbol NAME set — which `the_emitted_symbol_set_equals_the_abi`
    # already pins — so the case could not fail and its own name was a false
    # claim about what it checked.
    #
    # AND AN UNRECOGNISED SELECTOR PREFIX CONTRIBUTES NOTHING, WHICH IS HOW THIS
    # CASE STAYED GREEN THROUGH THE fcmp LANDING WITHOUT SEEING IT. With
    # `CQ_SHIM_FPRED_` absent from the prefix list, 56 live wrappers dispatched
    # an opcode this set had never heard of and the diff still matched: the
    # missing arm makes the case narrower, never red. So the prefixes are now a
    # table and every selector must match ONE of them.
    seen = set()
    for name, body in wrappers(defs or SHIPPED).items():
        sel = one_call(name, body)[1][0]
        hit = [op if op else sel[len(p):].lower()
               for p, op in SELECTORS if sel.startswith(p)]
        sc.check(len(hit) == 1, "%s: selector %r matches %d known enum "
                 "prefixes, expected exactly 1" % (name, sel, len(hit)))
        seen.add(hit[0])
    sc.diff_sets(seen, OPCODES, "opcodes DISPATCHED by a wrapper body")


def the_wrapper_shape_split_is_449_forward_449_unc_224_controlled(defs=None):
    # 389/389/214, then 417/417/214 when `fcmp` landed (its 28 new wrappers are
    # forwards and `_unc`s and the CONTROLLED figure did not move, because
    # compares get no controlled grid on ANY axis). Bead 9ve.36 is the first
    # landing that DOES move it: `fp_arith` carries `controlled_qq`/`_hl`/`_lh`,
    # ten wrappers across the four opcodes, plus 17 forward and 17 `_unc`
    # conversion bodies. An `fcmp` appearing in the controlled column would
    # still be a phantom symbol, which is what makes the +10 a claim rather
    # than a count.
    got = {"fwd": 0, "unc": 0, "controlled": 0}
    for name, body in wrappers(defs or SHIPPED).items():
        entry = one_call(name, body)[0]
        got["unc" if entry.endswith("_unc") else
            "controlled" if entry.endswith("_ctrl") else "fwd"] += 1
    sc.check(got == {"fwd": 449, "unc": 449, "controlled": 224},
             "shape split: expected 449/449/224, got %r" % got)
    ctrl = {n for n, b in wrappers(defs or SHIPPED).items()
            if one_call(n, b)[0].endswith("_ctrl")}
    sc.check(not any("cmp_" in n for n in ctrl),
             "a COMPARE acquired a controlled wrapper: %s"
             % sorted(n for n in ctrl if "cmp_" in n)[:4])


def every_wrapper_body_is_exactly_the_call_its_symbol_names(defs=None):
    # The selector, the width in BITS, the position of ctrl_flag/out_handle, the
    # operand order and LO-before-HI, in one ordered comparison. The expected
    # side is read out of the ABI name and the ABI declaration, so it shares
    # nothing with the yaml's `widths` map that gen_shim reads.
    defs = defs or SHIPPED
    for name, body in sorted(wrappers(defs).items()):
        got = one_call(name, body)[1]
        want = expected_args(name, defs[name][0])
        sc.check(got == want, "%s dispatches the wrong call\n     got  %s\n     want %s"
                 % (name, ", ".join(got), ", ".join(want)))


def the_classical_literal_rides_the_widths_bits_not_sizeof_the_c_type(defs=None):
    # The two witnesses where the two readings DISAGREE, pinned by name because
    # every other width makes them coincide: i80's `_hl` literal is a 128-bit
    # __int128 carrying an 80-bit register, and i1's is an 8-bit bool carrying a
    # 1-bit one. A generator sized from sizeof(c_type) is green at every other
    # width in the grid.
    defs = defs or SHIPPED
    for name, ctype, bits, wrong in (("cq_template_add_i1_hl", "bool", 1, 8),
                                     ("cq_template_shl_i80_hl", "__int128", 80, 128)):
        decl, body = defs[name]
        sc.check("%s b_classical" % ctype in decl,
                 "%s: expected a %s literal, got %s" % (name, ctype, decl))
        args = one_call(name, body)[1]
        sc.check(args[1] == str(bits),
                 "%s: passes width %s, the register is %d bits (sizeof(%s) would say %d)"
                 % (name, args[1], bits, ctype, wrong))
        sc.check("CQ_SHIM_LO(b_classical)" in body[0] and "CQ_SHIM_HI(b_classical)" in body[0],
                 "%s: literal is not decomposed into two words: %s" % (name, body[0]))
    n = sum(1 for _n, b in wrappers(defs).items() if "CQ_SHIM_LO(" in b[0])
    sc.check(n == 516, "expected 516 wrappers carrying a classical literal, got %d" % n)
    # AND THE fp LITERALS ARE A DISJOINT SET OF 46, counted with a macro name
    # that is not a substring of the integer one — `CQ_SHIM_F64_LO(` does not
    # contain `CQ_SHIM_LO(`, which is what keeps the two counts independent. It
    # was 28 (the 14 `fcmp` predicates x {`_hl`, `_hl_unc`}) until bead 9ve.36
    # added `fp_arith`'s 18: `fadd` and `fmul` carry `_hl` x {fwd, unc, ctrl}
    # and the two non-commutative opcodes carry `_lh` as well. Any wrapper
    # carrying BOTH macro pairs, or an fp body carrying the integer pair, moves
    # one of these two numbers and not the other.
    fp = {n2 for n2, b in wrappers(defs).items() if "CQ_SHIM_F64_LO(" in b[0]}
    sc.check(len(fp) == 52,
             "expected 52 wrappers carrying an f64 bit-pattern literal, got %d"
             % len(fp))
    # SIX MORE SHAPES SINCE bead 9ve.24, and they are the reason this list is
    # not `_hl`/`_lh`: a TERNARY carries its literal in the `b` lane, the `c`
    # lane or both, so the shape tokens are `_qql`, `_qlq` and `_qll`. `_qll`
    # carries TWO — one pair per lane — and one shared pair would make
    # `fma(a, 2.0, 3.0)` compute `fma(a, 3.0, 3.0)`.
    sc.check(all(any(t in n2 for t in ("_f64_hl", "_f64_lh", "_f64_qql",
                                       "_f64_qlq", "_f64_qll"))
                 for n2 in fp),
             "an f64 bit-pattern literal appears outside a literal-carrying "
             "shape at f64: %s" % sorted(fp)[:4])
    # AND IT IS THE TWO FAMILIES IT SHOULD BE. A cross-domain CAST has no
    # classical operand at all, so one appearing here would mean the generator
    # had invented a literal for an arity-1 symbol.
    sc.check(sorted({n2.split("_")[2] for n2 in fp}) == ["fadd", "fcmp", "fdiv",
                                                         "fma", "fmul", "fsub"],
             "the f64 literal carriers are not fcmp + the four fp_arith "
             "opcodes + fma: %s" % sorted({n2.split("_")[2] for n2 in fp}))
    sc.check(not (fp & {n2 for n2, b in wrappers(defs).items()
                        if "CQ_SHIM_LO(" in b[0]}),
             "a wrapper carries both the integer and the fp literal macros")


def every_abort_body_names_its_own_symbol_and_silences_its_parameters(defs=None):
    defs = defs or SHIPPED
    for name, body in sorted(aborts(defs).items()):
        text = "\n".join(body)
        sc.check('cq_shim_unsupported("%s",' % name in text,
                 "%s: abort body does not name itself: %r" % (name, body))
        decl = defs[name][0]
        for p in re.findall(r"(?:int32_t|bool|int\d+_t|__int128|_Float16|float|double|long double) "
                            r"([a-z_]+)", decl.split("(", 1)[1]):
            sc.check("(void)%s;" % p in text, "%s: parameter %s is not silenced" % (name, p))


def the_two_abort_reasons_are_distinct_and_neither_lies_about_its_bucket(defs=None):
    # One distinct message per bucket, so a symbol swept into the wrong one says
    # which it landed in. "fp is v2" on an integer symbol would be a FALSE
    # statement, not merely a vague one.
    defs = defs or SHIPPED
    sc.check(sc.FP_REASON != sc.INV_REASON, "the two abort reasons are the same string")
    # THE DISCRIMINATOR IS `is_deferred_fp`, NOT `is_fp`, SINCE 2026-09-18 — and
    # the difference is a FALSEHOOD PRINTED AT RUNTIME either way round. A landed
    # family's `_inv` carries an fp width token and is refused for D14's reason,
    # not because fp is v2: `cq_template_fcmp_oeq_f64_inv` saying "fp is v2"
    # would tell a caller to wait for a version that will never define it, since
    # an `fcmp` is non-injective at every width and in every version.
    # AND A THIRD REASON SINCE 2026-09-19 (bead 9ve.36), WHICH IS AN OVERRIDE
    # AND NOT A THIRD BUCKET. `uitofp i64 -> f64` is DECLINED — upstream routes
    # UIToFP to `soft_sitofp` with no bias correction at that one width — and
    # `"fp is v2"` there would be a falsehood twice over: f64 is in v2's scope
    # and seventeen sibling conversion pairs already ship. All THREE of its
    # symbols take the bead's reason, including the `_inv`, because D14's
    # sentence attaches to a family-width that has LANDED and this one has not.
    # AND A FOURTH AND FIFTH SINCE 2026-09-19 (bead 9ve.24), WHICH ARE A FOURTH
    # BUCKET. PRD-v2 §6.1's vendoring brought in 389 + 12 symbols, and `"fp is
    # v2"` is FALSE of most of them: `cq_template_ctpop_i32` is an integer
    # opcode at an integer width that no fp release reaches, and
    # `cq_template_lrint_f64_to_i64` touches a width that already SHIPS. The
    # integer intrinsics cite bead 9ve.29 and the two libm opcodes cite §7.9.
    for name, body in aborts(defs).items():
        if sc.is_declined(name):
            want = sc.DECLINED_REASON
        elif sc.is_defer(name):
            want = (sc.DEFER_LIBM_REASON
                    if name.startswith(("cq_template_lrint",
                                        "cq_template_llrint"))
                    else sc.DEFER_INTRINSIC_REASON)
        else:
            want = sc.FP_REASON if sc.is_deferred_fp(name) else sc.INV_REASON
        sc.check('"%s"' % want in "\n".join(body),
                 "%s: abort reason is not %r" % (name, want))
    # THE FOUR REASONS ARE PAIRWISE DISTINCT, which is what makes
    # `bucket_of_body` a partition rather than a first-match.
    reasons = [sc.FP_REASON, sc.INV_REASON, sc.DECLINED_REASON,
               sc.DEFER_INTRINSIC_REASON, sc.DEFER_LIBM_REASON]
    sc.check(len(set(reasons)) == len(reasons),
             "two abort reasons are the same string")
    deferred = {n for n in aborts(defs) if sc.is_defer(n)}
    sc.check(len(deferred) == 249,
             "expected 249 deferred aborts, got %d" % len(deferred))
    declined = {n for n in aborts(defs) if sc.is_declined(n)}
    sc.check(len(declined) == 3,
             "expected 3 declined aborts, got %d" % len(declined))
    sc.check(sc.DECLINED_REASON not in (sc.FP_REASON, sc.INV_REASON),
             "the declined reason is one of the other two")
    landed_inv = {n for n in aborts(defs) if sc.is_landed(n)}
    sc.check(len(landed_inv) == 65,
             "expected 65 D14 aborts inside the landed fp families, got %d"
             % len(landed_inv))
    sc.check(all(n.endswith("_inv") for n in landed_inv),
             "a LANDED fp symbol aborts without being an `_inv`: %s"
             % sorted(n for n in landed_inv if not n.endswith("_inv"))[:4])
    # PRD §1 fixes the fp wording verbatim; this is that sentence, reassembled.
    sc.check(sc.FP_REASON == "fp is v2", "PRD §1's fp reason clause changed")
    # AND THE `_inv` REASON MUST BE TRUE OF THE WHOLE BUCKET, WHICH IS A LIVE
    # CONSTRAINT rather than a style note. D14 scopes "f-inverse does not exist"
    # to tier 1 — and/or/udiv/trunc/icmp — so 119 of the 603 have an inverse that
    # DOES exist and are refused on tiers 2 and 3 instead. A message claiming
    # non-existence is a falsehood printed at runtime for those, and the sharpest
    # of them is D14's own worked example. This pins the 119 by opcode so a
    # future re-narrowing of the string has a witness rather than a comment.
    have_inverse = {n for n in aborts(defs) if not sc.is_fp(n)
                    and n.split("cq_template_")[1].split("_")[0]
                    in ("add", "sub", "xor", "sext", "zext")}
    sc.check(len(have_inverse) == 119,
             "expected 119 integer _inv symbols whose f-inverse exists, got %d"
             % len(have_inverse))
    sc.check("does not exist" not in sc.INV_REASON,
             "the _inv abort claims f-inverse does not exist; that is false for "
             "the %d symbols above, e.g. cq_template_add_i32_hl_inv, whose "
             "inverse D14 itself names as `subtract`" % len(have_inverse))


def the_new_fp_shapes_are_pinned_body_by_body(defs=None):
    # SIX BODIES VERBATIM, AND THE CASE IS ABOUT NON-VACUITY RATHER THAN ABOUT
    # THESE SIX. `every_wrapper_body_is_exactly_the_call_its_symbol_names`
    # already sweeps all 1112 against `expected_args`; what it CANNOT say is
    # that each of `expected_args`' branches is ever reached. A branch no
    # shipped symbol exercises is dead code that agrees with everything, and
    # the fp arithmetic surface added three of them at once — the
    # `CQ_SHIM_FOP_` arm, the `CQ_SHIM_FCAST_` arm and the `CQ_SHIM_F64_LO/HI`
    # pair on a lane that is not `fcmp`'s.
    #
    # SO EACH ROW IS A DIFFERENT SHAPE and the list is the argument for the
    # case: the `_hl` and `_lh` literal doors (where the pattern must be
    # REINTERPRETED and not converted), the `_ctrl` door (where `ctrl_flag` is
    # argument 0 and must not land in a data slot), both directions of a
    # cross-domain conversion (whose two widths are both shipped pairs, so a
    # transposition is a legal call for a different operation), and the bead
    # 9ve.34 abort, which is the one row of the seventeen-pair family that is
    # NOT a wrapper.
    defs = defs or SHIPPED
    want = {
        "cq_template_fadd_f64_hl":
            "    return cq_shim_fbin_hl(CQ_SHIM_FOP_FADD, 64, a_handle, "
            "CQ_SHIM_F64_LO(b_classical), CQ_SHIM_F64_HI(b_classical));",
        "cq_template_fsub_f64_lh":
            "    return cq_shim_fbin_lh(CQ_SHIM_FOP_FSUB, 64, "
            "CQ_SHIM_F64_LO(a_classical), CQ_SHIM_F64_HI(a_classical), "
            "b_handle);",
        "cq_template_fdiv_f64_controlled":
            "    return cq_shim_fbin_qq_ctrl(CQ_SHIM_FOP_FDIV, 64, ctrl_flag, "
            "a_handle, b_handle);",
        "cq_template_fptosi_f64_to_i8":
            "    return cq_shim_fcast(CQ_SHIM_FCAST_FPTOSI, 64, 8, a_handle);",
        "cq_template_sitofp_i16_to_f64_unc":
            "    cq_shim_fcast_unc(CQ_SHIM_FCAST_SITOFP, 16, 64, out_handle, "
            "a_handle);",
        "cq_template_uitofp_i64_to_f64":
            '    (void)a_handle;\n'
            '    cq_shim_unsupported("cq_template_uitofp_i64_to_f64", "%s");'
            % sc.DECLINED_REASON,
    }
    for name in sorted(want):
        got = "\n".join(defs[name][1])
        sc.check(got == want[name],
                 "%s\n     got  %s\n     want %s" % (name, got, want[name]))
    # AND THE BRANCHES REALLY ARE REACHED BY MORE THAN THESE SIX — a pin over a
    # handful of names says nothing about a branch that fires for one symbol
    # and is wrong for the other 33.
    live = wrappers(defs)
    for what, n in (("CQ_SHIM_FOP_", 30), ("CQ_SHIM_FCAST_", 34)):
        hit = sum(1 for b in live.values() if what in b[0])
        sc.check(hit == n, "expected %d bodies carrying %s, got %d"
                 % (n, what, hit))


# --- The EXECUTED witnesses: GONE, on the seam this header recorded ----------
#
# `the_generated_bodies_compile_under_the_projects_own_flags`,
# `a_signature_that_diverges_from_the_abi_is_caught`,
# `an_abort_body_really_aborts_with_prd_1s_message` and
# `the_two_word_literal_round_trips_at_every_shipped_width` moved to
# tests/test_gen_bodies_run.py on 2026-09-19 (bead 9ve.36), at the
# `the STATIC reading <-> the EXECUTED witnesses` line above — TRIGGER 260,
# measured at 292 once the fp ARITHMETIC surface arrived.
#
# THE RECORDED SEAM NAMED THREE AND FOUR MOVED, which is worth stating rather
# than leaving as a discrepancy. `a_signature_that_diverges_from_the_abi_is_
# caught` COMPILES but does not RUN, so the header's "compile and run
# something" list left it out; it is nonetheless the PROVOCATION for the case
# immediately above it, and separating a provocation from the assertion it
# provokes is exactly what tests/test_gen_bodies_provoked.py's own header says
# not to do. It needs a compiler, so it goes with the compiler.
#
# WHAT IS LEFT HERE READS TEXT AND NOTHING ELSE, which is the property that
# makes the cut a subject cut: these cases run in milliseconds and need no
# toolchain, and the four that moved are ~90 counted lines of embedded C.
# `CASES` is exported so that the sibling — and
# tests/test_gen_bodies_provoked.py — cannot fall out of step with a case
# added here.
CASES = [
    no_wrapper_is_inert,
    the_wrapper_calls_cover_all_twenty_six_dispatched_opcodes,
    the_wrapper_shape_split_is_449_forward_449_unc_224_controlled,
    every_wrapper_body_is_exactly_the_call_its_symbol_names,
    the_classical_literal_rides_the_widths_bits_not_sizeof_the_c_type,
    every_abort_body_names_its_own_symbol_and_silences_its_parameters,
    the_two_abort_reasons_are_distinct_and_neither_lies_about_its_bucket,
    the_new_fp_shapes_are_pinned_body_by_body,
]


if __name__ == "__main__":
    sys.exit(sc.run(CASES))
