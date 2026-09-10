/* shim/cq_runtime_abi.h — CQ_lang's 182 `cqrt_*` declarations, VERBATIM.
 *
 * WHY A HEADER OF OUR OWN RATHER THAN CQ_lang's. Nothing in this repository
 * includes anything from the CQ_lang tree, and Step 22 already answered the
 * same question the same way for the OTHER frozen ABI: `tests/abi/
 * cq_templates_abi.txt` vendors CQ_lang's 2479 `cq_template_*` declarations
 * rather than reading them live, because CQ_lang is UNPINNED — its HEAD has
 * already moved past `third_party/cq_lang/COMMIT`. This is the `cqrt_*` half
 * of that posture. It is under `shim/` and not under `tests/abi/` because the
 * LIBRARY includes it: `cq_runtime_rail.c` and `cq_runtime_gate.c` define 62 of
 * these symbols and a test directory is not on the library's include path.
 *
 * WHAT IT BUYS, AND IT IS NOT DECORATION. A `cqrt_*` DEFINITION needs no prior
 * declaration and `-Wmissing-prototypes` is not in this project's flag set
 * (CMakeLists.txt), so without this header a wrong return type or a wrong
 * parameter type in a definition compiles EXIT 0 clean under `-Wall -Wextra
 * -Werror -Wconversion` and is not a link error either, because C has no name
 * mangling. Measured: a file defining `int32_t cqrt_measure_i8(int32_t)` builds
 * clean on its own and fails with `conflicting types` the moment these
 * declarations are in scope. Every M26 file that DEFINES a `cqrt_*` symbol
 * therefore includes this first; `shim/cq_shim_ctx.c`, which defines none, does
 * not and does not need to.
 *
 * WHAT IT DOES NOT BUY, SAID PLAINLY. C ignores parameter NAMES, so a
 * transposition of two same-typed parameters is invisible to it — and this ABI
 * has exactly that trap: `cqrt_copy_<W>(src, dst)` is the reverse of
 * `cq_reg_xor_into(ctx, dst, src)` (src/reg.h says so in as many words), and
 * both are `int32_t`. That one is pinned BEHAVIOURALLY, in
 * tests/test_runtime_rail.c, by asserting which rail gained the wires.
 *
 * PROVENANCE, and the drift check that is NOT here.
 *   source        : CQ_lang runtime/cq_runtime.h (hand-written, not generated)
 *   sha256        : f7c44636d1adea8a094aac7cd87bbb8e38e5875074ed25c809d3fd5e67792da1
 *   last commit to that file : f92d95ea6996b54333a6bfe86ead3a3d60e4f14e (2026-09-02)
 *   CQ_lang HEAD when vendored : 170ede1a0170dfeb9ec6aba5fceeeeda3d61f494 (2026-09-10)
 *   extraction    : comment-stripped identifier extraction, NOT a line-anchored
 *                   grep — that header is column-aligned and the obvious regex
 *                   silently drops declarations (49 of 173 as measured at the
 *                   2026-08-23 vendor and recorded in docs/cqrt_census.txt;
 *                   re-measured at this one, `^void cqrt_|^int32_t cqrt_`
 *                   returns 174 of 182).
 *   verified      : the extraction was compiled TOGETHER WITH the source header
 *                   and produced zero `conflicting types`; a negative control
 *                   (one parameter widened from int8_t to int16_t) produced one.
 *                   So this is a verified copy, not a transcription.
 *
 * WHAT GUARDS THE COPY, exactly, because the first draft of this paragraph
 * claimed more than the tree delivered. `tests/test_runtime_abi.inc` compares
 * this file against `docs/cqrt_census.txt` — an enumeration produced from
 * CQ_lang at Step 0.3 by a different route — in two ways: the eight per-owner
 * declaration COUNTS against PART D's family x width grid, and the NAME SET,
 * both directions, over the 65 v1 symbols the census lists verbatim. The name
 * half was missing at first and the gap was not theoretical: during this step's
 * review a stray rename of `cqrt_xorc_i16` kept every count exact and left the
 * whole suite green while the definition of that symbol lost the only prototype
 * that type-checks it.
 *
 * WHAT STILL GUARDS NOTHING: a transposition of two same-typed parameters, which
 * C cannot see at all — pinned behaviourally in `tests/test_runtime_rail_write.inc`
 * for the one place it bites (`cqrt_copy_<W>(src, dst)`) — and the 108 v2 names,
 * which the census does not list verbatim and which Step 23.8's link witness
 * covers (`bd 216` step 8, "table 2 over the 171 `cqrt_*` from
 * docs/cqrt_census.txt"). A re-vendoring must move the four provenance lines
 * above with the bytes.
 *
 * RE-VENDORED 2026-09-10 AT CQ_lang `170ede1`, AND THE DELTA IS PURELY ADDITIVE
 * (`bd w9i`). 173 -> 182: the nine `cqrt_ry_<W>_controlled`, added upstream by
 * `f92d95e` (2026-09-02) — five integer widths, four fp. **Not one of the
 * existing 173 changed signature**, measured as nine pure insertions in the
 * declaration diff, and `third_party/cq_lang/opcode_table.yaml` is BYTE-
 * IDENTICAL at this revision (sha256 `6245117d…a3e82426`), so the 2,479
 * `cq_template_*` grid did not move and `third_party/` was not touched. The
 * disposition is PRD §15 D16's capability rule: the five integer widths are
 * IMPLEMENTED in `cq_runtime_gate.c` exactly as `cqrt_ry_<W>_controlled_inv`
 * is (the same body without its `-angle`), and the four fp widths join the fp
 * abort bucket — where `cqrt_ry_f<W>`, `cqrt_ry_f{32,64}_controlled_inv` and
 * `cqrt_rz_f<W>_controlled` already sit, and which `cqrt_alloc_f<W>`'s own
 * abort makes UNREACHABLE rather than merely deferred.
 *
 * IT MAKES `_Float16` AND `long double` A BUILD REQUIREMENT, and that is new.
 * Four of the deferred declarations use them, unguarded, exactly as CQ_lang's
 * header does — so this is inherited faithfully rather than mistranscribed, but
 * libcqops had no `_Float16` anywhere before Step 23.4 and now every M26
 * translation unit and both new test binaries see one. PRD §14's "nothing beyond
 * libc" is intact (these are C11 and a compiler extension, not a library), but
 * the toolchain floor moved: `_Float16` is a clang-15+/GCC-12+ x86-64 feature
 * and does not exist on every target. It is not guarded here because a guard
 * would be a divergence from a verbatim copy, and the copy's fidelity is the
 * whole of its authority; if a CI target ever lacks it, the guard goes here with
 * a note saying it is the one deliberate divergence.
 *
 * NOTE THE TWO NAMES THAT ARE DECLARED HERE AND DEFINED NOWHERE, deliberately:
 * `cqrt_h` and `cqrt_h_controlled` (PRD SS1, PRD SS15 D16). Declaring an
 * undefined symbol produces no undefined reference; that is the whole of why
 * omission is free for them and only for them.
 */
