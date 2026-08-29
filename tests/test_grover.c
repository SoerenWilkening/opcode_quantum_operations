/* tests/test_grover.c — L7, Step 25. PRD §12(2) and §12(3), as amended by
 * PRD §15 D22 (`bd ye7`, closed 2026-08-28).
 *
 * PRD §12's program, written straight against the frozen ABI: `cq_runtime_abi.h`
 * plus `cq_shim.h`, no CQ_lang and no `cqc` anywhere. That is possible because
 * §12 has three claims and only the FIRST of them needs CQ_lang — and D22's
 * whole content is that THE THREE DO NOT SHARE A MODE:
 *
 *   (1) "compiles through cqc, links, emits a NON-EMPTY gate stream" — needs
 *       CQ_lang's front end, so it lives in `tools/l7/` behind
 *       `-DCQOPS_CQLANG_DIR=`, exactly as L6 does, and NOT here;
 *   (2) CLASSICAL MODE — `M_PI/2` becomes `M_PI`, the program runs
 *       deterministically, `cq_measure` returns what plain C computes, and it
 *       costs ZERO gates and ZERO qubits;
 *   (3) QUANTUM MODE — the counter sink's Toffoli count and `cq_qubits_peak()`,
 *       stable across runs and PINNED AS GOLDENS.
 *
 * WHY (2)'s PARENTHETICAL IS RETIRED, WHICH IS THE MEASUREMENT D22 RESTS ON.
 * §12(2) claimed the classical run proves "the oracle's arithmetic circuits are
 * correct". Measured here: in classical mode the whole program emits ZERO gates
 * and takes ZERO qubits, because with `x` all-constant every kernel takes its R9
 * short-circuit and the §3 fold table produces the value outright — so the mul
 * and the icmp were never BUILT and nothing about their circuits was tested.
 * Their circuits are verified by L1, per kernel, at mixed bit-kind masks, which
 * is NORTH_STAR condition 2 and was met at Steps 10–17. What the classical run
 * DOES prove is the value and L5 AT PROGRAM SCALE, and both are asserted below.
 *
 * WHY THE GOLDEN IS TAKEN IN THE QUANTUM MODE. A golden pinned against the
 * classical run is a tuple of ZEROES and a vacuously green acceptance gate.
 *
 * WHAT THIS FILE DOES NOT MEASURE IS THE T-COUNT, AND THAT IS D22's OTHER HALF.
 * `cq_count_t` is `7 × ccx` ported from Bennett and is a LOWER BOUND the moment
 * §7's general rows fire (`bd qi9`) — which is exactly what the quantum mode
 * does. D22 puts the rotation term in M25: `tests/test_grover_qec.c`, opt-in
 * behind `-DCQOPS_QEC_DIR=`. THE TWO HALVES ARE SPLIT BECAUSE NEITHER
 * INSTRUMENT CAN MAKE THE OTHER'S CLAIM — this file pins the rotation ALPHABET
 * (a counting sink knows WHICH rotations were emitted and cannot cost them), and
 * that one costs each letter (the qec sink costs a rotation and never sees which
 * program asked for it). Delete either and the T-count becomes an assumption.
 */
#include "cq_runtime_abi.h"
#include "cq_shim.h"
#include "cq_shim_ctx.h"

#include "qubits.h"
#include "sink.h"

#include "sink_count.h"

#include "support/goldens.h"
#include "support/harness.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GROVER_W       8
#define GROVER_TARGET  42u

/* --- a counting sink, local so no other suite's totals can leak in -------- */

static unsigned g_x, g_cx, g_ccx, g_ry, g_rz, g_mz;

/* THE ROTATION ALPHABET, AND IT IS A BITWISE RECORD ON PURPOSE. `0.0` and
 * `-0.0` compare equal in C and are different gates to emit, so every angle in
 * this project is compared bitwise (`mock_sink.h`); here the same discipline is
 * what makes "the program emits exactly two distinct rotations" a claim rather
 * than a rounding. `*_alien` counts every later angle whose BITS differ from the
 * first one seen for that op. */
static double   g_ry_first, g_rz_first;
static unsigned g_ry_alien, g_rz_alien;

