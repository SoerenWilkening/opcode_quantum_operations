/* The object-bounded access (ABI §1.10 "Objects", src/qram.c): load, store, swap, plain and controlled,
 * one object and unions of two and three (select of pointers), against the same access forced onto
 * the bit-pattern bound (cq_qram_force_bits). Addresses come from the real add/select templates, so
 * their notes come from the derivation rules. Every address of every object is run: the loaded or
 * swapped rail, the tape slot and the cells afterwards agree, the address and control are unchanged,
 * every ancilla is back (no measurement in the access), the leaf count is the object bound, and a
 * store or swap makes exactly the kept leaves' cells QUANTUM. Then the fallbacks (each builds exactly
 * the bit-pattern leaves with the same gates) and the measured problem in miniature. The replay
 * backend is classical, so each run is one basis input. Always asserts. */
#undef NDEBUG
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cqrt.h"
#include "cq_bus.h"
#include "cq_config.h"
#include "cq_template.h"
#include "cq_replay.h"

static uint32_t M;
static uint8_t *H;
static uint32_t nblk;
#define HSIZE (1u << 20)
#define BLK 0x1000   /* one fresh block per case: objects never merge across cases */
#define WIN 0x800    /* the most cells the bit pattern of a case reaches */

typedef struct {
    const char *name;
    uint32_t bits;
    uint32_t (*ld)(uint32_t, uint32_t);
    uint32_t (*ldc)(uint32_t, uint32_t, uint32_t);
    void (*unc)(uint32_t, uint32_t, uint32_t);
    void (*uncc)(uint32_t, uint32_t, uint32_t, uint32_t);
    void (*st)(uint32_t, uint32_t, uint32_t);
    void (*stc)(uint32_t, uint32_t, uint32_t, uint32_t);
    void (*sw)(uint32_t, uint32_t, uint32_t);
    void (*swc)(uint32_t, uint32_t, uint32_t, uint32_t);
} fns;
#define F_(n, w) { #n, w, cqrt_qram_load_##n, cqrt_qram_load_##n##_controlled, cqrt_qram_load_##n##_unc, \
    cqrt_qram_load_##n##_controlled_unc, cqrt_qram_store_##n, cqrt_qram_store_##n##_controlled, \
    cqrt_qram_swap_##n, cqrt_qram_swap_##n##_controlled }
static const fns T[] = { F_(i8, 8), F_(i16, 16), F_(i32, 32), F_(i64, 64), F_(i80, 80) };

