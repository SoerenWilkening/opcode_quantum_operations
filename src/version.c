/* The one translation unit libcqops carries at Step 1.
 *
 * Its job is to make the Step 1 gate mean something: a test that only ran the
 * harness would prove the harness, not the build. Linking a real library
 * symbol proves the include path, the archive, and the link line under both
 * configurations. */

#include "cqops/cqops.h"

const char *cqops_version_string(void)
{
    return CQOPS_VERSION_STRING;
}
