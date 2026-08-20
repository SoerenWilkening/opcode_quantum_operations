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
    support/kernelctrl.c    # Step 20 — the controlled axis: the region mode,
                            # the control rail, §9's gate-tuple transform. Split
                            # from kerneldrv.c when the axis took it to 345/300
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

## M19 and M20, Step 17 — K12 divrem. SPLIT IN TWO ON THE MODULE SEAM, up
## front rather than after measuring, which is bd mmv's option (a): the bead
## records the barrel becoming the longest pole at a TENTH of K12's size and
## says K11 and K12 would need the split more. One udiv case is 2(34W^2+5W)
## gate emissions over 2(8W^2+4W-1) qubit acquisitions — 2.6x a mul case on 8x
## the region — and there are four opcodes. Two binaries let ctest -j overlap
## them, and the goldens are split the same way because cq_gold_close requires
## every row in a file to have been visited.
add_cqops_test(test_kernel_divrem)    # M19 — K12 udiv/urem. THE FIRST KERNEL
                                      # WHOSE WHOLE LOOP BODY BELONGS TO OTHER
                                      # MODULES: M16's cq_ult_step, M14's
                                      # cq_sub_step, M17's cq_mux_step. So what
                                      # is tested here is the SLOT ARITHMETIC
                                      # and the LAYOUT, not the gates
add_cqops_test(test_kernel_sdivrem)   # M20 — K12 sdiv/srem. The signed path was
                                      # the ONE thing K12.md's 2026-08-16
                                      # measurement pass did not reach; this is
                                      # where §3.4 stopped being arithmetic

## The driver's own assertions, made falsifiable — test_harness_negative's
## argument one level up. A mutation battery found five of the Phase-B pool
## assertions surviving mutation to always-true: correct, load-bearing, and
## never once observed to fire. This provokes each one.
add_cqops_test(test_kerneldrv)        # Step 10 — the infrastructure's gate

## Layer 1 — the controlled axis. L0 for §9, on test_emit_fold's shape: the
## promotion is swept over the product of the control's KIND and §3's own 5 / 25
## / 125 operand combinations, 775 cases, each pinning the ordered stream, the
## qubits allocated, the target's kind AND its shadow.
add_cqops_test(test_controlled)   # M06, Step 20 — §9 row 0, the promotion, the
                                  # shared ancilla, the nested AND, §7's two
                                  # rotation rows under §9, and bd skh's one
                                  # falsifying fixture

## M06's hard errors, Step 20. EVERY ONE ABORTS IN BOTH CONFIGURATIONS: the
## guards are about what gets EMITTED, and a promotion that pushed a malformed
## Toffoli at the sink in Release while aborting in Debug would be the worst of
## both. No case here carries CQ_DEATH_SKIP_WITHOUT_INVARIANTS.
add_cqops_death_test(test_controlled_death CASES
    pop_with_no_region_open
    an_unbalanced_push_is_caught_at_dispose
    push_inside_a_sandwich_compute_half
    the_control_wire_is_the_target_of_an_x
    the_control_wire_is_the_target_of_a_cx
    the_control_wire_is_the_target_of_a_ccx
    the_control_wire_is_a_control_of_a_cx
    the_control_wire_is_a_control_of_a_ccx
    a_nested_regions_and_input_is_the_target_of_a_gate
    a_nested_regions_inner_control_is_the_target_of_a_gate
    an_enclosing_regions_wire_is_the_target_of_a_gate
    the_control_wire_is_the_target_of_a_rotation
    ry_2pi_under_a_quantum_control
    ry_half_turn_on_a_constant_under_a_quantum_control
    ry_half_turn_on_a_qubit_under_a_quantum_control
    ry_neg_half_turn_under_a_quantum_control
    rz_on_a_constant_under_a_quantum_control
    a_measurement_inside_a_quantum_controlled_region
    a_measurement_inside_a_skipped_region
)

