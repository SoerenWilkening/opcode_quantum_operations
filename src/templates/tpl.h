/* Internal to libcq_templates.a: a template is its registry operation's shipped kernel
 * (src/kernels/kernels.h) on a fresh result rail. To add a handle-only template:
 * a kernel op in the registry, a declaration line in include/cq_template.h, and one
 * CQ_TPL_UNARY_ (or a sibling for more operands) per width list in its .c file. */
#ifndef CQ_TPL_H
#define CQ_TPL_H
#include <stdio.h>
#include <stdlib.h>
#include "cq_rt.h"
#include "kernels.h"

/* cq_invert (cq_rt.h): inside an open region the result rail is made now (a forward form returns
 * it at the call) and o ^= f(args) is recorded; it is its own inverse. A literal operand is a
 * classical rail whose free waits for the region (cq_inv_free). */
static inline uint32_t cq_tpl_run(int op, uint32_t w, uint32_t o, const uint32_t *args, int unc);
static inline uint32_t cq_tpl_cast_run(int op, uint32_t from, uint32_t to, uint32_t o, uint32_t a, int unc);
static inline void cq_tpl_run_k_(const cq_call *k, int inv) { (void)inv; cq_tpl_run((int)k->n[0], k->n[1], k->h[0], &k->h[1], (int)k->n[2]); }
static inline void cq_tpl_cast_k_(const cq_call *k, int inv) { (void)inv; cq_tpl_cast_run((int)k->n[0], k->n[1], k->n[2], k->h[0], k->h[1], (int)k->n[3]); }

/* o ^= f(args) by the kernel of op at width w. o == 0: forward, o is a fresh |0> rail.
 * Every handle must have the width the symbol names (the result: the op's out_width). */
static inline uint32_t cq_tpl_run(int op, uint32_t w, uint32_t o, const uint32_t *args, int unc) {
    const cq_kernel_op *kop = &cq_kernel_ops[op];
    uint32_t ow = kop->out_width ? kop->out_width : w;
    int32_t h[4];
    for (int j = 0; j < kop->nargs; j++) {
        h[j] = (int32_t)args[j];
        uint32_t aw = op == CQ_KOP_SELECT && !j ? 1 : w;   /* select's condition is i1 (kmgy.14) */
        if (cq_rail_width(h[j]) != aw) { fprintf(stderr, "cq_template_%s: operand %d is not i%u\n", kop->name, j, aw); abort(); }
    }
    if (!o) o = (uint32_t)cq_rail_alloc(ow, NULL);
    else if (cq_rail_width((int32_t)o) != ow) { fprintf(stderr, "cq_template_%s: result is not i%u\n", kop->name, ow); abort(); }
    if (cq_inv_open()) {
        cq_call c = { .run = cq_tpl_run_k_, .h = { o }, .n = { (uint32_t)op, w, (uint32_t)unc } };
        for (int j = 0; j < kop->nargs; j++) c.h[1 + j] = args[j];
        cq_call_do(&c);
        return o;
    }
    const cq_kernel *k = cq_kernel_pick(op, w);
    (unc && k->unc ? k->unc : k->emit)((int32_t)o, h, w);
    return o;
}

/* cq_template_<name>_<n> and its _unc for registry op kop at width bits. */
#define CQ_TPL_UNARY_(name, kop, n, bits) \
    uint32_t cq_template_##name##_##n(uint32_t a) { return cq_tpl_run(kop, bits, 0, &a, 0); } \
    void cq_template_##name##_##n##_unc(uint32_t o, uint32_t a) { cq_tpl_run(kop, bits, o, &a, 1); }

/* ---- Casts (kmgy.15): the operand is i<from>, the result i<to>; the kernel is keyed by to. */
static inline uint32_t cq_tpl_cast_run(int op, uint32_t from, uint32_t to, uint32_t o, uint32_t a, int unc) {
    const cq_kernel_op *kop = &cq_kernel_ops[op];
    if (cq_rail_width((int32_t)a) != from) { fprintf(stderr, "cq_template_%s: operand is not i%u\n", kop->name, from); abort(); }
    if (!o) o = (uint32_t)cq_rail_alloc(to, NULL);
    else if (cq_rail_width((int32_t)o) != to) { fprintf(stderr, "cq_template_%s: result is not i%u\n", kop->name, to); abort(); }
    if (cq_inv_open()) { cq_call_do(&(cq_call){ .run = cq_tpl_cast_k_, .h = { o, a }, .n = { (uint32_t)op, from, to, (uint32_t)unc } }); return o; }
    int32_t h = (int32_t)a;
    const cq_kernel *k = cq_kernel_pick(op, to);
    (unc && k->unc ? k->unc : k->emit)((int32_t)o, &h, to);
    return o;
}
/* cq_template_<name>_<f>_to_<t> and its _unc, for registry op kop. */
#define CQ_TPL_CAST_(name, kop, f, fb, t, tb) \
    uint32_t cq_template_##name##_##f##_to_##t(uint32_t a) { return cq_tpl_cast_run(kop, fb, tb, 0, a, 0); } \
    void cq_template_##name##_##f##_to_##t##_unc(uint32_t o, uint32_t a) { cq_tpl_cast_run(kop, fb, tb, o, a, 1); }

/* ---- Binary ops (a, b) and their literal shapes _hl (a, lit), _lh (lit, b) (kmgy.10).
 * A literal is a classical rail for the call: the kernel folds it into the gates, and
 * freeing it emits nothing (a kernel never targets an operand). */