static int same_bits(double a, double b)
{
    return memcmp(&a, &b, sizeof a) == 0;
}

static void s_x  (void *u, uint32_t q) { (void)u; (void)q; g_x++;   }
static void s_cx (void *u, uint32_t a, uint32_t b)
                                       { (void)u; (void)a; (void)b; g_cx++; }
static void s_ccx(void *u, uint32_t a, uint32_t b, uint32_t c)
                       { (void)u; (void)a; (void)b; (void)c; g_ccx++; }
static void s_ry (void *u, uint32_t q, double t)
{
    (void)u; (void)q;
    if (g_ry++ == 0u) g_ry_first = t;
    else if (!same_bits(t, g_ry_first)) g_ry_alien++;
}
static void s_rz (void *u, uint32_t q, double t)
{
    (void)u; (void)q;
    if (g_rz++ == 0u) g_rz_first = t;
    else if (!same_bits(t, g_rz_first)) g_rz_alien++;
}
static void s_mz (void *u, uint32_t q) { (void)u; (void)q; g_mz++;  }

static cq_sink g_probe = { s_x, s_cx, s_ccx, s_ry, s_rz, s_mz, NULL };

/* What one run reports. `live_after_each` is the pool's live count taken at the
 * END of every iteration, i.e. after the oracle has been uncomputed and its
 * rails freed — the number that says the workspace came back. */
typedef struct {
    uint8_t  measured;
    unsigned x, cx, ccx, ry, rz;
    uint32_t peak, minted, live_end, stranded;
    uint32_t live_after_each[4];
    double   ry_angle, rz_angle;
    unsigned ry_alien, rz_alien;
} grover_run;

/* --- the reference, written from PRD §12's C and not from the run ---------
 *
 * `cq_theta(v, pi)` is Ry(π) on EVERY BIT of the rail, and on a bit that is
 * already a definite classical constant Ry(π) is `X` up to a GLOBAL sign, which
 * is unobservable — so the bit flips and stays classical (PRD §7, Rule 15).
 * W bits flipping is `~v`. `cq_phi(·, pi)` is diagonal on a definite value, so
 * it is a global phase and does nothing at all. The oracle is computed and its
 * flag is consumed only by that phase, so classically it moves nothing — but it
 * is written out rather than elided, because the point of the reference is to be
 * the same PROGRAM, independently derived, not the same shortcut. */
static uint8_t ref_grover(int iters)
{
    uint8_t x = 0u;
    int     it;

    x = (uint8_t)~x;                                   /* cq_theta(x, pi)     */
    for (it = 0; it < iters; it++) {
        uint8_t y   = (uint8_t)(x * 3u + 1u);          /* the oracle          */
        int     hit = (y == (uint8_t)GROVER_TARGET);
        (void)hit;                                     /* cq_phi: a phase     */
        /* cq_phi(x, pi): nothing on a constant rail. */
        x = (uint8_t)~x;                               /* cq_theta(x, pi)     */
    }
    return x;
}

/* --- PRD §12's program, through the frozen ABI ---------------------------- */

/* THE ONE STRUCTURAL DIFFERENCE FROM §12's LISTING IS THE UNCOMPUTE, AND IT IS
 * REQUIRED RATHER THAN A CHOICE. §12 writes the oracle as C locals going out of
 * scope; CQ_lang's pass turns that into the `_unc` call plus `cqrt_free` that
 * appear here (PRD §10 — `cqrt_free` is the sole deallocator and `_unc`
 * reclaims nothing). Writing the forward without the `_unc` would leave `y` and
 * `hit` dirty at their free, which is the very thing D15 strands. */
/* THE PROGRAM ITSELF, WITH THE SINK ALREADY INSTALLED. Split out from
 * `run_grover` so §12(3)'s golden can run it twice — once through the local
 * probe, once through M24's counter sink — without a second transcription of
 * §12. `live_after_each` may be NULL. */
