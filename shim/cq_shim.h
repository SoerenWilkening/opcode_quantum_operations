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
#include <string.h>

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
 * NO fp c_type EVER REACHES *THESE TWO*, AND THE REASON IS NOT THE ONE THIS
 * COMMENT USED TO GIVE. It said "casts are arity-1" — true, and irrelevant: 328
 * grid symbols carry an fp CLASSICAL operand (`cq_template_fadd_f32_hl(int32_t,
 * float)` and its kin) and not one of them is a cast. What kept them away was
 * that every fp-touching symbol was an ABORT body (PRD §1), so no body of theirs
 * called one. **THAT PROTECTION EXPIRED ON 2026-09-18** when `fcmp` at `f64`
 * landed (PRD-v2 §1, bead 9ve.28) and 28 `_hl` bodies acquired a live
 * `double b_classical`. The real constraint is below. */
#define CQ_SHIM_LO(x) ((uint64_t)(unsigned __int128)(x))
#define CQ_SHIM_HI(x) ((uint64_t)(((unsigned __int128)(x)) >> 64))

/* THE fp LITERAL IS A BIT PATTERN AND `CQ_SHIM_LO` WOULD CONVERT IT. That is
 * the whole reason this pair exists, and it is the kind of defect this project
 * has no structural detector for: `CQ_SHIM_LO(3.5)` is a NUMERIC conversion to
 * `unsigned __int128`, so it yields 3. It compiles clean, it is silent under
 * -Wconversion because the cast is explicit, the symbol links, the gate count
 * is unchanged, and the rail holds the integer 3 instead of
 * 0x400C000000000000. Only an L1-shaped case comparing the rail's BITS can see
 * it (tests/test_template_fcmp.inc).
 *
 * A `memcpy`, NEVER A UNION AND NEVER A POINTER CAST. The union is C-legal but
 * is the spelling that stops being legal the moment someone "tidies" it into
 * `*(uint64_t *)&x`, which is a strict-aliasing violation this build's UBSan
 * does diagnose. Every compiler this project uses folds the memcpy away.
 *
 * AND THIS IS PRD-v2 §7.4's BOUNDARY, NOT AN EXCEPTION TO IT. §7.4 forbids
 * `double` ARITHMETIC in the library and names `cqrt_alloc_f64`/`cqrt_measure_f64`
 * as the memcpy sites; this is the same memcpy at the same boundary, forced by
 * the frozen ABI declaring `cq_template_fcmp_oeq_f64_hl(int32_t, double)`. The
 * value is never added, compared or rounded — it is reinterpreted once and is a
 * `uint64_t` from here down.
 *
 * `_HI` DISCARDS ITS ARGUMENT WITHOUT EVALUATING IT (`sizeof` is unevaluated),
 * which keeps the two-word call shape uniform with the integer pair at zero
 * cost and keeps the parameter "used" so -Wunused-parameter stays quiet. f64 is
 * the only fp width with a macro because f64 is the only fp width v2 ships
 * (PRD-v2 §1a); gen_bodies.py hard-errors rather than guessing for any other. */
static inline uint64_t cq_shim_f64_bits(double x)
{
    uint64_t u;
    memcpy(&u, &x, sizeof u);
    return u;
}

#define CQ_SHIM_F64_LO(x) cq_shim_f64_bits(x)
#define CQ_SHIM_F64_HI(x) ((void)sizeof(x), UINT64_C(0))

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

/* `opcode_table.yaml`'s `predicates: fcmp:` list, IN ITS ORDER — which is NOT
 * LLVM's numbering and NOT `cq_fcmp_pred`'s (src/kernels/fcmp.h is
 * OEQ,OGT,OGE,OLT,… while the yaml is OEQ,UNE,OLT,OGT,…). Two enums in two
 * orders over one predicate set is exactly the icmp situation
 * tests/test_template.c records, and it is why the dispatch table in
 * shim/cq_template_dispatch.c is written out row by row rather than indexed.
 *
 * A SEPARATE PREFIX FROM `CQ_SHIM_PRED_`, NOT A SHARED ONE. Four mnemonics —
 * `ult`, `ugt`, `ule`, `uge` — appear in BOTH predicate lists meaning different
 * things (integer unsigned-less-than vs fp unordered-or-less-than), so one
 * enumerator name for both would be a silent cross-family dispatch. */
