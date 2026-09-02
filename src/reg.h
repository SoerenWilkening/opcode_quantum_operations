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

/* TWO uint64_t WORDS CARRY A WHOLE REGISTER, IN AND OUT, and both directions
 * index by `i >> 6` into a two-element array: cq_reg_alloc_const /
 * cq_bits_from_words take (lo, hi), and M22's cq_measure returns them. Asserted
 * rather than commented so the two cannot drift — the src/kernels/divrem_u.c
 * idiom. `<=` and not `==`: the packing is correct at any cap at or below 128,
 * so an equality would be a false tripwire on a narrowing. Raising the cap
 * above 128 is a one-line edit here (the width is validated as a RANGE), and
 * without this line it would be a silent out-of-bounds write in cq_measure that
 * no test, no warning and no lint could see. */
_Static_assert(CQ_REG_WIDTH_MAX <= 128u,
               "two 64-bit words carry a whole register (lo, hi)");

/* Three states, NONE NUMBERED 0, so an all-zero slot is not a valid state and
 * neither is the 0xAA grow poison. This widens PRD §2.2's `uint8_t live`,
 * which cannot tell a tombstone from a measured rail from a realloc'd tail. */
enum {
    CQ_SLOT_LIVE     = 1,  /* readable, writable, freeable                   */
    CQ_SLOT_DEAD     = 2,  /* tombstone (D5): qubits returned, bits freed,
                            * the slot kept forever so numbering stays
                            * monotonic                                      */
    CQ_SLOT_MEASURED = 3,  /* terminal (PRD §7): CQ_lang emits no adjoint and
                            * no free, so the qubits are DELIBERATELY never
                            * reclaimed. Measured over the 239 goldens: 255
                            * measures, 0 later freed, 0 later referenced.
                            * Distinct from LIVE so a later free names the
                            * contract violation; distinct from DEAD because
                            * its qubits still exist and must still be swept
                            * by cq_reg_audit.                               */
    CQ_SLOT_TOKEN    = 4   /* a CLASSICAL RESOURCE TOKEN — `cqrt_tape_alloc`'s
                            * t<N>, and qram's a<N> when it lands (PRD §15 D23,
                            * plan §0.5). NOT A RAIL: width 0, bits NULL, owns
                            * nothing, is never tainted, freed or measured. It
                            * takes a slot ONLY so that it draws from this
                            * table's D5 counter — `tape` and `src` are both
                            * int32_t, and a token numbered elsewhere would
                            * collide with a rail. Every rail accessor refuses
                            * it through the two funnels in reg.c; the sweep
                            * and the D21 snapshot skip it as they skip DEAD. */
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

/* PRD §15 D23 / plan §0.5: mint a CLASSICAL TOKEN — a handle out of the D5
 * counter that is not a rail. Takes the TABLE and not the context, exactly as
 * cq_reg_alloc_const does, and for the same reason it matters to D21: it cannot
 * reach a qubit or a gate. Zero gates, zero qubits, at every call. */
int32_t cq_reg_alloc_token(cq_reg_table *t);
int     cq_reg_is_token(const cq_reg_table *t, int32_t h);

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
 * THIS SHAPE WAS A REFUSAL TO SETTLE bd ckd.17, AND THE REFUSAL TURNED OUT TO
 * BE THE ANSWER. It is M03's shape one level up — cq_qubits_release(pool, q,
 * int proven_zero) — for M03's reason: the two-bit shadow cannot be the
 * free-time oracle for a rotation-tainted rail, because §3's CX rule makes
 * poison sticky and a literal shadow check would hard-error on a legitimate
 * sandwich kernel over a tainted operand. ckd.17 asked whether the certificate
 * lives per qubit in the shadow or per register in M07; passing BOTH h and q
 * left both homes open. PRD §15 D15 answers with a THIRD home neither branch
 * named — a per-handle record over the CALL STREAM at the M26 handle boundary
 * — and KEEPS this exact signature: the certificate is what finally reads the
 * `h`, which today's only proof throws away (`(void)h;`). A whole-rail boolean
 * would pick per-register, and is the purest laundering vector available
 * besides — one wrong 1 launders every qubit of a dirty rail at once.
 *
 * M07 SHIPS NO PROOF FUNCTION and the library defines none. NULL means "no
 * evidence": every CQ_BIT_Q bit then fails, which fails loud, which is the
 * correct behaviour until M26 supplies D15's certificate at Step 23 (bd 06t).
 * The only proof in the tree lives in tests/support under a name saying it is
 * valid only while nothing has rotated. Note `void cqrt_free(int32_t)` carries
 * no evidence slot, so M07 is the END of this chain: D15's certificate is
 * supplied here.
 *
 * THAT "UNTIL" IS NOW MET (Step 23 landing 2, 2026-08-27), AND THE SENTENCE
 * ABOVE IS KEPT VERBATIM BECAUSE shim/cq_shim_proof.h CITES IT AS ITS LICENCE.
 * `cqrt_free` installs cq_shim_free_proof — D15's call-stream certificate AND
 * the shadow, dirty dominating. Nothing in src/ gained a proof: M07 still ships
 * none, which is what this paragraph is about, and the supplier is Layer 5.
 *
 * THE CONTRACT IS THREE-VALUED AND THE C TYPE DOES NOT CHANGE, because it was
 * never a boolean: the return is an `int`, and D15 §3 splits it BY SIGN.
 *
 *     > 0   proven CLEAN     release to the pool
 *    == 0   UNPROVEN         the oracle cannot tell
 *     < 0   proven DIRTY     the library can SEE the rail is not |0⟩
 *
 * `cq_shadow_known_zero` conflates the last two — it returns 0 both for
 * "unknown" and for "known 1" — and splitting them is exactly what this
 * boundary is for. A proof may return any positive or any negative value; the
 * SIGN is the contract and CQ_PROOF_* below are names for the canonical ones.
 *
 * A NULL PROOF IS A MISSING ARGUMENT AND NOT A FOURTH STATE. "The caller
 * supplied no oracle" and "the oracle cannot tell" are different facts, and
 * only the second is D15's unproven row: cq_reg_free hard-errors on the first
 * whenever the rail owns a qubit, which is strictly more conservative than
 * D15 requires (aborting is not recycling). Say it in those words at any new
 * call site or someone will "fix" it into the unproven row.
 *
 * THE ACT IS TWO-VALUED EVEN THOUGH THE VERDICT IS NOT (D15 §4's last clause,
 * confirmed 2026-08-22): proven-clean releases, and proven-dirty and unproven
 * ALIKE are STRANDED. Rule 6's hard error is unmoved where it was aimed — a
 * RELEASE of an index not proven |0⟩ — and that row can no longer arise,
 * because neither non-clean row reaches the pool. The verdict stays distinct
 * in the REPORT, which is what makes D15 §3's residue split producible at all;
 * that split is `bd 06t`'s first obligation. See cq_reg_disposition. */
