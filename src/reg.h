/* src/reg.h — M07: registers and the handle table. PRD §2.2, §10; D5, D7; I2, I4.
 *
 * A register is a width-sized array of tri-valued bits behind an int32_t
 * handle. That is the whole idea; everything else in this file exists to make
 * three invariants hold: handles are monotonic and never reused (D5), an
 * all-constant register owns zero qubits (I4), and no qubit index appears in
 * two live registers (I2) — which is what makes `cqrt_free` sound.
 *
 * LAYERING. ctx.h INCLUDES THIS FILE and embeds a cq_reg_table by value, since
 * ctx.h already names "a handle table (M07)" as one of the four members of the
 * backend's entire mutable state (Rule 13). So reg.h itself must stay above
 * nothing: it includes bit.h and forward-declares cq_ctx, and the functions
 * that need the pool, the shadow or the emitter take a cq_ctx * whose
 * definition arrives in reg.c. The graph is acyclic —
 *   bit.h <- reg.h <- ctx.h <- emit.h,  with reg.c including ctx.h and emit.h.
 * The typedef below is the ONLY one for cq_ctx; ctx.h spells the definition
 * `struct cq_ctx { ... };` so there is no redefinition to reason about.
 */
#ifndef CQOPS_REG_H
#define CQOPS_REG_H

#include <stdint.h>

#include "bit.h"

typedef struct cq_ctx cq_ctx;

/* HANDLE 0 IS VALID AND LIVE, so the "no register" sentinel has to be
 * negative. CQ_lang's counter is `static int32_t next_handle = 0;` with
 * `return next_handle++;` (CQ_lang runtime/cq_runtime.c:64,67) and every
 * golden trace opens `-> h0`; reserving 0 would buy a nicer sentinel and break
 * the h<N> convention every L6 diff depends on. Handles stay raw int32_t —
 * qubits.h spells indices as raw uint32_t, and a second name for a frozen ABI
 * type (`void cqrt_free(int32_t)`, cq_runtime.h:608) can only drift from it. */
#define CQ_REG_NONE       ((int32_t)-1)

/* i128 is the widest thing the ABI can name. Validated as a RANGE and never as
 * a whitelist: PRD §2.2's old "1, 8, 16, 32, 64 or 128" was stale against the
 * resolved i80 decision, and a whitelist would reject every i80 rail. */
#define CQ_REG_WIDTH_MAX  128u

/* Three states, NONE NUMBERED 0, so an all-zero slot is not a valid state and
 * neither is the 0xAA grow poison. This widens PRD §2.2's `uint8_t live`,
 * which cannot tell a tombstone from a measured rail from a realloc'd tail. */
enum {
    CQ_SLOT_LIVE     = 1,  /* readable, writable, freeable                   */
    CQ_SLOT_DEAD     = 2,  /* tombstone (D5): qubits returned, bits freed,
                            * the slot kept forever so numbering stays
                            * monotonic                                      */
    CQ_SLOT_MEASURED = 3   /* terminal (PRD §7): CQ_lang emits no adjoint and
                            * no free, so the qubits are DELIBERATELY never
                            * reclaimed. Measured over the 239 goldens: 255
                            * measures, 0 later freed, 0 later referenced.
                            * Distinct from LIVE so a later free names the
                            * contract violation; distinct from DEAD because
                            * its qubits still exist and must still be swept
                            * by cq_reg_audit.                               */
};

/* PRD §2.2 with `live` widened to `state`. `bits` is a right-sized heap array,
 * never a fixed cq_bit[128] — an i1 flag costs one entry — and being its own
 * allocation is what keeps a cq_bit * stable across a table realloc. */
typedef struct {
    uint32_t width;   /* 1..CQ_REG_WIDTH_MAX; retained on a tombstone        */
    cq_bit  *bits;    /* `width` entries, LSB at index 0; NULL once DEAD      */
    uint8_t  state;   /* read only through the validating accessor in reg.c   */
} cq_reg;

/* Dense, indexed by handle, growing only at the tail — which is what keeps
 * tombstones intact across a realloc. `n` IS the D5 monotonic counter.
 *
 * NO OWNER MAP FIELD. The I2 map is a derived view, rebuilt from the live
 * registers on each cq_reg_audit call and freed on return, so it cannot go
 * stale. See cq_reg_audit for why it cannot be maintained incrementally. */
