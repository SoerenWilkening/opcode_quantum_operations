/* Memory bus §1.10 with a classical address (kmgy.19): every load/store/swap form at every
 * width, against a host byte array and a plain C model of memory + overlay; the no-fault cases;
 * the link check. A quantum address rail: tests/test_qram.c. Always asserts. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "harness.h"
#include "cqrt.h"
#include "cq_bus.h"
#include "cq_replay.h"

static uint32_t M;   /* the bus */
static const hx_val zero;

/* ---- Every bus symbol of one width, which is also the link check. */
typedef struct {
    const char *name;
    uint32_t bits;
    uint32_t (*ld)(uint32_t, uint32_t);
    uint32_t (*ld_hl)(uint32_t, uint64_t);
    uint32_t (*ld_c)(uint32_t, uint32_t, uint32_t);
    uint32_t (*ld_hl_c)(uint32_t, uint32_t, uint64_t);
    void (*unc)(uint32_t, uint32_t, uint32_t);
    void (*unc_hl)(uint32_t, uint32_t, uint64_t);
    void (*unc_c)(uint32_t, uint32_t, uint32_t, uint32_t);
    void (*unc_hl_c)(uint32_t, uint32_t, uint32_t, uint64_t);
    void (*st)(uint32_t, uint32_t, uint32_t);
    void (*st_hl)(uint32_t, uint64_t, uint32_t);
    void (*st_c)(uint32_t, uint32_t, uint32_t, uint32_t);
    void (*st_hl_c)(uint32_t, uint32_t, uint64_t, uint32_t);
    void (*sw)(uint32_t, uint32_t, uint32_t);
    void (*sw_hl)(uint32_t, uint64_t, uint32_t);
    void (*sw_c)(uint32_t, uint32_t, uint32_t, uint32_t);
    void (*sw_hl_c)(uint32_t, uint32_t, uint64_t, uint32_t);
} fns;
#define FNS_(n, bits, ...) { #n, bits, \
    cqrt_qram_load_##n, cqrt_qram_load_##n##_hl, cqrt_qram_load_##n##_controlled, cqrt_qram_load_##n##_hl_controlled, \
    cqrt_qram_load_##n##_unc, cqrt_qram_load_##n##_hl_unc, cqrt_qram_load_##n##_controlled_unc, \
    cqrt_qram_load_##n##_hl_controlled_unc, \
    cqrt_qram_store_##n, cqrt_qram_store_##n##_hl, cqrt_qram_store_##n##_controlled, cqrt_qram_store_##n##_hl_controlled, \
    cqrt_qram_swap_##n, cqrt_qram_swap_##n##_hl, cqrt_qram_swap_##n##_controlled, cqrt_qram_swap_##n##_hl_controlled },
static const fns T[] = { CQ_WIDTHS(FNS_) CQ_FP_WIDTHS(FNS_) };
#define NT (sizeof T / sizeof T[0])

/* ---- Any form: hl picks the literal or a classical i64 address rail; ctl != 0 is the
 * _controlled form with control c. */
static uint32_t load(const fns *f, int hl, int ctl, uint32_t c, uint64_t a) {
    if (hl) return ctl ? f->ld_hl_c(c, M, a) : f->ld_hl(M, a);
    uint32_t r = cqrt_alloc_i64(a), h = ctl ? f->ld_c(c, M, r) : f->ld(M, r);
    cqrt_free(r);
    return h;
}
static void unc(const fns *f, int hl, int ctl, uint32_t c, uint64_t a, uint32_t h) {
    if (hl) { if (ctl) f->unc_hl_c(h, c, M, a); else f->unc_hl(h, M, a); return; }
    uint32_t r = cqrt_alloc_i64(a);
    if (ctl) f->unc_c(h, c, M, r); else f->unc(h, M, r);
    cqrt_free(r);
}
static void store(const fns *f, int hl, int ctl, uint32_t c, uint64_t a, uint32_t v) {
    if (hl) { if (ctl) f->st_hl_c(c, M, a, v); else f->st_hl(M, a, v); return; }
    uint32_t r = cqrt_alloc_i64(a);
    if (ctl) f->st_c(c, M, r, v); else f->st(M, r, v);
    cqrt_free(r);
}
static void swap(const fns *f, int hl, int ctl, uint32_t c, uint64_t a, uint32_t h) {
    if (hl) { if (ctl) f->sw_hl_c(c, M, a, h); else f->sw_hl(M, a, h); return; }
    uint32_t r = cqrt_alloc_i64(a);
    if (ctl) f->sw_c(c, M, r, h); else f->sw(M, r, h);
    cqrt_free(r);
}