typedef enum {
    CQ_SHIM_FPRED_OEQ, CQ_SHIM_FPRED_UNE, CQ_SHIM_FPRED_OLT, CQ_SHIM_FPRED_OGT,
    CQ_SHIM_FPRED_OLE, CQ_SHIM_FPRED_OGE, CQ_SHIM_FPRED_ONE, CQ_SHIM_FPRED_ORD,
    CQ_SHIM_FPRED_UNO, CQ_SHIM_FPRED_UEQ, CQ_SHIM_FPRED_UGT, CQ_SHIM_FPRED_UGE,
    CQ_SHIM_FPRED_ULT, CQ_SHIM_FPRED_ULE
} cq_shim_fpred;

/* `opcode_table.yaml`'s `fp_arith` BINARY opcodes that reach a wrapper, in the
 * yaml's own order (`:200-204`).
 *
 * TWO OF THAT FAMILY'S SIX OPCODES ARE ABSENT AND THE ABSENCE IS THE POINT.
 * `frem` is a `fp_arith` binary row with no kernel, and `fneg` is the yaml's
 * ONE unary opcode and also in `fp_arith`; both keep their `"fp is v2"` abort.
 * That is why `gen_shim.LANDED`'s key is `(opcode, width)` and not
 * `(family, width)` — a family-grained key would take all six together, which
 * for `frem` means a live wrapper dispatching an opcode no table has a row for.
 *
 * A SEPARATE PREFIX FROM `CQ_SHIM_OP_`, on `cq_shim_fpred`'s ground: an fp
 * `fadd` and an integer `add` are different operations over different kernels,
 * and one enumerator space for both is a silent cross-domain dispatch. */
typedef enum {
    CQ_SHIM_FOP_FADD, CQ_SHIM_FOP_FSUB, CQ_SHIM_FOP_FMUL, CQ_SHIM_FOP_FDIV
} cq_shim_fop;

/* The three integer width casts. */
typedef enum { CQ_SHIM_CAST_SEXT, CQ_SHIM_CAST_ZEXT, CQ_SHIM_CAST_TRUNC } cq_shim_cast_kind;

/* The four CROSS-DOMAIN casts (`opcode_table.yaml`'s `int_to_fp` and
 * `fp_to_int` kinds). A SEPARATE ENUM FROM `cq_shim_cast_kind`, not four more
 * enumerators in it: the two reach different dispatch tables over different
 * kernel types — M13's `(F, T)` shape in both cases, but M37's kernels and
 * M13's are not interchangeable — and the width PAIRS each admits are
 * disjoint. The order groups by direction rather than following the yaml's
 * `cast_opcodes` list; `tests/test_gen_shim.py` pins the SET against the yaml,
 * not the order.
 *
 * `fpext` AND `fptrunc` ARE NOT HERE. They are `fp_width` casts and would need
 * an f32 rail, which v2 does not have (PRD-v2 §7.9), so they keep their abort
 * and there is no enumerator for them to be dispatched through. */
typedef enum {
    CQ_SHIM_FCAST_FPTOSI, CQ_SHIM_FCAST_FPTOUI,
    CQ_SHIM_FCAST_SITOFP, CQ_SHIM_FCAST_UITOFP
} cq_shim_fcast_kind;

/* The UNARY fp operations. `fsqrt` is `llvm.sqrt` in CQ_lang's
 * intrinsic_table.yaml, which M27 does not generate from (PRD §1: scope is
 * `opcode_table.yaml` ONLY), so NO generated wrapper reaches this enum today —
 * the door is built, tested by name from tests/test_template_fp.c, and
 * unreached. `fneg`, the yaml's one unary opcode, is NOT here: its kernel is
 * bead 9ve.27's and does not exist, so it keeps its abort and
 * `gen_bodies.ENTRY` has no `("funary", ...)` row at all. */
typedef enum { CQ_SHIM_FUN_FSQRT } cq_shim_fun_op;

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

/* fp compares (PRD-v2 §1, bead 9ve.28): the SAME four shapes as `icmp` over the
 * SAME handle boundary — result a one-bit flag handle, no controlled axis, no
 * `_lh` on any axis — and a DIFFERENT predicate enum and a DIFFERENT kernel
 * table. `bits` is 64 and nothing else: PRD-v2 §1 scopes v2 to `f64`, every
 * `soft_fcmp_*` upstream is `(UInt64, UInt64)`, and `cq_kernel_fcmp_*` hard-errors
 * on any other width in BOTH configurations rather than inventing a fiction.
 *
 * THE CLASSICAL OPERAND ARRIVES AS A BIT PATTERN, decomposed by CQ_SHIM_F64_LO/HI
 * and NOT by CQ_SHIM_LO/HI — see those macros for what the wrong pair does. */
