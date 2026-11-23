/* The derivation note beside a 64-bit rail (ABI §1.10 "Objects", Derivation; src/note.c): the value
 * rule, the rules of copy, add, sub, select and the i64 load, the default drop and the end flag, a note
 * set by a call run at the end of a cq_invert region. Read through cq_note_get. Always asserts. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include "cqrt.h"
#include "cq_bus.h"
#include "cq_template.h"
#include "cq_replay.h"

/* X and Y touch, Z stands alone; P holds pointer tables (HOST cells, not an object). */
static _Alignas(64) uint64_t mem[64], P[16];
static uint64_t X, Y, Z, ZE;   /* ZE: one past Z's end, in no object */
static uint32_t M;

static int line;   /* of the failing check */
/* n = -1: no note; else the entries (a, end) as starts of X/Y/Z, any order. */
#define is(...) (line = __LINE__, is_(__VA_ARGS__))
static void is_(uint32_t h, int n, uint64_t a0, int e0, uint64_t a1, int e1) {
    cq_note t = cq_note_get((int32_t)h);
    if (t.n != n) fprintf(stderr, "line %d: %d entries, expected %d\n", line, t.n, n);
    assert(t.n == n);
    for (int j = 0; j < n; j++) {
        uint64_t a = j ? a1 : a0;
        int e = j ? e1 : e0, k = 0;
        while (k < t.n && t.e[k].a != a) k++;
        if (!(k < t.n && (int)t.e[k].end == e)) fprintf(stderr, "line %d: entry %d (end %d) missing\n", line, j, e);
        assert(k < t.n && (int)t.e[k].end == e);
    }
}
#define none(h) is(h, -1, 0, 0, 0, 0)
#define one(h, a, e) is(h, 1, a, e, 0, 0)

static uint32_t qbit(void) { uint32_t c = cqrt_alloc_i1(0); cq_get_qubit(cq_rail_bits((int32_t)c)); return c; }
/* A quantum rail holding v with no note: bits lo..hi-1 qubits (by replay, no gate), the rest classical. */
static uint32_t qrail(uint64_t v, int lo, int hi) {
    uint32_t h = cqrt_alloc_i64(v);
    cq_bit *b = cq_rail_bits((int32_t)h);
    for (int i = lo; i < hi; i++) { b[i] = cq_bit_const(0); cq_get_qubit(&b[i]); cq_replay_set_bit(b[i].q, (int)(v >> i & 1)); }
    return h;
}
/* A quantum rail c ? v : 0, noted by the copy rule from the classical v. */
static uint32_t noted(uint64_t v) {
    uint32_t p = cqrt_alloc_i64(v), d = cqrt_alloc_i64(0);
    cqrt_copy_i64_controlled(qbit(), p, d);
    return d;
}

static void value_rule(void) {
    is(cqrt_alloc_i64(0), 0, 0, 0, 0, 0);             /* null */
    one(cqrt_alloc_i64(X), X, 0);                     /* first cell */
    one(cqrt_alloc_i64(X + 63), X, 0);                /* last cell */
    is(cqrt_alloc_i64(Y), 2, Y, 0, X, 1);             /* one past X, touching Y */
    one(cqrt_alloc_i64(ZE), Z, 1);                    /* one past Z, nothing after */
    none(cqrt_alloc_i64(ZE + 1));                     /* unknown */
    none(cqrt_alloc_i32(0));                          /* not 64 bits */
    none(qrail(X, 0, 3));                             /* quantum, no rule set a note */
}