/* A rail holding v; quantum: every bit a qubit set by replay, no gate. */
static uint32_t prep(hx_val v, uint32_t w, int quantum) {
    int32_t h = cq_rail_alloc(w, v.l);
    cq_bit *b = cq_rail_bits(h);
    if (quantum)
        for (uint32_t i = 0; i < w; i++) {
            int one = b[i].kind == CQ_BIT_ONE;
            b[i] = cq_bit_const(0);
            cq_get_qubit(&b[i]);
            cq_replay_set_bit(b[i].q, one);
        }
    return (uint32_t)h;
}
/* A rail's value read from the replay backend, not measured: cq_measure_bit hands a measured-1
 * qubit back to the pool unreset (kmgy.31), and this test keeps qubits alive across calls
 * without cq_replay_reset. cqrt_free then resets properly. */
static hx_val peek(uint32_t h) {
    hx_val v = zero;
    const cq_bit *b = cq_rail_bits((int32_t)h);
    for (uint32_t i = 0; i < cq_rail_width((int32_t)h); i++)
        v.l[i / 64] |= (uint64_t)(cq_bit_is_q(b[i]) ? cq_replay_get_bit(b[i].q) : b[i].kind) << (i % 64);
    return v;
}
static uint8_t byte_of(hx_val v, uint32_t j) { return (uint8_t)(v.l[j / 8] >> (8 * (j % 8))); }
static int is_quantum(uint64_t a) { cq_bit b[8]; return cq_cell_read(a, b); }

