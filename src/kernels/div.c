/* BACKEND_ABI §2.2 udiv, urem, sdiv, srem (kmgy.12): o ^= f(a, b), totalized by CR §2. Everything is
 * recorded on one tape (kernels.h), the result XORed into o, the tape undone: the quotient and the
 * remainder are by-products of each other and neither is left behind.
 *
 * Unsigned: restoring long division in radix 2^k, k quotient bits per trip from the top. After a trip
 * has taken n bits of a, R' = 2^k R + those bits has at most n bits, so it is compared with each
 * multiple j b (j < 2^k, all at once) on n bits only, with the flag [j b < 2^n] (a suffix AND of
 * ~(j b)) ANDed in: t_j = carry out of R' + ~(j b) + 1, & that flag. The t_j are a thermometer of the
 * digit. The new remainder is R' ^ (R' ^ (R' - j b)) for the digit j, and R' ^ (R' - j b) = ~(j b) ^ the
 * carries, so no sum bits are formed. b = 0 gives every t_j = 1 and R = a: CR §2's udiv 2^W - 1 and
 * urem a, with no special case.
 * Signed: magnitudes |x| = (x ^ s..s) + s, the unsigned division, then the result negated the same way
 * under sign(a) ^ sign(b) (sdiv) or sign(a) (srem). Every other CR §2 row follows from the unsigned
 * one: a / 0 is all ones, negated to +1 when a < 0; a rem 0 = |a| negated back to a; INT_MIN / -1 is
 * 2^(W-1) / 1 with equal signs, INT_MIN, remainder 0. */
#include <stdlib.h>
#include "cq_rt.h"
#include "kernels.h"

static const cq_bit one = { CQ_BIT_ONE, 0 }, zeros[CQ_RAIL_MAX_WIDTH];

static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) abort();
    return p;
}

/* x & y: a classical operand folds to a pointer, no bit and no gate. */
static const cq_bit *and_(cq_tape *t, const cq_bit *x, const cq_bit *y) {
    if (!cq_bit_is_q(*x)) return x->kind ? y : x;
    if (!cq_bit_is_q(*y)) return y->kind ? x : y;
    cq_bit *r = cq_tape_bits(t, 1);
    cq_tape_ccx(t, x, y, r);
    return r;
}

/* x & ~y, folded like and_. */
static const cq_bit *andnot_(cq_tape *t, const cq_bit *x, const cq_bit *y) {
    if (!cq_bit_is_q(*y)) return y->kind ? zeros : x;
    if (!cq_bit_is_q(*x) && !x->kind) return x;
    cq_bit *r = cq_tape_bits(t, 1);
    cq_tape_cx(t, x, r);
    cq_tape_ccx(t, x, y, r);
    return r;
}

/* z[m] = AND of v[m..len-1] for m = 1..len, z[len] = 1: a Kogge-Stone suffix AND over bit pointers,
 * depth 2 log len. */
static void suffix_and(cq_tape *t, const cq_bit *v, uint32_t len, const cq_bit **z) {
    const cq_bit **y = xmalloc((len + 1) * sizeof *y);
    for (uint32_t m = 0; m < len; m++) z[m] = &v[m];
    z[len] = &one;
    for (uint32_t d = 1; d < len; d *= 2) {
        for (uint32_t m = 1; m < len; m++) y[m] = m + d < len ? and_(t, z[m], z[m + d]) : z[m];
        for (uint32_t m = 1; m < len; m++) z[m] = y[m];
    }
    free(y);
}

/* v copied onto the n fresh bits d[], by doubling: depth ceil(log2(n + 1)). */
static void spread(cq_tape *t, const cq_bit *v, cq_bit *const *d, uint32_t n) {
    for (uint32_t have = 0; have < n;) {
        uint32_t k = have + 1 < n - have ? have + 1 : n - have;
        for (uint32_t j = 0; j < k; j++) cq_tape_cx(t, j ? d[j - 1] : v, d[have + j]);
        have += k;
    }
}

#define KMAX 3                     /* radix 2^k, k <= KMAX */
#define JMAX ((1u << KMAX) - 1)

/* Records a / b (w bits each, unsigned, b = 0 as above) in radix 2^k: each trip takes k more bits of a
 * (the first trip the rest, (w - 1) % k + 1) and compares R' with every multiple j b, j < 2^k, in
 * parallel; the thermometer of those flags is the quotient digit, and its one-hot form selects
 * R' - j b. q[i] is quotient bit i. With rem, returns the remainder's w contiguous bits; else NULL and
 * the last trip stops at its quotient digit. */
