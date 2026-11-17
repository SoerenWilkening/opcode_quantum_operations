/* BACKEND_ABI §2.4 icmp, §2.5 select, §2.7 smax smin umax umin abs (kmgy.14): the compare flag and
 * the select on bits (kernels.h, "Compare and select") and the registry kernels built on them. */
#include <stdio.h>
#include <stdlib.h>
#include "cq_rt.h"
#include "kernels.h"

/* A gate onto the target: recorded on t (a tape bit) or plain (o, kept after the undo). */
static void gx(cq_tape *t, int rec, cq_bit *q) { if (rec) cq_tape_x(t, q); else cq_x(q); }
static void gcx(cq_tape *t, int rec, const cq_bit *c, cq_bit *q) { if (rec) cq_tape_cx(t, c, q); else cq_cx(c, q); }
static void gccx(cq_tape *t, int rec, const cq_bit *a, const cq_bit *b, cq_bit *q) {
    if (rec) cq_tape_ccx(t, a, b, q); else cq_ccx(a, b, q);
}

static void *ptrs(uint32_t n) {
    void *p = malloc(n * sizeof(cq_bit *));
    if (!p) { fprintf(stderr, "cq_runtime: out of memory\n"); abort(); }
    return p;
}

/* f ^= the AND of v[0 .. m-1], m >= 1: CCX into tape bits, a chain or a balanced tree; the last CCX onto f. */
static void and_all(cq_tape *t, int tree, const cq_bit **v, uint32_t m, cq_bit *f, int rec) {
    if (m > 2 && !tree) {
        cq_bit *x = cq_tape_bits(t, m - 2);
        for (uint32_t j = 0; m > 2; j++, m--) { cq_tape_ccx(t, v[m - 2], v[m - 1], &x[j]); v[m - 2] = &x[j]; }
    }
    while (m > 2) {
        cq_bit *x = cq_tape_bits(t, m / 2);
        for (uint32_t j = 0; j < m / 2; j++) cq_tape_ccx(t, v[2 * j], v[2 * j + 1], &x[j]);
        for (uint32_t j = 0; j < m / 2; j++) v[j] = &x[j];
        if (m & 1) v[m / 2] = v[m - 1];
        m = (m + 1) / 2;
    }
    if (m == 2) gccx(t, rec, v[0], v[1], f); else gcx(t, rec, v[0], f);
}

/* [a <u b] (sgn: [a <s b]) by a comparator tree. A node over bits lo..hi holds L = [a < b there] and
 * E = [a == b there]; hi:lo combine as L = L_hi ^ (E_hi & L_lo) (the two terms exclude each other),
 * E = E_hi & E_lo. Leaves: L_i = ~a_i & b_i, E_i = ~(a_i ^ b_i); signed, the sign bit compares
 * the other way round (L = a & ~b). L updates in place; the lowest node never needs E. */
static void lt_tree(cq_tape *t, int sgn, const cq_bit *a, const cq_bit *b, uint32_t n, cq_bit *f, int rec) {
    cq_bit *L = cq_tape_bits(t, n), *E = cq_tape_bits(t, n);
    cq_bit **l = ptrs(n), **e = ptrs(n);
    for (uint32_t i = 0; i < n; i++) {
        const cq_bit *x = &a[i], *y = &b[i];
        if (sgn && i == n - 1) { x = &b[i]; y = &a[i]; }
        if (i) cq_tape_cx(t, &a[i], &E[i]);
        cq_tape_cx(t, y, &L[i]);                       /* L = y ^ (x & y) = ~x & y */
        if (i) cq_tape_cx(t, &b[i], &E[i]);
        cq_tape_ccx(t, x, y, &L[i]);
        if (i) cq_tape_x(t, &E[i]);
        l[i] = &L[i]; e[i] = &E[i];
    }
    uint32_t m = n;
    while (m > 2) {
        cq_bit *x = cq_tape_bits(t, m / 2);
        for (uint32_t j = 0; j < m / 2; j++) {
            if (j) cq_tape_ccx(t, e[2 * j + 1], e[2 * j], &x[j]);
            cq_tape_ccx(t, e[2 * j + 1], l[2 * j], l[2 * j + 1]);
        }
        for (uint32_t j = 0; j < m / 2; j++) { l[j] = l[2 * j + 1]; e[j] = &x[j]; }
        if (m & 1) { l[m / 2] = l[m - 1]; e[m / 2] = e[m - 1]; }
        m = (m + 1) / 2;
    }
    if (m == 2) { gcx(t, rec, l[1], f); gccx(t, rec, e[1], l[0], f); }
    else gcx(t, rec, l[0], f);
    free(l); free(e);
}

