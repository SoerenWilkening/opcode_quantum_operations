/* Counting backend self-test: the circuit of tests/test_replay.c, whose counts are hand-computed
 * there; the `count` test compares the report with tests/count_expected.txt. */
#include "cq_gates.h"

int main(void) {
    __quantum__qis__x__body(0);              /* L1 on q0 */
    __quantum__qis__x__body(1);              /* L1 on q1 */
    __quantum__qis__cnot__body(0, 3);        /* L2 */
    __quantum__qis__ccx__body(0, 2, 3);      /* L3 */
    __quantum__qis__ry__body(0.0, 1);        /* L2 on q1 */
    __quantum__qis__ry__ctl(0.0, 1, 4);      /* L3 on q1, q4 */
    __quantum__qis__rz__body(0.0, 4);        /* L4 on q4 */
    __quantum__qis__m__body(3);              /* L4 on q3 */
    __quantum__qis__x__body(1000000);        /* grows the table: L1, a sixth qubit */
    return 0;
}
