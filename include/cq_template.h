/* Public integer templates of BACKEND_ABI §2 (libcq_templates.a), generated per width
 * from the lists in cq_widths.h. A handle is the ABI's i32.
 *   forward  uint32_t cq_template_X(args...)       a fresh rail o, o ^= f(args), returns o
 *   _unc     void cq_template_X_unc(uint32_t o, args...)   o ^= f(args): back to |0>, not freed
 * Operands are unchanged and no ancilla is left behind.
 *
 * One-operand ops have no literal shape: the shape rule (ABI §2.1, CR §1.2) names literal
 * shapes for binary, select and ternary ops only, and a one-operand op on a classical value
 * is not tainted, so the compiler never emits it (CR rule 4). Bare form and _unc only. */
#ifndef CQ_TEMPLATE_H
#define CQ_TEMPLATE_H
#include <stdint.h>
#include "cq_widths.h"

#define CQ_TPL_UNARY_DECL_(op, n) \
    uint32_t cq_template_##op##_##n(uint32_t a); \
    void cq_template_##op##_##n##_unc(uint32_t o, uint32_t a);

/* §2.8: bswap only where W is a multiple of 16, bitreverse at every width. */
#define CQ_TPL_BSWAP_DECL_(n, bits) CQ_TPL_UNARY_DECL_(bswap, n)
#define CQ_TPL_BITREVERSE_DECL_(n, bits) CQ_TPL_UNARY_DECL_(bitreverse, n)
CQ_WIDTHS_X16(CQ_TPL_BSWAP_DECL_)
CQ_WIDTHS(CQ_TPL_BITREVERSE_DECL_)

/* §2.6 integer casts, each pair of cq_cast_pairs.h: cq_template_zext_<F>_to_<T> and sext for F < T,
 * trunc for F > T, and the _unc of each. One operand, no literal shape (§2.1 names shapes for
 * binary, select and ternary ops; a cast of a classical value is not tainted, so never emitted).
 * Same forward/_unc contract as above; the result is T bits wide. */
#include "cq_cast_pairs.h"
#define CQ_TPL_CAST_DECL_(op, f, t) \
    uint32_t cq_template_##op##_##f##_to_##t(uint32_t a); \
    void cq_template_##op##_##f##_to_##t##_unc(uint32_t o, uint32_t a);
#define CQ_TPL_ZEXT_DECL_(f, fb, t, tb) CQ_TPL_CAST_DECL_(zext, f, t)
#define CQ_TPL_SEXT_DECL_(f, fb, t, tb) CQ_TPL_CAST_DECL_(sext, f, t)
#define CQ_TPL_TRUNC_DECL_(f, fb, t, tb) CQ_TPL_CAST_DECL_(trunc, t, f)   /* the pair read downwards */
CQ_CAST_PAIRS(CQ_TPL_ZEXT_DECL_)
CQ_CAST_PAIRS(CQ_TPL_SEXT_DECL_)
CQ_CAST_PAIRS(CQ_TPL_TRUNC_DECL_)

/* §2.2 add, sub and §2.7 {u,s}{add,sub}_overflow (result i1), {u,s}{add,sub}_sat (kmgy.10).
 * Shapes (§2.1): bare (a, b), _hl (a, lit), and _lh (lit, b) for the non-commutative ones
 * only (sub, usub_*, ssub_*); neg is sub_<W>_lh with lit 0. A literal is <T> of cqrt.h: the
 * smallest unsigned type holding W bits (only the low W bits are read), at i200 four 64-bit
 * limbs, low first. */
#include "cqrt.h"
/* LIT(bits) is the literal's parameter list: CQ_TPL_LIT_NARROW_ or CQ_TPL_LIT_WIDE_. */
#define CQ_TPL_LIT_NARROW_(bits) CQRT_UINT(bits) lit
#define CQ_TPL_LIT_WIDE_(bits) CQRT_LIMBS_
#define CQ_TPL_BIN_DECL_(op, n, bits, LIT) \
    uint32_t cq_template_##op##_##n(uint32_t a, uint32_t b); \
    void cq_template_##op##_##n##_unc(uint32_t o, uint32_t a, uint32_t b); \
    uint32_t cq_template_##op##_##n##_hl(uint32_t a, LIT(bits)); \
    void cq_template_##op##_##n##_hl_unc(uint32_t o, uint32_t a, LIT(bits));
#define CQ_TPL_LH_DECL_(op, n, bits, LIT) \
    uint32_t cq_template_##op##_##n##_lh(LIT(bits), uint32_t b); \
    void cq_template_##op##_##n##_lh_unc(uint32_t o, LIT(bits), uint32_t b);
#define CQ_TPL_ARITH_DECL_(n, bits, LIT) \
    CQ_TPL_BIN_DECL_(add, n, bits, LIT) CQ_TPL_BIN_DECL_(uadd_overflow, n, bits, LIT) CQ_TPL_BIN_DECL_(sadd_overflow, n, bits, LIT) \
    CQ_TPL_BIN_DECL_(uadd_sat, n, bits, LIT) CQ_TPL_BIN_DECL_(sadd_sat, n, bits, LIT) \
    CQ_TPL_BIN_DECL_(sub, n, bits, LIT) CQ_TPL_BIN_DECL_(usub_overflow, n, bits, LIT) CQ_TPL_BIN_DECL_(ssub_overflow, n, bits, LIT) \
    CQ_TPL_BIN_DECL_(usub_sat, n, bits, LIT) CQ_TPL_BIN_DECL_(ssub_sat, n, bits, LIT) \
    CQ_TPL_LH_DECL_(sub, n, bits, LIT) CQ_TPL_LH_DECL_(usub_overflow, n, bits, LIT) CQ_TPL_LH_DECL_(ssub_overflow, n, bits, LIT) \
    CQ_TPL_LH_DECL_(usub_sat, n, bits, LIT) CQ_TPL_LH_DECL_(ssub_sat, n, bits, LIT)