static uint8_t grover_program(double theta, int iters, uint32_t *live_after_each)
{
    int32_t x, y0, y1, hit;
    int     it;

    x = cqrt_alloc_i8(0);
    cqrt_ry_i8(x, theta);                              /* cq_theta            */

    for (it = 0; it < iters; it++) {
        y0  = cq_shim_bin_hl(CQ_SHIM_OP_MUL, GROVER_W, x,  3u, 0u);
        y1  = cq_shim_bin_hl(CQ_SHIM_OP_ADD, GROVER_W, y0, 1u, 0u);
        hit = cq_shim_icmp_hl(CQ_SHIM_PRED_EQ, GROVER_W, y1, GROVER_TARGET, 0u);

        cqrt_rz_i1(hit, M_PI);                         /* cq_phi: the marker  */

        /* THE CHAIN UNCOMPUTES IN REVERSE, ONE `_unc` PER FORWARD, AND EACH ONE
         * RUNS WHILE ITS OWN SOURCES ARE STILL LIVE. `y0` is therefore freed
         * LAST, after the add's `_unc` has consumed it. This is the shape the
         * first draft of this file got wrong and the pool case below caught: a
         * two-stage oracle with one `_unc` freed `y1` un-uncomputed, which
         * stranded 8 qubits per iteration and grew `live` by 8 each time round.
         * That is D15 working exactly as designed — nothing was laundered — and
         * it is the single most useful thing this file demonstrates for the
         * real oracle a bigger program would write. */
        cq_shim_icmp_hl_unc(CQ_SHIM_PRED_EQ, GROVER_W, hit, y1, GROVER_TARGET,
                            0u);
        cqrt_free(hit);
        cq_shim_bin_hl_unc(CQ_SHIM_OP_ADD, GROVER_W, y1, y0, 1u, 0u);
        cqrt_free(y1);
        cq_shim_bin_hl_unc(CQ_SHIM_OP_MUL, GROVER_W, y0, x, 3u, 0u);
        cqrt_free(y0);

        cqrt_rz_i8(x, M_PI);                           /* diffusion           */
        cqrt_ry_i8(x, theta);

        if (live_after_each && it < 4)
            live_after_each[it] = cq_qubits_live(&cq_shim_ctx()->pool);
    }

    return (uint8_t)cqrt_measure_i8(x);
}

static void run_grover(grover_run *r, double theta, int iters)
{
    cq_shim_ctx_reset();
    g_x = g_cx = g_ccx = g_ry = g_rz = g_mz = 0u;
    g_ry_alien = g_rz_alien = 0u;
    g_ry_first = g_rz_first = 0.0;
    cqops_set_sink(&g_probe);

    r->measured = grover_program(theta, iters, r->live_after_each);

    r->peak     = cq_qubits_peak(&cq_shim_ctx()->pool);
    r->minted   = cq_qubits_minted(&cq_shim_ctx()->pool);
    r->live_end = cq_qubits_live(&cq_shim_ctx()->pool);
    r->stranded = cq_qubits_stranded(&cq_shim_ctx()->pool);
    r->x = g_x; r->cx = g_cx; r->ccx = g_ccx; r->ry = g_ry; r->rz = g_rz;
    r->ry_angle = g_ry_first; r->rz_angle = g_rz_first;
    r->ry_alien = g_ry_alien; r->rz_alien = g_rz_alien;

    cqops_set_sink(NULL);
}

/* NOTE ON THE ORACLE'S SHAPE. §12's `f` is any plain-C arithmetic; `3x + 1` is
 * chosen because it is the SMALLEST one that is honest about two things at once.
 * It reaches BOTH K11 (mul) and K6 (add), so it emits Toffolis — a pure-XOR
 * oracle would not, and would make §12(3)'s Toffoli golden vacuous for a second,
 * quieter reason than the mode confusion `bd ye7` records. And it is TWO stages,
 * so the uncompute is a CHAIN rather than a single call, which is where the real
 * difficulty of a Grover oracle lives and where a one-stage fixture would have
 * proved nothing. A real oracle is this shape with more links. */

/* -------------------------------------------------------------------------
 * 1. Classical mode — the value.
 * ------------------------------------------------------------------------- */

CQ_TEST(classical_mode_returns_what_plain_c_computes)
{
    int iters;

    for (iters = 0; iters <= 3; iters++) {
        grover_run r;

        run_grover(&r, M_PI, iters);
        CHECK_EQ((long long)r.measured, (long long)ref_grover(iters));
    }
}

