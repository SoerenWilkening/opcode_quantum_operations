#!/usr/bin/env python3
"""Start a lab-report entry: emit the GENERATED header, leave the prose to a human.

The split is the whole design. Everything this script writes is EXTRACTED —
SHA range, commit subjects, beads, ctest counts, LOC, diffstat — because a fact
that reaches the report through someone's memory is the `bd j75` failure waiting
to happen. Everything it leaves blank is interpretation, which no script can
supply and which is the only reason the document is worth reading.

    python3 tools/labreport/new_entry.py               # since the last entry
    python3 tools/labreport/new_entry.py --since <sha>
    python3 tools/labreport/new_entry.py --release-tree <dir> --debug-tree <dir>

THE TESTS ROW NAMES THE TREE IT COUNTED AND THE OPT-INS THAT MOVE THAT COUNT
(bd bj0). Until 2026-09-17 the two build directories were string literals at the
call site, so the row reported whatever `build-release`/`build-debug` happened to
be configured with, whether or not the session had run them -- and entry 8 is the
measured case: it reads Release 359 / Debug 360 while that session's only
full-suite run, in scratch trees configured with neither opt-in flag, was Release
348 / Debug 349. A hardcoded path defeats "extracted, never typed" more quietly
than a typed number does, because the figure LOOKS extracted and named no tree.

Three things changed, and the third is the one that makes the number MEAN
something. The trees are arguments (defaulting to the old literals, so
`make labreport-entry` is unaffected); the row NAMES each tree, which is exactly
the fix bd a9e made to check_loc.sh's OK line for the same reason -- a figure
inviting a comparison it cannot support; and the row carries each tree's opt-in
flags, read out of that tree's own CMakeCache.txt. The four opt-in suites (L6,
L7 §12(1) and the two QEC sink suites) move this count BY DESIGN, so a count
without its flags is not a figure at all. CLAUDE.md's Session Completion rule 3
already requires an instrument AND a configuration of every hand-written figure;
the generated header was carrying the instrument alone.

A tree that cannot be counted SAYS SO and names the reason -- it never prints a
plausible number, and it never requires the tree to exist.

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


OPTIN = ("CQOPS_CQLANG_DIR", "CQOPS_QEC_DIR")


def path_tex(p):
    """A path that can WRAP. \\texttt neither hyphenates nor breaks at a slash,
    so an out-of-tree path -- which is exactly what a session running in scratch
    trees has to record -- would otherwise run off the page."""
    return tex(p).replace("/", "/\\allowbreak ")


def cache(tree):
    """A build tree's own CMakeCache.txt, or None if it is not a tree at all."""
    try:
        fh = open(os.path.join(ROOT, tree, "CMakeCache.txt"))
    except OSError:
        return None
    with fh:
        rows = (re.match(r"([A-Za-z_0-9]+):[A-Z]+=(.*)", l.strip()) for l in fh)
        return {m.group(1): m.group(2) for m in rows if m}


def tree_note(tree, want_type):
    """The configuration half of the figure: the opt-ins that move the count.

    Reported per tree because the two may disagree, and `(missing)` because a
    tree configured against a path that has since gone away still holds the
    CTestTestfile.cmake from when it resolved -- which is how entry 8's row came
    to name four suites nothing in that session could have run."""
    c = cache(tree)
    bits = []
    got = c.get("CMAKE_BUILD_TYPE", "")
    if got and got != want_type:
        bits.append("cache says \\textbf{%s}" % tex(got))
    for k in OPTIN:
        v = c.get(k, "")
        if v:
            bits.append("\\texttt{%s}=\\texttt{%s}%s" % (tex(k), path_tex(v),
                        "" if os.path.isdir(v) else " \\emph{(missing)}"))
    return ", ".join(bits) or "no opt-in flags"


def ctest_count(tree, want_type):
    """`N --- tree, flags`, or a STATED reason there is no number.

    The failure arms are the point: a generator that dies, or that quietly
    prints a plausible figure, when a named tree is missing or unconfigured is
    the same defect wearing different clothes."""
    if not os.path.isdir(os.path.join(ROOT, tree)):
        why = "no such tree"
    elif cache(tree) is None:
        why = "not a configured tree"
    else:
        m = re.search(r"Total Tests:\s*(\d+)", sh("ctest", "--test-dir", tree, "-N"))
        if m:
            return "\\textbf{%s} --- \\texttt{%s}, %s" % (
                m.group(1), path_tex(tree), tree_note(tree, want_type))
        why = "\\texttt{ctest -N} gave no count"
    return "\\emph{not counted} (%s: \\texttt{%s})" % (why, path_tex(tree))


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
    ap.add_argument("--release-tree", default="build-release",
                    help="the Release tree whose ctest -N the entry reports")
    ap.add_argument("--debug-tree", default="build-debug",
                    help="the Debug tree whose ctest -N the entry reports")
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
    w("  \\gitem{Tests}{\\texttt{ctest -N}. Release %s. Debug %s.}"
      % (ctest_count(a.release_tree, "Release"),
         ctest_count(a.debug_tree, "Debug")))
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
