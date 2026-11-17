cmake_minimum_required(VERSION 3.20)
project(cq_ops VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_compile_options(-Wall -Wextra)

# Build-time configuration, passed to the code as compile definitions.
# Ladder precision N: 53, docs/ANGLE_LADDER.md (kmgy.17). Any N in [1, 128]; another N needs
# tests/ref/k_vectors.txt regenerated for it.
set(CQ_LADDER_N 53 CACHE STRING "Angle-ladder precision N (docs/ANGLE_LADDER.md)")
set(CQ_K2 4096 CACHE STRING "Input bound K2")
# PROVISIONAL default; revisit when the memory bus (QRAM) is designed.
set(CQ_MAX_QADDR_BITS 16 CACHE STRING "Cap on quantum address bits (provisional)")

add_compile_definitions(CQ_K2=${CQ_K2} CQ_MAX_QADDR_BITS=${CQ_MAX_QADDR_BITS})
if(NOT CQ_LADDER_N STREQUAL "")
  add_compile_definitions(CQ_LADDER_N=${CQ_LADDER_N})
endif()

include_directories(include)

add_library(cq_templates STATIC src/templates/bits.c src/templates/cast.c)
add_library(cq_runtime STATIC src/emit.c src/rails.c)
add_library(cq_gates_replay STATIC src/gates_replay.c)

enable_testing()
add_executable(test_skeleton tests/test_skeleton.c)
target_link_libraries(test_skeleton cq_templates cq_runtime cq_gates_replay)
add_test(NAME skeleton COMMAND test_skeleton)

# Tests always assert, whatever the build type.
string(REGEX REPLACE "-DNDEBUG" "" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
add_executable(test_replay tests/test_replay.c)
target_link_libraries(test_replay cq_gates_replay)
add_test(NAME replay COMMAND test_replay)

add_executable(test_emit tests/test_emit.c)
target_link_libraries(test_emit cq_runtime cq_gates_replay)
add_test(NAME emit COMMAND test_emit)

# The sampled operation harness every operation test links (tests/harness.h).
add_library(cq_test_harness STATIC tests/harness.c)
target_include_directories(cq_test_harness PUBLIC tests)
target_link_libraries(cq_test_harness PUBLIC cq_runtime cq_gates_replay)

add_executable(test_harness tests/test_harness.c)
target_link_libraries(test_harness cq_test_harness)
add_test(NAME harness COMMAND test_harness)

# Integer kernels and their per-width choice (kmgy.9). kernel_choice.c is generated and
# checked in; regenerate it and docs/DEPTH.md with: cmake --build <build> --target depth_tables
target_sources(cq_runtime PRIVATE src/kernels/registry.c src/kernels/parity.c src/kernels/kernel_choice.c)
target_include_directories(cq_runtime PUBLIC src/kernels)
add_library(cq_depth STATIC tools/depth.c)
target_include_directories(cq_depth PUBLIC tools)
target_link_libraries(cq_depth PUBLIC cq_test_harness)
add_executable(depth_table tools/depth_table.c)
target_link_libraries(depth_table cq_depth)
add_custom_target(depth_tables
  COMMAND depth_table ${CMAKE_SOURCE_DIR}/src/kernels/kernel_choice.c ${CMAKE_SOURCE_DIR}/docs/DEPTH.md
  COMMENT "Measuring kernel candidates: src/kernels/kernel_choice.c, docs/DEPTH.md")
add_executable(test_depth tests/test_depth.c)
target_link_libraries(test_depth cq_depth)
target_compile_definitions(test_depth PRIVATE CQ_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME depth COMMAND test_depth)

# Core §1 entry points (kmgy.6): public header include/cqrt.h.
target_sources(cq_runtime PRIVATE src/core.c)
add_executable(test_core tests/test_core.c)
target_link_libraries(test_core cq_test_harness)
add_test(NAME core COMMAND test_core)

# Integer templates §2 (kmgy.25 on): public header include/cq_template.h, sources src/templates/,
# each calling its registry kernel. The runtime never references a template.
target_link_libraries(cq_templates PUBLIC cq_runtime)
target_sources(cq_runtime PRIVATE src/kernels/bits.c)
add_executable(test_tpl_bits tests/test_tpl_bits.c)
target_link_libraries(test_tpl_bits cq_templates cq_test_harness)
add_test(NAME tpl_bits COMMAND test_tpl_bits)
add_test(NAME runtime_no_templates COMMAND sh -c
  "t=$(${CMAKE_NM} \"$1\") && r=$(${CMAKE_NM} \"$2\") || exit 1; echo \"$t\" | grep -q ' T _*cq_template_bswap_i32$' || exit 1; ! echo \"$r\" | grep cq_template_"
  nm_check $<TARGET_FILE:cq_templates> $<TARGET_FILE:cq_runtime>)

# IR calling-convention test (kmgy.29): tests/ir/caller.ll is LLVM IR calling the core entry points
# the way CQ_lang's compiled code does, so it needs clang. Built at -O0, -O2 and the build's own flags.
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
  string(TOUPPER "${CMAKE_BUILD_TYPE}" cq_bt)
  separate_arguments(cq_ir_build_flags NATIVE_COMMAND "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_${cq_bt}}")
  foreach(cq_tag IN ITEMS O0 O2 build)
    if(cq_tag STREQUAL "build")
      set(cq_flags ${cq_ir_build_flags})
    else()
      set(cq_flags -${cq_tag})
    endif()
    set(cq_obj ${CMAKE_BINARY_DIR}/ir_caller_${cq_tag}.o)
    add_custom_command(OUTPUT ${cq_obj}
      COMMAND ${CMAKE_C_COMPILER} ${cq_flags} -w -c -x ir ${CMAKE_SOURCE_DIR}/tests/ir/caller.ll -o ${cq_obj}
      DEPENDS tests/ir/caller.ll VERBATIM)
    add_executable(test_ir_abi_${cq_tag} tests/ir/driver.c ${cq_obj})
    target_link_libraries(test_ir_abi_${cq_tag} cq_runtime cq_gates_replay)
    add_test(NAME ir_abi_${cq_tag} COMMAND test_ir_abi_${cq_tag})
  endforeach()
else()
  message(STATUS "ir_abi tests skipped: the C compiler is ${CMAKE_C_COMPILER_ID}, compiling .ll needs clang")
endif()

# Integer casts (kmgy.15): src/kernels/cast.c in the runtime, src/templates/cast.c in the templates.
target_sources(cq_runtime PRIVATE src/kernels/cast.c)
add_executable(test_tpl_cast tests/test_tpl_cast.c)
target_link_libraries(test_tpl_cast cq_templates cq_test_harness)
add_test(NAME tpl_cast COMMAND test_tpl_cast)
# Exactly the listed pairs are defined: 3 ops x 2 forms x pair count, no other cast symbol.
add_test(NAME tpl_cast_symbols COMMAND sh -c
  "n=$(\"$1\" --count) && c=$(${CMAKE_NM} \"$2\" | grep -cE ' T _*cq_template_(zext|sext|trunc)_i[0-9]+_to_i[0-9]+(_unc)?$') && [ \"$n\" = \"$c\" ]"
  nm_check $<TARGET_FILE:test_tpl_cast> $<TARGET_FILE:cq_templates>)

# Rotations with a classical angle (kmgy.7): src/rot.c, the host k(a) in src/angle_k.c.
target_sources(cq_runtime PRIVATE src/rot.c src/angle_k.c)
add_executable(test_rot tests/test_rot.c)
target_link_libraries(test_rot cq_test_harness)
target_include_directories(test_rot PRIVATE src)
target_compile_definitions(test_rot PRIVATE CQ_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME rot COMMAND test_rot)

# Memory bus, classical address (kmgy.19): src/bus.c, the non-faulting host access in src/hostmem.c.
target_sources(cq_runtime PRIVATE src/bus.c src/hostmem.c)
add_executable(test_bus tests/test_bus.c)
target_link_libraries(test_bus cq_test_harness)
add_test(NAME bus COMMAND test_bus)

# Bitwise and shifts (kmgy.13): src/kernels/{bitwise,shift}.c in the runtime, src/templates/{bitwise,shift}.c.
target_sources(cq_runtime PRIVATE src/kernels/bitwise.c src/kernels/shift.c)
target_sources(cq_templates PRIVATE src/templates/bitwise.c src/templates/shift.c)
add_executable(test_tpl_bitwise tests/test_tpl_bitwise.c)
target_link_libraries(test_tpl_bitwise cq_templates cq_test_harness)
add_test(NAME tpl_bitwise COMMAND test_tpl_bitwise)

# Add, sub, overflow and saturating ops (kmgy.10): the adder and tape in src/kernels/adder.c, the op
# kernels in src/kernels/arith.c (runtime), the templates in src/templates/arith.c.
target_sources(cq_runtime PRIVATE src/kernels/adder.c src/kernels/arith.c)
target_sources(cq_templates PRIVATE src/templates/arith.c)
add_executable(test_tpl_arith tests/test_tpl_arith.c)
target_link_libraries(test_tpl_arith cq_templates cq_test_harness)
add_test(NAME tpl_arith COMMAND test_tpl_arith)

# Tape and abort (kmgy.22): src/tape.c. Host callees are found by name (dlsym); the test exports
# its own callees for that.
target_sources(cq_runtime PRIVATE src/tape.c)
target_link_libraries(cq_runtime PUBLIC ${CMAKE_DL_LIBS})
add_executable(test_tape tests/test_tape.c)
target_link_libraries(test_tape cq_test_harness)
set_target_properties(test_tape PROPERTIES ENABLE_EXPORTS ON)
add_test(NAME tape COMMAND test_tape)
