#!/bin/sh
# check_cites.sh — the citation guard (bd 0a7, 2026-09-10). Rule 12's taste
# applied to prose: enforced by `make lint`, not by discipline.
#
# FAILS on a citation into a LIVING document that is a bare line number. The
# four living targets are PRD-v1.md, IMPLEMENTATION_PLAN.md, NORTH_STAR.md and
# CLAUDE.md — the documents this repo edits every week, where a line number
# rots in about three commits.
#
# Measured 2026-09-10: PRD-v1.md has taken 21 commits and grown 484 -> 2814
# lines since 2026-08-14; the K-docs make 53 line citations into it, counted at
# 961905f, and `bd 0a7` measures the ones it checked as displaced by +95..+183
# lines and moved again since. Re-measuring is not the fix and has been tried
# four times — K08, K09, K11 and K12 re-anchored by hand, all four stale again.
#
# THE ADOPTED FORM IS HYBRID: a section, a quoted phrase, and a line number
# legal ONLY when pinned to a commit SHA, because `git show <sha>:FILE | sed -n
# Np` reproduces a pinned line forever.
#
#     PRD §3, "flip the constant in place" (PRD-v1.md:377 @ 961905f)
#
# The guard is narrower than the form — nothing mechanical can ask for a
# section or a quote; it asks only that a line number carry its SHA.
#
# NOT FLAGGED, each deliberately:
#   third_party/bennett/CLAUDE.md:27  the snapshot is PINNED and read-only, so
#   bennett/CLAUDE.md:27              its lines cannot rot. Only the spelled-out
#                                     path passes: drop the path and the cite is
#                                     ambiguous with OUR CLAUDE.md, and is a hit.
#   docs/labreport/                   APPEND-ONLY: a committed entry is never
#                                     edited, so a citation that has rotted
#                                     since is a RECORD of what was true then.
#                                     Forcing it to change is the in-place edit
#                                     the lab report exists to forbid.
#   third_party/  build*/  .beads/    not ours, or generated.
#
# LIMIT — the ANCHORED form only: a second number written bare, as a lone
# `:432`, has nothing in front of it to key on and reads as prose. Cite in full.
#
# Scanned: *.md at the repo root, docs/constructions/*.md, docs/*.txt,
# CMakeLists.txt, cmake/*.cmake, and *.c *.h *.py *.inc *.sh *.cmake under the
# five roots check_loc.sh walks — WHOLE files, not comments only: a bare
# citation in a string literal rots exactly as one in a comment does, and
# measured today every code-side hit is in a comment anyway.

set -eu

root="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$root"

# The two PINNED-target prefixes and the optional ` @ <sha>` are folded INTO
# the pattern rather than written as a lookahead: POSIX ERE is leftmost-longest
# in both BSD and GNU grep, so an occurrence carrying a prefix or a SHA matches
# WITH it and is dropped by the two filters below. A lookahead needs PCRE,
# which BSD grep does not have.
TARGETS='(PRD-v1|IMPLEMENTATION_PLAN|NORTH_STAR|CLAUDE)\.md'
RANGE='((-|–)[0-9]+)?'   # ASCII hyphen AND en dash; the K-docs use both
PIN='[[:space:]]*@[[:space:]]*[0-9a-f]{7,40}'
# `plan:NNN` is the shorthand bd 0a7's own notes use. Measured 2026-09-10 it
# occurs ZERO times in the scanned set; it is here so it cannot arrive later.
# (Spelling it with a real number would correctly make this script its own
# first hit — hence every example above is pinned or has its number elided.)
CITE="((third_party/)?(bennett/)?${TARGETS}|plan):[0-9]+${RANGE}"
PAT="${CITE}(${PIN})?"

list_files() {
    find . -maxdepth 1 -type f -name '*.md'
    if [ -d docs/constructions ]; then
        find docs/constructions -maxdepth 1 -type f -name '*.md'
    fi
    if [ -d docs ]; then
        find docs -maxdepth 1 -type f -name '*.txt'
    fi
    [ -f CMakeLists.txt ] && echo ./CMakeLists.txt
    if [ -d cmake ]; then
        find cmake -maxdepth 1 -type f -name '*.cmake'
    fi
    for d in src include tests shim tools; do
        [ -d "$d" ] || continue
        find "$d" -type f \( -name '*.c' -o -name '*.h' -o -name '*.py' \
             -o -name '*.inc' -o -name '*.sh' -o -name '*.cmake' \)
    done
}

files=$(list_files \
        | sed 's,^\./,,' \
        | grep -vE '^(third_party|build[^/]*|\.beads|docs/labreport)/' \
        | sort -u)

if [ -z "$files" ]; then
    echo "check_cites: no documents or sources to scan"
    exit 0
fi
nl='
'
all=''
scanned=0

for f in $files; do
    scanned=$((scanned + 1))
    out=$(grep -noE "$PAT" "$f" 2>/dev/null \
          | grep -vE "@[[:space:]]*[0-9a-f]{7,40}\$" \
          | grep -v 'bennett/' || true)
    if [ -n "$out" ]; then
        entry=$(printf '%s\n' "$out" | sed "s|^\([0-9][0-9]*\):|${f}:\1: |")
        all="${all}${entry}${nl}"
    fi
done

if [ -n "$all" ]; then
    printf '%s' "$all"
    n=$(printf '%s' "$all" | wc -l | tr -d ' ')
    echo "check_cites: FAIL — $n bare line citation(s) into a living document"
    echo "             a line number is legal only when pinned to a commit (bd 0a7):"
    echo "             PRD §3, \"flip the constant in place\" (PRD-v1.md:377 @ 961905f)"
    exit 1
fi

echo "check_cites: OK — $scanned file(s) scanned, 0 bare living-target line citations"
