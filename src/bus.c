/* The memory bus of BACKEND_ABI §1.10 with a classical address: the overlay, the bus handle
 * and every load/store/swap entry point. An address rail holding a qubit is the QRAM path,
 * src/qram.c. Types and conventions: include/cqrt.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cqrt.h"
#include "cq_bus.h"

static void die(const char *msg, uint64_t v) {
    fprintf(stderr, "cq_runtime: %s (%llu)\n", msg, (unsigned long long)v);
    abort();
}

/* ---- The overlay: QUANTUM cells, open addressing keyed by address. An entry that goes back to
 * HOST stays as a non-quantum slot, so nothing is ever deleted. */
typedef struct { uint64_t a; uint8_t used, quantum, weak; cq_bit b[8]; cq_note t; } cell;   /* t, weak: cq_cell_kind */
static const cq_note none = { -1, { { 0, 0 } } };
static cell  *tab;
static size_t cap, used;    /* cap is 0 or a power of two */

static size_t slot_of(uint64_t a) { return (size_t)((a * 0x9e3779b97f4a7c15ull) >> 32) & (cap - 1); }

static cell *find(uint64_t a, int insert) {
    if (insert && 2 * (used + 1) > cap) {
        cell *old = tab;
        size_t n = cap;
        cap = cap ? 2 * cap : 64;
        if (!(tab = calloc(cap, sizeof *tab))) die("out of memory", cap);
        for (size_t i = 0; i < n; i++)
            if (old[i].used) { size_t j = slot_of(old[i].a); while (tab[j].used) j = (j + 1) & (cap - 1); tab[j] = old[i]; }
        free(old);
    }
    if (!cap) return NULL;
    for (size_t i = slot_of(a);; i = (i + 1) & (cap - 1)) {
        if (tab[i].used && tab[i].a == a) return &tab[i];
        if (!tab[i].used) {
            if (!insert) return NULL;
            used++;
            tab[i] = (cell){ .a = a, .used = 1 };
            return &tab[i];
        }
    }
}

/* host: the host byte if already read, or NULL to read it now. */
static int cell_bits(uint64_t a, cq_bit b[8], const uint8_t *host) {
    cell *e = find(a, 0);
    if (e && e->quantum) { memcpy(b, e->b, sizeof e->b); return 1; }
    uint8_t v = 0;
    if (host) v = *host;
    else if (!cq_host_read(a, &v, 1)) v = 0;
    for (int i = 0; i < 8; i++) b[i] = cq_bit_const((v >> i) & 1);
    return 0;
}

int cq_cell_read(uint64_t a, cq_bit b[8]) { return cell_bits(a, b, NULL); }

int cq_cells_read(uint64_t a, cq_bit *b, uint32_t n) {
    uint8_t host[256];
    int q = 0;
    for (uint32_t k = 0; k < n; k += 256) {   /* one host read per chunk; per cell where it fails */
        uint32_t m = n - k < 256 ? n - k : 256;
        int all = cq_host_read(a + k, host, m);
        for (uint32_t j = 0; j < m; j++) q |= cell_bits(a + k + j, &b[8 * (k + j)], all ? &host[j] : NULL);
    }
    return q;
}

void cq_cell_write(uint64_t a, const cq_bit b[8], int quantum) {
    uint8_t v = 0;
    for (int i = 0; i < 8; i++)
        if (cq_bit_is_q(b[i])) quantum = 1; else v |= (uint8_t)(b[i].kind << i);
    if (!quantum && cq_host_write(a, v)) {
        cell *e = find(a, 0);
        if (e) e->quantum = 0;
        return;
    }
    cell *e = find(a, 1);
    e->quantum = 1;
    e->weak = 0;
    e->t.n = -1;
    memcpy(e->b, b, sizeof e->b);
}

void cq_cell_note_set(uint64_t a, const cq_note *t, int weak) {
    cell *e = find(a, 0);
    if (e && e->quantum) { e->t = *t; e->weak = weak != 0; }
}

