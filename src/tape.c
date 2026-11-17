/* Tape and abort, BACKEND_ABI §1.11 (kmgy.22). One event sequence per process, in call order.
 * An event is performed at its completion when it is resolved (classical predicate and fields,
 * every earlier event done); otherwise it is deferred and performed at the end of the shot
 * (process exit, or a classical-1 abort flag). A host call is made with the x86-64 SysV
 * convention by name (dlsym), its fields as arguments; a format entry is the host printf.
 *
 * Not here yet:
 *   - INPUT calls with a quantum predicate, buffer address, length or stream: kmgy.28.
 *   - Capture through the bus (QUANTUM cells, address or length rails, no-fault reads, the
 *     HOST check of resolution): kmgy.30, after the bus (kmgy.19). Until then a buffer at a
 *     classical address is read directly as host memory. */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "cq_config.h"
#include "cq_rt.h"
#include "cqrt.h"

static void die(const char *msg, const char *what) {
    fprintf(stderr, "cq_runtime: tape: %s%s%s\n", msg, what ? ": " : "", what ? what : "");
    abort();
}

/* A field: a classical value (rail 0) or a tape rail h'. fp: the width token is a float. */
typedef struct { uint32_t bits; uint8_t fp; int32_t rail; uint64_t v[4]; } field;

enum { EV_FMT, EV_EFF, EV_ABORT };
enum { ST_OPEN, ST_DONE, ST_DEFERRED };
typedef struct {
    uint8_t kind, status, rfp;
    int8_t  pred;          /* 0 or 1, or -1: quantum, held in rail pr */
    int32_t pr;            /* the captured predicate, or the abort flag */
    char   *text;          /* fmt, or the callee name (NULL: the address in field 0) */
    field  *f;
    uint32_t nf, need, rbits;   /* need: fields a FMT still waits for; rbits: result width, 0 none */
} event;

typedef struct { field *f; uint32_t nf, cap; uint32_t open; } tape;   /* open: FMT event + 1, or 0 */

static tape   *tapes;
static uint32_t ntapes, captapes;
static event  *ev;
static uint32_t nev, capev, live;    /* live: the first event not done */
static int     inited, exiting;

static void *grow(void *p, uint32_t *cap, uint32_t n, size_t sz) {
    if (n < *cap) return p;
    *cap = *cap ? 2 * *cap : 16;
    if (!(p = realloc(p, *cap * sz))) die("out of memory", NULL);
    return p;
}

static char *copy_str(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *c = malloc(n);
    if (!c) die("out of memory", NULL);
    return memcpy(c, s, n);
}

static cq_bit *rail(uint32_t h, uint32_t w) {
    if (cq_rail_width((int32_t)h) != w) die("rail width does not match the call", NULL);
    return cq_rail_bits((int32_t)h);
}

static tape *get_tape(uint32_t t) {
    if (t == 0 || t > ntapes) die("bad tape handle", NULL);
    return &tapes[t - 1];
}

static int classical(int32_t h) {
    cq_bit *b = cq_rail_bits(h);
    for (uint32_t i = 0; i < cq_rail_width(h); i++) if (cq_bit_is_q(b[i])) return 0;
    return 1;
}

/* A field's value; a rail is measured (classical bits read as they are). */
static void value(const field *f, uint64_t v[4]) {
    if (!f->rail) { memcpy(v, f->v, sizeof f->v); return; }
    memset(v, 0, 4 * sizeof *v);
    cq_bit *b = cq_rail_bits(f->rail);
    for (uint32_t i = 0; i < f->bits; i++) if (cq_measure_bit(&b[i])) v[i / 64] |= 1ull << (i % 64);
}

/* ---- Formats: the arguments a printf/wprintf format consumes, in order. cls: 'i' an integer or
 * pointer, 'd' a double, 'L' a long double, 's' a string, 'S' a wide string; prec: a literal
 * precision, else -1. shortcut: positional %n$ is not parsed; add it if a program uses it. */
