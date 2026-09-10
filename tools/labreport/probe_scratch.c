/* Lab-report data probe: the PEAK and REGION scratch cost of K11 and K12,
 * MEASURED rather than written down. This is the j75 lesson as a build step —
 * `bd remember a-cited-scratch-figure-was-a-component-of-the-region`: a figure
 * that reaches a document through a formula someone typed is a figure nobody
 * re-derives. `cq_kd_peak` is the high-water mark DURING the call, which is the
 * bound a bounded device actually has to meet (PRD §15 D25(a)); the region is
 * asked of M19 itself.
 *
 * Emits a whitespace .dat for pgfplots. Built and run by gen_data.py against
 * build-release; it is registered in no CMake file and is not a test. */
#include <stdio.h>
#include "kerneldrv.h"
#include "kernels/mul.h"
#include "kernels/divrem_u.h"

static const cq_kd_spec MUL  = { "mul",  cq_kernel_mul,  NULL, NULL, NULL, NULL };
static const cq_kd_spec UDIV = { "udiv", cq_kernel_udiv, NULL, NULL, NULL, NULL };

int main(int argc, char **argv)
{
    static const int WS[] = { 2, 3, 4, 5, 6, 7, 8, 12, 16, 24, 32 };
    FILE *f = (argc > 1) ? fopen(argv[1], "w") : stdout;
    if (!f) return 1;

    fprintf(f, "# cq_kd_peak (high-water mark during the call) and\n"
               "# cq_divrem_region, measured against build-release/libcqops.a.\n");
    /* pgfplots reads column NAMES from the first NON-comment line. */
    fprintf(f, "W mulpeak divpeak divregion\n");
    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        uint32_t mp = 0, dp = 0;
        (void)cq_kd_peak(&MUL,  W, &mp);
        (void)cq_kd_peak(&UDIV, W, &dp);
        fprintf(f, "%d %u %u %d\n", W, mp, dp, cq_divrem_region(W, 1));
        fflush(f);
    }
    if (f != stdout) fclose(f);
    return 0;
}
