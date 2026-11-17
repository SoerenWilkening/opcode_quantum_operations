/* The angle-ladder reduction K = k(A) as a circuit (kmgy.18; BACKEND_ABI §1.5, §1.7;
 * docs/ANGLE_LADDER.md §5), bit for bit the host cq_angle_k (src/angle_k.c):
 *   [e == 0], [e == 0x7FF] by the EQ compare; e' = e with e'_0 = e_0 ^ [e == 0] (a subnormal has e = 1);
 *   m = (f, [e != 0]) AND NOT [e == 0x7FF] (Inf/NaN: m = 0, so k = 0);
 *   W = window(e', s) of 1/pi, by a barrel shift of the constant or a one-hot table;
 *   p = bits L-1 .. L+N-1 of m * W (cq_mul); K = (p_hi ^ sign) + (p_round ^ sign), since
 *   -(x + r) = ~x + ~r: the rounding and the negation in one N-bit add. */
#include <stdio.h>
#include <stdlib.h>
#include "cq_rt.h"
#include "kernels.h"
#include "../angle_k.h"

#define N CQ_LADDER_N
#define WB CQ_ANGLE_WBITS

static int pi_bit(uint32_t j) { return j < CQ_INV_PI_J && (cq_inv_pi[j / 64] >> (j % 64) & 1); }
/* window(e1, s) bit i = pi_bit(base(s) + ~e1 + i), ~e1 the 11-bit complement (cq_angle_window's shift) */
static uint32_t base(int s) { return (uint32_t)(CQ_INV_PI_J + 1075 - N + s - CQ_ANGLE_L - 2047); }

/* MSB first: after bit b only WB + 2^b - 1 bits can still reach the window. Each stage is the select
 * o = e1_b ? y_i : y_(i+2^b) on overlapping arms, e1_b fanned out (cq_select emits pass by pass). */
static cq_bit *barrel(cq_tape *t, const cq_bit *const *e1, int s) {
    uint32_t n = WB + 2047;
    cq_bit *y = cq_tape_bits(t, n);
    for (uint32_t i = 0; i < n; i++) if (pi_bit(base(s) + i)) cq_tape_x(t, &y[i]);   /* classical: no gate */
    for (int b = 10; b >= 0; b--) {
        uint32_t m = WB + (1u << b) - 1;
        cq_bit *o = cq_tape_bits(t, m);
        cq_select(CQ_SEL_DIFF_FANOUT, t, e1[b], y, y + (1u << b), o, m, 1);
        y = o;
    }
    return y;
}

/* One-hot decoder ind[v] = [e1 == v] (each level's bit fanned out), then W_j ^= ind[v] for every 1-bit. */
static cq_bit *table(cq_tape *t, const cq_bit *const *e1, int s) {
    static const cq_bit one = { CQ_BIT_ONE, 0 };
    const cq_bit **cur = malloc(2048 * sizeof *cur), **nxt = malloc(2048 * sizeof *nxt);
    if (!cur || !nxt) { fprintf(stderr, "cq_runtime: out of memory\n"); abort(); }
    cur[0] = &one;
    for (int b = 10, n = 1; b >= 0; b--, n *= 2) {
        cq_bit *f = n > 1 ? cq_tape_fanout(t, e1[b], (uint32_t)n - 1) : NULL, *d = cq_tape_bits(t, 2 * (uint32_t)n);
        for (int v = 0; v < n; v++) {
            cq_tape_ccx(t, cur[v], v ? &f[v - 1] : e1[b], &d[2 * v + 1]);
            cq_tape_cx(t, cur[v], &d[2 * v]);
            cq_tape_cx(t, &d[2 * v + 1], &d[2 * v]);
        }
        for (int v = 0; v < 2 * n; v++) nxt[v] = &d[v];
        const cq_bit **x = cur; cur = nxt; nxt = x;
    }
    cq_bit *w = cq_tape_bits(t, WB);
    for (uint32_t v = 0; v < 2048; v++)
        for (uint32_t i = 0; i < WB; i++) if (pi_bit(base(s) + 2047 - v + i)) cq_tape_cx(t, cur[v], &w[i]);
    free(cur); free(nxt);
    return w;
}

