/* tests/support/goldens.c — L4's on-disk counts, loaded, checked, regenerated. */

#include "goldens.h"

#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cq_gold_updating(void)
{
    const char *e = getenv("CQOPS_UPDATE_GOLDENS");
    if (e && e[0] && strcmp(e, "0") != 0) return 1;
    return cq_h_flag("--update-goldens");
}

/* Pulls the SHA out of third_party/bennett/COMMIT.
 *
 * THAT FILE IS A DOCUMENT, NOT A ONE-LINE SHA — it opens with a title and
 * carries the hash on a `commit:` line, alongside the tree, the ref and a
 * verification recipe. Reading its first line yields "Bennett.jl — vendored
 * snapshot (libcqops Step 0.1)", which would still be a stable string and
 * would still fail if it changed, but would NOT change when the snapshot is
 * re-pinned to a new commit — i.e. it would be a risk R3 check that cannot
 * see the exact event R3 is about. The COMMIT file's own text asks for this:
 * "L4 gate-count goldens must carry this commit SHA in a header comment". */
static int read_commit_sha(const char *path, char *out, size_t cap)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[512];
    int found = 0;

    while (fgets(line, sizeof line, f)) {
        char sha[80];
        if (sscanf(line, " commit: %79s", sha) == 1) {
            snprintf(out, cap, "%s", sha);
            found = 1;
            break;
        }
    }
    fclose(f);
    return found;
}

/* The `%255[^\n]` below is CQ_GOLD_MASK_MAX - 1 spelled as a literal, because
 * a scanf width cannot be a macro. A renumbering must break a build, not just
 * a comment (bit.h's rule for CQ_BIT_ZERO, applied here). */
_Static_assert(CQ_GOLD_MASK_MAX == 256,
               "the measured-at scanf width below is hard-coded at 255");

/* Trims the trailing newline (and any CR/space before it) off a header value.
 * `# measured-at: ...` is compared with strcmp, so an editor that left a CRLF
 * in the file would otherwise report a mismatch against an identical string. */
static void rstrip(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' ||
                 s[n - 1] == '\t'))
        s[--n] = '\0';
}

/* Every early return out of cq_gold_open goes through here. The rows are
 * already allocated by the time either cross-check runs, and LeakSanitizer is
 * on in Debug (bd kfi) — a refusal that leaked them would report as a leak in
 * whatever provoked the refusal, which is the wrong bug entirely. */
static int gold_abandon(cq_gold *g)
{
    free(g->v);
    g->v = NULL; g->n = g->cap = 0;
    return 0;
}

static cq_gold_row *push_row(cq_gold *g)
{
    if (g->n == g->cap) {
        size_t cap = g->cap ? g->cap * 2 : 32;
        cq_gold_row *v = realloc(g->v, cap * sizeof *v);
        if (!v) { cq_h_fail(__FILE__, __LINE__, "goldens: out of memory"); return NULL; }
        g->v = v;
        g->cap = cap;
    }
    memset(&g->v[g->n], 0, sizeof g->v[g->n]);
    return &g->v[g->n++];
}

