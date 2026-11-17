/* NORTH_STAR "Done when" 3 (kmgy.23): one all-classical program that calls every core family and
 * every integer template family at every width, with classical operands only, emits no gate and
 * takes no qubit. Through the public entry points, as a lowered program would.
 *
 * Calls that must emit gates even on classical data, and are therefore left out of the zero-gate
 * part (checked at the end to emit, so the exclusion is real):
 *   - cqrt_ry_<W>[_controlled], cqrt_ry_<W>_qangle[_controlled], cqrt_rz_i1, cqrt_rz_qangle[_controlled]:
 *     the rotations are gates by definition (ABI:161-164, obligation 8(a)); "A classical-valued h gets
 *     its qubits here" (ABI:258-259); a classical angle rail appends Ry(P k/2^N) (ABI:304-305).
 *   - The quantum-only rows (an address rail or predicate with a qubit, INPUT merges) are not
 *     classical by definition and are not called here. */
#include <assert.h>
#include <stdio.h>
#include "cq_cast_pairs.h"
#include "cq_replay.h"
#include "cq_rt.h"
#include "cq_template.h"
#include "cqrt.h"

static uint8_t buf[64];                         /* host memory the classical bus accesses touch */
#define ADDR ((uint64_t)(uintptr_t)(buf + 8))

/* A forward template call, its _unc and the free (ABI:698: _unc then cqrt_free). */
#define TC(op, ...) do { uint32_t o_ = op(__VA_ARGS__); op##_unc(o_, __VA_ARGS__); cqrt_free(o_); } while (0)