typedef struct {
    cq_reg  *slot;
    int32_t  n;
    int32_t  cap;
} cq_reg_table;

/* Dispose leaves the table usable as if init'd, matching M03. DISPOSE IS NOT A
 * FREE: it releases memory and returns NOTHING to the pool. A rail that is
 * `_unc`'d and never freed stays allocated for good — the intended Rule-6 safe
 * leak (PRD §10), not a leak to tidy up here. */
void    cq_reg_table_init(cq_reg_table *t);
void    cq_reg_table_dispose(cq_reg_table *t);
int32_t cq_reg_count(const cq_reg_table *t);

/* THE MINT PATH TAKES THE TABLE, NOT THE CONTEXT. With no pool in reach it
 * CANNOT allocate a qubit, which makes I4 a fact of the type system rather
 * than a promise someone maintains — the same move by which emit.h's `const
 * cq_bit *` controls enforce I6. Hard errors in BOTH configurations: width 0,
 * width above CQ_REG_WIDTH_MAX, or a counter about to pass INT32_MAX (CQ_lang's
 * own next_handle++ is UB there; a library whose defence is failing loud does
 * not inherit that). */
int32_t cq_reg_alloc_zero(cq_reg_table *t, uint32_t width);

/* `cqrt_alloc_<W>(value)`. TWO WORDS, NOT ONE, and that is not an I5
 * violation: I5 forbids a packed scalar in the REPRESENTATION, and these
 * decompose into cq_bit[width] before the function returns. A uint64_t
 * parameter would silently truncate every i80 and i128 literal above bit 63 —
 * the exact trap I5 exists to prevent — because the ABI really does hand us a
 * 128-bit literal: `int32_t cq_template_and_i80_hl(int32_t, __int128)`
 * (CQ_lang runtime/cq_templates.h:127). `__int128` itself stays out of this
 * header because it is a compiler extension and we are C11; the generated shim
 * splits it. */
int32_t cq_reg_alloc_const(cq_reg_table *t, uint32_t width,
                           uint64_t lo, uint64_t hi);

/* The decomposer, exposed so the _hl literal shapes can fill a stack
 * cq_bit[W] with no handle at all — a literal is an array of constant bits —
 * and so the LSB-first logic exists once rather than once per generated shim
 * entry. Every bit it writes is canonical: a constant carries q == 0. */
void cq_bits_from_words(cq_bit *bits, uint32_t width, uint64_t lo, uint64_t hi);

/* NO cq_reg * EVER ESCAPES. The slot array is realloc'd on growth and the shim
 * mints `dst` before resolving `a` and `b`, so a cq_reg * held across a mint
 * would dangle in the hot path and a kernel would read a stale width and a
 * stale bits pointer — a wrong circuit with a plausible trace. The bits array
 * is its own allocation and is stable for the register's life, which is also
 * exactly the shape Rule 7's kernel contract wants.
 *
 * These four abort in BOTH configurations on an out-of-range handle or a
 * tombstone: qubits.c sets the house rule that a miscompile signature is not a
 * style question. A MEASURED rail is readable through cq_reg_cbits and is
 * refused by cq_reg_bits, the mutable accessor. */
cq_bit       *cq_reg_bits (cq_reg_table *t,       int32_t h);
const cq_bit *cq_reg_cbits(const cq_reg_table *t, int32_t h);
uint32_t      cq_reg_width(const cq_reg_table *t, int32_t h);
int           cq_reg_state(const cq_reg_table *t, int32_t h);

/* A PREDICATE, not an accessor: an out-of-range or never-minted handle answers
 * 0 rather than aborting, mirroring cq_qubits_is_free. */
int cq_reg_is_live(const cq_reg_table *t, int32_t h);

/* Counts CQ_BIT_Q entries. I4's tripwire (0 for an all-constant rail), the
 * per-register half of L2, and exactly how many qubits cq_reg_free returns. A
 * loop over width, never a cached count and never a mask — that is I5. A
 * cached count would need M05 to notify M07 on every materialisation and would
 * drift the moment a kernel ran, so L2 would assert against a stale number and
 * stay green while the pool was wrong. */
uint32_t cq_reg_owned_qubits(const cq_reg_table *t, int32_t h);

