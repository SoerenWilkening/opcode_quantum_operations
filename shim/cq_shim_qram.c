/* shim/cq_shim_qram.c — see cq_shim_qram.h for what the table is and why a cell
 * is a register. */

#include "cq_shim_qram.h"

#include <stdio.h>
#include <stdlib.h>

#include "cq_shim_record.h"
#include "kernels/qrom.h"
#include "reg.h"

static cq_qram_array *g_arr;
static uint32_t g_n, g_cap;

static void cq_qram_die(const char *what, int32_t h, long k)
{
    fprintf(stderr, "libcqops: FATAL: shim: %s (h%d, %ld)\n", what, h, k);
    abort();
}

int32_t cq_qram_alloc(cq_ctx *ctx, uint32_t width, int32_t count)
{
    cq_qram_array *a;
    int32_t token;

    /* The kernel's own bound, asked here so the message names the ABI's
     * argument rather than a tree the caller never sees. */
    if (count <= 0)
        cq_qram_die("cqrt_qram_alloc: count is not positive", CQ_REG_NONE,
                    (long)count);
    if (count > CQ_QRAM_COUNT_MAX)
        cq_qram_die("cqrt_qram_alloc: count exceeds CQ_QRAM_COUNT_MAX (the "
                    "kernel's step index is an int)", CQ_REG_NONE, (long)count);

    if (g_n == g_cap) {
        uint32_t cap = g_cap ? g_cap * 2u : 8u;
        cq_qram_array *p = realloc(g_arr, cap * sizeof *p);
        if (!p) cq_qram_die("out of memory", CQ_REG_NONE, 0);
        g_arr = p; g_cap = cap;
    }

    /* THE TOKEN FIRST, so that the cells are a+1 … a+count and `cell0` is a
     * derived fact rather than a stored one that could drift. */
    token = cq_reg_alloc_token(&ctx->regs);
    cq_rec_mint(token, 0u, 0u, 0u, 0);

    a = &g_arr[g_n++];
    a->token    = token;
    a->width    = width;
    a->count    = count;
    a->cell0    = token + 1;
    a->tape     = NULL;
    a->n_tape   = 0u;
    a->cap_tape = 0u;

    for (int32_t j = 0; j < count; j++) {
        int32_t h = cq_reg_alloc_zero(&ctx->regs, width);
        if (h != a->cell0 + j)
            cq_qram_die("the cells of one array are not consecutive handles",
                        h, (long)(a->cell0 + j));
    }
    return token;
}

cq_qram_array *cq_qram_find_mut(int32_t h)
{
    for (uint32_t i = 0; i < g_n; i++)
        if (g_arr[i].token == h) return &g_arr[i];
    return NULL;
}

const cq_qram_array *cq_qram_find(int32_t h) { return cq_qram_find_mut(h); }

void cq_qram_push(cq_qram_array *a, cq_qram_entry e)
{
    if (a->n_tape == a->cap_tape) {
        uint32_t cap = a->cap_tape ? a->cap_tape * 2u : 4u;
        cq_qram_entry *p = realloc(a->tape, cap * sizeof *p);
        if (!p) cq_qram_die("out of memory", a->token, 0);
        a->tape = p; a->cap_tape = cap;
    }
    a->tape[a->n_tape++] = e;
}

const cq_qram_entry *cq_qram_top(const cq_qram_array *a)
{
    return a->n_tape ? &a->tape[a->n_tape - 1u] : NULL;
}

void cq_qram_pop(cq_qram_array *a)
{
    if (!a->n_tape)
        cq_qram_die("pop on an array with no unpopped store", a->token, 0);
    a->n_tape--;
}

uint32_t cq_qram_arrays(void) { return g_n; }

void cq_qram_reset(void)
{
    for (uint32_t i = 0; i < g_n; i++) free(g_arr[i].tape);
    free(g_arr);
    g_arr = NULL; g_n = 0u; g_cap = 0u;
}
