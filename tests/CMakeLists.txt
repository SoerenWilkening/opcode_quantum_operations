## Test support library. Grows as the §2.2 harness files land: mock_sink at
## Step 5, refmodel / bitkinds / poolcheck at Step 10 with the first kernels.
##
## kerneldrv and goldens are BEYOND §2.2's list of five, like death.c before
## them. Plan §4's Phase B gate asks for a "shared kernel driver" that applies
## L1-L4 "rather than written per kernel", and for "tests/goldens/,
## --update-goldens to regenerate", and assigns neither to a file. These are
## those two. TEN kernel modules and Step 20's controlled re-run all land
## against them.

add_library(cqops_test_support STATIC
    support/harness.c
    support/death.c
    support/mock_sink.c
    support/refmodel.c      # Step 10 — plain-C semantics, the L1 reference
    support/bitkinds.c      # Step 10 — (value, quantum-mask) -> register
    support/poolcheck.c     # Step 10 — the automatic L2 and L3 assertions
    support/kerneldrv.c     # Step 10 — the shared Phase-B four-level gate
    support/kernelsweep.c   # Step 11 — the sweep shapes (plan §2.2's split seam)
    support/goldens.c       # Step 10 — L4's pinned counts, on disk
)

## L4's goldens are DATA, read at run time, so the suite can compare the
## commit they were pinned against with the snapshot actually on disk — risk
## R3 ("Bennett.jl drift invalidates L4 goldens silently") mitigated by a
## check rather than by a comment nobody reads.
target_compile_definitions(cqops_test_support PUBLIC
    CQOPS_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/goldens"
    CQOPS_BENNETT_COMMIT="${CMAKE_SOURCE_DIR}/third_party/bennett/COMMIT"
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

## Layer 3 — kernels. The first exercise of the shared Phase-B driver.
add_cqops_test(test_kernel_bitwise)   # M10, Step 10 — K1/K2/K3, L1-L5
add_cqops_test(test_kernel_shift)     # M11, Step 11 — K4 constant shifts, D8
add_cqops_test(test_kernel_cast)      # M13, Step 11 — K5 casts, two widths, i128
add_cqops_test(test_kernel_add)       # M14, Step 12 — K6/K7, the first sandwiches
add_cqops_test(test_kernel_cmp)       # M16, Step 13 — K9, the ten icmp predicates
add_cqops_test(test_kernel_mux)       # M17, Step 14 — K10 select, THREE sources
add_cqops_test(test_kernel_shift_var) # M12, Step 14 — the barrel, and D8 across
                                      # the M11/M12 boundary
add_cqops_test(test_kernel_addacc)    # M15, Step 15 — K8 Cuccaro. THE FIRST
                                      # KERNEL THE SHARED DRIVER CANNOT DRIVE:
                                      # `acc += b` is not `dst ^= f(a,b)`, so
                                      # every level is restated by hand
add_cqops_test(test_kernel_mul)       # M18, Step 16 — K11 mul. THE FIRST
                                      # COMPOSITE whose inner construction is
                                      # another module's whole kernel, and the
                                      # most expensive thing the suite runs:
                                      # one case is 2(13W^2-8W) gates over
                                      # W^2+2W scratch qubits. Its sweep caps
                                      # are printed, in
                                      # tests/test_kernel_mul_sweep.inc

## The driver's own assertions, made falsifiable — test_harness_negative's
## argument one level up. A mutation battery found five of the Phase-B pool
## assertions surviving mutation to always-true: correct, load-bearing, and
## never once observed to fire. This provokes each one.
add_cqops_test(test_kerneldrv)        # Step 10 — the infrastructure's gate

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

## M10, Step 10. All five abort in BOTH configurations — D7a for M07's reason
## (src/reg.h): R2's value is firing during the Step 24 fixture run, which
## Rule 17 pins under Release.
add_cqops_death_test(test_kernel_bitwise_death CASES
    dst_aliases_a_quantum_source
    dst_aliases_a_classical_source
    dst_aliases_the_second_source
    dst_overlaps_a_source_without_sharing_a_base
    the_two_sources_are_the_same_register
    a_width_of_zero
    a_negative_width
)

## WHICH LAYER ABORTED, the fourth instance. `dst == a` is guarded by
## cq_kernel_check_dst here and — for a QUANTUM lane, in Debug only — by M05's
## §3 distinctness assert one level down. Without this property, deleting the
## kernel's check would leave dst_aliases_a_quantum_source green on M05's
## backstop. (dst_aliases_a_classical_source needs no property: M05's check
## compares qubit indices and cannot fire on constants, so nothing beneath the
## kernel guards it in either configuration — which is precisely why it is the
## case that goes red if cq_kernel_check_dst is deleted.)
set_tests_properties(test_kernel_bitwise_death.dst_aliases_a_quantum_source
                     test_kernel_bitwise_death.dst_aliases_the_second_source
                     test_kernel_bitwise_death.the_two_sources_are_the_same_register
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "§3 distinctness")

## M11 and M13, Step 11. FILED BY A MUTATION BATTERY: both modules shipped with
## four hard errors between them and no death test, and deleting each left all
## 80 tests green. cast.c's width guard prevents a heap overrun, not a wrong
## answer, and its own comment records it as a both-configuration error because
## Step 24 runs under Release.
add_cqops_death_test(test_kernel_step11_death CASES
    a_quantum_shift_amount
    a_quantum_bit_above_the_stages_is_fine
    a_shift_dst_overlaps_its_source
    a_widening_cast_that_narrows
    a_narrowing_cast_that_widens
    a_sign_extension_that_narrows
    a_cast_dst_overlaps_its_source
)

## WHICH LAYER ABORTED. The shift-amount guard is M11's own; nothing beneath it
## would speak for a CLASSICAL amount, but for a quantum one M05's distinctness
## assert could — in Debug. Pinning the message keeps the case honest.
set_tests_properties(test_kernel_step11_death.a_quantum_shift_amount
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "§3 distinctness")

## M14, Step 12. ONE CASE PER ENTRY POINT — the only thing M07's, M08's and
## M09's own suites cannot prove is that `cq_kernel_add` and `cq_kernel_sub`
## each actually CALL cq_kernel_check_dst. No FAIL_REGULAR_EXPRESSION is owed,
## and the reason is specific to the ALL-CLASSICAL alias these two cases use:
## it takes the R9 short-circuit, so it never reaches cq_sandwich, and its only
## emission is cq_emit_x on a constant, which carries no distinctness check.
## Nothing beneath M14 speaks for that case in either configuration.
##
## The QUANTUM alias would be caught, by cq_shadow_retire in M02 — see the file
## header, which used to say this kernel had no backstop at all and was wrong
## about it. That is exactly why the registered cases are the classical ones.
add_cqops_death_test(test_kernel_add_death CASES
    add_dst_aliases_a_classical_source
    sub_dst_aliases_a_classical_source
)

## M16, Step 13. Same argument as M14's, one predicate family at a time — and
## `the_two_sources_are_the_same_register` is the sharpest case in the project
## so far: with `eq(a,a)` on classical rails, deleting the guard returns the
## RIGHT ANSWER with no diagnostic in either configuration, because a == a. It
## is also the alias CQ_lang genuinely ships for this opcode
## (slice_select_rail_alias_cond.expected.log:4, `icmp_slt_i32(h0, h0)`), whose
## remedy is M26's defensive cqrt_copy at the handle boundary (bd -493).
##
## No FAIL_REGULAR_EXPRESSION is owed on any of them: every case uses
## ALL-CLASSICAL rails, so the kernel takes the R9 short-circuit, never reaches
## cq_sandwich, and emits only cq_emit_x on a constant — which carries no
## distinctness check. Nothing beneath M16 speaks for these, in either
## configuration.
add_cqops_death_test(test_kernel_cmp_death CASES
    eq_dst_aliases_a_classical_source
    ult_dst_aliases_a_classical_source
    slt_dst_aliases_a_classical_source
    ugt_dst_aliases_a_classical_source
    the_two_sources_are_the_same_register
    a_width_of_zero
    a_negative_width
)

## M17 and M12, Step 14. Same argument as M14's and M16's — with the difference
## that M12 could NOT use their trick, and paid for it.
##
## M14 and M16 register the ALL-CLASSICAL alias, which takes the R9
## short-circuit and so has no layer beneath it. M12 cannot: a classical shift
## amount DELEGATES to M11, whose own cq_kernel_check_dst would abort with the
## identical message one module down. So M12's alias cases use a QUANTUM AMOUNT
## to force the sandwich path — and that turned out not to be enough.
##
## MEASURED, by deleting cq_kernel_check_dst from barrel() and running them: all
## three *_var_dst_aliases_its_source cases STILL ABORT, from M02's
## cq_shadow_retire, in both configurations. Copy-out writes into `a`, the
## reverse half replays against it as a control, the compute half stops
## cancelling, and M09's epilogue catches it. That is the
## a-death-test-can-pass-on-a-broken-library shape for the fifth time, and the
## remedy is the same one: name the message the case must NOT abort with.
##
## M17 needs none. Measured the same way — with cq_kernel_check_n deleted from
## cq_kernel_mux, all SIX mux cases go red. A classical `cond` takes the entry
## dispatch, so the kernel never allocates scratch, never enters cq_sandwich,
## and emits only cq_emit_x on a constant: no distinctness check, no palindrome
## to break, nothing beneath it in either configuration.
add_cqops_death_test(test_kernel_step14_death CASES
    mux_dst_aliases_the_true_arm
    mux_dst_aliases_the_false_arm
    mux_dst_overlaps_the_condition
    mux_both_arms_are_the_same_register
    mux_the_condition_is_a_bit_of_an_arm
    mux_a_width_of_zero
    shl_var_dst_aliases_its_source
    lshr_var_dst_aliases_its_source
    ashr_var_dst_aliases_its_source
    shift_var_the_value_is_the_amount
    shift_var_a_width_of_zero
)

## WHICH LAYER ABORTED, the fifth instance and the first where the case had no
## classical variant to fall back on. Without this, deleting M12's guard leaves
## all three green on M02's epilogue with the rail already half-corrupted.
##
## shift_var_the_value_is_the_amount is deliberately NOT listed: measured, it
## goes red outright with the guard deleted, because two SOURCES aliasing does
## not corrupt the sandwich — it emits a malformed Toffoli whose two controls
## are one physical qubit and returns the right value, which is exactly the
## D7b hazard kernel.h describes and nothing downstream notices.
set_tests_properties(test_kernel_step14_death.shl_var_dst_aliases_its_source
                     test_kernel_step14_death.lshr_var_dst_aliases_its_source
                     test_kernel_step14_death.ashr_var_dst_aliases_its_source
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "retire of a determinate")

## AND ONE CASE THAT CANNOT BE DISCRIMINATED AT ALL, said out loud rather than
## dressed up. At W = 0, cq_shift_stages returns 0, so amount_is_classical is
## vacuously true whatever the amount's kinds are and M12 delegates — meaning
## M11's identical guard would abort with the identical message. No regex
## separates them. shift_var_a_width_of_zero is kept because it pins that the
## entry point refuses W = 0 at all rather than allocating a zero-length region,
## and it is recorded here as NOT proving M12 owns that refusal.

## M15, Step 15. THIS SUITE IS K8's L5. Every other kernel folds an
## all-classical operand to zero gates and zero qubits; K8 refuses one, because
## it targets its own addend and a classical bit materialised mid-construction
## makes the reverse replay stop cancelling while the value stays right. So
## these are not defensive garnish around the module — they are the level that
## the shared Phase-B gate carries as L5 everywhere else.
##
## NO FAIL_REGULAR_EXPRESSION IS OWED, and that was checked rather than assumed.
## Nothing beneath M15 speaks for any of these cases: M05's §3 distinctness
## assert compares qubit INDICES, so it is silent on a classical operand and
## silent on two disjoint registers; none of these calls is inside a sandwich,
## so M09 never runs; and M07 never sees a cq_bit array at all. Delete
## cq_addacc_check and the classical cases emit a wrong circuit with a right
## value in BOTH configurations.
add_cqops_death_test(test_kernel_addacc_death CASES
    classical_acc_bit
    classical_addend_bit
    classical_ancilla
    classical_ancilla_at_width_one
    acc_overlaps_the_addend
    the_ancilla_is_inside_the_accumulator
    a_width_of_zero
    check_called_directly_with_a_zero_width
    a_step_index_past_the_end
    a_negative_step_index
)

## WHICH LAYER ABORTED — the Step 7/8 discriminator, this time with the masking
## copy of the guard EARLIER in the call chain rather than later, and FILED BY A
## MUTATION BATTERY rather than foreseen. `cq_addacc_check`'s width guard
## survived mutation to always-true: both entry points call `cq_addacc_steps`
## first, and its identical guard fires one layer up. The guard is still wanted,
## because K11 calls cq_addacc_check directly and that path has no other check —
## so the two messages were made disjoint and these two properties pin which one
## spoke. Delete either guard and exactly one of the two cases goes red.
set_tests_properties(test_kernel_addacc_death.a_width_of_zero
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "cq_addacc_check received")

set_tests_properties(test_kernel_addacc_death.check_called_directly_with_a_zero_width
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "addacc: width is not positive")

## AND THE OVERLAP GUARD, WHERE THE MASKING LAYER IS CONFIGURATION-DEPENDENT —
## measured, not foreseen. Mutating cq_addacc_check's range test down to a
## base-pointer comparison was killed in RELEASE by both overlap cases but in
## DEBUG only by the_ancilla_is_inside_the_accumulator: with acc and b sharing
## qubits, M05's §3 distinctness assert aborts one layer down, so the death test
## still "passed" and hid the mutant. That is the Step 6/7 finding with the
## masking layer present in one configuration only, which no single-configuration
## battery would have separated. Pinning the message closes it in Debug too.
set_tests_properties(test_kernel_addacc_death.acc_overlaps_the_addend
                     test_kernel_addacc_death.the_ancilla_is_inside_the_accumulator
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "§3 distinctness")

## M18, Step 16. Same argument as M14's and M16's: every case uses ALL-CLASSICAL
## rails, so the kernel takes the R9 short-circuit, never allocates scratch,
## never enters cq_sandwich, never reaches M15's cq_addacc_check, and emits only
## cq_emit_x on a constant — which carries no distinctness check. Nothing
## beneath M18 speaks for any of them in either configuration.
##
## the_two_sources_are_the_same_register is the sharpest: mul(dst,a,a) is x²,
## which CQ_lang genuinely emits (cq_template_mul_i32(h10, h10)), and with the
## guard deleted it returns the RIGHT ANSWER with no diagnostic.
add_cqops_death_test(test_kernel_mul_death CASES
    mul_dst_aliases_a_classical_source
    mul_dst_aliases_the_second_source
    mul_dst_overlaps_a_source_without_sharing_a_base
    mul_the_two_sources_are_the_same_register
    mul_a_width_of_zero
    mul_a_negative_width
    mul_steps_called_directly_with_a_zero_width
)

## WHICH LAYER ABORTED — the Step-15 shape, where the masking copy of the guard
## is EARLIER in the call chain rather than later. cq_kernel_mul calls
## cq_kernel_check_dst before it ever reaches cq_mul_steps, so M18's own width
## guard is unreachable through the kernel and would survive mutation to
## always-true. The two messages are disjoint and these pin which one spoke:
## delete either guard and exactly one of the two cases goes red.
set_tests_properties(test_kernel_mul_death.mul_a_width_of_zero
                     test_kernel_mul_death.mul_a_negative_width
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "mul: width is not positive")

set_tests_properties(test_kernel_mul_death.mul_steps_called_directly_with_a_zero_width
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "destination width is not positive")

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
