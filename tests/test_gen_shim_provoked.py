#!/usr/bin/env python3
# test_gen_shim_provoked.py — the PROVOCATIONS half of Step 22's GRID gate, on
# the seam tests/test_gen_shim.py records at the foot of itself:
#
#     the ASSERTIONS <-> the PROVOCATIONS
#
# TAKEN 2026-09-19 (bead 9ve.36) ON THE FIRST MEASUREMENT PAST THE WALL, and it
# is the SAME cut tests/test_gen_bodies.py took a day earlier for the same
# reason. The `.py` reached 303 counted lines of Rule 12's 300 when the fp
# ARITHMETIC surface landed and brought a declined-reason case, two finer-key
# provocations and three more enum arms with it; Python has no `.inc` escape
# hatch (check_loc.sh counts every line of a multi-line string as code), so the
# seam is a second FILE and a fourth registered ctest entry.
#
# THE CUT IS A SUBJECT CUT AND ITS DISCRIMINATOR IS SHARP: everything in the
# sibling is a claim about the GENERATOR, and everything here is a claim about
# the SUITE. A case here re-renders the grid with a deliberately wrong bucket
# assignment and requires a NAMED case over there to go red AND to name the
# symbols — an assertion nobody has seen fail is an assertion nobody has
# tested, and mutating an assertion cannot fail against a correct generator, so
# the instrument is a PROVOCATION rather than a mutant.
#
# THE IMPORT IS ONE-WAY. This file imports the sibling; the sibling names
# nothing here, so its own run is unaffected by anything below. Importing it
# re-parses shim/generated/*.gen.c and re-loads the manifest once, which is the
# same work the sibling does and is why both entries take a couple of seconds.

import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "support"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shimcheck as sc  # noqa: E402
import test_gen_shim as gs  # noqa: E402

sys.path.insert(0, sc.SHIM)
import gen_shim  # noqa: E402

# THE EXPECTED SETS AND THE BUCKET READER ARE THE SIBLING'S, imported rather
# than rebuilt: a provocation that judged itself against its own idea of the
# partition would be measuring nothing at all.
EXPECT = gs.EXPECT
buckets = gs.buckets


def _rerender(swap=None, demote=None, land=None, promote=None):
    table, sha = gen_shim.load(sc.YAML)
    rows = gen_shim.expand(table)
    for r in rows:
        # THE KEY IS gen_shim's OWN since 2026-09-19 — `(opcode, width)`, or
        # `(opcode, from, to)` for a cast — so a provocation lands exactly the
        # rows a wrong LANDED line would.
        if land and r.bucket == "fp" \
                and gen_shim.landed_key(r.kind, r.opcode, r.widths) in land:
            r.bucket = "inv" if r.inv else "wrapper"
            r.reason = None
        if promote and r.name == promote:
            r.bucket = "wrapper"
        if swap and r.bucket in ("wrapper", "inv"):
            if r.variant in swap[0]:
                r.bucket = "inv"
            elif r.variant in swap[1]:
                r.bucket = "wrapper"
        if demote and r.name == demote:
            r.bucket = "inv"
    return sc.parse_definitions(gen_shim.files(rows, sha))


def _reds(defs):
    out = []
    for want, name in ((EXPECT["inv"], "inv"), (EXPECT["wrapper"], "wrapper")):
        try:
            sc.diff_sets(buckets(defs)[name], want, name)
        except sc.Fail as e:
            out.append(str(e))
    return out


def each_of_the_twenty_same_size_transpositions_turns_the_gate_red():
    # The 20 are MEASURED, not asserted from prose: a wrapper-bucket variant
    # token and an abort-bucket one carrying the SAME number of purely-integer
    # symbols, so the swap leaves 992/603/884/2479 all exact.
    rows = gen_shim.expand(gen_shim.load(sc.YAML)[0])
    size = {}
    for r in rows:
        if r.bucket in ("wrapper", "inv"):
            size.setdefault((r.bucket, r.variant), 0)
            size[(r.bucket, r.variant)] += 1
    pairs = [(a, b, n) for (ba, a), n in size.items() if ba == "wrapper"
             for (bb, b), m in size.items() if bb == "inv" and m == n]
    sc.check(len(pairs) == 20, "expected 20 same-size transpositions, measured %d" % len(pairs))
    # 82 until bead 9ve.36 landed `fadd`/`fsub`/`fmul`/`fdiv` at f64, which put
    # four more `controlled_qq` rows and four more `controlled_inv_qq` rows in
    # the live buckets. The PAIR COUNT is unmoved at 20, which is the thing
    # worth noticing: the two sides grew by the same amount.
    sc.check(("controlled_qq", "controlled_inv_qq", 86) in pairs,
             "controlled_qq <-> controlled_inv_qq at 86 is not among the 20")
    for a, b, n in pairs:
        defs = _rerender(swap=((a,), (b,)))
        sc.check(len(defs) == 2479, "%s<->%s changed the symbol COUNT" % (a, b))
        reds = _reds(defs)
        sc.check(len(reds) == 2,
                 "%s <-> %s (%d each) left the gate GREEN — only %d of 2 set "
                 "assertions fired" % (a, b, n, len(reds)))
        sc.check(all("cq_template_" in r for r in reds),
                 "%s <-> %s went red without naming a symbol" % (a, b))


