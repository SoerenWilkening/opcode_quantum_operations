## libcqops — CQ_lang's quantum backend.
##
## Two configurations, and the tests run under BOTH (IMPLEMENTATION_PLAN §2.1):
##
##   Debug     defines CQOPS_DEBUG_INVARIANTS (the I2 owner map, the I6
##             scratch-extent check, the operand-distinctness asserts) and
##             builds with -fsanitize=address,undefined. This is where the
##             invariant checks live.
##   Release   compiles all of that out. This is what gets its gate counts
##             pinned — a count pinned only in Debug is not pinned
##             (CLAUDE.md, Rule 17).
##
## No dependencies beyond libc (PRD §14), mirroring CQ_lang's own build so the
## two link without ceremony.

cmake_minimum_required(VERSION 3.20)
project(cqops VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "cqops: no CMAKE_BUILD_TYPE given, defaulting to Debug")
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build configuration" FORCE)
endif()

set(CQOPS_SANITIZERS "AUTO" CACHE STRING
    "Debug sanitizers: AUTO (use what runs on this host), ON (require them), OFF")
set_property(CACHE CQOPS_SANITIZERS PROPERTY STRINGS AUTO ON OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CqopsTest)
include(CqopsSanitizers)

enable_testing()

## ---------------------------------------------------------------------------
## Shared build contract: warnings, sanitizers, the Debug invariant define.
##
## Carried on an INTERFACE target so the library and every test binary are
## compiled under identical terms. -Wconversion is deliberate: the whole
## codebase is width-generic over reg->width (I5), so implicit narrowing is a
## real hazard rather than a style question.
## ---------------------------------------------------------------------------

add_library(cqops_build_flags INTERFACE)

# Probed, not assumed — see cmake/CqopsSanitizers.cmake for why.
set(CQOPS_SANITIZER_FLAGS "")
set(CQOPS_ASAN_ENABLED  0)
set(CQOPS_UBSAN_ENABLED 0)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    cqops_select_sanitizers(CQOPS_SANITIZER_FLAGS)
endif()

target_compile_options(cqops_build_flags INTERFACE
    -Wall -Wextra -Werror -Wconversion
    $<$<CONFIG:Debug>:-fno-omit-frame-pointer;-g>
    ${CQOPS_SANITIZER_FLAGS}
)

target_link_options(cqops_build_flags INTERFACE
    ${CQOPS_SANITIZER_FLAGS}
)

target_compile_definitions(cqops_build_flags INTERFACE
    $<$<CONFIG:Debug>:CQOPS_DEBUG_INVARIANTS=1>
    # What the build BELIEVES it enabled. test_skeleton checks these against
    # the compiler's own __has_feature, so a sanitizer that was selected but
    # never reached the compile line is caught rather than assumed.
    CQOPS_BUILD_ASAN=${CQOPS_ASAN_ENABLED}
    CQOPS_BUILD_UBSAN=${CQOPS_UBSAN_ENABLED}
)

## ---------------------------------------------------------------------------
## The library.
##
## Modules land here as the steps complete (plan §3): Layer 0 primitives at
## Steps 2-5, emission at Steps 6 and 20, registers and the sandwich at
## Steps 7-8, kernels through Phase B. Today it carries only the version
## symbol, which is what gives the Step 1 gate a real link to prove.
## ---------------------------------------------------------------------------

add_library(cqops STATIC
    src/version.c
)

target_include_directories(cqops
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(cqops PRIVATE cqops_build_flags)

add_library(cqops::cqops ALIAS cqops)

add_subdirectory(tests)

## ---------------------------------------------------------------------------
## The 300-line guard (Rule 12). Also reachable from the repo root as
## `make lint`; both spellings run the one script.
## ---------------------------------------------------------------------------

add_custom_target(lint
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_loc.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "check_loc.sh: no hand-written .c/.h/.py over 300 lines"
    VERBATIM
)
