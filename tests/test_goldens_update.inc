/* tests/test_goldens_update.inc — THE REMEDY. bd 7ew.
 *
 * Included by test_goldens.c rather than compiled on its own, so it shares that
 * file's tmpf fixtures, its MASK_A/MASK_B declarations and its mute/take
 * helpers.
 *
 * THE SEAM IS `the REFUSAL ↔ the REMEDY`, and it is a subject cut rather than a
 * size cut. Every refusal cq_gold_open and cq_gold_close can make ends with the
 * same sentence — "regenerate with CQOPS_UPDATE_GOLDENS=1". The parent file is
 * the refusals: a stale `# measured-at:` line, a missing one, a re-pinned
 * `# bennett:`, a missing file, a row nothing looked at. THIS file is the
 * instruction those messages print, executed and asserted to work.
 *
 * IT IS A CUT WORTH TAKING BECAUSE THE REMEDY HALF IS WHERE THE BUG WAS. At
 * Step 10 the R3 mismatch abort fired BEFORE the writer, so a checking run and
 * an update run failed identically — and the only way to obey the message the
 * check had just printed was to hand-edit the pinned SHA, which is precisely
 * the silent re-pinning the check exists to prevent, and which re-derives no
 * count at all. A refusal whose remedy cannot be carried out is worse than no
 * refusal: it teaches the maintainer to launder the pin. So every case here
 * drives the update branch, and each one asserts what the WRITER produced by
 * reading the file back, not merely that the loader stopped complaining.
 *
 * Trigger: test_goldens.c stood at 217 of 300 when bd 7ew's four cases arrived
 * (~+75), which is the house 240 threshold in one step. Rule 12 — a split is
 * scheduled, not improvised; recorded in IMPLEMENTATION_PLAN §3 with the file.
 *
 * Same fixture discipline as the parent: every path is a fresh mkstemp under
 * /tmp, nothing reads tests/goldens/, and nothing goes near
 * third_party/bennett/COMMIT, which is read-only in the bytes and in the pin
 * (Rule 1, and the Step 10 near-miss it records).
 */

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

    if (!open_updating(&g, t.path, MASK_B, NULL)) { tmpf_drop(&t); return; }
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

    if (!open_updating(&g, t.path, NULL, NULL)) { tmpf_drop(&t); return; }

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

    if (!open_updating(&g, t.path, big, NULL)) { tmpf_drop(&t); return; }
    (void)cq_h_take_failures();
    cq_h_mute(1);
    CHECK_EQ(cq_gold_close(&g), 0);
    cq_h_mute(0);
    CHECK_EQ(cq_h_take_failures(), 1);

    if (!open_updating(&g, t.path, "two\nlines", NULL)) { tmpf_drop(&t); return; }
    (void)cq_h_take_failures();
    cq_h_mute(1);
    CHECK_EQ(cq_gold_close(&g), 0);
    cq_h_mute(0);
    CHECK_EQ(cq_h_take_failures(), 1);

    tmpf_drop(&t);
}

/* THE OTHER HALF OF RISK R3, AND THE HALF THAT WAS ITSELF A BUG. The parent's
 * a_re_pinned_snapshot_invalidates_a_golden_that_was_not_regenerated asserts
 * the detector fires; this asserts the way out is walkable. An update run is
 * EXEMPT from the comparison, adopts the snapshot's SHA, and rewrites every
 * count from a live measurement — which is the honest version of the act that
 * editing `# bennett:` by hand would fake.
 *
 * The green checking run at the end is what makes it a round trip rather than
 * a write: a writer that adopted the SHA into `g->commit` and then emitted the
 * old one — or emitted `(unknown)` — would pass every assertion up to it. */
