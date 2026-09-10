/* tests/test_runtime_v2_table.inc — one no-argument thunk per deferred symbol,
 * and the table that drives the sweep. 39 rows (109 until PRD §15 D23 moved the
 * 11 tape symbols into scope on 2026-09-02, 35 once D24 moved the 63 qram, and
 * 39 since the 2026-09-10 re-vendor of the ABI at CQ_lang `170ede1` widened the
 * fp bucket by the four `cqrt_ry_f<W>_controlled` — `bd w9i`).
 *
 * A LARGE STATIC TEST TABLE, which is exactly what Rule 12's `.inc` escape
 * hatch is for (IMPLEMENTATION_PLAN §2.3). Included by tests/test_runtime_v2.c
 * and inheriting its includes; out of check_loc.sh's reach by the EXTENSION
 * rule.
 *
 * THE THUNK MACROS MIRROR shim/cq_runtime_v2.c's MACROS AND THAT IS NOT THE
 * "ORACLE SHARES A CONSTANT WITH THE CODE" TRAP. What a thunk shares with the
 * shim is the SYMBOL NAME as an identifier — and a wrong identifier here is a
 * LINK ERROR, not a silent agreement. What the sweep compares the shim against
 * is the string the shim PRINTS, and the oracle for that is
 * shim/cq_runtime_abi.h, which neither file's macros can reach. So a macro that
 * stringifies the wrong literal — `"cqrt_rz_f" #W` where the body is
 * `_controlled` — makes one name appear twice and another never, and BOTH
 * directions of the name-set check go red.
 *
 * NO ARGUMENT HERE IS MEANINGFUL. Every body aborts before reading anything, so
 * the values exist only to satisfy the prototype; a fixture that looked
 * plausible would invite the reader to think one of them mattered.
 */

#define CQ_V2_T_FP(W, VT)                                                     \
    static void t_alloc_f##W(void)   { (void)cqrt_alloc_f##W((VT)0); }        \
    static void t_measure_f##W(void) { (void)cqrt_measure_f##W(0); }          \
    static void t_ry_f##W(void)      { cqrt_ry_f##W(0, 0.0); }                \
    static void t_rz_f##W(void)      { cqrt_rz_f##W(0, 0.0); }                \
    static void t_rzc_f##W(void)     { cqrt_rz_f##W##_controlled(0, 0, 0.0); }\
    static void t_rzci_f##W(void)                                             \
    { cqrt_rz_f##W##_controlled_inv(0, 0, 0.0); }                             \
    static void t_ryc_f##W(void)     { cqrt_ry_f##W##_controlled(0, 0, 0.0); }\
    static void t_copy_f##W(void)    { cqrt_copy_f##W(0, 0); }                \
    static void t_copyc_f##W(void)   { cqrt_copy_f##W##_controlled(0, 0, 0); }

CQ_V2_T_FP(16, _Float16)
CQ_V2_T_FP(32, float)
CQ_V2_T_FP(64, double)
CQ_V2_T_FP(80, long double)

/* THE FORWARD IS IN THE PER-WIDTH MACRO ABOVE AND THE INVERSE IS NOT, AND THAT
 * ASYMMETRY IS THE ABI'S. `cqrt_ry_<W>_controlled` exists at all nine widths
 * since CQ_lang `f92d95e`; `cqrt_ry_<W>_controlled_inv` still exists at seven,
 * missing f16 and f80. Writing the `_inv` pair by hand is what keeps the macro
 * from minting the two symbols the frozen ABI does not declare. */
static void t_ryci_f32(void) { cqrt_ry_f32_controlled_inv(0, 0, 0.0); }
static void t_ryci_f64(void) { cqrt_ry_f64_controlled_inv(0, 0, 0.0); }

/* The 63 qram thunks: GONE, 2026-09-02 (PRD §15 D24) — the symbols are
 * real in shim/cq_runtime_qram.c and tests/test_runtime_qram.c drives them. */

static void t_alloc_handle(void) { (void)cqrt_alloc_handle(); }

#define CQ_V2_ROW_FP(W)                                                       \
    t_alloc_f##W, t_measure_f##W, t_ry_f##W, t_rz_f##W,                       \
    t_rzc_f##W, t_rzci_f##W, t_ryc_f##W, t_copy_f##W, t_copyc_f##W

static void (*const CQ_V2_THUNKS[])(void) = {
    CQ_V2_ROW_FP(16), CQ_V2_ROW_FP(32), CQ_V2_ROW_FP(64), CQ_V2_ROW_FP(80),
    t_ryci_f32, t_ryci_f64,

    t_alloc_handle
};

#define CQ_V2_N_THUNKS (sizeof CQ_V2_THUNKS / sizeof CQ_V2_THUNKS[0])
