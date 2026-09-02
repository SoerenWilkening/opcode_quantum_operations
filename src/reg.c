/* src/reg.c — M07. The handle table, tombstones (D5), the sole deallocator,
 * and the physical copy. See reg.h.
 *
 * THE I2 SWEEP AND THE D7 OPERAND CHECKS ARE NOT HERE. They moved to
 * src/reg_check.c at Step 23 on plan §3's recorded seam, `table ↔ invariant
 * checking`, when PRD §15 D15's free-time disposition took this file past the
 * 240 trigger. reg.h still declares all of it: the split is a translation-unit
 * boundary, not an API one. */

#include "reg.h"

#include "ctx.h"
#include "emit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(CQOPS_DEBUG_INVARIANTS) && CQOPS_DEBUG_INVARIANTS
#  define CQ_REG_DEBUG 1
#else
#  define CQ_REG_DEBUG 0
#endif

/* Hard errors live in BOTH configurations, except where reg.h says otherwise.
 * CQOPS_DEBUG_INVARIANTS gates checking machinery and never behaviour, and
 * every condition below is a miscompile signature rather than a style
 * question — releasing a rail this file could not prove clean would put a
 * non-|0⟩ qubit on the free list, which is why the disposition below strands
 * instead (PRD §15 D15 §3), and a use-after-free hands a kernel a stale width
 * and a stale bits pointer. */
static void cq_reg_die(const char *what, long a, long b)
{
    fprintf(stderr, "libcqops: FATAL: reg: %s (%ld, %ld)\n", what, a, b);
    abort();
}

/* --- The table itself. --------------------------------------------------- */

static void cq_reg_grow(cq_reg_table *t, int32_t need)
{
    if (need <= t->cap) return;

    int32_t cap = t->cap ? t->cap : 64;
    while (cap < need) {
        if (cap > INT32_MAX / 2) { cap = need; break; }
        cap *= 2;
    }

    cq_reg *slot = realloc(t->slot, (size_t)cap * sizeof *slot);
    if (!slot) cq_reg_die("out of memory growing the handle table", cap, t->cap);
    t->slot = slot;

    /* ONLY THE NEW TAIL, and poisoned rather than zeroed. Tail-only is what
     * keeps every tombstone intact across a realloc, which D5 requires — a
     * whole-array re-initialise would turn a use-after-free into a successful
     * read of a freed rail. The poison is M03's finding generalised: mutation
     * testing at Step 4 showed that dropping a mint-path initialiser passed an
     * entire suite purely because a fresh malloc is usually already zero.
     * 0xAA is outside {LIVE, DEAD, MEASURED} by construction, which is why
     * none of them is numbered 0. */
#if CQ_REG_DEBUG
    memset(t->slot + t->cap, 0xAA, (size_t)(cap - t->cap) * sizeof *t->slot);
#endif

    t->cap = cap;
}

/* Every read of a slot goes through here, so a poisoned (never-minted) entry
 * cannot be silently interpreted as a register. Validates against `n`, not
 * `cap`: the tail beyond `n` is legitimately poison. */
static const cq_reg *cq_reg_slot(const cq_reg_table *t, int32_t h)
{
    if (h < 0 || h >= t->n) cq_reg_die("handle out of range", h, t->n);
    const cq_reg *r = &t->slot[h];
    if (r->state != CQ_SLOT_LIVE && r->state != CQ_SLOT_DEAD &&
        r->state != CQ_SLOT_MEASURED && r->state != CQ_SLOT_TOKEN)
        cq_reg_die("slot state is not LIVE/DEAD/MEASURED/TOKEN — poison?", h, r->state);
    return r;
}

/* THE ONE TOKEN REFUSAL, AT THE TWO FUNNELS (PRD §15 D23, plan §0.5). Every
 * read goes through cq_reg_readable and every write, free and measure through
 * cq_reg_slot_mut, so a token handed to ANY rail entry point in the shim lands
 * here with no per-entry-point code — and no per-entry-point copy to mask it.
 * cq_reg_width, cq_reg_state and cq_reg_is_live deliberately stay permissive:
 * that is what lets the I2 sweep and the D21 snapshot skip a token as they
 * skip a tombstone, and what lets the shim ask "is this a token?" at all. */
static void cq_reg_refuse_token(const cq_reg *r, int32_t h)
{
    if (r->state == CQ_SLOT_TOKEN)
        cq_reg_die("a classical token (a tape or qram handle) is not a rail", h, 0);
}

