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
# THE NEXT SEAM, RECORDED NOW FOR THE SAME REASON: `the STATIC reading <-> the
# EXECUTED witnesses` -> tests/test_gen_bodies_run.py. The three cases that
# COMPILE AND RUN something (`the_generated_bodies_compile_...`,
# `an_abort_body_really_aborts_...`, `the_two_word_literal_round_trips_...`) are
# ~90 counted lines of embedded C and are the only ones needing a compiler; the
# rest read text. Trigger 260.

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
CALL = re.compile(r"^    (?:return )?(cq_shim_[a-z_0-9]+)\((.*)\);$")
PARAM = re.compile(r"\b([a-z_]+_(?:handle|classical|flag))\b")
OPCODES = {"add", "sub", "mul", "sdiv", "udiv", "srem", "urem", "and", "or",
           "xor", "shl", "lshr", "ashr", "icmp", "sext", "zext", "trunc",
           "fcmp"}
# The enum prefix a dispatch SELECTOR may carry, and the opcode each implies.
# `CQ_SHIM_FPRED_` is tested before `CQ_SHIM_PRED_` would ever match it — they
# are disjoint, checked in tests/test_gen_shim.py rather than assumed here.
SELECTORS = (("CQ_SHIM_OP_", None), ("CQ_SHIM_PRED_", "icmp"),
             ("CQ_SHIM_FPRED_", "fcmp"), ("CQ_SHIM_CAST_", None))


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
    m = re.match(r"([a-z]+)_(i\d+)_to_(i\d+)", stem)
    if m:
        head = ["CQ_SHIM_CAST_%s" % m.group(1).upper(),
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
        m = re.match(r"([a-z]+)_(i\d+)", stem)
        head = ["CQ_SHIM_OP_%s" % m.group(1).upper(), str(BITS[m.group(2)])]
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
    # 992 until PRD-v2's first fp family landed (2026-09-18, bead 9ve.28): the
    # 56 live `cq_template_fcmp_*_f64[_hl][_unc]` bodies are wrappers on exactly
    # the same terms, one call each.
    w = wrappers(defs or SHIPPED)
    sc.check(len(w) == 1048, "expected 1048 wrappers, got %d" % len(w))
    for name, body in sorted(w.items()):
        one_call(name, body)


def the_wrapper_calls_cover_all_eighteen_dispatched_opcodes(defs=None):
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


def the_wrapper_shape_split_is_417_forward_417_unc_214_controlled(defs=None):
    # 389/389/214 until the fcmp landing added 14 `fwd_qq` + 14 `fwd_hl` and
    # their two `_unc` twins. The CONTROLLED figure is unmoved and must stay so:
    # compares get no controlled grid on ANY axis (opcode_table.yaml's own
    # note), so an fcmp appearing here would be a phantom symbol.
    got = {"fwd": 0, "unc": 0, "controlled": 0}
    for name, body in wrappers(defs or SHIPPED).items():
        entry = one_call(name, body)[0]
        got["unc" if entry.endswith("_unc") else
            "controlled" if entry.endswith("_ctrl") else "fwd"] += 1
    sc.check(got == {"fwd": 417, "unc": 417, "controlled": 214},
             "shape split: expected 417/417/214, got %r" % got)


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
    # AND THE fp LITERALS ARE A DISJOINT SET OF 28, counted with a macro name
    # that is not a substring of the integer one — `CQ_SHIM_F64_LO(` does not
    # contain `CQ_SHIM_LO(`, which is what keeps the two counts independent. The
    # 28 are the 14 `fcmp` predicates x {`_hl`, `_hl_unc`}; any wrapper carrying
    # BOTH macro pairs, or an fp body carrying the integer pair, moves one of
    # these two numbers and not the other.
    fp = {n2 for n2, b in wrappers(defs).items() if "CQ_SHIM_F64_LO(" in b[0]}
    sc.check(len(fp) == 28,
             "expected 28 wrappers carrying an f64 bit-pattern literal, got %d"
             % len(fp))
    sc.check(all(n2.startswith("cq_template_fcmp_") and "_f64_hl" in n2 for n2 in fp),
             "an f64 bit-pattern literal appears outside fcmp's `_hl` shapes: %s"
             % sorted(fp)[:4])
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
    for name, body in aborts(defs).items():
        want = sc.FP_REASON if sc.is_deferred_fp(name) else sc.INV_REASON
        sc.check('"%s"' % want in "\n".join(body),
                 "%s: abort reason is not %r" % (name, want))
    landed_inv = {n for n in aborts(defs) if sc.is_landed(n)}
    sc.check(len(landed_inv) == 28,
             "expected 28 D14 aborts inside the landed fp family, got %d"
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


def the_generated_bodies_compile_under_the_projects_own_flags(defs=None):
    # C LINKS A WRONG SIGNATURE SILENTLY. Compiling every emitted body against
    # the ABI it claims to satisfy is the check `nm` cannot perform — and it is
    # what caught the one real defect in this module: at i32 the classical
    # operand's c_type IS int32_t, the same type a handle rides, so a
    # discriminator written on the TYPE passed 83 literals as handles. No
    # name-set or count assertion could have seen it.
    rc, out = sc.compile_gen(sc.GENERATED)
    sc.check(rc == 0, "the emitted shim does not compile against CQ_lang's own "
                      "declarations:\n%s" % out[:2000])


def a_signature_that_diverges_from_the_abi_is_caught(defs=None):
    # The provocation for the case above. C LINKS A WRONG SIGNATURE SILENTLY:
    # swap the `_lh` operand order and every name-set, count and bucket
    # assertion in both suites stays green, the symbol still links, and the
    # classical operand and the handle arrive in each other's slots.
    sys.path.insert(0, sc.SHIM)
    import gen_shim
    real = gen_shim.signature

    def swapped(kind, shape, axis, ctype):
        ret, ps = real(kind, shape, axis, ctype)
        if shape == "lh":
            i = len(ps) - 2
            ps = ps[:i] + [ps[i + 1], ps[i]]
        return ret, ps

    tmp = tempfile.mkdtemp(prefix="cqops-shim-bad-")
    try:
        gen_shim.signature = swapped
        table, sha = gen_shim.load(sc.YAML)
        for fn, text in gen_shim.files(gen_shim.expand(table), sha).items():
            open(os.path.join(tmp, fn), "w").write(text)
    finally:
        gen_shim.signature = real
    rc, out = sc.compile_gen(tmp)
    sc.check(rc != 0, "a swapped `_lh` operand order compiled cleanly against the ABI")
    sc.check("conflicting types" in out or "incompatible" in out,
             "the compile failed for the wrong reason:\n%s" % out[:600])


def an_abort_body_really_aborts_with_prd_1s_message(defs=None):
    # PRD §1 names the runtime behaviour, not a string in a generator:
    #   cqops: cq_template_sitofp_i32_to_f64 not implemented (fp is v2)
    # M26 does not exist yet, so the entry point is stubbed HERE — which is the
    # point: it witnesses the emitted body, not a future implementation.
    cc = os.environ.get("CQOPS_CC", "cc")
    tmp = tempfile.mkdtemp(prefix="cqops-shim-run-")
    main = os.path.join(tmp, "main.c")
    open(main, "w").write(
        '#include <stdio.h>\n#include <stdlib.h>\n#include <stdint.h>\n'
        'int32_t cq_template_sitofp_i32_to_f64(int32_t);\n'
        '_Noreturn void cq_shim_unsupported(const char *s, const char *r) {\n'
        '    fprintf(stderr, "cqops: %s not implemented (%s)\\n", s, r);\n'
        '    abort();\n}\n'
        'int main(void) { return (int)cq_template_sitofp_i32_to_f64(0); }\n')
    exe = os.path.join(tmp, "a.out")
    r = subprocess.run([cc, "-std=c11", "-I" + sc.SHIM, main,
                        os.path.join(sc.GENERATED, "cq_template_int_to_fp.gen.c"), "-o", exe],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    sc.check(r.returncode == 0, "witness program did not build:\n%s" % r.stdout.decode()[:1500])
    r = subprocess.run([exe], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    sc.check(r.returncode != 0, "an fp abort body returned normally")
    want = b"cqops: cq_template_sitofp_i32_to_f64 not implemented (fp is v2)"
    sc.check(want in r.stderr, "PRD §1's message not emitted; got %r" % r.stderr[:200])


def the_two_word_literal_round_trips_at_every_shipped_width(defs=None):
    # CQ_SHIM_LO/HI is the only executable LOGIC in the hand-written contract
    # header, and the generated bodies are 516 copies of it. Reading it is not
    # the same as running it: the C conversion to unsigned __int128 is modular,
    # so a negative signed literal must arrive SIGN-EXTENDED and M26's mask to
    # `bits` must then recover exactly the register value. i80 is the width that
    # can lose data (an 80-bit register on a 128-bit literal) and i1 is the width
    # where sizeof disagrees with the register; both are exercised above bit 63.
    cc = os.environ.get("CQOPS_CC", "cc")
    tmp = tempfile.mkdtemp(prefix="cqops-shim-lit-")
    src = os.path.join(tmp, "lit.c")
    open(src, "w").write(
        '#include <stdio.h>\n#include <stdint.h>\n#include <stdbool.h>\n#include <string.h>\n'
        '#include "cq_shim.h"\n'
        'static int fail = 0;\n'
        'static void chk(const char *w, int bits, uint64_t lo, uint64_t hi,\n'
        '                unsigned long long wh, unsigned long long wl) {\n'
        '    unsigned __int128 v = ((unsigned __int128)hi << 64) | lo;\n'
        '    if (bits < 128) v &= (((unsigned __int128)1 << bits) - 1);\n'
        '    unsigned long long l = (unsigned long long)v,'
        ' h = (unsigned long long)(v >> 64);\n'
        '    if (l != wl || h != wh) { fail = 1;\n'
        '        printf("%s: got %016llx%016llx want %016llx%016llx\\n", w, h, l, wh, wl); }\n}\n'
        'int main(void) {\n'
        '    bool t = true; int8_t m1 = -1, m128 = -128; int16_t s = -2;\n'
        '    int32_t i = -1; int64_t q = -1;\n'
        '    __int128 big = ((__int128)0x1234 << 64) | 0xdeadbeefcafef00dULL, neg = -(__int128)1;\n'
        '    chk("i1",    1, CQ_SHIM_LO(t),   CQ_SHIM_HI(t),   0, 0x1ULL);\n'
        '    chk("i8",    8, CQ_SHIM_LO(m1),  CQ_SHIM_HI(m1),  0, 0xffULL);\n'
        '    chk("i8b",   8, CQ_SHIM_LO(m128),CQ_SHIM_HI(m128),0, 0x80ULL);\n'
        '    chk("i16",  16, CQ_SHIM_LO(s),   CQ_SHIM_HI(s),   0, 0xfffeULL);\n'
        '    chk("i32",  32, CQ_SHIM_LO(i),   CQ_SHIM_HI(i),   0, 0xffffffffULL);\n'
        '    chk("i64",  64, CQ_SHIM_LO(q),   CQ_SHIM_HI(q),   0, 0xffffffffffffffffULL);\n'
        '    chk("i80",  80, CQ_SHIM_LO(big), CQ_SHIM_HI(big), 0x1234ULL, 0xdeadbeefcafef00dULL);\n'
        '    chk("i80n", 80, CQ_SHIM_LO(neg), CQ_SHIM_HI(neg), 0xffffULL, 0xffffffffffffffffULL);\n'
        '    chk("i128",128, CQ_SHIM_LO(neg), CQ_SHIM_HI(neg),\n'
        '        0xffffffffffffffffULL, 0xffffffffffffffffULL);\n'
        # THE fp PAIR IS RUN TOO, AND THE NEGATIVE CONTROL IS THE POINT OF IT.
        # CQ_SHIM_F64_LO must REINTERPRET; CQ_SHIM_LO on the same operand
        # CONVERTS, and 3.5 is chosen so the two answers are 0x400C…000 and 3 —
        # different in every bit that matters and both perfectly plausible. The
        # NaN and the -0.0 rows are built by memcpy FROM a pattern rather than
        # by `0.0/0.0` or `-0.0`, so the witness does no host fp arithmetic and
        # reads the same on every host (PRD-v2 §7.4).
        '    double d35 = 3.5, dnz, dnan;\n'
        '    uint64_t bnz = 0x8000000000000000ULL,'
        ' bnan = 0xfff8000000000000ULL;\n'
        '    memcpy(&dnz, &bnz, sizeof dnz);\n'
        '    memcpy(&dnan, &bnan, sizeof dnan);\n'
        '    chk("f64",   64, CQ_SHIM_F64_LO(d35),  CQ_SHIM_F64_HI(d35),\n'
        '        0, 0x400c000000000000ULL);\n'
        '    chk("f64nz", 64, CQ_SHIM_F64_LO(dnz),  CQ_SHIM_F64_HI(dnz),\n'
        '        0, 0x8000000000000000ULL);\n'
        '    chk("f64nan",64, CQ_SHIM_F64_LO(dnan), CQ_SHIM_F64_HI(dnan),\n'
        '        0, 0xfff8000000000000ULL);\n'
        '    if (CQ_SHIM_LO(d35) == CQ_SHIM_F64_LO(d35)) { fail = 1;\n'
        '        printf("the integer and fp literal macros agree on 3.5; one "\n'
        '               "of them is not doing its job\\n"); }\n'
        '    if (CQ_SHIM_LO(d35) != 3ULL) { fail = 1;\n'
        '        printf("CQ_SHIM_LO(3.5) is %llu, not the 3 a numeric "\n'
        '               "conversion gives\\n",'
        ' (unsigned long long)CQ_SHIM_LO(d35)); }\n'
        '    return fail;\n}\n')
    exe = os.path.join(tmp, "lit")
    r = subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wconversion",
                        "-I" + sc.SHIM, src, "-o", exe],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    sc.check(r.returncode == 0, "the literal witness did not build:\n%s" % r.stdout.decode()[:1500])
    r = subprocess.run([exe], stdout=subprocess.PIPE)
    sc.check(r.returncode == 0, "CQ_SHIM_LO/HI does not round-trip:\n%s" % r.stdout.decode())


# --- Negative controls: GONE, on the recorded seam ---------------------------
#
# `an_inert_wrapper_body_is_caught` and `every_way_a_wrapper_can_dispatch_wrong_
# is_caught` moved to tests/test_gen_bodies_provoked.py on 2026-09-18, at the
# `the ASSERTIONS <-> the PROVOCATIONS` line this file's header recorded before
# either was needed. They import the two cases above by name, so a case renamed
# here is an ImportError there rather than a silently unprovoked assertion.


if __name__ == "__main__":
    sys.exit(sc.run([
        no_wrapper_is_inert,
        the_wrapper_calls_cover_all_eighteen_dispatched_opcodes,
        the_wrapper_shape_split_is_417_forward_417_unc_214_controlled,
        every_wrapper_body_is_exactly_the_call_its_symbol_names,
        the_classical_literal_rides_the_widths_bits_not_sizeof_the_c_type,
        every_abort_body_names_its_own_symbol_and_silences_its_parameters,
        the_two_abort_reasons_are_distinct_and_neither_lies_about_its_bucket,
        the_generated_bodies_compile_under_the_projects_own_flags,
        a_signature_that_diverges_from_the_abi_is_caught,
        the_two_word_literal_round_trips_at_every_shipped_width,
        an_abort_body_really_aborts_with_prd_1s_message,
    ]))
