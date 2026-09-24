/* tests/test_template_fmisc.c — M38 at the ABI edge (bead 9ve.44): `fneg`,
 * `fabs` and the six rounding opcodes through `cq_shim_fun`, and `fmin`,
 * `fmax`, `copysign` through the `cq_shim_fbin_*` doors, every one reached
 * through its GENERATED `cq_template_*` symbol at f64.
 *
 * A NEW BINARY RATHER THAN MORE OF tests/test_template_fp.c, which stands at
 * 96 counted lines but carries three `.inc`s and runs the 229k-gate `fsqrt`
 * and 295k-gate `fmul` circuits; these families are cheap (the sign ops are
 * zero-Toffoli) and their subject is one tests/test_template_fp.c has no case
 * for: the LLVM-name -> soft_* map (PRD-v2 7.7), which is NOT identity.
 *
 * THE ORACLES ARE INDEPENDENT OF THE KERNELS AND OF THEIR `_eval` ROWS:
 *   - the sign ops and minNum/maxNum are tests/support/fmiscref.[ch], written
 *     from IEEE 754 §5.5.1 / §5.3.1 and upstream's DOCSTRING, never fmin.jl;
 *   - the rounding opcodes are the HOST's C99 functions, whose results IEEE
 *     754 fixes exactly on every non-NaN input (Annex F: the sign of a zero
 *     result included), after the in-process probe that the host rounds to
 *     nearest with no FTZ/DAZ. `round` is C's ties-AWAY and `rint`/`nearbyint`
 *     ties-to-EVEN, which is precisely the mapping under test.
 * A NaN INPUT IS NOT A ROUNDING ROW HERE: IEEE fixes that the result is a
 * quiet NaN and leaves the payload to the host (x86 and arm64 differ), so the
 * NaN cells are pinned by table in M38's own suite (tests/test_kernel_fround*),
 * not re-derived at the edge. The sign ops keep their NaN rows — negate, abs
 * and copySign are quiet bit operations on every input, NaNs included.
 */

#include "cq_shim.h"
#include "cq_shim_ctx.h"
#include "cq_shim_proof.h"
#include "cq_shim_record.h"

#include "cq_runtime_abi.h"

#include "bit.h"
#include "ctx.h"
#include "qubits.h"
#include "reg.h"
#include "sink.h"

#include "cqops/cqops.h"

#include "support/bitkinds.h"
#include "support/childcap.h"
#include "support/fmiscref.h"
#include "support/fpanchors.h"
#include "support/fphost.h"
#include "support/harness.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"
#include "support/refmodel.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* A `double` carrying a chosen BIT PATTERN, and back — memcpy, never a union
 * or a pointer cast (the strict-aliasing violation UBSan diagnoses). */
static double f64_of(uint64_t u)
{
    double d;
    memcpy(&d, &u, sizeof d);
    return d;
}

static uint64_t bits_of(double d)
{
    uint64_t u;
    memcpy(&u, &d, sizeof u);
    return u;
}

/* The host references, each a C99 function over the SAME pattern. */
static uint64_t h_floor(uint64_t a) { return bits_of(floor(f64_of(a))); }
static uint64_t h_ceil (uint64_t a) { return bits_of(ceil (f64_of(a))); }
static uint64_t h_trunc(uint64_t a) { return bits_of(trunc(f64_of(a))); }
static uint64_t h_round(uint64_t a) { return bits_of(round(f64_of(a))); }
static uint64_t h_rint (uint64_t a) { return bits_of(rint (f64_of(a))); }
static uint64_t h_nbint(uint64_t a) { return bits_of(nearbyint(f64_of(a))); }

#include "test_template_fmisc_rows.inc"
#include "test_template_fmisc_value.inc"
#include "test_template_fmisc_cert.inc"

CQ_TEST_MAIN(
    CQ_CASE(the_host_rounds_to_nearest_before_it_is_trusted_as_an_oracle),
    CQ_CASE(every_fp_unary_wrapper_agrees_with_an_independent_reference),
    CQ_CASE(the_tie_rows_separate_c_round_from_rint_and_nearbyint),
    CQ_CASE(every_fp_minmax_and_copysign_wrapper_agrees_with_an_independent_reference),
    CQ_CASE(copysign_takes_its_magnitude_from_the_first_lane_on_every_door),
    CQ_CASE(an_fneg_and_a_copysign_round_trip_on_wires_is_the_value_l2_and_the_pool),
    CQ_CASE(an_m38_unc_on_a_poisoned_source_is_discharged_by_the_certificate),
    CQ_CASE(a_rint_unc_does_not_pair_with_a_round_forward_on_the_same_handles),
    CQ_CASE(fneg_f64_inv_aborts_naming_itself_and_d14s_reason)
)
