/* The in-place add of ABI §1.15 (cqrt_add/sub/addc_<W>) and §1.9's copy and xorc on a target that is not
 * |0>, through the public entry points on the replay backend. Every case runs with s and d each
 * classical or forced to qubits; it checks the value against arithmetic mod 2^W, s unchanged (its bits
 * too: a classical bit stays classical), every qubit outside s and d at 0, the live-qubit count, and
 * no gate when all is classical. Exhaustive at W <= 4, sampled above. Prints the cost table. Always asserts. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "harness.h"
#include "cqrt.h"
#include "cq_replay.h"

typedef struct {
    const char *n; uint32_t w;
    void (*add)(uint32_t, uint32_t), (*sub)(uint32_t, uint32_t), (*copy)(uint32_t, uint32_t);
    void (*addc)(uint32_t, hx_val), (*xorc)(uint32_t, hx_val);
} fam;
/* Literals carry set bits above the width: only the low W bits may be read. */
static unsigned __int128 junk(uint32_t bits) { return bits >= 128 ? 0 : ~(unsigned __int128)0 << bits; }
#define WN_(n, bits) \
    static void addc_##n(uint32_t h, hx_val v) { cqrt_addc_##n(h, (CQRT_UINT(bits))(hx_u128(v) | junk(bits))); } \
    static void xorc_##n(uint32_t h, hx_val v) { cqrt_xorc_##n(h, (CQRT_UINT(bits))(hx_u128(v) | junk(bits))); }
#define WW_(n, bits) \
    static void addc_##n(uint32_t h, hx_val v) { cqrt_addc_##n(h, v.l[0], v.l[1], v.l[2], v.l[3] | ~0ull << ((bits) - 192)); } \
    static void xorc_##n(uint32_t h, hx_val v) { cqrt_xorc_##n(h, v.l[0], v.l[1], v.l[2], v.l[3] | ~0ull << ((bits) - 192)); }
CQ_WIDTHS_NARROW(WN_)
CQ_WIDTHS_WIDE(WW_)
#define ROW_(n, bits) { #n, bits, cqrt_add_##n, cqrt_sub_##n, cqrt_copy_##n, addc_##n, xorc_##n },
static const fam F[] = { CQ_WIDTHS(ROW_) };

static const hx_val zero;
static hx_val vadd(hx_val a, hx_val b, uint32_t w) {
    unsigned __int128 c = 0;
    for (int i = 0; i < (int)CQ_LIMBS; i++) { c += (unsigned __int128)a.l[i] + b.l[i]; a.l[i] = (uint64_t)c; c >>= 64; }
    return hx_mask(a, w);
}
static hx_val vneg(hx_val a, uint32_t w) {
    for (int i = 0; i < (int)CQ_LIMBS; i++) a.l[i] = ~a.l[i];
    return vadd(a, (hx_val){ { 1 } }, w);
}
static uint64_t rs = 0x9e3779b97f4a7c15ull;
static hx_val rnd(uint32_t w) {
    hx_val v;
    for (int i = 0; i < (int)CQ_LIMBS; i++) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; v.l[i] = rs; }
    return hx_mask(v, w);
}

#define B cqrt_invert_begin()
#define E cqrt_invert_end()
typedef void (*body)(const fam *f, uint32_t S, uint32_t D, hx_val lit);
static void b_add(const fam *f, uint32_t S, uint32_t D, hx_val l) { (void)l; f->add(S, D); }
static void b_sub(const fam *f, uint32_t S, uint32_t D, hx_val l) { (void)l; f->sub(S, D); }
static void b_addsub(const fam *f, uint32_t S, uint32_t D, hx_val l) { (void)l; f->add(S, D); f->sub(S, D); }
static void b_addc(const fam *f, uint32_t S, uint32_t D, hx_val l) { (void)S; f->addc(D, l); }
static void b_addc2(const fam *f, uint32_t S, uint32_t D, hx_val l) { (void)S; f->addc(D, l); f->addc(D, vneg(l, f->w)); }
static void b_copy(const fam *f, uint32_t S, uint32_t D, hx_val l) { (void)l; f->copy(S, D); }
static void b_xorc(const fam *f, uint32_t S, uint32_t D, hx_val l) { (void)S; f->xorc(D, l); }
/* cq_invert: a region replays reversed and inverted, so {add} is sub, {sub} is add, {addc l} is addc -l. */
static void i_add(const fam *f, uint32_t S, uint32_t D, hx_val l) { (void)l; B; f->add(S, D); E; }
static void i_sub(const fam *f, uint32_t S, uint32_t D, hx_val l) { (void)l; B; f->sub(S, D); E; }
static void i_addc(const fam *f, uint32_t S, uint32_t D, hx_val l) { (void)S; B; f->addc(D, l); E; }
static void i_id(const fam *f, uint32_t S, uint32_t D, hx_val l) { f->add(S, D); f->addc(D, l); B; f->add(S, D); f->addc(D, l); E; }
static void i_nest(const fam *f, uint32_t S, uint32_t D, hx_val l) { B; f->addc(D, l); B; f->add(S, D); E; E; }   /* d + s - l */

typedef struct { uint64_t gates, ccx; uint32_t depth, anc; } cost;
enum { SQ = 1, DQ = 2 };

