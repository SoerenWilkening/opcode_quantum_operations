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
# SPLIT SEAM, RECORDED BEFORE IT IS NEEDED (Rule 12: a split is scheduled, never
# improvised): `the ASSERTIONS <-> the PROVOCATIONS`, at the "Negative controls"
# rule below, moving to tests/test_gen_bodies_provoked.py. This file is at 259 of
# 300 and the provocation block is ~95 of that, so the seam is where the growth
# is — and it is the right subject line anyway, since the provocations are about
# the SUITE while everything above them is about the GENERATOR. Python has no
# `.inc` escape hatch (check_loc.sh counts every line of a multi-line string as
# code), so a split here means a third registered ctest test, not a second file
# on the same one.

import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "support"))
import shimcheck as sc  # noqa: E402

SHIPPED = sc.parse_definitions(sc.read_dir(sc.GENERATED))
BITS = {"i1": 1, "i8": 8, "i16": 16, "i32": 32, "i64": 64, "i80": 80, "i128": 128}
CALL = re.compile(r"^    (?:return )?(cq_shim_[a-z_]+)\((.*)\);$")
PARAM = re.compile(r"\b([a-z_]+_(?:handle|classical|flag))\b")
OPCODES = {"add", "sub", "mul", "sdiv", "udiv", "srem", "urem", "and", "or",
           "xor", "shl", "lshr", "ashr", "icmp", "sext", "zext", "trunc"}


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


def widths_in(name):
    m = re.search(r"_(i\d+)_to_(i\d+)", name)
    if m:
        return [BITS[m.group(1)], BITS[m.group(2)]]
    return [BITS[t] for t in re.findall(r"_(i\d+)(?=_|$)", name)]


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
    else:
        m = re.match(r"([a-z]+)_(i\d+)", stem)
        head = ["CQ_SHIM_OP_%s" % m.group(1).upper(), str(BITS[m.group(2)])]
    # Declaration order IS ABI order: `out_handle`/`ctrl_flag` is argument 0
    # (opcode_table.yaml:43-55), so walking the declaration pins their position
    # in the call too.
    tail = []
    for p in PARAM.findall(decl.split("(", 1)[1]):
        tail += ["CQ_SHIM_LO(%s)" % p, "CQ_SHIM_HI(%s)" % p] \
            if p.endswith("_classical") else [p]
    return head + tail


# --- The cases ---------------------------------------------------------------

def no_wrapper_is_inert(defs=None):
    w = wrappers(defs or SHIPPED)
    sc.check(len(w) == 992, "expected 992 wrappers, got %d" % len(w))
    for name, body in sorted(w.items()):
        one_call(name, body)


def the_wrapper_calls_cover_all_seventeen_integer_opcodes(defs=None):
    # READS THE CALL, NOT THE NAME. Until an adversarial review said so this
    # walked the symbol NAME set — which `the_emitted_symbol_set_equals_the_abi`
    # already pins — so the case could not fail and its own name was a false
    # claim about what it checked.
    seen = set()
    for name, body in wrappers(defs or SHIPPED).items():
        sel = one_call(name, body)[1][0]
        for prefix in ("CQ_SHIM_OP_", "CQ_SHIM_PRED_", "CQ_SHIM_CAST_"):
            if sel.startswith(prefix):
                seen.add("icmp" if prefix == "CQ_SHIM_PRED_" else sel[len(prefix):].lower())
    sc.diff_sets(seen, OPCODES, "opcodes DISPATCHED by a wrapper body")


def the_wrapper_shape_split_is_389_forward_389_unc_214_controlled(defs=None):
    got = {"fwd": 0, "unc": 0, "controlled": 0}
    for name, body in wrappers(defs or SHIPPED).items():
        entry = one_call(name, body)[0]
        got["unc" if entry.endswith("_unc") else
            "controlled" if entry.endswith("_ctrl") else "fwd"] += 1
    sc.check(got == {"fwd": 389, "unc": 389, "controlled": 214},
             "shape split: expected 389/389/214, got %r" % got)


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
    for name, body in aborts(defs).items():
        want = sc.FP_REASON if sc.is_fp(name) else sc.INV_REASON
        sc.check('"%s"' % want in "\n".join(body),
                 "%s: abort reason is not %r" % (name, want))
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
        '#include <stdio.h>\n#include <stdint.h>\n#include <stdbool.h>\n'
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
        '    return fail;\n}\n')
    exe = os.path.join(tmp, "lit")
    r = subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wconversion",
                        "-I" + sc.SHIM, src, "-o", exe],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    sc.check(r.returncode == 0, "the literal witness did not build:\n%s" % r.stdout.decode()[:1500])
    r = subprocess.run([exe], stdout=subprocess.PIPE)
    sc.check(r.returncode == 0, "CQ_SHIM_LO/HI does not round-trip:\n%s" % r.stdout.decode())


# --- Negative controls -------------------------------------------------------

def _broken(name, body):
    d = dict(SHIPPED)
    d[name] = (SHIPPED[name][0], body)
    return d


