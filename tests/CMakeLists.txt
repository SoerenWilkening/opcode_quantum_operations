## Test support library. Grows as the §2.2 harness files land: mock_sink at
## Step 5, refmodel / bitkinds / poolcheck across Phase B.

add_library(cqops_test_support STATIC
    support/harness.c
    support/death.c
    support/mock_sink.c
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
add_cqops_test(test_qubits)       # M03, Step 4 — pool, D2, D4, I3, peak
add_cqops_test(test_sink)         # M04, Step 5 — §8 vtable + mock_sink

## Layer 1 — emission.
add_cqops_test(test_emit_fold)    # M05, Step 6 — L0, 155 exhaustive cases

## Layer 2 — registers and the sandwich.
add_cqops_test(test_reg)          # M07, Step 7 — D5, tombstones, I4, D7a/D7b
add_cqops_test(test_scratch)      # M08, Step 8 — the cq_bit array, no qubits
add_cqops_test(test_sandwich)     # M09, Step 8 — the palindrome, I6(a)+(b)

## Layer 4 — sinks. M25 sink_qec is Step 26, against C_quantum_error_correction.
add_cqops_test(test_sink_printf)  # M23, Step 9 — the §8 format, %a, the flush
add_cqops_test(test_sink_count)   # M24, Step 9 — per-kind totals, T-count

## Fail-loud paths. NOT WILL_FAIL — that inverts a non-zero exit code and does
## not invert a crash, so it cannot express "this abort()s" at all; see
## tests/support/death.h.
add_cqops_death_test(test_shadow_death CASES
    reads_past_the_end
    writes_past_the_end
)

## The remaining 4 of Step 6's 159: the §3 distinctness asserts. Debug-gated
## by plan §2.1, so these SKIP in Release rather than reporting a false pass.
add_cqops_death_test(test_emit_death CASES
    cx_control_equals_target
    ccx_controls_are_the_same_qubit
    ccx_first_control_equals_target
    ccx_second_control_equals_target
    i6_target_outside_the_scratch_region
)

add_cqops_death_test(test_sink_death CASES
    missing_entry_is_not_called
    env_names_an_unknown_sink
    no_sink_registered_at_all
    registry_overflow
)

## M07, Step 7. The first fifteen are hard errors in BOTH configurations —
## D7a in particular carries no SKIP macro, because R2's value is firing during
## the Step 24 fixture run, which Rule 17 pins under Release. The four i2_*
## cases are Debug-only by plan §2.1 and SKIP in Release.
add_cqops_death_test(test_reg_death CASES
    free_without_proof
    free_dirty_rail
    a_zeroed_slot_is_not_a_valid_state
    double_free
    free_of_a_measured_rail
    read_after_free
    write_to_a_measured_rail
    handle_out_of_range
    handle_negative
    width_zero
    width_over_max
    handle_counter_exhausted
    copy_into_itself
    copy_between_mismatched_widths
    d7a_out_aliases_a_source
    operand_use_after_free
    i2_double_owned
    i2_self_double_owned
    i2_measured_rail_shares_a_qubit_with_a_live_one
    i2_owns_a_freed_qubit
    i2_invalid_bit
)

## WHICH LAYER ABORTED, not merely that something did. M07's free and M03's
## cq_qubits_release both guard "this qubit is not provably |0⟩", and plan §0
## records what that costs: at Step 6, DELETING M05's distinctness check still
## aborted, because M02 carried its own copy one layer down, so the death test
## passed on a broken library. Measured again here — removing M07's
## cq_reg_clean call left all 24 reg tests green, with the rail half-returned
## to the pool and only M03's message to show for it.
##
## FAIL_REGULAR_EXPRESSION is the fix and it composes: unlike
## PASS_REGULAR_EXPRESSION it does NOT displace the exit-code check, so the
## case still has to abort inside an armed window AND has to abort in M07.
set_tests_properties(test_reg_death.free_dirty_rail
                     test_reg_death.free_without_proof
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "release of a qubit not proven")

## M08 and M09, Step 8. Everything M08 guards is a hard error in BOTH
## configurations — a region disposed with a live qubit in it is a leaked
## ancilla, and Rule 17 pins L4 under Release. The two I6 DISCIPLINE sweeps in
## M09 are Debug-gated by plan §0.2 ("Run time (debug)") and SKIP in Release;
## the driver's O(1) premises and the epilogue are not.
add_cqops_death_test(test_scratch_death CASES
    dispose_with_a_materialised_bit
    dispose_with_a_constant_one_bit
    alloc_of_a_zero_width_region
    span_starts_past_the_end
    span_length_runs_off_the_end
)

add_cqops_death_test(test_sandwich_death CASES
    a_nested_sandwich
    an_unproven_release_is_refused_before_anything_is_retired
    a_copyout_step_rewrites_the_scratch_region
    a_scratch_bit_is_one_on_entry
    a_scratch_bit_is_already_a_qubit_on_entry
    a_multi_gate_step_is_not_an_involution
    a_null_compute_step_function
    a_copyout_count_with_no_copyout_function
    a_negative_step_count
    a_compute_step_writes_a_constant_into_scratch
    a_compute_step_targets_a_bit_outside_scratch
    the_reverse_pass_targets_a_bit_outside_scratch
    a_compute_step_rewrites_the_scratch_region
    the_reverse_pass_rewrites_the_scratch_region
)

## WHICH LOOP CAUGHT IT — the same discriminator as above, applied to three
## calls of ONE guard rather than to two modules. Measured with the mutation
## battery: the driver checks the region fingerprint after each of its three
## loops, and deleting ANY ONE of the three left all 65 tests green, because a
## later call caught what the deleted one would have. Naming the expected loop
## closes two of the three; the third is closed by a case that reaches the last
## check, which has nothing after it to lean on.
set_tests_properties(test_sandwich_death.a_compute_step_rewrites_the_scratch_region
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "a copyout step rewrote|the reverse compute half rewrote")

set_tests_properties(test_sandwich_death.a_copyout_step_rewrites_the_scratch_region
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "the reverse compute half rewrote")

## WHICH LAYER ABORTED, again — the Step 7 lesson applied where plan §2 said
## the next instance would be. A scratch bit that is ALREADY A QUBIT on entry
## is guarded twice: M09's entry sweep, and cq_materialise one layer down. Only
## the message tells them apart, so this pins that M09 caught it first. The
## sibling case, a_scratch_bit_is_one_on_entry, needs no property — nothing
## below M09 guards a constant ONE at all, which is exactly why it is the case
## that goes red if M09's sweep is deleted.
set_tests_properties(test_sandwich_death.a_scratch_bit_is_already_a_qubit_on_entry
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "materialise: bit is already a qubit")

## The ckd.17a certificate's Release backstop lives in M02, not M09, and this
## is what says so: a non-cancelling compute half must be caught by
## cq_shadow_retire's determinate-non-zero check, NOT by the Debug fingerprint
## and not by the pool's proven_zero guard — which M09 satisfies by design,
## since CQ_ZERO_BY_PALINDROME is a literal 1.
set_tests_properties(test_sandwich_death.a_multi_gate_step_is_not_an_involution
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "sandwich:|release of a qubit not proven")

## RELEASE FIRST, THEN RETIRE — cq_ctx_release_qubit's two statements, whose
## ORDER is the whole content of the ckd.17a certificate (PRD §10). Reversed,
## M02 would clear the shadow entry of a still-live qubit before M03 ever got
## to refuse the release. Both layers have something to say about this case;
## only the order decides which one speaks, and this is what hears it.
set_tests_properties(test_sandwich_death.an_unproven_release_is_refused_before_anything_is_retired
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "retire of a determinate")

add_cqops_death_test(test_qubits_death CASES
    ceiling_exceeded
    ceiling_set_below_live
    dirty_release
    release_of_a_qubit_left_at_one
    release_of_an_unminted_index
    double_release
)

## The harness's own negative self-test: expected to exit non-zero. It is what
## makes every other suite's green meaningful.
add_cqops_test(test_harness_negative WILL_FAIL)
