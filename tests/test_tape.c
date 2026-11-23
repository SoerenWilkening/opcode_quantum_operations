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
#include "cq_bus.h"
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

/* ---- Child runner: status as waitpid gives it, output into out (outn bytes). The child's stdin
 * is a pipe holding in_text, or the parent's when in_text is NULL. */
static char out[1 << 16];
static size_t outn;
static const char *in_text;
static int run(void (*scenario)(void)) {
    int p[2], ip[2];
    assert(pipe(p) == 0);
    if (in_text) {
        assert(pipe(ip) == 0);
        assert(write(ip[1], in_text, strlen(in_text)) == (ssize_t)strlen(in_text));
        close(ip[1]);
    }
    fflush(NULL);
    pid_t pid = fork();
    assert(pid >= 0);
    if (!pid) {
        close(p[0]); dup2(p[1], 1); dup2(p[1], 2); close(p[1]);
        if (in_text) { dup2(ip[0], 0); close(ip[0]); }
        setvbuf(stdout, NULL, _IONBF, 0);   /* write(1, ...) entries and stdio interleave in order */
        scenario();
        exit(0);
    }
    close(p[1]);
    if (in_text) close(ip[0]);
    size_t n = 0;
    ssize_t k;
    while ((k = read(p[0], out + n, sizeof out - 1 - n)) > 0) n += (size_t)k;
    out[outn = n] = 0;
    close(p[0]);
    int st;
    assert(waitpid(pid, &st, 0) == pid);
    return st;
}
static void expect_n(void (*s)(void), const char *name, int code, const char *want, size_t wn) {
    int st = run(s);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != code || outn != wn || memcmp(out, want, wn)) {
        fprintf(stderr, "%s: status %#x, output:\n%s\n--- wanted exit %d and:\n%s\n", name, st, out, code, want);
        abort();
    }
    printf("%s: ok\n", name);
}
static void expect(void (*s)(void), const char *name, int code, const char *want) { expect_n(s, name, code, want, strlen(want)); }
static void expect_abort(void (*s)(void), const char *name, const char *msg) {
    int st = run(s);
    if (!WIFSIGNALED(st) || WTERMSIG(st) != SIGABRT || !strstr(out, msg)) {
        fprintf(stderr, "%s: status %#x, output:\n%s\n--- wanted SIGABRT and '%s'\n", name, st, out, msg);
        abort();
    }
    printf("%s: ok\n", name);
}

#define LIM ": not implemented, a backend limit ABI §1.11 Limits permits"   /* every Limits message */

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

/* ---- Helpers for the bus-facing scenarios: values read without measuring (the replay backend
 * holds a basis state), and the ancilla checks. */
static uint64_t q64(uint64_t v) { uint32_t h = cqrt_alloc_i64(v); cq_get_qubit(cq_rail_bits((int32_t)h)); return h; }
static uint32_t qmask64(uint64_t v, uint64_t mask) {   /* a rail holding v, the bits of mask as qubits */
    uint32_t h = cqrt_alloc_i64(v);
    for (int i = 0; i < 64; i++) if (mask >> i & 1) cq_get_qubit(&cq_rail_bits((int32_t)h)[i]);
    return h;
}
static uint32_t q32(uint32_t v) { uint32_t h = cqrt_alloc_i32(v); cq_get_qubit(cq_rail_bits((int32_t)h)); return h; }
static int bitv(cq_bit b) { return cq_bit_is_q(b) ? cq_replay_get_bit(b.q) : b.kind; }
static uint64_t peekr(uint32_t h) {
    uint64_t v = 0;
    for (uint32_t i = 0; i < cq_rail_width((int32_t)h) && i < 64; i++) v |= (uint64_t)bitv(cq_rail_bits((int32_t)h)[i]) << i;
    return v;
}
static uint8_t peekc(uint64_t a) {
    cq_bit c[8];
    cq_cell_read(a, c);
    uint8_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint8_t)(bitv(c[i]) << i);
    return v;
}
static void hexm(const char *tag, const void *p, size_t n, char *o) {   /* "tag=" and n bytes in hex */
    o += snprintf(o, strlen(tag) + 2, "%s=", tag);
    for (size_t j = 0; j < n; j++) o += snprintf(o, 3, "%02x", ((const uint8_t *)p)[j]);
}
static void showm(uint64_t a, size_t n) {   /* the bus's view of n cells */
    uint8_t b[256];
    char o[1024];
    for (size_t j = 0; j < n; j++) b[j] = peekc(a + j);
    hexm(" m", b, n, o);
    printf("%s", o);
}
/* Every free qubit reads 0, and the live ones are exactly those held by live rails and by the
 * cells [a, a + n): no ancilla left taken. Valid while no rail has been freed in the child. */
