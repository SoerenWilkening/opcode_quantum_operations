/* Internal (not ABI, not installed): the integer kernel registry.
 * A kernel emits o ^= f(args) on rails (cq_rt.h): operands unchanged, ancillae back
 * to |0>. Per (operation, width) the shallowest candidate that passes its sampled
 * check ships (tie: fewer qubits, fewer gates, registration order). The choice is
 * the generated table kernel_choice.c, written with docs/DEPTH.md by tools/depth_table.
 *
 * A template or the angle ladder calls:  cq_kernel_pick(CQ_KOP_X, w)->emit(o, args, w); */
#ifndef CQ_KERNELS_H
#define CQ_KERNELS_H
#include <stdint.h>
#include "cq_widths.h"

typedef void (*cq_kernel_fn)(int32_t o, const int32_t *args, uint32_t w);
typedef struct {
    const char  *name;
    cq_kernel_fn emit;
    cq_kernel_fn unc;      /* NULL: emit again (o ^= f twice is the identity) */
    const char  *source;   /* literature source, printed in docs/DEPTH.md */
} cq_kernel;
typedef struct {
    const char      *name;       /* also seeds the samples: every candidate sees the same inputs */
    const char      *what;       /* one line for docs/DEPTH.md */
    int              nargs;      /* operands, each of width w */
    uint32_t         out_width;  /* 0: w */
    const cq_kernel *cand;
    int              ncand;
    uint32_t         width_step; /* 0: every width; else only widths that are a multiple of it */
    uint32_t         samples;    /* depth-table samples per cell, 0: the harness default (1000). A slow
                                  * op lowers it: with every operand a qubit the cost does not depend on the values */
} cq_kernel_op;
/* width_step of a cast: it is keyed by the RESULT width w, the operand width FROM varies (the
 * depth tables take the worst case over FROM): FROM < w (zext, sext) or FROM > w (trunc). */
#define CQ_STEP_FROM_BELOW UINT32_MAX
#define CQ_STEP_FROM_ABOVE (UINT32_MAX - 1)
extern const uint32_t cq_kernel_widths[];
extern const uint32_t cq_kernel_nwidths;
static inline int cq_kernel_has_width(const cq_kernel_op *op, uint32_t w) {
    if (op->width_step == CQ_STEP_FROM_BELOW) return w > cq_kernel_widths[0];
    if (op->width_step == CQ_STEP_FROM_ABOVE) return w < cq_kernel_widths[cq_kernel_nwidths - 1];
    return !op->width_step || w % op->width_step == 0;
}

/* The adder-based ops of kmgy.10 (ABI §2.2, §2.7): X(name, binary op commutes, result width). */
#define CQ_ARITH_OPS(X) \
    X(add, 1, 0) \
    X(sub, 0, 0) \
    X(uadd_overflow, 1, 1) \
    X(sadd_overflow, 1, 1) \
    X(usub_overflow, 0, 1) \
    X(ssub_overflow, 0, 1) \
    X(uadd_sat, 1, 0) \
    X(sadd_sat, 1, 0) \
    X(usub_sat, 0, 0) \
    X(ssub_sat, 0, 0)
#define CQ_KOP_ARITH_(name, comm, ow) CQ_KOP_ARITH_##name,
/* Divide and remainder (kmgy.12, ABI §2.2), all non-commutative: X(name). */
#define CQ_DIV_OPS(X) X(udiv) X(urem) X(sdiv) X(srem)
#define CQ_KOP_DIV_(name) CQ_KOP_DIV_##name,
/* Compare, select, min/max, abs (kmgy.14, ABI §2.4, §2.5, §2.7). icmp: X(pred, base flag CQ_CMP_*,
 * operands swapped, result negated); eq/ne take the CQ_CMP_EQ kernels, the rest CQ_CMP_ULT/SLT.
 * min/max: X(name, base flag, 1: the result is b when [a < b]). */
#define CQ_ICMP_EQ_PREDS(X) X(eq, CQ_CMP_EQ, 0, 0) X(ne, CQ_CMP_EQ, 0, 1)
#define CQ_ICMP_ORD_PREDS(X) \
    X(ult, CQ_CMP_ULT, 0, 0) X(ugt, CQ_CMP_ULT, 1, 0) X(uge, CQ_CMP_ULT, 0, 1) X(ule, CQ_CMP_ULT, 1, 1) \
    X(slt, CQ_CMP_SLT, 0, 0) X(sgt, CQ_CMP_SLT, 1, 0) X(sge, CQ_CMP_SLT, 0, 1) X(sle, CQ_CMP_SLT, 1, 1)
