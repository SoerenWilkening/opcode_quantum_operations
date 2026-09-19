#!/usr/bin/env python3
# test_gen_bodies_provoked.py — the PROVOCATIONS half of Step 22's bodies gate,
# on the seam tests/test_gen_bodies.py recorded in its own header BEFORE this
# file was needed:
#
#     the ASSERTIONS <-> the PROVOCATIONS
#
# TAKEN 2026-09-18 (bead 9ve.28) ON THE FIRST MEASUREMENT PAST THE WALL. The
# `.py` reached 319 counted lines of Rule 12's 300 when PRD-v2's first fp family
# landed and brought three new dispatch mutants with it; Python has no `.inc`
# escape hatch (check_loc.sh counts every line of a multi-line string as code),
# so the seam is a second FILE and a third registered ctest entry, exactly as
# that header said it would be.
#
# THE CUT IS A SUBJECT CUT AND ITS DISCRIMINATOR IS SHARP: everything in the
# sibling is about the GENERATOR, and everything here is about the SUITE. A case
# here breaks a definitions dict on purpose and requires a named case over there
# to go red AND to name the symbol — an assertion nobody has seen fail is an
# assertion nobody has tested, and mutating an assertion cannot fail against a
# correct generator, so the instrument is a PROVOCATION rather than a mutant.
#
# THE IMPORT IS ONE-WAY. This file imports the sibling's cases; the sibling
# names nothing here, so its own run is unaffected by anything below. Importing
# it re-parses shim/generated/*.gen.c once, which is the same work the sibling
# does and is why both entries take a couple of seconds.

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "support"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shimcheck as sc  # noqa: E402
import test_gen_bodies as gb  # noqa: E402

sys.path.insert(0, sc.SHIM)
import gen_bodies  # noqa: E402
import gen_shim  # noqa: E402

SHIPPED = gb.SHIPPED
no_wrapper_is_inert = gb.no_wrapper_is_inert
every_wrapper_body_is_exactly_the_call_its_symbol_names = \
    gb.every_wrapper_body_is_exactly_the_call_its_symbol_names


def _broken(name, body):
    d = dict(SHIPPED)
    d[name] = (SHIPPED[name][0], body)
    return d


def an_inert_wrapper_body_is_caught():
    # BOTH DOMAINS, because the wrapper bucket is no longer all-integer: an
    # inert fp body would leave the 1048 count and the 2479 name set exact just
    # as an inert integer one does.
    for victim in ("cq_template_add_i32", "cq_template_fcmp_olt_f64"):
        for body in ([], ["    return 0;"],
                     ["    (void)a_handle;", "    return b_handle;"]):
            try:
                no_wrapper_is_inert(_broken(victim, body))
            except sc.Fail as e:
                sc.check(victim in str(e), "the failure did not name %s: %s" % (victim, e))
                continue
            raise sc.Fail("an inert body %r passed no_wrapper_is_inert" % (body,))


def every_way_a_wrapper_can_dispatch_wrong_is_caught():
    # EIGHT PROVOCATIONS, ONE PER THING THE ARGUMENT LIST PINS. The first five
    # were found by an adversarial pass to survive the whole suite before this
    # case existed, and each survives EVERY other detector Step 22 and Step 23
    # have: the 2479-name set, all 2479 signatures, the bucket partition, the
    # one-call shape, the -Werror compile against CQ_lang's own declarations,
    # and `nm`.
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
    #
    # THE LAST THREE ARRIVED WITH PRD-v2's FIRST fp FAMILY (2026-09-18) and each
    # is its own defect class:
    #   (6) the CROSS-FAMILY predicate — `CQ_SHIM_PRED_ULT` where the enum must
    #       be `CQ_SHIM_FPRED_ULT`. The mnemonic is spelled identically in both
    #       lists, so this is the one a reader's eye slides over; it dispatches
    #       an IEEE pattern into the integer comparator.
    #   (7) the WRONG fp PREDICATE inside the right enum — K9's
    #       `uge`-meaning-`ule` in a new column, and nothing but L1 against
    #       `cq_fcmp_eval` sees it downstream of here.
    #   (8) the LITERAL MACRO PAIR — `CQ_SHIM_LO` on a `double`, which is a
    #       NUMERIC CONVERSION. It compiles clean, is silent under -Wconversion
    #       because the cast inside it is explicit, links, emits the same gate
    #       count, and puts the integer 3 in the rail where 0x400C000000000000
    #       belongs.
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
        ("cq_template_fcmp_ult_f64",
         lambda s: s.replace("CQ_SHIM_FPRED_ULT", "CQ_SHIM_PRED_ULT", 1),
         "integer predicate enum on an fp compare"),
        ("cq_template_fcmp_uge_f64",
         lambda s: s.replace("CQ_SHIM_FPRED_UGE", "CQ_SHIM_FPRED_ULE", 1),
         "collapsed fp predicate"),
        ("cq_template_fcmp_oeq_f64_hl",
         lambda s: s.replace("CQ_SHIM_F64_LO(b_classical), "
                             "CQ_SHIM_F64_HI(b_classical)",
                             "CQ_SHIM_LO(b_classical), CQ_SHIM_HI(b_classical)", 1),
         "fp literal converted instead of reinterpreted"),
        # AND FOUR MORE WITH THE fp ARITHMETIC SURFACE (2026-09-19, bead
        # 9ve.36). Each is a defect class the twelve above cannot reach:
        #   (13) the fp BINARY selector collapsed — `fsub` dispatching `fadd`.
        #        M33 runs ONE row program under two prologues, so the two emit
        #        the same gate tuple at the same width and differ only on ±0
        #        and the NaN rows; nothing structural sees it downstream.
        #   (14) the CONVERSION kind collapsed — `sitofp` where `uitofp`
        #        belongs, which is bead 9ve.34's defect exactly: `uitofp` IS
        #        `sitofp`'s row program, so the circuits are identical and they
        #        differ only on sources with the top bit set.
        #   (15) the two CONVERSION WIDTHS transposed. Unique to the
        #        cross-domain family: an integer cast's widths are refused by
        #        the direction guard one layer down, but `f64 -> i8` and
        #        `i8 -> f64` are BOTH shipped pairs, so a transposition is a
        #        legal call for a different operation.
        #   (16) the `_lh` literal words transposed on an fp operand, where the
        #        HI word is ALWAYS zero (CQ_SHIM_F64_HI discards its argument),
        #        so the transposition quietly puts 0 where the pattern belongs.
        ("cq_template_fsub_f64",
         lambda s: s.replace("CQ_SHIM_FOP_FSUB", "CQ_SHIM_FOP_FADD", 1),
         "collapsed fp arithmetic selector"),
        ("cq_template_sitofp_i8_to_f64",
         lambda s: s.replace("CQ_SHIM_FCAST_SITOFP", "CQ_SHIM_FCAST_UITOFP", 1),
         "collapsed cross-domain conversion kind"),
        ("cq_template_fptosi_f64_to_i8",
         lambda s: s.replace(", 64, 8,", ", 8, 64,", 1),
         "conversion widths transposed"),
        ("cq_template_fsub_f64_lh",
         lambda s: swap(s, "CQ_SHIM_F64_LO(a_classical)",
                        "CQ_SHIM_F64_HI(a_classical)"),
         "fp literal words transposed on the `_lh` door"),
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


