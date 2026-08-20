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

## -ffp-contract=off is NOT a style choice and NOT an optimisation setting: it
## makes M21's classification a function of the ANGLE rather than of the host.
## Measured at Step 18 — `a*b + c` may be contracted into one fused multiply-add
## at the compiler's discretion (clang defaults to `on` for C, gcc to `fast`),
## and src/angle.c's residual `fabs(theta - k*CQ_ANGLE_PI)` is exactly that
## shape. Contracted, it measures against the EXACT product; uncontracted,
## against the rounded one. Building the same source with -march=native changed
## 1,429 of 250,000 near-lattice classifications, first at
## theta = 0x1.019c501fbacffp+9 (GENERAL uncontracted, IDENTITY fused).
##
## BOTH ANSWERS SATISFY D10 — the fused one is strictly more accurate — so this
## is a REPRODUCIBILITY fix, not a correctness one. It matters because M22 turns
## these rows into emitted gates at Step 19 and this project pins gate counts as
## L4 goldens (risk R5): a golden that moved with the host's -march would be
## unpinnable. Every module below Layer 4 is integer-only, so the flag costs
## nothing anywhere else.
target_compile_options(cqops_build_flags INTERFACE
    -Wall -Wextra -Werror -Wconversion
    -ffp-contract=off
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
    src/shadow.c        # M02, Step 3 — the §3 shadow table (M01 bit.h is
                        # header-only and carries no translation unit)
    src/qubits.c        # M03, Step 4 — pool, LIFO free list, ceiling, peak
    src/sink.c          # M04, Step 5 — the §8 vtable, dispatch, CQOPS_SINK
    src/ctx.c           #      Step 6 — the shared context aggregate
    src/emit.c          # M05, Step 6 — the §3 fold table (critical path)
    src/reg.c           # M07, Step 7 — handle table, tombstones, I2, D7a/D7b
    src/scratch.c       # M08, Step 8 — the scratch cq_bit array (no qubits)
    src/sandwich.c      # M09, Step 8 — the driver, I6(a)+(b), the epilogue
    src/sink_printf.c   # M23, Step 9 — the default sink, one line per gate
    src/sink_count.c    # M24, Step 9 — per-kind totals, T-count = 7×Toffoli

    # Layer 3 — kernels. All independent of each other (plan §5).
    src/kernels/bitwise.c      # M10, Step 10 — K1 xor, K2 and, K3 or; naturally
                               # clean, no sandwich (kernel.h is header-only and
                               # carries no translation unit, like bit.h)
    src/kernels/shift_const.c  # M11, Step 11 — K4 constant shl/lshr/ashr, D8
    src/kernels/cast.c         # M13, Step 11 — K5 sext/zext/trunc, two widths
    src/kernels/add.c          # M14, Step 12 — K6 add, K7 sub. THE FIRST
                               # SANDWICH USERS: ripple-carry per PRD §4, not
                               # Cuccaro (that is K8, Step 15)
    src/kernels/cmp.c          # M16, Step 13 — K9 icmp, all ten predicates.
                               # eq/ult/slt are ported; the other seven are
                               # lower_icmp!'s own dispatch over them
    src/kernels/mux.c          # M17, Step 14 — K10 select. THREE sources, which
                               # bd ckd.15 resolved rather than smuggled
    src/kernels/shift_var.c    # M12, Step 14 — variable shl/lshr/ashr as a
                               # barrel over M17's block, exactly as upstream
                               # composes them; a classical amount goes to M11
    src/kernels/addacc.c       # M15, Step 15 — K8 Cuccaro. NOT a Rule 7 kernel:
                               # `acc += b`, in place, destructive, and its
                               # inverse is the reverse circuit rather than a
                               # re-run. Exists solely as K11's accumulator
    src/kernels/mul.c          # M18, Step 16 — K11 mul, shift-add over M15's
                               # accumulator. THE DELIBERATE DELTA FROM UPSTREAM:
                               # multiplier.jl:29 calls the ripple lower_add!,
                               # and substituting Cuccaro is what collapses
                               # scratch from 3W^2+W to W^2+2W. Self-pinned —
                               # this composition exists nowhere in Bennett
    src/kernels/divrem_u.c     # M19, Step 17 — K12 udiv/urem, PRD §15 D9. FLAT
                               # scratch, ONE sandwich, W iterations of M16's
                               # cq_ult_step + M14's cq_sub_step + M17's
                               # cq_mux_step. TRANSCRIBES NO GATE LIST: it is a
                               # step-index map, a scratch layout and the L5
                               # short-circuit. The largest object in v1 —
                               # 8W^2+4W-1 qubits, 131,583 at the shipped i128
    src/kernels/divrem_s.c     # M20, Step 17 — K12 sdiv/srem. Sign-magnitude
                               # over M19, with the ONE gate list K12 does not
                               # get from a sibling: _cond_negate_inplace!

    # Layer 4 — the analog surface. Everything above this line is a classical
    # permutation over {X, CX, CCX} (Rule 4), which is the surface the two-bit
    # shadow is EXACT on and the surface cq_pc_zero_proof_rotation_free rests
    # on. M21 is where that stops being the whole story — though it emits
    # nothing itself: it decides which of PRD §7's rows an angle sits on, and
    # M22 (Step 19) is what acts on the answer.
    src/angle.c                # M21, Step 18 — §7's row selection. Pure, no
                               # internal dependencies, no state but one
                               # configurable tolerance
    src/rotate.c               # M22, Step 19 — §7's twelve cells and the
                               # terminal measurement. THE FIRST CALLER OF
                               # cq_shadow_rotate IN src/: the rotation-free
                               # surface ends here, on exactly one of the
                               # twelve cells, the general-Ry row's two
                               # (PRD §15 D12), and
                               # cq_pc_zero_proof_rotation_free stops being
                               # exact for anything that meets that cell
)

target_include_directories(cqops
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(cqops PRIVATE cqops_build_flags)

## libm, for M21's round and fabs (and fmod/fma in test_angle_oracles.inc).
## M22 needs none of it: rotate.c does no float arithmetic — it passes theta
## through and hands one constant to sink.rz.
##
## NOT a new dependency — <math.h> is part of the C standard library, and PRD §14's
## "nothing beyond libc" is intact. It is a LINK detail that differs by platform: on
## macOS libm is folded into libSystem and the link succeeds without it, which is
## exactly why this is easy to omit here and discover on CI (bd kju) instead. glibc
## ships libm separately and `round` would be an undefined symbol.
##
## PUBLIC, so it propagates to every test binary through cqops_test_support.
find_library(CQOPS_LIBM m)
if(CQOPS_LIBM)
    target_link_libraries(cqops PUBLIC ${CQOPS_LIBM})
endif()

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
