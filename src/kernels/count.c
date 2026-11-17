/* BACKEND_ABI §2.8 ctpop, ctlz, cttz (kmgy.16): o ^= count(a). The result has width W, and the
 * count is at most W < 2^m, m = bitlen(W), so it lands in o's low m bits and the rest of o is
 * untouched. ctlz(0) = cttz(0) = W (ABI §2.8, CR §2); there is no is_zero_poison argument.
 * The counting circuits work on arrays of bit pointers (API: kernels.h, "Bit counting"), so
 * cttz is the clz circuit on the operand's bits in reverse order: wire reversal costs nothing. */
#include "cq_rt.h"
#include "kernels.h"

static const cq_bit ZERO_ = { CQ_BIT_ZERO, 0 }, ONE_ = { CQ_BIT_ONE, 0 };

uint32_t cq_count_width(uint32_t n) { uint32_t m = 0; while ((uint64_t)n >> m) m++; return m; }

/* ---- Population count */

/* Linear: each bit is the carry-in of an m-bit adder onto the running count (BACKEND_ABI §2.8 Impl.
 * sums the bits by W - 1 adds; here at width m, not W). */
static void pop_linear(cq_tape *t, const cq_bit *const *x, uint32_t n, const cq_bit **r) {
    static const cq_bit zeros[32];
    uint32_t m = cq_count_width(n);
    const cq_bit *c = cq_tape_bits(t, m);
    for (uint32_t i = 0; i < n; i++) c = cq_adder(cq_adder_kind(m), t, c, zeros, 0, x[i], m, CQ_ADD_SUM).s;
    for (uint32_t k = 0; k < m; k++) r[k] = &c[k];
}

/* Carry-save (Wallace) tree: per weight column, every three bits go through a full adder (sum
 * stays, carry moves up a column; carries out of column m - 1 are dropped, the count fits m bits)
 * until no column holds more than two; the two rows are copied out and added by one m-bit adder.
 * Full adder out of place, depth 3: s = a ^ b ^ c and carry = ab ^ bc ^ ca, interleaved. */
static void pop_csa(cq_tape *t, const cq_bit *const *x, uint32_t n, const cq_bit **r) {
    uint32_t m = cq_count_width(n), h[2][32] = { { n } }, cur = 0, big = n > 2;
    const cq_bit *col[2][m][n];
    for (uint32_t i = 0; i < n; i++) col[0][0][i] = x[i];
    while (big) {
        uint32_t nx = !cur, *hn = h[nx];
        for (uint32_t k = 0; k < m; k++) hn[k] = 0;
        for (uint32_t k = 0; k < m; k++) {
            const cq_bit **v = col[cur][k];
            uint32_t i = 0;
            for (; i + 3 <= h[cur][k]; i += 3) {
                cq_bit *s = cq_tape_bits(t, 2);
                int carry = k + 1 < m;
                if (carry) cq_tape_ccx(t, v[i], v[i + 1], &s[1]);
                cq_tape_cx(t, v[i + 2], &s[0]);
                if (carry) cq_tape_ccx(t, v[i + 1], v[i + 2], &s[1]);
                cq_tape_cx(t, v[i], &s[0]);
                if (carry) cq_tape_ccx(t, v[i + 2], v[i], &s[1]);
                cq_tape_cx(t, v[i + 1], &s[0]);
                col[nx][k][hn[k]++] = &s[0];
                if (carry) col[nx][k + 1][hn[k + 1]++] = &s[1];
            }
            for (; i < h[cur][k]; i++) col[nx][k][hn[k]++] = v[i];
        }
        cur = nx;
        big = 0;
        for (uint32_t k = 0; k < m; k++) big |= h[cur][k] > 2;
    }
    int two = 0;
    for (uint32_t k = 0; k < m; k++) { two |= h[cur][k] == 2; r[k] = h[cur][k] ? col[cur][k][0] : &ZERO_; }
    if (!two) return;   /* every column is one bit: those are the count */
    cq_bit *a = cq_tape_bits(t, m), *b = cq_tape_bits(t, m);
    for (uint32_t k = 0; k < m; k++) {
        if (h[cur][k] > 0) cq_tape_cx(t, col[cur][k][0], &a[k]);
        if (h[cur][k] > 1) cq_tape_cx(t, col[cur][k][1], &b[k]);
    }
    const cq_bit *s = cq_adder(cq_adder_kind(m), t, a, b, 0, NULL, m, CQ_ADD_SUM).s;
    for (uint32_t k = 0; k < m; k++) r[k] = &s[k];
}

/* ---- Leading zeros, counted from x[n-1] down */

/* Thermometer: N[j] = 1 iff x[n-1-j .. n-1] are all zero, a prefix AND of the negated bits (Kogge-Stone,
 * ceil(log2 n) CCX rounds). The count c is the number of ones of N, and N[j] = [j < c], so bit k of c is
 * the XOR of N[j] over the j where bit k of j + 1 differs from that of j, i.e. (j + 1) % 2^k == 0: one
 * XOR tree per result bit, no adder. */
