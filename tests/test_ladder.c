/* The angle ladder for a quantum angle rail (kmgy.18): the reduction circuit K = k(A) against
 * tests/ref/k_vectors.txt (tests/ref/k_ref.py, both P), fully and partly quantum A; the public
 * _qangle forms' gate streams; agreement with the one-rotation classical path. Always asserts.
 * Fully quantum A costs ~15 ms a vector, so by default: the specials and witnesses (172 lines) with both
 * window constructions, every 5th random line with the shipped one (400). CQ_LADDER_FULL=1 in the
 * environment: all 2172 lines with both (~1 min). Partly quantum: the 172 hard lines. */
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "harness.h"
#include "cqrt.h"
#include "cq_replay.h"
#include "kernels.h"
#include "angle_k.h"

#define N CQ_LADDER_N
#define ALL (~0ull)
#define SIGN (1ull << 63)
#define EXP (0x7FFull << 52)
#define SIG ((1ull << 52) - 1)
static const double FL_P[3] = { 0, 0x1.921fb54442d18p+2, 0x1.921fb54442d18p+3 };   /* fl(2pi), fl(4pi) by s */

static int32_t rail_of(uint64_t v, uint32_t w) { return hx_to_rail((hx_val){ { v } }, w); }
static void force_mask(int32_t h, uint64_t mask) {
    cq_bit *b = cq_rail_bits(h);
    for (uint32_t i = 0; i < cq_rail_width(h); i++) if (mask >> i & 1) cq_get_qubit(&b[i]);
}
/* Every qubit in the pool's free list reads 0: the ancillae came back clean. */
static void clean(void) {
    uint32_t n = cq_pool_minted() - cq_pool_live(), *q = malloc((n + 1) * sizeof *q);
    assert(q);
    for (uint32_t i = 0; i < n; i++) { q[i] = cq_pool_take(); assert(!cq_replay_get_bit(q[i])); }
    while (n) cq_pool_give(q[--n]);
    free(q);
}

/* K by the circuit, A's bits in mask forced to qubits: o ^= K, the tape undone (the second reduction).
 * Checks A unchanged, the pool balanced and every freed qubit 0; returns K, the reduction's cost in st
 * (compute, copy and uncompute). */
static uint64_t reduce(int kind, uint64_t a, uint64_t mask, int s, hx_stats *st) {
    uint32_t live = cq_pool_live();
    cq_replay_reset();
    int32_t A = rail_of(a, 64), o = rail_of(0, N);
    force_mask(A, mask);
    uint64_t g0 = cq_replay_gates();
    cq_tape t = { 0 };
    const cq_bit *k[N];
    cq_bit *ob = cq_rail_bits(o);
    cq_angle_k_circuit(kind, &t, cq_rail_bits(A), s, k);
    for (uint32_t i = 0; i < N; i++) cq_cx(k[i], &ob[i]);
    cq_tape_undo(&t);
    assert(cq_replay_gates_of(CQ_G_RY) + cq_replay_gates_of(CQ_G_RY_CTL) + cq_replay_gates_of(CQ_G_RZ) == 0);
    if (st) *st = (hx_stats){ cq_replay_gates() - g0, cq_replay_depth(), cq_replay_qubits(), 1 };
    uint64_t K = hx_from_rail(o).l[0];
    assert(hx_from_rail(A).l[0] == a);
    cq_rail_free(A); cq_rail_free(o);
    assert(cq_pool_live() == live);
    clean();
    return K;
}

typedef struct { uint64_t a, k; int s; } vec;
static vec v[4096];
static unsigned nv, nrand;   /* v[nrand ..] are the specials, the witnesses and their negations */

static void load(void) {
    FILE *f = fopen(CQ_SOURCE_DIR "/tests/ref/k_vectors.txt", "r");
    assert(f);
    char line[256], P[8];
    unsigned long long a, k;
    unsigned n;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        assert(sscanf(line, "%llx %7s %u %llx", &a, P, &n, &k) == 4 && n == N && nv < 4096);
        v[nv++] = (vec){ a, k, !strcmp(P, "4pi") ? 2 : 1 };
    }
    fclose(f);
    assert(nv == 2172);
    nrand = 2000;
}