static void balanced(uint64_t a, size_t n) {
    uint32_t nf = cq_pool_minted() - cq_pool_live(), q[4096], held = 0;
    assert(nf <= 4096);
    for (uint32_t i = 0; i < nf; i++) { q[i] = cq_pool_take(); assert(!cq_replay_get_bit(q[i])); }
    while (nf) cq_pool_give(q[--nf]);
    uint32_t probe = cqrt_alloc_i1(0);
    for (uint32_t h = 1; h < probe; h++)
        for (uint32_t i = 0; i < cq_rail_width((int32_t)h); i++) held += cq_bit_is_q(cq_rail_bits((int32_t)h)[i]);
    for (size_t j = 0; j < n; j++) { cq_bit c[8]; cq_cell_read(a + j, c); for (int i = 0; i < 8; i++) held += cq_bit_is_q(c[i]); }
    if (cq_pool_live() != held) { printf("pool: %u live, %u held\n", cq_pool_live(), held); exit(9); }
}
static void rest_stdio(void) {   /* what is left of stdin, through stdio */
    char r[256];
    size_t k = fread(r, 1, sizeof r - 1, stdin);
    r[k] = 0;
    printf(" rest=%s|\n", r);
}
static void rest_fd(int fd) {
    char r[256];
    ssize_t k = read(fd, r, sizeof r - 1);
    r[k < 0 ? 0 : k] = 0;
    printf(" rest=%s|", r);
}

/* ---- Capture (kmgy.30, §1.11 Resolved and Capture) */
static int P_;       /* a predicate qubit's value */
static uint64_t N_;  /* a quantum length, address offset or stream choice */

/* All HOST: performed at the call, in order. A QUANTUM cell: deferred, captured at the call. */
static void s_cap_cells(void) {
    uint32_t t = cqrt_tape_alloc(), one = cqrt_alloc_i1(1), M = cqrt_qram_alloc_i8(0);
    char buf[] = "abc\n", host[] = "host";
    static const char ro[] = "ro\n";   /* read-only: a classical store makes the cell QUANTUM, host byte kept */
    printf("A\n");
    cqrt_tape_write_i64_lit(t, ptr(host));
    cqrt_tape_effect_controlled(one, t, "puts", 1);   /* every cell HOST: at the call */
    printf("B\n");
    cqrt_qram_store_i8_hl(M, ptr(ro), cqrt_alloc_i8('R'));
    cqrt_tape_write_i64_lit(t, ptr(ro)); cqrt_tape_write_i64_lit(t, ptr(stdout));
    cqrt_tape_effect_controlled(one, t, "fputs", 2);   /* a QUANTUM cell, classical content: deferred */
    printf("C\n");
    cqrt_qram_store_i8_hl_controlled(qbit(1), M, ptr(buf + 1), cqrt_alloc_i8('Z'));   /* buf[1] QUANTUM 'Z' */
    cqrt_tape_write_i64_lit(t, 1); cqrt_tape_write_i64_lit(t, ptr(buf)); cqrt_tape_write_i64_lit(t, 4);
    cqrt_tape_effect_controlled(one, t, "write", 3);
    strcpy(buf, "xyz\n");   /* the capture was at the call */
    printf("live\n");
}
static void s_cap_cells2(void) {   /* the same under a quantum predicate, which defers anyway */
    uint32_t t = cqrt_tape_alloc(), M = cqrt_qram_alloc_i8(0);
    char buf[] = "abc\n";
    cqrt_qram_store_i8_hl_controlled(qbit(1), M, ptr(buf + 2), cqrt_alloc_i8('Q'));
    cqrt_tape_write_i64_lit(t, 1); cqrt_tape_write_i64_lit(t, ptr(buf)); cqrt_tape_write_i64_lit(t, 4);
    cqrt_tape_effect_controlled(qbit(P_), t, "write", 3);
    buf[0] = '-';
    printf("live\n");
}
static void s_cap_unreadable(void) {   /* never faults: an unreadable cell reads 0 */
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_write_i64_lit(t, 16);
    cqrt_tape_effect_controlled(qbit(1), t, "puts", 1);
    cqrt_tape_write_i64_lit(t, 1); cqrt_tape_write_i64_lit(t, 16); cqrt_tape_write_i64_lit(t, 3);
    cqrt_tape_effect_controlled(qbit(1), t, "write", 3);
    printf("live\n");
}
static char qbuf[128] __attribute__((aligned(64)));
static void s_cap_qaddr(void) {   /* puts and write at an address rail reaching qbuf + 16k, k < 4 */
    uint32_t t = cqrt_tape_alloc(), one = cqrt_alloc_i1(1);
    for (int k = 0; k < 4; k++) snprintf(qbuf + 16 * k, 16, "str%d", k);
    cqrt_tape_write_i64(t, qmask64(ptr(qbuf) + 16 * N_, 0x30));
    cqrt_tape_effect_controlled(one, t, "puts", 1);
    cqrt_tape_write_i64_lit(t, 1); cqrt_tape_write_i64(t, qmask64(ptr(qbuf) + 16 * N_, 0x30)); cqrt_tape_write_i64_lit(t, 5);
    cqrt_tape_effect_controlled(one, t, "write", 3);
    memset(qbuf, '!', 64);
    printf("live\n");
}
static void s_cap_len(void) {   /* write(1, buf, n) and fwrite(buf, 1, n, stdout), n a rail */
    uint32_t t = cqrt_tape_alloc(), one = cqrt_alloc_i1(1);
    char buf[] = "abcdefghijklmno";
    cqrt_tape_write_i64_lit(t, 1); cqrt_tape_write_i64_lit(t, ptr(buf)); cqrt_tape_write_i64(t, qmask64(N_, 7));
    cqrt_tape_effect_controlled(one, t, "write", 3);
    cqrt_tape_write_i64_lit(t, ptr(buf)); cqrt_tape_write_i64_lit(t, 1); cqrt_tape_write_i64(t, qmask64(N_, 7));
    cqrt_tape_write_i64_lit(t, ptr(stdout));
    cqrt_tape_effect_controlled(one, t, "fwrite", 4);
    cqrt_tape_write_i64_lit(t, ptr(buf)); cqrt_tape_write_i64(t, qmask64(2, 3)); cqrt_tape_write_i64(t, qmask64(N_, 7));
    cqrt_tape_write_i64_lit(t, ptr(stdout));
    cqrt_tape_effect_controlled(one, t, "fwrite", 4);   /* size 2, nmemb n: 2n bytes */
    memset(buf, '.', 15);
    printf("live|");
}
static void s_cap_va(void) {   /* vprintf whose %s string has a QUANTUM cell: deferred, read through the bus */
    uint32_t t = cqrt_tape_alloc(), M = cqrt_qram_alloc_i8(0);
    char s[] = "vs";
    cqrt_qram_store_i8_hl_controlled(qbit(1), M, ptr(s + 1), cqrt_alloc_i8('Q'));
    vdefer(t, cqrt_alloc_i1(1), "V %s %d\n", s, 5);
    s[0] = '-';
    printf("live\n");
}
/* A format with a QUANTUM cell: deferred; the conversions that decide the
 * capture come from the host bytes, the performed call prints the captured (measured) bytes. */
