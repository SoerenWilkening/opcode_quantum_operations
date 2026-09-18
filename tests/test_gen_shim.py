#!/usr/bin/env python3
# test_gen_shim.py — Step 22's gate, the GRID half.
#
# Split on the same seam as the module it tests (plan §3: `the ABI grid <-> the
# emitted bodies`). This file asserts the SYMBOL SET, the SIGNATURES and the
# BUCKET PARTITION; tests/test_gen_bodies.py asserts what is between the braces.
#
# THE ORACLE IS NOT THIS FILE'S OWN READING OF THE NAMING RULES. tests/abi/
# cq_templates_abi.txt is the 2479 declarations CQ_lang's own gen_templates.py
# produced from the SAME pinned yaml — a different program, so it cannot share
# a transcription slip with shim/gen_shim.py. Re-expanding the rules here
# instead would be the trap CLAUDE.md names: "an oracle that shares a constant
# with the code is blind to exactly what that constant gets wrong — and it
# agrees with the bug rather than failing."
#
# EVERY SET IS DIFFED IN BOTH DIRECTIONS, because a count is not an
# identification: 20 same-size variant transpositions leave 992 / 603 / 884 /
# 2479 all exact while swapping a live family for an aborting one, and neither
# Step 23's `nm` nor Step 24's corpus can notice. The last four cases are the
# NEGATIVE CONTROLS that make these assertions ones somebody has seen fail.

import hashlib
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "support"))
import shimcheck as sc  # noqa: E402

sys.path.insert(0, sc.SHIM)
import gen_shim  # noqa: E402

ABI = sc.manifest_decls()

# THE THREE CODE BUCKETS ARE NO LONGER "INTEGER, INTEGER-inv, fp" — they are
# "live, D14-refused, still-deferred" (PRD-v2 §1, bead 9ve.28). A LANDED fp
# family-width joins the first two on exactly the same rule an integer family
# does, so the expected sets are built from `is_deferred_fp` rather than from
# `is_fp`. The `_inv` suffix test is unchanged and is still the ONLY thing
# separating the first two, which is the half of D14 that is width-blind: an
# `fcmp` is non-injective at every width, so its `_inv` is refused whether or not
# its forward ships.
LIVE = {n for n in ABI if not sc.is_deferred_fp(n)}
EXPECT = {"wrapper": {n for n in LIVE if not n.endswith("_inv")},
          "inv": {n for n in LIVE if n.endswith("_inv")},
          "fp": {n for n in ABI if sc.is_deferred_fp(n)}}
# The four-way reading a reader and a reviewer actually want, kept as SETS so a
# family that moved domain is named rather than counted.
BY_DOMAIN = {
    "int_wrapper": {n for n in EXPECT["wrapper"] if not sc.is_fp(n)},
    "int_inv":     {n for n in EXPECT["inv"] if not sc.is_fp(n)},
    "fp_wrapper":  {n for n in EXPECT["wrapper"] if sc.is_fp(n)},
    "fp_inv":      {n for n in EXPECT["inv"] if sc.is_fp(n)},
}
SHIPPED = sc.parse_definitions(sc.read_dir(sc.GENERATED))


def buckets(defs):
    out = {"wrapper": set(), "inv": set(), "fp": set()}
    for name, (_decl, body) in defs.items():
        out.setdefault(sc.bucket_of_body(body), set()).add(name)
    return out


def the_manifest_is_the_expansion_of_the_pinned_yaml():
    # The gate on every other case in this file. CQ_lang is UNPINNED — its HEAD
    # has moved past third_party/cq_lang/COMMIT — so the manifest's authority
    # rests entirely on this equality, exactly as the L4 goldens' rests on the
    # Bennett COMMIT check (risk R3). Re-pinning the yaml must turn this red.
    sha = hashlib.sha256(open(sc.YAML, "rb").read()).hexdigest()
    sc.check(sha == sc.manifest_yaml_sha(),
             "tests/abi manifest was extracted from a DIFFERENT opcode_table.yaml:\n"
             "  pinned yaml : %s\n  manifest    : %s\n"
             "  Re-extract from CQ_lang's regenerated header; do not edit either."
             % (sha, sc.manifest_yaml_sha()))
    sc.check(len(ABI) == 2479, "manifest holds %d declarations, expected 2479" % len(ABI))


