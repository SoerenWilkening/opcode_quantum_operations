/* shim/cq_shim_record.h — D15's per-handle OBSERVED WRITE HISTORY, and the
 * EFFECT TABLE that says what each frozen-ABI opcode reads and writes.
 *
 * THE SEAM THIS TOOK AND THE ONE IT RESERVES. `shim/cq_shim_proof.h` recorded
 * `the EVIDENCE ↔ the RECORD` before landing 2 existed; this file is that
 * record. Its OWN reserve seam is `the RECORD ↔ the REDUCTION`, recorded in
 * IMPLEMENTATION_PLAN §3 before either file was written and TAKEN immediately:
 * the ported engine is `shim/cq_shim_reduce.[ch]`. The discriminator is which
 * PINNED ARTEFACT a line answers to — a wrong read/write set is an ABI
 * transcription bug that `cq_runtime.h` settles, a wrong pairing is an analysis
 * bug that `third_party/cq_free_pairing/free_pairing_check.py` settles, and no
 * line answers to both.
 *
 * IT IS A CALL STREAM, NOT A GATE LIST, and that is Rule 13 rather than a
 * preference. One entry per `cqrt_*` / `cq_shim_*` ENTRY POINT — the layer
 * CQ_lang actually calls — never one per emitted gate. A W-bit `cqrt_cswap` is
 * ONE record and 3W gates, and the gates are gone from our side the moment they
 * reach the sink.
 *
 * ENUMERATE THE WRITE SET FROM cq_runtime.h, NEVER FROM THE SYMBOL NAMES
 * (PRD §15 D15 §6(ii)). The table below is transcription, and three of its rows
 * are transcription precisely because the name lies:
 *
 *   cqrt_cswap(ctrl, a, b)              WRITES BOTH a AND b. "swap" is
 *       symmetric and upstream models it reads=(0,) writes=(1,2). A tracker
 *       that misses it silently WIDENS every rule, and D15 §6(ii) reproduces
 *       to the unit the historical figure that modelling error produced.
 *   cqrt_cnot_controlled(ctrl, target_ctrl, tgt)   `target_ctrl` is a READ.
 *       The word "target" inside a parameter named `target_ctrl` is the INNER
 *       CONTROL; the write is arg 2.
 *   cqrt_copy_<W>(src, dst)             the ABI's order is (src, dst) and
 *       libcqops' cq_reg_xor_into is (dst, src). Both are int32_t, so nothing
 *       a compiler can see separates them.
 *
 * AND ITS PRECONDITION IS FALSE IN THE CORPUS: cq_runtime.h says a copy's dst
 * is "a fresh |0> register", and the measured corpus targets an already-written
 * destination in a large fraction of integer controlled copies. It is modelled
 * `dst ^= src`, never `dst := src`, which is also what the shim implements.
 *
 * CONTROLS ⊆ READS IS A STRUCTURAL PRECONDITION OF THE PORTED ENGINE, not a
 * convention. `co_written_stable`'s soundness argument splits its window in two
 * and discharges the second half by observing that the READS loop has already
 * demanded the flag be unchanged across the whole interval. A future opcode
 * whose control is not in its read set breaks that argument silently. Every row
 * below satisfies it and `_Static_assert`s in the .c say so.
 *
 * RULE 12. Reserve seam, recorded at landing rather than when it bites:
 * `the STREAM ↔ the EFFECT TABLE`. The append-only call log, the per-handle
 * index and the `cqrt_x` parity counter stay here; the `EFF` table with its
 * `controls ⊆ reads` and completeness asserts moves to
 * `shim/cq_shim_effects.[ch]`. The discriminator is the same one that split
 * this file from `cq_shim_reduce.[ch]` — which PINNED ARTEFACT a line answers
 * to — read one level finer: a row answers to `cq_runtime.h`, a stream detail
 * answers to nothing but itself. TRIGGER 240, the house figure. What would take
 * it is v2: the 63 `qram` rows are aborts today and each needs a row here
 * first (the 11 `tape` symbols took TWO rows on 2026-09-02, PRD §15 D23 —
 * one per shape, as `cqrt_copy` has — and did not move the count much).
 *
 * WHAT IS DELIBERATELY *NOT* IN THE TABLE. The 63 qram, 34 fp-width
 * and `cqrt_alloc_handle` symbols are `cq_shim_unsupported` aborts in v1
 * (PRD §15 D16), so a call can never reach the recorder — which is why the
 * recorder cannot be WRONG about them, and why their notoriously misleading
 * write sets (`qram_store` writes arg 0 and READS `val`; the two `_unc`s in one
 * family use OPPOSITE slot conventions) are documented in `bd 06t`'s write
 * model rather than transcribed here. Adding any of them to v2 means adding a
 * row here FIRST; an unmodelled opcode reaching the recorder is a hard error,
 * never a skipped check, on upstream's own posture. */
