/* Core §1 entry points (kmgy.6) through the sampled harness at every width, in
 * both modes, plus direct tests and the link check. Always asserts. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "harness.h"
#include "cqrt.h"
#include "cq_replay.h"

static const hx_val zero;

static hx_val ref_a0(const hx_val *a, uint32_t w) { (void)w; return a[0]; }
static hx_val ref_zero(const hx_val *a, uint32_t w) { (void)a; (void)w; return zero; }
static hx_val ref_one(const hx_val *a, uint32_t w) { (void)a; (void)w; return (hx_val){ { 1 } }; }
static hx_val ref_ctl(const hx_val *a, uint32_t w) { (void)w; return a[0].l[0] ? a[1] : zero; }
static hx_val ref_and(const hx_val *a, uint32_t w) { (void)w; return hx_and(a[0], a[1]); }
static hx_val ref_or(const hx_val *a, uint32_t w) { (void)w; return hx_xor(hx_xor(a[0], a[1]), hx_and(a[0], a[1])); }

static void force(uint32_t h) {
    cq_bit *b = cq_rail_bits((int32_t)h);
    for (uint32_t i = 0; i < cq_rail_width((int32_t)h); i++) cq_get_qubit(&b[i]);
}

/* Literals carry set bits above the width: the entry point must read only the low W bits. */
static unsigned __int128 junk(uint32_t bits) { return bits >= 128 ? 0 : ~(unsigned __int128)0 << bits; }
static uint64_t junk64(uint32_t bits) { return bits >= 64 ? 0 : ~0ull << bits; }
#define LIT_N(n, bits, v) ((CQRT_UINT(bits))(hx_u128(v) | junk(bits)))
#define LIT_W(n, bits, v) (v).l[0], (v).l[1], (v).l[2], (v).l[3] | junk64((bits) - 192)
#define LIT_F(n, bits, v) fp_##n(v)

/* A measured narrow value: i1 is 0/1, every other width sign-extended from W. */
static hx_val meas_n(__int128 r, uint32_t bits) {
    unsigned __int128 u = (unsigned __int128)r, low = u & ~junk(bits), want = low;
    if (bits > 1 && bits < 128 && (low >> (bits - 1)) & 1) want |= junk(bits);
    assert(u == want);
    return hx_of_u128(low);
}
static hx_val meas_w(cqrt_u256 r, uint32_t bits) {
    hx_val v = zero;
    memcpy(v.l, r.l, sizeof r.l);
    assert(hx_eq(v, hx_mask(v, bits)));   /* 0 above W */
    return v;
}
static hx_val meas_f(const void *r, uint32_t bits) { hx_val v = zero; memcpy(v.l, r, bits / 8); return v; }
#define MEAS_N(n, bits, r) meas_n((__int128)(r), bits)
#define MEAS_W(n, bits, r) meas_w(r, bits)
#define MEAS_F(n, bits, r) meas_f(&(r), bits)

/* copy_controlled(c, s, d), s forced to all ones, d classical 0, control classical 0,
 * classical 1, quantum 0, quantum 1. */
static void check_copy_controlled(void (*f)(uint32_t, uint32_t, uint32_t), uint32_t bits) {
    for (int mode = 0; mode < 4; mode++) {
        cq_replay_reset();
        uint32_t base = cq_pool_live();
        int32_t c = hx_to_rail((hx_val){ { (uint64_t)(mode & 1) } }, 1);
        int32_t s = hx_to_rail(hx_ones(bits), bits), d = hx_to_rail(zero, bits);
        force((uint32_t)s);
        if (mode >= 2) force((uint32_t)c);
        uint64_t g0 = cq_replay_gates();
        f((uint32_t)c, (uint32_t)s, (uint32_t)d);
        assert(cq_replay_gates() - g0 == (mode == 0 ? 0 : bits));
        assert(cq_replay_gates_of(CQ_G_CCX) == (mode >= 2 ? bits : 0));
        assert(hx_eq(hx_from_rail(d), mode & 1 ? hx_ones(bits) : zero));
        assert(hx_eq(hx_from_rail(s), hx_ones(bits)) && hx_from_rail(c).l[0] == (uint64_t)(mode & 1));
        cq_rail_free(c); cq_rail_free(s); cq_rail_free(d);
        assert(cq_pool_live() == base);
    }
}