static inline uint32_t cq_tpl_run_lit(int op, uint32_t w, uint32_t o, uint32_t h, const uint64_t *v, int lit_first, int unc) {
    uint32_t l = (uint32_t)cq_rail_alloc(w, v), args[2] = { lit_first ? l : h, lit_first ? h : l };
    o = cq_tpl_run(op, w, o, args, unc);
    cq_inv_free((int32_t)l);
    return o;
}
#define CQ_TPL_BINARY_(name, kop, n, bits) \
    uint32_t cq_template_##name##_##n(uint32_t a, uint32_t b) { const uint32_t h[2] = { a, b }; return cq_tpl_run(kop, bits, 0, h, 0); } \
    void cq_template_##name##_##n##_unc(uint32_t o, uint32_t a, uint32_t b) { const uint32_t h[2] = { a, b }; cq_tpl_run(kop, bits, o, h, 1); }
/* LIT(bits) is the literal's parameter list (CQ_TPL_LIT_* of cq_template.h), V() declares its limbs v[]
 * (function-like, so V can pass through further macros). */
#define CQ_TPL_HL_(name, kop, n, bits, LIT, V) \
    uint32_t cq_template_##name##_##n##_hl(uint32_t a, LIT(bits)) { V(); return cq_tpl_run_lit(kop, bits, 0, a, v, 0, 0); } \
    void cq_template_##name##_##n##_hl_unc(uint32_t o, uint32_t a, LIT(bits)) { V(); cq_tpl_run_lit(kop, bits, o, a, v, 0, 1); }
#define CQ_TPL_LH_(name, kop, n, bits, LIT, V) \
    uint32_t cq_template_##name##_##n##_lh(LIT(bits), uint32_t b) { V(); return cq_tpl_run_lit(kop, bits, 0, b, v, 1, 0); } \
    void cq_template_##name##_##n##_lh_unc(uint32_t o, LIT(bits), uint32_t b) { V(); cq_tpl_run_lit(kop, bits, o, b, v, 1, 1); }
#define CQ_TPL_V_NARROW_() const uint64_t v[2] = { (uint64_t)lit, (uint64_t)((unsigned __int128)lit >> 64) }
#define CQ_TPL_V_WIDE_() const uint64_t v[4] = { l0, l1, l2, l3 }

/* ---- Ternary ops (a, b, k) in every shape of CQ_TPL_TERN_SHAPES (kmgy.13): bit j of lits marks
 * operand j a literal, its limbs in v[j]; it becomes a classical rail for the call. */
static inline uint32_t cq_tpl_run_lits(int op, uint32_t w, uint32_t o, uint32_t *h, unsigned lits, const uint64_t (*v)[4], int unc) {
    for (int j = 0; j < 3; j++) if (lits >> j & 1) h[j] = (uint32_t)cq_rail_alloc(w, v[j]);
    o = cq_tpl_run(op, w, o, h, unc);
    for (int j = 0; j < 3; j++) if (lits >> j & 1) cq_inv_free((int32_t)h[j]);
    return o;
}
#define CQ_TPL_LIMBS_NARROW_(x) (uint64_t)(x), (uint64_t)((unsigned __int128)(x) >> 64)
#define CQ_TPL_LIMBS_WIDE_(x) x##0, x##1, x##2, x##3
#define CQ_TPL_H_q_(LIMBS, x) x
#define CQ_TPL_H_l_(LIMBS, x) 0
#define CQ_TPL_V_q_(LIMBS, x) { 0 }
#define CQ_TPL_V_l_(LIMBS, x) { LIMBS(x) }
#define CQ_TPL_M_q_ 0u
#define CQ_TPL_M_l_ 1u
#define CQ_TPL_TERN_ARGS_(LIMBS, s0, s1, s2) \
    uint32_t h[3] = { CQ_TPL_H_##s0##_(LIMBS, a), CQ_TPL_H_##s1##_(LIMBS, b), CQ_TPL_H_##s2##_(LIMBS, k) }; \
    const uint64_t v[3][4] = { CQ_TPL_V_##s0##_(LIMBS, a), CQ_TPL_V_##s1##_(LIMBS, b), CQ_TPL_V_##s2##_(LIMBS, k) }; \
    const unsigned lits = CQ_TPL_M_##s0##_ | CQ_TPL_M_##s1##_ << 1 | CQ_TPL_M_##s2##_ << 2;
/* One shape: use as CQ_TPL_TERN_SHAPES(CQ_TPL_TERN_, name, kop, n, bits, NLIT, LIMBS). */
#define CQ_TPL_TERN_(name, kop, n, bits, NLIT, LIMBS, s0, s1, s2, sfx) \
    uint32_t cq_template_##name##_##n##sfx(CQ_TPL_TERN_PARAMS_(NLIT, bits, 0, s0, s1, s2)) { \
        CQ_TPL_TERN_ARGS_(LIMBS, s0, s1, s2) return cq_tpl_run_lits(kop, bits, 0, h, lits, v, 0); } \
    void cq_template_##name##_##n##sfx##_unc(uint32_t o, CQ_TPL_TERN_PARAMS_(NLIT, bits, 1, s0, s1, s2)) { \
        CQ_TPL_TERN_ARGS_(LIMBS, s0, s1, s2) cq_tpl_run_lits(kop, bits, o, h, lits, v, 1); }

#endif
