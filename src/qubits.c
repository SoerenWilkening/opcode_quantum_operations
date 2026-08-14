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

    /* Poison the new tail in Debug so that "forgot to initialise is_free for a
     * freshly minted index" is caught deterministically instead of depending
     * on what the allocator happened to hand back. Found by mutation testing
     * at Step 4: dropping the mint-path initialiser passed every test, purely
     * because a fresh malloc is usually already zero. 0xAA is neither 0 nor
     * 1, so cq_pool_free_flag rejects it. */
#if defined(CQOPS_DEBUG_INVARIANTS) && CQOPS_DEBUG_INVARIANTS
    memset(p->is_free + p->cap, 0xAA, (size_t)(cap - p->cap));
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

void cq_qubits_init(cq_qubit_pool *p)
{
    p->stack = NULL;
    p->is_free = NULL;
    p->cap = 0;
    p->n_free = 0;
    p->minted = 0;
    p->live = 0;
    p->peak = 0;
    p->ceiling = 0;
}

void cq_qubits_dispose(cq_qubit_pool *p)
{
    free(p->stack);
    free(p->is_free);
    cq_qubits_init(p);
}

void cq_qubits_set_ceiling(cq_qubit_pool *p, uint32_t ceiling)
{
    if (ceiling != 0u && ceiling < p->live)
        cq_pool_die("ceiling below the live count", ceiling, p->live);
    p->ceiling = ceiling;
}

uint32_t cq_qubits_ceiling(const cq_qubit_pool *p) { return p->ceiling; }
uint32_t cq_qubits_live   (const cq_qubit_pool *p) { return p->live;    }
uint32_t cq_qubits_peak   (const cq_qubit_pool *p) { return p->peak;    }
uint32_t cq_qubits_minted (const cq_qubit_pool *p) { return p->minted;  }
uint32_t cq_qubits_free   (const cq_qubit_pool *p) { return p->n_free;  }

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

    /* I3, and the one that matters. See qubits.h on why the evidence arrives
     * as an argument instead of being read out of a shadow here. */
    if (!proven_zero)
        cq_pool_die("release of a qubit not proven |0>", q, p->live);

    p->is_free[q] = 1;
    p->stack[p->n_free++] = q;
    p->live--;

    CQ_POOL_ASSERT(p->peak == p->minted);
    CQ_POOL_ASSERT(p->minted == p->live + p->n_free);
}