void cq_cmp(int kind, cq_tape *t, int pred, const cq_bit *a, const cq_bit *b, uint32_t n, cq_bit *f, int rec) {
    static const cq_bit one = { CQ_BIT_ONE, 0 };
    if (pred == CQ_CMP_EQ) {
        cq_bit *e = cq_tape_bits(t, n);
        const cq_bit **v = ptrs(n);
        for (uint32_t i = 0; i < n; i++) {
            cq_tape_cx(t, &a[i], &e[i]); cq_tape_cx(t, &b[i], &e[i]); cq_tape_x(t, &e[i]);
            v[i] = &e[i];
        }
        and_all(t, kind != CQ_CMP_CHAIN, v, n, f, rec);
        free(v);
        return;
    }
    if (kind == CQ_CMP_TREE) { lt_tree(t, pred == CQ_CMP_SLT, a, b, n, f, rec); return; }
    /* [a <u b] = no carry out of a + ~b + 1; [a <s b] = [a <u b] ^ sign(a) ^ sign(b). The gates on f
     * that need no carry go first, so they run beside the adder. */
    gx(t, rec, f);
    if (pred == CQ_CMP_SLT) { gcx(t, rec, &a[n - 1], f); gcx(t, rec, &b[n - 1], f); }
    cq_sum r = cq_adder(kind, t, a, b, 1, &one, n, CQ_ADD_COUT);
    gcx(t, rec, &r.c[n], f);
}

/* o ^= a2 ^ (c & (a1 ^ a2)): per bit CX(a2) and either CCX(c, a1), CCX(c, a2) (CCX2) or one CCX(c, d)
 * on d = a1 ^ a2 in scratch (DIFF); c drives every bit itself or through a fan-out (_FANOUT). */
void cq_select(int kind, cq_tape *t, const cq_bit *c, const cq_bit *a1, const cq_bit *a2, cq_bit *o, uint32_t n, int rec) {
    int fan = kind == CQ_SEL_CCX2_FANOUT || kind == CQ_SEL_DIFF_FANOUT, diff = kind >= CQ_SEL_DIFF;
    const cq_bit *F = fan && n > 1 ? cq_tape_fanout(t, c, n - 1) : NULL;
    cq_bit *d = diff ? cq_tape_bits(t, n) : NULL;
    for (uint32_t i = 0; diff && i < n; i++) { cq_tape_cx(t, &a1[i], &d[i]); cq_tape_cx(t, &a2[i], &d[i]); }
    for (uint32_t i = 0; i < n; i++) gcx(t, rec, &a2[i], &o[i]);
    for (uint32_t i = 0; i < n; i++) {
        const cq_bit *ci = F && i ? &F[i - 1] : c;
        if (diff) gccx(t, rec, ci, &d[i], &o[i]);
        else { gccx(t, rec, ci, &a1[i], &o[i]); gccx(t, rec, ci, &a2[i], &o[i]); }
    }
}

int cq_cmp_kind(int pred, uint32_t n) {
    int op = pred == CQ_CMP_EQ ? CQ_KOP_ICMP_eq : pred == CQ_CMP_ULT ? CQ_KOP_ICMP_ult : CQ_KOP_ICMP_slt;
    int i = (int)(cq_kernel_pick(op, n) - cq_kernel_ops[op].cand);
    return pred == CQ_CMP_EQ ? (i ? CQ_CMP_TREE : CQ_CMP_CHAIN) : i;
}
int cq_select_kind(uint32_t n) { return (int)(cq_kernel_pick(CQ_KOP_SELECT, n) - cq_kernel_ops[CQ_KOP_SELECT].cand); }

/* ---- Registry kernels: o ^= f(args). */
static void icmp(int kind, int pred, int swap, int neg, int32_t o, const int32_t *args, uint32_t w) {
    cq_bit *ob = cq_rail_bits(o), *a = cq_rail_bits(args[0]), *b = cq_rail_bits(args[1]);
    cq_tape t = { 0 };
    if (neg) cq_x(ob);
    cq_cmp(kind, &t, pred, swap ? b : a, swap ? a : b, w, ob, 0);
    cq_tape_undo(&t);
}
#define ICMP_EQ_K_(p, base, swap, neg) \
    void cq_k_icmp_##p##_chain(int32_t o, const int32_t *a, uint32_t w) { icmp(CQ_CMP_CHAIN, base, swap, neg, o, a, w); } \
    void cq_k_icmp_##p##_tree(int32_t o, const int32_t *a, uint32_t w) { icmp(CQ_CMP_TREE, base, swap, neg, o, a, w); }
#define ICMP_ORD_K_(p, base, swap, neg) \
    void cq_k_icmp_##p##_ripple(int32_t o, const int32_t *a, uint32_t w) { icmp(CQ_CMP_RIPPLE, base, swap, neg, o, a, w); } \
    void cq_k_icmp_##p##_lookahead(int32_t o, const int32_t *a, uint32_t w) { icmp(CQ_CMP_LOOKAHEAD, base, swap, neg, o, a, w); } \
    void cq_k_icmp_##p##_tree(int32_t o, const int32_t *a, uint32_t w) { icmp(CQ_CMP_TREE, base, swap, neg, o, a, w); }