/* The one const cast in the module. The table is never itself const; the
 * accessors above are const-qualified so a reader cannot mutate by accident. */
static cq_reg *cq_reg_slot_mut(cq_reg_table *t, int32_t h)
{
    const cq_reg *r = cq_reg_slot(t, h);
    cq_reg_refuse_token(r, h);
    return (cq_reg *)r;
}

static const cq_reg *cq_reg_readable(const cq_reg_table *t, int32_t h)
{
    const cq_reg *r = cq_reg_slot(t, h);
    cq_reg_refuse_token(r, h);
    if (r->state == CQ_SLOT_DEAD)
        cq_reg_die("use of a freed handle (tombstone)", h, 0);
    return r;
}

void cq_reg_table_init(cq_reg_table *t)
{
    t->slot = NULL;
    t->n    = 0;
    t->cap  = 0;
}

void cq_reg_table_dispose(cq_reg_table *t)
{
    /* Frees memory and returns NOTHING to the pool — see reg.h. free(NULL) on
     * a tombstone's already-released bits is well defined. */
    for (int32_t h = 0; h < t->n; h++) free(t->slot[h].bits);
    free(t->slot);
    cq_reg_table_init(t);
}

int32_t cq_reg_count(const cq_reg_table *t) { return t->n; }

/* --- Minting. No pool in reach, so I4 holds by construction. ------------- */

static int32_t cq_reg_mint(cq_reg_table *t, uint32_t width)
{
    if (width == 0u || width > CQ_REG_WIDTH_MAX)
        cq_reg_die("register width out of range", (long)width, CQ_REG_WIDTH_MAX);
    if (t->n == INT32_MAX)
        cq_reg_die("handle counter exhausted", t->n, 0);

    cq_reg_grow(t, t->n + 1);

    cq_reg *r = &t->slot[t->n];
    r->bits = malloc((size_t)width * sizeof *r->bits);
    if (!r->bits) cq_reg_die("out of memory allocating a register", t->n, (long)width);
    r->width = width;
    r->state = CQ_SLOT_LIVE;
    return t->n++;
}

void cq_bits_from_words(cq_bit *bits, uint32_t width, uint64_t lo, uint64_t hi)
{
    for (uint32_t i = 0; i < width; i++) {
        uint64_t w = (i < 64u) ? lo : hi;
        bits[i] = cq_bit_const((int)((w >> (i & 63u)) & 1u));
    }
}

int32_t cq_reg_alloc_const(cq_reg_table *t, uint32_t width,
                           uint64_t lo, uint64_t hi)
{
    int32_t h = cq_reg_mint(t, width);
    cq_bits_from_words(t->slot[h].bits, width, lo, hi);
    return h;
}

int32_t cq_reg_alloc_zero(cq_reg_table *t, uint32_t width)
{
    return cq_reg_alloc_const(t, width, 0u, 0u);
}

/* A token is NOT a width-0 register, and cq_reg_mint refusing width 0 is why
 * this is its own path rather than a special case there: a zero-width RAIL
 * would free CLEAN and measure to 0, where a token must be a hard error at
 * both. The counter clause is shared on purpose — see reg.h's enum. */
int32_t cq_reg_alloc_token(cq_reg_table *t)
{
    if (t->n == INT32_MAX)
        cq_reg_die("handle counter exhausted", t->n, 0);
    cq_reg_grow(t, t->n + 1);

    cq_reg *r = &t->slot[t->n];
    r->bits  = NULL;
    r->width = 0u;
    r->state = CQ_SLOT_TOKEN;
    return t->n++;
}

int cq_reg_is_token(const cq_reg_table *t, int32_t h)
{
    return h >= 0 && h < t->n && t->slot[h].state == CQ_SLOT_TOKEN;
}

/* --- Accessors. --------------------------------------------------------- */

cq_bit *cq_reg_bits(cq_reg_table *t, int32_t h)
{
    cq_reg *r = cq_reg_slot_mut(t, h);
    if (r->state != CQ_SLOT_LIVE)
        cq_reg_die("write access to a rail that is not live", h, r->state);
    return r->bits;
}

const cq_bit *cq_reg_cbits(const cq_reg_table *t, int32_t h)
{
    return cq_reg_readable(t, h)->bits;
}

/* Width survives the tombstone, so a use-after-free diagnostic can name it. */
uint32_t cq_reg_width(const cq_reg_table *t, int32_t h)
{
    return cq_reg_slot(t, h)->width;
}

int cq_reg_state(const cq_reg_table *t, int32_t h)
{
    return (int)cq_reg_slot(t, h)->state;
}

