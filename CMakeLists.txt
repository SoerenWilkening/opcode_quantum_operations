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

## BEFORE project(), AND THAT IS FORCED: CMAKE_C_COMPILER is consumed by the C
## language enable and cannot be changed afterwards. Debug asks for ASan, and on
## this dev box the DEFAULT toolchain's ASan runtime is broken — so which
## compiler Debug uses is part of what "Debug" means here. Probed, never pinned;
## Release is untouched; an explicit -DCMAKE_C_COMPILER= always wins. See
## cmake/CqopsDebugToolchain.cmake and `bd 6wg` for the measurement.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(CQOPS_DEBUG_TOOLCHAIN "AUTO" CACHE STRING
    "Debug C compiler: AUTO (switch to one whose ASan runtime runs, if the \
default's does not), ON (require one), OFF (never switch)")
set_property(CACHE CQOPS_DEBUG_TOOLCHAIN PROPERTY STRINGS AUTO ON OFF)
include(CqopsDebugToolchain)
cqops_select_debug_toolchain()

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

## LeakSanitizer is a SEPARATE knob from the two above, and `bd kfi` is why. It
## ships inside the ASan runtime rather than as a -fsanitize= flag, it is OFF BY
## DEFAULT on Darwin, and it is turned on through ASAN_OPTIONS — an ENVIRONMENT
## property on each test, not a compile option. Probed like everything else here:
## on a platform whose runtime has no LSan, ASAN_OPTIONS=detect_leaks=1 is a
## FATAL that would break every test. See cmake/CqopsSanitizers.cmake.
set(CQOPS_LEAK_CHECK "AUTO" CACHE STRING
    "Debug leak detection: AUTO (on if it works), ON (require it), OFF")
set_property(CACHE CQOPS_LEAK_CHECK PROPERTY STRINGS AUTO ON OFF)

## M27's generator and its two suites need a python3 that can `import yaml`.
## Probed, not assumed — see cmake/CqopsPython.cmake.
set(CQOPS_PYTHON "AUTO" CACHE STRING
    "Shim generator suites: AUTO (register if an interpreter qualifies), ON (require one), OFF")
set_property(CACHE CQOPS_PYTHON PROPERTY STRINGS AUTO ON OFF)

## M25 (Step 26). THE PROJECT NAME `C_quantum_error_correction` IS A REPOSITORY,
## NOT AN ARTEFACT: the library is that repo's `qec/` subdirectory, built as
## libqec.a with its header at qec/qec.h, so a find_library spelled from the
## project name finds nothing (PRD §15 D19). Point this at that `qec/`.
##
## OPT-IN, like CQOPS_CQLANG_DIR and for the same reason: this repository
## neither pins nor can build the QEC library, and a report that cannot name the
## revision it ran against is not a report. Absent, `src/sink_qec.c` still
## compiles — its no-library arm registers nothing, so CQOPS_SINK=qec takes
## src/sink.c's existing hard error instead of a silent fallback.
set(CQOPS_QEC_DIR "" CACHE PATH
    "Path to a built C_quantum_error_correction/qec; enables the M25 qec sink")
set(CQOPS_QEC_BUILD "build" CACHE STRING
    "QEC build directory, relative to CQOPS_QEC_DIR")

set(CQOPS_HAVE_QEC 0)
if(CQOPS_QEC_DIR)
    set(_qec_inc "${CQOPS_QEC_DIR}/include")
    set(_qec_lib "${CQOPS_QEC_DIR}/${CQOPS_QEC_BUILD}")
    ## All four are checked by PATH rather than by find_library, because a
    ## partial answer here is worse than none: linking libqec.a without
    ## libtommath.a gives an undefined-symbol wall from the gridsynth driver,
    ## which reads as our bug rather than as a missing dependency.
    if(EXISTS "${_qec_inc}/qec/qec.h"
       AND EXISTS "${_qec_lib}/libqec.a"
       AND EXISTS "${_qec_lib}/libtommath.a"
       AND EXISTS "${_qec_lib}/libcjson.a")
        set(CQOPS_HAVE_QEC 1)
        ## Order is load-bearing for static archives: libqec references
        ## libtommath (gridsynth's bignum) and libcjson (the config loader).
        set(CQOPS_QEC_LIBS "${_qec_lib}/libqec.a"
                           "${_qec_lib}/libtommath.a"
                           "${_qec_lib}/libcjson.a")
        message(STATUS "cqops: qec sink enabled from ${CQOPS_QEC_DIR}")
    else()
        message(FATAL_ERROR
            "cqops: CQOPS_QEC_DIR=${CQOPS_QEC_DIR} is not a built qec/ — "
            "expected include/qec/qec.h and ${CQOPS_QEC_BUILD}/{libqec,libtommath,libcjson}.a")
    endif()
