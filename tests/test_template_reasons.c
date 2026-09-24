/* tests/test_template_reasons.c — bead ar9: the SENTENCE every still-aborting
 * `fp` body prints, read out of the LINKED library in a child process.
 *
 * THE SUITE'S WHOLE SUBJECT IS A MESSAGE (bd memory
 * a-suite-whose-subject-is-a-message-must-read-the-message), so it forks each
 * symbol and compares stderr BYTE FOR BYTE, as tests/test_template_fconv.inc
 * does for bead 9ve.34's DECLINED row. A death case can say only "it aborted";
 * the defect this bead fixed was a body that aborted with a FALSE reason.
 *
 * THE DECISION (bead ar9, maintainer, 2026-09-24): "fp is v2" is retired from
 * the generated grid. v2 shipped at f64 (PRD-v2 §1), so
 *   - the still-aborting rows that touch f64 carry a reason of their OWN,
 *     citing PRD-v2 §7.9: `frem` (no construction, §3.3) and `fpext`/`fptrunc`
 *     (need an f16/f32/f80 rail);
 *   - every other `fp` row is at f16/f32/f80 and NAMES that width as outside
 *     v2's f64-only scope, an upstream dependency (PRD-v2 §1a, §6.2/§6.3).
 * The bucket of every row is unchanged; only the text moved.
 *
 * THE EXPECTED TEXT IS TYPED HERE, NOT IMPORTED from shim/gen_reasons.py: a
 * generator that printed a wrong width would agree with any oracle derived
 * from it. tests/test_gen_bodies.py sweeps all 804 `fp` bodies statically
 * against its own independent transcription; this file is the runtime witness
 * for one or more symbols of every reason class, through the real
 * `cq_shim_unsupported`. The hand-written fp-CORE aborts in
 * shim/cq_runtime_v2.c are NOT this bead's (see tests/test_runtime_v2.c).
 */

#include "cq_shim.h"

#include "support/childcap.h"
#include "support/harness.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* --- the reason texts, transcribed ------------------------------------------ */

#define R_FREM "frem f64 stays an abort on liveness: the pinned Bennett "      \
               "snapshot has no frem construction (no soft_frem) and the "     \
               "corpus calls it zero times (PRD-v2 3.3, 7.9)"
#define R_WIDEN(op, src, dst, w)                                              \
    op " " src " -> " dst " stays an abort: it needs an " w " rail, and v2 "  \
    "is f64-only, so no " w " rail can be allocated (PRD-v2 7.9)"
#define R_WIDTH(w) w " is outside v2's f64-only scope; that width is an "    \
                   "upstream dependency, not a pending port (PRD-v2 1a, 6.2/6.3)"
#define R_WIDTHS(a, b) a " and " b " are outside v2's f64-only scope; those " \
                       "widths are an upstream dependency, not a pending "   \
                       "port (PRD-v2 1a, 6.2/6.3)"

/* --- the symbols ------------------------------------------------------------ */

/* ALL FIFTEEN `frem_f64` SYMBOLS, one per ABI variant, `_inv` included: the
 * row has not landed, so its `_inv` is not a D14 row (gen_reasons' DECLINED
 * doctrine) and says the same sentence as its forward. */
#define REASON_ROWS(X)                                                        \
    X(frem_f64,                      (0, 0),          R_FREM)                 \
    X(frem_f64_hl,                   (0, 0.0),        R_FREM)                 \
    X(frem_f64_lh,                   (0.0, 0),        R_FREM)                 \
    X(frem_f64_unc,                  (0, 0, 0),       R_FREM)                 \
    X(frem_f64_hl_unc,               (0, 0, 0.0),     R_FREM)                 \
    X(frem_f64_lh_unc,               (0, 0.0, 0),     R_FREM)                 \
    X(frem_f64_controlled,           (0, 0, 0),       R_FREM)                 \
    X(frem_f64_hl_controlled,        (0, 0, 0.0),     R_FREM)                 \
    X(frem_f64_lh_controlled,        (0, 0.0, 0),     R_FREM)                 \
    X(frem_f64_inv,                  (0, 0),          R_FREM)                 \
    X(frem_f64_hl_inv,               (0, 0.0),        R_FREM)                 \
    X(frem_f64_lh_inv,               (0.0, 0),        R_FREM)                 \
    X(frem_f64_controlled_inv,       (0, 0, 0),       R_FREM)                 \
    X(frem_f64_hl_controlled_inv,    (0, 0, 0.0),     R_FREM)                 \
    X(frem_f64_lh_controlled_inv,    (0, 0.0, 0),     R_FREM)                 \
    /* fpext / fptrunc touching f64: the OTHER width is the rail it needs,  \
     * on either side of the arrow. */                                        \
    X(fpext_f32_to_f64,              (0),     R_WIDEN("fpext", "f32", "f64", "f32")) \
    X(fpext_f32_to_f64_unc,          (0, 0),  R_WIDEN("fpext", "f32", "f64", "f32")) \
    X(fpext_f64_to_f80_inv,          (0),     R_WIDEN("fpext", "f64", "f80", "f80")) \
    X(fptrunc_f64_to_f16,            (0),     R_WIDEN("fptrunc", "f64", "f16", "f16")) \
    X(fptrunc_f80_to_f64_unc,        (0, 0),  R_WIDEN("fptrunc", "f80", "f64", "f80")) \
    /* the width default, at each of the three widths and across families */ \
    X(fadd_f16,                      (0, 0),          R_WIDTH("f16"))         \
    X(fmul_f32_controlled,           (0, 0, 0),       R_WIDTH("f32"))         \
    X(fcmp_olt_f80_unc,              (0, 0, 0),       R_WIDTH("f80"))         \
    X(sitofp_i32_to_f32,             (0),             R_WIDTH("f32"))         \
    X(bitcast_i80_to_f80_inv,        (0),             R_WIDTH("f80"))         \
    X(floor_f16_unc,                 (0, 0),          R_WIDTH("f16"))         \
    X(fma_f32,                       (0, 0, 0),       R_WIDTH("f32"))         \
    /* a cast between two out-of-scope widths names both, in ABI order */    \
    X(fpext_f16_to_f32,              (0),             R_WIDTHS("f16", "f32")) \
    X(fpext_f16_to_f32_unc,          (0, 0),          R_WIDTHS("f16", "f32"))