int32_t cq_shim_fcmp_qq(cq_shim_fpred pred, int bits, int32_t a_handle, int32_t b_handle);
int32_t cq_shim_fcmp_hl(cq_shim_fpred pred, int bits, int32_t a_handle, uint64_t lo, uint64_t hi);
void cq_shim_fcmp_qq_unc(cq_shim_fpred pred, int bits, int32_t out_handle, int32_t a_handle, int32_t b_handle);
void cq_shim_fcmp_hl_unc(cq_shim_fpred pred, int bits, int32_t out_handle, int32_t a_handle, uint64_t lo, uint64_t hi);

/* fp ARITHMETIC (PRD-v2 §5 / §7.15, bead 9ve.36): the SAME nine shapes as the
 * integer binary family over the SAME handle boundary, at `bits == 64` and
 * nothing else. Unlike the compares, `fp_arith` DOES carry the `_lh` shape and
 * DOES carry the §9 controlled axis — `opcode_table.yaml:201,203` give `fsub`
 * and `fdiv` fifteen variants each — which is exactly the difference
 * `shim/cq_template_fp.c`'s recorded COMPARE<->ARITHMETIC seam turns on.
 *
 * `fadd` AND `fmul` HAVE NO `_lh` SYMBOL (they are commutative and the yaml
 * gives them ten variants), so `cq_shim_fbin_lh` is reached only by `fsub` and
 * `fdiv`. That is the ABI's shape, not a restriction here: the entry point
 * takes any `cq_shim_fop`, and no wrapper for the other two exists to call it.
 *
 * THE CLASSICAL OPERAND ARRIVES AS A BIT PATTERN, decomposed by
 * CQ_SHIM_F64_LO/HI and NOT by CQ_SHIM_LO/HI — see those macros. */
int32_t cq_shim_fbin_qq(cq_shim_fop op, int bits, int32_t a_handle, int32_t b_handle);
int32_t cq_shim_fbin_hl(cq_shim_fop op, int bits, int32_t a_handle, uint64_t lo, uint64_t hi);
int32_t cq_shim_fbin_lh(cq_shim_fop op, int bits, uint64_t lo, uint64_t hi, int32_t b_handle);

void cq_shim_fbin_qq_unc(cq_shim_fop op, int bits, int32_t out_handle, int32_t a_handle, int32_t b_handle);
void cq_shim_fbin_hl_unc(cq_shim_fop op, int bits, int32_t out_handle, int32_t a_handle, uint64_t lo, uint64_t hi);
void cq_shim_fbin_lh_unc(cq_shim_fop op, int bits, int32_t out_handle, uint64_t lo, uint64_t hi, int32_t b_handle);

int32_t cq_shim_fbin_qq_ctrl(cq_shim_fop op, int bits, int32_t ctrl_flag, int32_t a_handle, int32_t b_handle);
int32_t cq_shim_fbin_hl_ctrl(cq_shim_fop op, int bits, int32_t ctrl_flag, int32_t a_handle, uint64_t lo, uint64_t hi);
int32_t cq_shim_fbin_lh_ctrl(cq_shim_fop op, int bits, int32_t ctrl_flag, uint64_t lo, uint64_t hi, int32_t b_handle);

/* Casts: arity-1, two widths, no classical operand, no controlled axis. */
int32_t cq_shim_cast(cq_shim_cast_kind kind, int from_bits, int to_bits, int32_t a_handle);
void cq_shim_cast_unc(cq_shim_cast_kind kind, int from_bits, int to_bits, int32_t out_handle, int32_t a_handle);

/* CROSS-DOMAIN casts: the same arity-1 shape, and SEVENTEEN width pairs rather
 * than a rectangle. The NARROW ones are a COMPOSITION at the shim on upstream's
 * own shape — `instructions.jl:7649-7658` emits the narrowing as a SECOND IR
 * instruction — so `fptosi f64 -> i8` is M37's kernel at `T = 64` into a
 * workspace rail followed by `cq_kernel_trunc`, and `sitofp i8 -> f64` is
 * `cq_kernel_sext` into a workspace followed by the kernel at `F = 64`.
 * `uitofp`'s narrow rows take `F` directly, because there the widening is
 * WIRING inside M37 and costs nothing (PRD-v2 §7.3).
 *
 * `uitofp` FROM i64 IS REFUSED HERE, NOT ROUTED. Bead 9ve.34 / PRD-v2 §7.9:
 * upstream passes a 64-bit UIToFP source straight to `soft_sitofp`, which reads
 * bit 63 as a sign, so every `u >= 2^63` would convert negative. The generated
 * body for that row is an abort, so no wrapper can reach this door with it —
 * and the door refuses it anyway, because `cq_kernel_uitofp`'s own refusal
 * fires one layer down, AFTER a rail has been minted and a D21 bracket
 * opened. */