def the_composite_swap_that_really_does_abort_all_214_controlled_is_caught():
    # THE 20 ARE SINGLE-TOKEN SWAPS, AND THE SCENARIO THREE DOCUMENTS DESCRIBE
    # IS NOT ONE OF THEM. CLAUDE.md, the plan's Step 22 row and bd 819 all say
    # `controlled_inv_qq` <-> `controlled_qq` "makes the ENTIRE 214-symbol
    # integer `_controlled` grid abort". Measured here: that single swap moves
    # 86 each way and leaves 138 controlled wrappers (`controlled_hl` 86 +
    # `controlled_lh` 52) LIVE. Aborting the whole controlled grid takes the
    # COMPOSITE swap of all three shape tokens at once, which also holds the
    # three bucket counts exact and is a strictly larger family than the 20 —
    # so a negative control that pins only the 20 does not cover it. It does
    # now. (The controlled grid is 224 since bead 9ve.36, not the 214 those
    # three documents name: `fadd`/`fsub`/`fmul`/`fdiv` at f64 carry ten
    # controlled wrappers between them.)
    live, dead = ("controlled_qq", "controlled_hl", "controlled_lh"), \
                 ("controlled_inv_qq", "controlled_inv_hl", "controlled_inv_lh")
    single = _rerender(swap=(("controlled_qq",), ("controlled_inv_qq",)))
    survivors = {n for n in buckets(single)["wrapper"] if "_controlled" in n and not n.endswith("_inv")}
    sc.check(len(survivors) == 138,
             "the single swap left %d controlled wrappers live, expected 138" % len(survivors))
    reds = _reds(_rerender(swap=(live, dead)))
    sc.check(len(reds) == 2, "the composite 214-symbol swap left the gate green")
    sc.check(all("cq_template_" in r for r in reds), "the composite swap named no symbol")


def a_single_wrapper_demoted_to_an_abort_turns_the_gate_red():
    # The cheapest possible defect, and the one a count can never see: 2479
    # emitted, every bucket size wrong by exactly one in compensating
    # directions is impossible here, so this is caught by the SET alone.
    victim = "cq_template_mul_i32"
    defs = _rerender(demote=victim)
    reds = _reds(defs)
    sc.check(len(reds) == 2, "demoting %s left the gate green" % victim)
    sc.check(all(victim in r for r in reds), "the failure did not name %s: %s" % (victim, reds))