## WHICH LAYER ABORTED, for the sixth time (Steps 6, 7, 8, 11, 15 before this).
## The five coincidence cases could each reach a guard one layer down if M06's
## were deleted — cq_shadow_ccx's own distinctness assert, or cq_emit_ccx's —
## and BOTH of those are Debug-gated, so in Release the mutant would emit a
## malformed gate and the death would simply stop happening. Pinning M06's own
## message is what keeps the case honest in the configuration that matters.
##
## ONE BLOCK PER TEST, COMPOSED, NEVER TWO: set_tests_properties OVERWRITES a
## property, so a second block naming a test already named above would silently
## disarm this one and leave 100% green with nothing to look at.
## FAIL_REGULAR_EXPRESSION AND NOT PASS_, and the difference is load-bearing:
## PASS_ DISPLACES the exit-code check, so pinning "the message contains D11"
## that way would trade the death test's own contract for a substring. The
## patterns below therefore name the layers that must NOT have spoken.
set_tests_properties(test_controlled_death.the_control_wire_is_the_target_of_an_x
                     test_controlled_death.the_control_wire_is_the_target_of_a_cx
                     test_controlled_death.the_control_wire_is_the_target_of_a_ccx
                     test_controlled_death.the_control_wire_is_the_target_of_a_rotation
                     test_controlled_death.the_control_wire_is_a_control_of_a_cx
                     test_controlled_death.the_control_wire_is_a_control_of_a_ccx
                     test_controlled_death.a_nested_regions_and_input_is_the_target_of_a_gate
                     test_controlled_death.a_nested_regions_inner_control_is_the_target_of_a_gate
                     test_controlled_death.an_enclosing_regions_wire_is_the_target_of_a_gate
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "distinctness;shadow:")

## The five D11 refusals must come from M06's ONE greppable site — not from M22
## having quietly grown a second copy of the decision, and not from a rotation
## reaching the pool or the shadow first. That shared tripwire is the
## `rotate:;qubit pool:;distinctness` list, and it is REPEATED in each block
## below rather than set once, because set_tests_properties OVERWRITES.
##
## AND THE ROW NAME IS PINNED, WHICH IT WAS NOT UNTIL NOW. src/rotate.c's
## half_turn_row() takes a parity and a column and returns the string D11
## tabulates a phase for; inverting the parity argument — passing
## `row == CQ_ANGLE_HALF_TURN` where the code says NEG — was MEASURED to survive
## the whole 195-test suite in both configurations, because every case here
## asserted only that SOMETHING aborted. The parity is precisely what bd fna's
## `k mod 4` split was owed for: uncontrolled the two half turns are one cell
## and M22 emits the identical pair for both, so nothing else in the project can
## tell them apart, and D11 gives them DIFFERENT control-side phases
## (pi.b against pi.(1-b) on the constant column, -pi/2 against +pi/2 on the
## qubit one). A refusal that names the wrong row sends the reader to the wrong
## line of the PRD's table, which for a hand-derived phase table this project
## has no instrument to check is the whole cost of getting it wrong.
##
## THE PATTERNS ARE NEGATIVE, one per case, naming the string the OTHER parity
## or the OTHER column would have printed. Each also catches an inverted
## `was_qubit`, which the battery never tried: the constant-rail cases must
## never say "qubit column" and the qubit-rail case must never say "constant
## column". FAIL_ and not PASS_ throughout, for the reason stated above — PASS_
## displaces the exit-code check, so pinning the message that way would trade
## the death test's own contract for a substring match.
set_tests_properties(test_controlled_death.ry_2pi_under_a_quantum_control
                     test_controlled_death.rz_on_a_constant_under_a_quantum_control
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "rotate:;qubit pool:;distinctness")

## k = 1 (mod 4), constant column: "theta = pi  (mod 4pi) … (alpha = pi.b)".
set_tests_properties(test_controlled_death.ry_half_turn_on_a_constant_under_a_quantum_control
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "rotate:;qubit pool:;distinctness;theta = 3pi;qubit column")

## k = 1 (mod 4), qubit column: "theta = pi  (mod 4pi) … (alpha = -pi/2)".
set_tests_properties(test_controlled_death.ry_half_turn_on_a_qubit_under_a_quantum_control
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "rotate:;qubit pool:;distinctness;theta = 3pi;constant column")

