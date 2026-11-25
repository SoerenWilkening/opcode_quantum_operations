/* The public core entry points of BACKEND_ABI §1 that are not rotations, the
 * bus, the tape or cq_invert. Types and conventions: include/cqrt.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cqrt.h"
#include "cq_rt.h"
#include "kernels.h"   /* CQ_KOP_ARITH_add/sub, for the note */

static void die(const char *msg, uint32_t h) {
    fprintf(stderr, "cq_runtime: %s (handle %u)\n", msg, h);
    abort();
}

/* The bits of rail h, which the call's width token says is w bits wide. */
static cq_bit *rail(uint32_t h, uint32_t w) {
    if (cq_rail_width((int32_t)h) != w) die("rail width does not match the call", h);
    return cq_rail_bits((int32_t)h);
}

static uint32_t alloc(uint32_t w, const uint64_t *v) { return (uint32_t)cq_rail_alloc(w, v); }

/* v: zeroed limbs; gets the outcome. The rail stays allocated and holds it. */
static void measure(uint32_t h, uint32_t w, uint64_t *v) {
    cq_inv_flush();   /* a measurement inside a cq_invert region needs the gates before it */
    cq_bit *b = rail(h, w);
    cq_op_begin("measure", 0, &h, 1);
    for (uint32_t i = 0; i < w; i++)
        if (cq_measure_bit(&b[i])) v[i / 64] |= 1ull << (i % 64);
    cq_op_end();
}

/* ctl == 0: the uncontrolled form, c unused. */
static void copy(uint32_t c, int ctl, uint32_t s, uint32_t d, uint32_t w) {
    const cq_bit *cb = ctl ? rail(c, 1) : NULL;
    cq_bit *sb = rail(s, w), *db = rail(d, w);
    int zero = cq_rail_zero((int32_t)d), note = w == 64 && zero;   /* into |0>: d gets s's note (or null under c = 0) */
    cq_note nt = note ? cq_note_get((int32_t)s) : (cq_note){ 0 };
    /* "Data": into |0> what s can hold; d ^= s otherwise: data only if both are. */
    cq_note ct = zero ? cq_note_content((int32_t)s) : cq_content_all((uint32_t[]){ s, d }, 2);
    for (uint32_t i = 0; i < w; i++)
        if (cb) cq_ccx(cb, &sb[i], &db[i]); else cq_cx(&sb[i], &db[i]);
    if (note) cq_note_set((int32_t)d, &nt);
    cq_content_set((int32_t)d, &ct);
}

static void xorc(uint32_t c, int ctl, uint32_t h, uint32_t w, const uint64_t *v) {
    const cq_bit *cb = ctl ? rail(c, 1) : NULL;
    cq_bit *b = rail(h, w);
    cq_note ct = cq_content_all(&h, 1);   /* "Data": data stays data */
    for (uint32_t i = 0; i < w; i++)
        if ((v[i / 64] >> (i % 64)) & 1) { if (cb) cq_cx(cb, &b[i]); else cq_x(&b[i]); }
    cq_content_set((int32_t)h, &ct);
}

/* Through cq_invert (cq_rt.h): each of these is its own inverse. */
static void copy_k(const cq_call *k, int inv) { (void)inv; copy(k->h[0], (int)k->n[1], k->h[1], k->h[2], k->n[0]); }
void cq_copy(uint32_t c, int ctl, uint32_t s, uint32_t d, uint32_t w) {
    cq_call_do(&(cq_call){ .run = copy_k, .name = "copy", .h = { c, s, d }, .n = { w, (uint32_t)ctl } });
}
static void xorc_k(const cq_call *k, int inv) { (void)inv; xorc(k->h[0], (int)k->n[1], k->h[1], k->n[0], k->v); }
static void xorc_do(uint32_t c, int ctl, uint32_t h, uint32_t w, const uint64_t *v, size_t nv) {
    cq_call k = { .run = xorc_k, .name = "xorc", .h = { c, h }, .n = { w, (uint32_t)ctl } };
    memcpy(k.v, v, nv);
    cq_call_do(&k);
}

