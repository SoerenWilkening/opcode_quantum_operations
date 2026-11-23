/* ABI §2.4 icmp_<pred>, §2.5 select, §2.7 smax smin umax umin abs (kmgy.14): every public symbol,
 * every shape, every width, through the sampled harness in both modes, ancillae checked back to |0>;
 * pinned edge pairs (equal, adjacent, differing only in the sign bit, only in the top or bottom limb);
 * the inverted call; the internal flag and select API with recorded targets; the link check.
 * Always asserts. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include "harness.h"
#include "cq_template.h"
#include "cqrt.h"
#include "cq_replay.h"
#include "kernels.h"

/* ---- References, from the ABI text: LLVM icmp, select, llvm.smax/smin/umax/umin, llvm.abs with
 * abs(INT_MIN) = INT_MIN. Limb by limb with the sign read first; for W <= 128 also natively on
 * sign-extended __int128, and the two must agree. */
enum { EQ, NE, UGT, UGE, ULT, ULE, SGT, SGE, SLT, SLE, SMAX, SMIN, UMAX, UMIN, ABS, SEL };
static int bitv(const hx_val *v, uint32_t i) { return (int)(v->l[i / 64] >> (i % 64) & 1); }
static __int128 sx(hx_val v, uint32_t w) {   /* v as a signed w-bit value, w <= 128 */
    unsigned __int128 u = hx_u128(v);
    return w < 128 && bitv(&v, w - 1) ? (__int128)(u - ((unsigned __int128)1 << w)) : (__int128)u;
}
static int lt(hx_val x, hx_val y, uint32_t w, int sgn) {
    int r = 0, sxb = bitv(&x, w - 1), syb = bitv(&y, w - 1);
    if (sgn && sxb != syb) r = sxb;
    else for (uint32_t k = CQ_LIMBS; k--;) if (x.l[k] != y.l[k]) { r = x.l[k] < y.l[k]; break; }
    if (w <= 128) assert(r == (sgn ? sx(x, w) < sx(y, w) : hx_u128(x) < hx_u128(y)));
    return r;
}
static hx_val neg(hx_val v, uint32_t w) {
    hx_val r = hx_xor(v, hx_ones(w));
    for (uint32_t k = 0; k < CQ_LIMBS && !++r.l[k]; k++) {}
    r = hx_mask(r, w);
    if (w <= 128) assert(hx_eq(r, hx_mask(hx_of_u128((unsigned __int128)0 - hx_u128(v)), w)));
    return r;
}
static hx_val flag(int x) { return (hx_val){ { (uint64_t)x } }; }
static hx_val ref(int op, const hx_val *a, uint32_t w) {
    hx_val x = a[0], y = a[1];
    switch (op) {
    case EQ: return flag(hx_eq(x, y));
    case NE: return flag(!hx_eq(x, y));
    case UGT: return flag(lt(y, x, w, 0));
    case UGE: return flag(!lt(x, y, w, 0));
    case ULT: return flag(lt(x, y, w, 0));
    case ULE: return flag(!lt(y, x, w, 0));
    case SGT: return flag(lt(y, x, w, 1));
    case SGE: return flag(!lt(x, y, w, 1));
    case SLT: return flag(lt(x, y, w, 1));
    case SLE: return flag(!lt(y, x, w, 1));
    case SMAX: return lt(x, y, w, 1) ? y : x;
    case SMIN: return lt(x, y, w, 1) ? x : y;
    case UMAX: return lt(x, y, w, 0) ? y : x;
    case UMIN: return lt(x, y, w, 0) ? x : y;
    case ABS: return bitv(&x, w - 1) ? neg(x, w) : x;
    default: return a[0].l[0] & 1 ? a[1] : a[2];   /* SEL: (c, arm1, arm2) */
    }
}

/* Pinned pairs: the second operand derived from the first, u = T_k(v); edge_k picks k. */
static int edge_k;
static hx_val edge_of(hx_val v, uint32_t w) {
    hx_val u = v;
    uint64_t mix = (v.l[0] ^ v.l[CQ_LIMBS - 1] ^ w) * 0x9e3779b97f4a7c15ull + 0x632be59bd9b4e019ull;
    switch (edge_k) {
    case 0: break;                                                       /* equal */
    case 1: for (uint32_t k = 0; k < CQ_LIMBS && !++u.l[k]; k++) {} break;   /* v + 1, wraps */
    case 2: for (uint32_t k = 0; k < CQ_LIMBS && !u.l[k]--; k++) {} break;   /* v - 1, wraps */
    case 3: u.l[(w - 1) / 64] ^= 1ull << ((w - 1) % 64); break;          /* only the sign bit */
    case 4: u.l[(w - 1) / 64] = mix; break;                              /* only the top limb */
    default: u.l[0] = mix; break;                                        /* only the bottom limb */
    }
    return hx_mask(u, w);
}

