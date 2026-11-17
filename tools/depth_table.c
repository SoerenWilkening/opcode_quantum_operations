/* Measures every registered kernel candidate and writes the selection table and the depth doc.
 *   depth_table <src/kernels/kernel_choice.c> <docs/DEPTH.md>
 * Nothing is written unless every (operation, width) has a valid candidate. */
#include <stdio.h>
#include "depth.h"

static int copy(FILE *from, const char *path) {
    FILE *to = fopen(path, "w");
    if (!to) { perror(path); return -1; }
    char buf[4096];
    size_t n;
    rewind(from);
    while ((n = fread(buf, 1, sizeof buf, from)) > 0) fwrite(buf, 1, n, to);
    int bad = ferror(from) || ferror(to);
    if (fclose(to) || bad) { perror(path); return -1; }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: %s <kernel_choice.c> <DEPTH.md>\n", argv[0]); return 2; }
    FILE *cf = tmpfile(), *md = tmpfile();
    if (!cf || !md) { perror("tmpfile"); return 1; }
    if (depth_generate(cf, md, NULL)) { fprintf(stderr, "depth_table: failed, nothing written\n"); return 1; }
    if (copy(cf, argv[1]) || copy(md, argv[2])) return 1;
    printf("depth_table: wrote %s and %s\n", argv[1], argv[2]);
    return 0;
}
