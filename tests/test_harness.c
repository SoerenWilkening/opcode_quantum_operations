/* The harness on trivial operations, and proof that it catches broken ones. Always asserts. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include "harness.h"
#include "cq_rt.h"
#include "cq_widths.h"

static void copy(int32_t o, const int32_t *a, uint32_t w) {
    cq_bit *ob = cq_rail_bits(o), *ab = cq_rail_bits(a[0]);
    for (uint32_t i = 0; i < w; i++) cq_cx(&ab[i], &ob[i]);
}
static hx_val copy_ref(const hx_val *a, uint32_t w) { (void)w; return a[0]; }

static void and2(int32_t o, const int32_t *a, uint32_t w) {
    cq_bit *ob = cq_rail_bits(o), *x = cq_rail_bits(a[0]), *y = cq_rail_bits(a[1]);
    for (uint32_t i = 0; i < w; i++) cq_ccx(&x[i], &y[i], &ob[i]);
}
static hx_val and_ref(const hx_val *a, uint32_t w) { (void)w; return hx_and(a[0], a[1]); }

/* i1 result: o ^= a & 1 (exercises out_width) */
static void lsb(int32_t o, const int32_t *a, uint32_t w) { (void)w; cq_cx(&cq_rail_bits(a[0])[0], &cq_rail_bits(o)[0]); }

/* ---- Broken operations the harness must catch. */
static void skip_top(int32_t o, const int32_t *a, uint32_t w) {
    cq_bit *ob = cq_rail_bits(o), *ab = cq_rail_bits(a[0]);
    for (uint32_t i = 0; i + 1 < w; i++) cq_cx(&ab[i], &ob[i]);
}
static void skip_bit150(int32_t o, const int32_t *a, uint32_t w) {   /* a plain copy below i151 */
    cq_bit *ob = cq_rail_bits(o), *ab = cq_rail_bits(a[0]);
    for (uint32_t i = 0; i < w; i++) if (i != 150) cq_cx(&ab[i], &ob[i]);
}
static void dirty_operand(int32_t o, const int32_t *a, uint32_t w) { copy(o, a, w); cq_x(&cq_rail_bits(a[0])[0]); }
static void leak(int32_t o, const int32_t *a, uint32_t w) {
    copy(o, a, w);
    cq_bit anc = cq_bit_const(0);
    cq_get_qubit(&anc);                       /* ancilla taken, never returned */
}
static void gate_always(int32_t o, const int32_t *a, uint32_t w) { copy(o, a, w); cq_ry(0.0, &cq_rail_bits(o)[0]); }
static void no_unc(int32_t o, const int32_t *a, uint32_t w) { (void)o; (void)a; (void)w; }