static void s_cap_fmt_q(void) {
    uint32_t t = cqrt_tape_alloc(), M = cqrt_qram_alloc_i8(0);
    char f[] = "A%d|%s\n", s[] = "str";
    cqrt_qram_store_i8_hl_controlled(qbit(1), M, ptr(f + 3), cqrt_alloc_i8('#'));
    cqrt_tape_write_i64_lit(t, ptr(stdout)); cqrt_tape_write_i64_lit(t, ptr(f));
    cqrt_tape_write_i32_lit(t, 7); cqrt_tape_write_i64_lit(t, ptr(s));
    cqrt_tape_effect_controlled(cqrt_alloc_i1(1), t, "fprintf", 4);
    strcpy(f, "%p%p%p"); strcpy(s, "-");   /* captured at the call */
    printf("live\n");
}
static void s_cap_fmt_qaddr(void) {   /* printf's format at an address rail reaching qbuf + 16k, k < 4 */
    uint32_t t = cqrt_tape_alloc();
    for (int k = 0; k < 4; k++) snprintf(qbuf + 16 * k, 16, "F%d %%d\n", k);
    cqrt_tape_write_i64(t, qmask64(ptr(qbuf) + 16 * N_, 0x30)); cqrt_tape_write_i32_lit(t, 7);
    cqrt_tape_effect_controlled(cqrt_alloc_i1(1), t, "printf", 2);
    memset(qbuf, 0, 64);
    printf("live\n");
}
static void vdefer_q(uint32_t t, const char *f, ...) {   /* the va_list's first byte made QUANTUM, same value */
    va_list ap;
    va_start(ap, f);
    cqrt_qram_store_i8_hl_controlled(qbit(1), cqrt_qram_alloc_i8(0), ptr(ap), cqrt_alloc_i8(*(uint8_t *)(void *)ap));
    cqrt_tape_write_i64_lit(t, ptr(f)); cqrt_tape_write_i64_lit(t, ptr(ap));
    cqrt_tape_effect_controlled(cqrt_alloc_i1(1), t, "vprintf", 2);
    va_end(ap);
}
static void s_cap_va_bytes_q(void) { vdefer_q(cqrt_tape_alloc(), "V %s %d\n", "vs", 5); printf("live\n"); }
static void s_gap_va_ptr(void) {   /* a deferred vprintf whose va_list pointer is a rail: no rule, kmgy.43 */
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_write_i64_lit(t, ptr("%d\n")); cqrt_tape_write_i64(t, q64(ptr(qbuf)));
    cqrt_tape_effect_controlled(cqrt_alloc_i1(1), t, "vprintf", 2);
}
static void s_positional(void) {   /* Limits: positional %n$ fails loudly */
    cqrt_tape_fmt(cqrt_tape_alloc(), "%1$d\n");
}
static void s_stack17(void) {   /* Limits: more than 16 stack slots fails loudly */
    uint32_t t = cqrt_tape_alloc();
    for (int k = 0; k < 23; k++) cqrt_tape_write_i64_lit(t, (uint64_t)k);
    cqrt_tape_effect_controlled(cqrt_alloc_i1(1), t, "sum10", 23);
}

