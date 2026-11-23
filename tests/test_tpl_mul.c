/* ABI §2.2 mul and §2.7 umul_overflow, smul_overflow (kmgy.11): every public symbol, both shapes,
 * every width, through the sampled harness in both modes, ancillae checked back to |0>; aliasing,
 * literal cost, cq_invert, the cq_mul window API at unequal widths, and the link check. Always asserts. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include "harness.h"
#include "kernels.h"
#include "cq_template.h"
#include "cqrt.h"
#include "cq_replay.h"

/* ---- References, written from the ABI text: the exact product of the magnitudes, schoolbook on
 * 32-bit digits (2W <= 400 bits), cross-checked with unsigned/signed __int128 up to i64. */
enum { MUL, UMULO, SMULO };
#define PL 14   /* 32-bit digits of a product, 448 bits */
static int bitv(const uint32_t *v, uint32_t i) { return (int)(v[i / 32] >> (i % 32) & 1); }
static int hbit(const hx_val *v, uint32_t i) { return (int)(v->l[i / 64] >> (i % 64) & 1); }

/* |v| (v read as w-bit two's complement when sgn) into 32-bit digits; returns the sign. */
static int digits(hx_val v, uint32_t w, int sgn, uint32_t d[PL / 2]) {
    int neg = sgn && hbit(&v, w - 1);
    memset(d, 0, PL / 2 * sizeof *d);
    for (uint32_t i = 0; i < w; i++) if (hbit(&v, i)) d[i / 32] |= 1u << (i % 32);
    if (neg) {   /* 2^w - v */
        uint64_t c = 1;
        for (uint32_t k = 0; k < PL / 2; k++) { c += (uint32_t)~d[k]; d[k] = (uint32_t)c; c >>= 32; }
        for (uint32_t i = w; i < PL / 2 * 32; i++) d[i / 32] &= ~(1u << (i % 32));
    }
    return neg;
}

static hx_val ref(int op, const hx_val *a, uint32_t w) {
    uint32_t x[PL / 2], y[PL / 2], p[PL] = { 0 };
    int neg = digits(a[0], w, op == SMULO, x) ^ digits(a[1], w, op == SMULO, y);
    for (uint32_t i = 0; i < PL / 2; i++) {
        uint64_t c = 0;
        for (uint32_t j = 0; j < PL / 2; j++) { c += (uint64_t)x[i] * y[j] + p[i + j]; p[i + j] = (uint32_t)c; c >>= 32; }
        p[i + PL / 2] = (uint32_t)c;
    }
    int hi = 0, lo = 0;
    for (uint32_t i = w; i < 2 * w; i++) hi |= bitv(p, i);
    for (uint32_t i = 0; i + 1 < w; i++) lo |= bitv(p, i);
    hx_val r = { { 0 } };
    if (op == MUL) for (uint32_t i = 0; i < w; i++) r.l[i / 64] |= (uint64_t)bitv(p, i) << (i % 64);
    else if (op == UMULO) r.l[0] = (uint64_t)hi;
    else r.l[0] = (uint64_t)(neg ? hi || (bitv(p, w - 1) && lo) : hi || bitv(p, w - 1));   /* |p| > or >= 2^(W-1) */

    if (w <= 64) {
        unsigned __int128 ua = hx_u128(a[0]), ub = hx_u128(a[1]), up = ua * ub;
        __int128 sa = (__int128)ua - (hbit(&a[0], w - 1) ? (__int128)1 << w : 0);
        __int128 sb = (__int128)ub - (hbit(&a[1], w - 1) ? (__int128)1 << w : 0), sp = sa * sb, lim = (__int128)1 << (w - 1);
        uint64_t want = op == MUL ? (uint64_t)(w == 64 ? up : up & ((((unsigned __int128)1) << w) - 1))
                      : op == UMULO ? (uint64_t)(up >> w != 0) : (uint64_t)(sp < -lim || sp >= lim);
        if (r.l[0] != want) { fprintf(stderr, "ref disagrees with __int128 at i%u op %d\n", w, op); abort(); }
    }
    return r;
}

