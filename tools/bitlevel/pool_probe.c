/* tools/bitlevel/pool_probe.c — the experiment's pool reader. Linked onto the
 * fixture like tools/l6/l6_residue.c, and like it inert unless $CQOPS_BL_POOL
 * names a path. Reads the process context's pool through the internal headers
 * (peak is not public API; M24 deliberately has no qubit metric, CLAUDE.md).
 * The shim registers no atexit/destructor, so the context is intact here. */
#include <stdio.h>
#include <stdlib.h>
#include "cq_shim_ctx.h"
#include "qubits.h"

__attribute__((destructor))
static void bl_dump_pool(void)
{
    const char *path = getenv("CQOPS_BL_POOL");
    cq_ctx *c;
    FILE *f;
    if (!path || path[0] == '\0') return;
    c = cq_shim_ctx();
    f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "peak %u\nlive %u\nminted %u\nstranded %u\nfree %u\n",
            cq_qubits_peak(&c->pool), cq_qubits_live(&c->pool),
            cq_qubits_minted(&c->pool), cq_qubits_stranded(&c->pool),
            cq_qubits_free(&c->pool));
    fclose(f);
}
