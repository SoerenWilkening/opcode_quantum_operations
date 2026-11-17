/* The angle ladder for an angle rail with a qubit (kmgy.18; BACKEND_ABI §1.5, §1.7): K = k(A) on a
 * tape (src/kernels/ladder.c), N rotations controlled by K's bits, the tape undone (the reduction
 * again; K and every scratch bit back to |0>). The rotations are diagonal in K and about one axis, so
 * inv = 1 (cq_invert) is the same sequence with every angle negated. Inf/NaN gives K = 0: no rotation
 * fires on that branch. Angles are fl(P) 2^(i-N), exact scalings (docs/ANGLE_LADDER.md §6.1). */
#include <math.h>
#include "cq_rt.h"
#include "kernels.h"
#include "angle_k.h"

#define N CQ_LADDER_N

void cq_ladder(const cq_bit *c, cq_bit *h, uint32_t w, const cq_bit *A, int s, int inv) {
    if (c && !cq_bit_is_q(*c)) {
        if (c->kind == CQ_BIT_ZERO) return;
        c = NULL;
    }
    cq_tape t = { 0 };
    const cq_bit *k[N];
    cq_angle_k_circuit(cq_angle_k_kind(), &t, A, s, k);
    if (c) {   /* the bracket t_i ^= c AND K_i, kept until the undo */
        cq_bit *cf = cq_tape_fanout(&t, c, N - 1), *kc = cq_tape_bits(&t, N);
        for (uint32_t i = 0; i < N; i++) { cq_tape_ccx(&t, i ? &cf[i - 1] : c, k[i], &kc[i]); k[i] = &kc[i]; }
    }
    double P = ldexp(s == 2 ? 0x1.921fb54442d18p+3 : 0x1.921fb54442d18p+2, -N);
    if (inv) P = -P;
    if (!h) {   /* §1.7: P(2pi 2^(i-N)) on K_i, emitted as rz (uncontrolled: a global phase apart) */
        for (uint32_t i = 0; i < N; i++)   /* a tape bit; a classical one is a global phase */
            if (cq_bit_is_q(*k[i])) cq_rz(ldexp(P, (int)i), (cq_bit *)k[i]);
    } else {
        /* Round r: target q under K_((q + r) mod N), so W <= N targets take depth N; wider h uses
         * ceil(W/N) copies of each K bit, copy q / N for target q. */
        uint32_t cp = (w + N - 1) / N;
        const cq_bit *kk[N][(CQ_RAIL_MAX_WIDTH + N - 1) / N];
        for (uint32_t i = 0; i < N; i++) {
            cq_bit *f = cp > 1 ? cq_tape_fanout(&t, k[i], cp - 1) : NULL;
            for (uint32_t j = 0; j < cp; j++) kk[i][j] = j ? &f[j - 1] : k[i];
        }
        for (uint32_t r = 0; r < N; r++)
            for (uint32_t q = 0; q < w; q++) {
                uint32_t i = (q + r) % N;
                cq_ry_ctl(ldexp(P, (int)i), kk[i][q / N], &h[q]);
            }
    }
    cq_tape_undo(&t);
}