int cq_cell_kind(uint64_t a, cq_note *t, cq_word *m) {
    cell *e = find(a, 0);
    if (e && e->quantum) { *t = e->t; return e->weak; }
    if (!m->have || m->a != (a & ~7ull)) {
        uint64_t v = 0;   /* little-endian host, as the bus is (x86-64) */
        m->a = a & ~7ull; m->have = 1;
        if (!cq_host_read(m->a, (uint8_t *)&v, 8)) v = 0;
        m->t = cq_note_value(v);
        if (m->t.n < 0) m->t.n = 0;   /* a value in no object: data */
    }
    *t = m->t;
    return 0;
}

cq_note cq_note_cells(uint64_t a, const cq_bit *rb, int quantum) {
    cq_note t = { 0, { { 0, 0 } } }, ct;
    cq_word m = { 0 };
    uint64_t v = 0;
    for (int i = 0; i < 64; i++) v |= (uint64_t)(rb[i].kind & 1) << i;   /* HOST cells: classical bits */
    if (!quantum) return cq_note_value(v);
    for (int j = 0; j < 8; j++) {
        int weak = cq_cell_kind(a + (uint64_t)j, &ct, &m);
        t = ct.n > 0 && !weak ? cq_note_union(t, ct) : none;
    }
    return t;
}

/* ---- One access at a classical address (the _hl gate lists of §1.10 Impl). */
enum { LOAD = CQ_BUS_LOAD, STORE = CQ_BUS_STORE, SWAP = CQ_BUS_SWAP };
#define MAX_CELLS ((CQ_RAIL_MAX_WIDTH + 7) / 8)

/* h is the loaded or swapped rail, or the value v of a store; w bits. c: a qubit control, or
 * NULL for none (a classical control has been resolved by the caller). */
/* cn, weak: what the stored or swapped rail can hold ("Data") and whether it has no address note. The cell
 * rule, for the cells left QUANTUM: they get cn, under a control (they keep their content on c = 0) in
 * union with what they had: data adds nothing, unknown leaves unknown. */
static void access(int op, const cq_bit *c, uint64_t a, cq_bit *h, uint32_t w, cq_bit *slot, const cq_note *cn, int weak) {
    uint32_t s = (w + 7) / 8;
    cq_bit cells[8 * MAX_CELLS];
    uint8_t host[MAX_CELLS];
    int all = cq_host_read(a, host, s), pq = 0;   /* one read for the common case; else per cell */
    for (uint32_t j = 0; j < s; j++) pq |= cell_bits(a + j, &cells[8 * j], all ? &host[j] : NULL);   /* a + j wraps mod 2^64 */
    cq_note ot[MAX_CELLS];
    uint8_t ow[MAX_CELLS];
    cq_word m = { 0 };
    (void)pq;
    for (uint32_t j = 0; j < s && op != LOAD && c; j++) ow[j] = (uint8_t)cq_cell_kind(a + j, &ot[j], &m);
    if (op == LOAD) {   /* the cells are controls only: nothing to put back */
        for (uint32_t i = 0; i < w; i++)
            if (c) cq_ccx(c, &cells[i], &h[i]); else cq_cx(&cells[i], &h[i]);
        return;
    }
    /* A store swaps every bit of its cells with the fresh tape slot t, a swap the low w bits with h. */
    cq_bit *t = op != STORE ? h : slot ? slot : cq_rail_bits(cq_rail_alloc(8 * s, NULL));
    for (uint32_t i = 0, n = op == STORE ? 8 * s : w; i < n; i++)
        if (c) { cq_cx(&cells[i], &t[i]); cq_ccx(c, &t[i], &cells[i]); cq_cx(&cells[i], &t[i]); }
        else { cq_bit x = t[i]; t[i] = cells[i]; cells[i] = x; }   /* the swap as a relabeling: no gate */
    for (uint32_t i = 0; op == STORE && i < w; i++)
        if (c) cq_ccx(c, &h[i], &cells[i]); else cq_cx(&h[i], &cells[i]);
    /* Overlay: a qubit control, or a qubit left anywhere in the cells, makes them all QUANTUM. */
    int quantum = c != NULL;
    for (uint32_t i = 0; i < 8 * s; i++) quantum |= cq_bit_is_q(cells[i]);
    for (uint32_t j = 0; j < s; j++) {
        cq_cell_write(a + j, &cells[8 * j], quantum);
        cq_note u = c ? cq_note_union(ot[j], *cn) : *cn;
        int was = c && ot[j].n > 0, covered = was && !ow[j] && u.n == ot[j].n;   /* an address cell that already names them stays one */
        cq_cell_note_set(a + j, &u, (was && ow[j]) || (cn->n > 0 && weak && !covered));
    }
}

