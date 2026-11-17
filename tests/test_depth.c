/* The shipped kernel per (operation, width) is the measured minimum, and the checked-in
 * src/kernels/kernel_choice.c and docs/DEPTH.md are what tools/depth_table writes now. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "depth.h"

#define REGEN "cmake --build <build> --target depth_tables"

static void same_as_file(FILE *got, const char *path) {
    FILE *want = fopen(path, "r");
    if (!want) { perror(path); abort(); }
    rewind(got);
    for (long at = 0;; at++) {
        int a = fgetc(got), b = fgetc(want);
        if (a != b) {
            fprintf(stderr, "depth: %s is stale (differs at byte %ld); regenerate: " REGEN "\n", path, at);
            abort();
        }
        if (a == EOF) break;
    }
    fclose(want);
}

static void nop(int32_t o, const int32_t *a, uint32_t w) { (void)o; (void)a; (void)w; }
static hx_val parity_ref(const hx_val *a, uint32_t w) {
    (void)w;
    uint64_t x = 0;
    for (uint32_t k = 0; k < CQ_LIMBS; k++) x ^= a[0].l[k];
    return (hx_val){ { (uint64_t)__builtin_parityll(x) } };
}

int main(void) {
    /* Re-measure the registry; the linked table must be the minimum at every (op, width). */
    FILE *cf = tmpfile(), *md = tmpfile();
    assert(cf && md);
    uint8_t ch[CQ_KOP_COUNT][CQ_NWIDTHS];
    assert(depth_generate(cf, md, ch) == 0);
    for (int op = 0; op < CQ_KOP_COUNT; op++)
        for (int wi = 0; wi < CQ_NWIDTHS; wi++)
            if (ch[op][wi] != cq_kernel_choice[op][wi]) {
                fprintf(stderr, "depth: %s at i%u ships candidate %u, the minimum is %u (%s); regenerate: " REGEN "\n",
                        cq_kernel_ops[op].name, cq_kernel_widths[wi], cq_kernel_choice[op][wi], ch[op][wi],
                        cq_kernel_ops[op].cand[ch[op][wi]].name);
                abort();
            }
    same_as_file(cf, CQ_SOURCE_DIR "/src/kernels/kernel_choice.c");
    same_as_file(md, CQ_SOURCE_DIR "/docs/DEPTH.md");

    /* The demo crosses over: chain at small widths, tree at large ones; unlisted widths round up. */
    assert(!strcmp(cq_kernel_pick(CQ_KOP_PARITY, 6)->name, "chain"));
    assert(!strcmp(cq_kernel_pick(CQ_KOP_PARITY, 64)->name, "tree_inplace"));
    assert(cq_kernel_pick(CQ_KOP_PARITY, 9) == cq_kernel_pick(CQ_KOP_PARITY, 16));
    assert(cq_kernel_pick(CQ_KOP_PARITY, 1000) == cq_kernel_pick(CQ_KOP_PARITY, 200));

    /* A wrong candidate is never selected, even when it is the shallowest (depth 0). */
    const cq_kernel cand[] = { { "wrong", nop, NULL, "" }, { "chain", cq_k_parity_chain, NULL, "" } };
    const cq_kernel_op fake = { "parity", "", 1, 1, cand, 2, 0, 0 };
    static depth_cell cell[DEPTH_MAX_CAND][CQ_NWIDTHS];
    uint8_t c[CQ_NWIDTHS];
    fprintf(stderr, "---- expected failures follow\n");
    assert(depth_measure(&fake, parity_ref, cell, c) == 0);
    for (int wi = 0; wi < CQ_NWIDTHS; wi++) assert(!cell[0][wi].ok && cell[1][wi].ok && c[wi] == 1);
    const cq_kernel_op none = { "parity", "", 1, 1, cand, 1, 0, 0 };
    assert(depth_measure(&none, parity_ref, cell, c) == -1);
    fprintf(stderr, "---- end of expected failures\n");
    printf("depth: %d operation(s) x %d widths, shipped table is the minimum and up to date\n", CQ_KOP_COUNT, CQ_NWIDTHS);
    return 0;
}
