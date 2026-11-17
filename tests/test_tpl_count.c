/* ABI §2.8 ctpop, ctlz, cttz (kmgy.16): every public symbol at every width through the sampled
 * harness in both modes with ancillae checked back to |0>, pinned edge values, cq_invert, the
 * link check. Always asserts. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include "harness.h"
#include "cq_template.h"
#include "cqrt.h"
#include "cq_replay.h"

static const hx_val zero;

/* References on the limb value, from the ABI text: the count has width W, ctlz(0) = cttz(0) = W (CR §2). */
static int bitv(const hx_val *v, uint32_t i) { return (int)(v->l[i / 64] >> (i % 64) & 1); }
static hx_val ctpop_ref(const hx_val *a, uint32_t w) {
    uint64_t c = 0;
    for (uint32_t i = 0; i < w; i++) c += (uint64_t)bitv(a, i);
    return (hx_val){ { c } };
}
static hx_val ctlz_ref(const hx_val *a, uint32_t w) {
    uint32_t i = w;
    while (i && !bitv(a, i - 1)) i--;                  /* i: one above the highest set bit, 0 if none */
    return (hx_val){ { w - i } };
}
static hx_val cttz_ref(const hx_val *a, uint32_t w) {
    uint32_t i = 0;
    while (i < w && !bitv(a, i)) i++;
    return (hx_val){ { i } };
}
static hx_val ref_zero(const hx_val *a, uint32_t w) { (void)a; (void)w; return zero; }

/* Every qubit in the pool's free list reads 0: the kernel's ancillae came back clean. */
static void clean(void) {
    uint32_t n = cq_pool_minted() - cq_pool_live(), q[8192];
    assert(n <= 8192);
    for (uint32_t i = 0; i < n; i++) { q[i] = cq_pool_take(); assert(!cq_replay_get_bit(q[i])); }
    while (n) cq_pool_give(q[--n]);
}

/* A rail of w bits holding v, forced to qubits when quantum. */
static uint32_t rail(hx_val v, uint32_t w, int quantum) {
    int32_t r = cq_rail_alloc(w, v.l);
    cq_bit *b = cq_rail_bits(r);
    for (uint32_t i = 0; quantum && i < w; i++) { cq_get_qubit(&b[i]); cq_replay_set_bit(b[i].q, bitv(&v, i)); }
    return (uint32_t)r;
}

typedef uint32_t (*fwd_fn)(uint32_t);
typedef void (*unc_fn)(uint32_t, uint32_t);

/* Edge values, both modes: 0, 1, all ones, only the top bit, a single bit at each limb boundary
 * (63, 64, 127, 128, 191, 192 where below w). Forward against ref; classical: no gate, no qubit;
 * _unc clears; operand unchanged; ancillae clean; nothing leaks. */
static void pinned(fwd_fn fwd, unc_fn unc, hx_ref_fn ref, uint32_t w) {
    static const uint32_t at[] = { 63, 64, 127, 128, 191, 192 };
    hx_val v[4 + sizeof at / sizeof *at] = { zero, { { 1 } }, hx_ones(w), zero };
    unsigned nv = 4;
    v[3].l[(w - 1) / 64] = 1ull << ((w - 1) % 64);
    for (unsigned k = 0; k < sizeof at / sizeof *at; k++)
        if (at[k] < w) { v[nv] = zero; v[nv++].l[at[k] / 64] = 1ull << (at[k] % 64); }
    for (unsigned k = 0; k < nv; k++)
        for (int quantum = 1; quantum >= 0; quantum--) {
            cq_replay_reset();
            uint32_t base = cq_pool_live(), a = rail(v[k], w, quantum);
            uint32_t r = fwd(a);
            if (!quantum) assert(cq_replay_gates() == 0 && cq_pool_live() == base);
            clean();
            assert(hx_eq(hx_from_rail((int32_t)r), ref(&v[k], w)));
            uint32_t s = fwd(a);
            unc(s, a);
            clean();
            assert(hx_eq(hx_from_rail((int32_t)s), zero) && hx_eq(hx_from_rail((int32_t)a), v[k]));
            if (!quantum) assert(cq_replay_gates() == 0);
            cqrt_free(r); cqrt_free(s); cqrt_free(a);
            assert(cq_pool_live() == base);
        }
}

/* Samples per symbol and width (edge values first): 200 up to i64, 40 to i128, 20 at i200. */
#define SAMPLES(bits) ((bits) <= 64 ? 200 : (bits) <= 128 ? 40 : 20)

