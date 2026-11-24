/* The rotations of BACKEND_ABI §1.4-§1.7 with a classical angle: cqrt_ry_<W>[_controlled],
 * cqrt_rz_i1, and the _qangle forms when every bit of the angle rail A is classical (k on the
 * host, one rotation). A with a qubit is the angle ladder circuit, src/ladder.c. Types: include/cqrt.h.
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

/* A's bits if one of them is a qubit (the ladder circuit), else NULL. */
static const cq_bit *qangle(uint32_t A) {
    cq_bit *b = rail(A, 64);
    for (int i = 0; i < 64; i++) if (cq_bit_is_q(b[i])) return b;
    return NULL;
}

/* P*k(a)/2^N for the classical f64 rail A, P = 2^s*pi; NAN for a non-finite val(A).
 * The double is fl(k * fl(P)) * 2^-N: k is exact for N <= 53, one rounding of the product, an
 * exact scaling. That is the ladder's own total, sum of k_i * fl(P) * 2^(i-N), rounded once. */
static double angle(uint32_t A, int s) {
    cq_bit *b = rail(A, 64);
    uint64_t a = 0;
    for (int i = 0; i < 64; i++) a |= (uint64_t)b[i].kind << i;
    if (((a >> 52) & 0x7FF) == 0x7FF) return NAN;
    /* shortcut: for N > 53 (double)k rounds too; make it one rounding if N is ever raised. */
    return ldexp((double)cq_angle_k(a, s) * (s == 2 ? 0x1.921fb54442d18p+3 : 0x1.921fb54442d18p+2), -CQ_LADDER_N);
}

/* Every rotation goes through cq_invert (cq_rt.h): inv = 1 is the same rotation, angle negated.
 * A recorded _qangle form reads its angle rail when it runs. h[]: h, c, A; n[]: width, has c, has A. */
static void ry_k(const cq_call *k, int inv) {
    const cq_bit *qa = k->n[2] ? qangle(k->h[2]) : NULL;
    cq_note ct = cq_content_all(&k->h[0], 1);   /* ABI §1.10 "Data": a prepared input register is data */
    if (qa) cq_ladder(k->n[1] ? rail(k->h[1], 1) : NULL, rail(k->h[0], k->n[0]), k->n[0], qa, 2, inv);
    else {
        double theta = k->n[2] ? angle(k->h[2], 2) : k->theta;
        ry(k->n[1] ? rail(k->h[1], 1) : NULL, k->h[0], k->n[0], inv ? -theta : theta);
    }
    cq_content_set((int32_t)k->h[0], &ct);
}
#define RY_(bits, h, c, A, ctl, q, th) cq_call_do(&(cq_call){ .run = ry_k, .name = "ry", .h = { h, c, A }, .n = { bits, ctl, q }, .theta = th })
#define ROT_(n, bits, ...) \
    void cqrt_ry_##n(uint32_t h, double theta) { RY_(bits, h, 0, 0, 0, 0, theta); } \
    void cqrt_ry_##n##_controlled(uint32_t c, uint32_t h, double theta) { RY_(bits, h, c, 0, 1, 0, theta); } \
    void cqrt_ry_##n##_qangle(uint32_t h, uint32_t A) { RY_(bits, h, 0, A, 0, 1, 0); } \
    void cqrt_ry_##n##_qangle_controlled(uint32_t c, uint32_t h, uint32_t A) { RY_(bits, h, c, A, 1, 1, 0); }
CQ_WIDTHS(ROT_)
CQ_FP_WIDTHS(ROT_)

static void rz_i1_k(const cq_call *k, int inv) {
    cq_bit *b = rail(k->h[0], 1);
    if (isfinite(k->theta)) phase(inv ? -k->theta : k->theta, b);
}
void cqrt_rz_i1(uint32_t f, double theta) { cq_call_do(&(cq_call){ .run = rz_i1_k, .name = "rz", .h = { f }, .theta = theta }); }

/* A classical A gives every branch the same phase e^{i val(A)}: global, no gate. */
static void rz_qangle_k(const cq_call *k, int inv) {
    const cq_bit *qa = qangle(k->h[0]);
    if (qa) cq_ladder(NULL, NULL, 0, qa, 1, inv);
}
void cqrt_rz_qangle(uint32_t A) { cq_call_do(&(cq_call){ .run = rz_qangle_k, .name = "rz_qangle", .h = { A } }); }

/* §1.7's bracket with K classical (c AND K is c): t ^= c, P(2*pi*k/2^N) on t, t ^= c. */
static void rz_qangle_ctl_k(const cq_call *k, int inv) {
    const cq_bit *qa = qangle(k->h[1]);
    if (qa) { cq_ladder(rail(k->h[0], 1), NULL, 0, qa, 1, inv); return; }
    double theta = angle(k->h[1], 1);
    const cq_bit *cb = rail(k->h[0], 1);
    if (!isfinite(theta)) return;
    if (inv) theta = -theta;
    cq_bit t = cq_bit_const(0);
    cq_cx(cb, &t);
    phase(theta, &t);
    cq_cx(cb, &t);
    if (cq_bit_is_q(t)) cq_pool_give(t.q);   /* back at |0> */
}
void cqrt_rz_qangle_controlled(uint32_t c, uint32_t A) { cq_call_do(&(cq_call){ .run = rz_qangle_ctl_k, .name = "rz_qangle", .h = { c, A } }); }
