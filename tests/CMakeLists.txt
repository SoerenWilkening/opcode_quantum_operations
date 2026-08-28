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
    ## Step 23. The SOURCE directory, read at run time, for the same reason the
    ## goldens are: `bd d6m`'s third arm is a claim about WHERE a symbol is
    ## named, and no compiled artefact can answer that. Absolute, like the two
    ## above, so it does not depend on ctest's working directory.
    CQOPS_SHIM_DIR="${CMAKE_SOURCE_DIR}/shim"
    ## Step 23.4. docs/cqrt_census.txt is the INDEPENDENT oracle for the vendored
    ## ABI header's NAME set — it was produced from CQ_lang at Step 0.3 by a
    ## different route, and it is the only thing in this repository that can see
    ## a renamed declaration. Counting lines cannot: measured, three renamed
    ## declarations kept all eight per-group counts exact and the whole suite
    ## green while the definition they were supposed to type-check lost its
    ## prototype entirely.
    CQOPS_DOCS_DIR="${CMAKE_SOURCE_DIR}/docs"
)

## src/ is PRIVATE on the cqops target, because none of it is public API. The
## suites need it anyway: plan §2.1 is one test binary per *module*, and the
## modules are internal — test_bit.c includes "bit.h" directly. Carrying the
## path here rather than widening cqops keeps the exception on the test side.
##
## shim/ is here for the same reason and needs its own line: the PRIVATE entry
## on the cqops target reaches that target's own translation units and is
## excluded from its INTERFACE, so it does not propagate here.
target_include_directories(cqops_test_support PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/shim
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

## M25b, Step 26. THE HALF OF THE QEC SINK THAT RUNS ON EVERY BOX: the double →
## (p, q_denom) conversion depends on <math.h> and nothing else, so it is built
## and tested with or without the QEC library. That is half the reason plan §3
## recorded the split seam here rather than letting the vtable and the numerics
## share a module (Rule 12).
add_cqops_test(test_sink_qec_angle)
add_cqops_death_test(test_sink_qec_angle_death CASES
    a_nan_angle
    an_infinite_angle
    a_cap_below_one
    an_angle_too_large_for_a_long
)

## M25, Step 26 — the vtable half, which needs a real libqec.a and a real
## config. OPT-IN for L6's reason: this repository neither pins nor can build the
## QEC library, so a suite registered without it would be a green run claiming a
## link it never made. CMake hands the config path down as a define rather than
## letting the test hunt for one, so a red run names the exact file it ran
## against (Rule 17).
if(CQOPS_HAVE_QEC)
    add_cqops_test(test_sink_qec)
    add_cqops_death_test(test_sink_qec_death CASES
        a_coincident_toffoli
        a_gate_before_the_bind
        a_bind_without_a_config
        a_precision_above_the_conversions_floor
    )
    foreach(t test_sink_qec test_sink_qec_death)
        target_compile_definitions(${t} PRIVATE
            CQOPS_QEC_TEST_CONFIG="${CQOPS_QEC_DIR}/examples/config_ccx.json")
    endforeach()

    ## WHICH LAYER REFUSED. All four of M25's hard errors share one die and one
    ## "qec sink:" prefix, so the exit code cannot tell them apart — and three of
    ## the four are one edit away from being caught by a neighbouring guard.
    ## Negative, so it composes with the exit-code check instead of displacing
    ## it, and composed into ONE list per test because set_tests_properties
    ## OVERWRITES (tests/CMakeLists.txt:193-195).
    ##
    ## The coincident Toffoli is the load-bearing one: it must die on the
    ## LIBRARY's refusal reaching our check, not on a missing bind or a missing
    ## config, and above all not on our own Debug-gated distinctness assert —
    ## which is absent in Release and is the whole reason this case exists.
    set_tests_properties(test_sink_qec_death.a_coincident_toffoli
                         PROPERTIES FAIL_REGULAR_EXPRESSION
                         "before it was bound;CQOPS_QEC_CONFIG;PRECISION;distinctness")
    set_tests_properties(test_sink_qec_death.a_gate_before_the_bind
                         PROPERTIES FAIL_REGULAR_EXPRESSION
                         "refused a gate;CQOPS_QEC_CONFIG;PRECISION")
    set_tests_properties(test_sink_qec_death.a_bind_without_a_config
                         PROPERTIES FAIL_REGULAR_EXPRESSION
                         "refused a gate;before it was bound;PRECISION;refused the config")
    set_tests_properties(test_sink_qec_death.a_precision_above_the_conversions_floor
                         PROPERTIES FAIL_REGULAR_EXPRESSION
                         "refused a gate;before it was bound;needs CQOPS_QEC_CONFIG")

    ## M26's ANNOTATION half, `bd 76r` / PRD §15 D21. Same opt-in and for a
    ## sharper reason than M25's: the layer's ONE activation test is the FILE*
    ## M25 handed to qec_set_trace, so without a real libqec.a there is no
    ## configuration in which it emits a byte — which is the point, since it is
    ## what keeps M23's printf sink and the qec trace disjoint consumers.
    ##
    ## A DIFFERENT CONFIG FROM THE VTABLE SUITE'S, AND THE REASON IS COST RATHER
    ## THAN CAPABILITY. The only route from an all-classical program into a
    ## quantum one is a general Ry (PRD §15 D17's corpus finding), and an Ry is a
    ## gridsynth walk whose trace grows with the code distance: measured on this
    ## box, the same six-op program is 19,636 lines against config_demo3 and 55.9
    ## MILLION against config.json at the default precision. config_demo3's
    ## n_logical = 3 is also the POOL CEILING for these cases, because under this
    ## sink D2's ceiling is qec_n_logical and D21 (b) turns recycling off — so the
    ## budget is three qubits for a whole case, not three at a time.
    add_cqops_test(test_shim_trace)
    add_cqops_death_test(test_shim_trace_death CASES
        nested_bracket
        unopened_bracket
    )
    foreach(t test_shim_trace test_shim_trace_death)
        target_compile_definitions(${t} PRIVATE
            CQOPS_QEC_TRACE_CONFIG="${CQOPS_QEC_DIR}/examples/config_demo3.json")
    endforeach()

    ## WHICH LAYER REFUSED, again negatively so it composes with the exit code.
    ## Both cases bind a real qec context on their way to the assertion, so the
    ## sink's own die, the pool ceiling and M07's handle guard are all reachable
    ## from the setup and none of them is the abort under test. The two cases
    ## must also not catch EACH OTHER's message: a guard that fires on the wrong
    ## side of the bracket is the "which of three calls of the same guard fired"
    ## trap in a two-call spelling.
    set_tests_properties(test_shim_trace_death.nested_bracket
                         PROPERTIES FAIL_REGULAR_EXPRESSION
                         "UndefinedBehaviorSanitizer;never opened;qec sink:;qubit pool:")
    set_tests_properties(test_shim_trace_death.unopened_bracket
                         PROPERTIES FAIL_REGULAR_EXPRESSION
                         "UndefinedBehaviorSanitizer;forbids nesting;qec sink:;qubit pool:")
endif()

## THE UNCOMPUTE AXIS, Step 21. NO MODULE — `_unc` IS the same kernel called
## again (Rule 7), so plan §4's Green column reads "(thin, in M26)" and M26 is
## Step 23. This suite owns the axis's own claims, NOT L3 per kernel: that is
## cq_kd_case's, on every case of every kernel under each of §9's four regions.
##
## Its headline is the fixture where forward and `_unc` DIFFER. Every one of
## the 399 pinned (kernel, W) golden pairs is equal and always will be — L4 is
## measured at the all-quantum mask, the FIXED POINT of the drift — so until
## this landed, risk R6 ("someone asserts unc == forward") had no detector
## anywhere in the tree.
add_cqops_test(test_unc)          # Step 21 — PRD §10, R6, and bd 2cf measured

## WHICH LAYER ABORTED, the seventh instance. M07's cq_reg_free and M03's
## cq_qubits_release both guard "this qubit is not provably |0>", and plan §0
## records what confusing them costs: removing M07's cq_reg_clean call left all
## 24 reg tests green, with the rail half-returned to the pool and only M03's
## message to show for it. The same pattern as test_reg_death's block, repeated
## here rather than shared — set_tests_properties OVERWRITES a property, so a
## block naming a test another block already named would silently disarm one of
## them and leave a green run with nothing to look at.
##
## THE SECOND CASE WAS EXPECTED TO GO RED ONE DAY, ON PURPOSE, AND IT DID NOT.
## This used to say "bd 2cf is OPEN" and to predict that deciding bd ckd.17b
## would eventually give cqrt_free evidence other than the shadow. Both beads
## closed 2026-08-22 as PRD §15 D15, which supplies exactly that evidence — and
## the follow-on prediction, "this death stops happening once M26 carries the
## certificate", was MEASURED FALSE at landing 2 (2026-08-27). The certificate
## reads a call stream recorded at the M26 HANDLE boundary and this fixture
## drives the register layer directly, so it has no history to read and answers
## UNPROVEN. Both cases stand, both now run under CQOPS_FREE_ABORT, and the
## shape was reproduced where the certificate can see it — tests/test_unc_death.c
## names the sites. Re-read the case, never delete it.
add_cqops_death_test(test_unc_death CASES
    free_without_the_uncompute
    free_of_a_rotation_tainted_uncomputed_rail
)

## THE NEGATIVE LIST HAS TO BE EXHAUSTIVE, AND M07's OWN OTHER MESSAGES ARE
## THE EASY ONES TO FORGET. PASS_REGULAR_EXPRESSION cannot be used here — it
## DISPLACES the exit-code check, trading the death test's own contract for a
## substring — so "which layer aborted" has to be spelled as "which layers must
## NOT have spoken". Excluding only M03/M22/M02 leaves a mutant that makes
## cq_reg_free take its MEASURED or DOUBLE-FREE branch instead of the clean
## check: both deaths still abort inside their armed window, neither regex
## names the string, and the guard they exist to pin is bypassed entirely.
## AND AT STEP 23 THEY SPLIT INTO SEPARATE BLOCKS, because the two cases stopped
## being "the same guard by opposite routes" and became the two ROWS of PRD §15
## D15 §3 — one PROVEN DIRTY, one UNPROVEN. Each now pins the message the other
## row would have printed, on top of the layer list it already carried. Separate
## blocks because set_tests_properties OVERWRITES rather than adds, so the
## shared tripwires are repeated in each list rather than set once.
set_tests_properties(test_unc_death.free_without_the_uncompute
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "release of a qubit not proven;rotate:;shadow:;double free;measurement is terminal;tombstone;free of a rail not provably;UndefinedBehaviorSanitizer")

set_tests_properties(test_unc_death.free_of_a_rotation_tainted_uncomputed_rail
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "release of a qubit not proven;rotate:;shadow:;double free;measurement is terminal;tombstone;PROVEN not to be;UndefinedBehaviorSanitizer")

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
    free_unproven_rail
    an_unrecognised_free_abort_spelling_is_a_hard_error
    release_of_a_convicted_qubit_is_refused
    swap_a_rail_with_itself
    swap_between_mismatched_widths
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
## AND EVERY ONE OF THESE LISTS CARRIES `UndefinedBehaviorSanitizer` SINCE STEP
## 23, WHICH IS A DIFFERENT KIND OF TRIPWIRE FROM THE REST OF THE LINE.
## MEASURED: delete M07's NULL-proof guard and cq_reg_free calls through a NULL
## function pointer. In RELEASE that is a raw SIGSEGV, which is not SIGABRT, so
## the case fails and the mutant is killed. In DEBUG, UBSan intercepts the SEGV
## and calls abort() ITSELF -- which lands inside CQ_EXPECT_ABORT's armed
## window, so the case exits 0 and PASSES, having verified nothing. The battery
## reported that mutant as "killed in Release only", which is the inverse of the
## usual asymmetry and the second instance of it in this module.
##
## The general hazard is bigger than this guard: CQ_EXPECT_ABORT cannot tell
## "our die() ran" from "a sanitizer aborted", so ANY death case whose setup
## acquires undefined behaviour passes in Debug for free. Naming the sanitizer
## in the negative list is the only thing that separates them, and it composes
## with the exit-code check the way PASS_REGULAR_EXPRESSION would not.
##
## THE SWEEP LANDED (`bd u76`) AND IT IS STRUCTURAL, NOT PER CASE. Every one of
## the 254 death cases now carries `UndefinedBehaviorSanitizer;AddressSanitizer`,
## armed by add_cqops_death_test at registration and RE-ARMED by a
## cmake_language(DEFER) call after this whole directory has been processed —
## because set_tests_properties OVERWRITES, so each block below would otherwise
## wipe the registration-time arm. See cmake/CqopsTest.cmake for the mechanism
## and for the measurement. The consequence for a reader HERE: the tripwire is
## present whether or not a block spells it, so a list that omits it is not a
## gap, and a list that spells it (every one in this file that predates the
## sweep, kept as written) is not the reason it is there. The composed property
## is de-duplicated, so the two spellings cost nothing.
##
## VERIFIED THE WAY THE HOLE WAS FOUND, on a case that had NO
## FAIL_REGULAR_EXPRESSION at all before the sweep. Mutate src/sink.c's
## `if (!entry)` vtable guard to an always-false predicate and
## test_sink_death.missing_entry_is_not_called calls through a NULL function
## pointer. The BARE Debug binary exits 0 — "SUMMARY: UndefinedBehaviorSanitizer:
## SEGV sink.c:40 in cq_sink_ccx", inside the armed window, mutant SURVIVES.
## Under ctest with the tripwire it is `(Failed)` in Debug ("Error regular
## expression found in output. Regex=[UndefinedBehaviorSanitizer]") and
## `(SEGFAULT)` in Release. Note the two REASON strings differ, which is the
## recorded trap for any mutation runner that greps ctest's summary for
## `(Failed)` alone.
set_tests_properties(test_reg_death.free_without_proof
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "release of a qubit not proven;CQOPS_FREE_ABORT;UndefinedBehaviorSanitizer")

## AND AT STEP 23 THE TWO NON-CLEAN ROWS BECAME SEPARABLE FOR THE FIRST TIME.
## Before it, a dirty rail and an unproven one produced the IDENTICAL string —
## "free of a rail that is not provably clean" — so no regex could have told
## them apart, and cq_reg_clean had already thrown the distinction away one call
## down. PRD §15 D15 §3's residue split is stated in exactly these two rows and
## is `bd 06t`'s first obligation, so each case now pins the message the OTHER
## row would have printed. That also kills a mutant no earlier pin could see: a
## cq_reg_disposition whose fold returns at the first non-clean bit answers
## UNPROVEN for free_dirty_rail, whose bad bit is LAST, and every exit code
## stays identical.
##
## SEPARATE BLOCKS, ONE PER TEST, because set_tests_properties OVERWRITES a
## property rather than adding to it — a second block naming a test an earlier
## block already named silently disarms the earlier regex and leaves a green run
## with nothing to look at (measured at Step 19). Each test below appears in
## exactly one block, and each block composes every regex that test needs.
set_tests_properties(test_reg_death.free_dirty_rail
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "release of a qubit not proven;free of a rail not provably;UndefinedBehaviorSanitizer")

set_tests_properties(test_reg_death.free_unproven_rail
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "release of a qubit not proven;PROVEN not to be;UndefinedBehaviorSanitizer")

## The flag's own fail-loud path must abort in the RESOLVER, not by wandering
## into a free. Neither M07's free messages nor M03's may appear.
set_tests_properties(test_reg_death.an_unrecognised_free_abort_spelling_is_a_hard_error
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "release of a qubit not proven;free of a rail;STRANDED;UndefinedBehaviorSanitizer")

## THE ONE CASE WHERE M03's MESSAGE IS THE CORRECT ANSWER, and it is listed here
## so that nobody adds it to the negative list above by symmetry. It calls
## cq_qubits_release directly with a CONVICTION; M07 is not in the call stack at
## all, and what must not appear is any sign that the strand path ran instead.
set_tests_properties(test_reg_death.release_of_a_convicted_qubit_is_refused
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "STRANDED;strand of;free of a rail;UndefinedBehaviorSanitizer")

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
## bypasses cq_emit_* to emit. ckd18_* pins the refusal under the evidence the
## suite supplies — the shadow. This used to call bd ckd.18 an OPEN P0 and to
## predict the case going red when Step 23 selected CQOPS_FREE_RETIRE; the bead
## closed 2026-08-22 into PRD §15 D15 and D15 builds no such flag, so THAT
## prediction is retired. BOTH OF THE REASONS THIS BLOCK LISTED AS STILL OPEN
## ARE NOW SETTLED, and they settled in opposite directions. D15 §4's
## abort-vs-strand clause was CONFIRMED 2026-08-22 as (b) STRAND, so the DEFAULT
## act for this rail is not an abort at all; and the second reason was right —
## this fixture supplies cq_pc_zero_proof_rotation_free, the SHADOW, which
## reports `unknown` and can only ever say UNPROVEN, so the rail lands in that
## row and never in the dirty one. The case is therefore kept as the
## shadow-evidence refusal it actually is (bd 06t's option (a), taken
## deliberately) and RUNS UNDER CQOPS_FREE_ABORT; its default disposition —
## it strands, by index — is pinned in the ordinary suite as
## tests/test_rotate.c:a_general_ry_rail_strands_rather_than_aborting.
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
## If M03's message ever appears here, cq_reg_free has started releasing before
## verifying and a dirty rail is half-returned.
## Note ckd18_* passes cq_pc_zero_proof_rotation_free rather than NULL, so it is
## the POISON that refuses it — which is that case's actual claim (bd ckd.18,
## closed; PRD §15 D15), and is what makes disabling cq_shadow_rotate turn this
## case red.
set_tests_properties(test_rotate_death.ckd18_a_general_ry_rail_cannot_be_freed
                     test_rotate_death.a_null_proof_still_refuses_a_materialised_rail
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "PREFLIGHT-;release of a qubit not proven;UndefinedBehaviorSanitizer")

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
    double_strand
    release_of_a_stranded_index
    strand_of_an_unminted_index
    strand_of_a_freed_index
    release_of_a_retired_index
    strand_of_a_retired_index
    recycling_disabled_with_a_non_empty_free_list
    ceiling_exceeded_because_nothing_is_recycled
)

