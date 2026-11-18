/* Trace backend self-test: the seven operations and the operation notes, written at exit to the
 * file named by CQ_TRACE; the `trace` test compares it with tests/trace_expected.txt. */
#include "cq_gates.h"

#define NQ CQ_GATES_NO_QUBIT

int main(void) {
    __quantum__qis__x__body(7);                              /* outside any op: "other", an ancilla wire */

    cq_gates_op_begin("add");                                /* rail 1 on qubits 0, 1; rail 2 gets its qubits inside */
    cq_gates_rail(1, 2, (uint32_t[]){ 0, 1 });
    cq_gates_rail(2, 2, (uint32_t[]){ NQ, NQ });
    __quantum__qis__cnot__body(0, 2);
    __quantum__qis__ccx__body(0, 1, 3);
    cq_gates_rail(1, 2, (uint32_t[]){ 0, 1 });
    cq_gates_rail(2, 2, (uint32_t[]){ 2, 3 });
    cq_gates_op_end();

    cq_gates_op_begin("empty");                              /* no gate: not written */
    cq_gates_op_end();

    cq_gates_op_begin("measure");                            /* rail 1 gives up both qubits */
    cq_gates_rail(1, 2, (uint32_t[]){ 0, 1 });
    __quantum__qis__m__body(0);
    __quantum__qis__m__body(1);
    cq_gates_rail(1, 2, (uint32_t[]){ NQ, NQ });
    cq_gates_op_end();

    cq_gates_op_begin("ry");                                 /* qubit 0 again, now rail 3's: a new wire */
    cq_gates_rail(3, 1, (uint32_t[]){ NQ });
    __quantum__qis__ry__body(0.5, 0);
    __quantum__qis__ry__ctl(0.5, 2, 0);
    __quantum__qis__rz__body(0.5, 1000);                     /* grows the qubit table */
    cq_gates_rail(3, 1, (uint32_t[]){ 0 });
    cq_gates_op_end();
    return 0;
}
