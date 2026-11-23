/* ABI §2.2 udiv, urem, sdiv, srem (kmgy.12): every public symbol, every shape, every width, through
 * the sampled harness in both modes against CR §2's totalization, ancillae checked back to |0>;
 * pinned edge cases, aliasing, literal folding, cq_invert and the link check. Always asserts. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "harness.h"
#include "cq_template.h"
#include "cqrt.h"
#include "cq_replay.h"

/* ---- The reference: CR §2's rows taken literally, then long division on limbs; for W <= 64 also
 * C's own / and % on __int128 (truncating, remainder with the dividend's sign), which must agree. */
enum { UDIV, UREM, SDIV, SREM };
static const hx_val zero, one = { { 1 } };
static int bitv(const hx_val *v, uint32_t i) { return (int)(v->l[i / 64] >> (i % 64) & 1); }
static hx_val sub_(hx_val a, hx_val b) {                       /* a - b mod 2^(64 CQ_LIMBS) */
    uint64_t br = 0;
    for (uint32_t k = 0; k < CQ_LIMBS; k++) {
        uint64_t d = a.l[k] - b.l[k] - br;
        br = a.l[k] < b.l[k] || (a.l[k] == b.l[k] && br);
        a.l[k] = d;
    }
    return a;
}
static hx_val neg(hx_val v, uint32_t w) { return hx_mask(sub_(zero, v), w); }
static void udivmod(hx_val a, hx_val b, uint32_t w, hx_val *q, hx_val *r) {   /* b != 0 */
    *q = *r = zero;
    for (uint32_t i = w; i--;) {
        for (uint32_t k = CQ_LIMBS - 1; k; k--) r->l[k] = r->l[k] << 1 | r->l[k - 1] >> 63;
        r->l[0] = r->l[0] << 1 | (uint64_t)bitv(&a, i);
        hx_val d = sub_(*r, b);
        if (!bitv(&d, 64 * CQ_LIMBS - 1)) {            /* r >= b: r < 2^(w+1) leaves the top bit free */
            *r = d;
            q->l[i / 64] |= 1ull << (i % 64);
        }
    }
}
static hx_val ref_limbs(int op, hx_val a, hx_val b, uint32_t w) {
    hx_val ones = hx_ones(w), smin = { { 0 } };
    smin.l[(w - 1) / 64] = 1ull << ((w - 1) % 64);
    int sa = op >= SDIV && bitv(&a, w - 1), sb = op >= SDIV && bitv(&b, w - 1);
    if (hx_eq(b, zero)) return op == UDIV ? ones : op == SDIV ? (sa ? one : ones) : a;
    if (op >= SDIV && hx_eq(a, smin) && hx_eq(b, ones)) return op == SDIV ? smin : zero;
    hx_val q, r;
    udivmod(sa ? neg(a, w) : a, sb ? neg(b, w) : b, w, &q, &r);
    switch (op) {
    case UDIV: return q;
    case UREM: return r;
    case SDIV: return sa != sb ? neg(q, w) : q;
    default:   return sa ? neg(r, w) : r;
    }
}
static hx_val ref_native(int op, hx_val a, hx_val b, uint32_t w) {
    if (op < SDIV) {
        uint64_t x = a.l[0], y = b.l[0];
        if (!y) return op == UDIV ? hx_ones(w) : a;
        return hx_of_u128(op == UDIV ? x / y : x % y);
    }
    __int128 x = (__int128)a.l[0] << (128 - w) >> (128 - w), y = (__int128)b.l[0] << (128 - w) >> (128 - w);
    if (!y) return op == SDIV ? hx_mask(hx_of_u128((unsigned __int128)(x >= 0 ? -1 : 1)), w) : a;
    __int128 v = op == SDIV ? x / y : x % y;                   /* INT_MIN / -1 = 2^(W-1): INT_MIN mod 2^W, rem 0 */
    return hx_mask(hx_of_u128((unsigned __int128)v), w);
}
static hx_val ref(int op, const hx_val *a, uint32_t w) {
    hx_val r = ref_limbs(op, a[0], a[1], w);
    if (w <= 64) assert(hx_eq(r, ref_native(op, a[0], a[1], w)));
    return r;
}

/* Every qubit in the pool's free list reads 0: the kernel's ancillae came back clean. */
static void clean(void) {
    uint32_t n = cq_pool_minted() - cq_pool_live(), *q = malloc((n + 1) * sizeof *q);
    assert(q);
    for (uint32_t i = 0; i < n; i++) { q[i] = cq_pool_take(); assert(!cq_replay_get_bit(q[i])); }
    while (n) cq_pool_give(q[--n]);
    free(q);
}

