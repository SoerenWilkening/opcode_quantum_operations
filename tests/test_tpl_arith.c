/* ABI §2.2 add, sub and §2.7 {u,s}{add,sub}_overflow, {u,s}{add,sub}_sat (kmgy.10): every public
 * symbol, every shape, every width, through the sampled harness in both modes, ancillae checked
 * back to |0>; aliasing, literal cost and the link check. Always asserts. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include "harness.h"
#include "cq_template.h"
#include "cqrt.h"
#include "cq_replay.h"

/* ---- References, written from the ABI text. Exact (W+1)-bit values: natively for W <= 64,
 * else bit by bit; the two agree on every narrow sample (checked in ref()). */
enum { ADD, SUB, UADDO, SADDO, USUBO, SSUBO, UADDSAT, SADDSAT, USUBSAT, SSUBSAT };
static int bitv(const hx_val *v, uint32_t i) { return (int)(v->l[i / 64] >> (i % 64) & 1); }
static void setb(hx_val *v, uint32_t i, int x) { if (x) v->l[i / 64] |= 1ull << (i % 64); }

/* a +/- b on w + 1 bits, operands extended from w bits (signed: sign-extended). */
static hx_val exact_bits(hx_val a, hx_val b, uint32_t w, int sub, int sgn) {
    hx_val r = { { 0 } };
    int c = sub;
    for (uint32_t i = 0; i <= w; i++) {
        uint32_t j = i < w ? i : w - 1;
        int x = i < w || sgn ? bitv(&a, j) : 0, y = (i < w || sgn ? bitv(&b, j) : 0) ^ sub;
        setb(&r, i, x ^ y ^ c);
        c = (x & y) | (c & (x ^ y));
    }
    return r;
}
static hx_val exact_native(hx_val a, hx_val b, uint32_t w, int sub, int sgn) {
    __int128 x = (__int128)hx_u128(a), y = (__int128)hx_u128(b);
    if (sgn && bitv(&a, w - 1)) x -= (__int128)1 << w;
    if (sgn && bitv(&b, w - 1)) y -= (__int128)1 << w;
    return hx_mask(hx_of_u128((unsigned __int128)(sub ? x - y : x + y)), w + 1);
}

static hx_val ref(int op, const hx_val *a, uint32_t w) {
    int sub = op == SUB || op == USUBO || op == SSUBO || op == USUBSAT || op == SSUBSAT;
    int sgn = op == SADDO || op == SSUBO || op == SADDSAT || op == SSUBSAT;
    hx_val r = exact_bits(a[0], a[1], w, sub, sgn);
    if (w <= 64) assert(hx_eq(r, exact_native(a[0], a[1], w, sub, sgn)));
    hx_val low = hx_mask(r, w), smax = hx_ones(w - 1), smin = hx_xor(hx_ones(w), smax);
    int top = bitv(&r, w);                              /* unsigned: carry or borrow; signed: true sign */
    int sov = top != bitv(&r, w - 1);                   /* the w+1-bit value does not fit in w bits */
    hx_val one = { { 1 } }, zero = { { 0 } };
    switch (op) {
    case ADD: case SUB: return low;
    case UADDO: case USUBO: return top ? one : zero;
    case SADDO: case SSUBO: return sov ? one : zero;
    case UADDSAT: return top ? hx_ones(w) : low;
    case USUBSAT: return top ? zero : low;
    default: return sov ? (top ? smin : smax) : low;
    }
}

/* Every qubit in the pool's free list reads 0: the kernel's ancillae came back clean. */
static void clean(void) {
    uint32_t n = cq_pool_minted() - cq_pool_live(), q[4096];
    assert(n <= 4096);
    for (uint32_t i = 0; i < n; i++) { q[i] = cq_pool_take(); assert(!cq_replay_get_bit(q[i])); }
    while (n) cq_pool_give(q[--n]);
}

/* A literal from a sample value, with garbage above bit w (only the low w bits may be read). */
#define LITN_(bits, v) ((CQRT_UINT(bits))(hx_u128(v) | ((bits) < 128 ? ~(unsigned __int128)0 << ((bits) % 128) : 0)))
#define LITW_(v) (v).l[0], (v).l[1], (v).l[2], (v).l[3] | ~0xffull

/* Harness op: forward into a fresh r, CX r into o, _unc r, free r. Harness unc: the public _unc
 * on o. FWD and UNC spell one shape's call; UNC writes to dst. ow: the result's copy suffix,
 * owb its width for the harness (0: W). */
