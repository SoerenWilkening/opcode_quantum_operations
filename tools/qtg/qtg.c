/* tools/qtg/qtg.c — the Quantum Tree Generator of arXiv:2310.06623, Box 1,
 * for the paper's own KP_4(p, w; 7) example. STATE PREPARATION ONLY: this
 * emits G = prod_m U_m applied to |0>_1 |c>_2 |0>_3 and stops. No amplitude
 * amplification, no QMaxSearch, no oracle.
 *
 * THIS IS BEYOND THE CQ_lang ABI ON PURPOSE. It drives libcqops' internal
 * surface directly — the kernels, the M06 control region and M22's rotations —
 * because the QTG needs a per-qubit controlled Ry and a single-flag-controlled
 * add, neither of which the frozen cqrt_* grid exposes as one call. Nothing
 * here is linked into libcqops.a; tools/ is not part of `make test`.
 *
 * TWO DELIBERATE DEPARTURES FROM THE PAPER, both forced rather than chosen:
 *
 * 1. NO QFT. Section A5c builds SUB/ADD as Draper QFT adders, which need H and
 *    controlled-phase. Rule 4 forbids H on the classical path and PRD §8's
 *    vtable is frozen at six entries with no `h`, so the arithmetic here is in
 *    the COMPUTATIONAL basis: K8's Cuccaro accumulator (M15), with both QFTs
 *    and their inverses gone. The algorithm is unchanged — Eq. (A3) says only
 *    "integer subtraction by wm", not how.
 *
 * 2. MASK-THEN-ADD, NOT A PROMOTED REGION, for U2 and U3. Wrapping the adder
 *    in cq_ctrl_push/pop is correct and costs 19W-12 Toffolis where the adder
 *    itself is 4W-4; masking the ADDEND instead leaves the adder uncontrolled
 *    and pays only the mask. Because wm and pm are CONSTANTS the mask collapses
 *    further, from W Toffolis to popcount CNOTs — see mask() below. This is
 *    Bennett's own shape at multiplier.jl:22-29, already ported in
 *    src/kernels/mul.c, so it is Rule 1 rather than a new construction.
 *
 * U1's control CANNOT be masked away and is a real region: the biased Hadamard
 * is a ROTATION, and no operand of a rotation can absorb a control the way 0
 * absorbs into a sum. It goes through cq_ctrl_push, and because Eq. (3)'s
 * angles are off the pi-lattice it takes §7's GENERAL row — so PRD §15 D11's
 * refusal of the folding rows is unreachable in this circuit. A biased
 * Hadamard at b such that theta landed on the lattice WOULD hit it.
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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Eq. (3): the two biased Hadamards, as Ry angles --------------------- */

/* H^0_b = Ry(2 acos sqrt((1+b)/(2+b))) and H^1_b = Ry(2 acos sqrt(1/(2+b))).
 * With Ry(theta)|0> = cos(theta/2)|0> + sin(theta/2)|1>, the x'=1 row gives
 * sqrt(1/(b+2))|0> + sqrt((b+1)/(b+2))|1> — inclusion favoured, which is
 * Eq. (A3) read off directly. The two rows sum to pi because their radicands
 * are complementary; that identity is the cheapest check on this function. */
static double biased_hadamard_angle(unsigned xprime_m, double b)
{
    const double num = xprime_m ? 1.0 : (1.0 + b);
    return 2.0 * acos(sqrt(num / (2.0 + b)));
}

/* --- registers ----------------------------------------------------------- */

/* Every register is a plain cq_bit array. K8 REFUSES A CLASSICAL OPERAND
 * (addacc.h) rather than folding it, so the capacity, profit and scratch rails
 * are materialised up front — which is honest here anyway, since all three
 * become entangled with the path on the first layer. `path` is NOT
 * pre-materialised: §7's general Ry row allocates it (Rule 5), and `wreg` is
 * NOT either — leaving the comparison's second operand classical is what lets
 * the §3 fold table strip its lanes, and is the whole reason K9 costs less here
 * than a general two-register compare. */
typedef struct {
    cq_bit path[QTG_N];
    cq_bit cap[QTG_WC], profit[QTG_WP];
    cq_bit wreg[QTG_WC];                     /* the constant w_m, per layer  */
    cq_bit tcap[QTG_WC], tprof[QTG_WP];      /* mask scratch                 */
    cq_bit flag, xanc;                       /* compare result, K8's ancilla */
} qtg_regs;

