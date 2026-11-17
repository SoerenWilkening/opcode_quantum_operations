/* probes/qtg_mint.c -- the MINTED logical-qubit count, which is what the qec config's
 * circuit.n_logical must be.
 *
 * PROVENANCE: vendored verbatim from CQ_lang_hardware .scratch/cnot-latency/probes/
 * at c5e1101.
 *
 * WHY MINTED AND NOT PEAK: the qec sink disables index recycling (PRD 15 D21(b)), so
 * n_logical has to cover every index the program ever takes, not the high-water mark
 * of live ones. At n=4 that is 126 against a peak of 35; at n=20, 930 against 72.
 */
#include "ctx.h"
#include "qubits.h"
#include <stdio.h>
static void ctx_init_norecycle(cq_ctx* c, const cq_sink* s) { cq_ctx_init(c, s); cq_qubits_set_recycle(&c->pool, 0); }
static void report(cq_ctx* c) { fprintf(stderr, "MINTED %u PEAK %u\n", cq_qubits_minted(&c->pool), cq_qubits_peak(&c->pool)); }
#define cq_ctx_init(c, s) ctx_init_norecycle(c, s)
#define cq_ctx_dispose(c) (report(c), cq_ctx_dispose(c))
#include "qtg.c"
