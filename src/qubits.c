/* src/qubits.c — M03. Monotonic counter, LIFO free list (D4), D2 ceiling,
 * peak tracking, and the I3 clean-release check. */

#include "qubits.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(CQOPS_DEBUG_INVARIANTS) && CQOPS_DEBUG_INVARIANTS
#  define CQ_POOL_ASSERT(cond) assert(cond)
#else
#  define CQ_POOL_ASSERT(cond) ((void)0)
#endif

/* Hard errors live in BOTH configurations. CQOPS_DEBUG_INVARIANTS gates
 * checking machinery and never behaviour, and every condition below is a
 * miscompile signature rather than a style question: a dirty release puts a
 * non-|0⟩ qubit on the free list, and a double release puts one index there
 * twice, which hands the same qubit to two live registers (I2). */
static void cq_pool_die(const char *what, uint32_t a, uint32_t b)
{
    fprintf(stderr, "libcqops: FATAL: qubit pool: %s (%u, %u)\n", what, a, b);
    abort();
}

static void cq_pool_grow(cq_qubit_pool *p, uint32_t need)
{
    if (need <= p->cap) return;

    uint32_t cap = p->cap ? p->cap : 64u;
    while (cap < need) {
        if (cap > UINT32_MAX / 2u) { cap = need; break; }
        cap *= 2u;
    }

    uint32_t *stack   = realloc(p->stack,   (size_t)cap * sizeof *stack);
    if (!stack) cq_pool_die("out of memory growing the free list", cap, p->cap);
    p->stack = stack;

    uint8_t  *is_free = realloc(p->is_free, (size_t)cap * sizeof *is_free);
    if (!is_free) cq_pool_die("out of memory growing the state map", cap, p->cap);
    p->is_free = is_free;

    uint8_t  *is_str  = realloc(p->is_stranded, (size_t)cap * sizeof *is_str);
    if (!is_str) cq_pool_die("out of memory growing the strand map", cap, p->cap);
    p->is_stranded = is_str;

    uint8_t  *is_ret  = realloc(p->is_retired, (size_t)cap * sizeof *is_ret);
    if (!is_ret) cq_pool_die("out of memory growing the retire map", cap, p->cap);
    p->is_retired = is_ret;

    /* Poison the new tail in Debug so that "forgot to initialise is_free for a
     * freshly minted index" is caught deterministically instead of depending
     * on what the allocator happened to hand back. Found by mutation testing
     * at Step 4: dropping the mint-path initialiser passed every test, purely
     * because a fresh malloc is usually already zero. 0xAA is neither 0 nor
     * 1, so cq_pool_free_flag rejects it. */
#if defined(CQOPS_DEBUG_INVARIANTS) && CQOPS_DEBUG_INVARIANTS
    memset(p->is_free     + p->cap, 0xAA, (size_t)(cap - p->cap));
    memset(p->is_stranded + p->cap, 0xAA, (size_t)(cap - p->cap));
    memset(p->is_retired  + p->cap, 0xAA, (size_t)(cap - p->cap));
#endif

    p->cap = cap;
}

/* Every read of is_free goes through here, so a poisoned (never-initialised)
 * entry cannot be silently interpreted as a truth value. */
static int cq_pool_free_flag(const cq_qubit_pool *p, uint32_t q)
{
    CQ_POOL_ASSERT(q < p->minted);
    CQ_POOL_ASSERT(p->is_free[q] == 0 || p->is_free[q] == 1);
    return p->is_free[q];
}

/* The strand map's own accessor, deliberately a MIRROR of cq_pool_free_flag
 * rather than a shared one. Both maps are poisoned 0xAA on growth and both are
 * initialised on the mint path, so the same "forgot to initialise" bug is
 * caught in the same way for each — and keeping them separate is what stops a
 * stranded index ever reading as free (see qubits.h on the encoding). */
static int cq_pool_strand_flag(const cq_qubit_pool *p, uint32_t q)
{
    CQ_POOL_ASSERT(q < p->minted);
    CQ_POOL_ASSERT(p->is_stranded[q] == 0 || p->is_stranded[q] == 1);
    return p->is_stranded[q];
}

