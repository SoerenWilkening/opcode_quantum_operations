#!/bin/sh
# The symbol check of kmgy.23 (NORTH_STAR "Done when" 1-2): libcq_backend.a against the ABI's
# in-scope list (tests/abi_expected.c) and its out-of-scope names, by nm.
# Usage: abi_symbols.sh <nm> <abi_expected> <libcq_backend.a> <build dir>
# Fails on: an expected symbol not defined; a public cqrt_*/cq_template_* symbol
# defined that the list lacks; an out-of-scope or retired name defined; a __quantum__qis__*
# operation defined, or one referenced that is not one of the seven, or one of the seven never
# referenced; a cq/cqrt symbol the archive references and does not define; an expected symbol no test
# object references. (That nothing outside src/templates/ references a cq_template_* is the test
# templates_layering, by object.) Prints counts per family, and the internal globals nothing else references.
set -u
NM=$1 EXP=$2 LIB=$3 B=$4
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL: $1"; sed 's/^/  /' "$2" | head -50; n=$(wc -l < "$2"); [ "$n" -gt 50 ] && echo "  ... $n in all"; fail=1; }

# "<member> <type> <name>" for every global symbol, Mach-O's leading underscore removed.
syms() {
    us=0; [ "$(uname)" = Darwin ] && us=1
    "$NM" -g "$1" | awk -v us="$us" '
        BEGIN { m = "-" }
        /:$/ { m = $0; sub(/:$/, "", m); sub(/.*\(/, "", m); sub(/\)$/, "", m); next }
        NF == 0 { next }
        { t = $(NF-1); s = $NF; if (us) sub(/^_/, "", s); print m, t, s }'
}
syms "$LIB" > "$T/lib" || { echo "FAIL: $NM"; exit 1; }
"$EXP" > "$T/exp" || { echo "FAIL: $EXP"; exit 1; }
awk '$2 != "U" { print $3 }' "$T/lib" | sort -u > "$T/all_def"
awk '$1 == "templates" { print $3 }' "$T/exp" | sort > "$T/exp_tpl"
awk '$1 == "runtime" { print $3 }' "$T/exp" | sort > "$T/exp_rt"
sort "$T/exp_tpl" "$T/exp_rt" > "$T/exp_all"
uniq -d "$T/exp_all" > "$T/x"; [ -s "$T/x" ] && bad "the expected list repeats a name" "$T/x"

# 1. Every expected symbol is defined (ABI:102-104, inventory (b)).
comm -23 "$T/exp_tpl" "$T/all_def" > "$T/x"; [ -s "$T/x" ] && bad "expected cq_template_* not defined in libcq_backend.a" "$T/x"
comm -23 "$T/exp_rt" "$T/all_def" > "$T/x"; [ -s "$T/x" ] && bad "expected cqrt_* not defined in libcq_backend.a" "$T/x"

# 2. No public symbol beyond the list.
grep -E '^(cqrt_|cq_template_)' "$T/all_def" | comm -23 - "$T/exp_all" > "$T/x"
[ -s "$T/x" ] && bad "public symbols the ABI list does not have" "$T/x"

# 3. Out of scope and retired names (ABI:94-100, ABI:127-139, §2.9-§2.12, §1.14, §3): none defined, and
#    none in the expected list (a check on the list's patterns). An fp trunc carries an fp width token.
OUT='^cq_template_.*_(f16|bf16|f32|f64|f80)(_|$)
^cq_template_(fcmp|fcmps|fpext|fptrunc|sitofp|uitofp|fptosi|fptoui|bitcast|fadd|fsub|fmul|fdiv|frem|fneg|sqrt|fma|fabs|copysign|fmax|fmin|floor|ceil|rint|round|is_fpclass)_
^cq_template_.*_(rm|rmq|exc)(_|$)
^cq_template_.*_(inv|controlled)(_|$)
^cq_template_qrom_load_
^__cq_(q_|clone_|act_)
^__cq_(alloc|mem|sig|register|k|invert_begin|invert_end)$
^cq_(theta|measure)_(i1|c|si8|ui8|si16|ui16|si32|ui32|sl|ul|sll|ull|f32|f64|f80)$
^cq_phi$
^cqrt_(x|cnot|toffoli|cswap|qram_size|qram_free|tape_write_str_lit|tape_str_begin|tape_str_end)$
^cqrt_phase_f64
^cqrt_measure_.*_controlled
^cqrt_rz_(i[0-9]+|f16|bf16|f32|f64|f80)(_controlled)?$
^cqrt_.*_controlled_inv$
^cqrt_qram_alloc_host_
^cqrt_qram_realloc_
^cqrt_qram_store_.*_unc$
^cqrt_qram_alloc_'
printf '%s\n' "$OUT" > "$T/out"
# rz_i1 and qram_alloc_i8 are the ABI's own; the two broad retired patterns above catch them, so they are excepted.
cat "$T/all_def" "$T/exp_all" | sort -u | grep -Ef "$T/out" | grep -vxE 'cqrt_rz_i1|cqrt_qram_alloc_i8' > "$T/x"
[ -s "$T/x" ] && bad "out-of-scope or retired names defined or expected" "$T/x"

