/* The multiplier (kmgy.11): cq_mul (kernels.h) and the BACKEND_ABI §2.2 mul and §2.7
 * umul_overflow, smul_overflow kernels built on it. Two constructions, both recorded on a tape:
 *   shift-and-add: one row of partial products per bit of y, added one after the other by
 *     cq_adder (the adder add ships at that row's width);
 *   tree: every partial product in one CCX layer (operand bits fanned out first, so no bit
 *     controls two gates of the layer), a Dadda carry-save reduction to two rows, one cq_adder.
 * Signed products use the modified Baugh-Wooley array: the partial products of the sign rows are
 * complemented and a constant is added (below), so the circuit is the unsigned one. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cq_rt.h"
#include "kernels.h"

static const cq_bit one_ = { CQ_BIT_ONE, 0 }, zero_ = { CQ_BIT_ZERO, 0 };

static void *xalloc(size_t n, size_t size) {
    void *p = calloc(n ? n : 1, size);
    if (!p) { fprintf(stderr, "cq_runtime: out of memory\n"); abort(); }
    return p;
}

typedef struct {
    cq_tape *t;
    const cq_bit *x, *y;
    uint32_t nx, ny, top;   /* product columns 0 .. top-1 are formed */
    int sgn;
    cq_bit **xf, **yf;      /* fan-out copies of x_i, y_j (NULL: classical or used once) */
    uint32_t *xk, *yk;      /* uses so far; first the bit itself, then its copies */
} pp;

/* Term (i, j) of column i + j: 0 none, 1 x_i y_j, 2 ~(x_i y_j). Baugh-Wooley: for signed operands
 * x_(nx-1) y_j (j < ny-1) and x_i y_(ny-1) (i < nx-1) weigh negatively; -z 2^k = ~z 2^k - 2^k, and the
 * -2^k sum to the constant of bw_const. */
static int term(const pp *m, uint32_t i, uint32_t j) {
    if (i + j >= m->top) return 0;
    if (m->sgn && (i == m->nx - 1) != (j == m->ny - 1)) return 2;
    return m->x[i].kind != CQ_BIT_ZERO && m->y[j].kind != CQ_BIT_ZERO;
}

/* c (top classical bits) += 2^(nx+ny-1) + 2^(nx-1) + 2^(ny-1) mod 2^top: the Baugh-Wooley constant,
 * -(2^(nx+ny-2) - 2^(nx-1)) - (2^(nx+ny-2) - 2^(ny-1)) mod 2^(nx+ny). No gate: c is classical. */
static void bw_const(cq_tape *t, cq_bit *c, uint32_t nx, uint32_t ny, uint32_t top) {
    const uint32_t pos[3] = { nx + ny - 1, nx - 1, ny - 1 };
    for (int k = 0; k < 3; k++) {
        uint32_t p = pos[k];
        for (; p < top && c[p].kind == CQ_BIT_ONE; p++) cq_tape_x(t, &c[p]);
        if (p < top) cq_tape_x(t, &c[p]);
    }
}

static uint32_t lg(uint32_t n) { uint32_t r = 0; while ((1u << r) < n) r++; return r; }

/* Fan every quantum operand bit out to one bit per use, so the partial products form in one layer. */
static void fan(pp *m) {
    uint32_t *xu = xalloc(m->nx, sizeof *xu), *yu = xalloc(m->ny, sizeof *yu);
    for (uint32_t i = 0; i < m->nx; i++)
        for (uint32_t j = 0; j < m->ny; j++)
            if (term(m, i, j)) { xu[i] += cq_bit_is_q(m->x[i]); yu[j] += cq_bit_is_q(m->y[j]); }
    m->xf = xalloc(m->nx, sizeof *m->xf); m->yf = xalloc(m->ny, sizeof *m->yf);
    m->xk = xalloc(m->nx, sizeof *m->xk); m->yk = xalloc(m->ny, sizeof *m->yk);
    for (uint32_t i = 0; i < m->nx; i++) if (xu[i] > 1) m->xf[i] = cq_tape_fanout(m->t, &m->x[i], xu[i] - 1);
    for (uint32_t j = 0; j < m->ny; j++) if (yu[j] > 1) m->yf[j] = cq_tape_fanout(m->t, &m->y[j], yu[j] - 1);
    free(xu); free(yu);
}
static const cq_bit *take(const cq_bit *v, cq_bit *f, uint32_t *k) {
    if (!cq_bit_is_q(*v)) return v;
    uint32_t u = (*k)++;
    return u ? &f[u - 1] : v;
}
static void unfan(pp *m) { free(m->xf); free(m->yf); free(m->xk); free(m->yk); }