#ifndef CQ_RUNTIME_ABI_H
#define CQ_RUNTIME_ABI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Rail lifecycle — `cq_runtime_rail.c` (Step 23.4) --- */
int32_t cqrt_alloc_i1 (bool value);
int32_t cqrt_alloc_i8 (int8_t value);
int32_t cqrt_alloc_i16(int16_t value);
int32_t cqrt_alloc_i32(int32_t value);
int32_t cqrt_alloc_i64(int64_t value);
bool cqrt_measure_i1 (int32_t handle);
int8_t cqrt_measure_i8 (int32_t handle);
int16_t cqrt_measure_i16(int32_t handle);
int32_t cqrt_measure_i32(int32_t handle);
int64_t cqrt_measure_i64(int32_t handle);
void cqrt_free(int32_t handle);

/* --- Rail contents — `cq_runtime_rail.c` (Step 23.4) --- */
void cqrt_cswap(int32_t ctrl, int32_t a, int32_t b);
void cqrt_copy_i1 (int32_t src, int32_t dst);
void cqrt_copy_i8 (int32_t src, int32_t dst);
void cqrt_copy_i16(int32_t src, int32_t dst);
void cqrt_copy_i32(int32_t src, int32_t dst);
void cqrt_copy_i64(int32_t src, int32_t dst);
void cqrt_copy_i1_controlled (int32_t ctrl, int32_t src, int32_t dst);
void cqrt_copy_i8_controlled (int32_t ctrl, int32_t src, int32_t dst);
void cqrt_copy_i16_controlled(int32_t ctrl, int32_t src, int32_t dst);
void cqrt_copy_i32_controlled(int32_t ctrl, int32_t src, int32_t dst);
void cqrt_copy_i64_controlled(int32_t ctrl, int32_t src, int32_t dst);
void cqrt_addc_i1 (int32_t h, bool imm);
void cqrt_addc_i8 (int32_t h, int8_t imm);
void cqrt_addc_i16(int32_t h, int16_t imm);
void cqrt_addc_i32(int32_t h, int32_t imm);
void cqrt_addc_i64(int32_t h, int64_t imm);
void cqrt_xorc_i1 (int32_t h, bool imm);
void cqrt_xorc_i8 (int32_t h, int8_t imm);
void cqrt_xorc_i16(int32_t h, int16_t imm);
void cqrt_xorc_i32(int32_t h, int32_t imm);
void cqrt_xorc_i64(int32_t h, int64_t imm);

