/* tests/test_kerneldrv_budget.inc — bd 9ve.32. The PER-SHAPE L1 FLOOR, made
 * falsifiable, in the file whose whole subject is falsifying the driver.
 *
 * INCLUDED FROM tests/test_kerneldrv.c, like the anchors .inc beside it, and
 * for the same two reasons: that file is at Rule 12's edge, and an .inc is
 * exempt from the LOC guard by design ("move the big static table into an .inc"
 * is a real escape hatch rather than a rename that dodges the guard). It needs
 * NO CMake change — it is #included, not compiled.
 *
 * SELF-CONTAINED ON PURPOSE. It borrows only CQ_TEST, CHECK*, CQ_EXPECT_CLEAN
 * and cq_kd_* from its host; it does NOT reach for the anchors .inc's statics,
 * so this file's position in the include list is not load-bearing.
 *
 * FOUR CLAIMS, and each names the single case that goes red if it is deleted:
 *
 *   (a) a shape whose floor is sized for the whole anchor block runs exactly
 *       that many cases and drops NOTHING — every anchor at every mask row.
 *   (b) a shape with no floor runs exactly cq_kd_samples(), as it did before
 *       this bead — including a shape that declares anchors but no floor.
 *   (c) CQOPS_L1_SAMPLES wins over a floor in BOTH directions, and the printed
 *       `#` line says so.
 *   (d) a floor BELOW the constant budget is ignored — max, never replace.
 *
 * THE HARNESS BUG THIS FILE WAS WRITTEN AROUND IS FIXED (`bd 9ve.33`, landed
 * 2026-09-18). CQ_EXPECT_CLEAN used to open with a cq_h_take_failures() that
 * ZEROED the per-case counter CQ_TEST_MAIN reads, so a CHECK failing BEFORE one
 * of those macros printed its `# FAIL` line, was forgiven, and the case
 * reported GREEN — measured here while these cases were red, tallies of 2 and 1
 * against 7 and 2 emitted FAIL lines. A take now forgives only what the window
 * itself provoked, so a premise CHECK is falsifiable wherever it stands, and
 * the two below have been moved back ahead of the provocation they are premises
 * for.
 *
 * WHAT STAYS, AND FOR ITS OWN REASONS RATHER THAN THAT ONE: every case still
 * captures bud_calls into a local the moment its sampler returns, because the
 * NEXT CQ_EXPECT_CLEAN (and bud_line_says) resets that static, and it still
 * asserts after bud_env_restore(), because the environment is part of the
 * fixture here. Neither is a workaround for the harness.
 *
 * THE ENVIRONMENT IS PART OF THE FIXTURE HERE, which no other case in this
 * binary can say. cq_kd_samples() caches its answer for the life of the
 * process, so every case below sets CQOPS_L1_SAMPLES to what it needs, calls
 * cq_kd_samples_reset(), and restores BOTH the variable and the cache on the
 * way out — otherwise case (c) would leave the rest of the binary running at
 * whatever budget it last asked for.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- The environment, saved and restored per case. ---------------------- */

/* COPIED OUT, NOT POINTED AT: getenv returns a pointer into the environment
 * block and setenv/unsetenv may move it, so holding the returned pointer
 * across the very calls this file makes is a use-after-free of exactly the
 * shape CLAUDE.md records for cq_ctrl_push's realloc'ing stack. */
static char bud_saved[64];
static int  bud_had;

static void bud_env_save(void)
{
    const char *e = getenv("CQOPS_L1_SAMPLES");

    bud_had = e != NULL;
    bud_saved[0] = '\0';
    if (e) snprintf(bud_saved, sizeof bud_saved, "%s", e);
}

/* NULL unsets. Always followed by the cache reset, because the two together
 * are what "the budget is now N" means — the variable alone changes nothing. */
static void bud_env_set(const char *v)
{
    if (v) (void)setenv("CQOPS_L1_SAMPLES", v, 1);
    else   (void)unsetenv("CQOPS_L1_SAMPLES");
    cq_kd_samples_reset();
}

static void bud_env_restore(void)
{
    bud_env_set(bud_had ? bud_saved : NULL);
}