static uint64_t rng(uint64_t *s) {   /* splitmix64 */
    uint64_t z = (*s += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}
static int bitval(cq_bit b) { return cq_bit_is_q(b) ? cq_replay_get_bit(b.q) : b.kind; }
static uint64_t peek(uint32_t h) {
    uint64_t v = 0;
    const cq_bit *b = cq_rail_bits((int32_t)h);
    for (uint32_t i = 0; i < cq_rail_width((int32_t)h) && i < 64; i++) v |= (uint64_t)bitval(b[i]) << i;
    return v;
}
static uint32_t nq(const cq_bit *b, uint32_t n) { uint32_t k = 0; while (n--) k += cq_bit_is_q(b[n]); return k; }
static uint32_t qubit(int v) {
    uint32_t c = cqrt_alloc_i1(0);
    cq_get_qubit(cq_rail_bits((int32_t)c));
    cq_replay_set_bit(cq_rail_bits((int32_t)c)->q, v);
    return c;
}
/* A rail of w bits from s; quantum: every bit a qubit set by replay, no gate. */
static uint32_t prep(uint32_t w, uint64_t *s, int quantum) {
    uint32_t h = (uint32_t)cq_rail_alloc(w, NULL);
    cq_bit *b = cq_rail_bits((int32_t)h);
    for (uint32_t i = 0; i < w; i++) {
        int v = (int)(rng(s) & 1);
        b[i] = cq_bit_const(quantum ? 0 : v);
        if (quantum) { cq_get_qubit(&b[i]); cq_replay_set_bit(b[i].q, v); }
    }
    return h;
}
/* A 64-bit rail holding v, bits lo..hi-1 qubits (by replay, no gate), the rest classical. */
static uint32_t qrail(uint64_t v, uint32_t lo, uint32_t hi) {
    uint32_t h = cqrt_alloc_i64(v);
    cq_bit *b = cq_rail_bits((int32_t)h);
    for (uint32_t i = lo; i < hi; i++) { b[i] = cq_bit_const(0); cq_get_qubit(&b[i]); cq_replay_set_bit(b[i].q, (int)(v >> i & 1)); }
    return h;
}
static void pool_clean(void) {   /* every qubit in the free list reads 0 */
    uint32_t n = cq_pool_minted() - cq_pool_live(), *q = malloc((n + 1) * sizeof *q);
    assert(q);
    for (uint32_t i = 0; i < n; i++) { q[i] = cq_pool_take(); assert(!cq_replay_get_bit(q[i])); }
    while (n) cq_pool_give(q[--n]);
    free(q);
}
static uint64_t new_block(uint64_t *s) {
    assert((nblk + 1) * BLK <= HSIZE);
    uint8_t *b = H + (size_t)BLK * nblk++;
    for (uint32_t i = 0; i < BLK; i++) b[i] = (uint8_t)rng(s);
    return (uint64_t)(uintptr_t)b;
}
/* The window [lo, lo + n): every cell either path can reach, the bit pattern's (a cell read costs a
 * host read, a system call, so the window is kept to that). */
static uint64_t lo, wn;
static uint32_t win_q(void) {
    uint32_t n = 0;
    for (uint64_t a = lo; a < lo + wn; a++) { cq_bit b[8]; if (cq_cell_read(a, b)) n += nq(b, 8); }
    return n;
}
/* Every QUANTUM cell of the window back to HOST: its qubits reset, the (unchanged) host byte. */
static void win_reset(void) {
    for (uint64_t a = lo; a < lo + wn; a++) {
        cq_bit b[8];
        if (!cq_cell_read(a, b)) continue;
        uint8_t v = *(uint8_t *)(uintptr_t)a;
        for (int i = 0; i < 8; i++) { cq_reset_bit(&b[i]); b[i] = cq_bit_const(v >> i & 1); }
        cq_cell_write(a, b, 0);
    }
}

/* A case: m objects (start, size) in one block; addresses at granularity g (1: every byte, 8: the
 * low 3 bits classical). */
typedef struct { int m; uint64_t s[3], n[3], blk; uint32_t g; } kase;
static uint32_t clog2(uint64_t n) { uint32_t k = 0; while ((1ull << k) < n) k++; return k; }

/* The address of offset o in object j: s_j + o through the add template, s_j picked by select of the
 * object pointers (literals, so noted by the value rule) under qubits. The rails to free go to fr[]. */
static uint32_t address(const kase *k, int j, uint64_t o, uint32_t *fr, int *nf) {
    uint32_t K = 0;
    for (int i = 0; i < k->m; i++) if (clog2(k->n[i]) > K) K = clog2(k->n[i]);
    uint32_t off = qrail(o, k->g == 8 ? 3 : 0, K), p;
    fr[(*nf)++] = off;
    if (k->m == 1) return cq_template_add_i64_hl(off, k->s[0]);
    uint32_t c1 = qubit(j == 0);
    fr[(*nf)++] = c1;
    p = cq_template_select_i64_ll(c1, k->s[0], k->s[1]);
    fr[(*nf)++] = p;
    if (k->m == 3) {
        uint32_t c2 = qubit(j <= 1);
        fr[(*nf)++] = c2;
        p = cq_template_select_i64_hl(c2, p, k->s[2]);
        fr[(*nf)++] = p;
    }
    return cq_template_add_i64(off, p);
}

typedef struct { uint8_t h[128], t[128], cell[WIN]; uint32_t leaves, q; uint64_t gates; } snap;

/* One access: object j, offset o, ctl 0 none, 1 qubit 0, 2 qubit 1; force: the bit-pattern bound. */
static void run(const kase *k, const fns *f, int op, int j, uint64_t o, int ctl, int force, uint64_t seed, snap *out) {
    uint32_t W = f->bits, S = (W + 7) / 8, fr[8];
    int nf = 0;
    cq_replay_reset();
    uint32_t a = address(k, j, o, fr, &nf), c = ctl ? qubit(ctl == 2) : 0, h = 0, slot = 0;
    uint64_t s = seed;
    if (op != CQ_BUS_LOAD) h = prep(W, &s, (int)(seed & 1));
    const cq_bit *ab = cq_rail_bits((int32_t)a);
    cq_bit a0[64];
    memcpy(a0, ab, sizeof a0);
    uint64_t av = peek(a);
    assert(av == k->s[j] + o);
    out->q = nq(ab, 64);
    lo = 0; wn = S;
    for (uint32_t i = 0; i < 64; i++) if (cq_bit_is_q(ab[i])) wn += 1ull << i; else lo |= (uint64_t)ab[i].kind << i;
    assert(wn <= WIN);
    uint32_t live0 = cq_pool_live(), cq0 = win_q(), hq0 = h ? nq(cq_rail_bits((int32_t)h), W) : 0;
    uint64_t g0 = cq_replay_gates();
    cq_qram_force_bits = force;
    if (op == CQ_BUS_LOAD) h = ctl ? f->ldc(c, M, a) : f->ld(M, a);
    else if (op == CQ_BUS_SWAP) { if (ctl) f->swc(c, M, a, h); else f->sw(M, a, h); }
    else {
        if (ctl) f->stc(c, M, a, h); else f->st(M, a, h);
        slot = cqrt_alloc_i1(0) - 1;   /* handles are sequential: the store's tape slot */
        cqrt_free(slot + 1);
        assert(cq_rail_width((int32_t)slot) == 8 * S);
    }
    cq_qram_force_bits = 0;
    out->gates = cq_replay_gates() - g0;
    out->leaves = cq_qram_leaves;

    /* Ancillae: the free list reads 0; the live count grew by exactly the qubits now in the cells,
     * h and the tape slot. The address and the control unchanged. */
    pool_clean();
    uint32_t hq1 = nq(cq_rail_bits((int32_t)h), W), sq = slot ? nq(cq_rail_bits((int32_t)slot), 8 * S) : 0;
    assert(cq_pool_live() - live0 == win_q() - cq0 + hq1 - hq0 + sq);
    for (uint32_t i = 0; i < 64; i++) assert(ab[i].kind == a0[i].kind && ab[i].q == a0[i].q);
    assert(peek(a) == av);
    if (c) assert(peek(c) == (uint64_t)(ctl == 2));

    const cq_bit *hb = cq_rail_bits((int32_t)h);
    for (uint32_t i = 0; i < W; i++) out->h[i] = (uint8_t)bitval(hb[i]);
    for (uint32_t i = 0; i < 8 * S; i++) out->t[i] = slot ? (uint8_t)bitval(cq_rail_bits((int32_t)slot)[i]) : 0;
    int on = ctl != 1;
    for (uint32_t u = 0; u < wn; u++) {
        cq_bit b[8];
        int quantum = cq_cell_read(lo + u, b);
        out->cell[u] = 0;
        for (int i = 0; i < 8; i++) out->cell[u] |= (uint8_t)(bitval(b[i]) << i);
        if (op == CQ_BUS_LOAD) assert(!quantum);
        else if (!force && out->leaves != 1u << out->q) {   /* overlay: exactly the kept leaves' cells */
            int want = 0;
            for (int i = 0; i < k->m; i++)
                for (uint64_t x = 0; x < k->n[i] && !want; x += k->g)
                    want = lo + u >= k->s[i] + x && lo + u < k->s[i] + x + S;
            assert(quantum == want);
        }
    }
    /* Against the model: the cells at the address, h, t. */
    for (uint32_t i = 0; i < W && op == CQ_BUS_LOAD; i++)
        assert(out->h[i] == (on ? (*(uint8_t *)(uintptr_t)(av + i / 8) >> i % 8 & 1) : 0));
    if (op == CQ_BUS_LOAD) {   /* the _unc: h back to 0, ancillae back */
        if (ctl) f->uncc(h, c, M, a); else f->unc(h, M, a);
        assert(peek(h) == 0 && nq(hb, W) == hq1);
        for (uint32_t i = 64; i < W; i++) assert(!bitval(hb[i]));
        pool_clean();
    }
    assert(cq_replay_gates_of(CQ_G_M) == 0);   /* nothing measured, ancillae included */
    cqrt_free(h);
    if (slot) cqrt_free(slot);
    if (c) cqrt_free(c);
    cqrt_free(a);
    while (nf) cqrt_free(fr[--nf]);
    win_reset();
}

static uint64_t nruns, nobj, nfall;

/* Every op, width, control and offset: object path and bit pattern agree; the leaves are the bound. */
static void compare(const kase *k, int all_ctl) {
    static snap A, B;
    uint64_t obj = 0;
    for (int i = 0; i < k->m; i++) obj += (k->n[i] + k->g - 1) / k->g;
    for (unsigned w = 0; w < sizeof T / sizeof *T; w++)
        for (int op = 0; op < 3; op++)
            for (int j = 0; j < k->m; j++)
                for (uint64_t o = 0; o < k->n[j]; o += k->g)
                    for (int ctl = 0; ctl < 3; ctl++) {
                        if (!all_ctl && ctl == 1 && o && o + k->g < k->n[j]) continue;   /* control 0: ends only */
                        uint64_t seed = 0x6f626a ^ (o << 8) ^ (uint64_t)(w * 16 + (unsigned)op * 4 + (unsigned)ctl);
                        run(k, &T[w], op, j, o, ctl, 0, seed, &A);
                        run(k, &T[w], op, j, o, ctl, 1, seed, &B);
                        nruns += 2;
                        uint32_t S = (T[w].bits + 7) / 8;
                        assert(A.q == B.q && B.leaves == 1u << B.q);
                        assert(A.leaves == (obj < 1ull << A.q ? obj : 1u << A.q));
                        if (A.leaves == B.leaves) { assert(A.gates == B.gates); nfall++; } else nobj++;
                        assert(!memcmp(A.h, B.h, T[w].bits) && !memcmp(A.t, B.t, 8 * S) && !memcmp(A.cell, B.cell, wn));
                    }
}

static kase one(uint64_t *s, uint64_t at, uint64_t n, uint32_t g) {
    kase k = { 1, { 0 }, { n }, new_block(s), g };
    k.s[0] = k.blk + at;
    cqrt_qram_object((int64_t)k.s[0], (int64_t)n);
    return k;
}

static void singles(void) {
    uint64_t s = 0x73696e;
    static const uint64_t ns[] = { 5, 12, 100, 64 };
    for (unsigned i = 0; i < 4; i++)
        for (uint64_t at = 0x100; at <= 0x105; at += 5)
            for (uint32_t g = 1; g <= 8; g += 7) {
                if (g == 8 && ns[i] <= 8) continue;   /* one aligned address: a classical rail, the _hl path */
                kase k = one(&s, at, ns[i], g);
                compare(&k, ns[i] < 64);
            }
    printf("one object (5, 12, 100, 64 bytes; aligned and unaligned start; byte and 8-byte addresses), "
           "i8 i16 i32 i64 i80, load/store/swap, control none/0/1: ok\n");
}

static void unions(void) {
    uint64_t s = 0x756e69;
    for (uint32_t g = 1; g <= 8; g += 7) {   /* two touching objects */
        kase k = { 2, { 0 }, { 48, 40 }, new_block(&s), g };
        k.s[0] = k.blk + 0x100; k.s[1] = k.blk + 0x130;
        for (int i = 0; i < 2; i++) cqrt_qram_object((int64_t)k.s[i], (int64_t)k.n[i]);
        compare(&k, 1);
    }
    kase k = { 3, { 0 }, { 12, 20, 5 }, new_block(&s), 1 };   /* three, two of them touching */
    k.s[0] = k.blk + 0x100; k.s[1] = k.blk + 0x10c; k.s[2] = k.blk + 0x200;
    for (int i = 0; i < 3; i++) cqrt_qram_object((int64_t)k.s[i], (int64_t)k.n[i]);
    compare(&k, 1);
    printf("unions of two (touching, byte and 8-byte addresses) and three objects: ok\n");
}

/* One i8 load on the memory bound: its leaves are the registered cells of the bit pattern, and where
 * those are all 2^q, the gates are the forced bit pattern's. */
static void same_as_bits(uint32_t a) {
    uint64_t g[2], o, on, base = 0;
    uint32_t l[2], q = 0, want = 0, pos[64];
    const cq_bit *ab = cq_rail_bits((int32_t)a);
    for (uint32_t i = 0; i < 64; i++) if (cq_bit_is_q(ab[i])) pos[q++] = i; else base |= (uint64_t)ab[i].kind << i;
    for (uint64_t x = 0; x < 1ull << q; x++) {
        uint64_t v = base;
        for (uint32_t k = 0; k < q; k++) v |= (x >> k & 1) << pos[k];
        want += (uint32_t)cq_object_at(v, &o, &on);
    }
    for (int force = 0; force < 1 + (q <= CQ_MAX_QADDR_BITS); force++) {   /* forced above the cap aborts */
        cq_qram_force_bits = force;
        uint64_t g0 = cq_replay_gates();
        uint32_t h = cqrt_qram_load_i8(M, a);
        g[force] = cq_replay_gates() - g0; l[force] = cq_qram_leaves;
        assert(peek(h) == (cq_object_at(peek(a), &o, &on) || force ? *(uint8_t *)(uintptr_t)peek(a) : 0));
        cqrt_qram_load_i8_unc(h, M, a);
        assert(!peek(h));
        cqrt_free(h);
    }
    cq_qram_force_bits = 0;
    assert(l[0] == want && (q > CQ_MAX_QADDR_BITS || (l[1] == 1u << q && (l[0] != l[1] || g[0] == g[1]))));
}

static void cap_abort(uint64_t start) {
    fflush(NULL);
    int fd[2];
    assert(pipe(fd) == 0);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        dup2(fd[1], 2);
        cqrt_qram_object((int64_t)start, 1ll << (CQ_MAX_QADDR_BITS + 2));
        _exit(0);
    }
    close(fd[1]);
    char out[4096] = { 0 };
    size_t n = 0;
    ssize_t r;
    while (n < sizeof out - 1 && (r = read(fd[0], out + n, sizeof out - 1 - n)) > 0) n += (size_t)r;
    close(fd[0]);
    int st;
    assert(waitpid(pid, &st, 0) == pid && WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT);
    assert(strstr(out, "CQ_MAX_QADDR_BITS"));
}