def a_family_width_that_has_not_landed_cannot_be_declared_live():
    # THE NEW DEFECT CLASS THE `LANDED` TABLE INTRODUCES, and it has no
    # structural detector of its own: an extra line in gen_shim.LANDED emits 84
    # live wrappers per family-width whose kernel and dispatch row do not exist.
    # That is a LINK error today — which is exactly why it must be provoked
    # rather than trusted to stay one: the day `fp_arith`/`f64` lands, `fadd` at
    # `f32` becomes one wrong line from shipping a live wrapper that dispatches
    # a 32-bit operand into a 64-bit kernel, and nothing links-or-not about it.
    #
    # TWO PROVOCATIONS, AND THEY FAIL IN GENUINELY DIFFERENT WAYS — which is the
    # finding rather than a redundancy.
    #
    # (1) A NON-f64 fp WIDTH IS REFUSED BY THE GENERATOR ITSELF, before a byte is
    # written. `gen_bodies.literal` hard-errors because only `f64` has a
    # bit-pattern macro, so the failure names the symbol, the width and the file
    # that would have to change first. That is a stronger answer than a red set:
    # a `SystemExit` cannot be regenerated away, and the alternative — emitting
    # `CQ_SHIM_LO(b_classical)` on a `float` — is the NUMERIC-CONVERSION defect
    # `shim/cq_shim.h` exists to prevent, and it would compile clean.
    for width in ("f16", "f32", "f80"):
        try:
            _rerender(land={("fcmp", width)})
        except SystemExit as e:
            bits = {"f16": 16, "f32": 32, "f80": 80}[width]
            sc.check(width in str(e) and "CQ_SHIM_F%d_LO" % bits in str(e),
                     "the refusal for %s did not name the width and the "
                     "missing macro: %s" % (width, e))
            continue
        raise sc.Fail("landing fcmp/%s rendered without a refusal" % width)

    # (2) THE ONE THAT RENDERS PERFECTLY AND MUST STILL GO RED — the cheapest
    # possible version of the defect and the one no refusal can catch. A `qq`
    # row has NO classical operand, so the macro guard above never sees it:
    # `cq_template_fcmp_oeq_f32` promoted to a wrapper emits a syntactically
    # perfect `cq_shim_fcmp_qq(CQ_SHIM_FPRED_OEQ, 32, …)`, keeps the 2479-name
    # set, keeps every signature, and dispatches a 32-bit operand into a kernel
    # that hard-errors on anything but 64. Only the SET sees it, and it names it.
    victim = "cq_template_fcmp_oeq_f32"
    defs = _rerender(promote=victim)
    sc.check(len(defs) == 2479, "promoting %s changed the symbol COUNT" % victim)
    reds = _reds(defs)
    sc.check(len(reds) == 1,
             "promoting %s turned %d of the 2 set assertions red; expected the "
             "wrapper set alone (it is not an `_inv` name)" % (victim, len(reds)))
    sc.check(all(victim in r for r in reds),
             "the failure did not name %s: %s" % (victim, reds))

    # (3) THE OPCODE-GRAINED SLIP — `frem` and `fneg` swept live alongside
    # `fadd` by a `(family, width)` key — IS gen_bodies' FAILURE and is
    # provoked in tests/test_gen_bodies_provoked.py, at the two tables that
    # refuse it. It is named here and not duplicated: two copies of one
    # provocation means a deleted one keeps passing.

    # (4) AND THE CAST KEY, ONE STEP FINER AGAIN. `uitofp i64 -> f64` is bead
    # 9ve.34's REFUSED row; its four siblings ship. A key of `(opcode, width)`
    # for casts could not express that set at all, and landing it here renders
    # PERFECTLY — the body is a well-formed `cq_shim_fcast(CQ_SHIM_FCAST_UITOFP,
    # 64, 64, a_handle)` — which is exactly the defect: the shim would then have
    # to refuse it at runtime, and upstream's own routing converts every
    # u >= 2^63 negative. Only the SET sees it, and it names it.
    victim = "cq_template_uitofp_i64_to_f64"
    defs = _rerender(land={("uitofp", "i64", "f64")})
    sc.check(len(defs) == 2479, "landing %s changed the symbol COUNT" % victim)
    reds = _reds(defs)
    sc.check(len(reds) == 2,
             "landing %s left %d of the 2 set assertions red; expected both "
             "(its `_inv` moves too)" % (victim, len(reds)))
    sc.check(all(victim in r for r in reds),
             "the failure did not name %s: %s" % (victim, reds))


def the_check_mode_sees_a_tampered_file():
    # --check is CI's drift gate; a drift gate that cannot report drift is the
    # `set_tests_properties` trap in another dress.
    def run_check(tmp):
        return subprocess.run([sys.executable, os.path.join(sc.SHIM, "gen_shim.py"),
                               "--output-dir", tmp, "--check"],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    # STALE — a hand-edited generated file, the thing "never hand-write the
    # shim" exists to prevent.
    tmp = sc.generate()
    open(os.path.join(tmp, "cq_template_int_width.gen.c"), "a").write("/* tampered */\n")
    r = run_check(tmp)
    sc.check(r.returncode == 1, "--check passed on a hand-edited generated file")
    sc.check(b"cq_template_int_width.gen.c" in r.stdout, "--check did not name the stale file")

    # EXTRA — a family that stopped existing. The two arms are different code
    # paths, and the extra one has to ignore a .DS_Store or a __pycache__ or it
    # is a drift gate that goes red for nothing and gets turned off.
    tmp = sc.generate()
    open(os.path.join(tmp, "cq_template_ghost.gen.c"), "w").write("/* orphan */\n")
    open(os.path.join(tmp, ".DS_Store"), "w").write("junk")
    r = run_check(tmp)
    sc.check(r.returncode == 1, "--check passed with an orphaned .gen.c present")
    sc.check(b"cq_template_ghost.gen.c" in r.stdout, "--check did not name the orphan")
    sc.check(b".DS_Store" not in r.stdout, "--check reported a non-generated file as drift")
    os.remove(os.path.join(tmp, "cq_template_ghost.gen.c"))
    sc.check(run_check(tmp).returncode == 0, "--check stayed red once only .DS_Store remained")


if __name__ == "__main__":
    sys.exit(sc.run([
        each_of_the_twenty_same_size_transpositions_turns_the_gate_red,
        the_composite_swap_that_really_does_abort_all_214_controlled_is_caught,
        a_single_wrapper_demoted_to_an_abort_turns_the_gate_red,
        a_family_width_that_has_not_landed_cannot_be_declared_live,
        the_check_mode_sees_a_tampered_file,
    ]))