/* ---- The bus handle. It takes a rail slot only to get a handle number no rail has. */
static uint32_t bus;

uint32_t cqrt_qram_alloc_i8(uint32_t n) {
    if (n != 0) die("cqrt_qram_alloc_i8: n must be 0 (the one bus of 2^64 cells)", n);
    if (!bus) bus = (uint32_t)cq_rail_alloc(8, NULL);
    return bus;
}

static cq_bit *rail(uint32_t h, uint32_t w) {
    if (cq_rail_width((int32_t)h) != w) die("bus: rail width does not match the call, handle", h);
    return cq_rail_bits((int32_t)h);
}

/* ah: the address rail (plain forms), or NULL and the literal a (_hl forms).
 * ctl: the form has control c. Under a classical 0 control nothing is read or touched, and a
 * store still takes its tape slot, which stays |0>. */
static void bus_op(int op, int ctl, uint32_t c, uint32_t m, const uint32_t *ah, uint64_t a, uint32_t h, uint32_t w) {
    if (!bus || m != bus) die("bus: not the bus handle", m);
    cq_bit *hb = rail(h, w);
    const cq_bit *cb = ctl ? rail(c, 1) : NULL;
    if (cb && !cq_bit_is_q(*cb)) {
        if (cb->kind == CQ_BIT_ZERO) { if (op == STORE) cq_rail_alloc(8 * ((w + 7) / 8), NULL); return; }
        cb = NULL;
    }
    if (ah) {
        const cq_bit *ab = rail(*ah, 64);
        a = 0;
        for (int i = 0; i < 64; i++) {
            if (cq_bit_is_q(ab[i])) { cq_qram_access(op, cb, *ah, h, w); return; }
            a |= (uint64_t)ab[i].kind << i;
        }
    }
    /* The load rule (cq_rt.h): h = c ? the cells : 0, the cells' notes if QUANTUM. The cell rule: access. */
    /* The load rule (cq_rt.h): into a |0> h the address note of the 8 cells, and at any width cu, what the
     * cells read can hold; a swap leaves h with what it had or theirs. The cell rule: access. */
    int zero = cq_rail_zero((int32_t)h), note = op != STORE && w == 64 && zero, dh = op == SWAP || (op == LOAD && zero);
    cq_bit cb8[64];
    cq_note cn = cq_note_content((int32_t)h), ct, cu = zero ? (cq_note){ 0, { { 0, 0 } } } : cn;
    cq_note nt = note ? cq_note_cells(a, cb8, cq_cells_read(a, cb8, 8)) : none;
    int weak = !(w == 64 && cq_note_get((int32_t)h).n > 0);
    cq_word wm = { 0 };
    for (uint32_t j = 0; dh && j < (w + 7) / 8; j++) { cq_cell_kind(a + j, &ct, &wm); cu = cq_note_union(cu, ct); }
    access(op, cb, a, hb, w, NULL, &cn, weak);
    if (note) cq_note_set((int32_t)h, &nt);
    if (dh) cq_content_set((int32_t)h, &cu);
}

void cq_bus_store_at(const cq_bit *c, uint64_t a, cq_bit *v, uint32_t w, cq_bit *t) {
    if (c && !cq_bit_is_q(*c)) {
        if (c->kind == CQ_BIT_ZERO) { if (!t) cq_rail_alloc(8 * ((w + 7) / 8), NULL); return; }
        c = NULL;
    }
    access(STORE, c, a, v, w, t, &none, 0);   /* bytes the backend stores itself (§1.11): unknown */
}

/* cq_invert (cq_rt.h): a load or swap is its own inverse and is recorded; a store is a flush
 * point, performed at its call. h[]: c, m, addr rail, h; n[]: op, ctl, width, _hl; v[0]: addr. */