/* -------------------------------------------------------------------------
 * 2. Classical mode — the cost, and its pairing witness.
 * ------------------------------------------------------------------------- */

/* L5 AT PROGRAM SCALE, AND IT IS THE HEADLINE MEASUREMENT OF THIS FILE: a whole
 * Grover iteration over an 8-bit oracle costs ZERO gates and ZERO qubits when
 * the rail never leaves the classical domain. That is also exactly why it is not
 * §12(3)'s golden — there is nothing there to pin.
 *
 * IT IS AN L5-SHAPED ASSERTION, SO IT IS PAIRED (the repo's standing rule: "zero
 * gates" passes just as well against a program that never ran). The quantum arm
 * below runs the SAME function and requires the counts to be non-zero. */
CQ_TEST(classical_mode_costs_zero_gates_and_zero_qubits)
{
    grover_run r;

    run_grover(&r, M_PI, 2);

    CHECK_EQ(r.x, 0u);
    CHECK_EQ(r.cx, 0u);
    CHECK_EQ(r.ccx, 0u);
    CHECK_EQ(r.ry, 0u);
    CHECK_EQ(r.rz, 0u);
    CHECK_EQ(r.peak, 0u);
    CHECK_EQ(r.minted, 0u);
}

CQ_TEST(quantum_mode_builds_the_circuit_classical_mode_folds_away)
{
    grover_run q, c;

    run_grover(&q, M_PI / 2.0, 2);
    run_grover(&c, M_PI, 2);

    /* The pairing. Without this the case above is satisfied by a broken shim. */
    CHECK(q.ccx > 0u);
    CHECK(q.cx > 0u);
    CHECK(q.ry > 0u);
    CHECK(q.peak > 0u);
    CHECK_EQ(c.ccx, 0u);

    /* The GOLDEN is the next case. This one is the pairing witness and stays
     * golden-free on purpose: `CQOPS_UPDATE_GOLDENS=1` can bless a moved tuple,
     * and it must never be able to bless a program that stopped emitting. */
}

/* -------------------------------------------------------------------------
 * 3. The pool across the oracle's compute / uncompute bracket.
 * ------------------------------------------------------------------------- */

/* THE STRONGEST CLAIM IN THIS FILE, AND THE ONE NO KERNEL SUITE CAN MAKE: after
 * each iteration the pool is back to exactly the search rail's W lanes, on a
 * rail that a general `Ry` has ROTATION-TAINTED — so the shadow discharges
 * nothing at those frees and the qubits come back only because D15's observed
 * undo certificate pairs each forward with its `_unc` over the call stream.
 * Measured: live 8 after every iteration, minted 97, stranded 0.
 *
 * `minted` IS NOT COMPARED ACROSS THE LOOP and must never be: it is monotone
 * (`minted == live + free`), so requiring it to return is requiring the oracle
 * never to allocate. `live` is the one that comes back. */
CQ_TEST(the_oracle_workspace_is_reclaimed_on_a_rotation_tainted_rail)
{
    grover_run r;
    int        it;

    run_grover(&r, M_PI / 2.0, 3);

    for (it = 0; it < 3; it++)
        CHECK_EQ(r.live_after_each[it], (uint32_t)GROVER_W);

    CHECK_EQ(r.live_end, (uint32_t)GROVER_W);
    CHECK_EQ(r.stranded, 0u);
    CHECK(r.minted > (uint32_t)GROVER_W);   /* the workspace really was taken */
}

/* -------------------------------------------------------------------------
 * 4. §12(3) — the pinned golden, in the QUANTUM mode and only there.
 * ------------------------------------------------------------------------- */

/* THE GOLDEN FILE IS THE L4 MECHANISM REUSED AT PROGRAM SCALE, and the three
 * key columns mean what this case says they mean rather than what `goldens.h`
 * names them: `kernel` is the program, `pass` is the METRIC FAMILY, and `W` is
 * the ITERATION COUNT. Two families, because §12(3) asks for three numbers of
 * two different kinds and `cq_gold_row` carries a triple:
 *
 *     gates     -> (NOT, CNOT, Toffoli)   the counter sink's classical stream
 *     rot+pool  -> (ry, rz, peak)         the rotations, and cq_qubits_peak()
 *
 * `commit_file` is `third_party/bennett/COMMIT` and that is not ceremony: the
 * oracle's mul, add and compare ARE Bennett constructions, so re-pinning the
 * snapshot invalidates this golden exactly as it invalidates a kernel's (risk
 * R3), and the loader hard-errors rather than letting it drift quietly.
 *
 * §12(3) SAYS "THE COUNTER SINK", SO THIS CASE USES M24 rather than the local
 * probe — and cross-checks the two, which is the only thing in the file that
 * can catch a probe that miscounts. */