typedef int (*cq_zero_proof)(const cq_ctx *ctx, int32_t h, uint32_t q);

/* NAMES FOR THE THREE CANONICAL RETURNS, and the NUMBERING IS LOAD-BEARING
 * rather than decorative — hence the asserts, on bit.h's and angle.h's
 * precedent that a renumbering must break a BUILD and not just a comment.
 *
 * CQ_PROOF_CLEAN == 1 and CQ_PROOF_UNPROVEN == 0 are what let cq_reg_clean
 * stay a plain boolean over the same seven external call sites it had before
 * Step 23. Two of those read it as a NUMBER: tests/test_unc_asym.inc does
 * CHECK_EQ(cx.clean, 1), and `if (ry.clean)` there prints a bd 2cf REGRESSION
 * message on its TRUE branch — so any numbering with a non-zero UNPROVEN makes
 * that site report a regression that did not happen, with a misleading
 * message, on a rail nobody could clear. */
enum {
    CQ_PROOF_DIRTY    = -1,
    CQ_PROOF_UNPROVEN =  0,
    CQ_PROOF_CLEAN    =  1
};
_Static_assert(CQ_PROOF_CLEAN > 0 && CQ_PROOF_UNPROVEN == 0 && CQ_PROOF_DIRTY < 0,
               "D15 §3 splits the proof by SIGN; cq_reg_clean is its positive row");

/* Whether CQOPS_FREE_ABORT is in force — see cqops_set_free_abort in the
 * public header for the whole contract. Resolved on every call, exactly as
 * cq_sink_active is, so a setter or an environment change takes effect without
 * a rebuild. An unrecognised environment value is a hard error here rather
 * than a quiet "off". */
int cq_free_abort_active(void);

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