/* The ABI's own declarations for the symbols above (the manifests in tests/abi/); a wrong
 * one is a link error or a `conflicting types`, never a silent mismatch. */
int32_t cq_template_frem_f64(int32_t, int32_t);
int32_t cq_template_frem_f64_hl(int32_t, double);
int32_t cq_template_frem_f64_lh(double, int32_t);
void    cq_template_frem_f64_unc(int32_t, int32_t, int32_t);
void    cq_template_frem_f64_hl_unc(int32_t, int32_t, double);
void    cq_template_frem_f64_lh_unc(int32_t, double, int32_t);
int32_t cq_template_frem_f64_controlled(int32_t, int32_t, int32_t);
int32_t cq_template_frem_f64_hl_controlled(int32_t, int32_t, double);
int32_t cq_template_frem_f64_lh_controlled(int32_t, double, int32_t);
int32_t cq_template_frem_f64_inv(int32_t, int32_t);
int32_t cq_template_frem_f64_hl_inv(int32_t, double);
int32_t cq_template_frem_f64_lh_inv(double, int32_t);
int32_t cq_template_frem_f64_controlled_inv(int32_t, int32_t, int32_t);
int32_t cq_template_frem_f64_hl_controlled_inv(int32_t, int32_t, double);
int32_t cq_template_frem_f64_lh_controlled_inv(int32_t, double, int32_t);
int32_t cq_template_fpext_f32_to_f64(int32_t);
void    cq_template_fpext_f32_to_f64_unc(int32_t, int32_t);
int32_t cq_template_fpext_f64_to_f80_inv(int32_t);
int32_t cq_template_fptrunc_f64_to_f16(int32_t);
void    cq_template_fptrunc_f80_to_f64_unc(int32_t, int32_t);
int32_t cq_template_fadd_f16(int32_t, int32_t);
int32_t cq_template_fmul_f32_controlled(int32_t, int32_t, int32_t);
void    cq_template_fcmp_olt_f80_unc(int32_t, int32_t, int32_t);
int32_t cq_template_sitofp_i32_to_f32(int32_t);
int32_t cq_template_bitcast_i80_to_f80_inv(int32_t);
void    cq_template_floor_f16_unc(int32_t, int32_t);
int32_t cq_template_fma_f32(int32_t, int32_t, int32_t);
int32_t cq_template_fpext_f16_to_f32(int32_t);
void    cq_template_fpext_f16_to_f32_unc(int32_t, int32_t);

#define REASON_THUNK(sym, args, why)                                          \
    static void call_##sym(void *p) { (void)p; (void)cq_template_##sym args; }
REASON_ROWS(REASON_THUNK)

#define REASON_ENTRY(sym, args, why)                                          \
    { "cq_template_" #sym, call_##sym, why },
static const struct {
    const char *sym;
    void (*fn)(void *);
    const char *why;
} ROWS[] = { REASON_ROWS(REASON_ENTRY) };

#define N_ROWS (sizeof ROWS / sizeof ROWS[0])

/* --- the cases --------------------------------------------------------------- */

CQ_TEST(every_reason_class_prints_its_own_sentence_at_runtime)
{
    char msg[768];

    CHECK_EQ((int)N_ROWS, 29);
    for (size_t i = 0; i < N_ROWS; i++) {
        char want[768];
        const int aborted = cq_child_capture(CQ_CHILD_STDERR, ROWS[i].fn,
                                             NULL, NULL, 0u, msg, sizeof msg);
        CHECK(aborted);
        (void)snprintf(want, sizeof want, "cqops: %s not implemented (%s)\n",
                       ROWS[i].sym, ROWS[i].why);
        if (strcmp(msg, want) != 0)
            cq_h_fail(__FILE__, __LINE__, "%s said `%s`, wanted `%s`",
                      ROWS[i].sym, msg, want);
    }
}

/* THE NEGATIVE, AT RUNTIME: no symbol above prints the retired sentence, and
 * none of them prints a sentence naming a width the symbol does not touch. The
 * static sweep over every generated body is tests/test_gen_bodies.py's. */
CQ_TEST(no_reason_class_prints_the_retired_fp_is_v2)
{
    char msg[768];

    for (size_t i = 0; i < N_ROWS; i++) {
        (void)cq_child_capture(CQ_CHILD_STDERR, ROWS[i].fn, NULL, NULL, 0u,
                               msg, sizeof msg);
        if (strstr(msg, "fp is v2") != NULL)
            cq_h_fail(__FILE__, __LINE__, "%s still says `%s`",
                      ROWS[i].sym, msg);
        /* The f64 rows' own reasons never call f64 out of scope. */
        if (strstr(ROWS[i].sym, "f64") != NULL &&
            strstr(msg, "outside v2's f64-only scope") != NULL)
            cq_h_fail(__FILE__, __LINE__, "%s calls its width out of scope: "
                      "`%s`", ROWS[i].sym, msg);
    }
}

CQ_TEST_MAIN(
    CQ_CASE(every_reason_class_prints_its_own_sentence_at_runtime),
    CQ_CASE(no_reason_class_prints_the_retired_fp_is_v2)
)