int cq_gold_open(cq_gold *g, const char *path, const char *title,
                 const char *mask, const char *commit_file)
{
    char want[80];
    char got_mask[CQ_GOLD_MASK_MAX];
    int  saw_mask = 0;

    g->v = NULL; g->n = g->cap = 0;
    g->path = path; g->title = title; g->mask = mask; g->notes = NULL;
    g->commit[0] = '\0';
    got_mask[0] = '\0';
    g->updating = cq_gold_updating();
    g->loaded = 0;

    want[0] = '\0';
    if (commit_file && !read_commit_sha(commit_file, want, sizeof want))
        cq_h_fail(__FILE__, __LINE__,
                  "goldens: no `commit:` line in %s — risk R3's cross-check "
                  "cannot run", commit_file);

    FILE *f = fopen(path, "r");
    if (!f) {
        if (!g->updating)
            cq_h_fail(__FILE__, __LINE__,
                      "goldens: %s is missing. An unpinned gate count is not a "
                      "passing one; regenerate with CQOPS_UPDATE_GOLDENS=1",
                      path);
        /* An update run mints the file, and inherits the snapshot's commit. */
        snprintf(g->commit, sizeof g->commit, "%s", want);
        return g->updating;
    }

    /* Wide enough that a `# measured-at:` line at the cap round-trips whole:
     * the 15-byte tag plus CQ_GOLD_MASK_MAX plus the newline. A split line
     * would be read as a mask that differs from the one written. */
    char line[512];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') {
            char c[80];
            if (sscanf(line, "# bennett: %79s", c) == 1)
                snprintf(g->commit, sizeof g->commit, "%s", c);
            /* `%[^\n]` and not `%s`: the mask is a SENTENCE, and reading it a
             * word at a time would compare the first word only — which is
             * "ALL-QUANTUM" in ten of the twelve files and so would agree with
             * every mask this check exists to tell apart. */
            else if (sscanf(line, "# measured-at: %255[^\n]", got_mask) == 1) {
                rstrip(got_mask);
                saw_mask = 1;
            }
            continue;
        }

        cq_gold_row r;
        memset(&r, 0, sizeof r);
        unsigned long long x, cx, ccx;
        if (sscanf(line, "%23s %15s %d %llu %llu %llu",
                   r.kernel, r.pass, &r.W, &x, &cx, &ccx) != 6)
            continue;   /* blank or malformed: the visited sweep will notice */

        r.x = x; r.cx = cx; r.ccx = ccx;
        cq_gold_row *slot = push_row(g);
        if (!slot) { fclose(f); return gold_abandon(g); }
        *slot = r;
    }
    fclose(f);
    g->loaded = 1;

    /* RISK R3, WITH TEETH. A re-pinned Bennett snapshot whose goldens were not
     * regenerated is exactly the silent invalidation R3 describes.
     *
     * THE UPDATE RUN IS EXEMPT, and it has to be, or the remedy this very
     * message prints could never be carried out. An earlier draft failed here
     * before reaching the writer, so a checking run and an update run failed
     * identically and the golden file was left untouched — the only way to
     * follow "re-derive the counts, then regenerate" would have been to
     * hand-edit the `# bennett:` line, which is precisely the silent
     * re-pinning the check exists to prevent, and which re-derives nothing.
     * An update run adopts the snapshot's SHA below and rewrites every count
     * from a live measurement, which is the honest version of the same act. */
    if (want[0] && strcmp(g->commit, want) != 0 && !g->updating) {
        cq_h_fail(__FILE__, __LINE__,
                  "goldens: %s was pinned against Bennett %s but the snapshot "
                  "is now %s (risk R3). Re-derive the counts against the new "
                  "snapshot, then regenerate with CQOPS_UPDATE_GOLDENS=1",
                  path, g->commit[0] ? g->commit : "(no commit line)", want);
        return gold_abandon(g);
    }
    if (g->updating && want[0]) snprintf(g->commit, sizeof g->commit, "%s", want);

    /* THE MASK CLAIM, WITH THE SAME TEETH (bd 2r5). A count is a function of
     * (W, operand mask), so the mask a file was taken at is the other half of
     * what its rows mean — and until this check existed it was a literal in
     * the writer that no file could contradict, false in four of the twelve.
     *
     * The suite's declaration is the source of truth and the file is the
     * output, so a disagreement means the header is stale: the counts were
     * pinned under one description and are now being read under another. An
     * UPDATE run is exempt for the same reason the commit check is — it is
     * the act that makes the two agree again, honestly, by re-measuring. */
    if (mask && !g->updating && (!saw_mask || strcmp(got_mask, mask) != 0)) {
        cq_h_fail(__FILE__, __LINE__,
                  "goldens: %s says it was measured at \"%s\" but the suite "
                  "declares \"%s\" (bd 2r5). A count is a function of (W, "
                  "operand mask); regenerate with CQOPS_UPDATE_GOLDENS=1",
                  path, saw_mask ? got_mask : "(no measured-at line)", mask);
        return gold_abandon(g);
    }

    return 1;
}

int cq_gold_check(cq_gold *g, const char *kernel, const char *pass, int W,
                  uint64_t x, uint64_t cx, uint64_t ccx)
{
    for (size_t i = 0; i < g->n; i++) {
        cq_gold_row *r = &g->v[i];
        if (r->W != W || strcmp(r->kernel, kernel) || strcmp(r->pass, pass))
            continue;

        r->visited = 1;
        if (g->updating) { r->x = x; r->cx = cx; r->ccx = ccx; return 1; }

        if (r->x == x && r->cx == cx && r->ccx == ccx) return 1;

        cq_h_fail(__FILE__, __LINE__,
                  "L4 %s/%s W=%d: got (NOT %llu, CNOT %llu, Toffoli %llu; "
                  "total %llu), golden (%llu, %llu, %llu; total %llu)",
                  kernel, pass, W,
                  (unsigned long long)x, (unsigned long long)cx,
                  (unsigned long long)ccx,
                  (unsigned long long)(x + cx + ccx),
                  (unsigned long long)r->x, (unsigned long long)r->cx,
                  (unsigned long long)r->ccx,
                  (unsigned long long)(r->x + r->cx + r->ccx));
        return 0;
    }

    if (!g->updating) {
        cq_h_fail(__FILE__, __LINE__,
                  "L4 %s/%s W=%d: no golden pinned. Measured (NOT %llu, "
                  "CNOT %llu, Toffoli %llu)", kernel, pass, W,
                  (unsigned long long)x, (unsigned long long)cx,
                  (unsigned long long)ccx);
        return 0;
    }

    cq_gold_row *r = push_row(g);
    if (!r) return 0;
    snprintf(r->kernel, sizeof r->kernel, "%s", kernel);
    snprintf(r->pass,   sizeof r->pass,   "%s", pass);
    r->W = W; r->x = x; r->cx = cx; r->ccx = ccx; r->visited = 1;
    return 1;
}

