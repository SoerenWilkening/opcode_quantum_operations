/* tests/test_template_intr.c — the INTEGER intrinsics at the ABI edge (bead
 * 9ve.44): M41's ctpop/ctlz/cttz/bitreverse/bswap and M42's abs through the
 * new `cq_shim_iun` door, and M42's smin/smax/umin/umax through the integer
 * binary door, every one reached through its GENERATED `cq_template_*`
 * symbol — plus the rows of intrinsic_table.yaml that still abort.
 *
 * A NEW BINARY RATHER THAN A FOURTH SUBJECT IN tests/test_template.c, which
 * stands at 279 counted lines with three `.inc`s on it. The discriminator is
 * the one the shim draws: these rows come from a DIFFERENT YAML, reach a
 * NINTH tag space (`0x60000000`) and a widened `cq_shim_op`, and every case
 * here turns on one of those.
 *
 * THE ORACLE IS INDEPENDENT OF THE KERNELS: plain C over packed `uint64_t`
 * written from LLVM's langref definitions (Rule 3 licenses the packed scalar in
 * a test REFERENCE, W <= 64), sharing no row, constant or loop with
 * src/kernels/ibits.c or iminmax.c. What it judges is the DISPATCH — that
 * `ctlz` reaches `cq_kernel_ctlz` and not its mirror, that `smin` is signed —
 * which a table written by index gets wrong and nothing structural sees.
 *
 * THE CERTIFICATE CASES MINT THROUGH THE ABI (`cqrt_alloc_i<W>` + a general
 * `cqrt_ry_i<W>`), never through `cq_bk_reg`: a test-minted rail has no
 * record history and D15's operand-stability check SKIPS it (bd memory
 * a-test-minted-rail-has-no-certificate-history).
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
#include "support/harness.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"
#include "support/refmodel.h"

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

#include "test_template_intr_rows.inc"

/* A CLASSICAL rail through the ABI's own allocator at width W, so it has a
 * record history like any rail CQ_lang makes. */
static int32_t alloc_iw(int W, uint64_t v)
{
    switch (W) {
    case 8:  return cqrt_alloc_i8((int8_t)v);
    case 16: return cqrt_alloc_i16((int16_t)v);
    case 32: return cqrt_alloc_i32((int32_t)v);
    default: return cqrt_alloc_i64((int64_t)v);
    }
}

/* --- the reference, from LLVM's langref ----------------------------------- */

static int64_t sx(uint64_t v, int W)
{
    const uint64_t m = cq_ref_mask(W);
    v &= m;
    return (W < 64 && (v >> (W - 1)) & 1u) ? (int64_t)(v | ~m) : (int64_t)v;
}

static uint64_t ref_un(int op, uint64_t v, int W)
{
    uint64_t r = 0u;
    int i;

    v &= cq_ref_mask(W);
    switch (op) {
    case IU_CTPOP:
        for (i = 0; i < W; i++) r += (v >> i) & 1u;
        return r;
    case IU_CTLZ:           /* W at zero: the total function (K26 D-K26-4) */
        for (i = W - 1; i >= 0 && !((v >> i) & 1u); i--) r++;
        return r;
    case IU_CTTZ:
        for (i = 0; i < W && !((v >> i) & 1u); i++) r++;
        return r;
    case IU_BITREVERSE:
        for (i = 0; i < W; i++) r |= ((v >> i) & 1u) << (W - 1 - i);
        return r;
    case IU_BSWAP:
        for (i = 0; i < W / 8; i++)
            r |= ((v >> (8 * i)) & 0xFFu) << (8 * (W / 8 - 1 - i));
        return r;
    default:                /* abs: INT_MIN maps to itself (the sub wraps) */
        return sx(v, W) < 0 ? (~v + 1u) & cq_ref_mask(W) : v;
    }
}

static uint64_t ref_mm(int op, uint64_t a, uint64_t b, int W)
{
    const uint64_t m = cq_ref_mask(W);
    a &= m; b &= m;
    switch (op) {
    case MM_SMIN: return sx(a, W) <= sx(b, W) ? a : b;
    case MM_SMAX: return sx(a, W) >= sx(b, W) ? a : b;
    case MM_UMIN: return a <= b ? a : b;
    default:      return a >= b ? a : b;
    }
}

/* Operands that separate the rows from their neighbours: 0 (ctlz/cttz = W),
 * a lone TOP bit (INT_MIN for abs; signed vs unsigned for every min/max), a
 * lone bottom bit (ctlz != cttz), all-ones (-1), and two asymmetric patterns
 * whose byte and bit reversals differ from themselves and from each other. */
static const uint64_t IU_VALS[] = {
    0x0000000000000000ull, 0x8000000000000000ull, 0x0000000000000001ull,
    0xFFFFFFFFFFFFFFFFull, 0x0123456789ABCDEFull, 0x00F0000000000C80ull
};

#define IU_NV (sizeof IU_VALS / sizeof IU_VALS[0])

/* The top bit AT width W, since IU_VALS[1] is 0 below 64 once masked. */
static uint64_t val_at(size_t i, int W)
{
    return i == 1u ? (uint64_t)1u << (W - 1) : IU_VALS[i] & cq_ref_mask(W);
}

#include "test_template_intr_value.inc"
#include "test_template_intr_cert.inc"

CQ_TEST_MAIN(
    CQ_CASE(every_integer_unary_wrapper_agrees_with_an_independent_reference),
    CQ_CASE(every_min_max_wrapper_agrees_with_an_independent_reference),
    CQ_CASE(an_integer_unary_round_trip_on_wires_is_the_value_l2_and_the_pool),
    CQ_CASE(a_min_max_round_trip_on_wires_is_the_value_l2_and_the_pool),
    CQ_CASE(an_integer_unary_call_records_one_write_naming_a_under_its_tag),
    CQ_CASE(an_integer_unary_unc_on_a_poisoned_source_is_discharged_by_the_certificate),
    CQ_CASE(a_min_max_hl_unc_on_a_poisoned_source_is_discharged_by_the_certificate),
    CQ_CASE(a_ctpop_unc_does_not_pair_with_an_add_hl_forward_on_the_same_handles),
    CQ_CASE(an_smin_unc_does_not_pair_with_a_umin_forward_on_the_same_handles),
    CQ_CASE(every_still_aborting_intrinsic_row_names_itself_and_its_own_reason)
)
