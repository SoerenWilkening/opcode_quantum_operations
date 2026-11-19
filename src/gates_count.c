/* Counting gate backend: no state and no circuit, only the gate counts, the depth and the number
 * of qubits. At exit it writes them to the file named by the environment variable CQ_COUNT,
 * else stdout, one "name value" line each (tests/count_expected.txt).
 *
 * Depth: one number per qubit, the layer of its last gate. A gate lands on 1 + the largest of
 * its qubits' numbers, and that becomes the number of each of them; the depth is the largest.
 *
 * shortcut: m returns 0 (there is no state), so a program that branches on a measurement counts
 * the 0 branch only; the replay backend (cq_replay.h) counts the same and follows the branches.
 * shortcut: the counts are written by atexit, so a program that aborts reports none. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cq_gates.h"

enum { X, CNOT, CCX, RY, RY_CTL, RZ, M, KINDS };
static const char *const names[KINDS] = { "x", "cnot", "ccx", "ry", "ry_ctl", "rz", "m" };

static uint64_t *when;   /* per qubit: layer of its last gate, 0 if none */
static size_t    cap;
static uint64_t  kinds[KINDS], depth, qubits;

static void report(void) {
    const char *path = getenv("CQ_COUNT");
    FILE *f = path ? fopen(path, "w") : stdout;
    uint64_t total = 0;
    if (!f) { fprintf(stderr, "cq_count: cannot open the file named by CQ_COUNT\n"); return; }
    for (int k = 0; k < KINDS; k++) total += kinds[k];
    fprintf(f, "gates %llu\n", (unsigned long long)total);
    for (int k = 0; k < KINDS; k++) fprintf(f, "%s %llu\n", names[k], (unsigned long long)kinds[k]);
    fprintf(f, "depth %llu\nqubits %llu\n", (unsigned long long)depth, (unsigned long long)qubits);
    if (f != stdout) fclose(f); else fflush(f);
}

static uint64_t *at(uint32_t q) {
    if ((size_t)q >= cap) {
        size_t n = cap ? cap : 64;
        if (!cap) atexit(report);
        while (n <= q) n *= 2;
        if (!(when = realloc(when, n * sizeof *when))) { fprintf(stderr, "cq_count: out of memory\n"); abort(); }
        memset(when + cap, 0, (n - cap) * sizeof *when);
        cap = n;
    }
    return &when[q];
}

static void gate(int k, const uint32_t *qs, int n) {
    uint64_t layer = 0;
    for (int i = 0; i < n; i++) {
        uint64_t w = *at(qs[i]);
        if (!w) qubits++;
        if (w > layer) layer = w;
    }
    layer++;
    for (int i = 0; i < n; i++) when[qs[i]] = layer;
    if (layer > depth) depth = layer;
    kinds[k]++;
}

void __quantum__qis__x__body(uint32_t q) { gate(X, &q, 1); }
void __quantum__qis__cnot__body(uint32_t c, uint32_t t) { gate(CNOT, (uint32_t[]){ c, t }, 2); }
void __quantum__qis__ccx__body(uint32_t a, uint32_t b, uint32_t t) { gate(CCX, (uint32_t[]){ a, b, t }, 3); }
void __quantum__qis__ry__body(double th, uint32_t q) { (void)th; gate(RY, &q, 1); }
void __quantum__qis__ry__ctl(double th, uint32_t c, uint32_t t) { (void)th; gate(RY_CTL, (uint32_t[]){ c, t }, 2); }
void __quantum__qis__rz__body(double th, uint32_t q) { (void)th; gate(RZ, &q, 1); }
int __quantum__qis__m__body(uint32_t q) { gate(M, &q, 1); return 0; }