static uint64_t rng(uint64_t *s) {   /* splitmix64 */
    uint64_t z = (*s += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}
static hx_val rnd(uint64_t *s, uint32_t w) {
    hx_val v;
    for (uint32_t k = 0; k < CQ_LIMBS; k++) v.l[k] = rng(s);
    return hx_mask(v, w);
}

/* ---- Load through the sampled harness, HOST cells: the op writes the sampled content into
 * hbuf at an offset derived from it (junk above W in the last cell), then o ^= the cells. */
static uint8_t hbuf[64];
static const fns *cur;
static int cur_hl, cur_ctl;

static uint64_t place(hx_val v, uint32_t w) {
    uint32_t nb = (w + 7) / 8;
    uint64_t hsh = 0;
    for (uint32_t k = 0; k < CQ_LIMBS; k++) hsh = (hsh ^ v.l[k]) * 0x100000001b3ull;
    uint32_t off = (uint32_t)(hsh >> 40) % (sizeof hbuf - nb + 1);
    for (uint32_t j = 0; j < nb; j++) hbuf[off + j] = byte_of(v, j);
    if (w % 8) hbuf[off + nb - 1] |= (uint8_t)(0xff << (w % 8));
    return (uint64_t)(uintptr_t)hbuf + off;
}
static void h_fwd(int32_t o, const int32_t *a, uint32_t w) {
    uint64_t addr = place(hx_sample[cur_ctl], w);
    uint32_t h = load(cur, cur_hl, cur_ctl, cur_ctl ? (uint32_t)a[0] : 0, addr);
    cq_bit *hb = cq_rail_bits((int32_t)h), *ob = cq_rail_bits(o);
    for (uint32_t i = 0; i < w; i++) cq_cx(&hb[i], &ob[i]);
    unc(cur, cur_hl, cur_ctl, cur_ctl ? (uint32_t)a[0] : 0, addr, h);
    assert(hx_eq(peek(h), zero));
    cqrt_free(h);
}
static void h_unc(int32_t o, const int32_t *a, uint32_t w) {
    unc(cur, cur_hl, cur_ctl, cur_ctl ? (uint32_t)a[0] : 0, place(hx_sample[cur_ctl], w), (uint32_t)o);
}
static hx_val ref_a0(const hx_val *a, uint32_t w) { (void)w; return a[0]; }
static hx_val ref_ctl(const hx_val *a, uint32_t w) { (void)w; return a[0].l[0] ? a[1] : zero; }

static void harness_loads(const fns *f) {
    cur = f;
    for (cur_hl = 1; cur_hl < 2; cur_hl++)   /* _hl only: the model steps cover every form */
        for (cur_ctl = 0; cur_ctl < 2; cur_ctl++) {
            char n1[64], n2[64];
            snprintf(n1, sizeof n1, "load_%s%s%s", f->name, cur_hl ? "_hl" : "", cur_ctl ? "_controlled" : "");
            snprintf(n2, sizeof n2, "%s_unc", n1);
            const hx_op ops[] = { { n1, 1 + cur_ctl, 0, h_fwd, h_fwd, cur_ctl ? ref_ctl : ref_a0 },
                                  { n2, 1 + cur_ctl, 0, h_unc, h_unc, cur_ctl ? ref_ctl : ref_a0 } };
            for (int k = 0; k < 2; k++)
                assert(hx_run_w(&ops[k], f->bits, cur_ctl ? (const uint32_t[]){ 1, 0 } : NULL, 0, NULL) == HX_OK);
        }
}

/* ---- Random sequences of every form against a C model. mem: the expected bytes; tq: per bit,
 * 0 classical, 1 a qubit, 2 maybe a qubit (after a qubit control, where only the emitter's
 * folding decides). The overlay rule then predicts HOST (all 0) or QUANTUM (a 1, or a qubit
 * control), and a HOST cell's host byte must equal the model. */
#define SEQ 64
static uint8_t sbuf[SEQ], mem[SEQ], tq[SEQ][8];

static hx_val model(uint32_t off, uint32_t w) {
    hx_val v = zero;
    for (uint32_t j = 0; j < (w + 7) / 8; j++) v.l[j / 8] |= (uint64_t)mem[off + j] << (8 * (j % 8));
    return hx_mask(v, w);
}
static void check_all(int values) {
    for (uint32_t x = 0; x < SEQ; x++) {
        if (!is_quantum((uintptr_t)sbuf + x)) assert(sbuf[x] == mem[x]);
        if (!values) continue;
        uint32_t h = cqrt_qram_load_i8_hl(M, (uintptr_t)sbuf + x);
        assert((uint8_t)peek(h).l[0] == mem[x]);
        cqrt_free(h);
    }
}

static void step(const fns *f, uint64_t *s, int k) {
    uint32_t w = f->bits, nb = (w + 7) / 8, off = (uint32_t)(rng(s) % (SEQ - nb + 1));
    uint64_t a = (uintptr_t)sbuf + off;
    int op = (int)(k % 3), hl = (int)(k / 3) & 1, ctl = (int)(k / 6 % 5), forced = (int)(k / 30) & 1;   /* every form, both modes */
    int on = ctl == 0 || ctl == 2 || ctl == 4, cq = ctl >= 3;   /* ctl: none, 0, 1, qubit 0, qubit 1 */
    uint32_t c = ctl ? prep((hx_val){ { (uint64_t)(ctl == 2 || ctl == 4) } }, 1, cq) : 0;
    hx_val v = rnd(s, w), old = model(off, w);
    int classical_cells = 1;
    for (uint32_t i = 0; i < 8 * nb; i++) classical_cells &= tq[off + i / 8][i % 8] == 0;
    uint64_t g0 = cq_replay_gates();
    uint32_t live0 = cq_pool_live();
#define NO_GATE() assert(cq_replay_gates() == g0 && cq_pool_live() == live0)

    if (op == 0) {
        uint32_t h = load(f, hl, ctl != 0, c, a);
        if (!cq && (classical_cells || !on)) NO_GATE();
        assert(hx_eq(peek(h), on ? old : zero));
        unc(f, hl, ctl != 0, c, a, h);
        assert(hx_eq(peek(h), zero));
        cqrt_free(h);
    } else {
        uint32_t h = prep(v, w, forced);
        if (op == 1) store(f, hl, ctl != 0, c, a, h); else swap(f, hl, ctl != 0, c, a, h);
        if (!forced && !cq) NO_GATE();   /* the swaps are relabelings, whatever the cells hold */
        assert(hx_eq(peek(h), op == 2 && on ? old : v));
        cqrt_free(h);
        uint32_t nbits = op == 1 ? 8 * nb : w;   /* a store replaces every bit of its cells */
        for (uint32_t i = 0; i < nbits; i++) {
            uint8_t *t = &tq[off + i / 8][i % 8], *m = &mem[off + i / 8], bit = (uint8_t)(1u << (i % 8));
            if (on) *m = (uint8_t)((*m & ~bit) | ((i < w && (v.l[i / 64] >> (i % 64)) & 1) ? bit : 0));
            if (cq) { if (*t != 1) *t = 2; }
            else if (on) *t = (uint8_t)(i < w && forced);
        }
        if (ctl != 1) {
            int any1 = 0, all0 = 1;
            for (uint32_t i = 0; i < 8 * nb; i++) { any1 |= tq[off + i / 8][i % 8] == 1; all0 &= tq[off + i / 8][i % 8] == 0; }
            for (uint32_t j = 0; j < nb; j++)
                if (cq || any1) assert(is_quantum(a + j));
                else if (all0) assert(!is_quantum(a + j));
        }
    }
    if (c) cqrt_free(c);
}

/* ---- Direct tests. */
static uint64_t ld64(uint64_t a) { uint32_t h = cqrt_qram_load_i64_hl(M, a); uint64_t v = peek(h).l[0]; cqrt_free(h); return v; }
static uint8_t ld8(uint64_t a) { uint32_t h = cqrt_qram_load_i8_hl(M, a); uint8_t v = (uint8_t)peek(h).l[0]; cqrt_free(h); return v; }
static void st32(uint64_t a, uint32_t x) { uint32_t v = cqrt_alloc_i32(x); cqrt_qram_store_i32_hl(M, a, v); cqrt_free(v); }

static void byte_order(void) {
    uint8_t b[16] = { 0 };
    uint64_t a = (uintptr_t)b;
    cq_replay_reset();
    st32(a, 0x11223344);
    assert(b[0] == 0x44 && b[1] == 0x33 && b[2] == 0x22 && b[3] == 0x11 && b[4] == 0);
    uint32_t h = cqrt_qram_load_i16_hl(M, a + 1);
    assert(peek(h).l[0] == 0x2233);
    cqrt_free(h);
    /* i7: a store writes the value zero-extended, a swap leaves bit 7 where it is. */
    b[8] = 0xff;
    uint32_t v = cqrt_alloc_i7(0x55);
    cqrt_qram_store_i7_hl(M, a + 8, v);
    assert(b[8] == 0x55);
    b[8] = 0x80;
    cqrt_qram_swap_i7_hl(M, a + 8, v);
    assert(b[8] == 0xd5 && peek(v).l[0] == 0);
    cqrt_free(v);
    /* f80 touches 10 cells, not 16. */
    memset(b, 0xee, sizeof b);
    long double x = -1.5L;
    v = cqrt_alloc_f80(x);
    cqrt_qram_store_f80_hl(M, a, v);
    assert(memcmp(b, &x, 10) == 0 && b[10] == 0xee && b[15] == 0xee);
    cqrt_free(v);
    assert(cq_replay_gates() == 0);
}

/* Store and load at once through a quantum value: cells QUANTUM, round trip, then a classical
 * store makes them HOST again with the displaced qubits in the tape slot. */
static void quantum_round_trip(void) {
    static uint8_t b[8];   /* static: the overlay outlives a stack frame */
    uint64_t a = (uintptr_t)b;
    uint32_t v = prep((hx_val){ { 0xa5c3 } }, 16, 1);
    cqrt_qram_store_i16_hl(M, a, v);
    assert(is_quantum(a) && is_quantum(a + 1) && !is_quantum(a + 2) && b[0] == 0 && b[1] == 0);
    uint32_t h = cqrt_qram_load_i16_hl(M, a);
    assert(peek(h).l[0] == 0xa5c3);
    cqrt_free(h); cqrt_free(v);
    uint32_t live = cq_pool_live();
    uint64_t g0 = cq_replay_gates();
    st32(a, 0x01020304);
    assert(!is_quantum(a) && !is_quantum(a + 1) && b[0] == 4 && b[3] == 1);
    assert(cq_replay_gates() == g0 && cq_pool_live() == live);   /* the cell qubits went to the tape slot */
}

/* Under a classical 0 control nothing is touched: a store to a read-only page leaves its cell
 * HOST, a load reads 0. Under a qubit control the cells become QUANTUM even if they stay classical. */
static void controls(uint8_t *ro) {
    uint64_t a = (uintptr_t)ro;
    uint32_t c0 = cqrt_alloc_i1(0), v = cqrt_alloc_i32(0xdeadbeef);
    uint64_t g0 = cq_replay_gates();
    cqrt_qram_store_i32_hl_controlled(c0, M, a, v);
    uint32_t next = cqrt_alloc_i1(0);   /* handles are 1, 2, 3, ...: the store took its tape slot */
    assert(next == v + 2 && !is_quantum(a) && ld8(a) == ro[0]);
    cqrt_free(next);
    uint32_t h = cqrt_qram_load_i32_hl_controlled(c0, M, a);
    assert(peek(h).l[0] == 0 && cq_replay_gates() == g0);
    cqrt_free(h);
    static uint8_t b[4];
    uint32_t cq = prep((hx_val){ { 0 } }, 1, 1), z = cqrt_alloc_i32(0);
    cqrt_qram_swap_i32_hl_controlled(cq, M, (uintptr_t)b, z);
    assert(is_quantum((uintptr_t)b) && is_quantum((uintptr_t)b + 3) && peek(z).l[0] == 0);
    cqrt_free(c0); cqrt_free(v); cqrt_free(cq); cqrt_free(z);
}

static void no_fault(uint8_t *ro) {
    uint64_t un;
    { void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0); assert(p != MAP_FAILED); munmap(p, 4096); un = (uintptr_t)p; }
    /* Unreadable: page 0, an unmapped page, the top of the address space. */
    const uint64_t bad[] = { 0, 8, un, un + 1000, UINT64_MAX - 40 };
    for (unsigned k = 0; k < sizeof bad / sizeof *bad; k++)
        for (unsigned i = 0; i < NT; i++) {
            uint32_t h = load(&T[i], (int)(k & 1), 0, 0, bad[k]);
            assert(hx_eq(peek(h), zero));
            cqrt_free(h);
        }
    /* A swap's displaced read of an unmapped cell is 0; the cell is QUANTUM with h's value. */
    uint32_t h = cqrt_alloc_i64(0x8877665544332211ull);
    cqrt_qram_swap_i64_hl(M, un, h);
    assert(peek(h).l[0] == 0 && is_quantum(un) && ld64(un) == 0x8877665544332211ull);
    cqrt_free(h);
    /* Read-only: a load reads it, a store does not fault, lands in the overlay, the page is unchanged. */
    uint64_t a = (uintptr_t)ro + 100;
    assert(ld64(a) == 0x6766656463626160ull);
    uint32_t live = cq_pool_live();
    uint64_t g0 = cq_replay_gates();
    st32(a + 2, 0xcafef00d);
    assert(cq_replay_gates() == g0 && cq_pool_live() == live);   /* QUANTUM, but classical bits */
    assert(ld64(a) == 0x6766cafef00d6160ull && ro[102] == 0x62 && ro[105] == 0x65);
    assert(is_quantum(a + 2) && is_quantum(a + 5) && !is_quantum(a + 1) && !is_quantum(a + 6));
    /* Wrap mod 2^64: an i32 at 2^64 - 2 is cells 2^64-2, 2^64-1, 0, 1 (all unwritable). */
    assert(ld64(UINT64_MAX) == 0);
    st32(UINT64_MAX - 1, 0x44332211);
    assert(ld8(UINT64_MAX) == 0x22 && ld8(0) == 0x33 && ld8(1) == 0x44);
    assert(ld64(UINT64_MAX) == 0x443322);
    uint32_t r = cqrt_alloc_i64(UINT64_MAX);
    h = cqrt_qram_load_i16(M, r);
    assert(peek(h).l[0] == 0x3322);
    cqrt_free(h); cqrt_free(r);
}

