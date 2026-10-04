#!/usr/bin/env python3
# gen_reasons.py — M27, the REASON TABLES: the sentence every still-aborting
# `fp` / `defer` body prints (bead ar9, 2026-09-24).
#
# THIS IS THE CUT shim/gen_shim.py's HEADER RECORDED at bead 9ve.44: "the
# REASON TABLES (DEFER_REASON, INTRINSIC_DEFER_REASON, DECLINED) to
# shim/gen_reasons.py — they are the one part of this file that grows with
# every refusal rather than with a schema". Taken when bead ar9 had to add two
# more of them to a file at 288 counted lines. gen_shim.py keeps the BUCKET
# rule (`bucket_of`); this file only decides the TEXT of an abort, and nothing
# here may move a row between buckets — so no audit figure and no .gen.c file
# moves when a sentence changes. Import is one-way: gen_shim imports this.
#
# THE DOCTRINE IS gen_bodies' OWN RECORDED LESSON, which cost 119 symbols a
# falsehood once: THE REASON A BODY PRINTS MUST BE TRUE OF THAT BODY. The
# retired `fp` bucket sentence, "fp is v2", was true of NO row by 2026-09-24:
# v2 SHIPPED (at f64, PRD-v2 §1), so at f64 it pointed a caller at a release
# that has happened, and at f16/f32/f80 it promised a release that v2's
# f64-only scope (PRD-v2 §1a) will never make — those widths wait on UPSTREAM
# (PRD-v2 §6.2 f80, §6.3 f32), not on a port here. The maintainer decided the
# replacement on bead ar9 ("fix f64 AND reword all"), and it is below.

# --- The `fp` bucket ----------------------------------------------------------
# EVERY `fp` ROW GETS A SENTENCE FROM `fp_reason`, AND gen_bodies HAS NO DEFAULT
# LEFT: `gen_bodies.REASON` carries no `fp` key, so an `fp` row that reached the
# renderer with `reason=None` is a KeyError naming it rather than a stale
# sentence printed at runtime.
#
# A KEY IN DECLINED IS NOT LANDED AND CARRIES ITS OWN ABORT TEXT, including on
# its `_inv`: D14's sentence attaches to a LANDED family-width, and this pair
# has not landed at all, so its `_inv` is not a D14 row.
DECLINED = {
    ("uitofp", "i64", "f64"):
        "uitofp i64 -> f64 is REFUSED, not unported: upstream routes UIToFP to "
        "soft_sitofp with no bias correction at this width, so every u >= 2^63 "
        "would convert as a negative number (bead 9ve.34, PRD-v2 7.9)",
}

# THE STILL-ABORTING ROWS THAT TOUCH f64, PER OPCODE (PRD-v2 §7.9, bead ar9).
# DECLINED-shaped — the bucket stays `fp` — and each one TRUE of its body:
#   frem     no construction: the pinned snapshot has no `soft_frem` (its only
#            `frem` is extract/errors.jl's opcode-name table) and the corpus
#            calls it zero times, so it stays an abort on LIVENESS (§3.3).
#            NO ROW REACHES IT since bd n4ut (2026-10-04): the re-vendored
#            opcode_table.yaml (7307995f…, CQ_lang 61999c4a) has no `frem`, so
#            no body prints it. KEPT because the generator does not require
#            its removal; deleting it would make a returning `frem` f64 row
#            stop at the SystemExit below until someone writes its reason.
#   fpext /  every one of these rows ALSO touches f16/f32/f80, and needs a
#   fptrunc  rail at that width, which v2's f64-only scope cannot mint. The
#            text names no `cqrt_*` symbol: M27 emits none, by test.
# `%(other)s` is the row's non-f64 fp width, `%(src)s -> %(dst)s` its pair.
_WIDEN = ("%(op)s %(src)s -> %(dst)s stays an abort: it needs an %(other)s "
          "rail, and v2 is f64-only, so no %(other)s rail can be allocated "
          "(PRD-v2 7.9)")