int cq_reg_is_live(const cq_reg_table *t, int32_t h)
{
    return h >= 0 && h < t->n && t->slot[h].state == CQ_SLOT_LIVE;
}

uint32_t cq_reg_owned_qubits(const cq_reg_table *t, int32_t h)
{
    const cq_reg *r = cq_reg_readable(t, h);
    uint32_t n = 0;
    for (uint32_t i = 0; i < r->width; i++)
        if (cq_bit_is_qubit(r->bits[i])) n++;
    return n;
}

/* --- The free path. Rule 6, PRD §10, PRD §15 D15 (mechanism: bd 06t). ---- */

/* CQOPS_FREE_ABORT. Resolved on EVERY call, exactly as cq_sink_active is, so a
 * setter or an environment change takes effect without a rebuild — which is
 * PRD §15 D15 §3's explicit requirement for this flag. A negative override
 * means "no override"; 0 and 1 force. */
static int free_abort_override = -1;

void cqops_set_free_abort(int on) { free_abort_override = on; }

int cq_free_abort_active(void)
{
    if (free_abort_override >= 0) return free_abort_override != 0;

    const char *want = getenv("CQOPS_FREE_ABORT");
    if (!want || want[0] == '\0') return 0;      /* unset or empty means absent */

    if (strcmp(want, "0") == 0) return 0;
    if (strcmp(want, "1") == 0) return 1;

    /* A HARD ERROR, NEVER A QUIET "OFF", and this is the clause that is easy to
     * drop. cq_sink_active refuses an unregistered CQOPS_SINK rather than
     * substituting the default, for the reason that quietly handing a caller a
     * different behaviour than the one they asked for is worse than stopping.
     * Here the asymmetry is sharper still: CQOPS_FREE_ABORT=true silently
     * meaning OFF gives a maintainer who asked for termination exactly the
     * silence they were trying to break. */
    cq_reg_die("CQOPS_FREE_ABORT must be \"0\" or \"1\"", 0, 0);
    return 0;                                    /* unreachable; cq_reg_die aborts */
}

/* THE THREE-VALUED VERDICT. This is where the collapse used to live: until
 * Step 23 the only whole-rail predicate returned 0 for dirty and unproven
 * alike, so cq_reg_free could not have branched three ways however it was
 * written — the information was already gone one call down. See reg.h.
 *
 * NO EARLY RETURN ON THE UNPROVEN ROW. Dirty is absorbing and unproven is not,
 * so a fold that stopped at the first non-clean bit would answer UNPROVEN for a
 * rail carrying a conviction further along — which is precisely the residue
 * split D15 §3 says must be producible. Stopping at the first DIRTY bit is
 * sound (nothing can outrank it) and is what the loop does. */
int cq_reg_disposition(const cq_ctx *ctx, int32_t h, cq_zero_proof proof)
{
    const cq_reg *r = cq_reg_readable(&ctx->regs, h);
    int verdict = CQ_PROOF_CLEAN;

    for (uint32_t i = 0; i < r->width; i++) {
        /* A constant owns no qubit index, so it can neither reach the free
         * list nor collapse — see reg.h on why the scope is deliberate. This
         * is also what keeps an all-constant rail CLEAN under a NULL proof
         * (I4), which 40 of the corpus's 45 in-scope frees depend on. */
        if (!cq_bit_is_qubit(r->bits[i])) continue;

        /* A NULL proof is a missing argument, not evidence. The predicate
         * answers UNPROVEN so that cq_reg_clean keeps its pre-Step-23 meaning
         * at its seven external call sites; cq_reg_free is where the missing
         * argument becomes a hard error. */
        int p = proof ? proof(ctx, h, cq_bit_qindex(r->bits[i])) : CQ_PROOF_UNPROVEN;

        if (p < 0) return CQ_PROOF_DIRTY;        /* absorbing */
        if (p == 0) verdict = CQ_PROOF_UNPROVEN; /* and keep looking for a <0 */
    }
    return verdict;
}

/* A THIN WRAPPER, AND `> 0` IS THE ONLY CORRECT SPELLING. A conviction is a
 * NEGATIVE int, so `!= 0` — or returning the disposition raw — hands every one
 * of the seven external call sites a non-zero "clean" for the dirtiest rail the
 * library can recognise, and two of those sites read it as a number. */
int cq_reg_clean(const cq_ctx *ctx, int32_t h, cq_zero_proof proof)
{
    return cq_reg_disposition(ctx, h, proof) > 0;
}