/* --- Gate surface — `cq_runtime_gate.c` (Step 23.4) --- */
void cqrt_ry_i1 (int32_t handle, double angle);
void cqrt_ry_i8 (int32_t handle, double angle);
void cqrt_ry_i16(int32_t handle, double angle);
void cqrt_ry_i32(int32_t handle, double angle);
void cqrt_ry_i64(int32_t handle, double angle);
void cqrt_rz_i1 (int32_t handle, double angle);
void cqrt_rz_i8 (int32_t handle, double angle);
void cqrt_rz_i16(int32_t handle, double angle);
void cqrt_rz_i32(int32_t handle, double angle);
void cqrt_rz_i64(int32_t handle, double angle);
void cqrt_rz_i1_controlled (int32_t ctrl, int32_t handle, double angle);
void cqrt_rz_i8_controlled (int32_t ctrl, int32_t handle, double angle);
void cqrt_rz_i16_controlled(int32_t ctrl, int32_t handle, double angle);
void cqrt_rz_i32_controlled(int32_t ctrl, int32_t handle, double angle);
void cqrt_rz_i64_controlled(int32_t ctrl, int32_t handle, double angle);
void cqrt_rz_i1_controlled_inv (int32_t ctrl, int32_t handle, double angle);
void cqrt_rz_i8_controlled_inv (int32_t ctrl, int32_t handle, double angle);
void cqrt_rz_i16_controlled_inv(int32_t ctrl, int32_t handle, double angle);
void cqrt_rz_i32_controlled_inv(int32_t ctrl, int32_t handle, double angle);
void cqrt_rz_i64_controlled_inv(int32_t ctrl, int32_t handle, double angle);
void cqrt_ry_i1_controlled (int32_t ctrl, int32_t handle, double angle);
void cqrt_ry_i8_controlled (int32_t ctrl, int32_t handle, double angle);
void cqrt_ry_i16_controlled(int32_t ctrl, int32_t handle, double angle);
void cqrt_ry_i32_controlled(int32_t ctrl, int32_t handle, double angle);
void cqrt_ry_i64_controlled(int32_t ctrl, int32_t handle, double angle);
void cqrt_ry_i1_controlled_inv (int32_t ctrl, int32_t handle, double angle);
void cqrt_ry_i8_controlled_inv (int32_t ctrl, int32_t handle, double angle);
void cqrt_ry_i16_controlled_inv(int32_t ctrl, int32_t handle, double angle);
void cqrt_ry_i32_controlled_inv(int32_t ctrl, int32_t handle, double angle);
void cqrt_ry_i64_controlled_inv(int32_t ctrl, int32_t handle, double angle);
void cqrt_x (int32_t q);
void cqrt_cnot (int32_t ctrl, int32_t tgt);
void cqrt_toffoli(int32_t c1, int32_t c2, int32_t tgt);
void cqrt_x_controlled (int32_t ctrl, int32_t q);
void cqrt_cnot_controlled(int32_t ctrl, int32_t target_ctrl, int32_t tgt);