def the_emitted_symbol_set_equals_the_abi_in_both_directions():
    sc.diff_sets(set(SHIPPED), set(ABI), "emitted symbol set")


def every_emitted_signature_matches_cq_langs_own_declaration():
    # C LINKS A WRONG SIGNATURE SILENTLY AND `nm` CANNOT SEE IT (bd 819). This
    # is the strongest oracle Step 22 will ever have, and it is why signature()
    # lives on the grid side of the seam: it runs with no body emitted.
    bad = ["%s\n     ours: %s\n   CQ_lang: %s" % (n, SHIPPED[n][0], ABI[n])
           for n in sorted(ABI) if n in SHIPPED and SHIPPED[n][0] != ABI[n]]
    sc.check(not bad, "%d signature mismatch(es):\n  %s" % (len(bad), "\n  ".join(bad[:3])))


def the_partition_is_1048_wrappers_631_inv_aborts_800_fp_aborts():
    # It was 992 / 603 / 884 until PRD-v2's first fp family landed on
    # 2026-09-18: 84 `fcmp` symbols at `f64` left the fp bucket, 56 of them as
    # wrappers and 28 as D14 `_inv` aborts (bead 9ve.28).
    got = {k: len(v) for k, v in buckets(SHIPPED).items()}
    sc.check(got == {"wrapper": 1048, "inv": 631, "fp": 800},
             "PRD §1 + §15 D14 + PRD-v2 §1 partition: expected 1048/631/800, "
             "got %r" % got)


def the_four_way_domain_split_is_992_603_56_28_and_800():
    # A COUNT IS NOT AN IDENTIFICATION, AND THE THREE COARSE BUCKETS NOW HIDE A
    # DOMAIN MOVE. Once a family-width lands, `wrapper` holds integer AND fp
    # wrappers, so 1048/631/800 stays exact while, say, `fp_arith`/`f64` goes
    # live and `int_arith` loses the same number — which the case above cannot
    # see and this one names. The expected side is the ABI's own spelling
    # crossed with `is_landed`; gen_shim's audit reaches the same five numbers
    # down a different route (the yaml's `widths` map and `family` key).
    got = buckets(SHIPPED)
    live = {"wrapper": got["wrapper"], "inv": got["inv"]}
    for key, want in sorted(BY_DOMAIN.items()):
        dom, bucket = key.split("_", 1)
        have = {n for n in live[bucket] if (sc.is_fp(n) == (dom == "fp"))}
        sc.diff_sets(have, want, "%s set" % key)
    sc.check([len(BY_DOMAIN[k]) for k in
              ("int_wrapper", "int_inv", "fp_wrapper", "fp_inv")]
             == [992, 603, 56, 28],
             "the four-way split moved: %r"
             % {k: len(v) for k, v in BY_DOMAIN.items()})


def the_inv_abort_set_is_exactly_the_inv_names_of_every_live_family():
    # D14's own obligation, and it is a SET so that nothing is swept into the
    # bucket unobserved. The expected side is derived from the ABI's own
    # spelling — a name is fp-touching iff it carries an f16/f32/f64/f80 token,
    # and a LANDED one iff sc.LANDED_NAME matches — never from
    # gen_shim.bucket_of, which walks the yaml's `widths` map.
    sc.diff_sets(buckets(SHIPPED)["inv"], EXPECT["inv"], "_inv abort set")


def the_wrapper_set_is_exactly_the_non_inv_names_of_every_live_family():
    # Naming only the abort bucket leaves 1679 bodies unconstrained (bd 819).
    sc.diff_sets(buckets(SHIPPED)["wrapper"], EXPECT["wrapper"], "wrapper set")


