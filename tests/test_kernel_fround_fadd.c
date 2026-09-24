/* tests/test_kernel_fround_fadd.c — M38b, K25: the CIRCUIT sweeps of
 * `soft_floor` and `soft_ceil`, the two kernels whose program carries M33's
 * `soft_fadd` as one hand-off row (fround.jl:73, :93).
 *
 * SPLIT FROM tests/test_kernel_fround.c FOR WALL CLOCK ONLY: each of these two
 * compute halves is ~50k slots longer than the other three kernels', and a
 * separate binary lets ctest run them beside the structural suite. Everything
 * structural about floor and ceil — census, composition, the trunc-body
 * inlining identity, the slot scan with M33's run delegated, the span scan,
 * two programs in one region, the L4 goldens — is the other binary's; the
 * oracle, the anchors and the specs are shared through
 * tests/test_kernel_fround_spec.inc.
 */

#include "kernels/fround.h"

#include "bit.h"
#include "ctx.h"
#include "kernels/fpfield.h"
#include "reg.h"
#include "shadow.h"
#include "sink_count.h"

#include "support/bitkinds.h"
#include "support/fpanchors.h"
#include "support/fphost.h"
#include "support/harness.h"
#include "support/kerneldrv.h"
#include "support/refmodel.h"

#include <string.h>

#include "test_kernel_fround_spec.inc"

CQ_TEST(the_host_is_round_to_nearest)
{
    char why[CQ_FPHOST_WHY_MAX];

    if (!cq_fphost_check(why, sizeof why))
        cq_h_fail(__FILE__, __LINE__, "host fp probe: %s", why);
}

/* L1 + L2 + L3 + L5, Step 20's four regions, §9's promotion identity and the
 * peak. The controlled-row value is the one that takes the fadd path. */
CQ_TEST(circuit_ffloor) { fr_circuit_suite(CQ_FROUND_FLOOR, FR_N2P5); }
CQ_TEST(circuit_fceil)  { fr_circuit_suite(CQ_FROUND_CEIL,  FR_2P5);  }

CQ_TEST_MAIN_ARGV(
    CQ_CASE(the_host_is_round_to_nearest),
    CQ_CASE(circuit_ffloor),
    CQ_CASE(circuit_fceil)
)
