/* tests/test_goldens.c — the golden loader's own header claims, provoked.
 *
 * bd 2r5. `cq_gold_close` used to write "Measured at operand mask ALL-QUANTUM
 * on BOTH operands" into every golden it produced. That was a literal in the
 * writer with no per-file input, and it was FALSE in four of the twelve files:
 * K10's mux has three sources, K5's casts have one, K4's shift amount is
 * constrained CLASSICAL, and L7's Grover is a whole program where no operand
 * mask is a parameter at all. Rule 10's first L4 note is that a count is a
 * function of (W, operand mask), so a golden whose stated mask is wrong is a
 * golden documenting the wrong function — and nothing could ever contradict it,
 * because a comment is not data.
 *
 * THE FIX MAKES IT DATA, AND THIS FILE IS WHY THAT IS NOT JUST PROSE. An
 * assertion nobody has seen fail is an assertion nobody has tested — this
 * project has measured that twice (harness.h, and the five Phase-B pool
 * assertions that survived mutation to always-true). So each of the three new
 * refusals is PROVOKED here and asserted to fire, and each is paired with a
 * negative control asserting the accepted case is accepted. Without the
 * controls a loader that failed unconditionally would pass this suite.
 *
 * IT ALSO COVERS THE `# bennett:` CHECK, which is risk R3's mitigation and had
 * no test at all. CLAUDE.md says how, and why it is written this way: point the
 * test at a FIXTURE path, never at third_party/bennett/COMMIT, which is
 * READ-ONLY in the bytes and in the pin (Rule 1). Every file this suite touches
 * is a fresh mkstemp under /tmp.
 */

#include "support/goldens.h"
#include "support/harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------------- *
 * Fixtures. Everything is written, never read out of tests/goldens/.
 * ------------------------------------------------------------------------- */

typedef struct { char path[64]; } tmpf;

static int tmpf_write(tmpf *t, const char *body)
{
    snprintf(t->path, sizeof t->path, "/tmp/cqops_gold_XXXXXX");

    int fd = mkstemp(t->path);
    if (fd < 0) { cq_h_fail(__FILE__, __LINE__, "mkstemp failed"); return 0; }

    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); unlink(t->path); return 0; }

    if (body) fputs(body, f);
    fclose(f);
    return 1;
}

static void tmpf_drop(tmpf *t) { unlink(t->path); }

/* Reads a whole fixture back, so a case can assert what the WRITER produced
 * rather than only what the loader accepted. */
static int tmpf_read(const tmpf *t, char *out, size_t cap)
{
    FILE *f = fopen(t->path, "r");
    if (!f) return 0;
    size_t n = fread(out, 1, cap - 1, f);
    out[n] = '\0';
    fclose(f);
    return 1;
}

#define MASK_A "ALL-QUANTUM on both operands, at ctrl_depth 0"
#define MASK_B "ALL-QUANTUM on all THREE sources — cond, t and f"

static const char *golden_with(const char *mask_line)
{
    static char buf[1024];
    snprintf(buf, sizeof buf,
             "# a fixture\n"
             "# bennett: deadbeef\n"
             "%s"
             "# kernel   pass      W   NOT  CNOT  Toffoli\n"
             "xor       forward    8     0     8        0\n",
             mask_line);
    return buf;
}

/* A COMMIT fixture, so the R3 check can be provoked without writing a byte
 * under third_party/ (Rule 1, and the Step 10 near-miss it records). */
static int commit_fixture(tmpf *t, const char *sha)
{
    char buf[256];
    snprintf(buf, sizeof buf,
             "Bennett.jl — a FIXTURE, not the pin\n"
             "commit: %s\n", sha);
    return tmpf_write(t, buf);
}

/* Closes a fixture the way a READER must: every row marked visited, `updating`
 * cleared. cq_gold_close's unvisited-row sweep is a real assertion and would
 * otherwise fire on every case here, reporting the suite's own scaffolding as
 * a coverage hole. */
