/* BACKEND_ABI §2.3 shl, lshr, ashr, fshl, fshr. The amount k is the last operand, a W-bit
 * rail, and every op shifts by r = ⟦k⟧ mod W (CR §2 totalization, ABI §2.3).
 *
 * All five are one funnel shift of the 2W-bit value Z = hi:lo (ABI §2.3: fshl is the high W
 * bits of Z << r, fshr the low W bits of Z >> r):
 *   fshl(a, b): hi a, lo b      shl(a)  = fshl(a, 0)
 *   fshr(a, b): hi a, lo b      lshr(a) = fshr(0, a)      ashr(a) = fshr(W copies of a's sign, a)
 * A left funnel is a right funnel on Z and o with their bit order reversed.
 *
 * Classical amount (every _hl shape, the literal-amount funnel shapes): r is computed here and
 * o_i ^= Z_(i+r) is CX wiring, depth 1 (ABI §2.3 Impl.). Quantum amount: r = k mod W as an
 * n = ceil(log2 W)-bit value (the low n bits of k when W is a power of two, else the carry-save
 * residue sum below), then a barrel of n out-of-place mux stages, the last one onto o. Everything but o
 * is undone by replaying the recorded gates backwards (X, CX, CCX are self-inverse), and the
 * ancillae go back to the pool. The two candidates differ only in the mux controls: bit t of r
 * used directly by every mux of stage t, or first fanned out to one copy per mux. */
#include <stdio.h>
#include <stdlib.h>
#include "cq_rt.h"
#include "kernels.h"

static const cq_bit ZERO_ = { CQ_BIT_ZERO, 0 }, ONE_ = { CQ_BIT_ONE, 0 };

static void die(const char *msg) { fprintf(stderr, "cq_runtime: shift: %s\n", msg); abort(); }
static void *grow(void *p, size_t *cap, size_t n, size_t size) {
    if (n < *cap) return p;
    *cap = *cap ? 2 * *cap : 256;
    if (!(p = realloc(p, *cap * size))) die("out of memory");
    return p;
}

/* ---- The recorded gates and the memory of one kernel call (kernels never nest). */
typedef struct { const cq_bit *a, *b; cq_bit *t; } gate;   /* a NULL: X(t); b NULL: CX(a, t) */
typedef struct { void *p; size_t nbits; } block;           /* nbits 0: not ancilla bits */
static gate  *tape;
static block *blocks;
static size_t ntape, captape, nblocks, capblocks;

static void play(gate g) {
    if (!g.a) cq_x(g.t);
    else if (!g.b) cq_cx(g.a, g.t);
    else cq_ccx(g.a, g.b, g.t);
}
static void rec(const cq_bit *a, const cq_bit *b, cq_bit *t) {
    tape = grow(tape, &captape, ntape, sizeof *tape);
    tape[ntape] = (gate){ a, b, t };
    play(tape[ntape++]);
}

static void *keep(size_t n, size_t size, size_t nbits) {
    blocks = grow(blocks, &capblocks, nblocks, sizeof *blocks);
    void *p = calloc(n ? n : 1, size);
    if (!p) die("out of memory");
    blocks[nblocks++] = (block){ p, nbits };
    return p;
}
static cq_bit *anc(size_t n) { return keep(n, sizeof (cq_bit), n); }   /* fresh classical zeros */
static const cq_bit **ptrs(size_t n) { return keep(n, sizeof (const cq_bit *), 0); }

/* Undo every recorded gate; every ancilla is then |0> (or classical 0) and goes back. */
static void undo_and_release(void) {
    while (ntape) play(tape[--ntape]);
    while (nblocks) {
        block b = blocks[--nblocks];
        cq_bit *bits = b.p;
        for (size_t i = 0; i < b.nbits; i++) if (cq_bit_is_q(bits[i])) cq_pool_give(bits[i].q);
        free(b.p);
    }
}

static uint32_t clog2(uint32_t n) { uint32_t e = 0; while ((1ull << e) < n) e++; return e; }

/* n pointers to copies of *src, the first src itself: a CX doubling tree, depth ceil(log2 n).
 * A classical src needs no copy. */