int cq_gold_close(cq_gold *g)
{
    int ok = 1;

    if (g->updating) {
        /* NO MASK, NO FILE. cq_gold_open's NULL is a READER's escape hatch;
         * letting it reach the writer would mint exactly the unlabelled golden
         * bd 2r5 is about, and would do it silently. */
        if (!g->mask || !g->mask[0] ||
            strlen(g->mask) >= CQ_GOLD_MASK_MAX || strchr(g->mask, '\n')) {
            cq_h_fail(__FILE__, __LINE__,
                      "goldens: refusing to write %s — its `mask` is absent, "
                      "longer than %d bytes, or not one line. A golden that "
                      "does not name the operand mask it was measured at pins "
                      "nothing, and one whose mask does not round-trip fails "
                      "its own cross-check next run (bd 2r5)",
                      g->path, CQ_GOLD_MASK_MAX - 1);
            return gold_abandon(g);
        }

        FILE *f = fopen(g->path, "w");
        if (!f) {
            cq_h_fail(__FILE__, __LINE__, "goldens: cannot write %s", g->path);
            ok = 0;
        } else {
            fprintf(f,
                "# %s\n"
                "# libcqops L4 gate-count goldens. PIN COUNTS, NEVER TRACES\n"
                "# (risk R5): a trace churns on D4 free-list order and D6\n"
                "# non-demotion, neither of which changes the circuit.\n"
                "#\n"
                "# bennett: %s\n"
                "# measured-at: %s\n"
                "#\n"
                "# BOTH OF THOSE LINES ARE DATA, NOT DECORATION: cq_gold_open\n"
                "# reads them back, and a checking run fails if either\n"
                "# disagrees with the snapshot on disk or with what the suite\n"
                "# declares. The mask matters because operand folds still fire\n"
                "# (a^0 and x&0 legitimately emit fewer gates, which is what L5\n"
                "# proves), so a count is a function of (W, operand mask) — and\n"
                "# a golden that does not say which mask it was taken at pins\n"
                "# nothing. The line above is per-file for the reason bd 2r5\n"
                "# records: as a literal in the writer it was false in four of\n"
                "# the twelve files and nothing could contradict it.\n"
                "#\n"
                "# WHY ALL-QUANTUM IS THE RIGHT MASK FOR A KERNEL GOLDEN: with\n"
                "# no demotion (D6) a mask can only drift TOWARDS Q, so it is\n"
                "# the FIXED POINT, and the one mask where forward and unc\n"
                "# agree. A file measured anywhere else says so above, and owes\n"
                "# the reader a note saying why.\n"
                "#\n"
                "# forward and unc are pinned SEPARATELY and their equality is\n"
                "# NOT an invariant (Rule 14, PRD §10): _unc may legitimately\n"
                "# emit MORE gates than the forward did, because a rotation on\n"
                "# a source between the two materialises bits that were\n"
                "# constants at forward time. Do not 'fix' a difference.\n"
                "#\n"
                "# Regenerate: CQOPS_UPDATE_GOLDENS=1 ctest --test-dir\n"
                "# build-release -R kernel   (ctest does NOT forward trailing\n"
                "# --args to test binaries, so the env var is the mechanism;\n"
                "# --update-goldens works when running the binary directly.)\n"
                "#\n",
                g->title, g->commit[0] ? g->commit : "(unknown)", g->mask);
            if (g->notes) fputs(g->notes, f);
            fputs("# kernel   pass      W   NOT  CNOT  Toffoli\n", f);

            for (size_t i = 0; i < g->n; i++) {
                cq_gold_row *r = &g->v[i];
                if (!r->visited) continue;   /* stale rows do not survive */
                fprintf(f, "%-9s %-8s %3d %5llu %5llu %8llu\n",
                        r->kernel, r->pass, r->W,
                        (unsigned long long)r->x, (unsigned long long)r->cx,
                        (unsigned long long)r->ccx);
            }
            fclose(f);
            fprintf(stderr, "# goldens: rewrote %s\n", g->path);
        }
    } else if (g->loaded) {
        /* A row nobody looked at is a coverage hole the file claims is
         * covered. It is the same failure shape as a silent cap. */
        for (size_t i = 0; i < g->n; i++)
            if (!g->v[i].visited) {
                cq_h_fail(__FILE__, __LINE__,
                          "goldens: %s pins %s/%s W=%d and nothing checked it",
                          g->path, g->v[i].kernel, g->v[i].pass, g->v[i].W);
                ok = 0;
            }
    }

    free(g->v);
    g->v = NULL; g->n = g->cap = 0;
    return ok;
}
