/* cq_shim.h — the contract between M27's generated wrappers and M26.
 *
 * WHY THIS EXISTS AT STEP 22 RATHER THAN STEP 23. A generator cannot emit
 * calls into a vacuum: plan §3 gives gen_bodies.py the job of being "the only
 * file in M27 that names a libcqops or M26 symbol", which presupposes that
 * those symbols are named somewhere a compiler can see. So Step 22 ships the
 * DECLARATIONS — the shape of the boundary — and Step 23 (M26,
 * shim/cq_runtime_impl.c) ships the definitions. Nothing here is implemented
 * yet, and that is deliberate: Step 22's gate is "the generator is green", not
 * "the program links".
 *
 * WHAT THE BOUNDARY IS. A cq_template_* symbol is handle-level: CQ_lang's IR
 * pass names rails by int32_t handle and never sees a qubit. Everything below
 * — resolving a handle to a register, D7a's refusal and D7b's defensive copy,
 * minting the result rail, choosing the kernel, opening a §9 control region —
 * is M26's, in ONE place rather than 992. That is what makes these wrappers
 * "thin": the generated body carries only the constants the ABI fixes
 * (opcode, width in bits, predicate, cast pair) and no logic at all.
 *
 * WIDTHS ARE IN BITS AND COME FROM THE YAML'S OWN `widths` MAP, never from
 * sizeof(c_type). The two witnesses are i80, whose `_hl` literal rides a
 * 128-bit __int128 while the register is 80 bits, and i1, whose literal is an
 * 8-bit bool while the register is 1 bit.
 */
#ifndef CQ_SHIM_H
#define CQ_SHIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The classical operand arrives as a two-word little-endian pattern. The
 * conversion to unsigned __int128 is modular, so a negative signed literal
 * arrives sign-extended and M26 masks it to `bits` — the only layer that knows
 * the register width. THE WORD ORDER IS LOAD-BEARING and both words are
 * uint64_t, so C accepts a transposition silently; tests/test_gen_bodies.py
 * pins the whole argument list rather than its membership, and runs the macro.
 *
 * NO fp c_type EVER REACHES HERE, AND THE REASON IS NOT THE ONE THIS COMMENT
 * USED TO GIVE. It said "casts are arity-1" — true, and irrelevant: 328 grid
 * symbols carry an fp CLASSICAL operand (`cq_template_fadd_f32_hl(int32_t,
 * float)` and its kin) and not one of them is a cast. What keeps them away from
 * these macros is that every fp-touching symbol is an ABORT body (PRD §1), so no
 * body of theirs calls one. A v2 that implements fp inherits the real
 * constraint, not the decorative one. */
#define CQ_SHIM_LO(x) ((uint64_t)(unsigned __int128)(x))
#define CQ_SHIM_HI(x) ((uint64_t)(((unsigned __int128)(x)) >> 64))

/* The 13 integer binary opcodes that reach a wrapper. `icmp` is separate (it
 * carries a predicate); the fp opcodes never reach a wrapper at all. The set
 * is asserted against opcode_table.yaml by tests/test_gen_shim.py, so it
 * cannot drift from the ABI silently. */
typedef enum {
    CQ_SHIM_OP_ADD, CQ_SHIM_OP_SUB,  CQ_SHIM_OP_MUL,  CQ_SHIM_OP_SDIV,
    CQ_SHIM_OP_UDIV, CQ_SHIM_OP_SREM, CQ_SHIM_OP_UREM, CQ_SHIM_OP_AND,
    CQ_SHIM_OP_OR,  CQ_SHIM_OP_XOR,  CQ_SHIM_OP_SHL,  CQ_SHIM_OP_LSHR,
    CQ_SHIM_OP_ASHR
} cq_shim_op;

/* opcode_table.yaml's `predicates: icmp:` list, in its order. */
typedef enum {
    CQ_SHIM_PRED_EQ, CQ_SHIM_PRED_NE, CQ_SHIM_PRED_SLT, CQ_SHIM_PRED_SGT,
    CQ_SHIM_PRED_SLE, CQ_SHIM_PRED_SGE, CQ_SHIM_PRED_ULT, CQ_SHIM_PRED_UGT,
    CQ_SHIM_PRED_ULE, CQ_SHIM_PRED_UGE
} cq_shim_pred;