static const cq_bit **fan(const cq_bit *src, size_t n) {
    const cq_bit **c = ptrs(n);
    c[0] = src;
    if (!cq_bit_is_q(*src)) { for (size_t j = 1; j < n; j++) c[j] = src; return c; }
    cq_bit *a = n > 1 ? anc(n - 1) : NULL;
    for (size_t have = 1; have < n;) {
        size_t m = have < n - have ? have : n - have;
        for (size_t j = 0; j < m; j++) { rec(c[j], NULL, &a[have + j - 1]); c[have + j] = &a[have + j - 1]; }
        have += m;
    }
    return c;
}

/* ---- r = k mod w for w not a power of two. */

static uint32_t bitlen(uint64_t v) { uint32_t b = 0; while (b < 64 && v >> b) b++; return b; }

/* x + y of len-bit values into fresh bits, the carry out dropped (the caller's bound says it is 0):
 * every generate x_i y_i first, then the chain c_(i+1) ^= x_i c_i ^ y_i c_i (maj = xy ^ xc ^ yc),
 * two gates per bit, the sum bit x ^ y ^ c_i as its carry arrives. Depth about 2 len. */
static const cq_bit **add(const cq_bit **x, const cq_bit **y, uint32_t len, const cq_bit **carry_out) {
    cq_bit *z = anc(len), *c = anc(len + 1);
    const cq_bit **s = ptrs(len);
    for (uint32_t i = 0; i < len; i++) rec(x[i], y[i], &c[i + 1]);
    for (uint32_t i = 0; i < len; i++) { rec(x[i], NULL, &z[i]); rec(y[i], NULL, &z[i]); }
    for (uint32_t i = 0; i < len; i++) {
        if (i) { rec(x[i], &c[i], &c[i + 1]); rec(y[i], &c[i], &c[i + 1]); rec(&c[i], NULL, &z[i]); }
        s[i] = &z[i];
    }
    if (carry_out) *carry_out = &c[len];
    return s;
}

/* The m-bit s, minus C when s >= C (C < 2^m): g, the carry of s + 2^m - C, is [s >= C], fanned
 * out, then an out-of-place select r_i = s_i ^ g (s_i ^ d_i). */
static const cq_bit **condsub(const cq_bit **s, uint32_t m, uint64_t C) {
    const cq_bit **kc = ptrs(m), **rp = ptrs(m), *g;
    for (uint32_t i = 0; i < m; i++) kc[i] = (((1ull << m) - C) >> i) & 1 ? &ONE_ : &ZERO_;
    const cq_bit **d = add(s, kc, m, &g), **gs = fan(g, m);
    cq_bit *r = anc(m);
    for (uint32_t i = 0; i < m; i++) { rec(s[i], NULL, &r[i]); rp[i] = &r[i]; }
    for (uint32_t i = 0; i < m; i++) rec(gs[i], s[i], &r[i]);
    for (uint32_t i = 0; i < m; i++) rec(gs[i], d[i], &r[i]);
    return rp;
}

/* Weighted bits: col[b] holds h[b] bits of weight 2^b, b < m, and their sum is below 2^m. */
typedef struct { const cq_bit **col[64]; uint32_t h[64], m, cap; } cols;
static void cols_init(cols *c, uint32_t m, uint32_t cap) {
    c->m = m; c->cap = cap;
    for (uint32_t b = 0; b < m; b++) { c->col[b] = ptrs(cap); c->h[b] = 0; }
}
static void put(cols *c, uint32_t b, const cq_bit *x) {
    if (c->h[b] == c->cap) die("column overflow");
    c->col[b][c->h[b]++] = x;
}

/* The sum as m bits: Wallace rounds of out-of-place full adders (depth 3 each; a carry out of
 * column m - 1 is 0 by the bound and not computed) until no column holds three bits, then one add. */