static void capture_tests(void) {
    expect(s_cap_cells, "capture: HOST at the call, a QUANTUM cell deferred", 0, "A\nhost\nB\nC\nlive\nRo\naZc\n");
    for (P_ = 0; P_ < 2; P_++) expect(s_cap_cells2, P_ ? "capture: QUANTUM cell, predicate 1" : "capture: predicate 0", 0, P_ ? "live\nabQ\n" : "live\n");
    expect_n(s_cap_unreadable, "capture: unreadable address reads 0", 0, "live\n\n\0\0\0", 9);
    for (N_ = 0; N_ < 4; N_++) {
        char w[64];
        snprintf(w, sizeof w, "live\nstr%d\nstr%d", (int)N_, (int)N_);
        expect_n(s_cap_qaddr, "capture: buffer at an address rail", 0, w, strlen(w) + 1);
    }
    const uint64_t ns[] = { 0, 1, 3, 5, 7 };
    for (int i = 0; i < 5; i++) {
        N_ = ns[i];
        char w[64];
        int k = snprintf(w, sizeof w, "live|%.*s%.*s%.*s", (int)N_, "abcdefgh", (int)N_, "abcdefgh", (int)(2 * N_), "abcdefghijklmno");
        expect_n(s_cap_len, "capture: rail length", 0, w, (size_t)k);
    }
    expect(s_cap_va, "capture: va_list string with a QUANTUM cell", 0, "live\nV vQ 5\n");
    expect(s_cap_fmt_q, "capture: format with a QUANTUM cell", 0, "live\nA7#str\n");
    for (N_ = 0; N_ < 4; N_++) {
        char w[64];
        snprintf(w, sizeof w, "live\nF%d 7\n", (int)N_);
        expect(s_cap_fmt_qaddr, "capture: format at an address rail", 0, w);
    }
    expect(s_cap_va_bytes_q, "capture: va_list bytes QUANTUM", 0, "live\nV vs 5\n");
    expect_abort(s_gap_va_ptr, "capture: va_list pointer a rail (ABI open)", "deferred v-form's va_list pointer that is a quantum rail: not defined by ABI §1.11 (opcode_quantum_operations-kmgy.43)");
    expect_abort(s_positional, "Limits: positional %n$", "positional %n$ conversions" LIM);
    expect_abort(s_stack17, "Limits: more than 16 stack slots", "a host call with more than 16 stack slots of arguments" LIM);
}

/* ---- INPUT merge rows (kmgy.28, §1.11 host-boundary table). Each child prints r, the bus's view
 * of the destination and what is left of the input; the parent's model says what they must be. */
static char dst[64] __attribute__((aligned(64)));
static void fill(void) { memset(dst, 'X', sizeof dst); }