/* Every qubit in the pool's free list reads 0: the kernel's ancillae came back clean. */
static void clean(void) {
    uint32_t n = cq_pool_minted() - cq_pool_live(), *q = malloc((n ? n : 1) * sizeof *q);
    assert(q);
    for (uint32_t i = 0; i < n; i++) { q[i] = cq_pool_take(); assert(!cq_replay_get_bit(q[i])); }
    while (n) cq_pool_give(q[--n]);
    free(q);
}

/* A literal from a sample value, with garbage above bit w (only the low w bits may be read). */
#define LITN_(bits, v) ((CQRT_UINT(bits))(hx_u128(v) | ((bits) < 128 ? ~(unsigned __int128)0 << ((bits) % 128) : 0)))
#define LITW_(v) (v).l[0], (v).l[1], (v).l[2], (v).l[3] | ~0xffull

/* A rail of w bits holding v, its bits forced to qubits when quantum. */
static int32_t railv(hx_val v, uint32_t w, int quantum) {
    int32_t r = cq_rail_alloc(w, v.l);
    cq_bit *b = cq_rail_bits(r);
    for (uint32_t i = 0; quantum && i < w; i++) { cq_get_qubit(&b[i]); cq_replay_set_bit(b[i].q, hbit(&v, i)); }
    return r;
}

/* The overflow boundaries the random samples rarely hit, at every width: products just below and at
 * 2^W, at -2^(W-1) and 2^(W-1), INT_MIN times -1 and 1, all-ones times 1 and 2 (k = (W-1)/2). One
 * call of h->op on o = 0 against h->ref, operands checked unchanged. */
static hx_val p2(uint32_t k) { hx_val v = { { 0 } }; v.l[k / 64] = 1ull << (k % 64); return v; }
static hx_val addm(hx_val v, uint64_t d, uint32_t w) {   /* v + d mod 2^w, d = 1 or -1 */
    for (uint32_t k = 0; k < CQ_LIMBS; k++) { uint64_t o = v.l[k]; v.l[k] += d; if (d == 1 ? v.l[k] > o : v.l[k] < o) break; }
    return hx_mask(v, w);
}
static hx_val negm(hx_val v, uint32_t w) { return addm(hx_mask(hx_xor(v, hx_ones(w)), w), 1, w); }
static void directed(const hx_op *h, uint32_t w, int quantum, unsigned npairs) {
    uint32_t k = (w - 1) / 2, ow = h->out_width ? h->out_width : w;
    hx_val pk = p2(k), q = p2(w - k < w ? w - k : 0), s = p2(w - 1 - k), ones = hx_ones(w), min = p2(w - 1), two = p2(1 % w);
    const hx_val pairs[][2] = {
        { pk, addm(q, (uint64_t)-1, w) }, { pk, q }, { ones, p2(0) }, { ones, two },
        { negm(pk, w), s }, { pk, s }, { negm(pk, w), negm(s, w) }, { pk, addm(s, (uint64_t)-1, w) },
        { min, ones }, { min, p2(0) }, { ones, ones },
    };
    for (unsigned i = 0; i < sizeof pairs / sizeof *pairs && i < npairs; i++) {
        int32_t o = railv((hx_val){ { 0 } }, ow, 0), a[2] = { railv(pairs[i][0], w, quantum), railv(pairs[i][1], w, quantum) };
        hx_sample[0] = pairs[i][0]; hx_sample[1] = pairs[i][1];
        h->op(o, a, w);
        if (!hx_eq(hx_from_rail(o), hx_mask(h->ref(pairs[i], w), ow))) {
            fprintf(stderr, "%s: boundary pair %u wrong (%s)\n", h->name, i, quantum ? "quantum" : "classical");
            abort();
        }
        for (int j = 0; j < 2; j++) { assert(hx_eq(hx_from_rail(a[j]), pairs[i][j])); cq_rail_free(a[j]); }
        cq_rail_free(o);
    }
}

