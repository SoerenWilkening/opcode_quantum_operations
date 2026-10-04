#!/bin/sh
# ctest_names.sh — what a `ctest -R` gate actually SELECTS, by name, grouped by
# binary (bd luk, 2026-09-28).
#
#     sh tools/ctest_names.sh BUILD_DIR REGEX [EXCLUDE_REGEX]
#
# Runs `ctest --test-dir BUILD_DIR -N -R REGEX [-E EXCLUDE_REGEX]` and prints
# every selected test NAME under its binary, a per-binary count and a total.
# Exit 0 when at least one test is selected, 1 when ZERO are, 2 on a usage
# error or a ctest failure. `-N` only lists — nothing is built or run — so a
# configured but unbuilt tree lists the same names; ctest's "Could not find
# executable" noise on such a tree is filtered out.
#
# WHY IT EXISTS. A -R regex is a STRING MATCH over the registered names, not a
# coverage claim in either direction. add_cqops_death_test registers one test
# per case, named `binary.case` (cmake/CqopsTest.cmake), so a regex matches
# words inside CASE names too. Measured twice in one batch (2026-09-17):
#
#   OVER-SELECTION is LOUD, but only to someone who reads the names. bd 8u0's
#   gate `shift_var|step14|template` picked up
#   test_runtime_tape_death.a_token_as_a_template_operand through the word
#   "template" in a case name; it surfaced only as a (Not Run).
#
#   ZERO-SELECTION is SILENT AND GREEN. bd 7b5's prescribed `ctest -R childcap`
#   matches no test — the controls are tests/test_childcap_controls.inc, inside
#   test_runtime_gate. The bead quotes "100% tests passed, 0 tests failed out
#   of 0"; ctest 4.3.2 here prints "No tests were found!!!" instead, and BOTH
#   exit 0 (measured 2026-09-28; `-N` exits 0 on "Total Tests: 0" too). Hence
#   this script's exit 1. At RUN time, ctest's own `--no-tests=error` fails a
#   zero selection (rc 8 here) — it cannot see an over-selection.
#
# Read the NAMES, not the total: a stray case shows up as a binary nobody meant
# to select. The binary is the part of a name before its first `.`; a name with
# no `.` is its own binary. Death-case binaries were the only dotted names until
# 2026-10-04, when add_cqops_test's SPLIT (bd 5mqp) began registering
# `binary.case` and `binary.case.IofK` entries for an ordinary suite; the part
# before the first `.` is still the binary, so the grouping is unchanged.
#
# POSIX sh + awk, like check_loc.sh and check_cites.sh. Deliberately defines no
# shell function, so tools/function_map.py has nothing to index here.

set -eu

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ] || [ -z "$2" ]; then
    echo "usage: sh tools/ctest_names.sh BUILD_DIR REGEX [EXCLUDE_REGEX]" >&2
    echo "       (REGEX must be non-empty; to list everything, pass '.')" >&2
    exit 2
fi

dir=$1
regex=$2

if [ ! -f "$dir/CTestTestfile.cmake" ]; then
    echo "ctest_names: '$dir' has no CTestTestfile.cmake — configure it first" >&2
    exit 2
fi

if [ "$#" -eq 3 ]; then
    set -- ctest --test-dir "$dir" -N -R "$regex" -E "$3"
else
    set -- ctest --test-dir "$dir" -N -R "$regex"
fi

if ! out=$("$@" 2>&1); then
    printf '%s\n' "$out" >&2
    echo "ctest_names: ctest failed: $*" >&2
    exit 2
fi

# The command line travels through ENVIRON, not `awk -v`: -v would apply
# escape processing to a regex's backslashes before printing it.
printf '%s\n' "$out" | CTEST_NAMES_CMD="$*" awk '
    /^ *Test +#[0-9]+: / {
        name = $0
        sub(/^ *Test +#[0-9]+: /, "", name)
        bin = name
        sub(/\..*$/, "", bin)
        if (!(bin in n))
            order[++nbin] = bin
        n[bin]++
        names[bin] = names[bin] "    " name "\n"
        total++
    }
    END {
        cmd = ENVIRON["CTEST_NAMES_CMD"]
        if (total == 0) {
            print "ctest_names: ZERO tests selected by: " cmd > "/dev/stderr"
            print "ctest_names: a gate on this regex is VACUOUS — plain ctest " \
                  "runs nothing and still exits 0" > "/dev/stderr"
            exit 1
        }
        for (i = 1; i <= nbin; i++)
            printf "%s  (%d)\n%s", order[i], n[order[i]], names[order[i]]
        printf "total: %d test(s) in %d binar%s, selected by: %s\n", total, nbin,
               (nbin == 1 ? "y" : "ies"), cmd
    }'
