/* Tape and abort §1.11 (kmgy.22). Each scenario runs in a forked child whose stdout and stderr
 * go to a pipe, so the real end of the shot (process exit, abort_i1) and the real host calls
 * are what is tested. The callees below are found by name (dlsym): they are not static. */
#undef NDEBUG
#include <assert.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cqrt.h"
#include "cq_replay.h"
#include "harness.h"

/* ---- Host callees */
int rec_i(int a) { printf("rec(%d)\n", a); return 2 * a; }
double mix(int a, double b, long long c, float d) { printf("mix(%d,%g,%lld,%g)\n", a, b, c, (double)d); return a + b + (double)c + d; }
long sum10(long a, long b, long c, long d, long e, long f, long g, long h, long i, long j) {
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i + 10 * j;
}
double dsum10(double a, double b, double c, double d, double e, double f, double g, double h, double i, double j) {
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i + 10 * j;
}
long double ld_twice(int k, long double x) { return k * x; }
int sx8(signed char x) { return x; }   /* a narrow signed parameter: the caller sign-extends */

#define ECHO_N(n, bits) CQRT_UINT(bits) echo_##n(CQRT_UINT(bits) x) { return x; }
#define ECHO_F(n, bits, T) T echo_##n(T x) { return x; }
CQ_WIDTHS_NARROW(ECHO_N)
CQ_FP_WIDTHS(ECHO_F)
unsigned _BitInt(200) echo_i200(unsigned _BitInt(200) x) { return x; }

/* ---- Child runner: status as waitpid gives it, output into out. */
static char out[1 << 16];
static int run(void (*scenario)(void)) {
    int p[2];
    assert(pipe(p) == 0);
    fflush(NULL);
    pid_t pid = fork();
    assert(pid >= 0);
    if (!pid) {
        close(p[0]); dup2(p[1], 1); dup2(p[1], 2); close(p[1]);
        scenario();
        exit(0);
    }
    close(p[1]);
    size_t n = 0;
    ssize_t k;
    while ((k = read(p[0], out + n, sizeof out - 1 - n)) > 0) n += (size_t)k;
    out[n] = 0;
    close(p[0]);
    int st;
    assert(waitpid(pid, &st, 0) == pid);
    return st;
}
static void expect(void (*s)(void), const char *name, int code, const char *want) {
    int st = run(s);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != code || strcmp(out, want)) {
        fprintf(stderr, "%s: status %#x, output:\n%s\n--- wanted exit %d and:\n%s\n", name, st, out, code, want);
        abort();
    }
    printf("%s: ok\n", name);
}
static void expect_abort(void (*s)(void), const char *name, const char *msg) {
    int st = run(s);
    if (!WIFSIGNALED(st) || WTERMSIG(st) != SIGABRT || !strstr(out, msg)) {
        fprintf(stderr, "%s: status %#x, output:\n%s\n--- wanted SIGABRT and '%s'\n", name, st, out, msg);
        abort();
    }
    printf("%s: ok\n", name);
}

static uint32_t qbit(int v) {   /* an i1 rail forced to a qubit holding v */
    uint32_t h = cqrt_alloc_i1(v);
    cq_get_qubit(cq_rail_bits((int32_t)h));
    return h;
}
static uint64_t ptr(const void *p) { return (uint64_t)(uintptr_t)p; }

