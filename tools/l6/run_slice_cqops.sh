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
# THE CURRENT LINK LINE (PRD-v2 §6.1, bead 9ve.24) IS:
#   s2.o  l6_residue.o  libcqops.a  [libcqops dependencies]
# libcqops defines all 2,820 cq_template_* symbols (2,880 until bd n4ut's
# 2026-10-04 re-vendor removed `frem`). No CQ_lang template object
# is extracted or linked; `libcq_runtime.a` is absent too. The link map below
# checks that no CQ_lang archive supplied an object.
#
# WHY THE PROVENANCE ARM EXISTS — measured 2026-08-27 against CQ_lang 134e625,
# BEFORE the intrinsic/libm symbols were vendored. The old line extracted
# cq_intrinsic_templates.c.o and cq_libm_templates.c.o from libcq_templates.a
# and linked them before libcqops.a. Naming the WHOLE archive was dangerous:
# opposite archive orders both linked with exit 0 but produced different answers
# (7 vs -1), because cq_templates.c.o is CQ_lang's trace-only stub. That is
# `bd 216` checklist item 20's silent link failure. The old two objects defined
# 401 symbols disjoint from libcqops.a's then 2850 symbols; this is historical
# measurement, not today's link contract.
#
# usage:
#   run_slice_cqops.sh CLANG OPT LLVMLINK PASS INC SRC LIBSRCS CQOPS UNUSED OUT
# LIBSRCS is a space-separated list of companion .c files, empty for the
# single-file shape.
set -euo pipefail
CLANG=$1 OPT=$2 LLVMLINK=$3 PASS=$4 INC=$5 SRC=$6 LIBSRCS=$7 CQOPS=$8 OBJDIR=$9
OUT=${10}
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
REPO=$(CDPATH='' cd -- "$HERE/../.." && pwd)

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
#
# $CQOPS_L6_LDLIBS IS THE ARCHIVE'S OWN DEPENDENCIES AND IT GOES AFTER IT, WHICH
# IS A DIFFERENT JOB FROM $LDX. Measured 2026-08-28 at Step 25: an archive
# configured with -DCQOPS_QEC_DIR= carries M25's `sink_qec.c.o`, whose
# `qec_create` / `qec_cx` / `qec_rz` / … are undefined until libqec.a,
# libtommath.a and libcjson.a are on the line — so EVERY L6 fixture stops at the
# LINK stage against a QEC-enabled build, with an error that names libcqops
# rather than the missing archives. It is empty for a build without the library,
# which is why nothing saw it at Step 24 and why the two steps disagree about
# whether this line works. CMake fills it from ${CQOPS_QEC_LIBS}; the order is
# load-bearing (a static archive resolves what precedes it), so these go LAST.
#
# $CQOPS_L6_LINK_CC IS THE COMPILER THAT BUILT THE ARCHIVE, AND IT IS NOT
# NECESSARILY $CLANG. Stages 1-2 must use CQ_LANG's clang — it owns `CQ.h` and
# the pass plugin — but stage 3 links against OUR archive, and a sanitizer
# runtime is pulled in by the DRIVER. Measured 2026-08-28: since `bd 6wg` gave
# Debug its own ASan-capable compiler, linking a Debug archive with CQ_lang's
# clang-19 produces a binary that dies with SIGILL (rc 132) before `main` —
# CLAUDE.md's "a Debug binary that SIGILLs at startup is the ASan runtime, not
# our code", arriving here as a MIXED runtime rather than a broken one. Defaults
# to $CLANG, which is what Release wants and what every earlier run used; CMake
# points it at ${CMAKE_C_COMPILER}. The newer clang emits `-Woverride-module`
# on CQ_lang's triple and compiles the LLVM-19 textual IR fine.
LDX=${CQOPS_L6_LDFLAGS:-}
LDLIBS=${CQOPS_L6_LDLIBS:-}
LINKCC=${CQOPS_L6_LINK_CC:-$CLANG}
set -f
# shellcheck disable=SC2086  # $LDX is an intentional word-split
"$LINKCC" -c $LDX "$TMP/s2.ll" -o "$TMP/s2.o" 2>> "$OUT.link"
# `bd c55`. THE HARNESS'S OWN RESIDUE READER, and it is OURS rather than the
# library's: cqops_read_residue is a pure read, and registering an atexit dump
# is the CALLER's decision to take. See tools/l6/l6_residue.c. It is inert
# unless $CQOPS_L6_RESIDUE names a path at RUN time, which is why it is on every
# link line here and costs the L7 entry that shares this runner nothing.
# shellcheck disable=SC2086
"$LINKCC" -c $LDX -I"$REPO/include" "$HERE/l6_residue.c" \
         -o "$TMP/l6_residue.o" 2>> "$OUT.link"
# THE TWO CQ_lang STUB OBJECTS ARE GONE FROM THIS LINE (PRD-v2 §6.1, bead
# 9ve.24), AND THEIR ABSENCE IS THE POINT OF THE VENDORING. Until 2026-09-19
# libcqops left `intrinsic_table.yaml`'s 389 and `libm_table.yaml`'s 12
# `cq_template_*` symbols undefined, so every fixture that called `ctlz`,
# `ctpop`, `bswap`, `fshl` or `fma` needed CQ_lang's own trace-only bodies —
# whose minting arms call `cqrt_alloc_handle`, which PRD §15 D16 makes a loud
# abort. §6.1's decision was to OWN those symbols; the archive now defines all
# 2,820, so passing the stub objects as well is a DUPLICATE-SYMBOL hard error.
# Loud, at the link stage, and the good failure mode — but it has to go in the
# same commit as the definitions, which is why this edit is not optional.
#
# The L6, L7 and bit-level drivers also skip extraction.
# shellcheck disable=SC2086
"$LINKCC" $LDX "$TMP/s2.o" "$TMP/l6_residue.o" "$CQOPS" $LDLIBS \
         -Wl,-map,"$OUT.map" -o "$OUT.bin" 2>> "$OUT.link"
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
#
# $CQOPS_L6_RESIDUE IS SET HERE AND NOWHERE ELSE, so the residue file sits
# beside the run's other artefacts and `--reuse` re-reads it with them. A
# fixture that ABORTS writes none — abort() runs no destructor — and that
# absence is the honest report for a program that never reached its end.
set +e
CQOPS_L6_RESIDUE="$OUT.residue" "$OUT.bin" > "$OUT.out" 2> "$OUT.err"
rc=$?
set -e
echo "$rc" > "$OUT.rc"
exit "$rc"