static const char *kname(int kind) { return cq_kernel_ops[CQ_KOP_ANGLE_K].cand[kind].name; }

/* The vectors from i0 on (the random ones before nrand only every step-th), mask forced quantum;
 * every mismatch printed, then assert. */
static void vectors(int kind, unsigned i0, unsigned step, uint64_t mask, uint64_t below_gates, const char *what) {
    unsigned bad = 0, n = 0;
    hx_stats st;
    for (unsigned i = i0; i < nv; i += i < nrand ? step : 1) {
        n++;
        uint64_t K = reduce(kind, v[i].a, mask, v[i].s, &st);
        if (K != v[i].k) {
            fprintf(stderr, "%s %s: k(%016llx, %s) = %llx, want %llx\n", kname(kind), what, (unsigned long long)v[i].a,
                    v[i].s == 2 ? "4pi" : "2pi", (unsigned long long)K, (unsigned long long)v[i].k);
            bad++;
        }
        if (below_gates) assert(st.gates < below_gates);
    }
    assert(!bad);
    printf("reduction %-6s %-16s %4u vectors: K = k_ref, A unchanged, K and ancillae back to 0\n", kname(kind), what, n);
}

/* ---- The public forms with a quantum A. */
static uint64_t rots(void) {
    return cq_replay_gates_of(CQ_G_RY) + cq_replay_gates_of(CQ_G_RY_CTL) + cq_replay_gates_of(CQ_G_RZ);
}
/* The logged rotation angles are each fl(P) 2^(i-N), i < N, sign sg, every i exactly per times. */
static void ladder_angles(int s, double sg, uint64_t per) {
    uint64_t cnt[N] = { 0 }, n = rots();
    assert(n == per * N && n <= CQ_REPLAY_ANGLES);
    for (uint64_t r = 0; r < n; r++) {
        double a = cq_replay_angle(r) * sg;
        int i = ilogb(a) - ilogb(FL_P[s]) + N;
        assert(i >= 0 && i < N && a == ldexp(FL_P[s], i - N));
        cnt[i]++;
    }
    for (int i = 0; i < N; i++) assert(cnt[i] == per);
}

typedef void (*ryq_fn)(uint32_t, uint32_t);
typedef void (*ryqc_fn)(uint32_t, uint32_t, uint32_t);
static void ry_forms(const char *name, uint32_t w, ryq_fn ryq, ryqc_fn ryqc, uint64_t red_gates) {
    const uint64_t a = 0x400921fb54442d18ull;   /* fl(pi) */
    for (int mode = 0; mode < 4; mode++)        /* 0 uncontrolled, 1 c = 0, 2 c = 1, 3 c quantum */
        for (int inv = 0; inv < 2; inv++) {
            uint32_t live = cq_pool_live();
            cq_replay_reset();
            int32_t A = rail_of(a, 64), h = hx_to_rail(hx_ones(w), w), c = rail_of(mode >= 2, 1);
            force_mask(A, ALL); force_mask(h, ALL);
            if (mode == 3) force_mask(c, 1);
            uint64_t g0 = cq_replay_gates();
            if (inv) cqrt_invert_begin();
            if (mode == 0) ryq((uint32_t)h, (uint32_t)A); else ryqc((uint32_t)c, (uint32_t)h, (uint32_t)A);
            if (inv) { assert(cq_replay_gates() == g0); cqrt_invert_end(); }
            uint64_t g = cq_replay_gates() - g0;
            if (!mode && !inv) printf(" %s:%u", name, cq_replay_depth());
            if (mode == 1) assert(g == 0);   /* classical control 0: nothing at all */
            else {
                assert(cq_replay_gates_of(CQ_G_RY_CTL) == (uint64_t)N * w && cq_replay_gates_of(CQ_G_RY) == 0);
                if ((uint64_t)N * w <= CQ_REPLAY_ANGLES) ladder_angles(2, inv ? -1 : 1, w);
                /* besides the rotations: the reduction twice, K's copies for a wide h, the bracket */
                assert(g - (uint64_t)N * w >= red_gates - N);
                if (w <= N && mode != 3) assert(g - (uint64_t)N * w == red_gates - N);   /* no copy of K */
            }
            assert(hx_eq(hx_from_rail(h), hx_ones(w)) && hx_from_rail(A).l[0] == a);
            assert(hx_from_rail(c).l[0] == (uint64_t)(mode >= 2));
            cq_rail_free(A); cq_rail_free(h); cq_rail_free(c);
            assert(cq_pool_live() == live);
            clean();
        }
}