CQ_TEST(quantum_mode_counts_are_pinned_and_the_counter_sink_agrees)
{
    cq_gold  g;
    int      iters;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/grover.counts",
                      "L7 Grover, PRD §12(3) — QUANTUM mode (theta = pi/2)",
                      "NO OPERAND MASK IS A PARAMETER HERE — this is a whole "
                      "program, not a kernel call: the rail is all-quantum "
                      "because a general Ry put it there. At ctrl_depth 0",
                      CQOPS_BENNETT_COMMIT))
        return;

    /* What is left of the fixed preamble after bd 2r5 moved the mask out to
     * the `measured-at` line above is still a KERNEL golden's claim, and two of
     * its sentences are false here; `goldens.h` explains why the correction is
     * a per-file note rather than a per-caller preamble. */
    g.notes =
        "# THIS IS A PROGRAM-SCALE GOLDEN, NOT A KERNEL ONE, SO TWO\n"
        "# SENTENCES ABOVE DO NOT APPLY AND THE COLUMN LINE BELOW IS\n"
        "# ONLY HALF RIGHT. There is no `forward`/`unc` split here (the\n"
        "# program contains both), so nothing above about pinning the two\n"
        "# passes separately has anything to pin, and the three key\n"
        "# columns mean:\n"
        "#\n"
        "#     kernel = the program        pass = the METRIC FAMILY\n"
        "#     W      = the ITERATION COUNT, not a width (it is 8)\n"
        "#\n"
        "#     gates    -> (NOT, CNOT, Toffoli)   the counter sink\n"
        "#     rot+pool -> (ry,  rz,   peak)      rotations, and\n"
        "#                                        cq_qubits_peak()\n"
        "#\n"
        "# The bennett line IS load-bearing: the oracle's mul, add and\n"
        "# compare are Bennett constructions, so re-pinning the snapshot\n"
        "# invalidates these rows exactly as it invalidates a kernel's.\n"
        "#\n"
        "# Regenerate with -R grover, not -R kernel.\n"
        "#\n";

    for (iters = 0; iters <= 3; iters++) {
        grover_run  r;
        cq_counter *c = cq_sink_counter_register();

        run_grover(&r, M_PI / 2.0, iters);

        /* The same program again, through M24 this time. Two independent
         * recorders of one stream: a disagreement is a bug in whichever is
         * newer, and without it the golden pins the probe rather than the
         * library. */
        cq_shim_ctx_reset();
        cq_count_reset(c);
        cqops_set_sink(cq_sink_by_name("counter"));
        (void)grover_program(M_PI / 2.0, iters, NULL);
        cqops_set_sink(NULL);

        CHECK_EQ((long long)c->x,   (long long)r.x);
        CHECK_EQ((long long)c->cx,  (long long)r.cx);
        CHECK_EQ((long long)c->ccx, (long long)r.ccx);
        CHECK_EQ((long long)c->ry,  (long long)r.ry);
        CHECK_EQ((long long)c->rz,  (long long)r.rz);

        cq_gold_check(&g, "grover", "gates",    iters, r.x,  r.cx, r.ccx);
        cq_gold_check(&g, "grover", "rot+pool", iters, r.ry, r.rz, r.peak);
    }

    cq_gold_close(&g);
}

/* -------------------------------------------------------------------------
 * 5. The same costs, derived rather than pinned.
 * ------------------------------------------------------------------------- */