int32_t cq_shim_fcast(cq_shim_fcast_kind kind, int from_bits, int to_bits, int32_t a_handle);
void cq_shim_fcast_unc(cq_shim_fcast_kind kind, int from_bits, int to_bits, int32_t out_handle, int32_t a_handle);

/* The UNARY fp door. One source, one width, a 64-lane result — the shape
 * PRD-v2 §7.11 predicts for the whole of M38 (`fneg`/`fabs`/the rounding
 * family), so what lands here is what they reuse. No classical operand and no
 * controlled axis: the yaml gives `fneg` three variants (fwd/inv/unc) and
 * nothing else. */
/* --- the fp TERNARY family (PRD-v2 §6.1's vendoring, bead 9ve.24) ---------
 *
 * ONE ENUMERATOR, AND IT IS STILL AN ENUM RATHER THAN A BARE `bits`. The tag
 * space folds the OPCODE and the width together exactly as every other
 * family's does (cq_template_boundary.h), so a second ternary opcode arriving
 * upstream has a slot to occupy and cannot silently share `fma`'s twin
 * identity. `intrinsic_table.yaml` has exactly one `arity: ternary` row today
 * and `gen_bodies.SELECTOR["fternary"]` has exactly one entry, so a second one
 * is a generator KeyError before it is anything else.
 *
 * THE FOUR SHAPES ARE THE ABI's, not a convenience: `fma(a, b, c)` may take a
 * literal in the `b` lane, the `c` lane, or both, and upstream canonicalises
 * the PRODUCT pair handle-before-literal so there is no `lqq` or `lql`. Each
 * literal lane carries its own two-word pair; ONE shared pair would make
 * `fma(a, 2.0, 3.0)` compute `fma(a, 3.0, 3.0)`. */
typedef enum { CQ_SHIM_FMA_FMA } cq_shim_fma_op;

int32_t cq_shim_fma_qqq(cq_shim_fma_op op, int bits, int32_t a_handle,
                        int32_t b_handle, int32_t c_handle);
int32_t cq_shim_fma_qql(cq_shim_fma_op op, int bits, int32_t a_handle,
                        int32_t b_handle, uint64_t c_lo, uint64_t c_hi);
int32_t cq_shim_fma_qlq(cq_shim_fma_op op, int bits, int32_t a_handle,
                        uint64_t b_lo, uint64_t b_hi, int32_t c_handle);
int32_t cq_shim_fma_qll(cq_shim_fma_op op, int bits, int32_t a_handle,
                        uint64_t b_lo, uint64_t b_hi,
                        uint64_t c_lo, uint64_t c_hi);
void cq_shim_fma_qqq_unc(cq_shim_fma_op op, int bits, int32_t out_handle,
                         int32_t a_handle, int32_t b_handle, int32_t c_handle);
void cq_shim_fma_qql_unc(cq_shim_fma_op op, int bits, int32_t out_handle,
                         int32_t a_handle, int32_t b_handle,
                         uint64_t c_lo, uint64_t c_hi);
void cq_shim_fma_qlq_unc(cq_shim_fma_op op, int bits, int32_t out_handle,
                         int32_t a_handle, uint64_t b_lo, uint64_t b_hi,
                         int32_t c_handle);
void cq_shim_fma_qll_unc(cq_shim_fma_op op, int bits, int32_t out_handle,
                         int32_t a_handle, uint64_t b_lo, uint64_t b_hi,
                         uint64_t c_lo, uint64_t c_hi);

int32_t cq_shim_fun(cq_shim_fun_op op, int bits, int32_t a_handle);
void cq_shim_fun_unc(cq_shim_fun_op op, int bits, int32_t out_handle, int32_t a_handle);

/* The v1 boundary, made visible at RUNTIME rather than at link time (PRD §1).
 * `reason` distinguishes the two abort buckets — "fp is v2" and D14's `_inv` —
 * so a symbol swept into the wrong one says which. Read the populations from
 * `shim/gen_shim.py`'s EXPECTED, never from a comment: they were 884 / 603
 * until `fp_compare`/`f64` landed and they move again with every LANDED line.
 * _Noreturn is what lets a value-returning abort body carry no return. */
_Noreturn void cq_shim_unsupported(const char *symbol, const char *reason);

#ifdef __cplusplus
}
#endif

#endif /* CQ_SHIM_H */