static void fallbacks(void) {
    uint64_t s = 0x66616c;
    cq_replay_reset();
    kase k = one(&s, 0x100, 100, 1);
    uint64_t X = k.s[0], ZE = new_block(&s) + 0x100;
    cqrt_qram_object((int64_t)(ZE - 16), 16);   /* Z = [ZE - 16, ZE), nothing at ZE */
    uint32_t a = qrail(X, 0, 7);                 /* no note */
    assert(cq_note_get((int32_t)a).n < 0);
    same_as_bits(a);
    a = cq_template_add_i64_hl(qrail(0, 0, 4), ZE);   /* only an end entry */
    cq_note t = cq_note_get((int32_t)a);
    assert(t.n == 1 && t.e[0].end == 1);
    same_as_bits(a);
    a = qrail(X + 0x1000, 0, 3);                 /* d = 0x1000 + 0..7: bit 12 classical 1 */
    cq_note_set((int32_t)a, &(cq_note){ 1, { { X, 0 } } });
    same_as_bits(a);
    a = qrail(X - 0x100, 0, 6);                  /* the object path is not smaller: 64 bit-pattern leaves */
    cqrt_qram_object((int64_t)(X - 0x100), 64);
    cq_note_set((int32_t)a, &(cq_note){ 1, { { X - 0x100, 0 } } });
    same_as_bits(a);
    /* an object above 2^Q cells aborts at its registration; an address of Q + 2 quantum bits does not */
    cap_abort(0x7a0000000000ull);
    same_as_bits(qrail(X, 0, CQ_MAX_QADDR_BITS + 2));
    /* and with that note, an address of 7 quantum bits in the 100-byte object: 100 leaves */
    cq_replay_reset();
    a = qrail(X, 0, 7);
    cq_note_set((int32_t)a, &(cq_note){ 1, { { X, 0 } } });
    uint32_t h = cqrt_qram_load_i8(M, a);
    assert(cq_qram_leaves == 100);
    cqrt_qram_load_i8_unc(h, M, a);
    printf("fallbacks (no note, only an end entry, a classical high bit of d, not smaller, above the cap): the memory bound, ok\n");
}