/* ABI §1.15: d += s in place; sub = 1: d -= s, the same gate list reversed. The Cuccaro adder (Cuccaro,
 * Draper, Kutin, Moulton, quant-ph/0410184) without the carry out, in its depth 2W + O(1) order: the CXs
 * of every MAJ first, the carry CCXs up, the top sum bit, then the 3-CNOT UMA (b negated around its
 * CCX) with the CCXs down before the CXs. c_i, the carry into bit i, lives on wire a_(i-1) (c_0: one
 * ancilla). Wire a_i is s's bit when that is a qubit, else a scratch bit holding its classical value
 * (addc's literal too). Scratch and the ancilla end as they began and return to the pool; classical
 * bits fold in the emitter, so an all-classical call emits no gate.
 * shortcut: depth linear in W; a log-depth in-place adder (Draper et al., quant-ph/0406142 §4) if it matters. */
typedef struct { const cq_bit *a, *b; cq_bit *t; } gate;   /* b NULL: CX; a NULL too: X */
static void add(uint32_t s, uint32_t d, uint32_t w, int sub) {
    if (s == d) die("add/sub: s and d are the same rail", s);
    cq_bit *sb = rail(s, w), *db = rail(d, w), cin = cq_bit_const(0);
    cq_bit *a = malloc(w * sizeof *a), **ap = malloc(w * sizeof *ap);
    gate *g = malloc(9 * (size_t)w * sizeof *g);
    if (!a || !ap || !g) die("out of memory", d);
    /* The derivation note and "Data" (ABI §1.10), as the add/sub template leaves them on its result. */
    int op = sub ? CQ_KOP_ARITH_sub : CQ_KOP_ARITH_add;
    cq_note nt = w == 64 ? cq_note_tpl(op, (uint32_t[]){ d, s }) : (cq_note){ -1, { { 0, 0 } } };
    cq_note ct = cq_content_all((uint32_t[]){ d, s }, 2);
    if (ct.n < 0 && nt.n > 0) ct = nt;
    size_t n = 0;
    for (uint32_t i = 0; i < w; i++) { a[i] = sb[i]; ap[i] = cq_bit_is_q(sb[i]) ? &sb[i] : &a[i]; }
#define C_(i) ((i) ? ap[(i) - 1] : &cin)
    for (uint32_t i = 0; i < w; i++) g[n++] = (gate){ ap[i], NULL, &db[i] };                 /* d_i ^= a_i */
    for (uint32_t i = 0; i + 1 < w; i++) g[n++] = (gate){ ap[i], NULL, C_(i) };              /* c_i ^= a_i */
    for (uint32_t i = 0; i + 1 < w; i++) g[n++] = (gate){ C_(i), &db[i], ap[i] };            /* a_i = c_(i+1) */
    if (w > 1) g[n++] = (gate){ ap[w - 2], NULL, &db[w - 1] };                               /* the top sum bit */
    for (uint32_t i = 0; i + 1 < w; i++) { g[n++] = (gate){ NULL, NULL, &db[i] }; g[n++] = (gate){ C_(i), NULL, &db[i] }; }
    for (uint32_t i = w - 1; i-- > 0;) g[n++] = (gate){ C_(i), &db[i], ap[i] };              /* a_i restored */
    for (uint32_t i = w - 1; i-- > 0;) {                                                     /* c_i restored, d_i the sum bit */
        g[n++] = (gate){ NULL, NULL, &db[i] }; g[n++] = (gate){ ap[i], NULL, C_(i) }; g[n++] = (gate){ ap[i], NULL, &db[i] };
    }
#undef C_
    for (size_t k = 0; k < n; k++) {
        const gate *x = &g[sub ? n - 1 - k : k];
        if (x->b) cq_ccx(x->a, x->b, x->t); else if (x->a) cq_cx(x->a, x->t); else cq_x(x->t);
    }
    for (uint32_t i = 0; i < w; i++)         /* scratch: back to its classical value, flipped to |0>, released */
        if (ap[i] == &a[i] && cq_bit_is_q(a[i])) { if (sb[i].kind == CQ_BIT_ONE) cq_x(&a[i]); cq_pool_give(a[i].q); }
    if (cq_bit_is_q(cin)) cq_pool_give(cin.q);
    free(a); free(ap); free(g);
    if (w == 64) cq_note_set((int32_t)d, &nt);
    cq_content_set((int32_t)d, &ct);
}
/* Through cq_invert: the inverse of add is sub, that of addc(lit) the reversed list too (= addc(-lit)). */
static void add_k(const cq_call *k, int inv) { add(k->h[0], k->h[1], k->n[0], (int)k->n[1] ^ inv); }
static void add_do(uint32_t s, uint32_t d, uint32_t w, int sub) {
    cq_call_do(&(cq_call){ .run = add_k, .name = sub ? "sub" : "add", .h = { s, d }, .n = { w, (uint32_t)sub } });
}
static void addc_k(const cq_call *k, int inv) {
    int32_t l = cq_rail_alloc(k->n[0], k->v);   /* classical: add never targets it, the free emits nothing */
    add((uint32_t)l, k->h[0], k->n[0], inv);
    cq_rail_free(l);
}
static void addc_do(uint32_t h, uint32_t w, const uint64_t *v, size_t nv) {
    cq_call k = { .run = addc_k, .name = "addc", .h = { h }, .n = { w } };
    memcpy(k.v, v, nv);
    cq_call_do(&k);
}
/* Integer widths only (ABI §1.15). */
#define ADD_(n, bits, LIT, V) \
    void cqrt_add_##n(uint32_t s, uint32_t d) { add_do(s, d, bits, 0); } \
    void cqrt_sub_##n(uint32_t s, uint32_t d) { add_do(s, d, bits, 1); } \
    void cqrt_addc_##n(uint32_t h, LIT) { V(bits); addc_do(h, bits, v, sizeof v); }

