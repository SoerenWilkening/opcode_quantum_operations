## Debug toolchain selection — `bd 6wg`, resolved 2026-08-27.
##
## THE PROBLEM. IMPLEMENTATION_PLAN §2.1 asks for -fsanitize=address,undefined in
## Debug, because Debug is where this project's invariant checks live and the
## qubit pool's free list (M03, I3), the handle table (M07, I2/D5 tombstones) and
## the scratch region (M08) are exactly where a memory bug would hide. On this
## project's dev box the AddressSanitizer *runtime* is broken: Apple clang 17.0.0
## on macOS 26 / Darwin 25 / x86_64 dies with SIGILL inside libsystem_pthread
## ("BUG IN LIBPTHREAD: PTHREAD_SELF TSD not initialized") before reaching main,
## on `int main(void){return 0;}`. Re-measured 2026-08-27 on Apple clang 17.0.0
## (clang-1700.6.4.2): still exit 132. So cmake/CqopsSanitizers.cmake probes each
## sanitizer and drops what does not run, and Debug there was UBSan-only.
##
## THE FIX, AND WHY IT IS A SEARCH RATHER THAN A PIN. Homebrew LLVM's runtime
## works — measured 2026-08-27 on Homebrew clang 22.1.5 at /usr/local/opt/llvm:
## the ASan probe exits 0, the whole tree builds clean under -Wall -Wextra -Werror
## -Wconversion, and ctest is 292/292 with `undefined, address` both ACTIVE. But
## PINNING that path would be wrong twice over: it does not exist on a box without
## Homebrew LLVM (the repo must still configure there, exactly as the Ninja probe
## in the Makefile must still fall back to Make), and it is the wrong path on
## Apple Silicon, where the prefix is /opt/homebrew. So the compiler is PROBED —
## compiled *and run* with -fsanitize=address — and the first candidate that
## actually works is taken. This is cmake/CqopsSanitizers.cmake's own posture one
## layer down: the sanitizer probe can only choose among flags for a compiler that
## has already been fixed, and by then the choice that matters has been made.
##
## DEBUG ONLY, DELIBERATELY. Release keeps whatever compiler CMake picks. Release
## is what gets its gate counts pinned (Rule 17), risk R5 is a golden that moves
## with the host, and Release has no use for a sanitizer runtime — so there is
## nothing to buy and a pinned-count-sized thing to lose. The two configurations
## may therefore be built by two different compilers on the same box; `make test`
## builds both, which is what keeps a -Werror difference between them visible.
##
## THE DEFAULT COMPILER IS PROBED FIRST AND WINS IF IT WORKS. Switching toolchains
## is not free — it changes which warnings -Werror rejects and which libc headers
## are in play — so it happens only when the default genuinely cannot run ASan.
## On a Linux CI runner, or once a later Xcode fixes the runtime, nothing here
## fires and this module becomes inert without anyone editing it.
##
## AN EXPLICIT CHOICE IS ALWAYS RESPECTED. -DCMAKE_C_COMPILER=… or CC= in the
## environment disables the search outright: a maintainer who names a compiler has
## made a decision, and quietly overriding it would be the same class of defect as
## a sink name that resolves to something else.