def an_inert_wrapper_body_is_caught(defs=None):
    victim = "cq_template_add_i32"
    for body in ([], ["    return 0;"], ["    (void)a_handle;", "    return b_handle;"]):
        try:
            no_wrapper_is_inert(_broken(victim, body))
        except sc.Fail as e:
            sc.check(victim in str(e), "the failure did not name %s: %s" % (victim, e))
            continue
        raise sc.Fail("an inert body %r passed no_wrapper_is_inert" % (body,))


def every_way_a_wrapper_can_dispatch_wrong_is_caught(defs=None):
    # FIVE PROVOCATIONS, ONE PER THING THE ARGUMENT LIST PINS. Each was found by
    # an adversarial pass to survive the whole suite before this case existed, and
    # each survives EVERY other detector Step 22 and Step 23 have: the 2479-name
    # set, all 2479 signatures, the bucket partition, the one-call shape, the
    # -Werror compile against CQ_lang's own declarations, and `nm`.
    #
    #   (1) the width sized from sizeof(c_type) — the mutant every width but i1
    #       and i80 hides, since sizeof and the register width agree elsewhere.
    #   (2) a collapsed SELECTOR — every wrapper dispatching one opcode. The
    #       largest miscompile M27 can carry: right symbol, right shape, right
    #       width, wrong operation, and only M26 would ever know.
    #   (3) the two literal words transposed. Both are uint64_t, so C is silent;
    #       at every width <= 64 the HI word is 0, so `x + 5` becomes `x + 0`.
    #   (4) `qq` operands swapped — `sub_i32(a,b)` dispatching `b - a`. The
    #       compile check cannot see this one: both operands are int32_t. (It DOES
    #       see the `_lh` swap, which is why that provocation is a separate case.)
    #   (5) ctrl_flag in a data slot — PRD §9's control wire arriving as an
    #       operand, which is bd d6m's failure mode with no diagnostic anywhere.
    def swap(s, a, b):
        return s.replace(a, "\0", 1).replace(b, a, 1).replace("\0", b, 1)
    cases = [
        ("cq_template_add_i1_hl", lambda s: s.replace(", 1,", ", 8,", 1), "width from sizeof"),
        ("cq_template_shl_i80_hl", lambda s: s.replace(", 80,", ", 128,", 1), "width from sizeof"),
        ("cq_template_sub_i32", lambda s: s.replace("CQ_SHIM_OP_SUB", "CQ_SHIM_OP_ADD", 1),
         "collapsed selector"),
        ("cq_template_icmp_ult_i32", lambda s: s.replace("CQ_SHIM_PRED_ULT", "CQ_SHIM_PRED_EQ", 1),
         "collapsed predicate"),
        ("cq_template_sext_i8_to_i32", lambda s: s.replace("CQ_SHIM_CAST_SEXT", "CQ_SHIM_CAST_ZEXT", 1),
         "collapsed cast kind"),
        ("cq_template_add_i8_hl", lambda s: swap(s, "CQ_SHIM_LO(b_classical)", "CQ_SHIM_HI(b_classical)"),
         "literal words transposed"),
        ("cq_template_sub_i32", lambda s: swap(s, "a_handle", "b_handle"), "qq operands swapped"),
        ("cq_template_sub_i32_controlled", lambda s: swap(s, "ctrl_flag", "a_handle"),
         "ctrl_flag in a data slot"),
        ("cq_template_sub_i32_unc", lambda s: swap(s, "out_handle", "a_handle"),
         "out_handle in a source slot"),
    ]
    for victim, mutate, what in cases:
        body = [mutate(SHIPPED[victim][1][0])]
        sc.check(body != SHIPPED[victim][1], "%s: the %r provocation did not change the body"
                 % (victim, what))
        try:
            every_wrapper_body_is_exactly_the_call_its_symbol_names(_broken(victim, body))
        except sc.Fail as e:
            sc.check(victim in str(e), "%s (%s): the failure did not name it: %s"
                     % (victim, what, e))
            continue
        raise sc.Fail("%s with %r survived the argument-list pin: %s" % (victim, what, body[0]))


if __name__ == "__main__":
    sys.exit(sc.run([
        no_wrapper_is_inert,
        the_wrapper_calls_cover_all_seventeen_integer_opcodes,
        the_wrapper_shape_split_is_389_forward_389_unc_214_controlled,
        every_wrapper_body_is_exactly_the_call_its_symbol_names,
        the_classical_literal_rides_the_widths_bits_not_sizeof_the_c_type,
        every_abort_body_names_its_own_symbol_and_silences_its_parameters,
        the_two_abort_reasons_are_distinct_and_neither_lies_about_its_bucket,
        the_generated_bodies_compile_under_the_projects_own_flags,
        a_signature_that_diverges_from_the_abi_is_caught,
        the_two_word_literal_round_trips_at_every_shipped_width,
        an_abort_body_really_aborts_with_prd_1s_message,
        an_inert_wrapper_body_is_caught,
        every_way_a_wrapper_can_dispatch_wrong_is_caught,
    ]))