static const cq_bit **colsum(cols *c) {
    for (;;) {
        uint32_t nfa = 0;
        for (uint32_t b = 0; b < c->m; b++) nfa += c->h[b] / 3;
        if (!nfa) break;
        cols nx;
        cols_init(&nx, c->m, c->cap);
        cq_bit *f = anc(2 * nfa);
        for (uint32_t b = 0; b < c->m; b++) {
            uint32_t i = 0;
            for (; c->h[b] - i >= 3; i += 3, f += 2) {
                const cq_bit *x = c->col[b][i], *y = c->col[b][i + 1], *z = c->col[b][i + 2];
                int carry = b + 1 < c->m;
                if (carry) rec(x, y, &f[1]);
                rec(z, NULL, &f[0]); rec(x, NULL, &f[0]);
                if (carry) rec(y, z, &f[1]);
                rec(y, NULL, &f[0]);
                if (carry) rec(x, z, &f[1]);
                put(&nx, b, &f[0]);
                if (carry) put(&nx, b + 1, &f[1]);
            }
            for (; i < c->h[b]; i++) put(&nx, b, c->col[b][i]);
        }
        *c = nx;
    }
    const cq_bit **x = ptrs(c->m), **y = ptrs(c->m);
    int two = 0;
    for (uint32_t b = 0; b < c->m; b++) {
        x[b] = c->h[b] > 0 ? c->col[b][0] : &ZERO_;
        y[b] = c->h[b] > 1 ? c->col[b][1] : &ZERO_;
        two |= c->h[b] > 1;
    }
    return two ? add(x, y, c->m, NULL) : x;
}

/* n = ceil(log2 w) bits holding k mod w. k = sum k_j 2^j, so k mod w = sum k_j (2^j mod w) mod w:
 * bit k_j wired to the 1-bits of the constant 2^j mod w (no gate) and the columns summed. The same
 * step on the sum while it shrinks it (to below 4w), then conditional subtractions of w 2^t,
 * t descending. bound is the largest value the bits can hold. */
static const cq_bit **reduce(const cq_bit *k, uint32_t w, uint32_t n) {
    const cq_bit **s = ptrs(n);
    for (uint32_t i = 0; i < n; i++) s[i] = &k[i];
    if (!(w & (w - 1))) return s;
    uint64_t bound = 0;
    uint32_t m = w, height = w;   /* round 1: the w bits of k */
    s = ptrs(w);
    for (uint32_t i = 0; i < w; i++) s[i] = &k[i];
    for (int round = 0;; round++) {
        uint64_t nb = 0;
        for (uint32_t j = 0, c = 1; j < m; j++, c = 2 * c % w) nb += c;
        if (round && bitlen(nb) + 1 >= bitlen(bound)) break;   /* a round must save two bits to beat condsub */
        cols cs;
        cols_init(&cs, bitlen(nb), height + 2);
        for (uint32_t j = 0, c = 1; j < m; j++, c = 2 * c % w)
            for (uint32_t b = 0; b < n; b++) if (c >> b & 1) put(&cs, b, s[j]);
        s = colsum(&cs);
        bound = nb;
        m = cs.m;
        height = m;
    }
    for (int t = 40; t >= 0; t--) {
        uint64_t C = (uint64_t)w << t;
        if (C > bound) continue;
        s = condsub(s, m, C);
        bound = bound - C > C - 1 ? bound - C : C - 1;
    }
    return s;
}

/* ---- The funnel. */

/* CX(y, t) (a NULL) or CCX(a, y, t); recorded unless it targets o (the last stage). */
static void gate3(const cq_bit *a, const cq_bit *y, cq_bit *t, int onto_o) {
    if (onto_o) { if (a) cq_ccx(a, y, t); else cq_cx(y, t); }
    else rec(a ? a : y, a ? y : NULL, t);
}

/* o ^= the low w bits of Z >> r (left 0) or the high w bits of Z << r (left 1), Z = hi:lo of
 * 2w bits, r = ⟦k⟧ mod w. hi or lo NULL: zeros. sign: hi is w copies of *sign (hi unused). */