/* ---- Classical rows: each performed once, at its call, in order; control 0 skips; no gate. */
static void s_classical(void) {
    cq_replay_reset();
    uint32_t t = cqrt_tape_alloc(), one = cqrt_alloc_i1(1), zero = cqrt_alloc_i1(0);
    cqrt_tape_fmt(t, "A%d|%s\n");
    cqrt_tape_write_i32_lit(t, 7);
    printf("between fields\n");   /* the entry completes at its last field, below */
    cqrt_tape_write_i64_lit(t, ptr("str"));
    printf("m1\n");
    cqrt_tape_write_i32_lit(t, 5);
    uint32_t r = cqrt_tape_effect_i32_controlled(one, t, "rec_i", 1);
    assert(cqrt_measure_i32(r) == 10);
    printf("m2\n");
    cqrt_tape_fmt_controlled(zero, t, "never\n");
    cqrt_tape_write_i32_lit(t, 1);
    assert(cqrt_measure_i32(cqrt_tape_effect_i32_controlled(zero, t, "rec_i", 1)) == 0);
    cqrt_tape_effect_controlled(zero, t, "rec_i", 1);
    cqrt_tape_fmt_controlled(one, t, "B\n");
    cqrt_tape_write_i32_lit(t, 3); cqrt_tape_write_f64_lit(t, 2.5);
    cqrt_tape_write_i64_lit(t, (uint64_t)-4); cqrt_tape_write_f32_lit(t, 1.5f);
    assert(cqrt_measure_f64(cqrt_tape_effect_f64_controlled(one, t, "mix", 4)) == 3.0);
    for (int k = 1; k <= 10; k++) cqrt_tape_write_i64_lit(t, (uint64_t)k);
    assert(cqrt_measure_i64(cqrt_tape_effect_i64_controlled(one, t, "sum10", 10)) == 385);
    for (int k = 1; k <= 10; k++) cqrt_tape_write_f64_lit(t, k);
    assert(cqrt_measure_f64(cqrt_tape_effect_f64_controlled(one, t, "dsum10", 10)) == 385.0);
    cqrt_tape_write_i32_lit(t, 3); cqrt_tape_write_f80_lit(t, 1.25L);
    assert(cqrt_measure_f80(cqrt_tape_effect_f80_controlled(one, t, "ld_twice", 2)) == 3.75L);
    cqrt_tape_write_i8_lit(t, 0xfe);
    assert(cqrt_measure_i32(cqrt_tape_effect_i32_controlled(one, t, "sx8", 1)) == -2);
    cqrt_tape_write_i64_lit(t, ptr((void *)rec_i)); cqrt_tape_write_i32_lit(t, 9);   /* null name */
    assert(cqrt_measure_i32(cqrt_tape_effect_i32_controlled(one, t, NULL, 2)) == 18);
    cqrt_tape_fmt(t, "%s %5.2f %c%%\n");
    cqrt_tape_write_i64_lit(t, ptr("fmt")); cqrt_tape_write_f64_lit(t, 3.14159); cqrt_tape_write_i32_lit(t, 'z');
    uint32_t h = cqrt_alloc_i32(42);   /* a classical rail field */
    cqrt_tape_fmt(t, "rail %d\n");
    cqrt_tape_write_i32(t, h);
    cqrt_abort_i1(zero);   /* classical 0: returns, nothing happens */
    assert(cq_replay_gates() == 0);
    printf("end\n");
}

/* ---- Every width: a field written as a literal, a classical rail, and under control 0 and 1,
 * passed to echo_<W> and returned in r. */
