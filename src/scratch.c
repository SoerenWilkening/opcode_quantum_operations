/* src/scratch.c — M08. The cq_bit array and its two lifetime checks. */

#include "scratch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(CQOPS_DEBUG_INVARIANTS) && CQOPS_DEBUG_INVARIANTS
#  define CQ_SCRATCH_DEBUG 1
#else
#  define CQ_SCRATCH_DEBUG 0
#endif

/* Hard errors in BOTH configurations. A region disposed with a live qubit in
 * it is a leaked ancilla, which is the one unforgivable bug (NORTH_STAR §3);
 * Rule 17 pins L4 under Release, where a Debug-gated assert is simply absent. */
static void cq_scratch_die(const char *what, unsigned long a, unsigned long b)
{
    fprintf(stderr, "libcqops: FATAL: scratch: %s (%lu, %lu)\n", what, a, b);
    abort();
}

void cq_scratch_alloc(cq_scratch *s, uint32_t n)
{
    if (n == 0u) cq_scratch_die("region width is zero", 0ul, 0ul);

    s->bits = malloc((size_t)n * sizeof *s->bits);
    if (!s->bits) cq_scratch_die("out of memory allocating a region", n, 0ul);
    s->n = n;

    /* POISON FIRST, THEN INITIALISE. M03's Step 4 mutation finding, and it
     * bites harder here than anywhere: an all-zero cq_bit is a VALID
     * CQ_BIT_ZERO, so deleting the loop below would leave a fresh malloc
     * looking exactly like a correctly initialised region and the whole suite
     * would pass on allocator luck. 0xAA is not a valid kind, so the driver's
     * entry check catches the mutation instead. */
#if CQ_SCRATCH_DEBUG
    memset(s->bits, 0xAA, (size_t)n * sizeof *s->bits);
#endif

    for (uint32_t i = 0; i < n; i++) s->bits[i] = cq_bit_zero();
}

void cq_scratch_dispose(cq_scratch *s)
{
    /* See scratch.h: a KIND check, never a shadow read. A CQ_BIT_Q bit here is
     * a qubit no one will ever return — M09's epilogue is the only thing that
     * releases one, and it did not run. A CQ_BIT_ONE bit owns nothing, but it
     * would make I6(b)'s "scratch is born BIT_ZERO, so materialisation emits
     * no X" false the next time round. */
    for (uint32_t i = 0; i < s->n; i++) {
        if (!cq_bit_is_zero(s->bits[i]))
            cq_scratch_die("region bit is not CQ_BIT_ZERO on dispose "
                           "(index, kind)", i, s->bits[i].kind);
    }

    free(s->bits);
    s->bits = NULL;
    s->n = 0u;
}

uint32_t cq_scratch_size(const cq_scratch *s) { return s->n; }

cq_bit *cq_scratch_span(cq_scratch *s, uint32_t off, uint32_t len)
{
    /* Written as `len > n - off` rather than `off + len > n` on purpose: the
     * first clause has already established off <= n, so the subtraction cannot
     * wrap, while the addition could. */
    if (off > s->n || len > s->n - off)
        cq_scratch_die("span is outside the region (off, len)", off, len);
    return s->bits + off;
}