uint32_t cq_reg_strand_reports(const cq_ctx *ctx) { return ctx->strand_reports; }

/* D15 §3's residue split. See src/reg.h for why there are two GRAINS and why
 * the rail rows deliberately do not sum to the number of frees. */
uint32_t cq_reg_stranded_dirty   (const cq_ctx *c) { return c->stranded_dirty; }
uint32_t cq_reg_stranded_unproven(const cq_ctx *c) { return c->stranded_unproven; }
uint32_t cq_reg_frees_dirty      (const cq_ctx *c) { return c->frees_dirty; }
uint32_t cq_reg_frees_unproven   (const cq_ctx *c) { return c->frees_unproven; }

/* D15 §3's report. The FIRST occurrence names the handle; the rest are silent,
 * because the residue spans a great many frees and a line per stranded qubit
 * buries the one line that matters under its own noise. Stranding is loud, not
 * silent — but loud once. */
static void report_first_strand(cq_ctx *ctx, int32_t h, uint32_t q, int verdict)
{
    /* THE COUNTER IS INCREMENTED ONLY WHERE THE LINE IS ACTUALLY PRINTED, which
     * is what makes the one-shot falsifiable: delete the early return and the
     * count runs away with the number of stranded qubits. Incrementing it above
     * the guard would restore the untestable flag in a new spelling. */
    if (ctx->strand_reports > 0u) return;
    ctx->strand_reports++;
    fprintf(stderr,
            "libcqops: STRANDED: qubit q%u of handle h%d is %s at its free — "
            "never released, never on the free list, counted "
            "(PRD §15 D15 §3; set CQOPS_FREE_ABORT=1 to stop here instead). "
            "Further strands are not reported.\n",
            q, (int)h, verdict < 0 ? "provably NOT |0>" : "not provably |0>");
}

void cq_reg_free(cq_ctx *ctx, int32_t h, cq_zero_proof proof)
{
    cq_reg *r = cq_reg_slot_mut(&ctx->regs, h);
    if (r->state == CQ_SLOT_DEAD)
        cq_reg_die("double free of a handle", h, 0);
    if (r->state == CQ_SLOT_MEASURED)
        cq_reg_die("free of a measured rail — measurement is terminal", h, 0);

    /* A MISSING ARGUMENT, NOT AN EPISTEMIC STATE, and the distinction is worth
     * the extra branch. "The caller supplied no oracle" and "the oracle cannot
     * tell" are different facts; only the second is D15's unproven row.
     * Aborting here is strictly more conservative than D15 requires — aborting
     * is not recycling — and it keeps an all-constant free working with no
     * evidence at all, which I4 guarantees is safe and which the corpus's
     * classical loop counters depend on. */
    if (!proof && cq_reg_owned_qubits(&ctx->regs, h) > 0u)
        cq_reg_die("free of a qubit-owning rail with no zero-proof supplied",
                   h, (long)cq_reg_owned_qubits(&ctx->regs, h));

    /* 1. The WHOLE RAIL's verdict, before anything is released. Under
     *    stranding a partly-released rail is the correct outcome rather than a
     *    hazard, so this pass is no longer about atomicity against a mid-loop
     *    abort — it is about producing the rail-level verdict, and about being
     *    the one place CQOPS_FREE_ABORT can stop without half-returning. */
    int verdict = cq_reg_disposition(ctx, h, proof);

    /* THE RAIL ROW IS TALLIED BEFORE THE ABORT, NOT AFTER, and that ordering is
     * the whole reason it is here rather than folded into the loop below. Under
     * CQOPS_FREE_ABORT the next statement does not return, so a tally placed
     * after it would be permanently zero in exactly the configuration a
     * maintainer reaches for when they want to know what is happening. */
    if (verdict < 0)       ctx->frees_dirty++;
    else if (verdict == 0) ctx->frees_unproven++;

    if (verdict <= 0 && cq_free_abort_active())
        cq_reg_die(verdict < 0
                       ? "CQOPS_FREE_ABORT: free of a rail PROVEN not to be |0>"
                       : "CQOPS_FREE_ABORT: free of a rail not provably |0>",
                   h, (long)cq_reg_owned_qubits(&ctx->regs, h));

    /* 2. PER QUBIT (D15 §3 says per qubit, not per rail): CLEAN releases,
     *    everything else strands. A per-rail act passes every uniform fixture
     *    and leaks the provably clean qubits of every mixed rail, which nothing
     *    would shout about — the pool would just grow.
     *
     *    `p` IS THE PROOF'S OWN ANSWER AND IT IS POSITIVE ON THIS BRANCH, never
     *    a literal 1: that would be a laundering site invisible to a grep for
     *    cq_reg_free. Note what the three-valued contract does to M03's guard
     *    beneath us — a CONVICTION is a negative int, which `!proven_zero`
     *    reads as TRUE, i.e. as proof. That is why the branch is `p > 0` and
     *    why cq_qubits_release now refuses `proven_zero <= 0` rather than
     *    `!proven_zero`; either alone would let a convicted qubit onto the free
     *    list, which is the one unforgivable bug.
     *
     *    THROUGH cq_ctx_release_qubit, NOT cq_qubits_release DIRECTLY (Step 8,
     *    bd ckd.17a). That joint releases and then retires the shadow entry, in
     *    that order, and it is deliberately not scratch-specific. A STRANDED
     *    index is deliberately NOT retired: it is still live, nobody got it
     *    back, and cq_shadow_retire writes {0,0} — publishing "provably |0⟩"
     *    over the very entry that said otherwise. */
    for (uint32_t i = 0; i < r->width; i++) {
        if (!cq_bit_is_qubit(r->bits[i])) continue;
        uint32_t q = cq_bit_qindex(r->bits[i]);
        int p = proof(ctx, h, q);

        if (p > 0) {
            cq_ctx_release_qubit(ctx, q, p);
        } else {
            /* D15 §3's residue split, at the ONE branch that knows both the
             * verdict and the act. `p` is the proof's own answer for THIS
             * qubit — not the rail's `verdict`, which is absorbing and would
             * report every unproven qubit of a mixed rail as convicted. */
            if (p < 0) ctx->stranded_dirty++;
            else       ctx->stranded_unproven++;
            report_first_strand(ctx, h, q, p);
            cq_qubits_strand(&ctx->pool, q);
        }
    }

    /* 3. ONLY NOW. Writing a constant over a Q bit before its release erases
     *    the index irrecoverably — a constant carries a canonical q == 0 — and
     *    the qubit leaks in silence, with no assert and a growing pool. */
    free(r->bits);
    r->bits  = NULL;
    r->state = CQ_SLOT_DEAD;
}

