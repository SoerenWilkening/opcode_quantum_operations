/* cq_invert, ABI §1.13 (kmgy.8): through the public entry points. Every family is checked with
 * the identity `X; invert{X}` = identity and `invert{X}; X` = identity (every recorded call is
 * self-inverse except a rotation), sampled in both modes, so an all-classical region emits no
 * gate and takes no qubit; then order, angles, the calls that take effect at the call, nesting.
 * Always asserts. */
#undef NDEBUG
#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "harness.h"
#include "cqrt.h"
#include "cq_template.h"
#include "cq_replay.h"

#define B cqrt_invert_begin()
#define E cqrt_invert_end()
static const hx_val zero;
static hx_val ref_zero(const hx_val *a, uint32_t w) { (void)a; (void)w; return zero; }
static hx_val ref_a0(const hx_val *a, uint32_t w) { (void)w; return a[0]; }

/* A literal of width bits from a sample value. */
#define LN(bits, v) (CQRT_UINT(bits))hx_u128(v)
#define LW(bits, v) (v).l[0], (v).l[1], (v).l[2], (v).l[3]

static uint32_t M;              /* the bus */
static uint8_t buf[64];         /* host memory the bus reads at a classical address */
static uint64_t A(void) { return (uint64_t)(uintptr_t)buf; }

/* FAM(name, X): X is one call on o and the sample's rails a[]; two ops, X then the inverted X,
 * and the inverted X then X, each expected to leave o at o0. */
#define FAM(name, X) \
    static void name##_fi(int32_t o, const int32_t *a, uint32_t w) { (void)o; (void)a; (void)w; X; B; X; E; } \
    static void name##_if(int32_t o, const int32_t *a, uint32_t w) { (void)o; (void)a; (void)w; B; X; E; X; }