/* THE RAIL'S VERDICT: the three-valued fold of `proof` over every
 * qubit-carrying bit, by the same sign convention (CQ_PROOF_* above).
 * DIRTY DOMINATES UNPROVEN DOMINATES CLEAN — one convicted bit convicts the
 * rail, and a rail with no conviction and any gap is unproven.
 *
 * THIS IS WHERE THE COLLAPSE USED TO LIVE, and moving it is the whole of the
 * M07 half of `bd 06t`. Until Step 23 cq_reg_clean was the only whole-rail
 * predicate and it returned 0 for dirty and unproven alike, so cq_reg_free
 * could not have branched three ways however it was written — the information
 * had already been thrown away one call down. cq_reg_clean is now a thin
 * wrapper over this (`> 0`), which breaks the collapse WITHOUT re-typing the
 * seven call sites that read it as a boolean.
 *
 * NON-ABORTING, like cq_reg_clean, and for the same recorded reason: a check
 * inlined into the free would still abort if deleted, via M03's own guard one
 * layer down, so its death test would pass on a broken library.
 *
 * An all-constant rail is CLEAN under any proof including NULL — by I4 it owns
 * no index, so nothing can reach the free list and nothing can collapse. A
 * NULL proof over a rail that DOES own a qubit answers UNPROVEN here; the
 * missing-argument hard error is cq_reg_free's, not this predicate's. */
int cq_reg_disposition(const cq_ctx *ctx, int32_t h, cq_zero_proof proof);

/* Has this context already reported a strand on stderr? D15 §3 asks that the
 * FIRST occurrence name the handle, and one line per stranded qubit would bury
 * it: the corpus strands its residue across a great many frees. Exposed so
 * that "the first, and only the first" is a tested claim rather than a
 * reviewed one. PER CONTEXT rather than per process, which is what makes it
 * observable from the second case of a test binary onward — and the shim's
 * context is process-global anyway, so this is a superset of D15's wording.
 *
 * IT COUNTS EMISSIONS AND IS NOT A FLAG. A flag answers "did it ever report";
 * the claim D15 makes is "it reported exactly ONCE", and only a count can carry
 * that. Measured: as a flag, deleting the one-shot early return SURVIVED the
 * whole suite in both configurations, because the suppressed path still set the
 * flag. So the value is 0 before any strand and 1 for the rest of the
 * context's life however many qubits strand. */
uint32_t cq_reg_strand_reports(const cq_ctx *ctx);

/* PRD §15 D15 §3's RESIDUE SPLIT — `bd 06t`'s FIRST obligation, and the reason
 * this is four functions rather than one number.
 *
 * "AN IMPLEMENTATION THAT REPORTS A SINGLE RESIDUE FIGURE HAS NOT YET DECIDED
 * WHICH ROW EACH FREE LANDS ON." Under D15 §4 the two non-clean rows take the
 * SAME ACT — never released, never on the free list, counted — so the pool
 * cannot tell them apart and `cq_qubits_stranded()` is one total by
 * construction. The VERDICT survives the collapse and is reported here.
 *
 * TWO GRAINS, BECAUSE THEY ANSWER DIFFERENT QUESTIONS AND DISAGREE ON A REAL
 * SHAPE. The QUBIT pair is D15 §3's residue — what actually leaked, one entry
 * per qubit that never came back. The RAIL pair is `bd 06t`'s own wording,
 * "which row each FREE lands on", one entry per call to cq_reg_free. They are
 * NOT derivable from one another: a MIXED rail carrying one unproven qubit and
 * one convicted qubit adds to BOTH qubit rows and to the rail-level DIRTY row
 * ONLY, because cq_reg_disposition's lattice makes dirty absorbing. That rail
 * is exactly the shape the fold's no-early-return exists to reach, so reporting
 * one grain would discard the fact the fold was written for.
 *
 * THE QUBIT ROWS SUM TO cq_qubits_stranded() AND THAT IS AN ASSERTION, NOT A
 * COINCIDENCE — every increment here sits beside the cq_qubits_strand call it
 * describes, on the same branch. A drift between them means a strand happened
 * somewhere that did not go through cq_reg_free, which is a fact worth a red
 * test rather than a silent discrepancy. Note the sum holds PER POOL and these
 * counters are PER CONTEXT; they coincide because a context owns one pool.
 *
 * THE RAIL ROWS DO NOT SUM TO ANYTHING. A clean free is counted nowhere: an
 * all-constant rail (I4) never reaches the proof at all, and counting it would
 * make the denominator mean "frees that owned a qubit" in one reading and
 * "frees" in another. D15 §2 is explicit that the I4 frees are NOT certificate
 * coverage; a total that silently included them would report exactly the
 * inflation that warning is about. */
uint32_t cq_reg_stranded_dirty   (const cq_ctx *ctx);
uint32_t cq_reg_stranded_unproven(const cq_ctx *ctx);
uint32_t cq_reg_frees_dirty      (const cq_ctx *ctx);
uint32_t cq_reg_frees_unproven   (const cq_ctx *ctx);

