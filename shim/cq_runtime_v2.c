/* shim/cq_runtime_v2.c — M26's V1 BOUNDARY, Step 23 landing 1 step 5. The 109
 * `cqrt_*` symbols libcqops COULD serve but v1 defers: 34 fp-width core
 * symbols, 63 `qram`, ~~11 `tape`~~ and `cqrt_alloc_handle`. **98 since
 * 2026-09-02** — the 11 tape bodies moved into scope (PRD §15 D23,
 * shim/cq_runtime_tape.c); **35 since later that day** — PRD §15 D24 moved the
 * 63 qram bodies into shim/cq_runtime_qram.c. Every "109" and "74" below is the
 * 2026-08-27 figure.
 *
 * PRD §15 D16 (bd vxk, bd r3y, bd ck6), and the discriminator is CAPABILITY
 * rather than liveness. libcqops DEFINES every `cqrt_*` it could serve —
 * implemented where v1 is in scope, a loud `cq_shim_unsupported` naming the
 * symbol where v1 defers — and leaves UNDEFINED only what it could not serve at
 * any point in v1. That is exactly `cqrt_h` and `cqrt_h_controlled`: Rule 4
 * forbids H on the classical path and PRD §8's vtable is frozen at six entries
 * with no `h` slot. "Is it minted by the pass?" — which PRD §1 and
 * docs/cqrt_census.txt both implied — is measurably the WRONG discriminator: it
 * would also strike `cqrt_x_controlled` and `cqrt_cnot_controlled`, which are
 * literally a CX and a CCX and which ship next door.
 *
 * THE GROUND IS LINKAGE, NOT SCOPE, AND THE TWO DO NOT AGREE. PRD §1 alone
 * would license leaving all 109 undefined — fp, qram and tape are out of v1
 * either way. Linkage forbids it: `libcq_runtime.a` is ONE object holding all
 * 173 `cqrt_*` as `T`, so any undefined REFERENCED symbol pulls the whole
 * member, and that produces either a duplicate-symbol wall naming
 * `_cqrt_alloc_i32` and `_cqrt_free` — symbols we implement CORRECTLY — with
 * the culprit named nowhere, or, worse and measured, a SILENT successful link
 * in which CQ_lang's trace-only stub serves every call. Defining them makes
 * `libcq_runtime.a` inert under BOTH candidate link lines, so this does not
 * wait on `bd 590`. The two `cqrt_h*` omissions are safe on the other half of
 * the same fact: nothing references them, measured with real link experiments
 * under both link lines.
 *
 * WHY AN ABORT AND NOT A RAIL, for `cqrt_alloc_handle` (bd ck6, resolved by
 * measurement 2026-08-22): the rail would be a LIE at any width.
 * `runtime/cq_intrinsic_templates.c` is a trace-only stub — every minting body
 * is `int32_t h = cqrt_alloc_handle(); printf(...); return h;` and NO GATE IS
 * EMITTED ANYWHERE IN THAT TU — so minting a real rail hands back something
 * nothing computed, with a plausible trace and a wrong value in both
 * configurations. That is the Prime Directive's clean-trace / wrong-circuit
 * signature in its purest form. A SECOND counter is worse still: src/reg.c
 * makes `t->n` simultaneously the D5 monotonic handle and the dense array
 * index, so CQ_lang's own numbering would collide with ours and a later
 * `cqrt_measure` would read the wrong rail with no abort.
 *
 * AND THE ABORT CANNOT NAME ITS CALLER. `int32_t cqrt_alloc_handle(void)`
 * carries no width, no type and no operand, so the reason says "a CQ_lang
 * intrinsic or libm template minted a handle" rather than pretending to
 * identify the symbol that called it. Do NOT "repair" the six marginal fixtures
 * by keeping our numbering in step without creating a slot: every later
 * `cqrt_free` / `cqrt_measure` on that handle then hits M07's "handle out of
 * range" — loud, but blaming the handle table for a scope decision.
 *
 * THE BUCKETS ARE BY FAMILY FIRST AND BY WIDTH SECOND, which is why
 * `cqrt_qram_alloc_f32` says QRAM and not FP. The 63 qram symbols are
 * undecidable for libcqops at EVERY width — there is no addressable quantum
 * array in this library at i1 either — so the fp width is not what defers them.
 * This is NOT in tension with `bd 819`'s name rule ("a symbol is fp-touching
 * iff its name carries an f16/f32/f64/f80 token"): that rule partitions the
 * 2,479 `cq_template_*` names expanded from `opcode_table.yaml`, a different
 * population reached through a different header. `bd vxk`'s measured trap is
 * about FIXTURE attribution, not symbol attribution — 21 of the 33 fixtures
 * that reach qram before any fp call hit `cqrt_qram_alloc_f{32,64,80}` FIRST,
 * so a test that wants to prove the qram bucket exists must drive an INTEGER
 * width or it pins a message the fp bucket would have printed anyway.
 *
 * RULE 12. Budget 150. Split seam, RECORDED IN IMPLEMENTATION_PLAN §3 BEFORE
 * THIS FILE WAS WRITTEN because `bd r3y` measured the file over budget with no
 * seam owned by anyone:
 *
 *     the DEFERRED CORE WIDTHS <-> the ADDRESSABLE-MEMORY families
 *                                          ->  shim/cq_runtime_v2_mem.c
 *
 * The 34 fp symbols are the SHIPPED families at a width v1 defers, one width
 * token away from cq_runtime_rail.c and cq_runtime_gate.c, and in v2 they
 * become real code by WIDENING what exists. The 74 qram + tape symbols are a
 * different DATA MODEL and need a subsystem. `cqrt_alloc_handle` stays with the
 * fp half: it is about the shared D5 counter, and it is the one body here that
 * is REACHABLE today. Trigger 240, the house threshold.
 */

