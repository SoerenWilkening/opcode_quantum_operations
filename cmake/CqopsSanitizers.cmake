## Sanitizer selection for the Debug configuration (IMPLEMENTATION_PLAN §2.1).
##
## The plan asks for -fsanitize=address,undefined in Debug. That is not always
## available: on this project's dev box (macOS 26 / Darwin 25, x86_64, Apple
## clang 17) the AddressSanitizer runtime aborts with SIGILL inside
## libsystem_pthread before main is reached, on a trivial hello-world. It is a
## toolchain defect, not a property of any code we write, and it makes every
## Debug test binary unrunnable.
##
## So each sanitizer is PROBED — compiled *and run* — and enabled only if it
## actually works. What is enabled is then printed at configure time and
## asserted by test_skeleton against the compiler's own __has_feature, so a
## run's coverage claim stays literal (CLAUDE.md, Rule 17). A sanitizer that
## silently went missing would be the worst outcome of the three.
##
## THIS MODULE CHOOSES AMONG FLAGS FOR A COMPILER THAT IS ALREADY FIXED, and on
## that box the broken thing was the COMPILER. cmake/CqopsDebugToolchain.cmake
## runs before project() and switches Debug to a toolchain whose ASan runtime
## actually runs (`bd 6wg`, 2026-08-27), so on this dev box the probes below now
## both succeed. The warning path is still live and still correct — it is what a
## host with no working runtime at all falls back to.

include(CheckCSourceRuns)

# cqops_probe_sanitizer(<flag> <cache_var>) — sets <cache_var> to 1 iff a
# trivial program built with <flag> both compiles and runs successfully.
function(cqops_probe_sanitizer flag cache_var)
    set(CMAKE_REQUIRED_FLAGS "${flag}")
    set(CMAKE_REQUIRED_LINK_OPTIONS "${flag}")
    set(CMAKE_REQUIRED_QUIET TRUE)
    check_c_source_runs("int main(void) { return 0; }" ${cache_var})
endfunction()

# cqops_select_sanitizers(<out_flags_var>) — resolves CQOPS_SANITIZERS into a
# list of flags for the current compiler, and reports the outcome. Also exports
# CQOPS_ASAN_ENABLED / CQOPS_UBSAN_ENABLED (0 or 1) so the build's belief can be
# handed to the test binary and checked against the compiler's __has_feature.
function(cqops_select_sanitizers out_var)
    set(flags "")
    set(enabled "")
    set(missing "")

    set(CQOPS_ASAN_ENABLED  0 PARENT_SCOPE)
    set(CQOPS_UBSAN_ENABLED 0 PARENT_SCOPE)

    if(CQOPS_SANITIZERS STREQUAL "OFF")
        message(STATUS "cqops: sanitizers disabled by CQOPS_SANITIZERS=OFF")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    cqops_probe_sanitizer("-fsanitize=undefined" CQOPS_UBSAN_RUNS)
    cqops_probe_sanitizer("-fsanitize=address"   CQOPS_ASAN_RUNS)

    if(CQOPS_UBSAN_RUNS)
        list(APPEND enabled "undefined")
        set(CQOPS_UBSAN_ENABLED 1 PARENT_SCOPE)
    else()
        list(APPEND missing "undefined")
    endif()

    if(CQOPS_ASAN_RUNS)
        list(APPEND enabled "address")
        set(CQOPS_ASAN_ENABLED 1 PARENT_SCOPE)
    else()
        list(APPEND missing "address")
    endif()

    if(CQOPS_SANITIZERS STREQUAL "ON" AND missing)
        list(JOIN missing ", " missing_str)
        message(FATAL_ERROR
            "cqops: CQOPS_SANITIZERS=ON but these sanitizers do not run with "
            "${CMAKE_C_COMPILER}: ${missing_str}. Point CMAKE_C_COMPILER at a "
            "working toolchain, or use CQOPS_SANITIZERS=AUTO to build with "
            "whatever works on this host.")
    endif()

    if(enabled)
        list(JOIN enabled "," enabled_arg)
        list(APPEND flags "-fsanitize=${enabled_arg}")
        # Without -fno-sanitize-recover, UBSan prints a diagnostic and carries
        # on, so a test binary can report a violation and still exit 0 — a
        # green run that verified nothing.
        list(APPEND flags "-fno-sanitize-recover=all")
        list(JOIN enabled ", " enabled_str)
        message(STATUS "cqops: Debug sanitizers ACTIVE: ${enabled_str}")
    endif()

    if(missing)
        list(JOIN missing ", " missing_str)
        message(WARNING
            "cqops: Debug sanitizers UNAVAILABLE on this toolchain: "
            "${missing_str}. They compile but do not run with "
            "${CMAKE_C_COMPILER}. Debug coverage is reduced — say so when "
            "reporting what was verified (Rule 17). A toolchain whose runtime "
            "works, e.g. Homebrew LLVM, restores it via -DCMAKE_C_COMPILER=.")
    endif()

    set(${out_var} "${flags}" PARENT_SCOPE)
