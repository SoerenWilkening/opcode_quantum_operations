/* shim/cq_shim_record.c — see cq_shim_record.h for the seam, the table's
 * provenance and the three rows whose names lie about their write sets. */

#include "cq_shim_record.h"

#include <stdlib.h>
#include <string.h>

#include "reg.h"

/* A zero-initialised record must be the INERT row (bit.h's own idiom: "a
 * renumbering must break a build, not just a comment"). */
_Static_assert(CQ_ROP_NONE == 0, "a zeroed cq_call_rec must record nothing");

#define R(i) (uint8_t)(1u << (i))

/* THE EFFECT TABLE. Transcribed from CQ_lang/runtime/cq_runtime.h, slot by
 * slot. `controls` is always a SUBSET of `reads` — asserted below, because the
 * ported engine's co_written_stable rests on it. */
static const cq_reff EFF[CQ_ROP__N] = {
/*                        reads         writes        controls    imms   diag self negA negI twin twin_of */
[CQ_ROP_NONE]        = {0,              0,            0,          0,     0, 0, 0, 0, 0, 0},
[CQ_ROP_MINT]        = {0,              0,            0,          0,     0, 0, 0, 0, 0, 0},
/* GIVING THE MINT A WRITE IS A MEASURED EQUIVALENT, and WHY it is one is the
 * marker's whole design working: the mint's record sits at `birth_pos`, and
 * every window the certificate opens is `(birth_pos, now)` STRICTLY, so a write
 * recorded at `birth_pos` is excluded from the rail's own history by the same
 * strictness that made the marker necessary. The row stays zero because it is
 * TRUE — a mint is a birth, not a write — and because a future window that did
 * include its own left endpoint would then be wrong twice instead of once. */
[CQ_ROP_X]           = {0,              R(0),         0,          0,     0, 1, 0, 0, 0, 0},
[CQ_ROP_CNOT]        = {R(0),           R(1),         R(0),       0,     0, 1, 0, 0, 0, 0},
[CQ_ROP_TOFFOLI]     = {R(0)|R(1),      R(2),         R(0)|R(1),  0,     0, 1, 0, 0, 0, 0},
[CQ_ROP_X_CTRL]      = {R(0),           R(1),         R(0),       0,     0, 1, 0, 0, 0, 0},
[CQ_ROP_CNOT_CTRL]   = {R(0)|R(1),      R(2),         R(0)|R(1),  0,     0, 1, 0, 0, 0, 0},
[CQ_ROP_RY]          = {0,              R(0),         0,          0,     0, 0, 1, 0, 0, 0},
[CQ_ROP_RZ]          = {0,              R(0),         0,          0,     1, 0, 1, 0, 0, 0},
[CQ_ROP_RY_CTRL_INV] = {R(0),           R(1),         R(0),       0,     0, 0, 1, 0, 0, 0},
[CQ_ROP_RZ_CTRL]     = {R(0),           R(1),         R(0),       0,     1, 0, 1, 0, 0, 0},
[CQ_ROP_RZ_CTRL_INV] = {R(0),           R(1),         R(0),       0,     1, 0, 1, 0, 0, 0},
[CQ_ROP_COPY]        = {R(0),           R(1),         0,          0,     0, 1, 0, 0, 0, 0},
[CQ_ROP_COPY_CTRL]   = {R(0)|R(1),      R(2),         R(0),       0,     0, 1, 0, 0, 0, 0},
[CQ_ROP_CSWAP]       = {R(0),           R(1)|R(2),    R(0),       0,     0, 1, 0, 0, 0, 0},
[CQ_ROP_TAPE_WRITE]  = {R(0),           R(1),         0,          0,     0, 0, 0, 0, 0, 0},
[CQ_ROP_TAPE_WRITE_CTRL] = {R(0)|R(1),  R(2),         R(0),       0,     0, 0, 0, 0, 0, 0},
/* D24: slots are (out, arr, idx) for a load and (arr, idx, val, T) for a store.
 * The pop READS the slot it restores from; the push does not (T is born |0>). */
[CQ_ROP_QRAM_LOAD]      = {R(1)|R(2),      R(0),         0,          0,     0, 0, 0, 0, 1, CQ_ROP_QRAM_LOAD_UNC},
[CQ_ROP_QRAM_LOAD_UNC]  = {R(1)|R(2),      R(0),         0,          0,     0, 0, 0, 0, 1, CQ_ROP_QRAM_LOAD},
[CQ_ROP_QRAM_STORE]     = {R(0)|R(1)|R(2), R(0)|R(3),    0,          0,     0, 0, 0, 0, 1, CQ_ROP_QRAM_STORE_UNC},
[CQ_ROP_QRAM_STORE_UNC] = {R(0)|R(1)|R(2)|R(3), R(0)|R(3), 0,        0,     0, 0, 0, 0, 1, CQ_ROP_QRAM_STORE},
[CQ_ROP_QRAM_STORE_CTRL]     = {R(0)|R(1)|R(2), R(0)|R(3), 0,        0,     0, 0, 0, 0, 1, CQ_ROP_QRAM_STORE_CTRL_UNC},
[CQ_ROP_QRAM_STORE_CTRL_UNC] = {R(0)|R(1)|R(2)|R(3), R(0)|R(3), 0,   0,     0, 0, 0, 0, 1, CQ_ROP_QRAM_STORE_CTRL},
[CQ_ROP_ADDC]        = {0,              R(0),         0,          R(1),  0, 0, 0, 1, 0, 0},
[CQ_ROP_XORC]        = {0,              R(0),         0,          R(1),  0, 1, 0, 0, 0, 0},
[CQ_ROP_TPL_FWD]     = {R(1)|R(2),      R(0),         0,          0,     0, 0, 0, 0, 1, CQ_ROP_TPL_UNC},
[CQ_ROP_TPL_UNC]     = {R(1)|R(2),      R(0),         0,          0,     0, 0, 0, 0, 1, CQ_ROP_TPL_FWD},
};