## k = 3 (mod 4), constant column: "theta = 3pi (mod 4pi) … (alpha = pi.(1-b))".
## `alpha = pi.b` cannot match that — after "pi" comes "." then "(" — and is
## exactly what the k = 1 spelling prints, so it is the crisp discriminator.
set_tests_properties(test_controlled_death.ry_neg_half_turn_under_a_quantum_control
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "rotate:;qubit pool:;distinctness;alpha = pi.b;qubit column")

## Layer 4 — the analog surface and the sinks. M25 sink_qec is Step 26, against
## C_quantum_error_correction.
add_cqops_test(test_rotate)       # M22, Step 19 — §7's twelve cells, the θ≡π
                                  # asymmetry, D12's poison rule, and the
                                  # terminal measurement. NO GOLDEN FILE, and
                                  # deliberately: cq_gold_row carries
                                  # {x, cx, ccx} and every count that
                                  # distinguishes an M22 row is ry/rz/mz, M22
                                  # has no upstream construction for the R3
                                  # commit check to guard, and the counts ARE
                                  # the specification — so a regenerable file
                                  # would let CQOPS_UPDATE_GOLDENS=1 bless a
                                  # row that stopped emitting a rotation
add_cqops_test(test_angle)        # M21, Step 18 — PRD §7's four rows, the
                                  # mod-4π against mod-2π boundary, and the
                                  # tolerance edges. Every sweep is checked
                                  # against an INDEPENDENT reduction rather
                                  # than against a hand-written table
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

## M19 and M20, Step 17 — plus the two EXPORTS plan §0.4 required of M14 and
## M16. The exports are the interesting half: cq_sub_step and cq_ult_step are
## the first step functions whose caller is a DIFFERENT MODULE mapping its own
## index space onto theirs, and an off-by-one there emits a real, plausible gate
## from the wrong stage rather than failing.
##
## Same argument as M14's, M16's and M18's for the alias cases: every one uses
## ALL-CLASSICAL rails, so the kernel takes the R9 short-circuit, never
## allocates scratch, never enters cq_sandwich, never reaches the step guards,
## and emits only cq_emit_x on a constant — which carries no distinctness check.
## Nothing beneath M19/M20 speaks for any of them in either configuration.
add_cqops_death_test(test_kernel_divrem_death CASES
    udiv_dst_aliases_a_classical_source
    urem_dst_aliases_the_second_source
    sdiv_dst_aliases_a_classical_source
    srem_dst_aliases_a_classical_source
    udiv_dst_overlaps_a_source_without_sharing_a_base
    the_two_sources_are_the_same_register
    udiv_a_width_of_zero
    sdiv_a_negative_width
    divrem_steps_called_directly_with_a_zero_width
    divrem_region_called_directly_with_a_zero_width
    sdivrem_steps_called_directly_with_a_zero_width
    sdivrem_region_called_directly_with_a_zero_width
    sub_steps_called_directly_with_a_zero_width
    sub_step_index_past_the_end
    sub_step_index_is_negative
    ult_steps_called_directly_with_a_zero_width
    ult_step_index_past_the_end
    ult_step_index_is_negative
    divrem_step_index_past_the_end
    the_quotient_of_a_remainder_only_block
    the_classical_fold_above_the_register_cap
)

## WHICH LAYER ABORTED — the Step-15 shape, where the masking copy of the guard
## is EARLIER in the call chain, and now in FOUR modules at once. Every entry
## point calls cq_kernel_check_dst before it reaches M19's, M20's, M14's or
## M16's own width guard, so each of those would survive mutation to always-true
## without a message pin. The five messages are disjoint; delete any one guard
## and exactly one case goes red.
set_tests_properties(test_kernel_divrem_death.udiv_a_width_of_zero
                     test_kernel_divrem_death.sdiv_a_negative_width
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "divrem: width is not positive|sdivrem: width is not positive")

set_tests_properties(test_kernel_divrem_death.divrem_steps_called_directly_with_a_zero_width
                     test_kernel_divrem_death.divrem_region_called_directly_with_a_zero_width
                     test_kernel_divrem_death.sdivrem_steps_called_directly_with_a_zero_width
                     test_kernel_divrem_death.sdivrem_region_called_directly_with_a_zero_width
                     test_kernel_divrem_death.sub_steps_called_directly_with_a_zero_width
                     test_kernel_divrem_death.ult_steps_called_directly_with_a_zero_width
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "destination width is not positive")