typedef void (*arg_fn)(void *ctx, char cls, long prec);

static int in(const char *set, uint32_t c) { return c && c < 128 && strchr(set, (int)c); }

static uint32_t walk(const void *p, int wide, arg_fn arg, void *ctx) {
#define AT(i) (wide ? (uint32_t)((const wchar_t *)p)[i] : (uint32_t)((const unsigned char *)p)[i])
#define ARG(c, pr) do { n++; if (arg) arg(ctx, c, pr); } while (0)
    uint32_t n = 0;
    for (size_t i = 0; AT(i); i++) {
        if (AT(i) != '%' || AT(++i) == '%') continue;
        while (in("-+ #0'", AT(i))) i++;
        if (AT(i) == '*') { ARG('i', -1); i++; }
        while (AT(i) >= '0' && AT(i) <= '9') i++;
        long prec = -1;
        if (AT(i) == '.') {
            i++;
            if (AT(i) == '*') { ARG('i', -1); i++; }
            else for (prec = 0; AT(i) >= '0' && AT(i) <= '9'; i++) prec = prec * 10 + (long)(AT(i) - '0');
        }
        int l = 0, L = 0;
        for (; in("hlLjztq", AT(i)); i++) { l |= AT(i) == 'l'; L |= AT(i) == 'L'; }
        uint32_t c = AT(i);
        if (!c) break;
        if (in("diouxXcCpn", c)) ARG('i', -1);
        else if (in("eEfFgGaA", c)) ARG(L ? 'L' : 'd', -1);
        else if (c == 's') ARG(l ? 'S' : 's', prec);
        else if (c == 'S') ARG('S', prec);
    }
    return n;
#undef ARG
#undef AT
}

/* ---- Capture (a deferred entry): a buffer at a classical address, copied now. */
static void *cap_buf(uint64_t a, uint64_t len) {
    if (!a) return NULL;   /* shortcut: no probe of the address; kmgy.30 reads through the bus */
    void *c = malloc(len ? len : 1);
    if (!c) die("out of memory", NULL);
    return memcpy(c, (const void *)(uintptr_t)a, len);
}

static void *cap_str(uint64_t a, int wide, long prec) {
    if (!a) return NULL;
    size_t u = wide ? sizeof(wchar_t) : 1, n = 0, lim = prec >= 0 && prec < CQ_K2 ? (size_t)prec : CQ_K2;
    const char *s = (const char *)(uintptr_t)a;
    while (n < lim && (wide ? ((const wchar_t *)s)[n] != 0 : s[n] != 0)) n++;
    char *c = calloc(n + 1, u);
    if (!c) die("out of memory", NULL);
    return memcpy(c, s, n * u);
}

static field *pointer(event *e, uint32_t k) {
    if (k >= e->nf) die("entry has fewer fields than its callee reads", e->text);
    field *f = &e->f[k];
    if (f->rail && !classical(f->rail))
        die("a buffer at a quantum address or of quantum length in a deferred entry is not implemented yet (kmgy.30)", e->text);
    value(f, f->v);
    f->rail = 0;
    return f;
}

static void set_ptr(field *f, const void *p) { memset(f->v, 0, sizeof f->v); f->v[0] = (uint64_t)(uintptr_t)p; }

/* The %s arguments of a format at field k, its arguments from field k + 1. */
typedef struct { event *e; uint32_t k; } fmt_ctx;
static void cap_fmt_arg(void *ctx, char cls, long prec) {
    fmt_ctx *c = ctx;
    uint32_t k = c->k++;
    if (cls == 's' || cls == 'S') { field *f = pointer(c->e, k); set_ptr(f, cap_str(f->v[0], cls == 'S', prec)); }
}

/* A va_list (x86-64 SysV) and the slots its format takes, copied into a fresh va_list whose
 * arguments all sit in its overflow area, as the lowered caller's do (CR rule 16). */
