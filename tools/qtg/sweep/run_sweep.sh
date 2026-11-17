#!/bin/zsh
# run_sweep.sh — the QTG factory-size sweep, end to end.
#
#   zsh tools/qtg/sweep/run_sweep.sh <work-dir> [hardware-tree] [sizes] [factories]
#
# Builds a Release libqec from <hardware-tree>/qec, compiles one driver per instance
# size against build-release/libcqops.a, and runs every (size, F) arm IN PARALLEL.
# Counts are deterministic — wall clock is a ranking only, never a result (bd 97s).
#
# COST: an n=4 arm is ~2 min; an n=20 arm is ~80 min and ~1.2e11 physical gates.
# Six arms want six physical cores.
#
# THE ARCHIVE MUST BE THE RELEASE ONE. A Debug libcqops carries sanitizer runtimes the
# driver would have to link, and Release is the configuration whose counts are pinned
# (Rule 17).
set -e
W=${1:?work dir}
Q=${2:-$HOME/Desktop/CQ_lang_hardware}
SIZES=${3:-"n4 n20"}
FACTORIES=${4:-"1 2 4 8 16 32"}
O="$(cd -- "$(dirname -- "$0")/../../.." && pwd)"
S="$O/tools/qtg/sweep"
mkdir -p $W/logs

echo "### build $(date +%T) hardware=$(git -C $Q log -1 --format=%h) cqops=$(git -C $O log -1 --format=%h)" \
    >> $W/logs/progress.log
cmake -S $Q/qec -B $W/build-qec -DCMAKE_BUILD_TYPE=Release > $W/logs/cmake.log 2>&1
cmake --build $W/build-qec --parallel 6 --target qec cjson tommath >> $W/logs/cmake.log 2>&1
LIBS="$W/build-qec/libqec.a $W/build-qec/libcjson.a $W/build-qec/libtommath.a"

# One driver per size.  The instance is chosen by -I ORDER: the shadowing directory
# comes first, so qtg.c's #include "instance.h" resolves there; n4 has no shadow and
# falls through to the shipped tools/qtg/instance.h.
build_driver() {  # $1 = size tag
    local shadow=""
    [ "$1" != "n4" ] && shadow="-I$S/$1"
    cc -O2 -std=c11 ${=shadow} -I$O/src -I$O/tools/qtg -I$O/include -I$Q/qec/include \
       $S/probes/qtg_depth.c $O/build-release/libcqops.a ${=LIBS} -lm -o $W/${1}_depth
}
for t in ${=SIZES}; do build_driver $t; done

run() {  # $1 = arm name, $2 = driver, $3 = config
    echo "### $1 start $(date +%T)" >> $W/logs/progress.log
    CQOPS_SINK=qec CQOPS_QEC_CONFIG=$3 /usr/bin/time -l $2 > $W/logs/$1.log 2>&1 < /dev/null
    echo "### $1 done rc=$? $(date +%T)" >> $W/logs/progress.log
}
for t in ${=SIZES}; do for F in ${=FACTORIES}; do
    ( run ${t}_F$F $W/${t}_depth $S/configs/qtg_${t}_F$F.json ) &
done; done
wait
echo "### ALL DONE $(date +%T)" >> $W/logs/progress.log

for t in ${=SIZES}; do for F in ${=FACTORIES}; do
    printf "%-10s " ${t}_F$F; grep -h "^QEC d=" $W/logs/${t}_F$F.log || echo "(no row — see the log)"
done; done
