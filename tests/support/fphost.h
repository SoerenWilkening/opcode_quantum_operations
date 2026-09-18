/* tests/support/fphost.h — the HOST probe for L1's floating-point oracle
 * (PRD-v2 §7.4, §7.12).
 *
 * WHY THIS EXISTS, AND WHY IT IS TEST-SIDE ONLY. PRD-v2 §7.4 decides two things
 * that pull in opposite directions. The LIBRARY's fp short-circuit is a C
 * transcription of Bennett's Julia body over `uint64_t`, so it never touches a
 * C `double` and is host-independent by construction. L1's ORACLE is the
 * opposite: it is the HOST operator, deliberately, because an oracle that
 * shares code with the implementation is blind to exactly what that code gets
 * wrong (the Step 18 trap). That buys independence and it buys a dependency on
 * the host, and the dependency is real — every cell in §7.4's table is
 * IEEE-unspecified and every value in it is x86's choice.
 *
 * So the oracle's ground has to be checked rather than assumed. A host whose
 * rounding mode is not to-nearest, or that flushes subnormals, or whose NaN
 * cells are ARM's, would fail every fp anchor IN THE ORACLE rather than in the
 * port — and the red would read as a kernel bug. This header is what makes that
 * failure arrive as itself.
 *
 * NOTHING UNDER src/ OR shim/ MAY INCLUDE THIS. The asymmetry is the proof, the
 * same way _DEFAULT_SOURCE is (tests/CMakeLists.txt): the library compiles
 * strict C11 against libc alone, and it does no `double` arithmetic at all.
 *
 * MEASURED 2026-09-18, this box (x86_64 Darwin 25), Apple clang 17 and Homebrew
 * clang 22, both at -std=c11 -ffp-contract=off. The two compilers agree on
 * every cell below.
 */
#ifndef CQOPS_TEST_FPHOST_H
#define CQOPS_TEST_FPHOST_H

#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * §7.4's five cells, as named constants — ONE PLACE, so the fp oracle's table
 * (bead `hkg` / M36) reads them from here rather than re-deriving them.
 *
 * INDEF is Intel's "indefinite" QNaN, SDM Vol 1 §4.8.3.7, and it is what
 * Bennett writes as `INDEF = 0xFFF8000000000000` (softfloat_common.jl:14). Note
 * the SIGN BIT: x86's default NaN is NEGATIVE. ARM's is positive
 * (0x7ff8000000000000), which is the single cheapest way for this probe to
 * notice it is on the wrong machine.
 * ------------------------------------------------------------------------- */
#define CQ_FPHOST_INDEF       UINT64_C(0xfff8000000000000)

/* The quiet bit — bit 51, the mantissa MSB. A signalling NaN that reaches an
 * arithmetic operator comes back with this set. */
#define CQ_FPHOST_QUIET_BIT   UINT64_C(0x0008000000000000)

/* Two DISTINGUISHABLE quiet NaNs and one signalling NaN. The payloads differ so
 * that "the first operand won" and "the qNaN won" are separable claims — see
 * the rule below, which was measured with a payload pair that separates them. */
#define CQ_FPHOST_QNAN_A      UINT64_C(0x7ff8000000000001)
#define CQ_FPHOST_QNAN_B      UINT64_C(0x7ff8000000000002)
#define CQ_FPHOST_SNAN        UINT64_C(0x7ff0000000000001)

/* What CQ_FPHOST_SNAN becomes when it is the operand that wins. */
#define CQ_FPHOST_SNAN_QUIET  (CQ_FPHOST_SNAN | CQ_FPHOST_QUIET_BIT)

/* THE RULE, AND A CORRECTION TO THE PROSE THAT DESCRIBES IT. PRD-v2 §7.4's
 * table row reads "qNaN + sNaN, both orders | the qNaN — no signalling
 * priority", and `bd remember fp-nan-cells-are-x86-and-pinned-by-table` says
 * the same. Measured here with a payload pair that can tell the two readings
 * apart, that row is DEGENERATE: it was taken with the sNaN's payload equal to
 * the qNaN's, and quietening CQ_FPHOST_SNAN gives exactly CQ_FPHOST_QNAN_A, so
 * "the qNaN won" and "the first operand won" print the same bits.
 *
 * With CQ_FPHOST_QNAN_B they separate, and the answer is the SECOND reading:
 *
 *     SNAN + QNAN_B  ->  SNAN_QUIET   (NOT QNAN_B)
 *     QNAN_B + SNAN  ->  QNAN_B
 *
 * i.e. the FIRST operand wins, quietened if it was signalling. "No signalling
 * priority" is true and is not the whole rule. This is the same shape as the
 * hand-derived phase table in CLAUDE.md: four degenerate rows agreed with both
 * readings and only the fifth fixed the convention. */

/* Ten arms: the rounding mode, DAZ, FTZ, and the seven bitwise assertions
 * §7.4's five rows expand into (three INDEF cells, two qNaN orders, two
 * qNaN/sNaN orders). The number is here so a deleted arm breaks a test rather
 * than a comment. */
#define CQ_FPHOST_ARMS        10

/* Enough for the longest arm string above, which is a nan cell's. */
#define CQ_FPHOST_WHY_MAX     192

/* 1 iff every arm holds on this host, right now — the rounding mode is
 * run-time state, so the configure-time verdict is necessary and not
 * sufficient. On 0, `why` (when non-NULL and cap > 0) names the FIRST failing
 * arm; on 1 it is set to the empty string. A count is not an identification:
 * ten arms can each return 0 and only the string says which. */
int cq_fphost_check(char *why, size_t cap);

/* How many arms the last cq_fphost_check() got through — CQ_FPHOST_ARMS on a
 * conforming host, and the index of the failure otherwise. This is the ONLY
 * detector for a DELETED arm: a deleted arm still returns 1 here and is
 * invisible to the verdict, exactly as childcap's EINTR retry sites are
 * invisible to a green run without their static assert. */
int cq_fphost_arms_run(void);

/* Bit-level access to a double, for the oracle's table. memcpy, never a union
 * and never a pointer cast: the first is the only spelling that is neither
 * implementation-defined nor a strict-aliasing violation, and this project
 * compiles with -fsanitize=undefined in Debug. */
uint64_t cq_fphost_bits(double d);
double   cq_fphost_from_bits(uint64_t u);

#endif /* CQOPS_TEST_FPHOST_H */