CQ_TEST(an_update_run_adopts_a_re_pinned_sha_and_then_checks_green)
{
    tmpf gold, commit;
    cq_gold g;
    char body[4096];

    if (!tmpf_write(&gold, golden_with("# measured-at: " MASK_A "\n"))) return;
    if (!commit_fixture(&commit, "0badcafe")) { tmpf_drop(&gold); return; }

    /* The state the detector leaves behind: file says deadbeef, snapshot says
     * 0badcafe, checking run red. */
    CHECK_EQ(failures_from_open(&g, gold.path, MASK_A, commit.path), 1);

    if (!open_updating(&g, gold.path, MASK_A, commit.path)) goto out;
    CHECK_STR_EQ(g.commit, "0badcafe");          /* adopted at OPEN */
    CHECK(cq_gold_check(&g, "xor", "forward", 8, 0, 8, 0));
    CHECK(cq_gold_close(&g));

    CHECK(tmpf_read(&gold, body, sizeof body));
    CHECK(strstr(body, "# bennett: 0badcafe\n") != NULL);
    CHECK(strstr(body, "deadbeef") == NULL);     /* replaced, not appended */

    /* And the remedy actually remedies: the same checking run is now green. */
    CHECK_EQ(failures_from_open(&g, gold.path, MASK_A, commit.path), 0);
    close_as_reader(&g);

out:
    tmpf_drop(&commit);
    tmpf_drop(&gold);
}

/* A MISSING GOLDEN IS MINTED, AND INHERITS THE PIN. The parent's
 * a_missing_golden_is_not_a_passing_one is the refusal; this is its remedy,
 * and the clause that matters is the SHA. cq_gold_open's missing-file branch
 * returns before the load loop, so the fresh file's `# bennett:` line can only
 * come from `want` — a branch that returned before copying it would mint a
 * golden reading `(unknown)`, which is a file that can never fail R3 again.
 *
 * open_updating CHECKs its return, which is right HERE — a mint that did not
 * open is not a mint — and is exactly why the refusal half lives in the parent
 * and goes through failures_from_open instead. */
CQ_TEST(an_update_run_mints_a_missing_golden_and_inherits_the_pin)
{
    tmpf gold, commit;
    cq_gold g;
    char body[4096];

    if (!tmpf_write(&gold, "")) return;
    tmpf_drop(&gold);                    /* a PATH, with no file behind it */

    if (!commit_fixture(&commit, "cafef00d")) return;

    if (!open_updating(&g, gold.path, MASK_A, commit.path)) goto out;
    CHECK_STR_EQ(g.commit, "cafef00d");
    CHECK_EQ((int)g.n, 0);
    CHECK(cq_gold_check(&g, "xor", "forward", 8, 0, 8, 0));
    CHECK(cq_gold_close(&g));

    CHECK(tmpf_read(&gold, body, sizeof body));
    CHECK(strstr(body, "# bennett: cafef00d\n") != NULL);
    CHECK(strstr(body, "# measured-at: " MASK_A "\n") != NULL);
    CHECK(strstr(body, "xor") != NULL);

    /* The minted file is a golden, not just a header: it loads, it carries the
     * row, and it satisfies both cross-checks.
     *
     * THE `g.n == 1` GUARD IS NOT DEFENSIVE PADDING — IT IS WHAT MAKES THE
     * FAILURE LEGIBLE. Every refusal in cq_gold_open ends in gold_abandon,
     * which frees the rows and NULLs the pointer (it has to: LeakSanitizer is
     * on in Debug, and a refusal that leaked would report as a leak in whatever
     * provoked it). So chasing g.v[0] unconditionally turns a REPORTED failure
     * into a SIGSEGV — measured, with the mutant that stops the mint inheriting
     * the pin: ctest still called the suite red, but the binary died mid-case
     * and printed no `not ok` line at all, so the battery could see THAT
     * test_goldens failed and not WHICH claim. An assertion that counts is not
     * an assertion that identifies. */
    CHECK_EQ(failures_from_open(&g, gold.path, MASK_A, commit.path), 0);
    CHECK_EQ((int)g.n, 1);
    if (g.n == 1) {
        CHECK_EQ((int)g.v[0].W, 8);
        CHECK_EQ((int)g.v[0].cx, 8);
    }
    close_as_reader(&g);

out:
    tmpf_drop(&commit);
    tmpf_drop(&gold);
}