static void s_fgets_pred(void) {   /* the predicate row: performed always, merged under p */
    fill();
    uint32_t t = cqrt_tape_alloc(), p = qbit(P_);
    cqrt_tape_write_i64_lit(t, ptr(dst)); cqrt_tape_write_i32_lit(t, 16); cqrt_tape_write_i64_lit(t, ptr(stdin));
    uint32_t r = cqrt_tape_effect_i64_controlled(p, t, "fgets", 3);
    balanced(ptr(dst), 16);
    printf("r=%d", peekr(r) == ptr(dst) ? 1 : (int)peekr(r));
    showm(ptr(dst), 16);
    rest_stdio();
}
static void s_fgetc_pred(void) {
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_write_i64_lit(t, ptr(stdin));
    uint32_t r = cqrt_tape_effect_i32_controlled(qbit(P_), t, "getc_unlocked", 1);
    balanced(0, 0);
    printf("r=%c", (int)peekr(r) ? (int)peekr(r) : '0');
    rest_stdio();
}
static void vscan(uint32_t t, uint32_t c, uint32_t *r, const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    cqrt_tape_write_i64_lit(t, ptr(f)); cqrt_tape_write_i64_lit(t, ptr(ap));
    *r = cqrt_tape_effect_i32_controlled(c, t, "vscanf", 2);
    va_end(ap);
}
static void s_scanf_pred(void) {   /* scanf and vscanf: each pointer's bytes stored under p */
    fill();
    uint32_t t = cqrt_tape_alloc(), r1, r2;
    cqrt_tape_write_i64_lit(t, ptr("%d %3s")); cqrt_tape_write_i64_lit(t, ptr(dst)); cqrt_tape_write_i64_lit(t, ptr(dst + 8));
    r1 = cqrt_tape_effect_i32_controlled(qbit(P_), t, "scanf", 3);
    vscan(t, qbit(P_), &r2, " %hhd%n", dst + 16, dst + 20);
    balanced(ptr(dst), 32);
    printf("r=%d,%d", (int)peekr(r1), (int)peekr(r2));
    showm(ptr(dst), 24);
    rest_stdio();
}
static void s_getline_pred(void) {
    struct { char *line; size_t cap; } lc = { NULL, 0 };
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_write_i64_lit(t, ptr(&lc.line)); cqrt_tape_write_i64_lit(t, ptr(&lc.cap)); cqrt_tape_write_i64_lit(t, ptr(stdin));
    uint32_t r = cqrt_tape_effect_i64_controlled(qbit(P_), t, "getline", 3);
    balanced(ptr(&lc), sizeof lc);
    uint64_t l = 0, c = 0;
    for (int j = 7; j >= 0; j--) l = l << 8 | peekc(ptr(&lc.line) + j), c = c << 8 | peekc(ptr(&lc.cap) + j);
    printf("r=%d line=%s cap>0=%d", (int)peekr(r), l ? (char *)(uintptr_t)l : "null", c > 0);
    rest_stdio();
}
static void s_gets_pred(void) {
    fill();
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_write_i64_lit(t, ptr(dst));
    uint32_t r = cqrt_tape_effect_i64_controlled(qbit(P_), t, "gets", 1);
    balanced(ptr(dst), 8);
    printf("r=%d", peekr(r) == ptr(dst));
    showm(ptr(dst), 8);
    rest_stdio();
}
static void s_fgets_len(void) {   /* the length row: n a rail holding N_ */
    fill();
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_write_i64_lit(t, ptr(dst)); cqrt_tape_write_i32(t, q32((uint32_t)N_)); cqrt_tape_write_i64_lit(t, ptr(stdin));
    uint32_t r = cqrt_tape_effect_i64_controlled(qbit(P_), t, "fgets", 3);
    balanced(ptr(dst), 16);
    printf("r=%d", peekr(r) == ptr(dst));
    showm(ptr(dst), 16);
    rest_stdio();
}
static void s_read_len(void) {
    fill();
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_write_i32_lit(t, 0); cqrt_tape_write_i64_lit(t, ptr(dst)); cqrt_tape_write_i64(t, q64(N_));
    uint32_t r = cqrt_tape_effect_i64_controlled(cqrt_alloc_i1(1), t, "read", 3);
    balanced(ptr(dst), 16);
    printf("r=%lld", (long long)peekr(r));
    showm(ptr(dst), 16);
    rest_fd(0);
    printf("\n");
}
static uint64_t S_;   /* fread: N_ nmemb (S_ classical 2) or, S_ >= 100, size S_ - 100 quantum and nmemb 2 */
static void s_fread_len(void) {
    fill();
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_write_i64_lit(t, ptr(dst));
    if (S_ < 100) { cqrt_tape_write_i64_lit(t, S_); cqrt_tape_write_i64(t, q64(N_)); }
    else { cqrt_tape_write_i64(t, q64(S_ - 100)); cqrt_tape_write_i64_lit(t, 2); }
    cqrt_tape_write_i64_lit(t, ptr(stdin));
    uint32_t r = cqrt_tape_effect_i64_controlled(cqrt_alloc_i1(1), t, "fread", 4);
    if (S_ < 100) balanced(ptr(dst), 16);   /* the divider's rails are freed: no handle census */
    printf("r=%lld", (long long)peekr(r));
    showm(ptr(dst), 16);
    rest_stdio();
}
static void s_fgets_addr(void) {   /* the buffer address row: dst + 16 N_ through a rail reaching 4 slots */
    fill();
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_write_i64(t, qmask64(ptr(dst) + 16 * N_, 0x30)); cqrt_tape_write_i32_lit(t, 8); cqrt_tape_write_i64_lit(t, ptr(stdin));
    uint32_t r = cqrt_tape_effect_i64_controlled(qbit(P_), t, "fgets", 3);
    balanced(ptr(dst), 64);
    printf("r=%lld", (long long)(peekr(r) ? peekr(r) - ptr(dst) : -1));
    showm(ptr(dst), 64);
    rest_stdio();
}
static void s_stream_file(void) {   /* the identifier row on FILE*: two streams the tape opened */
    fill();
    uint32_t t = cqrt_tape_alloc(), one = cqrt_alloc_i1(1);
    FILE *f[2];
    for (int k = 0; k < 2; k++) {
        f[k] = (FILE *)(uintptr_t)peekr(cqrt_tape_effect_i64_controlled(one, t, "tmpfile", 0));
        fputs(k ? "CD\nE" : "AB\nF", f[k]); rewind(f[k]);
    }
    uint64_t a = ptr(f[0]), b = ptr(f[1]);
    cqrt_tape_write_i64_lit(t, ptr(dst)); cqrt_tape_write_i32_lit(t, 8); cqrt_tape_write_i64(t, qmask64(N_ ? b : a, a ^ b));
    uint32_t r = cqrt_tape_effect_i64_controlled(qbit(P_), t, "fgets", 3);
    balanced(ptr(dst), 8);
    printf("r=%d", peekr(r) == ptr(dst));
    showm(ptr(dst), 8);
    printf(" rest=%c%c\n", fgetc(f[0]), fgetc(f[1]));
}
static void s_stream_fd(void) {   /* the identifier row on a descriptor: fds 10 and 11 */
    fill();
    for (int k = 0; k < 2; k++) {
        int p[2];
        assert(pipe(p) == 0);
        assert(write(p[1], k ? "abcdef" : "123456", 6) == 6);
        close(p[1]); dup2(p[0], 10 + k); close(p[0]);
    }
    uint32_t t = cqrt_tape_alloc(), fd = cqrt_alloc_i32(10 + (uint32_t)N_);
    cq_get_qubit(&cq_rail_bits((int32_t)fd)[0]);
    cqrt_tape_write_i32(t, fd); cqrt_tape_write_i64_lit(t, ptr(dst)); cqrt_tape_write_i64_lit(t, 4);
    uint32_t r = cqrt_tape_effect_i64_controlled(qbit(P_), t, "read", 3);
    balanced(ptr(dst), 8);
    printf("r=%d", (int)peekr(r));
    showm(ptr(dst), 8);
    rest_fd(10); rest_fd(11);
    printf("\n");
}
static void s_classical_in(void) {   /* the classical INPUT row: no gate; gets is fgets at K2 without the newline */
    cq_replay_reset();
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_write_i64_lit(t, ptr(dst)); cqrt_tape_write_i32_lit(t, 16); cqrt_tape_write_i64_lit(t, ptr(stdin));
    cqrt_tape_effect_i64_controlled(cqrt_alloc_i1(1), t, "fgets", 3);
    printf("%s", dst);
    cqrt_tape_write_i64_lit(t, ptr(dst));
    assert(cqrt_measure_i64(cqrt_tape_effect_i64_controlled(cqrt_alloc_i1(1), t, "gets", 1)) == ptr(dst));
    assert(cq_replay_gates() == 0 && cq_pool_live() == 0);
    printf("%s|", dst);
    rest_stdio();
}
/* pread's offset row: off a rail whose bits 2 and 3 are qubits (0, 4, 8 or 12 into a 10-byte file),
 * under predicate P_; the descriptor's position is untouched. */
