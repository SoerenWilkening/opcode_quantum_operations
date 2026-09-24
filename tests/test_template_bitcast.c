/* tests/test_template_bitcast.c — `bitcast f64 <-> i64` at the ABI edge
 * (bead s8g), both directions, through the GENERATED `cq_template_*` symbols.
 *
 * THE DOOR IS THE INTEGER CAST DOOR. `bitcast` is `opcode_table.yaml`'s
 * `reinterpret` family, which gen_bodies.entry_family already routes to
 * `cq_shim_cast`, and M13's cast kernels already accept `T == F` as an identity
 * copy (src/kernels/cast.c) — so the row lands as ONE more `cq_shim_cast_kind`
 * (CQ_SHIM_CAST_BITCAST) through `cq_kernel_zext` at `F == T == 64`: 64 CX, a
 * PHYSICAL copy (Rule 5 / I2), and on a classical rail a copied constant with
 * zero gates and zero qubits (L5). Not the conversion door `cq_shim_fcast`:
 * that one carries M37's VALUE conversions, and a bitcast is not a conversion.
 *
 * THE ORACLE IS memcpy OF THE BIT PATTERN, NEVER A HOST fp OPERATION. A bitcast
 * never quietens — an sNaN's pattern, a NaN's payload and a negative zero come
 * out exactly as they went in — so every value is compared as a 64-bit word.
 *
 * WHAT READS NO GOLDEN: the gate tuple is asserted against the closed form
 * `(0, popcount(mask), 0)` at every mask drawn — `(0, 64, 0)` at all-quantum —
 * and the stream is asserted LANE BY LANE (`CX(a_i, out_i)`), which is what
 * sees a permuted lane, a flipped one, and an alias (an `out` that shares `a`'s
 * qubits has no CX to emit at all).
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
#include "support/fpanchors.h"
#include "support/harness.h"
#include "support/kerneldrv.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"
#include "support/refmodel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Declared by hand: a wrong prototype is a `conflicting types` error, and the
 * compiler-checkable statement of the shapes is tests/abi/cq_templates_abi.txt,
 * which tests/test_gen_shim.py diffs. */
int32_t cq_template_bitcast_f64_to_i64(int32_t);
void    cq_template_bitcast_f64_to_i64_unc(int32_t, int32_t);
int32_t cq_template_bitcast_f64_to_i64_inv(int32_t);
int32_t cq_template_bitcast_i64_to_f64(int32_t);
void    cq_template_bitcast_i64_to_f64_unc(int32_t, int32_t);
int32_t cq_template_bitcast_i64_to_f64_inv(int32_t);
void    cq_template_fneg_f64_unc(int32_t, int32_t);

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

/* THE INDEPENDENT REFERENCE: C's own reinterpretation, memcpy in both
 * directions, never a union and never a pointer cast. */
static double f64_of(uint64_t u)  { double d;  memcpy(&d, &u, sizeof d); return d; }
static int64_t i64_of(uint64_t u) { int64_t i; memcpy(&i, &u, sizeof i); return i; }
static uint64_t ref_f2i(double d)  { int64_t i; memcpy(&i, &d, sizeof i);
                                     uint64_t u; memcpy(&u, &i, sizeof u); return u; }
static uint64_t ref_i2f(int64_t x) { double d; memcpy(&d, &x, sizeof d);
                                     uint64_t u; memcpy(&u, &d, sizeof u); return u; }

/* THE TWO DIRECTIONS AS ROWS. `src` mints a CLASSICAL rail through the ABI's
 * own allocator for that direction's source type, so it has a record history
 * like any rail CQ_lang makes. */
typedef struct {
    const char *name;
    int32_t   (*fwd)(int32_t);
    void      (*unc)(int32_t, int32_t);
    int32_t   (*src)(uint64_t);
    uint64_t  (*ref)(uint64_t);
} bc_row;

static int32_t src_f64(uint64_t v) { return cqrt_alloc_f64(f64_of(v)); }
static int32_t src_i64(uint64_t v) { return cqrt_alloc_i64(i64_of(v)); }
static uint64_t row_f2i(uint64_t v) { return ref_f2i(f64_of(v)); }
static uint64_t row_i2f(uint64_t v) { return ref_i2f(i64_of(v)); }

static const bc_row BC[2] = {
    { "bitcast_f64_to_i64", cq_template_bitcast_f64_to_i64,
      cq_template_bitcast_f64_to_i64_unc, src_f64, row_f2i },
    { "bitcast_i64_to_f64", cq_template_bitcast_i64_to_f64,
      cq_template_bitcast_i64_to_f64_unc, src_i64, row_i2f },
};

/* THE PATTERNS A QUIETENING OR SIGN-DROPPING BITCAST WOULD GET WRONG: signed
 * zeros, both infinities, x86's default NaN, quiet NaNs with payloads, three
 * SIGNALLING NaNs (smallest payload, largest payload, negative), subnormals,
 * the all-ones word, and the integer corners seen from the i64 side. */