static void close_as_reader(cq_gold *g)
{
    for (size_t i = 0; i < g->n; i++) g->v[i].visited = 1;
    g->updating = 0;
    CHECK(cq_gold_close(g));
}

/* Opens as a REGENERATION run, through the real mechanism rather than by poking
 * the flag. `g->updating` is latched at open — which is why the unsetenv below
 * does not disarm it, and why the checking-run cases that follow are unaffected
 * — and an update run is deliberately exempt from BOTH cross-checks, since it
 * is the act that makes a stale header honest again by re-measuring. */
static int open_updating(cq_gold *g, const char *path, const char *mask)
{
    int rc;

    setenv("CQOPS_UPDATE_GOLDENS", "1", 1);
    rc = cq_gold_open(g, path, "a fixture", mask, NULL);
    unsetenv("CQOPS_UPDATE_GOLDENS");

    CHECK(rc);
    CHECK_EQ(g->updating, 1);
    return rc;
}

/* Provokes and counts. The mute/take pair is the shape harness.h mandates:
 * it cannot leave the mute on, because the take is unconditional. */
static int failures_from_open(cq_gold *g, const char *path, const char *mask,
                              const char *commit_file)
{
    int rc, n;

    (void)cq_h_take_failures();          /* start from a clean slate */
    cq_h_mute(1);
    rc = cq_gold_open(g, path, "a fixture", mask, commit_file);
    cq_h_mute(0);
    n = cq_h_take_failures();

    /* A refusal must BOTH report and return 0: a loader that reported and then
     * returned 1 would let the caller sweep on against rows it had just called
     * stale, which is the silent half of the bug. */
    if (n && rc) cq_h_fail(__FILE__, __LINE__,
                           "cq_gold_open reported %d failure(s) and still "
                           "returned 1", n);
    return n;
}

/* ------------------------------------------------------------------------- *
 * 1. The mask claim is falsifiable.
 * ------------------------------------------------------------------------- */

/* THIS SUITE MUST BE A CHECKING RUN, and the environment can say otherwise:
 * `CQOPS_UPDATE_GOLDENS=1 ctest` would put cq_gold_open into the update branch,
 * where BOTH cross-checks are deliberately exempt — every provocation below
 * would then pass by not running. Cleared here rather than assumed, and
 * asserted, because a silent skip is the failure mode this whole file is about.
 * `--update-goldens` cannot arrive: CQ_TEST_MAIN records no flags. */
CQ_TEST(this_suite_is_a_checking_run_whatever_the_environment_asked_for)
{
    unsetenv("CQOPS_UPDATE_GOLDENS");
    CHECK_EQ(cq_gold_updating(), 0);
}

CQ_TEST(a_golden_whose_measured_at_line_disagrees_with_the_suite_is_refused)
{
    tmpf t;
    cq_gold g;

    if (!tmpf_write(&t, golden_with("# measured-at: " MASK_A "\n"))) return;

    /* The negative control FIRST, so a loader that refused everything could
     * not pass this case. */
    CHECK_EQ(failures_from_open(&g, t.path, MASK_A, NULL), 0);
    close_as_reader(&g);

    CHECK_EQ(failures_from_open(&g, t.path, MASK_B, NULL), 1);

    tmpf_drop(&t);
}

/* THE PREFIX CASE, AND IT IS THE ONE A `%s` PARSE WOULD FAIL. Ten of the twelve
 * shipped masks begin with the word ALL-QUANTUM, so a loader that compared the
 * first WORD would accept every one of them for every other — which is exactly
 * the confusion bd 2r5 is about (mux's three sources against xor's two). */
CQ_TEST(the_whole_mask_sentence_is_compared_not_its_first_word)
{
    tmpf t;
    cq_gold g;

    if (!tmpf_write(&t, golden_with("# measured-at: " MASK_A "\n"))) return;

    CHECK_EQ(failures_from_open(&g, t.path,
                                "ALL-QUANTUM on both operands", NULL), 1);
    tmpf_drop(&t);
}

