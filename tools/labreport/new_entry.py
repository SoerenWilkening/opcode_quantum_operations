#!/usr/bin/env python3
"""Start a lab-report entry: emit the GENERATED header, leave the prose to a human.

The split is the whole design. Everything this script writes is EXTRACTED —
SHA range, commit subjects, beads, ctest counts, LOC, diffstat — because a fact
that reaches the report through someone's memory is the `bd j75` failure waiting
to happen. Everything it leaves blank is interpretation, which no script can
supply and which is the only reason the document is worth reading.

    python3 tools/labreport/new_entry.py               # since the last entry
    python3 tools/labreport/new_entry.py --since <sha>

APPEND-ONLY: this refuses to overwrite an existing entry. A correction to an
earlier entry is a NEW entry carrying \\supersedes — never an edit, because a
rendered PDF is not something anyone diffs.
"""
import argparse, datetime, json, os, re, subprocess, sys

ROOT     = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SESSIONS = os.path.join(ROOT, "docs", "labreport", "sessions")


def sh(*cmd):
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


def tex(s):
    for a, b in (("\\", "\\textbackslash{}"), ("&", "\\&"), ("%", "\\%"),
                 ("$", "\\$"), ("#", "\\#"), ("_", "\\_"), ("{", "\\{"),
                 ("}", "\\}"), ("~", "\\textasciitilde{}"), ("^", "\\textasciicircum{}")):
        s = s.replace(a, b)
    return s


def entries():
    if not os.path.isdir(SESSIONS):
        return []
    return sorted(f for f in os.listdir(SESSIONS) if re.match(r"\d{4}-.*\.tex$", f))


def last_to_sha():
    """The `to` SHA of the newest entry — where this one picks up."""
    e = entries()
    if not e:
        return ""
    for line in open(os.path.join(SESSIONS, e[-1])):
        m = re.search(r"%\s*labreport-to:\s*(\S+)", line)
        if m:
            return m.group(1)
    return ""


def ctest_count(tree):
    out = sh("ctest", "--test-dir", tree, "-N")
    m = re.search(r"Total Tests:\s*(\d+)", out)
    return m.group(1) if m else "--"


def beads(flag, since):
    out = sh("bd", "list", flag, since, "--json", "--brief", "--all")
    try:
        data = json.loads(out) if out else []
    except json.JSONDecodeError:
        return []
    rows = data.get("issues", data) if isinstance(data, dict) else data
    return [(i.get("id", "?").split("-")[-1], i.get("title", "")) for i in rows]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--since", default="", help="start SHA (default: last entry's end)")
    ap.add_argument("--title", default="", help="entry title")
    a = ap.parse_args()

    os.makedirs(SESSIONS, exist_ok=True)
    today = datetime.date.today().isoformat()
    num   = len(entries()) + 1
    path  = os.path.join(SESSIONS, "%04d-%s.tex" % (num, today))
    if os.path.exists(path):
        sys.exit("labreport: %s exists — an entry is never rewritten (append-only). "
                 "A correction is a NEW entry with \\supersedes." % os.path.relpath(path, ROOT))

    since = a.since or last_to_sha()
    head  = sh("git", "rev-parse", "--short", "HEAD")
    rng   = "%s..%s" % (since, head) if since else head
    log   = sh("git", "log", "--pretty=format:%h %s", "%s..HEAD" % since) if since else ""
    stat  = sh("git", "diff", "--stat", "%s..HEAD" % since) if since else ""
    dirty = sh("git", "status", "--porcelain")
    loc   = sh("tools/check_loc.sh")

    L = []
    w = L.append
    w("%% labreport entry %d — %s" % (num, today))
    w("%% labreport-to: %s" % head)
    w("%%")
    w("%% APPEND-ONLY. Once this entry is committed it is never edited: a later")
    w("%% correction is a NEW entry carrying \\supersedes{%d}{field}." % num)
    w("")
    w("\\entry{%d}{%s}{%s}" % (num, today, tex(a.title or "TITLE")))
    w("")
    w("\\begin{generated}")
    w("  \\gitem{Range}{\\texttt{%s}}" % tex(rng))
    if dirty:
        w("  \\gitem{Tree}{\\emph{dirty at capture} --- %d path(s) uncommitted}"
          % len(dirty.splitlines()))
    w("  \\gitem{Tests}{Release \\textbf{%s}, Debug \\textbf{%s} (\\texttt{ctest -N})}"
      % (ctest_count("build-release"), ctest_count("build-debug")))
    w("  \\gitem{LOC}{%s}" % tex(loc.replace("check_loc: ", "")))
    L2 = [l for l in log.splitlines() if l.strip()]
    if L2:
        w("  \\gitem{Commits}{\\begin{commitlist}")
        for c in L2:
            h, _, s = c.partition(" ")
            w("    \\item \\texttt{%s} %s" % (tex(h), tex(s)))
        w("  \\end{commitlist}}")
    else:
        w("  \\gitem{Commits}{none in range at capture}")
    for label, flag in (("Beads closed", "--closed-after"), ("Beads filed", "--created-after")):
        rows = beads(flag, today)
        w("  \\gitem{%s}{%s}" % (label,
          ", ".join("\\bead{%s} %s" % (tex(i), tex(t)) for i, t in rows) or "none"))
    if stat:
        w("  \\gitem{Diff}{\\texttt{%s}}" % tex(stat.splitlines()[-1].strip()))
    w("\\end{generated}")
    w("")
    w("%% ---- Hand-written below. Omit any field the session did not earn; a")
    w("%% ---- one-bead session is a paragraph, not a page. Fields 1, 2 and 6 are")
    w("%% ---- the ones that are never omitted.")
    w("")
    for f, hint in (
        ("The ask", "One or two sentences: what was asked for."),
        ("What landed", "What is now true that was not. Not a narration of steps."),
        ("Measured", "Use \\begin{figures} ... \\fig{value}{instrument}{configuration}. "
                     "A number without its instrument does not go in."),
        ("Decided", "The D-number or plan section ONLY. Never restate a decision's content."),
        ("Not taken", "What was refused or deferred, and why. Without this the next "
                      "session re-proposes it."),
        ("Verified", "Rule 17, literally: name the layers and configurations that RAN. "
                     "Never an unqualified green claim."),
        ("Left open", "Beads filed and what a next session would pick up."),
    ):
        w("\\field{%s}" % f)
        w("%% %s" % hint)
        w("")

    open(path, "w").write("\n".join(L) + "\n")
    print("labreport: %s" % os.path.relpath(path, ROOT))
    print("labreport: add \\input to docs/labreport/labreport.tex, then `make labreport`")
    return 0


if __name__ == "__main__":
    sys.exit(main())