static void run(const fam *f, int mode, hx_val s, hx_val d, hx_val lit, body b, hx_val want, cost *c) {
    uint32_t w = f->w;
    cq_replay_reset();
    uint32_t base = cq_pool_live();
    int32_t S = hx_to_rail(s, w), D = hx_to_rail(d, w);
    cq_bit *sb = cq_rail_bits(S), *db = cq_rail_bits(D), *s0 = malloc(w * sizeof *s0);
    for (uint32_t i = 0; i < w; i++) { if (mode & SQ) cq_get_qubit(&sb[i]); if (mode & DQ) cq_get_qubit(&db[i]); }
    memcpy(s0, sb, w * sizeof *s0);
    uint64_t g0 = cq_replay_gates();
    uint32_t live0 = cq_pool_live();
    b(f, (uint32_t)S, (uint32_t)D, lit);
    uint32_t nq = 0, top = cq_replay_max_index();
    uint8_t *own = calloc((size_t)top + 1, 1);
    for (uint32_t i = 0; i < w; i++) {
        assert(sb[i].kind == s0[i].kind && (!cq_bit_is_q(sb[i]) || sb[i].q == s0[i].q));   /* s as it was */
        if (cq_bit_is_q(sb[i])) { nq++; if (sb[i].q <= top) own[sb[i].q] = 1; }
        if (cq_bit_is_q(db[i])) { nq++; if (db[i].q <= top) own[db[i].q] = 1; }
    }
    assert(cq_pool_live() - base == nq);                       /* nothing but s and d holds a qubit */
    if (mode & DQ) assert(cq_pool_live() == live0);            /* the live count is the same before and after */
    if (!mode) assert(cq_replay_gates() == g0 && cq_pool_live() == base);   /* all classical: no gate, no qubit */
    for (uint32_t q = 0; q <= top; q++) assert(own[q] || cq_replay_get_bit(q) == 0);   /* every ancilla at 0 */
    if (c) {   /* s = d = 0: no preparation gate, so the counters are the body's */
        assert(!g0);
        *c = (cost){ cq_replay_gates(), cq_replay_gates_of(CQ_G_CCX), cq_replay_depth(), cq_replay_qubits() };
    }
    hx_val got = hx_from_rail(D);
    if (!hx_eq(got, want)) { fprintf(stderr, "%s mode %d: wrong result\n", f->n, mode); abort(); }
    assert(hx_eq(hx_from_rail(S), hx_mask(s, w)));
    cq_rail_free(S); cq_rail_free(D);
    assert(cq_pool_live() == base);
    free(s0); free(own);
}

static void sample(const fam *f, hx_val s, hx_val d) {
    uint32_t w = f->w;
    hx_val sum = vadd(d, s, w), diff = vadd(d, vneg(s, w), w), x = hx_mask(hx_xor(d, s), w);
    for (int m = 0; m < 4; m++) {   /* s doubles as the literal */
        run(f, m, s, d, s, b_add, sum, NULL);
        run(f, m, s, d, s, b_sub, diff, NULL);
        run(f, m, s, d, s, b_addsub, d, NULL);
        run(f, m, s, d, s, b_addc, sum, NULL);
        run(f, m, s, d, s, b_addc2, d, NULL);
        run(f, m, s, d, s, b_copy, x, NULL);
        run(f, m, s, d, s, b_xorc, x, NULL);
        run(f, m, s, d, s, i_add, diff, NULL);
        run(f, m, s, d, s, i_sub, sum, NULL);
        run(f, m, s, d, s, i_addc, diff, NULL);
        run(f, m, s, d, s, i_id, d, NULL);
        run(f, m, s, d, d, i_nest, s, NULL);   /* lit = d: d + s - d */
    }
}

int main(void) {
    printf("%-5s %8s | %21s | %21s\n%-5s %8s | %5s %5s %5s %3s | %5s %5s %5s %3s\n", "", "", "add, s and d qubits", "addc, lit = 2^W - 1",
           "width", "samples", "gates", "CCX", "depth", "anc", "gates", "CCX", "depth", "anc");
    for (size_t k = 0; k < sizeof F / sizeof *F; k++) {
        const fam *f = &F[k];
        uint32_t w = f->w, n = 0;
        if (w <= 4) {
            for (uint64_t s = 0; s < 1ull << w; s++)
                for (uint64_t d = 0; d < 1ull << w; d++, n++) sample(f, (hx_val){ { s } }, (hx_val){ { d } });
        } else {
            const hx_val edge[] = { zero, { { 1 } }, hx_ones(w), rnd(w) };   /* 0, 1, 2^W - 1: wraparound */
            for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++, n++) sample(f, edge[i], edge[j]);
            uint32_t r = w == 8 || w == 32 || w == 64 || w == 100 || w == 128 ? 200 : 20;
            for (uint32_t i = 0; i < r; i++, n++) sample(f, rnd(w), rnd(w));
        }
        cost a, c;
        run(f, SQ | DQ, zero, zero, zero, b_add, zero, &a);
        run(f, DQ, zero, zero, hx_ones(w), b_addc, hx_ones(w), &c);
        /* anc: qubits a gate touched beyond the operands' (all 2W, or W, are touched at W >= 2). */
        a.anc -= w == 1 ? a.anc : 2 * w; c.anc -= w;
        assert(a.ccx == 2 * (w - 1) && a.gates == (w == 1 ? 1 : 9 * w - 7) && a.depth <= 2 * w + 6 && a.anc == (w > 1));
        printf("%-5s %8u | %5llu %5llu %5u %3u | %5llu %5llu %5u %3u\n", f->n, n, (unsigned long long)a.gates, (unsigned long long)a.ccx,
               a.depth, a.anc, (unsigned long long)c.gates, (unsigned long long)c.ccx, c.depth, c.anc);
    }
    printf("add, sub, addc, copy and xorc on a live target, cq_invert: ok\n");
    return 0;
}
