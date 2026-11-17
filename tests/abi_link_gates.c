/* A second, independent definition of the seven operations (kmgy.23, NORTH_STAR "Done when" 1: "any
 * definition of the seven operations"): one bit per qubit and a gate count, nothing shared with
 * src/gates_replay.c. Rotations count and do nothing. Prints the count at exit; no gate at all fails. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static uint8_t *bit;
static uint32_t nbit;
static unsigned long long gates;

static uint8_t *q(uint32_t i) {
    if (i >= nbit) {
        uint32_t n = i * 2 + 64;
        if (!(bit = realloc(bit, n))) abort();
        for (uint32_t j = nbit; j < n; j++) bit[j] = 0;
        nbit = n;
    }
    return &bit[i];
}

void __quantum__qis__x__body(uint32_t t) { gates++; *q(t) ^= 1; }
void __quantum__qis__cnot__body(uint32_t c, uint32_t t) { gates++; *q(t) ^= *q(c); }
void __quantum__qis__ccx__body(uint32_t a, uint32_t b, uint32_t t) { gates++; *q(t) ^= *q(a) & *q(b); }
void __quantum__qis__ry__body(double th, uint32_t t) { (void)th; (void)q(t); gates++; }
void __quantum__qis__ry__ctl(double th, uint32_t c, uint32_t t) { (void)th; (void)q(c); (void)q(t); gates++; }
void __quantum__qis__rz__body(double th, uint32_t t) { (void)th; (void)q(t); gates++; }
int __quantum__qis__m__body(uint32_t t) { gates++; return *q(t); }

__attribute__((destructor)) static void report(void) {
    printf("counting backend: %llu operations\n", gates);
    fflush(stdout);
    if (!gates) _exit(1);
}
