#!/usr/bin/env python3
"""tools/l6/l6_report.py — Step 24 (L6): THE VERDICT HALF.

Split out of `l6_run.py` on 2026-09-10 (`bd wgi`) along the seam
IMPLEMENTATION_PLAN §3's Layer 6 table recorded in advance, `the FIXTURE LIST ↔
the VERDICT`. Nothing here is new: every line below was in `l6_run.py` and is
moved verbatim, EXCEPT two positional words the move itself falsified -- a
`below` and an `above` that pointed at code now on the other side of the seam.
Both are corrected in place and both carry what they used to say, because a
shipped comment asserting something false is the one defect no test in this
project can see.

THE DISCRIMINATOR IS WHICH REPOSITORY A LINE BREAKS WITH, and it is what makes
this a seam rather than a line-count dodge. Everything in this file breaks with
THIS repository: `DEFERRED` is the exact text `cq_shim_unsupported` prints
(`shim/cq_shim_ctx.c`), `STRAND` is D15 §3's one-shot line (`src/reg.c`),
`RESIDUE_FIELDS` is `cqops_read_residue`'s six-field read
(`include/cqops/cqops.h`), and `residue_summary`'s self-check is `src/reg.h`'s
own identity. Nothing here reads CQ_lang, runs a compiler, or knows what a
fixture is. The half left behind breaks with CQ_lang and with nothing else.

THE IMPORT IS ONE WAY AND MUST STAY THAT WAY. `l6_run.py` imports this module;
this module imports nothing of the repository's. `l6_run.py` ends in
`sys.exit(main())` at module level -- importing IT runs the corpus -- so the
dependency cannot be reversed even accidentally, and a report helper that
reached back for a fixture path would be re-crossing the seam.

A CASUALTY IS NOT A FAILURE. Anything reaching a v1-deferred symbol aborts
loudly and BY DESIGN (PRD 1 and PRD 15 D16), so the driver CLASSIFIES rather
than merely passing or failing, and it matches on our own abort text so a
casualty is never reported as an unexplained abort. Measured 2026-08-27 against
CQ_lang 134e625, the four reasons that actually fire are `fp is v2`, `qram is
v2`, `tape is v2`, and `cqrt_alloc_handle` -- the last being D16's refusal to
mint a register-less handle, which is what stops the purely-INTEGER
intrinsic-bearing fixtures. Not one casualty was an integer `_inv` body.
`tape is v2` STOPPED FIRING on 2026-09-02 (PRD 15 D23 put the tape in scope),
and `qram is v2` stopped later the same day (PRD 15 D24 put QRAM in scope at
all nine widths — the fp-width qram fixtures now stop at `fp is v2` instead);
if either ever appears again, a shim file has regressed, not the corpus.

THE RESIDUE IS READ ALONGSIDE THE GATE AND IS NEVER A GATE (PRD 15 D18). Until
`bd c55` the only thing observable from outside a fixture was whether D15 3's
one-shot strand line had fired -- a BOOLEAN, where the split shipped TWO GRAINS
that genuinely disagree. `cqops_read_residue` is the public read, and
`l6_residue.c` -- the HARNESS's translation unit, on the link line, never the
library's -- writes it beside the run's other artefacts. A fixture that ABORTS
writes none, because abort() runs no destructor; that absence is reported as
absence and never as zeroes.
"""
import re


# OUR OWN LOUD v1 DEFERRAL, and it is matched on the EXACT text
# `cq_shim_unsupported` prints (shim/cq_shim_ctx.c) -- not on a guess. It is
# deliberately NOT the `libcqops: FATAL: <layer>:` house shape every other hard
# error in the library uses, so a regex written from that shape matches nothing
# and every casualty reads as an unexplained ABORT.
DEFERRED = re.compile(r"^cqops: (\S+) not implemented \((.*)\)", re.M)
STRAND = re.compile(r"strand", re.I)


def classify(ran, rc, err, prov):
    """LINK / RUN / ABORT, plus the v1-deferred row.

    `ran` is False when the fixture never reached stage 4 -- a front-end,
    lowering or LINK failure, which is a different fact from "it ran and
    aborted" and must never be folded into it. 128+SIGABRT is 134 in a shell's
    status, and that is the one outcome this gate exists to detect.
    """
    if prov:
        return "PROVENANCE"
    if not ran:
        return "BUILD_FAIL"
    if rc == 0:
        return "OK"
    if DEFERRED.search(err):
        return "DEFERRED"        # a v1 boundary, loud and by design
    if rc >= 128:
        return "ABORT"
    return "RUN_FAIL"