/* A golden written before the line existed is STALE, not exempt. The alternative
 * — treating a missing line as "no claim, so no disagreement" — would leave every
 * pre-2r5 file permanently unlabelled and the check permanently vacuous. */
CQ_TEST(a_golden_with_no_measured_at_line_at_all_is_refused)
{
    tmpf t;
    cq_gold g;

    if (!tmpf_write(&t, golden_with(""))) return;

    CHECK_EQ(failures_from_open(&g, t.path, MASK_A, NULL), 1);
    tmpf_drop(&t);
}

/* The reader's escape hatch. test_unc.c opens eleven goldens it did not measure
 * and has no mask of its own to declare; NULL must skip the comparison rather
 * than assert an empty one. */
CQ_TEST(a_null_mask_is_a_readers_escape_hatch_and_skips_the_comparison)
{
    tmpf t;
    cq_gold g;

    if (!tmpf_write(&t, golden_with("# measured-at: " MASK_A "\n"))) return;

    CHECK_EQ(failures_from_open(&g, t.path, NULL, NULL), 0);
    CHECK_EQ((int)g.n, 1);               /* and it really did load the rows */
    close_as_reader(&g);

    tmpf_drop(&t);
}

/* ------------------------------------------------------------------------- *
 * 2. The writer states the mask, and refuses to write without one.
 * ------------------------------------------------------------------------- */

CQ_TEST(the_writer_emits_the_suites_own_mask_and_the_file_round_trips)
{
    tmpf t;
    cq_gold g;
    char body[4096];

    /* A file with the OLD boilerplate, which is what a real regeneration finds:
     * the header is stale, the counts are not necessarily. */
    if (!tmpf_write(&t, golden_with(
            "# Measured at operand mask ALL-QUANTUM on BOTH operands, and\n"
            "# at ctrl_depth 0.\n"))) return;

    if (!open_updating(&g, t.path, MASK_B)) { tmpf_drop(&t); return; }
    CHECK(cq_gold_check(&g, "mux", "forward", 8, 0, 4, 4));
    CHECK(cq_gold_close(&g));

    CHECK(tmpf_read(&t, body, sizeof body));
    CHECK(strstr(body, "# measured-at: " MASK_B "\n") != NULL);

    /* THE CLAIM THAT MATTERS: the old literal is gone, not merely joined. A
     * writer that emitted both would still be asserting "BOTH operands" into a
     * three-source kernel's golden. */
    CHECK(strstr(body, "ALL-QUANTUM on BOTH operands") == NULL);

    /* And it reads back clean under the same declaration — the round trip is
     * what makes the cross-check usable rather than a permanent red. */
    CHECK_EQ(failures_from_open(&g, t.path, MASK_B, NULL), 0);
    close_as_reader(&g);

    tmpf_drop(&t);
}

CQ_TEST(an_update_run_with_no_mask_writes_nothing_and_says_so)
{
    tmpf t;
    cq_gold g;
    char body[4096];

    if (!tmpf_write(&t, "# untouched\n")) return;

    if (!open_updating(&g, t.path, NULL)) { tmpf_drop(&t); return; }

    (void)cq_h_take_failures();
    cq_h_mute(1);
    int rc = cq_gold_close(&g);
    cq_h_mute(0);
    CHECK_EQ(cq_h_take_failures(), 1);
    CHECK_EQ(rc, 0);

    /* NOT WRITTEN, not written-then-blamed. A refusal that had already
     * truncated the file would have destroyed the golden it declined to
     * label. */
    CHECK(tmpf_read(&t, body, sizeof body));
    CHECK_STR_EQ(body, "# untouched\n");

    tmpf_drop(&t);
}

/* A mask that cannot round-trip is refused at the WRITER rather than truncated,
 * because a truncated one would fail its own cross-check on the next run and
 * read as a stale golden — a real bug reported as the wrong bug. */
