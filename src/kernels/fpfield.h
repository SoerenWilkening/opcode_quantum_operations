/* src/kernels/fpfield.h — M31, K22, the VIEWS half. PRD-v2 §3.1, §5, §7.
 *
 * Read docs/constructions/K22.md before changing anything here. The constants
 * below are `third_party/bennett/src/softfloat/softfloat_common.jl:8-14`
 * transcribed; nothing else in this file is a construction at all.
 *
 * WHAT A VIEW IS: PURE ADDRESSING, AND THAT IS THE WHOLE OF M31's FIRST HALF.
 * `ea = (a >> 52) & 0x7FF` (fadd.jl:21) is a constant shift and a constant
 * mask. Upstream gives each a fresh 64-wire vector and `W` gates
 * (`lower_lshr!` at arith.jl:314-320, `lower_and!` at :268-272); we hand the
 * consumer an ASSEMBLED `cq_bit[64]` whose lanes are struct COPIES of the
 * field's bits with CQ_BIT_ZERO fill. Zero gates, zero qubits, no block.
 * PRD-v2 §7.10's "casts and constant shifts are wiring" is the licence for the
 * shift; the mask is the same claim one operator further along, and K22.md §5
 * records it as this module's one delta from a literal reading of §7.2
 * together with the arithmetic that makes the two agree lane for lane.
 *
 * WHY THIS IS NOT AN I2 VIOLATION. A copied `cq_bit` with `kind == CQ_BIT_Q`
 * names the SAME qubit index as the bit it was copied from, so for a moment it
 * looks like the aliasing I2 forbids. I2 is about live REGISTERS — "no qubit
 * index appears in two live registers" — and a view is not a register: it owns
 * nothing, it is never handed to cq_reg_*, it is never freed, and it dies with
 * the caller's stack frame. What makes it sound is narrower and is checkable:
 * A VIEW IS ONLY EVER A CONTROL. It reaches the emitter through
 * `cq_emit_*`'s `const cq_bit *` parameters, so it can never be a target and
 * can never be materialised — I6(a) by construction, exactly as `cq_ult_block`'s
 * and `cq_eq_block`'s operands are (kernels/cmp.h), and exactly as K12's
 * shifted remainder `r_in[t]` is (K12.md §2.1a). Debug's `cq_bit_coincident`
 * still fires if two CONTROLS of one gate resolve to the same index, so the
 * one hazard a copy could introduce is not merely argued away but policed.
 *
 * THE ZERO FILL IS A CONSTANT LANE AND IT IS SUPPOSED TO FOLD. The §3 fold
 * table elides a CX or CCX whose control is CQ_BIT_ZERO, so a block run over a
 * view emits fewer gates than it has SLOTS. That is D-K18-6, stated for K18
 * and true here first: M31 is the module where slots and gates come apart, and
 * every composition identity in tests/ is therefore over SLOTS.
 *
 * NO NARROWING. A consumer runs its block at W = 64 over the assembled view,
 * never at W = 11 over `cq_fp_span_exp`. PRD-v2 §7.2 forbids narrowing towards
 * the field width, D9's K12 precedent refused the identical trade, and K11's
 * finding is that the one mutant L1 cannot see is the one that looks like an
 * optimisation. The raw sub-span accessors below exist for a consumer that
 * genuinely wants the contiguous field (a sign bit read as a single control,
 * say); they are not a licence to re-width a block.
 */
#ifndef CQOPS_KERNELS_FPFIELD_H
#define CQOPS_KERNELS_FPFIELD_H

#include "bit.h"

#include <stdint.h>

/* IEEE 754 binary64's field geometry. PRD-v2 §1: `f64` ONLY, so 64 is a
 * constant here and not a ladder — there is no `W` to be generic over, which
 * is why this module names no width parameter anywhere. */
enum {
    CQ_FP64_W       = 64,
    CQ_FP64_FRAC_W  = 52,   /* the STORED fraction; the implicit bit is not in it */
    CQ_FP64_EXP_W   = 11,
    CQ_FP64_FRAC_LO = 0,
    CQ_FP64_EXP_LO  = 52,
    CQ_FP64_SIGN_LO = 63
};

_Static_assert(CQ_FP64_FRAC_W + CQ_FP64_EXP_W + 1 == CQ_FP64_W,
               "binary64 is 1 + 11 + 52; a renumbering must break a build, "
               "not just a comment");

/* softfloat_common.jl:8-14, VERBATIM, in the order the file writes them.
 * `CQ_FP64_EXP_ALL` is not one of upstream's seven: it is the literal
 * `UInt64(0x7FF)` the class predicates compare `ea` against (fadd.jl:29). */