#define FAMS(n, bits, L) \
FAM(copy_##n, cqrt_copy_##n(a[0], o)) \
FAM(copyc_##n, cqrt_copy_##n##_controlled(a[0], a[1], o)) \
FAM(xorc_##n, cqrt_xorc_##n(o, L(bits, hx_sample[0]))) \
FAM(xorcc_##n, cqrt_xorc_##n##_controlled(a[0], o, L(bits, hx_sample[1]))) \
FAM(and_##n, cq_template_and_##n##_unc(o, a[0], a[1])) \
FAM(andhl_##n, cq_template_and_##n##_hl_unc(o, a[0], L(bits, hx_sample[1]))) \
FAM(add_##n, cq_template_add_##n##_unc(o, a[0], a[1])) \
FAM(sublh_##n, cq_template_sub_##n##_lh_unc(o, L(bits, hx_sample[0]), a[1])) \
FAM(shl_##n, cq_template_shl_##n##_unc(o, a[0], a[1])) \
FAM(ldhl_##n, cqrt_qram_load_##n##_hl_unc(o, M, A())) \
FAM(ld_##n, { uint32_t r = cqrt_alloc_i64(A()); cqrt_qram_load_##n##_controlled_unc(o, a[0], M, r); cqrt_free(r); }) \
FAM(swhl_##n, cqrt_qram_swap_##n##_hl(M, A(), o)) \
FAM(sw_##n, { uint32_t r = cqrt_alloc_i64(A()); cqrt_qram_swap_##n(M, r, o); cqrt_free(r); }) \
/* (A swap under a quantum control leaves the cells QUANTUM: qubits the harness counts as a leak.) \
 * A forward template and load inside the region: the result handle exists at the call, holds \
 * |0> until the end, then f(args); the _unc after the end clears it. */ \
static void addfw_##n(int32_t o, const int32_t *a, uint32_t w) { \
    (void)w; B; uint32_t t = cq_template_add_##n(a[0], a[1]); \
    assert(cq_rail_width((int32_t)t) == bits && hx_eq(hx_from_rail((int32_t)t), zero)); E; \
    cq_template_add_##n##_unc(t, a[0], a[1]); cqrt_copy_##n(t, o); cqrt_free(t); } \
static void ldfw_##n(int32_t o, const int32_t *a, uint32_t w) { \
    (void)a; (void)w; B; uint32_t t = cqrt_qram_load_##n##_hl(M, A()); assert(cq_rail_width((int32_t)t) == bits); E; \
    cqrt_qram_load_##n##_hl_unc(t, M, A()); cqrt_copy_##n(t, o); cqrt_free(t); } \
/* The inverted X alone is X: o ^= f. */ \
static void copy1_##n(int32_t o, const int32_t *a, uint32_t w) { (void)w; B; cqrt_copy_##n(a[0], o); E; } \
static void check_##n(void) { \
    const struct { hx_op op; int ctl; } t[] = { \
        { { "copy " #n, 1, 0, copy_##n##_fi, copy_##n##_if, ref_zero }, 0 }, \
        { { "copy_controlled " #n, 2, 0, copyc_##n##_fi, copyc_##n##_if, ref_zero }, 1 }, \
        { { "xorc " #n, 1, 0, xorc_##n##_fi, xorc_##n##_if, ref_zero }, 0 }, \
        { { "xorc_controlled " #n, 2, 0, xorcc_##n##_fi, xorcc_##n##_if, ref_zero }, 1 }, \
        { { "and " #n, 2, 0, and_##n##_fi, and_##n##_if, ref_zero }, 0 }, \
        { { "and_hl " #n, 2, 0, andhl_##n##_fi, andhl_##n##_if, ref_zero }, 0 }, \
        { { "add " #n, 2, 0, add_##n##_fi, add_##n##_if, ref_zero }, 0 }, \
        { { "sub_lh " #n, 2, 0, sublh_##n##_fi, sublh_##n##_if, ref_zero }, 0 }, \
        { { "shl " #n, 2, 0, shl_##n##_fi, shl_##n##_if, ref_zero }, 0 }, \
        { { "load_hl_unc " #n, 0, 0, ldhl_##n##_fi, ldhl_##n##_if, ref_zero }, 0 }, \
        { { "load_controlled_unc " #n, 1, 0, ld_##n##_fi, ld_##n##_if, ref_zero }, 1 }, \
        { { "swap_hl " #n, 0, 0, swhl_##n##_fi, swhl_##n##_if, ref_zero }, 0 }, \
        { { "swap " #n, 0, 0, sw_##n##_fi, sw_##n##_if, ref_zero }, 0 }, \
        { { "add forward " #n, 2, 0, addfw_##n, NULL, ref_zero }, 0 }, \
        { { "load_hl forward " #n, 0, 0, ldfw_##n, NULL, ref_zero }, 0 }, \
        { { "copy inverted " #n, 1, 0, copy1_##n, NULL, ref_a0 }, 0 }, \
    }; \
    for (unsigned k = 0; k < sizeof t / sizeof *t; k++) \
        assert(hx_run_w(&t[k].op, bits, t[k].ctl ? (const uint32_t[]){ 1, 0 } : NULL, 0, NULL) == HX_OK); \
    printf("%-5s copy xorc [_controlled], and, and_hl, add, sub_lh, shl, load_unc, swap, forward add/load: ok\n", #n); \
}
FAMS(i1, 1, LN)
FAMS(i7, 7, LN)
FAMS(i64, 64, LN)
FAMS(i200, 200, LW)

/* The i1 logic, a cast and bswap. */
FAM(not, cqrt_not_i1(o))
FAM(and1, cqrt_and_i1(a[0], a[1], o))
FAM(or1, cqrt_or_i1(a[0], a[1], o))
FAM(xor1, cqrt_xor_i1(a[0], o))
FAM(zext, cq_template_zext_i7_to_i64_unc(o, a[0]))
FAM(sext, cq_template_sext_i1_to_i200_unc(o, a[0]))
FAM(trunc, cq_template_trunc_i200_to_i7_unc(o, a[0]))
FAM(bswap16, cq_template_bswap_i16_unc(o, a[0]))
FAM(bswap64, cq_template_bswap_i64_unc(o, a[0]))

static void check_misc(void) {
    const struct { hx_op op; uint32_t w, aw; } t[] = {
        { { "not_i1", 0, 0, not_fi, not_if, ref_zero }, 1, 0 },
        { { "and_i1", 2, 0, and1_fi, and1_if, ref_zero }, 1, 0 },
        { { "or_i1", 2, 0, or1_fi, or1_if, ref_zero }, 1, 0 },
        { { "xor_i1", 1, 0, xor1_fi, xor1_if, ref_zero }, 1, 0 },
        { { "zext_i7_to_i64", 1, 64, zext_fi, zext_if, ref_zero }, 7, 0 },
        { { "sext_i1_to_i200", 1, 200, sext_fi, sext_if, ref_zero }, 1, 0 },
        { { "trunc_i200_to_i7", 1, 7, trunc_fi, trunc_if, ref_zero }, 200, 0 },
        { { "bswap_i16", 1, 0, bswap16_fi, bswap16_if, ref_zero }, 16, 0 },
        { { "bswap_i64", 1, 0, bswap64_fi, bswap64_if, ref_zero }, 64, 0 },
    };
    for (unsigned k = 0; k < sizeof t / sizeof *t; k++) assert(hx_run(&t[k].op, t[k].w, 0, NULL) == HX_OK);
    printf("i1 logic, zext/sext/trunc, bswap i16/i64: ok\n");
}

/* Two calls that do not commute: b ^= a, then c ^= b. Inverted, c ^= b runs first, on b's value
 * before the region; in the recorded order c would see b ^ a. a = 1, b = c = 0. */
static void check_order(void) {
    for (int q = 0; q < 2; q++) {
        cq_replay_reset();
        uint32_t base = cq_pool_live(), a = cqrt_alloc_i8(1), b = cqrt_alloc_i8(0), c = cqrt_alloc_i8(0);
        if (q) cqrt_ry_i8(a, 0.0), cqrt_ry_i8(b, 0.0), cqrt_ry_i8(c, 0.0);   /* force qubits */
        B; cqrt_copy_i8(a, b); cqrt_copy_i8(b, c); E;
        assert(cqrt_measure_i8(b) == 1 && cqrt_measure_i8(c) == 0);
        /* forward, then the same calls inverted: everything back */
        cqrt_copy_i8(a, b); cqrt_copy_i8(b, c);
        B; cqrt_copy_i8(a, b); cqrt_copy_i8(b, c); E;
        assert(cqrt_measure_i8(a) == 1 && cqrt_measure_i8(b) == 1 && cqrt_measure_i8(c) == 0);
        cqrt_free(a); cqrt_free(b); cqrt_free(c);
        assert(cq_pool_live() == base);
        if (!q) assert(cq_replay_gates() == 0);
    }
    printf("order: ok\n");
}

/* Rotations: replayed reversed with negated angles; nesting flips them back. */
static uint64_t nrot(void) {
    return cq_replay_gates_of(CQ_G_RY) + cq_replay_gates_of(CQ_G_RY_CTL) + cq_replay_gates_of(CQ_G_RZ);
}
static void expect_angles(const double *want, uint64_t n) {
    assert(nrot() == n);
    for (uint64_t i = 0; i < n; i++)
        if (cq_replay_angle(i) != want[i]) {
            fprintf(stderr, "angle %llu: got %g want %g\n", (unsigned long long)i, cq_replay_angle(i), want[i]);
            abort();
        }
}
static double qangle_theta(uint32_t A) {   /* the angle ry_qangle applies, read off the replay log */
    cq_replay_reset();
    uint32_t h = cqrt_alloc_i1(0);
    cqrt_ry_i1_qangle(h, A);
    double th = cq_replay_angle(0);
    cqrt_free(h);
    return th;
}
static void check_rotations(void) {
    double x = 0.75;
    uint64_t xb;
    memcpy(&xb, &x, 8);
    uint32_t Ah = cqrt_alloc_i64(xb), c = cqrt_alloc_i1(1), h = cqrt_alloc_i7(0), f = cqrt_alloc_i1(0);
    double tq = qangle_theta(Ah);
    assert(tq != 0 && isfinite(tq));
    cqrt_ry_i1(c, 0.0); cqrt_ry_i1(f, 0.0);   /* quantum control and phase target */

    cq_replay_reset();
    B;
    cqrt_ry_i7(h, 0.1);
    cqrt_ry_i7_controlled(c, h, 0.2);
    cqrt_ry_i7_qangle(h, Ah);
    cqrt_ry_i7_qangle_controlled(c, h, Ah);
    cqrt_rz_i1(f, 0.3);
    assert(nrot() == 0);   /* recorded, not emitted */
    E;
    double want[4 * 7 + 1], *p = want;
    *p++ = -0.3;
    for (int i = 0; i < 7; i++) *p++ = -tq;
    for (int i = 0; i < 7; i++) *p++ = -tq;
    for (int i = 0; i < 7; i++) *p++ = -0.2;
    for (int i = 0; i < 7; i++) *p++ = -0.1;
    expect_angles(want, 29);

    /* rz_qangle_controlled: §1.7's bracket, P negated. rz_qangle of a classical A: no gate. */
    cq_replay_reset();
    B; cqrt_rz_qangle(Ah); cqrt_rz_qangle_controlled(c, Ah); E;
    assert(nrot() == 1 && cq_replay_angle(0) < 0);
    double pz = -cq_replay_angle(0);
    cq_replay_reset();
    cqrt_rz_qangle_controlled(c, Ah);
    assert(nrot() == 1 && cq_replay_angle(0) == pz);

    /* Nesting: invert{ a; invert{ b }; c } emits -c, b, -a. An inner region after a flush point:
     * invert{ a; invert{ b; measure; c }; d } emits a, b forward, then -d, c. Empty regions: nothing. */
    uint32_t g = cqrt_alloc_i1(0);
    cq_replay_reset();
    B; cqrt_ry_i1(g, 1); B; cqrt_ry_i1(g, 2); E; cqrt_ry_i1(g, 3); E;
    expect_angles((const double[]){ -3, 2, -1 }, 3);
    cq_replay_reset();
    B; cqrt_ry_i1(g, 1); B; cqrt_ry_i1(g, 2); (void)cqrt_measure_i1(f); cqrt_ry_i1(g, 3); E; cqrt_ry_i1(g, 4); E;
    expect_angles((const double[]){ 1, 2, -4, 3 }, 4);
    cq_replay_reset();
    B; E; B; B; E; E;
    B; B; B; cqrt_ry_i1(g, 5); E; E; E;   /* three inversions */
    expect_angles((const double[]){ -5 }, 1);
    assert(cq_replay_gates() == 1);

    /* op; invert{op} on a rotation: θ then -θ. */
    cq_replay_reset();
    cqrt_ry_i1_controlled(c, g, 0.5); B; cqrt_ry_i1_controlled(c, g, 0.5); E;
    expect_angles((const double[]){ 0.5, -0.5 }, 2);
    cqrt_free(Ah); cqrt_free(c); cqrt_free(h); cqrt_free(f); cqrt_free(g);
    printf("rotations: reversed and negated, nesting, empty regions: ok\n");
}

/* ---- What takes effect at the call. */
int inv_seen = -1;
void inv_cb(int x) { inv_seen = x; }   /* an effect entry's host callee, found by dlsym */

static void check_at_call(void) {
    for (int q = 0; q < 2; q++) {
        cq_replay_reset();
        uint32_t base = cq_pool_live();
        /* alloc: a usable rail at the call */
        uint32_t o = cqrt_alloc_i8(0);
        if (q) cqrt_ry_i8(o, 0.0);
        B; uint32_t a = cqrt_alloc_i8(9); cqrt_copy_i8(a, o); E;
        assert(cqrt_measure_i8(o) == 9);
        /* measure: the outcome at the call, after the gates before it ran forward; only the
         * calls after it are reversed */
        B; cqrt_xorc_i8(o, 5); assert(cqrt_measure_i8(o) == (9 ^ 5)); cqrt_xorc_i8(o, 3); cqrt_xorc_i8(o, 3); cqrt_xorc_i8(o, 64); E;
        assert(cqrt_measure_i8(o) == (9 ^ 5 ^ 64));
        /* bus store: performed at the call, the recorded xorc before it first */
        memset(buf, 0, sizeof buf);
        B; cqrt_xorc_i8(a, 0x30); cqrt_qram_store_i8_hl(M, A(), a); assert(buf[0] == (9 ^ 0x30)); E;
        assert(buf[0] == (9 ^ 0x30));
        /* tape: the write's XOR gate is recorded, the effect entry is performed at the call and
         * flushes it */
        uint32_t t = cqrt_tape_alloc(), one = cqrt_alloc_i1(1);
        inv_seen = -1;
        B; cqrt_xorc_i8(a, 0x40); (void)cqrt_tape_write_i8(t, a); cqrt_tape_effect_controlled(one, t, "inv_cb", 1);
        assert(inv_seen == (9 ^ 0x30 ^ 0x40)); E;
        /* free: held until after the reversed list, which still uses the rail */
        B; uint32_t s = cqrt_alloc_i8(0); cqrt_copy_i8(o, s); cqrt_copy_i8(o, s); cqrt_free(s);
        assert(cq_rail_width((int32_t)s) == 8); E;
        /* push/pop: at the call */
        B; cqrt_push(7); assert(cqrt_pop() == 7); E;
        cqrt_free(o); cqrt_free(a); cqrt_free(one);
        assert(cq_pool_live() == base);
        if (!q) assert(cq_replay_gates() == 0 && cq_replay_qubits() == 0);
    }
    printf("alloc, measure, store, tape effect, free, push/pop at the call: ok\n");
}

/* An abort flag is bookkeeping at the call: a classical 1 ends the shot there. And `end`
 * without `begin` aborts. Each in a forked child. */
static int child(void (*f)(void)) {
    fflush(NULL);
    pid_t p = fork();
    assert(p >= 0);
    if (!p) {
        int fd = open("/dev/null", O_WRONLY); if (fd >= 0) dup2(fd, 2);
        f(); _exit(0);
    }
    int st;
    assert(waitpid(p, &st, 0) == p);
    return st;
}
static void end_alone(void) { cqrt_invert_end(); }
static void end_twice(void) { B; E; E; }
static void abort_in_region(void) { B; cqrt_abort_i1(cqrt_alloc_i1(1)); _exit(3); }
static void abort0_in_region(void) { B; cqrt_abort_i1(cqrt_alloc_i1(0)); E; _exit(4); }

int main(void) {
    M = cqrt_qram_alloc_i8(0);
    for (unsigned i = 0; i < sizeof buf; i++) buf[i] = (uint8_t)(i * 37 + 11);
    check_i1(); check_i7(); check_i64();
    hx_max_samples = 100;   /* i200: the 1000-sample run of the templates alone takes ~13 s */
    check_i200();
    hx_max_samples = 0;
    check_misc();
    check_order();
    check_rotations();
    check_at_call();
    int st = child(end_alone);
    assert(WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT);
    st = child(end_twice);
    assert(WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT);
    st = child(abort_in_region);
    assert(WIFEXITED(st) && WEXITSTATUS(st) == EXIT_FAILURE);
    st = child(abort0_in_region);
    assert(WIFEXITED(st) && WEXITSTATUS(st) == 4);
    printf("end without begin aborts; abort_i1 at the call: ok\n");
    return 0;
}