## WHICH GUARD FIRED, for the four stranding deaths (PRD §15 D15 §3, Step 23).
## All eight of M03's hard errors share one cq_pool_die and therefore one
## "qubit pool:" prefix, so an exit code cannot tell them apart — and three of
## the four new cases are one edit away from being caught by a NEIGHBOURING
## guard instead of their own, which is this project's most-recorded failure
## shape (the same guard called three times, Step 8).
##
## Expressed NEGATIVELY, and that is deliberate: PASS_REGULAR_EXPRESSION
## DISPLACES the exit-code check while FAIL_REGULAR_EXPRESSION composes with
## it, so naming the messages that must NOT appear keeps the death test's own
## contract (tests/CMakeLists.txt:193-195). Each list is composed into ONE
## semicolon-separated property, because set_tests_properties OVERWRITES.
##
## release_of_a_stranded_index is the load-bearing one: it must die on the
## strand mark, NOT on "not proven |0>" — the case passes `1` precisely so that
## a guard placed after the proven_zero test would let it through, and NOT on
## "double release", which is the adjacent flag.
set_tests_properties(test_qubits_death.release_of_a_stranded_index
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "not proven;double release;never minted")

## The mirror: a double strand must not report as a double RELEASE, and must
## not be silently accepted into the free-list guard.
set_tests_properties(test_qubits_death.double_strand
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "double release;on the free list;never minted")