#define CQ_ICMP_PREDS(X) CQ_ICMP_EQ_PREDS(X) CQ_ICMP_ORD_PREDS(X)
#define CQ_MINMAX_OPS(X) X(smax, CQ_CMP_SLT, 1) X(smin, CQ_CMP_SLT, 0) X(umax, CQ_CMP_ULT, 1) X(umin, CQ_CMP_ULT, 0)
#define CQ_KOP_ICMP_(p, base, swap, neg) CQ_KOP_ICMP_##p,
#define CQ_KOP_MINMAX_(name, base, max) CQ_KOP_##name,

/* One line per operation; its candidates are listed in registry.c. */
enum {
    CQ_KOP_PARITY,   /* demo of the depth tooling, not an ABI template */
    CQ_KOP_BSWAP,
    CQ_KOP_BITREVERSE,
    CQ_KOP_ZEXT,     /* casts: key = result width, see cq_kernel_op.cast */
    CQ_KOP_SEXT,
    CQ_KOP_TRUNC,
    CQ_ARITH_OPS(CQ_KOP_ARITH_)   /* CQ_KOP_ARITH_add, ...; CQ_KOP_ADD below */
    CQ_KOP_AND,      /* bitwise and shifts (kmgy.13); the amount is the last operand */
    CQ_KOP_OR,
    CQ_KOP_XOR,
    CQ_KOP_SHL,
    CQ_KOP_LSHR,
    CQ_KOP_ASHR,
    CQ_KOP_FSHL,
    CQ_KOP_FSHR,
    CQ_DIV_OPS(CQ_KOP_DIV_)       /* CQ_KOP_DIV_udiv, ... (kmgy.12) */
    CQ_ICMP_PREDS(CQ_KOP_ICMP_)   /* CQ_KOP_ICMP_eq, ... (kmgy.14) */
    CQ_KOP_SELECT,                /* (c, a1, a2): c is i1, the arms W */
    CQ_MINMAX_OPS(CQ_KOP_MINMAX_) /* CQ_KOP_smax, ... */
    CQ_KOP_ABS,
    CQ_KOP_CTPOP,    /* bit counting (kmgy.16) */
    CQ_KOP_CTLZ,
    CQ_KOP_CTTZ,
    CQ_KOP_MUL,      /* multiply (kmgy.11) */
    CQ_KOP_UMUL_OVERFLOW,
    CQ_KOP_SMUL_OVERFLOW,
    CQ_KOP_COUNT
};
extern const cq_kernel_op cq_kernel_ops[CQ_KOP_COUNT];

#define CQ_KW_ONE_(name, bits) +1
enum { CQ_NWIDTHS = 0 CQ_WIDTHS(CQ_KW_ONE_) };
extern const uint32_t cq_kernel_widths[CQ_NWIDTHS];                  /* CQ_WIDTHS in order */
extern const uint8_t  cq_kernel_choice[CQ_KOP_COUNT][CQ_NWIDTHS];   /* generated: kernel_choice.c */

/* The shipped candidate of op at width w. */
const cq_kernel *cq_kernel_pick(int op, uint32_t w);

/* ---- Kernels */
void cq_k_parity_chain(int32_t o, const int32_t *args, uint32_t w);
void cq_k_parity_tree (int32_t o, const int32_t *args, uint32_t w);
void cq_k_bswap       (int32_t o, const int32_t *args, uint32_t w);
void cq_k_bitreverse  (int32_t o, const int32_t *args, uint32_t w);
void cq_k_cast_copy   (int32_t o, const int32_t *args, uint32_t w);   /* zext, trunc */
void cq_k_sext_chain  (int32_t o, const int32_t *args, uint32_t w);
void cq_k_sext_tree   (int32_t o, const int32_t *args, uint32_t w);
#define CQ_K_ARITH_DECL_(name, comm, ow) \
    void cq_k_##name##_ripple(int32_t o, const int32_t *args, uint32_t w); \
    void cq_k_##name##_lookahead(int32_t o, const int32_t *args, uint32_t w);
CQ_ARITH_OPS(CQ_K_ARITH_DECL_)
void cq_k_and(int32_t o, const int32_t *args, uint32_t w);   /* bitwise.c */
void cq_k_or (int32_t o, const int32_t *args, uint32_t w);
void cq_k_xor(int32_t o, const int32_t *args, uint32_t w);
#define CQ_K_SHIFT_DECL_(cand) \
    void cq_k_shl_##cand (int32_t o, const int32_t *args, uint32_t w); \
    void cq_k_lshr_##cand(int32_t o, const int32_t *args, uint32_t w); \
    void cq_k_ashr_##cand(int32_t o, const int32_t *args, uint32_t w); \
    void cq_k_fshl_##cand(int32_t o, const int32_t *args, uint32_t w); \
    void cq_k_fshr_##cand(int32_t o, const int32_t *args, uint32_t w);
