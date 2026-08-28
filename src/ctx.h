/* src/ctx.h — the backend's entire mutable state.
 *
 * NOT IN PLAN §3's MODULE MAP. The map jumps from Layer 0 primitives to M05
 * `emit`, but PRD §3's emitter signature takes a `cq_ctx *` and M06–M09 all
 * need the same aggregate, so it has to exist somewhere by Step 6. It is kept
 * deliberately tiny — a struct and two functions, no policy — so that it stays
 * an aggregate rather than becoming a module with opinions of its own.
 *
 * Rule 13: this is ALL of it. A handle table (M07), a qubit pool with its free
 * list, one classical shadow bit-pair per qubit, and a borrowed sink. No
 * circuit object, no gate list, no statevector, and no simulator — a gate goes
 * out through the sink and is gone from our side.
 *
 * NOT PUBLIC, AND THAT IS NOW THE ANSWER RATHER THAN A DEFERRAL (Step 23,
 * 2026-08-23). This paragraph reserved the question by name — PRD §14 says
 * <cqops/cqops.h> carries "context, sink, config", the only consumer of a
 * public context is the shim at M26, and freezing an ABI four steps before its
 * first caller would be guessing. M26 has now arrived, and it does not want
 * one. The answer is forced, not chosen:
 *
 *   - THE CONSUMER IS IN THIS REPOSITORY. shim/cq_shim_ctx.c holds the one
 *     process-global context and reaches these internals by include path, the
 *     same way tests/ has since Step 2. An in-repo consumer needs a path, not
 *     a public header.
 *   - NO CALLER OUTSIDE IT CAN EVER HOLD ONE. Measured: 0 of CQ_lang's 2479
 *     `cq_template_*` declarations and 0 of its 173 `cqrt_*` declarations name
 *     a context, a pointer or a struct — every parameter in both frozen ABIs is
 *     a scalar, and `opcode_table.yaml` contains the string "ctx" zero times.
 *   - THERE IS NO OPAQUE PATH TO PUBLISH. Every cq_ctx in the tree is by value;
 *     there is no cq_ctx_create, no heap allocation of one and no
 *     sizeof(cq_ctx) anywhere. Publishing it means publishing this whole struct
 *     and the four it contains.
 *   - AND ITS LAYOUT IS CONFIGURATION-DEPENDENT. sizeof(cq_ctx) is 128 without
 *     CQOPS_DEBUG_INVARIANTS and 144 with it, because of the #if-guarded
 *     scratch extent below — so publishing it would put a Debug/Release-varying
 *     layout into the one surface include/cqops/cqops.h documents as the place
 *     that define must not reach.
 *
 * PRD §14's layout sketch is the single sentence pointing the other way; it
 * already carries a correction block for three other errors and CLAUDE.md's
 * standing rule is that the plan's module map supersedes it where they differ.
 * The sink half of §14 IS public and always was, because CQ_lang's fixtures
 * select one by environment variable.
 *
 * ONE CONTEXT PER PROCESS MAKES THE SHIM SINGLE-THREADED BY CONSTRUCTION, not
 * by omission: the frozen ABI has no context parameter, so there is nowhere to
 * put a second one, and the sink registry, the handle counter, the pool and the
 * shadow are one mutable object each with no locking anywhere. Two independent
 * circuits at once means two processes. See shim/cq_shim_ctx.h.
 */
#ifndef CQOPS_CTX_H
#define CQOPS_CTX_H

#include "bit.h"
#include "controlled.h"
#include "qubits.h"
#include "reg.h"
#include "shadow.h"
#include "sink.h"

/* The TAG is required and the typedef is NOT repeated here: reg.h declares
 * `typedef struct cq_ctx cq_ctx;` so that its own signatures can name a
 * context it must not include. This struct was anonymous until Step 7. */