## cqops_select_debug_toolchain()
##
## Must be called BEFORE project(): CMAKE_C_COMPILER is read during the C language
## enable and cannot be changed afterwards. That is also why the probe is an
## execute_process rather than check_c_source_runs — there is no compiler to ask
## yet, so the candidate is invoked directly.
function(cqops_select_debug_toolchain)
    if(CQOPS_DEBUG_TOOLCHAIN STREQUAL "OFF")
        return()
    endif()

    ## Debug only. CMAKE_BUILD_TYPE is available before project() when it came
    ## from -D; empty means the top-level's "default to Debug" is about to fire,
    ## so treat it as Debug — except under a multi-config generator, where the
    ## build type is chosen at build time and Release would inherit the switch.
    ## (GENERATOR_IS_MULTI_CONFIG is not set until project(), so this matches on
    ## the generator name, which IS set this early.)
    set(is_debug FALSE)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(is_debug TRUE)
    elseif(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_GENERATOR MATCHES "Visual Studio|Xcode")
        set(is_debug TRUE)
    endif()
    if(NOT is_debug)
        return()
    endif()

    ## Two ways CMAKE_C_COMPILER can already be set, and they are DIFFERENT
    ## FACTS that want different words. On a fresh tree it can only have come
    ## from -D or CC=, which is a decision and is obeyed in silence. On a
    ## RE-CONFIGURE it is whatever the tree chose the first time — CMAKE_CACHEFILE_DIR
    ## is the tell, since it exists only once a cache has been loaded — and CMake
    ## refuses to change a build tree's compiler in place, so the search below
    ## could not take effect even if it ran. That second case is the one that
    ## would go quiet: a tree configured before this module existed keeps its
    ## broken-ASan compiler forever and nothing says why.
    if(DEFINED CMAKE_C_COMPILER OR DEFINED ENV{CC})
        if(NOT DEFINED CMAKE_CACHEFILE_DIR)
            message(STATUS "cqops: Debug toolchain named explicitly — no ASan search")
            return()
        endif()
        _cqops_probe_asan_compiler("${CMAKE_C_COMPILER}" pinned_ok)
        if(pinned_ok)
            return()
        endif()
        if(CQOPS_DEBUG_TOOLCHAIN STREQUAL "ON")
            message(FATAL_ERROR
                "cqops: CQOPS_DEBUG_TOOLCHAIN=ON but this build tree is already "
                "fixed to ${CMAKE_C_COMPILER}, whose -fsanitize=address binaries "
                "do not run. CMake cannot change a tree's compiler in place — "
                "delete the tree (`make clean`) and configure again.")
        endif()
        message(STATUS
            "cqops: this build tree is fixed to ${CMAKE_C_COMPILER}, whose ASan "
            "runtime does not run, and CMake cannot change a tree's compiler in "
            "place. Delete the tree (`make clean`) and configure again to pick "
            "one that can (bd 6wg).")
        return()
    endif()

    ## Sanitizers turned off outright: nothing to search for.
    if(DEFINED CQOPS_SANITIZERS AND CQOPS_SANITIZERS STREQUAL "OFF")
        return()
    endif()

    ## The compiler CMake would pick on its own, probed FIRST so a working host
    ## is left alone. NAMES mirrors CMake's own C compiler search order.
    find_program(CQOPS_DEFAULT_CC NAMES cc clang gcc)
    if(CQOPS_DEFAULT_CC)
        _cqops_probe_asan_compiler("${CQOPS_DEFAULT_CC}" default_ok)
        if(default_ok)
            return()
        endif()
    endif()

    ## Candidates, in the order they are tried. `brew --prefix llvm` first because
    ## it is the only one that is correct on both Intel and Apple Silicon without
    ## this file knowing which it is on; the two literal prefixes are the fallback
    ## for a box where brew is not on PATH but its LLVM is installed.
    set(candidates "")
    find_program(CQOPS_BREW brew)
    if(CQOPS_BREW)
        execute_process(COMMAND "${CQOPS_BREW}" --prefix llvm
                        OUTPUT_VARIABLE brew_llvm
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET RESULT_VARIABLE brew_rc)
        if(brew_rc EQUAL 0 AND brew_llvm)
            list(APPEND candidates "${brew_llvm}/bin/clang")
        endif()
    endif()
    list(APPEND candidates
         "/usr/local/opt/llvm/bin/clang"      # Homebrew, Intel
         "/opt/homebrew/opt/llvm/bin/clang")  # Homebrew, Apple Silicon
    list(REMOVE_DUPLICATES candidates)

    foreach(cc IN LISTS candidates)
        if(NOT EXISTS "${cc}")
            continue()
        endif()
        _cqops_probe_asan_compiler("${cc}" ok)
        if(ok)
            set(CMAKE_C_COMPILER "${cc}" CACHE FILEPATH
                "C compiler (selected by cqops: its ASan runtime runs, bd 6wg)")
            message(STATUS
                "cqops: Debug C compiler switched to ${cc} — the default "
                "(${CQOPS_DEFAULT_CC}) compiles with -fsanitize=address but the "
                "resulting binary does not run (bd 6wg). Set "
                "CQOPS_DEBUG_TOOLCHAIN=OFF, or name a compiler with "
                "-DCMAKE_C_COMPILER=, to keep the default.")
            return()
        endif()
    endforeach()

    ## Nothing on this host runs ASan. AUTO carries on — cqops_select_sanitizers
    ## will drop `address` and warn, which is the pre-existing behaviour and is
    ## what keeps the repo configurable on a box with no working runtime at all.
    if(CQOPS_DEBUG_TOOLCHAIN STREQUAL "ON")
        list(JOIN candidates ", " tried)
        message(FATAL_ERROR
            "cqops: CQOPS_DEBUG_TOOLCHAIN=ON but no C compiler on this host runs "
            "an -fsanitize=address binary. Tried the default "
            "(${CQOPS_DEFAULT_CC}) and: ${tried}. Install a working toolchain "
            "(Homebrew LLVM: `brew install llvm`), name one with "
            "-DCMAKE_C_COMPILER=, or use CQOPS_DEBUG_TOOLCHAIN=AUTO to build "
            "with whatever this host has.")
    endif()

    message(STATUS
        "cqops: no ASan-capable C compiler found — Debug will be UBSan-only "
        "(bd 6wg). `brew install llvm` restores it; CQOPS_DEBUG_TOOLCHAIN=ON "
        "makes its absence a configure error.")
