/* tests/support/kernelctrl.h — Step 20's half of the shared kernel driver.
 *
 * Split out of kerneldrv.c when the controlled axis took that file to 345 of
 * 300 (Rule 12). The seam is `the four LEVELS` against `the AXIS`: what asserts
 * L1/L2/L3/L5 per case stays in kerneldrv.c, and what selects a region, mints
 * the control rail and measures §9's transform is here.
 *
 * The public surface — cq_kd_set_ctrl, cq_kd_for_each_region and
 * cq_kd_check_promotion — is declared in kerneldrv.h with the rest of the
 * driver, because a suite should see one driver rather than two. This header is
 * the internal joint between the two translation units and nothing else
 * includes it.
 */
#ifndef CQOPS_TEST_KERNELCTRL_H
#define CQOPS_TEST_KERNELCTRL_H

#include "ctx.h"
#include "support/kerneldrv.h"

#include <stdint.h>

/* Mints the control rail for the active mode and remembers it. Returns
 * CQ_REG_NONE when the mode is CQ_KD_CTRL_NONE, which is what makes "is a
 * region open?" a single comparison at the push site. */
int32_t cq_kd_ctrl_rail(cq_ctx *ctx);

/* The rail minted by the last cq_kd_ctrl_rail, or CQ_REG_NONE. */
int32_t cq_kd_ctrl_handle(void);

/* Forgets it. Called from the fixture's close, so a case that never minted one
 * cannot push a stale handle from the previous case into a fresh table. */
void cq_kd_ctrl_clear(void);

#endif /* CQOPS_TEST_KERNELCTRL_H */
