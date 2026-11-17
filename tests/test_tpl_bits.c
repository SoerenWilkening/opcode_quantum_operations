/* ABI §2.8 bswap and bitreverse (kmgy.25): every public symbol at every width through the
 * sampled harness in both modes, the cost of a wire permutation asserted exactly, the
 * width lists checked, and the link check. Always asserts. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include "harness.h"
#include "cq_template.h"
#include "cqrt.h"
#include "cq_replay.h"

static const hx_val zero;

/* References on the limb value, written from the ABI text, not from the kernels. */
static uint64_t byte_at(const hx_val *v, uint32_t j) { return v->l[j / 8] >> (8 * (j % 8)) & 0xff; }
static hx_val bswap_ref(const hx_val *a, uint32_t w) {
    hx_val r = zero;
    for (uint32_t j = 0, n = w / 8; j < n; j++) r.l[j / 8] |= byte_at(a, n - 1 - j) << (8 * (j % 8));
    return r;
}
static hx_val bitreverse_ref(const hx_val *a, uint32_t w) {
    hx_val r = zero;
    for (uint32_t i = 0; i < w; i++)
        if (a->l[i / 64] >> (i % 64) & 1) r.l[(w - 1 - i) / 64] |= 1ull << ((w - 1 - i) % 64);
    return r;
}

typedef uint32_t (*fwd_fn)(uint32_t);
typedef void (*unc_fn)(uint32_t, uint32_t);

/* One symbol pair, both modes: classical takes no qubit and emits no gate; forced-quantum
 * forward is exactly w CX at depth 1 on 2w qubits, and _unc the same w CX. */
static void cost(fwd_fn fwd, unc_fn unc, uint32_t w) {
    const hx_val v = hx_mask((hx_val){ { 0x0123456789abcdefull, 0xfedcba9876543210ull, 0x5a5a5a5a5a5a5a5aull, 0xa5a5ull } }, w);
    for (int quantum = 1; quantum >= 0; quantum--) {
        cq_replay_reset();
        uint32_t base = cq_pool_live();
        int32_t a = hx_to_rail(quantum ? zero : v, w);
        cq_bit *ab = cq_rail_bits(a);
        for (uint32_t i = 0; quantum && i < w; i++) { cq_get_qubit(&ab[i]); cq_replay_set_bit(ab[i].q, (int)(v.l[i / 64] >> (i % 64) & 1)); }
        uint32_t r = fwd((uint32_t)a);
        if (quantum)
            assert(cq_replay_gates() == w && cq_replay_gates_of(CQ_G_CNOT) == w && cq_replay_depth() == 1 && cq_replay_qubits() == 2 * w);
        else
            assert(cq_replay_gates() == 0 && cq_pool_live() == base);
        unc(r, (uint32_t)a);
        assert(cq_replay_gates() == (quantum ? 2 * w : 0) && cq_replay_qubits() == (quantum ? 2 * w : 0));
        assert(hx_eq(hx_from_rail((int32_t)r), zero) && hx_eq(hx_from_rail(a), v));
        cqrt_free(r);
        cqrt_free((uint32_t)a);
        assert(cq_pool_live() == base);
    }
}

/* Harness op: forward into a fresh rail r, CX r into the harness's o, _unc r back to 0, free r.
 * Harness unc: the public _unc on o, so op then unc must give o back its start value. */
#define CHECK_(op, n, bits) \
    static void fwd_##op##_##n(int32_t o, const int32_t *a, uint32_t w) { \
        (void)w; uint32_t r = cq_template_##op##_##n((uint32_t)a[0]); \
        cqrt_copy_##n(r, (uint32_t)o); cq_template_##op##_##n##_unc(r, (uint32_t)a[0]); cqrt_free(r); } \
    static void unc_##op##_##n(int32_t o, const int32_t *a, uint32_t w) { (void)w; cq_template_##op##_##n##_unc((uint32_t)o, (uint32_t)a[0]); } \
    static void check_##op##_##n(void) { \
        const hx_op h = { "cq_template_" #op "_" #n, 1, 0, fwd_##op##_##n, unc_##op##_##n, op##_ref }; \
        assert(hx_run(&h, bits, 0, NULL) == HX_OK); \
        cost(cq_template_##op##_##n, cq_template_##op##_##n##_unc, bits); \
        printf("cq_template_%s_%s[_unc]: ok\n", #op, #n); }
#define BSWAP_(n, bits) CHECK_(bswap, n, bits)
#define BITREVERSE_(n, bits) CHECK_(bitreverse, n, bits)
CQ_WIDTHS_X16(BSWAP_)
CQ_WIDTHS(BITREVERSE_)

/* The link check: every generated name, forward and _unc. */
typedef void (*fn)(void);
#define ADDR_BSWAP_(n, bits) (fn)cq_template_bswap_##n, (fn)cq_template_bswap_##n##_unc,
#define ADDR_BITREVERSE_(n, bits) (fn)cq_template_bitreverse_##n, (fn)cq_template_bitreverse_##n##_unc,
static const fn linked[] = { CQ_WIDTHS_X16(ADDR_BSWAP_) CQ_WIDTHS(ADDR_BITREVERSE_) };

#define BITS_(n, bits) bits,
static const uint32_t all[] = { CQ_WIDTHS(BITS_) }, x16[] = { CQ_WIDTHS_X16(BITS_) };
#define NALL (sizeof all / sizeof *all)
#define NX16 (sizeof x16 / sizeof *x16)

int main(void) {
    /* CQ_WIDTHS_X16 is exactly the widths of CQ_WIDTHS that are a multiple of 16, in order. */
    unsigned k = 0;
    for (unsigned i = 0; i < NALL; i++)
        if (all[i] % 16 == 0) assert(k < NX16 && x16[k++] == all[i]);
    assert(k == NX16);
    assert(sizeof linked / sizeof *linked == 2 * (NALL + NX16));
    for (unsigned i = 0; i < sizeof linked / sizeof *linked; i++) assert(linked[i]);

#define RUN_BSWAP_(n, bits) check_bswap_##n();
#define RUN_BITREVERSE_(n, bits) check_bitreverse_##n();
    CQ_WIDTHS_X16(RUN_BSWAP_)
    CQ_WIDTHS(RUN_BITREVERSE_)
    printf("%zu template symbols linked and checked\n", sizeof linked / sizeof *linked);
    return 0;
}
