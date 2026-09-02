/* shim/cq_shim_qram.h — the QRAM PAYLOAD TABLE (v1.2, PRD §15 D24, plan §0.6,
 * `bd 9zq`, Step 27). What an array token OWNS beyond its identity.
 *
 * THE TOKEN IS M07's; THE PAYLOAD IS THE SHIM's. `cqrt_qram_alloc_<W>(count)`
 * mints the SAME `CQ_SLOT_TOKEN` a tape does, out of the SAME D5 counter (plan
 * §0.5 — one token state, two owners, no fifth state), and then `count`
 * ordinary M07 registers of width W, consecutively, all-constant at birth (I4:
 * zero qubits) and NEVER freed — the ABI has no qram free, so the cells are the
 * program's persistent state. What the token owns beyond identity — the
 * element width, the count, the first cell handle and the per-array LIFO tape
 * stack — lives HERE, keyed by the token handle, never on `cq_reg`: M07
 * carries identity and nothing else (D23's last paragraph).
 *
 * A CELL IS A REGISTER, NOT A SHIM-OWNED cq_bit ARRAY, because L2 and I2 are
 * stated over registers: `cq_reg_audit` and `cq_pc_live_is_exactly` would read
 * a qubit materialised into a shim-owned array as live-and-unowned. As
 * registers the cells cost L2 nothing. The D5 counter advances by 1 + count
 * per alloc, which is the numbering divergence D18 already retired.
 *
 * THE TAPE STACK IS PER ARRAY AND LIFO, ON CQ_lang's OWN CONTRACT (PRD-7.5
 * §2.8(ii)): `_unc` twins fire in strict reverse program order of the forward
 * stores to one array. An entry records the SLOT the shim minted and the
 * (idx, val, pred) handles the push was called with; the pop VERIFIES the top
 * against its own operands — handles are D5-monotonic, so equality is
 * identity — and a mismatch or an empty stack is the runtime file's hard
 * error in both configurations.
 *
 * TWO OWNERS OF ONE STATE, AND `cq_qram_find` IS HOW EITHER SIDE TELLS: NULL for
 * a rail, a tombstone or a TAPE token; the array for a qram token. That is what
 * lets cq_runtime_tape.c refuse a qram array in its tape slot — until this file
 * existed the state check alone let one through.
 *
 * RULE 12. Budget 140, recorded before the file existed. Seam: `the ARRAY
 * TABLE ↔ the TAPE STACK` → shim/cq_shim_qram_tape.c; discriminator: a line
 * that names a slot handle is the stack's. Trigger 240.
 */
#ifndef CQ_SHIM_QRAM_H
#define CQ_SHIM_QRAM_H

#include <stdint.h>

#include "ctx.h"

/* One pushed store: the tape SLOT (a W-bit rail CQ_lang never sees, born |0>)
 * and the operands the push named, for the pop to verify against. */
typedef struct {
    int32_t slot, idx, val, pred;   /* pred is CQ_REG_NONE for the plain family */
} cq_qram_entry;

typedef struct {
    int32_t        token;      /* the a<N> handle                              */
    uint32_t       width;      /* element width W                              */
    int32_t        count;      /* cells                                        */
    int32_t        cell0;      /* cell j is handle cell0 + j                   */
    cq_qram_entry *tape;       /* the LIFO, top at n_tape − 1                  */
    uint32_t       n_tape, cap_tape;
} cq_qram_array;

/* Mint the token, then `count` cells, and give the token a record HISTORY
 * (D24 (c): unlike a tape token, an array token is written by every store and
 * read by every load, so the load pair's certificate can see a store between a
 * load and its `_unc`). Zero gates, zero qubits. `count <= 0` and
 * `count > CQ_QRAM_COUNT_MAX` are hard errors naming the symbol's contract. */
int32_t cq_qram_alloc(cq_ctx *ctx, uint32_t width, int32_t count);

/* The array behind a handle, or NULL if the handle is not a qram token. */
const cq_qram_array *cq_qram_find(int32_t h);
cq_qram_array       *cq_qram_find_mut(int32_t h);

static inline int32_t cq_qram_cell(const cq_qram_array *a, int32_t j)
{
    return a->cell0 + j;
}

void                 cq_qram_push(cq_qram_array *a, cq_qram_entry e);
const cq_qram_entry *cq_qram_top (const cq_qram_array *a);   /* NULL if empty */
void                 cq_qram_pop (cq_qram_array *a);         /* top must exist */

/* How many arrays the table holds — for tests. */
uint32_t cq_qram_arrays(void);

/* Drops every array and every stack. Called by cq_shim_ctx_reset, on
 * cq_rec_reset's precedent: the table is keyed by handle, handles restart at
 * 0 with a fresh context, and a second test case would otherwise find the
 * first case's array under its own token number. */
void cq_qram_reset(void);

#endif /* CQ_SHIM_QRAM_H */
