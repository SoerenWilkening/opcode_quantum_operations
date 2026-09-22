foreach(required IN ITEMS
        cqops_source_dir cqops_build_dir cqops_install_libdir
        cqops_cqlang_dir cqops_cqlang_build cqops_python cqops_c_compiler)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

set(work "${cqops_build_dir}/package-install-test")
set(prefix "${work}/prefix")
set(consumer "${work}/consumer")
file(REMOVE_RECURSE "${work}")

set(cq_install "${CMAKE_COMMAND}" --install
               "${cqops_cqlang_dir}/${cqops_cqlang_build}"
               --prefix "${prefix}")
set(cqops_install "${CMAKE_COMMAND}" --install "${cqops_build_dir}"
                  --prefix "${prefix}")
if(DEFINED cqops_config AND NOT "${cqops_config}" STREQUAL "")
    list(APPEND cq_install --config "${cqops_config}")
    list(APPEND cqops_install --config "${cqops_config}")
endif()

foreach(command IN ITEMS cq_install cqops_install)
    execute_process(COMMAND ${${command}} RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${command} failed: ${result}")
    endif()
endforeach()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
            -S "${cqops_source_dir}/tests/package-consumer"
            -B "${consumer}"
            "-DCMAKE_C_COMPILER=${cqops_c_compiler}"
            "-DCMAKE_PREFIX_PATH=${prefix}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "installed consumer configuration failed: ${result}")
endif()

set(build_command "${CMAKE_COMMAND}" --build "${consumer}")
if(DEFINED cqops_config AND NOT "${cqops_config}" STREQUAL "")
    list(APPEND build_command --config "${cqops_config}")
endif()
execute_process(COMMAND ${build_command} RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "installed consumer build failed: ${result}")
endif()

set(executable "${consumer}/cqops_package_consumer")
if(DEFINED cqops_config AND
   EXISTS "${consumer}/${cqops_config}/cqops_package_consumer")
    set(executable "${consumer}/${cqops_config}/cqops_package_consumer")
endif()
execute_process(COMMAND "${executable}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "installed consumer failed: ${result}")
endif()

set(package_dir "${prefix}/${cqops_install_libdir}/cmake/CQBackend")
execute_process(
    COMMAND "${cqops_python}"
            "${cqops_cqlang_dir}/tools/backend_contract.py"
            --root "${cqops_cqlang_dir}" check
            --archive "${prefix}/${cqops_install_libdir}/libcqops.a"
            --manifest "${package_dir}/backend-manifest.json"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "installed backend contract check failed: ${result}")
endif()

set(map "${consumer}/cqops-package-consumer.map")
if(NOT EXISTS "${map}")
    message(FATAL_ERROR "consumer link map was not written: ${map}")
endif()
file(READ "${map}" map_text)
if(NOT map_text MATCHES "libcqops\\.a")
    message(FATAL_ERROR "consumer link map did not pull in libcqops.a")
endif()
if(map_text MATCHES "libcq_templates\\.a|libcq_runtime\\.a")
    message(FATAL_ERROR
        "CQ trace archive supplied symbols to the installed backend consumer")
endif()

message(STATUS
    "installed CQBackend consumer ran; libcqops.a present and CQ trace archives absent")