int main(void) {
    /* value helpers: i100 is 100 bits, not 128; i200 is 200, not 256 */
    hx_val ones = hx_ones(CQ_LIMBS * 64);
    assert(hx_eq(hx_mask(ones, 100), (hx_val){ { ~0ull, (1ull << 36) - 1 } }));
    assert(hx_eq(hx_mask(ones, 64), (hx_val){ { ~0ull } }) && hx_eq(hx_mask(ones, 7), (hx_val){ { 0x7f } }));
    assert(hx_eq(hx_mask(ones, 128), hx_of_u128(~(unsigned __int128)0)));
    assert(hx_eq(hx_ones(200), (hx_val){ { ~0ull, ~0ull, ~0ull, (1ull << 8) - 1 } }));
    assert(hx_u128(hx_of_u128((unsigned __int128)3 << 99)) == (unsigned __int128)3 << 99);
    int32_t h = hx_to_rail(ones, 100);
    assert(hx_eq(hx_from_rail(h), hx_mask(ones, 100)));
    cq_rail_free(h);
    /* bits above 128, through a classical rail and through a forced-quantum one */
    const hx_val big = { { 0x0123456789abcdefull, 0xfedcba9876543210ull, 0xa5a5a5a5deadbeefull, 0xc3ull } };
    h = hx_to_rail(big, 200);
    assert(hx_eq(hx_from_rail(h), big));
    cq_rail_free(h);
    h = hx_to_rail(big, 200);
    cq_bit *hb = cq_rail_bits(h);
    for (uint32_t i = 0; i < 200; i++) cq_ry(0.0, &hb[i]);   /* every bit a qubit, values kept */
    assert(cq_pool_live() == 200 && hx_eq(hx_from_rail(h), big));
    cq_rail_free(h);
    assert(hx_eq(hx_mask(big, 199), hx_xor(big, (hx_val){ { 0, 0, 0, 0x80 } })));   /* bit 199 set */

    const hx_op ops[] = {
        { "copy", 1, 0, copy, copy, copy_ref },
        { "and",  2, 0, and2, and2, and_ref },
        { "lsb",  1, 1, lsb,  lsb,  copy_ref },
    };
    for (unsigned k = 0; k < sizeof ops / sizeof ops[0]; k++) {
        hx_check_all(&ops[k]);
#define X(wn, bits) {                                                                    \
        hx_stats st;                                                                     \
        assert(hx_run(&ops[k], bits, 0, &st) == HX_OK);                                   \
        uint32_t per = k == 2 ? 1 : bits, nq = (uint32_t)ops[k].nargs + 1;               \
        assert(st.gates == per && st.depth == 1 && st.qubits == nq * per);               \
        assert(st.samples <= 1000 && st.samples >= (bits * ops[k].nargs <= 8 ? 2u : 1000u)); \
        printf("%-4s %-4s gates=%llu depth=%u qubits=%u samples=%u\n", ops[k].name, #wn, \
               (unsigned long long)st.gates, st.depth, st.qubits, st.samples); }
        CQ_WIDTHS(X)
#undef X
    }
    /* i200 by name, not only through the list: copy and and, forced-quantum and all-classical. */
    {
        hx_stats st;
        assert(hx_run(&ops[0], 200, 0, &st) == HX_OK && st.gates == 200 && st.qubits == 400 && st.samples == 1000);
        assert(hx_run(&ops[1], 200, 0, &st) == HX_OK && st.gates == 200 && st.qubits == 600 && st.samples == 1000);
    }

    /* Negative tests: each broken op is caught at every width, with the right reason. */
    struct { hx_op op; hx_fail want; } bad[] = {
        { { "skip_top",      1, 0, skip_top,      NULL,   copy_ref }, HX_WRONG_RESULT },
        { { "dirty_operand", 1, 0, dirty_operand, NULL,   copy_ref }, HX_OPERAND_CHANGED },
        { { "leak",          1, 0, leak,          NULL,   copy_ref }, HX_LEAK },
        { { "gate_always",   1, 0, gate_always,   NULL,   copy_ref }, HX_CLASSICAL_GATE },
        { { "no_unc",        1, 0, copy,          no_unc, copy_ref }, HX_UNC_RESULT },
    };
    fprintf(stderr, "---- expected failures follow\n");
    for (unsigned k = 0; k < sizeof bad / sizeof bad[0]; k++) {
#define X(name, bits) assert(hx_run(&bad[k].op, bits, HX_NOABORT, NULL) == bad[k].want);
        CQ_WIDTHS(X)
#undef X
    }
    /* At i200 by name: the top bit (199) is missed, and so is a bit in limb 2 below it. */
    assert(hx_run(&bad[0].op, 200, HX_NOABORT, NULL) == HX_WRONG_RESULT);
    const hx_op skip150 = { "skip_bit150", 1, 0, skip_bit150, NULL, copy_ref };
    assert(hx_run(&skip150, 128, HX_NOABORT, NULL) == HX_OK);
    assert(hx_run(&skip150, 200, HX_NOABORT, NULL) == HX_WRONG_RESULT);
    fprintf(stderr, "---- end of expected failures\n");
    return 0;
}
