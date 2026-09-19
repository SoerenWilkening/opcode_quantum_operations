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
# identification: 20 same-size variant transpositions leave every bucket figure
# exact while swapping a live family for an aborting one, and neither Step 23's
# `nm` nor Step 24's corpus can notice.
#
# SPLIT SEAM: TAKEN 2026-09-19 (bead 9ve.36) — `the ASSERTIONS <-> the
# PROVOCATIONS` -> tests/test_gen_shim_provoked.py. The NEGATIVE CONTROLS that
# make every assertion here one somebody has SEEN fail live there; see the note
# beside `CASES` at the foot of this file.

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
# A FOURTH BUCKET SINCE PRD-v2 §6.1's VENDORING (bead 9ve.24). `defer` is
# everything that aborts for a reason that is neither D14 nor "fp is v2" — the
# integer `llvm.*` intrinsics (bead 9ve.29) and libm's `lrint`/`llrint`
# (§7.9). It is a BUCKET rather than a third reason inside `fp` because the
# bucket is what the audit counts and what each `.gen.c` banner prints, so
# folding 249 symbols into `fp` would make every published fp figure wrong.
DEFER = {n for n in ABI if sc.is_defer(n) and not sc.is_landed(n)}
LIVE = {n for n in ABI if not sc.is_deferred_fp(n) and n not in DEFER}
EXPECT = {"wrapper": {n for n in LIVE if not n.endswith("_inv")},
          "inv": {n for n in LIVE if n.endswith("_inv")},
          "fp": {n for n in ABI if sc.is_deferred_fp(n) and n not in DEFER},
          "defer": DEFER}
