/* tests/support/kernelctrl.c — the controlled axis, on the test side.
 *
 * See kernelctrl.h for the seam. Everything here is Step 20: the region mode,
 * the control rail it mints, the four-region loop each suite drives its own
 * sweep through, and §9's gate-tuple transform measured against the same run's
 * uncontrolled tuple.
 */

#include "support/kernelctrl.h"

#include "reg.h"
#include "support/bitkinds.h"
#include "support/harness.h"
#include "sink_count.h"

#include <stdio.h>

static cq_kd_ctrl g_ctrl   = CQ_KD_CTRL_NONE;
static int32_t    g_ctrl_h = CQ_REG_NONE;   /* the live control rail, or none */

void cq_kd_set_ctrl(cq_kd_ctrl mode) { g_ctrl = mode; }
cq_kd_ctrl cq_kd_get_ctrl(void)      { return g_ctrl; }

const char *cq_kd_ctrl_name(cq_kd_ctrl mode)
{
    switch (mode) {
    case CQ_KD_CTRL_NONE: return "uncontrolled";
    case CQ_KD_CTRL_ZERO: return "ctrl=ZERO";
    case CQ_KD_CTRL_ONE:  return "ctrl=ONE";
    case CQ_KD_CTRL_Q0:   return "ctrl=Q(0)";
    case CQ_KD_CTRL_Q1:   return "ctrl=Q(1)";
    }
    return "(invalid)";
}

/* A ONE-BIT rail, which is what CQ_lang's control flags measurably are: over
 * all 239 goldens every control operand is an i1, 4,080 of them born
 * `cqrt_alloc_i1(0)` and conjoined with cnot/toffoli, the other 853 an
 * icmp/fcmp `flag_handle`. It is a REGISTER rather than a bare cq_bit so that
 * L2 can name it — an unowned live index is exactly what
 * cq_pc_live_is_exactly calls a leaked ancilla. */
int32_t cq_kd_ctrl_handle(void) { return g_ctrl_h; }
void    cq_kd_ctrl_clear(void)  { g_ctrl_h = CQ_REG_NONE; }

/* THE FIXTURE MUST BE WHAT IT CLAIMS TO BE, AND NOTHING ELSE HERE CAN CHECK IT.
 *
 * Every assertion the four-region loop makes is CONDITIONAL on the region it
 * says it opened, so a mis-minted rail does not fail — it silently moves the
 * whole sweep onto a different row of PRD §9 row 0 and stays green. MEASURED:
 * change CQ_KD_CTRL_Q0's rail from `cq_bk_reg(ctx, 1u, 0u, 1u)` to
 * `(…, 0u, 0u)` and the sharpest fixture in the driver — a QUANTUM control
 * whose shadow reads 0, the only one that can falsify bd skh and the only one
 * that can see a promoted region that ran when it should not have — degrades
 * into an ordinary CQ_BIT_ZERO SKIP. L1's `ctrl_off` row then expects exactly
 * zero and gets zero, L2 and L3 see a rail that owns no qubit, L5's classical
 * branch is the one that runs, and the entire 195-test suite passes in both
 * configurations while a whole column of the axis has stopped being tested.
 *
 * So the mode's NAME is asserted against the rail's KIND and its VALUE. The
 * kind is what row 0 dispatches on (D6: never the shadow), and the value is
 * what separates Q0 from Q1 — a distinction no gate count can see, since both
 * promote identically and differ only in which branch L1's oracle expects. The
 * `unknown` clause is not decoration either: a poisoned control makes
 * cq_pc_value's answer -1 and would take L1's oracle out with it. */
static void check_rail(const cq_ctx *ctx)
{
    const int want_q = (g_ctrl == CQ_KD_CTRL_Q0 || g_ctrl == CQ_KD_CTRL_Q1);
    const int want_v = (g_ctrl == CQ_KD_CTRL_ONE || g_ctrl == CQ_KD_CTRL_Q1);
    const cq_bit *b;
    int value;

    if (g_ctrl == CQ_KD_CTRL_NONE) {
        if (g_ctrl_h != CQ_REG_NONE)
            cq_h_fail(__FILE__, __LINE__,
                      "%s minted a control rail; every region assertion below "
                      "would be measured against one that is open",
                      cq_kd_ctrl_name(g_ctrl));
        return;
    }

    b = cq_reg_cbits(&ctx->regs, g_ctrl_h);

    if (!!cq_bit_is_qubit(b[0]) != want_q) {
        cq_h_fail(__FILE__, __LINE__,
                  "%s: the control rail is %s — PRD §9 row 0 takes the wrong "
                  "branch and every assertion under this region is vacuous",
                  cq_kd_ctrl_name(g_ctrl),
                  want_q ? "a CONSTANT, want a wire"
                         : "a WIRE, want a constant");
        return;                       /* the value check below would misread */
    }

    if (want_q) {
        const cq_shadow sh = cq_shadow_get(&ctx->shadow, cq_bit_qindex(b[0]));

        if (sh.unknown)
            cq_h_fail(__FILE__, __LINE__,
                      "%s: the control wire is poisoned, so L1's oracle has no "
                      "answer to compare against", cq_kd_ctrl_name(g_ctrl));
        value = (int)sh.value;
    } else {
        value = cq_bit_value(b[0]);
    }

    if (value != want_v)
        cq_h_fail(__FILE__, __LINE__,
                  "%s: the control rail holds %d, want %d — the mode's name is "
                  "what L1 picks its expected `dst` from", cq_kd_ctrl_name(g_ctrl),
                  value, want_v);
}