/* Every qubit in the pool's free list reads 0: the kernel's ancillae came back clean. */
static void clean(void) {
    uint32_t n = cq_pool_minted() - cq_pool_live(), q[8192];
    assert(n <= 8192);
    for (uint32_t i = 0; i < n; i++) { q[i] = cq_pool_take(); assert(!cq_replay_get_bit(q[i])); }
    while (n) cq_pool_give(q[--n]);
}

/* A rail of w bits holding v, quantum like the sample's rail h. */
static uint32_t rail_like(int32_t h, hx_val v, uint32_t w) {
    int32_t r = hx_to_rail(v, w);
    cq_bit *b = cq_rail_bits(r);
    for (uint32_t i = 0; cq_bit_is_q(cq_rail_bits(h)[0]) && i < w; i++) { cq_get_qubit(&b[i]); cq_replay_set_bit(b[i].q, bitv(&v, i)); }
    return (uint32_t)r;
}

/* A literal from a value, with garbage above bit w (only the low w bits may be read). */
#define LITN_(bits, v) ((CQRT_UINT(bits))(hx_u128(v) | ((bits) < 128 ? ~(unsigned __int128)0 << ((bits) % 128) : 0)))
#define LITW_(v) (v).l[0], (v).l[1], (v).l[2], (v).l[3] | ~0xffull

/* Harness op: PRE, forward into a fresh r, CX r into o, _unc r, free r, POST. Harness unc: the public
 * _unc on o. FWD and UNC spell one shape's call; UNC writes to dst. ow: the result's copy suffix,
 * owb its width for the harness (0: W); AW the operand widths (NULL: all W). */
#define SHAPE_(tag, ow, owb, bits, nargs, AW, REFX, PRE, FWD, UNC, POST, N) \
    static void f_##tag(int32_t o, const int32_t *a, uint32_t w) { \
        (void)w; (void)a; PRE; uint32_t r = FWD; clean(); cqrt_copy_##ow(r, (uint32_t)o); \
        { uint32_t dst = r; UNC; } cqrt_free(r); POST; clean(); } \
    static void u_##tag(int32_t o, const int32_t *a, uint32_t w) { (void)w; (void)a; PRE; { uint32_t dst = (uint32_t)o; UNC; } POST; } \
    static hx_val r_##tag(const hx_val *a, uint32_t w) { REFX; } \
    static void c_##tag(void) { const hx_op h = { #tag, nargs, owb, f_##tag, u_##tag, r_##tag }; \
        hx_max_samples = (N); assert(hx_run_w(&h, bits, AW, 0, NULL) == HX_OK); hx_max_samples = 0; }
/* Samples per symbol and width, to keep the suite at seconds (990 symbols; an i200 compare is
 * thousands of gates): bare forms 60 up to i64 and 20 above, literal shapes 20; pinned pairs 10 per
 * (op, shape, pair) up to i64 (the first operand 0, 1, -1, INT_MIN, INT_MAX) and 4 above (0, 1).
 * Edge values (0, 1, -1, INT_MIN, INT_MAX for each operand) always come first. */
#define BARE_SAMPLES(bits) ((bits) <= 64 ? 60 : 20)
#define LIT_SAMPLES 20
#define EDGE_SAMPLES(bits) ((bits) <= 64 ? 10 : 4)
#define A0 ((uint32_t)a[0])
#define A1 ((uint32_t)a[1])
#define A2 ((uint32_t)a[2])
#define S1 hx_sample[1]
#define S2 hx_sample[2]
#define T_(name, n) cq_template_##name##_##n
#define REF2_(OPN) return ref(OPN, a, w)
#define REFE_(OPN) const hx_val v[2] = { a[0], edge_of(a[0], w) }; return ref(OPN, v, w)
#define NONE_ (void)0
static const uint32_t cond_w[HX_MAX_ARGS] = { 1 };

