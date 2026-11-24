/* Memory bus §1.10 with a quantum address (kmgy.21, docs/QRAM_CONSTRUCTION.md): load, swap, store
 * and their controlled forms at i8, i16, i17 and i64, q = 1..6 quantum address bits at low, high,
 * scattered and wrapping positions, against a plain C model of memory + overlay; every ancilla
 * back; the depth formula of the note in the clean case and linear growth in q; the cap abort;
 * cq_invert. The replay backend is classical: each run is one basis address. Always asserts. */
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
#include "cq_replay.h"

static uint32_t M;
static uint8_t *H;           /* host memory the reachable sets fall in, HSIZE-aligned */
#define HSIZE (1u << 16)

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
#define F_(n) { #n, 0, cqrt_qram_load_##n, cqrt_qram_load_##n##_controlled, cqrt_qram_load_##n##_unc, \
    cqrt_qram_load_##n##_controlled_unc, cqrt_qram_store_##n, cqrt_qram_store_##n##_controlled, \
    cqrt_qram_swap_##n, cqrt_qram_swap_##n##_controlled }
static fns T[] = { F_(i8), F_(i16), F_(i17), F_(i64) };
static const uint32_t TW[] = { 8, 16, 17, 64 };

static uint64_t rng(uint64_t *s) {   /* splitmix64 */
    uint64_t z = (*s += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}
static uint64_t mask(uint32_t w) { return w >= 64 ? ~0ull : (1ull << w) - 1; }
static uint64_t bitval(cq_bit b) { return cq_bit_is_q(b) ? (uint64_t)cq_replay_get_bit(b.q) : b.kind; }

/* A rail's value from the replay bits, not measured; its qubit count. */
static uint64_t peek(uint32_t h) {
    uint64_t v = 0;
    const cq_bit *b = cq_rail_bits((int32_t)h);
    for (uint32_t i = 0; i < cq_rail_width((int32_t)h); i++) v |= bitval(b[i]) << i;
    return v;
}
static uint32_t nq(const cq_bit *b, uint32_t n) { uint32_t k = 0; while (n--) k += cq_bit_is_q(b[n]); return k; }
/* A rail holding v; quantum: every bit a qubit set by replay, no gate. */
static uint32_t prep(uint64_t v, uint32_t w, int quantum) {
    uint32_t h = (uint32_t)cq_rail_alloc(w, &v);
    cq_bit *b = cq_rail_bits((int32_t)h);
    for (uint32_t i = 0; quantum && i < w; i++) {
        b[i] = cq_bit_const(0);
        cq_get_qubit(&b[i]);
        cq_replay_set_bit(b[i].q, (int)(v >> i & 1));
    }
    return h;
}
/* Every qubit in the pool's free list reads 0. */
static void pool_clean(void) {
    uint32_t n = cq_pool_minted() - cq_pool_live(), *q = malloc((n + 1) * sizeof *q);
    assert(q);
    for (uint32_t i = 0; i < n; i++) { q[i] = cq_pool_take(); assert(!cq_replay_get_bit(q[i])); }
    while (n) cq_pool_give(q[--n]);
    free(q);
}

/* ---- One case: the address rail, its reachable cells and their model. */
typedef struct {
    uint32_t q, pos[64], S, nc;
    uint64_t base, *ca;           /* distinct reachable cells, ascending */
    uint8_t *cv, *cqm;            /* model: byte, QUANTUM */
    uint32_t r;                   /* the address rail */
} kase;

static uint64_t leaf(const kase *k, uint32_t a) {
    uint64_t la = k->base;
    for (uint32_t j = 0; j < k->q; j++) la |= (uint64_t)(a >> j & 1) << k->pos[j];
    return la;
}
static int cmp64(const void *x, const void *y) { uint64_t a = *(const uint64_t *)x, b = *(const uint64_t *)y; return (a > b) - (a < b); }
static uint32_t cell_ix(const kase *k, uint64_t a) {
    const uint64_t *p = bsearch(&a, k->ca, k->nc, sizeof a, cmp64);
    assert(p);
    return (uint32_t)(p - k->ca);
}
static int in_H(uint64_t a) { return a - (uintptr_t)H < HSIZE; }
static uint32_t cells_q(const kase *k) {
    uint32_t n = 0;
    for (uint32_t u = 0; u < k->nc; u++) { cq_bit b[8]; cq_cell_read(k->ca[u], b); n += nq(b, 8); }
    return n;
}
/* Every reachable cell matches the model: kind, value, and the host byte of a HOST cell. */
static void check_cells(const kase *k) {
    for (uint32_t u = 0; u < k->nc; u++) {
        cq_bit b[8];
        assert(cq_cell_read(k->ca[u], b) == k->cqm[u]);
        uint8_t v = 0;
        for (int i = 0; i < 8; i++) v |= (uint8_t)(bitval(b[i]) << i);
        assert(v == k->cv[u]);
        if (!k->cqm[u] && in_H(k->ca[u])) assert(H[k->ca[u] - (uintptr_t)H] == v);
    }
}

/* mix: 0 every cell as it is (HOST), 1 every cell QUANTUM, 2 a random half QUANTUM. */
static void case_open(kase *k, uint32_t q, const uint32_t *pos, uint64_t base, uint32_t S, int mix, uint64_t *s) {
    k->q = q; k->S = S; k->base = base;
    memcpy(k->pos, pos, q * sizeof *pos);
    uint32_t N = 1u << q;
    k->ca = malloc((size_t)N * S * sizeof *k->ca);
    k->cv = malloc((size_t)N * S); k->cqm = malloc((size_t)N * S);
    assert(k->ca && k->cv && k->cqm);
    for (uint32_t a = 0; a < N; a++)
        for (uint32_t j = 0; j < S; j++) k->ca[a * S + j] = leaf(k, a) + j;
    qsort(k->ca, (size_t)N * S, sizeof *k->ca, cmp64);
    k->nc = 0;
    for (uint32_t i = 0; i < N * S; i++) if (!k->nc || k->ca[k->nc - 1] != k->ca[i]) k->ca[k->nc++] = k->ca[i];
    for (uint32_t u = 0; u < k->nc; u++) {
        uint64_t a = k->ca[u];
        if (mix == 1 || (mix == 2 && rng(s) & 1)) {   /* QUANTUM: an _hl store of a quantum byte */
            uint32_t v = prep(rng(s) & 0xff, 8, 1);
            cqrt_qram_store_i8_hl(M, a, v);
            cqrt_free(v + 1); cqrt_free(v);            /* the tape slot is v + 1 */
        } else if (in_H(a)) {
            cq_bit b[8];
            if (!cq_cell_read(a, b)) H[a - (uintptr_t)H] = (uint8_t)rng(s);
        }
        cq_bit b[8];
        k->cqm[u] = (uint8_t)cq_cell_read(a, b);
        k->cv[u] = 0;
        for (int i = 0; i < 8; i++) k->cv[u] |= (uint8_t)(bitval(b[i]) << i);
        if (!k->cqm[u]) assert(k->cv[u] == (in_H(a) ? H[a - (uintptr_t)H] : 0));   /* unreadable reads 0 */
    }
    uint64_t bv = base;
    k->r = prep(bv, 64, 0);
    cq_bit *rb = cq_rail_bits((int32_t)k->r);
    for (uint32_t j = 0; j < q; j++) { assert(!(base >> pos[j] & 1)); cq_get_qubit(&rb[pos[j]]); }
}
/* Back to HOST where writable (an _hl store of 0), the tape slots and the rail freed. */
static void case_close(kase *k) {
    cqrt_free(k->r);
    for (uint32_t u = 0; u < k->nc; u++) {
        uint32_t v = cqrt_alloc_i8(0);
        cqrt_qram_store_i8_hl(M, k->ca[u], v);
        cqrt_free(v + 1); cqrt_free(v);
    }
    free(k->ca); free(k->cv); free(k->cqm);
}

/* After cq_replay_reset: the qubits of the address and of QUANTUM cells hold their model values. */
static void set_state(const kase *k, uint32_t av) {
    const cq_bit *rb = cq_rail_bits((int32_t)k->r);
    for (uint32_t j = 0; j < k->q; j++) cq_replay_set_bit(rb[k->pos[j]].q, (int)(av >> j & 1));
    for (uint32_t u = 0; u < k->nc; u++) {
        cq_bit b[8];
        cq_cell_read(k->ca[u], b);
        for (int i = 0; i < 8; i++)
            if (cq_bit_is_q(b[i])) cq_replay_set_bit(b[i].q, k->cv[u] >> i & 1);
    }
}

static int formula(int op, uint32_t q, uint32_t wb) {   /* QRAM_CONSTRUCTION.md §4, fan-out */
    int L = 0;
    while ((1u << L) < wb) L++;
    int P = 2 * (int)q + 1 + L;
    return 2 * P + 2 * (int)q + (op == CQ_BUS_LOAD ? 3 : op == CQ_BUS_SWAP ? 5 : 4);
}

/* Excess depth over the formula, max per category, for the report. */
static int ex_clean_lo = 1 << 30, ex_clean_hi = -(1 << 30), ex_overlap[3], ex_mixed[3];
static uint64_t runs;

/* One run: basis address av, control mode ctl (0 none, 1 classical 0, 2 classical 1, 3 qubit 0,
 * 4 qubit 1), the value rail quantum or classical. Returns the depth. */
static uint32_t run(kase *k, const fns *f, int op, uint32_t av, int ctl, int forced, uint64_t *s) {
    uint32_t W = f->bits, S = k->S, wb = op == CQ_BUS_STORE ? 8 * S : W;
    cq_replay_reset();
    set_state(k, av);
    int on = ctl == 0 || ctl == 2 || ctl == 4;
    uint32_t c = ctl ? prep(ctl == 2 || ctl == 4, 1, ctl >= 3) : 0;
    uint64_t v = rng(s) & mask(W);
    uint32_t h = op == CQ_BUS_LOAD ? 0 : prep(v, W, forced);
    uint64_t la = leaf(k, av), old = 0;
    for (uint32_t j = 0; j < S; j++) old |= (uint64_t)k->cv[cell_ix(k, la + j)] << (8 * j);
    int clean = 1;   /* every reachable cell bit a qubit, leaves disjoint */
    for (uint32_t u = 0; u < k->nc; u++) { cq_bit b[8]; cq_cell_read(k->ca[u], b); clean &= nq(b, 8) == 8; }
        uint32_t live0 = cq_pool_live(), cq0 = cells_q(k), hq0 = h ? nq(cq_rail_bits((int32_t)h), W) : 0;
    const cq_bit *rb = cq_rail_bits((int32_t)k->r);
    cq_bit r0[64];
    memcpy(r0, rb, sizeof r0);

    uint32_t slot = 0;
    if (op == CQ_BUS_LOAD) h = ctl ? f->ldc(c, M, k->r) : f->ld(M, k->r);
    else if (op == CQ_BUS_SWAP) { if (ctl) f->swc(c, M, k->r, h); else f->sw(M, k->r, h); }
    else {
        if (ctl) f->stc(c, M, k->r, h); else f->st(M, k->r, h);
        slot = cqrt_alloc_i1(0) - 1;   /* handles are sequential: the store's tape slot */
        cqrt_free(slot + 1);
        assert(cq_rail_width((int32_t)slot) == 8 * S);
    }
    uint32_t depth = cq_replay_depth();
    runs++;

    /* Ancillae: the free list reads 0, and the live count grew by exactly the qubits now held by
     * the cells, h and the tape slot. */
    pool_clean();
    uint32_t hq1 = nq(cq_rail_bits((int32_t)h), W), sq = slot ? nq(cq_rail_bits((int32_t)slot), 8 * S) : 0;
    assert(cq_pool_live() - live0 == cells_q(k) - cq0 + hq1 - hq0 + sq);
    /* The address rail and the control are unchanged. */
    for (uint32_t i = 0; i < 64; i++) assert(rb[i].kind == r0[i].kind && rb[i].q == r0[i].q);
    for (uint32_t j = 0; j < k->q; j++) assert(cq_replay_get_bit(rb[k->pos[j]].q) == (int)(av >> j & 1));
    if (c) assert(peek(c) == (uint64_t)(ctl == 2 || ctl == 4));
    if (ctl == 1) assert(cq_replay_gates() == 0);

    /* Values against the model. */
    if (op == CQ_BUS_LOAD) assert(peek(h) == (on ? old & mask(W) : 0));
    else if (op == CQ_BUS_SWAP) {
        assert(peek(h) == (on ? old & mask(W) : v));
        if (on) old = (old & ~mask(W)) | v;
    } else {
        assert(peek(slot) == (on ? old : 0) && peek(h) == v);
        if (on) old = v;
    }
    for (uint32_t j = 0; j < S; j++) k->cv[cell_ix(k, la + j)] = (uint8_t)(old >> (8 * j));
    if (op != CQ_BUS_LOAD && ctl != 1)   /* the overlay: every reachable cell QUANTUM */
        for (uint32_t u = 0; u < k->nc; u++) k->cqm[u] = 1;
    check_cells(k);

    if (ctl != 1) {
        int ex = (int)depth - formula(op, k->q, wb);
        uint64_t m = (S + (1ull << k->pos[0]) - 1) >> k->pos[0];   /* most leaves sharing a cell, §4.4 */
        if (m > 1u << k->q) m = 1u << k->q;
        if (m > 1) {   /* overlapping leaves serialise on the shared cells: within the note's bound */
            assert(ex <= (op == CQ_BUS_LOAD ? 2 : op == CQ_BUS_SWAP ? 4 : 3) * (int)(m - 1));
            if (ex > ex_overlap[op]) ex_overlap[op] = ex;
        } else if (clean && forced) {
            if (ex < ex_clean_lo) ex_clean_lo = ex;
            if (ex > ex_clean_hi) ex_clean_hi = ex;
            /* The note's formula with a qubit control; without one the token is a known 1 and the
             * emitter folds the first CCX of the decoder: one layer less. */
            assert(ex == (ctl >= 3 ? 0 : -1));
        } else {   /* known bits (HOST cells, a classical value) only remove gates */
            assert(ex <= 0);
            if (ex < ex_mixed[op]) ex_mixed[op] = ex;
        }
    }

    if (op == CQ_BUS_LOAD) {   /* the _unc clears h, ancillae back again */
        if (ctl) f->uncc(h, c, M, k->r); else f->unc(h, M, k->r);
        assert(peek(h) == 0);
        pool_clean();
        check_cells(k);
    }
    cqrt_free(h);
    if (slot) cqrt_free(slot);
    if (c) cqrt_free(c);
    return depth;
}

/* Quantum positions: 0 low (0..q-1, leaves overlap when S > 1), 1 high (63, 62, ...: one leaf in
 * H, the rest unreadable), 2 scattered in 3..12 (disjoint leaves, non-contiguous, in H), 3 wrap
 * (high bits, the base the last byte below 2^64). */
static uint64_t positions(int kind, uint32_t q, uint32_t *pos, uint64_t *s) {
    if (kind == 0) { for (uint32_t j = 0; j < q; j++) pos[j] = j; return (uintptr_t)H; }
    if (kind == 1 || kind == 3) {
        uint64_t base = kind == 1 ? (uintptr_t)H : UINT64_MAX;
        for (uint32_t j = 0; j < q; j++) { pos[j] = 64 - q + j; base &= ~(1ull << pos[j]); }
        return base;
    }
    uint32_t pick = 0;
    while ((uint32_t)__builtin_popcount(pick) < q) pick |= 1u << (3 + rng(s) % 10);
    for (uint32_t j = 0, b = 0; j < q; b++) if (pick >> b & 1) pos[j++] = b;
    return (uintptr_t)H;
}

static void sweep(void) {
    uint64_t s = 0x6b6d67792e3231ull;
    cq_qram_force_bits = 1;   /* the sweep's addresses are mostly outside H: every bit-pattern leaf, registered or not */
    int cs = 0;
    for (unsigned t = 0; t < sizeof T / sizeof *T; t++)
        for (uint32_t q = 1; q <= 6; q++)
            for (int kind = 0; kind < 4; kind++)
                for (int op = 0; op < 3; op++, cs++) {
                    uint32_t pos[64];
                    uint64_t base = positions(kind, q, pos, &s);
                    kase k;
                    case_open(&k, q, pos, base, (T[t].bits + 7) / 8, (int)(q + (uint32_t)kind + (uint32_t)op) % 3, &s);
                    uint32_t N = 1u << q, nrun = q <= 3 ? N : 4;   /* every address, or 0, N-1 and two sampled */
                    for (uint32_t i = 0; i < nrun; i++) {
                        uint32_t av = nrun == N ? i : i == 0 ? 0 : i == 1 ? N - 1 : (uint32_t)(rng(&s) % N);
                        for (int ctl = 0; ctl < 5; ctl++) run(&k, &T[t], op, av, ctl, (int)(i + ctl + 1) & 1 || op == CQ_BUS_LOAD, &s);
                    }
                    case_close(&k);
                }
    printf("sweep: %d cases, %llu runs (i8 i16 i17 i64, q 1..6, 4 position kinds, load/swap/store, 5 controls): ok\n",
           cs, (unsigned long long)runs);
    printf("depth minus formula: clean %d..%d (qubit control 0, none -1); overlapping leaves max load +%d swap +%d store +%d; "
           "disjoint, HOST/mixed cells or a classical value: at most 0, min load %+d swap %+d store %+d\n", ex_clean_lo, ex_clean_hi,
           ex_overlap[0], ex_overlap[2], ex_overlap[1], ex_mixed[0], ex_mixed[2], ex_mixed[1]);
}

/* Depth exactly the formula and linear in q: clean case, disjoint leaves (positions 3..), a qubit
 * control, up to q = 10 at i8 and 7 at i64. */
static void linear(void) {
    uint64_t s = 0x6c696e;
    for (unsigned t = 0; t < sizeof T / sizeof *T; t += 3)
        for (int op = 0; op < 3; op++) {
            uint32_t prev = 0, qmax = T[t].bits == 8 ? 10 : 7;
            printf("%s %s depth, q = 1..%u:", T[t].name, op == 0 ? "load" : op == 1 ? "store" : "swap", qmax);
            for (uint32_t q = 1; q <= qmax; q++) {
                uint32_t pos[64];
                for (uint32_t j = 0; j < q; j++) pos[j] = 3 + j;
                kase k;
                case_open(&k, q, pos, (uintptr_t)H, (T[t].bits + 7) / 8, 1, &s);
                uint32_t d = run(&k, &T[t], op, (uint32_t)rng(&s) % (1u << q), 4, 1, &s);
                case_close(&k);
                printf(" %u", d);
                assert(prev == 0 || d - prev == 6);   /* 2P + 2q + const, P = 2q + 1 + L */
                prev = d;
            }
            printf("\n");
        }
}

/* With the memory filled to 2^Q cells, one more registered cell aborts at the registration, naming Q and the total. */
static void cap_abort(void) {
    int fd[2];
    assert(pipe(fd) == 0);
    fflush(NULL);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        dup2(fd[1], 2);
        cqrt_qram_object((int64_t)(uintptr_t)H, HSIZE);   /* again: idempotent */
        cqrt_qram_object(0x7a0000000000ll, (1ll << CQ_MAX_QADDR_BITS) - HSIZE);   /* full: no abort */
        fprintf(stderr, "full\n");
        cqrt_qram_object(0x7b0000000000ll, 1);
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
    char want[2][64];
    snprintf(want[0], sizeof want[0], "2^%d", CQ_MAX_QADDR_BITS);
    snprintf(want[1], sizeof want[1], "%lld cells", (1ll << CQ_MAX_QADDR_BITS) + 1);
    assert(strstr(out, "full\n") && strstr(out, "CQ_MAX_QADDR_BITS") && strstr(out, want[0]) && strstr(out, want[1]));
    printf("a registration above 2^CQ_MAX_QADDR_BITS cells aborts: ok\n");
}

/* cq_invert: X; invert{X} restores the state, for a swap and a load _unc. */
static void invert(void) {
    uint64_t s = 0x696e76;
    uint32_t pos[] = { 0, 4, 9 };
    kase k;
    case_open(&k, 3, pos, (uintptr_t)H, 2, 2, &s);
    for (uint32_t av = 0; av < 8; av++) {
        cq_replay_reset();
        set_state(&k, av);
        uint32_t h = prep(0xbeef, 16, 1);
        cqrt_qram_swap_i16(M, k.r, h);
        cqrt_invert_begin(); cqrt_qram_swap_i16(M, k.r, h); cqrt_invert_end();
        assert(peek(h) == 0xbeef);
        uint32_t g = cqrt_qram_load_i16(M, k.r);
        cqrt_invert_begin(); cqrt_qram_load_i16_unc(g, M, k.r); cqrt_invert_end();
        assert(peek(g) == 0);
        pool_clean();
        for (uint32_t u = 0; u < k.nc; u++) k.cqm[u] = 1;   /* the swaps made every reachable cell QUANTUM */
        check_cells(&k);
        cqrt_free(g); cqrt_free(h);
    }
    case_close(&k);
    printf("cq_invert: swap; invert{swap} and load; invert{load_unc} restore the state: ok\n");
}

int main(void) {
    for (unsigned t = 0; t < sizeof T / sizeof *T; t++) T[t].bits = TW[t];
    M = cqrt_qram_alloc_i8(0);
    uint8_t *m = mmap(NULL, 2 * HSIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(m != MAP_FAILED);
    H = m + (-(uintptr_t)m & (HSIZE - 1));   /* aligned: the quantum positions are 0 in H's address */
    assert(CQ_MAX_QADDR_BITS >= 16 && !getenv("CQ_MAX_QADDR_BITS"));   /* H fits; cap_abort's numbers */
    cqrt_qram_object((int64_t)(uintptr_t)H, HSIZE);   /* a quantum address reaches registered memory only */
    cap_abort();
    invert();
    linear();
    sweep();
    return 0;
}
