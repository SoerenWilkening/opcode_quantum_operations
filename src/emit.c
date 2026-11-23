/* The qubit pool and the emitter: NORTH_STAR's folding table, nothing else. */
#include <stdio.h>
#include <stdlib.h>
#include "cq_gates.h"
#include "cq_rt.h"

static void die(const char *msg) {
    fprintf(stderr, "cq_runtime: %s\n", msg);
    abort();
}

/* ---- Pool */
static uint32_t *stack, n_free, cap, minted;   /* stack and is_free both hold cap entries */
static uint8_t  *is_free;                      /* catches a double give, which would alias two bits */

uint32_t cq_pool_take(void) {
    if (n_free) { uint32_t q = stack[--n_free]; is_free[q] = 0; return q; }
    if (minted == cap) {
        if (cap > UINT32_MAX / 2) die("qubit pool exhausted");
        cap = cap ? 2 * cap : 64;
        stack = realloc(stack, cap * sizeof *stack);
        is_free = realloc(is_free, cap);
        if (!stack || !is_free) die("out of memory");
    }
    is_free[minted] = 0;
    return minted++;
}

void cq_pool_give(uint32_t q) {
    if (q >= minted || is_free[q]) die("release of a qubit that is not taken");
    is_free[q] = 1;
    stack[n_free++] = q;
}

uint32_t cq_pool_live(void)   { return minted - n_free; }
uint32_t cq_pool_minted(void) { return minted; }

/* ---- Gate stamps (the derivation note, cq_rt.h): gate k stamps its target qubit with k. */
static uint64_t n_gates, *last;
static uint32_t cap_last;

static uint32_t stamp(uint32_t q) {
    if (q >= cap_last) {
        uint32_t c = cap_last ? cap_last : 64;
        while (c <= q) c *= 2;
        if (!(last = realloc(last, c * sizeof *last))) die("out of memory");
        for (uint32_t i = cap_last; i < c; i++) last[i] = 0;
        cap_last = c;
    }
    last[q] = ++n_gates;
    return q;
}
uint64_t cq_gate_stamp(void) { return n_gates; }
uint64_t cq_qubit_stamp(uint32_t q) { return q < cap_last ? last[q] : 0; }

/* ---- Emitter */
static int same(const cq_bit *a, const cq_bit *b) {
    return a == b || (cq_bit_is_q(*a) && cq_bit_is_q(*b) && a->q == b->q);
}

void cq_get_qubit(cq_bit *b) {
    if (cq_bit_is_q(*b)) return;
    int one = b->kind == CQ_BIT_ONE;
    *b = (cq_bit){ CQ_BIT_Q, stamp(cq_pool_take()) };
    if (one) __quantum__qis__x__body(b->q);
}

void cq_x(cq_bit *t) {
    if (cq_bit_is_q(*t)) __quantum__qis__x__body(stamp(t->q));
    else t->kind ^= 1;
}

void cq_cx(const cq_bit *c, cq_bit *t) {
    if (same(c, t)) die("CX control is the target");
    if (!cq_bit_is_q(*c)) { if (c->kind == CQ_BIT_ONE) cq_x(t); return; }
    cq_get_qubit(t);
    __quantum__qis__cnot__body(c->q, stamp(t->q));
}

void cq_ccx(const cq_bit *a, const cq_bit *b, cq_bit *t) {
    if (same(a, t) || same(b, t)) die("CCX control is the target");
    if (a->kind == CQ_BIT_ZERO || b->kind == CQ_BIT_ZERO) return;
    if (a->kind == CQ_BIT_ONE) { cq_cx(b, t); return; }
    if (b->kind == CQ_BIT_ONE || same(a, b)) { cq_cx(a, t); return; }   /* a & a = a */
    cq_get_qubit(t);
    __quantum__qis__ccx__body(a->q, b->q, stamp(t->q));
}

void cq_ry(double theta, cq_bit *t) {
    cq_get_qubit(t);
    __quantum__qis__ry__body(theta, stamp(t->q));
}

void cq_ry_ctl(double theta, const cq_bit *c, cq_bit *t) {
    if (same(c, t)) die("Ry-ctl control is the target");
    if (!cq_bit_is_q(*c)) { if (c->kind == CQ_BIT_ONE) cq_ry(theta, t); return; }
    cq_get_qubit(t);
    __quantum__qis__ry__ctl(theta, c->q, stamp(t->q));
}

void cq_rz(double theta, cq_bit *t) {
    cq_get_qubit(t);
    __quantum__qis__rz__body(theta, stamp(t->q));
}

int cq_measure_bit(cq_bit *b) {
    if (!cq_bit_is_q(*b)) return b->kind;
    int v = __quantum__qis__m__body(stamp(b->q)) != 0;
    if (v) __quantum__qis__x__body(b->q);  /* every free pool index is |0> */
    cq_pool_give(b->q);
    *b = cq_bit_const(v);
    return v;
}

void cq_reset_bit(cq_bit *b) {
    if (cq_bit_is_q(*b) && __quantum__qis__m__body(stamp(b->q))) __quantum__qis__x__body(b->q);
    if (cq_bit_is_q(*b)) cq_pool_give(b->q);
    *b = cq_bit_const(0);
}