static const uint64_t BC_VALS[] = {
    CQ_F64_POS_ZERO, CQ_F64_NEG_ZERO, CQ_F64_ONE, CQ_F64_NEG(CQ_F64_ONE),
    CQ_F64_POS_INF, CQ_F64_NEG_INF, CQ_F64_DEFAULT_NAN,
    CQ_F64_QNAN_A, CQ_F64_QNAN_B, CQ_F64_SNAN, 0x7ff7ffffffffffffull,
    0xfff0000000000001ull, CQ_F64_MIN_SUBNORMAL, CQ_F64_MAX_SUBNORMAL,
    CQ_F64_MAX, 0xffffffffffffffffull, 0x7fffffffffffffffull,
    0x8000000000000001ull, 0x5555555555555555ull, 0xaaaaaaaaaaaaaaaaull,
};
#define BC_NV (sizeof BC_VALS / sizeof BC_VALS[0])

/* The ORACLE is checked against itself once, on the rows it exists for, so a
 * reference that went through a host `double` and quietened would be seen
 * here rather than trusted: memcpy through a double must hand the sNaN back
 * bit for bit. */
CQ_TEST(the_memcpy_reference_is_bit_exact_on_nans_and_signed_zero)
{
    for (size_t i = 0; i < BC_NV; i++) {
        CHECK_EQ(row_f2i(BC_VALS[i]), BC_VALS[i]);
        CHECK_EQ(row_i2f(BC_VALS[i]), BC_VALS[i]);
    }
}

/* L1 AND L5 ON CLASSICAL RAILS: every pattern through both generated symbols,
 * zero gates, zero qubits (the pool snapshot is unchanged), the source
 * unchanged, the result 64 bits wide, and each `_unc` zeroing its forward. */
CQ_TEST(every_bitcast_wrapper_is_a_bit_exact_copy_on_classical_rails)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const cq_pc_snap before = cq_pc_take(ctx);

    for (size_t r = 0; r < 2; r++)
        for (size_t i = 0; i < BC_NV; i++) {
            const uint64_t v = BC_VALS[i];
            const int32_t a = BC[r].src(v);
            int32_t out;

            CHECK_EQ(cq_pc_value(ctx, a), v);       /* the rail holds v exactly */
            out = BC[r].fwd(a);
            CHECK_EQ(cq_reg_width(&ctx->regs, out), 64u);
            if (cq_pc_value(ctx, out) != BC[r].ref(v))
                cq_h_fail(__FILE__, __LINE__, "%s(%016llx) = %016llx, memcpy "
                          "says %016llx", BC[r].name, (unsigned long long)v,
                          (unsigned long long)cq_pc_value(ctx, out),
                          (unsigned long long)BC[r].ref(v));
            CHECK_EQ(cq_pc_value(ctx, a), v);
            BC[r].unc(out, a);
            CHECK_EQ(cq_pc_value(ctx, out), 0u);
        }
    CHECK_EQ(cq_mock_count(&m), 0);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    cq_reg_audit(ctx);
    close_with(&m);
}

/* The stream of ONE call, asserted lane by lane: exactly one CX per quantum
 * lane of `a`, in lane order, from `a`'s qubit to `out`'s qubit at the SAME
 * lane — and every classical lane of `a` is the same constant in `out`. */
static void bc_stream_is_the_lane_copy(cq_ctx *ctx, const cq_mock *m,
                                       size_t base, int32_t a, int32_t out)
{
    const cq_bit *ab = cq_reg_cbits(&ctx->regs, a);
    const cq_bit *ob = cq_reg_cbits(&ctx->regs, out);
    size_t k = base;

    for (uint32_t i = 0; i < 64u; i++) {
        if (!cq_bit_is_qubit(ab[i])) {
            CHECK(cq_bit_is_const(ob[i]));
            CHECK_EQ(cq_bit_value(ob[i]), cq_bit_value(ab[i]));
            continue;
        }
        CHECK(cq_bit_is_qubit(ob[i]));
        if (!cq_bit_is_qubit(ob[i]) || k >= cq_mock_count(m)) {
            cq_h_fail(__FILE__, __LINE__, "lane %u of a is a qubit and the "
                      "stream has no CX for it", i);
            return;
        }
        CHECK(cq_bit_qindex(ob[i]) != cq_bit_qindex(ab[i]));   /* not an alias */
        CHECK_EQ((int)cq_mock_at(m, k)->op, (int)CQ_OP_CX);
        CHECK_EQ(cq_mock_at(m, k)->a, cq_bit_qindex(ab[i]));
        CHECK_EQ(cq_mock_at(m, k)->t, cq_bit_qindex(ob[i]));
        k++;
    }
    CHECK_EQ(k, cq_mock_count(m));
}

#include "test_template_bitcast_wires.inc"
#include "test_template_bitcast_cert.inc"

CQ_TEST_MAIN(
    CQ_CASE(the_memcpy_reference_is_bit_exact_on_nans_and_signed_zero),
    CQ_CASE(every_bitcast_wrapper_is_a_bit_exact_copy_on_classical_rails),
    CQ_CASE(every_bitcast_wrapper_is_a_lane_copy_at_every_mask_drawn),
    CQ_CASE(the_all_quantum_tuple_is_0_64_0_forward_and_unc_in_both_directions),
    CQ_CASE(a_bitcast_round_trip_on_wires_is_the_value_l2_and_the_pool),
    CQ_CASE(a_bitcast_records_one_write_naming_a_under_its_cast_tag),
    CQ_CASE(a_bitcast_unc_on_a_poisoned_source_is_discharged_by_the_certificate),
    CQ_CASE(the_two_directions_pair_because_they_are_one_function),
    CQ_CASE(a_bitcast_forward_does_not_pair_with_an_fneg_unc),
    CQ_CASE(both_bitcast_inv_symbols_abort_naming_themselves_and_d14s_reason)
)
