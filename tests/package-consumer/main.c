#include <stdint.h>

/* A lowered CQ object carries this declaration in its IR. Keeping the probe
 * independent of headers also verifies that the backend target supplies link
 * requirements rather than accidentally inheriting CQ's trace target. */
extern int32_t cqrt_alloc_i8(int8_t value);
extern int8_t cqrt_measure_i8(int32_t handle);

int main(void)
{
    const int32_t handle = cqrt_alloc_i8(7);
    return cqrt_measure_i8(handle) == 7 ? 0 : 1;
}