CQ_TEST(an_over_long_or_multi_line_mask_is_refused_rather_than_truncated)
{
    tmpf t;
    cq_gold g;
    char big[CQ_GOLD_MASK_MAX + 8];

    memset(big, 'q', sizeof big - 1);
    big[sizeof big - 1] = '\0';

    if (!tmpf_write(&t, "# untouched\n")) return;

    if (!open_updating(&g, t.path, big)) { tmpf_drop(&t); return; }
    (void)cq_h_take_failures();
    cq_h_mute(1);
    CHECK_EQ(cq_gold_close(&g), 0);
    cq_h_mute(0);
    CHECK_EQ(cq_h_take_failures(), 1);

    if (!open_updating(&g, t.path, "two\nlines")) { tmpf_drop(&t); return; }
    (void)cq_h_take_failures();
    cq_h_mute(1);
    CHECK_EQ(cq_gold_close(&g), 0);
    cq_h_mute(0);
    CHECK_EQ(cq_h_take_failures(), 1);

    tmpf_drop(&t);
}

/* ------------------------------------------------------------------------- *
 * 3. Risk R3's commit check, which had no test either.
 * ------------------------------------------------------------------------- */

CQ_TEST(a_re_pinned_snapshot_invalidates_a_golden_that_was_not_regenerated)
{
    tmpf gold, commit;
    cq_gold g;

    if (!tmpf_write(&gold, golden_with("# measured-at: " MASK_A "\n"))) return;

    /* The negative control: the fixture's own SHA agrees. */
    if (!commit_fixture(&commit, "deadbeef")) { tmpf_drop(&gold); return; }
    CHECK_EQ(failures_from_open(&g, gold.path, MASK_A, commit.path), 0);
    close_as_reader(&g);
    tmpf_drop(&commit);

    /* Re-pinned, goldens untouched: exactly the silent invalidation R3 names. */
    if (!commit_fixture(&commit, "0badcafe")) { tmpf_drop(&gold); return; }
    CHECK_EQ(failures_from_open(&g, gold.path, MASK_A, commit.path), 1);
    tmpf_drop(&commit);

    tmpf_drop(&gold);
}

/* The `commit:` line is on the SECOND line of the real COMMIT file, and reading
 * the first would give a stable string that never moves when the snapshot is
 * re-pinned — a check that cannot see the event it exists for. */
CQ_TEST(the_commit_sha_is_read_off_the_commit_line_not_the_first_line)
{
    tmpf gold, commit;
    cq_gold g;

    if (!tmpf_write(&gold, golden_with("# measured-at: " MASK_A "\n"))) return;
    if (!commit_fixture(&commit, "deadbeef")) { tmpf_drop(&gold); return; }

    CHECK_EQ(failures_from_open(&g, gold.path, MASK_A, commit.path), 0);
    CHECK_STR_EQ(g.commit, "deadbeef");
    close_as_reader(&g);

    tmpf_drop(&commit);
    tmpf_drop(&gold);
}

CQ_TEST_MAIN(
    /* FIRST, and the ordering is load-bearing: it clears the env var every
     * case below depends on being absent. */
    CQ_CASE(this_suite_is_a_checking_run_whatever_the_environment_asked_for),
    CQ_CASE(a_golden_whose_measured_at_line_disagrees_with_the_suite_is_refused),
    CQ_CASE(the_whole_mask_sentence_is_compared_not_its_first_word),
    CQ_CASE(a_golden_with_no_measured_at_line_at_all_is_refused),
    CQ_CASE(a_null_mask_is_a_readers_escape_hatch_and_skips_the_comparison),
    CQ_CASE(the_writer_emits_the_suites_own_mask_and_the_file_round_trips),
    CQ_CASE(an_update_run_with_no_mask_writes_nothing_and_says_so),
    CQ_CASE(an_over_long_or_multi_line_mask_is_refused_rather_than_truncated),
    CQ_CASE(a_re_pinned_snapshot_invalidates_a_golden_that_was_not_regenerated),
    CQ_CASE(the_commit_sha_is_read_off_the_commit_line_not_the_first_line)
)
