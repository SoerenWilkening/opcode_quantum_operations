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

/* Optional: operation notes, for a gate backend that draws operations (src/gates_trace.c).
 * libcq_backend.a holds weak do-nothing definitions of the three, so a backend need not define
 * them; one that does replaces them. Only the outermost operation of an ABI call is noted. */
#define CQ_GATES_NO_QUBIT UINT32_MAX
int  cq_gates_op_begin(const char *name);   /* 0: not interested, no rail and no end call follows */
void cq_gates_op_end(void);
/* Rail `handle` as it is now, told for each operand after begin and again before end:
 * qubit[i] is bit i's qubit, or CQ_GATES_NO_QUBIT for a classical bit. width 0: freed. */
void cq_gates_rail(uint32_t handle, uint32_t width, const uint32_t *qubit);

#endif