typedef struct { uint32_t gp, fp; uint8_t *over, *regs; } valist;
typedef struct { valist *src; uint64_t *area; uint32_t n; } va_ctx;
static void cap_va_arg(void *ctx, char cls, long prec) {
    va_ctx *c = ctx;
    if (c->n + 3 > CQ_K2) die("va_list format takes too many arguments", NULL);
    if (cls == 'L') {
        c->src->over = (uint8_t *)(((uintptr_t)c->src->over + 15) & ~(uintptr_t)15);
        c->n += c->n & 1;
        memcpy(&c->area[c->n], c->src->over, 16);
        c->src->over += 16; c->n += 2;
        return;
    }
    uint8_t *at;
    if (cls == 'd' && c->src->fp < 176) { at = c->src->regs + c->src->fp; c->src->fp += 16; }
    else if (cls != 'd' && c->src->gp < 48) { at = c->src->regs + c->src->gp; c->src->gp += 8; }
    else { at = c->src->over; c->src->over += 8; }
    memcpy(&c->area[c->n], at, 8);
    if (cls == 's' || cls == 'S') c->area[c->n] = (uint64_t)(uintptr_t)cap_str(c->area[c->n], cls == 'S', prec);
    c->n++;
}

/* What a deferred entry of a known OUTPUT callee reads, and which callees are INPUT calls.
 * spec: s K string, w K wide string, f K format and its arguments, F K wide format, v K format
 * and the va_list at K + 1, V K wide, b K L buffer of length L, m K L M buffer of length L·M,
 * P K an fpos_t, e K an fenv_t, x K an fexcept_t; I: an INPUT call. */
static const struct { const char *name, *spec; } callees[] = {
    { "fgets", "I" }, { "fgetws", "I" }, { "fread", "I" }, { "read", "I" }, { "pread", "I" },
    { "gets", "I" }, { "fgetc", "I" }, { "getc", "I" }, { "getchar", "I" }, { "fgetwc", "I" },
    { "getwc", "I" }, { "getwchar", "I" }, { "getw", "I" }, { "ungetc", "I" }, { "ungetwc", "I" },
    { "feof", "I" }, { "ferror", "I" }, { "clearerr", "I" }, { "scanf", "I" }, { "fscanf", "I" },
    { "vscanf", "I" }, { "vfscanf", "I" }, { "wscanf", "I" }, { "fwscanf", "I" }, { "vwscanf", "I" },
    { "vfwscanf", "I" }, { "getline", "I" }, { "getdelim", "I" }, { "fgetln", "I" },
    { "printf", "f0" }, { "fprintf", "f1" }, { "vprintf", "v0" }, { "vfprintf", "v1" },
    { "wprintf", "F0" }, { "fwprintf", "F1" }, { "vwprintf", "V0" }, { "vfwprintf", "V1" },
    { "puts", "s0" }, { "fputs", "s0" }, { "perror", "s0" }, { "fputws", "w0" },
    { "fwrite", "m012" }, { "write", "b12" }, { "fsetpos", "P1" },
    { "remove", "s0" }, { "unlink", "s0" }, { "rename", "s0s1" }, { "fopen", "s0s1" }, { "open", "s0" },
    { "__assert_fail", "s0s1s3" }, { "fesetenv", "e0" }, { "feupdateenv", "e0" }, { "fesetexceptflag", "x0" },
};

static const char *spec_of(const char *name) {
    if (!name) return "";
    size_t n = strlen(name), u = strlen("_unlocked");
    if (n > u && !strcmp(name + n - u, "_unlocked")) n -= u;   /* an _unlocked form is its locked one */
    for (size_t i = 0; i < sizeof callees / sizeof *callees; i++)
        if (strlen(callees[i].name) == n && !strncmp(callees[i].name, name, n)) return callees[i].spec;
    return "";
}