/* Per width. LIT is the literal parameter list, RET the measure type. V(bits) declares the
 * limbs v of the literal; OUT(bits, RET) returns the measured limbs v as RET. */
#define CORE_(n, bits, LIT, RET, V, OUT) \
    uint32_t cqrt_alloc_##n(LIT) { V(bits); return alloc(bits, v); } \
    RET cqrt_measure_##n(uint32_t h) { uint64_t v[4] = { 0 }; measure(h, bits, v); OUT(bits, RET); } \
    void cqrt_copy_##n(uint32_t s, uint32_t d) { cq_copy(0, 0, s, d, bits); } \
    void cqrt_copy_##n##_controlled(uint32_t c, uint32_t s, uint32_t d) { cq_copy(c, 1, s, d, bits); } \
    void cqrt_xorc_##n(uint32_t h, LIT) { V(bits); xorc_do(0, 0, h, bits, v, sizeof v); } \
    void cqrt_xorc_##n##_controlled(uint32_t c, uint32_t h, LIT) { V(bits); xorc_do(c, 1, h, bits, v, sizeof v); }

static unsigned __int128 sext(const uint64_t *v, uint32_t w) {
    unsigned __int128 x = (unsigned __int128)v[1] << 64 | v[0];
    if (w > 1 && w < 128 && (x >> (w - 1)) & 1) x |= ~(unsigned __int128)0 << w;
    return x;
}

