/* tests/test_runtime_abi.inc — the vendored ABI declarations, and nothing else.
 *
 * A THIRD SUBJECT AND ITS OWN CUT, taken at Rule 12's 300-line HARD LIMIT when
 * the rail suite's name-set check pushed the `.c` to 319. It is a subject cut
 * rather than a size cut, and the discriminator is sharp: this case drives NO
 * libcqops entry point at all. It opens two FILES — `shim/cq_runtime_abi.h` and
 * `docs/cqrt_census.txt` — and compares a vendored copy against an independent
 * enumeration. Everything else in the suite calls a `cqrt_*` symbol and asserts
 * what the library did. The rail file's own recorded seam
 * (`the rail's LIFETIME <-> the rail's CONTENTS`) had already been taken on the
 * test side by tests/test_runtime_rail_write.inc; this is the second, and it is
 * recorded here because it was improvised at the wall rather than scheduled —
 * which is what Rule 12 asks to be said out loud when it happens.
 *
 * Included by tests/test_runtime_rail.c and inheriting its includes; out of
 * check_loc.sh's reach by the EXTENSION rule.
 */

/* THE HEADER IS A COPY, AND A COPY NEEDS A DETECTOR THAT IS NOT ITSELF.
 *
 * Its provenance block records that the extraction was compiled TOGETHER WITH
 * CQ_lang's own header and produced zero `conflicting types`, with a negative
 * control that produced one — but that was an ACT, not a test, and CQ_lang is
 * deliberately off this project's build path. What is in this repository and is
 * independent is docs/cqrt_census.txt, produced from CQ_lang at Step 0.3 by a
 * different route: PART D's family x width grid gives the eight per-owner
 * counts, and its verbatim list of the 65 v1 symbols gives the NAMES.
 *
 * BOTH HALVES ARE CHECKED, AND THE FIRST DRAFT HAD ONLY THE COUNTS. That gap is
 * not hypothetical: during this step's adversarial review a concurrent process
 * renamed `cqrt_xorc_i16` to `cqrt_xorq_i16` in the header, and the whole suite
 * stayed green — every count is exact under a rename — while the definition of
 * `cqrt_xorc_i16` silently lost the only prototype that type-checks it, which is
 * the ONE thing this header exists to provide. A transposition of two same-typed
 * parameters is still invisible here and always will be, because C ignores
 * parameter names; that one is pinned behaviourally by the copy case below.
 *
 * THE NAME SET IS COMPARED BOTH WAYS over the v1 subset, which is the 65 the
 * census lists verbatim and is exactly the header minus its fp widths, its qram
 * and its tape — 173 - 34 - 63 - 11 = 65. A name in the header and not the
 * census is an invention; a name in the census and not the header is a
 * declaration we have lost. */

/* Every `cqrt_*` identifier that opens a declaration line, in file order. */
static uint32_t abi_names(const char *path, char out[][48], uint32_t cap,
                          unsigned *groups)
{
    char line[512];
    uint32_t n = 0u;
    FILE *f = fopen(path, "r");

    *groups = 0u;
    if (!f) { cq_h_fail(__FILE__, __LINE__, "cannot open %s", path); return 0u; }

    while (fgets(line, sizeof line, f)) {
        if (strstr(line, "/* --- ")) { (*groups)++; continue; }
        if (line[0] == ' ' || line[0] == '*' || line[0] == '/') continue;
        if (!strstr(line, ");")) continue;
        const char *p = strstr(line, "cqrt_");
        if (!p) continue;
        size_t k = 0u;
        while (p[k] && (p[k] == '_' || (p[k] >= 'a' && p[k] <= 'z')
                        || (p[k] >= '0' && p[k] <= '9')) && k < 47u) k++;
        if (n < cap) { memcpy(out[n], p, k); out[n][k] = '\0'; n++; }
    }
    (void)fclose(f);
    return n;
}

/* The census's verbatim v1 list, whitespace-separated, between its own marker
 * and the NOTE that follows it. */
static uint32_t census_v1_names(char out[][48], uint32_t cap)
{
    char path[512], line[512];
    uint32_t n = 0u;
    int inside = 0;

    (void)snprintf(path, sizeof path, "%s/cqrt_census.txt", CQOPS_DOCS_DIR);
    FILE *f = fopen(path, "r");
    if (!f) { cq_h_fail(__FILE__, __LINE__, "cannot open %s", path); return 0u; }

    while (fgets(line, sizeof line, f)) {
        if (strstr(line, "The 76 v1 symbols, verbatim:")) { inside = 1; continue; }
        if (!inside) continue;
        if (strstr(line, "NOTE the 76")) break;
        for (const char *p = line; (p = strstr(p, "cqrt_")) != NULL; ) {
            size_t k = 0u;
            while (p[k] && (p[k] == '_' || (p[k] >= 'a' && p[k] <= 'z')
                            || (p[k] >= '0' && p[k] <= '9')) && k < 47u) k++;
            if (n < cap) { memcpy(out[n], p, k); out[n][k] = '\0'; n++; }
            p += k;
        }
    }
    (void)fclose(f);
    return n;
}

