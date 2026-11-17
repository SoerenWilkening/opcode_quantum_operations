/* ABI §2.3 and, or, xor, shl, lshr, ashr, fshl, fshr (kmgy.13): every public symbol at every
 * width through the sampled harness in both modes; the shift amount also at 0, 1, W-1, W, W+1,
 * 2W and 2^W-1 (CR §2: amount mod W); the cost of the literal shapes; the link check.
 * Literals carry garbage above bit W-1 (only the low W bits may be read). Always asserts. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "harness.h"
#include "cq_template.h"
#include "cq_replay.h"

static const hx_val zero;

/* ---- References on limb values, from the ABI §2.3 text, not from the kernels. */
static int bit(hx_val v, uint32_t i) { return (int)(v.l[i / 64] >> (i % 64) & 1); }
static void set(hx_val *v, uint32_t i) { v->l[i / 64] |= 1ull << (i % 64); }
static hx_val or_(hx_val a, hx_val b) { for (uint32_t j = 0; j < CQ_LIMBS; j++) a.l[j] |= b.l[j]; return a; }

/* ⟦k⟧ mod w by long division over the 64-bit limbs, high first */
static uint32_t amt(hx_val k, uint32_t w) {
    unsigned __int128 r = 0;
    for (int j = CQ_LIMBS; j--;) r = ((r << 64) | k.l[j]) % w;
    return (uint32_t)r;
}
static hx_val shl_by(hx_val a, uint32_t r, uint32_t w) {
    hx_val o = zero;
    for (uint32_t i = r; i < w; i++) if (bit(a, i - r)) set(&o, i);
    return o;
}
static hx_val shr_by(hx_val a, uint32_t r, uint32_t w, int arith) {
    hx_val o = zero;
    for (uint32_t i = 0; i < w; i++) if (i + r < w ? bit(a, i + r) : arith && bit(a, w - 1)) set(&o, i);
    return o;
}
static hx_val and_ref(const hx_val *a, uint32_t w) { (void)w; return hx_and(a[0], a[1]); }
static hx_val or_ref(const hx_val *a, uint32_t w) { (void)w; return or_(a[0], a[1]); }
static hx_val xor_ref(const hx_val *a, uint32_t w) { (void)w; return hx_xor(a[0], a[1]); }
static hx_val shl_ref(const hx_val *a, uint32_t w) { return shl_by(a[0], amt(a[1], w), w); }
static hx_val lshr_ref(const hx_val *a, uint32_t w) { return shr_by(a[0], amt(a[1], w), w, 0); }
static hx_val ashr_ref(const hx_val *a, uint32_t w) { return shr_by(a[0], amt(a[1], w), w, 1); }
/* ABI §2.3: k = s mod W; k = 0 gives a (fshl) or b (fshr); else (a << k) | (b >> (W - k)),
 * for fshr (a << (W - k)) | (b >> k). */
static hx_val fshl_ref(const hx_val *a, uint32_t w) {
    uint32_t k = amt(a[2], w);
    return k ? or_(shl_by(a[0], k, w), shr_by(a[1], w - k, w, 0)) : a[0];
}
static hx_val fshr_ref(const hx_val *a, uint32_t w) {
    uint32_t k = amt(a[2], w);
    return k ? or_(shl_by(a[0], w - k, w), shr_by(a[1], k, w, 0)) : a[1];
}

/* ---- One symbol: its forward and _unc on handles h and values v (literal slots read v). */
typedef uint32_t (*fwd_fn)(const int32_t *h, const hx_val *v);
typedef void (*unc_fn)(uint32_t o, const int32_t *h, const hx_val *v);
typedef struct { const char *name; uint32_t w; int nargs, lits; hx_ref_fn ref; fwd_fn fwd; unc_fn unc; } sym;