static void capture(event *e) {
    if (e->kind == EV_FMT) { fmt_ctx c = { e, 0 }; walk(e->text, 0, cap_fmt_arg, &c); return; }
    for (const char *s = spec_of(e->text); *s; ) {
        char op = *s++;
        uint32_t k = (uint32_t)(*s++ - '0');
        field *f = pointer(e, k);
        uint64_t a = f->v[0];
        if (op == 's' || op == 'w') set_ptr(f, cap_str(a, op == 'w', -1));
        else if (op == 'f' || op == 'F') {
            fmt_ctx c = { e, k + 1 };
            if (a) walk((const void *)(uintptr_t)a, op == 'F', cap_fmt_arg, &c);
            set_ptr(f, cap_str(a, op == 'F', -1));
        } else if (op == 'v' || op == 'V') {
            field *ap = pointer(e, k + 1);
            if (a && ap->v[0]) {
                valist src = *(const valist *)(uintptr_t)ap->v[0], *nv = malloc(sizeof *nv);
                va_ctx c = { &src, calloc(CQ_K2, 8), 0 };
                if (!nv || !c.area) die("out of memory", NULL);
                walk((const void *)(uintptr_t)a, op == 'V', cap_va_arg, &c);
                *nv = (valist){ 48, 176, (uint8_t *)c.area, NULL };
                set_ptr(ap, nv);
            }
            set_ptr(f, cap_str(a, op == 'V', -1));
        } else if (op == 'b') set_ptr(f, cap_buf(a, pointer(e, (uint32_t)(*s++ - '0'))->v[0]));
        else if (op == 'm') {
            uint64_t l = pointer(e, (uint32_t)(s[0] - '0'))->v[0], m = pointer(e, (uint32_t)(s[1] - '0'))->v[0];
            s += 2;
            set_ptr(f, cap_buf(a, l * m));
        } else set_ptr(f, cap_buf(a, op == 'P' ? sizeof(fpos_t) : op == 'e' ? sizeof(fenv_t) : sizeof(fexcept_t)));
    }
}

/* ---- The host call, x86-64 SysV: six integer registers, eight vector registers, then the
 * stack in argument order. Every callee is called through one variadic prototype that fills
 * exactly those: f(g0..g5, d0..d7, s0..s15). A variadic callee gets al = 8, a fixed one
 * ignores it. shortcut: x86-64 only (the ABI's target, CR §1); at most 16 stack slots. */
typedef struct { uint64_t g[6], s[16]; double d[8]; int ng, nd, ns, maxg; } args;

static void slot(args *a, uint64_t x) {
    if (a->ns == 16) die("more than 16 stack slots of arguments", NULL);
    a->s[a->ns++] = x;
}

static void push(args *a, uint32_t bits, int fp, const uint64_t *v) {
    if (fp && bits <= 64) { if (a->nd < 8) memcpy(&a->d[a->nd++], v, 8); else slot(a, v[0]); }
    else if (fp || bits > 128) {   /* long double: 16 bytes, 16-aligned; _BitInt(>128): memory */
        if (fp) a->ns += a->ns & 1;
        for (int k = 0; k < (fp ? 2 : 4); k++) slot(a, v[k]);
    } else if (bits <= 64) {
        uint64_t x = v[0];
        /* shortcut: the callee's signedness is not in the entry; a narrow field is sign-extended
         * (char and short are signed on x86-64), i1 zero-extended. */
        if (bits > 1 && bits < 32 && (x >> (bits - 1)) & 1) x |= ~0ull << bits;
        if (a->ng < a->maxg) a->g[a->ng++] = x; else slot(a, x);
    } else if (a->ng + 2 <= a->maxg) { a->g[a->ng++] = v[0]; a->g[a->ng++] = v[1]; }
    else { a->ns += a->ns & 1; slot(a, v[0]); slot(a, v[1]); }
}

