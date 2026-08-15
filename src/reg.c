/* src/reg.c — M07. The handle table, tombstones (D5), the sole deallocator,
 * the I2 owner-map sweep and the D7a/D7b operand checks. See reg.h. */

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
 * question — a free of a dirty rail puts a non-|0⟩ qubit on the free list, and
 * a use-after-free hands a kernel a stale width and a stale bits pointer. */
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
        r->state != CQ_SLOT_MEASURED)
        cq_reg_die("slot state is not LIVE/DEAD/MEASURED — poison?", h, r->state);
    return r;
}

/* The one const cast in the module. The table is never itself const; the
 * accessors above are const-qualified so a reader cannot mutate by accident. */
static cq_reg *cq_reg_slot_mut(cq_reg_table *t, int32_t h)
{
    return (cq_reg *)cq_reg_slot(t, h);
}

static const cq_reg *cq_reg_readable(const cq_reg_table *t, int32_t h)
{
    const cq_reg *r = cq_reg_slot(t, h);
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

/* --- The free path. Rule 6, PRD §10, and bd ckd.17 left open. ------------ */

int cq_reg_clean(const cq_ctx *ctx, int32_t h, cq_zero_proof proof)
{
    const cq_reg *r = cq_reg_readable(&ctx->regs, h);
    for (uint32_t i = 0; i < r->width; i++) {
        /* A constant owns no qubit index, so it can neither reach the free
         * list nor collapse — see reg.h on why the scope is deliberate. */
        if (!cq_bit_is_qubit(r->bits[i])) continue;
        if (!proof || !proof(ctx, h, cq_bit_qindex(r->bits[i]))) return 0;
    }
    return 1;
}

void cq_reg_free(cq_ctx *ctx, int32_t h, cq_zero_proof proof)
{
    cq_reg *r = cq_reg_slot_mut(&ctx->regs, h);
    if (r->state == CQ_SLOT_DEAD)
        cq_reg_die("double free of a handle", h, 0);
    if (r->state == CQ_SLOT_MEASURED)
        cq_reg_die("free of a measured rail — measurement is terminal", h, 0);

    /* 1. Verify the whole rail before releasing any of it, so a dirty free
     *    never leaves the rail half-returned to the pool. */
    if (!cq_reg_clean(ctx, h, proof))
        cq_reg_die("free of a rail that is not provably clean",
                   h, (long)cq_reg_owned_qubits(&ctx->regs, h));

    /* 2. Forward the proof's PER-QUBIT answer. Never a literal 1 here: that
     *    would be a laundering site invisible to a grep for cq_reg_free, and
     *    it would defeat the whole reason M03 takes the evidence as an
     *    argument. `proof` cannot be NULL on this path — step 1 returns 0 for
     *    any Q bit under a NULL proof, so the loop body is unreachable.
     *
     *    THROUGH cq_ctx_release_qubit, NOT cq_qubits_release DIRECTLY (Step 8,
     *    bd ckd.17a). That joint releases and then retires the shadow entry,
     *    in that order, and it is deliberately not scratch-specific — M09's
     *    sandwich epilogue uses the identical path. Retiring here is also what
     *    closes the hazard Step 7 recorded and left open: nothing un-poisoned
     *    a released index, so a REUSED index kept its stale entry, because
     *    cq_ctx_fresh_qubit only ensures up to `minted` and cq_shadow_ensure
     *    returns early for an index it has already seen. */
    for (uint32_t i = 0; i < r->width; i++) {
        if (!cq_bit_is_qubit(r->bits[i])) continue;
        uint32_t q = cq_bit_qindex(r->bits[i]);
        cq_ctx_release_qubit(ctx, q, proof(ctx, h, q));
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

/* --- The physical copy (Rule 5), and the D7 operand checks. -------------- */

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

void cq_reg_check_operands(const cq_reg_table *t, int32_t out,
                           const int32_t *srcs, uint32_t n)
{
    if (out != CQ_REG_NONE && !cq_reg_is_live(t, out))
        cq_reg_die("operand check: the result handle is not a live rail", out, 0);

    for (uint32_t i = 0; i < n; i++) {
        if (!cq_reg_is_live(t, srcs[i]))
            cq_reg_die("operand check: a source handle is not a live rail",
                       srcs[i], (long)i);
        /* D7a only. Source-source aliasing is D7b and is LEGAL — CQ_lang ships
         * ten integer-surface fixture lines that do it. See reg.h. */
        if (out != CQ_REG_NONE && srcs[i] == out)
            cq_reg_die("D7a: the result handle is also a source", out, (long)i);
    }
}

int cq_reg_sources_alias(const int32_t *srcs, uint32_t n)
{
    for (uint32_t i = 0; i + 1u < n; i++)
        for (uint32_t j = i + 1u; j < n; j++)
            if (srcs[i] == srcs[j]) return 1;
    return 0;
}

void cq_reg_audit(const cq_ctx *ctx)
{
#if CQ_REG_DEBUG
    const cq_reg_table *t = &ctx->regs;
    uint32_t minted = cq_qubits_minted(&ctx->pool);
    int32_t *owner = NULL;

    if (minted > 0u) {
        owner = malloc((size_t)minted * sizeof *owner);
        if (!owner) cq_reg_die("out of memory building the I2 owner map", (long)minted, 0);
        memset(owner, 0xFF, (size_t)minted * sizeof *owner);   /* == CQ_REG_NONE */
    }

    /* MEASURED slots are swept and DEAD ones are not: a measured rail still
     * owns its qubits (they are deliberately never reclaimed), while a
     * tombstone's bits array is gone. I2 is scoped to live registers. */
    for (int32_t h = 0; h < t->n; h++) {
        const cq_reg *r = cq_reg_slot(t, h);
        if (r->state == CQ_SLOT_DEAD) continue;

        for (uint32_t i = 0; i < r->width; i++) {
            cq_bit b = r->bits[i];
            if (!cq_bit_valid(b))
                cq_reg_die("I1: malformed bit — a constant carrying a qubit index", h, (long)i);
            if (!cq_bit_is_qubit(b)) continue;

            uint32_t q = cq_bit_qindex(b);
            if (q >= minted)
                cq_reg_die("register holds a qubit index that was never minted", h, (long)q);
            if (cq_qubits_is_free(&ctx->pool, q))
                cq_reg_die("register holds a qubit that is on the free list", h, (long)q);
            if (owner[q] == h)
                cq_reg_die("I2: one register holds the same qubit index twice", h, (long)q);
            if (owner[q] != CQ_REG_NONE)
                cq_reg_die("I2: qubit index held by two live registers", owner[q], (long)q);
            owner[q] = h;
        }
    }

    free(owner);
#else
    (void)ctx;
#endif
}
