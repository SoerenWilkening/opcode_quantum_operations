/* The memory bus with a quantum address (BACKEND_ABI §1.10, issue kmgy.21): the fan-out decoder
 * and the leaf operations of docs/QRAM_CONSTRUCTION.md §2.2, §2.3 and §3, in that note's names.
 * The reachable set is the 2^q assignments of the address rail's q qubits. Every ancilla is
 * taken from one tape A and released only when the call ends, so no ancilla is reused within the
 * call: a reused qubit would carry its last layer and break the note's schedule. */
#include <stdio.h>
#include <stdlib.h>
#include "cq_config.h"
#include "cq_bus.h"
#include "kernels.h"

static void die(const char *msg, unsigned long long v) {
    fprintf(stderr, "cq_runtime: %s (%llu)\n", msg, v);
    abort();
}

static void *alloc(size_t n, size_t size) {
    void *p = calloc(n ? n : 1, size);
    if (!p) die("bus: out of memory, elements", n);
    return p;
}

typedef struct { uint64_t a; uint32_t k; } ref;   /* a cell address and its (leaf, byte) index */
static int by_addr(const void *x, const void *y) {
    uint64_t a = ((const ref *)x)->a, b = ((const ref *)y)->a;
    return (a > b) - (a < b);
}

typedef struct {
    uint32_t N, S, wb;     /* leaves; cells and bits per leaf (wb: 8S for a store, else w) */
    cq_bit **G;            /* the flag plane: G[a*wb + i] */
    cq_bit *cell;          /* 8 bits per distinct reachable cell */
    uint32_t *at;          /* at[a*S + j]: the distinct cell of byte j of leaf a */
    cq_tape *A;            /* owns every ancilla */
} qram;

static cq_bit *kappa(const qram *r, uint32_t a, uint32_t i) {
    return &r->cell[8 * r->at[a * r->S + i / 8] + i % 8];
}

/* t ^= flag ∧ cell bit for every unknown cell bit; X, n (first pass only): the parity tree inputs.
 * A known cell bit takes no gate: a 1 feeds its flag copy into the tree, a 0 is left out. */
static void and_pass(const qram *r, cq_bit *t, cq_bit **X, uint32_t *n) {
    for (uint32_t i = 0; i < r->wb; i++) {
        if (X) n[i] = 0;
        for (uint32_t a = 0; a < r->N; a++) {
            cq_bit *k = kappa(r, a, i), *g = r->G[(size_t)a * r->wb + i], *u = &t[(size_t)a * r->wb + i];
            if (cq_bit_is_q(*k)) cq_ccx(g, k, u);
            if (X && (cq_bit_is_q(*k) || k->kind)) X[(size_t)i * r->N + n[i]++] = cq_bit_is_q(*k) ? u : g;
        }
    }
}

/* LOAD(y): y_i ^= bit i of the flagged leaf. The AND is undone in its forward order, not
 * reversed: with overlapping leaves (§4.4) that is the order the note measured, 2(m-1) -> m-1. */
static void load(const qram *r, cq_bit *y, cq_bit *t, cq_bit **X, uint32_t *n) {
    cq_tape T = { 0 };
    and_pass(r, t, X, n);
    for (uint32_t i = 0; i < r->wb; i++)   /* TREE, in place: x[0] ends as the parity */
        for (uint32_t s = 1; s < n[i]; s *= 2)
            for (uint32_t m = 0; m + s < n[i]; m += 2 * s) cq_tape_cx(&T, X[(size_t)i * r->N + m + s], X[(size_t)i * r->N + m]);
    for (uint32_t i = 0; i < r->wb; i++)
        if (n[i]) cq_cx(X[(size_t)i * r->N], &y[i]);
    cq_tape_undo(&T);
    and_pass(r, t, NULL, NULL);
}

/* BCAST(s): cell bit i of every leaf ^= flag ∧ s_i, s_i copied to N places in U (from A). */
static void bcast(const qram *r, const cq_bit *s, cq_bit *U) {
    cq_tape C = { 0 };
    const cq_bit **u = alloc(r->N, sizeof *u);
    for (uint32_t i = 0; i < r->wb; i++) {
        u[0] = &s[i];
        for (uint32_t a = 1; a < r->N; a++) u[a] = &U[(size_t)i * (r->N - 1) + a - 1];
        for (uint32_t st = 1; st < r->N; st *= 2)
            for (uint32_t j = 0; j < st && j + st < r->N; j++) cq_tape_cx(&C, u[j], (cq_bit *)u[j + st]);
        for (uint32_t a = 0; a < r->N; a++) cq_ccx(r->G[(size_t)a * r->wb + i], u[a], kappa(r, a, i));
    }
    free(u);
    cq_tape_undo(&C);
}