/* ---- Shift-and-add: acc (columns j .. top-1, contiguous) += row j, the row's bits fresh. */
static void shift_add(pp *m, const cq_bit **res) {
    cq_bit *cur = cq_tape_bits(m->t, m->top);
    if (m->sgn) bw_const(m->t, cur, m->nx, m->ny, m->top);
    int have = m->sgn;   /* acc holds something; else the first row is the acc */
    uint32_t j = 0;
    for (; j < m->ny && j < m->top; j++) {
        uint32_t n = m->top - j, ni = m->nx < n ? m->nx : n, any = 0;
        for (uint32_t i = 0; i < ni && !any; i++) any = term(m, i, j) != 0;
        if (any) {
            cq_bit *row = cq_tape_bits(m->t, n);
            for (uint32_t i = 0; i < ni; i++) {
                int tm = term(m, i, j);
                if (!tm) continue;
                cq_tape_ccx(m->t, take(&m->x[i], m->xf[i], &m->xk[i]), take(&m->y[j], m->yf[j], &m->yk[j]), &row[i]);
                if (tm == 2) cq_tape_x(m->t, &row[i]);
            }
            cur = have ? cq_adder(cq_adder_kind(n), m->t, cur, row, 0, NULL, n, CQ_ADD_SUM).s : row;
            have = 1;
        }
        res[j] = cur++;
    }
    for (uint32_t c = j; c < m->top; c++) res[c] = &cur[c - j];
}

/* ---- Tree: columns of (bit, estimated ready time), Dadda stages, one final add. */
typedef struct { const cq_bit *b; uint32_t t; } cbit;
typedef struct { cbit *v; uint32_t n, cap; } col;
static void push(col *c, const cq_bit *b, uint32_t t) {
    if (b->kind == CQ_BIT_ZERO) return;   /* formed already, so a classical 0 stays 0 */
    if (c->n == c->cap) {
        c->cap = c->cap ? 2 * c->cap : 8;
        if (!(c->v = realloc(c->v, c->cap * sizeof *c->v))) { fprintf(stderr, "cq_runtime: out of memory\n"); abort(); }
    }
    c->v[c->n++] = (cbit){ b, t };
}
static int by_time(const void *a, const void *b) {
    uint32_t x = ((const cbit *)a)->t, y = ((const cbit *)b)->t;
    return (x > y) - (x < y);
}

/* One Dadda stage: every column to at most d bits, counting the carries it receives. A full adder
 * is depth 3 (each input and output in one gate per layer); in the top column the carry is
 * dropped (mod 2^top) and only the sum is formed. */
