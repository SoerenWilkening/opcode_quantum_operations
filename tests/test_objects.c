/* The object registry of the memory bus (ABI §1.10 "Objects", src/qram.c): cqrt_qram_object and the
 * two lookups. Each case uses its own address range, since the registry is never cleared. Always asserts. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include "cqrt.h"
#include "cq_bus.h"

static void at(uint64_t a, uint64_t s, uint64_t n) {   /* n = 0: no object contains a */
    uint64_t s1 = 0, n1 = 0;
    assert(cq_object_at(a, &s1, &n1) == (n != 0));
    if (n) assert(s1 == s && n1 == n);
}
static void ends(uint64_t a, uint64_t s, uint64_t n) {
    uint64_t s1 = 0, n1 = 0;
    assert(cq_object_ending_at(a, &s1, &n1) == (n != 0));
    if (n) assert(s1 == s && n1 == n);
}

int main(void) {
    at(0x1000, 0, 0); ends(0, 0, 0);
    /* first, last, one past; idempotent */
    cqrt_qram_object(0x1000, 16); cqrt_qram_object(0x1000, 16);
    at(0xfff, 0, 0); at(0x1000, 0x1000, 16); at(0x100f, 0x1000, 16); at(0x1010, 0, 0);
    ends(0x1010, 0x1000, 16); ends(0x100f, 0, 0); ends(0x1000, 0, 0);
    /* size 0 and negative register nothing */
    cqrt_qram_object(0x2000, 0); cqrt_qram_object(0x2000, -8);
    at(0x2000, 0, 0); at(0x1ff8, 0, 0); ends(0x2000, 0, 0);
    /* touching stay separate: 0x3000+8 and 0x3008+8; the end lookup at the seam names the first */
    cqrt_qram_object(0x3008, 8); cqrt_qram_object(0x3000, 8);
    at(0x3007, 0x3000, 8); at(0x3008, 0x3008, 8); ends(0x3008, 0x3000, 8); ends(0x3010, 0x3008, 8);
    /* overlap merges into the union, also across several and when one contains another */
    cqrt_qram_object(0x4000, 16); cqrt_qram_object(0x4020, 16); cqrt_qram_object(0x400c, 0x18);
    at(0x4000, 0x4000, 0x30); at(0x402f, 0x4000, 0x30); at(0x4030, 0, 0);
    ends(0x4010, 0, 0); ends(0x4030, 0x4000, 0x30);
    cqrt_qram_object(0x4004, 4); at(0x4004, 0x4000, 0x30);
    cqrt_qram_object(0x3ff0, 0x50); at(0x3ff0, 0x3ff0, 0x50); ends(0x4040, 0x3ff0, 0x50);
    at(0x3008, 0x3008, 8); at(0x1000, 0x1000, 16);   /* earlier objects untouched */
    /* reaching 2^64 is ignored; ending just below it is kept */
    cqrt_qram_object(-16, 16); cqrt_qram_object(-16, 100); at(UINT64_MAX, 0, 0); at(-16, 0, 0);
    cqrt_qram_object(-32, 16); at((uint64_t)-17, (uint64_t)-32, 16); ends((uint64_t)-16, (uint64_t)-32, 16);
    /* many, out of order: the array grows and stays sorted */
    for (int i = 999; i >= 0; i--) cqrt_qram_object(0x100000 + 64 * i, 32);
    for (int i = 0; i < 1000; i++) {
        at(0x100000 + 64 * i + 31, 0x100000 + 64 * i, 32); at(0x100000 + 64 * i + 32, 0, 0);
        ends(0x100000 + 64 * i + 32, 0x100000 + 64 * i, 32);
    }
    printf("objects ok\n");
    return 0;
}