static const cq_bit *udivrem(int kind, uint32_t k, cq_tape *t, const cq_bit *a, const cq_bit *b, uint32_t w, int rem, const cq_bit **q) {
    /* The odd multiples o b on E = w + k bits, each in a block with k low slots, so (o b) << s is the
     * view block + k - s: pos (slots 0) and nm = ~(o b) (slots 1). An even multiple is such a view. */
    uint32_t E = w + k, J = (1u << k) - 1;
    cq_bit *pos[JMAX + 1] = { 0 }, *nm[JMAX + 1] = { 0 };
    const cq_bit **z[JMAX + 1] = { 0 };
    for (uint32_t o = 1; o <= J; o += 2) {
        pos[o] = cq_tape_bits(t, E + k) + k;
        nm[o] = cq_tape_bits(t, E + k) + k;
        const cq_bit *v = b;
        if (o > 1) {                                    /* o b = (o - 1) b + b, (o - 1) b = (o' b) << s */
            uint32_t s = (uint32_t)__builtin_ctz(o - 1);
            v = cq_adder(kind, t, pos[(o - 1) >> s] - s, pos[1], 0, NULL, E, CQ_ADD_SUM).s;
        }
        for (uint32_t i = 0; i < k; i++) cq_tape_x(t, &nm[o][(int)i - (int)k]);
        for (uint32_t i = 0; i < E; i++) {
            cq_tape_x(t, &nm[o][i]);
            if (o > 1 || i < w) {
                if (J > 1) cq_tape_cx(t, &v[i], &pos[o][i]);
                cq_tape_cx(t, &v[i], &nm[o][i]);
            }
        }
        z[o] = xmalloc((E + 1) * sizeof *z[o]);
        suffix_and(t, nm[o], E, z[o]);
    }
    const cq_bit *nmj[JMAX + 1];                        /* ~(j b): an even j gets its own copy, so no two */
    for (uint32_t j = 1; j <= J; j++) {                 /* comparisons of a trip read one bit */
        uint32_t s = (uint32_t)__builtin_ctz(j);
        if (!s) { nmj[j] = nm[j]; continue; }
        cq_bit *v = cq_tape_bits(t, E);
        for (uint32_t i = 0; i < E; i++) cq_tape_cx(t, &nm[j >> s][(int)i - (int)s], &v[i]);
        nmj[j] = v;
    }

    uint32_t g = (w - 1) % k + 1, n = g;               /* this trip's new bits of a; bits taken so far */
    cq_bit *x = cq_tape_bits(t, n);                     /* R' = 2^g R + the next g bits of a */
    for (uint32_t i = 0; i < g; i++) cq_tape_cx(t, &a[w - g + i], &x[i]);
    for (;;) {
        uint32_t jn = (1u << g) - 1, gn = n == w ? 0 : k, i0 = w - n;
        int mux = n < w || rem;
        const cq_bit *tj[JMAX + 2], *zj[JMAX + 1];
        cq_bit *cp[JMAX + 1], *c[JMAX + 1], *dst[JMAX + 1];
        cq_bit *rn = mux ? cq_tape_bits(t, gn + n) : NULL;   /* the next R': the next gn bits of a, then R */
        for (uint32_t i = 0; i < gn; i++) cq_tape_cx(t, &a[i0 - gn + i], &rn[i]);
        for (uint32_t j = 1; j <= jn; j++) {
            uint32_t s = (uint32_t)__builtin_ctz(j);
            zj[j] = z[j >> s][n - s];                   /* [j b < 2^n] */
            cp[j] = j == 1 || (!cq_bit_is_q(*zj[j]) && !zj[j]->kind) ? x : cq_tape_bits(t, n);
        }
        for (uint32_t i = 0; i < n; i++) {             /* copies of R': one per comparison beyond the first, and R */
            uint32_t m = 0;
            for (uint32_t j = 2; j <= jn; j++) if (cp[j] != x) dst[m++] = &cp[j][i];
            if (mux) dst[m++] = &rn[gn + i];
            spread(t, &x[i], dst, m);
        }
        for (uint32_t j = 1; j <= jn; j++) {           /* t_j = [R' >= j b]: carry out of R' + ~(j b) + 1, and [j b < 2^n] */
            if (!cq_bit_is_q(*zj[j]) && !zj[j]->kind) { tj[j] = zj[j]; c[j] = NULL; continue; }
            c[j] = cq_adder(kind, t, cp[j], nmj[j], 0, &one, n, CQ_ADD_COUT).c;
            tj[j] = and_(t, &c[j][n], zj[j]);
        }
        tj[jn + 1] = zeros;
        for (uint32_t j = 1; mux && j <= jn; j++) {    /* R ^= [digit = j] (R' ^ (R' - j b)), that is ~(j b) ^ the carries */
            if (!c[j]) continue;
            const cq_bit *sj = andnot_(t, tj[j], tj[j + 1]), *f = cq_tape_fanout(t, sj, n - 1);
            for (uint32_t i = 0; i < n; i++) {
                cq_tape_cx(t, &nmj[j][i], &c[j][i]);
                cq_tape_ccx(t, i ? &f[i - 1] : sj, &c[j][i], &rn[gn + i]);
            }
        }
        for (uint32_t i = 0; i < g; i++) {             /* digit bit i: XOR of t_j over the multiples j of 2^i; */
            uint32_t j = 1u << i;                       /* emitted after the selection, which is on the critical path */
            if (2 * j > jn) { q[i0 + i] = tj[j]; continue; }
            cq_bit *r = cq_tape_bits(t, 1);
            for (; j <= jn; j += 1u << i) cq_tape_cx(t, tj[j], r);
            q[i0 + i] = r;
        }
        if (!mux) break;
        x = rn;
        if (n == w) break;
        n += k; g = k;
    }
    for (uint32_t o = 1; o <= J; o += 2) free(z[o]);
    return rem ? x : NULL;
}