static void stage(cq_tape *t, col *cols, uint32_t top, uint32_t d) {
    col *nx = xalloc(top, sizeof *nx);
    uint32_t cin = 0;
    for (uint32_t c = 0; c < top; c++) {
        col *cc = &cols[c];
        qsort(cc->v, cc->n, sizeof *cc->v, by_time);
        uint32_t k = 0, total = cc->n + cin, cout = 0, last = c + 1 == top;
        uint32_t nfa = 0, nha = 0;
        for (uint32_t h = cc->n, tot = total; tot > d && h >= 2; cout++)   /* plan, to allocate once */
            if (tot - d >= 2 && h >= 3) { nfa++; h -= 3; tot -= 2; } else { nha++; h -= 2; tot -= 1; }
        cq_bit *o = cq_tape_bits(t, 2 * (nfa + nha));
        for (uint32_t a = 0; a < nfa + nha; a++, o += 2) {
            const cbit *x = &cc->v[k], *y = &cc->v[k + 1], *z = a < nfa ? &cc->v[k + 2] : NULL;
            uint32_t tm = x->t > y->t ? x->t : y->t;
            if (z && z->t > tm) tm = z->t;
            cq_bit *s = &o[0], *cy = &o[1];
            k += z ? 3 : 2;
            if (!cq_bit_is_q(*x->b) && !cq_bit_is_q(*y->b) && (!z || !cq_bit_is_q(*z->b))) {   /* classical: no tape bits */
                int u = x->b->kind, v = y->b->kind, w = z ? z->b->kind : 0;
                if (u ^ v ^ w) push(&nx[c], &one_, 0);
                if (!last && ((u & v) | (u & w) | (v & w))) push(&nx[c + 1], &one_, 0);
                continue;
            }
            if (z) {
                cq_tape_cx(t, x->b, s);
                if (!last) cq_tape_ccx(t, y->b, z->b, cy);
                cq_tape_cx(t, y->b, s);
                if (!last) cq_tape_ccx(t, x->b, z->b, cy);
                cq_tape_cx(t, z->b, s);
                if (!last) cq_tape_ccx(t, x->b, y->b, cy);
            } else {
                cq_tape_cx(t, x->b, s);
                if (!last) cq_tape_ccx(t, x->b, y->b, cy);
                cq_tape_cx(t, y->b, s);
            }
            push(&nx[c], s, tm + 3);
            if (!last) push(&nx[c + 1], cy, tm + 3);
        }
        for (; k < cc->n; k++) push(&nx[c], cc->v[k].b, cc->v[k].t);
        cin = cout;
        free(cc->v);
    }
    memcpy(cols, nx, top * sizeof *cols);
    free(nx);
}

static void tree(pp *m, const cq_bit **res) {
    cq_tape *t = m->t;
    uint32_t top = m->top;
    col *cols = xalloc(top, sizeof *cols);
    cq_bit *f = cq_tape_bits(t, m->nx * m->ny);
    for (uint32_t i = 0; i < m->nx; i++)
        for (uint32_t j = 0; j < m->ny; j++) {
            int tm = term(m, i, j), qx = cq_bit_is_q(m->x[i]), qy = cq_bit_is_q(m->y[j]);
            if (!tm) continue;
            uint32_t ready = 1 + (qx && m->xf[i] ? lg(m->ny) : 0);
            const cq_bit *a = take(&m->x[i], m->xf[i], &m->xk[i]), *b = take(&m->y[j], m->yf[j], &m->yk[j]);
            if (tm == 1 && !(qx && qy)) { push(&cols[i + j], qx ? a : qy ? b : &one_, 0); continue; }   /* a factor is 1 */
            cq_bit *p = &f[i * m->ny + j];
            cq_tape_ccx(t, a, b, p);
            if (tm == 2) cq_tape_x(t, p);
            push(&cols[i + j], p, ready);
        }
    if (m->sgn) {
        cq_bit *k = cq_tape_bits(t, top);
        bw_const(t, k, m->nx, m->ny, top);
        for (uint32_t c = 0; c < top; c++) if (k[c].kind == CQ_BIT_ONE) push(&cols[c], &one_, 0);
    }
    for (;;) {
        uint32_t h = 0, d = 2;
        for (uint32_t c = 0; c < top; c++) if (cols[c].n > h) h = cols[c].n;
        if (h <= 2) break;
        while (d * 3 / 2 < h) d = d * 3 / 2;   /* the largest Dadda height below h */
        stage(t, cols, top, d);
    }
    uint32_t c0 = 0;   /* columns below c0 hold at most one bit and receive no carry: final already */
    for (; c0 < top && cols[c0].n < 2; c0++) res[c0] = cols[c0].n ? cols[c0].v[0].b : &zero_;
    if (c0 < top) {
        uint32_t n = top - c0;
        cq_bit *r = cq_tape_bits(t, 2 * n);   /* the two rows, contiguous for the adder */
        for (uint32_t c = c0; c < top; c++)
            for (uint32_t k = 0; k < cols[c].n; k++) cq_tape_cx(t, cols[c].v[k].b, &r[k * n + c - c0]);
        cq_bit *s = cq_adder(cq_adder_kind(n), t, r, r + n, 0, NULL, n, CQ_ADD_SUM).s;
        for (uint32_t c = c0; c < top; c++) res[c] = &s[c - c0];
    }
    for (uint32_t c = 0; c < top; c++) free(cols[c].v);
    free(cols);
}