# The reading a reader and a reviewer actually want, kept as SETS so a family
# that moved domain is named rather than counted. SEVEN populations now: the
# three new ones are what makes the vendoring visible at all.
BY_DOMAIN = {
    "int_wrapper": {n for n in EXPECT["wrapper"] if not sc.is_fp(n)},
    "int_inv":     {n for n in EXPECT["inv"] if not sc.is_fp(n)},
    "int_defer":   {n for n in DEFER if not sc.is_fp(n)},
    "fp_wrapper":  {n for n in EXPECT["wrapper"] if sc.is_fp(n)},
    "fp_inv":      {n for n in EXPECT["inv"] if sc.is_fp(n)},
    "fp_defer":    {n for n in DEFER if sc.is_fp(n)},
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
    # THREE PAIRS SINCE THE VENDORING, EACH CHECKED AGAINST ITS OWN YAML.
    for yaml_path, manifest, n_want in sc.SOURCES:
        sha = hashlib.sha256(open(yaml_path, "rb").read()).hexdigest()
        rec = sc.manifest_yaml_sha(manifest)
        sc.check(sha == rec,
                 "%s was extracted from a DIFFERENT %s:\n"
                 "  pinned yaml : %s\n  manifest    : %s\n"
                 "  Re-extract from CQ_lang's regenerated header; do not edit "
                 "either." % (os.path.basename(manifest),
                              os.path.basename(yaml_path), sha, rec))
    sc.check(len(ABI) == 2880,
             "the three manifests hold %d declarations, expected 2880"
             % len(ABI))
    # AND THEY ARE PAIRWISE DISJOINT, which is what makes 2479 + 389 + 12 a
    # SUM rather than an upper bound. sc.manifest_decls asserts it on every
    # load; this restates it as the claim the grid's size rests on.
    sc.check(sum(n for _, _, n in sc.SOURCES) == 2880,
             "the three manifests' own counts do not sum to 2880")


def the_emitted_symbol_set_equals_the_abi_in_both_directions():
    sc.diff_sets(set(SHIPPED), set(ABI), "emitted symbol set")


def every_emitted_signature_matches_cq_langs_own_declaration():
    # C LINKS A WRONG SIGNATURE SILENTLY AND `nm` CANNOT SEE IT (bd 819). This
    # is the strongest oracle Step 22 will ever have, and it is why signature()
    # lives on the grid side of the seam: it runs with no body emitted.
    bad = ["%s\n     ours: %s\n   CQ_lang: %s" % (n, SHIPPED[n][0], ABI[n])
           for n in sorted(ABI) if n in SHIPPED and SHIPPED[n][0] != ABI[n]]
    sc.check(not bad, "%d signature mismatch(es):\n  %s" % (len(bad), "\n  ".join(bad[:3])))


def the_partition_is_1122_wrappers_668_inv_841_fp_and_249_deferred():
    # 992 / 603 / 884 until PRD-v2's first fp family landed (2026-09-18), then
    # 1048 / 631 / 800, then 1112 / 668 / 699 at bead 9ve.36. Bead 9ve.24's
    # vendoring ADDS 401 symbols rather than moving any: 10 land as wrappers
    # (`fma` x8 and `sqrt` x2 at f64), 142 join the fp aborts, and 249 are the
    # new fourth bucket.
    got = {k: len(v) for k, v in buckets(SHIPPED).items()}
    sc.check(got == {"wrapper": 1122, "inv": 668, "fp": 841, "defer": 249},
             "PRD §1 + §15 D14 + PRD-v2 §1 + §6.1 partition: expected "
             "1122/668/841/249, got %r" % got)


def the_domain_split_is_992_603_237_130_65_and_12():
    # A COUNT IS NOT AN IDENTIFICATION, AND THE THREE COARSE BUCKETS NOW HIDE A
    # DOMAIN MOVE. Once a family-width lands, `wrapper` holds integer AND fp
    # wrappers, so 1048/631/800 stays exact while, say, `fp_arith`/`f64` goes
    # live and `int_arith` loses the same number — which the case above cannot
    # see and this one names. The expected side is the ABI's own spelling
    # crossed with `is_landed`; gen_shim's audit reaches the same five numbers
    # down a different route (the yaml's `widths` map and `family` key).
    got = buckets(SHIPPED)
    for key, want in sorted(BY_DOMAIN.items()):
        dom, bucket = key.split("_", 1)
        have = {n for n in got[bucket] if (sc.is_fp(n) == (dom == "fp"))}
        sc.diff_sets(have, want, "%s set" % key)
    sc.check([len(BY_DOMAIN[k]) for k in
              ("int_wrapper", "int_inv", "int_defer",
               "fp_wrapper", "fp_inv", "fp_defer")]
             == [992, 603, 237, 130, 65, 12],
             "the domain split moved: %r"
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
    # 189 of these carry an integer width in their NAME as well — the
    # cross-domain casts — so they look integer-ish and are not (PRD §1). It was
    # 240 until bead 9ve.36 landed the seventeen shipped conversion pairs, and
    # pinning it beside the coarse totals is what would catch a landing that
    # swept a cross-domain cast into the live set: `fcmp` and `fp_arith` carry
    # no integer width token, so only this number moves for that defect.
    got = buckets(SHIPPED)["fp"]
    sc.diff_sets(got, EXPECT["fp"], "fp abort set")
    crossdomain = {n for n in got if any("_i%d" % b in n for b in (1, 8, 16, 32, 64, 80, 128))}
    sc.check(len(crossdomain) == 189,
             "expected 189 integer-ish fp symbols, got %d" % len(crossdomain))
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
    # AND THE OPCODE-GRAINED HALF OF THE KEY, WHICH IS WHAT bead 9ve.36 ADDED.
    # FORTY-FIVE f64 SYMBOLS ARE STILL ABORTS and the census is the assertion: a
    # `LANDED` key widened back to `(family, width)` would take `frem` and
    # `fneg` with `fadd` (they are all `fp_arith`), and a cast key widened from
    # `(opcode, from, to)` to `(opcode, width)` would take `uitofp i64` with the
    # other four `uitofp` rows. Both are exactly the rows this line names, and
    # neither moves any figure above by a number a reader could attribute.
    f64_left = sorted(n for n in got if "_f64" in n)
    by_op = {}
    for n in f64_left:
        by_op.setdefault(n.split("cq_template_")[1].split("_")[0], 0)
        by_op[n.split("cq_template_")[1].split("_")[0]] += 1
    # M38's TEN OPCODES JOINED THAT CENSUS AT THE VENDORING (bead 9ve.24) and
    # they are the half a reader is likeliest to misread: `trunc` here is the
    # intrinsic table's fp round-toward-zero at f64, NOT `opcode_table.yaml`'s
    # integer cast, which is `trunc_i64_to_i32` and carries no `_f64` token at
    # all. They are bead 9ve.27's and abort until it runs. `fma` and `sqrt` are
    # ABSENT from this census, which is the assertion that they LANDED.
    sc.check(by_op == {"frem": 15, "fneg": 3, "uitofp": 3,
                       "fpext": 9, "fptrunc": 9, "bitcast": 6,
                       "ceil": 2, "copysign": 6, "fabs": 2, "floor": 2,
                       "fmax": 4, "fmin": 4, "nearbyint": 2, "rint": 2,
                       "round": 2, "trunc": 2},
             "the f64 symbols still aborting are not the expected census: %r"
             % by_op)
    sc.check("fma" not in by_op and "sqrt" not in by_op,
             "an f64 `fma` or `sqrt` symbol is still in the `fp is v2` bucket; "
             "bead 9ve.24 landed both")


def the_three_declined_symbols_are_in_the_fp_bucket_with_their_own_reason():
    # BEAD 9ve.34's ROW, AND IT IS A THIRD REASON RATHER THAN A FOURTH BUCKET.
    # `uitofp i64 -> f64` is not landed, so all three of its symbols abort; but
    # `"fp is v2"` would be a FALSEHOOD PRINTED AT RUNTIME, because f64 is in
    # v2's scope and seventeen sibling conversion pairs already ship. Keeping it
    # in the `fp` bucket is what leaves every count and every `.gen.c` banner
    # unmoved, so the only thing that can go wrong is the sentence — which is
    # what this case reads, out of the emitted body rather than the generator.
    got = buckets(SHIPPED)["fp"]
    declined = {n for n in SHIPPED if sc.is_declined(n)}
    sc.check(len(declined) == 3,
             "expected exactly 3 declined symbols, got %d" % len(declined))
    sc.check(declined <= got,
             "a declined symbol left the `fp` bucket: %s"
             % sorted(declined - got))
    for n in sorted(declined):
        text = "\n".join(SHIPPED[n][1])
        sc.check('"%s"' % sc.DECLINED_REASON in text,
                 "%s does not carry bead 9ve.34's reason: %s" % (n, text))
        sc.check('"%s"' % sc.FP_REASON not in text and
                 '"%s"' % sc.INV_REASON not in text,
                 "%s carries a second, wrong reason as well" % n)
    # AND NOTHING ELSE CARRIES IT. A reason that leaked onto a sibling row would
    # tell a caller a width is refused when it is merely unported.
    leaked = {n for n, (_d, b) in SHIPPED.items()
              if sc.DECLINED_REASON in "\n".join(b)} - declined
    sc.check(not leaked, "bead 9ve.34's reason leaked onto %s" % sorted(leaked)[:4])


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
    #
    # AND THE SAME SPLIT ONE FAMILY OVER SINCE 2026-09-19 (bead 9ve.36): an fp
    # `fadd` and an integer `add` share `kind == "binary"`, and a cross-domain
    # `sitofp` and an integer `zext` share `kind == "cast"`. Both pairs reach
    # different entry points over different enums, so the arms are split by the
    # yaml's own `domain` and cast `kind` and the four prefixes are checked
    # pairwise disjoint rather than assumed — they differ at ONE letter each.
    for a, b in (("CQ_SHIM_FPRED_OEQ", "CQ_SHIM_PRED_"),
                 ("CQ_SHIM_FOP_FADD", "CQ_SHIM_OP_"),
                 ("CQ_SHIM_FCAST_FPTOSI", "CQ_SHIM_CAST_")):
        sc.check(not a.startswith(b),
                 "%r starts with %r; the two arms would merge and stop "
                 "discriminating" % (a, b))
    xdom = ("int_to_fp", "fp_to_int")
    for prefix, want in (
            ("CQ_SHIM_OP_", {r.opcode for r in live
                             if r.kind == "binary" and r.domain != "fp"}),
            ("CQ_SHIM_FOP_", {r.opcode for r in live
                              if r.kind == "binary" and r.domain == "fp"}),
            ("CQ_SHIM_PRED_", {r.pred for r in live if r.opcode == "icmp"}),
            ("CQ_SHIM_FPRED_", {r.pred for r in live if r.opcode == "fcmp"}),
            ("CQ_SHIM_CAST_", {r.opcode for r in live
                               if r.kind == "cast" and r.family not in xdom}),
            ("CQ_SHIM_FCAST_", {r.opcode for r in live
                                if r.kind == "cast" and r.family in xdom})):
        got = {t[len(prefix):].lower() for t in header.split() if t.startswith(prefix)}
        got = {t.rstrip(",}") for t in got}
        sc.diff_sets(got, want, "%s* enum" % prefix)

    # THE UNARY ENUM IS THE ONE ARM THE GRID CANNOT SUPPLY, AND IT IS ASSERTED
    # AS AN ABSENCE. `cq_shim_fun_op` holds `fsqrt`, which is `llvm.sqrt` in
    # CQ_lang's intrinsic_table.yaml and so reaches NO row of this grid (PRD §1:
    # M27 generates from `opcode_table.yaml` only); the yaml's one unary opcode
    # is `fneg`, which has no kernel and stays an abort. So the live set is
    # EMPTY and the header must nonetheless carry exactly one enumerator — the
    # door is built, tested by name, and unreached. A `CQ_SHIM_FUN_FNEG`
    # appearing here would mean `fneg` had been landed without its kernel.
    fun = {t[len("CQ_SHIM_FUN_"):].rstrip(",}").lower()
           for t in header.split() if t.startswith("CQ_SHIM_FUN_")}
    sc.check(fun == {"fsqrt"}, "cq_shim_fun_op is %r, expected {'fsqrt'}" % fun)
    sc.check(not [r for r in live if r.kind == "unary"],
             "a unary opcode went live in the grid; gen_bodies.ENTRY has no "
             "`funary` row, so this cannot be right")


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
    # ALL THREE GRIDS, because the vendoring added three families and a check
    # built from `opcode_table.yaml` alone would call them strays.
    rows = gen_shim.expand(gen_shim.load(sc.YAML)[0])
    itab = gen_shim.load(sc.SOURCES[1][0])[0]
    ltab = gen_shim.load(sc.SOURCES[2][0])[0]
    gen_shim.gen_intrinsics.expand_intrinsic(itab, gen_shim._emitter(rows, itab["widths"]))
    gen_shim.gen_intrinsics.expand_libm(ltab, gen_shim._emitter(rows, ltab["widths"]))
    want = {"cq_template_%s.gen.c" % f for f in {r.family for r in rows}}
    sc.diff_sets(set(sc.read_dir(sc.GENERATED)), want, "emitted file set")
    sc.check(len(want) == 13, "expected 13 opcode families, got %d" % len(want))
    # AND EACH FILE IS FED BY EXACTLY ONE YAML, which `render` refuses to
    # violate because a banner can only name one provenance. Stated here too:
    # the banner is prose and nothing else reads it.
    src_of = {}
    for r in rows:
        src_of.setdefault(r.family, set()).add(r.source)
    mixed = sorted(f for f, v in src_of.items() if len(v) != 1)
    sc.check(not mixed, "family/families fed by more than one yaml: %s" % mixed)
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


# --- Negative controls: GONE, on the seam this file now records --------------
#
# `_rerender`, `_reds` and the four provocation cases moved to
# tests/test_gen_shim_provoked.py on 2026-09-19 (bead 9ve.36), at the
# `the ASSERTIONS <-> the PROVOCATIONS` line — the SAME cut
# tests/test_gen_bodies.py took a day earlier, and forced by the same kind of
# measurement: this file reached 303 counted lines of Rule 12's 300 WALL when
# the bead added a declined-reason case, two finer-key provocations and three
# more enum arms. Python has no `.inc` escape hatch (check_loc.sh counts every
# line of a multi-line string as code), so the split is a fourth registered
# ctest entry and not a second file on the same one.
#
# THE IMPORT IS ONE-WAY and the sibling names these cases BY NAME, so a case
# renamed here is an ImportError there rather than a silently unprovoked
# assertion. `CASES` is exported for the same reason at one remove: a case
# ADDED here and forgotten in a hand-kept list over there would simply never be
# provoked, and nothing would say so.
CASES = [
    the_manifest_is_the_expansion_of_the_pinned_yaml,
    the_emitted_symbol_set_equals_the_abi_in_both_directions,
    every_emitted_signature_matches_cq_langs_own_declaration,
    the_partition_is_1122_wrappers_668_inv_841_fp_and_249_deferred,
    the_domain_split_is_992_603_237_130_65_and_12,
    the_inv_abort_set_is_exactly_the_inv_names_of_every_live_family,
    the_wrapper_set_is_exactly_the_non_inv_names_of_every_live_family,
    the_fp_abort_set_is_exactly_the_fp_touching_names_that_have_not_landed,
    the_three_declined_symbols_are_in_the_fp_bucket_with_their_own_reason,
    no_cqrt_rotation_symbol_appears_anywhere_in_the_emitted_shim,
    the_shim_enums_match_the_opcodes_the_grid_reaches,
    every_family_gets_its_own_file_and_only_its_own_symbols,
    the_generated_files_on_disk_are_up_to_date,
]


if __name__ == "__main__":
    sys.exit(sc.run(CASES))