/* And the third state's reader, on the same terms (PRD §15 D21 (b)). */
static int cq_pool_retire_flag(const cq_qubit_pool *p, uint32_t q)
{
    CQ_POOL_ASSERT(q < p->minted);
    CQ_POOL_ASSERT(p->is_retired[q] == 0 || p->is_retired[q] == 1);
    return p->is_retired[q];
}

void cq_qubits_init(cq_qubit_pool *p)
{
    p->stack = NULL;
    p->is_free = NULL;
    p->is_stranded = NULL;
    p->is_retired = NULL;
    p->cap = 0;
    p->n_free = 0;
    p->minted = 0;
    p->live = 0;
    p->peak = 0;
    p->n_stranded = 0;
    p->n_retired = 0;
    p->ceiling = 0;
    p->recycle = 1;            /* D4 unless the qec sink turns it off */
}

void cq_qubits_dispose(cq_qubit_pool *p)
{
    free(p->stack);
    free(p->is_free);
    free(p->is_stranded);
    free(p->is_retired);
    cq_qubits_init(p);
}

void cq_qubits_set_ceiling(cq_qubit_pool *p, uint32_t ceiling)
{
    if (ceiling != 0u && ceiling < p->live)
        cq_pool_die("ceiling below the live count", ceiling, p->live);
    p->ceiling = ceiling;
}

/* PRD §15 D21 (b). See qubits.h for why this exists and what it buys that the
 * D2 ceiling cannot buy on its own. */
void cq_qubits_set_recycle(cq_qubit_pool *p, int on)
{
    if (!on && p->n_free != 0u)
        cq_pool_die("recycling disabled with a non-empty free list",
                    p->n_free, p->live);
    p->recycle = on ? 1u : 0u;
}

int cq_qubits_recycles(const cq_qubit_pool *p) { return p->recycle; }

uint32_t cq_qubits_ceiling(const cq_qubit_pool *p) { return p->ceiling; }
uint32_t cq_qubits_live   (const cq_qubit_pool *p) { return p->live;    }
uint32_t cq_qubits_peak   (const cq_qubit_pool *p) { return p->peak;    }
uint32_t cq_qubits_minted (const cq_qubit_pool *p) { return p->minted;  }
uint32_t cq_qubits_free   (const cq_qubit_pool *p) { return p->n_free;  }
uint32_t cq_qubits_stranded(const cq_qubit_pool *p) { return p->n_stranded; }
uint32_t cq_qubits_retired (const cq_qubit_pool *p) { return p->n_retired;  }

int cq_qubits_is_stranded(const cq_qubit_pool *p, uint32_t q)
{
    return q < p->minted && cq_pool_strand_flag(p, q);
}

int cq_qubits_is_retired(const cq_qubit_pool *p, uint32_t q)
{
    return q < p->minted && cq_pool_retire_flag(p, q);
}

int cq_qubits_is_free(const cq_qubit_pool *p, uint32_t q)
{
    return q < p->minted && cq_pool_free_flag(p, q);
}

uint32_t cq_qubits_acquire(cq_qubit_pool *p)
{
    /* D2: the bound is on qubits live at once, so reuse never trips it. */
    if (p->ceiling != 0u && p->live >= p->ceiling)
        cq_pool_die("ceiling exceeded", p->live + 1u, p->ceiling);

    uint32_t q;
    if (p->n_free > 0u) {
        q = p->stack[--p->n_free];      /* D4: LIFO, the last one released */
        p->is_free[q] = 0;
    } else {
        cq_pool_grow(p, p->minted + 1u);
        q = p->minted++;
        p->is_free[q] = 0;
        /* Only the FRESH arm initialises the strand map: a reused index came
         * off the free list, and a stranded index never reaches it, so the
         * reuse arm's entry is already 0 and re-zeroing it would hide a bug
         * rather than prevent one. In Debug the 0xAA poison makes the missing
         * initialiser fail here deterministically. */
        p->is_stranded[q] = 0;
        p->is_retired[q]  = 0;
    }

    p->live++;
    if (p->live > p->peak) p->peak = p->live;

    /* Minting happens only with an empty free list, i.e. only when live had
     * already reached minted — so the counter is the high-water mark. */
    CQ_POOL_ASSERT(p->peak == p->minted);
    CQ_POOL_ASSERT(p->minted == p->live + p->n_free);
    return q;
}

