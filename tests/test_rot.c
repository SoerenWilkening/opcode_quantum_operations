/* Rotations with a classical angle (kmgy.7): the gate stream of every rotation symbol at every
 * width (the replay backend ignores angles, so no values), the emitted angles, and the host
 * k(a) against tests/ref/k_vectors.txt (tests/ref/k_ref.py). Always asserts. */
#define _POSIX_C_SOURCE 200809L
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "harness.h"
#include "cqrt.h"
#include "cq_replay.h"
#include "angle_k.h"

static const double FL_PI = 0x1.921fb54442d18p+1;

static int32_t rail_of(uint64_t v, uint32_t w) { return hx_to_rail((hx_val){ { v } }, w); }
static int32_t f64_rail(double x) { uint64_t a; memcpy(&a, &x, 8); return rail_of(a, 64); }
static void force(int32_t h) {
    cq_bit *b = cq_rail_bits(h);
    for (uint32_t i = 0; i < cq_rail_width(h); i++) cq_get_qubit(&b[i]);
}
static int all_q(int32_t h) {
    cq_bit *b = cq_rail_bits(h);
    for (uint32_t i = 0; i < cq_rail_width(h); i++) if (!cq_bit_is_q(b[i])) return 0;
    return 1;
}
static uint64_t rots(void) {
    return cq_replay_gates_of(CQ_G_RY) + cq_replay_gates_of(CQ_G_RY_CTL) + cq_replay_gates_of(CQ_G_RZ);
}
/* The rotations logged from index r0 on number n and all have angle th (bit-identical). */
static int angles_are(uint64_t r0, uint64_t n, double th) {
    if (rots() - r0 != n) return 0;
    for (uint64_t i = r0; i < r0 + n; i++) {
        double a = cq_replay_angle(i);
        if (memcmp(&a, &th, sizeof a)) return 0;
    }
    return 1;
}

typedef void (*ry_fn)(uint32_t, double);
typedef void (*ryc_fn)(uint32_t, uint32_t, double);
typedef void (*ryq_fn)(uint32_t, uint32_t);
typedef void (*ryqc_fn)(uint32_t, uint32_t, uint32_t);

/* One theta through every rotation form of width w: the uncontrolled form, the controlled one
 * with control classical 0, classical 1 and a qubit. qangle: ang < 0 means call `ry` with theta,
 * else the qangle forms with the rail ang. Expect W gates of the right kind on a forced-quantum or
 * newly forced target, none for control 0 or a non-finite angle. */
static void forms(uint32_t w, ry_fn ry, ryc_fn ryc, ryq_fn ryq, ryqc_fn ryqc, int32_t ang, double theta, double want) {
    int fin = isfinite(want);
    uint32_t base = cq_pool_live();
    for (int mode = 0; mode < 4; mode++) {   /* 0 uncontrolled, 1 c = 0, 2 c = 1, 3 c quantum */
        cq_replay_reset();
        int32_t h = hx_to_rail(hx_ones(w), w), c = rail_of(mode == 2 || mode == 3, 1);
        if (mode == 3) force(c);
        uint64_t g0 = cq_replay_gates(), x0 = cq_replay_gates_of(CQ_G_X), r0 = rots();
        if (mode == 0) { if (ang < 0) ry((uint32_t)h, theta); else ryq((uint32_t)h, (uint32_t)ang); }
        else if (ang < 0) ryc((uint32_t)c, (uint32_t)h, theta);
        else ryqc((uint32_t)c, (uint32_t)h, (uint32_t)ang);
        uint64_t n = fin && mode != 1 ? w : 0;
        /* a classical target gets its qubits here: one X per ONE bit, then W rotations */
        assert(cq_replay_gates() - g0 == 2 * n);
        assert(cq_replay_gates_of(CQ_G_X) - x0 == n);
        assert(cq_replay_gates_of(mode == 3 ? CQ_G_RY_CTL : CQ_G_RY) == n && angles_are(r0, n, want));
        assert(all_q(h) == (n != 0));
        assert(cq_pool_live() == base + (uint32_t)n + (mode == 3));
        assert(hx_eq(hx_from_rail(h), hx_ones(w)));   /* replay: Ry does nothing to the bits */
        cq_rail_free(h); cq_rail_free(c);
        assert(cq_pool_live() == base);
    }
}

