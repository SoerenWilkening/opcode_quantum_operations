## The HOST floating-point probe, for L1's fp oracle (PRD-v2 §7.4, §7.12).
##
## PRD-v2 §7.4 splits the fp surface in two, on a measurement. The LIBRARY's
## classical short-circuit is a C transcription of Bennett's Julia body over
## uint64_t, so it never evaluates a C `double` and is host-independent by
## construction. L1's ORACLE is the opposite and deliberately so: it is the HOST
## operator, because an oracle that shares code with the implementation is blind
## to exactly what that code gets wrong (the Step 18 trap, CLAUDE.md). That
## independence is bought with a dependency on the host — and every cell of
## §7.4's table is IEEE-UNSPECIFIED, every value in it x86's choice. ARM differs
## on all of them (a POSITIVE default NaN).
##
## So the ground the oracle stands on is a fact to MEASURE. A host whose
## rounding mode is not to-nearest, or that flushes subnormals, or whose NaN
## cells are not these, fails every fp anchor IN THE ORACLE rather than in the
## port — and the red reads as a kernel bug. §7.4 asks for this probe by name,
## "of the sanitizer probe's shape", and that is what it is: COMPILED AND RUN,
## never inferred from a target triple, with the verdict printed at configure
## time and compiled in for tests/test_skeleton.c to cross-check.
##
## IT COMPILES tests/support/fphost.c ITSELF RATHER THAN A COPY OF THE ARMS.
## cmake/CqopsSanitizers.cmake can afford an embedded `int main(void){return 0;}`
## because that program says nothing; ten fp arms do, and a second transcription
## of them is the drift this repository keeps paying for — a probe and a test
## that agree until the day they quietly do not, caught by nothing, because no
## test reads a comment. One copy, two callers.
##
## WHAT THE TWO CALLERS DO NOT SHARE IS THE COMPILE LINE, WHICH IS THE POINT.
## This probe builds with -std=c11 -ffp-contract=off and NO -O and NO sanitizer;
## the test binary carries the configuration's own optimisation level and, in
## Debug, its sanitizers and possibly a DIFFERENT COMPILER (see
## cmake/CqopsDebugToolchain.cmake). test_skeleton asserts the two verdicts
## agree, so this is two independent measurements of one host rather than one
## measurement stated twice.
##
##   -ffp-contract=off  the same reproducibility argument the root CMakeLists
##                      makes for src/angle.c: a contracted expression is
##                      measured against an unrounded intermediate.
##   no -O              nothing here is a benchmark, and every operand in
##                      fphost.c is `volatile` anyway — which is what keeps the
##                      probe about the MACHINE rather than about the compiler's
##                      model of it.
##   no sanitizer       the fp environment does not depend on one, and Debug's
##                      ASan runtime is the thing this box has historically
##                      failed to run at all (`bd 6wg`).
##
## CQOPS_FPHOST is AUTO (default — measure and report), ON (require it; a hard
## configure error if any arm fails) or OFF (do not measure). ON is what CI
## should pass, alongside the three knobs it already hardens: like those, this
## one degrades QUIETLY, since an unmeasured host is indistinguishable from a
## conforming one in a green run.
##
## THIS MODULE RUNS AT include() TIME and leaves CQOPS_FPHOST_BUILD behind.
## There is no second entry point to forget to call, because unlike the
## sanitizer selection its verdict is not something the caller chooses: it is a
## property of the machine, the same in every configuration unless the compiler
## differs, which is precisely what this measures.

set(CQOPS_FPHOST "AUTO" CACHE STRING
    "Host fp probe: AUTO (measure and report), ON (require it), OFF (skip)")
set_property(CACHE CQOPS_FPHOST PROPERTY STRINGS AUTO ON OFF)

# _cqops_fphost_compile(<src> <main> <inc> <exe> <use_libm> <rc_var> <out_var>)
#
# Builds the two translation units into <exe>. Split out because libm is a
# platform question and not an fp one: glibc needs -lm for fegetround and sqrt,
# macOS folds both into libSystem and has no libm.dylib on disk to find. The
# caller tries with it and falls back to without, which is cheaper and more
# honest than a find_library that can answer "no" on a host where the link
# would have worked anyway. (Same fact the root CMakeLists records for the
# `cqops` target: on this box libm did not need to be linked, which is exactly
# why the omission would have surfaced only on a glibc runner.)
function(_cqops_fphost_compile src main inc exe use_libm rc_var out_var)
    file(REMOVE "${exe}")

    set(extra "")
    if(use_libm)
        set(extra "-lm")
    endif()

    execute_process(
        COMMAND "${CMAKE_C_COMPILER}" -std=c11 -ffp-contract=off
                "-I${inc}" "${src}" "${main}" -o "${exe}" ${extra}
        RESULT_VARIABLE rc OUTPUT_VARIABLE c_out ERROR_VARIABLE c_err)

    set(${rc_var} "${rc}" PARENT_SCOPE)
    set(${out_var} "${c_out}${c_err}" PARENT_SCOPE)
endfunction()

