/* tools/l6/l6_residue.c — `bd c55`. The L6 harness's own residue reader.
 *
 * THIS IS NOT PART OF libcqops AND MUST NEVER BECOME PART OF IT. The library
 * exposes cqops_read_residue as a pure READ; the dump was rejected at the
 * library layer on the two counts include/cqops/cqops.h records — it would
 * write into a stream the CALLER owns, and a constructor inside a STATIC
 * archive is dropped along with the member nothing references (measured, see
 * shim/cq_shim_ctx.c), so it would arm on some link lines and not others.
 * Neither objection touches a destructor in a translation unit the LINKER WAS
 * HANDED DIRECTLY, which is what this is: run_slice_cqops.sh compiles it and
 * names the object, so it can never be dropped, and nothing that links libcqops
 * for any other purpose gets it.
 *
 * WHY THE FIXTURE HAS TO DO THE READING AT ALL. The residue lives in the
 * process the fixture runs in, and D15 §3's report on stderr is one-shot BY
 * DESIGN — the corpus strands across a great many frees and a line per stranded
 * qubit would bury the one that matters. So from outside the process the split
 * was a BOOLEAN. "The certificate discharged every rail in N of M fixtures" was
 * already sayable from that; "and in the others it left X qubits convicted and
 * Y merely unproven" — the sentence D15's residue split was built to make — was
 * not. One line written from inside the process is what makes it sayable. The
 * figures belong in the run's report and in `bd remember`, never here.
 *
 * IT WRITES TO A FILE AND NEVER TO stdout OR stderr. `$OUT.out` and `$OUT.err`
 * are what l6_run.py classifies a fixture from, and our own diagnostic
 * appearing in either would be the harness measuring itself. Absent
 * CQOPS_L6_RESIDUE it does nothing at all, which is why adding this object to
 * the link line is safe for every other caller of run_slice_cqops.sh.
 *
 * A FIXTURE THAT ABORTS WRITES NOTHING, AND THAT IS CORRECT RATHER THAN A GAP:
 * abort() runs no destructor, and a DEFERRED or ABORT fixture never reached the
 * end of its program, so it has no final residue to report. The absence of the
 * file IS the fact, and l6_run.py reports it as such rather than as zeroes.
 */
#include <stdio.h>
#include <stdlib.h>

#include "cqops/cqops.h"

__attribute__((destructor))
static void l6_dump_residue(void)
{
    const char *path = getenv("CQOPS_L6_RESIDUE");
    cqops_residue r;
    FILE *f;

    if (!path || path[0] == '\0') return;

    cqops_read_residue(&r);

    f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "stranded_dirty %u\nstranded_unproven %u\n"
               "frees_dirty %u\nfrees_unproven %u\n"
               "stranded_qubits %u\nstrand_reports %u\n",
            r.stranded_dirty, r.stranded_unproven,
            r.frees_dirty, r.frees_unproven,
            r.stranded_qubits, r.strand_reports);
    fclose(f);
}