def the_fp_abort_set_is_exactly_the_fp_touching_names_that_have_not_landed():
    # 240 of these carry an integer width in their NAME as well — the
    # cross-domain casts — so they look integer-ish and are not (PRD §1). That
    # figure is UNMOVED by the fcmp landing, which is the point of pinning it
    # beside a number that did move: `fcmp` carries no integer width token, so a
    # landing that had wrongly swept a cross-domain cast into the live set would
    # show up here and nowhere else.
    got = buckets(SHIPPED)["fp"]
    sc.diff_sets(got, EXPECT["fp"], "fp abort set")
    crossdomain = {n for n in got if any("_i%d" % b in n for b in (1, 8, 16, 32, 64, 80, 128))}
    sc.check(len(crossdomain) == 240,
             "expected 240 integer-ish fp symbols, got %d" % len(crossdomain))
    # AND THE STILL-DEFERRED fp WIDTHS ARE STILL THERE IN FULL. `fcmp` ships at
    # f64 ALONE (PRD-v2 §1a), so its other three widths must remain aborts —
    # 14 preds x 6 variants x 3 widths. A "LANDED = the whole family" slip is
    # invisible to every count above, because it would move 252 symbols out of
    # `fp` and into `wrapper`/`inv` and the partition case would simply report
    # three different numbers without saying which family moved.
    deferred_fcmp = {n for n in got if n.startswith("cq_template_fcmp_")}
    sc.check(len(deferred_fcmp) == 252,
             "expected 252 still-deferred fcmp symbols (f16/f32/f80), got %d"
             % len(deferred_fcmp))
    sc.check(not any("_f64" in n for n in deferred_fcmp),
             "an f64 fcmp symbol is still in the `fp is v2` bucket")


def no_cqrt_rotation_symbol_appears_anywhere_in_the_emitted_shim():
    # `_inv` NAMES TWO UNRELATED THINGS (D14). On the controlled ROTATIONS it is
    # θ-negation — cqrt_ry_<W>_controlled_inv (7 widths) and
    # cqrt_rz_<W>_controlled_inv (9, bd tso) — which is LIVE and M26's to
    # implement normally at Step 23. Getting this backwards makes the link
    # succeed and a shipped rotation abort at runtime. They are `cqrt_*`, not
    # `cq_template_*`, so M27 must not emit them at all.
    #
    # THIS SCANS RAW TEXT, AND THE REASON IS THE WHOLE POINT OF THE CASE. Its
    # first form asked the PARSED definitions whether any name started with
    # `cqrt_` — and parse_definitions' regex admits only `cq_template_*` names,
    # so the question was structurally unanswerable and the case could not fail.
    # An adversarial pass found it; the file header claims every assertion here
    # has been seen to fail, and this one had not. It has now: the provocation
    # below is part of the case rather than a separate one, because a case whose
    # own witness lives elsewhere is how the first form survived.
    def scan(text):
        stray = sorted(set(re.findall(r"\bcqrt_[A-Za-z0-9_]+", text)))
        rot = sorted(set(re.findall(
            r"\bcq_template_(?:[A-Za-z0-9_]*_)?r[yz]_[A-Za-z0-9_]*", text)))
        return stray + rot

    text = "".join(sc.read_dir(sc.GENERATED).values())
    sc.check(not scan(text), "M27 emitted or named a rotation symbol: %s" % scan(text)[:4])
    for injected in ("int32_t cqrt_rz_i32_controlled_inv(int32_t c, int32_t h, double t);",
                     "int32_t cq_template_ry_i32_controlled(int32_t c, int32_t h);"):
        sc.check(scan(text + "\n" + injected),
                 "the scan does not see %r — it could not fail" % injected)


