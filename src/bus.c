/* The memory bus of BACKEND_ABI §1.10 with a classical address: the overlay, the bus handle
 * and every load/store/swap entry point. An address rail holding a qubit is the QRAM path,
 * issue kmgy.21, and aborts here. Types and conventions: include/cqrt.h. */
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
typedef struct { uint64_t a; uint8_t used, quantum; cq_bit b[8]; } cell;
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
    memcpy(e->b, b, sizeof e->b);
}

/* ---- One access at a classical address (the _hl gate lists of §1.10 Impl). */
enum { LOAD, STORE, SWAP };
#define MAX_CELLS ((CQ_RAIL_MAX_WIDTH + 7) / 8)

/* h is the loaded or swapped rail, or the value v of a store; w bits. c: a qubit control, or
 * NULL for none (a classical control has been resolved by the caller). */
static void access(int op, const cq_bit *c, uint64_t a, cq_bit *h, uint32_t w) {
    uint32_t s = (w + 7) / 8;
    cq_bit cells[8 * MAX_CELLS];
    uint8_t host[MAX_CELLS];
    int all = cq_host_read(a, host, s);   /* one read for the common case; else per cell */
    for (uint32_t j = 0; j < s; j++) cell_bits(a + j, &cells[8 * j], all ? &host[j] : NULL);   /* a + j wraps mod 2^64 */
    if (op == LOAD) {   /* the cells are controls only: nothing to put back */
        for (uint32_t i = 0; i < w; i++)
            if (c) cq_ccx(c, &cells[i], &h[i]); else cq_cx(&cells[i], &h[i]);
        return;
    }
    /* A store swaps every bit of its cells with the fresh tape slot t, a swap the low w bits with h. */
    cq_bit *t = op == STORE ? cq_rail_bits(cq_rail_alloc(8 * s, NULL)) : h;
    for (uint32_t i = 0, n = op == STORE ? 8 * s : w; i < n; i++)
        if (c) { cq_cx(&cells[i], &t[i]); cq_ccx(c, &t[i], &cells[i]); cq_cx(&cells[i], &t[i]); }
        else { cq_bit x = t[i]; t[i] = cells[i]; cells[i] = x; }   /* the swap as a relabeling: no gate */
    for (uint32_t i = 0; op == STORE && i < w; i++)
        if (c) cq_ccx(c, &h[i], &cells[i]); else cq_cx(&h[i], &cells[i]);
    /* Overlay: a qubit control, or a qubit left anywhere in the cells, makes them all QUANTUM. */
    int quantum = c != NULL;
    for (uint32_t i = 0; i < 8 * s; i++) quantum |= cq_bit_is_q(cells[i]);
    for (uint32_t j = 0; j < s; j++) cq_cell_write(a + j, &cells[8 * j], quantum);
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
            if (cq_bit_is_q(ab[i]))
                die("bus: quantum address rail, the QRAM path of issue kmgy.21 is not implemented; handle", *ah);
            a |= (uint64_t)ab[i].kind << i;
        }
    }
    access(op, cb, a, hb, w);
}

/* cq_invert (cq_rt.h): a load or swap is its own inverse and is recorded; a store is a flush
 * point, performed at its call. h[]: c, m, addr rail, h; n[]: op, ctl, width, _hl; v[0]: addr. */
static void bus_k(const cq_call *k, int inv) {
    (void)inv;
    bus_op((int)k->n[0], (int)k->n[1], k->h[0], k->h[1], k->n[3] ? NULL : &k->h[2], k->v[0], k->h[3], k->n[2]);
}
static void bus_do(int op, int ctl, uint32_t c, uint32_t m, const uint32_t *ah, uint64_t a, uint32_t h, uint32_t w) {
    if (op == STORE) { cq_inv_flush(); bus_op(op, ctl, c, m, ah, a, h, w); return; }
    cq_call_do(&(cq_call){ .run = bus_k, .h = { c, m, ah ? *ah : 0, h }, .n = { (uint32_t)op, (uint32_t)ctl, w, !ah }, .v = { a } });
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