struct cq_ctx {
    cq_qubit_pool   pool;
    cq_shadow_table shadow;
    cq_reg_table    regs;     /* M07, Step 7 — handles, tombstones, I2, I4 */
    const cq_sink  *sink;     /* borrowed; see below on why it is resolved once */

    /* M09, Step 8. NOT Debug-gated, unlike the extent below, and the asymmetry
     * is deliberate. "No nested sandwich" is one of the driver's own premises
     * (plan §0.1) and it is O(1) to check, so it lives in both configurations
     * — where the I6 sweeps do not. It also covers a case the extent could
     * not: a sandwich attempted from a COPYOUT step, where the extent is
     * deliberately disarmed and so cannot answer. */
    int             sandwich_depth;

    /* M06, Step 20 — PRD §9's control stack. NOT Debug-gated, and for a
     * stronger reason than sandwich_depth's: this one CHANGES BEHAVIOUR, and
     * the house rule is that CQOPS_DEBUG_INVARIANTS gates checking and never
     * behaviour, so that both configurations emit the identical gate stream
     * (include/cqops/cqops.h). Empty means no region is open and every
     * cq_emit_* takes the path it took before Step 20 existed. */
    cq_ctrl_stack   ctrl;

    /* M07, Step 23 — PRD §15 D15 §3's report. The FIRST strand names its
     * handle on stderr and the rest are silent; the residue spans a great many
     * frees and a line per stranded qubit buries the one that matters.
     *
     * PER CONTEXT rather than a file-static one-shot, and that is about being
     * testable: a process-wide latch is unobservable from the second case of a
     * test binary onward, so every case after the first would assert nothing.
     * The shim's context is process-global anyway (Step 23), so per-context is
     * a superset of D15's wording rather than a weakening of it. NOT
     * Debug-gated: it is behaviour, not checking.
     *
     * A COUNT OF EMISSIONS, NOT A FLAG, AND THAT IS A MEASURED CORRECTION. It
     * was an `int strand_reported` latch until a mutation battery deleted the
     * latch and SURVIVED the whole suite in both configurations: dropping the
     * early return makes the report fire once per stranded qubit, but it still
     * sets the flag, so a predicate reading the flag answers 1 either way. The
     * claim is "exactly once", which only a count can carry. */
    uint32_t        strand_reports;

    /* M07, Step 23 landing 2 — PRD §15 D15 §3's RESIDUE SPLIT, and `bd 06t`'s
     * first obligation: "an implementation that reports a single residue figure
     * has not yet decided which row each free lands on".
     *
     * THE EPISTEMIC STATE IS THREE-VALUED WHILE THE ACT IS TWO-VALUED, so the
     * split cannot be read off the pool. `cq_qubits_stranded()` is one total and
     * `cq_qubits_strand()` is Layer 0 — it has no handle, no proof and no
     * verdict, deliberately (`src/qubits.h`), so it cannot be told which row it
     * is serving without dragging the whole epistemic vocabulary one layer down
     * past M03's no-internal-dependencies boundary. The counters therefore live
     * HERE, beside the report they belong with, and are incremented by
     * cq_reg_free from the proof's own answer.
     *
     * TWO GRAINS, AND THEY GENUINELY DISAGREE — that is why both exist rather
     * than one being derivable from the other. The QUBIT pair is the residue
     * D15 §3 defines ("the qubit is never released … and is counted"); the RAIL
     * pair is what `bd 06t` literally asks for ("which row each free lands
     * on"). A MIXED rail whose first non-clean qubit is UNPROVEN and which
     * carries a conviction further along contributes to BOTH qubit rows while
     * landing on the rail-level DIRTY row alone, because cq_reg_disposition's
     * lattice makes dirty absorbing. Collapsing the two grains loses exactly
     * that rail, which is also the shape the disposition fold's
     * no-early-return exists for.
     *
     * NOT Debug-gated, for strand_reports' reason: this is behaviour a caller
     * reads, not checking. */
    uint32_t        stranded_dirty;     /* qubits convicted, then stranded    */
    uint32_t        stranded_unproven;  /* qubits merely unproven, stranded   */
    uint32_t        frees_dirty;        /* frees whose RAIL verdict was < 0   */
    uint32_t        frees_unproven;     /* frees whose RAIL verdict was == 0  */

#if defined(CQOPS_DEBUG_INVARIANTS) && CQOPS_DEBUG_INVARIANTS
    /* I6 (plan §0.2): inside a cq_sandwich compute half, every gate TARGET is
     * a bit of the scratch region. This is the half of the enforcement that
     * cannot be done by the type system — the other half is that cq_emit_*
     * takes controls as `const cq_bit *`, so a source cannot be materialised
     * by construction. NULL means no sandwich is active and the check is off;
     * M09 sets the extent at Step 8. The range is over cq_bit ADDRESSES, not
     * qubit indices, because I6 is a statement about which bits may be
     * written, and scratch is a contiguous cq_bit array. */
    const cq_bit *scratch_lo, *scratch_hi;
#endif
};

