cmake_minimum_required(VERSION 3.20)
project(cqops_package_consumer C)

find_package(CQ CONFIG REQUIRED)
find_package(CQBackend CONFIG REQUIRED)

if(NOT TARGET CQBackend::backend)
    message(FATAL_ERROR "CQBackend package did not define CQBackend::backend")
endif()
if(NOT EXISTS "${CQBackend_PROVIDER_MANIFEST}")
    message(FATAL_ERROR
        "CQBackend provider manifest is missing: ${CQBackend_PROVIDER_MANIFEST}")
endif()

add_executable(cqops_package_consumer main.c)
target_link_libraries(cqops_package_consumer PRIVATE CQBackend::backend)

set(_map "${CMAKE_CURRENT_BINARY_DIR}/cqops-package-consumer.map")
if(APPLE)
    target_link_options(cqops_package_consumer PRIVATE "-Wl,-map,${_map}")
else()
    target_link_options(cqops_package_consumer PRIVATE "-Wl,-Map,${_map}")
endif()
