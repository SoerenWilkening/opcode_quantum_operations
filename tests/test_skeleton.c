/* Skeleton test: the width list iterates, and the library links against a
 * test-defined gate backend (the backend archive defines none of the seven). */
#include <stdio.h>
#include "cq_gates.h"
#include "cq_widths.h"
#include "cq_config.h"
#include "cq_replay.h"

int main(void) {
    int n = 0, bits = 0;
#define X(name, b) printf("%s = %d bits\n", #name, b); n++; bits += b;
    CQ_WIDTHS(X)
#undef X
    __quantum__qis__x__body(0);
    printf("K2=%d MAX_QADDR_BITS=%d\n", CQ_K2, CQ_MAX_QADDR_BITS);
    return (n == 30 && bits == 1270 && cq_replay_gates() == 1) ? 0 : 1;
}
