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

    set_tests_properties(${name} PROPERTIES
        # abort_on_error/halt_on_error: a sanitizer report must fail the test,
        # not merely appear in its output.
        ENVIRONMENT "ASAN_OPTIONS=abort_on_error=1;UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1"
    )

    if(ARG_WILL_FAIL)
        set_tests_properties(${name} PROPERTIES WILL_FAIL TRUE)
    endif()
endfunction()
