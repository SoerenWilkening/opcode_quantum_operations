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
## Homebrew LLVM's runtime does work; to get full Debug coverage on such a host:
##   cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug \
##         -DCMAKE_C_COMPILER=/usr/local/opt/llvm/bin/clang

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