/* --- Deferred to v2 — fp widths (38) — `cq_runtime_v2.c` (Step 23.5) --- */
int32_t cqrt_alloc_f32(float value);
int32_t cqrt_alloc_f64(double value);
int32_t cqrt_alloc_f16(_Float16 value);
int32_t cqrt_alloc_f80 (long double value);
float cqrt_measure_f32(int32_t handle);
double cqrt_measure_f64(int32_t handle);
_Float16 cqrt_measure_f16(int32_t handle);
long double cqrt_measure_f80 (int32_t handle);
void cqrt_ry_f32(int32_t handle, double angle);
void cqrt_ry_f64(int32_t handle, double angle);
void cqrt_ry_f16(int32_t handle, double angle);
void cqrt_ry_f80 (int32_t handle, double angle);
void cqrt_rz_f32(int32_t handle, double angle);
void cqrt_rz_f64(int32_t handle, double angle);
void cqrt_rz_f16(int32_t handle, double angle);
void cqrt_rz_f80 (int32_t handle, double angle);
void cqrt_rz_f32_controlled(int32_t ctrl, int32_t handle, double angle);
void cqrt_rz_f64_controlled(int32_t ctrl, int32_t handle, double angle);
void cqrt_rz_f16_controlled(int32_t ctrl, int32_t handle, double angle);
void cqrt_rz_f80_controlled (int32_t ctrl, int32_t handle, double angle);
void cqrt_rz_f32_controlled_inv(int32_t ctrl, int32_t handle, double angle);
void cqrt_rz_f64_controlled_inv(int32_t ctrl, int32_t handle, double angle);
void cqrt_rz_f16_controlled_inv(int32_t ctrl, int32_t handle, double angle);
void cqrt_rz_f80_controlled_inv (int32_t ctrl, int32_t handle, double angle);
void cqrt_ry_f32_controlled(int32_t ctrl, int32_t handle, double angle);
void cqrt_ry_f64_controlled(int32_t ctrl, int32_t handle, double angle);
void cqrt_ry_f16_controlled(int32_t ctrl, int32_t handle, double angle);
void cqrt_ry_f80_controlled (int32_t ctrl, int32_t handle, double angle);
void cqrt_ry_f32_controlled_inv(int32_t ctrl, int32_t handle, double angle);
void cqrt_ry_f64_controlled_inv(int32_t ctrl, int32_t handle, double angle);
void cqrt_copy_f16(int32_t src, int32_t dst);
void cqrt_copy_f32(int32_t src, int32_t dst);
void cqrt_copy_f64(int32_t src, int32_t dst);
void cqrt_copy_f80(int32_t src, int32_t dst);
void cqrt_copy_f16_controlled(int32_t ctrl, int32_t src, int32_t dst);
void cqrt_copy_f32_controlled(int32_t ctrl, int32_t src, int32_t dst);
void cqrt_copy_f64_controlled(int32_t ctrl, int32_t src, int32_t dst);
void cqrt_copy_f80_controlled(int32_t ctrl, int32_t src, int32_t dst);