#define OPS(n, bits, LIT, MEAS) \
static void copy_##n(int32_t o, const int32_t *a, uint32_t w) { (void)w; cqrt_copy_##n(a[0], o); } \
static void copyc_##n(int32_t o, const int32_t *a, uint32_t w) { (void)w; cqrt_copy_##n##_controlled(a[0], a[1], o); } \
static void xorc_##n(int32_t o, const int32_t *a, uint32_t w) { (void)a; (void)w; cqrt_xorc_##n(o, LIT(n, bits, hx_sample[0])); } \
static void xorcc_##n(int32_t o, const int32_t *a, uint32_t w) { \
    (void)w; cqrt_xorc_##n##_controlled(a[0], o, LIT(n, bits, hx_sample[1])); } \
static void alloc_##n(int32_t o, const int32_t *a, uint32_t w) { \
    (void)a; (void)w; uint32_t t = cqrt_alloc_##n(LIT(n, bits, hx_sample[0])); cqrt_copy_##n(t, o); cqrt_free(t); } \
static void measure_##n(int32_t o, const int32_t *a, uint32_t w) { \
    (void)w; __typeof__(cqrt_measure_##n(0)) r = cqrt_measure_##n(a[0]); cqrt_xorc_##n(o, LIT(n, bits, MEAS(n, bits, r))); } \
static void free_##n(int32_t o, const int32_t *a, uint32_t w) { \
    (void)o; (void)w; uint32_t t = cqrt_alloc_##n(LIT(n, bits, zero)); cqrt_copy_##n(a[0], t); cqrt_free(t); } \
static void check_##n(void) { \
    const hx_op ops[] = { \
        { "copy_" #n, 1, 0, copy_##n, copy_##n, ref_a0 }, \
        { "copy_" #n "_controlled", 2, 0, copyc_##n, copyc_##n, ref_ctl }, \
        { "xorc_" #n, 1, 0, xorc_##n, xorc_##n, ref_a0 }, \
        { "xorc_" #n "_controlled", 2, 0, xorcc_##n, xorcc_##n, ref_ctl }, \
        { "alloc_" #n, 1, 0, alloc_##n, NULL, ref_a0 }, \
        { "measure_" #n, 1, 0, measure_##n, NULL, ref_a0 }, \
        { "free_" #n, 1, 0, free_##n, NULL, ref_zero }, \
    }; \
    for (unsigned k = 0; k < sizeof ops / sizeof ops[0]; k++) /* ops 1 and 3: arg 0 is the control */ \
        assert(hx_run_w(&ops[k], bits, k == 1 || k == 3 ? (const uint32_t[]){ 1, 0 } : NULL, 0, NULL) == HX_OK); \
    /* alloc takes no qubit; measure of a classical rail emits no gate; of a quantum one, \
     * W m gates, qubits back to the pool, the rail kept holding the outcome; free returns qubits. */ \
    const hx_val ones = hx_ones(bits); \
    uint32_t base = cq_pool_live(); \
    cq_replay_reset(); \
    uint32_t h = cqrt_alloc_##n(LIT(n, bits, ones)); \
    assert(cq_pool_live() == base && cq_replay_gates() == 0); \
    __typeof__(cqrt_measure_##n(0)) r = cqrt_measure_##n(h); \
    assert(hx_eq(MEAS(n, bits, r), ones) && cq_replay_gates() == 0); \
    force(h); \
    assert(cq_pool_live() == base + bits); \
    r = cqrt_measure_##n(h); \
    assert(hx_eq(MEAS(n, bits, r), ones) && cq_pool_live() == base && cq_replay_gates_of(CQ_G_M) == bits); \
    r = cqrt_measure_##n(h); \
    assert(hx_eq(MEAS(n, bits, r), ones) && cq_replay_gates_of(CQ_G_M) == bits); \
    force(h); \
    cqrt_free(h); \
    assert(cq_pool_live() == base); \
    check_copy_controlled(cqrt_copy_##n##_controlled, bits); \
    /* xorc_controlled of all ones onto a quantum 0: control classical 0, 1, quantum 1. */ \
    for (int mode = 0; mode < 3; mode++) { \
        cq_replay_reset(); \
        uint32_t c = cqrt_alloc_i1(mode != 0), t = cqrt_alloc_##n(LIT(n, bits, zero)); \
        force(t); \
        if (mode == 2) force(c); \
        uint64_t g0 = cq_replay_gates(); \
        cqrt_xorc_##n##_controlled(c, t, LIT(n, bits, ones)); \
        assert(cq_replay_gates() - g0 == (mode ? bits : 0)); \
        assert(cq_replay_gates_of(CQ_G_CNOT) == (mode == 2 ? bits : 0)); \
        r = cqrt_measure_##n(t); \
        assert(hx_eq(MEAS(n, bits, r), mode ? ones : zero)); \
        cqrt_free(c); cqrt_free(t); \
        assert(cq_pool_live() == base); \
    } \
}

#define NARROW_OPS(n, bits) OPS(n, bits, LIT_N, MEAS_N)
#define WIDE_OPS(n, bits) OPS(n, bits, LIT_W, MEAS_W)
#define FP_OPS(n, bits, T) \
    static T fp_##n(hx_val v) { T x; memset(&x, 0, sizeof x); memcpy(&x, v.l, (bits) / 8); return x; } \
    OPS(n, bits, LIT_F, MEAS_F)
CQ_WIDTHS_NARROW(NARROW_OPS)
CQ_WIDTHS_WIDE(WIDE_OPS)
CQ_FP_WIDTHS(FP_OPS)

/* i1 logic, harness at width 1. Each is an involution: its unc is the same call. */
static void not_op(int32_t o, const int32_t *a, uint32_t w) { (void)a; (void)w; cqrt_not_i1(o); }
static void and_op(int32_t o, const int32_t *a, uint32_t w) { (void)w; cqrt_and_i1(a[0], a[1], o); }
static void or_op(int32_t o, const int32_t *a, uint32_t w) { (void)w; cqrt_or_i1(a[0], a[1], o); }
static void xor_op(int32_t o, const int32_t *a, uint32_t w) { (void)w; cqrt_xor_i1(a[0], o); }
/* One rail in both read-only slots (ABI §0, Aliasing). */
static void and_aa(int32_t o, const int32_t *a, uint32_t w) { (void)w; cqrt_and_i1(a[0], a[0], o); }
static void or_aa(int32_t o, const int32_t *a, uint32_t w) { (void)w; cqrt_or_i1(a[0], a[0], o); }

/* The link check: every in-scope §1 name of docs/ABI_SYMBOLS.md (rows 1-3, 7-16) at every
 * width is defined in libcq_runtime.a, the only archive besides the replay gates and the
 * harness this test links. */
typedef void (*fn)(void);
#define ADDR_(n, ...) (fn)cqrt_alloc_##n, (fn)cqrt_measure_##n, (fn)cqrt_copy_##n, \
    (fn)cqrt_copy_##n##_controlled, (fn)cqrt_xorc_##n, (fn)cqrt_xorc_##n##_controlled,
static const fn linked[] = { CQ_WIDTHS(ADDR_) CQ_FP_WIDTHS(ADDR_)
    (fn)cqrt_free, (fn)cqrt_not_i1, (fn)cqrt_and_i1, (fn)cqrt_or_i1, (fn)cqrt_xor_i1,
    (fn)cqrt_push, (fn)cqrt_pop };

int main(void) {
    unsigned nw = 0;
#define COUNT_(...) nw++;
    CQ_WIDTHS(COUNT_) CQ_FP_WIDTHS(COUNT_)
    assert(nw == 20 && sizeof linked / sizeof *linked == 6 * nw + 7);
    for (unsigned k = 0; k < sizeof linked / sizeof *linked; k++) assert(linked[k]);

    const hx_op i1ops[] = {
        { "not_i1", 0, 0, not_op, not_op, ref_one },
        { "and_i1", 2, 0, and_op, and_op, ref_and },
        { "or_i1",  2, 0, or_op,  or_op,  ref_or },
        { "xor_i1", 1, 0, xor_op, xor_op, ref_a0 },
        { "and_i1 aliased", 1, 0, and_aa, and_aa, ref_a0 },
        { "or_i1 aliased",  1, 0, or_aa,  or_aa,  ref_a0 },
    };
    for (unsigned k = 0; k < sizeof i1ops / sizeof i1ops[0]; k++) assert(hx_run(&i1ops[k], 1, 0, NULL) == HX_OK);

#define CHECK_(n, ...) check_##n(); printf("%-5s alloc measure free copy[_controlled] xorc[_controlled]: ok\n", #n);
    CQ_WIDTHS(CHECK_) CQ_FP_WIDTHS(CHECK_)

    /* push/pop: LIFO, nesting, growth, no gate. */
    cq_replay_reset();
    cqrt_push(1); cqrt_push(2);
    assert(cqrt_pop() == 2);
    cqrt_push(3); cqrt_push(UINT32_MAX);
    assert(cqrt_pop() == UINT32_MAX && cqrt_pop() == 3);
    for (uint32_t i = 0; i < 1000; i++) cqrt_push(i * 7);
    for (uint32_t i = 1000; i-- > 0;) assert(cqrt_pop() == i * 7);
    assert(cqrt_pop() == 1 && cq_replay_gates() == 0);
    printf("i1 logic, push/pop: ok; %zu symbols linked\n", sizeof linked / sizeof *linked);
    return 0;
}