static void s_pread_off(void) {
    fill();
    FILE *fp = tmpfile();
    int fd = fileno(fp);
    assert(write(fd, "0123456789", 10) == 10);
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_write_i32_lit(t, (uint32_t)fd); cqrt_tape_write_i64_lit(t, ptr(dst)); cqrt_tape_write_i64_lit(t, 4);
    cqrt_tape_write_i64(t, qmask64(N_, 0xc));
    uint32_t r = cqrt_tape_effect_i64_controlled(qbit(P_), t, "pread", 4);
    balanced(ptr(dst), 8);
    printf("r=%d", (int)peekr(r));
    showm(ptr(dst), 8);
    printf(" rest=%lld|\n", (long long)lseek(fd, 0, SEEK_CUR));
}
static void s_pread_cap(void) {   /* Limits: an enumerated rail above the cap fails loudly */
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_write_i32_lit(t, 0); cqrt_tape_write_i64_lit(t, ptr(dst)); cqrt_tape_write_i64_lit(t, 4);
    cqrt_tape_write_i64(t, qmask64(0, (1ull << (CQ_MAX_QADDR_BITS + 1)) - 1));
    cqrt_tape_effect_i64_controlled(cqrt_alloc_i1(1), t, "pread", 4);
}
/* getdelim's delimiter row: d a rail holding D_ with the bits of DM_ as qubits, under P_. Prints
 * the result, the line and size the cells name, and the input left. */
static uint64_t D_, DM_;
static FILE *gd_in;
static void s_getdelim_d(void) {
    struct { char *line; size_t cap; } lc = { NULL, 0 };
    uint32_t t = cqrt_tape_alloc(), d = cqrt_alloc_i32((uint32_t)D_);
    for (int i = 0; i < 32; i++) if (DM_ >> i & 1) cq_get_qubit(&cq_rail_bits((int32_t)d)[i]);
    FILE *in = gd_in ? gd_in : stdin;
    cqrt_tape_write_i64_lit(t, ptr(&lc.line)); cqrt_tape_write_i64_lit(t, ptr(&lc.cap)); cqrt_tape_write_i32(t, d);
    cqrt_tape_write_i64_lit(t, ptr(in));
    uint32_t r = cqrt_tape_effect_i64_controlled(qbit(P_), t, "getdelim", 4);
    balanced(ptr(&lc), sizeof lc);
    uint64_t l = 0, c = 0;
    for (int j = 7; j >= 0; j--) l = l << 8 | peekc(ptr(&lc.line) + j), c = c << 8 | peekc(ptr(&lc.cap) + j);
    printf("r=%lld line=%.12s cap=%llu", (long long)peekr(r), l ? (char *)(uintptr_t)l : "null", (unsigned long long)c);
    if (gd_in) printf(" pos=%ld\n", ftell(in)); else rest_stdio();
}
static void s_getdelim_long(void) {   /* K2 + 5 bytes in a file: the K2 call's position on every branch */
    gd_in = tmpfile();
    for (int k = 0; k < CQ_K2 + 5; k++) fputc(k == 2 ? ',' : 'x', gd_in);
    rewind(gd_in);
    s_getdelim_d();
}
/* A scanf whose format has a QUANTUM cell after its first conversion: no abort, no fault; the
 * text defines the first conversion only. P_ 2: a classical predicate (the host call as it stands). */
static void s_scanf_fmt_q(void) {
    fill();
    char f[] = "%d,%d";
    cqrt_qram_store_i8_hl_controlled(qbit(1), cqrt_qram_alloc_i8(0), ptr(f + 2), cqrt_alloc_i8('#'));
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_write_i64_lit(t, ptr(f)); cqrt_tape_write_i64_lit(t, ptr(dst)); cqrt_tape_write_i64_lit(t, ptr(dst + 4));
    uint32_t r = cqrt_tape_effect_i32_controlled(P_ == 2 ? cqrt_alloc_i1(1) : qbit(P_), t, "scanf", 3);
    int a = 0;
    for (int j = 3; j >= 0; j--) a = a << 8 | peekc(ptr(dst) + j);
    printf("a=%d ok=%d\n", a, P_ ? peekr(r) == 1 || peekr(r) == 2 : peekr(r) == 0);
}
static void s_scanf_positional(void) {   /* Limits: a scanf row's positional %n$ fails loudly */
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_write_i64_lit(t, ptr("%1$d")); cqrt_tape_write_i64_lit(t, ptr(dst));
    cqrt_tape_effect_i32_controlled(qbit(1), t, "scanf", 2);
}
/* Limits: what ABI §1.11 lets this backend leave unimplemented, failing loudly. */
static void s_gap_ungetc(void) {
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_write_i32(t, q32('a')); cqrt_tape_write_i64_lit(t, ptr(stdin));
    cqrt_tape_effect_i32_controlled(cqrt_alloc_i1(1), t, "ungetc", 2);
}
static void s_gap_scanf_fmt(void) {
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_write_i64(t, q64(ptr("%d"))); cqrt_tape_write_i64_lit(t, ptr(dst));
    cqrt_tape_effect_i32_controlled(cqrt_alloc_i1(1), t, "scanf", 2);
}
static void s_gap_vscanf_ap(void) {
    uint32_t t = cqrt_tape_alloc();
    cqrt_tape_write_i64_lit(t, ptr("%d")); cqrt_tape_write_i64(t, q64(ptr(dst)));
    cqrt_tape_effect_i32_controlled(cqrt_alloc_i1(1), t, "vscanf", 2);
}

