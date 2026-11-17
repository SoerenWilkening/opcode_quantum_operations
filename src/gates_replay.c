/* Replay gate backend: one classical bit per qubit, gate and depth counters. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cq_gates.h"
#include "cq_replay.h"

static uint8_t  *bit;    /* per qubit: 0/1 */
static uint8_t  *seen;   /* per qubit: touched by a gate */
static uint32_t *when;   /* per qubit: layer of its last gate */
static size_t    cap;
static uint64_t  kinds[CQ_G_KINDS], total, nseen;
static uint32_t  depth, maxidx;
static double    angles[CQ_REPLAY_ANGLES];
static uint64_t  nrot;

static void rot(double th) { if (nrot < CQ_REPLAY_ANGLES) angles[nrot] = th; nrot++; }

static void die(const char *msg) {
    fprintf(stderr, "cq_replay: out-of-contract call: %s\n", msg);
    abort();
}

static void grow(uint32_t q) {
    if ((size_t)q < cap) return;
    size_t n = cap ? cap : 64;
    while (n <= q) n *= 2;
    bit = realloc(bit, n); seen = realloc(seen, n); when = realloc(when, n * sizeof *when);
    if (!bit || !seen || !when) die("out of memory");
    memset(bit + cap, 0, n - cap); memset(seen + cap, 0, n - cap);
    memset(when + cap, 0, (n - cap) * sizeof *when);
    cap = n;
}

/* Count one gate on qubits qs[0..n): layer = 1 + max of their times. */
static void gate(cq_gate_kind k, const uint32_t *qs, int n) {
    uint32_t layer = 0;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (qs[i] == qs[j]) die("repeated qubit in one gate");
    for (int i = 0; i < n; i++) { grow(qs[i]); if (when[qs[i]] > layer) layer = when[qs[i]]; }
    layer++;
    for (int i = 0; i < n; i++) {
        when[qs[i]] = layer;
        if (!seen[qs[i]]) { seen[qs[i]] = 1; nseen++; }
        if (qs[i] > maxidx) maxidx = qs[i];
    }
    if (layer > depth) depth = layer;
    kinds[k]++; total++;
}

void __quantum__qis__x__body(uint32_t q) { gate(CQ_G_X, &q, 1); bit[q] ^= 1; }
void __quantum__qis__cnot__body(uint32_t c, uint32_t t) {
    uint32_t qs[] = {c, t}; gate(CQ_G_CNOT, qs, 2); bit[t] ^= bit[c];
}
void __quantum__qis__ccx__body(uint32_t a, uint32_t b, uint32_t t) {
    uint32_t qs[] = {a, b, t}; gate(CQ_G_CCX, qs, 3); bit[t] ^= bit[a] & bit[b];
}
void __quantum__qis__ry__body(double th, uint32_t q) { rot(th); gate(CQ_G_RY, &q, 1); }
void __quantum__qis__ry__ctl(double th, uint32_t c, uint32_t t) {
    uint32_t qs[] = {c, t}; rot(th); gate(CQ_G_RY_CTL, qs, 2);
}
void __quantum__qis__rz__body(double th, uint32_t q) { rot(th); gate(CQ_G_RZ, &q, 1); }
int __quantum__qis__m__body(uint32_t q) { gate(CQ_G_M, &q, 1); return bit[q]; }

void cq_replay_reset(void) {
    memset(bit, 0, cap); memset(seen, 0, cap); memset(when, 0, cap * sizeof *when);
    memset(kinds, 0, sizeof kinds); total = nseen = nrot = 0; depth = maxidx = 0;
}
int cq_replay_get_bit(uint32_t q) { return (size_t)q < cap ? bit[q] : 0; }
void cq_replay_set_bit(uint32_t q, int v) { grow(q); bit[q] = v != 0; }
uint64_t cq_replay_gates(void) { return total; }
uint64_t cq_replay_gates_of(cq_gate_kind k) { return kinds[k]; }
uint32_t cq_replay_depth(void) { return depth; }
uint32_t cq_replay_qubits(void) { return (uint32_t)nseen; }
uint32_t cq_replay_max_index(void) { return maxidx; }
double cq_replay_angle(uint64_t i) {
    if (i >= nrot || i >= CQ_REPLAY_ANGLES) die("no logged rotation at that index");
    return angles[i];
}