static void load_const(cq_bit *r, int W, unsigned v)
{
    for (int i = 0; i < W; i++) r[i] = cq_bit_const((v >> i) & 1u);
}

static void materialise(cq_ctx *ctx, cq_bit *r, int W)
{
    for (int i = 0; i < W; i++) cq_materialise(ctx, &r[i]);
}

static void regs_init(cq_ctx *ctx, qtg_regs *g)
{
    memset(g, 0, sizeof *g);                 /* CQ_BIT_ZERO == 0 (bit.h)     */
    load_const(g->cap, QTG_WC, qtg_c);       /* |c>_2                        */

    materialise(ctx, g->cap,   QTG_WC);      /* emits X per set bit of c     */
    materialise(ctx, g->profit, QTG_WP);     /* |0>_3 — no gate              */
    materialise(ctx, g->tcap,  QTG_WC);
    materialise(ctx, g->tprof, QTG_WP);
    cq_materialise(ctx, &g->xanc);
}

/* --- the masked constant ------------------------------------------------- */

/* t <- ctrl ? K : 0, for a CONSTANT K, and its own inverse.
 *
 * THE GENERAL MASK IS `t[k] ^= ctrl & b[k]`, W Toffolis. Here b[k] is a
 * classical bit of K, so the AND is decided at build time: a set lane is one
 * CX from ctrl and a clear lane is nothing at all. popcount(K) CNOTs, no
 * Toffoli, no fold-table dependence. Calling it twice restores t to |0> because
 * CX is self-inverse and the accumulate between them leaves both ctrl and t
 * unchanged (K8 restores its addend — addacc.h). */
static void mask(cq_ctx *ctx, const cq_bit *ctrl, cq_bit *t, int W, unsigned K)
{
    for (int i = 0; i < W; i++)
        if ((K >> i) & 1u) cq_emit_cx(ctx, ctrl, &t[i]);
}

/* acc += (ctrl ? K : 0), in place, with the adder itself UNCONTROLLED. */
static void ctrl_addc(cq_ctx *ctx, const cq_bit *ctrl, cq_bit *acc,
                      cq_bit *t, cq_bit *x, int W, unsigned K)
{
    cq_addacc_block k;

    k.acc = acc; k.b = t; k.x = x; k.W = W;

    mask(ctx, ctrl, t, W, K);
    cq_kernel_addacc(ctx, &k);
    mask(ctx, ctrl, t, W, K);
}

/* --- Eq. (4): one layer unitary ------------------------------------------ */

static void layer(cq_ctx *ctx, qtg_regs *g, int m)
{
    const unsigned wm = qtg_w[m], pm = qtg_p[m];

    /* U1 = C^2_{>=wm}(H^b_{x'_m}). The comparison lands in `flag`, which is
     * Ha's one live ancilla for this layer; K9's dst is ONE bit while W is the
     * OPERAND width, which is why `flag` is a scalar and not a rail. */
    load_const(g->wreg, QTG_WC, wm);
    cq_kernel_uge(ctx, &g->flag, g->cap, g->wreg, QTG_WC);

    cq_ctrl_push(ctx, &g->flag);
    cq_rotate_ry_bit(ctx, &g->path[m],
                     biased_hadamard_angle(qtg_xprime[m], QTG_BIAS));
    cq_ctrl_pop(ctx);

    /* UNCOMPUTE THE FLAG IMMEDIATELY, BEFORE U2 TOUCHES THE CAPACITY. K9 is a
     * Rule 7 kernel — dst ^= f(a,b) — so the SAME call run again is the
     * uncompute (f ^ f = 0), and it is exact only while `cap` still holds what
     * it held at the first call. Deferring this past SUB_wm would uncompute
     * against a capacity that had already changed and leave `flag` dirty: the
     * ancilla reuse Section A5e's qubit count assumes would then be wrong. */
    cq_kernel_uge(ctx, &g->flag, g->cap, g->wreg, QTG_WC);

    /* U2 = C^1_m(SUB_wm), as an ADD of the two's complement. Legal because the
     * capacity register is modular and the comparison has already established
     * cap >= wm on this branch, so the borrow can never escape. */
    ctrl_addc(ctx, &g->path[m], g->cap, g->tcap, &g->xanc, QTG_WC,
              ((1u << QTG_WC) - wm) & ((1u << QTG_WC) - 1u));

    /* U3 = C^1_m(ADD_pm). */
    ctrl_addc(ctx, &g->path[m], g->profit, g->tprof, &g->xanc, QTG_WP, pm);
}

/* --- the classical cross-check ------------------------------------------- */