CQ_ICMP_EQ_PREDS(ICMP_EQ_K_)
CQ_ICMP_ORD_PREDS(ICMP_ORD_K_)

/* args[0] is the i1 condition. */
static void sel(int kind, int32_t o, const int32_t *args, uint32_t w) {
    cq_tape t = { 0 };
    cq_select(kind, &t, cq_rail_bits(args[0]), cq_rail_bits(args[1]), cq_rail_bits(args[2]), cq_rail_bits(o), w, 0);
    cq_tape_undo(&t);
}
void cq_k_select_ccx2(int32_t o, const int32_t *a, uint32_t w) { sel(CQ_SEL_CCX2, o, a, w); }
void cq_k_select_ccx2_fanout(int32_t o, const int32_t *a, uint32_t w) { sel(CQ_SEL_CCX2_FANOUT, o, a, w); }
void cq_k_select_diff(int32_t o, const int32_t *a, uint32_t w) { sel(CQ_SEL_DIFF, o, a, w); }
void cq_k_select_diff_fanout(int32_t o, const int32_t *a, uint32_t w) { sel(CQ_SEL_DIFF_FANOUT, o, a, w); }

/* f = [a < b] on the tape, then o ^= f ? b : a (max) or f ? a : b (min). */
static void minmax(int ck, int sk, int pred, int max, int32_t o, const int32_t *args, uint32_t w) {
    cq_bit *a = cq_rail_bits(args[0]), *b = cq_rail_bits(args[1]);
    cq_tape t = { 0 };
    cq_bit *f = cq_tape_bits(&t, 1);
    cq_cmp(ck, &t, pred, a, b, w, f, 1);
    cq_select(sk, &t, f, max ? b : a, max ? a : b, cq_rail_bits(o), w, 0);
    cq_tape_undo(&t);
}
#define MINMAX_K2_(name, base, max, ck, cn) \
    void cq_k_##name##_##cn(int32_t o, const int32_t *a, uint32_t w) { minmax(ck, CQ_SEL_DIFF, base, max, o, a, w); } \
    void cq_k_##name##_##cn##_fanout(int32_t o, const int32_t *a, uint32_t w) { minmax(ck, CQ_SEL_DIFF_FANOUT, base, max, o, a, w); }
#define MINMAX_K_(name, base, max) \
    MINMAX_K2_(name, base, max, CQ_CMP_RIPPLE, ripple) MINMAX_K2_(name, base, max, CQ_CMP_LOOKAHEAD, lookahead) \
    MINMAX_K2_(name, base, max, CQ_CMP_TREE, tree)
CQ_MINMAX_OPS(MINMAX_K_)

/* |a| = (a ^ s) + s, s the sign: a conditional negate; abs(INT_MIN) = INT_MIN falls out (ABI §2.7).
 * The top bit of a ^ s is 0, so only the w - 1 bits below take s. */
static void abs_(int kind, int fan, int32_t o, const int32_t *args, uint32_t w) {
    cq_bit *a = cq_rail_bits(args[0]), *ob = cq_rail_bits(o);
    const cq_bit *s = &a[w - 1];
    cq_tape t = { 0 };
    const cq_bit *F = fan && w > 2 ? cq_tape_fanout(&t, s, w - 2) : NULL;
    cq_bit *x = cq_tape_bits(&t, w), *zero = cq_tape_bits(&t, w);
    for (uint32_t i = 0; i + 1 < w; i++) { cq_tape_cx(&t, &a[i], &x[i]); cq_tape_cx(&t, F && i ? &F[i - 1] : s, &x[i]); }
    cq_sum r = cq_adder(kind, &t, x, zero, 0, s, w, CQ_ADD_SUM);
    for (uint32_t i = 0; i < w; i++) cq_cx(&r.s[i], &ob[i]);
    cq_tape_undo(&t);
}
void cq_k_abs_ripple(int32_t o, const int32_t *a, uint32_t w) { abs_(CQ_ADDER_RIPPLE, 0, o, a, w); }
void cq_k_abs_ripple_fanout(int32_t o, const int32_t *a, uint32_t w) { abs_(CQ_ADDER_RIPPLE, 1, o, a, w); }
void cq_k_abs_lookahead(int32_t o, const int32_t *a, uint32_t w) { abs_(CQ_ADDER_LOOKAHEAD, 0, o, a, w); }
void cq_k_abs_lookahead_fanout(int32_t o, const int32_t *a, uint32_t w) { abs_(CQ_ADDER_LOOKAHEAD, 1, o, a, w); }