/* The three integer width casts. fp casts never reach a wrapper. */
typedef enum { CQ_SHIM_CAST_SEXT, CQ_SHIM_CAST_ZEXT, CQ_SHIM_CAST_TRUNC } cq_shim_cast_kind;

/* Forward: mint a fresh rail holding f(a, b) and return its handle. */
int32_t cq_shim_bin_qq(cq_shim_op op, int bits, int32_t a_handle, int32_t b_handle);
int32_t cq_shim_bin_hl(cq_shim_op op, int bits, int32_t a_handle, uint64_t lo, uint64_t hi);
int32_t cq_shim_bin_lh(cq_shim_op op, int bits, uint64_t lo, uint64_t hi, int32_t b_handle);

/* Uncompute: `out ^= f(sources)` in place, void. NOT a reclaim — `cqrt_free`
 * is the sole deallocator (PRD §10). */
void cq_shim_bin_qq_unc(cq_shim_op op, int bits, int32_t out_handle, int32_t a_handle, int32_t b_handle);
void cq_shim_bin_hl_unc(cq_shim_op op, int bits, int32_t out_handle, int32_t a_handle, uint64_t lo, uint64_t hi);
void cq_shim_bin_lh_unc(cq_shim_op op, int bits, int32_t out_handle, uint64_t lo, uint64_t hi, int32_t b_handle);

/* Controlled (PRD §9, Rule 9): an EMITTER MODE, not a kernel rewrite.
 *
 * WHAT STEP 22 PINS HERE IS THE GENERATED TEXT, NOT THE REGION. Each of these
 * bodies is exactly one call, so the ABI offers M26 no way to bracket more than
 * one kernel invocation per controlled symbol. That is a NECESSARY condition for
 * bd d6m's preferred fix (a) and nothing more: what M26 does between
 * cq_ctrl_push and cq_ctrl_pop is unconstrained by anything M27 emits, and d6m —
 * a control wire stored BY VALUE, so a region outliving its control rail names a
 * recycled qubit index — is OPEN and is M26's to fix at Step 23. An earlier draft
 * of this comment claimed the pin settled it. It does not. */
int32_t cq_shim_bin_qq_ctrl(cq_shim_op op, int bits, int32_t ctrl_flag, int32_t a_handle, int32_t b_handle);
int32_t cq_shim_bin_hl_ctrl(cq_shim_op op, int bits, int32_t ctrl_flag, int32_t a_handle, uint64_t lo, uint64_t hi);
int32_t cq_shim_bin_lh_ctrl(cq_shim_op op, int bits, int32_t ctrl_flag, uint64_t lo, uint64_t hi, int32_t b_handle);

/* Compares: result is a one-bit flag handle, and there is NO controlled axis
 * (opcode_table.yaml:45-46 — nesting combines flags via cqrt_toffoli) and no
 * `_lh` on any axis (a lhs-literal compare canonicalises by predicate flip). */
int32_t cq_shim_icmp_qq(cq_shim_pred pred, int bits, int32_t a_handle, int32_t b_handle);
int32_t cq_shim_icmp_hl(cq_shim_pred pred, int bits, int32_t a_handle, uint64_t lo, uint64_t hi);
void cq_shim_icmp_qq_unc(cq_shim_pred pred, int bits, int32_t out_handle, int32_t a_handle, int32_t b_handle);
void cq_shim_icmp_hl_unc(cq_shim_pred pred, int bits, int32_t out_handle, int32_t a_handle, uint64_t lo, uint64_t hi);

/* Casts: arity-1, two widths, no classical operand, no controlled axis. */
int32_t cq_shim_cast(cq_shim_cast_kind kind, int from_bits, int to_bits, int32_t a_handle);
void cq_shim_cast_unc(cq_shim_cast_kind kind, int from_bits, int to_bits, int32_t out_handle, int32_t a_handle);

/* The v1 boundary, made visible at RUNTIME rather than at link time (PRD §1).
 * `reason` distinguishes the two abort buckets — "fp is v2" (884 symbols) and
 * D14's `_inv` (603) — so a symbol swept into the wrong one says which.
 * _Noreturn is what lets a value-returning abort body carry no return. */
_Noreturn void cq_shim_unsupported(const char *symbol, const char *reason);

#ifdef __cplusplus
}
#endif

#endif /* CQ_SHIM_H */