int32_t cq_kd_ctrl_rail(cq_ctx *ctx)
{
    switch (g_ctrl) {
    case CQ_KD_CTRL_NONE: g_ctrl_h = CQ_REG_NONE;                     break;
    case CQ_KD_CTRL_ZERO: g_ctrl_h = cq_bk_reg(ctx, 1u, 0u, 0u);      break;
    case CQ_KD_CTRL_ONE:  g_ctrl_h = cq_bk_reg(ctx, 1u, 1u, 0u);      break;
    case CQ_KD_CTRL_Q0:   g_ctrl_h = cq_bk_reg(ctx, 1u, 0u, 1u);      break;
    case CQ_KD_CTRL_Q1:   g_ctrl_h = cq_bk_reg(ctx, 1u, 1u, 1u);      break;
    }

    check_rail(ctx);
    return g_ctrl_h;
}


/* §9's transform, measured against the same run's uncontrolled tuple. */
uint64_t cq_kd_check_promotion(const cq_kd_spec *k, int W)
{
    cq_counter u_f, u_u, c_f, c_u;
    const cq_kd_ctrl saved = g_ctrl;

    g_ctrl = CQ_KD_CTRL_NONE;
    cq_kd_measure(k, W, &u_f, &u_u);
    g_ctrl = CQ_KD_CTRL_Q1;
    cq_kd_measure(k, W, &c_f, &c_u);
    g_ctrl = saved;

    /* An empty uncontrolled tuple is NOT a defect — see the header — but the
     * identity is then vacuous, so the total goes back to the caller, who owns
     * the claim that its ladder reached something. The (0,0,0) -> (0,0,0) check
     * below still runs: "promoted nothing" is itself worth pinning. */

    /* THE LEADING ZERO IS AN ASSERTION, not a formality: at the all-quantum
     * mask every `dst` lane is materialised from ZERO by the gate that first
     * targets it, so no materialising X survives and every X the kernel emits
     * lands on a scratch wire — which promotion turns into a CX. A non-zero X
     * component here means a CQ_BIT_ONE was materialised inside the region,
     * which would be a finding about bd skh rather than a golden to update.
     *
     * AND A DELIBERATELY-EQUIVALENT SURVIVOR LIVES HERE, on M09's `0xAA`
     * precedent: replacing THIS clause with `if (0)` survives the whole suite
     * in both configurations, because the uncompute clause below is the same
     * assertion over the same emitter. Both passes are the same kernel called
     * again through the same promotion, so a defect in §9's transform moves
     * both tuples and the second detector catches everything the first would.
     *
     * ITS VALUE IS PROVED BY THE PAIRED MUTATION RATHER THAN ARGUED, which is
     * the whole point of the precedent: changing the `3u` here to `2u` IS
     * killed, so the clause is reachable and does fire — it is redundant as a
     * DETECTOR, not dead. It stays because forward and uncompute are pinned
     * separately everywhere else in this project (Rule 14, and L4 does the
     * same), and collapsing them here would quietly make `unc == forward` an
     * assumption of the axis. */
    if (c_f.x != 0u || c_f.cx != u_f.x || c_f.ccx != u_f.cx + 3u * u_f.ccx)
        cq_h_fail(__FILE__, __LINE__,
                  "%s W=%d forward: promoted (%llu, %llu, %llu), but §9 maps "
                  "(%llu, %llu, %llu) to (0, %llu, %llu)", k->name, W,
                  (unsigned long long)c_f.x, (unsigned long long)c_f.cx,
                  (unsigned long long)c_f.ccx,
                  (unsigned long long)u_f.x, (unsigned long long)u_f.cx,
                  (unsigned long long)u_f.ccx,
                  (unsigned long long)u_f.x,
                  (unsigned long long)(u_f.cx + 3u * u_f.ccx));

    /* The uncompute pass is measured SEPARATELY, exactly as L4 does, because
     * forward == unc is not an invariant (Rule 14) either side of the axis. */
    if (c_u.x != 0u || c_u.cx != u_u.x || c_u.ccx != u_u.cx + 3u * u_u.ccx)
        cq_h_fail(__FILE__, __LINE__,
                  "%s W=%d uncompute: promoted (%llu, %llu, %llu), want "
                  "(0, %llu, %llu)", k->name, W,
                  (unsigned long long)c_u.x, (unsigned long long)c_u.cx,
                  (unsigned long long)c_u.ccx,
                  (unsigned long long)u_u.x,
                  (unsigned long long)(u_u.cx + 3u * u_u.ccx));

    return cq_count_total(&u_f);
}

/* Four regions, the suite's own sweep, and a printed line per region — bd mmv's
 * discipline is that a run covering less than it looks like has to say so in
 * its own output, and a controlled run that was indistinguishable from an
 * uncontrolled one in the log would be exactly that. */
void cq_kd_for_each_region(const char *what, void (*body)(void))
{
    static const cq_kd_ctrl modes[] = {
        CQ_KD_CTRL_ZERO, CQ_KD_CTRL_ONE, CQ_KD_CTRL_Q0, CQ_KD_CTRL_Q1
    };
    const cq_kd_ctrl saved = g_ctrl;

    for (size_t i = 0; i < sizeof modes / sizeof modes[0]; i++) {
        g_ctrl = modes[i];
        printf("# %s under %s (PRD §9 row 0 / the promotion)\n",
               what, cq_kd_ctrl_name(modes[i]));
        body();
    }

    g_ctrl = saved;
}
