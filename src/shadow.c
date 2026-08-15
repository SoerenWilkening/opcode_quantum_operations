/* src/shadow.c — M02. The four PRD §3 rules, and a table that grows. */

#include "shadow.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(CQOPS_DEBUG_INVARIANTS) && CQOPS_DEBUG_INVARIANTS
#  define CQ_SHADOW_ASSERT(cond) assert(cond)
#else
#  define CQ_SHADOW_ASSERT(cond) ((void)0)
#endif

/* Hard errors, live in BOTH configurations. CQOPS_DEBUG_INVARIANTS gates
 * checking machinery and never behaviour, so anything that must abort has to
 * abort identically in Release — and an out-of-range qubit index is a memory
 * error, not a style question. Rule 6's posture generalises: when we cannot
 * prove the thing, fail loud. */
static void cq_shadow_die(const char *what, uint32_t a, uint32_t b)
{
    fprintf(stderr, "libcqops: FATAL: shadow: %s (%u, %u)\n", what, a, b);
    abort();
}

static void cq_shadow_bounds(const cq_shadow_table *sh, uint32_t q)
{
    if (q >= sh->n) cq_shadow_die("qubit index out of range", q, sh->n);
}

void cq_shadow_init(cq_shadow_table *sh)
{
    sh->e = NULL;
    sh->n = 0;
    sh->cap = 0;
}

void cq_shadow_dispose(cq_shadow_table *sh)
{
    free(sh->e);
    cq_shadow_init(sh);
}

void cq_shadow_ensure(cq_shadow_table *sh, uint32_t n)
{
    if (n <= sh->n) return;            /* monotone: never shrinks */

    if (n > sh->cap) {
        uint32_t cap = sh->cap ? sh->cap : 64u;
        while (cap < n) {
            if (cap > UINT32_MAX / 2u) { cap = n; break; }
            cap *= 2u;
        }

        cq_shadow *e = realloc(sh->e, (size_t)cap * sizeof *e);
        if (!e) cq_shadow_die("out of memory growing the table", cap, sh->cap);

        sh->e = e;
        sh->cap = cap;
    }

    /* Only the new tail is initialised. Re-initialising the whole array would
     * silently un-poison every live qubit — the shadow would start claiming
     * determinate exactly where it had lost the truth, and nothing downstream
     * could tell. Entries are born known-0 because I3 guarantees a qubit off
     * the free list is |0⟩. */
    for (uint32_t q = sh->n; q < n; q++) {
        sh->e[q].value = 0;
        sh->e[q].unknown = 0;
    }
    sh->n = n;
}

uint32_t cq_shadow_count(const cq_shadow_table *sh)
{
    return sh->n;
}

cq_shadow cq_shadow_get(const cq_shadow_table *sh, uint32_t q)
{
    cq_shadow_bounds(sh, q);
    return sh->e[q];
}

int cq_shadow_known_zero(const cq_shadow_table *sh, uint32_t q)
{
    cq_shadow_bounds(sh, q);
    return !sh->e[q].unknown && sh->e[q].value == 0;   /* unknown first */
}

/* --- The four §3 rules. None of them can clear `unknown`. --------------- */

void cq_shadow_x(cq_shadow_table *sh, uint32_t t)
{
    cq_shadow_bounds(sh, t);
    if (!sh->e[t].unknown) sh->e[t].value ^= 1u;
}

void cq_shadow_cx(cq_shadow_table *sh, uint32_t c, uint32_t t)
{
    cq_shadow_bounds(sh, c);
    cq_shadow_bounds(sh, t);
    CQ_SHADOW_ASSERT(c != t);          /* §3 distinctness; M05 is the owner */

    sh->e[t].unknown |= sh->e[c].unknown;
    if (!sh->e[t].unknown) sh->e[t].value ^= sh->e[c].value;
}

void cq_shadow_ccx(cq_shadow_table *sh, uint32_t a, uint32_t b, uint32_t t)
{
    cq_shadow_bounds(sh, a);
    cq_shadow_bounds(sh, b);
    cq_shadow_bounds(sh, t);
    CQ_SHADOW_ASSERT(a != b && a != t && b != t);

    sh->e[t].unknown |= (uint8_t)(sh->e[a].unknown | sh->e[b].unknown);
    if (!sh->e[t].unknown)
        sh->e[t].value ^= (uint8_t)(sh->e[a].value & sh->e[b].value);
}

void cq_shadow_rotate(cq_shadow_table *sh, uint32_t q)
{
    cq_shadow_bounds(sh, q);
    sh->e[q].unknown = 1;
}

/* --- Retirement (bd ckd.17a). The one write that clears `unknown`. ------- */

void cq_shadow_retire(cq_shadow_table *sh, uint32_t q)
{
    cq_shadow_bounds(sh, q);

    /* BOTH configurations. This is the Release backstop for the whole sandwich
     * machinery: with the I6 sweeps compiled out, an entry that is determinate
     * and non-zero at retirement time is a qubit demonstrably not in |0⟩ whose
     * caller just certified it clean. */
    if (!sh->e[q].unknown && sh->e[q].value != 0u)
        cq_shadow_die("retire of a determinate NON-ZERO entry — "
                      "the compute half did not cancel", q, sh->e[q].value);

    sh->e[q].value = 0;
    sh->e[q].unknown = 0;
}