#define SHAPE_(tag, ow, owb, bits, OPN, FWD, UNC, N) \
    static void f_##tag(int32_t o, const int32_t *a, uint32_t w) { \
        (void)w; (void)a; uint32_t r = FWD; clean(); cqrt_copy_##ow(r, (uint32_t)o); \
        { uint32_t dst = r; UNC; } cqrt_free(r); clean(); } \
    static void u_##tag(int32_t o, const int32_t *a, uint32_t w) { (void)w; (void)a; uint32_t dst = (uint32_t)o; UNC; } \
    static hx_val r_##tag(const hx_val *a, uint32_t w) { return ref(OPN, a, w); } \
    static void c_##tag(void) { const hx_op h = { #tag, 2, owb, f_##tag, u_##tag, r_##tag }; \
        hx_max_samples = (N); assert(hx_run(&h, bits, 0, NULL) == HX_OK); hx_max_samples = 0; }
/* Samples per symbol and width, to keep the suite at seconds (750 symbols; an i200 call is thousands
 * of gates): the bare forms 200 up to i64 and 40 above; a literal shape runs the same kernel with a
 * classical rail, 40. Edge values (0, 1, -1, INT_MIN, INT_MAX for each operand) always come first. */
#define BARE_SAMPLES(bits) ((bits) <= 64 ? 200 : 40)
#define LIT_SAMPLES 40
#define A0 ((uint32_t)a[0])
#define A1 ((uint32_t)a[1])
#define S0 hx_sample[0]
#define S1 hx_sample[1]
#define T_(name, n) cq_template_##name##_##n

