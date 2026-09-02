/* Step 23, landing 1, step 5: the V1 BOUNDARY — shim/cq_runtime_v2.c, the 109
 * `cqrt_*` symbols libcqops could serve and v1 defers (PRD §15 D16; bd vxk,
 * bd r3y, bd ck6). **98 since 2026-09-02**: PRD §15 D23 moved the 11 `tape`
 * symbols into scope (shim/cq_runtime_tape.c), and every "109" below that is
 * not a count the suite asserts is the 2026-08-27 figure.
 *
 * THE SUITE'S WHOLE SUBJECT IS A MESSAGE, so it reads the real one rather than
 * asserting that something aborted. A death case can say only "it aborted",
 * and with 109 macro-expanded bodies the interesting failure is not a missing
 * abort — it is an abort that NAMES THE WRONG SYMBOL or carries the WRONG
 * BUCKET, both of which a death case and its FAIL_REGULAR_EXPRESSION are blind
 * to. So every one of the 109 is forked and its stderr is compared byte for
 * byte. tests/test_shim_ctx_region.inc pins the same function's FORMAT against
 * a symbol and reason IT supplies; this pins what the 109 shipped bodies
 * actually pass.
 *
 * THE ORACLE IS shim/cq_runtime_abi.h AND IT IS INDEPENDENT OF BOTH MACRO
 * SETS. The shim stringifies its names out of a width token; the thunk table
 * next door calls them as identifiers, where a mistake is a link error. Neither
 * can reach the header, and the header is what says which 109 names exist. A
 * body that stringifies the wrong literal makes one name appear twice and
 * another never, and both directions of the set check go red.
 *
 * THIS IS THE THIRD COPY OF A fork-and-capture HELPER IN tests/ — after
 * tests/test_shim_ctx_region.inc (stderr) and tests/test_runtime_gate_rotate.inc
 * (stdout AND stderr) — and it is NOT extracted here on purpose. Extraction
 * means a new tests/support/ module, which is LOC-counted under Rule 12, plus
 * edits to two working suites whose captures differ in what they redirect;
 * doing that inside a step whose subject is 109 abort bodies would be scope the
 * step did not ask for. Filed rather than accreted silently.
 *
 * RULE 12. Split seam, RECORDED BEFORE THE CASES WERE WRITTEN AND TAKEN ON THE
 * FIRST MEASUREMENT:
 *
 *     the SURFACE (which symbols exist) <-> the MESSAGE (what each one says)
 *                                          ->  tests/test_runtime_v2_message.inc
 *
 * The file landed at 277 of 300 — past the house 240 trigger and 23 from Rule
 * 12's wall — so the split was taken immediately rather than deferred, which is
 * the plan working as written rather than an exception. The surface half stays
 * here: it opens FILES (the ABI header, the five shim sources) and names no
 * message at all. The message half forks a child per symbol and compares bytes.
 * Nothing here forks and nothing there reads a file. The 109-row thunk table is
 * separately in tests/test_runtime_v2_table.inc, which is the plain "large
 * static test table" case (plan §2.3) rather than a seam.
 */

#include "cq_runtime_abi.h"

#include "cq_shim.h"

#include "support/harness.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test_runtime_v2_table.inc"

/* Shared by both halves of the seam: the surface half sizes the header
 * enumeration with it, the message half sizes the parsed name. */
#define V2_NAME_MAX 48

/* THE THREE BUCKET STRINGS (four until D23 retired `tape is v2`), WRITTEN OUT BY HAND. They are the contract, so the
 * test states them rather than importing them: a shim that swapped two buckets
 * would agree with itself and with any oracle derived from it. */
#define V2_R_FP   "fp is v2"
#define V2_R_QRAM "qram is v2: libcqops models no addressable quantum array " \
                  "at any width"
#define V2_R_HAND "a CQ_lang intrinsic or libm template minted a handle; "    \
                  "libcqops cannot mint a register-less handle without "      \
                  "diverging the shared D5 counter (bd ck6)"

/* --- the header, read as the independent enumeration ---------------------- */

/* D16's population, derived from the NAME and from nothing else: qram is a
 * family and is deferred at EVERY width, so it is tested before the fp token
 * (tape was, until D23 put it in scope — a `cqrt_tape_` name is v1.1's now). That order IS the decision `bd vxk` recorded — `cqrt_qram_alloc_f32`
 * is qram's, not fp's — and writing it the other way round is what the case
 * below exists to catch. */
