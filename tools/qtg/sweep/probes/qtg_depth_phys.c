/* probes/qtg_depth_phys.c -- qtg_depth.c plus the S9 CLASSICAL CONTROL-PLANE
 * VOLUME REPORT (qec_physics_report_get).  Identical driver; the only addition
 * is the report dump after the resource row, so a NOISY arm can be SHOWN to
 * have sampled rather than assumed to have.
 *
 * The getter REFUSES (-1, writing nothing) when the decoder is OFF, so this
 * prints a refusal line for a noiseless config rather than a zero record.
 *
 * Everything below the header is qtg_depth.c verbatim -- including its own
 * provenance and the include-order trick that selects the instance.
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
    if (c) {
        qec_physics_report pr;
        if (qec_physics_report_get(c, &pr) == 0)
            fprintf(stderr,
                "PHYS bits=%" PRIu64 " bits_x=%" PRIu64 " bits_z=%" PRIu64
                " rounds=%" PRIu64 " round_max=%" PRIu64 " round_min=%" PRIu64
                " decodes=%" PRIu64 " find=%" PRIu64 " union=%" PRIu64
                " grow=%" PRIu64 " peel=%" PRIu64 " corr=%" PRIu64
                " p=(%g,%g,%g,%g) d=%" PRIu32 " decoder=%" PRIu32
                " seed=%" PRIu64 " noise_seed=%" PRIu64 " outcome_seed=%" PRIu64
                " shot=%" PRIu64 "\n",
                pr.syndrome_bits_total, pr.syndrome_bits_x, pr.syndrome_bits_z,
                pr.syndrome_rounds, pr.syndrome_bits_round_max, pr.syndrome_bits_round_min,
                pr.decode_invocations, pr.uf_find_ops, pr.uf_union_ops,
                pr.uf_grow_steps, pr.peel_edge_ops, pr.corrections_applied,
                pr.p_x, pr.p_y, pr.p_z, pr.p_m, pr.distance, pr.decoder,
                pr.seed, pr.noise_seed, pr.outcome_seed, pr.shot_index);
        else
            fprintf(stderr, "PHYS: report refused (decoder off)\n");
    }
    return rc;
}