/* A literal of width bits as its C type, with ones above bit bits-1 that must be ignored. */
#define NL_(bits, j) ((CQRT_UINT(bits))(hx_u128(v[j]) | ((bits) < 128 ? ~(unsigned __int128)0 << ((bits) % 128) : 0)))
#define WL_(bits, j) v[j].l[0], v[j].l[1], v[j].l[2], v[j].l[3] | ~0ull << ((bits) % 64)
#define ARG_q_(L, bits, j) (uint32_t)h[j]
#define ARG_l_(L, bits, j) L(bits, j)
#define MASK_q_ 0
#define MASK_l_ 1
/* symbol cq_template_<op>_<n><sfx>, operand letters s0 s1 [s2] */
#define SYM2_(op, n, bits, L, s0, s1, sfx) \
    static uint32_t f_##op##_##n##sfx(const int32_t *h, const hx_val *v) { (void)h; (void)v; \
        return cq_template_##op##_##n##sfx(ARG_##s0##_(L, bits, 0), ARG_##s1##_(L, bits, 1)); } \
    static void u_##op##_##n##sfx(uint32_t o, const int32_t *h, const hx_val *v) { (void)h; (void)v; \
        cq_template_##op##_##n##sfx##_unc(o, ARG_##s0##_(L, bits, 0), ARG_##s1##_(L, bits, 1)); }
#define SYM3_(op, n, bits, L, s0, s1, s2, sfx) \
    static uint32_t f_##op##_##n##sfx(const int32_t *h, const hx_val *v) { (void)h; (void)v; \
        return cq_template_##op##_##n##sfx(ARG_##s0##_(L, bits, 0), ARG_##s1##_(L, bits, 1), ARG_##s2##_(L, bits, 2)); } \
    static void u_##op##_##n##sfx(uint32_t o, const int32_t *h, const hx_val *v) { (void)h; (void)v; \
        cq_template_##op##_##n##sfx##_unc(o, ARG_##s0##_(L, bits, 0), ARG_##s1##_(L, bits, 1), ARG_##s2##_(L, bits, 2)); }
#define ROW2_(op, n, bits, s0, s1, sfx) { "cq_template_" #op "_" #n #sfx, bits, 2, MASK_##s0##_ | MASK_##s1##_ << 1, op##_ref, f_##op##_##n##sfx, u_##op##_##n##sfx },
#define ROW3_(op, n, bits, s0, s1, s2, sfx) { "cq_template_" #op "_" #n #sfx, bits, 3, \
    MASK_##s0##_ | MASK_##s1##_ << 1 | MASK_##s2##_ << 2, op##_ref, f_##op##_##n##sfx, u_##op##_##n##sfx },

/* The shapes, per width: X2(op, s0, s1, sfx), X3(op, s0, s1, s2, sfx). */
#define SHAPES_(X2, X3, ...) \
    X2(__VA_ARGS__, and, q, q, ) X2(__VA_ARGS__, and, q, l, _hl) \
    X2(__VA_ARGS__, or, q, q, ) X2(__VA_ARGS__, or, q, l, _hl) \
    X2(__VA_ARGS__, xor, q, q, ) X2(__VA_ARGS__, xor, q, l, _hl) \
    X2(__VA_ARGS__, shl, q, q, ) X2(__VA_ARGS__, shl, q, l, _hl) X2(__VA_ARGS__, shl, l, q, _lh) \
    X2(__VA_ARGS__, lshr, q, q, ) X2(__VA_ARGS__, lshr, q, l, _hl) X2(__VA_ARGS__, lshr, l, q, _lh) \
    X2(__VA_ARGS__, ashr, q, q, ) X2(__VA_ARGS__, ashr, q, l, _hl) X2(__VA_ARGS__, ashr, l, q, _lh) \
    CQ_TPL_TERN_SHAPES(X3, __VA_ARGS__, fshl) CQ_TPL_TERN_SHAPES(X3, __VA_ARGS__, fshr)
