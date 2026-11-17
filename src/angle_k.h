/* The angle-ladder index k(a) on the host (BACKEND_ABI §1.5, §1.7; docs/ANGLE_LADDER.md).
 * Internal to libcq_runtime.a; the quantum angle rail (kmgy.18) builds its circuit from the
 * same constant and window. */
#ifndef CQ_ANGLE_K_H
#define CQ_ANGLE_K_H
#include <stdint.h>
#include "cq_config.h"

#define CQ_ANGLE_G 63                            /* guard width, proven for every N <= 128 */
#define CQ_ANGLE_L (53 + CQ_ANGLE_G)             /* fraction bits the window keeps */
#define CQ_ANGLE_WBITS (CQ_LADDER_N + CQ_ANGLE_L) /* window and product width: 169 at N = 53 */
#define CQ_INV_PI_J 1216                         /* cq_inv_pi = floor(2^1216 / pi), 19 limbs */
_Static_assert(CQ_LADDER_N >= 1 && CQ_LADDER_N <= 128, "the guard search covers N in [1, 128]");

/* floor(2^1216 / pi), little-endian 64-bit limbs: bit j of 1/pi is bit 1216 - j. It reaches
 * bit N + g + 1023 of 1/pi for every N <= 128 (at N = 53 the note needs bits 1..1140). */
extern const uint64_t cq_inv_pi[19];

/* The window for exponent field e1 (0 replaced by 1) and P = 2^s * pi (s = 2: Ry, 1: phase):
 * bits t-N+1 .. t+L of 1/pi, t = e1 - 1075 + N - s, as an integer of CQ_ANGLE_WBITS bits. */
void cq_angle_window(uint32_t e1, int s, uint64_t w[4]);

/* k(a) = round(val(a) * 2^N / P) mod 2^N for the double with bit pattern a, 0 for Inf/NaN.
 * Correctly rounded for every pattern (docs/ANGLE_LADDER.md §4). */
unsigned __int128 cq_angle_k(uint64_t a, int s);

#endif