void cq_qram_access(int op, const cq_bit *c, const cq_bit *addr, cq_bit *h, uint32_t w) {
    uint32_t pos[64], q = 0;
    uint64_t base = 0;
    for (uint32_t i = 0; i < 64; i++)
        if (cq_bit_is_q(addr[i])) pos[q++] = i; else base |= (uint64_t)addr[i].kind << i;
    if (q > CQ_MAX_QADDR_BITS) {
        fprintf(stderr, "cq_runtime: bus: the address rail has %u quantum bits, above the cap CQ_MAX_QADDR_BITS = %d. "
                "The cap is this library's build-time resource limit (ancilla cost is O(w 2^q)), not the ABI's\n",
                q, CQ_MAX_QADDR_BITS);
        abort();
    }
    uint32_t S = (w + 7) / 8, N = 1u << q;
    cq_tape A = { 0 };
    qram r = { N, S, op == CQ_BUS_STORE ? 8 * S : w, NULL, NULL, NULL, &A };

    /* The reachable cells, each read once: leaves that share a cell (§4.4) share its bits. */
    ref *rs = alloc((size_t)N * S, sizeof *rs);
    for (uint32_t a = 0; a < N; a++) {
        uint64_t la = base;
        for (uint32_t k = 0; k < q; k++) la |= (uint64_t)(a >> k & 1) << pos[k];
        for (uint32_t j = 0; j < S; j++) rs[a * S + j] = (ref){ la + j, a * S + j };   /* wraps mod 2^64 */
    }
    qsort(rs, (size_t)N * S, sizeof *rs, by_addr);
    r.cell = alloc((size_t)8 * N * S, sizeof *r.cell);
    r.at = alloc((size_t)N * S, sizeof *r.at);
    uint32_t nc = 0;
    for (size_t k = 0; k < (size_t)N * S; k++) {
        if (!k || rs[k].a != rs[k - 1].a) cq_cell_read(rs[k].a, &r.cell[8 * nc++]);
        r.at[rs[k].k] = nc - 1;
    }

    /* FO-DECODE: token at the root, address bit q-1-l copied to the 2^l nodes of level l, then
     * per node y = f ∧ bit, f ^= y: f becomes child 2j (bit 0) and y child 2j+1. */
    cq_bit **f = alloc(N, sizeof *f), *fb = cq_tape_bits(&A, N);
    f[0] = &fb[0];
    if (c) cq_tape_cx(&A, c, f[0]); else cq_tape_x(&A, f[0]);
    const cq_bit **B = alloc(N, sizeof *B);
    for (uint32_t l = 0, nl = 1; l < q; l++, nl *= 2) {
        const cq_bit *b = &addr[pos[q - 1 - l]], *cp = nl > 1 ? cq_tape_fanout(&A, b, nl - 1) : NULL;
        B[0] = b;
        for (uint32_t j = 1; j < nl; j++) B[j] = &cp[j - 1];
        for (uint32_t j = nl; j-- > 0;) {
            cq_bit *y = &fb[nl + j];
            cq_tape_ccx(&A, f[j], B[j], y);
            cq_tape_cx(&A, y, f[j]);
            f[2 * j + 1] = y;
            f[2 * j] = f[j];
        }
    }
    free(B);
    /* PLANE: wb copies of each flag, so no flag controls two gates in one layer. */
    r.G = alloc((size_t)N * r.wb, sizeof *r.G);
    for (uint32_t a = 0; a < N; a++) {
        cq_bit *cp = r.wb > 1 ? cq_tape_fanout(&A, f[a], r.wb - 1) : NULL;
        r.G[(size_t)a * r.wb] = f[a];
        for (uint32_t i = 1; i < r.wb; i++) r.G[(size_t)a * r.wb + i] = &cp[i - 1];
    }
    free(f);

    cq_bit **X = alloc((size_t)N * r.wb, sizeof *X), *t = cq_tape_bits(&A, N * r.wb);
    uint32_t *n = alloc(r.wb, sizeof *n);
    if (op == CQ_BUS_LOAD) load(&r, h, t, X, n);
    else {
        /* Every reachable cell becomes QUANTUM (Overlay); a HOST bit gets its qubit from the
         * emitter when a gate first targets it. */
        cq_bit *U = cq_tape_bits(&A, r.wb * (N - 1));
        if (op == CQ_BUS_SWAP) { bcast(&r, h, U); load(&r, h, t, X, n); bcast(&r, h, U); }
        else {   /* store: ts = old cells, then cells ^= ts ^ v = old ^ v, so they hold v */
            cq_bit *ts = cq_rail_bits(cq_rail_alloc(8 * S, NULL));
            load(&r, ts, t, X, n);
            for (uint32_t i = 0; i < w; i++) cq_cx(&h[i], &ts[i]);
            bcast(&r, ts, U);
            for (uint32_t i = 0; i < w; i++) cq_cx(&h[i], &ts[i]);
        }
        for (size_t k = 0; k < (size_t)N * S; k++)
            if (!k || rs[k].a != rs[k - 1].a) cq_cell_write(rs[k].a, &r.cell[8 * r.at[rs[k].k]], 1);
    }
    cq_tape_undo(&A);   /* PLANE and DECODE reversed; every ancilla back to the pool */
    free(X); free(n); free(r.G); free(r.cell); free(r.at); free(rs);
}
