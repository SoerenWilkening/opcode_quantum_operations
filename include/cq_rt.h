/* Runtime internals shared by the parts of libcq_backend.a: the
 * tri-valued bit, the qubit pool, the emitter and the handle table.
 * Process-global state, as the ABI has no context parameter; single-threaded.
 *
 * Every gate goes through the emitter, which folds classical operands per
 * NORTH_STAR's table and calls the seven __quantum__qis__* functions directly.
 *
 * Coinciding operands (the emitter never emits a gate with a repeated qubit):
 *   - CCX with both controls the same bit, or the same qubit: a & a = a, so it
 *     folds to CX(a, t).
 *   - A control that is the target (same bit, or same qubit index) in CX, CCX
 *     or Ry-ctl: hard error (abort), even when classical. t ^= t is not a
 *     reversible gate and the ABI never asks for it (§0, Aliasing).
 * "Same bit" means the same cq_bit pointer; two distinct classical bits are
 * independent and never coincide. */
#ifndef CQ_RT_H
#define CQ_RT_H
#include <stdint.h>
#include "cq_widths.h"

/* ---- The bit: exactly one of ZERO, ONE, Q(q). A constant's kind is its value. */
enum { CQ_BIT_ZERO = 0, CQ_BIT_ONE = 1, CQ_BIT_Q = 2 };
typedef struct { uint8_t kind; uint32_t q; } cq_bit;   /* q valid iff kind == CQ_BIT_Q */

static inline cq_bit cq_bit_const(int v)   { return (cq_bit){ (uint8_t)(v ? CQ_BIT_ONE : CQ_BIT_ZERO), 0 }; }
static inline int    cq_bit_is_q(cq_bit b) { return b.kind == CQ_BIT_Q; }

/* ---- The pool: LIFO free list over indices 0, 1, 2, ... Every free index is |0>. */
uint32_t cq_pool_take(void);
void     cq_pool_give(uint32_t q);      /* caller guarantees q is back in |0> */
uint32_t cq_pool_live(void);            /* indices currently taken */
uint32_t cq_pool_minted(void);          /* distinct indices ever handed out */

/* ---- The emitter. Controls are read only; targets may get a qubit. */
void cq_x     (cq_bit *t);
void cq_cx    (const cq_bit *c, cq_bit *t);
void cq_ccx   (const cq_bit *a, const cq_bit *b, cq_bit *t);
void cq_ry    (double theta, cq_bit *t);
void cq_ry_ctl(double theta, const cq_bit *c, cq_bit *t);
void cq_rz    (double theta, cq_bit *t);
void cq_get_qubit(cq_bit *b);           /* classical -> Q: pool index, x if ONE; no-op on Q */

/* A qubit bit: m, index back to the pool, the bit becomes the outcome.
 * A classical bit: its value, no gate. */
int  cq_measure_bit(cq_bit *b);
/* A qubit bit: m, x on outcome 1, index back to the pool. Either way the bit ends ZERO. */
void cq_reset_bit(cq_bit *b);

/* ---- Rails and handles. A handle is the ABI's i32: 1, 2, 3, ... in order of
 * allocation, never reused (0 is reserved). Widths 1..CQ_RAIL_MAX_WIDTH. */
/* The widest width of CQ_WIDTHS: the size of a union with one char[bits] member per width. */
#define CQ_W_MEMBER_(name, bits) char name[bits];
union cq_widths_max_ { CQ_WIDTHS(CQ_W_MEMBER_) };
#undef CQ_W_MEMBER_
#define CQ_RAIL_MAX_WIDTH ((uint32_t)sizeof(union cq_widths_max_))
#define CQ_LIMBS ((CQ_RAIL_MAX_WIDTH + 63) / 64)   /* 64-bit limbs of the widest value */
/* A classical rail holding v mod 2^width. v is little-endian 64-bit limbs (bit i is
 * bit i % 64 of v[i / 64]); only the ceil(width/64) limbs that hold bits 0..width-1
 * are read, bits at and above width are ignored. v == NULL: 0. No qubit, no gate. */
int32_t  cq_rail_alloc(uint32_t width, const uint64_t *v);
cq_bit  *cq_rail_bits(int32_t h);       /* aborts on a bad or freed handle; stable until free */
uint32_t cq_rail_width(int32_t h);
void     cq_rail_free(int32_t h);       /* cq_reset_bit on every bit, then drop the handle */

/* ---- cq_invert (ABI §1.13, src/invert.c). Every reversible entry point packs its call into a
 * cq_call and hands it to cq_call_do: run now, or, inside an open region, recorded and run at the
 * outermost end, reversed, run(k, 1) for the inverse (only rotations differ: angle negated).
 * The call, not the gate, is recorded: a classical bit emits no gate. Handles are kept as
 * numbers (never reused), literals by value. */
typedef struct cq_call cq_call;
struct cq_call {
    void (*run)(const cq_call *k, int inv);
    const char *name;   /* noted as an operation when it runs (cq_op_begin); NULL: run notes itself */
    uint32_t h[6];      /* handles, as run reads them */
    uint32_t n[4];      /* small integers: width, op, flags */
    double   theta;
    uint64_t v[4];      /* a literal's limbs, or a classical address */
};
void cq_call_do(const cq_call *k);
int  cq_inv_open(void);               /* a region is open */
void cq_inv_flush(void);              /* a flush point: the recorded calls run forward, now */
void cq_inv_free(int32_t h);          /* cq_rail_free, after the outermost end inside a region */
void cq_copy(uint32_t c, int ctl, uint32_t s, uint32_t d, uint32_t w);   /* cqrt_copy_<W>[_controlled] */

/* ---- Operation notes for the gate backend (cq_gates.h, src/rails.c). The gates between begin and
 * end are operation `name` (with "_unc" appended if unc) on the rails h[0..nh); 0 entries are
 * skipped. Nested pairs are not noted. */
void cq_op_begin(const char *name, int unc, const uint32_t *h, int nh);
void cq_op_end(void);

#endif