CQ_K_SHIFT_DECL_(barrel)       /* shift.c */
CQ_K_SHIFT_DECL_(barrel_fan)
CQ_K_SHIFT_DECL_(barrel_fold)
#define CQ_K_DIV_DECL_(name) \
    void cq_k_##name##_ripple(int32_t o, const int32_t *args, uint32_t w); \
    void cq_k_##name##_lookahead(int32_t o, const int32_t *args, uint32_t w); \
    void cq_k_##name##_r4(int32_t o, const int32_t *args, uint32_t w); \
    void cq_k_##name##_r8(int32_t o, const int32_t *args, uint32_t w);
CQ_DIV_OPS(CQ_K_DIV_DECL_)    /* div.c */

/* ---- The adder (kmgy.10), for every integer circuit built on addition (mul, div, compare,
 * bit counting, the angle ladder).
 *
 * A tape is a Bennett region: gates recorded through cq_tape_x/cx/ccx are emitted at once
 * (folding classical bits) and cq_tape_undo emits them again in reverse, then returns the
 * tape's own bits to the pool. So: record the computation, XOR the result into o with plain
 * cq_cx/cq_ccx (not recorded), undo. Tapes nest freely (each owns its bits); zero-init one:
 *     cq_tape t = { 0 }; cq_sum r = cq_adder(cq_adder_kind(w), &t, a, b, 0, NULL, w, CQ_ADD_SUM);
 *     for (i...) cq_cx(&r.s[i], &o[i]);  cq_tape_undo(&t);
 * Never record a gate whose target is an operand or o: the undo would emit it again. */
#include <stddef.h>
#include "cq_rt.h"
typedef struct { const cq_bit *a, *b; cq_bit *t; } cq_tape_gate;   /* b NULL: CX; a NULL too: X */
typedef struct {
    cq_tape_gate *g; size_t n, cap;
    cq_bit **blk; uint32_t *blkn; size_t nblk, capblk, capblkn;
} cq_tape;
cq_bit *cq_tape_bits(cq_tape *t, uint32_t n);      /* n fresh ZERO bits; a qubit only once a gate forces one */
void    cq_tape_x  (cq_tape *t, cq_bit *q);
void    cq_tape_cx (cq_tape *t, const cq_bit *c, cq_bit *q);
void    cq_tape_ccx(cq_tape *t, const cq_bit *a, const cq_bit *b, cq_bit *q);
void    cq_tape_undo(cq_tape *t);                  /* reverse, release the bits; t is empty again */
cq_bit *cq_tape_fanout(cq_tape *t, const cq_bit *v, uint32_t n);   /* n copies of v, depth ceil(log2(n+1)) */

/* The adder candidates, in the order every adder-based op lists them in registry.c. */
enum { CQ_ADDER_RIPPLE, CQ_ADDER_LOOKAHEAD };
#define CQ_KOP_ADD CQ_KOP_ARITH_add
typedef struct { cq_bit *s; cq_bit *c; } cq_sum;
enum { CQ_ADD_SUM = 1, CQ_ADD_COUT = 2 };
/* Records onto t the n-bit sum x = a + (nb ? ~b : b) + cin. want: CQ_ADD_SUM, CQ_ADD_COUT or both.
 *   s[i], i < n: bit i of x (CQ_ADD_SUM; else NULL)
 *   c[i]: the carry into bit i, for i < n; c[0] = cin. With CQ_ADD_COUT also c[n], the carry
 *         out (a compare's flag: with nb = 1 and cin = 1, c[n] = [a >=u b]), and c[n] ^ c[n-1]
 *         is signed overflow. Without it the top carry is not computed (shallower).
 * a, b: n bits each, read only (controls only), classical or quantum, may alias each other.
 * cin: one bit or NULL for 0. Classical bits fold: an all-classical call takes no qubit and
 * emits no gate; a classical operand (a literal) makes it cheaper. Both outputs are tape bits,
 * valid until cq_tape_undo. */
cq_sum cq_adder(int kind, cq_tape *t, const cq_bit *a, const cq_bit *b, int nb, const cq_bit *cin, uint32_t n, int want);
int    cq_adder_kind(uint32_t n);                  /* the adder add ships at width n (generated table) */