/* Caller-supplied evidence that qubit index `q`, held by register `h`, is
 * provably |0⟩. Must be pure: cq_reg_free consults it twice per qubit.
 *
 * THIS IS A REFUSAL TO SETTLE bd ckd.17, NOT AN ANSWER TO IT. It is M03's
 * shape one level up — cq_qubits_release(pool, q, int proven_zero) — for M03's
 * reason: the two-bit shadow cannot be the free-time oracle, because §3's CX
 * rule makes poison sticky and a literal shadow check would hard-error on
 * every legitimate sandwich kernel. ckd.17 asks whether the certificate lives
 * per qubit in the shadow or per register in M07; passing BOTH h and q leaves
 * both homes open, so resolving it needs no signature change here. A whole-rail
 * boolean would pick per-register, and is the purest laundering vector
 * available besides — one wrong 1 launders every qubit of a dirty rail at once.
 *
 * M07 SHIPS NO PROOF FUNCTION and the library defines none. NULL means "no
 * evidence": every CQ_BIT_Q bit then fails, which fails loud, which is the
 * correct behaviour for an unresolved P0 blocker. The only proof in the tree
 * lives in tests/support under a name saying it is valid only while no kernel
 * exists. Note `void cqrt_free(int32_t)` carries no evidence slot, so M07 is
 * the END of this chain: whatever ckd.17 decides, M26 supplies it here. */
typedef int (*cq_zero_proof)(const cq_ctx *ctx, int32_t h, uint32_t q);

/* NON-ABORTING: does every qubit-carrying bit of `h` satisfy `proof`?
 *
 * SEPARATE FROM cq_reg_free ON PURPOSE — it is the mutation-testing
 * discriminator. A check inlined into the free would still abort if deleted,
 * via M03's own `if (!proven_zero) cq_pool_die(...)` one layer down, so its
 * death test would pass on a broken library; that is the defence-in-depth trap
 * recorded in plan §0 after M05's distinctness check survived deletion. An
 * ORDINARY test asserting this returns 0 has no backstop beneath it.
 *
 * SCOPED TO QUBIT-CARRYING BITS, deliberately. Rule 6's older wording — "every
 * bit is BIT_ZERO or a known-zero qubit" — read literally aborts on a rail
 * holding a constant ONE, i.e. on `int x = 5;` going out of scope, and would
 * make L5's zero-cost classical path unreachable. PRD §10's own "return every
 * qubit `h` still owns" is the operative wording; a constant owns no index, so
 * it can neither reach the free list nor collapse. */
int cq_reg_clean(const cq_ctx *ctx, int32_t h, cq_zero_proof proof);

/* THE SOLE DEALLOCATOR (PRD §10). Two passes, and the order is load-bearing:
 *   1. verify EVERY bit through cq_reg_clean before releasing ANY, so a rail
 *      is never left half-returned;
 *   2. release each CQ_BIT_Q index, forwarding `proof`'s per-qubit answer to
 *      cq_qubits_release — NEVER a literal 1, which would be a laundering site
 *      invisible to a grep for cq_reg_free;
 *   3. only THEN free the bits array and tombstone the slot, because writing a
 *      constant over a Q bit first erases its index irrecoverably (a constant
 *      carries a canonical q == 0) and leaks the qubit in silence.
 *
 * Hard errors in BOTH configurations: a dirty free, a double free, a free of a
 * tombstone, a free of a MEASURED rail. Do not weaken any of them to a
 * warning. */
void cq_reg_free(cq_ctx *ctx, int32_t h, cq_zero_proof proof);

/* LIVE -> MEASURED. Keeps the bits and the qubits forever, emits nothing, and
 * touches neither the pool nor the shadow: the measurement gate and the ABI
 * return value are M26's at Step 23. Table-only by signature, so it cannot
 * drift into emitting. */
void cq_reg_mark_measured(cq_reg_table *t, int32_t h);

/* dst ^= src, bitwise through cq_emit_cx — so a constant source folds to 0
 * gates and a quantum source materialises dst and emits one CX. On a fresh
 * all-BIT_ZERO dst that is Rule 5's PHYSICAL copy, which is what makes
 * cqrt_free sound (I2), and it is why emit.h exposes cq_materialise "because
 * M07 needs it for cqrt_copy". It is also the one place D7b's defensive copy
 * will go — one place, not twelve (risk R2, bd -493). Widths must match and
 * dst == src aborts. NOTE the ABI spells cqrt_copy_<W>(src, dst), the reverse
 * of this argument order (cq_runtime.h:293-297). */