static void rz_forms(uint64_t red_gates) {
    const uint64_t a = 0xc0091eb851eb851full;   /* -3.14 */
    for (int mode = 0; mode < 4; mode++)
        for (int inv = 0; inv < 2; inv++) {
            uint32_t live = cq_pool_live();
            cq_replay_reset();
            int32_t A = rail_of(a, 64), c = rail_of(mode >= 2, 1);
            force_mask(A, ALL);
            if (mode == 3) force_mask(c, 1);
            uint64_t g0 = cq_replay_gates();
            if (inv) cqrt_invert_begin();
            if (mode == 0) cqrt_rz_qangle((uint32_t)A); else cqrt_rz_qangle_controlled((uint32_t)c, (uint32_t)A);
            if (inv) cqrt_invert_end();
            uint64_t g = cq_replay_gates() - g0;
            if (mode == 1) assert(g == 0);
            else {
                assert(cq_replay_gates_of(CQ_G_RZ) == N && cq_replay_gates_of(CQ_G_RY_CTL) == 0);
                ladder_angles(1, inv ? -1 : 1, 1);
                if (mode == 3) assert(g - N >= red_gates); else assert(g - N == red_gates - N);
            }
            assert(hx_from_rail(A).l[0] == a && hx_from_rail(c).l[0] == (uint64_t)(mode >= 2));
            cq_rail_free(A); cq_rail_free(c);
            assert(cq_pool_live() == live);
            clean();
        }
}

/* tests/test_invert.c's pattern: X; invert{X} and invert{X}; X give the identity, the angles of one
 * half the other's negated (calls in reverse order), and A, h and every ancilla as before. */
static void invert_pair(void) {
    for (int order = 0; order < 2; order++) {
        uint32_t live = cq_pool_live();
        cq_replay_reset();
        int32_t A = rail_of(0x401921fb54442d18ull, 64), h = rail_of(0x5a, 8), c = rail_of(1, 1);
        force_mask(A, ALL); force_mask(h, ALL); force_mask(c, 1);
        for (int half = 0; half < 2; half++) {
            int inv = half != order;
            if (inv) cqrt_invert_begin();
            cqrt_ry_i8_qangle_controlled((uint32_t)c, (uint32_t)h, (uint32_t)A);
            cqrt_rz_qangle((uint32_t)A);
            if (inv) cqrt_invert_end();
        }
        uint64_t n = rots();
        assert(n == 2 * (8 * N + N));
        /* forward [ry's 8N, rz's N]; inverted the calls reversed, each call's angles negated in place */
        uint64_t f0 = order ? n / 2 : 0, i0 = order ? 0 : n / 2;
        for (uint64_t i = 0; i < n / 2; i++)
            assert(cq_replay_angle(f0 + i) == -cq_replay_angle(i0 + (i < 8 * N ? N + i : i - 8 * N)));
        assert(hx_from_rail(A).l[0] == 0x401921fb54442d18ull && hx_from_rail(h).l[0] == 0x5a && hx_from_rail(c).l[0] == 1);
        cq_rail_free(A); cq_rail_free(h); cq_rail_free(c);
        assert(cq_pool_live() == live);
        clean();
    }
    printf("X; invert{X} and invert{X}; X: identity, calls reversed, angles negated\n");
}

/* Classical A: one Ry of fl(K fl(P)) 2^-N. Quantum A: the rotations K_i selects add to K fl(P) 2^-N
 * exactly. Their difference is the classical path's one rounding: at most half an ulp of it. */