#define G5_ a.g[0], a.g[1], a.g[2], a.g[3], a.g[4]
#define REST_ a.d[0], a.d[1], a.d[2], a.d[3], a.d[4], a.d[5], a.d[6], a.d[7], \
    a.s[0], a.s[1], a.s[2], a.s[3], a.s[4], a.s[5], a.s[6], a.s[7], \
    a.s[8], a.s[9], a.s[10], a.s[11], a.s[12], a.s[13], a.s[14], a.s[15]
#define CALL_(R, fn) ((R (*)(uint64_t, ...))(fn))

/* Calls fn with fields f[0..nf); the result's bits go to out. The prototype never matches the
 * callee's by design, so UBSan's function-type check is off here. */
#if defined(__clang__)
__attribute__((no_sanitize("function")))
#endif
static void host_call(void *fn, const field *f, uint32_t nf, uint32_t rbits, int rfp, uint64_t out[4]) {
    int sret = !rfp && rbits > 128;
    args a = { .maxg = sret ? 5 : 6 };
    for (uint32_t k = 0; k < nf; k++) { uint64_t v[4]; value(&f[k], v); push(&a, f[k].bits, f[k].fp, v); }
    memset(out, 0, 4 * sizeof *out);
    if (sret) { cqrt_u256 r = CALL_(cqrt_u256, fn)(G5_, REST_); memcpy(out, r.l, sizeof r.l); }
    else if (rfp && rbits == 80) { long double r = CALL_(long double, fn)(G5_, a.g[5], REST_); memcpy(out, &r, 10); }
    else if (rfp) { double r = CALL_(double, fn)(G5_, a.g[5], REST_); memcpy(out, &r, 8); }
    else if (rbits > 64) { unsigned __int128 r = CALL_(unsigned __int128, fn)(G5_, a.g[5], REST_); memcpy(out, &r, 16); }
    else out[0] = CALL_(uint64_t, fn)(G5_, a.g[5], REST_);
}

/* Performs e now; out: the callee's result. */
static void perform(event *e, uint64_t out[4]) {
    if (e->kind == EV_FMT) {
        field *f = malloc((e->nf + 1) * sizeof *f);
        if (!f) die("out of memory", NULL);
        f[0] = (field){ 64, 0, 0, { (uint64_t)(uintptr_t)e->text } };
        memcpy(f + 1, e->f, e->nf * sizeof *f);
        host_call((void *)printf, f, e->nf + 1, 0, 0, out);
        free(f);
        return;
    }
    const field *f = e->f;
    uint32_t nf = e->nf;
    void *fn;
    if (!e->text) {
        if (!nf) die("an entry with a null name has no callee field", NULL);
        uint64_t v[4];
        value(f, v);
        fn = (void *)(uintptr_t)v[0];
        f++, nf--;
    } else if (!strcmp(e->text, "__builtin_trap")) __builtin_trap();
    else if (exiting && !strcmp(e->text, "exit")) {
        /* exit from inside an atexit handler is undefined: stdio flushed, then _Exit.
         * shortcut: handlers registered before ours do not run; keep a status and return if that matters. */
        uint64_t v[4] = { 0 };
        if (nf) value(f, v);
        fflush(NULL);
        _Exit((int)v[0]);
    } else if (!(fn = dlsym(RTLD_DEFAULT, e->text))) die("no host function of that name", e->text);
    host_call(fn, f, nf, e->rbits, e->rfp, out);
}

/* The end of the shot: measure the deferred events, perform them in order until an abort flag
 * reads 1. Reentrant: an event is done before it is performed. */
static void end_shot(void) {
    for (; live < nev; live++) {
        event *e = &ev[live];
        if (e->status != ST_DEFERRED) { e->status = ST_DONE; continue; }   /* an unfinished format prints nothing */
        e->status = ST_DONE;
        if (e->kind == EV_ABORT) {
            if (cq_measure_bit(cq_rail_bits(e->pr))) {
                for (live++; live < nev; live++) ev[live].status = ST_DONE;
                return;
            }
            continue;
        }
        if (e->pred < 0 && !cq_measure_bit(cq_rail_bits(e->pr))) continue;
        uint64_t out[4];
        perform(e, out);
    }
}