#ifndef CQ_SHIM_RECORD_H
#define CQ_SHIM_RECORD_H

#include <stdint.h>

/* The v1 opcode surface, one entry per shape the recorder can see. The
 * numbering is not load-bearing except for CQ_ROP_NONE == 0, which is asserted
 * in the .c: a zero-initialised record must be the INERT row, so a caller who
 * forgets to assign records nothing rather than recording an `x`. */
typedef enum {
    CQ_ROP_NONE = 0,
    /* THE MINT OCCUPIES A STREAM POSITION AND WRITES NOTHING, and both halves
     * of that are load-bearing. The reduction's window is STRICTLY open —
     * `lo < p < hi`, which is its termination proof — so a rail whose birth
     * position equalled its first write's position would have that write
     * EXCLUDED from its own history. Measured, and it is not a subtle failure:
     * every U1, U2, U3 and ckd.18 case answered as though the first call had
     * not happened, which for a two-call cancelling pair means one unpaired
     * write and a strand, and for a lone forward means an EMPTY history that
     * reduces vacuously and RELEASES. The second is the unsound direction, so
     * the off-by-one had one safe face and one unsafe one.
     *
     * It writes nothing because a mint is not a write: the rail's state at
     * birth is the BIRTH VALUE, which the history is measured against rather
     * than being part of. Upstream has this for free — its `alloc` really is a
     * call in the stream — and the port has to say it. */
    CQ_ROP_MINT,
    CQ_ROP_X, CQ_ROP_CNOT, CQ_ROP_TOFFOLI,
    CQ_ROP_X_CTRL, CQ_ROP_CNOT_CTRL,
    CQ_ROP_RY, CQ_ROP_RZ,
    CQ_ROP_RY_CTRL_INV, CQ_ROP_RZ_CTRL, CQ_ROP_RZ_CTRL_INV,
    CQ_ROP_COPY, CQ_ROP_COPY_CTRL,
    CQ_ROP_CSWAP,
    /* PRD §15 D23 (v1.1): `cqrt_tape_write_<W>(tape, src) -> out` and its
     * controlled twin. `cqrt_copy`'s two rows with the destination MINTED
     * rather than named: the write READS `src` and writes the kept rail from
     * birth 0. The TAPE TOKEN IS NOT AN OPERAND SLOT — it has no history, no
     * mint marker, and `cq_rec_hist(token)` is NULL; it is represented in this
     * layer by its absence, because a token is never written and never read.
     * Every pairing flag is 0: a second write mints a DIFFERENT `out`, so
     * `self_adjoint` would be a lie, and CQ_lang never frees the kept rail. */
    CQ_ROP_TAPE_WRITE, CQ_ROP_TAPE_WRITE_CTRL,
    CQ_ROP_ADDC, CQ_ROP_XORC,
    CQ_ROP_TPL_FWD, CQ_ROP_TPL_UNC,
    CQ_ROP__N
} cq_rop;

/* One recorded call. Operands are HANDLES, never qubit indices: the certificate
 * is keyed by handle (D15 §7 — "the certificate is what finally uses the h"),
 * and a qubit index is recycled LIFO (D4) while a handle is monotonic (D5), so
 * an index cannot identify a rail across a free. */
