/* tests/support/kerneldrv.h — the shared Phase-B kernel gate.
 *
 * BEYOND PLAN §2.2's LIST OF FIVE, and it is the file plan §4 assumes without
 * naming: "Every kernel step uses the same four-part gate, APPLIED
 * AUTOMATICALLY BY THE SHARED KERNEL DRIVER rather than written per kernel."
 * TEN kernel modules land against it, not eleven, and the exception is the
 * interesting part. M10-M14 and M16-M20 do; M15 does NOT, and cannot. Cuccaro
 * is `acc += b` — in place, destructive in its first operand and transiently in
 * its second — and this driver hard-codes Rule 7's contract in three
 * independent places: cq_kd_case mints `dst` as a fresh separate zero register,
 * asserts every source unchanged in VALUE AND KIND after the forward, and
 * implements L3 as a SECOND CALL required to return dst to zero (which for K8
 * gives `acc + 2b`). cq_kd_spec has no member that could express any of it. So
 * tests/test_kernel_addacc.c restates the levels by hand and its L3 is a
 * descending-index replay — which is how K11's sandwich will undo it anyway.
 * The count in this header said eleven until Step 15 measured otherwise.
 *
 * Step 20 re-runs every kernel that DOES land here under cq_ctrl_push — which
 * is only "one parameter in the kernel test driver, not twelve new suites"
 * (plan §4) if the driver is one object.
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

/* --- Step 20: the whole of "one parameter, not twelve new suites". --------
 *
 * PRD §9's controlled axis is an EMITTER MODE, so a kernel needs no change to
 * become controlled and neither does its spec. What the driver needs is a
 * control rail to push, and one place to push it: `cq_kd_call_kernel`, which
 * is the sole route from all five kernel invocations in the driver — two in
 * cq_kd_case, three in kernelmeasure.c — to a kernel (kernelfix.h). Setting
 * the mode is therefore a driver-scoped SETTER rather than a parameter — there
 * are 71 entry-point call sites across twelve .c files and eight .inc files,
 * and none of them changes.
 *
 * FIVE MODES, BECAUSE §9 ROW 0 HAS THREE ROWS AND THE QUANTUM ONE HAS TWO
 * BRANCHES. The two Q modes emit the IDENTICAL circuit — the fold table reads
 * kind and never shadow (D6) — and differ only in what the answer is, which is
 * Bennett's `controlled()` contract: `(ctrl, x, 0) -> (ctrl, x, ctrl ? f(x) : 0)`.
 *
 * CQ_KD_CTRL_Q0 IS THE SHARPEST OF THE FIVE and it is not a formality. At an
 * all-classical operand mask it is the only fixture in the project that can see
 * a controlled region silently made unconditional: the kernel's L5 short-circuit
 * writes `dst` through `cq_emit_x`, whose constant row rewrites a bit in place
 * for zero gates, and if M06 did not intercept that row the rail would be
 * flipped on BOTH branches — value `f(a,b)` where the answer is 0. Nothing at
 * the all-quantum mask can reach that row, and no gate count can see it.
 *
 * NONE IS ZERO so the zero-initialised default is exactly the behaviour every
 * suite had before Step 20, and the numbering is pinned rather than commented,
 * on CQ_BIT_ZERO's and CQ_ANGLE_GENERAL's precedent. */
typedef enum {
    CQ_KD_CTRL_NONE = 0,   /* no region at all                              */
    CQ_KD_CTRL_ZERO,       /* CQ_BIT_ZERO: row 0 skips — 0 gates, 0 qubits  */
    CQ_KD_CTRL_ONE,        /* CQ_BIT_ONE:  row 0 emits it verbatim          */
    CQ_KD_CTRL_Q0,         /* a wire whose shadow is 0: promoted, dst stays */
    CQ_KD_CTRL_Q1          /* a wire whose shadow is 1: promoted, dst = f   */
} cq_kd_ctrl;

_Static_assert(CQ_KD_CTRL_NONE == 0,
               "a zero-initialised driver runs uncontrolled, as it did before "
               "Step 20");

void        cq_kd_set_ctrl(cq_kd_ctrl mode);
cq_kd_ctrl  cq_kd_get_ctrl(void);
const char *cq_kd_ctrl_name(cq_kd_ctrl mode);


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