#define V_NARROW_(bits) const uint64_t v[2] = { (uint64_t)lit, (uint64_t)((unsigned __int128)lit >> 64) }
#define OUT_NARROW_(bits, RET) return (RET)sext(v, bits)
#define NARROW_(n, bits) \
    _Static_assert(bits <= 128, #n " belongs in CQ_WIDTHS_WIDE"); \
    CORE_(n, bits, CQRT_UINT(bits) lit, CQRT_INT(bits), V_NARROW_, OUT_NARROW_) \
    ADD_(n, bits, CQRT_UINT(bits) lit, V_NARROW_)
CQ_WIDTHS_NARROW(NARROW_)

#define V_WIDE_(bits) const uint64_t v[4] = { l0, l1, l2, l3 }
#define OUT_WIDE_(bits, RET) RET r; memcpy(r.l, v, sizeof r.l); return r
#define WIDE_(n, bits) \
    _Static_assert(bits > 192 && bits <= 256, #n ": four limbs carry 193..256 bits"); \
    CORE_(n, bits, CQRT_LIMBS_, cqrt_u256, V_WIDE_, OUT_WIDE_) \
    ADD_(n, bits, CQRT_LIMBS_, V_WIDE_)
CQ_WIDTHS_WIDE(WIDE_)

/* An fp literal is its bit pattern: the low bits/8 bytes of its object (f80: 10 of 16). */
#define V_FP_(bits) uint64_t v[2] = { 0 }; memcpy(v, &lit, (bits) / 8)
/* The volatile hides how the bits were built: at -O3 clang turns setting the sign bit of a half
 * into fneg/fabs, which x86 lowers through float, and that quiets a signalling NaN. */
#define OUT_FP_(bits, RET) volatile uint64_t opaque[2] = { v[0], v[1] }; const uint64_t b[2] = { opaque[0], opaque[1] }; \
    RET r; memset(&r, 0, sizeof r); memcpy(&r, b, (bits) / 8); return r
#define FP_(n, bits, T) CORE_(n, bits, T lit, T, V_FP_, OUT_FP_)
CQ_FP_WIDTHS(FP_)

void cqrt_free(uint32_t h) { cq_inv_free((int32_t)h); }   /* trusted: m, x on 1, back to the pool */

/* The i1 logic, t ^= f(a, b): each its own inverse. */
enum { NOT, AND, OR, XOR };
static void logic_k(const cq_call *k, int inv) {
    (void)inv;
    cq_bit *tb = rail(k->h[2], 1), *ab = k->n[0] != NOT ? rail(k->h[0], 1) : NULL;
    cq_bit *bb = k->n[0] == AND || k->n[0] == OR ? rail(k->h[1], 1) : NULL;
    cq_note ct = cq_content_all((uint32_t[]){ k->h[2], ab ? k->h[0] : k->h[2], bb ? k->h[1] : k->h[2] }, 3);   /* "Data" */
    switch (k->n[0]) {
    case NOT: cq_x(tb); break;
    case AND: cq_ccx(ab, bb, tb); break;
    case OR:  cq_cx(ab, tb); cq_cx(bb, tb); cq_ccx(ab, bb, tb); break;   /* a ^ b ^ ab = a | b */
    default:  cq_cx(ab, tb);
    }
    cq_content_set((int32_t)k->h[2], &ct);
}
static void logic(uint32_t op, uint32_t a, uint32_t b, uint32_t t) { cq_call_do(&(cq_call){ .run = logic_k, .name = (const char *[]){ "not", "and", "or", "xor" }[op], .h = { a, b, t }, .n = { op } }); }
void cqrt_not_i1(uint32_t h) { logic(NOT, 0, 0, h); }
void cqrt_and_i1(uint32_t a, uint32_t b, uint32_t t) { logic(AND, a, b, t); }
void cqrt_or_i1(uint32_t a, uint32_t b, uint32_t t) { logic(OR, a, b, t); }
void cqrt_xor_i1(uint32_t a, uint32_t t) { logic(XOR, a, 0, t); }

/* The word stack. Balance is the caller's obligation (ABI §1.12); popping an empty stack aborts. */
static uint32_t *words;
static size_t    nwords, capwords;

void cqrt_push(uint32_t w) {
    if (nwords == capwords) {
        capwords = capwords ? 2 * capwords : 64;
        if (!(words = realloc(words, capwords * sizeof *words))) die("out of memory", w);
    }
    words[nwords++] = w;
}

uint32_t cqrt_pop(void) {
    if (!nwords) die("cqrt_pop on an empty stack", 0);
    return words[--nwords];
}