/* Borrows `sink` — it must outlive the context. Resolved ONCE here rather
 * than per gate: cq_sink_active() re-reads the environment on every call,
 * which is the right thing for a process-wide default and the wrong thing to
 * do a million times inside a kernel. Passing NULL takes cq_sink_active(). */
void cq_ctx_init(cq_ctx *ctx, const cq_sink *sink);
void cq_ctx_dispose(cq_ctx *ctx);

/* Takes a qubit from the pool and grows the shadow to cover it. The pool
 * guarantees |0> (I3) and a fresh shadow entry is born known-0, so the two
 * agree by construction. This is the only place those two facts are joined,
 * and cq_materialise is its only caller inside the library — tests use it to
 * build operands that look like a real program's. */
uint32_t cq_ctx_fresh_qubit(cq_ctx *ctx);

/* THE ONE JOINT WHERE A QUBIT LEAVES DATA USE, and the ORDER IS THE
 * ENFORCEMENT (PRD §10, bd ckd.17a). Verbatim from the PRD:
 *
 *     cq_qubits_release(&ctx->pool, q, proven_zero);   // aborts unless proven
 *     cq_shadow_retire(&ctx->shadow, q);               // reached only if it did
 *
 * The governing rule it exists to make unbreakable: a certificate may only be
 * written on a qubit that has ALREADY left data use. Retirement therefore
 * cannot run on a live qubit, which is what keeps poison sticky everywhere it
 * matters — a certified-but-live qubit read as a control hits
 * `t.unknown |= c.unknown`, so with `c.unknown` freshly zeroed the poison
 * stops propagating and the shadow starts claiming determinate downstream of a
 * genuine superposition. That is the one direction M02's discipline forbids,
 * and it is why the two calls may never be reordered or separated.
 *
 * `proven_zero` is the caller's evidence and stays the caller's problem — this
 * function adds none. Its THREE callers are cq_reg_free, which forwards a
 * per-qubit proof (M07); cq_sandwich's epilogue, which passes
 * CQ_ZERO_BY_PALINDROME (M09); and cq_ctrl_pop, which passes
 * CQ_ZERO_BY_CTRL_UNCOMPUTE for the shared Toffoli ancilla and the nested AND
 * flag (M06, Step 20). Those two literals are the only ones in src/ and there
 * is no third: PRD §10's rule is that a constant may exist only where the code
 * that RUNS a construction can assert that construction's premises.
 * Retirement here also closes the Step 7 hazard appended to ckd.17 (closed;
 * what cqrt_free reads at a CQ_lang rail is PRD §15 D15): nothing used to
 * un-poison a released index, so a REUSED index kept its stale entry —
 * cq_ctx_fresh_qubit only ensures up to `minted`, and cq_shadow_ensure returns
 * early. */
void cq_ctx_release_qubit(cq_ctx *ctx, uint32_t q, int proven_zero);

#endif /* CQOPS_CTX_H */