## And a strand of a freed index must name the free list rather than the
## strand map — the two flags are disjoint and this is what pins that.
set_tests_properties(test_qubits_death.strand_of_a_freed_index
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "double strand;never minted;not proven")

## Step 26's two retirement deaths, on the same argument, and both are load
## bearing for the same reason as release_of_a_stranded_index: the RETIRE flag
## is a THIRD disjoint state, so each case has two neighbouring guards that
## would happily catch it and report the wrong cause. The release case must not
## report as a double release (the free flag is 0 for a retired index) nor as
## "not proven" (the caller passes a 1, and did prove it, once); the strand case
## must not report as a double strand (the strand flag is 0) nor as a free-list
## strand. Both would otherwise pass while testing nothing.
set_tests_properties(test_qubits_death.release_of_a_retired_index
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "double release;not proven;STRANDED;never minted")

set_tests_properties(test_qubits_death.strand_of_a_retired_index
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "double strand;on the free list;never minted")

## And the ceiling reached through retirement must be the CEILING refusing, not
## the retire path — the whole content of the case is that `live` never falls,
## so a mutant that decremented it would make this case survive as a pass.
set_tests_properties(test_qubits_death.ceiling_exceeded_because_nothing_is_recycled
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "RETIRED;double release;not proven")

## The harness's own negative self-test: expected to exit non-zero. It is what
## makes every other suite's green meaningful.
add_cqops_test(test_harness_negative WILL_FAIL)

