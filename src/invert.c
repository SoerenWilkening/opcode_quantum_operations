/* cqrt_invert_begin/end, BACKEND_ABI §1.13 and CR rule 17 (kmgy.8).
 *
 * One flat list of recorded calls and a stack of region starts. A call recorded inside a region
 * is run when the outermost region ends: `end` reverses the innermost region's slice of the list
 * and flips each call's inverse flag, so a nested region inside an inverted one comes out
 * forward again (two reversals, two flips). What is not recorded takes effect at its call:
 * allocations (the result rail of a recorded template or load too), push/pop, tape bookkeeping.
 * A measurement, a bus store and an effect entry performed at its call are flush points: every
 * call recorded so far, in every open region, runs forward first. A free is held until after
 * the outermost reversed list. */
#include <stdio.h>
#include <stdlib.h>
#include "cqrt.h"
#include "cq_rt.h"

typedef struct { cq_call k; int inv; } rec;
static rec      *recs;
static uint32_t  nrecs, caprecs, *starts, depth, capstarts;
static int32_t  *frees;
static uint32_t  nfrees, capfrees;

static void *grow(void *p, uint32_t *cap, uint32_t n, size_t sz) {
    if (n < *cap) return p;
    *cap = *cap ? 2 * *cap : 64;
    if (!(p = realloc(p, *cap * sz))) { fprintf(stderr, "cq_runtime: invert: out of memory\n"); abort(); }
    return p;
}

int cq_inv_open(void) { return depth > 0; }

void cq_call_do(const cq_call *k) {
    if (!depth) { k->run(k, 0); return; }
    recs = grow(recs, &caprecs, nrecs, sizeof *recs);
    recs[nrecs++] = (rec){ *k, 0 };
}

/* Runs the whole list in order and empties it; depth 0 meanwhile, so the calls execute. */
static void run_all(void) {
    uint32_t d = depth;
    depth = 0;
    for (uint32_t i = 0; i < nrecs; i++) recs[i].k.run(&recs[i].k, recs[i].inv);
    nrecs = 0;
    depth = d;
    for (uint32_t i = 0; i < d; i++) starts[i] = 0;
}

void cq_inv_flush(void) { if (depth) run_all(); }

void cq_inv_free(int32_t h) {
    if (!depth) { cq_rail_free(h); return; }
    frees = grow(frees, &capfrees, nfrees, sizeof *frees);
    frees[nfrees++] = h;
}

void cqrt_invert_begin(void) {
    starts = grow(starts, &capstarts, depth, sizeof *starts);
    starts[depth++] = nrecs;
}

void cqrt_invert_end(void) {
    if (!depth) { fprintf(stderr, "cq_runtime: cqrt_invert_end without an open region\n"); abort(); }
    uint32_t s = starts[--depth];
    for (uint32_t i = s, j = nrecs; i < j--; i++) { rec t = recs[i]; recs[i] = recs[j]; recs[j] = t; }
    for (uint32_t i = s; i < nrecs; i++) recs[i].inv ^= 1;
    if (depth) return;
    run_all();
    for (uint32_t i = 0; i < nfrees; i++) cq_rail_free(frees[i]);
    nfrees = 0;
}