/* argument order adapters: SHAPES_ passes (n, bits, L, op, s...) */
#define DEF2_(n, bits, L, op, s0, s1, sfx) SYM2_(op, n, bits, L, s0, s1, sfx)
#define DEF3_(n, bits, L, op, s0, s1, s2, sfx) SYM3_(op, n, bits, L, s0, s1, s2, sfx)
#define TAB2_(n, bits, L, op, s0, s1, sfx) ROW2_(op, n, bits, s0, s1, sfx)
#define TAB3_(n, bits, L, op, s0, s1, s2, sfx) ROW3_(op, n, bits, s0, s1, s2, sfx)
#define DEF_NARROW_(n, bits) SHAPES_(DEF2_, DEF3_, n, bits, NL_)
#define DEF_WIDE_(n, bits) SHAPES_(DEF2_, DEF3_, n, bits, WL_)
#define TAB_(n, bits) SHAPES_(TAB2_, TAB3_, n, bits, NL_)
CQ_WIDTHS_NARROW(DEF_NARROW_)
CQ_WIDTHS_WIDE(DEF_WIDE_)
static const sym syms[] = { CQ_WIDTHS(TAB_) };
#define NSYMS (sizeof syms / sizeof *syms)

/* ---- Harness glue. cur: the symbol under test. fixed: the amount (last operand) is K, not
 * sampled: the harness op then has one operand less and the amount rail is made here. */
static const sym *cur;
static int fixed;
static hx_val K;

static int32_t expand(const int32_t *a, int32_t *h, hx_val *v) {
    int n = cur->nargs - fixed;
    for (int j = 0; j < n; j++) { h[j] = a[j]; v[j] = hx_sample[j]; }
    if (!fixed) return 0;
    v[n] = K;
    h[n] = hx_to_rail(K, cur->w);
    int quantum = cq_bit_is_q(cq_rail_bits(a[0])[0]);
    cq_bit *kb = cq_rail_bits(h[n]);
    for (uint32_t i = 0; quantum && i < cur->w; i++) cq_get_qubit(&kb[i]);
    return h[n];
}
/* Forward into a fresh r, r into o, _unc r: r must measure 0. */
static void fwd_op(int32_t o, const int32_t *a, uint32_t w) {
    int32_t h[3]; hx_val v[3];
    int32_t tmp = expand(a, h, v);
    int32_t r = (int32_t)cur->fwd(h, v);
    assert(cq_rail_width(r) == w);
    cq_bit *rb = cq_rail_bits(r), *ob = cq_rail_bits(o);
    for (uint32_t i = 0; i < w; i++) cq_cx(&rb[i], &ob[i]);
    cur->unc((uint32_t)r, h, v);
    assert(hx_eq(hx_from_rail(r), zero));
    cq_rail_free(r);
    if (tmp) cq_rail_free(tmp);   /* not measured: m returns a 1 qubit to the pool as it is */
}
static void unc_op(int32_t o, const int32_t *a, uint32_t w) {
    (void)w;
    int32_t h[3]; hx_val v[3];
    int32_t tmp = expand(a, h, v);
    cur->unc((uint32_t)o, h, v);
    if (tmp) cq_rail_free(tmp);
}
static hx_val ref_op(const hx_val *a, uint32_t w) {
    hx_val v[3];
    memcpy(v, a, (size_t)(cur->nargs - fixed) * sizeof *v);
    if (fixed) v[cur->nargs - 1] = K;
    return cur->ref(v, w);
}
static void check(const sym *s) {
    cur = s;
    const hx_op op = { s->name, s->nargs - fixed, 0, fwd_op, unc_op, ref_op };
    assert(hx_run(&op, s->w, 0, NULL) == HX_OK);
}
static int is_shift(const sym *s) { return strstr(s->name, "sh") != NULL; }   /* shl lshr ashr fshl fshr */