/* The measured problem: a table T of N pointers into A (|A| = 512), index quantum, address =
 * T + (index << 3) through the templates, load the pointer, load through it. */
static void problem(uint32_t N) {
    uint64_t s = 0x7072 + N;
    uint64_t blk = new_block(&s), Ta = blk + 8, Aa = blk + 0x800, or = 0;   /* T only 8-aligned: carries */
    uint64_t *Tp = (uint64_t *)(uintptr_t)Ta, *Ap = (uint64_t *)(uintptr_t)Aa;
    cqrt_qram_object((int64_t)Ta, 8 * (int64_t)N);
    cqrt_qram_object((int64_t)Aa, 512);
    for (uint32_t i = 0; i < N; i++) { Tp[i] = Aa + 8 * ((i * 37 + 5) % 64); or |= Tp[i] - Aa; }
    uint32_t lg = clog2(N), want2 = 1u << __builtin_popcountll(or);
    for (uint32_t i = 0; i < N; i++) {
        cq_replay_reset();
        uint32_t idx = qrail(i, 0, lg), sh = cq_template_shl_i64_hl(idx, 3), a = cq_template_add_i64_hl(sh, Ta);
        uint32_t q1 = nq(cq_rail_bits((int32_t)a), 64), live = cq_pool_live();
        uint32_t p = cqrt_qram_load_i64(M, a);
        assert(cq_qram_leaves == N && N < 1u << q1);
        assert(peek(p) == Tp[i]);
        cq_note t = cq_note_get((int32_t)p);
        assert(t.n == 1 && t.e[0].a == Aa && !t.e[0].end);
        uint32_t v = cqrt_qram_load_i64(M, p);
        assert(cq_qram_leaves == want2 && peek(v) == Ap[(Tp[i] - Aa) / 8]);
        if (N == 64) assert(cq_qram_leaves == 512 / 8);
        cqrt_qram_load_i64_unc(v, M, p);
        cqrt_qram_load_i64_unc(p, M, a);
        assert(!peek(v) && !peek(p));
        pool_clean();
        assert(cq_pool_live() == live + nq(cq_rail_bits((int32_t)p), 64) + nq(cq_rail_bits((int32_t)v), 64));
        assert(cq_replay_gates_of(CQ_G_M) == 0);
        cqrt_free(v); cqrt_free(p); cqrt_free(a); cqrt_free(sh); cqrt_free(idx);
    }
    printf("table of %u pointers: %u leaves, then %u through the loaded pointer: ok\n", N, N, want2);
}