/* ---- A kernel that counts its calls and records the mask row. ----------- */

/* Two calls per case — the forward and L3's uncompute — so the case count is
 * bud_calls / 2. Asserted in that form rather than divided out, because a
 * driver that stopped running the uncompute would otherwise pass unchanged. */
enum { BUD_LOG_MAX = 256 };
static int bud_log[BUD_LOG_MAX];   /* the mask row, per CALL */
static int bud_calls;

/* 0 both operands classical, 1 only `a` quantum, 2 only `b` quantum, 3 other
 * (which is what a random draw's mask row usually looks like). The three
 * anchor rows of kerneldrv.h's schedule are 0, 1 and 2 in that order. */
static int bud_row_of(const cq_bit *a, const cq_bit *b, int W)
{
    int qa = 0, qb = 0;

    for (int i = 0; i < W; i++) {
        if (!cq_bit_is_const(a[i])) qa = 1;
        if (!cq_bit_is_const(b[i])) qb = 1;
    }
    if (!qa && !qb) return 0;
    if ( qa && !qb) return 1;
    if (!qa &&  qb) return 2;
    return 3;
}

/* bud_calls counts EVERY call and the array holds the first BUD_LOG_MAX, so
 * the count assertions stay honest at any CQOPS_L1_SAMPLES rather than
 * silently becoming "the log filled up". */
static void k_bud_counts(cq_ctx *ctx, cq_bit *dst, const cq_bit *a,
                         const cq_bit *b, int W)
{
    if (bud_calls < BUD_LOG_MAX) bud_log[bud_calls] = bud_row_of(a, b, W);
    bud_calls++;
    cq_kernel_xor(ctx, dst, a, b, W);
}

/* ---- The shapes. -------------------------------------------------------- */

/* THE LINE AN fp KERNEL'S SHAPE FUNCTION WRITES, and the deliverable of this
 * bead: the whole §7.12 anchor block at all three mask rows, plus a small
 * random tail. The count is read back out of the provider that was just
 * installed, so the same two lines serve any provider — including a composing
 * one — and a width with no anchors (everything but f64 today) gets a floor of
 * 8, which is below the constant budget and is therefore ignored. */
static void shape_fp_floor(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->anchors     = cq_fp_anchors_binary;
    out->min_samples = 3 * out->anchors(W, -1, NULL) + 8;
}

/* The same provider with NO floor — the pre-9ve.32 shape, which must still
 * truncate. This is what keeps the floor OPT-IN rather than implied by
 * declaring anchors. */
static void shape_anchors_no_floor(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->anchors = cq_fp_anchors_binary;
}

/* A floor and no anchors: the budget arithmetic on its own, at a width cheap
 * enough to run a hundred cases through. */
static void shape_floor_83(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->min_samples = 83;
}

/* A floor BELOW the constant budget. */
static void shape_floor_8(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->min_samples = 8;
}

/* ---- (a) The floor runs the whole block, and drops nothing. ------------- */

CQ_TEST(a_shape_floor_runs_the_whole_anchor_block_and_drops_none)
{
    const cq_kd_spec k = { "budget-floor", k_bud_counts, cq_ref_xor,
                           shape_fp_floor, NULL, NULL };
    const int A    = cq_fp_anchors_binary(64, -1, NULL);
    const int want = 3 * A;                 /* every anchor at every mask row */
    const int n    = want + 8;              /* what shape_fp_floor asks for   */
    const char *src = NULL;

    int n_default, got_n, calls;

    bud_env_save();
    bud_env_set(NULL);      /* the ambient CQOPS_L1_SAMPLES must not decide it */

    n_default = cq_kd_samples();
    got_n     = cq_kd_budget(n, &src);

    /* THE PREMISE, ASSERTED, AND ASSERTED BEFORE THE THING IT IS A PREMISE FOR:
     * without a floor this table is three times over budget. If that ever stops
     * being true the case is vacuous, and it would be vacuous in the GREEN
     * direction. It sat at the end of the case until `bd 9ve.33` — where it was
     * still correct, but a reader had to run the whole sampler to reach it. */
    CHECK(want > n_default);

    bud_calls = 0;
    CQ_EXPECT_CLEAN("the f64 table with a floor sized to hold it",
                    cq_kd_sample_at(&k, 64));
    calls = bud_calls;

    bud_env_restore();

    CHECK_EQ(got_n, n);
    CHECK_STR_EQ(src, "floor");
    CHECK_EQ(calls, 2 * n);
    CHECK(2 * (2 + want) <= BUD_LOG_MAX);   /* the log really reached slot 76 */

    /* AND NOTHING WAS DROPPED. A COUNT CANNOT SAY THIS: a budget of 83 that
     * still truncated the block to 30 anchor cases would run 83 cases all the
     * same. What identifies it is the row of every one of the 75 anchor slots,
     * in the row-major-BY-ROW order kerneldrv.h pins — so a truncation shows up
     * as a random draw's row where a mixed row belongs. */
    for (int j = 0; j < want; j++)
        CHECK_EQ(bud_log[2 * (2 + j)], j / A);
}