/* Harness op: forward into a fresh r, CX r into o, _unc r, free r; ancillae checked clean after the
 * forward and after the _unc. Harness unc: the public _unc on o. Then the boundary pairs: classical
 * at every width, quantum all up to i16 and the first two above. */
#define SHAPE_(tag, ow, owb, bits, OPN, FWD, UNC, N) \
    static void f_##tag(int32_t o, const int32_t *a, uint32_t w) { \
        (void)w; (void)a; uint32_t r = FWD; clean(); cqrt_copy_##ow(r, (uint32_t)o); \
        { uint32_t dst = r; UNC; } cqrt_free(r); clean(); } \
    static void u_##tag(int32_t o, const int32_t *a, uint32_t w) { (void)w; (void)a; uint32_t dst = (uint32_t)o; UNC; } \
    static hx_val r_##tag(const hx_val *a, uint32_t w) { return ref(OPN, a, w); } \
    static void c_##tag(void) { const hx_op h = { #tag, 2, owb, f_##tag, u_##tag, r_##tag }; \
        hx_max_samples = (N); assert(hx_run(&h, bits, 0, NULL) == HX_OK); hx_max_samples = 0; \
        directed(&h, bits, 0, 99); directed(&h, bits, 1, (bits) <= 16 ? 99 : 2); }
/* Samples per symbol and width (each runs 2 o0 x 2 modes x forward/+unc; a sample is (a, b, o0)).
 * An i200 call is ~10^6 gates and ~2 10^5 scratch bits, so: bare 200 up to i8, 60 up to i17, 24 up
 * to i32, 10 up to i64, 6 up to i128, 4 at i200; _hl (the same kernel with a classical rail) 40 up to
 * i17, 12 up to i32, 6 up to i64, 4 above. Edge values (0, 1, -1, INT_MIN, INT_MAX per operand) come
 * first; 4 samples are the tuple (0, 0) and one random one, so the boundary pairs carry the wide widths. */
#define BARE_SAMPLES(bits) ((bits) <= 8 ? 200 : (bits) <= 17 ? 60 : (bits) <= 32 ? 24 : (bits) <= 64 ? 10 : (bits) <= 128 ? 6 : 4)
#define LIT_SAMPLES(bits) ((bits) <= 17 ? 40 : (bits) <= 32 ? 12 : (bits) <= 64 ? 6 : 4)
#define A0 ((uint32_t)a[0])
#define A1 ((uint32_t)a[1])
#define S1 hx_sample[1]
#define T_(name, n) cq_template_##name##_##n