def a_landed_key_that_sweeps_a_kernel_less_opcode_in_is_refused():
    # THE DEFECT THAT MADE `gen_shim.LANDED`'s KEY FINER (2026-09-19, bead
    # 9ve.36), provoked at the two tables that refuse it.
    #
    # `fp_arith` is SIX opcodes and only four have kernels. `frem` is a binary
    # row with none, and `fneg` — the yaml's ONE unary opcode — is in that same
    # family and has none either. A `LANDED` key of `(family, width)` takes all
    # six together, and the results are NOT a red test: `frem` would emit
    # `CQ_SHIM_FOP_FREM`, an enumerator that does not exist, so the failure
    # would surface as a C compile error in a GENERATED file whose header says
    # never to edit it; `fneg` would need an entry point that has not been
    # written. Both tables are explicit precisely so the failure is a KeyError
    # naming the thing, at the generator, before a byte is written.
    #
    # IT DRIVES `gen_bodies.body` DIRECTLY rather than re-rendering the grid,
    # which is the sharper instrument: it names the exact ROW and the exact
    # TABLE, and it cannot be satisfied by some other row of the same family
    # happening to fail first.
    # `fneg`'s REFUSAL MOVED TABLES AT bead 9ve.24 AND DID NOT WEAKEN. Before
    # the vendoring `ENTRY` had no `("funary", ...)` row at all, so a swept-in
    # `fneg` failed there; the vendoring gave `funary` its two rows for M40's
    # `sqrt`, so the refusal is now `SELECTOR["funary"]["fneg"]` — one table
    # later and NAMING THE OPCODE rather than the family, which is strictly
    # sharper. `frem` is unmoved. The expectation below is what each table's
    # KeyError actually says, so a row silently acquiring an entry point in
    # EITHER table still turns this red.
    #
    # THE THIRD ROW IS NEW AND IT IS THE VENDORING'S OWN SHAPE: `ctpop` at i32
    # is an INTEGER intrinsic in the `defer` bucket, and sweeping it live is
    # the bead 9ve.29 mistake. It has no `unary` entry at all, so `ENTRY` is
    # what refuses it.
    rows = gen_shim.expand(gen_shim.load(sc.YAML)[0])
    itab = gen_shim.load(sc.SOURCES[1][0])[0]
    gen_shim.gen_intrinsics.expand_intrinsic(
        itab, gen_shim._emitter(rows, itab["widths"]))
    for opcode, table in (("frem", "frem"), ("fneg", "fneg"),
                          ("ctpop", "unary")):
        w = "i32" if opcode == "ctpop" else "f64"
        victims = [r for r in rows
                   if r.opcode == opcode and r.widths[0] == w and not r.inv]
        sc.check(victims, "no %s row at %s to provoke with" % (opcode, w))
        r = victims[0]
        sc.check(r.bucket in ("fp", "defer"),
                 "%s is not an abort any more; this provocation is stale" % r.name)
        r.bucket, r.reason = "wrapper", None
        try:
            gen_bodies.body(r)
        except KeyError as e:
            sc.check(table in str(e),
                     "%s rendered with %r, which does not name %r"
                     % (r.name, e, table))
            continue
        raise sc.Fail("%s rendered a wrapper body with no kernel behind it: %r"
                      % (r.name, gen_bodies.body(r)))


if __name__ == "__main__":
    sys.exit(sc.run([
        an_inert_wrapper_body_is_caught,
        every_way_a_wrapper_can_dispatch_wrong_is_caught,
        a_landed_key_that_sweeps_a_kernel_less_opcode_in_is_refused,
    ]))