/* CONTROLS ⊆ READS, ONE ROW AT A TIME. Not a loop and not a comment: the
 * ported engine's `co_written_stable` discharges half its soundness argument by
 * observing that the READS loop has already demanded the flag be unchanged over
 * the whole interval, and a row with a control outside its read set breaks that
 * silently, in the discharge direction. A _Static_assert is the only detector
 * for a table entry — `angle.h`'s recorded lesson, applied to data. */
#define CQ_CR(row) _Static_assert((EFF_CR_##row) == 0, \
    "controls must be a subset of reads: " #row)
#define EFF_CR_CNOT        (R(0) & ~(R(0)))
#define EFF_CR_TOFFOLI     ((R(0)|R(1)) & ~(R(0)|R(1)))
#define EFF_CR_X_CTRL      (R(0) & ~(R(0)))
#define EFF_CR_CNOT_CTRL   ((R(0)|R(1)) & ~(R(0)|R(1)))
#define EFF_CR_RY_CTRL_INV (R(0) & ~(R(0)))
#define EFF_CR_RZ_CTRL     (R(0) & ~(R(0)))
#define EFF_CR_RZ_CTRL_INV (R(0) & ~(R(0)))
#define EFF_CR_COPY_CTRL   (R(0) & ~(R(0)|R(1)))
#define EFF_CR_CSWAP       (R(0) & ~(R(0)))
#define EFF_CR_TAPE_WRITE_CTRL (R(0) & ~(R(0)|R(1)))
CQ_CR(CNOT); CQ_CR(TOFFOLI); CQ_CR(X_CTRL); CQ_CR(CNOT_CTRL);
CQ_CR(RY_CTRL_INV); CQ_CR(RZ_CTRL); CQ_CR(RZ_CTRL_INV);
CQ_CR(COPY_CTRL); CQ_CR(CSWAP); CQ_CR(TAPE_WRITE_CTRL);

/* AND THE TABLE MUST BE COMPLETE. A row left implicitly zero is the INERT row,
 * which for a real opcode means "reads nothing, writes nothing" — the widest
 * possible model, and the direction that discharges frees it should not. Every
 * row but CQ_ROP_NONE therefore has at least one write. */
_Static_assert(sizeof EFF / sizeof EFF[0] == (size_t)CQ_ROP__N,
               "the effect table must have exactly one row per cq_rop");

const cq_reff *cq_rec_effect(uint16_t op)
{
    return op < (uint16_t)CQ_ROP__N ? &EFF[op] : NULL;
}

/* --- the stream and the per-handle index -------------------------------- */

static cq_call_rec  *g_call;
static uint32_t g_n, g_cap;
static cq_hist *g_h;
static uint32_t g_nh;

/* cqrt_x PARITY, per handle, incremented on EVERY CQ_ROP_X and FROZEN into the
 * record at the position the call is pushed. It does double duty in the ported
 * engine and both duties are load-bearing: the same gate on the COMPLEMENTARY
 * branch of its flag is NOT the adjoint (so a differing parity REFUSES a pair),
 * and two writes on complementary branches of one flag provably commute (so a
 * differing parity ADMITS a reordering). Frozen at push, never read live: the
 * counter keeps moving and a later read would answer about the wrong moment. */
static uint32_t *g_xpar;