endfunction()

## ---------------------------------------------------------------------------
## LEAK DETECTION (`bd kfi`) — a THIRD probe, because LSan is neither implied by
## ASan nor free to ask for.
##
## LeakSanitizer ships inside the ASan runtime, but on Darwin it is OFF BY
## DEFAULT: a leaking binary built with -fsanitize=address exits 0 until
## ASAN_OPTIONS=detect_leaks=1 is set. (The comment in tests/test_shim_ctx.c that
## said LSan was "unavailable on Darwin" was measuring that default and drawing
## the wrong conclusion; it is corrected.) So enabling it is an ENVIRONMENT
## change, made in cmake/CqopsTest.cmake, not a compile flag — which is exactly
## why it needs a probe of its own rather than riding on CQOPS_ASAN_ENABLED.
##
## AND ASKING FOR IT UNCONDITIONALLY IS NOT SAFE. On a platform whose ASan
## runtime has no LSan, ASAN_OPTIONS=detect_leaks=1 is a FATAL "detect_leaks is
## not supported on this platform" — so a hard-coded option string would break
## EVERY test on such a host. Same posture as the two probes above and as
## cmake/CqopsDebugToolchain.cmake: measure, then use what works.
##
## WHAT THE PROBE ASSERTS, AND WHY BOTH ARMS ARE NEEDED. A probe that only
## checked "the option does not crash the process" would pass on a host that
## IGNORES it, which is the failure this whole file exists to prevent. So it
## runs two programs under detect_leaks=1 and requires them to DISAGREE:
##
##   leaky  -> non-zero exit AND the report names LeakSanitizer.  Non-zero on its
##            own is not enough: the "not supported on this platform" fatal is
##            also non-zero, and it does NOT say LeakSanitizer.
##   clean  -> exit 0.  This is the false-positive arm — without it, a runtime
##            that failed every process under the option would read as working.
##
## MEASURED 2026-08-27/28 on Homebrew clang 22.1.5, Darwin 25 / x86_64: leaky
## exits 1 with "ERROR: LeakSanitizer: detected memory leaks / Direct leak of
## 4096 byte(s)", clean exits 0, and abort_on_error=1 does NOT turn a leak into a
## SIGABRT — a leak is a NORMAL non-zero exit. That last fact is what lets
## tests/test_lsan_negative.c be a plain WILL_FAIL binary (death.h explains why
## WILL_FAIL cannot express a crash).

# cqops_probe_leak_check(<out>) — 1 iff ASAN_OPTIONS=detect_leaks=1 both works
# and discriminates with the current compiler. Cached per compiler; delete the
# CQOPS_LSAN_RUNS_* cache entry, or `cmake --fresh`, to re-measure.
function(cqops_probe_leak_check out)
    string(MAKE_C_IDENTIFIER "${CMAKE_C_COMPILER}" key)
    set(cache_var "CQOPS_LSAN_RUNS_${key}")
    if(DEFINED ${cache_var})
        set(${out} "${${cache_var}}" PARENT_SCOPE)
        return()
    endif()

    set(dir "${CMAKE_BINARY_DIR}/CMakeFiles/cqops-lsan-probe")
    file(MAKE_DIRECTORY "${dir}")

    ## The allocation goes through a noinline function and a volatile static that
    ## is then cleared, so nothing on the stack or in a global still points at
    ## the block when LSan's atexit hook runs — LSan scans both conservatively as
    ## roots, and a block it can still reach is "still reachable", which it does
    ## not report by default. tests/test_lsan_negative.c leaks the same way.
    file(WRITE "${dir}/leaky.c"
         "#include <stdlib.h>\n"
         "static void *volatile hold;\n"
         "__attribute__((noinline)) static void leak_one(void)"
         " { hold = malloc(4096); hold = 0; }\n"
         "int main(void) { leak_one(); return 0; }\n")
    file(WRITE "${dir}/clean.c"
         "#include <stdlib.h>\n"
         "static void *volatile hold;\n"
         "int main(void) { hold = malloc(4096); free((void *)hold); hold = 0; return 0; }\n")

    set(ok 0)
    set(leaky_rc "not built")
    set(clean_rc "not built")

    _cqops_lsan_build_and_run("${dir}/leaky.c" "${dir}/leaky" leaky_rc leaky_out)
    _cqops_lsan_build_and_run("${dir}/clean.c" "${dir}/clean" clean_rc clean_out)

    if(NOT leaky_rc STREQUAL "0" AND leaky_out MATCHES "LeakSanitizer"
       AND clean_rc STREQUAL "0")
        set(ok 1)
    endif()

    set(${cache_var} ${ok} CACHE INTERNAL
        "ASAN_OPTIONS=detect_leaks=1 works with ${CMAKE_C_COMPILER}")
    set(${out} ${ok} PARENT_SCOPE)