## AND WHICH OF THE TWO GUARDS INSIDE ONE STEP FUNCTION. cq_sub_step and
## cq_ult_step both call their _steps() width guard before their own index
## check, so an index case must NOT be allowed to pass on the width message —
## otherwise the range check, which is the guard the whole export exists to
## have, would survive mutation to always-true.
set_tests_properties(test_kernel_divrem_death.sub_step_index_past_the_end
                     test_kernel_divrem_death.sub_step_index_is_negative
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "sub: width is not positive")

set_tests_properties(test_kernel_divrem_death.ult_step_index_past_the_end
                     test_kernel_divrem_death.ult_step_index_is_negative
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "ult: width is not positive")

set_tests_properties(test_kernel_divrem_death.divrem_step_index_past_the_end
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "divrem: width is not positive|span is outside")

## M21, Step 18. Both configurations — nothing here is an invariant check, and
## a tolerance is configuration rather than a gate.
##
## THE TWO HALVES ARE NOT THE SAME KIND OF GUARD, which is why both are
## registered. A tolerance below the range (negative, NaN) is a LOUDNESS matter:
## it would degrade cq_angle_lattice to CQ_ANGLE_GENERAL, which is the sound
## answer, so the circuit stays right and only §7's folds are lost. A tolerance
## above CQ_ANGLE_TOLERANCE_MAX is a CORRECTNESS matter: the cap is what bounds
## |k| for the (long long) cast in cq_angle_lattice.
##
## NO FAIL_REGULAR_EXPRESSION IS OWED, and that was checked rather than assumed:
## the two guards sit on disjoint call paths (the setter never enters the
## lattice; a direct lattice call never enters the setter), so neither can mask
## the other. Delete either and exactly its own four cases report "survived",
## and the two clauses of the shared predicate have disjoint witnesses.
add_cqops_death_test(test_angle_death CASES
    a_negative_tolerance
    a_nan_tolerance
    an_infinite_tolerance
    a_tolerance_above_the_cap
    the_lattice_called_directly_with_a_negative_tolerance
    the_lattice_called_directly_with_a_nan_tolerance
    the_lattice_called_directly_with_an_infinite_tolerance
    the_lattice_called_directly_with_a_tolerance_above_the_cap
)

## M22, Step 19. Four families, and read tests/test_rotate_death.c's header for
## why they are not the same kind of thing. The sandwich refusal is M22's own
## and stands in for BOTH I6 mechanisms, neither of which reaches a module that
## bypasses cq_emit_* to emit; ckd18_* pins an OPEN P0's CURRENT behaviour and
## is EXPECTED to go red when Step 23 selects CQOPS_FREE_RETIRE.
add_cqops_death_test(test_rotate_death CASES
    ry_inside_a_sandwich_compute_half
    rz_inside_a_sandwich_compute_half
    measure_inside_a_sandwich_compute_half
    ckd18_a_general_ry_rail_cannot_be_freed
    a_null_proof_still_refuses_a_materialised_rail
    ry_of_a_freed_handle
    rz_of_a_freed_handle
    ry_of_a_measured_rail
    rz_of_a_measured_rail
    measure_of_a_measured_rail
    measure_of_a_freed_handle
    free_after_measure
)

## WHICH LAYER ABORTED, as a NEGATIVE pin. These two abort at the same site with
## the same message — cq_reg_clean's `!proof ||` short-circuits at the first
## qubit-carrying bit exactly as a failing proof does — so no regex separates
## them from each other, and that is recorded rather than dressed up. What the
## property DOES pin is the Step 7 discriminator: the refusal comes from M07's
## two-pass check, which releases NOTHING, and not from M03's per-qubit release
## guard one layer down. If M03's message ever appears here, cq_reg_free has
## started releasing before verifying and a dirty rail is half-returned.
set_tests_properties(test_rotate_death.ckd18_a_general_ry_rail_cannot_be_freed
                     test_rotate_death.a_null_proof_still_refuses_a_materialised_rail
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "release of a qubit not proven")