/* ---- Bit counting (kmgy.16, src/kernels/count.c), for the ctpop/ctlz/cttz templates and any circuit
 * that needs a count (a normalisation step: leading zeros).
 * x: n >= 1 bit pointers, classical or quantum, read only; x[n-1] is the top bit. Any view is free:
 * reversed (trailing zeros = cq_clz of the reversed pointers), strided, constants mixed in.
 * Records onto t and sets r[0 .. m-1] to the count's bits, low first, m = cq_count_width(n) (the bit
 * length of n, so n fits). The r bits are read only (some may be x bits or constants), valid until
 * cq_tape_undo. All-classical x takes no qubit and emits no gate.
 *   cq_popcount: the number of set bits. kind CQ_POPCOUNT_LINEAR (one adder per bit) or _CSA
 *                (carry-save tree, one final adder).
 *   cq_clz:      the number of zero bits above the highest set one, n when all are zero.
 *                kind CQ_CLZ_THERMO (prefix AND, XOR trees) or _TREE (priority-encoder tree).
 * The kinds are in the order the registry lists ctpop's and ctlz's candidates. */
enum { CQ_POPCOUNT_LINEAR, CQ_POPCOUNT_CSA };
enum { CQ_CLZ_THERMO, CQ_CLZ_TREE };
uint32_t cq_count_width(uint32_t n);
void cq_popcount(int kind, cq_tape *t, const cq_bit *const *x, uint32_t n, const cq_bit **r);
void cq_clz(int kind, cq_tape *t, const cq_bit *const *x, uint32_t n, const cq_bit **r);
void cq_k_ctpop_linear(int32_t o, const int32_t *args, uint32_t w);
void cq_k_ctpop_csa   (int32_t o, const int32_t *args, uint32_t w);
void cq_k_ctlz_thermo (int32_t o, const int32_t *args, uint32_t w);
void cq_k_ctlz_tree   (int32_t o, const int32_t *args, uint32_t w);
void cq_k_cttz_thermo (int32_t o, const int32_t *args, uint32_t w);
void cq_k_cttz_tree   (int32_t o, const int32_t *args, uint32_t w);

/* ---- Compare and select (kmgy.14, src/kernels/cmp.c), for the icmp/select/min/max/abs templates and
 * any circuit that needs a flag or a merge (the tape INPUT rows, the quantum-address bus, the divider).
 * a, b, a1, a2: n >= 1 bits each, read only, classical or quantum (a literal folds), may alias.
 * f, o: the target. rec = 0: its gates are emitted plainly (o of a kernel, kept after cq_tape_undo);
 * rec = 1: they are recorded on t too (f from cq_tape_bits, cleared again by cq_tape_undo).
 * Scratch is recorded on t either way: undo t after use. All-classical: no qubit, no gate.
 *   cq_cmp:    f ^= [a pred b], pred CQ_CMP_EQ (a == b), CQ_CMP_ULT (a <u b), CQ_CMP_SLT (a <s b).
 *              The other predicates: swap a and b, or cq_x(f) (ne, uge, ule, sge, sle).
 *              kind: for EQ CQ_CMP_CHAIN or _TREE (AND of the XNORs); for ULT, SLT CQ_CMP_RIPPLE,
 *              _LOOKAHEAD (the carry out of a + ~b + 1) or _TREE (log-depth comparator).
 *   cq_select: o[i] ^= c ? a1[i] : a2[i] on n bits; c one bit. kind CQ_SEL_*.
 * cq_cmp_kind / cq_select_kind: the candidate icmp_eq/ult/slt and select ship at width n. */
enum { CQ_CMP_EQ, CQ_CMP_ULT, CQ_CMP_SLT };
enum { CQ_CMP_CHAIN = 0, CQ_CMP_RIPPLE = 0, CQ_CMP_LOOKAHEAD = 1, CQ_CMP_TREE = 2 };
enum { CQ_SEL_CCX2, CQ_SEL_CCX2_FANOUT, CQ_SEL_DIFF, CQ_SEL_DIFF_FANOUT };   /* registry order */
void cq_cmp(int kind, cq_tape *t, int pred, const cq_bit *a, const cq_bit *b, uint32_t n, cq_bit *f, int rec);
void cq_select(int kind, cq_tape *t, const cq_bit *c, const cq_bit *a1, const cq_bit *a2, cq_bit *o, uint32_t n, int rec);
int  cq_cmp_kind(int pred, uint32_t n);
int  cq_select_kind(uint32_t n);
#define CQ_K_ICMP_EQ_DECL_(p, base, swap, neg) \
    void cq_k_icmp_##p##_chain(int32_t o, const int32_t *args, uint32_t w); \
    void cq_k_icmp_##p##_tree(int32_t o, const int32_t *args, uint32_t w);
