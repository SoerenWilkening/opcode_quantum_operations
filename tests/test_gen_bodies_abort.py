#!/usr/bin/env python3
# test_gen_bodies_abort.py — the ABORT CLAIMS half of Step 22's bodies gate,
# on the seam tests/test_gen_bodies.py recorded in its own header BEFORE this
# file was needed:
#
#     the WRAPPER claims <-> the ABORT claims
#
# TAKEN 2026-09-24 (bead 6y9) at the recorded TRIGGER of 260, measured at 284
# counted lines of Rule 12's 300. Python has no `.inc` escape hatch
# (check_loc.sh counts every line of a multi-line string as code), so the seam
# is a new FILE and a sixth registered ctest entry — which must also be listed
# in .github/workflows/ci.yml's two registration loops, or it could stop
# running with nothing to say so.
#
# THE DISCRIMINATOR IS WHICH BUCKET A CASE READS. Everything in the sibling
# reads the WRAPPER bucket (one call each, and exactly the call its symbol
# names); everything here reads the ABORT bucket — that each abort body names
# itself and silences its parameters, and that the sentence it prints is TRUE
# of it. This is the side that grows as PRD-v2 §7.15's remaining families land
# and the `fp` bucket's sentences change.
#
# A PURE MOVE: both cases and the `aborts` helper are verbatim. The declined
# rows of the sibling's `the_new_fp_shapes_are_pinned_body_by_body` stayed
# there with the rest of that case — splitting it would have renamed a case.
#
# THE IMPORT IS ONE-WAY and brings the sibling's parsed dictionary, so both
# entries read the same `shim/generated/` parse; the sibling names nothing
# here.
#
# NEXT SEAM: none recorded yet — at 81 counted lines this file has headroom.

import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "support"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shimcheck as sc  # noqa: E402
import test_gen_bodies as gb  # noqa: E402

SHIPPED = gb.SHIPPED


def aborts(defs):
    return {n: b for n, (_d, b) in defs.items() if sc.bucket_of_body(b) != "wrapper"}


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
    # which it landed in. An fp sentence on an integer symbol would be a FALSE
    # statement, not merely a vague one.
    defs = defs or SHIPPED
    # THE DISCRIMINATOR IS `is_deferred_fp`, NOT `is_fp`, SINCE 2026-09-18 — and
    # the difference is a FALSEHOOD PRINTED AT RUNTIME either way round. A landed
    # family's `_inv` carries an fp width token and is refused for D14's reason,
    # not for an fp reason: `cq_template_fcmp_oeq_f64_inv` saying "fp is v2"
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
    # integer intrinsics cited bead 9ve.29 and the two libm opcodes cite §7.9;
    # since bead 9ve.44 the integer rows that still defer cite their OWN
    # reasons (9ve.29.3 for the funnel shifts, K27 for the saturating four).
    for name, body in aborts(defs).items():
        if sc.is_declined(name):
            want = sc.DECLINED_REASON
        elif sc.is_defer(name):
            # THREE DEFER REASONS SINCE bead 9ve.44, each read off the stem:
            # `fshl`/`fshr` REFUSED (9ve.29.3), `*_sat` unported (K27), libm.
            want = sc.defer_reason_of(name)
        else:
            # bead ar9: the `fp` reason is per NAME (its width, or the f64
            # row's own §7.9 reason) — sc.fp_reason_of, typed independently.
            want = (sc.fp_reason_of(name) if sc.is_deferred_fp(name)
                    else sc.INV_REASON)
        sc.check('"%s"' % want in "\n".join(body),
                 "%s: abort reason is not %r" % (name, want))
    # THE FOUR REASONS ARE PAIRWISE DISTINCT, which is what makes
    # `bucket_of_body` a partition rather than a first-match.
    fp_texts = {sc.fp_reason_of(n) for n in aborts(defs) if sc.is_deferred_fp(n)}
    reasons = [sc.INV_REASON] + list(sc.DEFER_REASONS)
    sc.check(len(set(reasons)) == len(reasons) and not fp_texts & set(reasons),
             "two abort reasons are the same string")
    # bead ar9's CENSUS of the `fp` sentences, by class (measured off the
    # generator 2026-09-24): 15 frem f64, 18 fpext/fptrunc f64, 3 declined,
    # 768 width default (250 at each single width + 18 two-width casts).
    census = {}
    for n in aborts(defs):
        if sc.is_deferred_fp(n) and not sc.is_defer(n):
            k = sc.fp_reason_of(n)
            k = ("width" if "outside v2's" in k else k.split(" ")[0])
            census[k] = census.get(k, 0) + 1
    sc.check(census == {"frem": 15, "fpext": 9, "fptrunc": 9, "uitofp": 3,
                        "width": 768}, "fp reason census moved: %r" % census)
    deferred = {n for n in aborts(defs) if sc.is_defer(n)}
    sc.check(len(deferred) == 132,
             "expected 132 deferred aborts, got %d" % len(deferred))
    # AND bead 9ve.29's OLD SENTENCE IS ON NO BODY: it said the integer
    # intrinsics were "a v1-shaped port that has not run yet", which is true
    # of no row now — the landed ones run and the rest are refused/unported.
    stale = sorted(n for n, (_d, b) in defs.items()
                   if "v1-shaped port that has not run yet" in "\n".join(b))
    sc.check(not stale, "bead 9ve.29's stale reason survives on %s" % stale[:4])
    declined = {n for n in aborts(defs) if sc.is_declined(n)}
    sc.check(len(declined) == 3,
             "expected 3 declined aborts, got %d" % len(declined))
    sc.check(sc.DECLINED_REASON != sc.INV_REASON,
             "the declined reason is D14's")
    landed_inv = {n for n in aborts(defs) if sc.is_landed(n)}
    # 68 since bead s8g: + the two bitcast f64 <-> i64 `_inv`.
    sc.check(len(landed_inv) == 68,
             "expected 68 D14 aborts inside the landed fp families, got %d"
             % len(landed_inv))
    sc.check(all(n.endswith("_inv") for n in landed_inv),
             "a LANDED fp symbol aborts without being an `_inv`: %s"
             % sorted(n for n in landed_inv if not n.endswith("_inv"))[:4])
    # bead ar9 RETIRED PRD §1's "fp is v2" from the generated grid: no body at
    # any width may print it (true of none since v2 shipped at f64).
    stale = sorted(n for n, (_d, b) in defs.items()
                   if sc.RETIRED_FP_REASON in "\n".join(b))
    sc.check(not stale, "\"fp is v2\" survives on %s" % stale[:4])
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


CASES = [
    every_abort_body_names_its_own_symbol_and_silences_its_parameters,
    the_two_abort_reasons_are_distinct_and_neither_lies_about_its_bucket,
]


if __name__ == "__main__":
    sys.exit(sc.run(CASES))
