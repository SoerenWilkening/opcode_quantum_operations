/* probes/qtg_depth.c -- run tools/qtg's gate stream through the qec sink and report
 * the resource row: distance, physical gates, qec_depth, T, CX, H, and the qubit
 * columns (n_physical, n_logical, n_reserved, n_above_reserve).
 *
 * PROVENANCE: vendored from CQ_lang_hardware's own measurement scratch
 * (.scratch/cnot-latency/probes/qtg_depth.c, at CQ_lang_hardware c5e1101), with the
 * four qubit columns added here. It is a MEASUREMENT INSTRUMENT, not library code:
 * nothing under src/ or shim/ links it, and `make test` does not build it.
 *
 * HOW IT REACHES THE QEC SINK WITHOUT TOUCHING tools/qtg/qtg.c: two #defines and a
 * textual include. `cq_sink_counter` is redirected to a forwarding sink that resolves
 * cq_sink_active() per call, and `cq_ctx_init` to a wrapper that calls
 * cq_sink_qec_bind AFTER the context exists -- the two-calls-on-either-side install
 * M25 documents, which no single call site can satisfy.
 *
 * THE INSTANCE IS CHOSEN BY INCLUDE ORDER. qtg.c says #include "instance.h"; put
 * sweep/n20/ ahead of tools/qtg/ on the -I line and it compiles the KP_20 instance
 * instead of the shipped KP_4 one. See sweep/README.md.
 */
#include "instance.h"
#include "controlled.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/addacc.h"
#include "kernels/cmp.h"
#include "rotate.h"
#include "shadow.h"
#include "sink_count.h"
#include "sink_printf.h"
#include "sink_qec.h"
#include <qec/qec.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#define LIVE (cq_sink_active())
static void tr_x(void *u, uint32_t q) { (void)u; LIVE->x(LIVE->user, q); }
static void tr_mz(void *u, uint32_t q) { (void)u; LIVE->mz(LIVE->user, q); }
static void tr_cx(void *u, uint32_t c, uint32_t t) { (void)u; LIVE->cx(LIVE->user, c, t); }
static void tr_ccx(void *u, uint32_t a, uint32_t b, uint32_t t) { (void)u; LIVE->ccx(LIVE->user, a, b, t); }
static void tr_ry(void *u, uint32_t q, double a) { (void)u; LIVE->ry(LIVE->user, q, a); }
static void tr_rz(void *u, uint32_t q, double a) { (void)u; LIVE->rz(LIVE->user, q, a); }
static cq_sink qec_inner(void) { cq_sink s; s.x = tr_x; s.cx = tr_cx; s.ccx = tr_ccx; s.ry = tr_ry; s.rz = tr_rz; s.mz = tr_mz; s.user = NULL; return s; }
static void ctx_init_bound(cq_ctx* c, const cq_sink* s) {
    cq_ctx_init(c, s);
    cq_sink_qec_bind(&c->pool);
    qec_ctx* q = (qec_ctx*)cq_sink_qec_handle();
    fprintf(stderr, "QEC bound: %s d=%" PRIu32 "\n", q ? "yes" : "NO", q ? qec_distance(q) : 0u);
}
#define cq_sink_counter(c) qec_inner()
#define cq_ctx_init(c, s) ctx_init_bound(c, s)
#define main qtg_main
#include "qtg.c"
#undef main
int main(int argc, char** argv) {
    setenv("CQOPS_SINK", "qec", 1);
    cq_sink_qec_register();
    int rc = qtg_main(argc, argv);
    qec_ctx* c = (qec_ctx*)cq_sink_qec_handle();
    if (c) fprintf(stderr, "QEC d=%" PRIu32 " gates=%" PRIu64 " depth=%" PRIu64 " T=%" PRIu64 " CX=%" PRIu64 " H=%" PRIu64
        " nphys=%" PRIu32 " nlog=%" PRIu32 " reserved=%" PRIu64 " above=%" PRIu64 "\n",
        qec_distance(c), qec_total(c), qec_depth(c), qec_count(c, QEC_GATE_T), qec_count(c, QEC_GATE_CX), qec_count(c, QEC_GATE_H),
        qec_n_physical(c), qec_n_logical(c), qec_n_reserved(c), qec_n_above_reserve(c));
    else fprintf(stderr, "QEC: no qec sink bound\n");
    return rc;
}
