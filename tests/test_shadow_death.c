/* Step 3, the other half of the gate: M02's hard error is real.
 *
 * An out-of-range qubit index must abort in BOTH configurations —
 * CQOPS_DEBUG_INVARIANTS gates checking machinery, never behaviour, so a
 * fail-loud path that existed only in Debug would be a Rule 17 claim Release
 * does not honour.
 *
 * WHY THIS IS NOT REGISTERED WILL_FAIL, which is the obvious way to write it:
 * CTest's WILL_FAIL inverts a non-zero *exit code* and does NOT invert an
 * abnormal termination. abort() raises SIGABRT, which CTest reports as
 * "Subprocess aborted" and leaves failed no matter what WILL_FAIL says.
 * (Measured, 2026-08-14; it is also what the CMake docs mean by "this does not
 * work for the crash cases".) So the binary catches SIGABRT itself and exits
 * 0 only if the abort arrived exactly where it was expected. That inverts the
 * default too: this is a normal test, and it fails if nothing aborts.
 *
 * `expecting` is armed on the line before the call and never disarmed, so an
 * abort from anywhere else — a sanitizer report under ASAN_OPTIONS=
 * abort_on_error=1, say — cannot masquerade as the death being asserted.
 *
 * Structure note for Steps 4 and 6: one death per binary. Step 4 wants two
 * (D2 ceiling, I3 dirty release) and Step 6 four (the §3 distinctness
 * asserts); a selector-driven harness beats six near-identical files, and is
 * filed as its own bead.
 */

#include "shadow.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static volatile sig_atomic_t expecting = 0;

static void on_abort(int sig)
{
    (void)sig;
    _Exit(expecting ? 0 : 4);
}

int main(void)
{
    cq_shadow_table sh;
    cq_shadow_init(&sh);
    cq_shadow_ensure(&sh, 4u);

    /* In range: must NOT abort. Checked before the handler is installed, so
     * a bounds check that fired one entry early cannot be mistaken for the
     * death under test. */
    if (cq_shadow_get(&sh, 3u).unknown) {
        fprintf(stderr, "unreachable: q3 was born poisoned\n");
        return 2;
    }
    if (cq_shadow_count(&sh) != 4u) {
        fprintf(stderr, "unreachable: ensure(4) gave %u\n",
                cq_shadow_count(&sh));
        return 3;
    }

    if (signal(SIGABRT, on_abort) == SIG_ERR) {
        fprintf(stderr, "could not install SIGABRT handler\n");
        return 5;
    }

    fflush(stdout);
    fflush(stderr);

    expecting = 1;
    cq_shadow_get(&sh, 4u);   /* one past the end — must abort, exit 0 */

    /* Reached only if the bounds check is missing or was compiled out. */
    fprintf(stderr, "libcqops: BUG: out-of-range shadow read did not abort\n");
    cq_shadow_dispose(&sh);
    return 1;
}