## The same argument for LEAK DETECTION (`bd kfi`). detect_leaks=1 is not a
## compile flag — it is a string in the ENVIRONMENT property that
## cmake/CqopsTest.cmake writes onto every test — so losing it would change
## nothing about any green run. This binary leaks on purpose and is the only
## thing in the tree that goes red when that happens.
##
## REGISTERED ONLY WHEN THE BUILD CLAIMS LEAK DETECTION. Without it the binary
## would exit 0 and WILL_FAIL would report a failure that is a correct
## description of a configuration we deliberately support (Release, a host with
## no LSan, CQOPS_LEAK_CHECK=OFF). The claim it makes is conditional, so its
## registration is too — and test_skeleton is what makes the ABSENCE loud, by
## checking the build's belief against the environment CTest handed it.
if(CQOPS_LSAN_ENABLED)
    add_cqops_test(test_lsan_negative WILL_FAIL)
endif()

## Layer 5 — M26's foundation (Step 23, landing 1). THE FIRST C SUITE OVER
## shim/: everything Step 22 put there is generated or generating, and the only
## thing that compiled any of it was a PyYAML-gated python subprocess.
##
## Four subjects, and each is here because something else would be green for
## the wrong reason without it: the process-global context and the reset that
## keeps a multi-case binary honest (cq_ctx_init borrows the sink ONCE, so case
## 2's cqops_set_sink is otherwise ignored and its gates land in case 1's mock);
## installation of the built-in sinks (bd utk — they had no caller, so any
## process that had not called cqops_set_sink hard-errored inside cq_ctx_init);
## the ONE §9 region bracket, with bd d6m fix (a)'s three arms; and PRD §1's
## cq_shim_unsupported, whose message was until now pinned only against a stub
## tests/test_gen_bodies.py writes for itself.
add_cqops_test(test_shim_ctx)

add_cqops_death_test(test_shim_ctx_death CASES
    the_v1_boundary_terminates
    a_control_flag_wider_than_one_bit
    the_no_region_sentinel_is_not_a_control_flag
    a_region_with_no_body
    a_region_body_that_does_not_balance_its_own_pushes
    a_region_body_that_pops_more_than_it_pushed
    a_nested_region
    a_tombstoned_control_flag
    a_reset_with_a_region_still_open
)