/* THE L1 SAMPLE BUDGET: how many cases one kernel gets at one width. A SMALL
 * CONSTANT, independent of W and of the kernel — the sweep is a sample, never a
 * product. Default 32; override with CQOPS_L1_SAMPLES in the environment for a
 * mutation battery or a bisect, and never from a test's CMake ENVIRONMENT
 * property, which would win over the shell. See kernelsweep.c for what the
 * constant replaced and what it gave up. */
int cq_kd_samples(void);

/* One kernel at one width: cq_kd_samples() cases, each drawing a mask pair AND
 * a value pair from one seeded RNG. The all-classical pair (which IS L5), the
 * all-quantum pair (which is what L4 pins) and the four value corners are taken
 * first, INSIDE the budget rather than on top of it.
 *
 * Serves every arity and every operand width from the spec's own shape, so a
 * three-source kernel needs no bespoke driver: the old cq_kd_case2 path filled
 * values[2] with ZERO and ran every mux case with one arm pinned at 0. */
void cq_kd_sample_at(const cq_kd_spec *k, int W);

/* The whole L1-L3 sweep for one kernel: cq_kd_sample_at over the standard
 * width ladder {1,2,3,4,5,8,16,32,64}. Widths are enumerated, not sampled —
 * see kernelsweep.c.
 *
 * NO SILENT CAPS: every width prints the case count, the mask-pair pool it drew
 * from and its seed, so a run that covered less than it looks like says so in
 * its own output. */
void cq_kd_sweep(const cq_kd_spec *k);

/* The same at one explicit width, for a kernel whose widths are not the
 * standard ladder (a cast is a PAIR of widths, so its suite drives this).
 *
 * `exhaustive` IS IGNORED and kept only so the 71 existing call sites compile:
 * there is no exhaustive mode any more, at any width. */
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
/* THE STEP-20 GATE, AND IT TAKES THE SUITE'S OWN SWEEP RATHER THAN IMPOSING
 * ONE. Runs `body` four times, once under each of §9's regions, printing which.
 *
 * A fixed shape would have been wrong for a third of the catalogue and wrong
 * SILENTLY: a cast's sweep is over a width PAIR that its own spec adapter
 * reads, and K10's mux must drive cq_kd_case directly because cq_kd_case2 fills
 * values[2] with zero — so a mux swept "the ordinary way" runs every case with
 * one arm pinned at 0 and prints a six-figure case count for half a kernel.
 * Passing the suite's own body keeps each of those exactly as it already is.
 *
 * `body` should be the suite's sweep at its CHEAP widths only. The promotion is
 * a property of the emitter — per gate, and width-independent — so what the
 * axis adds is its interaction with the §3 fold table — a property of the
 * bit-KIND space (D6: the table reads kind, never value), densest at narrow
 * widths, where cq_kd_samples() draws cover most of the mask-pair pool; nothing
 * has been exhaustive since 2026-08-21. Every shipped width is still covered,
 * by cq_kd_check_promotion below, at two kernel calls apiece. */
void cq_kd_for_each_region(const char *what, void (*body)(void));

/* §9's promotion maps the by-kind tuple exactly — X→CX, CX→CCX, CCX→3 CCX — so
 * at the ALL-QUANTUM mask `(x, cx, ccx)` becomes `(0, x, cx + 3·ccx)`. Asserted
 * against the SAME RUN's uncontrolled measurement, so it reads no golden and
 * `CQOPS_UPDATE_GOLDENS=1` cannot bless a promotion that stopped promoting.
 *
 * Scoped to the all-quantum mask, and there only: away from it a gate that
 * folded to nothing uncontrolled can emit under promotion. That is the same
 * mask every L4 golden is pinned at.
 *
 * RETURNS THE UNCONTROLLED FORWARD TOTAL, and a caller that ignores it can go
 * silently vacuous. Zero is not a defect — measure_setup drives every operand
 * all-ones, and for K4 that saturates under D8 at every non-power-of-two width
 * (the amount is masked to ceil(log2 W) bits, so W = 3, 5 and 80 shift by more
 * than W and `shl`/`lshr` emit nothing at all). The caller therefore owns the
 * non-vacuity claim: sum these over the ladder and assert it is non-zero. */
uint64_t cq_kd_check_promotion(const cq_kd_spec *k, int W);

#endif /* CQOPS_TEST_KERNELDRV_H */