int main(void) {
    M = cqrt_qram_alloc_i8(0);
    assert(M && cqrt_qram_alloc_i8(0) == M);
    assert(NT == 20);
    printf("%u bus symbols linked\n", (unsigned)(16 * NT + 1));

    uint8_t *ro = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(ro != MAP_FAILED);
    for (int i = 0; i < 4096; i++) ro[i] = (uint8_t)(0x60 + i - 100);
    assert(mprotect(ro, 4096, PROT_READ) == 0);

    byte_order();
    controls(ro);
    for (unsigned i = 0; i < NT; i++) harness_loads(&T[i]);
    printf("load: the 4 _hl forms x %u widths through the harness, both modes: ok\n", (unsigned)NT);
    cq_replay_reset();   /* the harness measured: free pool qubits may hold 1 (kmgy.31); no cell holds a qubit yet */

    quantum_round_trip();
    no_fault(ro);
    printf("byte order, quantum round trip, controls, no fault (page 0, unmapped, read-only, wrap): ok\n");

    uint64_t s = 0x6b6d67792e3139ull;
    for (unsigned i = 0; i < NT; i++) {
        for (int k = 0; k < 300; k++) { step(&T[i], &s, k); if (k % 16 == 15) check_all(1); }
        check_all(1);
    }
    printf("load/store/swap, all forms and controls, %u widths, 300 model steps each: ok\n", (unsigned)NT);
    return 0;
}
