/* Tape and abort, BACKEND_ABI §1.11 (kmgy.22, kmgy.30, kmgy.28). One event sequence per process,
 * in call order. An event is performed at its completion when it is resolved (classical predicate
 * and fields, every cell of a buffer it reads HOST, every earlier event done); otherwise it is
 * deferred, its buffers captured through the bus at the call, and performed at the end of the
 * shot (process exit, or a classical-1 abort flag). An INPUT call is never deferred: classical, it
 * is performed at once (gets: always as fgets at K2); with a quantum predicate, buffer address,
 * length, stream, pread offset or getdelim delimiter it is merged
 * (performed into temporary buffers, its bytes bus-stored and its result XORed under flags). A
 * host call is made with the x86-64 SysV convention by name (dlsym), its fields as arguments; a
 * format entry is the host printf.
 *
 * Backend limits the ABI permits (§1.11 Limits) fail loudly, each with its own message (LIMIT):
 * ungetc's quantum character, an INPUT call's quantum format or va_list pointer, positional %n$,
 * more than 16 stack slots, a rail with too many quantum bits to enumerate. What the ABI does not
 * rule aborts naming opcode_quantum_operations-kmgy.43: a deferred v-form's va_list pointer that
 * is a quantum rail. */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <fcntl.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "cq_bus.h"
#include "cq_config.h"
#include "cq_rt.h"
#include "cqrt.h"
#include "kernels.h"

#define LIMIT ": not implemented, a backend limit ABI §1.11 Limits permits"

static void die(const char *msg, const char *what) {
    fprintf(stderr, "cq_runtime: tape: %s%s%s\n", msg, what ? ": " : "", what ? what : "");
    abort();
}

/* A field: a classical value (rail 0) or a tape rail h'. fp: the width token is a float. */
typedef struct { uint32_t bits; uint8_t fp; int32_t rail; uint64_t v[4]; } field;

/* A captured buffer: n bytes; q[j] a tape rail holding byte j (XORed in when performed), or 0. */
typedef struct { uint8_t *b; int32_t *q; uint32_t n; } cbuf;

enum { EV_FMT, EV_EFF, EV_ABORT };
enum { ST_OPEN, ST_DONE, ST_DEFERRED };
typedef struct {
    uint8_t kind, status, rfp;
    int8_t  pred;          /* 0 or 1, or -1: quantum, held in rail pr */
    int32_t pr;            /* the captured predicate, or the abort flag */
    int32_t r;             /* the result rail, 0: none */
    char   *text;          /* fmt, or the callee name (NULL: the address in field 0) */
    field  *f;
    uint32_t nf, need, rbits;   /* need: fields a FMT still waits for; rbits: result width, 0 none */
    cbuf   *cb;
    uint32_t ncb, capcb;
    uint8_t lk, mk;        /* a rail length captured at clen bytes: its field + 1 (mk: nmemb of size·nmemb) */
    uint64_t clen;
} event;

typedef struct { field *f; uint32_t nf, cap; uint32_t open; } tape;   /* open: FMT event + 1, or 0 */

static tape   *tapes;
static uint32_t ntapes, captapes;
static event  *ev;
static uint32_t nev, capev, live;    /* live: the first event not done */
static int     inited, exiting;
static const cq_bit ZERO_ = { CQ_BIT_ZERO, 0 }, ONE_ = { CQ_BIT_ONE, 0 };

static void *grow(void *p, uint32_t *cap, uint32_t n, size_t sz) {
    if (n < *cap) return p;
    *cap = *cap ? 2 * *cap : 16;
    if (!(p = realloc(p, *cap * sz))) die("out of memory", NULL);
    return p;
}

static void *zalloc(size_t n, size_t sz) {
    void *p = calloc(n ? n : 1, sz);
    if (!p) die("out of memory", NULL);
    return p;
}

static char *copy_str(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    return memcpy(zalloc(n, 1), s, n);
}

static cq_bit *rail(uint32_t h, uint32_t w) {
    if (cq_rail_width((int32_t)h) != w) die("rail width does not match the call", NULL);
    return cq_rail_bits((int32_t)h);
}

static tape *get_tape(uint32_t t) {
    if (t == 0 || t > ntapes) die("bad tape handle", NULL);
    return &tapes[t - 1];
}

static int anyq(const cq_bit *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (cq_bit_is_q(b[i])) return 1;
    return 0;
}
static int classical(int32_t h) { return !anyq(cq_rail_bits(h), cq_rail_width(h)); }

/* n constant bits of v, zero-extended (n <= 256). */
static void kbits(cq_bit *b, uint32_t n, uint64_t v) {
    for (uint32_t i = 0; i < n; i++) b[i] = cq_bit_const(i < 64 && (v >> i) & 1);
}
static uint64_t low64(const cq_bit *b, uint32_t n) {   /* the classical bits; a qubit reads 0 */
    uint64_t v = 0;
    for (uint32_t i = 0; i < n && i < 64; i++) v |= (uint64_t)(b[i].kind == CQ_BIT_ONE) << i;
    return v;
}

/* A field's value; a rail is measured (classical bits read as they are). */
static void value(const field *f, uint64_t v[4]) {
    if (!f->rail) { memcpy(v, f->v, sizeof f->v); return; }
    memset(v, 0, 4 * sizeof *v);
    cq_bit *b = cq_rail_bits(f->rail);
    for (uint32_t i = 0; i < f->bits; i++) if (cq_measure_bit(&b[i])) v[i / 64] |= 1ull << (i % 64);
}
/* The value of a field's classical bits, without measuring. */
static void cval(const field *f, uint64_t v[4]) {
    if (!f->rail) { memcpy(v, f->v, sizeof f->v); return; }
    memset(v, 0, 4 * sizeof *v);
    cq_bit *b = cq_rail_bits(f->rail);
    for (uint32_t i = 0; i < f->bits; i++) if (b[i].kind == CQ_BIT_ONE) v[i / 64] |= 1ull << (i % 64);
}
static uint64_t fval(const field *f) { uint64_t v[4]; cval(f, v); return v[0]; }
static int fq(const field *f) { return f->rail && !classical(f->rail); }
/* The bits of a field: its rail's, or tmp[256] holding its value. */
static const cq_bit *fbits(const field *f, cq_bit *tmp) {
    if (f->rail) return cq_rail_bits(f->rail);
    for (uint32_t i = 0; i < f->bits; i++) tmp[i] = cq_bit_const((f->v[i / 64] >> (i % 64)) & 1);
    return tmp;
}
static void set_ptr(field *f, const void *p) { f->rail = 0; memset(f->v, 0, sizeof f->v); f->v[0] = (uint64_t)(uintptr_t)p; }

/* ---- Flags: tape bits, cleared by the tape's undo. */
static const cq_bit *and_(cq_tape *T, const cq_bit *a, const cq_bit *b) {
    if (!cq_bit_is_q(*a)) return a->kind ? b : &ZERO_;
    if (!cq_bit_is_q(*b)) return b->kind ? a : &ZERO_;
    cq_bit *t = cq_tape_bits(T, 1);
    cq_tape_ccx(T, a, b, t);
    return t;
}
/* [x pred y] (negated if neg) AND g; x, y n bits. */
static const cq_bit *cmp_(cq_tape *T, int pred, const cq_bit *x, const cq_bit *y, uint32_t n, int neg, const cq_bit *g) {
    cq_bit *f = cq_tape_bits(T, 1);
    if (neg) cq_tape_x(T, f);
    cq_cmp(cq_cmp_kind(pred, n), T, pred, x, y, n, f, 1);
    return and_(T, f, g);
}
/* [x == v] AND g, x n <= 64 bits; k holds v's bits until T is undone (the tape keeps pointers). */
static const cq_bit *eq_(cq_tape *T, const cq_bit *x, uint32_t n, uint64_t v, const cq_bit *g, cq_bit *k) {
    kbits(k, n, v);
    return cmp_(T, CQ_CMP_EQ, x, k, n, 0, g);
}

