## Python selection for M27's generator and its two suites (Step 22).
##
## PROBED, NOT ASSUMED — the same posture as cmake/CqopsSanitizers.cmake, and
## for the same reason: on this project's dev box the two python3 on PATH
## differ in whether they can run the generator at all. `python3` is pyenv
## 3.11.9 WITH PyYAML; /usr/bin/python3 is 3.9.6 WITHOUT it. Finding an
## interpreter is not enough, so the probe RUNS `import yaml` rather than
## checking a version, and what it selected is printed at configure time.
##
## IMPLEMENTATION_PLAN.md §3's M27 row anticipates this exactly: a reserve
## split seam (`the reader <-> the expander`, moving to shim/yamlmin.py) is
## recorded against the day CI forces a hand-rolled reader. Until that day the
## honest thing is to say out loud when the suites are not running.
##
## CQOPS_PYTHON is AUTO (default — register the suites if an interpreter can
## run them), ON (require one; a hard configure error otherwise) or OFF.
##
## When no interpreter qualifies the suites are NOT registered — deliberately
## rather than registered-and-skipped. A test that passes without running is
## the vacuous-green hazard test_harness_negative.c exists to rule out; a test
## count that visibly drops by two, next to a configure-time warning, is not.

function(cqops_select_python out_var)
    set(${out_var} "" PARENT_SCOPE)

    if(CQOPS_PYTHON STREQUAL "OFF")
        message(STATUS "cqops: python suites disabled by CQOPS_PYTHON=OFF")
        return()
    endif()

    set(candidates "")
    find_program(CQOPS_PYTHON3 python3)
    if(CQOPS_PYTHON3)
        list(APPEND candidates "${CQOPS_PYTHON3}")
    endif()
    list(APPEND candidates /usr/local/bin/python3 /opt/homebrew/bin/python3 /usr/bin/python3)

    foreach(py IN LISTS candidates)
        if(NOT EXISTS "${py}")
            continue()
        endif()
        # The probe is a RUN, not a version compare: PyYAML is what decides.
        execute_process(COMMAND "${py}" -c "import yaml"
                        RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
        if(rc EQUAL 0)
            execute_process(COMMAND "${py}" -c "import sys,yaml;print(sys.version.split()[0],yaml.__version__)"
                            OUTPUT_VARIABLE ver OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            message(STATUS "cqops: python suites enabled — ${py} (python ${ver} PyYAML)")
            set(${out_var} "${py}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    if(CQOPS_PYTHON STREQUAL "ON")
        message(FATAL_ERROR
            "cqops: CQOPS_PYTHON=ON but no python3 on this host can `import yaml`.\n"
            "  Tried: ${candidates}\n"
            "  Install PyYAML, or configure with -DCQOPS_PYTHON=AUTO to skip the shim suites.")
    endif()
    message(WARNING
        "cqops: NO python3 with PyYAML found — test_gen_shim and test_gen_bodies "
        "are NOT registered. Step 22's shim generator is unverified in this build. "
        "Tried: ${candidates}")
endfunction()
