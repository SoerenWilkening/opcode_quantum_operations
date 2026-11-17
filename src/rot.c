/* The rotations of BACKEND_ABI §1.4-§1.7 with a classical angle: cqrt_ry_<W>[_controlled],
 * cqrt_rz_i1, and the _qangle forms when every bit of the angle rail A is classical (k on the
 * host, one rotation). A quantum A is the angle ladder circuit, kmgy.18. Types: include/cqrt.h.
 *
 * P(theta) is emitted as rz: they differ by a global phase, and no P here is under a control
 * (§1.7 brackets it with CX onto an ancilla). On a classical bit P is a global phase too, so
 * it emits nothing: an all-classical program emits no gate (NORTH_STAR "Done when" 3). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "cqrt.h"
#include "cq_rt.h"
#include "angle_k.h"

static void die(const char *msg, uint32_t h) {
    fprintf(stderr, "cq_runtime: %s (handle %u)\n", msg, h);
    abort();
}

static cq_bit *rail(uint32_t h, uint32_t w) {
    if (cq_rail_width((int32_t)h) != w) die("rail width does not match the call", h);
    return cq_rail_bits((int32_t)h);
}

static void phase(double theta, cq_bit *b) { if (cq_bit_is_q(*b)) cq_rz(theta, b); }

/* Ry(theta) on every bit of h, each under c unless c is NULL. Non-finite: identity, no gate. */
static void ry(const cq_bit *c, uint32_t h, uint32_t w, double theta) {
    cq_bit *b = rail(h, w);
    if (!isfinite(theta)) return;
    for (uint32_t i = 0; i < w; i++)
        if (c) cq_ry_ctl(theta, c, &b[i]); else cq_ry(theta, &b[i]);
}

/* P*k(a)/2^N for the classical f64 rail A, P = 2^s*pi; NAN for a non-finite val(A).
 * The double is fl(k * fl(P)) * 2^-N: k is exact for N <= 53, one rounding of the product, an
 * exact scaling. That is the ladder's own total, sum of k_i * fl(P) * 2^(i-N), rounded once. */
static double angle(uint32_t A, int s) {
    cq_bit *b = rail(A, 64);
    uint64_t a = 0;
    for (int i = 0; i < 64; i++) {
        if (cq_bit_is_q(b[i])) die("quantum angle rail: the angle ladder circuit is kmgy.18, not implemented yet", A);
        a |= (uint64_t)b[i].kind << i;
    }
    if (((a >> 52) & 0x7FF) == 0x7FF) return NAN;
    /* shortcut: for N > 53 (double)k rounds too; make it one rounding if N is ever raised. */
    return ldexp((double)cq_angle_k(a, s) * (s == 2 ? 0x1.921fb54442d18p+3 : 0x1.921fb54442d18p+2), -CQ_LADDER_N);
}

#define ROT_(n, bits, ...) \
    void cqrt_ry_##n(uint32_t h, double theta) { ry(NULL, h, bits, theta); } \
    void cqrt_ry_##n##_controlled(uint32_t c, uint32_t h, double theta) { ry(rail(c, 1), h, bits, theta); } \
    void cqrt_ry_##n##_qangle(uint32_t h, uint32_t A) { ry(NULL, h, bits, angle(A, 2)); } \
    void cqrt_ry_##n##_qangle_controlled(uint32_t c, uint32_t h, uint32_t A) { \
        double theta = angle(A, 2); ry(rail(c, 1), h, bits, theta); }
CQ_WIDTHS(ROT_)
CQ_FP_WIDTHS(ROT_)

void cqrt_rz_i1(uint32_t f, double theta) {
    cq_bit *b = rail(f, 1);
    if (isfinite(theta)) phase(theta, b);
}

/* A classical A gives every branch the same phase e^{i val(A)}: global, no gate. */
void cqrt_rz_qangle(uint32_t A) { (void)angle(A, 1); }

/* §1.7's bracket with K classical (c AND K is c): t ^= c, P(2*pi*k/2^N) on t, t ^= c. */
void cqrt_rz_qangle_controlled(uint32_t c, uint32_t A) {
    double theta = angle(A, 1);
    const cq_bit *cb = rail(c, 1);
    if (!isfinite(theta)) return;
    cq_bit t = cq_bit_const(0);
    cq_cx(cb, &t);
    phase(theta, &t);
    cq_cx(cb, &t);
    if (cq_bit_is_q(t)) cq_pool_give(t.q);   /* back at |0> */
}