/* The values a rail of n <= 64 bits can take: its classical bits and every assignment of its q
 * qubits, the bound R of ABI §1.10 "No fault". */
typedef struct { uint64_t base; uint32_t pos[64], q; } reach;
static reach reach_of(const cq_bit *x, uint32_t n) {
    reach r = { 0 };
    for (uint32_t i = 0; i < n; i++)
        if (cq_bit_is_q(x[i])) r.pos[r.q++] = i; else r.base |= (uint64_t)x[i].kind << i;
    if (r.q > CQ_MAX_QADDR_BITS) die("a rail with more quantum bits than CQ_MAX_QADDR_BITS to enumerate, the build-time cap" LIMIT, NULL);
    return r;
}
static uint64_t reach_at(const reach *r, uint64_t a) {
    uint64_t v = r->base;
    for (uint32_t k = 0; k < r->q; k++) v |= (a >> k & 1) << r->pos[k];
    return v;
}
static uint64_t reach_max(const cq_bit *x, uint32_t n) {   /* the largest value, saturating */
    uint64_t v = 0;
    for (uint32_t i = 0; i < n; i++)
        if (cq_bit_is_q(x[i]) || x[i].kind) { if (i >= 64) return UINT64_MAX; v |= 1ull << i; }
    return v;
}

/* ---- Formats: the arguments a printf/wprintf format consumes, in order. cls: 'i' an integer or
 * pointer, 'd' a double, 'L' a long double, 's' a string, 'S' a wide string; prec: a literal
 * precision, else -1. Positional %n$ fails loudly (§1.11 Limits): POSITIONAL
 * is at a conversion's first character; a format numbers all its conversions or none. */
typedef void (*arg_fn)(void *ctx, char cls, long prec);

static int in(const char *set, uint32_t c) { return c && c < 128 && strchr(set, (int)c); }

#define AT(i) (wide ? (uint32_t)((const wchar_t *)p)[i] : (uint32_t)((const unsigned char *)p)[i])
#define POSITIONAL(i) for (size_t j_ = (i); AT(j_) >= '0' && AT(j_) <= '9'; j_++) \
    if (AT(j_ + 1) == '$') die("positional %n$ conversions" LIMIT, NULL)
