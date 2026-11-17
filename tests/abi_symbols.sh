#!/bin/sh
# The symbol check of kmgy.23 (NORTH_STAR "Done when" 1-2): the archives against the ABI's
# in-scope list (tests/abi_expected.c) and its out-of-scope names, by nm.
# Usage: abi_symbols.sh <nm> <abi_expected> <libcq_templates.a> <libcq_runtime.a> <build dir>
# Fails on: an expected symbol not defined in its archive; a public cqrt_*/cq_template_* symbol
# defined that the list lacks; an out-of-scope or retired name defined; a __quantum__qis__*
# operation defined, or one referenced that is not one of the seven, or one of the seven never
# referenced; a cq_template_* referenced by libcq_runtime.a; a symbol libcq_templates.a references
# that neither archive defines (other than the seven and libc); an expected symbol no test
# object references. Prints counts per family, and the internal globals nothing else references.
set -u
NM=$1 EXP=$2 TPL=$3 RT=$4 B=$5
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
syms "$TPL" > "$T/tpl" && syms "$RT" > "$T/rt" || { echo "FAIL: $NM"; exit 1; }
"$EXP" > "$T/exp" || { echo "FAIL: $EXP"; exit 1; }
awk '$2 != "U" { print $3 }' "$T/tpl" | sort -u > "$T/tpl_def"
awk '$2 != "U" { print $3 }' "$T/rt" | sort -u > "$T/rt_def"
cat "$T/tpl_def" "$T/rt_def" | sort -u > "$T/all_def"
awk '$1 == "templates" { print $3 }' "$T/exp" | sort > "$T/exp_tpl"
awk '$1 == "runtime" { print $3 }' "$T/exp" | sort > "$T/exp_rt"
sort "$T/exp_tpl" "$T/exp_rt" > "$T/exp_all"
uniq -d "$T/exp_all" > "$T/x"; [ -s "$T/x" ] && bad "the expected list repeats a name" "$T/x"

# 1. Every expected symbol is defined, in its archive (ABI:102-104, inventory (b)).
comm -23 "$T/exp_tpl" "$T/tpl_def" > "$T/x"; [ -s "$T/x" ] && bad "expected cq_template_* not defined in libcq_templates.a" "$T/x"
comm -23 "$T/exp_rt" "$T/rt_def" > "$T/x"; [ -s "$T/x" ] && bad "expected cqrt_* not defined in libcq_runtime.a" "$T/x"
grep '^cqrt_' "$T/tpl_def" > "$T/x"; [ -s "$T/x" ] && bad "cqrt_* defined in libcq_templates.a" "$T/x"
grep '^cq_template_' "$T/rt_def" > "$T/x"; [ -s "$T/x" ] && bad "cq_template_* defined in libcq_runtime.a" "$T/x"

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

# 4. The seven operations: defined by neither archive, referenced, and nothing else of QIR.
SEVEN='__quantum__qis__x__body __quantum__qis__cnot__body __quantum__qis__ccx__body __quantum__qis__ry__body __quantum__qis__ry__ctl __quantum__qis__rz__body __quantum__qis__m__body'
printf '%s\n' $SEVEN | sort > "$T/seven"
grep '^__quantum__' "$T/all_def" > "$T/x"; [ -s "$T/x" ] && bad "__quantum__* defined in the archives" "$T/x"
cat "$T/tpl" "$T/rt" | awk '$2 == "U" && $3 ~ /^__quantum__/ { print $3 }' | sort -u > "$T/qref"
comm -23 "$T/qref" "$T/seven" > "$T/x"; [ -s "$T/x" ] && bad "__quantum__* referenced that is not one of the seven" "$T/x"
awk '$2 == "U" && $3 ~ /^__quantum__/ { print $3 }' "$T/rt" | sort -u | comm -13 - "$T/seven" > "$T/x"
[ -s "$T/x" ] && bad "one of the seven never referenced by libcq_runtime.a" "$T/x"

# 5. Link order main.o libcq_templates.a libcq_runtime.a (ABI:102-104), checked here because Apple's
#    ld does not enforce it: the runtime references no template, and every cq/cqrt symbol either archive
#    references is defined by the runtime or by the archive itself.
awk '$2 == "U" && $3 ~ /^cq_template_/ { print $1, $3 }' "$T/rt" > "$T/x"
[ -s "$T/x" ] && bad "libcq_runtime.a references cq_template_*" "$T/x"
awk '$2 == "U" { print $3 }' "$T/tpl" | grep -E '^_*cq' | sort -u | comm -23 - "$T/all_def" > "$T/x"
[ -s "$T/x" ] && bad "libcq_templates.a references cq symbols no archive defines" "$T/x"
awk '$2 == "U" { print $3 }' "$T/rt" | grep -E '^_*cq' | sort -u | comm -23 - "$T/rt_def" > "$T/x"
[ -s "$T/x" ] && bad "libcq_runtime.a references cq symbols it does not define" "$T/x"

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
find "$B/CMakeFiles" -name '*.o' ! -path '*/cq_runtime.dir/*' ! -path '*/cq_templates.dir/*' ! -path '*/cq_gates_replay.dir/*' |
    while read -r o; do syms "$o"; done | awk '$2 == "U" { print $3 }' > "$T/uref"
cat "$T/tpl" "$T/rt" | awk '$2 == "U" { print $3 }' >> "$T/uref"
sort -u -o "$T/uref" "$T/uref"
grep -vE '^(cqrt_|cq_template_)' "$T/all_def" | comm -23 - "$T/uref" > "$T/x"
echo "internal globals referenced by nothing outside their own object: $(wc -l < "$T/x" | tr -d ' ')"
sed 's/^/  /' "$T/x"

[ $fail = 0 ] && echo "abi_symbols ok"
exit $fail