void cq_reg_xor_into(cq_ctx *ctx, int32_t dst, int32_t src);

/* D7, and THE CORPUS SETTLES ITS TWO HALVES IN OPPOSITE DIRECTIONS. Measured
 * at Step 7 over all 239 goldens — 62,930 cq_template_* calls, 25,147 _unc:
 *
 *   D7a  `out` among the sources: 0 of 25,147. It breaks Rule 7's dst ^= f(a,b)
 *        outright. HARD ERROR IN BOTH CONFIGURATIONS — R2's entire value is
 *        firing during the L6 fixture run at Step 24, and Rule 17 pins L6 under
 *        Release, where a Debug-gated assert is simply absent.
 *   D7b  two sources aliasing each other: 599 occurrences, 10 of them on v1's
 *        integer surface — CQ_lang ships a fixture named for it,
 *        tests/e2e/slice_select_rail_alias_cond.expected.log:4, :12, :31, :36,
 *        :44; spec_newcand_qsq_caller:13, :16; spec_replan_qpow_caller:13, :18;
 *        slice_i128_mulhi:4. THIS MUST NOT ABORT. See cq_reg_sources_alias.
 *
 * Also validates, in both configurations, that every handle named is a live
 * rail — which catches a use-after-free operand. `out` may be CQ_REG_NONE for
 * a call that mints nothing. */
void cq_reg_check_operands(const cq_reg_table *t, int32_t out,
                           const int32_t *srcs, uint32_t n);

/* The D7b query, split out because it is a FACT THE CALLER MUST ACT ON rather
 * than an error: R2's remedy is a defensive copy, and this is that decision's
 * predicate. Nothing in v1 calls it yet — M26 does at Step 23 (bd -493). Takes
 * no context: it is pure handle comparison. 1 if any two sources are equal. */
int cq_reg_sources_alias(const int32_t *srcs, uint32_t n);

/* I2, BY SWEEP, DEBUG-ONLY, WITH NO AUTOMATIC CALL SITE. Rebuilds the owner
 * map from scratch over every LIVE and MEASURED slot and asserts:
 *   (a) I1  — cq_bit_valid on every bit;
 *   (b) I2  — no qubit index in two registers;
 *   (c) no index twice WITHIN one register: a self-aliased rail is the same
 *       bug and double-releases at free;
 *   (d) no register holds an index that is on the pool's free list — the
 *       laundering signature;
 *   (e) no register holds an index at or beyond the pool's minted count.
 *
 * WHY A SWEEP AND NOT AN INCREMENTAL MAP. cq_materialise takes (cq_ctx *,
 * cq_bit *) and records the owner nowhere, and it is reached from inside
 * cq_emit_cx and cq_emit_ccx — so a kernel running Rule 7's contract allocates
 * into a rail with zero M07 involvement. An incremental map would need M05 to
 * notify M07, pushing a Layer-2 concept into Layer 1 and touching the one
 * module Rule 11 says to leave alone, and it would STILL be blind to a plain
 * `dst[i] = src[i]`, since cq_bit is a POD. I5 is what makes the sweep exact:
 * with no packed scalar, every owned qubit is visible by walking the bits.
 *
 * WHY NO AUTOMATIC CALL SITE. Cost is O(sum of live widths) and the corpus
 * performs 51,696 frees against a peak of 1,165 simultaneously live rails, so
 * auditing per free would be ~10^9 bit visits at Step 24 and the likely "fix"
 * would be deleting the check. Callers place it: the suites here at Step 7,
 * the poolcheck harness on every L1 case from Step 10.
 *
 * IT ASSERTS ONLY THE WEAK FORM. `pool.live == sum of owned` is true today and
 * becomes FALSE at Step 8, because I6(b) pre-materialises the whole sandwich
 * scratch region and scratch is not a register — and false again at Step 20
 * with M06's shared ancilla. Asserting it now would schedule a Step-8 breakage
 * that presents as an M08 bug.
 *
 * Debug-gated per plan §2.1, which names the I2 owner map; in Release it
 * consumes its argument and returns, following emit.c rather than an #ifdef at
 * every call site. Its death cases carry CQ_DEATH_SKIP_WITHOUT_INVARIANTS and
 * report a SKIP in Release, never a pass. */
void cq_reg_audit(const cq_ctx *ctx);

#endif /* CQOPS_REG_H */