static void check(uint32_t w, ry_fn ry, ryc_fn ryc, ryq_fn ryq, ryqc_fn ryqc) {
    /* ry(h, 0.0) forces every bit of h, emits W Ry(+0.0), and nothing else. */
    uint32_t base = cq_pool_live();
    cq_replay_reset();
    int32_t h = hx_to_rail(hx_of_u128(0), w);
    ry((uint32_t)h, 0.0);
    assert(all_q(h) && cq_replay_gates() == w && angles_are(0, w, 0.0));
    assert(cq_pool_live() == base + w);
    ry((uint32_t)h, -1.5);   /* already quantum: no new qubit, no X */
    assert(cq_replay_gates() == 2 * w && angles_are(w, w, -1.5) && cq_pool_live() == base + w);
    cqrt_push(7); cqrt_push((uint32_t)h);   /* a pushed word is not a control (§1.12) */
    ry((uint32_t)h, 1e300);
    assert(cq_replay_gates() == 3 * w && angles_are(2 * w, w, 1e300));
    assert(cqrt_pop() == (uint32_t)h && cqrt_pop() == 7);
    cq_rail_free(h);
    assert(cq_pool_live() == base);

    const double thetas[] = { 0.0, -0.0, 0.25, -7.0, 1e300, 5e-324, INFINITY, -INFINITY, NAN };
    for (unsigned k = 0; k < sizeof thetas / sizeof *thetas; k++)
        forms(w, ry, ryc, ryq, ryqc, -1, thetas[k], thetas[k]);

    /* qangle with a classical A: P = 4*pi, theta = fl(k * fl(4pi)) * 2^-53. */
    const struct { double x, want; } qa[] = {
        { FL_PI, FL_PI },                       /* k = 2^51: fl(4pi)/4 exactly */
        { -FL_PI, ldexp(3.0 * 0x1.921fb54442d18p+3, -2) },   /* k = 3 * 2^51 */
        { 0.0, 0.0 }, { -0.0, 0.0 },            /* k = 0: Ry(0) is still appended */
        { 4.5e15, 0 },                          /* computed below from the vector-checked k */
        { INFINITY, NAN }, { -INFINITY, NAN }, { NAN, NAN },
    };
    for (unsigned k = 0; k < sizeof qa / sizeof *qa; k++) {
        double want = qa[k].want;
        if (qa[k].x == 4.5e15) {
            uint64_t a; memcpy(&a, &qa[k].x, 8);
            want = ldexp((double)cq_angle_k(a, 2) * 0x1.921fb54442d18p+3, -53);
            assert(want >= 0 && want < 0x1.921fb54442d18p+3);
        }
        int32_t A = f64_rail(qa[k].x);
        forms(w, ry, ryc, ryq, ryqc, A, 0, want);
        assert(cq_rail_width(A) == 64 && !all_q(A));   /* A untouched, still classical */
        cq_rail_free(A);
    }
}

/* A quantum angle rail is kmgy.18's: the classical path must abort, not guess. */
static void must_abort(void (*f)(void)) {
    fflush(NULL);
    pid_t p = fork();
    assert(p >= 0);
    if (p == 0) { f(); _exit(0); }
    int st;
    assert(waitpid(p, &st, 0) == p && WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT);
}
static int32_t qA(void) { int32_t A = f64_rail(1.0); cq_get_qubit(&cq_rail_bits(A)[63]); return A; }
static void abort_ry(void) { cqrt_ry_i8_qangle((uint32_t)rail_of(0, 8), (uint32_t)qA()); }
static void abort_ryc(void) { cqrt_ry_f64_qangle_controlled((uint32_t)rail_of(1, 1), (uint32_t)rail_of(0, 64), (uint32_t)qA()); }
static void abort_rz(void) { cqrt_rz_qangle((uint32_t)qA()); }
static void abort_rzc(void) { cqrt_rz_qangle_controlled((uint32_t)rail_of(1, 1), (uint32_t)qA()); }

