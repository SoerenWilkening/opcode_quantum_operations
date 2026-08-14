/* src/bit.h — M01: the tri-valued register bit. PRD §2.2, invariant I1.
 *
 * A register bit is exactly one of ZERO, ONE, or "on qubit q". Never two,
 * never neither (I1). That single discriminant is also the quantum mask: a bit
 * is classical iff it is not on a qubit, so the mask is just "which entries
 * are CQ_BIT_Q". This is invariant I5 — there is no uint64_t classical and no
 * uint64_t qmask in this codebase, which is the whole reason i128 costs nothing
 * here: with no packed scalar, 128 is a loop bound rather than a second word
 * plus a 128-bit variant of every identity peephole.
 *
 * Header-only and all static inline, per plan §3: Layer 0, no internal
 * dependencies, no split seam. Nothing in this file allocates, emits, or
 * touches the pool — cq_materialise is M05's, and it is the only place a
 * qubit is ever allocated for data (Rule 5).
 */
#ifndef CQOPS_BIT_H
#define CQOPS_BIT_H

#include <stdint.h>

/* The three kinds.
 *
 * ZERO and ONE are numbered 0 and 1 deliberately: a constant bit's kind IS its
 * value, which is what lets cq_bit_value be a cast rather than a branch. The
 * numbering is load-bearing, so it is pinned below and re-asserted in
 * tests/test_bit.c — a renumbering must break a build, not just a comment. */
enum {
    CQ_BIT_ZERO = 0,
    CQ_BIT_ONE  = 1,
    CQ_BIT_Q    = 2
};

typedef struct {
    uint8_t  kind;   /* CQ_BIT_ZERO | CQ_BIT_ONE | CQ_BIT_Q */
    uint32_t q;      /* qubit index; valid iff kind == CQ_BIT_Q, else 0 */
} cq_bit;

_Static_assert(CQ_BIT_ZERO == 0 && CQ_BIT_ONE == 1,
               "cq_bit_value reads a constant's kind as its value");
_Static_assert(CQ_BIT_Q == 2, "CQ_BIT_Q must not collide with a constant");

/* Debug-only precondition checks. Gated exactly like every other invariant
 * check in the project: CQOPS_DEBUG_INVARIANTS gates the *checking* machinery
 * and never behaviour, so Debug and Release emit the identical gate stream
 * (include/cqops/cqops.h). In Release the condition is not evaluated. */
#if defined(CQOPS_DEBUG_INVARIANTS) && CQOPS_DEBUG_INVARIANTS
#  include <assert.h>
#  define CQ_BIT_ASSERT(cond) assert(cond)
#else
#  define CQ_BIT_ASSERT(cond) ((void)0)
#endif

/* --- Constructors. Every one of them produces a valid bit (I1). ---------- */

static inline cq_bit cq_bit_zero(void)  { return (cq_bit){ CQ_BIT_ZERO, 0u }; }
static inline cq_bit cq_bit_one(void)   { return (cq_bit){ CQ_BIT_ONE,  0u }; }

/* A C truth value, not a bit pattern: any non-zero is ONE. Reached from
 * CQ_lang literals, where 7 and -1 are ordinary inputs. */
static inline cq_bit cq_bit_const(int value)
{
    return (cq_bit){ (uint8_t)(value ? CQ_BIT_ONE : CQ_BIT_ZERO), 0u };
}

/* Wraps an already-allocated qubit index. It does NOT allocate: the pool is
 * M03's and materialisation is M05's. */
static inline cq_bit cq_bit_qubit(uint32_t q)
{
    return (cq_bit){ CQ_BIT_Q, q };
}

/* --- The I1 partition. ---------------------------------------------------- */

static inline int cq_bit_is_zero (cq_bit b) { return b.kind == CQ_BIT_ZERO; }
static inline int cq_bit_is_one  (cq_bit b) { return b.kind == CQ_BIT_ONE;  }
static inline int cq_bit_is_qubit(cq_bit b) { return b.kind == CQ_BIT_Q;    }

static inline int cq_bit_is_const(cq_bit b)
{
    return b.kind == CQ_BIT_ZERO || b.kind == CQ_BIT_ONE;
}

/* I1, mechanically. Two clauses, and the second is the one that earns its
 * keep: a constant carries no qubit index. Were a stale index allowed to ride
 * along on a constant, a later fold could hand a rail owned by someone else to
 * the sink — while I4 says an all-constant register owns zero qubits.
 * Canonical zero makes that unrepresentable rather than merely unlikely. */
static inline int cq_bit_valid(cq_bit b)
{
    if (b.kind == CQ_BIT_Q) return 1;
    return cq_bit_is_const(b) && b.q == 0u;
}

/* --- Accessors. Preconditions are asserted, not assumed. ----------------- */

static inline int cq_bit_value(cq_bit b)
{
    CQ_BIT_ASSERT(cq_bit_is_const(b));
    return (int)b.kind;   /* the numbering above: a constant's kind is its value */
}

static inline uint32_t cq_bit_qindex(cq_bit b)
{
    CQ_BIT_ASSERT(cq_bit_is_qubit(b));
    return b.q;
}

/* --- The two operations M01 owns. ---------------------------------------- */

/* X on a constant (PRD §3, row 1) and the θ ≡ π classical row (Rule 15) are
 * the same primitive: flip the constant, 0 gates, 0 qubits. Written against
 * the kinds rather than `^ 1` so the numbering has exactly one consumer,
 * cq_bit_value. Applying it twice is the identity, which is what makes a
 * sandwich's reverse half cancel on a constant target. */
static inline void cq_bit_flip_const(cq_bit *b)
{
    CQ_BIT_ASSERT(cq_bit_is_const(*b));
    b->kind = (uint8_t)(b->kind == CQ_BIT_ZERO ? CQ_BIT_ONE : CQ_BIT_ZERO);
}

/* Operand distinctness, per PRD §3: a coincident operand is a meaningless
 * channel and a real miscompile signature. Distinctness is a property of
 * *bits*, not of kinds — two different bits may both be Q on different
 * indices, which is an ordinary legal pair. So this can only ever fire on
 * CQ_BIT_Q operands, and never on two constants, which are genuinely
 * independent channels. Takes pointers because pointer identity is half the
 * question; every other query here is by value. */
static inline int cq_bit_coincident(const cq_bit *a, const cq_bit *b)
{
    if (a == b) return 1;
    return a->kind == CQ_BIT_Q && b->kind == CQ_BIT_Q && a->q == b->q;
}

#endif /* CQOPS_BIT_H */
