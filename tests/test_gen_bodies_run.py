#!/usr/bin/env python3
# test_gen_bodies_run.py — the EXECUTED WITNESSES half of Step 22's bodies
# gate, on the seam tests/test_gen_bodies.py recorded in its own header BEFORE
# this file was needed:
#
#     the STATIC reading <-> the EXECUTED witnesses
#
# TAKEN 2026-09-19 (bead 9ve.36) at the recorded TRIGGER of 260, measured at
# 292 of Rule 12's 300 once the fp ARITHMETIC surface landed. Python has no
# `.inc` escape hatch (check_loc.sh counts every line of a multi-line string as
# code), so the seam is a second FILE and a fifth registered ctest entry.
#
# THE DISCRIMINATOR IS A COMPILER. Everything in the sibling READS TEXT: it
# parses the emitted bodies and compares them against the ABI's own names and
# declarations, in milliseconds and with no toolchain. Every case here WRITES A
# C PROGRAM, compiles it and — for three of the four — runs it, which is the
# only way to make a claim about what a body DOES rather than what it says:
#   * the whole grid compiles against CQ_lang's own 2479 declarations, which is
#     the check `nm` structurally cannot perform (C links a wrong signature
#     silently, and it is what caught this module's one real defect);
#   * a swapped `_lh` operand order is REFUSED by that compile, which is the
#     provocation for it and is here because it needs the same compiler;
#   * an abort body really aborts with PRD §1's message;
#   * `CQ_SHIM_LO/HI` and `CQ_SHIM_F64_LO/HI` round-trip at every shipped
#     width, with the negative control that the two macro pairs must DISAGREE
#     on 3.5 — reading a macro is not the same as running it.
#
# THE IMPORT IS ONE-WAY and brings only `sc`: these cases build their own
# inputs from `shim/generated/` directly, so the sibling's parsed dictionary is
# not needed here and the two entries share no state.

import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "support"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shimcheck as sc  # noqa: E402
import test_gen_bodies as gb  # noqa: E402

SHIPPED = gb.SHIPPED


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
        # ALL THREE GRIDS since PRD-v2 §6.1's vendoring (bead 9ve.24), and
        # `files()` takes a sha PER SOURCE now because a banner can only name
        # one provenance. The swap still lands only on `_lh` rows, which are
        # `opcode_table.yaml`'s alone — the intrinsic table has no `lh`
        # variant — so what the extra families add here is the compile of 401
        # more bodies against the same ABI header, not a second provocation.
        gen_shim.signature = swapped
        table, sha = gen_shim.load(sc.YAML)
        itab, isha = gen_shim.load(sc.SOURCES[1][0])
        ltab, lsha = gen_shim.load(sc.SOURCES[2][0])
        rows = gen_shim.expand(table)
        gen_shim.gen_intrinsics.expand_intrinsic(
            itab, gen_shim._emitter(rows, itab["widths"]))
        gen_shim.gen_intrinsics.expand_libm(
            ltab, gen_shim._emitter(rows, ltab["widths"]))
        shas = {"opcode": sha, "intrinsic": isha, "libm": lsha}
        for fn, text in gen_shim.files(rows, shas).items():
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
    #
    # PRD §1's OWN EXAMPLE SYMBOL SHIPPED ON 2026-09-19 (bead 9ve.36), so the
    # witness moved one width over to `_to_f32`. That is the decision working
    # rather than a drift: `sitofp i32 -> f64` is one of the seventeen landed
    # conversion pairs and is now a live wrapper, while f32 has no rail at all
    # (PRD-v2 §7.9) and is exactly as deferred as it ever was. The MESSAGE is
    # unchanged, which is what the PRD fixes.
    #
    # THE TWO `cq_shim_fcast*` STUBS ARE WHY THIS FILE STILL LINKS. The same
    # `.gen.c` now holds 34 live conversion wrappers; without them the witness
    # fails at the LINK stage and says nothing about any abort body.
    cc = os.environ.get("CQOPS_CC", "cc")
    tmp = tempfile.mkdtemp(prefix="cqops-shim-run-")
    main = os.path.join(tmp, "main.c")
    open(main, "w").write(
        '#include <stdio.h>\n#include <stdlib.h>\n#include <stdint.h>\n'
        'int32_t cq_template_sitofp_i32_to_f32(int32_t);\n'
        'int32_t cq_shim_fcast(int k, int f, int t, int32_t a)\n'
        '{ (void)k; (void)f; (void)t; return a; }\n'
        'void cq_shim_fcast_unc(int k, int f, int t, int32_t o, int32_t a)\n'
        '{ (void)k; (void)f; (void)t; (void)o; (void)a; }\n'
        '_Noreturn void cq_shim_unsupported(const char *s, const char *r) {\n'
        '    fprintf(stderr, "cqops: %s not implemented (%s)\\n", s, r);\n'
        '    abort();\n}\n'
        'int main(void) { return (int)cq_template_sitofp_i32_to_f32(0); }\n')
    exe = os.path.join(tmp, "a.out")
    r = subprocess.run([cc, "-std=c11", "-I" + sc.SHIM, main,
                        os.path.join(sc.GENERATED, "cq_template_int_to_fp.gen.c"), "-o", exe],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    sc.check(r.returncode == 0, "witness program did not build:\n%s" % r.stdout.decode()[:1500])
    r = subprocess.run([exe], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    sc.check(r.returncode != 0, "an fp abort body returned normally")
    want = b"cqops: cq_template_sitofp_i32_to_f32 not implemented (fp is v2)"
    sc.check(want in r.stderr, "PRD §1's message not emitted; got %r" % r.stderr[:200])
    # AND PRD §1's LITERAL EXAMPLE IS NOW A WRAPPER, asserted rather than left
    # as a comment: if it ever goes back to aborting, this bead was reverted and
    # the witness above should move back with it.
    sc.check(sc.bucket_of_body(SHIPPED["cq_template_sitofp_i32_to_f64"][1])
             == "wrapper",
             "cq_template_sitofp_i32_to_f64 is not a live wrapper; PRD §1's "
             "example symbol landed at bead 9ve.36")


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


if __name__ == "__main__":
    sys.exit(sc.run([
        the_generated_bodies_compile_under_the_projects_own_flags,
        a_signature_that_diverges_from_the_abi_is_caught,
        the_two_word_literal_round_trips_at_every_shipped_width,
        an_abort_body_really_aborts_with_prd_1s_message,
    ]))