static int name_in(char set[][48], uint32_t n, const char *want)
{
    for (uint32_t i = 0; i < n; i++) if (strcmp(set[i], want) == 0) return 1;
    return 0;
}

/* fp width and qram are the two deferred populations the census's 76 excludes;
 * tape was the third until PRD §15 D23 (2026-09-02). */
static int is_v1_name(const char *s)
{
    if (strncmp(s, "cqrt_qram", 9) == 0) return 0;
    return !(strstr(s, "_f16") || strstr(s, "_f32") ||
             strstr(s, "_f64") || strstr(s, "_f80"));
}

CQ_TEST(the_vendored_abi_header_declares_the_whole_frozen_surface)
{
    static char hdr[256][48], cen[128][48], v1[128][48];
    char path[512], line[512];
    unsigned groups = 0u;

    (void)snprintf(path, sizeof path, "%s/cq_runtime_abi.h", CQOPS_SHIM_DIR);
    const uint32_t n_hdr = abi_names(path, hdr, 256u, &groups);

    /* (a) THE COUNTS, per owning group, from PART D's grid:
     *   11 rail lifetime (5 alloc + 5 measure + free)
     *   21 rail contents (5 copy + 5 copy_controlled + cswap + 5 addc + 5 xorc)
     *   30 gate          (5 discrete + 5 ry + 5 ry_controlled_inv + 5 rz
     *                                + 5 rz_controlled + 5 rz_controlled_inv)
     *   11 tape (v1.1 since PRD §15 D23 — its OWN group, cq_runtime_tape.c)
     *   34 fp widths, 63 qram, 1 alloc_handle            -> 98, cq_runtime_v2.c
     *    2 cqrt_h / cqrt_h_controlled, DELIBERATELY UNDEFINED (PRD §15 D16) */
    unsigned per[9] = { 0u }, g = 0u, total = 0u;
    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    if (f) {
        while (fgets(line, sizeof line, f)) {
            if (strstr(line, "/* --- ")) { g++; continue; }
            if (line[0] == ' ' || line[0] == '*' || line[0] == '/') continue;
            if (!strstr(line, "cqrt_") || !strstr(line, ");")) continue;
            total++;
            if (g >= 1u && g <= 8u) per[g]++;
        }
        (void)fclose(f);
    }
    CHECK_EQ(total, 173);
    CHECK_EQ(n_hdr, 173);
    CHECK_EQ(groups, 8);
    CHECK_EQ(per[1], 11);   /* rail, lifetime  */
    CHECK_EQ(per[2], 21);   /* rail, contents  */
    CHECK_EQ(per[3], 30);   /* gate            */
    CHECK_EQ(per[4], 34);   /* v2: fp widths   */
    CHECK_EQ(per[5], 63);   /* v2: qram        */
    CHECK_EQ(per[6], 11);   /* v1.1: tape      */
    CHECK_EQ(per[7], 1);    /* v2: alloc_handle*/
    CHECK_EQ(per[8], 2);    /* undefined       */
    CHECK_EQ(per[1] + per[2], 32);

    /* (b) THE NAMES, both directions, over the v1 subset. */
    uint32_t n_v1 = 0u;
    for (uint32_t i = 0; i < n_hdr; i++)
        if (is_v1_name(hdr[i]) && n_v1 < 128u) {
            memcpy(v1[n_v1], hdr[i], sizeof v1[0]); n_v1++;
        }
    const uint32_t n_cen = census_v1_names(cen, 128u);

    CHECK_EQ(n_cen, 76);
    CHECK_EQ(n_v1, 76);
    for (uint32_t i = 0; i < n_cen; i++)
        if (!name_in(v1, n_v1, cen[i]))
            cq_h_fail(__FILE__, __LINE__,
                      "the census names %s and the header does not declare it",
                      cen[i]);
    for (uint32_t i = 0; i < n_v1; i++)
        if (!name_in(cen, n_cen, v1[i]))
            cq_h_fail(__FILE__, __LINE__,
                      "the header declares %s and the census does not name it",
                      v1[i]);
}
