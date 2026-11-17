/* create the ctx from a config, print the derived distance and fabric size, exit. */
#include <qec/qec.h>
#include <inttypes.h>
#include <stdio.h>
int main(int argc, char **argv) {
    if (argc < 2) return 2;
    qec_ctx *c = qec_create(argv[1]);
    if (!c) { fprintf(stderr, "%s: qec_create FAILED\n", argv[1]); return 1; }
    printf("%-28s d=%" PRIu32 " physical=%" PRIu32 "\n",
           argv[1], qec_distance(c), (uint32_t)qec_n_physical(c));
    qec_destroy(c);
    return 0;
}