static void keep(void) {
    uint32_t d = noted(X + 8), e = noted(Z + 8), off = qrail(16, 0, 6), c = qbit(), t;
    one(d, X, 0);
    t = cqrt_alloc_i64(0); cqrt_copy_i64(d, t); one(t, X, 0);
    t = cqrt_alloc_i64(0); cqrt_copy_i64_controlled(c, d, t); one(t, X, 0);
    one(cq_template_add_i64_hl(d, 16), X, 0);
    one(cq_template_add_i64(off, d), X, 0);
    one(cq_template_add_i64(d, cqrt_alloc_i64(ZE + 1)), X, 0);   /* an unknown operand is not noted */
    one(cq_template_sub_i64_hl(d, 8), X, 0);
    one(cq_template_sub_i64(d, off), X, 0);
    one(cq_template_sub_i64_lh(X + 32, off), X, 0);   /* a literal minuend by the value rule */
    is(cq_template_select_i64(c, d, e), 2, X, 0, Z, 0);
    one(cq_template_select_i64_hl(c, d, 0), X, 0);    /* null arm */
    one(cq_template_select_i64_lh(c, 0, d), X, 0);
    one(cq_template_select_i64(c, d, noted(X + 40)), X, 0);   /* same object: one entry */
    is(cq_template_select_i64_ll(c, X, Z + 1), 2, X, 0, Z, 0);
    none(cq_template_select_i64_hl(c, d, ZE + 1));    /* unknown arm */
    none(cq_template_select_i64(c, d, off));          /* arm with no note */
    /* the cap: a union above CQ_NOTE_MAX objects is no note */
    uint64_t o[5];
    for (int i = 0; i < 5; i++) { o[i] = 0x7000000 + 0x100 * (uint64_t)i; cqrt_qram_object((int64_t)o[i], 16); }
    uint32_t u = cq_template_select_i64_ll(c, o[0], o[1]);
    u = cq_template_select_i64(c, u, cq_template_select_i64_ll(c, o[2], o[3]));
    assert(cq_note_get((int32_t)u).n == 4);
    none(cq_template_select_i64_hl(c, u, o[4]));
}

static void drops(void) {
    uint32_t d, t, off = qrail(16, 0, 6);
    d = noted(X + 8); cqrt_xorc_i64(d, 1); none(d);
    d = noted(X + 8); t = cq_template_add_i64_hl(d, 16); one(t, X, 0);
    cq_template_add_i64_hl_unc(t, d, 16); none(t);
    d = noted(X + 8); t = noted(Z + 8); cqrt_copy_i64(d, t); none(t);      /* copy into a non-zero rail */
    none(cq_template_add_i64(noted(X), noted(Z)));                          /* two noted operands */
    none(cq_template_add_i64(noted(X), noted(X + 8)));
    none(cq_template_sub_i64(noted(X + 8), noted(X)));                      /* noted subtrahend */
    none(cq_template_sub_i64(off, noted(X)));
    d = noted(X + 8);
    none(cq_template_trunc_i64_to_i32(d));                                  /* not 64 bits */
    none(cq_template_zext_i32_to_i64(cq_template_trunc_i64_to_i32(d)));
    none(cq_template_mul_i64_hl(d, 1)); none(cq_template_shl_i64_hl(d, 0)); none(cq_template_and_i64_hl(d, ~0ull));
    none(cq_template_or_i64_hl(d, 0)); none(cq_template_add_i64_hl(cq_template_mul_i64_hl(d, 1), 8));
    one(d, X, 0);                                                           /* the operand keeps its note */
    for (int i = 0; i < 64; i++)
        if (cq_bit_is_q(cq_rail_bits((int32_t)d)[i])) { cq_x(&cq_rail_bits((int32_t)d)[i]); break; }
    none(d);                                                                /* a gate on one of its qubits */
    d = noted(X + 8); cqrt_ry_i64(d, 0.5); none(d);
}

static void end_flag(void) {
    uint32_t d = noted(ZE), lo = qrail(16, 0, 6), hi = qrail(16, 0, 64);
    one(d, Z, 1);
    one(cq_template_add_i64_hl(d, 8), Z, 1);          /* literal offset, bit 63 classical 0 */
    one(cq_template_add_i64(lo, d), Z, 1);            /* quantum offset, bit 63 classical 0 */
    one(cq_template_add_i64(d, hi), Z, 0);            /* bit 63 a qubit */
    one(cq_template_add_i64_hl(d, (uint64_t)-8), Z, 0);
    one(cq_template_sub_i64_hl(d, 8), Z, 0);
    one(cq_template_sub_i64_hl(d, 0), Z, 0);
    is(noted(Y), 2, Y, 0, X, 1);
    is(cq_template_select_i64_hl(qbit(), noted(Y), X + 8), 2, Y, 0, X, 0);   /* end only if both arms have it */
    one(cq_template_select_i64_hl(qbit(), d, ZE), Z, 1);
}

