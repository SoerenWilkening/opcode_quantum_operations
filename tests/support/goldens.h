/* tests/support/goldens.h — L4's pinned gate counts, on disk.
 *
 * BEYOND PLAN §2.2's LIST OF FIVE, like death.[ch] before it. §2.2 names
 * harness / mock_sink / refmodel / bitkinds / poolcheck; plan §4's Phase B
 * gate then asks every kernel step for "(NOT, CNOT, Toffoli) at each W matches
 * the golden ... tests/goldens/, --update-goldens to regenerate", and assigns
 * that to no file. This is it.
 *
 * WHY A FILE AND NOT A STATIC TABLE. Risk R3 is that Bennett.jl drift
 * invalidates the goldens silently, and its mitigation is "goldens carry the
 * Bennett commit in a header comment". A comment nobody reads is not a
 * mitigation, so the commit line is DATA here: cq_gold_open compares it
 * against third_party/bennett/COMMIT and fails if the snapshot has been
 * re-pinned without the goldens being regenerated. That check cannot exist in
 * a static table compiled into the suite.
 *
 * WHAT L4 IS FOR, so nobody weakens it into a smoke test: L1 and L5 catch
 * bugs; L4 is what stops a "harmless" refactor from silently doubling the
 * T-count (Rule 10). It is also WEAKER THAN IT LOOKS — a matching total is not
 * evidence a sandwich cancelled, because K12's reversed forward list has a
 * different multiset with the identical total. Always match the full tuple,
 * never the total, and never read a green L4 as an ancilla-clean result.
 */
#ifndef CQOPS_TEST_GOLDENS_H
#define CQOPS_TEST_GOLDENS_H

#include <stdint.h>
#include <stddef.h>

/* The `# measured-at:` value's on-disk cap, and the parse buffer's size. The
 * writer refuses a longer mask rather than truncating one: a mask that did not
 * round-trip would fail its own cross-check on the very next run, which reads
 * as a stale golden rather than as an over-long string. */
#define CQ_GOLD_MASK_MAX 256

typedef struct {
    char     kernel[24];
    char     pass[16];      /* "forward" | "unc" — pinned SEPARATELY, Rule 14 */
    int      W;
    uint64_t x, cx, ccx;
    int      visited;
} cq_gold_row;

typedef struct {
    cq_gold_row *v;
    size_t       n, cap;
    const char  *path;
    const char  *title;     /* one line, written into the regenerated header */
    const char  *mask;      /* the measurement condition; see cq_gold_open */
    const char  *notes;     /* optional extra header block; see below */
    char         commit[80];
    int          updating;
    int          loaded;
} cq_gold;

/* True iff this run regenerates rather than checks. Two triggers, and the
 * reason there are two is that CTEST DOES NOT FORWARD TRAILING ARGUMENTS to
 * test executables — plan §4's `ctest -R kernel -- --update-goldens` cannot
 * work as written. `--update-goldens` therefore applies when the binary is run
 * DIRECTLY, and the environment variable CQOPS_UPDATE_GOLDENS=1 is the one
 * that survives ctest. Both are documented in CLAUDE.md's Build & Test. */
int cq_gold_updating(void);

/* `mask` — THE MEASUREMENT CONDITION THIS FILE'S COUNTS WERE TAKEN UNDER. One
 * line, no leading `# `, no newline; it is written into the regenerated header
 * on a `# measured-at:` line and read back on every load.
 *
 * IT IS A PARAMETER BECAUSE IT USED TO BE A LITERAL IN THE WRITER, AND THE
 * LITERAL WAS FALSE IN FOUR FILES (bd 2r5). The old boilerplate asserted
 * "measured at operand mask ALL-QUANTUM on BOTH operands" into every golden
 * cq_gold_close wrote. K10's mux has THREE sources; K5's casts have ONE; K4's
 * shift amount is CONSTRAINED CLASSICAL (`classical[1] = ~0`), so that file's
 * second operand is the one kind the claim denies; and L7's Grover is a
 * program, where no operand mask is a parameter at all. A count is a function
 * of (W, operand mask) — CLAUDE.md Rule 10's first L4 note — so a golden whose
 * stated mask is wrong is a golden that documents the wrong function.
 *
 * IT IS ALSO DATA, NOT PROSE, EXACTLY AS `# bennett:` IS. cq_gold_open parses
 * the line back and a checking run fails if it disagrees with what the suite
 * declares, so the claim is falsifiable rather than decorative. That is the
 * whole difference between this and the comment it replaced: nothing could
 * ever have contradicted the literal.
 *
 * Pass NULL only for a READER — a suite that opens someone else's golden to
 * audit its rows and writes nothing (tests/test_unc.c). NULL skips the
 * comparison, and cq_gold_close REFUSES to write a file without a mask, so the
 * escape hatch cannot be used to mint an unlabelled golden.
 *
 * `g->notes` — SET IT AFTER cq_gold_open AND BEFORE cq_gold_close, or leave the
 * NULL cq_gold_open puts there. Every line must already begin with `# `; it is
 * written verbatim between the fixed preamble and the column line.
 *
 * IT EXISTS BECAUSE THE FIXED PREAMBLE IS A CLAIM, NOT DECORATION. What is left
 * of that preamble after `mask` moved out is still a KERNEL golden's claim: it
 * says `forward` and `unc` are pinned separately and it labels the three
 * columns `NOT CNOT Toffoli` — neither true of L7's program-scale golden, whose
 * `pass` column names a metric FAMILY and whose second family is
 * `(ry, rz, peak)`. A reader who takes the column line at face value there reads
 * a peak qubit count as a Toffoli count.
 *
 * Loads `path`, or starts an empty set if it is missing AND this is an update
 * run (a missing golden in a checking run is a hard failure — an unpinned
 * count is not a passing one).
 *
 * `commit_file` is third_party/bennett/COMMIT. The loaded file's own commit
 * line must match it, or the run fails: that is risk R3's mitigation with
 * teeth. Pass NULL to skip the comparison only for a kernel with no upstream
 * construction at all. */
int cq_gold_open(cq_gold *g, const char *path, const char *title,
                 const char *mask, const char *commit_file);

/* Checks one measurement, or records it in an update run. Reports through the
 * harness, so a mismatch names the kernel, the pass and the width, and prints
 * both tuples. Returns 1 on match. */
int cq_gold_check(cq_gold *g, const char *kernel, const char *pass, int W,
                  uint64_t x, uint64_t cx, uint64_t ccx);

/* Rewrites the file in an update run; in a checking run, verifies that EVERY
 * row in the file was visited. An unvisited row is a coverage hole that a
 * green run would otherwise report as coverage — the golden claims a width is
 * pinned and nothing ever looked at it.
 *
 * AN UPDATE RUN WITH NO `mask` IS A HARD FAILURE AND WRITES NOTHING. That is
 * the half of bd 2r5's fix that cannot be worked around: the reader's NULL
 * escape hatch stops at the writer, so the only way to produce a golden is to
 * say what mask it was produced at. */
int cq_gold_close(cq_gold *g);

#endif /* CQOPS_TEST_GOLDENS_H */