/* THE SOLE DEALLOCATOR (PRD §10). Two passes, and the order is load-bearing:
 *   1. take the WHOLE RAIL's verdict through cq_reg_disposition before
 *      releasing ANY of it;
 *   2. then, PER QUBIT (D15 §3 says per qubit, not per rail): a proof answering
 *      CLEAN releases the index; anything else STRANDS it. Forward `proof`'s
 *      own per-qubit answer to cq_qubits_release — NEVER a literal 1, which
 *      would be a laundering site invisible to a grep for cq_reg_free;
 *   3. only THEN free the bits array and tombstone the slot, because writing a
 *      constant over a Q bit first erases its index irrecoverably (a constant
 *      carries a canonical q == 0) and leaks the qubit in silence.
 *
 * WHY PASS 1 SURVIVES D15 §4, WITH A DIFFERENT REASON THAN IT WAS BUILT FOR.
 * It existed so a rail was never left half-returned by a mid-loop abort. Under
 * stranding nothing aborts mid-loop, and a partly-released rail is now the
 * CORRECT outcome rather than a hazard — see step 2. What pass 1 is for now is
 * the two things a per-qubit loop cannot do: it produces the rail-level verdict
 * D15 §3's residue split is written in terms of, and it is where
 * CQOPS_FREE_ABORT stops — which reintroduces the mid-loop abort, so the
 * pre-pass is exactly what keeps that flag atomic. Deleting it makes
 * CQOPS_FREE_ABORT half-return a rail, silently and only under the flag.
 *
 * HARD ERRORS IN BOTH CONFIGURATIONS: a NULL proof for a rail that owns a
 * qubit (a MISSING ARGUMENT, not an epistemic state — see cq_zero_proof), a
 * double free, a free of a tombstone, a free of a MEASURED rail, and — only
 * when CQOPS_FREE_ABORT is in force — a rail that is not proven clean. Do not
 * weaken any of them to a warning.
 *
 * WHAT IS NOT A HARD ERROR, AND THIS IS THE STEP-23 CHANGE: a free the library
 * cannot clear. Both non-clean rows STRAND. Rule 6's hard error is unmoved
 * where it was aimed — cq_qubits_release's own refusal of an index not proven
 * |0⟩ — and under stranding the free path never reaches it, so that guard
 * becomes a backstop rather than the normal disposition. What stays absolutely
 * forbidden is RECYCLING: handing a non-|0⟩ index to the next cq_materialise
 * corrupts an unrelated rail, and that is the one unforgivable bug. */
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

/* RAIL-TO-RAIL EXCHANGE: `cqrt_cswap`'s CONSTANT-control row, which PRD §2.1
 * prices at ZERO GATES and zero qubits. Emits nothing, allocates nothing,
 * touches neither pool nor shadow — a constant control is a DECISION, not a
 * circuit, which is the §3 fold table's own posture one level up. A quantum
 * control is a different function entirely: a Fredkin per bit, through the
 * emitter.
 *
 * THREE WRONG ROUTES GIVE THE RIGHT VALUE AND THE WRONG COST, all three
 * verified against the emitter, and that is why this exists rather than a
 * cq_emit_* call with a folded control. (i) The Fredkin with ctrl == CQ_BIT_ZERO
 * keeps gates 1 and 3 — two gates where the spec says none, since only the
 * middle CCX has the control on it. (ii) With ctrl == CQ_BIT_ONE the middle
 * Toffoli folds to a CX and you get the three-CNOT SWAP plus three freshly
 * materialised wires on a rail that was entirely classical — three gates and
 * three qubits where the spec says none, and I4 violated in silence. (iii)
 * Pushing a §9 region around an uncontrolled SWAP promotes it to THREE
 * TOFFOLIS rather than 2·CX + 1·CCX. The cost IS the specification here.
 *
 * Widths must match and a == b aborts, in both configurations. It exchanges
 * bit CONTENTS rather than the two `bits` pointers, so reg.h's promise that a
 * register's bits array is stable for its life survives — a kernel holding a
 * cq_bit * across a swap would otherwise be reading the other rail. */
void cq_reg_swap_bits(cq_reg_table *t, int32_t a, int32_t b);

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
 * WHY NO AUTOMATIC CALL SITE. Cost is O(sum of live widths), and the L6 corpus
 * performs tens of thousands of frees against a peak in the low thousands of
 * simultaneously live rails, so auditing per free would be on the order of 10^9
 * bit visits at Step 24 and the likely "fix" would be deleting the check. (No
 * exact figure here on purpose: the CQ_lang corpus is UNPINNED and moved four
 * times on 2026-08-22 alone -- PRD §15 D15 §0. A count in a shipped header is a
 * count nothing will ever re-check.) Callers place it: the suites here at Step 7,
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