/* ---- (b) No floor is today, byte for byte. ------------------------------ */

CQ_TEST(a_shape_with_no_floor_runs_exactly_the_constant_budget)
{
    const cq_kd_spec plain = { "budget-none", k_bud_counts, cq_ref_xor,
                               NULL, NULL, NULL };
    const cq_kd_spec anch  = { "budget-none-anchored", k_bud_counts, cq_ref_xor,
                               shape_anchors_no_floor, NULL, NULL };
    const char *src = NULL;
    int n0, got_n, calls_plain, calls_anch;

    bud_env_save();
    bud_env_set(NULL);

    n0    = cq_kd_samples();
    got_n = cq_kd_budget(0, &src);

    bud_calls = 0;
    CQ_EXPECT_CLEAN("no shape at all", cq_kd_sample_at(&plain, 64));
    calls_plain = bud_calls;

    /* AND ANCHORS WITHOUT A FLOOR ARE STILL TRUNCATED. Declaring anchors must
     * not raise the budget by itself: bd hkg's schedule and its "N DROPPED"
     * line are the behaviour every fp shape inherits until it opts in.
     *
     * NOT THE SOLE DETECTOR, AND SAYING SO IS THE POINT. Measured: the mutant
     * that makes a declared provider IMPLY a floor is killed here AND by
     * anchors_sit_inside_the_budget_and_never_on_top_of_it in the anchors .inc,
     * which pre-dates this bead. This leg is kept as the post-9ve.32 statement
     * of the same claim — the floor is OPT-IN — and a reader must not take it
     * for the only thing standing between the suite and that fault. */
    bud_calls = 0;
    CQ_EXPECT_CLEAN("the same anchors, no floor", cq_kd_sample_at(&anch, 64));
    calls_anch = bud_calls;

    bud_env_restore();

    CHECK_EQ(got_n, n0);
    CHECK_STR_EQ(src, "default");
    CHECK_EQ(calls_plain, 2 * n0);
    CHECK_EQ(calls_anch,  2 * n0);
}

/* ---- (c) The environment wins, in both directions, and says so. --------- */

/* Runs the sampler with stdout captured and fails if `needle` is missing.
 * THE PRINTED LINE IS THE ONLY PLACE a reader learns which of the three rows
 * set the budget, so it is asserted rather than eyeballed — the argument this
 * whole binary exists to make. A harness failure inside the window would have
 * its MESSAGE captured and dropped; its COUNT still accrues, so the case still
 * reports. */
static void bud_line_says(const char *needle, const cq_kd_spec *k, int W)
{
    char buf[8192];
    FILE *tmp = tmpfile();
    int saved = dup(STDOUT_FILENO);
    size_t got;

    if (tmp == NULL || saved < 0) {
        cq_h_fail(__FILE__, __LINE__, "could not capture stdout");
        if (tmp) (void)fclose(tmp);
        return;
    }

    fflush(stdout);
    (void)dup2(fileno(tmp), STDOUT_FILENO);
    cq_kd_sample_at(k, W);
    fflush(stdout);
    (void)dup2(saved, STDOUT_FILENO);
    (void)close(saved);

    rewind(tmp);
    got = fread(buf, 1u, sizeof buf - 1u, tmp);
    buf[got] = '\0';
    (void)fclose(tmp);

    if (strstr(buf, needle) == NULL)
        cq_h_fail(__FILE__, __LINE__,
                  "the sampler's # line does not name the budget source: "
                  "looked for \"%s\" in [%s]", needle, buf);
}