## WHICH LAYER ABORTED, and the UBSan tripwire (bd u76), composed into ONE
## semicolon-separated list per test because set_tests_properties OVERWRITES.
##
## `UndefinedBehaviorSanitizer` is not decoration: CQ_EXPECT_ABORT arms a
## SIGABRT window and cannot tell whose abort it caught, and UBSan runs
## -fno-sanitize-recover=all and calls abort() itself, so in Debug a case whose
## setup acquires undefined behaviour passes having verified nothing. It is
## FAIL_ and not PASS_ because PASS_REGULAR_EXPRESSION DISPLACES the exit-code
## check, trading the death test's own contract for a substring.
##
## AND WHICH SHIM GUARD, NOT ONLY WHICH LAYER. All five shim-owned refusals
## print the same `libcqops: FATAL: shim:` prefix, so an exit-code assertion
## cannot tell them apart and swapping two of their strings would survive every
## case in the file — CLAUDE.md's recorded Step 20 survivor (ii), "NO TEST READ
## THE REFUSAL MESSAGE". Each list therefore names the strings the OTHER shim
## refusals would have printed. (The positive half — that the right one really
## is printed, on stderr — is tests/test_shim_ctx.c's
## each_region_refusal_names_itself_on_stderr, which forks and reads the bytes;
## PASS_REGULAR_EXPRESSION could not do it without displacing the exit code.)
set_tests_properties(test_shim_ctx_death.a_control_flag_wider_than_one_bit
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;reg: handle out of range;with no body;never nests;unbalanced")

set_tests_properties(test_shim_ctx_death.a_region_with_no_body
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;reg: handle out of range;one bit wide;never nests;unbalanced")

set_tests_properties(test_shim_ctx_death.a_nested_region
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;reg: handle out of range;one bit wide;with no body;unbalanced")

## The two balance cases are the SAME guard reached from opposite directions,
## which is what pins `!= 1` rather than `> 1`. Both must be the shim's refusal:
## the popping one would otherwise be caught by M06's empty-stack abort one
## layer down, meaning the shim never looked.
set_tests_properties(test_shim_ctx_death.a_region_body_that_does_not_balance_its_own_pushes
                     test_shim_ctx_death.a_region_body_that_pops_more_than_it_pushed
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;reg: handle out of range;one bit wide;with no body;never nests;cq_ctrl_pop with no region open")

## THE MIRROR IMAGE, TWICE: these two MUST be somebody else's refusal, so the
## shim's own prefix is what must not appear. CQ_REG_NONE is the absence of the
## _controlled symbol and must not become a fifth row 0; a tombstone is M07's
## use-after-free diagnostic, and it is only M07's because the bracket resolves
## through cq_reg_cbits BEFORE comparing the width.
set_tests_properties(test_shim_ctx_death.the_no_region_sentinel_is_not_a_control_flag
                     test_shim_ctx_death.a_tombstoned_control_flag
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;FATAL: shim:")

## And this one must be M06's, reached THROUGH cq_ctx_dispose: it is the only
## detector for cq_shim_ctx_reset actually disposing rather than dropping the
## latch, whose other half is the ordinary case reading the disposed object.
set_tests_properties(test_shim_ctx_death.a_reset_with_a_region_still_open
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;FATAL: shim:")

## PRD §1's boundary prints `cqops: ... not implemented (...)` and NOT the house
## `libcqops: FATAL: <layer>:` prefix, which is what lets this assert negatively
## that no other layer spoke. The bytes themselves are pinned positively in
## tests/test_shim_ctx.c, which forks to read the child's stderr — a
## PASS_REGULAR_EXPRESSION here would have cost the exit-code check.
set_tests_properties(test_shim_ctx_death.the_v1_boundary_terminates
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;libcqops: FATAL")

## Layer 5 — M26's rail and gate surfaces (Step 23, landing 1, step 4). The
## first `cqrt_*` DEFINITIONS in the project: 32 rail symbols and 30 gate
## symbols, against `shim/cq_runtime_abi.h`'s verified copy of CQ_lang's 173
## declarations.
##
## THE RAIL SUITE IS SPLIT ON THE SOURCE FILE'S OWN RECORDED SEAM,
## `the rail's LIFETIME <-> the rail's CONTENTS`, and the .inc half carries the
## subject where THE COST IS THE SPECIFICATION: PRD §2.1 prices `cqrt_cswap`
## under a constant control at zero gates, and src/reg.h names three routes that
## give the right VALUE and the wrong cost.
add_cqops_test(test_runtime_rail)

## D15's OBSERVED UNDO CERTIFICATE (Step 23 landing 2, bd 06t). The seam
## `the ENGINE <-> the ENTRY CONDITIONS` was recorded in IMPLEMENTATION_PLAN §3
## before this file's first case and taken with it.
add_cqops_test(test_shim_cert)

## The recorder's ONE hard error. A separate binary, not a seam: CQ_DEATH_MAIN
## needs its own translation unit. It exists because a mutant survived — turning
## cq_rec_push's refusal of an unmodelled opcode into a `return` left the whole
## suite green, and a silently ignored call makes EVERY certificate rule wider,
## which is the direction that discharges frees it should not.
add_cqops_death_test(test_shim_cert_death CASES
    an_unmodelled_opcode_is_a_hard_error
    an_opcode_past_the_table_is_a_hard_error
)
## NEGATIVE ONLY, so it composes with the exit-code check rather than displacing
## it (PASS_REGULAR_EXPRESSION would). It names the layers that must NOT have
## spoken: the abort has to come from the RECORDER, not from a rail resolve, a
## free, or a sanitizer.
set_tests_properties(test_shim_cert_death.an_unmodelled_opcode_is_a_hard_error
                     test_shim_cert_death.an_opcode_past_the_table_is_a_hard_error
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "reg:;rail:;qubit pool:;shadow:;rotate:;release of a qubit not proven;UndefinedBehaviorSanitizer")
add_cqops_test(test_runtime_gate)

## The V1 BOUNDARY (Step 23 landing 1 step 5). NOT a death suite, and the
## reason is the whole design: with 109 macro-expanded abort bodies the
## interesting failure is not a missing abort but one that NAMES THE WRONG
## SYMBOL or carries the WRONG BUCKET, and a FAIL_REGULAR_EXPRESSION cannot see
## either. Every one of the 109 is forked and its stderr compared byte for
## byte, against an enumeration read out of shim/cq_runtime_abi.h.
add_cqops_test(test_runtime_v2)

add_cqops_death_test(test_runtime_rail_death CASES
    measure_of_a_rail_wider_than_its_symbol
    copy_between_rails_of_different_widths
    copy_of_two_rails_of_the_same_wrong_width
    addc_of_a_rail_wider_than_its_symbol
    xorc_of_a_rail_wider_than_its_symbol
    cswap_of_a_rail_with_itself
    cswap_whose_control_aliases_a_rail_it_writes
    cswap_whose_control_aliases_its_second_rail
    cswap_with_a_control_flag_wider_than_one_bit
    cswap_between_rails_of_different_widths
    cswap_validates_its_operands_even_on_the_zero_row
    double_free_is_m07s_refusal
    free_of_a_measured_rail_is_m07s_refusal
    a_second_measure_is_m07s_refusal
    use_of_a_freed_rail_is_m07s_refusal
    xorc_of_a_freed_rail_of_the_wrong_width_is_a_use_after_free
    xorc_of_a_measured_rail_is_a_write_refusal
    measure_of_a_freed_rail_of_the_wrong_width_is_a_use_after_free
)

## Step 23, landing 1, step 8 — THE LINK GATE. Two tables, both external-linkage
## address tables read at a RUNTIME index, plus the set-difference arm the link
## cannot make. See cmake/CqopsLinkWitness.cmake for the four measured ways the
## obvious witness is green and vacuous, and cmake/CqopsSymbolSets.cmake for the
## outward direction the witness is structurally blind to.
include(${CMAKE_SOURCE_DIR}/cmake/CqopsLinkWitness.cmake)
cqops_add_link_witness()

## Layer 5 — M26's OPCODE surface (Step 23, landing 1, step 6). The FIFTEEN
## `cq_shim_*` entry points all 992 generated integer wrappers call, split on
## the seam IMPLEMENTATION_PLAN §3 recorded before either file existed:
## `the OPCODE SURFACE <-> the HANDLE BOUNDARY`, with the boundary half in
## tests/test_template_d7.inc.
add_cqops_test(test_template)

add_cqops_death_test(test_template_death CASES
    d7a_out_aliases_the_first_source_of_a_qq_unc
    d7a_out_aliases_the_second_source_of_a_qq_unc
    d7a_out_aliases_the_only_handle_of_an_hl_unc
    d7a_out_aliases_the_only_handle_of_an_lh_unc
    d7a_out_aliases_a_source_of_an_icmp_qq_unc
    d7a_out_aliases_the_source_of_an_icmp_hl_unc
    d7a_out_aliases_the_source_of_a_cast_unc
    a_source_rail_is_not_the_width_its_symbol_names
    an_unc_destination_that_has_been_measured_is_a_write_refusal
    an_unc_destination_is_not_the_width_its_symbol_names
    an_icmp_unc_destination_wider_than_one_bit_is_refused
    a_freed_source_of_the_wrong_width_is_a_use_after_free
    an_opcode_outside_the_abis_enum_is_refused
    a_predicate_outside_the_abis_enum_is_refused
    a_cast_kind_outside_the_abis_enum_is_refused
    a_width_outside_the_abis_range_is_refused
    a_cast_to_a_width_above_the_abis_range_is_refused
)

## D7a IS M07'S REFUSAL AND MUST STAY M07'S. The shim resolves the handles and
## hands them to `cq_reg_check_operands`; if it ever grew its own copy of the
## check, the masking-layer trap this project has recorded five times would put
## the shim's message here instead and nobody would notice. So the list bans
## every string the SHIM could have printed — and bans M05's `distinctness`
## too, because an unrefused D7a reaches a kernel with `dst == a` and aborts
## there in DEBUG ONLY, seventeen steps from its cause (Rule 11's prediction).
set_tests_properties(test_template_death.d7a_out_aliases_the_first_source_of_a_qq_unc
                     test_template_death.d7a_out_aliases_the_second_source_of_a_qq_unc
                     test_template_death.d7a_out_aliases_the_only_handle_of_an_hl_unc
                     test_template_death.d7a_out_aliases_the_only_handle_of_an_lh_unc
                     test_template_death.d7a_out_aliases_a_source_of_an_icmp_qq_unc
                     test_template_death.d7a_out_aliases_the_source_of_an_icmp_hl_unc
                     test_template_death.d7a_out_aliases_the_source_of_a_cast_unc
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;FATAL: shim:;distinctness;kernel:;not live")

## THE WIDTH TOKEN IS THE SHIM'S OWN CLAIM and must NOT be answered by M07: a
## LIVE rail of the wrong width is not a use-after-free, and the enum guards
## must stay out of it — a table lookup that aborted for a legal opcode would
## satisfy every one of these cases while breaking the whole grid.
set_tests_properties(test_template_death.a_source_rail_is_not_the_width_its_symbol_names
                     test_template_death.an_unc_destination_is_not_the_width_its_symbol_names
                     test_template_death.an_icmp_unc_destination_wider_than_one_bit_is_refused
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;outside the ABI;FATAL: reg:;kernel:;distinctness")

## RESOLVE FIRST, COMPARE SECOND — the ORDER claim `bd pnu` found unpinned on
## four sibling helpers. These two must be M07's message and NOT the shim's
## width message, which is the whole difference the hoisted comparison makes.
set_tests_properties(test_template_death.a_freed_source_of_the_wrong_width_is_a_use_after_free
                     test_template_death.an_unc_destination_that_has_been_measured_is_a_write_refusal
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;not the width its cq_template;outside the ABI;kernel:")

## AND THE ENUM GUARDS MUST BE THE DISPATCH FILE'S, not a width check and not
## M07's. Each names an ENUM, and `cq_template_dispatch.c`'s three strings are
## deliberately disjoint from `cq_template_impl.c`'s, so swapping two of them
## would be caught here rather than by a reviewer.
set_tests_properties(test_template_death.an_opcode_outside_the_abis_enum_is_refused
                     test_template_death.a_predicate_outside_the_abis_enum_is_refused
                     test_template_death.a_cast_kind_outside_the_abis_enum_is_refused
                     test_template_death.a_width_outside_the_abis_range_is_refused
                     test_template_death.a_cast_to_a_width_above_the_abis_range_is_refused
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;not the width its cq_template;FATAL: reg:;kernel:;distinctness")

add_cqops_death_test(test_runtime_gate_death CASES
    x_on_a_rail_wider_than_one_bit
    cnot_on_a_rail_wider_than_one_bit
    toffoli_on_a_rail_wider_than_one_bit
    x_controlled_on_a_rail_wider_than_one_bit
    cnot_controlled_on_a_rail_wider_than_one_bit
    rz_of_a_rail_wider_than_its_symbol
    cnot_whose_control_is_its_target
    toffoli_with_two_equal_controls
    x_controlled_whose_flag_is_its_target
    x_controlled_whose_zero_flag_is_its_target
    toffoli_whose_first_control_is_its_target
    toffoli_whose_second_control_is_its_target
    cnot_controlled_whose_flag_is_its_inner_control
    cnot_controlled_whose_flag_is_its_target
    cnot_controlled_whose_inner_control_is_its_target
    cnot_controlled_whose_zero_flag_is_its_inner_control
    cnot_controlled_whose_zero_flag_is_its_target
    x_of_a_measured_rail_is_a_write_refusal
    x_of_a_freed_rail_wider_than_one_bit_is_a_use_after_free
    cnot_whose_control_is_a_freed_wide_rail_is_a_use_after_free
    ry_of_a_rail_wider_than_its_symbol
    rz_of_a_rail_narrower_than_its_symbol
    rz_controlled_of_a_rail_wider_than_its_symbol
    rz_controlled_inv_of_a_rail_wider_than_its_symbol
    ry_controlled_inv_of_a_rail_wider_than_its_symbol
    ry_of_a_freed_rail_of_the_wrong_width_is_a_use_after_free
    ry_of_a_measured_rail_of_the_wrong_width_is_a_write_refusal
    rz_controlled_on_a_constant_lane_is_m06s_refusal
    ry_controlled_inv_at_a_half_turn_is_m06s_refusal
)

## WHICH SHIM GUARD, NOT ONLY WHICH LAYER, composed into ONE semicolon-separated
## list per group because set_tests_properties OVERWRITES — a second block
## silently disarms the first and everything stays green.
##
## All five rail-side refusals print the same `libcqops: FATAL: shim:` prefix,
## so an exit-code assertion cannot tell them apart and swapping two of their
## strings would survive every case in the file. Each list therefore names the
## strings the OTHER refusals would have printed. `UndefinedBehaviorSanitizer`
## leads every list (bd u76): CQ_EXPECT_ABORT cannot tell whose abort it caught,
## and UBSan calls abort() itself under -fno-sanitize-recover=all.
## The width token is a claim about the rail — and it must be OUR refusal,
## not M07's: a live rail of the wrong width is not a use-after-free.
## AND `width mismatch in a copy` IS IN THE LIST BECAUSE IT WAS MISSING AND THAT
## COST THE CASE ITS TEETH. `copy_between_rails_of_different_widths` sets up two
## rails of DIFFERENT widths, which is exactly what `cq_reg_xor_into` refuses on
## its own — measured, with the shim's check disabled the case still exits 0 on
## M07's message. The sibling cswap block already banned M07's swap string, so
## this was an omission rather than a policy.
##
## `copy_of_two_rails_of_the_same_wrong_width` is the shape only the shim can
## refuse, and it is why the fix is a new case and not just a longer regex: with
## both rails the same width M07 finds nothing wrong, so the ABI's width TOKEN is
## the only thing violated.
set_tests_properties(test_runtime_rail_death.measure_of_a_rail_wider_than_its_symbol
                     test_runtime_rail_death.copy_between_rails_of_different_widths
                     test_runtime_rail_death.copy_of_two_rails_of_the_same_wrong_width
                     test_runtime_rail_death.addc_of_a_rail_wider_than_its_symbol
                     test_runtime_rail_death.xorc_of_a_rail_wider_than_its_symbol
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;cswap of a rail with itself;control aliases a rail it writes;cswap control flag rail is not one bit;cswap between rails of different widths;width mismatch in a copy;reg: handle out of range;not live")

## cq_reg_swap_bits has its own `a == b` refusal and it must NOT be the one
## that fires: it is reached only on the CQ_BIT_ONE row, so leaning on it would
## leave the ZERO row a silent no-op and the quantum row a Release miscompile.
##
## THE LAYER PREFIX IS LOAD-BEARING IN THIS ONE and it went red before it went
## green. M07's message is `swap of a rail with itself` and the shim's is
## `cqrt_cswap of a rail with itself` — the first is a SUBSTRING of the second,
## so the obvious negative pattern matched the very refusal the case wants and
## reported a correct abort as a failure. `FATAL: reg:` is what tells the two
## apart. Whenever a shim message is worded after the one it is meant to
## displace, check the negative list against the positive case.
set_tests_properties(test_runtime_rail_death.cswap_of_a_rail_with_itself
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;not the width its cqrt;control aliases a rail it writes;cswap control flag rail is not one bit;cswap between rails of different widths;FATAL: reg: swap of a rail with itself")

## AND IT MUST NOT BE M05's DISTINCTNESS ASSERT, which is Debug-gated: if
## that is what fires, the Release build has no detector at all.
set_tests_properties(test_runtime_rail_death.cswap_whose_control_aliases_a_rail_it_writes
                     test_runtime_rail_death.cswap_whose_control_aliases_its_second_rail
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;not the width its cqrt;cswap of a rail with itself;cswap control flag rail is not one bit;cswap between rails of different widths;distinctness")

## The flag guard, and the rail guard's string must be absent: they are
## different claims about different operands.
set_tests_properties(test_runtime_rail_death.cswap_with_a_control_flag_wider_than_one_bit
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;not the width its cqrt;cswap of a rail with itself;control aliases a rail it writes;cswap between rails of different widths")

## Both must be the SHIM's width refusal reached before the branch on the
## flag's kind — cq_reg_swap_bits's own is on the ONE row only, so if that
## string appears the zero-row case is passing for the wrong reason.
set_tests_properties(test_runtime_rail_death.cswap_between_rails_of_different_widths
                     test_runtime_rail_death.cswap_validates_its_operands_even_on_the_zero_row
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;not the width its cqrt;cswap of a rail with itself;control aliases a rail it writes;cswap control flag rail is not one bit;width mismatch in a rail-to-rail swap")

## THE MIRROR IMAGE: these four MUST be M07's, so the shim's own prefix is
## what must not appear. A shim that re-implemented any of them would be the
## masking layer this project has recorded four times — and would make M07's
## own death cases untestable from here.
set_tests_properties(test_runtime_rail_death.double_free_is_m07s_refusal
                     test_runtime_rail_death.free_of_a_measured_rail_is_m07s_refusal
                     test_runtime_rail_death.a_second_measure_is_m07s_refusal
                     test_runtime_rail_death.use_of_a_freed_rail_is_m07s_refusal
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;FATAL: shim:")

## `rail_w`'s ORDER AND ITS ACCESSOR — two properties of one three-line
## function, and each case below is the sole detector for one of them. Both
## aborts are M07's, so the discriminator is the whole `FATAL: shim:` prefix:
## under mutant rail-R03 (resolve and compare swapped) the freed case prints the
## SHIM's width message instead of M07's tombstone one, and both orders still
## abort, so the exit code alone cannot see it.
##
## THE WIDTHS DIFFER ON PURPOSE. The order case needs a WRONG width — the only
## shape where the two orders disagree, which is exactly why the file's existing
## `use_of_a_freed_rail_is_m07s_refusal` (a freed i32 handed to `cqrt_xorc_i32`)
## could not see rail-R03. The accessor case needs a MATCHING one: with the
## width wrong it would abort under mutant rail-R04 too, on the shim's width
## message, and would be a weaker second copy of the order case rather than the
## sole detector for terminality-on-write.
set_tests_properties(test_runtime_rail_death.xorc_of_a_freed_rail_of_the_wrong_width_is_a_use_after_free
                     test_runtime_rail_death.xorc_of_a_measured_rail_is_a_write_refusal
                     test_runtime_rail_death.measure_of_a_freed_rail_of_the_wrong_width_is_a_use_after_free
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;FATAL: shim:")

## The gate surface. The one-bit guard is a DIFFERENT string from the rail
## surface's width guard, and both are listed against each other so a copy-paste
## between the two files goes red.
set_tests_properties(test_runtime_gate_death.x_on_a_rail_wider_than_one_bit
                     test_runtime_gate_death.cnot_on_a_rail_wider_than_one_bit
                     test_runtime_gate_death.toffoli_on_a_rail_wider_than_one_bit
                     test_runtime_gate_death.x_controlled_on_a_rail_wider_than_one_bit
                     test_runtime_gate_death.cnot_controlled_on_a_rail_wider_than_one_bit
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;the same rail twice;not the width its cqrt;D11;not live")

## The rotation width guard is the RAIL surface's string, not the one-bit one:
## a rotation is defined on a rail of any width, so what it refuses is a rail
## that is not the width its own symbol names.
set_tests_properties(test_runtime_gate_death.rz_of_a_rail_wider_than_its_symbol
                     test_runtime_gate_death.ry_of_a_rail_wider_than_its_symbol
                     test_runtime_gate_death.rz_of_a_rail_narrower_than_its_symbol
                     test_runtime_gate_death.rz_controlled_of_a_rail_wider_than_its_symbol
                     test_runtime_gate_death.rz_controlled_inv_of_a_rail_wider_than_its_symbol
                     test_runtime_gate_death.ry_controlled_inv_of_a_rail_wider_than_its_symbol
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;wider than one bit;the same rail twice;D11")

## THE ORDER, NOT THE REFUSAL. `gate_rot_width`'s `(void)cq_reg_bits` buys one
## thing: M07's lifetime refusal lands BEFORE the width comparison, because
## cq_reg_width goes through cq_reg_slot and a tombstone keeps its width on
## purpose. The refusal itself is M22's one layer down (src/rotate.c), so the
## discriminator has to be NEGATIVE and it is `not the width its cqrt` — with
## the line deleted that is exactly what these two print instead, and they still
## abort, so the exit code alone cannot see it. Measured: the deletion survives
## all 256 tests in both configurations without these two. Keep them in EXACTLY
## this one block — set_tests_properties OVERWRITES, and a second block naming
## either case would silently delete the only thing with teeth here.
set_tests_properties(test_runtime_gate_death.ry_of_a_freed_rail_of_the_wrong_width_is_a_use_after_free
                     test_runtime_gate_death.ry_of_a_measured_rail_of_the_wrong_width_is_a_write_refusal
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;not the width its cqrt;wider than one bit;the same rail twice;D11")

## AND NOT M05's DISTINCTNESS ASSERT, which is Debug-gated — if that is what
## fires then Release has no detector and the measured malformed `cx q0 q0`
## reaches the sink.
## AND `FATAL: controlled:` JOINS THE LIST, added 2026-08-27 for `bd pnu`. M06's
## check_one refuses PRD §9 row B one layer down, and once tests/test_runtime_
## gate_death.c's `one_bit()` started minting a real CQ_BIT_Q wire it began
## catching the three FLAG coincidences whenever the shim's `gate_distinct` was
## deleted — with a message containing none of the four discriminators above, so
## the cases kept exiting 0. Measured: gate-G13, gate-G15 and gate-G16b all
## survived the whole 258-test suite in both configurations.
##
## IT DISARMS NOTHING, and that was checked case by case rather than assumed:
## `gate_distinct` runs BEFORE `cq_shim_region` in both controlled entry points
## and every one of these eight has a coincident pair, so in a correct tree the
## shim's own message is the only one printed and M06 never speaks. The four
## uncontrolled cases cannot reach M06 at all. Verified by running all eight
## after the edit.
##
## THE SECOND ARM IS THE BLOCK BELOW, and it is the one with teeth: a
## CQ_BIT_ZERO flag, where M06 CANNOT fire and the shim's guard is not merely
## first but ALONE.
set_tests_properties(test_runtime_gate_death.cnot_whose_control_is_its_target
                     test_runtime_gate_death.toffoli_with_two_equal_controls
                     test_runtime_gate_death.x_controlled_whose_flag_is_its_target
                     test_runtime_gate_death.toffoli_whose_first_control_is_its_target
                     test_runtime_gate_death.toffoli_whose_second_control_is_its_target
                     test_runtime_gate_death.cnot_controlled_whose_flag_is_its_inner_control
                     test_runtime_gate_death.cnot_controlled_whose_flag_is_its_target
                     test_runtime_gate_death.cnot_controlled_whose_inner_control_is_its_target
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;wider than one bit;not the width its cqrt;distinctness;D11;FATAL: controlled:")

## `gate_ctrl`'s AND `gate_target`'s ORDER — the same negative discriminator as
## the rail surface's, and for the same reason: both fixtures are a FREED rail
## whose width is ALSO wrong, which is the only shape where the two orders
## disagree, and both orders still abort, so the exit code alone cannot see it.
## Under mutants gate-G20 / gate-G21 (the comparison hoisted above the resolve)
## these print the shim's `wider than one bit` instead of M07's `write access to
## a rail that is not live` / `use of a freed handle`. Banning the whole
## `FATAL: shim:` prefix is what makes them claims about WHO refuses.
set_tests_properties(test_runtime_gate_death.x_of_a_freed_rail_wider_than_one_bit_is_a_use_after_free
                     test_runtime_gate_death.cnot_whose_control_is_a_freed_wide_rail_is_a_use_after_free
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;FATAL: shim:")

## PRD §9 ROW 0, WHERE THE SHIM'S COINCIDENCE GUARD IS THE ONLY DETECTOR THERE
## IS. The flag is a CQ_BIT_ZERO, so `cq_shim_region` opens a SKIP frame: M06
## never promotes and so never compares a wire, and M05's `cq_bit_coincident`
## fires only on two CQ_BIT_Q bits, of which this fixture has none. With
## `gate_distinct` deleted these three do not abort AT ALL — the exit code
## convicts, and the negative list below is what stops them passing on some
## OTHER refusal. `distinctness` and `FATAL: controlled:` are in it although
## both are structurally unreachable here: that is the CLAIM (nothing but the
## shim may speak on row 0), not dead text, and it is what goes red the day a
## guard migrates down a layer.
##
## `x_of_a_measured_rail_is_a_write_refusal` shares the list for one reason —
## it must be M07's `write access to a rail that is not live`, so every shim
## string has to be absent, `wider than one bit` most of all: its rail is ONE
## bit wide precisely so the one-bit guard cannot be what fires.
set_tests_properties(test_runtime_gate_death.x_controlled_whose_zero_flag_is_its_target
                     test_runtime_gate_death.cnot_controlled_whose_zero_flag_is_its_inner_control
                     test_runtime_gate_death.cnot_controlled_whose_zero_flag_is_its_target
                     test_runtime_gate_death.x_of_a_measured_rail_is_a_write_refusal
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;wider than one bit;not the width its cqrt;distinctness;D11;FATAL: controlled:")

## D11 IS M06's AND MUST STAY M06's. PRD §15 D11 places the refusal in the
## rotation module; a shim-side copy would pre-empt it, make M22's own five
## death cases unreachable from here, and be a second place to keep correct.
set_tests_properties(test_runtime_gate_death.rz_controlled_on_a_constant_lane_is_m06s_refusal
                     test_runtime_gate_death.ry_controlled_inv_at_a_half_turn_is_m06s_refusal
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                     "UndefinedBehaviorSanitizer;AddressSanitizer;FATAL: shim:")

## Layer 5 — the shim generator (M27, Step 22).
##
## THE FIRST NON-C SUITES IN THE TREE, and the first raw add_test:
## add_cqops_test resolves ${name}.c against a CMake TARGET, and a .py file is
## neither. They are registered here rather than by widening CqopsTest.cmake,
## because everything that function does for a C binary — linking
## cqops_test_support, the sanitizer ENVIRONMENT — is meaningless for a
## generator that emits text.
##
## The interpreter is PROBED (cmake/CqopsPython.cmake): PyYAML is what decides,
## and on this box the two python3 on PATH disagree about having it. When none
## qualifies these two are NOT registered and CMake warns — a suite that passes
## without running would be worse than a test count that visibly drops.
##
## CQOPS_CC hands the suites the compiler THIS build configured, so the compile
## check runs against the same toolchain everything else does. WORKING_DIRECTORY
## is the source root because the generator's default paths are relative to it.
cqops_select_python(CQOPS_PY)
if(CQOPS_PY)
    foreach(pysuite test_gen_shim test_gen_bodies)
        add_test(NAME ${pysuite}
                 COMMAND ${CQOPS_PY} "${CMAKE_CURRENT_SOURCE_DIR}/${pysuite}.py"
                 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
        ## `not ok` is NOT anchored, and that is a correction rather than a
        ## looseness. CTest matches FAIL_REGULAR_EXPRESSION against the WHOLE
        ## captured output as one string, so `^` anchors at the first byte of the
        ## run — which is always "TAP version 13". The anchored form this line
        ## carried could never match: a build-level detector nobody had seen fire,
        ## next to a real one, which is the `set_tests_properties` family of trap
        ## CLAUDE.md already records once. Today the exit code governs
        ## (`sys.exit(sc.run(...))`), so this is the independent second detector —
        ## it also catches a suite that reports failures and then exits 0.
        set_tests_properties(${pysuite} PROPERTIES
            ENVIRONMENT "CQOPS_CC=${CMAKE_C_COMPILER};PYTHONDONTWRITEBYTECODE=1"
            FAIL_REGULAR_EXPRESSION "not ok")
    endforeach()
endif()

## ---------------------------------------------------------------------------
## L6 — Step 24. CQ_lang's own e2e fixtures, lowered by CQ_lang's own pass and
## LINKED AGAINST libcqops instead of its trace-only runtime stub.
##
## THE GATE IS PRD §15 D18: THE FIXTURES LINK, RUN, AND DO NOT ABORT. There is
## no diff, and adding one would reintroduce a retired criterion — read
## `tools/l6/run_slice_cqops.sh`'s header for the two measured reasons the
## goldens are not an oracle in any column.
##
## OPT-IN, AND THAT IS NOT A DODGE. libcqops is a standalone library and CQ_lang
## is ONE CALLER of it (PRD §15 D18): this repo does not pin CQ_lang, does not
## know where it lives on a given box, and cannot build it. A test registered
## unconditionally would either be skipped — a green run claiming a link it never
## made — or would make the default `make test` depend on a tree we do not
## control. So it registers only when pointed at one, and CMake says so at
## configure time either way, on `cqops_select_python`'s precedent below: a
## suite that is silently absent is worse than a test count that visibly moves.
##
## `--min-ok` IS A FLOOR, NOT A PIN. CQ_lang's corpus grows (239 → 241 → 249
## goldens over four days, measured), so an exact count would churn on every
## upstream commit; a floor still catches an in-scope fixture regressing into
## the casualty list, which is the regression that matters. RE-MEASURE IT when
## the number moves rather than lowering it to make a run green.
set(CQOPS_CQLANG_DIR "" CACHE PATH
    "Path to a built CQ_lang checkout; enables the L6 (Step 24) fixture run")
set(CQOPS_CQLANG_BUILD "build" CACHE STRING
    "CQ_lang build directory, relative to CQOPS_CQLANG_DIR")
set(CQOPS_L6_MIN_OK "43" CACHE STRING
    "Floor on the number of L6 fixtures that run to completion")

find_program(CQOPS_PY3 NAMES python3)
if(CQOPS_CQLANG_DIR AND CQOPS_PY3 AND EXISTS
   "${CQOPS_CQLANG_DIR}/${CQOPS_CQLANG_BUILD}/runtime/libcq_templates.a")
    add_test(NAME l6_cqlang_fixtures
             COMMAND ${CQOPS_PY3} "${CMAKE_SOURCE_DIR}/tools/l6/l6_run.py"
                     --cqlang "${CQOPS_CQLANG_DIR}"
                     --cqbuild "${CQOPS_CQLANG_BUILD}"
                     --archive "$<TARGET_FILE:cqops>"
                     --out "${CMAKE_CURRENT_BINARY_DIR}/l6"
                     --json "${CMAKE_CURRENT_BINARY_DIR}/l6.json"
                     --gate --min-ok "${CQOPS_L6_MIN_OK}")
    ## The whole corpus is minutes, not seconds — it is 250 four-stage compile /
    ## lower / link / run pipelines, not a unit suite.
    set_tests_properties(l6_cqlang_fixtures PROPERTIES TIMEOUT 3600)
else()
    message(STATUS "cqops: L6 (Step 24) not registered — "
                   "configure with -DCQOPS_CQLANG_DIR=<a built CQ_lang> to run "
                   "CQ_lang's e2e fixtures against this archive")
endif()