static const char *bucket_of(const char *name)
{
    if (strcmp(name, "cqrt_alloc_handle") == 0)  return V2_R_HAND;
    if (strncmp(name, "cqrt_qram_", 10) == 0)    return V2_R_QRAM;
    if (strstr(name, "_f16") || strstr(name, "_f32") ||
        strstr(name, "_f64") || strstr(name, "_f80")) return V2_R_FP;
    return NULL;                                  /* v1 serves it */
}

/* EVERY `cqrt_` IDENTIFIER ON A LINE, and not `strstr(line, "cqrt_h(")`. The
 * ABI header spells two of its declarations with a SPACE before the paren
 * (`void cqrt_h (int32_t q);`), so a substring test that assumes the paren is
 * adjacent finds nothing and the case passes for the wrong reason — measured,
 * it reported 0 of an expected 2 the first time it ran. Extracting the whole
 * identifier also makes `cqrt_h` and `cqrt_h_controlled` distinguishable, which
 * a prefix test is not. */
static uint32_t cqrt_idents(const char *line, char out[][V2_NAME_MAX],
                            uint32_t cap)
{
    uint32_t n = 0u;
    const char *p = line;

    while ((p = strstr(p, "cqrt_")) != NULL) {
        uint32_t k = 0u;

        while (k + 1u < V2_NAME_MAX &&
               (p[k] == '_' || (p[k] >= 'a' && p[k] <= 'z') ||
                (p[k] >= '0' && p[k] <= '9'))) k++;

        if (n < cap) {
            memcpy(out[n], p, k);
            out[n][k] = '\0';
            n++;
        }
        p += k;
    }
    return n;
}

static uint32_t header_deferred(char out[][V2_NAME_MAX], uint32_t cap)
{
    char line[512], path[512];
    uint32_t n = 0u;

    /* CQOPS_SHIM_DIR, not a relative path: ctest runs a binary from its own
     * build directory, and tests/CMakeLists.txt defines the source-tree roots
     * for exactly this reason (tests/test_runtime_abi.inc's precedent). */
    (void)snprintf(path, sizeof path, "%s/cq_runtime_abi.h", CQOPS_SHIM_DIR);

    FILE *f = fopen(path, "r");
    if (!f) { cq_h_fail(__FILE__, __LINE__, "cannot open %s", path); return 0u; }

    while (fgets(line, sizeof line, f)) {
        if (line[0] == ' ' || line[0] == '*' || line[0] == '/') continue;
        if (!strstr(line, ");")) continue;

        char name[1][V2_NAME_MAX];
        if (cqrt_idents(line, name, 1u) == 0u) continue;

        if (!bucket_of(name[0])) continue;
        if (n >= cap) { cq_h_fail(__FILE__, __LINE__, "deferred set overflow"); break; }
        memcpy(out[n++], name[0], V2_NAME_MAX);
    }
    fclose(f);
    return n;
}

/* -------------------------------------------------------------------------
 * The cases.
 * ------------------------------------------------------------------------- */

/* THE COUNT IS 98 AND ITS DECOMPOSITION IS PRD §15 D16's ARITHMETIC AS D23 LEFT IT:
 * 173 declared = 32 rail + 30 gate + 11 tape (v1.1) + 2 cqrt_h* + 34 fp + 63 qram + 1
 * alloc_handle. The counterfactual is what makes it decisive — if either of the
 * rail or gate surfaces took all nine widths of its families it would be 48,
 * not 32 or 30. */
/* OBSERVED FAILING (2026-08-27, both configurations): one extra deferred
 * declaration added to the ABI header. That is the only thing that CAN move it
 * — the case is a claim about the header, not about the shim — which is exactly
 * why it is worth having: it is the tripwire for a re-pin of CQ_lang's ABI
 * quietly widening the population this file is responsible for. */
CQ_TEST(the_deferred_surface_is_exactly_the_headers_98_declarations)
{
    char want[128][V2_NAME_MAX];
    const uint32_t nw = header_deferred(want, 128u);

    CHECK_EQ((int)nw, 98);
    CHECK_EQ((int)CQ_V2_N_THUNKS, 98);

    /* strcmp and not `==`: two occurrences of the same string literal are not
     * required to share an address, so a pointer comparison here is
     * unspecified — and -Wstring-compare says so, which is why it is a build
     * error in this project rather than a latent one. */
    uint32_t fp = 0u, qram = 0u, hand = 0u;
    for (uint32_t i = 0; i < nw; i++) {
        const char *b = bucket_of(want[i]);

        if (!b)                              continue;
        if (strcmp(b, V2_R_FP) == 0)         fp++;
        else if (strcmp(b, V2_R_QRAM) == 0)  qram++;
        else if (strcmp(b, V2_R_HAND) == 0)  hand++;
    }
    CHECK_EQ((int)fp, 34);
    CHECK_EQ((int)qram, 63);
    CHECK_EQ((int)hand, 1);
}