def the_shim_enums_match_the_opcodes_the_grid_reaches():
    # shim/cq_shim.h is hand-written (it is the M26 contract), so its enums are
    # a second transcription of the ABI and have to be checked against it.
    header = open(os.path.join(sc.SHIM, "cq_shim.h")).read()
    rows = gen_shim.expand(gen_shim.load(sc.YAML)[0])
    live = [r for r in rows if r.bucket == "wrapper"]
    # THE TWO PREDICATE ENUMS ARE SEPARATED BY THE OPCODE AND NOT BY THE KIND,
    # because `icmp` and `fcmp` share `kind == "compare"` and their predicate
    # lists OVERLAP in `ult`/`ugt`/`ule`/`uge`. A single `kind == "compare"` arm
    # would demand all 24 mnemonics of `CQ_SHIM_PRED_*` and find 10 — which is
    # how this case first went red, and the right fix was the split enum rather
    # than a union.
    #
    # THE `CQ_SHIM_FPRED_` PREFIX IS TESTED FIRST AND THE TEST IS `startswith`,
    # so the two arms are disjoint only because `CQ_SHIM_FPRED_` does not start
    # with `CQ_SHIM_PRED_`. It does not — the letters differ at position 9 — and
    # that is checked here rather than assumed, because a rename to
    # `CQ_SHIM_PRED_F*` would silently merge the two sets into one green arm.
    sc.check(not "CQ_SHIM_FPRED_OEQ".startswith("CQ_SHIM_PRED_"),
             "the two predicate enum prefixes are no longer disjoint; the arms "
             "below would merge and stop discriminating")
    for prefix, want in (
            ("CQ_SHIM_OP_", {r.opcode for r in live if r.kind == "binary"}),
            ("CQ_SHIM_PRED_", {r.pred for r in live if r.opcode == "icmp"}),
            ("CQ_SHIM_FPRED_", {r.pred for r in live if r.opcode == "fcmp"}),
            ("CQ_SHIM_CAST_", {r.opcode for r in live if r.kind == "cast"})):
        got = {t[len(prefix):].lower() for t in header.split() if t.startswith(prefix)}
        got = {t.rstrip(",}") for t in got}
        sc.diff_sets(got, want, "%s* enum" % prefix)


def every_family_gets_its_own_file_and_only_its_own_symbols():
    # RISK R7 — "one .gen.c per opcode family; the generator already splits" —
    # was asserted NOWHERE. Measured by an adversarial pass: remapping one family
    # onto another's file deletes cq_template_int_arith.gen.c outright, ships 9
    # files instead of 10, and leaves BOTH suites green AND `--check` at rc 0,
    # because --check regenerates the same 9. Only two filenames appear anywhere
    # in either suite and both incidentally.
    #
    # R7 is a COMPILE-TIME risk, so nothing about correctness goes red when it is
    # violated — which is exactly why it needs a detector of its own rather than
    # riding on one.
    rows = gen_shim.expand(gen_shim.load(sc.YAML)[0])
    want = {"cq_template_%s.gen.c" % f for f in {r.family for r in rows}}
    sc.diff_sets(set(sc.read_dir(sc.GENERATED)), want, "emitted file set")
    sc.check(len(want) == 10, "expected 10 opcode families, got %d" % len(want))
    # ...and each file holds ONLY its own family, which a file-set check alone
    # does not give: a generator could emit ten correctly-named files with the
    # symbols shuffled between them.
    fam_of = {r.name: r.family for r in rows}
    for fn, text in sorted(sc.read_dir(sc.GENERATED).items()):
        want_fam = fn[len("cq_template_"):-len(".gen.c")]
        strays = sorted({n for n in sc.parse_definitions({fn: text})
                         if fam_of[n] != want_fam})
        sc.check(not strays, "%s holds %d symbol(s) from another family: %s"
                 % (fn, len(strays), strays[:3]))


