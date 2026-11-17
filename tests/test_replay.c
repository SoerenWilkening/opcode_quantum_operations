/* Replay backend self-test: hand-computed circuit, reset, growth. Always asserts. */
#undef NDEBUG
#include <assert.h>
#include "cq_gates.h"
#include "cq_replay.h"

int main(void) {
    cq_replay_reset();
    cq_replay_set_bit(2, 1);                 /* input prep: not a gate */
    assert(cq_replay_gates() == 0 && cq_replay_depth() == 0 && cq_replay_qubits() == 0);

    __quantum__qis__x__body(0);              /* L1 on q0 */
    __quantum__qis__x__body(1);              /* L1 on q1: disjoint, shares layer 1 */
    __quantum__qis__cnot__body(0, 3);        /* L2: q3 = 1 */
    __quantum__qis__ccx__body(0, 2, 3);      /* L3: q3 ^= 1&1 -> 0 */
    __quantum__qis__ry__body(0.0, 1);        /* L2 on q1, bit unchanged */
    __quantum__qis__ry__ctl(0.0, 1, 4);      /* L3 on q1,q4 */
    __quantum__qis__rz__body(0.0, 4);        /* L4 on q4 */
    assert(__quantum__qis__m__body(3) == 0); /* L4 on q3 */

    assert(cq_replay_get_bit(0) == 1 && cq_replay_get_bit(1) == 1 && cq_replay_get_bit(2) == 1);
    assert(cq_replay_get_bit(3) == 0 && cq_replay_get_bit(4) == 0);
    assert(cq_replay_gates() == 8);
    assert(cq_replay_gates_of(CQ_G_X) == 2 && cq_replay_gates_of(CQ_G_CNOT) == 1 &&
           cq_replay_gates_of(CQ_G_CCX) == 1 && cq_replay_gates_of(CQ_G_RY) == 1 &&
           cq_replay_gates_of(CQ_G_RY_CTL) == 1 && cq_replay_gates_of(CQ_G_RZ) == 1 &&
           cq_replay_gates_of(CQ_G_M) == 1);
    assert(cq_replay_depth() == 4);
    assert(cq_replay_qubits() == 5 && cq_replay_max_index() == 4);

    cq_replay_reset();
    assert(cq_replay_gates() == 0 && cq_replay_depth() == 0 && cq_replay_qubits() == 0);
    assert(cq_replay_gates_of(CQ_G_X) == 0 && cq_replay_get_bit(0) == 0 && cq_replay_get_bit(2) == 0);
    __quantum__qis__x__body(0);
    assert(cq_replay_depth() == 1);          /* per-qubit times were cleared too */

    cq_replay_reset();
    assert(cq_replay_get_bit(5000000) == 0); /* read does not need growth */
    __quantum__qis__x__body(1000000);
    __quantum__qis__cnot__body(1000000, 70000);
    assert(cq_replay_get_bit(70000) == 1 && cq_replay_get_bit(999999) == 0);
    assert(cq_replay_qubits() == 2 && cq_replay_max_index() == 1000000 && cq_replay_depth() == 2);
    return 0;
}