#define BARE_(name, n, bits, ow, owb, OPN) SHAPE_(name##_##n, ow, owb, bits, OPN, \
    T_(name, n)(A0, A1), T_(name, n##_unc)(dst, A0, A1), BARE_SAMPLES(bits))
#define HLN_(name, n, bits, ow, owb, OPN) SHAPE_(name##_##n##_hl, ow, owb, bits, OPN, \
    T_(name, n##_hl)(A0, LITN_(bits, S1)), T_(name, n##_hl_unc)(dst, A0, LITN_(bits, S1)), LIT_SAMPLES(bits))
#define HLW_(name, n, bits, ow, owb, OPN) SHAPE_(name##_##n##_hl, ow, owb, bits, OPN, \
    T_(name, n##_hl)(A0, LITW_(S1)), T_(name, n##_hl_unc)(dst, A0, LITW_(S1)), LIT_SAMPLES(bits))

/* X(name, OP, result copy suffix (W or i1), harness width (0: W)). All commutative: no _lh (ABI §2.1). */
#define OPS_(X, n, bits, HL) \
    X(mul, MUL, n, 0, n, bits, HL) X(umul_overflow, UMULO, i1, 1, n, bits, HL) X(smul_overflow, SMULO, i1, 1, n, bits, HL)
#define DEF_OP_(name, OPN, ow, owb, n, bits, HL) BARE_(name, n, bits, ow, owb, OPN) HL(name, n, bits, ow, owb, OPN)
#define DEF_NARROW_(n, bits) OPS_(DEF_OP_, n, bits, HLN_)
#define DEF_WIDE_(n, bits) OPS_(DEF_OP_, n, bits, HLW_)
CQ_WIDTHS_NARROW(DEF_NARROW_)
CQ_WIDTHS_WIDE(DEF_WIDE_)

#define RUN_OP_(name, OPN, ow, owb, n, bits, HL) c_##name##_##n(); c_##name##_##n##_hl();
#define RUN_(n, bits) { clock_t t0 = clock(); OPS_(RUN_OP_, n, bits, ) \
    printf("i%u: ok (%.2f s)\n", bits, (double)(clock() - t0) / CLOCKS_PER_SEC); fflush(stdout); }
typedef void (*fn)(void);
#define ADDR_OP_(name, OPN, ow, owb, n, bits, HL) (fn)T_(name, n), (fn)T_(name, n##_unc), (fn)T_(name, n##_hl), (fn)T_(name, n##_hl_unc),
#define ADDR_(n, bits) OPS_(ADDR_OP_, n, bits, )
static const fn linked[] = { CQ_WIDTHS(ADDR_) };

/* cq_invert (ABI §1.13): X then the inverted X, and the inverted X then X, leave o at o0. */
#define B cqrt_invert_begin()
#define E cqrt_invert_end()
static hx_val ref_zero(const hx_val *a, uint32_t w) { (void)a; (void)w; return (hx_val){ { 0 } }; }
#define INV_(n, bits, X) \
    static void inv_fi_##n(int32_t o, const int32_t *a, uint32_t w) { (void)w; X; B; X; E; } \
    static void inv_if_##n(int32_t o, const int32_t *a, uint32_t w) { (void)w; B; X; E; X; } \
    static void inv_##n(void) { const hx_op h = { "invert mul " #n, 2, 0, inv_fi_##n, inv_if_##n, ref_zero }; \
        hx_max_samples = 40; assert(hx_run(&h, bits, 0, NULL) == HX_OK); hx_max_samples = 0; }
INV_(i8, 8, cq_template_mul_i8_unc((uint32_t)o, A0, A1))
INV_(i32, 32, cq_template_mul_i32_hl_unc((uint32_t)o, A0, LITN_(32, hx_sample[1])))
static void inv1_fi(int32_t o, const int32_t *a, uint32_t w) {   /* a flag result: o is i1 */
    (void)w; cq_template_smul_overflow_i17_unc((uint32_t)o, A0, A1); B; cq_template_smul_overflow_i17_unc((uint32_t)o, A0, A1); E; }
static void inv1_if(int32_t o, const int32_t *a, uint32_t w) {
    (void)w; B; cq_template_smul_overflow_i17_unc((uint32_t)o, A0, A1); E; cq_template_smul_overflow_i17_unc((uint32_t)o, A0, A1); }

/* A rail of w bits holding v, forced to qubits when quantum. */
static uint32_t rail(uint64_t v, uint32_t w, int quantum) {
    int32_t r = cq_rail_alloc(w, (const uint64_t[4]){ v, 0, 0, 0 });
    cq_bit *b = cq_rail_bits(r);
    for (uint32_t i = 0; quantum && i < w; i++) { cq_get_qubit(&b[i]); cq_replay_set_bit(b[i].q, (int)(v >> i & 1)); }
    return (uint32_t)r;
}

/* cq_mul at unequal widths with a window (the angle ladder's call): x quantum, y a classical
 * constant or quantum, each kind, against the reference product bits. */
static void window(int kind, uint32_t nx, uint32_t ny, int sgn, int yq, uint32_t lo, uint32_t n, uint64_t xv, uint64_t yv) {
    cq_bit x[64], y[64];
    for (uint32_t i = 0; i < nx; i++) { x[i] = cq_bit_const(0); cq_get_qubit(&x[i]); cq_replay_set_bit(x[i].q, (int)(xv >> i & 1)); }
    for (uint32_t j = 0; j < ny; j++) {
        y[j] = cq_bit_const((int)(yv >> j & 1));
        if (yq) { y[j] = cq_bit_const(0); cq_get_qubit(&y[j]); cq_replay_set_bit(y[j].q, (int)(yv >> j & 1)); }
    }
    __int128 X = (__int128)xv, Y = (__int128)yv;
    if (sgn && nx < 64 && xv >> (nx - 1) & 1) X -= (__int128)1 << nx;
    if (sgn && ny < 64 && yv >> (ny - 1) & 1) Y -= (__int128)1 << ny;
    unsigned __int128 P = (unsigned __int128)(X * Y);
    const cq_bit *out[64];
    cq_tape t = { 0 };
    cq_mul(kind, &t, x, nx, y, ny, sgn, lo, n, out);
    for (uint32_t k = 0; k < n; k++) {
        cq_bit b = *out[k];
        int got = cq_bit_is_q(b) ? cq_replay_get_bit(b.q) : b.kind;
        if (got != (int)(P >> (lo + k) & 1)) { fprintf(stderr, "cq_mul kind %d %ux%u sgn %d bit %u\n", kind, nx, ny, sgn, lo + k); abort(); }
    }
    cq_tape_undo(&t);
    clean();
    for (uint32_t i = 0; i < nx; i++) { assert(cq_replay_get_bit(x[i].q) == (int)(xv >> i & 1)); cq_reset_bit(&x[i]); }
    for (uint32_t j = 0; j < ny && yq; j++) cq_reset_bit(&y[j]);
}

int main(void) {
    assert(sizeof linked / sizeof *linked == 29 * 3 * 4);
    for (unsigned i = 0; i < sizeof linked / sizeof *linked; i++) assert(linked[i]);
    uint32_t base = cq_pool_live();

    /* Aliasing (ABI §0): one rail in both operand slots: a * a. */
    for (int quantum = 1; quantum >= 0; quantum--) {
        uint32_t a = rail(0xb5, 8, quantum), s = rail(0x80, 8, quantum);
        uint32_t m = cq_template_mul_i8(a, a), u = cq_template_umul_overflow_i8(a, a), v = cq_template_smul_overflow_i8(a, a);
        uint32_t w = cq_template_smul_overflow_i8(s, s);   /* (-128)^2 = 2^14 */
        assert((uint8_t)cqrt_measure_i8(m) == (uint8_t)(0xb5 * 0xb5) && cqrt_measure_i1(u) == 1);
        assert(cqrt_measure_i1(v) == 1 && cqrt_measure_i1(w) == 1);   /* 0xb5 = -75: 5625 > 127 */
        assert((uint8_t)cqrt_measure_i8(a) == 0xb5);
        cqrt_free(m); cqrt_free(u); cqrt_free(v); cqrt_free(w); cqrt_free(a); cqrt_free(s);
    }
    assert(cq_pool_live() == base);

    /* A literal folds: _hl is cheaper than two quantum operands (a 0 bit drops its row, a 1 bit
     * makes it wires), and an all-classical call takes no qubit and emits no gate. */
    {
        static const struct { uint64_t lit; } L[] = { { 0x10001 }, { 0xdeadbeef }, { 0xffffffff } };
        for (unsigned k = 0; k < 3; k++) {
            cq_replay_reset();   /* before the inputs: a reset clears every bit */
            uint32_t a = rail(0x12345678, 32, 1), b = rail(L[k].lit, 32, 1);
            uint32_t s = cq_template_mul_i32(a, b);
            uint64_t g2 = cq_replay_gates(); uint32_t d2 = cq_replay_depth(), q2 = cq_replay_qubits();
            assert((uint32_t)cqrt_measure_i32(s) == (uint32_t)(0x12345678u * (uint32_t)L[k].lit));
            cqrt_free(s); cqrt_free(a); cqrt_free(b);
            cq_replay_reset();
            a = rail(0x12345678, 32, 1);
            uint32_t t = cq_template_mul_i32_hl(a, (uint32_t)L[k].lit);
            uint64_t g1 = cq_replay_gates(); uint32_t d1 = cq_replay_depth(), q1 = cq_replay_qubits();
            printf("mul_i32 by 0x%llx: bare %u / %llu / %u, _hl %u / %llu / %u (depth / gates / qubits)\n",
                   (unsigned long long)L[k].lit, d2, (unsigned long long)g2, q2, d1, (unsigned long long)g1, q1);
            assert(g1 < g2 && q1 < q2 && d1 <= d2);
            assert(k || (g1 * 4 < g2 && q1 * 4 < q2));   /* two set bits: two rows of wires */
            assert((uint32_t)cqrt_measure_i32(t) == (uint32_t)(0x12345678u * (uint32_t)L[k].lit));
            cqrt_free(t); cqrt_free(a);
        }
        uint32_t c = cqrt_alloc_i200(3, 0, 0, 1ull << 7);   /* 3 + 2^199 */
        uint32_t minted = cq_pool_minted();
        cq_replay_reset();
        uint32_t m = cq_template_mul_i200_hl(c, 5, 0, 0, 0);
        uint32_t u = cq_template_umul_overflow_i200_hl(c, 2, 0, 0, 0), v = cq_template_smul_overflow_i200(c, c);
        assert(cq_replay_gates() == 0 && cq_replay_qubits() == 0 && cq_pool_minted() == minted);
        cqrt_u256 r = cqrt_measure_i200(m);
        assert(r.l[0] == 15 && r.l[1] == 0 && r.l[2] == 0 && r.l[3] == 1ull << 7);   /* 15 + 5 * 2^199 mod 2^200 */
        assert(cqrt_measure_i1(u) == 1 && cqrt_measure_i1(v) == 1);
        cqrt_free(m); cqrt_free(u); cqrt_free(v); cqrt_free(c);
    }
    assert(cq_pool_live() == base);

    /* The kernel API the angle ladder calls: unequal widths, a window, a classical or quantum y. */
    for (int kind = CQ_MUL_SHIFT_ADD; kind <= CQ_MUL_TREE; kind++) {
        window(kind, 13, 37, 0, 0, 20, 30, 0x1abc, 0x1e0ff0f0f1ull);
        window(kind, 13, 37, 0, 1, 7, 43, 0x1fff, 0x1fffffffffull);
        window(kind, 37, 13, 0, 1, 0, 50, 0x123456789ull, 0x1001);
        window(kind, 5, 9, 1, 1, 0, 14, 0x11, 0x1ff);   /* -15 * -1 */
        window(kind, 9, 5, 1, 0, 3, 11, 0x100, 0x10);   /* -256 * -16 */
        window(kind, 1, 1, 1, 1, 0, 2, 1, 1);           /* -1 * -1 */
    }
    assert(cq_pool_live() == base);

    /* cq_invert: the recorded template call, reversed. */
    inv_i8(); inv_i32();
    {
        const hx_op h = { "invert smul_overflow i17", 2, 1, inv1_fi, inv1_if, ref_zero };
        hx_max_samples = 40;
        assert(hx_run_w(&h, 17, NULL, 0, NULL) == HX_OK);
        hx_max_samples = 0;
    }

    CQ_WIDTHS(RUN_)
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    printf("%zu template symbols linked and checked; peak RSS %ld MB\n", sizeof linked / sizeof *linked,
           (long)ru.ru_maxrss / (1024 * 1024));   /* macOS reports bytes */
    return 0;
}