/* A literal from a sample value, with garbage above bit w (only the low w bits may be read). */
#define LITN_(bits, v) ((CQRT_UINT(bits))(hx_u128(v) | ((bits) < 128 ? ~(unsigned __int128)0 << ((bits) % 128) : 0)))
#define LITW_(v) (v).l[0], (v).l[1], (v).l[2], (v).l[3] | ~0xffull

/* Harness op: forward into a fresh r, clean, CX r into o, _unc r, free r, clean. Harness unc: the
 * public _unc on o. FWD and UNC spell one shape's call; UNC writes to dst. */
#define SHAPE_(tag, bits, OPN, FWD, UNC, N) \
    static void f_##tag(int32_t o, const int32_t *a, uint32_t w) { \
        (void)w; (void)a; uint32_t r = FWD; clean(); cq_copy(0, 0, r, (uint32_t)o, bits); \
        { uint32_t dst = r; UNC; } cqrt_free(r); clean(); } \
    static void u_##tag(int32_t o, const int32_t *a, uint32_t w) { (void)w; (void)a; uint32_t dst = (uint32_t)o; UNC; } \
    static hx_val r_##tag(const hx_val *a, uint32_t w) { return ref(OPN, a, w); } \
    static void c_##tag(void) { const hx_op h = { #tag, 2, 0, f_##tag, u_##tag, r_##tag }; \
        hx_max_samples = (N); assert(hx_run(&h, bits, 0, NULL) == HX_OK); hx_max_samples = 0; }
/* Samples per symbol and width, fewer where a call is big (~10^6 gates at i200); main() prints them.
 * Edge values (0, 1, -1, INT_MIN, INT_MAX per operand) come first; pinned() covers them at every width. */
#define BARE_SAMPLES(bits) ((bits) <= 8 ? 100 : (bits) <= 32 ? 12 : (bits) <= 64 ? 4 : 2)
#define LIT_SAMPLES(bits) ((bits) <= 8 ? 20 : (bits) <= 32 ? 4 : 2)
#define A0 ((uint32_t)a[0])
#define A1 ((uint32_t)a[1])
#define S0 hx_sample[0]
#define S1 hx_sample[1]
#define T_(name, n) cq_template_##name##_##n

