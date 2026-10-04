/* tests/test_harness_select.c — case selection through the REAL main (bd 5mqp).
 *
 * Three trivial cases on CQ_TEST_MAIN_ARGV. The verdict is not in here: it is
 * in tests/CMakeLists.txt, where SPLIT registers one entry per slice and each
 * entry's FAIL_REGULAR_EXPRESSION names the cases it must not run, plus one
 * WILL_FAIL entry that asks for a case this table does not hold. What it pins
 * is the wiring tests/test_skeleton.c's in-process cases cannot reach: that
 * argv arrives at cq_h_select at all, and that add_cqops_test's SPLIT spells
 * the --case / --skip / --shard lines that partition the table.
 *
 * gamma PRINTS the shard it saw, and that line is what each shard entry's
 * regex reads — a shard entry that silently ran unsharded (shard 0/1) or as
 * the other half would go red there. */

#include "support/harness.h"

#include <stdio.h>

CQ_TEST(alpha) { CHECK(1 == 1); }

CQ_TEST(beta) { CHECK(2 == 2); }

CQ_TEST(gamma)
{
    int i;
    const int k = cq_h_shard(&i);

    printf("# gamma under shard %d/%d\n", i, k);
    CHECK(k >= 1);
    CHECK(i >= 0 && i < k);
}

CQ_TEST_MAIN_ARGV(
    CQ_CASE(alpha),
    CQ_CASE(beta),
    CQ_CASE(gamma)
)