static void agree(void) {
    double worst = 0, worst_ulps = 0;
    unsigned n = 0;
    for (unsigned j = 0; j < nrand; j += 10) {
        uint64_t a = v[j].a;
        int s = v[j].s;
        double x; memcpy(&x, &a, 8);
        if (!isfinite(x) || s != 2) continue;
        cq_replay_reset();
        int32_t A = rail_of(a, 64), h = rail_of(0, 1);
        cqrt_ry_i1_qangle((uint32_t)h, (uint32_t)A);
        assert(rots() == 1);
        double one = cq_replay_angle(0);
        cq_rail_free(A); cq_rail_free(h);
        uint64_t K = reduce(cq_angle_k_kind(), a, ALL, s, NULL);
        /* the ladder's sum K fl(P) 2^-N = K M 2^-sc, fl(P) = M 2^(e-53): exact in 128 bits, and so is one 2^sc */
        int e; double m = frexp(FL_P[s], &e), u = ldexp(1, ilogb(one) - 52);
        int sc = 53 + N - e;
        unsigned __int128 KM = (unsigned __int128)K * (uint64_t)ldexp(m, 53);
        double oi = ldexp(one, sc);
        assert(oi == floor(oi) && oi < 0x1p127);
        __int128 d = (__int128)(unsigned __int128)oi - (__int128)KM;
        double diff = ldexp((double)(d < 0 ? -d : d), -sc);
        assert(K == 0 ? one == 0 : diff <= u / 2);
        n++;
        if (diff > worst) worst = diff;
        if (K && diff / u > worst_ulps) worst_ulps = diff / u;
    }
    printf("classical one-rotation angle vs the ladder's sum, %u doubles: max |difference| %.3g rad (%.3g ulp of the angle)\n",
           n, worst, worst_ulps);
}

int main(void) {
    load();
    int ship = cq_angle_k_kind(), other = !ship;
    hx_stats full[2], st;
    for (int kind = 0; kind < 2; kind++) {
        (void)reduce(kind, 0x400921fb54442d18ull, ALL, 2, &full[kind]);
        printf("reduction %-6s fully quantum, compute + copy + uncompute: %llu gates, depth %u, %u qubits\n",
               kname(kind), (unsigned long long)full[kind].gates, full[kind].depth, full[kind].qubits);
    }
    int full_run = getenv("CQ_LADDER_FULL") != NULL;
    vectors(ship, 0, full_run ? 1 : 5, ALL, 0, "fully quantum");
    vectors(other, full_run ? 0 : nrand, 1, ALL, 0, "fully quantum");
    const struct { uint64_t mask; const char *what; } part[] = {
        { SIGN, "sign quantum" }, { EXP, "exponent quantum" }, { SIG, "fraction quantum" },
        { SIGN | (1ull << 52) | 1, "3 bits quantum" }, { 0, "classical" } };
    for (unsigned p = 0; p < sizeof part / sizeof *part; p++)
        vectors(ship, nrand, 1, part[p].mask, full[ship].gates, part[p].what);
    /* all-classical A: no gate, no qubit */
    assert(reduce(ship, v[nv - 1].a, 0, v[nv - 1].s, &st) == v[nv - 1].k && st.gates == 0);

    /* The reduction's own gate count is the same for every value when A is all qubits. */
    (void)reduce(ship, 0xbd06c6cbc45dc8deull, ALL, 2, &st);
    assert(st.gates == full[ship].gates);
    printf("ry_<W>_qangle depth (reduction, N rounds of rotations, reduction undone; A, h prepared):");
#define RYF_(n, bits, ...) ry_forms(#n, bits, cqrt_ry_##n##_qangle, cqrt_ry_##n##_qangle_controlled, full[ship].gates);
    CQ_WIDTHS(RYF_) CQ_FP_WIDTHS(RYF_)
    printf("\nry_<W>_qangle[_controlled], every width: N W controlled Ry, angles fl(4pi) 2^(i-N) (negated inverted)\n");
    (void)reduce(ship, 0xc0091eb851eb851full, ALL, 1, &st);   /* P = 2pi: another constant, other foldings */
    rz_forms(st.gates);
    printf("rz_qangle[_controlled]: N rz, angles fl(2pi) 2^(i-N) (negated inverted)\n");
    invert_pair();
    agree();
    return 0;
}
