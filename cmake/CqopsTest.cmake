## _cqops_sanitizer_env(<out>) — the ENVIRONMENT property every test and every
## death case gets. ONE place, so the two registrars cannot drift.
##
## abort_on_error / halt_on_error: a sanitizer report must FAIL the test, not
## merely appear in its output.
##
## detect_leaks (`bd kfi`) is appended only when cmake/CqopsSanitizers.cmake has
## MEASURED that it works here — on a runtime without LSan the option is a fatal
## error that would break every test, and on Darwin it is off by default, so
## neither hard-coding it nor omitting it is right on its own.
##
## THE SHELL CANNOT DO THIS. CTest's ENVIRONMENT property WINS over the inherited
## environment, so `ASAN_OPTIONS=detect_leaks=1 ctest ...` is silently ignored —
## it was tried while measuring `bd 6wg` and produced a green run that proved
## nothing about leaks. The same fact CLAUDE.md records for CQOPS_UPDATE_GOLDENS.
##
## DEATH CASES ARE UNAFFECTED BY THE LEAK HALF, AND THAT IS WORTH STATING RATHER
## THAN DISCOVERING. Every exit path in tests/support/death.c is _Exit(), which
## skips atexit handlers — so LSan's end-of-process check never runs for a death
## case that actually died. It is set on them anyway: the option is harmless
## there, and a death binary whose case list path returns from main normally
## (argc != 2) is then checked like any other process.
function(_cqops_sanitizer_env out)
    set(asan "abort_on_error=1")
    if(CQOPS_LSAN_ENABLED)
        string(APPEND asan ":detect_leaks=1")
    endif()
    set(${out} "ASAN_OPTIONS=${asan};UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1"
        PARENT_SCOPE)
endfunction()

## add_cqops_test(<name> [WILL_FAIL] [SOURCES ...])
##
## One test binary per module (IMPLEMENTATION_PLAN §2.1). `name` is both the
## CTest test name and the basename of its source: add_cqops_test(test_bit)
## compiles tests/test_bit.c.
##
## WILL_FAIL registers a binary that is *expected* to exit non-zero. It exists
## for the harness's own negative self-test: a CHECK that cannot fail would
## make every later suite vacuously green, so the failure path is asserted
## rather than assumed.
##
## SOURCES adds extra translation units — the escape hatch for a suite whose
## large static table lives in a separate file (plan §2.3).

function(add_cqops_test name)
    cmake_parse_arguments(ARG "WILL_FAIL" "" "SOURCES" ${ARGN})

    add_executable(${name} "${CMAKE_CURRENT_SOURCE_DIR}/${name}.c" ${ARG_SOURCES})

    # cqops arrives transitively — cqops_test_support links it PUBLIC. Naming
    # it here as well is what makes Apple ld warn about a duplicate library.
    target_link_libraries(${name} PRIVATE
        cqops_test_support
        cqops_build_flags
    )

    add_test(NAME ${name} COMMAND ${name})

    _cqops_sanitizer_env(sanitizer_env)
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "${sanitizer_env}")

    if(ARG_WILL_FAIL)
        set_tests_properties(${name} PROPERTIES WILL_FAIL TRUE)
    endif()
endfunction()

## add_cqops_death_test(<name> CASES <case> [<case> ...])
##
## One binary hosting several fail-loud assertions, registered as one CTest
## test per case: the binary takes the case name on argv[1] and the process is
## gone after the abort, so it is one death per RUN but not one per FILE.
##
## Deliberately NOT WILL_FAIL. That property inverts a non-zero exit code and
## does not invert a crash, so it cannot express "this abort()s" at all — see
## tests/support/death.h. These binaries catch SIGABRT themselves and exit 0
## only when the abort landed in an armed window, which makes them ordinary
## tests and makes "nothing aborted" a failure.
##
## EVERY CASE IS ARMED WITH THE SANITIZER TRIPWIRE, at registration and again
## after the whole directory has been processed (`bd u76`). A death case cannot
## be written without it and cannot be silently disarmed by a later
## set_tests_properties block — see _cqops_death_tripwire below for what the
## tripwire is for and what it measured.

function(add_cqops_death_test name)
    cmake_parse_arguments(ARG "" "" "CASES" ${ARGN})

    if(NOT ARG_CASES)
        message(FATAL_ERROR "add_cqops_death_test(${name}): CASES is required")
    endif()

    add_executable(${name} "${CMAKE_CURRENT_SOURCE_DIR}/${name}.c")
    target_link_libraries(${name} PRIVATE cqops_test_support cqops_build_flags)

    ## `bd u76`: every death case is born with the sanitizer tripwire in its
    ## FAIL_REGULAR_EXPRESSION, and is recorded so that the deferred re-arm below
    ## can put the tripwire back after the per-case "which layer aborted" blocks
    ## in tests/CMakeLists.txt have overwritten this property. See
    ## _cqops_death_tripwire and cqops_death_arm_sanitizer_tripwire for why both
    ## halves are needed.
    _cqops_death_tripwire(tripwire)
    _cqops_sanitizer_env(sanitizer_env)

    foreach(case IN LISTS ARG_CASES)
        add_test(NAME ${name}.${case} COMMAND ${name} ${case})
        set_tests_properties(${name}.${case} PROPERTIES
            ENVIRONMENT "${sanitizer_env}"
            FAIL_REGULAR_EXPRESSION "${tripwire}"
        )
        set_property(GLOBAL APPEND PROPERTY CQOPS_DEATH_TESTS ${name}.${case})
    endforeach()

    ## Schedule the re-arm ONCE per directory, the first time a death test is
    ## registered there. DEFER runs it after everything else in the directory,
    ## which is what makes it immune to a set_tests_properties block added below
    ## any hand-placed call site.
    get_property(scheduled DIRECTORY PROPERTY CQOPS_DEATH_TRIPWIRE_SCHEDULED)
    if(NOT scheduled)
        set_property(DIRECTORY PROPERTY CQOPS_DEATH_TRIPWIRE_SCHEDULED TRUE)
        cmake_language(DEFER CALL cqops_death_arm_sanitizer_tripwire)
    endif()
