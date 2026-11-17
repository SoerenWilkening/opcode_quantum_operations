/* ABI §2.6 zext, sext, trunc (kmgy.15): every pair of cq_cast_pairs.h through the sampled
 * harness (200 seeded samples per pair and op, edge values first, both modes, forward and _unc), exact cost
 * of zext/trunc, sext depth against the chain bound, the pair list against CQ_WIDTHS, and
 * the link check. `--count` prints the number of symbols that must exist (for the nm test). */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "harness.h"
#include "cq_template.h"
#include "cqrt.h"
#include "cq_replay.h"

static const hx_val zero;
static uint32_t from_bits;   /* the operand width of the pair under test, for the references */

/* References on the limb value, written from LLVM's definitions, bit by bit. */
static int bit(const hx_val *v, uint32_t i) { return (int)(v->l[i / 64] >> (i % 64) & 1); }
static hx_val ext_ref(const hx_val *a, uint32_t w, int sign) {
    hx_val r = zero;
    for (uint32_t i = 0; i < w; i++)
        if (i < from_bits ? bit(a, i) : sign && bit(a, from_bits - 1)) r.l[i / 64] |= 1ull << (i % 64);
    return r;
}
static hx_val zext_ref(const hx_val *a, uint32_t w) { return ext_ref(a, w, 0); }
static hx_val sext_ref(const hx_val *a, uint32_t w) { return ext_ref(a, w, 1); }
/* trunc: the same bit loop reads only i < w bits because the harness hands w = TO < from_bits. */
static hx_val trunc_ref(const hx_val *a, uint32_t w) {
    hx_val r = zero;
    for (uint32_t i = 0; i < w; i++) if (bit(a, i)) r.l[i / 64] |= 1ull << (i % 64);
    return r;
}

typedef uint32_t (*fwd_fn)(uint32_t);
typedef void (*unc_fn)(uint32_t, uint32_t);
enum { ZEXT, SEXT, TRUNC };

/* Cost through the public symbols: zext/trunc exactly `copied` CX at depth 1, sext at most the chain depth;
 * classical: no gate, no qubit. */
static void cost(int kind, fwd_fn fwd, unc_fn unc, uint32_t from, uint32_t to) {
    const hx_val v = hx_mask((hx_val){ { 0x8123456789abcdefull, 0xfedcba9876543210ull, 0x5a5a5a5a5a5a5a5aull, 0xa5a5ull } }, from);
    uint32_t copied = from < to ? from : to;
    for (int quantum = 1; quantum >= 0; quantum--) {
        cq_replay_reset();
        uint32_t base = cq_pool_live();
        int32_t a = hx_to_rail(quantum ? zero : v, from);
        cq_bit *ab = cq_rail_bits(a);
        for (uint32_t i = 0; quantum && i < from; i++) { cq_get_qubit(&ab[i]); cq_replay_set_bit(ab[i].q, bit(&v, i)); }
        uint32_t r = fwd((uint32_t)a);
        if (quantum && kind != SEXT)
            assert(cq_replay_gates() == copied && cq_replay_depth() == 1 && cq_replay_qubits() == 2 * copied);
        else if (quantum)   /* the chain is the bound: the shipped kernel is never deeper */
            assert(cq_replay_depth() <= to - from + 1 && cq_replay_gates() >= to);
        else
            assert(cq_replay_gates() == 0 && cq_replay_qubits() == 0 && cq_pool_live() == base);
        unc(r, (uint32_t)a);
        assert(hx_eq(hx_from_rail((int32_t)r), zero) && hx_eq(hx_from_rail(a), v));
        cqrt_free(r);
        cqrt_free((uint32_t)a);
        assert(cq_pool_live() == base);
    }
}

