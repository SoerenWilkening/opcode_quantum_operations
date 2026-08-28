/* shim/cq_runtime_abi.h — CQ_lang's 173 `cqrt_*` declarations, VERBATIM.
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
 *   sha256        : 41e1e20f2536759063bf5e850093f160b7c3f7761a43d98e9877324af32dcac7
 *   last commit to that file : 591f478ce7e1d12b3dd8d8e1babfe6adb63f32df (2026-07-28)
 *   CQ_lang HEAD when vendored : 34b799521a4d69324f77787b4759cc9356d430c1 (2026-08-23)
 *   extraction    : comment-stripped identifier extraction, NOT a line-anchored
 *                   grep — that header is column-aligned and the obvious regex
 *                   silently drops 49 of its 173 declarations
 *                   (docs/cqrt_census.txt records the trap).
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

/* --- Deferred to v2 — fp widths (34) — `cq_runtime_v2.c` (Step 23.5) --- */
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

/* --- Deferred to v2 — tape (11) — `cq_runtime_v2.c` (Step 23.5) --- */
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