static void clz_thermo(cq_tape *t, const cq_bit *const *x, uint32_t n, const cq_bit **r) {
    uint32_t m = cq_count_width(n);
    const cq_bit *N[n];
    cq_bit *nx = cq_tape_bits(t, n);
    for (uint32_t j = 0; j < n; j++) { cq_tape_x(t, &nx[j]); cq_tape_cx(t, x[n - 1 - j], &nx[j]); N[j] = &nx[j]; }
    for (uint32_t d = 1; d < n; d *= 2) {
        /* Gates j and j - d share N[j - d]: emit the even blocks of d, then the odd, so the round is depth 2. */
        cq_bit *f = cq_tape_bits(t, n - d);
        for (uint32_t odd = 0; odd < 2; odd++)
            for (uint32_t j = d; j < n; j++) if ((j / d & 1) == odd) cq_tape_ccx(t, N[j], N[j - d], &f[j - d]);
        for (uint32_t j = d; j < n; j++) N[j] = &f[j - d];
    }
    for (uint32_t k = 0; k < m; k++) {
        const cq_bit *v[n];
        uint32_t cnt = 0;
        for (uint32_t j = (1u << k) - 1; j < n; j += 1u << k) v[cnt++] = N[j];
        while (cnt > 1) {   /* XOR tree, pairs into fresh bits */
            uint32_t h = 0;
            for (uint32_t i = 0; i + 1 < cnt; i += 2) {
                cq_bit *f = cq_tape_bits(t, 1);
                cq_tape_cx(t, v[i], f);
                cq_tape_cx(t, v[i + 1], f);
                v[h++] = f;
            }
            if (cnt & 1) v[h++] = v[cnt - 1];
            cnt = h;
        }
        r[k] = v[0];
    }
}

/* Priority tree: n padded at the bottom to 2^L bits with ones (so a zero operand still reads n
 * leading zeros, and the padding never counts). A node over 2^l bits holds z (all its bits are
 * zero) and c (its leading zeros, l bits, when z = 0). Two children H (high) and L give
 * z = zH zL and c = zH ? 2^l + cL : cH: bit l is zH (a wire) and the low bits a mux, in place on the
 * children's scratch: cL ^= cH, then cH ^= zH cL, zH fanned out to one copy per mux.
 * An all-zero node ends with c = 2^l - 1, so at the root c ^= z gives n (a power of two then). */
static void clz_tree(cq_tape *t, const cq_bit *const *x, uint32_t n, const cq_bit **r) {
    uint32_t L = 0;
    while ((1u << L) < n) L++;
    uint32_t P = 1u << L, pad = P - n;
    cq_bit *z[P], *c[P][L + 1];
    cq_bit *leaf = cq_tape_bits(t, P);
    for (uint32_t i = 0; i < P; i++) {
        cq_tape_x(t, &leaf[i]);
        cq_tape_cx(t, i < pad ? &ONE_ : x[i - pad], &leaf[i]);
        z[i] = &leaf[i];
    }
    for (uint32_t l = 0; l < L; l++)
        for (uint32_t i = 0; i < P >> (l + 1); i++) {
            cq_bit *zH = z[2 * i + 1], *zL = z[2 * i], **cH = c[2 * i + 1], **cL = c[2 * i];
            cq_bit *zn = cq_tape_bits(t, 1), *cp = l > 1 ? cq_tape_fanout(t, zH, l - 1) : NULL;
            cq_tape_ccx(t, zH, zL, zn);
            for (uint32_t k = 0; k < l; k++) {
                cq_tape_cx(t, cH[k], cL[k]);
                cq_tape_ccx(t, k ? &cp[k - 1] : zH, cL[k], cH[k]);
                c[i][k] = cH[k];
            }
            c[i][l] = zH;
            z[i] = zn;
        }
    cq_bit *zf = L > 1 ? cq_tape_fanout(t, z[0], L - 1) : NULL;   /* one copy per write, in parallel */
    for (uint32_t k = 0; k < L; k++) { cq_tape_cx(t, k ? &zf[k - 1] : z[0], c[0][k]); r[k] = c[0][k]; }
    if (!pad) r[L] = z[0];
}

/* ---- The registry kernels: o ^= the count of a's bits, a read in order (rev: reversed). */
static void count(int32_t o, const int32_t *args, uint32_t w, int rev,
                  void (*f)(cq_tape *, const cq_bit *const *, uint32_t, const cq_bit **)) {
    cq_bit *ob = cq_rail_bits(o), *a = cq_rail_bits(args[0]);
    const cq_bit *x[w], *r[32];
    for (uint32_t i = 0; i < w; i++) x[i] = &a[rev ? w - 1 - i : i];
    cq_tape t = { 0 };
    f(&t, x, w, r);
    for (uint32_t k = 0, m = cq_count_width(w); k < m; k++) cq_cx(r[k], &ob[k]);
    cq_tape_undo(&t);
}
void cq_k_ctpop_linear(int32_t o, const int32_t *a, uint32_t w) { count(o, a, w, 0, pop_linear); }
void cq_k_ctpop_csa   (int32_t o, const int32_t *a, uint32_t w) { count(o, a, w, 0, pop_csa); }
void cq_k_ctlz_thermo (int32_t o, const int32_t *a, uint32_t w) { count(o, a, w, 0, clz_thermo); }
void cq_k_ctlz_tree   (int32_t o, const int32_t *a, uint32_t w) { count(o, a, w, 0, clz_tree); }
void cq_k_cttz_thermo (int32_t o, const int32_t *a, uint32_t w) { count(o, a, w, 1, clz_thermo); }
void cq_k_cttz_tree   (int32_t o, const int32_t *a, uint32_t w) { count(o, a, w, 1, clz_tree); }

void cq_popcount(int kind, cq_tape *t, const cq_bit *const *x, uint32_t n, const cq_bit **r) {
    (kind == CQ_POPCOUNT_CSA ? pop_csa : pop_linear)(t, x, n, r);
}
void cq_clz(int kind, cq_tape *t, const cq_bit *const *x, uint32_t n, const cq_bit **r) {
    (kind == CQ_CLZ_TREE ? clz_tree : clz_thermo)(t, x, n, r);
}