static void grow_handles(int32_t h)
{
    uint32_t need = (uint32_t)h + 1u;
    if (need <= g_nh) return;
    g_h    = realloc(g_h, need * sizeof *g_h);
    g_xpar = realloc(g_xpar, need * sizeof *g_xpar);
    if (!g_h || !g_xpar) abort();
    memset(&g_h[g_nh], 0, (need - g_nh) * sizeof *g_h);
    memset(&g_xpar[g_nh], 0, (need - g_nh) * sizeof *g_xpar);
    for (uint32_t i = g_nh; i < need; i++) {
        g_h[i].first_rot = -1;
        g_h[i].last_read = -1;
    }
    g_nh = need;
}

void cq_rec_reset(void)
{
    for (uint32_t i = 0; i < g_nh; i++) free(g_h[i].w);
    free(g_h); free(g_xpar); free(g_call);
    g_h = NULL; g_xpar = NULL; g_call = NULL;
    g_nh = 0; g_n = 0; g_cap = 0;
}

void cq_rec_mint(int32_t h, uint32_t width, uint64_t lo, uint64_t hi,
                 int from_template)
{
    if (h < 0) return;
    grow_handles(h);
    free(g_h[h].w);
    memset(&g_h[h], 0, sizeof g_h[h]);
    g_h[h].live          = 1;
    g_h[h].from_template = from_template;
    g_h[h].birth_lo      = lo;
    g_h[h].birth_hi      = hi;
    g_h[h].birth_pos     = g_n;   /* the slot the marker below occupies */
    g_h[h].width         = width;
    g_h[h].first_rot     = -1;
    g_h[h].last_read     = -1;

    /* THE MARKER. It must be pushed AFTER birth_pos is read, and the two must
     * agree: birth_pos names this record's own slot, so every later write is
     * strictly above it and the strictly-open window contains the whole
     * history. */
    {
        cq_call_rec c;
        memset(&c, 0, sizeof c);
        c.op   = (uint16_t)CQ_ROP_MINT;
        c.h[0] = h; c.h[1] = -1; c.h[2] = -1; c.h[3] = -1;
        c.ctrl = -1;
        cq_rec_push(&c);
    }
}

void cq_rec_retire(int32_t h)
{
    if (h < 0 || (uint32_t)h >= g_nh) return;
    g_h[h].live = 0;
}

void cq_rec_swap(int32_t a, int32_t b)
{
    cq_hist t;
    if (a < 0 || b < 0 || a == b) return;
    grow_handles(a > b ? a : b);
    t = g_h[a]; g_h[a] = g_h[b]; g_h[b] = t;
}

static void note_write(int32_t h, uint32_t pos)
{
    cq_hist *r;
    if (h < 0) return;
    grow_handles(h);
    r = &g_h[h];
    if (r->nw == r->capw) {
        uint32_t cap = r->capw ? r->capw * 2u : 8u;
        uint32_t *p  = realloc(r->w, cap * sizeof *p);
        if (!p) abort();
        r->w = p; r->capw = cap;
    }
    r->w[r->nw++] = pos;
}