static hx_val pat(uint32_t bits) {
    hx_val v;
    for (unsigned k = 0; k < CQ_LIMBS; k++) v.l[k] = 0xa5c3f00f12345678ull * (k + 1);
    return hx_mask(v, bits);
}
#define LIT_N(bits, v) ((CQRT_UINT(bits))hx_u128(v))
#define LIT_W(bits, v) (v).l[0], (v).l[1], (v).l[2], (v).l[3]
#define ECHO_CHECK(n, bits, LIT) { \
    hx_val p = pat(bits), z = { { 0 } }; \
    cqrt_tape_write_##n##_lit(t, LIT(bits, p)); \
    assert(hx_eq(hx_from_rail((int32_t)cqrt_tape_effect_##n##_controlled(one, t, "echo_" #n, 1)), p)); \
    uint32_t h = (uint32_t)hx_to_rail(p, bits); \
    cqrt_tape_write_##n(t, h); \
    assert(hx_eq(hx_from_rail((int32_t)cqrt_tape_effect_##n##_controlled(one, t, "echo_" #n, 1)), p)); \
    cqrt_tape_write_##n##_controlled(one, t, h); \
    assert(hx_eq(hx_from_rail((int32_t)cqrt_tape_effect_##n##_controlled(one, t, "echo_" #n, 1)), p)); \
    cqrt_tape_write_##n##_controlled(zero, t, h); \
    assert(hx_eq(hx_from_rail((int32_t)cqrt_tape_effect_##n##_controlled(one, t, "echo_" #n, 1)), z)); \
    assert(hx_eq(hx_from_rail((int32_t)h), p)); }
#define ECHO_NARROW(n, bits) ECHO_CHECK(n, bits, LIT_N)
#define ECHO_WIDE(n, bits) ECHO_CHECK(n, bits, LIT_W)
#define ECHO_FP(n, bits, T) { \
    T x = (T)1.5, y; uint32_t h = cqrt_alloc_##n(x); \
    cqrt_tape_write_##n##_lit(t, x); \
    y = cqrt_measure_##n(cqrt_tape_effect_##n##_controlled(one, t, "echo_" #n, 1)); assert(!memcmp(&x, &y, (bits) / 8)); \
    cqrt_tape_write_##n(t, h); \
    y = cqrt_measure_##n(cqrt_tape_effect_##n##_controlled(one, t, "echo_" #n, 1)); assert(!memcmp(&x, &y, (bits) / 8)); \
    cqrt_tape_write_##n##_controlled(one, t, h); \
    y = cqrt_measure_##n(cqrt_tape_effect_##n##_controlled(one, t, "echo_" #n, 1)); assert(!memcmp(&x, &y, (bits) / 8)); \
    cqrt_tape_write_##n##_controlled(zero, t, h); \
    y = cqrt_measure_##n(cqrt_tape_effect_##n##_controlled(one, t, "echo_" #n, 1)); assert(y == 0); }
static void s_widths(void) {
    cq_replay_reset();
    uint32_t t = cqrt_tape_alloc(), one = cqrt_alloc_i1(1), zero = cqrt_alloc_i1(0);
    CQ_WIDTHS_NARROW(ECHO_NARROW)
    CQ_WIDTHS_WIDE(ECHO_WIDE)
    CQ_FP_WIDTHS(ECHO_FP)
    assert(cq_replay_gates() == 0 && cq_pool_live() == 0);
    printf("widths\n");
}

/* ---- Deferred output: behind a quantum predicate everything but INPUT waits for the end of the
 * shot, in order; buffers are captured at the call. */
static void vdefer(uint32_t t, uint32_t c, const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    cqrt_tape_write_i64_lit(t, ptr(f)); cqrt_tape_write_i64_lit(t, ptr(ap));
    cqrt_tape_effect_controlled(c, t, "vprintf", 2);
    va_end(ap);
}
static void s_deferred(void) {
    uint32_t t = cqrt_tape_alloc(), one = cqrt_alloc_i1(1);
    cqrt_tape_fmt_controlled(qbit(1), t, "D1\n");
    cqrt_tape_fmt(t, "D2 %d\n"); cqrt_tape_write_i32_lit(t, 4);
    cqrt_tape_fmt_controlled(qbit(0), t, "never\n");
    uint32_t h = cqrt_alloc_i32(77);
    cq_get_qubit(&cq_rail_bits((int32_t)h)[0]);
    cqrt_tape_fmt(t, "D3 %d\n"); cqrt_tape_write_i32(t, h);
    char buf[] = "orig", b2[] = "xyz\n", s3[] = "vs";
    cqrt_tape_fmt(t, "D4 %s\n"); cqrt_tape_write_i64_lit(t, ptr(buf));
    cqrt_tape_write_i32_lit(t, 6);
    assert(cqrt_measure_i32(cqrt_tape_effect_i32_controlled(one, t, "rec_i", 1)) == 0);   /* deferred: |0> */
    cqrt_tape_write_i64_lit(t, ptr(b2)); cqrt_tape_write_i64_lit(t, 1); cqrt_tape_write_i64_lit(t, 4);
    cqrt_tape_write_i64_lit(t, ptr(stdout));
    cqrt_tape_effect_controlled(one, t, "fwrite", 4);
    vdefer(t, one, "V %s %d %g\n", s3, 5, 2.5);
    strcpy(buf, "chgd"); strcpy(b2, "XYZ\n"); strcpy(s3, "VS");
    FILE *in = tmpfile();   /* INPUT behind deferred output: performed at the call */
    fputs("Q", in); rewind(in);
    cqrt_tape_write_i64_lit(t, ptr(in));
    assert(cqrt_measure_i32(cqrt_tape_effect_i32_controlled(one, t, "fgetc", 1)) == 'Q');
    assert(cqrt_measure_i32(cqrt_tape_effect_i32_controlled(cqrt_alloc_i1(0), t, "fgetc", 1)) == 0);
    printf("live\n");
}

/* ---- Abort */
static void s_abort_deferred_exit(void) {   /* the noreturn entry is performed by the abort */
    uint32_t t = cqrt_tape_alloc(), one = cqrt_alloc_i1(1);
    cqrt_tape_fmt_controlled(qbit(1), t, "D\n");
    cqrt_tape_fmt(t, "B\n");
    cqrt_tape_write_i32_lit(t, 3);
    cqrt_tape_effect_controlled(one, t, "exit", 1);
    printf("live\n");
    cqrt_abort_i1(one);
    printf("unreached\n");
}
static void s_abort_flag(int v) {   /* a quantum flag: reads v at the end of the shot */
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_fmt(t, "before\n");
    cqrt_abort_i1(qbit(v));
    cqrt_tape_fmt(t, "after\n");
    printf("live\n");
}
static void s_abort_flag1(void) { s_abort_flag(1); }
static void s_abort_flag0(void) { s_abort_flag(0); }
static void s_abort_bare(void) { printf("x\n"); cqrt_abort_i1(cqrt_alloc_i1(1)); printf("unreached\n"); }
static void s_exit_at_end(void) {   /* a deferred exit performed at process exit sets the status */
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_fmt_controlled(qbit(1), t, "D\n");
    cqrt_tape_write_i32_lit(t, 4);
    cqrt_tape_effect_controlled(cqrt_alloc_i1(1), t, "exit", 1);
    printf("live\n");
}
static void s_trap(void) {
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_effect_controlled(cqrt_alloc_i1(1), t, "__builtin_trap", 0);
}

/* ---- INPUT rows that merge under a quantum predicate or field: kmgy.28 */
static uint32_t q64(uint64_t v) { uint32_t h = cqrt_alloc_i64(v); cq_get_qubit(cq_rail_bits((int32_t)h)); return h; }
static void s_in_pred(void) {
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_write_i64_lit(t, ptr(stdin));
    cqrt_tape_effect_i32_controlled(qbit(1), t, "fgetc", 1);
}
static void s_in_addr(void) {
    char b[8];
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_write_i64(t, q64(ptr(b))); cqrt_tape_write_i32_lit(t, 8); cqrt_tape_write_i64_lit(t, ptr(stdin));
    cqrt_tape_effect_i64_controlled(cqrt_alloc_i1(1), t, "fgets", 3);
}
static void s_in_len(void) {
    char b[8];
    uint32_t t = cqrt_tape_alloc(), n = cqrt_alloc_i32(8);
    cq_get_qubit(cq_rail_bits((int32_t)n));
    cqrt_tape_write_i64_lit(t, ptr(b)); cqrt_tape_write_i32(t, n); cqrt_tape_write_i64_lit(t, ptr(stdin));
    cqrt_tape_effect_i64_controlled(cqrt_alloc_i1(1), t, "fgets", 3);
}
static void s_in_stream(void) {
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_write_i64(t, q64(ptr(stdin)));
    cqrt_tape_effect_i32_controlled(cqrt_alloc_i1(1), t, "getc_unlocked", 1);
}
static void s_out_qaddr(void) {   /* a deferred buffer at an address rail: kmgy.30 */
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_write_i64(t, q64(ptr("s")));
    cqrt_tape_effect_controlled(cqrt_alloc_i1(1), t, "puts", 1);
}

/* ---- tape_write through the sampled harness: h' ^= h (controlled: c ? h : 0), h unchanged.
 * h' is never freed by the ABI; the test frees it once copied out so the leak check holds. */
static uint32_t T;
static hx_val ref_a0(const hx_val *a, uint32_t w) { (void)w; return a[0]; }
static hx_val ref_ctl(const hx_val *a, uint32_t w) { (void)w; return a[0].l[0] ? a[1] : (hx_val){ { 0 } }; }
#define HX_TAPE(n, bits, ...) \
    static void w_##n(int32_t o, const int32_t *a, uint32_t w) { \
        (void)w; uint32_t r = cqrt_tape_write_##n(T, (uint32_t)a[0]); cqrt_copy_##n(r, (uint32_t)o); cqrt_free(r); } \
    static void wc_##n(int32_t o, const int32_t *a, uint32_t w) { \
        (void)w; uint32_t r = cqrt_tape_write_##n##_controlled((uint32_t)a[0], T, (uint32_t)a[1]); \
        cqrt_copy_##n(r, (uint32_t)o); cqrt_free(r); } \
    static void hx_##n(void) { \
        assert(hx_run(&(hx_op){ "tape_write_" #n, 1, 0, w_##n, NULL, ref_a0 }, bits, 0, NULL) == HX_OK); \
        assert(hx_run_w(&(hx_op){ "tape_write_" #n "_controlled", 2, 0, wc_##n, NULL, ref_ctl }, bits, \
                        (const uint32_t[]){ 1, 0 }, 0, NULL) == HX_OK); }
CQ_WIDTHS(HX_TAPE)
CQ_FP_WIDTHS(HX_TAPE)

typedef void (*fn)(void);
#define ADDR_(n, ...) (fn)cqrt_tape_effect_##n##_controlled, (fn)cqrt_tape_write_##n, \
    (fn)cqrt_tape_write_##n##_controlled, (fn)cqrt_tape_write_##n##_lit,
static const fn linked[] = { CQ_WIDTHS(ADDR_) CQ_FP_WIDTHS(ADDR_)
    (fn)cqrt_tape_alloc, (fn)cqrt_tape_fmt, (fn)cqrt_tape_fmt_controlled, (fn)cqrt_tape_effect_controlled,
    (fn)cqrt_abort_i1 };

int main(void) {
    assert(sizeof linked / sizeof *linked == 4 * 20 + 5);
    for (unsigned k = 0; k < sizeof linked / sizeof *linked; k++) assert(linked[k]);

    expect(s_classical, "classical rows", 0,
           "between fields\nA7|str\nm1\nrec(5)\nm2\nB\nmix(3,2.5,-4,1.5)\nrec(9)\nfmt  3.14 z%\nrail 42\nend\n");
    expect(s_widths, "every width", 0, "widths\n");
    expect(s_deferred, "deferred output", 0, "live\nD1\nD2 4\nD3 77\nD4 orig\nrec(6)\nxyz\nV vs 5 2.5\n");
    expect(s_abort_deferred_exit, "abort: deferred exit entry", 3, "live\nD\nB\n");
    expect(s_abort_flag1, "abort: quantum flag reading 1", 0, "before\nlive\n");
    expect(s_abort_flag0, "abort: quantum flag reading 0", 0, "before\nlive\nafter\n");
    expect(s_abort_bare, "abort: classical 1, no noreturn entry", EXIT_FAILURE, "x\n");
    expect(s_exit_at_end, "deferred exit at process exit", 4, "live\nD\n");
    int st = run(s_trap);
    assert(WIFSIGNALED(st) && (WTERMSIG(st) == SIGILL || WTERMSIG(st) == SIGTRAP));
    printf("__builtin_trap: ok\n");
    expect_abort(s_in_pred, "INPUT, quantum predicate", "kmgy.28");
    expect_abort(s_in_addr, "INPUT, quantum buffer address", "kmgy.28");
    expect_abort(s_in_len, "INPUT, quantum length", "kmgy.28");
    expect_abort(s_in_stream, "INPUT, quantum stream", "kmgy.28");
    expect_abort(s_out_qaddr, "deferred buffer at an address rail", "kmgy.30");

    T = cqrt_tape_alloc();
#define HX_RUN(n, ...) hx_##n();
    CQ_WIDTHS(HX_RUN) CQ_FP_WIDTHS(HX_RUN)
    printf("tape_write[_controlled] sampled at every width: ok; %zu symbols linked\n", sizeof linked / sizeof *linked);
    return 0;
}