void cq_mul(int kind, cq_tape *t, const cq_bit *x, uint32_t nx, const cq_bit *y, uint32_t ny, int sgn,
            uint32_t lo, uint32_t n, const cq_bit **out) {
    pp m = { t, x, y, nx, ny, lo + n, sgn, NULL, NULL, NULL, NULL };
    if (!nx || !ny || (sgn && m.top > nx + ny)) { fprintf(stderr, "cq_mul: bad window\n"); abort(); }
    const cq_bit **res = xalloc(m.top, sizeof *res);
    fan(&m);
    if (kind == CQ_MUL_TREE) tree(&m, res); else shift_add(&m, res);
    memcpy(out, res + lo, n * sizeof *out);
    free(res);
    unfan(&m);
}

int cq_mul_kind(uint32_t w) {
    return (int)(cq_kernel_pick(CQ_KOP_MUL, w) - cq_kernel_ops[CQ_KOP_MUL].cand);
}

/* o ^= OR_k (h_k ^ h_(k+1)) over k < n (sgn), or OR_k h_k: NOT of an AND tree of the complements. */
static void or_into(cq_tape *t, const cq_bit **h, uint32_t n, int sgn, cq_bit *o) {
    cq_bit *e = cq_tape_bits(t, n);
    const cq_bit **v = xalloc(n, sizeof *v);
    for (uint32_t k = 0; k < n; k++) { cq_tape_x(t, &e[k]); cq_tape_cx(t, h[k], &e[k]); v[k] = &e[k]; }
    for (uint32_t k = 0; sgn && k < n; k++) cq_tape_cx(t, h[k + 1], &e[k]);   /* a second pass: one chained pass would be depth n */
    for (uint32_t m = n; m > 1; m = (m + 1) / 2) {
        cq_bit *a = cq_tape_bits(t, m / 2);
        for (uint32_t k = 0; k < m / 2; k++) { cq_tape_ccx(t, v[2 * k], v[2 * k + 1], &a[k]); v[k] = &a[k]; }
        if (m & 1) v[m / 2] = v[m - 1];
    }
    cq_cx(v[0], o);
    cq_x(o);
    free(v);
}

enum { MUL, UMULO, SMULO };
static void mul_k(int op, int kind, int32_t o, const int32_t *args, uint32_t w) {
    cq_bit *ob = cq_rail_bits(o), *a = cq_rail_bits(args[0]), *b = cq_rail_bits(args[1]);
    const cq_bit **p = xalloc(w + 1, sizeof *p);
    cq_tape t = { 0 };
    switch (op) {
    case MUL:
        cq_mul(kind, &t, a, w, b, w, 0, 0, w, p);
        for (uint32_t i = 0; i < w; i++) cq_cx(p[i], &ob[i]);
        break;
    case UMULO:   /* product >= 2^W: a bit of the high half is set */
        cq_mul(kind, &t, a, w, b, w, 0, w, w, p);
        or_into(&t, p, w, 0, ob);
        break;
    case SMULO:   /* the 2W-bit signed product fits in W bits iff bits W-1 .. 2W-1 are equal */
        cq_mul(kind, &t, a, w, b, w, 1, w - 1, w + 1, p);
        or_into(&t, p, w, 1, ob);
        break;
    }
    cq_tape_undo(&t);
    free(p);
}

#define MUL_K_(name, OP) \
    void cq_k_##name##_shift_add(int32_t o, const int32_t *a, uint32_t w) { mul_k(OP, CQ_MUL_SHIFT_ADD, o, a, w); } \
    void cq_k_##name##_tree(int32_t o, const int32_t *a, uint32_t w) { mul_k(OP, CQ_MUL_TREE, o, a, w); }
MUL_K_(mul, MUL)
MUL_K_(umul_overflow, UMULO)
MUL_K_(smul_overflow, SMULO)