void cq_angle_k_circuit(int kind, cq_tape *t, const cq_bit *a, int s, const cq_bit **k) {
    static const cq_bit zeros[N > 11 ? N : 11], ones[11] = {
        { 1, 0 }, { 1, 0 }, { 1, 0 }, { 1, 0 }, { 1, 0 }, { 1, 0 }, { 1, 0 }, { 1, 0 }, { 1, 0 }, { 1, 0 }, { 1, 0 } };
    const cq_bit *e = a + 52;
    cq_bit *f = cq_tape_bits(t, 4);   /* [e == 0], [e == 0x7FF], e'_0, [e != 0] and not Inf/NaN */
    cq_cmp(cq_cmp_kind(CQ_CMP_EQ, 11), t, CQ_CMP_EQ, e, zeros, 11, &f[0], 1);
    cq_cmp(cq_cmp_kind(CQ_CMP_EQ, 11), t, CQ_CMP_EQ, e, ones, 11, &f[1], 1);
    cq_tape_cx(t, &e[0], &f[2]); cq_tape_cx(t, &f[0], &f[2]);
    cq_tape_cx(t, &f[0], &f[3]); cq_tape_cx(t, &f[1], &f[3]); cq_tape_x(t, &f[3]);   /* the two never hold together */
    const cq_bit *e1[11] = { &f[2] };
    for (int b = 1; b < 11; b++) e1[b] = &e[b];

    cq_bit *fin = cq_tape_bits(t, 1), *m = cq_tape_bits(t, 53);
    cq_tape_cx(t, &f[1], fin); cq_tape_x(t, fin);
    cq_bit *fc = cq_tape_fanout(t, fin, 51);
    for (uint32_t i = 0; i < 52; i++) cq_tape_ccx(t, &a[i], i ? &fc[i - 1] : fin, &m[i]);
    cq_tape_cx(t, &f[3], &m[52]);

    cq_bit *w = kind == CQ_ANGLE_TABLE ? table(t, e1, s) : barrel(t, e1, s);
    const cq_bit *p[N + 1];
    cq_mul(cq_mul_kind(WB), t, m, 53, w, WB, 0, CQ_ANGLE_L - 1, N + 1, p);

    cq_bit *sg = cq_tape_fanout(t, &a[63], N), *x = cq_tape_bits(t, N + 1);
    for (uint32_t i = 0; i <= N; i++) { cq_tape_cx(t, p[i], &x[i]); cq_tape_cx(t, i ? &sg[i - 1] : &a[63], &x[i]); }
    cq_bit *sum = cq_adder(cq_adder_kind(N), t, x + 1, zeros, 0, &x[0], N, CQ_ADD_SUM).s;
    for (uint32_t i = 0; i < N; i++) k[i] = &sum[i];
}

int cq_angle_k_kind(void) { return (int)(cq_kernel_pick(CQ_KOP_ANGLE_K, 64) - cq_kernel_ops[CQ_KOP_ANGLE_K].cand); }

/* Registry kernels: o (N bits) ^= k(A) for P = 4pi; the phase form differs only in the constant. */
static void kern(int kind, int32_t o, const int32_t *args, uint32_t w) {
    (void)w;
    cq_tape t = { 0 };
    const cq_bit *k[N];
    cq_bit *ob = cq_rail_bits(o);
    cq_angle_k_circuit(kind, &t, cq_rail_bits(args[0]), 2, k);
    for (uint32_t i = 0; i < N; i++) cq_cx(k[i], &ob[i]);
    cq_tape_undo(&t);
}
void cq_k_angle_k_barrel(int32_t o, const int32_t *a, uint32_t w) { kern(CQ_ANGLE_BARREL, o, a, w); }
void cq_k_angle_k_table(int32_t o, const int32_t *a, uint32_t w) { kern(CQ_ANGLE_TABLE, o, a, w); }
