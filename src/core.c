/* The public core entry points of BACKEND_ABI §1 that are not rotations, the
 * bus, the tape or cq_invert. Types and conventions: include/cqrt.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cqrt.h"
#include "cq_rt.h"

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
    for (uint32_t i = 0; i < w; i++)
        if (cb) cq_ccx(cb, &sb[i], &db[i]); else cq_cx(&sb[i], &db[i]);
}

static void xorc(uint32_t c, int ctl, uint32_t h, uint32_t w, const uint64_t *v) {
    const cq_bit *cb = ctl ? rail(c, 1) : NULL;
    cq_bit *b = rail(h, w);
    for (uint32_t i = 0; i < w; i++)
        if ((v[i / 64] >> (i % 64)) & 1) { if (cb) cq_cx(cb, &b[i]); else cq_x(&b[i]); }
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
    CORE_(n, bits, CQRT_UINT(bits) lit, CQRT_INT(bits), V_NARROW_, OUT_NARROW_)
CQ_WIDTHS_NARROW(NARROW_)

#define V_WIDE_(bits) const uint64_t v[4] = { l0, l1, l2, l3 }
#define OUT_WIDE_(bits, RET) RET r; memcpy(r.l, v, sizeof r.l); return r
#define WIDE_(n, bits) \
    _Static_assert(bits > 192 && bits <= 256, #n ": four limbs carry 193..256 bits"); \
    CORE_(n, bits, CQRT_LIMBS_, cqrt_u256, V_WIDE_, OUT_WIDE_)
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
    switch (k->n[0]) {
    case NOT: cq_x(tb); break;
    case AND: cq_ccx(ab, bb, tb); break;
    case OR:  cq_cx(ab, tb); cq_cx(bb, tb); cq_ccx(ab, bb, tb); break;   /* a ^ b ^ ab = a | b */
    default:  cq_cx(ab, tb);
    }
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