static void at_exit(void) { exiting = 1; end_shot(); }

static void init(void) {
    if (!inited && atexit(at_exit)) die("atexit failed", NULL);
    inited = 1;
}

static event *new_event(uint8_t kind, uint32_t c, int ctl) {
    init();
    ev = grow(ev, &capev, nev, sizeof *ev);
    event *e = &ev[nev++];
    *e = (event){ .kind = kind, .status = ST_OPEN, .pred = 1 };
    if (ctl) {
        cq_bit *b = rail(c, 1);
        if (!cq_bit_is_q(*b)) e->pred = (int8_t)b->kind;
        else { e->pred = -1; e->pr = cq_rail_alloc(1, NULL); cq_cx(b, cq_rail_bits(e->pr)); }
    }
    return e;
}

/* e is complete. Returns 1 if it was performed now (out: its result). */
static int complete(event *e, uint64_t out[4]) {
    int input = e->kind == EV_EFF && !strcmp(spec_of(e->text), "I"), quantum = e->pred < 0;
    for (uint32_t k = 0; k < e->nf; k++) quantum |= e->f[k].rail && !classical(e->f[k].rail);
    if (e->pred == 0) { e->status = ST_DONE; return 0; }   /* skipped: never performed */
    if (input && quantum)
        die("an INPUT call with a quantum predicate, buffer address, length, stream or other field "
            "is not implemented yet (opcode_quantum_operations-kmgy.28)", e->text);
    if (input || (!quantum && live == (uint32_t)(e - ev))) {
        e->status = ST_DONE;   /* before the call: a noreturn callee re-enters at exit */
        perform(e, out);
        return 1;
    }
    capture(e);
    e->status = ST_DEFERRED;
    return 0;
}

static void advance(void) { while (live < nev && ev[live].status == ST_DONE) live++; }

/* ---- Entry points */
uint32_t cqrt_tape_alloc(void) {
    init();
    tapes = grow(tapes, &captapes, ntapes, sizeof *tapes);
    tapes[ntapes++] = (tape){ 0 };
    return ntapes;
}

static void fmt(uint32_t c, int ctl, uint32_t t, const char *f) {
    if (!f) die("null format", NULL);
    tape *tp = get_tape(t);
    event *e = new_event(EV_FMT, c, ctl);
    e->text = copy_str(f);
    e->need = walk(f, 0, NULL, NULL);
    if (!e->need) { uint64_t out[4]; complete(e, out); advance(); }
    else tp->open = nev;
}
void cqrt_tape_fmt(uint32_t t, const char *f) { fmt(0, 0, t, f); }
void cqrt_tape_fmt_controlled(uint32_t c, uint32_t t, const char *f) { fmt(c, 1, t, f); }

static void append(uint32_t t, field fl) {
    tape *tp = get_tape(t);
    tp->f = grow(tp->f, &tp->cap, tp->nf, sizeof *tp->f);
    tp->f[tp->nf++] = fl;
    if (!tp->open) return;
    event *e = &ev[tp->open - 1];
    if (--e->need) return;
    tp->open = 0;
    e->nf = walk(e->text, 0, NULL, NULL);
    e->f = malloc(e->nf * sizeof *e->f);
    if (!e->f) die("out of memory", NULL);
    memcpy(e->f, tp->f + tp->nf - e->nf, e->nf * sizeof *e->f);
    uint64_t out[4];
    complete(e, out);
    advance();
}