def the_generated_files_on_disk_are_up_to_date():
    # PRD §14: "CI regenerates and diffs." This is that gate, run locally.
    r = subprocess.run([sys.executable, os.path.join(sc.SHIM, "gen_shim.py"), "--check"],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    sc.check(r.returncode == 0,
             "shim/generated is stale — re-run `python3 shim/gen_shim.py`:\n%s"
             % r.stdout.decode())


# --- Negative controls -------------------------------------------------------
# An assertion nobody has seen fail is an assertion nobody has tested. A
# mutation of a test's own assertion cannot fail against a correct generator,
# so the instrument here is a PROVOCATION: re-render the grid with a
# deliberately wrong bucket assignment and require the gate above to go red AND
# to name the symbols.

def _rerender(swap=None, demote=None, land=None, promote=None):
    table, sha = gen_shim.load(sc.YAML)
    rows = gen_shim.expand(table)
    for r in rows:
        if land and r.bucket == "fp" and (r.family, r.widths[0]) in land:
            r.bucket = "inv" if r.inv else "wrapper"
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
    sc.check(("controlled_qq", "controlled_inv_qq", 82) in pairs,
             "controlled_qq <-> controlled_inv_qq at 82 is not among the 20")
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
    # 82 each way and leaves 132 controlled wrappers (`controlled_hl` 82 +
    # `controlled_lh` 50) LIVE. Aborting all 214 takes the COMPOSITE swap of
    # all three shape tokens at once, which also holds 992/603/884/2479 exact
    # and is a strictly larger family than the 20 — so a negative control that
    # pins only the 20 does not cover it. It does now.
    live, dead = ("controlled_qq", "controlled_hl", "controlled_lh"), \
                 ("controlled_inv_qq", "controlled_inv_hl", "controlled_inv_lh")
    single = _rerender(swap=(("controlled_qq",), ("controlled_inv_qq",)))
    survivors = {n for n in buckets(single)["wrapper"] if "_controlled" in n and not n.endswith("_inv")}
    sc.check(len(survivors) == 132,
             "the single swap left %d controlled wrappers live, expected 132" % len(survivors))
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
            _rerender(land={("fp_compare", width)})
        except SystemExit as e:
            bits = {"f16": 16, "f32": 32, "f80": 80}[width]
            sc.check(width in str(e) and "CQ_SHIM_F%d_LO" % bits in str(e),
                     "the refusal for %s did not name the width and the "
                     "missing macro: %s" % (width, e))
            continue
        raise sc.Fail("landing fp_compare/%s rendered without a refusal" % width)

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

    # AND A NOTE THAT IS CHEAPER TO RECORD THAN TO REDISCOVER: landing
    # `fp_arith`/`f64` here raises `KeyError: ('unary', 'un', 'fwd')` from
    # gen_bodies.ENTRY, because that family carries `fneg`, the yaml's ONE unary
    # opcode, and no unary entry point exists yet. Landing an arithmetic fp
    # family is therefore an ENTRY-table change as well as a LANDED line — which
    # is the KeyError doing its documented job ("this table is explicit and
    # KeyError is the failure") rather than a gap.


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
        the_manifest_is_the_expansion_of_the_pinned_yaml,
        the_emitted_symbol_set_equals_the_abi_in_both_directions,
        every_emitted_signature_matches_cq_langs_own_declaration,
        the_partition_is_1048_wrappers_631_inv_aborts_800_fp_aborts,
        the_four_way_domain_split_is_992_603_56_28_and_800,
        the_inv_abort_set_is_exactly_the_inv_names_of_every_live_family,
        the_wrapper_set_is_exactly_the_non_inv_names_of_every_live_family,
        the_fp_abort_set_is_exactly_the_fp_touching_names_that_have_not_landed,
        no_cqrt_rotation_symbol_appears_anywhere_in_the_emitted_shim,
        the_shim_enums_match_the_opcodes_the_grid_reaches,
        every_family_gets_its_own_file_and_only_its_own_symbols,
        the_generated_files_on_disk_are_up_to_date,
        each_of_the_twenty_same_size_transpositions_turns_the_gate_red,
        the_composite_swap_that_really_does_abort_all_214_controlled_is_caught,
        a_single_wrapper_demoted_to_an_abort_turns_the_gate_red,
        a_family_width_that_has_not_landed_cannot_be_declared_live,
        the_check_mode_sees_a_tampered_file,
    ]))