endfunction()

## _cqops_probe_asan_compiler(<cc> <out>) — 1 iff a trivial program built by <cc>
## with -fsanitize=address both LINKS and RUNS.
##
## RUNNING IS THE WHOLE POINT AND COMPILING PROVES NOTHING: Apple clang builds the
## probe happily and the binary then dies in the ASan runtime's own initialiser,
## which is why `bd 6wg` existed at all. Cached per compiler so a reconfigure does
## not pay for it twice; `cmake --fresh`, or deleting the CQOPS_ASAN_RUNS_* entry,
## re-measures after a toolchain upgrade.
function(_cqops_probe_asan_compiler cc out)
    string(MAKE_C_IDENTIFIER "${cc}" key)
    set(cache_var "CQOPS_ASAN_RUNS_${key}")
    if(DEFINED ${cache_var})
        set(${out} "${${cache_var}}" PARENT_SCOPE)
        return()
    endif()

    set(dir "${CMAKE_BINARY_DIR}/CMakeFiles/cqops-asan-probe")
    file(MAKE_DIRECTORY "${dir}")
    file(WRITE "${dir}/probe.c" "int main(void) { return 0; }\n")
    set(exe "${dir}/probe")
    file(REMOVE "${exe}")

    set(ok 0)
    execute_process(
        COMMAND "${cc}" -fsanitize=address "${dir}/probe.c" -o "${exe}"
        RESULT_VARIABLE build_rc OUTPUT_QUIET ERROR_QUIET)
    if(build_rc EQUAL 0 AND EXISTS "${exe}")
        execute_process(COMMAND "${exe}"
                        RESULT_VARIABLE run_rc OUTPUT_QUIET ERROR_QUIET)
        if(run_rc EQUAL 0)
            set(ok 1)
        endif()
    endif()

    set(${cache_var} ${ok} CACHE INTERNAL "an -fsanitize=address binary runs with ${cc}")
    set(${out} ${ok} PARENT_SCOPE)
endfunction()