endfunction()

## ---------------------------------------------------------------------------
## THE SANITIZER TRIPWIRE (`bd u76`) — the negative pin that separates "our
## die() ran" from "a sanitizer aborted", armed STRUCTURALLY on every death case
## rather than case by case.
##
## THE HOLE IT CLOSES. tests/support/death.h arms a SIGABRT window and exits 0
## when the abort lands inside it. It cannot tell WHOSE abort it was, and Debug
## builds with -fno-sanitize-recover=all, so a sanitizer's own abort() satisfies
## CQ_EXPECT_ABORT exactly as libcqops' die() does. Any death case whose setup or
## body acquires undefined behaviour therefore PASSES in Debug having verified
## nothing about the guard it is named for. MEASURED at Step 23: delete M07's
## NULL-proof guard in cq_reg_free and pass 2 calls through a NULL function
## pointer — Debug prints "SUMMARY: UndefinedBehaviorSanitizer: SEGV reg.c in
## cq_reg_free", aborts inside the window, exits 0, and the mutant SURVIVES;
## Release raises a raw SIGSEGV, which is not SIGABRT, so the case fails and the
## mutant is KILLED. The INVERSE of the usual configuration asymmetry, which is
## why a battery must be run in both (Rule 17).
##
## WHY FAIL_ AND NOT PASS_. FAIL_REGULAR_EXPRESSION COMPOSES with the exit-code
## check; PASS_REGULAR_EXPRESSION DISPLACES it, and would trade the death test's
## own contract ("it aborted, inside an armed window") for a substring match.
##
## WHY NOT A SIGSEGV HANDLER IN death.c. In Debug UBSan intercepts the signal
## first and ours would never run, so it would close the Release side — which is
## already closed, Release correctly fails — and leave Debug exactly as it is,
## while competing with the sanitizer's own handler.
##
## AddressSanitizer IS IN THE LIST, and since `bd 6wg` (2026-08-27) it is a regex
## that can actually match: Debug now selects a compiler whose ASan runtime runs,
## and ASan aborts under ASAN_OPTIONS=abort_on_error=1 exactly as UBSan does. It
## was added BEFORE that, when it could not match anything, on the argument that
## the hole is LARGER on a box whose ASan works and a dead regex costs nothing.
## That turned out to be the whole of the ASan side's preparation.
function(_cqops_death_tripwire out)
    set(${out} "UndefinedBehaviorSanitizer;AddressSanitizer" PARENT_SCOPE)
endfunction()

## Re-arms the tripwire over every death case registered in this directory,
## COMPOSING it into whatever FAIL_REGULAR_EXPRESSION each already carries.
##
## THIS EXISTS BECAUSE set_tests_properties OVERWRITES A PROPERTY RATHER THAN
## ADDING TO IT. A second block naming a test an earlier block already named
## silently disarms the earlier regex and leaves a green run with nothing to look
## at (measured at Step 19, and recorded in tests/CMakeLists.txt in four places).
## Arming the tripwire at registration time is therefore not enough on its own:
## the ~60 per-case blocks that pin WHICH LAYER ABORTED all run afterwards and
## would each wipe it. So the arm happens LAST, reads what is there, and puts it
## back with the tripwire prepended — which makes the hazard unrepresentable
## instead of merely absent, and means a new death case cannot be born without
## the tripwire because nobody has to remember to add it.
##
## It is scheduled with cmake_language(DEFER) rather than called at the bottom of
## tests/CMakeLists.txt, and that is the whole point: DEFER runs after EVERYTHING
## in the directory, including a set_tests_properties block appended below the
## call site by a future step. A hand-placed call would be one edit away from the
## Step 19 trap it exists to defeat.
function(cqops_death_arm_sanitizer_tripwire)
    _cqops_death_tripwire(tripwire)
    get_property(death_tests GLOBAL PROPERTY CQOPS_DEATH_TESTS)

    set(n 0)
    foreach(t IN LISTS death_tests)
        get_test_property(${t} FAIL_REGULAR_EXPRESSION existing)
        if(existing STREQUAL "NOTFOUND")
            set(existing "")
        endif()

        set(composed "${tripwire}")
        list(APPEND composed ${existing})
        list(REMOVE_DUPLICATES composed)

        set_tests_properties(${t} PROPERTIES
                             FAIL_REGULAR_EXPRESSION "${composed}")
        math(EXPR n "${n} + 1")
    endforeach()

    message(STATUS "cqops: sanitizer tripwire armed on ${n} death cases")
endfunction()
