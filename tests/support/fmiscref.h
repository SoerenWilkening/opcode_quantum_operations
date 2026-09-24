/* tests/support/fmiscref.h — M38 K24's L1 ORACLE: IEEE 754 minNum / maxNum
 * and the three sign operations, over f64 bit patterns.
 *
 * WRITTEN FROM THE RULES, NEVER FROM fmin.jl. src/kernels/fmisc_minmax.c
 * carries `cq_fmin_eval`, a C transcription of the Julia body over `uint64_t`
 * (PRD-v2 §7.4), and that is exactly what this file must NOT be — an oracle
 * that shares a decomposition with the implementation agrees with whatever
 * that decomposition gets wrong (the Step 18 trap; PRD-v2 §7.16's decision for
 * M32, applied again). So the ordered case asks the HOST `<` — IEEE specifies
 * every comparison's result on every input, so it reads the same on x86 and on
 * arm64 (bd fcmp-oracle-is-host-independent-unlike-fadds) — and the NaN and ±0
 * rows are stated as the standard and the docstring state them:
 *
 *   IEEE 754-2008 §5.3.1 minNum/maxNum, as LLVM's `llvm.minnum` adopts it:
 *   if exactly one operand is a NaN, the result is the OTHER operand. LLVM's
 *   langref and upstream's docstring (fmin.jl:81-84, "If exactly one operand
 *   is NaN, returns the other") make no exception for a SIGNALLING NaN, and
 *   neither does this reference: an sNaN against a number returns the number,
 *   bit-exact.
 *
 *   Two extra rules are UPSTREAM'S CONTRACT and not IEEE's, and they are taken
 *   from the docstring's prose rather than from its code: both operands NaN →
 *   the CANONICAL quiet NaN 0x7FF8000000000000 (never either payload, and
 *   positive where x86's default NaN is negative); and ±0 against ±0 → the
 *   NEGATIVE zero for min if either is negative, the POSITIVE zero for max if
 *   either is positive (fmin.jl:11-15, :83, :112).
 *
 * THE SIGN OPS ARE HOST BIT ARITHMETIC, which is the whole of their meaning:
 * IEEE 754 §5.5.1 defines negate, abs and copySign as quiet operations on the
 * sign bit alone — NaNs included, payload and signalling-ness preserved. */
#ifndef CQOPS_TEST_FMISCREF_H
#define CQOPS_TEST_FMISCREF_H

#include <stdint.h>

uint64_t cq_ref_fmin(uint64_t a, uint64_t b);
uint64_t cq_ref_fmax(uint64_t a, uint64_t b);

uint64_t cq_ref_fneg(uint64_t a);
uint64_t cq_ref_fabs(uint64_t a);
uint64_t cq_ref_copysign(uint64_t x, uint64_t y);

/* The canonical NaN of the both-NaN row, named so a test can assert it is
 * NOT the x86 default NaN (0xFFF8…) and not a payload. */
#define CQ_REF_FMINMAX_CANON_NAN  UINT64_C(0x7FF8000000000000)

#endif /* CQOPS_TEST_FMISCREF_H */