/* Binary ops (icmp, min/max): bare, _hl, and both again on pinned pairs. */
#define BIN_(name, n, bits, ow, owb, OPN, LIT) \
    SHAPE_(name##_##n, ow, owb, bits, 2, NULL, REF2_(OPN), NONE_, \
        T_(name, n)(A0, A1), T_(name, n##_unc)(dst, A0, A1), NONE_, BARE_SAMPLES(bits)) \
    SHAPE_(name##_##n##_hl, ow, owb, bits, 2, NULL, REF2_(OPN), NONE_, \
        T_(name, n##_hl)(A0, LIT(bits, S1)), T_(name, n##_hl_unc)(dst, A0, LIT(bits, S1)), NONE_, LIT_SAMPLES) \
    SHAPE_(name##_##n##_e, ow, owb, bits, 1, NULL, REFE_(OPN), \
        hx_val u = edge_of(hx_sample[0], bits); uint32_t b = rail_like(a[0], u, bits), \
        T_(name, n)(A0, b), T_(name, n##_unc)(dst, A0, b), cqrt_free(b), EDGE_SAMPLES(bits)) \
    SHAPE_(name##_##n##_hl_e, ow, owb, bits, 1, NULL, REFE_(OPN), hx_val u = edge_of(hx_sample[0], bits), \
        T_(name, n##_hl)(A0, LIT(bits, u)), T_(name, n##_hl_unc)(dst, A0, LIT(bits, u)), NONE_, EDGE_SAMPLES(bits))
#define LN_(bits, v) LITN_(bits, v)
#define LW_(bits, v) LITW_(v)
/* select (c, arm1, arm2): c is i1. At i200, _ll's forward form has the unused r9 slot before arm2. */
#define SEL_(n, bits, LIT, PAD) \
    SHAPE_(select_##n, n, 0, bits, 3, cond_w, REF2_(SEL), NONE_, \
        T_(select, n)(A0, A1, A2), T_(select, n##_unc)(dst, A0, A1, A2), NONE_, BARE_SAMPLES(bits)) \
    SHAPE_(select_##n##_hl, n, 0, bits, 3, cond_w, REF2_(SEL), NONE_, \
        T_(select, n##_hl)(A0, A1, LIT(bits, S2)), T_(select, n##_hl_unc)(dst, A0, A1, LIT(bits, S2)), NONE_, LIT_SAMPLES) \
    SHAPE_(select_##n##_lh, n, 0, bits, 3, cond_w, REF2_(SEL), NONE_, \
        T_(select, n##_lh)(A0, LIT(bits, S1), A2), T_(select, n##_lh_unc)(dst, A0, LIT(bits, S1), A2), NONE_, LIT_SAMPLES) \
    SHAPE_(select_##n##_ll, n, 0, bits, 3, cond_w, REF2_(SEL), NONE_, \
        T_(select, n##_ll)(A0, LIT(bits, S1), PAD LIT(bits, S2)), \
        T_(select, n##_ll_unc)(dst, A0, LIT(bits, S1), LIT(bits, S2)), NONE_, LIT_SAMPLES)
#define ABS_(n, bits) SHAPE_(abs_##n, n, 0, bits, 1, NULL, REF2_(ABS), NONE_, \
    T_(abs, n)(A0), T_(abs, n##_unc)(dst, A0), NONE_, BARE_SAMPLES(bits))

/* X(name, OP, flag result): the binary ops of this issue. */
#define OPS_(X, ...) \
    X(icmp_eq, EQ, 1, __VA_ARGS__) X(icmp_ne, NE, 1, __VA_ARGS__) X(icmp_ugt, UGT, 1, __VA_ARGS__) \
    X(icmp_uge, UGE, 1, __VA_ARGS__) X(icmp_ult, ULT, 1, __VA_ARGS__) X(icmp_ule, ULE, 1, __VA_ARGS__) \
    X(icmp_sgt, SGT, 1, __VA_ARGS__) X(icmp_sge, SGE, 1, __VA_ARGS__) X(icmp_slt, SLT, 1, __VA_ARGS__) \
    X(icmp_sle, SLE, 1, __VA_ARGS__) \
    X(smax, SMAX, 0, __VA_ARGS__) X(smin, SMIN, 0, __VA_ARGS__) X(umax, UMAX, 0, __VA_ARGS__) X(umin, UMIN, 0, __VA_ARGS__)
#define DEF_OP_(name, OPN, fl, n, bits, LIT) DEF_OP2_##fl(name, OPN, n, bits, LIT)
#define DEF_OP2_0(name, OPN, n, bits, LIT) BIN_(name, n, bits, n, 0, OPN, LIT)
#define DEF_OP2_1(name, OPN, n, bits, LIT) BIN_(name, n, bits, i1, 1, OPN, LIT)
#define PADN_
#define PADW_ 0,
#define DEF_NARROW_(n, bits) OPS_(DEF_OP_, n, bits, LN_) SEL_(n, bits, LN_, PADN_) ABS_(n, bits)
#define DEF_WIDE_(n, bits) OPS_(DEF_OP_, n, bits, LW_) SEL_(n, bits, LW_, PADW_) ABS_(n, bits)
CQ_WIDTHS_NARROW(DEF_NARROW_)
CQ_WIDTHS_WIDE(DEF_WIDE_)

/* Running all of them, and the link check: every generated name, forward and _unc. */
#define RUN_OP_(name, OPN, fl, n, bits) c_##name##_##n(); c_##name##_##n##_hl(); \
    for (edge_k = 0; edge_k < 6; edge_k++) { c_##name##_##n##_e(); c_##name##_##n##_hl_e(); }
#define RUN_(n, bits) OPS_(RUN_OP_, n, bits) \
    c_select_##n(); c_select_##n##_hl(); c_select_##n##_lh(); c_select_##n##_ll(); c_abs_##n(); \
    printf("i%u: ok\n", bits); fflush(stdout);
typedef void (*fn)(void);
#define ADDR_OP_(name, OPN, fl, n, bits) (fn)T_(name, n), (fn)T_(name, n##_unc), (fn)T_(name, n##_hl), (fn)T_(name, n##_hl_unc),
#define ADDR_(n, bits) OPS_(ADDR_OP_, n, bits) (fn)T_(abs, n), (fn)T_(abs, n##_unc), \
    (fn)T_(select, n), (fn)T_(select, n##_unc), (fn)T_(select, n##_hl), (fn)T_(select, n##_hl_unc), \
    (fn)T_(select, n##_lh), (fn)T_(select, n##_lh_unc), (fn)T_(select, n##_ll), (fn)T_(select, n##_ll_unc),
static const fn linked[] = { CQ_WIDTHS(ADDR_) };

/* cq_invert (ABI §1.13): X; invert{X} and invert{X}; X leave o as it was, both modes. */
static const hx_val zero_;
static hx_val ref_zero(const hx_val *a, uint32_t w) { (void)a; (void)w; return zero_; }
#define INV_(tag, owb, bits, nargs, AW, X) \
    static void fi_##tag(int32_t o, const int32_t *a, uint32_t w) { (void)w; (void)a; X; cqrt_invert_begin(); X; cqrt_invert_end(); } \
    static void if_##tag(int32_t o, const int32_t *a, uint32_t w) { (void)w; (void)a; cqrt_invert_begin(); X; cqrt_invert_end(); X; } \
    static void inv_##tag(void) { const hx_op h = { #tag " inverted", nargs, owb, fi_##tag, if_##tag, ref_zero }; \
        hx_max_samples = 40; assert(hx_run_w(&h, bits, AW, 0, NULL) == HX_OK); hx_max_samples = 0; }
#define INVS_(n, bits, LIT) \
    INV_(slt_hl_##n, 1, bits, 1, NULL, T_(icmp_slt, n##_hl_unc)((uint32_t)o, A0, LIT(bits, hx_sample[0]))) \
    INV_(eq_##n, 1, bits, 2, NULL, T_(icmp_eq, n##_unc)((uint32_t)o, A0, A1)) \
    INV_(smax_##n, 0, bits, 2, NULL, T_(smax, n##_unc)((uint32_t)o, A0, A1)) \
    INV_(abs_##n, 0, bits, 1, NULL, T_(abs, n##_unc)((uint32_t)o, A0)) \
    INV_(sel_ll_##n, 0, bits, 3, cond_w, T_(select, n##_ll_unc)((uint32_t)o, A0, LIT(bits, S1), LIT(bits, S2)))
INVS_(i1, 1, LN_)
INVS_(i7, 7, LN_)
INVS_(i64, 64, LN_)
INVS_(i200, 200, LW_)
#define RUN_INV_(n) inv_slt_hl_##n(); inv_eq_##n(); inv_smax_##n(); inv_abs_##n(); inv_sel_ll_##n();

/* The internal API with recorded targets (rec = 1), every kind: f = [a < b] (or ==) on the tape, then
 * x = f ? a : b on the tape, XOR x into o, undo; so o ^= min-like(a, b), and the tape leaves no trace. */
static int api_pred, api_kind, api_sel;
static void api_op(int32_t o, const int32_t *args, uint32_t w) {
    cq_bit *a = cq_rail_bits(args[0]), *b = cq_rail_bits(args[1]), *ob = cq_rail_bits(o);
    cq_tape t = { 0 };
    cq_bit *f = cq_tape_bits(&t, 1), *x = cq_tape_bits(&t, w);
    cq_cmp(api_kind, &t, api_pred, a, b, w, f, 1);
    cq_select(api_sel, &t, f, a, b, x, w, 1);
    for (uint32_t i = 0; i < w; i++) cq_cx(&x[i], &ob[i]);
    cq_tape_undo(&t);
}
static hx_val api_ref(const hx_val *a, uint32_t w) {
    int f = api_pred == CQ_CMP_EQ ? hx_eq(a[0], a[1]) : lt(a[0], a[1], w, api_pred == CQ_CMP_SLT);
    return f ? a[0] : a[1];
}
static void check_api(void) {
    static const uint32_t ws[] = { 1, 7, 64, 100, 200 };
    for (api_pred = CQ_CMP_EQ; api_pred <= CQ_CMP_SLT; api_pred++)
        for (int k = 0; k < 3; k++)
            for (api_sel = CQ_SEL_CCX2; api_sel <= CQ_SEL_DIFF_FANOUT; api_sel++)
                for (unsigned i = 0; i < sizeof ws / sizeof *ws; i++) {
                    api_kind = api_pred == CQ_CMP_EQ ? (k ? CQ_CMP_TREE : CQ_CMP_CHAIN) : k;
                    const hx_op h = { "cq_cmp+cq_select rec", 2, 0, api_op, api_op, api_ref };
                    hx_max_samples = 20;
                    assert(hx_run(&h, ws[i], 0, NULL) == HX_OK);
                    hx_max_samples = 0;
                }
    /* The shipped kinds are valid kinds. */
    assert(cq_cmp_kind(CQ_CMP_EQ, 200) == CQ_CMP_CHAIN || cq_cmp_kind(CQ_CMP_EQ, 200) == CQ_CMP_TREE);
    assert(cq_cmp_kind(CQ_CMP_SLT, 200) >= 0 && cq_cmp_kind(CQ_CMP_SLT, 200) <= CQ_CMP_TREE);
    assert(cq_select_kind(200) >= CQ_SEL_CCX2 && cq_select_kind(200) <= CQ_SEL_DIFF_FANOUT);
    printf("cq_cmp, cq_select with recorded targets: ok\n");
}

/* kmgy.37: cq_select on overlapping slices of one rail x (a barrel stage), m = w - s bits:
 * o ^= c ? x[i] : x[i+s], every kind, o plain or recorded, both modes, ancillae clean; and its depth
 * equals the depth on disjoint arms. The public select with both arms one rail: o = that rail. */
static int ov_kind, ov_rec;
static uint32_t ov_s;
static void ov_op(int32_t o, const int32_t *args, uint32_t w) {
    cq_bit *x = cq_rail_bits(args[1]), *ob = cq_rail_bits(o);
    cq_tape t = { 0 };
    cq_bit *y = ov_rec ? cq_tape_bits(&t, w - ov_s) : ob;
    cq_select(ov_kind, &t, cq_rail_bits(args[0]), x, x + ov_s, y, w - ov_s, ov_rec);
    for (uint32_t i = 0; ov_rec && i < w - ov_s; i++) cq_cx(&y[i], &ob[i]);
    cq_tape_undo(&t);
    clean();
}
static hx_val ov_ref(const hx_val *a, uint32_t w) {
    hx_val r = { { 0 } };
    for (uint32_t i = 0; i + ov_s < w; i++)
        if (bitv(&a[1], a[0].l[0] & 1 ? i : i + ov_s)) r.l[i / 64] |= 1ull << (i % 64);
    return r;
}
static int32_t qrail(uint32_t w) {   /* w quantum bits, |0> */
    int32_t r = cq_rail_alloc(w, NULL);
    for (uint32_t i = 0; i < w; i++) cq_get_qubit(&cq_rail_bits(r)[i]);
    return r;
}
static uint32_t sel_depth(int kind, uint32_t m, uint32_t s, int overlap) {
    int32_t c = qrail(1), x = qrail(overlap ? m + s : m), x2 = overlap ? 0 : qrail(m), o = qrail(m);
    cq_bit *xb = cq_rail_bits(x);
    cq_tape t = { 0 };
    cq_replay_reset();
    cq_select(kind, &t, cq_rail_bits(c), xb, overlap ? xb + s : cq_rail_bits(x2), cq_rail_bits(o), m, 0);
    cq_tape_undo(&t);
    uint32_t d = cq_replay_depth();
    cq_rail_free(c); cq_rail_free(x); cq_rail_free(o);
    if (x2) cq_rail_free(x2);
    return d;
}
SHAPE_(select_same_i64, i64, 0, 64, 2, cond_w, (void)w; return a[1], NONE_,
    T_(select, i64)(A0, A1, A1), T_(select, i64_unc)(dst, A0, A1, A1), NONE_, 60)
static void check_overlap(void) {
    static const uint32_t ws[] = { 2, 7, 64, 200 }, ss[] = { 0, 1, 3, 32 };
    for (ov_kind = CQ_SEL_CCX2; ov_kind <= CQ_SEL_DIFF_FANOUT; ov_kind++)
        for (unsigned i = 0; i < sizeof ws / sizeof *ws; i++)
            for (unsigned j = 0; j < sizeof ss / sizeof *ss && ss[j] < ws[i]; j++) {
                ov_s = ss[j];
                for (ov_rec = 0; ov_rec < 2; ov_rec++) {
                    const hx_op h = { "cq_select overlapping arms", 2, 0, ov_op, ov_op, ov_ref };
                    hx_max_samples = 20;
                    assert(hx_run_w(&h, ws[i], cond_w, 0, NULL) == HX_OK);
                    hx_max_samples = 0;
                }
                assert(sel_depth(ov_kind, ws[i] - ov_s, ov_s, 1) == sel_depth(ov_kind, ws[i] - ov_s, ov_s, 0));
            }
    c_select_same_i64();
    printf("cq_select on overlapping slices of one rail, select(c, x, x): ok\n");
}

int main(void) {
    assert(sizeof linked / sizeof *linked == 30 * 66);
    for (unsigned i = 0; i < sizeof linked / sizeof *linked; i++) assert(linked[i]);

    /* The pinned pairs are what they claim. */
    for (edge_k = 0; edge_k < 3; edge_k++) {
        hx_val v = hx_ones(200), u = edge_of(v, 200);
        assert(edge_k == 0 ? hx_eq(u, v) : edge_k == 1 ? hx_eq(u, zero_) : hx_eq(u, hx_xor(v, (hx_val){ { 1 } })));
    }
    edge_k = 0;

    /* Literal operands fold; an all-classical call takes no qubit and emits no gate; aliasing. */
    {
        uint32_t base = cq_pool_live();
        uint32_t c = cqrt_alloc_i200(1, 2, 3, 4), f = cqrt_alloc_i1(1);
        cq_replay_reset();
        uint32_t s = cq_template_select_i200_ll(f, 5, 6, 7, 8, 0, 9, 10, 11, 12);
        uint32_t m = cq_template_smin_i200_hl(c, 0, 0, 0, 1ull << 7);   /* the literal is INT_MIN */
        uint32_t e = cq_template_icmp_eq_i200(c, c), g = cq_template_abs_i200(m);
        assert(cq_replay_gates() == 0 && cq_pool_live() == base + 0);
        cqrt_u256 v = cqrt_measure_i200(s), w = cqrt_measure_i200(m), x = cqrt_measure_i200(g);
        assert(v.l[0] == 5 && v.l[3] == 8 && w.l[3] == 0x80 && w.l[0] == 0 && x.l[3] == 0x80);
        assert(cqrt_measure_i1(e) == 1);
        cqrt_free(s); cqrt_free(m); cqrt_free(e); cqrt_free(g); cqrt_free(c); cqrt_free(f);
        assert(cq_pool_live() == base);
    }

    check_api();
    check_overlap();
    RUN_INV_(i1) RUN_INV_(i7) RUN_INV_(i64) RUN_INV_(i200)
    printf("inverted icmp_slt_hl, icmp_eq, smax, abs, select_ll at i1 i7 i64 i200: ok\n");
    CQ_WIDTHS(RUN_)
    printf("%zu template symbols linked and checked\n", sizeof linked / sizeof *linked);
    return 0;
}