#define SHAPES_(name, OPN, n, bits, LIT) \
    SHAPE_(name##_##n, bits, OPN, T_(name, n)(A0, A1), T_(name, n##_unc)(dst, A0, A1), BARE_SAMPLES(bits)) \
    SHAPE_(name##_##n##_hl, bits, OPN, T_(name, n##_hl)(A0, LIT(S1)), T_(name, n##_hl_unc)(dst, A0, LIT(S1)), LIT_SAMPLES(bits)) \
    SHAPE_(name##_##n##_lh, bits, OPN, T_(name, n##_lh)(LIT(S0), A1), T_(name, n##_lh_unc)(dst, LIT(S0), A1), LIT_SAMPLES(bits))
#define OPS_(X, n, bits, LIT) X(udiv, UDIV, n, bits, LIT) X(urem, UREM, n, bits, LIT) X(sdiv, SDIV, n, bits, LIT) X(srem, SREM, n, bits, LIT)
/* LITN_ needs bits as its first argument: one wrapper per narrow width. */
#define LITN_BITS_(n, bits) static inline CQRT_UINT(bits) lit_##n(hx_val v) { return LITN_(bits, v); }
CQ_WIDTHS_NARROW(LITN_BITS_)
#define DEF_NARROW_(n, bits) OPS_(SHAPES_, n, bits, lit_##n)
#define DEF_WIDE_(n, bits) OPS_(SHAPES_, n, bits, LITW_)
CQ_WIDTHS_NARROW(DEF_NARROW_)
CQ_WIDTHS_WIDE(DEF_WIDE_)

/* Running all of them, and the link check: every generated name, forward and _unc. */
#define RUN_OP_(name, OPN, n, bits, LIT) c_##name##_##n(); c_##name##_##n##_hl(); c_##name##_##n##_lh();
#define RUN_(n, bits) OPS_(RUN_OP_, n, bits, ) printf("i%u: ok\n", bits); fflush(stdout);
typedef void (*fn)(void);
#define ADDR_OP_(name, OPN, n, bits, LIT) (fn)T_(name, n), (fn)T_(name, n##_unc), (fn)T_(name, n##_hl), \
    (fn)T_(name, n##_hl_unc), (fn)T_(name, n##_lh), (fn)T_(name, n##_lh_unc),
#define ADDR_(n, bits) OPS_(ADDR_OP_, n, bits, )
static const fn linked[] = { CQ_WIDTHS(ADDR_) };

/* A rail of w bits holding v, forced to qubits when quantum. */
static uint32_t rail(hx_val v, uint32_t w, int quantum) {
    int32_t r = cq_rail_alloc(w, v.l);
    cq_bit *b = cq_rail_bits(r);
    for (uint32_t i = 0; quantum && i < w; i++) { cq_get_qubit(&b[i]); cq_replay_set_bit(b[i].q, bitv(&v, i)); }
    return (uint32_t)r;
}

/* Pinned edge cases through the bare forward symbol, forced-quantum: divisor 0, 1, -1, INT_MIN / -1,
 * dividend 0, equal operands, divisor > dividend, and the same rail as both operands. */
typedef uint32_t (*bin_fn)(uint32_t, uint32_t);
static void pin1(bin_fn f, int op, uint32_t w, hx_val x, hx_val y, int alias) {
    uint32_t base = cq_pool_live(), a = rail(x, w, 1), b = alias ? a : rail(y, w, 1), o = f(a, b);
    clean();
    hx_val args[2] = { x, alias ? x : y }, got = hx_from_rail((int32_t)o), want = ref(op, args, w);
    if (!hx_eq(got, want)) { fprintf(stderr, "pinned op %d i%u: 0x%llx / 0x%llx\n", op, w, (unsigned long long)x.l[0], (unsigned long long)y.l[0]); abort(); }
    assert(hx_eq(hx_from_rail((int32_t)a), x) && (alias || hx_eq(hx_from_rail((int32_t)b), y)));
    cqrt_free(o); cqrt_free(a); if (!alias) cqrt_free(b);
    assert(cq_pool_live() == base);
}
static void pinned(const bin_fn *f, uint32_t w) {
    hx_val ones = hx_ones(w), smin = { { 0 } }, seven = { { 7 } };
    smin.l[(w - 1) / 64] = 1ull << ((w - 1) % 64);
    const hx_val big = hx_xor(ones, smin), small = hx_mask(seven, w);   /* INT_MAX, 7 */
    const hx_val pairs[][2] = {
        { big, zero }, { ones, zero }, { zero, zero }, { smin, zero },   /* divisor 0 */
        { big, one }, { smin, one }, { big, ones }, { small, ones },     /* divisor 1, -1 */
        { smin, ones },                                                  /* INT_MIN / -1 */
        { zero, small }, { zero, ones },                                 /* dividend 0 */
        { big, big }, { smin, smin },                                    /* equal */
        { small, big }, { one, smin },                                   /* divisor > dividend */
    };
    /* Above i64 (a call is up to ~10^6 gates) only divisor 0, INT_MIN / -1 and the aliased pair. */
    unsigned np = w <= 64 ? sizeof pairs / sizeof *pairs : 1;
    for (int op = 0; op < 4; op++) {
        for (unsigned k = 0; k < np; k++) pin1(f[op], op, w, pairs[k][0], pairs[k][1], 0);
        if (w > 64) pin1(f[op], op, w, smin, ones, 0);
        pin1(f[op], op, w, small, zero, 1);
        if (w <= 64) pin1(f[op], op, w, ones, zero, 1);
    }
}
#define PIN_(n, bits) pinned((const bin_fn[]){ T_(udiv, n), T_(urem, n), T_(sdiv, n), T_(srem, n) }, bits); \
    printf("pinned i%u: ok\n", bits); fflush(stdout);

/* cq_invert: invert{ X } alone is X (o ^= f), and a forward call inside a region holds |0> until the
 * end, then f(a, b); both modes through the harness. */
#define INV_(n, bits, LIT) \
    static void inv_##n(int32_t o, const int32_t *a, uint32_t w) { (void)w; \
        cqrt_invert_begin(); cq_template_sdiv_##n##_unc((uint32_t)o, A0, A1); cqrt_invert_end(); } \
    static void invlh_##n(int32_t o, const int32_t *a, uint32_t w) { (void)w; \
        cqrt_invert_begin(); cq_template_urem_##n##_lh_unc((uint32_t)o, LIT(S0), A1); cqrt_invert_end(); } \
    static void invfw_##n(int32_t o, const int32_t *a, uint32_t w) { (void)w; \
        cqrt_invert_begin(); uint32_t t = cq_template_udiv_##n(A0, A1); \
        assert(hx_eq(hx_from_rail((int32_t)t), zero)); cqrt_invert_end(); \
        cq_copy(0, 0, t, (uint32_t)o, bits); cq_template_udiv_##n##_unc(t, A0, A1); cqrt_free(t); } \
    static hx_val rs_##n(const hx_val *a, uint32_t w) { return ref(SDIV, a, w); } \
    static hx_val ru_##n(const hx_val *a, uint32_t w) { return ref(UREM, a, w); } \
    static hx_val rd_##n(const hx_val *a, uint32_t w) { return ref(UDIV, a, w); } \
    static void inv_check_##n(void) { \
        hx_max_samples = (bits) < 32 ? 20 : 6; \
        assert(hx_run(&(hx_op){ "invert sdiv_unc " #n, 2, 0, inv_##n, NULL, rs_##n }, bits, 0, NULL) == HX_OK); \
        assert(hx_run(&(hx_op){ "invert urem_lh_unc " #n, 2, 0, invlh_##n, NULL, ru_##n }, bits, 0, NULL) == HX_OK); \
        assert(hx_run(&(hx_op){ "invert udiv forward " #n, 2, 0, invfw_##n, NULL, rd_##n }, bits, 0, NULL) == HX_OK); \
        hx_max_samples = 0; }
INV_(i7, 7, lit_i7)
INV_(i32, 32, lit_i32)

int main(void) {
    assert(sizeof linked / sizeof *linked == 30 * 4 * 6);
    for (unsigned i = 0; i < sizeof linked / sizeof *linked; i++) assert(linked[i]);

    /* A literal operand folds: cheaper than two quantum operands; an all-classical call takes no qubit
     * and emits no gate. Division by a literal power of two is reported (not a special symbol). */
    {
        uint32_t base = cq_pool_live();
        cq_replay_reset();
        hx_val v5 = { { 1000 } }, v9 = { { 9 } };
        uint32_t a = rail(v5, 32, 1), b = rail(v9, 32, 1);
        uint64_t g0 = cq_replay_gates();
        uint32_t s = cq_template_udiv_i32(a, b);
        uint64_t both = cq_replay_gates() - g0, g1 = cq_replay_gates();
        uint32_t t = cq_template_udiv_i32_hl(a, 9);
        uint64_t one_ = cq_replay_gates() - g1, g2 = cq_replay_gates();
        uint32_t p = cq_template_udiv_i32_hl(a, 8);
        uint64_t pow2 = cq_replay_gates() - g2;
        assert(one_ < both);
        assert(cqrt_measure_i32(s) == 111 && cqrt_measure_i32(t) == 111 && cqrt_measure_i32(p) == 125);
        printf("udiv_i32: %llu gates, _hl by 9: %llu, _hl by 8: %llu\n",
               (unsigned long long)both, (unsigned long long)one_, (unsigned long long)pow2);
        cqrt_free(s); cqrt_free(t); cqrt_free(p); cqrt_free(a); cqrt_free(b);
        uint32_t c = cqrt_alloc_i200(1, 2, 3, 4);
        cq_replay_reset();
        uint32_t m = cq_template_sdiv_i200_lh(~0ull, ~0ull, ~0ull, ~0ull, c);   /* -1 / c */
        uint32_t k = cq_template_urem_i200_hl(c, 0, 0, 0, 0);                  /* c rem 0 = c */
        assert(cq_replay_gates() == 0 && cq_pool_live() == base);
        cqrt_u256 v = cqrt_measure_i200(m), u = cqrt_measure_i200(k);
        assert(v.l[0] == 0 && v.l[1] == 0 && v.l[2] == 0 && v.l[3] == 0);
        assert(u.l[0] == 1 && u.l[1] == 2 && u.l[2] == 3 && u.l[3] == 4);
        cqrt_free(m); cqrt_free(k); cqrt_free(c);
        assert(cq_pool_live() == base);
    }

    /* Narrow widths first: clean() walks the whole pool, which the wide calls grow. */
    CQ_WIDTHS(RUN_)
    inv_check_i7(); inv_check_i32();
    printf("cq_invert: ok\n"); fflush(stdout);
    CQ_WIDTHS(PIN_)
    printf("%zu template symbols linked and checked; samples per width: bare 100 (<= i8), 12 (<= i32), "
           "4 (i48, i64), 2 (i80 and up); literal shapes 20, 4, 2\n", sizeof linked / sizeof *linked);
    return 0;
}
