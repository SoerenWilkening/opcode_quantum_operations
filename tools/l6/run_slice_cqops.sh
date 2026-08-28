#!/usr/bin/env bash
# tools/l6/run_slice_cqops.sh — Step 24 (L6). ONE CQ_lang e2e fixture, lowered
# by CQ_lang's own pass and linked against libcqops INSTEAD of CQ_lang's
# trace-only runtime stub.
#
# THE GATE IS `LINK, RUN, AND DO NOT ABORT` — PRD §15 D18, NORTH_STAR condition
# 1 verbatim. There is deliberately NO `diff` here and one must never be added:
# CQ_lang's .expected.log goldens are ITS regression oracle for ITS OWN IR pass,
# captured against a "trace-only runtime stub" that this link replaces. The two
# measured reasons they are not an oracle in ANY column live in PRD §15 D18 —
# every stub measure body prints a LITERAL so all its measure lines read `-> 0`
# whatever the circuit computes, and our handle numbering diverges on purpose
# because `cqrt_addc` and D7b mint rails the ABI does not name.
#
# STAGES 1-3 MIRROR CQ_lang/tests/e2e/run_slice{,_multi}.sh. Stage 2b (its
# free-pairing check) is deliberately NOT run: it is a check on CQ_lang's
# lowered IR, it is already green in CQ_lang's own suite, and
# `third_party/cq_free_pairing/` is READING MATERIAL that Rule 1 forbids us to
# execute or add to any build.
#
# THE LINK LINE IS THE SHARP EDGE, AND IT IS `bd 216` CHECKLIST ITEM 20.
# CQ_lang/runtime/CMakeLists.txt builds ONE `cq_templates` archive out of
# cq_templates.c + cq_intrinsic_templates.c + cq_libm_templates.c. Naming that
# archive here would put a SECOND definition of all 2479 grid symbols on the
# line, and the failure is SILENT: measured, the same two archives in opposite
# order both exit 0 with DIFFERENT ANSWERS (7 vs -1). So the intrinsic and libm
# members are taken as OBJECTS (`ar x`) and `libcq_runtime.a` is never on the
# line at all. Measured 2026-08-27 against CQ_lang 134e625: those two objects
# define exactly 401 symbols — PRD §1's "deliberately not ours" — and their
# intersection with libcqops.a's 2850 defined symbols is EMPTY.
#
# usage:
#   run_slice_cqops.sh CLANG OPT LLVMLINK PASS INC SRC LIBSRCS CQOPS OBJDIR OUT
# LIBSRCS is a space-separated list of companion .c files, empty for the
# single-file shape.
set -euo pipefail
CLANG=$1 OPT=$2 LLVMLINK=$3 PASS=$4 INC=$5 SRC=$6 LIBSRCS=$7 CQOPS=$8 OBJDIR=$9
OUT=${10}
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

# Stage 1 — frontend. The companion TUs are compiled WITHOUT `-include CQ.h`,
# exactly as run_slice_multi.sh does it.
"$CLANG" -I"$INC" -include CQ.h -O1 -emit-llvm -S "$SRC" -o "$TMP/cq.ll"
linkin=("$TMP/cq.ll")
i=0
set -f
for lib in $LIBSRCS; do
    set +f
    "$CLANG" -I"$INC" -O1 -emit-llvm -S "$lib" -o "$TMP/lib_$i.ll"
    linkin+=("$TMP/lib_$i.ll")
    i=$((i + 1))
    set -f
done
set +f

if [ "${#linkin[@]}" -gt 1 ]; then
    "$LLVMLINK" -S "${linkin[@]}" -o "$TMP/s1.ll"
else
    cp -f "$TMP/cq.ll" "$TMP/s1.ll"
fi

"$OPT" -load-pass-plugin "$PASS" -passes=cq-lowering -S "$TMP/s1.ll" \
       -o "$TMP/s2.ll"                                                # Stage 2

# Stage 3 — link. No -O flag, on CQ_lang's own `bd szpr` pin: the lowered IR is
# the verified artifact and an optimised stage 3 could const-fold or reorder the
# classical remainder out from under the run.
# $CQOPS_L6_LDFLAGS is how the DEBUG archive is linked: it is built with
# -fsanitize=undefined, and the sanitizer runtime is pulled in by the DRIVER at
# link time, not by the archive. Without it the link fails on ___ubsan_handle_*.
# Empty for Release, which is the default.
LDX=${CQOPS_L6_LDFLAGS:-}
set -f
# shellcheck disable=SC2086  # $LDX is an intentional word-split
"$CLANG" -c $LDX "$TMP/s2.ll" -o "$TMP/s2.o"
# shellcheck disable=SC2086
"$CLANG" $LDX "$TMP/s2.o" "$OBJDIR/cq_intrinsic_templates.c.o" \
         "$OBJDIR/cq_libm_templates.c.o" "$CQOPS" \
         -Wl,-map,"$OUT.map" -o "$OUT.bin" 2> "$OUT.link"
set +f

# THE PROVENANCE ARM (item 20). The map turns "it linked" into "it linked
# against the archive we meant": no object on the map may come from a CQ_lang
# archive, because that is exactly what a silently-shadowed grid symbol looks
# like from the outside.
# The verdict is written as a FILE, not carried in the exit code alone, so that
# re-reading a completed run (l6_run.py --reuse) sees the same fact the first
# pass did. An exit code does not survive the run.
if grep -Eq 'libcq_templates\.a|libcq_runtime\.a' "$OUT.map"; then
    echo "L6: PROVENANCE FAILURE — a CQ_lang archive is on the link map" \
        | tee "$OUT.prov" >&2
    exit 3
fi

# Stage 4 — RUN. The status is written to a file, and that is not bookkeeping:
# `abort()` is SIGABRT, which a shell reports as 134 and a `subprocess`
# returncode reports as -6, while a LINK failure never reaches this line at all.
# Recording the run's own status separately is what lets the caller tell "did
# not link" from "ran and aborted" — and the second is the one outcome this gate
# exists to detect.
set +e
"$OUT.bin" > "$OUT.out" 2> "$OUT.err"
rc=$?
set -e
echo "$rc" > "$OUT.rc"
exit "$rc"
