#!/usr/bin/env bash
# tools/bitlevel/run.sh — tools/l6/run_slice_cqops.sh with three changes for
# the one-opcode experiment: the -O level and extra -D flags are parameters,
# every intermediate .ll is KEPT beside the output (the whole point is to read
# what clang did before the pass saw the program), and pool_probe.o joins the
# link line. Stages, link order and the provenance arm are the runner's;
# read its header for why each is the way it is.
#
# usage: run.sh CLANG OPT PASS INC SRC OLEVEL "DEFS" CQOPS OBJDIR OUT
set -euo pipefail
CLANG=$1 OPT=$2 PASS=$3 INC=$4 SRC=$5 OLEVEL=$6 DEFS=$7 CQOPS=$8 OBJDIR=$9
OUT=${10}
HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
REPO=$(CDPATH='' cd -- "$HERE/../.." && pwd)
LDX=${CQOPS_L6_LDFLAGS:-}
LDLIBS=${CQOPS_L6_LDLIBS:-}
LINKCC=${CQOPS_L6_LINK_CC:-$CLANG}

now() { perl -MTime::HiRes=time -e 'printf "%.4f\n", time'; }   # ~10 ms itself
set -f
t0=$(now)
# shellcheck disable=SC2086
"$CLANG" -I"$INC" -include CQ.h "-O$OLEVEL" $DEFS -emit-llvm -S "$SRC" \
         -o "$OUT.s1.ll"                                                # 1
t1=$(now)
"$OPT" -load-pass-plugin "$PASS" -passes=cq-lowering -S "$OUT.s1.ll" \
       -o "$OUT.s2.ll" 2> "$OUT.pass.err"                                # 2
t2=$(now)
# shellcheck disable=SC2086
"$LINKCC" -c $LDX "$OUT.s2.ll" -o "$OUT.o" 2>> "$OUT.link"                # 3
t3=$(now)
printf "clang %s\npass %s\ncc %s\n" "$(echo "$t1 - $t0" | bc)" \
       "$(echo "$t2 - $t1" | bc)" "$(echo "$t3 - $t2" | bc)" > "$OUT.times"
# shellcheck disable=SC2086
"$LINKCC" -c $LDX -I"$REPO/include" "$REPO/tools/l6/l6_residue.c" \
         -o "$OUT.residue.o" 2>> "$OUT.link"
# shellcheck disable=SC2086
"$LINKCC" -c $LDX -std=c11 -I"$REPO/src" -I"$REPO/shim" -I"$REPO/include" \
         "$HERE/pool_probe.c" -o "$OUT.probe.o" 2>> "$OUT.link"
# shellcheck disable=SC2086
# The two CQ_lang stub objects are gone from this line for the reason
# tools/l6/run_slice_cqops.sh states at length (PRD-v2 §6.1, bead 9ve.24):
# libcqops defines all 2,880 cq_template_* symbols now, so passing them as well
# is a duplicate-symbol hard error.
"$LINKCC" $LDX "$OUT.o" "$OUT.residue.o" "$OUT.probe.o" \
         "$CQOPS" $LDLIBS -Wl,-map,"$OUT.map" -o "$OUT.bin" 2>> "$OUT.link"
set +f
if grep -Eq 'libcq_templates\.a|libcq_runtime\.a' "$OUT.map"; then
    echo "PROVENANCE FAILURE — a CQ_lang archive is on the link map" \
        | tee "$OUT.prov" >&2
    exit 3
fi

# 4 — RUN, twice out of one artefact: quantum (θ = π/2), then classical (π).
# Timing is the DRIVER's (repeated direct runs, min of N): a `python3 -c`
# launch here measured ~2 s on this box against an 18 ms binary.
set +e
CQOPS_SINK=printf CQOPS_L6_RESIDUE="$OUT.q.residue" CQOPS_BL_POOL="$OUT.q.pool" \
    "$OUT.bin" > "$OUT.q.out" 2> "$OUT.q.err"
rc=$?
echo "$rc" > "$OUT.q.rc"
CQOPS_SINK=printf CQOPS_L6_RESIDUE="$OUT.c.residue" CQOPS_BL_POOL="$OUT.c.pool" \
    "$OUT.bin" classical > "$OUT.c.out" 2> "$OUT.c.err"
echo "$?" > "$OUT.c.rc"
set -e
exit "$rc"