/* Harness op: forward into a fresh r, CX r into o, _unc r, free r. Harness unc: the public _unc on o.
 * Then the pinned values, and cq_invert: X; invert{X} and invert{X}; X leave o as it was. */
#define CHECK_(op, n, bits) \
    static void fwd_##op##_##n(int32_t o, const int32_t *a, uint32_t w) { \
        (void)w; uint32_t r = cq_template_##op##_##n((uint32_t)a[0]); clean(); \
        cqrt_copy_##n(r, (uint32_t)o); cq_template_##op##_##n##_unc(r, (uint32_t)a[0]); cqrt_free(r); clean(); } \
    static void unc_##op##_##n(int32_t o, const int32_t *a, uint32_t w) { (void)w; cq_template_##op##_##n##_unc((uint32_t)o, (uint32_t)a[0]); } \
    static void fi_##op##_##n(int32_t o, const int32_t *a, uint32_t w) { \
        (void)w; unc_##op##_##n(o, a, w); cqrt_invert_begin(); unc_##op##_##n(o, a, w); cqrt_invert_end(); clean(); } \
    static void if_##op##_##n(int32_t o, const int32_t *a, uint32_t w) { \
        (void)w; cqrt_invert_begin(); unc_##op##_##n(o, a, w); cqrt_invert_end(); unc_##op##_##n(o, a, w); clean(); } \
    static void check_##op##_##n(void) { \
        const hx_op h = { "cq_template_" #op "_" #n, 1, 0, fwd_##op##_##n, unc_##op##_##n, op##_ref }; \
        const hx_op inv = { "invert cq_template_" #op "_" #n, 1, 0, fi_##op##_##n, if_##op##_##n, ref_zero }; \
        hx_max_samples = SAMPLES(bits); \
        assert(hx_run(&h, bits, 0, NULL) == HX_OK); \
        hx_max_samples = 10; \
        assert(hx_run(&inv, bits, 0, NULL) == HX_OK); \
        hx_max_samples = 0; \
        pinned(cq_template_##op##_##n, cq_template_##op##_##n##_unc, op##_ref, bits); }
#define OPS_(n, bits) CHECK_(ctpop, n, bits) CHECK_(ctlz, n, bits) CHECK_(cttz, n, bits)
CQ_WIDTHS(OPS_)

/* The link check: every generated name, forward and _unc. */
typedef void (*fn)(void);
#define ADDR1_(op, n) (fn)cq_template_##op##_##n, (fn)cq_template_##op##_##n##_unc,
#define ADDR_(n, bits) ADDR1_(ctpop, n) ADDR1_(ctlz, n) ADDR1_(cttz, n)
static const fn linked[] = { CQ_WIDTHS(ADDR_) };
#define ONE_(n, bits) +1

int main(void) {
    assert(sizeof linked / sizeof *linked == 3 * 2 * (0 CQ_WIDTHS(ONE_)));
    for (unsigned i = 0; i < sizeof linked / sizeof *linked; i++) assert(linked[i]);

    /* A few values by hand, independent of the references: i8 0x10 has 1 set bit, 3 leading and 4 trailing
     * zeros; i1: ctlz(0) = cttz(0) = 1, ctpop(1) = 1. */
    uint32_t a = cqrt_alloc_i8(0x10), z = cqrt_alloc_i1(0), b = cqrt_alloc_i1(1);
    uint32_t p = cq_template_ctpop_i8(a), l = cq_template_ctlz_i8(a), t = cq_template_cttz_i8(a);
    uint32_t lz = cq_template_ctlz_i1(z), tz = cq_template_cttz_i1(z), pb = cq_template_ctpop_i1(b);
    assert(cqrt_measure_i8(p) == 1 && cqrt_measure_i8(l) == 3 && cqrt_measure_i8(t) == 4);
    assert(cqrt_measure_i1(lz) == 1 && cqrt_measure_i1(tz) == 1 && cqrt_measure_i1(pb) == 1);
    cqrt_free(p); cqrt_free(l); cqrt_free(t); cqrt_free(lz); cqrt_free(tz); cqrt_free(pb);
    cqrt_free(a); cqrt_free(z); cqrt_free(b);

#define RUN_(n, bits) check_ctpop_##n(); check_ctlz_##n(); check_cttz_##n(); printf("ctpop ctlz cttz %s: ok\n", #n); fflush(stdout);
    CQ_WIDTHS(RUN_)
    printf("%zu template symbols linked and checked\n", sizeof linked / sizeof *linked);
    return 0;
}
