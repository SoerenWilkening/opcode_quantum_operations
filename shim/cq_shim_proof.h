/* shim/cq_shim_proof.h — M26's free-time EVIDENCE, and the third home PRD §15
 * D15 named for it.
 *
 * `cq_reg_free` takes a `cq_zero_proof` and the library has never shipped one:
 * `src/reg.h` says so in as many words — "M07 SHIPS NO PROOF FUNCTION and the
 * library defines none … which is the correct behaviour UNTIL M26 SUPPLIES
 * D15's CERTIFICATE AT STEP 23". M26 has arrived, so this file is that supplier.
 * Without it `cqrt_free` cannot be written at all: a NULL proof is a hard error
 * for any rail that owns a qubit (src/reg.c), so there is no third option.
 *
 * TWO SHIPPED SENTENCES SAY THE PROOF "MUST NEVER MOVE INTO src/", AND BOTH ARE
 * HONOURED RATHER THAN OVERRIDDEN. `tests/support/poolcheck.h` and
 * `tests/test_kernel_addacc_upstream.inc` both forbid promoting
 * `cq_pc_zero_proof_rotation_free` into `src/`. Three things make this file
 * consistent with them rather than a violation of them, and all three matter:
 *
 *   1. IT IS NOT IN `src/`. It is Layer 5, M26, where D15 §7 puts the supplier
 *      and where `src/reg.h`'s own sentence points. Nothing in `src/` gained a
 *      proof; M07 still ships none.
 *   2. POOLCHECK'S PROHIBITION CARRIES ITS OWN EXPIRY CLAUSE and this is it —
 *      "NULL meaning 'no evidence, fail loud' stays the correct posture UNTIL
 *      M26 supplies D15's certificate (src/reg.h; bd 06t)".
 *   3. THE OBJECTION'S PREMISE WAS THE ABORT DISPOSITION, WHICH D15 §4 REMOVED.
 *      `test_kernel_addacc_upstream.inc` objects that the shadow "does
 *      hard-error on a genuinely clean rail … a rail a general Ry has touched".
 *      Since D15 §4's last clause was confirmed (2026-08-22) a rail the library
 *      cannot clear is STRANDED, not aborted — so the shadow's incompleteness
 *      costs qubits, loudly and once, instead of killing the program. That is
 *      exactly D15's residue, and it is what makes landing 1 MONOTONE: landing 2
 *      adds evidence beside this and converts strands to releases.
 *
 * SOUNDNESS AND COMPLETENESS ARE DIFFERENT HERE, AND THE DOCUMENTS BLUR THEM.
 * `cq_pc_zero_proof_rotation_free` is named for the surface on which it is
 * COMPLETE. This predicate is SOUND everywhere, in all three rows, and that is
 * not a stronger claim than poolcheck makes — it is the same claim read
 * carefully. `unknown` has exactly one producer in the whole library
 * (`cq_shadow_rotate`, and `src/rotate.c` is its only caller in `src/`, on two
 * of PRD §7's twelve cells — PRD §15 D12), and `cq_shadow_cx` / `cq_shadow_ccx`
 * only ever propagate it. So a DETERMINATE entry proves that neither the qubit
 * nor anything that ever controlled it met a general `Ry`, which makes the
 * classical shadow EXACT for that qubit: determinate-0 really is |0⟩ and
 * determinate-1 really is not. What the rotation-free scope buys is
 * COMPLETENESS — off it, the answer is UNPROVEN a great deal of the time, and
 * D15 measured that as "essentially nothing discharged" on the CQ_lang corpus.
 * Landing 2's certificate reads the CALL STREAM and is what fixes that.
 *
 * WHY IT IS A FILE AND NOT A `static` IN cq_runtime_rail.c. Two M26 files free:
 * this one's caller today (`cqrt_free`, and `cqrt_addc`'s two transients), and
 * `cq_template_impl.c` at Step 23.6, which must free D7b's defensive copy
 * (`bd 493`). A `static` duplicated across them is the improvised split Rule 12
 * exists to prevent, and landing 2 grows this file substantially — D15's ported
 * reduction and the per-handle record. IMPLEMENTATION_PLAN §3's M26 table
 * provisionally called that file `shim/cq_certificate.[ch]`; it is this file,
 * renamed before it was written so that landing 2 adds rather than moves.
 *
 * RULE 12. Budget 120. Reserve seam, recorded before landing 2 needs it:
 * `the EVIDENCE ↔ the RECORD` — the predicate stays here and D15's per-handle
 * write-history record moves to `shim/cq_shim_record.[ch]`. TRIGGER 240, the
 * house figure, measured against Rule 12's 300-line hard limit rather than
 * against this budget: an earlier draft of this line used the 0.8-of-budget
 * ratio `shim/cq_shim_ctx.c` records, which would put the trigger at 96 and
 * fire it the moment landing 2 adds a reduction engine — a two-file plan
 * wearing a trigger's clothes. The two files this step also landed reject the
 * ratio for the same reason and say so; three files in one step should not
 * carry two conventions.
 */