/* Every template family at width n; L is the literal 5 as the width's parameter list. */
#define TEMPLATES(n, bits, L, A, B, C) do { \
    TC(cq_template_add_##n, A, B); TC(cq_template_sub_##n, A, B); TC(cq_template_add_##n##_hl, A, L); \
    TC(cq_template_sub_##n##_lh, L, B); \
    TC(cq_template_mul_##n, A, B); \
    TC(cq_template_udiv_##n, A, B); TC(cq_template_urem_##n, A, B); TC(cq_template_sdiv_##n, A, B); \
    TC(cq_template_srem_##n, A, B); TC(cq_template_udiv_##n##_hl, A, L); \
    TC(cq_template_and_##n, A, B); TC(cq_template_or_##n, A, B); TC(cq_template_xor_##n, A, B); \
    TC(cq_template_shl_##n, A, B); TC(cq_template_lshr_##n, A, B); TC(cq_template_ashr_##n, A, B); \
    TC(cq_template_shl_##n##_hl, A, L); TC(cq_template_fshl_##n, A, B, B); TC(cq_template_fshr_##n##_qql, A, B, L); \
    TC(cq_template_icmp_eq_##n, A, B); TC(cq_template_icmp_ne_##n, A, B); TC(cq_template_icmp_ugt_##n, A, B); \
    TC(cq_template_icmp_uge_##n, A, B); TC(cq_template_icmp_ult_##n, A, B); TC(cq_template_icmp_ule_##n, A, B); \
    TC(cq_template_icmp_sgt_##n, A, B); TC(cq_template_icmp_sge_##n, A, B); TC(cq_template_icmp_slt_##n, A, B); \
    TC(cq_template_icmp_sle_##n, A, B); TC(cq_template_icmp_slt_##n##_hl, A, L); \
    TC(cq_template_select_##n, C, A, B); TC(cq_template_select_##n##_hl, C, A, L); \
    TC(cq_template_uadd_overflow_##n, A, B); TC(cq_template_sadd_overflow_##n, A, B); \
    TC(cq_template_usub_overflow_##n, A, B); TC(cq_template_ssub_overflow_##n, A, B); \
    TC(cq_template_umul_overflow_##n, A, B); TC(cq_template_smul_overflow_##n, A, B); \
    TC(cq_template_uadd_sat_##n, A, B); TC(cq_template_sadd_sat_##n, A, B); \
    TC(cq_template_usub_sat_##n, A, B); TC(cq_template_ssub_sat_##n, A, B); \
    TC(cq_template_smax_##n, A, B); TC(cq_template_smin_##n, A, B); TC(cq_template_umax_##n, A, B); \
    TC(cq_template_umin_##n, A, B); TC(cq_template_abs_##n, A); \
    TC(cq_template_ctlz_##n, A); TC(cq_template_cttz_##n, A); TC(cq_template_ctpop_##n, A); \
    TC(cq_template_bitreverse_##n, A); \
} while (0)

/* Every core family at width n (INT and fp): alloc, copy, xorc, measure, the bus, the tape. */
#define CORE(n, LIT, A) do { \
    uint32_t h = A, d = cqrt_alloc_##n(LIT), one = cqrt_alloc_i1(1), zero = cqrt_alloc_i1(0); \
    cqrt_copy_##n(h, d); cqrt_copy_##n##_controlled(one, h, d); cqrt_xorc_##n(d, LIT); \
    cqrt_xorc_##n##_controlled(zero, d, LIT); (void)cqrt_measure_##n(d); \
    uint32_t m = cqrt_qram_alloc_i8(0), ar = cqrt_alloc_i64(ADDR); \
    cqrt_qram_store_##n##_hl(m, ADDR, h); cqrt_qram_store_##n(m, ar, h); \
    cqrt_qram_store_##n##_controlled(one, m, ar, h); cqrt_qram_store_##n##_hl_controlled(zero, m, ADDR, h); \
    uint32_t l1 = cqrt_qram_load_##n##_hl(m, ADDR), l2 = cqrt_qram_load_##n(m, ar); \
    uint32_t l3 = cqrt_qram_load_##n##_controlled(one, m, ar), l4 = cqrt_qram_load_##n##_hl_controlled(one, m, ADDR); \
    cqrt_qram_load_##n##_hl_unc(l1, m, ADDR); cqrt_qram_load_##n##_unc(l2, m, ar); \
    cqrt_qram_load_##n##_controlled_unc(l3, one, m, ar); cqrt_qram_load_##n##_hl_controlled_unc(l4, one, m, ADDR); \
    cqrt_free(l1); cqrt_free(l2); cqrt_free(l3); cqrt_free(l4); \
    cqrt_qram_swap_##n##_hl(m, ADDR, d); cqrt_qram_swap_##n(m, ar, d); \
    cqrt_qram_swap_##n##_controlled(one, m, ar, d); cqrt_qram_swap_##n##_hl_controlled(zero, m, ADDR, d); \
    uint32_t t = cqrt_tape_alloc(); \
    (void)cqrt_tape_write_##n(t, h); (void)cqrt_tape_write_##n##_controlled(one, t, h); \
    cqrt_tape_write_##n##_lit(t, LIT); \
    (void)cqrt_measure_##n(cqrt_tape_effect_##n##_controlled(zero, t, "abs", 1)); \
} while (0)

#define LN_(bits) (CQRT_UINT(bits))5
#define NARROW_(n, bits) do { \
    uint32_t a = cqrt_alloc_##n((CQRT_UINT(bits))0x9e3779b97f4a7c15ull), b = cqrt_alloc_##n((CQRT_UINT(bits))3), \
             c = cqrt_alloc_i1(1); \
    TEMPLATES(n, bits, LN_(bits), a, b, c); CORE(n, LN_(bits), a); \
} while (0);
#define LW_ 5, 0, 0, 0                          /* the literal 5 as four limbs */
#define WIDE_(n, bits) do { \
    uint32_t a = cqrt_alloc_##n(0x9e3779b97f4a7c15ull, 1, 2, 3), b = cqrt_alloc_##n(3, 0, 0, 0), c = cqrt_alloc_i1(0); \
    TEMPLATES(n, bits, LW_, a, b, c); CORE(n, LW_, a); \
} while (0);
#define FP_(n, bits, T) do { CORE(n, (T)1.5, cqrt_alloc_##n((T)-2)); } while (0);
#define BSWAP_(n, bits) do { uint32_t a = cqrt_alloc_##n((CQRT_UINT(bits))0x1234567890abcdefull); TC(cq_template_bswap_##n, a); } while (0);
/* Cast operands are classical rails made by cq_rail_alloc, which cqrt_alloc_<W> is, at any width. */
#define CAST_(f, fb, t, tb) do { \
    uint32_t a = (uint32_t)cq_rail_alloc(fb, (uint64_t[4]){ 0xa5a5a5a5a5a5a5a5ull, 1, 2, 3 }), \
             w = (uint32_t)cq_rail_alloc(tb, (uint64_t[4]){ 0xa5a5a5a5a5a5a5a5ull, 1, 2, 3 }); \
    TC(cq_template_zext_##f##_to_##t, a); TC(cq_template_sext_##f##_to_##t, a); TC(cq_template_trunc_##t##_to_##f, w); \
} while (0);

int main(void) {
    cq_replay_reset();
    uint32_t minted = cq_pool_minted();
    CQ_WIDTHS_NARROW(NARROW_)
    CQ_WIDTHS_WIDE(WIDE_)
    CQ_FP_WIDTHS(FP_)
    CQ_WIDTHS_X16(BSWAP_)
    CQ_CAST_PAIRS(CAST_)
    cqrt_invert_begin();                        /* a region of classical calls emits nothing either */
    { uint32_t a = cqrt_alloc_i32(7), b = cqrt_alloc_i32(9); TC(cq_template_mul_i32, a, b); cqrt_xorc_i32(a, 1); }
    cqrt_invert_end();
    cqrt_push(1); assert(cqrt_pop() == 1);
    cqrt_abort_i1(cqrt_alloc_i1(0));            /* a classical 0 flag: bookkeeping, returns */
    printf("all-classical: %llu gates, %u qubits touched, %u qubits taken\n",
           (unsigned long long)cq_replay_gates(), cq_replay_qubits(), cq_pool_minted() - minted);
    assert(cq_replay_gates() == 0 && cq_replay_qubits() == 0 && cq_pool_minted() == minted);

    /* The listed exception is real: a rotation on a classical rail emits. */
    cqrt_ry_i8(cqrt_alloc_i8(1), 0.5);
    assert(cq_replay_gates_of(CQ_G_RY) == 8);
    puts("abi_classical ok");
    return 0;
}
