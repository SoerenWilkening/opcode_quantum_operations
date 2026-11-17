/* Host side of the IR calling-convention test (kmgy.29): the checked-in caller.ll is compiled by
 * clang, calls every core entry point as LLVM IR would, and prints "FAIL <width>: <symbol> ...".
 * Here: refuse a stale caller.ll (a width added to cq_widths.h without regenerating) and run it. */
#include <stdio.h>
#include <string.h>
#include "cq_widths.h"

extern const char cq_ir_widths[];
extern int cq_ir_caller(void);

#define W_(n, bits) #n ","
#define F_(n, bits, T) #n ","

int main(void) {
    const char *want = CQ_WIDTHS(W_) CQ_FP_WIDTHS(F_);
    if (strcmp(cq_ir_widths, want)) {
        printf("FAIL caller.ll is stale: it covers \"%s\" but cq_widths.h has \"%s\"; "
               "regenerate: cd tests/ir && python3 gen.py > caller.ll\n", cq_ir_widths, want);
        return 1;
    }
    int bad = cq_ir_caller();
    puts(bad ? "FAIL: IR caller disagrees with the runtime (see lines above)" : "ir_abi ok");
    return bad;
}