/* ---- Cost of the literal shapes, forced-quantum value, literal from v: the gates of one forward. */
static void cost_lit(const sym *s, hx_val lit, int lit_slot, uint32_t max_depth) {
    cq_replay_reset();
    uint32_t base = cq_pool_live();
    const hx_val val = hx_mask((hx_val){ { 0x0123456789abcdefull, 0xfedcba9876543210ull, 0x5a5a5a5a5a5a5a5aull, 0xa5a5ull } }, s->w);
    int32_t h[3] = { 0 }; hx_val v[3] = { val, val, val };
    v[lit_slot] = lit;
    for (int j = 0; j < s->nargs; j++) {
        if (s->lits >> j & 1) continue;
        h[j] = hx_to_rail(zero, s->w);
        cq_bit *b = cq_rail_bits(h[j]);
        for (uint32_t i = 0; i < s->w; i++) { cq_get_qubit(&b[i]); cq_replay_set_bit(b[i].q, bit(val, i)); }
    }
    uint32_t r = s->fwd(h, v);
    if (cq_replay_depth() > max_depth || cq_replay_gates_of(CQ_G_CCX)) {
        fprintf(stderr, "%s: depth %u > %u or %llu CCX\n", s->name, cq_replay_depth(), max_depth,
                (unsigned long long)cq_replay_gates_of(CQ_G_CCX));
        abort();
    }
    s->unc(r, h, v);
    assert(hx_eq(hx_from_rail((int32_t)r), zero));
    cq_rail_free((int32_t)r);
    for (int j = 0; j < s->nargs; j++) if (h[j]) cq_rail_free(h[j]);
    assert(cq_pool_live() == base);
}

/* The link check: every generated name, forward and _unc. */
typedef void (*fn)(void);
#define ADDR2_(n, bits, L, op, s0, s1, sfx) (fn)cq_template_##op##_##n##sfx, (fn)cq_template_##op##_##n##sfx##_unc,
#define ADDR3_(n, bits, L, op, s0, s1, s2, sfx) ADDR2_(n, bits, L, op, s0, s1, sfx)
#define ADDR_(n, bits) SHAPES_(ADDR2_, ADDR3_, n, bits, NL_)
static const fn linked[] = { CQ_WIDTHS(ADDR_) };

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc > 1 && !strcmp(argv[1], "--count")) { printf("%zu\n", sizeof linked / sizeof *linked); return 0; }
    assert(sizeof linked / sizeof *linked == 2 * NSYMS);
    for (size_t i = 0; i < sizeof linked / sizeof *linked; i++) assert(linked[i]);

    for (size_t i = 0; i < NSYMS; i++) {
        const sym *s = &syms[i];
        /* a quantum amount runs the barrel (and at most widths the mod-W reduction): fewer samples */
        int barrel = is_shift(s) && !(s->lits >> (s->nargs - 1) & 1);
        hx_max_samples = !barrel ? (s->w > 16 ? 200 : 0) : s->w <= 16 ? 40 : s->w <= 32 ? 20 : s->w <= 128 ? 8 : 4;
        fixed = 0;
        check(s);
        if (is_shift(s)) {
            /* the amount at the edges of the totalization, the value still sampled */
            hx_max_samples = barrel ? 2 : 10;
            fixed = 1;
            uint32_t w = s->w;
            const uint64_t ks[] = { 0, 1, w - 1, w, w + 1, 2 * w };
            for (size_t j = 0; j <= sizeof ks / sizeof *ks; j++) {
                K = j < sizeof ks / sizeof *ks ? hx_mask((hx_val){ { ks[j] } }, w) : hx_ones(w);
                check(s);
            }
        }
        /* literal-amount shapes (and the bitwise _hl): CX/X wiring, depth 1 (xor 2; ashr's sign fan-out
         * 2 ceil(log2 W) + 1 at most) */
        if (s->lits == 1 << (s->nargs - 1)) {
            uint32_t d = strstr(s->name, "ashr") ? 2 * 8 + 1 : strstr(s->name, "xor") ? 2 : 1;
            for (uint64_t k = 0; k < 3; k++) cost_lit(s, hx_mask((hx_val){ { k * (s->w / 2 + 1) } }, s->w), s->nargs - 1, d);
        }
        printf("%s[_unc]: ok\n", s->name);
    }
    printf("%zu template symbols linked and checked\n", sizeof linked / sizeof *linked);
    return 0;
}
