/* tests/support/kerneldrv.h — the shared Phase-B kernel gate.
 *
 * BEYOND PLAN §2.2's LIST OF FIVE, and it is the file plan §4 assumes without
 * naming: "Every kernel step uses the same four-part gate, APPLIED
 * AUTOMATICALLY BY THE SHARED KERNEL DRIVER rather than written per kernel."
 * Eleven kernel modules land against it (Steps 10-17), and Step 20 re-runs
 * every one of them under cq_ctrl_push — which is only "one parameter in the
 * kernel test driver, not twelve new suites" (plan §4) if the driver is one
 * object.
 *
 * WHAT IT ASSERTS, per case, all four at once:
 *
 *   L1  the VALUE of dst equals the plain-C reference. Values, never kinds.
 *   L2  the pool's live set is exactly what the operands and dst own — no
 *       leak — checked after the forward, after the uncompute AND after the
 *       free, because a leak that nets to zero passes any one of them.
 *   L3  a second call to the same kernel returns dst to zero, and freeing it
 *       restores the pool exactly. (Rule 7: uncompute IS the same kernel, so
 *       there is no separate _unc entry point to call.)
 *   L5  on the all-classical mask, zero gates and zero qubits.
 *
 * THE PRIME DIRECTIVE IS WHY ALL FOUR RUN TOGETHER. L1 alone is green through
 * a leaked ancilla; L4 alone is green through a non-cancelling sandwich with a
 * coincidentally equal total. Correctness here is L1 AND L2/L3 AND L4, never
 * any one of them. And the assertions are themselves falsifiable — see
 * tests/test_kerneldrv.c, which provokes each one and asserts it refuses.
 *
 * IT DOES NOT ASSERT BIT-KINDS ACROSS THE UNCOMPUTE AXIS (Rule 14). It does
 * assert them on the SOURCES after the forward call, which is a different
 * claim and a required one: Rule 7 says the sources come back unchanged, and
 * every K-doc restates it as "they occur only as controls and are never
 * materialised".
 *
 * TWO GENERALISATIONS LANDED AT STEP 11, both driven by kernels rather than by
 * taste. K4 needs an operand CONSTRAINED TO BE CLASSICAL (the shift amount —
 * a quantum one is M12's, not M11's). K5 is UNARY WITH TWO WIDTHS, and its
 * values run to 128 bits, because casts are the only way an i128 register
 * exists at all. Both are expressed through `cq_kd_shape` rather than by
 * forking the driver, and `cq_kernel_fn` is untouched.
 */
#ifndef CQOPS_TEST_KERNELDRV_H
#define CQOPS_TEST_KERNELDRV_H

#include "kernels/kernel.h"
#include "sink_count.h"
#include "support/bitkinds.h"
#include "support/refmodel.h"

#include <stdint.h>

/* K10's mux is the widest arity in the catalogue: cond, then two arms. */
enum { CQ_KD_MAX_SRC = 3 };

/* The per-width shape of one kernel call. `classical[i]` names the bits of
 * source i that MUST be classical — the driver clears them from every mask it
 * generates, so a constrained operand is never handed a qubit. `~0` means the
 * whole operand (K4's shift amount); 0 means unconstrained (the usual case). */
typedef struct {
    int      n_src;
    int      w[CQ_KD_MAX_SRC];
    int      w_dst;
    uint64_t classical[CQ_KD_MAX_SRC];
} cq_kd_shape;

typedef void     (*cq_kd_shape_fn)(int W, cq_kd_shape *out);
typedef void     (*cq_kd_call_fn )(cq_ctx *ctx, cq_bit *dst,
                                   const cq_bit *const *src,
                                   const cq_kd_shape *sh);
/* The N-ary reference. Values are two-word so a cast to i128 can be checked;
 * for W <= 64 the high word is always 0 and these agree with the one-word
 * functions bit for bit. */
typedef cq_ref_w (*cq_kd_refn_fn)(const cq_ref_w *src, const cq_kd_shape *sh);

/* The one-word, arity-2 reference — the shape most of the catalogue has. */
typedef uint64_t (*cq_ref_fn)(uint64_t a, uint64_t b, int W);

/* THE TRAILING THREE MEMBERS ARE OPTIONAL AND ZERO MEANS "the ordinary shape",
 * which is what keeps the Step 10 specs compiling untouched: C zero-fills
 * trailing initialisers, so `{ "xor", cq_kernel_xor, cq_ref_xor }` still means
 * arity 2, every width W, no constraint, called and referenced directly. */
typedef struct {
    const char     *name;      /* also the L4 golden's key */
    cq_kernel_fn    kernel;
    cq_ref_fn       ref;

    cq_kd_shape_fn  shape;     /* NULL: n_src 2, all widths W, no constraint */
    cq_kd_call_fn   call;      /* NULL: kernel(ctx, dst, src[0], src[1], W)   */
    cq_kd_refn_fn   refn;      /* NULL: ref(src[0].lo, src[1].lo, W)          */
} cq_kd_spec;

/* Fills `out` with the defaults for width W. Exposed so a spec's own shape
 * function can start from them. */
void cq_kd_default_shape(int W, cq_kd_shape *out);

/* One case: build the operands, run forward, check L1/L2 (and L5 if the masks
 * are all-classical), run the uncompute, check L3. Reports through the
 * harness; every failure names the kernel, W, the values and the mask. */
void cq_kd_case(const cq_kd_spec *k, int W,
                const cq_ref_w *values, const cq_bk_pair *m);

/* The arity-2, one-word convenience form the bitwise suite uses. */
void cq_kd_case2(const cq_kd_spec *k, int W,
                 uint64_t va, uint64_t vb, const cq_bk_pair *m);

/* The full L1-L3 sweep for one kernel: every (a,b) pair crossed with every
 * fixed mask pair at the exhaustive widths, then deterministic sampling at the
 * widths where exhaustion is out of reach.
 *
 * NO SILENT CAPS: cq_kd_sweep prints the exact case count it ran per width, so
 * a run that covered less than it looks like says so in its own output. */
void cq_kd_sweep(const cq_kd_spec *k);

/* The same, at one explicit width — for a kernel whose widths are not the
 * standard ladder (a cast is a PAIR of widths, so its suite drives this). */
void cq_kd_sweep_at(const cq_kd_spec *k, int W, int exhaustive);

/* L4's measurement. Runs the kernel once at the ALL-QUANTUM operand mask —
 * which is the fixed point, since with no demotion (D6) a mask can only drift
 * towards Q — and returns the forward and uncompute gate counts SEPARATELY.
 * Their equality is not an invariant and must never be asserted (Rule 14).
 * Bits a shape constrains to be classical stay classical here too. */
void cq_kd_measure(const cq_kd_spec *k, int W,
                   cq_counter *forward, cq_counter *unc);

/* Peak allocation across one call, for the "zero ancillae" claim. Returns the
 * qubits dst ends up owning; `*peak_delta` is how many were live at the high
 * water mark. A clean kernel has them equal — a kernel that took scratch and
 * tidily released it does not, and L2 cannot see the difference. */
uint32_t cq_kd_peak(const cq_kd_spec *k, int W, uint32_t *peak_delta);

#endif /* CQOPS_TEST_KERNELDRV_H */