static void model(char *w, int r, const void *m, size_t n, const char *rest) {   /* "r=.. m=.. rest=..|" */
    w += snprintf(w, 32, "r=%d ", r);
    hexm("m", m, n, w);
    snprintf(w + strlen(w), strlen(rest) + 9, " rest=%s|\n", rest);
}
static void merge_tests(void) {
    char w[1024], m[64];
    in_text = "hi\nrest\n";
    for (P_ = 0; P_ < 2; P_++) {
        memset(m, 'X', 16);
        if (P_) memcpy(m, "hi\n", 4);
        model(w, P_, m, 16, "rest\n");
        expect(s_fgets_pred, "INPUT, quantum predicate: fgets", 0, w);
        snprintf(w, sizeof w, "r=%c rest=i\nrest\n|\n", P_ ? 'h' : '0');
        expect(s_fgetc_pred, "INPUT, quantum predicate: getc_unlocked", 0, w);
        memset(m, 'X', 16);
        if (P_) memcpy(m, "hi\0", 3);
        model(w, P_, m, 8, "rest\n");
        expect(s_gets_pred, "INPUT, quantum predicate: gets", 0, w);
        snprintf(w, sizeof w, "r=%d line=%s cap>0=%d rest=rest\n|\n", P_ ? 3 : 0, P_ ? "hi\n" : "null", P_);
        expect(s_getline_pred, "INPUT, quantum predicate: getline", 0, w);
    }
    in_text = "42 abc -7xyz";
    for (P_ = 0; P_ < 2; P_++) {
        memset(m, 'X', 24);
        if (P_) { int v = 42; memcpy(m, &v, 4); memcpy(m + 8, "abc", 4); m[16] = (char)-7; v = 3; memcpy(m + 20, &v, 4); }
        snprintf(w, sizeof w, "r=%d,%d ", P_ ? 2 : 0, P_);
        hexm("m", m, 24, w + strlen(w));
        strcat(w, " rest=xyz|\n");
        expect(s_scanf_pred, "INPUT, quantum predicate: scanf, vscanf", 0, w);
    }
    in_text = "hello\nworld\n";
    const int64_t fl[] = { 0, 1, 2, 4, 7, 8, 4101 };
    for (P_ = 0; P_ < 2; P_++)
        for (int i = 0; i < 7; i++) {
            N_ = (uint64_t)fl[i];
            memset(m, 'X', 16);
            if (P_ && fl[i] >= 1) { int64_t k = fl[i] - 1 < 6 ? fl[i] - 1 : 6; memcpy(m, "hello\n", (size_t)k); m[k] = 0; }
            model(w, P_ && fl[i] >= 1, m, 16, "world\n");
            expect(s_fgets_len, "INPUT, quantum length: fgets", 0, w);
        }
    in_text = "abcdef";
    const uint64_t rl[] = { 0, 1, 3, 6, 9, 4106 };
    for (int i = 0; i < 6; i++) {
        N_ = rl[i];
        uint64_t k = N_ < 6 ? N_ : 6;
        memset(m, 'X', 16); memcpy(m, "abcdef", k);
        model(w, (int)k, m, 16, "");
        expect(s_read_len, "INPUT, quantum length: read", 0, w);
    }
    in_text = "abcdefg";
    const uint64_t fr[][2] = { { 2, 0 }, { 2, 1 }, { 2, 3 }, { 2, 5 }, { 100, 0 }, { 101, 0 }, { 103, 0 }, { 104, 0 } };
    for (int i = 0; i < 8; i++) {
        S_ = fr[i][0], N_ = fr[i][1];
        uint64_t s = S_ < 100 ? S_ : S_ - 100, nm = S_ < 100 ? N_ : 2, bytes = s * nm < 7 ? s * nm : 7;
        memset(m, 'X', 16); memcpy(m, "abcdefg", bytes);
        model(w, (int)(s ? (nm < 7 / s ? nm : 7 / s) : 0), m, 16, "");
        expect(s_fread_len, "INPUT, quantum length: fread size·nmemb", 0, w);
    }
    in_text = "hey\nmore\n";
    for (P_ = 0; P_ < 2; P_++)
        for (N_ = 0; N_ < 4; N_++) {
            memset(m, 'X', 64);
            if (P_) memcpy(m + 16 * N_, "hey\n", 5);
            model(w, P_ ? (int)(16 * N_) : -1, m, 64, "more\n");
            expect(s_fgets_addr, "INPUT, quantum buffer address: fgets", 0, w);
        }
    in_text = NULL;
    for (P_ = 0; P_ < 2; P_++)
        for (N_ = 0; N_ < 2; N_++) {
            memset(m, 'X', 8);
            if (P_) memcpy(m, N_ ? "CD\n" : "AB\n", 4);
            model(w, P_, m, 8, "");
            snprintf(strstr(w, " rest="), 16, " rest=FE\n");
            expect(s_stream_file, "INPUT, quantum stream: FILE*", 0, w);
            memset(m, 'X', 8);
            if (P_) memcpy(m, N_ ? "abcd" : "1234", 4);
            model(w, P_ ? 4 : 0, m, 8, "");
            snprintf(strstr(w, " rest="), 32, " rest=56| rest=ef|\n");
            expect(s_stream_fd, "INPUT, quantum stream: descriptor", 0, w);
        }
    in_text = "line\nnext\nrest";
    expect(s_classical_in, "INPUT, classical: no gate; gets", 0, "line\nnext| rest=rest|\n");
    in_text = NULL;
    for (P_ = 0; P_ < 2; P_++)
        for (N_ = 0; N_ < 16; N_ += 4) {   /* the model: pread on the 10-byte file */
            int k = P_ && N_ < 10 ? (int)(10 - N_ < 4 ? 10 - N_ : 4) : 0;
            memset(m, 'X', 8); memcpy(m, &"0123456789"[N_ < 10 ? N_ : 0], (size_t)k);
            model(w, k, m, 8, "10");
            expect(s_pread_off, "INPUT, quantum offset: pread", 0, w);
        }
    expect_abort(s_pread_cap, "Limits: enumerated rail above the cap", "more quantum bits than CQ_MAX_QADDR_BITS to enumerate, the build-time cap" LIM);
    /* getdelim: two candidates (',' and '<', which does not occur) and four (',' '-' '<' '='), input
     * shorter than K2; then the empty input; then K2 + 5 bytes in a file. Model: memchr. */
    const char *gin[] = { "ab-cd,ef=g", "" };
    const uint64_t masks[] = { 0x10, 0x11 };
    for (int s = 0; s < 2; s++)
        for (int mk = 0; mk < 2; mk++)
            for (P_ = 0; P_ < 2; P_++)
                for (uint64_t a = 0; a < 4; a++) {
                    if (a & ~(uint64_t)(mk ? 3 : 2)) continue;
                    D_ = 0x2c | (a & 1) | (a >> 1 & 1) << 4, DM_ = masks[mk];
                    in_text = gin[s];
                    size_t n = strlen(gin[s]);
                    const char *at = memchr(gin[s], (int)D_, n);
                    size_t len = at ? (size_t)(at - gin[s]) + 1 : n;
                    if (!P_) snprintf(w, sizeof w, "r=0 line=null cap=0 rest=|\n");
                    else snprintf(w, sizeof w, "r=%lld line=%.*s cap=%zu rest=|\n", n ? (long long)len : -1LL, (int)len, gin[s], len + 1);
                    expect(s_getdelim_d, "INPUT, quantum delimiter: getdelim", 0, w);
                }
    in_text = NULL;
    for (P_ = 0; P_ < 2; P_++)
        for (uint64_t a = 0; a < 2; a++) {
            D_ = 0x2c | a << 4, DM_ = 0x10;
            if (!P_) snprintf(w, sizeof w, "r=0 line=null cap=0 pos=%d\n", CQ_K2);
            else snprintf(w, sizeof w, "r=%d line=%.12s cap=%d pos=%d\n", a ? CQ_K2 : 3, a ? "xx,xxxxxxxxx" : "xx,", (a ? CQ_K2 : 3) + 1, CQ_K2);
            expect(s_getdelim_long, "INPUT, quantum delimiter: getdelim past K2", 0, w);
        }
    in_text = "42,7";
    for (P_ = 0; P_ < 3; P_++) {
        snprintf(w, sizeof w, "a=%d ok=1\n", P_ ? 42 : 0x58585858);
        expect(s_scanf_fmt_q, "INPUT, scanf format with a QUANTUM cell", 0, w);
    }
    expect_abort(s_scanf_positional, "Limits: scanf positional %n$", "positional %n$ conversions" LIM);
    in_text = NULL;
    expect_abort(s_gap_ungetc, "Limits: ungetc's quantum character", "ungetc/ungetwc with a quantum character" LIM);
    expect_abort(s_gap_scanf_fmt, "Limits: scanf's quantum format pointer", "an INPUT call's format pointer that is a quantum rail" LIM);
    expect_abort(s_gap_vscanf_ap, "Limits: vscanf's quantum va_list pointer", "an INPUT call's va_list pointer that is a quantum rail" LIM);
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
    assert(sizeof linked / sizeof *linked == 4 * 37 + 5);
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
    capture_tests();
    merge_tests();

    T = cqrt_tape_alloc();
#define HX_RUN(n, ...) hx_##n();
    CQ_WIDTHS(HX_RUN) CQ_FP_WIDTHS(HX_RUN)
    printf("tape_write[_controlled] sampled at every width: ok; %zu symbols linked\n", sizeof linked / sizeof *linked);
    return 0;
}