#define CQ_TPL_ARITH_DECL_NARROW_(n, bits) CQ_TPL_ARITH_DECL_(n, bits, CQ_TPL_LIT_NARROW_)
#define CQ_TPL_ARITH_DECL_WIDE_(n, bits) CQ_TPL_ARITH_DECL_(n, bits, CQ_TPL_LIT_WIDE_)
CQ_WIDTHS_NARROW(CQ_TPL_ARITH_DECL_NARROW_)
CQ_WIDTHS_WIDE(CQ_TPL_ARITH_DECL_WIDE_)


/* §2.3 bitwise and shifts (kmgy.13). and, or, xor: bare and _hl (commutative, so no _lh; not is
 * xor_<W>_hl with all ones). shl, lshr, ashr (a, k): bare, _hl (literal k), _lh (literal a).
 * fshl, fshr (a, b, k): bare and every mixed shape _qql _qlq _qll _lqq _lql _llq, a letter per
 * operand, q a handle and l a literal. The amount k has width W like the value, and every shift
 * is by ⟦k⟧ mod W (CR §2). A literal is <T> as above, at i200 four limbs named a0..a3 (b, k). */
#define CQ_TPL_NLIT_NARROW_(bits, x) CQRT_UINT(bits) x
#define CQ_TPL_NLIT_WIDE_(bits, x) uint64_t x##0, uint64_t x##1, uint64_t x##2, uint64_t x##3
#define CQ_TPL_P_q_(LIT, bits, x) uint32_t x
#define CQ_TPL_P_l_(LIT, bits, x) LIT(bits, x)
/* The parameter list (a, b, k) of shape s0 s1 s2 at width bits; LIT is CQ_TPL_NLIT_*. */
#define CQ_TPL_TERN_PARAMS_(LIT, bits, s0, s1, s2) \
    CQ_TPL_P_##s0##_(LIT, bits, a), CQ_TPL_P_##s1##_(LIT, bits, b), CQ_TPL_P_##s2##_(LIT, bits, k)
#define CQ_TPL_TERN_DECL_(op, n, bits, LIT, s0, s1, s2, sfx) \
    uint32_t cq_template_##op##_##n##sfx(CQ_TPL_TERN_PARAMS_(LIT, bits, s0, s1, s2)); \
    void cq_template_##op##_##n##sfx##_unc(uint32_t o, CQ_TPL_TERN_PARAMS_(LIT, bits, s0, s1, s2));
/* X(args..., s0, s1, s2, suffix) for each of the seven shapes of a ternary op. */
#define CQ_TPL_TERN_SHAPES(X, ...) \
    X(__VA_ARGS__, q, q, q, ) X(__VA_ARGS__, q, q, l, _qql) X(__VA_ARGS__, q, l, q, _qlq) X(__VA_ARGS__, q, l, l, _qll) \
    X(__VA_ARGS__, l, q, q, _lqq) X(__VA_ARGS__, l, q, l, _lql) X(__VA_ARGS__, l, l, q, _llq)
#define CQ_TPL_BITSH_DECL_(n, bits, LIT, NLIT) \
    CQ_TPL_BIN_DECL_(and, n, bits, LIT) CQ_TPL_BIN_DECL_(or, n, bits, LIT) CQ_TPL_BIN_DECL_(xor, n, bits, LIT) \
    CQ_TPL_BIN_DECL_(shl, n, bits, LIT) CQ_TPL_BIN_DECL_(lshr, n, bits, LIT) CQ_TPL_BIN_DECL_(ashr, n, bits, LIT) \
    CQ_TPL_LH_DECL_(shl, n, bits, LIT) CQ_TPL_LH_DECL_(lshr, n, bits, LIT) CQ_TPL_LH_DECL_(ashr, n, bits, LIT) \
    CQ_TPL_TERN_SHAPES(CQ_TPL_TERN_DECL_, fshl, n, bits, NLIT) CQ_TPL_TERN_SHAPES(CQ_TPL_TERN_DECL_, fshr, n, bits, NLIT)
#define CQ_TPL_BITSH_DECL_NARROW_(n, bits) CQ_TPL_BITSH_DECL_(n, bits, CQ_TPL_LIT_NARROW_, CQ_TPL_NLIT_NARROW_)
#define CQ_TPL_BITSH_DECL_WIDE_(n, bits) CQ_TPL_BITSH_DECL_(n, bits, CQ_TPL_LIT_WIDE_, CQ_TPL_NLIT_WIDE_)
CQ_WIDTHS_NARROW(CQ_TPL_BITSH_DECL_NARROW_)
CQ_WIDTHS_WIDE(CQ_TPL_BITSH_DECL_WIDE_)

#endif