void cq_reg_mark_measured(cq_reg_table *t, int32_t h)
{
    cq_reg *r = cq_reg_slot_mut(t, h);
    if (r->state != CQ_SLOT_LIVE)
        cq_reg_die("measure of a rail that is not live", h, r->state);
    r->state = CQ_SLOT_MEASURED;
}

/* --- The physical copy (Rule 5), and the rail-to-rail exchange. ---------- */

void cq_reg_xor_into(cq_ctx *ctx, int32_t dst, int32_t src)
{
    if (dst == src) cq_reg_die("copy into itself", dst, src);

    uint32_t w = cq_reg_width(&ctx->regs, dst);
    if (w != cq_reg_width(&ctx->regs, src))
        cq_reg_die("width mismatch in a copy", dst, src);

    cq_bit       *d = cq_reg_bits (&ctx->regs, dst);
    const cq_bit *s = cq_reg_cbits(&ctx->regs, src);
    for (uint32_t i = 0; i < w; i++) cq_emit_cx(ctx, &s[i], &d[i]);
}

/* cqrt_cswap's CONSTANT-control row: 0 gates, 0 qubits. See reg.h for the
 * three wrong routes that give the right value and the wrong cost.
 *
 * BY CONTENTS, NOT BY POINTER. Swapping the two `bits` pointers is O(1) and
 * computes the same thing, and it would quietly break reg.h's promise that a
 * register's bits array is stable for its life — a promise Rule 7's kernel
 * contract rests on, since a kernel is handed a cq_bit * and holds it. W is at
 * most CQ_REG_WIDTH_MAX, so the loop is bounded by 128. */
void cq_reg_swap_bits(cq_reg_table *t, int32_t a, int32_t b)
{
    if (a == b) cq_reg_die("swap of a rail with itself", a, b);

    uint32_t w = cq_reg_width(t, a);
    if (w != cq_reg_width(t, b))
        cq_reg_die("width mismatch in a rail-to-rail swap", a, b);

    cq_bit *pa = cq_reg_bits(t, a);
    cq_bit *pb = cq_reg_bits(t, b);
    for (uint32_t i = 0; i < w; i++) {
        cq_bit tmp = pa[i];
        pa[i] = pb[i];
        pb[i] = tmp;
    }
}