void cq_qubits_release(cq_qubit_pool *p, uint32_t q, int proven_zero)
{
    if (q >= p->minted)
        cq_pool_die("release of an index that was never minted", q, p->minted);
    if (cq_pool_free_flag(p, q))
        cq_pool_die("double release", q, p->n_free);

    /* BEFORE the proven_zero test, deliberately. A stranded index is exactly
     * the one nobody could prove |0⟩, so a caller reaching here with a 1 is
     * asserting something the strand already recorded as unavailable — and
     * putting it on the free list is the laundering D15 §3 exists to forbid.
     * Ordering the check after proven_zero would let a confident caller
     * through and report the wrong cause for the ones it caught. */
    if (cq_pool_strand_flag(p, q))
        cq_pool_die("release of a STRANDED index", q, p->n_stranded);

    /* And the retired one, for the mirror-image reason: a retired index has
     * already been disposed of, so a second release is the caller losing track
     * of it. It cannot be caught by the is_free test above — a retired index
     * never reaches the free list, which is the whole of what retirement is. */
    if (cq_pool_retire_flag(p, q))
        cq_pool_die("release of a RETIRED index", q, p->n_retired);

    /* I3, and the one that matters. See qubits.h on why the evidence arrives
     * as an argument instead of being read out of a shadow here.
     *
     * `<= 0`, NOT `!proven_zero`, AND THE DIFFERENCE ARRIVED WITH STEP 23. The
     * evidence is now three-valued BY SIGN (src/reg.h's cq_zero_proof): a
     * NEGATIVE answer is the strongest statement the library can make — "I can
     * see this is not |0>" — and as a bare int it is non-zero, which the old
     * spelling read as PROOF. That is the one unforgivable bug arriving through
     * the very guard written to prevent it. This module never sees a proof
     * function and cannot police the convention itself, so it polices the
     * value: only a POSITIVE claim releases. */
    if (proven_zero <= 0)
        cq_pool_die("release of a qubit not proven |0>", q, p->live);

    /* PRD §15 D21 (b). The proof was made and accepted — this qubit IS |0⟩ —
     * and the index is dropped anyway, because the trace contract's register
     * model cannot express one index belonging to two rails. Nothing else
     * moves, exactly as for a strand: `live` still counts it, so both
     * identities hold with no arithmetic. */
    if (!p->recycle) {
        p->is_retired[q] = 1;
        p->n_retired++;
        CQ_POOL_ASSERT(p->peak == p->minted);
        CQ_POOL_ASSERT(p->minted == p->live + p->n_free);
        return;
    }

    p->is_free[q] = 1;
    p->stack[p->n_free++] = q;
    p->live--;

    CQ_POOL_ASSERT(p->peak == p->minted);
    CQ_POOL_ASSERT(p->minted == p->live + p->n_free);
}

/* PRD §15 D15 §3. Pushes nothing, marks the index, counts it. See qubits.h for
 * why this is the alternative to passing 1 rather than an exception to it, and
 * for why the mark is a separate array. */
void cq_qubits_strand(cq_qubit_pool *p, uint32_t q)
{
    if (q >= p->minted)
        cq_pool_die("strand of an index that was never minted", q, p->minted);
    if (cq_pool_free_flag(p, q))
        cq_pool_die("strand of an index on the free list", q, p->n_free);
    if (cq_pool_strand_flag(p, q))
        cq_pool_die("double strand", q, p->n_stranded);
    if (cq_pool_retire_flag(p, q))
        cq_pool_die("strand of a RETIRED index", q, p->n_retired);

    p->is_stranded[q] = 1;
    p->n_stranded++;

    /* NOTHING ELSE MOVES, and that is the mechanism rather than an omission:
     * `live` still counts this index because nobody got it back, so both
     * identities survive untouched. A `p->live--` here is the whole bug D15 §3
     * measured a third bucket into. */
    CQ_POOL_ASSERT(p->peak == p->minted);
    CQ_POOL_ASSERT(p->minted == p->live + p->n_free);
}