#include "cq_runtime_abi.h"

#include "cq_shim.h"

#include <stdint.h>

/* The reason strings — two since D24 retired `qram is v2` (three after D23). `"fp is v2"` is
 * VERBATIM the bucket string M28's generated bodies already use (shim/cq_shim.h),
 * because it is the same reason; the other two are what `bd vxk`'s "a third
 * reason string" asked for. They are pinned literally, per symbol, in
 * tests/test_runtime_v2.c. */
static const char *const V2_FP     = "fp is v2";
static const char *const V2_HANDLE = "a CQ_lang intrinsic or libm template "
                                     "minted a handle; libcqops cannot mint a "
                                     "register-less handle without diverging "
                                     "the shared D5 counter (bd ck6)";

/* --- The 34 fp-width core symbols ---------------------------------------- */

/* Every parameter is `(void)`-cast although `cq_shim_unsupported` is _Noreturn:
 * -Wunused-parameter is a declaration-level diagnostic and does not care that
 * the function cannot return. The _Noreturn is what lets a value-returning body
 * carry no `return` at all. */
#define CQ_V2_FP_WIDTH(W, VT)                                                 \
    int32_t cqrt_alloc_f##W(VT value)                                         \
    { (void)value; cq_shim_unsupported("cqrt_alloc_f" #W, V2_FP); }           \
    VT cqrt_measure_f##W(int32_t handle)                                      \
    { (void)handle; cq_shim_unsupported("cqrt_measure_f" #W, V2_FP); }        \
    void cqrt_ry_f##W(int32_t handle, double angle)                           \
    { (void)handle; (void)angle;                                              \
      cq_shim_unsupported("cqrt_ry_f" #W, V2_FP); }                           \
    void cqrt_rz_f##W(int32_t handle, double angle)                           \
    { (void)handle; (void)angle;                                              \
      cq_shim_unsupported("cqrt_rz_f" #W, V2_FP); }                           \
    void cqrt_rz_f##W##_controlled(int32_t ctrl, int32_t handle, double angle)\
    { (void)ctrl; (void)handle; (void)angle;                                  \
      cq_shim_unsupported("cqrt_rz_f" #W "_controlled", V2_FP); }             \
    void cqrt_rz_f##W##_controlled_inv(int32_t ctrl, int32_t handle,          \
                                       double angle)                          \
    { (void)ctrl; (void)handle; (void)angle;                                  \
      cq_shim_unsupported("cqrt_rz_f" #W "_controlled_inv", V2_FP); }         \
    void cqrt_copy_f##W(int32_t src, int32_t dst)                             \
    { (void)src; (void)dst;                                                   \
      cq_shim_unsupported("cqrt_copy_f" #W, V2_FP); }                         \
    void cqrt_copy_f##W##_controlled(int32_t ctrl, int32_t src, int32_t dst)  \
    { (void)ctrl; (void)src; (void)dst;                                       \
      cq_shim_unsupported("cqrt_copy_f" #W "_controlled", V2_FP); }

CQ_V2_FP_WIDTH(16, _Float16)
CQ_V2_FP_WIDTH(32, float)
CQ_V2_FP_WIDTH(64, double)
CQ_V2_FP_WIDTH(80, long double)

/* THE Ry AXIS IS 6 HERE AND NOT 8, AND IT IS STILL NOT AN OMISSION. It was 2
 * until 2026-09-10: `cqrt_ry_<W>_controlled` did not exist upstream at any width
 * until CQ_lang `f92d95e` (2026-09-02), re-vendored at `170ede1` (`bd w9i`).
 * The FORWARD half is now all four fp widths; the `_inv` half is still f32 and
 * f64 ONLY, missing f16 and f80. A uniform 9 x {controlled, controlled_inv}
 * cross product is STILL WRONG and would mint two symbols the frozen ABI does
 * not declare: measured from the declarations at `170ede1`, Ry is
 * 9 forward + 9 controlled + 7 controlled_inv = 25, Rz is 9 + 9 + 9 = 27.
 *
 * WHY THE FOUR FORWARDS ARE ABORTS WHILE THE FIVE INTEGER ONES ARE IMPLEMENTED
 * — PRD §15 D16's capability rule, family first and width second. The family
 * `ry` is in v1 scope, so the five integer widths are real bodies in
 * `cq_runtime_gate.c`. The width is what defers these four, exactly as it
 * defers `cqrt_ry_f<W>` one block up.
 *
 * AND HERE THE DEFERRAL IS UNREACHABILITY, NOT PREFERENCE, WHICH IS THE PART
 * WORTH WRITING DOWN. `cqrt_alloc_f<W>` aborts, so no fp rail handle can exist
 * at runtime in v1 — there is nothing to hand these. An implemented body would
 * call `cq_rotate_ry`, which resolves the handle through M07 and would land on
 * M07's GENERIC handle error instead of `cq_shim_unsupported`'s named-symbol
 * one: strictly worse diagnostics, a forward implemented where its own `_inv`
 * is not, and the only implemented fp rotation of any kind. The abort's reason
 * string is `"fp is v2"`, which is precisely the claim. */
void cqrt_ry_f32_controlled(int32_t ctrl, int32_t handle, double angle)
{ (void)ctrl; (void)handle; (void)angle;
  cq_shim_unsupported("cqrt_ry_f32_controlled", V2_FP); }

void cqrt_ry_f64_controlled(int32_t ctrl, int32_t handle, double angle)
{ (void)ctrl; (void)handle; (void)angle;
  cq_shim_unsupported("cqrt_ry_f64_controlled", V2_FP); }

void cqrt_ry_f16_controlled(int32_t ctrl, int32_t handle, double angle)
{ (void)ctrl; (void)handle; (void)angle;
  cq_shim_unsupported("cqrt_ry_f16_controlled", V2_FP); }

void cqrt_ry_f80_controlled(int32_t ctrl, int32_t handle, double angle)
{ (void)ctrl; (void)handle; (void)angle;
  cq_shim_unsupported("cqrt_ry_f80_controlled", V2_FP); }

void cqrt_ry_f32_controlled_inv(int32_t ctrl, int32_t handle, double angle)
{ (void)ctrl; (void)handle; (void)angle;
  cq_shim_unsupported("cqrt_ry_f32_controlled_inv", V2_FP); }

void cqrt_ry_f64_controlled_inv(int32_t ctrl, int32_t handle, double angle)
{ (void)ctrl; (void)handle; (void)angle;
  cq_shim_unsupported("cqrt_ry_f64_controlled_inv", V2_FP); }

/* --- The 63 qram symbols: GONE, 2026-09-02 (PRD §15 D24) ----------------- */
/* They live in shim/cq_runtime_qram.c as v1.2 — D23's token plus `count`
 * registers, Bennett's unary-iteration tree for the read and his shadow store
 * under it for the write. This file's population is 35: the 34 fp-width core
 * symbols and `cqrt_alloc_handle`. The `V2_QRAM` reason string is gone with
 * them, and the ADDRESSABLE-MEMORY side of the recorded seam below is empty. */

/* --- The 11 tape symbols: GONE, 2026-09-02 (PRD §15 D23) ------------------ */

/* They live in shim/cq_runtime_tape.c as v1.1. §1's "no consumer until printf
 * on tainted data" was measured false at Step 24 — six shipped fixtures ARE
 * that consumer — and the cost was the copy's: a tape write is `cqrt_copy`
 * into a kept rail, and the tape handle is a zero-qubit token (plan §0.5).
 * This file's population is 98. */

/* --- cqrt_alloc_handle, which is not an ordinary member of this file ------- */

/* THE ONE BODY HERE THAT IS REACHABLE TODAY. The other 108 are called only by
 * fixtures libcqops has already declined; this one is referenced by CQ_lang's
 * OWN template archives — 1830 references in runtime/cq_templates.c, 202 in
 * cq_intrinsic_templates.c, 6 in cq_libm_templates.c — and is never emitted by
 * the IR pass at all. Our 2,479 generated `.gen.c` bodies call it ZERO times,
 * which is why Step 23's own gate does not need it; it bites at Step 24.
 *
 * COST, measured: 149 fixtures abort at their first intrinsic template call,
 * 143 of which ALREADY die on fp, so the MARGINAL set is 6 — and none of the
 * six was ever runnable correctly, because the intrinsic body emits no circuit.
 * Whether Step 24's gate must include them is `bd 590`'s question. */
int32_t cqrt_alloc_handle(void)
{ cq_shim_unsupported("cqrt_alloc_handle", V2_HANDLE); }
