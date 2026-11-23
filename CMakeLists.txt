cmake_minimum_required(VERSION 3.20)
project(cq_ops VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_compile_options(-Wall -Wextra)
# The tests run the replay backend and the sampled harness; unoptimised they take twice as long.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

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

add_library(cq_backend STATIC src/templates/bits.c src/templates/cast.c src/emit.c src/rails.c)
add_library(cq_gates_replay STATIC src/gates_replay.c)
# Second gate backend: prints a qec-trace-viewer trace instead of replaying (src/gates_trace.c).
add_library(cq_gates_trace STATIC src/gates_trace.c)
# Third: only counts gates, depth and qubits (src/gates_count.c).
add_library(cq_gates_count STATIC src/gates_count.c)

enable_testing()
add_executable(test_skeleton tests/test_skeleton.c)
target_link_libraries(test_skeleton cq_backend cq_gates_replay)
add_test(NAME skeleton COMMAND test_skeleton)

# Tests always assert, whatever the build type.
string(REGEX REPLACE "-DNDEBUG" "" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
add_executable(test_replay tests/test_replay.c)
target_link_libraries(test_replay cq_gates_replay)
add_test(NAME replay COMMAND test_replay)
add_executable(test_trace tests/test_trace.c)
target_link_libraries(test_trace cq_gates_trace)
add_test(NAME trace COMMAND sh -c "CQ_TRACE=test_trace.out \"$1\" && cmp test_trace.out \"$2\""
  trace $<TARGET_FILE:test_trace> ${CMAKE_SOURCE_DIR}/tests/trace_expected.txt)
add_executable(test_count tests/test_count.c)
target_link_libraries(test_count cq_gates_count)
add_test(NAME count COMMAND sh -c "CQ_COUNT=test_count.out \"$1\" && cmp test_count.out \"$2\""
  count $<TARGET_FILE:test_count> ${CMAKE_SOURCE_DIR}/tests/count_expected.txt)

add_executable(test_emit tests/test_emit.c)
target_link_libraries(test_emit cq_backend cq_gates_replay)
add_test(NAME emit COMMAND test_emit)

# The sampled operation harness every operation test links (tests/harness.h).
add_library(cq_test_harness STATIC tests/harness.c)
target_include_directories(cq_test_harness PUBLIC tests)
target_link_libraries(cq_test_harness PUBLIC cq_backend cq_gates_replay)

add_executable(test_harness tests/test_harness.c)
target_link_libraries(test_harness cq_test_harness)
add_test(NAME harness COMMAND test_harness)

# Integer kernels and their per-width choice (kmgy.9). kernel_choice.c is generated and
# checked in; regenerate it and docs/DEPTH.md with: cmake --build <build> --target depth_tables
target_sources(cq_backend PRIVATE src/kernels/registry.c src/kernels/parity.c src/kernels/kernel_choice.c)
target_include_directories(cq_backend PUBLIC src/kernels)
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
target_sources(cq_backend PRIVATE src/core.c)
add_executable(test_core tests/test_core.c)
target_link_libraries(test_core cq_test_harness)
add_test(NAME core COMMAND test_core)

# Integer templates §2 (kmgy.25 on): public header include/cq_template.h, sources src/templates/,
# each calling its registry kernel. Nothing outside src/templates/ references a template (templates_layering).
target_sources(cq_backend PRIVATE src/kernels/bits.c)
add_executable(test_tpl_bits tests/test_tpl_bits.c)
target_link_libraries(test_tpl_bits cq_test_harness)
add_test(NAME tpl_bits COMMAND test_tpl_bits)
# Layering inside the one archive: no object outside src/templates/ mentions a cq_template_* symbol, and
# the template objects do define some (the check cannot pass vacuously). By object, not archive member:
# src/kernels/bits.c and src/templates/bits.c share a member name.
add_test(NAME templates_layering COMMAND sh -c
  "nm=$1; IFS=';'; set -- $2; unset IFS; t=0; for o; do s=$($nm \"$o\") || exit 1; case $o in */src/templates/*) echo \"$s\" | grep -q ' T _*cq_template_' && t=1;; *) echo \"$s\" | grep cq_template_ && { echo \"$o\"; exit 1; };; esac; done; [ $t = 1 ]"
  nm_check ${CMAKE_NM} "$<TARGET_OBJECTS:cq_backend>")

# IR calling-convention test (kmgy.29, kmgy.33): tests/ir/caller.ll is LLVM IR calling the entry points
# the way CQ_lang's compiled code does. The reference compiler for it is the LLVM CQ_lang pins, 19.1.7
# (brew llvm@19, docs/ABI_SYMBOLS.md); without it the build's clang compiles it, and without either
# the ir_abi tests are not built. ir_abi_reference is Skipped, saying why, unless 19.1.7 ran.
# Built at -O0, -O2 and the build's own flags.
set(CQ_LLVM_PREFIX "/usr/local/opt/llvm@19" CACHE PATH "LLVM whose clang compiles tests/ir/caller.ll (reference: 19.1.7)")
set(cq_ir_cc "${CQ_LLVM_PREFIX}/bin/clang")
if(NOT EXISTS "${cq_ir_cc}")
  set(cq_ir_cc "")
  if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(cq_ir_cc "${CMAKE_C_COMPILER}")
  endif()
endif()
if(cq_ir_cc)
  execute_process(COMMAND "${cq_ir_cc}" --version OUTPUT_VARIABLE cq_ir_ver ERROR_QUIET)
  string(REGEX MATCH "clang version [0-9.]+" cq_ir_ver "${cq_ir_ver}")
  set(cq_ir_why "tests/ir/caller.ll compiled by ${cq_ir_cc} (${cq_ir_ver}), not the reference LLVM 19.1.7")
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
      COMMAND ${cq_ir_cc} ${cq_flags} -w -c -x ir ${CMAKE_SOURCE_DIR}/tests/ir/caller.ll -o ${cq_obj}
      DEPENDS tests/ir/caller.ll VERBATIM)
    add_executable(test_ir_abi_${cq_tag} tests/ir/driver.c ${cq_obj})
    target_link_libraries(test_ir_abi_${cq_tag} cq_backend cq_gates_replay)
    set_target_properties(test_ir_abi_${cq_tag} PROPERTIES ENABLE_EXPORTS ON)   # the tape's callee, dlsym
    add_test(NAME ir_abi_${cq_tag} COMMAND test_ir_abi_${cq_tag})
  endforeach()
else()
  set(cq_ir_why "ir_abi tests not built: no ${CQ_LLVM_PREFIX}/bin/clang and the C compiler is ${CMAKE_C_COMPILER_ID}")
endif()
if(cq_ir_ver STREQUAL "clang version 19.1.7")
  add_test(NAME ir_abi_reference COMMAND ${CMAKE_COMMAND} -E echo "tests/ir/caller.ll compiled by ${cq_ir_cc} (19.1.7)")
else()
  message(STATUS "${cq_ir_why}")
  add_test(NAME ir_abi_reference COMMAND sh -c "echo \"$1\"; exit 77" ir "${cq_ir_why}")
  set_tests_properties(ir_abi_reference PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Integer casts (kmgy.15): src/kernels/cast.c in the runtime, src/templates/cast.c in the templates.
target_sources(cq_backend PRIVATE src/kernels/cast.c)
add_executable(test_tpl_cast tests/test_tpl_cast.c)
target_link_libraries(test_tpl_cast cq_test_harness)
add_test(NAME tpl_cast COMMAND test_tpl_cast)
# Exactly the listed pairs are defined: 3 ops x 2 forms x pair count, no other cast symbol.
add_test(NAME tpl_cast_symbols COMMAND sh -c
  "n=$(\"$1\" --count) && c=$(${CMAKE_NM} \"$2\" | grep -cE ' T _*cq_template_(zext|sext|trunc)_i[0-9]+_to_i[0-9]+(_unc)?$') && [ \"$n\" = \"$c\" ]"
  nm_check $<TARGET_FILE:test_tpl_cast> $<TARGET_FILE:cq_backend>)

# Rotations with a classical angle (kmgy.7): src/rot.c, the host k(a) in src/angle_k.c.
target_sources(cq_backend PRIVATE src/rot.c src/angle_k.c)
add_executable(test_rot tests/test_rot.c)
target_link_libraries(test_rot cq_test_harness)
target_include_directories(test_rot PRIVATE src)
target_compile_definitions(test_rot PRIVATE CQ_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME rot COMMAND test_rot)

# Memory bus, classical address (kmgy.19): src/bus.c, the non-faulting host access in src/hostmem.c.
target_sources(cq_backend PRIVATE src/bus.c src/hostmem.c)
add_executable(test_bus tests/test_bus.c)
target_link_libraries(test_bus cq_test_harness)
add_test(NAME bus COMMAND test_bus)

# Bitwise and shifts (kmgy.13): src/kernels/{bitwise,shift}.c in the runtime, src/templates/{bitwise,shift}.c.
target_sources(cq_backend PRIVATE src/kernels/bitwise.c src/kernels/shift.c)
target_sources(cq_backend PRIVATE src/templates/bitwise.c src/templates/shift.c)
add_executable(test_tpl_bitwise tests/test_tpl_bitwise.c)
target_link_libraries(test_tpl_bitwise cq_test_harness)
add_test(NAME tpl_bitwise COMMAND test_tpl_bitwise)

# Add, sub, overflow and saturating ops (kmgy.10): the adder and tape in src/kernels/adder.c, the op
# kernels in src/kernels/arith.c (runtime), the templates in src/templates/arith.c.
target_sources(cq_backend PRIVATE src/kernels/adder.c src/kernels/arith.c)
target_sources(cq_backend PRIVATE src/templates/arith.c)
add_executable(test_tpl_arith tests/test_tpl_arith.c)
target_link_libraries(test_tpl_arith cq_test_harness)
add_test(NAME tpl_arith COMMAND test_tpl_arith)

# Tape and abort (kmgy.22): src/tape.c. Host callees are found by name (dlsym); the test exports
# its own callees for that.
target_sources(cq_backend PRIVATE src/tape.c)
target_link_libraries(cq_backend PUBLIC ${CMAKE_DL_LIBS})
add_executable(test_tape tests/test_tape.c)
target_link_libraries(test_tape cq_test_harness)
set_target_properties(test_tape PROPERTIES ENABLE_EXPORTS ON)
add_test(NAME tape COMMAND test_tape)

# cq_invert begin/end (kmgy.8): src/invert.c; every reversible entry point goes through cq_call_do.
target_sources(cq_backend PRIVATE src/invert.c)
add_executable(test_invert tests/test_invert.c)
target_link_libraries(test_invert cq_test_harness)
set_target_properties(test_invert PROPERTIES ENABLE_EXPORTS ON)   # its effect callee is found by dlsym
add_test(NAME invert COMMAND test_invert)

# Bit counting ctpop, ctlz, cttz (kmgy.16): src/kernels/count.c in the runtime, src/templates/count.c.
target_sources(cq_backend PRIVATE src/kernels/count.c)
target_sources(cq_backend PRIVATE src/templates/count.c)
add_executable(test_tpl_count tests/test_tpl_count.c)
target_link_libraries(test_tpl_count cq_test_harness)
add_test(NAME tpl_count COMMAND test_tpl_count)

# Divide and remainder (kmgy.12): src/kernels/div.c in the runtime, src/templates/div.c in the templates.
target_sources(cq_backend PRIVATE src/kernels/div.c)
target_sources(cq_backend PRIVATE src/templates/div.c)
add_executable(test_tpl_div tests/test_tpl_div.c)
target_link_libraries(test_tpl_div cq_test_harness)
add_test(NAME tpl_div COMMAND test_tpl_div)

# Multiply (kmgy.11): the multiplier and its kernels in src/kernels/mul.c (runtime), src/templates/mul.c.
target_sources(cq_backend PRIVATE src/kernels/mul.c)
target_sources(cq_backend PRIVATE src/templates/mul.c)
add_executable(test_tpl_mul tests/test_tpl_mul.c)
target_link_libraries(test_tpl_mul cq_test_harness)
add_test(NAME tpl_mul COMMAND test_tpl_mul)

# Memory bus, quantum address (kmgy.21): the fan-out QRAM of docs/QRAM_CONSTRUCTION.md, src/qram.c.
target_sources(cq_backend PRIVATE src/qram.c src/note.c)   # note.c: the derivation note, ABI §1.10 "Objects"
add_executable(test_qram tests/test_qram.c)
target_link_libraries(test_qram cq_test_harness)
add_test(NAME qram COMMAND test_qram)
add_executable(test_objects tests/test_objects.c)   # the object registry, ABI §1.10 "Objects"
target_link_libraries(test_objects cq_test_harness)
add_test(NAME objects COMMAND test_objects)
add_executable(test_qram_obj tests/test_qram_obj.c)   # the object-bounded access against the bit pattern
target_link_libraries(test_qram_obj cq_test_harness)
add_test(NAME qram_obj COMMAND test_qram_obj)
add_executable(test_note tests/test_note.c)   # the derivation note and its rules
target_link_libraries(test_note cq_test_harness)
add_test(NAME note COMMAND test_note)

# Compare, select, min/max, abs (kmgy.14): src/kernels/cmp.c in the runtime, src/templates/cmp.c.
target_sources(cq_backend PRIVATE src/kernels/cmp.c)
target_sources(cq_backend PRIVATE src/templates/cmp.c)
add_executable(test_tpl_cmp tests/test_tpl_cmp.c)
target_link_libraries(test_tpl_cmp cq_test_harness)
add_test(NAME tpl_cmp COMMAND test_tpl_cmp)

# Angle ladder, quantum angle rail (kmgy.18): the reduction kernel in src/kernels/ladder.c, the rotations in
# src/ladder.c. CQ_LADDER_FULL=1 in the environment runs every vector fully quantum (slow; see the test).
target_sources(cq_backend PRIVATE src/ladder.c src/kernels/ladder.c)
add_executable(test_ladder tests/test_ladder.c)
target_link_libraries(test_ladder cq_test_harness)
target_include_directories(test_ladder PRIVATE src)
target_compile_definitions(test_ladder PRIVATE CQ_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME ladder COMMAND test_ladder)

# Link and completeness check (kmgy.23; NORTH_STAR "Done when" 1-3; docs/COMPLETENESS.md).
# abi_symbols: nm on libcq_backend.a against the in-scope list that tests/abi_expected.c writes from the ABI
# text, the out-of-scope and retired names, the seven operations, the references-nothing-undefined property and the test
# objects' references. abi_classical: one all-classical program over every family and width emits no gate.
add_executable(abi_expected tests/abi_expected.c)
add_test(NAME abi_symbols COMMAND sh ${CMAKE_SOURCE_DIR}/tests/abi_symbols.sh ${CMAKE_NM}
  $<TARGET_FILE:abi_expected> $<TARGET_FILE:cq_backend> ${CMAKE_BINARY_DIR})
add_executable(test_abi_classical tests/test_abi_classical.c)
target_link_libraries(test_abi_classical cq_backend cq_gates_replay)
add_test(NAME abi_classical COMMAND test_abi_classical)
# abi_link_*: tests/abi_link.ll, ABI calls only, compiled like tests/ir/caller.ll (above) and linked
# main.o libcq_backend.a <the seven operations>: the replay archive, then a second,
# independent definition (tests/abi_link_gates.c).
if(cq_ir_cc)
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/abi_link.o
    COMMAND ${cq_ir_cc} -w -c -x ir ${CMAKE_SOURCE_DIR}/tests/abi_link.ll -o ${CMAKE_BINARY_DIR}/abi_link.o
    DEPENDS tests/abi_link.ll VERBATIM)
  add_library(cq_abi_count_gates STATIC tests/abi_link_gates.c)
  foreach(cq_gates IN ITEMS cq_gates_replay cq_abi_count_gates)
    add_executable(test_abi_link_${cq_gates} ${CMAKE_BINARY_DIR}/abi_link.o)
    set_target_properties(test_abi_link_${cq_gates} PROPERTIES LINKER_LANGUAGE C)
    target_link_libraries(test_abi_link_${cq_gates} cq_backend ${cq_gates})
    add_test(NAME abi_link_${cq_gates} COMMAND test_abi_link_${cq_gates})
  endforeach()
else()
  add_test(NAME abi_link COMMAND sh -c "echo \"$1\"; exit 77" abi_link "${cq_ir_why}")
  set_tests_properties(abi_link PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Local examples (examples/ is not in the repository): each examples/<name>.c is built against the
# trace backend as example_<name>.
file(GLOB cq_examples examples/*.c)
foreach(cq_ex IN LISTS cq_examples)
  get_filename_component(cq_ex_name ${cq_ex} NAME_WE)
  add_executable(example_${cq_ex_name} ${cq_ex})
  target_link_libraries(example_${cq_ex_name} cq_backend cq_gates_trace)
endforeach()

# Install: the four archives and the public headers.
include(GNUInstallDirs)
install(TARGETS cq_backend cq_gates_replay cq_gates_trace cq_gates_count ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/cqrt.h include/cq_template.h include/cq_gates.h include/cq_replay.h
              include/cq_widths.h include/cq_cast_pairs.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