/* An i64 load from the table at P + 8 * (2 quantum bits): the 4 entries v[]. */
static uint32_t load_table(const uint64_t *v) {
    for (int i = 0; i < 4; i++) P[i] = v[i];
    return cqrt_qram_load_i64(M, qrail((uint64_t)(uintptr_t)P, 3, 5));
}

static void loads(void) {
    one(load_table((uint64_t[]){ X, X + 8, X + 16, X + 56 }), X, 0);
    one(load_table((uint64_t[]){ X, 0, X + 16, X + 56 }), X, 0);                 /* a null entry */
    is(load_table((uint64_t[]){ X, Z, 0, ZE }), 2, X, 0, Z, 0);                 /* Z and its end: plain */
    is(load_table((uint64_t[]){ X, ZE, 0, ZE }), 2, X, 0, Z, 1);
    none(load_table((uint64_t[]){ X, ZE + 1, X + 16, X + 56 }));                 /* an unknown entry */
    /* one cell made QUANTUM by a prior store */
    for (int i = 0; i < 4; i++) P[i] = X + 8 * (uint64_t)i;
    uint32_t v = cqrt_alloc_i8(0);
    cq_get_qubit(cq_rail_bits((int32_t)v));
    cqrt_qram_store_i8_hl(M, (uint64_t)(uintptr_t)&P[2] + 1, v);
    none(cqrt_qram_load_i64(M, qrail((uint64_t)(uintptr_t)P, 3, 5)));
    one(cqrt_qram_load_i64(M, qrail((uint64_t)(uintptr_t)P, 3, 4)), X, 0);    /* leaves 0, 1 only */
    /* classical address: controlled into |0>, and the uncontrolled classical result */
    P[8] = Z + 8;
    one(cqrt_qram_load_i64_hl_controlled(qbit(), M, (uint64_t)(uintptr_t)&P[8]), Z, 0);
    one(cqrt_qram_load_i64_hl(M, (uint64_t)(uintptr_t)&P[8]), Z, 0);
    P[8] = ZE + 1;
    none(cqrt_qram_load_i64_hl_controlled(qbit(), M, (uint64_t)(uintptr_t)&P[8]));
    P[8] = Z + 8;
    none(cqrt_qram_load_i32_hl_controlled(qbit(), M, (uint64_t)(uintptr_t)&P[8]));   /* not 64 bits */
    uint32_t h = noted(X);   /* a load _unc into a rail that is not |0> sets nothing */
    cqrt_qram_load_i64_hl_controlled_unc(h, qbit(), M, (uint64_t)(uintptr_t)&P[8]);
    none(h);
}

static void invert(void) {
    uint32_t d = noted(X + 8), t, u;
    cqrt_invert_begin();
    t = cq_template_add_i64_hl(d, 8);
    is(t, 0, 0, 0, 0, 0);          /* recorded, not run: still classical 0 */
    u = cqrt_alloc_i64(0);
    cqrt_copy_i64(d, u);
    is(u, 0, 0, 0, 0, 0);
    cqrt_invert_end();
    one(t, X, 0);
    one(u, X, 0);
}

/* cqrt_qram_unbound: no note from then on, for a quantum rail, a classical one and a later rule. */
static void unbound(void) {
    uint32_t d = noted(X + 8), k = cqrt_alloc_i64(X + 8), z = cqrt_alloc_i64(0);
    cqrt_qram_unbound(d); none(d);
    one(k, X, 0); cqrt_qram_unbound(k); none(k);
    cqrt_qram_unbound(z); cqrt_copy_i64(noted(X + 8), z); none(z);
    one(cq_template_add_i64_hl(noted(X + 8), 8), X, 0);   /* other rails are untouched */
}

int main(void) {
    X = (uint64_t)(uintptr_t)mem; Y = X + 64; Z = X + 256; ZE = Z + 64;
    cqrt_qram_object((int64_t)X, 64); cqrt_qram_object((int64_t)Y, 64); cqrt_qram_object((int64_t)Z, 64);
    M = cqrt_qram_alloc_i8(0);
    value_rule(); keep(); drops(); end_flag(); loads(); invert(); unbound();
    printf("note ok\n");
    return 0;
}