#define CQ_FP64_FRAC_MASK  UINT64_C(0x000FFFFFFFFFFFFF)
#define CQ_FP64_IMPLICIT   UINT64_C(0x0010000000000000)
#define CQ_FP64_EXP_MASK   UINT64_C(0x7FF0000000000000)
#define CQ_FP64_INF_BITS   UINT64_C(0x7FF0000000000000)
#define CQ_FP64_QNAN       UINT64_C(0x7FF8000000000000)
#define CQ_FP64_QUIET_BIT  UINT64_C(0x0008000000000000)
#define CQ_FP64_INDEF      UINT64_C(0xFFF8000000000000)
#define CQ_FP64_EXP_ALL    UINT64_C(0x7FF)

/* --- The views. `out` is CQ_FP64_W entries and is written whole. ---------- */

/* The general form: lanes [lo, lo+n) of `a` land in out[0..n), and out[n..64)
 * is CQ_BIT_ZERO. A hard error in BOTH configurations on a span that is not
 * inside [0, 64) — an off-by-one here reads a neighbouring rail's bit as a
 * control, which is a wrong CIRCUIT that computes a plausible value. */
void cq_fp_view(const cq_bit *a, int lo, int n, cq_bit *out);

void cq_fp_view_exp (const cq_bit *a, cq_bit *out);  /* (a >> 52) & 0x7FF */
void cq_fp_view_frac(const cq_bit *a, cq_bit *out);  /* a & FRAC_MASK     */
void cq_fp_view_sign(const cq_bit *a, cq_bit *out);  /* a >> 63           */

/* The raw contiguous sub-spans, for a consumer that wants the field itself
 * rather than a 64-lane view of it. Pure addressing again: no gate, no qubit,
 * and the result is `const` because a field of a source is a CONTROL. */
const cq_bit *cq_fp_span_exp (const cq_bit *a);   /* 11 lanes */
const cq_bit *cq_fp_span_frac(const cq_bit *a);   /* 52 lanes */
const cq_bit *cq_fp_span_sign(const cq_bit *a);   /*  1 lane  */

/* --- Constants as SOURCES (PRD-v2 §7.3). --------------------------------- */

/* Fills out[0..64) with CQ_BIT_ZERO / CQ_BIT_ONE from `pattern`, lane i taking
 * bit i. Upstream allocates a fresh wire per constant lane (arith.jl:202:
 * "Constants are always safe (their wires are freshly allocated by
 * resolve!)"); we do not, and §7.3's consequence is explicit — our peak is
 * BELOW upstream's ancilla figure for the same construction and the difference
 * is not a bug to chase in either direction. */
void cq_fp_const(cq_bit *out, uint64_t pattern);

static inline void cq_fp_const_frac_mask(cq_bit *o) { cq_fp_const(o, CQ_FP64_FRAC_MASK); }
static inline void cq_fp_const_implicit (cq_bit *o) { cq_fp_const(o, CQ_FP64_IMPLICIT);  }
static inline void cq_fp_const_exp_mask (cq_bit *o) { cq_fp_const(o, CQ_FP64_EXP_MASK);  }
static inline void cq_fp_const_inf_bits (cq_bit *o) { cq_fp_const(o, CQ_FP64_INF_BITS);  }
static inline void cq_fp_const_qnan     (cq_bit *o) { cq_fp_const(o, CQ_FP64_QNAN);      }
static inline void cq_fp_const_quiet_bit(cq_bit *o) { cq_fp_const(o, CQ_FP64_QUIET_BIT); }
static inline void cq_fp_const_indef    (cq_bit *o) { cq_fp_const(o, CQ_FP64_INDEF);     }
static inline void cq_fp_const_zero     (cq_bit *o) { cq_fp_const(o, UINT64_C(0));       }
static inline void cq_fp_const_exp_all  (cq_bit *o) { cq_fp_const(o, CQ_FP64_EXP_ALL);   }

/* --- The classical row's one packing step. ------------------------------- */

/* The 64 lanes of an ALL-CLASSICAL `a` as a `uint64_t`, for risk R9's
 * short-circuit. A hard error in both configurations if any lane is a qubit.
 *
 * THIS IS NOT AN I5 VIOLATION, AND THE ARGUMENT IS NOT THE ONE cmp.c MAKES.
 * I5 forbids a packed scalar in the REPRESENTATION; a rail is still 64 tri-
 * valued `cq_bit`s and nothing here changes that. M16's `const_raw` goes
 * bit-serial instead because `icmp` ships at i80 and a 64-bit word would CAP
 * the kernel (kernels/cmp.c). No such cap exists here: PRD-v2 §1 scopes v2 to
 * `f64`, the pattern IS 64 bits by definition, and PRD-v2 §7.4 decides the
 * short-circuit is "a C transcription of the Julia body over `uint64_t`" —
 * the same source evaluated on constants, so the classical and quantum modes
 * agree bit-for-bit on every host. The library still never evaluates a
 * `double`; its only contact with one is the memcpy at cqrt_alloc_f64. */
uint64_t cq_fp_pack(const cq_bit *a);

#endif /* CQOPS_KERNELS_FPFIELD_H */