static void funnel(int32_t oh, const cq_bit *hi, const cq_bit *lo, const cq_bit *sign,
                   const cq_bit *k, uint32_t w, int left, int fan_out) {
    cq_bit *o = cq_rail_bits(oh), **T = (cq_bit **)ptrs(w);
    const cq_bit **Z = ptrs(2 * w);
    for (uint32_t i = 0; i < w; i++) {
        Z[i] = lo ? &lo[i] : &ZERO_;
        Z[w + i] = sign ? sign : hi ? &hi[i] : &ZERO_;
        T[left ? w - 1 - i : i] = &o[i];
    }
    for (uint32_t i = 0; left && i < w; i++) { const cq_bit *t = Z[i]; Z[i] = Z[2 * w - 1 - i]; Z[2 * w - 1 - i] = t; }

    int classical = 1;
    for (uint32_t i = 0; i < w; i++) classical &= !cq_bit_is_q(k[i]);
    if (classical) {
        uint32_t r = 0;
        for (uint32_t i = w; i--;) r = (uint32_t)((2ull * r + k[i].kind) % w);
        /* ashr reads the sign r + 1 times: fan it out when that is shallower than reading it in turn */
        if (sign && 2 * clog2(r + 1) + 1 < r + 1) { const cq_bit **c = fan(sign, r + 1); for (uint32_t j = 0; j <= r; j++) Z[w - 1 + j] = c[j]; }
        for (uint32_t i = 0; i < w; i++) cq_cx(Z[i + r], T[i]);
        undo_and_release();
        return;
    }

    uint32_t n = clog2(w), len = 2 * w;
    const cq_bit **r = reduce(k, w, n), **Y = Z;
    if (sign && fan_out) { const cq_bit **c = fan(sign, w + 1); for (uint32_t j = 1; j <= w; j++) Z[w - 1 + j] = c[j]; }
    if (!n) for (uint32_t i = 0; i < w; i++) cq_cx(Y[i], T[i]);
    for (uint32_t t = 0; t < n; t++) {
        /* stage t: Y'_i = r_t ? Y_(i+s) : Y_i, for the positions the later stages still read */
        uint32_t s = 1u << t, last = t == n - 1, rest = (1u << n) - (2u << t);
        uint32_t need = last ? w : (w + rest < len ? w + rest : len);
        const cq_bit **c = fan_out ? fan(r[t], need) : NULL, **Yn = ptrs(need);
        cq_bit *out = last ? NULL : anc(need);
        /* three passes, so a bit of Y is used once per pass: depth 3 per stage once r_t is fanned out */
        for (uint32_t i = 0; i < need; i++) Yn[i] = last ? T[i] : &out[i];
        for (uint32_t i = 0; i < need; i++) gate3(NULL, Y[i], (cq_bit *)Yn[i], last);
        for (uint32_t i = 0; i < need; i++) gate3(c ? c[i] : r[t], Y[i], (cq_bit *)Yn[i], last);
        for (uint32_t i = 0; i < need; i++) gate3(c ? c[i] : r[t], i + s < len ? Y[i + s] : &ZERO_, (cq_bit *)Yn[i], last);
        Y = Yn;
        len = need;
    }
    undo_and_release();
}

#define SHIFT_KERNELS_(cand, fo) \
    void cq_k_shl_##cand(int32_t o, const int32_t *a, uint32_t w) { \
        funnel(o, cq_rail_bits(a[0]), NULL, NULL, cq_rail_bits(a[1]), w, 1, fo); } \
    void cq_k_lshr_##cand(int32_t o, const int32_t *a, uint32_t w) { \
        funnel(o, NULL, cq_rail_bits(a[0]), NULL, cq_rail_bits(a[1]), w, 0, fo); } \
    void cq_k_ashr_##cand(int32_t o, const int32_t *a, uint32_t w) { \
        const cq_bit *x = cq_rail_bits(a[0]); funnel(o, NULL, x, &x[w - 1], cq_rail_bits(a[1]), w, 0, fo); } \
    void cq_k_fshl_##cand(int32_t o, const int32_t *a, uint32_t w) { \
        funnel(o, cq_rail_bits(a[0]), cq_rail_bits(a[1]), NULL, cq_rail_bits(a[2]), w, 1, fo); } \
    void cq_k_fshr_##cand(int32_t o, const int32_t *a, uint32_t w) { \
        funnel(o, cq_rail_bits(a[0]), cq_rail_bits(a[1]), NULL, cq_rail_bits(a[2]), w, 0, fo); }
SHIFT_KERNELS_(barrel, 0)
SHIFT_KERNELS_(barrel_fan, 1)