/* NOT A SIMULATION (Rule 13 forbids one anywhere). It walks ONE path — the
 * greedy x' — through the same constants the circuit is built from, in plain
 * C, and checks that the two's complement wrap reproduces the subtraction and
 * that neither register leaves its width. It cannot see a wrong gate; it exists
 * so a typo in w, p or the modular constant is caught before the gate stream
 * is read. Amplitudes are out of reach here by construction. */
static int classical_check(void)
{
    unsigned cap = qtg_c, profit = 0u, ok = 1u;

    for (int m = 0; m < QTG_N; m++) {
        if (!qtg_xprime[m]) continue;
        if (cap < qtg_w[m]) { ok = 0; break; }
        cap = (cap + (((1u << QTG_WC) - qtg_w[m]) & ((1u << QTG_WC) - 1u)))
            & ((1u << QTG_WC) - 1u);
        profit += qtg_p[m];
    }
    if (cap != QTG_GREEDY_CAP || profit != QTG_GREEDY_PROFIT) ok = 0;
    return (int)ok;
}

/* --- the Ry accuracy, pushed at the QEC sink ----------------------------- */

/* eps -> the integer Ross-Selinger precision, and CEIL rather than round: the
 * contract is "at least this accurate", so 2^-precision <= QTG_RY_EPSILON must
 * hold and rounding down would quietly deliver a looser rotation than asked for.
 *
 * SET IN THE SCRIPT, NOT IN THE CONFIG (see instance.h). This runs whether or
 * not the qec sink is selected — it is one setenv and it means that selecting
 * the sink later, with CQOPS_SINK=qec, cannot silently pick up sink_qec.c's
 * default of 20 and synthesise the biased Hadamards six times more accurately
 * than this instance needs. An explicit CQOPS_QEC_PRECISION in the environment
 * WINS: a caller sweeping precision must not be overridden by the program. */
static int qtg_rotation_precision(void)
{
    const double p = ceil(-log2(QTG_RY_EPSILON));

    if (!(p >= 0.0 && p <= 30.0)) {
        fprintf(stderr, "qtg: QTG_RY_EPSILON gives precision %g, outside the "
                        "library's [0, 30]\n", p);
        exit(1);
    }
    return (int)p;
}

static void qtg_publish_precision(int prec)
{
    char buf[16];

    if (getenv("CQOPS_QEC_PRECISION")) return;      /* the caller's wins */
    snprintf(buf, sizeof buf, "%d", prec);
    setenv("CQOPS_QEC_PRECISION", buf, 1);
}

/* --- the rotation probe -------------------------------------------------- */

/* A GATE COUNT CANNOT SEE A WRONG ANGLE, and neither can the pool, the value or
 * a golden — tests/test_grover.c records the same finding for §12, where
 * flipping the sign of the emitted angle left every other instrument green.
 * This sink forwards to whichever real sink is active and additionally logs the
 * `ry` stream, so the two claims below can be made about the angles themselves.
 * It is a test fixture living in a tool, not a simulator: it records a stream
 * and holds no state of the circuit (Rule 13). */
typedef struct { cq_sink in; double ry[2 * QTG_N + 1]; unsigned n; } qtg_probe;

#define FWD0(N, ...) static void pr_##N(void *u, __VA_ARGS__)
FWD0(x, uint32_t q)               { qtg_probe *p = u; p->in.x(p->in.user, q); }
FWD0(mz, uint32_t q)              { qtg_probe *p = u; p->in.mz(p->in.user, q); }
FWD0(cx, uint32_t c, uint32_t t)  { qtg_probe *p = u; p->in.cx(p->in.user, c, t); }
FWD0(ccx, uint32_t a, uint32_t b, uint32_t t)
                                  { qtg_probe *p = u; p->in.ccx(p->in.user, a, b, t); }
FWD0(rz, uint32_t q, double a)    { qtg_probe *p = u; p->in.rz(p->in.user, q, a); }
FWD0(ry, uint32_t q, double a)
{
    qtg_probe *p = u;
    if (p->n < sizeof p->ry / sizeof *p->ry) p->ry[p->n] = a;
    p->n++;
    p->in.ry(p->in.user, q, a);
}

static cq_sink probe_sink(qtg_probe *p, cq_sink inner)
{
    cq_sink s;

    p->in = inner; p->n = 0u;
    s.x = pr_x; s.cx = pr_cx; s.ccx = pr_ccx;
    s.ry = pr_ry; s.rz = pr_rz; s.mz = pr_mz; s.user = p;
    return s;
}

