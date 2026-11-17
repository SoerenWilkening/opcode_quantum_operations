/* The handle table: handle h is slot h of a growing array; slot 0 is never used. */
#include <stdio.h>
#include <stdlib.h>
#include "cq_rt.h"

typedef struct { uint32_t width; cq_bit *bits; } rail;   /* bits == NULL: free or never made */

static rail   *slot;
/* shortcut: a freed slot keeps its 16 bytes forever; reuse slots if handle churn ever matters. */
static int32_t n = 1, cap;    /* next handle; handles are never reused */

static void die(const char *msg, int64_t v) {
    fprintf(stderr, "cq_runtime: %s (%lld)\n", msg, (long long)v);
    abort();
}

int32_t cq_rail_alloc(uint32_t width, const uint64_t *v) {
    if (width == 0 || width > CQ_RAIL_MAX_WIDTH) die("rail width out of range", width);
    if (n == INT32_MAX) die("out of handles", n);
    if (n >= cap) {
        cap = cap ? 2 * cap : 64;
        if (!(slot = realloc(slot, (size_t)cap * sizeof *slot))) die("out of memory", cap);
    }
    cq_bit *b = malloc(width * sizeof *b);
    if (!b) die("out of memory", width);
    for (uint32_t i = 0; i < width; i++)
        b[i] = cq_bit_const(v && (v[i / 64] >> (i % 64)) & 1);
    slot[n] = (rail){ width, b };
    return n++;
}

static rail *get(int32_t h) {
    if (h <= 0 || h >= n || !slot[h].bits) die("bad or freed handle", h);
    return &slot[h];
}

cq_bit  *cq_rail_bits(int32_t h)  { return get(h)->bits; }
uint32_t cq_rail_width(int32_t h) { return get(h)->width; }

void cq_rail_free(int32_t h) {
    rail *r = get(h);
    for (uint32_t i = 0; i < r->width; i++) cq_reset_bit(&r->bits[i]);
    free(r->bits);
    r->bits = NULL;
}
