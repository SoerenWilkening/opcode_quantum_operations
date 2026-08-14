## Test support library. Grows as the §2.2 harness files land: mock_sink at
## Step 5, refmodel / bitkinds / poolcheck across Phase B.

add_library(cqops_test_support STATIC
    support/harness.c
)

## src/ is PRIVATE on the cqops target, because none of it is public API. The
## suites need it anyway: plan §2.1 is one test binary per *module*, and the
## modules are internal — test_bit.c includes "bit.h" directly. Carrying the
## path here rather than widening cqops keeps the exception on the test side.
target_include_directories(cqops_test_support PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(cqops_test_support
    PUBLIC  cqops
    PRIVATE cqops_build_flags
)

## Suites. Layer order mirrors plan §3; each lands with its step.

add_cqops_test(test_skeleton)

## Layer 0 — primitives.
add_cqops_test(test_bit)          # M01, Step 2 — I1
add_cqops_test(test_shadow)       # M02, Step 3 — the §3 shadow update table

## M02's fail-loud path. NOT WILL_FAIL: that inverts a non-zero exit code and
## does not invert a crash, and this death is an abort(). The binary catches
## SIGABRT itself and exits 0 only if the abort arrived where expected, so it
## registers as an ordinary test — see the file header.
add_cqops_test(test_shadow_death)

## The harness's own negative self-test: expected to exit non-zero. It is what
## makes every other suite's green meaningful.
add_cqops_test(test_harness_negative WILL_FAIL)