/* cqrt_h AND cqrt_h_controlled ARE DECLARED AND DELIBERATELY NOT DEFINED, and
 * that is the other half of D16: libcqops defines everything it COULD serve and
 * leaves undefined only what it could not serve at any point in v1. Rule 4
 * forbids H on the classical path and PRD §8's vtable is frozen at six entries
 * with no `h` slot, so a stub would be a body that can only ever lie.
 *
 * IT IS CHECKED IN THE SOURCES BECAUSE A LINKED BINARY CANNOT SEE AN ABSENCE.
 * The claim is about what libcqops SHIPS, and the two symbols being absent from
 * every shim source is that claim at the place it is made. `nm` on the archive
 * would be the other route and it needs a path this suite is not given. */
/* OBSERVED FAILING (2026-08-27, both configurations) and SOLE detector for its
 * mutant: a `cqrt_h` body appended to shim/cq_runtime_v2.c. Nothing else in the
 * tree notices — the symbol links, nothing calls it, and every other case here
 * is about the 109. */
CQ_TEST(cqrt_h_and_its_controlled_twin_are_defined_nowhere_in_the_shim)
{
    static const char *const files[] = {
        "cq_shim_ctx.c", "cq_shim_proof.c", "cq_runtime_rail.c",
        "cq_runtime_gate.c", "cq_runtime_v2.c", "cq_runtime_tape.c"
    };
    char line[512], path[512], ids[8][V2_NAME_MAX];
    int  declared = 0;

    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        (void)snprintf(path, sizeof path, "%s/%s", CQOPS_SHIM_DIR, files[i]);

        FILE *f = fopen(path, "r");
        if (!f) { cq_h_fail(__FILE__, __LINE__, "cannot open %s", path); continue; }

        while (fgets(line, sizeof line, f)) {
            /* Comment-only lines are skipped by their FIRST NON-SPACE
             * character, not by column 0: every definition inside this file's
             * macros is indented, so an `if (line[0] == \' \')` filter would
             * skip the very lines a definition could hide in. */
            const char *t = line;
            while (*t == ' ' || *t == '\t') t++;
            if (t[0] == '*' || (t[0] == '/' && (t[1] == '*' || t[1] == '/')))
                continue;

            const uint32_t n = cqrt_idents(line, ids, 8u);
            for (uint32_t k = 0; k < n; k++)
                if (strcmp(ids[k], "cqrt_h") == 0 ||
                    strcmp(ids[k], "cqrt_h_controlled") == 0)
                    cq_h_fail(__FILE__, __LINE__,
                              "%s names %s: PRD §15 D16 leaves exactly these "
                              "two UNDEFINED", files[i], ids[k]);
        }
        fclose(f);
    }

    /* And the header still DECLARES them, which is what makes the omission a
     * link error rather than a silent hole if CQ_lang ever emits one. */
    (void)snprintf(path, sizeof path, "%s/cq_runtime_abi.h", CQOPS_SHIM_DIR);

    FILE *h = fopen(path, "r");
    if (!h) { cq_h_fail(__FILE__, __LINE__, "cannot open %s", path); return; }
    while (fgets(line, sizeof line, h)) {
        if (!strstr(line, ");")) continue;

        const uint32_t n = cqrt_idents(line, ids, 8u);
        for (uint32_t k = 0; k < n; k++)
            if (strcmp(ids[k], "cqrt_h") == 0 ||
                strcmp(ids[k], "cqrt_h_controlled") == 0)
                declared++;
    }
    fclose(h);
    CHECK_EQ(declared, 2);
}

#include "test_runtime_v2_message.inc"

CQ_TEST_MAIN(
    CQ_CASE(the_deferred_surface_is_exactly_the_headers_98_declarations),
    CQ_CASE(every_deferred_symbol_aborts_with_a_well_formed_message),
    CQ_CASE(the_names_the_bodies_print_are_exactly_the_headers_deferred_set),
    CQ_CASE(each_body_carries_its_own_buckets_reason),
    CQ_CASE(qram_at_an_fp_width_is_qrams_refusal_and_not_fps),
    CQ_CASE(cqrt_h_and_its_controlled_twin_are_defined_nowhere_in_the_shim)
)