static uint32_t walk(const void *p, int wide, arg_fn arg, void *ctx) {
#define ARG(c, pr) do { n++; if (arg) arg(ctx, c, pr); } while (0)
    uint32_t n = 0;
    for (size_t i = 0; AT(i); i++) {
        if (AT(i) != '%' || AT(++i) == '%') continue;
        POSITIONAL(i);
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
}

/* ---- Capture (§1.11 "Capture"): the buffers a deferred entry reads, read through the bus when
 * the entry is complete. A HOST cell is its byte, a QUANTUM cell is XOR-copied under the entry's
 * predicate into a tape rail, measured when the entry is performed. */
static event *cev;              /* the entry being captured */
static const cq_bit *cpred;     /* its predicate */
static int cqcell;              /* a QUANTUM cell was met */

static uint32_t new_buf(uint64_t n) {
    if (n > UINT32_MAX - 8) die("captured buffer too large", cev->text);
    cev->cb = grow(cev->cb, &cev->capcb, cev->ncb, sizeof *cev->cb);
    cev->cb[cev->ncb] = (cbuf){ zalloc(n + 8, 1), zalloc(n, sizeof(int32_t)), (uint32_t)n };   /* + 8: a string's 0 */
    return cev->ncb++;
}

/* Byte j of buffer k ^= the cell bits x under g. */
static void put(uint32_t k, uint64_t j, const cq_bit *x, const cq_bit *g) {
    cbuf *b = &cev->cb[k];
    if (!cq_bit_is_q(*g) && !anyq(x, 8)) {
        if (g->kind) for (int i = 0; i < 8; i++) b->b[j] ^= (uint8_t)(x[i].kind << i);
        return;
    }
    if (!b->q[j]) b->q[j] = cq_rail_alloc(8, NULL);
    cq_bit *r = cq_rail_bits(b->q[j]);
    for (int i = 0; i < 8; i++) cq_ccx(g, &x[i], &r[i]);
}

/* What to capture at an address: u > 0 a string of u-byte units, up to its first HOST zero unit
 * or len units; else len bytes, and with n (nw bits) byte j under [j < n] only. */
typedef struct { uint32_t u; uint64_t len; const cq_bit *n; uint32_t nw; } ext;

enum { CHUNK = 256 };
static void cap_run(uint32_t k, uint64_t a, ext x, const cq_bit *g) {
    cq_bit c[8 * CHUNK];
    if (x.u) {
        for (uint64_t j = 0; j < x.len; j++) {
            int q = cq_cells_read(a + j * x.u, c, x.u), zero = !q;
            for (uint32_t i = 0; i < 8 * x.u; i++) zero &= !c[i].kind;
            cqcell |= q;
            if (zero) return;
            for (uint32_t i = 0; i < x.u; i++) put(k, j * x.u + i, &c[8 * i], g);
        }
        return;
    }
    for (uint64_t j = 0; j < x.len; j += CHUNK) {
        uint32_t m = x.len - j < CHUNK ? (uint32_t)(x.len - j) : CHUNK;
        cqcell |= cq_cells_read(a + j, c, m);
        for (uint32_t i = 0; i < m; i++) {
            if (!x.n) { put(k, j + i, &c[8 * i], g); continue; }
            cq_tape T = { 0 };
            cq_bit jb[256];
            kbits(jb, x.nw, j + i);
            put(k, j + i, &c[8 * i], cmp_(&T, CQ_CMP_ULT, jb, x.n, x.nw, 0, g));
            cq_tape_undo(&T);
        }
    }
}

/* Captures at the 64 address bits A into a new buffer; its bytes, or NULL at a classical 0. At an
 * address rail, every address it can reach under the flag [A == a] (the _controlled load of
 * §1.10 per cell). */
static uint8_t *cap_at(const cq_bit *A, ext x) {
    if (!anyq(A, 64) && !low64(A, 64)) return NULL;
    uint32_t k = new_buf(x.u ? x.len * x.u : x.len);
    if (!anyq(A, 64)) cap_run(k, low64(A, 64), x, cpred);
    else {
        reach r = reach_of(A, 64);
        for (uint64_t i = 0; i < 1ull << r.q; i++) {
            cq_tape T = { 0 };
            cq_bit kb[64];
            uint64_t a = reach_at(&r, i);
            cap_run(k, a, x, eq_(&T, A, 64, a, cpred, kb));
            cq_tape_undo(&T);
        }
    }
    return cev->cb[k].b;
}

static field *fld(event *e, uint32_t k) {
    if (k >= e->nf) die("entry has fewer fields than its callee reads", e->text);
    return &e->f[k];
}

/* Captures the buffer at field k; the field then holds its copy's address. */
static uint8_t *cap_field(uint32_t k, ext x) {
    field *f = fld(cev, k);
    cq_bit t[256];
    if (f->bits != 64) die("a buffer field is not a 64-bit address", cev->text);
    uint8_t *p = cap_at(fbits(f, t), x);
    set_ptr(f, p);
    return p;
}

static ext str_ext(int wide, long prec) {
    return (ext){ wide ? (uint32_t)sizeof(wchar_t) : 1, prec >= 0 && prec < CQ_K2 ? (uint64_t)prec : CQ_K2, NULL, 0 };
}

/* A string as host code reads it (CR rule 16): its host bytes, at a QUANTUM cell the host byte
 * under the overlay, up to its first zero unit or K2 units; an unreadable unit reads 0. The
 * caller frees it. */
static void *str_host(uint64_t a, uint32_t u) {
    uint8_t *s = zalloc((size_t)CQ_K2 + 1, u);
    for (uint64_t j = 0; a && j < CQ_K2; j++) {
        uint8_t *c = s + j * u;
        if (!cq_host_read(a + j * u, c, u)) { memset(c, 0, u); break; }
        int zero = 1;
        for (uint32_t i = 0; i < u; i++) zero &= !c[i];
        if (zero) break;
    }
    return s;
}

/* A format a deferred entry reads: captured as a string; its conversions, which decide what else
 * is captured, are read from its host bytes at the call (§1.11 Capture). Those bytes are
 * unspecified at a QUANTUM cell (here: the host byte under the overlay) and for a format at an
 * address rail (here: at the address with its qubits read as 0). The caller frees the result. */
static void *cap_fmt(uint32_t k, int wide) {
    cq_bit t[256];
    field *f = fld(cev, k);
    void *h = str_host(low64(fbits(f, t), f->bits), wide ? (uint32_t)sizeof(wchar_t) : 1);
    cap_field(k, str_ext(wide, -1));
    return h;
}

/* The %s arguments of a format at field k, its arguments from field k + 1. */
typedef struct { event *e; uint32_t k; } fmt_ctx;
static void cap_fmt_arg(void *ctx, char cls, long prec) {
    fmt_ctx *c = ctx;
    uint32_t k = c->k++;
    if (cls == 's' || cls == 'S') cap_field(k, str_ext(cls == 'S', prec));
}

/* A va_list (x86-64 SysV) and the slots its format takes, copied into a fresh va_list whose
 * arguments all sit in its overflow area, as the lowered caller's do (CR rule 16). */
typedef struct { uint32_t gp, fp; uint64_t over, regs; } valist;
/* Its 24 bytes as host code reads them: at a QUANTUM cell unspecified (§1.11 Capture), here the
 * host byte under the overlay; unreadable: 0. */
static void rd_va(uint64_t a, valist *v) {
    cq_bit c[8 * sizeof *v];
    cqcell |= cq_cells_read(a, c, sizeof *v);
    if (!cq_host_read(a, (uint8_t *)v, sizeof *v)) memset(v, 0, sizeof *v);
}
typedef struct { valist *src; uint32_t k, n; } va_ctx;   /* k: the area buffer, n: slots used */
static void cap_va_arg(void *ctx, char cls, long prec) {
    va_ctx *c = ctx;
    valist *s = c->src;
    if (c->n + 3 > CQ_K2) die("va_list format takes too many arguments", NULL);
    uint64_t at;
    uint32_t len = 8;
    if (cls == 'L') { s->over = (s->over + 15) & ~15ull; c->n += c->n & 1; at = s->over; s->over += 16; len = 16; }
    else if (cls == 'd' && s->fp < 176) { at = s->regs + s->fp; s->fp += 16; }
    else if (cls != 'd' && s->gp < 48) { at = s->regs + s->gp; s->gp += 8; }
    else { at = s->over; s->over += 8; }
    cq_bit b[8 * 16];
    cqcell |= cq_cells_read(at, b, len);
    if (cls == 's' || cls == 'S') {   /* the pointer's 64 bits, qubits included, are the string's address */
        uint64_t p = (uint64_t)(uintptr_t)cap_at(b, str_ext(cls == 'S', prec));
        memcpy(&cev->cb[c->k].b[8 * c->n], &p, 8);
    } else for (uint32_t i = 0; i < len; i++) put(c->k, 8 * c->n + i, &b[8 * i], cpred);
    c->n += len / 8;
}

/* What a deferred entry of a known OUTPUT callee reads. spec: s K string, w K wide string,
 * f K format and its arguments, F K wide format, v K format and the va_list at K + 1, V K wide,
 * b K L buffer of length L, m K L M buffer of length L·M, P K an fpos_t, e K an fenv_t,
 * x K an fexcept_t. */
static const struct { const char *name, *spec; } callees[] = {
    { "printf", "f0" }, { "fprintf", "f1" }, { "vprintf", "v0" }, { "vfprintf", "v1" },
    { "wprintf", "F0" }, { "fwprintf", "F1" }, { "vwprintf", "V0" }, { "vfwprintf", "V1" },
    { "puts", "s0" }, { "fputs", "s0" }, { "perror", "s0" }, { "fputws", "w0" },
    { "fwrite", "m012" }, { "write", "b12" }, { "fsetpos", "P1" },
    { "remove", "s0" }, { "unlink", "s0" }, { "rename", "s0s1" }, { "fopen", "s0s1" }, { "open", "s0" },
    { "__assert_fail", "s0s1s3" }, { "fesetenv", "e0" }, { "feupdateenv", "e0" }, { "fesetexceptflag", "x0" },
};

/* name is base or its _unlocked form (an _unlocked form is its locked one). */
static int named(const char *name, const char *base) {
    if (!name) return 0;
    size_t n = strlen(name), u = strlen("_unlocked");
    if (n > u && !strcmp(name + n - u, "_unlocked")) n -= u;
    return strlen(base) == n && !strncmp(base, name, n);
}

static const char *spec_of(const char *name) {
    for (size_t i = 0; i < sizeof callees / sizeof *callees; i++)
        if (named(name, callees[i].name)) return callees[i].spec;
    return "";
}

static void capture(event *e) {
    cev = e, cpred = e->pred < 0 ? cq_rail_bits(e->pr) : &ONE_, cqcell = 0;
    if (e->kind == EV_FMT) { fmt_ctx c = { e, 0 }; walk(e->text, 0, cap_fmt_arg, &c); return; }
    for (const char *s = spec_of(e->text); *s; ) {
        char op = *s++;
        uint32_t k = (uint32_t)(*s++ - '0');
        if (op == 's' || op == 'w') cap_field(k, str_ext(op == 'w', -1));
        else if (op == 'f' || op == 'F') {
            void *p = cap_fmt(k, op == 'F');
            fmt_ctx c = { e, k + 1 };
            walk(p, op == 'F', cap_fmt_arg, &c);
            free(p);
        } else if (op == 'v' || op == 'V') {
            void *p = cap_fmt(k, op == 'V');
            field *ap = fld(e, k + 1);
            if (fq(ap)) die("a deferred v-form's va_list pointer that is a quantum rail: not defined by ABI §1.11 (opcode_quantum_operations-kmgy.43)", e->text);
            if (fval(ap)) {
                valist src;
                rd_va(fval(ap), &src);
                va_ctx c = { &src, new_buf(8 * (uint64_t)CQ_K2), 0 };
                walk(p, op == 'V', cap_va_arg, &c);
                uint32_t kv = new_buf(sizeof(valist));
                *(valist *)(void *)e->cb[kv].b = (valist){ 48, 176, (uint64_t)(uintptr_t)e->cb[c.k].b, 0 };
                set_ptr(ap, e->cb[kv].b);
            }
            free(p);
        } else if (op == 'b' || op == 'm') {
            uint32_t L = (uint32_t)(*s++ - '0'), M = op == 'm' ? (uint32_t)(*s++ - '0') : 0;
            field *fl = fld(e, L), *fm = op == 'm' ? fld(e, M) : NULL;
            cq_tape T = { 0 };
            cq_bit tl[256], tm[256], P[256];
            ext x = { 0 };
            if (!fq(fl) && (!fm || !fq(fm))) x.len = fval(fl) * (fm ? fval(fm) : 1);
            else {   /* a rail length: K2 bytes at most, byte j under [j < n] (Capture) */
                const cq_bit *bl = fbits(fl, tl);
                uint64_t mx = reach_max(bl, fl->bits);
                x.n = bl, x.nw = fl->bits;
                if (fm) {   /* n = size·nmemb, exact */
                    const cq_bit *bm = fbits(fm, tm), *o[256];
                    uint64_t mm = reach_max(bm, fm->bits);
                    mx = mm && mx > UINT64_MAX / mm ? UINT64_MAX : mx * mm;
                    x.nw = fl->bits + fm->bits;
                    cq_mul(cq_mul_kind(fl->bits), &T, bl, fl->bits, bm, fm->bits, 0, 0, x.nw, o);
                    for (uint32_t i = 0; i < x.nw; i++) P[i] = *o[i];   /* read-only views of the product */
                    x.n = P;
                }
                x.len = mx < CQ_K2 ? mx : CQ_K2;
                e->lk = (uint8_t)(L + 1), e->mk = (uint8_t)(fm ? M + 1 : 0), e->clen = x.len;
            }
            cap_field(k, x);
            cq_tape_undo(&T);
        } else cap_field(k, (ext){ 0, op == 'P' ? sizeof(fpos_t) : op == 'e' ? sizeof(fenv_t) : sizeof(fexcept_t), NULL, 0 });
    }
}

static void drop_bufs(event *e) {
    for (uint32_t k = 0; k < e->ncb; k++) { free(e->cb[k].b); free(e->cb[k].q); }
    e->ncb = 0;
}

/* ---- The host call, x86-64 SysV: six integer registers, eight vector registers, then the
 * stack in argument order. Every callee is called through one variadic prototype that fills
 * exactly those: f(g0..g5, d0..d7, s0..s15). A variadic callee gets al = 8, a fixed one
 * ignores it. shortcut: x86-64 only (the ABI's target, CR §1). At most 16 stack slots (§1.11 Limits). */
typedef struct { uint64_t g[6], s[16]; double d[8]; int ng, nd, ns, maxg; } args;

static void slot(args *a, uint64_t x) {
    if (a->ns == 16) die("a host call with more than 16 stack slots of arguments" LIMIT, NULL);
    a->s[a->ns++] = x;
}

static void push(args *a, uint32_t bits, int fp, const uint64_t *v) {
    if (fp && bits <= 64) { if (a->nd < 8) memcpy(&a->d[a->nd++], v, 8); else slot(a, v[0]); }
    else if (fp || bits > 128) {   /* long double: 16 bytes, 16-aligned; _BitInt(>128): memory */
        if (fp) a->ns += a->ns & 1;
        for (int k = 0; k < (fp ? 2 : 4); k++) slot(a, v[k]);
    } else if (bits <= 64) {
        uint64_t x = v[0];
        /* The callee's signedness is not in the entry: a field narrower than 32 bits is
         * sign-extended, an i1 zero-extended (§1.11 Resolved). */
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

/* The streams a FILE* value can name (§1.11, the identifier row): the standard three and every
 * FILE an entry this tape performed returned, until a performed fclose. The host C library made
 * every FILE the program holds (§1.11 Resolved), each through such an entry. */
static uint64_t *streams;
static uint32_t nstreams, capstreams;
static const char *const openers[] = { "fopen", "fdopen", "freopen", "tmpfile", "popen", "fmemopen", "open_memstream" };
static void track(const event *e, const uint64_t out[4]) {
    if (!e->text) return;
    for (size_t i = 0; i < sizeof openers / sizeof *openers; i++)
        if (named(e->text, openers[i]) && out[0]) {
            streams = grow(streams, &capstreams, nstreams, sizeof *streams);
            streams[nstreams++] = out[0];
        }
    if ((named(e->text, "fclose") || named(e->text, "pclose")) && e->nf)
        for (uint32_t i = 0; i < nstreams; i++) if (streams[i] == fval(&e->f[0])) streams[i] = 0;
}
static int is_open(uint64_t v, uint32_t bits) {
    if (bits <= 32) return (int32_t)v >= 0 && fcntl((int)(int32_t)v, F_GETFD) != -1;
    if (!v) return 0;
    if (v == (uintptr_t)stdin || v == (uintptr_t)stdout || v == (uintptr_t)stderr) return 1;
    for (uint32_t i = 0; i < nstreams; i++) if (streams[i] == v) return 1;
    return 0;
}

/* Performs e now; out: the callee's result. A deferred entry's captured bytes are measured first. */
static void perform(event *e, uint64_t out[4]) {
    for (uint32_t k = 0; k < e->ncb; k++)
        for (uint32_t j = 0; j < e->cb[k].n; j++)
            if (e->cb[k].q[j]) {
                cq_bit *r = cq_rail_bits(e->cb[k].q[j]);
                for (int i = 0; i < 8; i++) e->cb[k].b[j] ^= (uint8_t)(cq_measure_bit(&r[i]) << i);
                e->cb[k].q[j] = 0;
            }
    if (e->lk) {   /* a rail length captured at clen bytes: the call reads no more (a stated cut at K2) */
        field *l = &e->f[e->lk - 1], *m = e->mk ? &e->f[e->mk - 1] : NULL;
        value(l, l->v), l->rail = 0;
        if (m) value(m, m->v), m->rail = 0;
        if (!m && l->v[0] > e->clen) l->v[0] = e->clen;
        if (m && l->v[0] && m->v[0] > e->clen / l->v[0]) m->v[0] = e->clen / l->v[0];
    }
    if (e->kind == EV_FMT) {
        field *f = zalloc(e->nf + 1, sizeof *f);
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
        /* A deferred exit performed at the end of the shot (process exit or a classical-1 abort) is
         * fflush(NULL) then _Exit(code) (§1.11 Deferred); exit inside an atexit handler is undefined. */
        uint64_t v[4] = { 0 };
        if (nf) value(f, v);
        fflush(NULL);
        _Exit((int)v[0]);
    } else if (!(fn = dlsym(RTLD_DEFAULT, e->text))) die("no host function of that name", e->text);
    host_call(fn, f, nf, e->rbits, e->rfp, out);
    track(e, out);
}

/* Performed at its call: the result into r. */
static void perform_now(event *e) {
    uint64_t out[4];
    e->status = ST_DONE;   /* before the call: a noreturn callee re-enters at exit */
    perform(e, out);
    if (!e->r) return;
    cq_bit *r = cq_rail_bits(e->r);
    for (uint32_t i = 0; i < e->rbits; i++) r[i] = cq_bit_const((out[i / 64] >> (i % 64)) & 1);
}

/* ---- INPUT calls (§1.11 host-boundary table, the INPUT rows). cls: what the call writes; id:
 * the stream field (-1: stdin); other: the field of the pread offset, getdelim delimiter and
 * ungetc character rows (-1: none). */
enum { IN_NONE, IN_FGETS, IN_FREAD, IN_READ, IN_GETS, IN_GETLINE, IN_FGETLN, IN_SCANF };
static const struct { const char *name; uint8_t cls; int8_t id, other; uint8_t wide, va; } inputs[] = {
    { "fgetc", IN_NONE, 0, -1, 0, 0 }, { "getc", IN_NONE, 0, -1, 0, 0 }, { "getchar", IN_NONE, -1, -1, 0, 0 },
    { "fgetwc", IN_NONE, 0, -1, 0, 0 }, { "getwc", IN_NONE, 0, -1, 0, 0 }, { "getwchar", IN_NONE, -1, -1, 0, 0 },
    { "getw", IN_NONE, 0, -1, 0, 0 }, { "ungetc", IN_NONE, 1, 0, 0, 0 }, { "ungetwc", IN_NONE, 1, 0, 0, 0 },
    { "feof", IN_NONE, 0, -1, 0, 0 }, { "ferror", IN_NONE, 0, -1, 0, 0 }, { "clearerr", IN_NONE, 0, -1, 0, 0 },
    { "fgets", IN_FGETS, 2, -1, 0, 0 }, { "fgetws", IN_FGETS, 2, -1, 1, 0 }, { "fread", IN_FREAD, 3, -1, 0, 0 },
    { "read", IN_READ, 0, -1, 0, 0 }, { "pread", IN_READ, 0, 3, 0, 0 }, { "gets", IN_GETS, -1, -1, 0, 0 },
    { "getline", IN_GETLINE, 2, -1, 0, 0 }, { "getdelim", IN_GETLINE, 3, 2, 0, 0 }, { "fgetln", IN_FGETLN, 0, -1, 0, 0 },
    { "scanf", IN_SCANF, -1, -1, 0, 0 }, { "fscanf", IN_SCANF, 0, -1, 0, 0 },
    { "vscanf", IN_SCANF, -1, -1, 0, 1 }, { "vfscanf", IN_SCANF, 0, -1, 0, 1 },
    { "wscanf", IN_SCANF, -1, -1, 1, 0 }, { "fwscanf", IN_SCANF, 0, -1, 1, 0 },
    { "vwscanf", IN_SCANF, -1, -1, 1, 1 }, { "vfwscanf", IN_SCANF, 0, -1, 1, 1 },
};
static int input_of(const char *name) {
    for (int i = 0; i < (int)(sizeof inputs / sizeof *inputs); i++) if (named(name, inputs[i].name)) return i;
    return -1;
}

static void setv(field *a, uint64_t v) { memset(a->v, 0, sizeof a->v); a->v[0] = v; }

/* r ^= v under g; r ^= x under g (x n bits). */
static void res_k(event *e, const cq_bit *g, const uint64_t *v) {
    for (uint32_t i = 0; e->r && i < e->rbits; i++) if ((v[i / 64] >> (i % 64)) & 1) cq_cx(g, &cq_rail_bits(e->r)[i]);
}
static void res_q(event *e, const cq_bit *g, const cq_bit *x, uint32_t n) {
    for (uint32_t i = 0; e->r && i < e->rbits && i < n; i++) cq_ccx(g, &x[i], &cq_rail_bits(e->r)[i]);
}

/* One bus store (§1.10) of the byte bits v at A + off under g. At an address rail: the flag
 * [A == a] per address it can reach, one tape slot for them all (Impl, address rail). */
static void store1(const cq_bit *A, uint64_t off, cq_bit *v, const cq_bit *g) {
    if (!cq_bit_is_q(*g) && !g->kind) return;
    if (!anyq(A, 64)) { cq_bus_store_at(g, low64(A, 64) + off, v, 8, NULL); return; }
    cq_bit *t = cq_rail_bits(cq_rail_alloc(8, NULL));
    reach r = reach_of(A, 64);
    for (uint64_t i = 0; i < 1ull << r.q; i++) {
        cq_tape T = { 0 };
        cq_bit kb[64];
        uint64_t a = reach_at(&r, i);
        cq_bus_store_at(eq_(&T, A, 64, a, g, kb), a + off, v, 8, t);
        cq_tape_undo(&T);
    }
}
static void store_k(const cq_bit *A, uint64_t off, const void *b, uint64_t n, const cq_bit *g) {
    for (uint64_t j = 0; j < n; j++) {
        cq_bit v[8];
        kbits(v, 8, ((const uint8_t *)b)[j]);
        store1(A, off + j, v, g);
    }
}

/* The conversions of a scanf format that store through a pointer. kind 'f': sz bytes; 'n': %n,
 * sz bytes; 's': a string of sz-byte units; 'c': exactly sz bytes. tsz: the temporary's size.
 * The format is rewritten into o with a width K2 - 1 on each unbounded %s and %[, so the
 * temporary holds it (a cut at K2, as gets has). Positional %n$ fails loudly, as in walk. */
typedef struct { char kind; uint32_t sz, tsz; } conv;
static uint32_t scan_walk(const void *p, int wide, conv *cv, void *o) {
#define PUT(c) (wide ? (void)(((wchar_t *)o)[no++] = (wchar_t)(c)) : (void)(((char *)o)[no++] = (char)(c)))
    uint32_t n = 0;
    size_t no = 0;
    for (size_t i = 0; AT(i);) {
        uint32_t ch = AT(i++);
        PUT(ch);
        if (ch != '%') continue;
        if (AT(i) == '%') { PUT(AT(i++)); continue; }
        POSITIONAL(i);
        int sup = AT(i) == '*';
        if (sup) PUT(AT(i++));
        long w = -1;
        for (; AT(i) >= '0' && AT(i) <= '9'; i++) { w = (w < 0 ? 0 : w * 10) + (long)(AT(i) - '0'); PUT(AT(i)); }
        uint32_t c = AT(i);
        for (size_t j = i; in("hlLjztq", AT(j)); j++) c = AT(j + 1);
        if (w < 0 && !sup && (c == 's' || c == 'S' || c == '[')) {
            char d[16];
            for (char *q = d + snprintf(d, sizeof d, "%d", CQ_K2 - 1), *r = d; r < q; r++) PUT(*r);
        }
        int h = 0, l = 0, L = 0;
        for (; in("hlLjztq", AT(i)); i++) { h += AT(i) == 'h'; l += AT(i) != 'h' && AT(i) != 'L'; L |= AT(i) == 'L'; PUT(AT(i)); }
        if (!c) break;
        PUT(AT(i++));
        if (c == '[') {
            if (AT(i) == '^') PUT(AT(i++));
            if (AT(i) == ']') PUT(AT(i++));
            while (AT(i) && AT(i) != ']') PUT(AT(i++));
            if (AT(i)) PUT(AT(i++));
        }
        if (sup) continue;
        uint32_t u = l || c == 'C' || c == 'S' ? (uint32_t)sizeof(wchar_t) : 1;
        conv k = { 'f', 0, 16 };
        if (in("diouxXn", c)) { k.kind = c == 'n' ? 'n' : 'f'; k.sz = h >= 2 ? 1 : h ? 2 : l || L ? 8 : 4; }
        else if (in("aAeEfFgG", c)) k.sz = L ? 10 : l ? 8 : 4;
        else if (c == 'p') k.sz = 8;
        else if (c == 'c' || c == 'C') { k.kind = 'c'; k.sz = k.tsz = (uint32_t)(w < 0 ? 1 : w) * u; }
        else if (c == 's' || c == 'S' || c == '[') { k.kind = 's'; k.sz = u; k.tsz = (uint32_t)((w < 0 ? CQ_K2 - 1 : w) + 1) * u; }
        else continue;
        if (n == CQ_K2) die("scanf format takes too many arguments", NULL);
        cv[n++] = k;
    }
    PUT(0);
    return n;
#undef PUT
}
#undef POSITIONAL
#undef AT
static size_t u_len(const void *p, uint32_t u) { return u == 1 ? strlen(p) : wcslen(p); }

/* One INPUT call on stream value idv into temporary buffers; its bytes are bus-stored and its
 * result merged under g. The rows compose: a quantum predicate is in g; a quantum stream gives
 * one g per open value; a quantum buffer address is the store's address rail; a quantum length
 * is a per-byte flag. */
static void run(event *e, int ix, uint64_t idv, const cq_bit *g, void *fn) {
    field *a = zalloc(e->nf, sizeof *a), *f = e->f;
    for (uint32_t k = 0; k < e->nf; k++) { a[k] = (field){ f[k].bits, f[k].fp, 0, { 0 } }; cval(&f[k], a[k].v); }
    if (inputs[ix].id >= 0) setv(&a[inputs[ix].id], idv);
    uint64_t out[4];
    cq_bit t0[256], t1[256], t2[256];
    void *tmp = NULL;
    switch (inputs[ix].cls) {
    case IN_NONE:
        host_call(fn, a, e->nf, e->rbits, e->rfp, out);
        res_k(e, g, out);
        break;
    case IN_FGETS: {   /* fgets(s, n, f): the row of n, ABI:516 */
        uint32_t u = inputs[ix].wide ? (uint32_t)sizeof(wchar_t) : 1, nw = fld(e, 1)->bits;
        const cq_bit *S = fbits(fld(e, 0), t0), *N = fbits(&f[1], t1);
        int nq = fq(&f[1]);
        int64_t n = nq ? CQ_K2 : (int32_t)fval(&f[1]);
        tmp = zalloc((size_t)(n > 0 ? n : 0) + 1, u);
        setv(&a[0], (uintptr_t)tmp), setv(&a[1], (uint64_t)n);
        host_call(fn, a, e->nf, 64, 0, out);
        size_t len = !out[0] ? 0 : u == 1 ? strlen(tmp) : wcslen(tmp);
        if (!nq) {
            if (out[0]) store_k(S, 0, tmp, (len + 1) * u, g), res_q(e, g, S, 64);
            break;
        }
        cq_tape T = { 0 };
        cq_bit one[256], zero[256];
        kbits(one, nw, 1), kbits(zero, nw, 0);
        if (!out[0]) {   /* end of input: only fgets(s, 1, f) stores its 0 and returns s */
            const cq_bit *f1 = cmp_(&T, CQ_CMP_EQ, N, one, nw, 0, g);
            store_k(S, 0, tmp, u, f1), res_q(e, f1, S, 64);
            cq_tape_undo(&T);
            break;
        }
        for (size_t j = 0; j <= len; j++) {   /* unit j: tmp[j] under [j + 1 < n], a 0 under [j + 1 == n] */
            cq_tape J = { 0 };
            cq_bit jb[256], j1[256];
            kbits(jb, nw, j), kbits(j1, nw, j + 1);
            const cq_bit *st = cmp_(&J, CQ_CMP_SLT, jb, N, nw, 0, g), *keep = cmp_(&J, CQ_CMP_SLT, j1, N, nw, 0, &ONE_);
            for (uint32_t i = 0; i < u; i++) {
                cq_bit v[8];
                for (int b = 0; b < 8; b++) v[b] = ((uint8_t *)tmp)[j * u + i] >> b & 1 ? *keep : ZERO_;
                store1(S, j * u + i, v, st);
            }
            cq_tape_undo(&J);
        }
        res_q(e, cmp_(&T, CQ_CMP_SLT, zero, N, nw, 0, g), S, 64);   /* s for n >= 1 */
        cq_tape_undo(&T);
        break;
    }
    case IN_FREAD: {   /* fread(p, size, nmemb, f) */
        const cq_bit *P = fbits(&f[0], t0), *S = fbits(&f[1], t1), *M = fbits(&f[2], t2);
        if (!fq(&f[1]) && !fq(&f[2])) {
            uint64_t s = fval(&f[1]), m = fval(&f[2]);
            tmp = zalloc(s && m ? s * m : 1, 1);
            setv(&a[0], (uintptr_t)tmp);
            host_call(fn, a, e->nf, 64, 0, out);
            store_k(P, 0, tmp, out[0] * s, g);
            res_k(e, g, out);
            break;
        }
        /* The row of n = size·nmemb (ABI:516): one call at K2 bytes; byte j < got under [j < n];
         * the result min(nmemb, got / size), 0 for size 0. */
        tmp = zalloc(CQ_K2, 1);
        setv(&a[0], (uintptr_t)tmp), setv(&a[1], 1), setv(&a[2], CQ_K2);
        host_call(fn, a, e->nf, 64, 0, out);
        uint64_t got = out[0];
        cq_tape T = { 0 };
        const cq_bit *o[128];
        cq_bit n[128], zero[64], Q[64];
        cq_mul(cq_mul_kind(64), &T, S, 64, M, 64, 0, 0, 128, o);
        for (int i = 0; i < 128; i++) n[i] = *o[i];
        for (uint64_t j = 0; j < got; j++) {
            cq_tape J = { 0 };
            cq_bit jb[128], v[8];
            kbits(jb, 128, j), kbits(v, 8, ((uint8_t *)tmp)[j]);
            store1(P, j, v, cmp_(&J, CQ_CMP_ULT, jb, n, 128, 0, g));
            cq_tape_undo(&J);
        }
        int32_t qr = 0, gr = 0;
        const cq_kernel *dk = cq_kernel_pick(CQ_KOP_DIV_udiv, 64);
        int32_t da[2];
        if (!fq(&f[1])) { uint64_t s = fval(&f[1]); kbits(Q, 64, s ? got / s : 0); }
        else {   /* got / size by the divider kernel */
            gr = cq_rail_alloc(64, &got), qr = cq_rail_alloc(64, NULL);
            da[0] = gr, da[1] = f[1].rail;
            dk->emit(qr, da, 64);
            memcpy(Q, cq_rail_bits(qr), sizeof Q);
        }
        kbits(zero, 64, 0);
        cq_bit *sel = cq_tape_bits(&T, 64);
        cq_select(cq_select_kind(64), &T, cmp_(&T, CQ_CMP_ULT, M, Q, 64, 0, &ONE_), M, Q, sel, 64, 1);
        res_q(e, cmp_(&T, CQ_CMP_EQ, S, zero, 64, 1, g), sel, 64);
        cq_tape_undo(&T);
        if (qr) { (dk->unc ? dk->unc : dk->emit)(qr, da, 64); cq_rail_free(qr); cq_rail_free(gr); }
        break;
    }
    case IN_READ: {   /* read(fd, p, n), pread(fd, p, n, off) */
        const cq_bit *P = fbits(&f[1], t0), *N = fbits(&f[2], t1);
        uint32_t nw = f[2].bits;
        if (!fq(&f[2])) {
            uint64_t n = fval(&f[2]);
            tmp = zalloc(n, 1);
            setv(&a[1], (uintptr_t)tmp);
            host_call(fn, a, e->nf, 64, 0, out);
            if ((int64_t)out[0] > 0) store_k(P, 0, tmp, out[0], g);
            res_k(e, g, out);
            break;
        }
        /* The row of n (ABI:516): one call at K2; byte j < got under [j < n]; the result min(n, got). */
        tmp = zalloc(CQ_K2, 1);
        setv(&a[1], (uintptr_t)tmp), setv(&a[2], CQ_K2);
        host_call(fn, a, e->nf, 64, 0, out);
        int64_t got = (int64_t)out[0];
        for (int64_t j = 0; j < got; j++) {
            cq_tape J = { 0 };
            cq_bit jb[256], v[8];
            kbits(jb, nw, (uint64_t)j), kbits(v, 8, ((uint8_t *)tmp)[j]);
            store1(P, (uint64_t)j, v, cmp_(&J, CQ_CMP_ULT, jb, N, nw, 0, g));
            cq_tape_undo(&J);
        }
        cq_tape T = { 0 };
        cq_bit G[256];
        kbits(G, nw, (uint64_t)got);
        if (got < 0) {   /* the error, but 0 for n = 0 */
            kbits(G, nw, 0);
            res_k(e, cmp_(&T, CQ_CMP_EQ, N, G, nw, 1, g), out);
        } else {
            cq_bit *sel = cq_tape_bits(&T, nw);
            cq_select(cq_select_kind(nw), &T, cmp_(&T, CQ_CMP_ULT, N, G, nw, 0, &ONE_), N, G, sel, nw, 1);
            res_q(e, g, sel, nw);
        }
        cq_tape_undo(&T);
        break;
    }
    case IN_GETS: {   /* gets(s): at most K2 bytes (the table), read as fgets at K2 without its newline */
        tmp = zalloc(CQ_K2, 1);
        if (fgets(tmp, CQ_K2, stdin)) {
            size_t len = strlen(tmp);
            if (len && ((char *)tmp)[len - 1] == '\n') ((char *)tmp)[--len] = 0;
            const cq_bit *S = fbits(&f[0], t0);
            store_k(S, 0, tmp, len + 1, g), res_q(e, g, S, 64);
        }
        break;
    }
    case IN_GETLINE: {   /* getline(lp, np, f): the line goes to a fresh host buffer (as if *lp were
                          * NULL; the ABI has not ruled on a non-NULL *lp); the pointer cell and the
                          * size cell are stored. */
        char *line = NULL;
        size_t cap = 0;
        if (inputs[ix].other >= 0 && fq(&f[2])) {
            /* getdelim's delimiter a rail d (§1.11, the delimiter row): K2 bytes read once; for each
             * value v of d, what getdelim with delimiter v gives from them (the bytes up to and
             * including the first (unsigned char)v, as memchr compares, else all of them; -1 and an
             * empty line at end of input) in a fresh host buffer of len + 1 bytes, its pointer and
             * size cells and the result under [d == v]. */
            tmp = zalloc(CQ_K2, 1);
            size_t got = fread(tmp, 1, CQ_K2, (FILE *)(uintptr_t)idv);
            const cq_bit *D = cq_rail_bits(f[2].rail);
            reach r = reach_of(D, f[2].bits);
            for (uint64_t i = 0; i < 1ull << r.q; i++) {
                uint64_t v = reach_at(&r, i);
                const uint8_t *at = memchr(tmp, (uint8_t)v, got);
                size_t len = at ? (size_t)(at - (uint8_t *)tmp) + 1 : got;
                uint64_t res[4] = { got ? len : (uint64_t)-1 };
                cap = len + 1;
                line = memcpy(zalloc(cap, 1), tmp, len);
                cq_tape T = { 0 };
                cq_bit kb[64];
                const cq_bit *gv = eq_(&T, D, f[2].bits, v, g, kb);
                store_k(fbits(&f[0], t0), 0, &line, 8, gv), store_k(fbits(&f[1], t1), 0, &cap, 8, gv);
                res_k(e, gv, res);
                cq_tape_undo(&T);
            }
            break;
        }
        setv(&a[0], (uintptr_t)&line), setv(&a[1], (uintptr_t)&cap);
        host_call(fn, a, e->nf, 64, 0, out);
        if (line) store_k(fbits(&f[0], t0), 0, &line, 8, g), store_k(fbits(&f[1], t1), 0, &cap, 8, g);
        res_k(e, g, out);
        break;
    }
    case IN_FGETLN: {   /* fgetln(f, np) */
        size_t len = 0;
        setv(&a[1], (uintptr_t)&len);
        host_call(fn, a, e->nf, 64, 0, out);
        if (out[0]) store_k(fbits(&f[1], t1), 0, &len, 8, g);
        res_k(e, g, out);
        break;
    }
    case IN_SCANF: {
        uint32_t fk = inputs[ix].id < 0 ? 0 : (uint32_t)inputs[ix].id + 1, u = inputs[ix].wide ? (uint32_t)sizeof(wchar_t) : 1;
        if (fq(fld(e, fk))) die("an INPUT call's format pointer that is a quantum rail" LIMIT, e->text);
        void *fmt = str_host(fval(&f[fk]), u), *fmt2 = zalloc(3 * (size_t)CQ_K2 + 1, u);
        conv *cv = zalloc(CQ_K2, sizeof *cv);
        uint32_t nc = scan_walk(fmt, inputs[ix].wide, cv, fmt2);
        setv(&a[fk], (uintptr_t)fmt2);
        uint8_t **tp = zalloc(nc, sizeof *tp);
        cq_bit (*A)[64] = zalloc(nc, sizeof *A);   /* each pointer's address bits */
        for (uint32_t i = 0; i < nc; i++) {
            tp[i] = zalloc(cv[i].tsz, 1);
            if (cv[i].kind == 'n') memset(tp[i], 0xff, cv[i].tsz);   /* unchanged: %n not reached */
        }
        valist nv = { 48, 176, 0, 0 };
        uint64_t *area = NULL;
        if (!inputs[ix].va) {
            if (fk + 1 + nc > e->nf) die("entry has fewer fields than its format takes", e->text);
            for (uint32_t i = 0; i < nc; i++) {
                if (f[fk + 1 + i].bits != 64) die("a scanf pointer field is not a 64-bit address", e->text);
                memcpy(A[i], fbits(&f[fk + 1 + i], t0), sizeof A[i]);
                setv(&a[fk + 1 + i], (uintptr_t)tp[i]);
            }
        } else {
            field *ap = fld(e, fk + 1);
            if (fq(ap)) die("an INPUT call's va_list pointer that is a quantum rail" LIMIT, e->text);
            valist src;
            cev = e;
            rd_va(fval(ap), &src);
            area = zalloc(nc, 8);
            for (uint32_t i = 0; i < nc; i++) {   /* every argument is a pointer: an integer slot */
                uint64_t at = src.gp < 48 ? src.regs + src.gp : src.over;
                if (src.gp < 48) src.gp += 8; else src.over += 8;
                cq_cells_read(at, A[i], 8);   /* its bits, qubits included: a quantum address */
                area[i] = (uintptr_t)tp[i];
            }
            nv.over = (uintptr_t)area;
            setv(&a[fk + 1], (uintptr_t)&nv);
        }
        host_call(fn, a, e->nf, 64, 0, out);
        int ret = (int)(int32_t)out[0];
        for (uint32_t i = 0, as = 0; i < nc; i++) {
            uint64_t n = cv[i].sz;
            if (cv[i].kind == 'n') { if (tp[i][0] == 0xff && !memcmp(tp[i], tp[i] + 1, n - 1)) continue; }
            else if ((int)as++ >= ret) continue;
            if (cv[i].kind == 's') n = (u_len(tp[i], cv[i].sz) + 1) * cv[i].sz;
            store_k(A[i], 0, tp[i], n, g);
        }
        res_k(e, g, out);
        for (uint32_t i = 0; i < nc; i++) free(tp[i]);
        free(tp), free(A), free(area), free(cv), free(fmt), free(fmt2);
        break;
    }
    }
    free(tmp);
    free(a);
}

/* The identifier row (§1.11): one call per value that names an open stream, under [id == v]. */
static void by_id(event *e, int ix, const cq_bit *P, void *fn) {
    int id = inputs[ix].id;
    if (id < 0) { run(e, ix, (uintptr_t)stdin, P, fn); return; }
    field *f = fld(e, (uint32_t)id);
    if (!fq(f)) { run(e, ix, fval(f), P, fn); return; }
    cq_bit t[256];
    const cq_bit *x = fbits(f, t);
    reach r = reach_of(x, f->bits);
    for (uint64_t i = 0; i < 1ull << r.q; i++) {
        uint64_t v = reach_at(&r, i);
        if (!is_open(v, f->bits)) continue;
        cq_tape T = { 0 };
        cq_bit kb[64];
        run(e, ix, v, eq_(&T, x, f->bits, v, P, kb), fn);
        cq_tape_undo(&T);
    }
}

static void merge(event *e, int ix) {
    const cq_bit *P = e->pred < 0 ? cq_rail_bits(e->pr) : &ONE_;
    void *fn = inputs[ix].cls == IN_GETS ? NULL : dlsym(RTLD_DEFAULT, e->text);
    if (inputs[ix].cls != IN_GETS && !fn) die("no host function of that name", e->text);
    field *o = inputs[ix].other >= 0 ? fld(e, (uint32_t)inputs[ix].other) : NULL;
    if (!o || !fq(o) || inputs[ix].cls == IN_GETLINE) { by_id(e, ix, P, fn); return; }   /* getdelim's d: in run */
    if (inputs[ix].cls == IN_NONE)
        die("ungetc/ungetwc with a quantum character" LIMIT, e->text);
    /* pread's offset row (§1.11): one call per value v of off, under [off == v]; the field holds v
     * meanwhile, so the rows above compose with it. */
    field q = *o;
    const cq_bit *x = cq_rail_bits(q.rail);
    reach r = reach_of(x, q.bits);
    for (uint64_t i = 0; i < 1ull << r.q; i++) {
        uint64_t v = reach_at(&r, i);
        cq_tape T = { 0 };
        cq_bit kb[64];
        const cq_bit *g = eq_(&T, x, q.bits, v, P, kb);
        *o = (field){ q.bits, 0, 0, { v } };
        by_id(e, ix, g, fn);
        cq_tape_undo(&T);
    }
    *o = q;
}

/* ---- The sequence */

/* The end of the shot: measure the deferred events, perform them in order until an abort flag
 * reads 1. Reentrant: an event is done before it is performed. */
static void end_shot(void) {
    exiting = 1;
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

static void at_exit(void) { end_shot(); }

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
        else { e->pred = -1; e->pr = cq_rail_alloc(1, NULL); cqrt_xor_i1(c, (uint32_t)e->pr); }   /* a gate: cq_invert records it */
    }
    return e;
}

/* e is complete: performed at once (r set), merged (INPUT), skipped or deferred. */
static void complete(event *e) {
    int ix = e->kind == EV_EFF ? input_of(e->text) : -1, quantum = e->pred < 0;
    for (uint32_t k = 0; k < e->nf; k++) quantum |= fq(&e->f[k]);
    if (e->pred == 0) { e->status = ST_DONE; return; }   /* skipped: never performed */
    /* Performed or merged at its call, or gates on a quantum predicate: a flush point of an open
     * cq_invert region (ABI §1.13), which also runs the recorded XOR of that predicate. */
    if (ix >= 0 || quantum) cq_inv_flush();
    if (ix >= 0) {
        e->status = ST_DONE;
        if (quantum || inputs[ix].cls == IN_GETS) merge(e, ix);   /* gets is always fgets at K2 (the table) */
        else perform_now(e);
        return;
    }
    if (!quantum && live == (uint32_t)(e - ev)) {   /* resolved if every cell it reads is HOST */
        field *save = memcpy(zalloc(e->nf, sizeof *save), e->f, e->nf * sizeof *save);
        capture(e);
        if (!cqcell) {
            drop_bufs(e);
            memcpy(e->f, save, e->nf * sizeof *save);
            free(save);
            cq_inv_flush();
            perform_now(e);
            return;
        }
        free(save);
    } else capture(e);
    e->status = ST_DEFERRED;
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
    if (!e->need) { complete(e); advance(); }
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
    e->f = memcpy(zalloc(e->nf, sizeof *e->f), tp->f + tp->nf - e->nf, e->nf * sizeof *e->f);
    complete(e);
    advance();
}

static uint32_t effect(uint32_t c, uint32_t t, const char *name, uint32_t n, uint32_t rbits, int rfp) {
    tape *tp = get_tape(t);
    if (n > tp->nf) die("effect entry asks for more fields than its tape has", name);
    event *e = new_event(EV_EFF, c, 1);
    e->text = copy_str(name);
    e->rbits = rbits, e->rfp = (uint8_t)rfp, e->nf = n;
    e->f = memcpy(zalloc(n, sizeof *e->f), tp->f + tp->nf - n, n * sizeof *e->f);
    e->r = rbits ? cq_rail_alloc(rbits, NULL) : 0;   /* skipped or deferred: |0> */
    int32_t r = e->r;
    complete(e);   /* may grow ev: e is not used after */
    advance();
    return (uint32_t)r;
}
void cqrt_tape_effect_controlled(uint32_t c, uint32_t t, const char *name, uint32_t n) { effect(c, t, name, n, 0, 0); }

/* The write is an XOR gate (cq_copy, which cq_invert records); the field keeps its place. */
static uint32_t write_rail(uint32_t c, int ctl, uint32_t t, uint32_t h, uint32_t bits, int fp) {
    if (ctl) rail(c, 1);
    rail(h, bits);
    int32_t r = cq_rail_alloc(bits, NULL);
    cq_copy(c, ctl, h, (uint32_t)r, bits);
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
