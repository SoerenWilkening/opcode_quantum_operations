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

#endif