# 4. The seven operations: not defined by the archive, referenced, and nothing else of QIR.
SEVEN='__quantum__qis__x__body __quantum__qis__cnot__body __quantum__qis__ccx__body __quantum__qis__ry__body __quantum__qis__ry__ctl __quantum__qis__rz__body __quantum__qis__m__body'
printf '%s\n' $SEVEN | sort > "$T/seven"
grep '^__quantum__' "$T/all_def" > "$T/x"; [ -s "$T/x" ] && bad "__quantum__* defined in the archives" "$T/x"
awk '$2 == "U" && $3 ~ /^__quantum__/ { print $3 }' "$T/lib" | sort -u > "$T/qref"
comm -23 "$T/qref" "$T/seven" > "$T/x"; [ -s "$T/x" ] && bad "__quantum__* referenced that is not one of the seven" "$T/x"
comm -13 "$T/qref" "$T/seven" > "$T/x"
[ -s "$T/x" ] && bad "one of the seven never referenced by libcq_backend.a" "$T/x"

# 5. Link order main.o libcq_backend.a <gate backend>: every cq/cqrt symbol the archive references is
#    defined by it.
awk '$2 == "U" { print $3 }' "$T/lib" | grep -E '^_*cq' | sort -u | comm -23 - "$T/all_def" > "$T/x"
[ -s "$T/x" ] && bad "libcq_backend.a references cq symbols it does not define" "$T/x"

# 6. Done-when 2, the cheap form: every expected symbol is referenced by some test object (the sampled
#    harness tests, tests/test_*.c, and the harness itself; not this check's own abi_* programs).
find "$B/CMakeFiles" -name '*.o' \( -path '*/test_*.dir/tests/test_*' -o -path '*/cq_test_harness.dir/*' \) ! -path '*/test_abi_*' | sort > "$T/objs"
[ -s "$T/objs" ] || { echo "FAIL: no test objects under $B/CMakeFiles"; exit 1; }
while read -r o; do syms "$o"; done < "$T/objs" | awk '$2 == "U" { print $3 }' | sort -u > "$T/testref"
comm -23 "$T/exp_all" "$T/testref" > "$T/x"; [ -s "$T/x" ] && bad "expected symbols no test references" "$T/x"

# Counts per family: expected / defined / referenced by a test.
echo "archive   family     expected  defined  tested"
awk 'FILENAME == ARGV[1] { d[$1] = 1; next } FILENAME == ARGV[2] { r[$1] = 1; next }
     { k = $1 " " $2; e[k]++; E++; if ($3 in d) { D[k]++; DD++ } if ($3 in r) { R[k]++; RR++ } }
     END { for (k in e) { split(k, a, " "); printf "%-9s %-9s %9d %8d %7d\n", a[1], a[2], e[k], D[k], R[k] }
           printf "%-19s %9d %8d %7d\n", "total", E, DD, RR }' "$T/all_def" "$T/testref" "$T/exp" | sort -k1,1 -k2,2

# Internal globals (no public prefix) that no other member of the archives, no test and no tool
# references: candidates for static. Information, not a failure.
find "$B/CMakeFiles" -name '*.o' ! -path '*/cq_backend.dir/*' ! -path '*/cq_gates_replay.dir/*' |
    while read -r o; do syms "$o"; done | awk '$2 == "U" { print $3 }' > "$T/uref"
awk '$2 == "U" { print $3 }' "$T/lib" >> "$T/uref"
sort -u -o "$T/uref" "$T/uref"
grep -vE '^(cqrt_|cq_template_)' "$T/all_def" | comm -23 - "$T/uref" > "$T/x"
echo "internal globals referenced by nothing outside their own object: $(wc -l < "$T/x" | tr -d ' ')"
sed 's/^/  /' "$T/x"

[ $fail = 0 ] && echo "abi_symbols ok"
exit $fail