F64_REASON = {
    "frem": "frem f64 stays an abort on liveness: the pinned Bennett snapshot "
            "has no frem construction (no soft_frem) and the corpus calls it "
            "zero times (PRD-v2 3.3, 7.9)",
    "fpext": _WIDEN,
    "fptrunc": _WIDEN,
}

# THE DEFAULT, AT f16/f32/f80 ONLY. It NAMES the width, because the width is
# the whole reason: the opcode may well ship at f64 (`fadd_f32` aborts while
# `fadd_f64` is live). A cast touching two such widths names both.
_OUT_OF_SCOPE = ("%s is outside v2's f64-only scope; that width is an upstream "
                 "dependency, not a pending port (PRD-v2 1a, 6.2/6.3)")
_OUT_OF_SCOPE_2 = ("%s and %s are outside v2's f64-only scope; those widths "
                   "are an upstream dependency, not a pending port "
                   "(PRD-v2 1a, 6.2/6.3)")


def fp_reason(opcode, key, widths, domains):
    if key in DECLINED:
        return DECLINED[key]
    fpw = [w for w in widths if domains[w] == "fp"]
    if "f64" in fpw:
        # NO SILENT DEFAULT AT f64. A new unlanded row touching f64 must have
        # its sentence WRITTEN DOWN here; the width default would be false.
        if opcode not in F64_REASON:
            raise SystemExit(
                "gen_shim: %r touches f64, is not LANDED, and has no written "
                "reason; add it to gen_reasons.F64_REASON (or DECLINED)"
                % (key,))
        other = [w for w in fpw if w != "f64"]
        return F64_REASON[opcode] % {"op": opcode, "src": widths[0],
                                     "dst": widths[-1],
                                     "other": "/".join(other)}
    if len(fpw) == 1:
        return _OUT_OF_SCOPE % fpw[0]
    return _OUT_OF_SCOPE_2 % tuple(fpw)


# --- The `defer` bucket -------------------------------------------------------
# Rows that abort for a reason that is NEITHER D14 NOR an fp width: printing an
# fp sentence at `cq_template_ctpop_i32` would be a falsehood of exactly the
# 119-symbol kind — an INTEGER opcode at an INTEGER width. `defer` is a fourth
# BUCKET (gen_shim.bucket_of) rather than a fourth reason inside `fp`, because
# the bucket is what the audit counts.
DEFER_REASON = {
    "libm": "lrint/llrint have no soft_lrint upstream and ZERO corpus calls; "
            "they are composable as soft_round then soft_fptosi the day a "
            "caller appears (PRD-v2 7.9)",
}

# THE INTRINSIC TABLE'S REASON IS PER OPCODE SINCE bead 9ve.44, AND THE OLD ONE
# IS GONE RATHER THAN NARROWED ("a v1-shaped port that has not run yet" is true
# of no row since M41/M42 ran). A KeyError here is the failure for an integer
# intrinsic row that defers with no reason written down.
_FUNNEL = ("fshl/fshr are REFUSED, not unported: Bennett's expansion "
           "(instructions.jl:4947-4996) is not LLVM's funnel shift at a shift "
           "congruent to 0 mod W, so porting it is a known miscompile and "
           "correcting it is a re-derivation (bead 9ve.29.3, K26.md section 6)")
_SAT = ("the saturating add/sub intrinsics have NO construction in the pinned "
        "Bennett snapshot, so Rule 1 forbids writing one (K27.md, Not ported)")
INTRINSIC_DEFER_REASON = {"fshl": _FUNNEL, "fshr": _FUNNEL,
                          "uadd_sat": _SAT, "sadd_sat": _SAT,
                          "usub_sat": _SAT, "ssub_sat": _SAT}


def defer_reason(opcode, source):
    if source == "intrinsic":
        return INTRINSIC_DEFER_REASON[opcode]
    return DEFER_REASON[source]
