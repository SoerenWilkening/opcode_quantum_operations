/* tests/test_template_fp.c — M26's fp ARITHMETIC surface (bead 9ve.36,
 * PRD-v2 §1 / §5 / §7.15): the nine `cq_shim_fbin_*` entry points, the two
 * `cq_shim_fcast*` ones, the two `cq_shim_fun*` ones, and the 64 generated
 * wrappers plus 37 D14 `_inv` aborts that stand behind them.
 *
 * A NEW BINARY RATHER THAN A FOURTH `.inc` OF tests/test_template.c. That file
 * stood at 279 counted lines of Rule 12's 300 with three `.inc`s already
 * hanging off it, so another subject there is the surprise refactor Rule 12
 * forbids. The cut is the one shim/cq_template_fp.c's own header records one
 * level down — `the COMPARE family <-> the ARITHMETIC families` — and the
 * discriminator is the same: a compare's result is one bit and has no `_lh`
 * shape and no §9 axis, while every family here has all three.
 *
 * THE ORACLE IS THE KERNEL'S OWN `cq_*_eval`, AND NEVER THE HOST `double`
 * OPERATOR. PRD-v2 §7.4: `Inf − Inf`, `0 · Inf` and which NaN payload survives
 * are x86's choices and ARM differs on all of them, so `a + b` in C is not a
 * statement about what this library must compute. That is the opposite of
 * tests/test_template_fcmp.inc, where the host IS the oracle because IEEE
 * fixes all fourteen comparison rows on every operand pair — and the reason
 * the two files reach for different references is worth stating rather than
 * leaving to be rediscovered.
 *
 * WHAT AN `_eval` ORACLE CAN AND CANNOT SEE. It is the kernel's own classical
 * row, so it cannot judge the kernel; M33/M34/M35/M37/M40's own suites do that
 * against §7.12's anchors and §7.5's pinned tables. What it judges here is the
 * DISPATCH — that `CQ_SHIM_FOP_FSUB` reaches `cq_kernel_fsub` and not
 * `cq_kernel_fadd`, that `fptosi f64 -> i8` is the 64-bit kernel followed by a
 * trunc and not a 64-bit kernel with its top lanes dropped — which is exactly
 * what a table written by index gets wrong and what nothing structural sees
 * (K9's `uge`-meaning-`ule`, one more time).
 *
 * COST IS A DESIGN CONSTRAINT IN THIS FILE, not an afterthought. One
 * all-quantum `fdiv` is 235,568 gates over 64,659 scratch qubits and one
 * `fmul` is 294,692; every such call runs twice through a recording sink in
 * Debug under ASan. So the all-quantum calls are COUNTED and placed
 * deliberately: the value, L2 and dispatch claims ride the all-CLASSICAL mask
 * (which is also L5, and is where the operand VALUE reaches the circuit at
 * all), and a wire is used only where the claim is about wires — the pool, the
 * certificate, §9's promotion and D7b.
 */

#include "cq_shim.h"
#include "cq_shim_ctx.h"
#include "cq_shim_proof.h"
#include "cq_shim_record.h"

#include "cq_runtime_abi.h"

#include "bit.h"
#include "controlled.h"
#include "ctx.h"
#include "qubits.h"
#include "reg.h"
#include "rotate.h"
#include "sink.h"

#include "kernels/fadd.h"
#include "kernels/fconv.h"
#include "kernels/fdiv.h"
#include "kernels/fmul.h"
#include "kernels/fsqrt.h"

#include "cqops/cqops.h"

#include "support/bitkinds.h"
#include "support/childcap.h"
#include "support/fpanchors.h"
#include "support/harness.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"
#include "support/refmodel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* tests/test_template.c's three helpers, verbatim: the context latches its
 * sink at construction, so both the SELECTION and the CONTEXT must be reset
 * before the first cq_shim_ctx() of a case. */
static void fresh(void)
{
    cq_sink_reset();
    unsetenv("CQOPS_SINK");
    cq_shim_ctx_reset();
}

static cq_ctx *open_with(cq_mock *m, cq_sink *s)
{
    cq_mock_init(m);
    *s = cq_mock_sink(m);
    fresh();
    cqops_set_sink(s);
    return cq_shim_ctx();
}

static void close_with(cq_mock *m)
{
    cq_shim_ctx_reset();
    cqops_set_sink(NULL);
    cq_mock_dispose(m);
}

/* An ALL-QUANTUM rail at width W holding `v`. */
static int32_t qreg(cq_ctx *ctx, uint32_t W, uint64_t v)
{
    return cq_bk_reg(ctx, W, v, cq_ref_mask((int)W));
}