#define BARE_(name, n, bits, ow, owb, OPN) SHAPE_(name##_##n, ow, owb, bits, OPN, \
    T_(name, n)(A0, A1), T_(name, n##_unc)(dst, A0, A1), BARE_SAMPLES(bits))
#define HLN_(name, n, bits, ow, owb, OPN) SHAPE_(name##_##n##_hl, ow, owb, bits, OPN, \
    T_(name, n##_hl)(A0, LITN_(bits, S1)), T_(name, n##_hl_unc)(dst, A0, LITN_(bits, S1)), LIT_SAMPLES)
#define LHN_(name, n, bits, ow, owb, OPN) SHAPE_(name##_##n##_lh, ow, owb, bits, OPN, \
    T_(name, n##_lh)(LITN_(bits, S0), A1), T_(name, n##_lh_unc)(dst, LITN_(bits, S0), A1), LIT_SAMPLES)
#define HLW_(name, n, bits, ow, owb, OPN) SHAPE_(name##_##n##_hl, ow, owb, bits, OPN, \
    T_(name, n##_hl)(A0, LITW_(S1)), T_(name, n##_hl_unc)(dst, A0, LITW_(S1)), LIT_SAMPLES)
#define LHW_(name, n, bits, ow, owb, OPN) SHAPE_(name##_##n##_lh, ow, owb, bits, OPN, \
    T_(name, n##_lh)(LITW_(S0), A1), T_(name, n##_lh_unc)(dst, LITW_(S0), A1), LIT_SAMPLES)

/* X(name, OP, flag result, commutative, ...): the ops of this issue (ABI §2.1: _lh only when
 * not commutative). */
#define OPS_(X, n, bits, A, B) \
    X(add, ADD, 0, 1, n, bits, A, B) X(sub, SUB, 0, 0, n, bits, A, B) \
    X(uadd_overflow, UADDO, 1, 1, n, bits, A, B) X(sadd_overflow, SADDO, 1, 1, n, bits, A, B) \
    X(usub_overflow, USUBO, 1, 0, n, bits, A, B) X(ssub_overflow, SSUBO, 1, 0, n, bits, A, B) \
    X(uadd_sat, UADDSAT, 0, 1, n, bits, A, B) X(sadd_sat, SADDSAT, 0, 1, n, bits, A, B) \
    X(usub_sat, USUBSAT, 0, 0, n, bits, A, B) X(ssub_sat, SSUBSAT, 0, 0, n, bits, A, B)
#define DEF_OP_(name, OPN, flag, comm, n, bits, HL, LH) DEF_OP2_##flag(name, OPN, comm, n, bits, HL, LH)
#define DEF_OP2_0(name, OPN, comm, n, bits, HL, LH) DEF_OP3_(name, OPN, comm, n, bits, HL, LH, n, 0)
#define DEF_OP2_1(name, OPN, comm, n, bits, HL, LH) DEF_OP3_(name, OPN, comm, n, bits, HL, LH, i1, 1)
#define DEF_OP3_(name, OPN, comm, n, bits, HL, LH, ow, owb) \
    BARE_(name, n, bits, ow, owb, OPN) HL(name, n, bits, ow, owb, OPN) LH_IF_##comm(LH, name, n, bits, ow, owb, OPN)
#define LH_IF_1(LH, name, n, bits, ow, owb, OPN)
#define LH_IF_0(LH, name, n, bits, ow, owb, OPN) LH(name, n, bits, ow, owb, OPN)
#define DEF_NARROW_(n, bits) OPS_(DEF_OP_, n, bits, HLN_, LHN_)
#define DEF_WIDE_(n, bits) OPS_(DEF_OP_, n, bits, HLW_, LHW_)
CQ_WIDTHS_NARROW(DEF_NARROW_)
CQ_WIDTHS_WIDE(DEF_WIDE_)

/* Running all of them, and the link check: every generated name, forward and _unc. */
#define RUN_OP_(name, OPN, flag, comm, n, bits, A, B) c_##name##_##n(); c_##name##_##n##_hl(); RUN_LH_##comm(name, n)
#define RUN_LH_1(name, n)
#define RUN_LH_0(name, n) c_##name##_##n##_lh();
#define RUN_(n, bits) OPS_(RUN_OP_, n, bits, , ) printf("i%u: ok\n", bits); fflush(stdout);
typedef void (*fn)(void);
#define ADDR_OP_(name, OPN, flag, comm, n, bits, A, B) (fn)T_(name, n), (fn)T_(name, n##_unc), \
    (fn)T_(name, n##_hl), (fn)T_(name, n##_hl_unc), ADDR_LH_##comm(name, n)
#define ADDR_LH_1(name, n)
#define ADDR_LH_0(name, n) (fn)T_(name, n##_lh), (fn)T_(name, n##_lh_unc),
#define ADDR_(n, bits) OPS_(ADDR_OP_, n, bits, , )
static const fn linked[] = { CQ_WIDTHS(ADDR_) };

/* A rail of w bits holding v, forced to qubits when quantum. */
static uint32_t rail(uint64_t v, uint32_t w, int quantum) {
    int32_t r = cq_rail_alloc(w, (const uint64_t[4]){ v, 0, 0, 0 });
    cq_bit *b = cq_rail_bits(r);
    for (uint32_t i = 0; quantum && i < w; i++) { cq_get_qubit(&b[i]); cq_replay_set_bit(b[i].q, (int)(v >> i & 1)); }
    return (uint32_t)r;
}

int main(void) {
    assert(sizeof linked / sizeof *linked == 30 * 2 * 25);
    for (unsigned i = 0; i < sizeof linked / sizeof *linked; i++) assert(linked[i]);

    /* Aliasing (ABI §0): one rail in both operand slots. a + a = 2a, a - a = 0, borrow 0. */
    for (int quantum = 1; quantum >= 0; quantum--) {
        uint32_t a = rail(0xb5, 8, quantum);
        uint32_t s = cq_template_add_i8(a, a), d = cq_template_sub_i8(a, a), f = cq_template_usub_overflow_i8(a, a);
        assert((uint8_t)cqrt_measure_i8(s) == 0x6a && cqrt_measure_i8(d) == 0 && cqrt_measure_i1(f) == 0);
        assert((uint8_t)cqrt_measure_i8(a) == 0xb5);
        cqrt_free(s); cqrt_free(d); cqrt_free(f); cqrt_free(a);
    }

    /* neg is sub_lh with 0; and a literal operand folds: cheaper than two quantum operands, and an
     * all-classical call (no handle quantum) takes no qubit and emits no gate. */
    {
        uint32_t base = cq_pool_live();
        cq_replay_reset();   /* before the inputs: a reset clears every bit */
        uint32_t a = rail(5, 32, 1), b = rail(9, 32, 1);
        uint64_t g0 = cq_replay_gates();
        uint32_t s = cq_template_sub_i32(a, b);
        uint64_t both = cq_replay_gates() - g0, g1 = cq_replay_gates();
        uint32_t t = cq_template_sub_i32_hl(a, 9);
        uint64_t one = cq_replay_gates() - g1;
        uint32_t n = cq_template_sub_i32_lh(0, a);
        assert(one < both);
        assert((uint32_t)cqrt_measure_i32(s) == (uint32_t)-4 && (uint32_t)cqrt_measure_i32(t) == (uint32_t)-4);
        assert((uint32_t)cqrt_measure_i32(n) == (uint32_t)-5);
        cqrt_free(s); cqrt_free(t); cqrt_free(n); cqrt_free(a); cqrt_free(b);
        uint32_t c = cqrt_alloc_i200(1, 2, 3, 4);
        cq_replay_reset();
        uint32_t m = cq_template_ssub_sat_i200_lh(7, 0, 0, 0, c);
        uint32_t k = cq_template_sadd_overflow_i200_hl(c, ~0ull, ~0ull, ~0ull, ~0ull);
        assert(cq_replay_gates() == 0);
        cqrt_u256 v = cqrt_measure_i200(m);
        assert(v.l[0] == 6 && v.l[1] == (uint64_t)-2 && v.l[2] == (uint64_t)-4 && v.l[3] == 0xfb);   /* 7 - c mod 2^200 */
        assert(cqrt_measure_i1(k) == 0);
        cqrt_free(m); cqrt_free(k); cqrt_free(c);
        assert(cq_pool_live() == base);
    }

    CQ_WIDTHS(RUN_)
    printf("%zu template symbols linked and checked\n", sizeof linked / sizeof *linked);
    return 0;
}