endfunction()

# _cqops_lsan_build_and_run(<src> <exe> <rc_var> <out_var>) — compiles <src> with
# -fsanitize=address and runs it under detect_leaks=1, merging stdout+stderr.
#
# abort_on_error=0 is set so a host that DOES turn the report into a SIGABRT
# still lands on a plain non-zero status rather than a signal, which keeps the
# probe's verdict about leak detection rather than about signal handling.
function(_cqops_lsan_build_and_run src exe rc_var out_var)
    file(REMOVE "${exe}")
    execute_process(COMMAND "${CMAKE_C_COMPILER}" -fsanitize=address "${src}" -o "${exe}"
                    RESULT_VARIABLE build_rc OUTPUT_QUIET ERROR_QUIET)
    if(NOT build_rc EQUAL 0 OR NOT EXISTS "${exe}")
        set(${rc_var} "not built" PARENT_SCOPE)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
                "ASAN_OPTIONS=detect_leaks=1:abort_on_error=0" "${exe}"
        RESULT_VARIABLE run_rc
        OUTPUT_VARIABLE run_out ERROR_VARIABLE run_err)
    set(${rc_var} "${run_rc}" PARENT_SCOPE)
    set(${out_var} "${run_out}${run_err}" PARENT_SCOPE)
endfunction()

# cqops_select_leak_check(<out_var>) — resolves CQOPS_LEAK_CHECK into 0/1.
#
# Requires ASan: LSan lives in that runtime, so with `address` dropped there is
# nothing to turn on. A request for ON in that state is an error rather than a
# silent 0, for the same reason CQOPS_SANITIZERS=ON is.
function(cqops_select_leak_check out_var)
    set(${out_var} 0 PARENT_SCOPE)

    if(CQOPS_LEAK_CHECK STREQUAL "OFF")
        message(STATUS "cqops: leak detection disabled by CQOPS_LEAK_CHECK=OFF")
        return()
    endif()

    if(NOT CQOPS_ASAN_ENABLED)
        if(CQOPS_LEAK_CHECK STREQUAL "ON")
            message(FATAL_ERROR
                "cqops: CQOPS_LEAK_CHECK=ON but AddressSanitizer is not enabled "
                "in this configuration — LeakSanitizer ships inside the ASan "
                "runtime and has nothing to attach to. Fix ASan first (see "
                "cmake/CqopsDebugToolchain.cmake) or use CQOPS_LEAK_CHECK=AUTO.")
        endif()
        message(STATUS "cqops: leak detection OFF — ASan is not enabled here")
        return()
    endif()

    cqops_probe_leak_check(lsan_ok)
    if(lsan_ok)
        message(STATUS "cqops: leak detection ACTIVE (ASAN_OPTIONS=detect_leaks=1)")
        set(${out_var} 1 PARENT_SCOPE)
        return()
    endif()

    if(CQOPS_LEAK_CHECK STREQUAL "ON")
        message(FATAL_ERROR
            "cqops: CQOPS_LEAK_CHECK=ON but ASAN_OPTIONS=detect_leaks=1 does not "
            "detect a deliberate leak with ${CMAKE_C_COMPILER}. Use "
            "CQOPS_LEAK_CHECK=AUTO to build without it.")
    endif()

    message(WARNING
        "cqops: leak detection UNAVAILABLE — ASAN_OPTIONS=detect_leaks=1 does "
        "not report a deliberate leak with ${CMAKE_C_COMPILER}. Debug coverage "
        "is reduced; say so when reporting what was verified (Rule 17).")
endfunction()