/* Records (x ^ s..s) + s for w bit pointers x, f the w - 1 copies of s: x negated when s. */
static const cq_bit *cneg(int kind, cq_tape *t, const cq_bit *const *x, const cq_bit *s, const cq_bit *f, uint32_t w) {
    cq_bit *v = cq_tape_bits(t, w);
    for (uint32_t k = 0; k < w; k++) { cq_tape_cx(t, x[k], &v[k]); cq_tape_cx(t, k ? &f[k - 1] : s, &v[k]); }
    return cq_adder(kind, t, v, zeros, 0, s, w, CQ_ADD_SUM).s;
}

enum { UDIV, UREM, SDIV, SREM };

static void divrem(int op, int kind, uint32_t k, int32_t o, const int32_t *args, uint32_t w) {
    cq_bit *ob = cq_rail_bits(o);
    const cq_bit *a = cq_rail_bits(args[0]), *b = cq_rail_bits(args[1]);
    int rem = op == UREM || op == SREM;
    const cq_bit **p = xmalloc(w * sizeof *p), **q = xmalloc(w * sizeof *q), *r;
    cq_tape t = { 0 };
    if (op >= SDIV) {
        const cq_bit *sa = &a[w - 1], *sb = &b[w - 1], *s = sa;
        const cq_bit *fa = cq_tape_fanout(&t, sa, w - 1), *fb = cq_tape_fanout(&t, sb, w - 1), *fs = fa;
        if (op == SDIV) {
            cq_bit *x = cq_tape_bits(&t, 1);
            cq_tape_cx(&t, sa, x); cq_tape_cx(&t, sb, x);
            s = x; fs = cq_tape_fanout(&t, x, w - 1);
        }
        for (uint32_t k = 0; k < w; k++) p[k] = &a[k];
        a = cneg(kind, &t, p, sa, fa, w);
        for (uint32_t k = 0; k < w; k++) p[k] = &b[k];
        b = cneg(kind, &t, p, sb, fb, w);
        r = udivrem(kind, k, &t, a, b, w, rem, q);
        if (rem) for (uint32_t k = 0; k < w; k++) q[k] = &r[k];
        r = cneg(kind, &t, q, s, fs, w);
        for (uint32_t k = 0; k < w; k++) q[k] = &r[k];
    } else if ((r = udivrem(kind, k, &t, a, b, w, rem, q)))
        for (uint32_t k = 0; k < w; k++) q[k] = &r[k];
    for (uint32_t k = 0; k < w; k++) cq_cx(q[k], &ob[k]);
    cq_tape_undo(&t);
    free(p); free(q);
}

#define DIV_K_(name, OP) \
    void cq_k_##name##_ripple(int32_t o, const int32_t *a, uint32_t w) { divrem(OP, CQ_ADDER_RIPPLE, 1, o, a, w); } \
    void cq_k_##name##_lookahead(int32_t o, const int32_t *a, uint32_t w) { divrem(OP, CQ_ADDER_LOOKAHEAD, 1, o, a, w); } \
    void cq_k_##name##_r4(int32_t o, const int32_t *a, uint32_t w) { divrem(OP, CQ_ADDER_LOOKAHEAD, 2, o, a, w); } \
    void cq_k_##name##_r8(int32_t o, const int32_t *a, uint32_t w) { divrem(OP, CQ_ADDER_LOOKAHEAD, 3, o, a, w); }
DIV_K_(udiv, UDIV)
DIV_K_(urem, UREM)
DIV_K_(sdiv, SDIV)
DIV_K_(srem, SREM)