/* --- Deferred to v2 — qram (63) — `cq_runtime_v2.c` (Step 23.5) --- */
int32_t cqrt_qram_alloc_i1 (int32_t count);
int32_t cqrt_qram_alloc_i8 (int32_t count);
int32_t cqrt_qram_alloc_i16(int32_t count);
int32_t cqrt_qram_alloc_i32(int32_t count);
int32_t cqrt_qram_alloc_i64(int32_t count);
int32_t cqrt_qram_alloc_f16(int32_t count);
int32_t cqrt_qram_alloc_f32(int32_t count);
int32_t cqrt_qram_alloc_f64(int32_t count);
int32_t cqrt_qram_alloc_f80(int32_t count);
int32_t cqrt_qram_load_i1 (int32_t arr, int32_t idx);
int32_t cqrt_qram_load_i8 (int32_t arr, int32_t idx);
int32_t cqrt_qram_load_i16(int32_t arr, int32_t idx);
int32_t cqrt_qram_load_i32(int32_t arr, int32_t idx);
int32_t cqrt_qram_load_i64(int32_t arr, int32_t idx);
int32_t cqrt_qram_load_f16(int32_t arr, int32_t idx);
int32_t cqrt_qram_load_f32(int32_t arr, int32_t idx);
int32_t cqrt_qram_load_f64(int32_t arr, int32_t idx);
int32_t cqrt_qram_load_f80(int32_t arr, int32_t idx);
void cqrt_qram_load_i1_unc (int32_t out, int32_t arr, int32_t idx);
void cqrt_qram_load_i8_unc (int32_t out, int32_t arr, int32_t idx);
void cqrt_qram_load_i16_unc(int32_t out, int32_t arr, int32_t idx);
void cqrt_qram_load_i32_unc(int32_t out, int32_t arr, int32_t idx);
void cqrt_qram_load_i64_unc(int32_t out, int32_t arr, int32_t idx);
void cqrt_qram_load_f16_unc(int32_t out, int32_t arr, int32_t idx);
void cqrt_qram_load_f32_unc(int32_t out, int32_t arr, int32_t idx);
void cqrt_qram_load_f64_unc(int32_t out, int32_t arr, int32_t idx);
void cqrt_qram_load_f80_unc(int32_t out, int32_t arr, int32_t idx);
void cqrt_qram_store_i1 (int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_i8 (int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_i16(int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_i32(int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_i64(int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_f16(int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_f32(int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_f64(int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_f80(int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_i1_unc (int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_i8_unc (int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_i16_unc(int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_i32_unc(int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_i64_unc(int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_f16_unc(int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_f32_unc(int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_f64_unc(int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_f80_unc(int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_i1_controlled (int32_t pred, int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_i8_controlled (int32_t pred, int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_i16_controlled(int32_t pred, int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_i32_controlled(int32_t pred, int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_i64_controlled(int32_t pred, int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_f16_controlled(int32_t pred, int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_f32_controlled(int32_t pred, int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_f64_controlled(int32_t pred, int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_f80_controlled(int32_t pred, int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_i1_controlled_unc (int32_t pred, int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_i8_controlled_unc (int32_t pred, int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_i16_controlled_unc(int32_t pred, int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_i32_controlled_unc(int32_t pred, int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_i64_controlled_unc(int32_t pred, int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_f16_controlled_unc(int32_t pred, int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_f32_controlled_unc(int32_t pred, int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_f64_controlled_unc(int32_t pred, int32_t arr, int32_t idx, int32_t val);
void cqrt_qram_store_f80_controlled_unc(int32_t pred, int32_t arr, int32_t idx, int32_t val);

/* --- v1.1 — tape (11) — `cq_runtime_tape.c` (PRD §15 D23; aborts in `cq_runtime_v2.c` until 2026-09-02) --- */
int32_t cqrt_tape_alloc(void);
int32_t cqrt_tape_write_i1 (int32_t tape, int32_t src);
int32_t cqrt_tape_write_i8 (int32_t tape, int32_t src);
int32_t cqrt_tape_write_i16(int32_t tape, int32_t src);
int32_t cqrt_tape_write_i32(int32_t tape, int32_t src);
int32_t cqrt_tape_write_i64(int32_t tape, int32_t src);
int32_t cqrt_tape_write_i1_controlled (int32_t ctrl, int32_t tape, int32_t src);
int32_t cqrt_tape_write_i8_controlled (int32_t ctrl, int32_t tape, int32_t src);
int32_t cqrt_tape_write_i16_controlled(int32_t ctrl, int32_t tape, int32_t src);
int32_t cqrt_tape_write_i32_controlled(int32_t ctrl, int32_t tape, int32_t src);
int32_t cqrt_tape_write_i64_controlled(int32_t ctrl, int32_t tape, int32_t src);

/* --- Deferred to v2 — the handle helper (1) — `cq_runtime_v2.c`, blocked on bd ck6 --- */
int32_t cqrt_alloc_handle(void);

/* --- DELIBERATELY LEFT UNDEFINED (2) — PRD SS1 / SS15 D16 --- */
void cqrt_h (int32_t q);
void cqrt_h_controlled (int32_t ctrl, int32_t q);

#ifdef __cplusplus
}
#endif

#endif /* CQ_RUNTIME_ABI_H */