#ifndef CQ_SHIM_PROOF_H
#define CQ_SHIM_PROOF_H

#include <stdint.h>

#include "ctx.h"

/* THE EVIDENCE M26 SUPPLIES AT EVERY FREE. A `cq_zero_proof` (src/reg.h), so
 * three-valued BY SIGN: > 0 proven clean, == 0 unproven, < 0 proven DIRTY.
 *
 * It reads the shadow and nothing else, which is why the name says shadow. The
 * `h` is unused — this is per-qubit evidence — and landing 2 is what finally
 * reads it, because the certificate is keyed by handle. Do not delete the
 * parameter to silence anything: the signature is `src/reg.h`'s and changing it
 * would re-type the free path for a cosmetic reason.
 *
 * `unknown` FIRST, ALWAYS. A poisoned entry still carries its last determinate
 * value byte, frozen, so reading `value` first would call a poisoned rail clean
 * whenever that stale byte happened to read 0 — the laundering `src/shadow.h`
 * forbids by name — and would CONVICT one whose stale byte read 1, which is a
 * claim this predicate has no right to make. After a general `Ry` it knows
 * nothing, and UNPROVEN is what "nothing" is spelled. */
int cq_shim_shadow_proof(const cq_ctx *ctx, int32_t h, uint32_t q);

/* D15's OBSERVED UNDO CERTIFICATE — landing 2. Also a `cq_zero_proof`, so also
 * three-valued by sign, and it is what `src/reg.h` means by "the certificate is
 * what finally uses the `h`": this predicate reads the RAIL's recorded call
 * history and ignores `q` entirely, which is the exact mirror of the shadow
 * proof above.
 *
 * IT IS THE COMBINATION THAT `cqrt_free` INSTALLS, not this alone —
 * cq_shim_free_proof below. Exposed separately so a test can drive the
 * certificate's own verdict without the shadow masking or rescuing it.
 *
 * THREE ENTRY CONDITIONS INTO ONE ENGINE (D15 §2). U3 is the classical-immediate
 * fast path: a rail written only by `addc`/`xorc` has an arithmetic answer and
 * needs no reduction. U1 and U2 are the same call: does the write history since
 * the MINT reduce to the identity? U1's `_unc` is not a separate rule — the
 * forward template call is RECORDED as a write to the rail it mints, with the
 * `_unc` as its declared twin, so the one engine pairs them.
 *
 * AND THEN THE BIRTH VALUE DECIDES THE SIGN, which is the port's one deliberate
 * divergence from upstream's obligation and is where D15 §4's carve-out comes
 * from: a history that reduces returns the rail to the value it was MINTED
 * holding, not to zero. Zero is CLEAN; anything else is a CONVICTION. */
int cq_shim_certificate(const cq_ctx *ctx, int32_t h, uint32_t q);

/* WHAT `cqrt_free` ACTUALLY INSTALLS: the certificate and the shadow together.
 *
 * DIRTY DOMINATES, THEN CLEAN, THEN UNPROVEN. Both predicates are SOUND in all
 * three rows and differ only in COMPLETENESS — the shadow is complete on the
 * rotation-free surface and discharges essentially nothing on the corpus; the
 * certificate is the other way round — so either one's proof suffices and
 * either one's conviction stands. They can only contradict if one is unsound,
 * and letting DIRTY win there is the conservative reading: it strands.
 *
 * THE ORDER MATTERS AND `CLEAN` FIRST WOULD BE WRONG. A rail the certificate
 * convicts (a non-zero birth literal with a cancelling rotation pair) is one
 * the SHADOW cannot see at all — it reports `unknown` — so a best-evidence-wins
 * rule that took the first non-zero answer would depend on which predicate was
 * consulted first. Dominance has no such ordering dependence. */
int cq_shim_free_proof(const cq_ctx *ctx, int32_t h, uint32_t q);

#endif /* CQ_SHIM_PROOF_H */