CQ_TEST(the_environment_wins_over_the_floor_in_both_directions)
{
    const cq_kd_spec wide  = { "budget-env-down", k_bud_counts, cq_ref_xor,
                               shape_fp_floor, NULL, NULL };
    const cq_kd_spec small = { "budget-env-up", k_bud_counts, cq_ref_xor,
                               shape_floor_83, NULL, NULL };
    const char *src8 = NULL, *src100 = NULL;
    int n0, seen8, got8, got100, calls8, calls100, back;

    bud_env_save();
    bud_env_set(NULL);
    n0 = cq_kd_samples();          /* what the rest of the binary must get back */

    /* DOWNWARD. 8 is below the floor of 83 and 8 is what a maintainer who
     * typed 8 must get: a knob that silently refused to go down is a knob
     * nobody can bisect with. */
    bud_env_set("8");
    seen8 = cq_kd_samples();
    got8  = cq_kd_budget(83, &src8);

    bud_calls = 0;
    CQ_EXPECT_CLEAN("CQOPS_L1_SAMPLES=8 against a floor of 83",
                    cq_kd_sample_at(&wide, 64));
    calls8 = bud_calls;

    /* UPWARD. 100 is above it, and a floor must not cap a maintainer asking
     * for depth either — which a max(env, floor) would do at env=8 and a
     * min() would do here. */
    bud_env_set("100");
    got100 = cq_kd_budget(83, &src100);

    bud_calls = 0;
    CQ_EXPECT_CLEAN("CQOPS_L1_SAMPLES=100 against a floor of 83",
                    cq_kd_sample_at(&small, 8));
    calls100 = bud_calls;

    /* AND THE FIXTURE PUTS THE PROCESS BACK. Compared against what this case
     * found rather than against 32, so it is still right on a run a maintainer
     * launched with CQOPS_L1_SAMPLES already set. */
    bud_line_says("budget=env", &small, 8);
    bud_env_restore();
    back = cq_kd_samples();

    CHECK_EQ(seen8, 8);
    CHECK_EQ(got8, 8);
    CHECK_STR_EQ(src8, "env");
    CHECK_EQ(calls8, 2 * 8);
    CHECK_EQ(got100, 100);
    CHECK_STR_EQ(src100, "env");
    CHECK_EQ(calls100, 2 * 100);
    CHECK_EQ(back, n0);
}

/* ---- (d) A floor below the budget is ignored. --------------------------- */

CQ_TEST(a_floor_below_the_constant_budget_is_ignored)
{
    const cq_kd_spec k = { "budget-low-floor", k_bud_counts, cq_ref_xor,
                           shape_floor_8, NULL, NULL };
    const char *src = NULL;
    int n0, got_n, calls;

    bud_env_save();
    bud_env_set(NULL);
    n0    = cq_kd_samples();
    got_n = cq_kd_budget(8, &src);

    CHECK(n0 > 8);                 /* the premise: 8 really is below it */

    bud_calls = 0;
    CQ_EXPECT_CLEAN("a floor of 8 under a budget of 32",
                    cq_kd_sample_at(&k, 8));
    calls = bud_calls;

    /* The line still names the row, because a shape that DECLARED a floor and
     * did not get it is exactly the case a reader would otherwise have to
     * infer from a case count. */
    bud_line_says("budget=default", &k, 8);
    bud_env_restore();

    /* MAX, NEVER REPLACE. A floor that replaced the budget would make a kernel
     * run FEWER cases than every other kernel in the catalogue — a coverage CUT
     * wearing the word "floor", and green. */
    CHECK_EQ(got_n, n0);
    CHECK_STR_EQ(src, "default");
    CHECK_EQ(calls, 2 * n0);
}