static void bus_k(const cq_call *k, int inv) {
    (void)inv;
    bus_op((int)k->n[0], (int)k->n[1], k->h[0], k->h[1], k->n[3] ? NULL : &k->h[2], k->v[0], k->h[3], k->n[2]);
}
static void bus_do(int op, int ctl, uint32_t c, uint32_t m, const uint32_t *ah, uint64_t a, uint32_t h, uint32_t w) {
    const uint32_t hs[4] = { c, m, ah ? *ah : 0, h };
    if (op == STORE) { cq_inv_flush(); cq_op_begin("store", 0, hs, 4); bus_op(op, ctl, c, m, ah, a, h, w); cq_op_end(); return; }
    cq_call_do(&(cq_call){ .run = bus_k, .name = op == LOAD ? "load" : "swap", .h = { c, m, ah ? *ah : 0, h }, .n = { (uint32_t)op, (uint32_t)ctl, w, !ah }, .v = { a } });
}

static uint32_t load(int ctl, uint32_t c, uint32_t m, const uint32_t *ah, uint64_t a, uint32_t w) {
    uint32_t h = (uint32_t)cq_rail_alloc(w, NULL);
    bus_do(LOAD, ctl, c, m, ah, a, h, w);
    return h;
}

/* Every width of CQ_WIDTHS and CQ_FP_WIDTHS; the fp tokens are bit patterns. */
#define BUS_(n, bits, ...) \
    uint32_t cqrt_qram_load_##n(uint32_t m, uint32_t addr) { return load(0, 0, m, &addr, 0, bits); } \
    uint32_t cqrt_qram_load_##n##_hl(uint32_t m, uint64_t addr) { return load(0, 0, m, NULL, addr, bits); } \
    uint32_t cqrt_qram_load_##n##_controlled(uint32_t c, uint32_t m, uint32_t addr) { return load(1, c, m, &addr, 0, bits); } \
    uint32_t cqrt_qram_load_##n##_hl_controlled(uint32_t c, uint32_t m, uint64_t addr) { return load(1, c, m, NULL, addr, bits); } \
    void cqrt_qram_load_##n##_unc(uint32_t h, uint32_t m, uint32_t addr) { bus_do(LOAD, 0, 0, m, &addr, 0, h, bits); } \
    void cqrt_qram_load_##n##_hl_unc(uint32_t h, uint32_t m, uint64_t addr) { bus_do(LOAD, 0, 0, m, NULL, addr, h, bits); } \
    void cqrt_qram_load_##n##_controlled_unc(uint32_t h, uint32_t c, uint32_t m, uint32_t addr) { bus_do(LOAD, 1, c, m, &addr, 0, h, bits); } \
    void cqrt_qram_load_##n##_hl_controlled_unc(uint32_t h, uint32_t c, uint32_t m, uint64_t addr) { bus_do(LOAD, 1, c, m, NULL, addr, h, bits); } \
    void cqrt_qram_store_##n(uint32_t m, uint32_t addr, uint32_t v) { bus_do(STORE, 0, 0, m, &addr, 0, v, bits); } \
    void cqrt_qram_store_##n##_hl(uint32_t m, uint64_t addr, uint32_t v) { bus_do(STORE, 0, 0, m, NULL, addr, v, bits); } \
    void cqrt_qram_store_##n##_controlled(uint32_t c, uint32_t m, uint32_t addr, uint32_t v) { bus_do(STORE, 1, c, m, &addr, 0, v, bits); } \
    void cqrt_qram_store_##n##_hl_controlled(uint32_t c, uint32_t m, uint64_t addr, uint32_t v) { bus_do(STORE, 1, c, m, NULL, addr, v, bits); } \
    void cqrt_qram_swap_##n(uint32_t m, uint32_t addr, uint32_t h) { bus_do(SWAP, 0, 0, m, &addr, 0, h, bits); } \
    void cqrt_qram_swap_##n##_hl(uint32_t m, uint64_t addr, uint32_t h) { bus_do(SWAP, 0, 0, m, NULL, addr, h, bits); } \
    void cqrt_qram_swap_##n##_controlled(uint32_t c, uint32_t m, uint32_t addr, uint32_t h) { bus_do(SWAP, 1, c, m, &addr, 0, h, bits); } \
    void cqrt_qram_swap_##n##_hl_controlled(uint32_t c, uint32_t m, uint64_t addr, uint32_t h) { bus_do(SWAP, 1, c, m, NULL, addr, h, bits); }
CQ_WIDTHS(BUS_)
CQ_FP_WIDTHS(BUS_)