void cq_rec_push(const cq_call_rec *c)
{
    const cq_reff *e = cq_rec_effect(c->op);
    uint32_t pos = g_n;
    cq_call_rec  *slot;

    /* AN UNMODELLED OPCODE IS A HARD ERROR, NEVER A SKIPPED CHECK — upstream's
     * own posture, and the one that matters most here: a silently ignored call
     * makes every rule WIDER, which discharges frees it should not. */
    if (!e || c->op == CQ_ROP_NONE) abort();

    if (g_n == g_cap) {
        uint32_t cap = g_cap ? g_cap * 2u : 64u;
        cq_call_rec  *p   = realloc(g_call, cap * sizeof *p);
        if (!p) abort();
        g_call = p; g_cap = cap;
    }
    slot = &g_call[g_n++];
    *slot = *c;

    for (uint32_t i = 0; i < CQ_REC_SLOTS; i++) {
        int32_t h = c->h[i];
        if (h < 0) continue;
        grow_handles(h);
        /* PARITY IS FROZEN ON CONTROL SLOTS ONLY, and that is upstream's shape
         * rather than a saving: its `ctl_parity` is a tuple aligned to
         * `eff.controls`, and both readers zip against `eff.controls`.
         *
         * FREEZING IT ON EVERY SLOT IS A REAL BUG AND IT WAS MEASURED HERE.
         * `cqrt_x` increments the parity of the rail it TARGETS, so with the
         * target slot included two consecutive `cqrt_x(q)` calls carry
         * parities 0 and 1 — and `adjoint_matches` refuses a pair whose
         * parities differ. The most basic self-inverse pair in the model
         * stopped cancelling, and the failure direction was a DECLINE, which is
         * why it presented as "nothing reduces" rather than as a miscompile.
         *
         * AND IT IS MASKED TO ONE BIT — `& 1u`, read off the pinned source
         * (`free_pairing_check.py:1026-1027`, whose own comment says "Parity
         * MOD 2 — cqrt_x is an involution, so only the POLARITY of the flag at
         * this point matters, not how many brackets deep it is"). Storing the
         * raw counter is a second, independent decline bug and it is the one
         * that would have cost the corpus its commonest shape: the two-arm
         * emitter's `¬flag cqrt_x` bracket flips the flag TWICE around an arm,
         * so an unmasked counter reports the same branch as parities 0 and 2
         * and refuses to pair the arm with its undo. Reading the pin is what
         * settled this; it is not derivable from the four function names
         * `bd 06t` cites, which is the whole argument for vendoring. */
        slot->xpar[i] = (e->controls & R(i)) ? (g_xpar[h] & 1u) : 0u;
        if (e->reads & R(i)) {
            /* THE R1 SUBSTITUTE'S SECOND HALF. Upstream layers a whole
             * correlation closure over its rules; ours is a past-only witness
             * and is STRICTLY STRONGER — see cq_shim_reduce.h. */
            if ((long)pos > g_h[h].last_read) g_h[h].last_read = (long)pos;
        }
    }
    /* A REGION'S CONTROL FLAG IS A READ TOO, and it is the one read that is not
     * in any operand slot: the flag never reaches cq_reg_check_operands and the
     * template path performs no ctrl/operand distinctness check at all. */
    if (c->ctrl >= 0) {
        grow_handles(c->ctrl);
        if ((long)pos > g_h[c->ctrl].last_read) g_h[c->ctrl].last_read = (long)pos;
    }

    /* A DIAGONAL GATE NEVER ENTERS A WRITE HISTORY (PRD §15 D12; upstream's
     * `if not e.diagonal`). It maps |v> to a phase times |v> and so cannot move
     * a computational-basis value. It is still recorded as a CALL, because its
     * operands are still reads and its position still bounds an interval. */
    if (!e->diagonal) {
        for (uint32_t i = 0; i < CQ_REC_SLOTS; i++)
            if ((e->writes & R(i)) && c->h[i] >= 0) note_write(c->h[i], pos);
    }

    /* A NON-DIAGONAL ROTATION TAINTS THE RAIL IT WRITES, for the R1 substitute.
     * Only the general `Ry` column reaches here: every `Rz` row is diagonal. */
    if ((c->op == CQ_ROP_RY || c->op == CQ_ROP_RY_CTRL_INV)) {
        for (uint32_t i = 0; i < CQ_REC_SLOTS; i++)
            if ((e->writes & R(i)) && c->h[i] >= 0 && g_h[c->h[i]].first_rot < 0)
                g_h[c->h[i]].first_rot = (long)pos;
    }

    if (c->op == CQ_ROP_X && c->h[0] >= 0) g_xpar[c->h[0]]++;
}

const cq_hist *cq_rec_hist(int32_t h)
{
    if (h < 0 || (uint32_t)h >= g_nh || !g_h[h].live) return NULL;
    return &g_h[h];
}

const cq_call_rec *cq_rec_at(uint32_t pos) { return pos < g_n ? &g_call[pos] : NULL; }
uint32_t      cq_rec_len(void)        { return g_n; }

uint32_t cq_rec_writes_in(int32_t h, uint32_t lo, uint32_t hi,
                          uint32_t *out, uint32_t cap)
{
    uint32_t n = 0;
    const cq_hist *r;
    if (h < 0 || (uint32_t)h >= g_nh) return 0u;
    r = &g_h[h];
    for (uint32_t i = 0; i < r->nw; i++) {
        /* STRICTLY lo < p < hi. Relaxing either end to `<=` is the documented
         * one-character way to break the engine's narrowing termination. */
        if (r->w[i] > lo && r->w[i] < hi) {
            if (n < cap) out[n] = r->w[i];
            n++;
        }
    }
    return n;
}

/* The completeness half of the table check, at RUNTIME because C has no
 * constant-expression way to fold over a designated-initialiser array. Called
 * once from the certificate's first consult; a row that models nothing would
 * make every rule that reads it wider. */
int cq_rec_table_is_complete(void)
{
    for (int i = CQ_ROP_NONE + 1; i < CQ_ROP__N; i++) {
        /* CQ_ROP_MINT IS THE ONE ROW THAT LEGITIMATELY WRITES NOTHING, and it
         * is exempted BY NAME rather than by the loop happening not to reach
         * it: a mint is the rail's birth, not a write to it, and the birth
         * value is what the history is measured AGAINST. Every other row that
         * writes nothing is a transcription slip in the widening direction. */
        if (i == CQ_ROP_MINT) continue;
        if (EFF[i].writes == 0u) return 0;
    }
    return 1;
}
