/* The tape (a Bennett region) and the two adders every integer circuit builds on (kmgy.10).
 * API and contract: kernels.h, "The adder". */
#include <stdio.h>
#include <stdlib.h>
#include "cq_rt.h"
#include "kernels.h"

static void die(const char *msg) {
    fprintf(stderr, "cq_runtime: %s\n", msg);
    abort();
}

static void *grow(void *p, size_t *cap, size_t need, size_t size) {
    if (need <= *cap) return p;
    *cap = *cap ? 2 * *cap : 64;
    if (*cap < need) *cap = need;
    if (!(p = realloc(p, *cap * size))) die("out of memory");
    return p;
}

cq_bit *cq_tape_bits(cq_tape *t, uint32_t n) {
    cq_bit *b = calloc(n ? n : 1, sizeof *b);   /* all-zero bytes: CQ_BIT_ZERO */
    if (!b) die("out of memory");
    t->blk = grow(t->blk, &t->capblk, t->nblk + 1, sizeof *t->blk);
    t->blkn = grow(t->blkn, &t->capblkn, t->nblk + 1, sizeof *t->blkn);
    t->blk[t->nblk] = b;
    t->blkn[t->nblk++] = n;
    return b;
}

static void rec(cq_tape *t, const cq_bit *a, const cq_bit *b, cq_bit *q) {
    t->g = grow(t->g, &t->cap, t->n + 1, sizeof *t->g);
    t->g[t->n++] = (cq_tape_gate){ a, b, q };
}
void cq_tape_x(cq_tape *t, cq_bit *q) { cq_x(q); rec(t, NULL, NULL, q); }
void cq_tape_cx(cq_tape *t, const cq_bit *c, cq_bit *q) { cq_cx(c, q); rec(t, c, NULL, q); }
void cq_tape_ccx(cq_tape *t, const cq_bit *a, const cq_bit *b, cq_bit *q) { cq_ccx(a, b, q); rec(t, a, b, q); }

void cq_tape_undo(cq_tape *t) {
    while (t->n) {
        const cq_tape_gate *g = &t->g[--t->n];
        if (g->b) cq_ccx(g->a, g->b, g->t); else if (g->a) cq_cx(g->a, g->t); else cq_x(g->t);
    }
    for (size_t k = 0; k < t->nblk; k++) {
        for (uint32_t i = 0; i < t->blkn[k]; i++) {
            cq_bit *b = &t->blk[k][i];
            if (cq_bit_is_q(*b)) cq_pool_give(b->q);   /* |0> by the reversal */
            else if (b->kind != CQ_BIT_ZERO) die("tape bit not back to 0");
        }
        free(t->blk[k]);
    }
    free(t->g); free(t->blk); free(t->blkn);
    *t = (cq_tape){ 0 };
}

/* Doubling: every bit holding v (v and the copies so far) copies into a fresh one. */
cq_bit *cq_tape_fanout(cq_tape *t, const cq_bit *v, uint32_t n) {
    cq_bit *f = cq_tape_bits(t, n);
    for (uint32_t have = 0; have < n;) {
        uint32_t k = have + 1 < n - have ? have + 1 : n - have;
        for (uint32_t j = 0; j < k; j++) cq_tape_cx(t, j ? &f[j - 1] : v, &f[have + j]);
        have += k;
    }
    return f;
}

/* P[i] = a_i ^ b_i ^ nb (i < n), Z[i+1] = g_i (i < m) with the carry-in folded into Z[1], Z[0] = cin.
 * Emission order keeps the input layer at depth 3 in both polarities. */
static void pg(cq_tape *t, const cq_bit *a, const cq_bit *b, int nb, const cq_bit *cin, uint32_t n, uint32_t m, cq_bit *P, cq_bit *Z) {
    for (uint32_t i = 0; i < n; i++) {
        if (nb) cq_tape_x(t, &P[i]);                    /* classical: no gate until P[i] is forced */
        if (i < m) cq_tape_ccx(t, &a[i], &b[i], &Z[i + 1]);
        cq_tape_cx(t, &b[i], &P[i]);
        if (nb && i < m) cq_tape_cx(t, &a[i], &Z[i + 1]);   /* a & ~b = a ^ (a & b) */
        cq_tape_cx(t, &a[i], &P[i]);
    }
    if (cin) {
        cq_tape_cx(t, cin, &Z[0]);
        if (m) cq_tape_ccx(t, cin, &P[0], &Z[1]);       /* c_1 = g_0 ^ cin p_0 */
    }
}

/* Z[i+1] ^= P[i] Z[i]: depth n - 1. */
static void prefix_ripple(cq_tape *t, cq_bit *P, cq_bit *Z, uint32_t n) {
    for (uint32_t i = 1; i < n; i++) cq_tape_ccx(t, &P[i], &Z[i], &Z[i + 1]);
}

/* Draper-Kutin-Rains-Svore §3: P-, G- and C-rounds, with X[0] = P. The P^-1 rounds are
 * left to the tape's undo. */
static void prefix_lookahead(cq_tape *t, cq_bit *P, cq_bit *Z, uint32_t n) {
    uint32_t L = 0;
    while ((2u << L) <= n) L++;                         /* floor(log2 n) */
    cq_bit *X[32] = { P };
    for (uint32_t s = 1; s + 1 <= L; s++) {             /* P-rounds 1 .. L-1 */
        X[s] = cq_tape_bits(t, n >> s);
        for (uint32_t m = 1; m < (n >> s); m++) cq_tape_ccx(t, &X[s - 1][2 * m], &X[s - 1][2 * m + 1], &X[s][m]);
    }
    for (uint32_t s = 1; s <= L; s++)                   /* G-rounds */
        for (uint32_t m = 0; m < (n >> s); m++)
            cq_tape_ccx(t, &Z[(m << s) + (1u << (s - 1))], &X[s - 1][2 * m + 1], &Z[(m << s) + (1u << s)]);
    for (uint32_t s = L; s >= 1; s--)                   /* C-rounds; empty while 3 * 2^(s-1) > n */
        for (uint32_t m = 1; (m << s) + (1u << (s - 1)) <= n; m++)
            cq_tape_ccx(t, &Z[m << s], &X[s - 1][2 * m], &Z[(m << s) + (1u << (s - 1))]);
}

cq_sum cq_adder(int kind, cq_tape *t, const cq_bit *a, const cq_bit *b, int nb, const cq_bit *cin, uint32_t n, int want) {
    /* Carries c_1 .. c_m: without the carry out the sum needs only c_1 .. c_(n-1). */
    uint32_t m = want & CQ_ADD_COUT || !n ? n : n - 1;
    cq_bit *P = cq_tape_bits(t, n), *Z = cq_tape_bits(t, n + 1);
    pg(t, a, b, nb, cin, n, m, P, Z);
    if (kind == CQ_ADDER_LOOKAHEAD) prefix_lookahead(t, P, Z, m); else prefix_ripple(t, P, Z, m);
    if (want & CQ_ADD_SUM) for (uint32_t i = 0; i < n; i++) cq_tape_cx(t, &Z[i], &P[i]);
    return (cq_sum){ want & CQ_ADD_SUM ? P : NULL, Z };
}

int cq_adder_kind(uint32_t n) {
    return (int)(cq_kernel_pick(CQ_KOP_ADD, n) - cq_kernel_ops[CQ_KOP_ADD].cand);
}