typedef struct {
    uint16_t op;
    int32_t  h[3];      /* CQ_REG_NONE for an absent slot                    */
    uint64_t imm;       /* addc/xorc immediate, already masked to the width  */
    uint64_t angle;     /* the double's BIT PATTERN — see the .c on why      */
    int32_t  ctrl;      /* the §9 region's flag handle, or CQ_REG_NONE       */
    uint32_t tag;       /* template opcode identity; 0 elsewhere             */
    uint32_t xpar[3];   /* each operand's cqrt_x parity, FROZEN here         */
} cq_call_rec;   /* NOT `cq_rec` — tests/support/mock_sink.h already owns that
                  * name for a RECORDED GATE, and the two would collide in any
                  * translation unit that includes both. Only a test including
                  * both could ever see it, which is exactly what happened. */

/* A handle's history. `birth` is the classical value it was minted holding —
 * the field upstream has no analogue for and the one that makes the port SOUND
 * for Rule 6 rather than merely faithful (see the .c). */
typedef struct {
    int      live;          /* has this handle been minted through here?     */
    int      from_template; /* minted by a cq_template_* forward             */
    uint64_t birth_lo, birth_hi;
    uint32_t birth_pos;     /* stream position of the mint                   */
    uint32_t width;
    long     first_rot;     /* position of the first NON-DIAGONAL rotation   */
    long     last_read;     /* position of the last READ of this handle      */
    uint32_t *w;            /* stream positions at which it was WRITTEN      */
    uint32_t nw, capw;
} cq_hist;

/* The effect table, one row per cq_rop. Masks are over operand slots 0..2. */
typedef struct {
    uint8_t reads, writes, controls, imms;
    uint8_t diagonal;       /* phase-only: never enters a write history      */
    uint8_t self_adjoint;   /* pairs with an identical call                  */
    uint8_t neg_angle;      /* pairs with the same call at -angle            */
    uint8_t neg_imm;        /* pairs with the same call at -imm              */
    uint8_t twin;           /* pairs with cq_rop `twin_of`                   */
    uint8_t twin_of;
} cq_reff;

const cq_reff *cq_rec_effect(uint16_t op);

/* Every row but CQ_ROP_NONE models at least one write. A row left implicitly
 * zero is the INERT row, i.e. "reads nothing, writes nothing" — the WIDEST
 * possible model, and the direction that discharges frees it should not. */
int cq_rec_table_is_complete(void);

/* Recording. `cq_rec_reset` drops everything; `cq_shim_ctx_reset` calls it, so
 * a second test case does not inherit the first's stream. */
void cq_rec_reset(void);
void cq_rec_mint(int32_t h, uint32_t width, uint64_t lo, uint64_t hi,
                 int from_template);
void cq_rec_push(const cq_call_rec *c);
void cq_rec_retire(int32_t h);

/* EXCHANGE TWO HANDLES' HISTORIES. `cqrt_cswap`'s constant-ONE row swaps the
 * two rails' BIT ARRAYS rather than emitting gates, so after it handle `a`
 * holds what `b` held. A history keyed by handle must move with the contents or
 * the certificate reasons about the wrong rail — silently, because both rails
 * are still live and still the right width. This is the record's half of
 * `cq_reg_swap_bits`. */
void cq_rec_swap(int32_t a, int32_t b);

/* Read side, for the reduction and for tests. */
const cq_hist *cq_rec_hist(int32_t h);
const cq_call_rec  *cq_rec_at(uint32_t pos);
uint32_t       cq_rec_len(void);

/* The positions at which `h` was written, strictly inside (lo, hi). THE
 * STRICTNESS IS THE TERMINATION PROOF of the ported engine and is not a
 * stylistic choice — see shim/cq_shim_reduce.h. Returns the count and fills
 * `out` up to `cap`; a caller that overflows `cap` gets the truth about how
 * many there were, which is what lets the engine refuse rather than truncate. */
uint32_t cq_rec_writes_in(int32_t h, uint32_t lo, uint32_t hi,
                          uint32_t *out, uint32_t cap);

#endif /* CQ_SHIM_RECORD_H */
