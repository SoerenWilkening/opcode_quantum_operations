/* probes/qtg_pl_memory.c -- the pL_memory experiment at the QTG's OWN derived
 * code distance, driven from the QTG's own noisy config.
 *
 *   qtg_pl_memory <config.json> [rounds] [shots] [logical_q]
 *
 * WHY THIS RATHER THAN qec's OWN examples/pl_sweep: pl_sweep's `sweep` and
 * `report` arms take a single --p and set p_x = p_y = p_z = p/3, p_m = p
 * (pl_sweep.c:257).  The channel asked for here is p_x = p_y = p_z = p_m = 1e-3,
 * which that mapping cannot express -- only pl_sweep's `exact` arm reads the four
 * rates separately, and that arm is the truncated enumeration, not the Monte
 * Carlo.  So the rates come from the CONFIG, where they are stated exactly, and
 * d comes from qec_create deriving it as the sweep's own arms do (d=9 at n=4,
 * d=13 at n=20) rather than from a hand-built context.
 *
 * WHAT THE NUMBER IS -- qec.h's D16 scope, carried verbatim because it is the
 * whole of the caveat: a PER-PATCH MEMORY logical error rate for an IDLING patch
 * under the UNWEIGHTED union-find decoder.  Gadget- and injection-level error are
 * UNMODELLED -- no merge, no split, no injection and no factory round is sampled.
 * IT MUST NEVER BE COMPOSED WITH THE T COUNT to produce a circuit-level figure,
 * and it does not feed compute_code_distance.  It is an UPPER BOUND for that
 * decoder, not the code's intrinsic performance.
 *
 * DEDICATED CONTEXT.  qec_pl_memory emits real counted gates, so this program
 * runs the experiment and NOTHING else -- it never touches the QTG circuit.  The
 * gate counts it prints are the experiment's own and are not a circuit cost.
 */
#include <qec/qec.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <config.json> [rounds] [shots] [q]\n", argv[0]); return 2; }
    const uint64_t rounds = (argc > 2) ? strtoull(argv[2], NULL, 10) : 5u;
    const uint64_t shots  = (argc > 3) ? strtoull(argv[3], NULL, 10) : 1000u;
    const uint32_t q      = (argc > 4) ? (uint32_t)strtoul(argv[4], NULL, 10) : 0u;

    qec_ctx* c = qec_create(argv[1]);
    if (!c) { fprintf(stderr, "%s: qec_create FAILED\n", argv[1]); return 1; }

    /* Patches are lazy: qec_idle(ctx, q, 0) is the zero-round, zero-gate gate
     * that builds one (qec.h's validity window for qec_pl_memory). */
    if (qec_idle(c, q, 0u) != 0) { fprintf(stderr, "qec_idle refused\n"); qec_destroy(c); return 1; }

    double   pl = -1.0;
    uint64_t failures = 0u;
    if (qec_pl_memory(c, q, rounds, shots, &pl, &failures) != 0) {
        fprintf(stderr, "qec_pl_memory REFUSED (d=%" PRIu32 ", rounds=%" PRIu64 ", shots=%" PRIu64 ")\n",
                qec_distance(c), rounds, shots);
        qec_destroy(c);
        return 1;
    }

    /* Binomial 95 % interval, as pl_sweep prints it: quote it or do not quote pL. */
    const double half = (shots > 0u) ? 1.96 * sqrt(pl * (1.0 - pl) / (double)shots) : 0.0;
    printf("PL config=%s d=%" PRIu32 " rounds=%" PRIu64 " shots=%" PRIu64
           " q=%" PRIu32 " failures=%" PRIu64 " pL=%.6g ci95=+-%.3g\n",
           argv[1], qec_distance(c), rounds, shots, q, failures, pl, half);

    qec_physics_report pr;
    if (qec_physics_report_get(c, &pr) == 0)
        printf("PLPHYS bits=%" PRIu64 " rounds=%" PRIu64 " decodes=%" PRIu64
               " find=%" PRIu64 " union=%" PRIu64 " grow=%" PRIu64
               " peel=%" PRIu64 " corr=%" PRIu64 " p=(%g,%g,%g,%g) d=%" PRIu32
               " seed=%" PRIu64 " noise_seed=%" PRIu64 " outcome_seed=%" PRIu64 "\n",
               pr.syndrome_bits_total, pr.syndrome_rounds, pr.decode_invocations,
               pr.uf_find_ops, pr.uf_union_ops, pr.uf_grow_steps, pr.peel_edge_ops,
               pr.corrections_applied, pr.p_x, pr.p_y, pr.p_z, pr.p_m, pr.distance,
               pr.seed, pr.noise_seed, pr.outcome_seed);

    qec_destroy(c);
    return 0;
}