## "ABORTS HAVING EMITTED ZERO GATES" IS A PROMISE, SO IT NEEDS A DETECTOR.
## cq_measure marks the rail MEASURED before it reads a single bit, so a second
## measure aborts before touching the sink. Nothing about that ordering was
## observable until this pin: the case's rail is materialised, its sink reports
## any `mz` emitted after the first legitimate measure, and this turns that
## report into a failure. A refactor that moves the mark below the loop — the
## natural thing to do while taking the recorded `rotation table / measurement`
## seam — passes every other assertion in the project.
## FAIL_REGULAR_EXPRESSION IS SET ONCE PER TEST, AND A SECOND set_tests_properties
## OVERWRITES THE FIRST RATHER THAN ADDING TO IT. That is not a style note: this
## file briefly carried the per-case pins in separate blocks, and the later block
## silently disarmed the earlier one — the mutant it existed to catch went back to
## surviving, with every test still green. So every regex a test needs is composed
## into ONE semicolon-separated list here, and the shared PREFLIGHT- tripwire is
## repeated in each list rather than set once globally.
##
## PREFLIGHT- catches the setup tripwires in tests/test_rotate_death.c: a fold row
## that allocated a qubit, or a general Ry that failed to materialise. Without it
## those fprintf calls print and the case still passes.
set_tests_properties(test_rotate_death.ry_inside_a_sandwich_compute_half
                     test_rotate_death.rz_inside_a_sandwich_compute_half
                     test_rotate_death.ry_of_a_freed_handle
                     test_rotate_death.rz_of_a_freed_handle
                     test_rotate_death.ry_of_a_measured_rail
                     test_rotate_death.rz_of_a_measured_rail
                     test_rotate_death.measure_of_a_freed_handle
                     test_rotate_death.free_after_measure
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "PREFLIGHT-")

## WHICH GUARD SPOKE, for the measurement sandwich case. Without this pin,
## disabling cq_measure's OWN refusal still leaves the case green: rotating_step
## measures the same handle on the forward pass and again on cq_sandwich's reverse
## replay, so M07's cq_reg_mark_measured aborts INSIDE the armed window and the
## case cannot tell that from M22's guard. Measured — the mutant survived the whole
## 174-test suite in both configurations. The ry/rz siblings need no such pin:
## without their guard the sandwich RETURNS and cq_death_survived fires.
set_tests_properties(test_rotate_death.measure_inside_a_sandwich_compute_half
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "PREFLIGHT-;measure of a rail that is not live")

## WHICH LAYER REFUSED THE FREE. These two abort at the same site with the same
## message — cq_reg_clean's `!proof ||` short-circuits at the first qubit-carrying
## bit exactly as a failing proof does — so no regex separates them from each
## other, and that is recorded rather than dressed up. What this DOES pin is the
## Step 7 discriminator: the refusal comes from M07's two-pass check, which
## releases NOTHING, and not from M03's per-qubit release guard one layer down.
## Note ckd18_* passes cq_pc_zero_proof_rotation_free rather than NULL, so it is
## the POISON that refuses it — which is ckd.18's actual claim, and is what makes
## disabling cq_shadow_rotate turn this case red.
set_tests_properties(test_rotate_death.ckd18_a_general_ry_rail_cannot_be_freed
                     test_rotate_death.a_null_proof_still_refuses_a_materialised_rail
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "PREFLIGHT-;release of a qubit not proven")

## "ABORTS HAVING EMITTED ZERO GATES" IS A PROMISE, SO IT NEEDS A DETECTOR.
## cq_measure marks the rail MEASURED before it reads a single bit, so a second
## measure aborts before touching the sink. Nothing about that ordering was
## observable until this pin: the case's rail is materialised, its sink reports any
## `mz` emitted after the first legitimate measure, and this turns that report into
## a failure. A refactor moving the mark below the loop — the natural thing to do
## while taking the recorded `rotation table / measurement` seam — passes every
## other assertion in the project.
set_tests_properties(test_rotate_death.measure_of_a_measured_rail
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "PREFLIGHT-;MZ-EMITTED-PAST-THE-ABORT-POINT")

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
