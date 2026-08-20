/* test_controlled_oracle.inc — PRD §9 restated, independently of M06.
 *
 * Included by test_controlled.c. This is a SECOND STATEMENT of the promotion
 * table, never a call into it: it follows §9's three rows in §9's order
 * (`NOT→CNOT`, `CNOT→Toffoli`, `Toffoli→` three Toffolis and one reusable
 * ancilla) composed with the §3 fold on the CONTROLS, and it models the pool's
 * allocation order by hand so the qubit indices in the expected stream are
 * predicted rather than read back.
 *
 * Because it is a second reading of the same prose, a misreading could in
 * principle appear both here and in src/controlled.c. Three things close that:
 * test_controlled_table.inc pins the promoted cells LITERALLY and never goes
 * through this file; the row-0 cases are DIFFERENTIAL against the same gate run
 * with no region, so they need no oracle at all; and check_universal asserts
 * six things that hold regardless of what §9 says.
 *
 * WHAT IT DELIBERATELY DOES NOT MODEL: the two rotation rows. `cq_ctrl_ry/rz`
 * are pinned literally in test_controlled.c against §9's printed identity,
 * because a four-gate block with two half angles is exactly the shape where a
 * second transcription is a second chance to make the same sign error.
 */

/* A promoted CCX with a materialising X on the target is the longest cell:
 * one X, then three Toffolis. */
typedef struct {
    cq_rec   gate[6];
    size_t   n_gates;
    uint32_t n_alloc;     /* qubits taken: the shared ancilla, the target, or both */
    int      is_q;        /* resulting target kind */
    int      value;       /* resulting target state ... */
    int      unknown;     /* ... meaningful only when !unknown */
} pexpect;

static void push_rec(pexpect *e, cq_rec r)
{
    if (e->n_gates >= sizeof e->gate / sizeof e->gate[0])
        cq_h_fail(__FILE__, __LINE__, "oracle overflowed its gate array");
    else
        e->gate[e->n_gates++] = r;
}

/* The §3 shadow rule for a Toffoli, applied to the oracle's own model of the
 * target. Restated here rather than called, for the same reason as everything
 * else in this file. */
static void sh_ccx(pexpect *e, int a_val, int a_unk, int b_val, int b_unk)
{
    e->unknown |= (a_unk | b_unk);
    if (!e->unknown) e->value ^= (a_val & b_val);
}

/* Materialisation, and it is UNPROMOTED — `bd skh`, resolved at Step 20. The
 * fresh qubit takes the constant's value unconditionally: an encoding is not
 * conditional on anything, and the X (if any) never becomes a CX. */
static uint32_t materialise_p(pexpect *e, okind t, uint32_t next_q)
{
    e->n_alloc++;
    e->is_q = 1;
    e->value = kind_value(t);
    e->unknown = 0;
    if (kind_value(t)) {
        cq_rec r = { CQ_OP_X, 0u, 0u, next_q, 0.0 };
        push_rec(e, r);
    }
    return next_q;
}

/* The target as it stands before the gate, whether or not it is on a wire. */
static void target_before(pexpect *e, okind t, uint32_t tq)
{
    (void)tq;
    e->is_q    = kind_is_q(t);
    e->value   = kind_value(t);
    e->unknown = kind_unknown(t);
}

/* §9 row 3's physical CX, once the target is known to be a wire. */
static void promoted_cx(pexpect *e, uint32_t wq, int w_val, int w_unk,
                        uint32_t tq)
{
    cq_rec r = { CQ_OP_CX, wq, 0u, tq, 0.0 };
    push_rec(e, r);
    e->unknown |= w_unk;
    if (!e->unknown) e->value ^= w_val;
}

/* The oracle proper. `k` names the three gates; every surviving control is
 * already known to be quantum by the time §9's promotion applies, because the
 * §3 fold on the controls runs FIRST (src/controlled.h states why that order is
 * not symmetric with the target's fold).
 *
 * `wq`/`w_val`/`w_unk` describe the control wire; `next_q` is the pool's next
 * fresh index at the moment of the call. */
static void promote_oracle(pexpect *e,
                           int arity, okind c1, okind c2, okind t,
                           uint32_t wq, int w_val, int w_unk,
                           uint32_t c1q, uint32_t c2q, uint32_t tq,
                           uint32_t next_q)
{
    target_before(e, t, tq);

    /* --- CCX: PRD §3's control folds, in the PRD's order. ---------------- */
    if (arity == 3) {
        if (c1 == K_ZERO || c2 == K_ZERO) return;    /* controlled-nothing   */

        /* §3's control SWAP, so the c1 = ONE and c2 = ONE rows are one rule. */
        if (c2 == K_ONE) { okind s = c1; c1 = c2; c2 = s;
                           uint32_t sq = c1q; c1q = c2q; c2q = sq; }
        if (c1 == K_ONE) { arity = 2; c1 = c2; c1q = c2q; }
    }

    /* --- CX: the same two rows. ------------------------------------------ */
    if (arity == 2) {
        if (c1 == K_ZERO) return;                    /* controlled-nothing   */
        if (c1 == K_ONE) arity = 1;                  /* CX(ONE,t) IS X(t)    */
    }

    /* --- Row 1: NOT → CNOT. ---------------------------------------------- */
    if (arity == 1) {
        if (!kind_is_q(t)) tq = materialise_p(e, t, next_q);
        promoted_cx(e, wq, w_val, w_unk, tq);
        return;
    }

    /* --- Row 2: CNOT → Toffoli. ------------------------------------------ */
    if (arity == 2) {
        if (!kind_is_q(t)) tq = materialise_p(e, t, next_q);
        cq_rec r = { CQ_OP_CCX, wq, c1q, tq, 0.0 };
        push_rec(e, r);
        sh_ccx(e, w_val, w_unk, kind_value(c1), kind_unknown(c1));
        return;
    }

    /* --- Row 3: Toffoli → three Toffolis and one reusable ancilla. -------
     *
     * THE ANCILLA IS TAKEN FIRST, before the target is materialised, and the
     * order is observable: it decides which index is which in the stream. That
     * is a real claim about M06 and not a convention — a version that
     * materialised first would emit the same GATES with the two indices
     * swapped, and only an ordered comparison sees it. */
    const uint32_t anc = next_q;
    e->n_alloc++;

    cq_rec g1 = { CQ_OP_CCX, wq, c1q, anc, 0.0 };
    push_rec(e, g1);

    /* THE MATERIALISATION LANDS BETWEEN GATE 1 AND GATE 2, not before gate 1,
     * because the target is materialised by the gate that first TARGETS it —
     * which is gate 2. Getting this backwards was the first draft of this file
     * and it emitted the same four gates in a different order; only an ordered
     * comparison saw it. */
    if (!kind_is_q(t)) tq = materialise_p(e, t, next_q + 1u);

    cq_rec g2 = { CQ_OP_CCX, anc, c2q, tq, 0.0 };
    push_rec(e, g2);
    push_rec(e, g1);

    /* anc = w ∧ c1 while gate 2 runs, and back to |0⟩ after gate 3. */
    sh_ccx(e, w_val & kind_value(c1), w_unk | kind_unknown(c1),
           kind_value(c2), kind_unknown(c2));
}