# cqops_select_fp_host(<out_var>) — resolves CQOPS_FPHOST and sets <out_var> to
# the value test_skeleton.c is handed as CQOPS_BUILD_FPHOST.
#
# THREE-VALUED, AND THAT IS NOT DECORATION: 1 probed and every arm held, 0
# probed and an arm failed, -1 NOT PROBED. Two values would make "not measured"
# and "measured and false" the same number, so the binary could not tell a host
# it was never told about from one it was told was wrong — which is the shape
# `bd remember three-valued-proof-negative-is-not-zero` records one layer down.
# A consumer compares with >= 0, never with a bare negation.
function(cqops_select_fp_host out_var)
    set(${out_var} -1 PARENT_SCOPE)

    if(CQOPS_FPHOST STREQUAL "OFF")
        message(STATUS "cqops: fp host probe SKIPPED by CQOPS_FPHOST=OFF — "
                       "L1's fp oracle (PRD-v2 §7.4) is unverified on this host")
        return()
    endif()

    set(tests_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tests")
    set(src "${tests_dir}/support/fphost.c")
    set(hdr "${tests_dir}/support/fphost.h")

    if(NOT EXISTS "${src}")
        message(FATAL_ERROR
            "cqops: ${src} is missing — this probe compiles the test-side arms "
            "themselves rather than a copy of them, so there is nothing to run.")
    endif()

    ## Without this the cached verdict below outlives an edit to the arms, and
    ## the gate would report on a file it had not read — the defect the childcap
    ## EINTR count block in tests/CMakeLists.txt records at length.
    set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND
                 PROPERTY CMAKE_CONFIGURE_DEPENDS "${src}" "${hdr}")

    ## The cache key is the ARMS plus the COMPILER, so re-running cmake is free
    ## while editing either one re-measures. `cmake --fresh`, or deleting
    ## CQOPS_FPHOST_KEY, forces it by hand.
    file(SHA256 "${src}" src_sha)
    file(SHA256 "${hdr}" hdr_sha)
    set(key "${src_sha}:${hdr_sha}:${CMAKE_C_COMPILER}")

    if(DEFINED CQOPS_FPHOST_KEY AND CQOPS_FPHOST_KEY STREQUAL "${key}")
        set(ok "${CQOPS_FPHOST_OK}")
        set(why "${CQOPS_FPHOST_WHY}")
    else()
        set(dir "${CMAKE_BINARY_DIR}/CMakeFiles/cqops-fphost-probe")
        file(MAKE_DIRECTORY "${dir}")
        file(WRITE "${dir}/main.c"
             "#include \"support/fphost.h\"\n"
             "#include <stdio.h>\n"
             "int main(void)\n"
             "{\n"
             "    char why[CQ_FPHOST_WHY_MAX];\n"
             "    if (cq_fphost_check(why, sizeof why)) {\n"
             "        printf(\"%d arm(s)\\n\", cq_fphost_arms_run());\n"
             "        return 0;\n"
             "    }\n"
             "    printf(\"%s\\n\", why);\n"
             "    return 1;\n"
             "}\n")

        _cqops_fphost_compile("${src}" "${dir}/main.c" "${tests_dir}"
                              "${dir}/probe" TRUE build_rc build_out)
        if(NOT build_rc EQUAL 0)
            _cqops_fphost_compile("${src}" "${dir}/main.c" "${tests_dir}"
                                  "${dir}/probe" FALSE build_rc build_out)
        endif()

        if(NOT build_rc EQUAL 0 OR NOT EXISTS "${dir}/probe")
            set(ok 0)
            string(REGEX REPLACE "\n.*" "" first_line "${build_out}")
            set(why "the probe did not build: ${first_line}")
        else()
            execute_process(COMMAND "${dir}/probe"
                            RESULT_VARIABLE run_rc
                            OUTPUT_VARIABLE run_out ERROR_VARIABLE run_err)
            string(REGEX REPLACE "\n.*" "" first_line "${run_out}${run_err}")
            if(run_rc EQUAL 0)
                set(ok 1)
                set(why "${first_line}")
            else()
                set(ok 0)
                set(why "${first_line}")
            endif()
        endif()

        set(CQOPS_FPHOST_OK  ${ok}     CACHE INTERNAL "fp host arms all hold")
        set(CQOPS_FPHOST_WHY "${why}"  CACHE INTERNAL "fp host probe verdict")
        set(CQOPS_FPHOST_KEY "${key}"  CACHE INTERNAL "fp host probe cache key")
    endif()

    if(ok)
        message(STATUS "cqops: fp host probe OK — round-to-nearest, FTZ/DAZ "
                       "off, PRD-v2 §7.4's five NaN cells exact (${why})")
        set(${out_var} 1 PARENT_SCOPE)
        return()
    endif()

    ## The STATUS line names the ARM, always, and it is what a reader and a CI
    ## grep get to look at. The WARNING beside it says what the failure COSTS,
    ## which is the part that is easy to misread: nothing in src/ is wrong on
    ## such a host, and every fp anchor will still go red.
    message(STATUS "cqops: fp host probe FAILED — ${why}")

    if(CQOPS_FPHOST STREQUAL "ON")
        message(FATAL_ERROR
            "cqops: CQOPS_FPHOST=ON but this host fails an arm of PRD-v2 §7.4: "
            "${why}\n"
            "  The library is unaffected — its fp short-circuit does no C "
            "`double` arithmetic. What breaks is L1's ORACLE, which is the host "
            "operator by design.\n"
            "  Use -DCQOPS_FPHOST=AUTO to configure anyway and read the warning "
            "instead.")
    endif()

    message(WARNING
        "cqops: fp host probe FAILED — ${why}. L1's fp oracle (PRD-v2 §7.4) is "
        "the HOST operator with the IEEE-unspecified cells pinned by table, so "
        "on this host every fp anchor fails IN THE ORACLE and not in the port. "
        "The library itself is unaffected: its classical short-circuit is a C "
        "transcription over uint64_t and never evaluates a `double`. Say so "
        "when reporting what was verified (Rule 17).")
    set(${out_var} 0 PARENT_SCOPE)
endfunction()

cqops_select_fp_host(CQOPS_FPHOST_BUILD)