RESIDUE_FIELDS = ("stranded_dirty", "stranded_unproven", "frees_dirty",
                  "frees_unproven", "stranded_qubits", "strand_reports")


def residue(path):
    """The six-field read `tools/l6/l6_residue.c` wrote, or None.

    NONE IS NOT ZEROES. A fixture that aborted, or that never linked, ran no
    destructor and so has no final residue at all; reporting it as a row of
    zeroes would say "this program leaked nothing", which is a claim nobody
    made. Every consumer distinguishes the two: `residue_totals` and
    `residue_summary` below, and `l6_run.py`'s row loop, which this sentence
    could call BELOW too until the 2026-09-10 split (`bd wgi`) moved it across
    the seam.
    """
    text = read(path)
    if not text.strip():
        return None
    out = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0] in RESIDUE_FIELDS:
            try:
                out[parts[0]] = int(parts[1])
            except ValueError:
                return None
    return out if len(out) == len(RESIDUE_FIELDS) else None


def residue_totals(rows):
    """Corpus totals over the fixtures that actually reported one."""
    seen = [r["residue"] for r in rows if r["residue"]]
    t = {k: sum(x[k] for x in seen) for k in RESIDUE_FIELDS}
    t["fixtures_reporting"] = len(seen)
    t["fixtures_with_residue"] = sum(1 for x in seen if x["stranded_qubits"])
    return t


def residue_summary(rows):
    """THE SENTENCE D15 3's SPLIT WAS BUILT TO MAKE (`bd c55`).

    IT IS AN OBSERVATION AND NOT A GATE, exactly as PRD 15 D18 requires -- the
    verdict above is LINK / RUN / DO NOT ABORT and nothing here feeds it. What
    it adds is resolution: "the certificate discharged every rail in N of M
    fixtures" was already sayable from the boolean, and "and in the others it
    left X qubits convicted and Y merely unproven" was not.

    THE TWO GRAINS ARE PRINTED SEPARATELY AND ARE NOT ADDED TOGETHER. A mixed
    rail contributes to both qubit rows and to the rail-level DIRTY row alone,
    so a single figure would discard exactly the rail the split exists for.
    """
    t = residue_totals(rows)
    n = t["fixtures_reporting"]
    if not n:
        return ("  residue    no fixture reported one — every run aborted or "
                "failed to link before its reader could write")
    clean = n - t["fixtures_with_residue"]

    # THE ONE THING THE HARNESS CAN CHECK ABOUT ITSELF, and it is src/reg.h's
    # own assertion: every strand increments a qubit row beside the strand it
    # describes, so the two rows sum to the pool's total. A mismatch here is
    # NOT a fixture leaking — it is `l6_residue.c` having mislabelled a field or
    # the library's free path stranding somewhere that did not go through
    # cq_reg_free, and either way this report is lying. It is printed and not
    # gated: PRD §15 D18 is explicit that the residue is read ALONGSIDE the gate
    # and never as one, and the GATE owns the exit code -- `l6_run.py`'s, which
    # this line read as "the verdict above" until the 2026-09-10 split.
    bad = [r["name"] for r in rows if r["residue"]
           and r["residue"]["stranded_qubits"] !=
               r["residue"]["stranded_dirty"] + r["residue"]["stranded_unproven"]]
    warn = ("" if not bad else
            f"\n             !! the two qubit rows do not sum to the pool total "
            f"in {len(bad)} fixture(s): {', '.join(bad[:4])} — this report is "
            f"not trustworthy")
    return (
        f"  residue    {clean} of {n} fixtures that ran to the end stranded "
        f"nothing\n"
        f"             qubits: {t['stranded_dirty']} convicted, "
        f"{t['stranded_unproven']} merely unproven "
        f"(pool total {t['stranded_qubits']})\n"
        f"             frees:  {t['frees_dirty']} landed on the dirty row, "
        f"{t['frees_unproven']} on the unproven row" + warn)


def read(p):
    try:
        with open(p, "r", errors="replace") as fh:
            return fh.read()
    except OSError:
        return ""


def first_line(s):
    for l in s.splitlines():
        if l.strip():
            return l.strip()[:200]
    return ""


def first_error(s):
    for l in s.splitlines():
        if "error" in l.lower() or "Undefined" in l:
            return l.strip()[:200]
    return ""
