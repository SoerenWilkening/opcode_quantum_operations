/* Step 3's other half: M02's hard errors are real.
 *
 * An out-of-range qubit index must abort in BOTH configurations —
 * CQOPS_DEBUG_INVARIANTS gates checking machinery, never behaviour, so a
 * fail-loud path that existed only in Debug would be a Rule 17 claim Release
 * does not honour.
 *
 * Rewritten at Step 4 onto the shared death harness (tests/support/death.h),
 * which is where the "why not WILL_FAIL" reasoning now lives.
 */

#include "shadow.h"
#include "support/death.h"

#include <stdio.h>

static void reads_past_the_end(void)
{
    cq_shadow_table sh;
    cq_shadow_init(&sh);
    cq_shadow_ensure(&sh, 4u);

    /* In range: must NOT abort. Checked outside the armed window, so a bounds
     * check that fired one entry early cannot pass for the death under test. */
    if (cq_shadow_get(&sh, 3u).unknown || cq_shadow_count(&sh) != 4u) {
        fprintf(stderr, "unreachable: ensure(4) did not give 4 clean qubits\n");
        return;
    }

    CQ_EXPECT_ABORT(cq_shadow_get(&sh, 4u));
}

static void writes_past_the_end(void)
{
    cq_shadow_table sh;
    cq_shadow_init(&sh);
    cq_shadow_ensure(&sh, 4u);

    CQ_EXPECT_ABORT(cq_shadow_x(&sh, 4u));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(reads_past_the_end),
    CQ_DEATH_CASE(writes_past_the_end)
)