/* READS NO GOLDEN, SO `CQOPS_UPDATE_GOLDENS=1` CANNOT BLESS IT AWAY — the
 * standing hazard of every L4 row in this project, and it matters more here
 * than anywhere else because this golden is the ACCEPTANCE gate.
 *
 * Two independent claims. (i) EVERY ITERATION COSTS THE SAME, which is a real
 * property rather than a restatement: `x` is all-quantum after the first
 * `cq_theta` and D6 never demotes, so the oracle meets the identical operand
 * mask every time round and the §3 fold table has nothing left to fold. A
 * per-iteration delta that MOVES is a mask drifting, i.e. Rule 14's asymmetry
 * arriving where it does not belong. (ii) THE ROTATIONS HAVE A CLOSED FORM:
 * `cqrt_ry_i8` and `cqrt_rz_i8` rotate EVERY LANE (§7 is per bit — a W-bit
 * `Ry(2π)` contributes `(−1)^W`, so one gate per register would be a miscompile
 * at every even width), and the flag's `cqrt_rz_i1` adds exactly one. */
CQ_TEST(the_per_iteration_cost_is_constant_and_the_rotations_are_closed_form)
{
    grover_run r[4];
    int        i;

    for (i = 0; i <= 3; i++) run_grover(&r[i], M_PI / 2.0, i);

    for (i = 2; i <= 3; i++) {
        CHECK_EQ((long long)(r[i].x   - r[i - 1].x),
                 (long long)(r[1].x   - r[0].x));
        CHECK_EQ((long long)(r[i].cx  - r[i - 1].cx),
                 (long long)(r[1].cx  - r[0].cx));
        CHECK_EQ((long long)(r[i].ccx - r[i - 1].ccx),
                 (long long)(r[1].ccx - r[0].ccx));
    }
    CHECK(r[1].ccx > 0u);                    /* not a constant-zero delta */

    for (i = 0; i <= 3; i++) {
        CHECK_EQ((long long)r[i].ry, (long long)(GROVER_W * (i + 1)));
        CHECK_EQ((long long)r[i].rz, (long long)(i * (GROVER_W + 1)));
    }
}

/* -------------------------------------------------------------------------
 * 6. The rotation ALPHABET — §12(3)'s T-count, first half.
 * ------------------------------------------------------------------------- */

/* THE HALF OF THE T-COUNT THAT ONLY A COUNTING SINK CAN MAKE. `cq_count_t` is
 * `7 × ccx` and is exact iff every rotation the program emits is Clifford
 * (`bd qi9`). Whether a given angle is Clifford is a SYNTHESIS question and
 * belongs to M25 — `tests/test_grover_qec.c` measures it. What belongs HERE is
 * the other operand of that argument: WHICH angles the program emits, which the
 * qec sink structurally cannot know because it is handed one rotation at a time
 * and never sees the program.
 *
 * Bitwise, for the reason `mock_sink.h` gives: `0.0` and `-0.0` are equal in C
 * and are different gates. `*_alien` is what turns "the first angle was π/2"
 * into "every angle was π/2" — a first-angle check alone would pass against a
 * program whose later rotations drifted, which is precisely the shape that
 * would silently invalidate the T-count. */
CQ_TEST(the_rotation_alphabet_is_exactly_ry_half_pi_and_rz_pi)
{
    grover_run r;

    run_grover(&r, M_PI / 2.0, 2);

    CHECK(r.ry > 0u);
    CHECK(r.rz > 0u);
    CHECK(same_bits(r.ry_angle, M_PI / 2.0));
    CHECK(same_bits(r.rz_angle, M_PI));
    CHECK_EQ((long long)r.ry_alien, 0);
    CHECK_EQ((long long)r.rz_alien, 0);
}

CQ_TEST_MAIN(
    CQ_CASE(classical_mode_returns_what_plain_c_computes),
    CQ_CASE(classical_mode_costs_zero_gates_and_zero_qubits),
    CQ_CASE(quantum_mode_builds_the_circuit_classical_mode_folds_away),
    CQ_CASE(the_oracle_workspace_is_reclaimed_on_a_rotation_tainted_rail),
    CQ_CASE(quantum_mode_counts_are_pinned_and_the_counter_sink_agrees),
    CQ_CASE(the_per_iteration_cost_is_constant_and_the_rotations_are_closed_form),
    CQ_CASE(the_rotation_alphabet_is_exactly_ry_half_pi_and_rz_pi)
)