#define CQ_K_ICMP_ORD_DECL_(p, base, swap, neg) \
    void cq_k_icmp_##p##_ripple(int32_t o, const int32_t *args, uint32_t w); \
    void cq_k_icmp_##p##_lookahead(int32_t o, const int32_t *args, uint32_t w); \
    void cq_k_icmp_##p##_tree(int32_t o, const int32_t *args, uint32_t w);
CQ_ICMP_EQ_PREDS(CQ_K_ICMP_EQ_DECL_)
CQ_ICMP_ORD_PREDS(CQ_K_ICMP_ORD_DECL_)
void cq_k_select_ccx2(int32_t o, const int32_t *args, uint32_t w);
void cq_k_select_ccx2_fanout(int32_t o, const int32_t *args, uint32_t w);
void cq_k_select_diff(int32_t o, const int32_t *args, uint32_t w);
void cq_k_select_diff_fanout(int32_t o, const int32_t *args, uint32_t w);
/* min/max: the flag by each compare kind, then the select DIFF or DIFF_FANOUT. */
#define CQ_K_MINMAX_DECL_(name, base, max) \
    void cq_k_##name##_ripple(int32_t o, const int32_t *args, uint32_t w); \
    void cq_k_##name##_ripple_fanout(int32_t o, const int32_t *args, uint32_t w); \
    void cq_k_##name##_lookahead(int32_t o, const int32_t *args, uint32_t w); \
    void cq_k_##name##_lookahead_fanout(int32_t o, const int32_t *args, uint32_t w); \
    void cq_k_##name##_tree(int32_t o, const int32_t *args, uint32_t w); \
    void cq_k_##name##_tree_fanout(int32_t o, const int32_t *args, uint32_t w);
CQ_MINMAX_OPS(CQ_K_MINMAX_DECL_)
/* abs: a ^ sign (sign fanned out or not), plus sign by the adder. */
void cq_k_abs_ripple(int32_t o, const int32_t *args, uint32_t w);
void cq_k_abs_ripple_fanout(int32_t o, const int32_t *args, uint32_t w);
void cq_k_abs_lookahead(int32_t o, const int32_t *args, uint32_t w);
void cq_k_abs_lookahead_fanout(int32_t o, const int32_t *args, uint32_t w);

/* ---- The multiplier (kmgy.11, src/kernels/mul.c), for mul, {u,s}mul_overflow and the angle ladder
 * (K from the significand times a window of 2^N/P: x = the 53 significand bits, y = the constant's
 * bits as classical cq_bit_const values, lo/n = the bits kept).
 * Records onto t bits lo .. lo+n-1 of the product x * y and sets out[0 .. n-1] to them (read only:
 * some may be operand bits or constants; valid until cq_tape_undo). x: nx bits, y: ny bits, read
 * only, classical or quantum, may alias. sgn = 0: unsigned; sgn = 1: both two's complement, and
 * lo + n <= nx + ny. Columns below lo are formed too (their carries reach the window), none above.
 * A classical factor bit folds: a 0 drops its partial products, a 1 makes them wires, so a constant
 * operand costs about its popcount rows; all-classical takes no qubit and emits no gate.
 * kind CQ_MUL_SHIFT_ADD (a cq_adder per row of y, depth ~ ny log) or CQ_MUL_TREE (fan-out, one CCX
 * layer, Dadda tree, one cq_adder: depth ~ log(nx) + 3 log1.5(min(nx, ny)) + log(lo+n), up to
 * ~5 nx ny scratch bits with two quantum operands: 2 10^5 at 200 x 200); registry order.
 * cq_mul_kind: what mul ships at width w (the tree everywhere but i1). */
enum { CQ_MUL_SHIFT_ADD, CQ_MUL_TREE };
void cq_mul(int kind, cq_tape *t, const cq_bit *x, uint32_t nx, const cq_bit *y, uint32_t ny, int sgn,
            uint32_t lo, uint32_t n, const cq_bit **out);
int  cq_mul_kind(uint32_t w);
#define CQ_K_MUL_DECL_(name) \
    void cq_k_##name##_shift_add(int32_t o, const int32_t *args, uint32_t w); \
    void cq_k_##name##_tree(int32_t o, const int32_t *args, uint32_t w);
CQ_K_MUL_DECL_(mul)
CQ_K_MUL_DECL_(umul_overflow)
CQ_K_MUL_DECL_(smul_overflow)

#endif
