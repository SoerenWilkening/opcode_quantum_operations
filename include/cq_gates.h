/* The seven operations the library emits. The library archives define none of
 * them; a gate backend (libcq_gates_replay.a, or any other) does. */
#ifndef CQ_GATES_H
#define CQ_GATES_H
#include <stdint.h>

void __quantum__qis__x__body   (uint32_t q);
void __quantum__qis__cnot__body(uint32_t control, uint32_t target);
void __quantum__qis__ccx__body (uint32_t c1, uint32_t c2, uint32_t target);
void __quantum__qis__ry__body  (double theta, uint32_t q);
void __quantum__qis__ry__ctl   (double theta, uint32_t control, uint32_t target);
void __quantum__qis__rz__body  (double theta, uint32_t q);
int  __quantum__qis__m__body   (uint32_t q); /* outcome, 0 or 1 */

#endif