/* TWO CLAIMS, AND THE FIRST IS WHY THE SECOND IS NOT ENOUGH.
 *
 * (1) EXACTLY TWO Ry PER LAYER. §9's promotion of a rotation is
 * `R(t/2); CX; R(-t/2); CX`, so a live control region emits TWO; an
 * uncontrolled rotation emits ONE. The count therefore witnesses that
 * cq_ctrl_push actually promoted — which is the failure this circuit is most
 * exposed to, since a controlled region silently made unconditional keeps the
 * right value on the ctrl = 1 branch and is invisible to every other
 * instrument here (CLAUDE.md's L5-under-a-control callout).
 *
 * (2) THE ANGLES ARE Eq. (3)'s, BITWISE. `==` is wrong: 0.0 and -0.0 compare
 * equal in C and are different gates to emit, and the second half of each pair
 * is a NEGATION. Comparing the whole ordered sequence rather than a set is what
 * makes a swapped pair (a layer given the other row of Eq. (3)) visible — which
 * matters because x' selects the row PER ITEM and three of these four layers
 * share a value. */
static int check_rotations(const qtg_probe *p)
{
    if (p->n != 2u * QTG_N) return 0;

    for (int m = 0; m < QTG_N; m++) {
        const double half = biased_hadamard_angle(qtg_xprime[m], QTG_BIAS) * 0.5;
        const double neg  = -half;

        if (memcmp(&p->ry[2 * m],     &half, sizeof half) != 0) return 0;
        if (memcmp(&p->ry[2 * m + 1], &neg,  sizeof neg)  != 0) return 0;
    }
    return 1;
}

/* --- driver -------------------------------------------------------------- */

int main(int argc, char **argv)
{
    const int trace = (argc > 1 && strcmp(argv[1], "--trace") == 0);
    cq_counter c;
    qtg_probe  probe;
    cq_sink    sink;
    cq_ctx     ctx;
    qtg_regs   g;

    if (!classical_check()) {
        fprintf(stderr, "qtg: the instance constants do not close — "
                        "greedy x' must leave capacity 2 and profit 9\n");
        return 1;
    }

    qtg_publish_precision(qtg_rotation_precision());

    cq_count_reset(&c);
    sink = probe_sink(&probe, trace ? cq_sink_printf(stdout)
                                    : cq_sink_counter(&c));
    cq_ctx_init(&ctx, &sink);

    regs_init(&ctx, &g);
    for (int m = 0; m < QTG_N; m++) layer(&ctx, &g, m);

    if (!check_rotations(&probe)) {
        fprintf(stderr, "qtg: FAIL — the Ry stream is not %d controlled biased "
                        "Hadamards at Eq. (3)'s angles (saw %u Ry)\n",
                        QTG_N, probe.n);
        cq_ctx_dispose(&ctx);
        return 1;
    }

    if (!trace) {
        printf("QTG state preparation — KP_%d(p, w; %u), b = %.2f, x' = 1110\n",
               QTG_N, qtg_c, QTG_BIAS);
        printf("  Eq. (2) registers  n=%d |c_bin|=%d |P_bin|=%d Ha=%d -> %d\n",
               QTG_N, QTG_WC, QTG_WP, QTG_N,
               QTG_N + QTG_WC + QTG_WP + QTG_N);
        printf("  NOT %llu  CNOT %llu  Toffoli %llu  Ry %llu  Rz %llu\n",
               (unsigned long long)c.x,  (unsigned long long)c.cx,
               (unsigned long long)c.ccx, (unsigned long long)c.ry,
               (unsigned long long)c.rz);
        printf("  T = %llu  (7/Toffoli — a LOWER BOUND: the %llu Ry are "
               "unsynthesised here; their cost is the qec sink's, at eps = %g)\n",
               (unsigned long long)cq_count_t(&c), (unsigned long long)c.ry,
               QTG_RY_EPSILON);
        printf("  peak qubits %u   live at exit %u\n",
               cq_qubits_peak(&ctx.pool), cq_qubits_live(&ctx.pool));
        printf("  rotations OK: %d controlled biased Hadamards, "
               "angles bitwise Eq. (3)\n", QTG_N);
        printf("  Ry accuracy eps = %g -> CQOPS_QEC_PRECISION=%s "
               "(config epsilon is the FAILURE BUDGET, a different quantity)\n",
               QTG_RY_EPSILON, getenv("CQOPS_QEC_PRECISION"));
    }

    cq_ctx_dispose(&ctx);
    return 0;
}