static uint32_t effect(uint32_t c, uint32_t t, const char *name, uint32_t n, uint32_t rbits, int rfp) {
    tape *tp = get_tape(t);
    if (n > tp->nf) die("effect entry asks for more fields than its tape has", name);
    event *e = new_event(EV_EFF, c, 1);
    e->text = copy_str(name);
    e->rbits = rbits, e->rfp = (uint8_t)rfp, e->nf = n;
    e->f = malloc((n ? n : 1) * sizeof *e->f);
    if (!e->f) die("out of memory", NULL);
    memcpy(e->f, tp->f + tp->nf - n, n * sizeof *e->f);
    uint64_t out[4] = { 0 };
    int done = complete(e, out);
    advance();
    if (!rbits) return 0;
    return (uint32_t)cq_rail_alloc(rbits, done ? out : NULL);   /* skipped or deferred: |0> */
}
void cqrt_tape_effect_controlled(uint32_t c, uint32_t t, const char *name, uint32_t n) { effect(c, t, name, n, 0, 0); }

static uint32_t write_rail(uint32_t c, int ctl, uint32_t t, uint32_t h, uint32_t bits, int fp) {
    const cq_bit *cb = ctl ? rail(c, 1) : NULL;
    cq_bit *hb = rail(h, bits);
    int32_t r = cq_rail_alloc(bits, NULL);
    cq_bit *rb = cq_rail_bits(r);
    for (uint32_t i = 0; i < bits; i++) if (cb) cq_ccx(cb, &hb[i], &rb[i]); else cq_cx(&hb[i], &rb[i]);
    append(t, (field){ bits, (uint8_t)fp, r, { 0 } });
    return (uint32_t)r;
}

static void write_lit(uint32_t t, uint32_t bits, int fp, const uint64_t *v) {
    field f = { bits, (uint8_t)fp, 0, { 0 } };
    for (uint32_t i = 0; i < bits; i++) f.v[i / 64] |= v[i / 64] & (1ull << (i % 64));
    append(t, f);
}

#define TAPE_(n, bits, fp, LIT, V) \
    uint32_t cqrt_tape_effect_##n##_controlled(uint32_t c, uint32_t t, const char *name, uint32_t nf) { \
        return effect(c, t, name, nf, bits, fp); } \
    uint32_t cqrt_tape_write_##n(uint32_t t, uint32_t h) { return write_rail(0, 0, t, h, bits, fp); } \
    uint32_t cqrt_tape_write_##n##_controlled(uint32_t c, uint32_t t, uint32_t h) { return write_rail(c, 1, t, h, bits, fp); } \
    void cqrt_tape_write_##n##_lit(uint32_t t, LIT) { V(bits); write_lit(t, bits, fp, v); }
#define V_NARROW_(bits) const uint64_t v[2] = { (uint64_t)lit, (uint64_t)((unsigned __int128)lit >> 64) }
#define V_WIDE_(bits) const uint64_t v[4] = { l0, l1, l2, l3 }
#define V_FP_(bits) uint64_t v[2] = { 0 }; memcpy(v, &lit, (bits) / 8)
#define T_NARROW_(n, bits) TAPE_(n, bits, 0, CQRT_UINT(bits) lit, V_NARROW_)
#define T_WIDE_(n, bits) TAPE_(n, bits, 0, CQRT_LIMBS_, V_WIDE_)
#define T_FP_(n, bits, T) TAPE_(n, bits, 1, T lit, V_FP_)
CQ_WIDTHS_NARROW(T_NARROW_)
CQ_WIDTHS_WIDE(T_WIDE_)
CQ_FP_WIDTHS(T_FP_)

void cqrt_abort_i1(uint32_t f) {
    cq_bit *b = rail(f, 1);
    event *e = new_event(EV_ABORT, 0, 0);
    if (cq_bit_is_q(*b)) { e->pr = (int32_t)f; e->status = ST_DEFERRED; return; }   /* kept by the ABI */
    e->status = ST_DONE;
    advance();
    if (b->kind == CQ_BIT_ZERO) return;
    end_shot();
    exit(EXIT_FAILURE);   /* no performed entry ended the process */
}