else()
    message(STATUS "cqops: qec sink (M25, Step 26) not enabled — "
                   "configure with -DCQOPS_QEC_DIR=<a built qec/> to route the "
                   "gate stream into qec_*")
endif()

## CMAKE_MODULE_PATH was already extended above project(), for CqopsDebugToolchain.
include(CqopsTest)
include(CqopsSanitizers)
include(CqopsPython)
include(CqopsFreePairingPin)

# R3 for the VENDORED ANALYSIS, at configure time. The certificate in shim/ is a
# PORT of third_party/cq_free_pairing/free_pairing_check.py (PRD §15 D15 §2,
# `bd 06t`), so those bytes are as load-bearing as third_party/bennett's are for
# a kernel — and until now nothing checked them. See the module for why this is a
# hard configure error and not a test.
cqops_check_free_pairing_pin()

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
set(CQOPS_LSAN_ENABLED  0)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    cqops_select_sanitizers(CQOPS_SANITIZER_FLAGS)
    ## AFTER the sanitizer selection, because it reads CQOPS_ASAN_ENABLED: LSan
    ## lives in that runtime and has nothing to attach to without it. Consulted
    ## only in Debug, exactly as CQOPS_SANITIZERS is — Release pins gate counts
    ## and has no use for a sanitizer runtime (risk R5).
    cqops_select_leak_check(CQOPS_LSAN_ENABLED)
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
    # And what it believes about LEAK detection, which __has_feature cannot
    # answer: LSan here is an ASAN_OPTIONS setting, so test_skeleton checks it
    # against the environment CTest actually handed the process (`bd kfi`).
    CQOPS_BUILD_LSAN=${CQOPS_LSAN_ENABLED}
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
    src/controlled.c    # M06, Step 20 — §9's promotion, the control stack,
                        # the shared ancilla, the nested AND, D11's refusal
    src/reg.c           # M07, Step 7 — handle table, tombstones, the free path
    src/reg_check.c     # M07, Step 23 — the I2 sweep and the D7a/D7b checks,
                        # split from reg.c on plan §3's RECORDED seam (`table
                        # ↔ invariant checking`, trigger 240) when D15's
                        # free-time disposition landed. One module, two TUs
    src/scratch.c       # M08, Step 8 — the scratch cq_bit array (no qubits)
    src/sandwich.c      # M09, Step 8 — the driver, I6(a)+(b), the epilogue
    src/sink_printf.c   # M23, Step 9 — the default sink, one line per gate
    src/sink_count.c    # M24, Step 9 — per-kind totals, T-count = 7×Toffoli
    src/sink_qec.c      # M25, Step 26 — the §8 vtable onto qec_*, D19's built
                        # ry/rz, the trace FILE* and its atexit, and the install
                        # hook that sets BOTH pool modes (D20 ceiling, D21 (b)
                        # no-recycle). COMPILED UNCONDITIONALLY: its no-library
                        # arm registers nothing, so CQOPS_SINK=qec keeps M04's
                        # hard error rather than falling back to printf
    src/sink_qec_angle.c # M25b, Step 26 — the SPLIT SEAM plan §3 recorded in
                        # advance: double → (p, q_denom) by continued fractions
                        # on θ/π at double-double precision, and D19's
                        # denominator cap. Depends on <math.h> and nothing else,
                        # so it is built and tested on a box with no QEC library

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

    # Layer 5 — the shim. M26's foundation, and the FIRST thing under shim/
    # that CMake compiles: Step 22 shipped a generator, a contract header and
    # 2479 generated bodies, and the only compiler of any of them was a
    # PyYAML-gated python subprocess. The other four M26 files and the ten
    # *.gen.c join this list as Steps 23.4-23.7 land.
    shim/cq_shim_ctx.c         # M26, Step 23 — the process-global cq_ctx and
                               # its reset, installation of the built-in sinks
                               # (bd utk), the ONE §9 region bracket (bd d6m
                               # fix (a)), and PRD §1's cq_shim_unsupported
    shim/cq_shim_proof.c       # M26, Step 23 — the free-time EVIDENCE. The
                               # library shipped no cq_zero_proof and a NULL one
                               # is a hard error for a qubit-owning rail, so
                               # cqrt_free could not be written without this.
                               # Landing 1 reads the SHADOW, three-valued;
                               # landing 2 adds D15's certificate beside it
    shim/cq_shim_record.c      # M26, Step 23 landing 2 — D15's per-handle
                               # OBSERVED WRITE HISTORY plus the EFFECT TABLE
                               # transcribed from cq_runtime.h. A CALL STREAM,
                               # never a gate list (Rule 13)
    shim/cq_shim_reduce.c      # M26, Step 23 landing 2 — the reduction, PORTED
                               # from the pinned
                               # third_party/cq_free_pairing/free_pairing_check.py.
                               # ONE engine; U1/U2/U3 are entry conditions into
                               # it, never three separate checkers (D15 §2)
    shim/cq_shim_trace.c       # M26, `bd 76r` — the ANNOTATION half (§15 D21):
                               # the `#REGISTER` header, assembled at END of
                               # program because handoff §3 wants every one of
                               # them before the FIRST bracket and CQ_lang
                               # allocates throughout, and the flat `op
                               # begin`/`op end` brackets every cqrt_* and
                               # cq_shim_* entry point opens. It prints NOTHING
                               # for a gate — those, and every #PATCH, are the
                               # QEC library's own. Inert under every sink but
                               # qec, and in a build without the library.
    shim/cq_runtime_rail.c     # M26, Step 23 — the rail surface: 32 symbols,
                               # alloc / measure / free / copy[_controlled] /
                               # cswap / addc / xorc
    shim/cq_runtime_gate.c     # M26, Step 23 — the gate surface: 30 symbols,
                               # x / cnot / toffoli / x_controlled /
                               # cnot_controlled / ry / rz / rz_controlled[_inv]
                               # / ry_controlled_inv
    shim/cq_runtime_v2.c       # M26, Step 23 — the V1 BOUNDARY: the 109 cqrt_*
                               # symbols v1 defers, as loud cq_shim_unsupported
                               # bodies (PRD §15 D16 — 34 fp core, 63 qram, 11
                               # tape, cqrt_alloc_handle). DEFINED rather than
                               # omitted because libcq_runtime.a is ONE object
                               # holding all 173 as T, so an undefined
                               # REFERENCED symbol pulls the whole member and
                               # gives a duplicate-symbol wall — or, worse and
                               # measured, a silent link in which CQ_lang's
                               # trace-only stub serves every call. cqrt_h and
                               # cqrt_h_controlled stay UNDEFINED on purpose:
                               # nothing references them, and Rule 4 means
                               # libcqops could not serve them at any point
    shim/cq_template_dispatch.c # M26, Step 23.6 — the opcode DISPATCH half:
                               # three tables indexed by the frozen ABI's own
                               # enums, each with a _Static_assert tying its
                               # length to the last enumerator, and every
                               # src/kernels/ include in the shim
    shim/cq_template_impl.c    # M26, Step 23.6 — the handle BOUNDARY: the
                               # FIFTEEN remaining cq_shim_* entry points that
                               # M28's 992 generated wrappers call. Handle
                               # resolution, D7a's refusal, D7b's defensive
                               # copy, minting dst at the RESULT width, and the
                               # §9 region through cq_shim_ctx.c's one bracket

    # M28 — THE GENERATED GRID, Step 23.7. LISTED EXPLICITLY AND NEVER
    # file(GLOB): a glob is evaluated at configure time and does not re-run when
    # a family file DISAPPEARS, so a regeneration that dropped one would link
    # green against a stale object and only fail once someone reconfigured for
    # an unrelated reason. That is risk R7 with no detector, and bd 216's
    # checklist item 16 names it. LOC-exempt by extension (tools/check_loc.sh
    # skips *.gen.c), not by location.
    #
    # 2479 symbols across ten families: 992 integer wrappers that call into
    # cq_template_impl.c, 603 integer _inv aborts (PRD §15 D14) and 884
    # fp-touching aborts (PRD §1), every abort naming its own symbol and its
    # bucket through cq_shim_unsupported.
    shim/generated/cq_template_fp_arith.gen.c
    shim/generated/cq_template_fp_compare.gen.c
    shim/generated/cq_template_fp_to_int.gen.c
    shim/generated/cq_template_fp_width.gen.c
    shim/generated/cq_template_int_arith.gen.c
    shim/generated/cq_template_int_bitwise.gen.c
    shim/generated/cq_template_int_compare.gen.c
    shim/generated/cq_template_int_to_fp.gen.c
    shim/generated/cq_template_int_width.gen.c
    shim/generated/cq_template_reinterpret.gen.c
)

target_include_directories(cqops
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    ## shim/ joins src/ here at Step 23: M26 is inside this repository, so it
    ## reaches the internals by include path rather than through a public
    ## header — which is the answer src/ctx.h reserved for this step, and the
    ## same posture tests/ has had since Step 2.
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/shim
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

## M25's library, when there is one. PUBLIC on all three counts, and each for
## its own reason: the archives so every test binary links them; the include
## path so tests/test_sink_qec.c can read qec_count back through <qec/qec.h>
## (src/sink_qec.h deliberately does not include it, so the module compiles
## identically in both builds); and the define so a suite can compile out what
## it cannot run rather than pass vacuously.
if(CQOPS_HAVE_QEC)
    target_compile_definitions(cqops PUBLIC CQOPS_HAVE_QEC=1)
    target_include_directories(cqops PUBLIC "${CQOPS_QEC_DIR}/include")
    target_link_libraries(cqops PUBLIC ${CQOPS_QEC_LIBS})
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