static void check_rz(void) {
    uint32_t base = cq_pool_live();
    /* rz_i1: P on a qubit is one rz; on a classical flag a global phase, no gate, no qubit. */
    for (int v = 0; v < 4; v++) {   /* v & 1: value, v & 2: forced */
        const double th[] = { 0.5, -0.0, INFINITY, NAN };
        for (unsigned k = 0; k < 4; k++) {
            cq_replay_reset();
            int32_t f = rail_of(v & 1, 1);
            if (v & 2) force(f);
            uint64_t g0 = cq_replay_gates();
            cqrt_rz_i1((uint32_t)f, th[k]);
            uint64_t n = (v & 2) && k < 2;
            assert(cq_replay_gates() - g0 == n && cq_replay_gates_of(CQ_G_RZ) == n && angles_are(0, n, th[k]));
            assert(hx_from_rail(f).l[0] == (uint64_t)(v & 1));
            cq_rail_free(f);
        }
    }
    /* rz_qangle(A), A classical: a global phase, nothing. _controlled: the bracket CX(c, t),
     * rz(2*pi*k/2^N) on the ancilla t, CX(c, t), only when c is a qubit and val(A) finite. */
    const struct { double x, want; } qa[] = { { FL_PI, FL_PI }, { 0.0, 0.0 }, { -2.0, 0 }, { NAN, NAN }, { -INFINITY, NAN } };
    for (unsigned k = 0; k < sizeof qa / sizeof *qa; k++) {
        double want = qa[k].want;
        if (qa[k].x == -2.0) {
            uint64_t a; memcpy(&a, &qa[k].x, 8);
            want = ldexp((double)cq_angle_k(a, 1) * 0x1.921fb54442d18p+2, -53);
        }
        for (int mode = 0; mode < 4; mode++) {   /* 0 uncontrolled, 1 c = 0, 2 c = 1, 3 c quantum */
            cq_replay_reset();
            int32_t A = f64_rail(qa[k].x), c = rail_of(mode >= 2, 1);
            if (mode == 3) force(c);
            uint64_t g0 = cq_replay_gates();
            if (mode == 0) cqrt_rz_qangle((uint32_t)A); else cqrt_rz_qangle_controlled((uint32_t)c, (uint32_t)A);
            int n = mode == 3 && isfinite(want);
            assert(cq_replay_gates() - g0 == 3u * n && cq_replay_gates_of(CQ_G_CNOT) == 2u * n);
            assert(cq_replay_gates_of(CQ_G_RZ) == (uint64_t)n && angles_are(0, n, want));
            if (n) assert(cq_replay_qubits() == 2 && cq_replay_depth() == 4);   /* c (X, CX, CX) and the ancilla */
            assert(cq_pool_live() == base + (mode == 3) && !all_q(A));
            assert(hx_from_rail(c).l[0] == (uint64_t)(mode >= 2));
            cq_rail_free(A); cq_rail_free(c);
            assert(cq_pool_live() == base);
        }
    }
}

/* Every line of tests/ref/k_vectors.txt: k(a) for both P. */
static void check_k(void) {
    FILE *f = fopen(CQ_SOURCE_DIR "/tests/ref/k_vectors.txt", "r");
    assert(f);
    char line[256], P[8];
    unsigned long long a, k;
    unsigned n = 0, N, per[2] = { 0, 0 };
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        assert(sscanf(line, "%llx %7s %u %llx", &a, P, &N, &k) == 4);
        if (N != CQ_LADDER_N) { fprintf(stderr, "k_vectors.txt is for N = %u: regenerate it\n", N); assert(0); }
        int s = !strcmp(P, "4pi") ? 2 : 1;
        assert(s == 2 || !strcmp(P, "2pi"));
        unsigned __int128 got = cq_angle_k(a, s);
        if (got != k) { fprintf(stderr, "k(%016llx, %s) = %llx, want %llx\n", a, P, (unsigned long long)got, k); assert(0); }
        per[s - 1]++; n++;
    }
    fclose(f);
    assert(per[0] == per[1] && per[0] >= 1086);   /* 1000 seeded, 32 specials, 27 witnesses, 27 negated */
    printf("k(a): %u vectors match k_ref.py\n", n);
}

#define FNS_(n, ...) cqrt_ry_##n, cqrt_ry_##n##_controlled, cqrt_ry_##n##_qangle, cqrt_ry_##n##_qangle_controlled
typedef void (*fn)(void);
#define ADDR_(n, ...) (fn)cqrt_ry_##n, (fn)cqrt_ry_##n##_controlled, (fn)cqrt_ry_##n##_qangle, (fn)cqrt_ry_##n##_qangle_controlled,
static const fn linked[] = { CQ_WIDTHS(ADDR_) CQ_FP_WIDTHS(ADDR_)
    (fn)cqrt_rz_i1, (fn)cqrt_rz_qangle, (fn)cqrt_rz_qangle_controlled };

int main(void) {
    unsigned nw = 0;
#define COUNT_(...) nw++;
    CQ_WIDTHS(COUNT_) CQ_FP_WIDTHS(COUNT_)
    assert(sizeof linked / sizeof *linked == 4 * nw + 3);
    for (unsigned k = 0; k < sizeof linked / sizeof *linked; k++) assert(linked[k]);

    check_k();
#define CHECK_(n, bits, ...) check(bits, FNS_(n)); printf("%-5s ry ry_controlled ry_qangle ry_qangle_controlled: ok\n", #n);
    CQ_WIDTHS(CHECK_) CQ_FP_WIDTHS(CHECK_)
    check_rz();
    printf("rz_i1 rz_qangle rz_qangle_controlled: ok\n");
    must_abort(abort_ry); must_abort(abort_ryc); must_abort(abort_rz); must_abort(abort_rzc);
    printf("quantum angle rail aborts (kmgy.18); %zu symbols linked\n", sizeof linked / sizeof *linked);
    return 0;
}