/* The leaves are the intersection with the bit pattern: struct { i64 x, y; } arr[3] at +0x28, address
 * bits 4 and 5 quantum, bit 6 classical 0. arr[2].x (bit 6 = 1) is not reachable; the ripple of
 * d = addr - start alone would keep it and a store would hide a later host write to it. */
static void strict(void) {
    uint64_t s = 0x57, X = new_block(&s) + 0x28;
    cqrt_qram_object((int64_t)X, 48);
    cq_replay_reset();
    uint32_t a = qrail(X, 4, 6), v = cqrt_alloc_i64(0xaaaa);
    cq_note_set((int32_t)a, &(cq_note){ 1, { { X, 0 } } });
    cqrt_qram_store_i64(M, a, v);
    assert(cq_qram_leaves == 2);
    *(uint64_t *)(uintptr_t)(X + 32) = 0x7777;
    assert(peek(cqrt_qram_load_i64_hl(M, X + 32)) == 0x7777);
    assert(peek(cqrt_qram_load_i64_hl(M, X)) == 0xaaaa);
    printf("strict intersection (a cell of the object the address bits cannot reach stays HOST): ok\n");
}

int main(void) {
    M = cqrt_qram_alloc_i8(0);
    uint8_t *m = mmap(NULL, 2 * HSIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(m != MAP_FAILED);
    H = m + (-(uintptr_t)m & (HSIZE - 1));
    fallbacks();
    strict();
    problem(2); problem(4); problem(64);
    unions();
    singles();
    printf("%llu runs: %llu on the object path, %llu fell back with the same gates\n",
           (unsigned long long)nruns, (unsigned long long)nobj, (unsigned long long)nfall);
    return 0;
}