/* A `double` carrying a chosen BIT PATTERN. A memcpy, never a union and never
 * a pointer cast (the last is the strict-aliasing violation this build's UBSan
 * diagnoses), and never `0.0/0.0` — this file does no host fp arithmetic. */
static double f64_of(uint64_t u)
{
    double d;
    memcpy(&d, &u, sizeof d);
    return d;
}

/* §7.12's cells as OPERAND PAIRS. Each row is here because some pair of the
 * four arithmetic opcodes disagrees about it and about nothing cheaper: two
 * ordinary normals in both orders (`fsub`/`fdiv` are the order detectors and
 * `fadd`/`fmul` are blind to a transposition), ±0 (the sign of a zero result
 * is an arithmetic fact the compare surface cannot reach), a NaN in each
 * position (which payload survives is §7.4's x86-pinned cell), an infinity
 * pair (`Inf - Inf` and `Inf / Inf` are the INDEF rows), and a subnormal. */
static const uint64_t FP_PAIRS[][2] = {
    { CQ_F64_ONE,         0x4000000000000000ull },  /* 1.0, 2.0            */
    { 0x4000000000000000ull, CQ_F64_ONE         },  /* 2.0, 1.0            */
    { CQ_F64_POS_ZERO,    CQ_F64_NEG_ZERO       },  /* +0, -0              */
    { CQ_F64_QNAN_A,      CQ_F64_ONE            },  /* NaN lhs             */
    { CQ_F64_ONE,         CQ_F64_QNAN_B         },  /* NaN rhs             */
    { CQ_F64_POS_INF,     CQ_F64_POS_INF        },  /* Inf-Inf, Inf/Inf    */
    { CQ_F64_NEG_INF,     CQ_F64_POS_ZERO       },  /* -Inf, +0            */
    { 0x0000000000000001ull, CQ_F64_ONE         }   /* the smallest subnormal */
};

#define FP_N_PAIRS (sizeof FP_PAIRS / sizeof FP_PAIRS[0])

#include "test_template_fbin.inc"
#include "test_template_fconv.inc"
#include "test_template_fun.inc"

CQ_TEST_MAIN(
    CQ_CASE(every_fbin_f64_wrapper_agrees_with_its_kernels_own_eval),
    CQ_CASE(the_fbin_literal_doors_carry_the_pattern_on_the_side_they_name),
    CQ_CASE(an_fbin_f64_round_trip_on_wires_is_the_value_l2_and_the_pool),
    CQ_CASE(an_fbin_f64_unc_on_a_poisoned_source_is_discharged_by_the_certificate),
    CQ_CASE(an_fadd_unc_does_not_pair_with_an_add_i64_forward_on_the_same_handles),
    CQ_CASE(a_controlled_fbin_call_obeys_row_zero_on_a_classical_flag),
    CQ_CASE(a_quantum_flag_makes_the_classical_short_circuit_a_wire),
    CQ_CASE(d7b_on_the_fp_doors_costs_exactly_two_w_cnots_over_an_unaliased_call),
    CQ_CASE(every_fbin_f64_inv_aborts_naming_itself_and_d14s_reason),
    CQ_CASE(every_shipped_fconv_pair_agrees_with_its_kernels_own_eval),
    CQ_CASE(a_narrow_fconv_composes_the_kernel_with_the_integer_cast),
    CQ_CASE(a_composed_conversion_mints_its_workspace_before_its_result),
    CQ_CASE(the_composed_conversion_records_three_calls_and_names_a_in_the_outer),
    CQ_CASE(a_narrow_fconv_on_wires_frees_its_workspace_and_restores_the_pool),
    CQ_CASE(a_narrow_fconv_unc_on_a_poisoned_source_is_discharged_by_the_certificate),
    CQ_CASE(an_fconv_unc_does_not_pair_with_an_integer_cast_on_the_same_handles),
    CQ_CASE(the_uitofp_i64_row_aborts_naming_bead_9ve34_on_all_three_symbols),
    CQ_CASE(every_fconv_inv_aborts_naming_itself_and_d14s_reason),
    CQ_CASE(the_unary_door_dispatches_fsqrt_on_a_constant_rail),
    CQ_CASE(a_fsqrt_round_trip_on_wires_zeroes_its_out_rail_and_frees_clean),
    CQ_CASE(a_fsqrt_unc_does_not_pair_with_an_fadd_forward_on_the_same_handles),
    CQ_CASE(a_fsqrt_unc_on_a_poisoned_source_is_discharged_by_the_certificate)
)