#define CHECK_(op, kind, f, fb, t, tb) \
    static void fwd_##op##_##f##_to_##t(int32_t o, const int32_t *a, uint32_t w) { \
        (void)w; uint32_t r = cq_template_##op##_##f##_to_##t((uint32_t)a[0]); \
        cqrt_copy_##t(r, (uint32_t)o); cq_template_##op##_##f##_to_##t##_unc(r, (uint32_t)a[0]); cqrt_free(r); } \
    static void unc_##op##_##f##_to_##t(int32_t o, const int32_t *a, uint32_t w) { (void)w; cq_template_##op##_##f##_to_##t##_unc((uint32_t)o, (uint32_t)a[0]); } \
    static void check_##op##_##f##_to_##t(void) { \
        const hx_op h = { "cq_template_" #op "_" #f "_to_" #t, 1, 0, fwd_##op##_##f##_to_##t, unc_##op##_##f##_to_##t, op##_ref }; \
        const uint32_t aw[HX_MAX_ARGS] = { fb }; \
        from_bits = fb; \
        assert(hx_run_w(&h, tb, aw, 0, NULL) == HX_OK); \
        cost(kind, cq_template_##op##_##f##_to_##t, cq_template_##op##_##f##_to_##t##_unc, fb, tb); }
/* trunc_<big>_to_<small>: the harness result width is the small one. */
#define ZEXT_(f, fb, t, tb) CHECK_(zext, ZEXT, f, fb, t, tb)
#define SEXT_(f, fb, t, tb) CHECK_(sext, SEXT, f, fb, t, tb)
#define TRUNC_(f, fb, t, tb) CHECK_(trunc, TRUNC, t, tb, f, fb)
CQ_CAST_PAIRS(ZEXT_)
CQ_CAST_PAIRS(SEXT_)
CQ_CAST_PAIRS(TRUNC_)

typedef void (*fn)(void);
#define ADDR_(op, f, t) (fn)cq_template_##op##_##f##_to_##t, (fn)cq_template_##op##_##f##_to_##t##_unc,
#define ADDR_ZEXT_(f, fb, t, tb) ADDR_(zext, f, t)
#define ADDR_SEXT_(f, fb, t, tb) ADDR_(sext, f, t)
#define ADDR_TRUNC_(f, fb, t, tb) ADDR_(trunc, t, f)
static const fn linked[] = { CQ_CAST_PAIRS(ADDR_ZEXT_) CQ_CAST_PAIRS(ADDR_SEXT_) CQ_CAST_PAIRS(ADDR_TRUNC_) };

#define BITS_(n, bits) bits,
static const uint32_t all[] = { CQ_WIDTHS(BITS_) };
#define NALL (sizeof all / sizeof *all)
#define PAIR_(f, fb, t, tb) { fb, tb },
static const uint32_t pairs[][2] = { CQ_CAST_PAIRS(PAIR_) };

int main(int argc, char **argv) {
    hx_max_samples = 200;   /* 315 (op, pair) cases: 200 samples each keeps the run in seconds */
    if (argc > 1 && !strcmp(argv[1], "--count")) { printf("%zu\n", sizeof linked / sizeof *linked); return 0; }
    /* The pair list is exactly every i < j of CQ_WIDTHS, in order. */
    unsigned k = 0;
    for (unsigned i = 0; i < NALL; i++)
        for (unsigned j = i + 1; j < NALL; j++, k++) {
            if (k >= sizeof pairs / sizeof *pairs || pairs[k][0] != all[i] || pairs[k][1] != all[j]) {
                fprintf(stderr, "include/cq_cast_pairs.h disagrees with CQ_WIDTHS at pair i%u -> i%u: regenerate it\n", all[i], all[j]);
                return 1;
            }
        }
    assert(k == sizeof pairs / sizeof *pairs && sizeof linked / sizeof *linked == 6 * k);
    for (unsigned i = 0; i < sizeof linked / sizeof *linked; i++) assert(linked[i]);

#define RUN_ZEXT_(f, fb, t, tb) check_zext_##f##_to_##t();
#define RUN_SEXT_(f, fb, t, tb) check_sext_##f##_to_##t();
#define RUN_TRUNC_(f, fb, t, tb) check_trunc_##t##_to_##f();
    CQ_CAST_PAIRS(RUN_ZEXT_)
    CQ_CAST_PAIRS(RUN_SEXT_)
    CQ_CAST_PAIRS(RUN_TRUNC_)
    printf("%zu cast symbols linked; %u pairs x zext, sext, trunc x 200 samples, both modes, checked\n", sizeof linked / sizeof *linked, k);
    return 0;
}
