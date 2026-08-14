/* test_emit_fold_oracle.inc — PRD §3 restated, independently of the emitter.
 *
 * Included by test_emit_fold.c. Split out at Step 6 when the suite crossed the
 * 300-line guard; plan §2.3 names the .inc as the escape hatch, and "the
 * oracle" versus "the sweeps that drive it" is the honest seam.
 *
 * This is a SECOND STATEMENT of the fold table, never a call into it. It
 * follows the PRD's rows in the PRD's order, and for CCX it uses the PRD's own
 * reductions ("either control ZERO -> nothing", "c1 = ONE -> emit_cx(c2,t)")
 * rather than re-deriving anything. Because it is a second reading of the same
 * prose, a misreading could in principle appear in both this and src/emit.c —
 * which is why the 25 CX rows are ALSO pinned literally in
 * test_emit_fold_cx.inc, and why check_universal asserts five things that hold
 * regardless of what the fold table says.
 */

/* ------------------------------------------------------------------------
 * The oracle — PRD §3, restated.
 * ------------------------------------------------------------------------ */

typedef struct {
    cq_rec   gate[2];      /* PRD §3's rightmost column tops out at 2 */
    size_t   n_gates;
    uint32_t n_alloc;      /* qubits allocated */
    int      is_q;         /* resulting target kind */
    int      value;        /* resulting target state ... */
    int      unknown;      /* ... meaningful only when !unknown */
} expect;

/* Target state as a (value, unknown) pair before the op. */
static void x_oracle(expect *e, okind t, uint32_t tq)
{
    /* Row 1: t constant -> flip the constant in place, 0 gates.
     * Row 2: t qubit    -> sink.x, 1 gate; shadow flips if known. */
    if (!kind_is_q(t)) {
        e->is_q = 0;
        e->value = !kind_value(t);
        e->unknown = 0;
        return;
    }
    e->is_q = 1;
    e->unknown = kind_unknown(t);
    e->value = e->unknown ? kind_value(t) : !kind_value(t);   /* nop on poison */

    cq_rec r = { CQ_OP_X, 0u, 0u, tq, 0.0 };
    e->gate[e->n_gates++] = r;
}

/* Materialisation (PRD §3): take a qubit from the pool — guaranteed |0> by
 * I3 — emit X if the constant was 1, set kind = CQ_BIT_Q. */
static uint32_t materialise_oracle(expect *e, okind t, uint32_t next_q)
{
    e->n_alloc++;
    if (kind_value(t)) {
        cq_rec r = { CQ_OP_X, 0u, 0u, next_q, 0.0 };
        e->gate[e->n_gates++] = r;
    }
    return next_q;
}

static void cx_oracle(expect *e, okind c, okind t,
                      uint32_t cq_, uint32_t tq, uint32_t next_q)
{
    if (c == K_ZERO) {                       /* row: c = ZERO -> nothing */
        e->is_q = kind_is_q(t);
        e->value = kind_value(t);
        e->unknown = kind_unknown(t);
        return;
    }
    if (c == K_ONE) { x_oracle(e, t, tq); return; }   /* row: c = ONE -> X(t) */

    /* c = Q from here. */
    int t_val = kind_value(t), t_unk = kind_unknown(t);
    uint32_t target = tq;
    if (!kind_is_q(t)) {                     /* row: c = Q, t constant */
        target = materialise_oracle(e, t, next_q);
        t_unk = 0;                           /* fresh qubit: known */
    }

    cq_rec r = { CQ_OP_CX, cq_, 0u, target, 0.0 };
    e->gate[e->n_gates++] = r;

    /* §3 shadow rule: t.unknown |= c.unknown; if (!t.unknown) t.value ^= c.value */
    e->is_q = 1;
    e->unknown = t_unk || kind_unknown(c);
    e->value = e->unknown ? t_val : (t_val ^ kind_value(c));
}

static void ccx_oracle(expect *e, okind c1, okind c2, okind t,
                       uint32_t q1, uint32_t q2, uint32_t tq, uint32_t next_q)
{
    /* row: either control ZERO -> nothing */
    if (c1 == K_ZERO || c2 == K_ZERO) {
        e->is_q = kind_is_q(t);
        e->value = kind_value(t);
        e->unknown = kind_unknown(t);
        return;
    }
    /* CCX is symmetric in its controls, so the two ONE rows are one rule:
     * swap so a ONE is always c1, then reduce to CX on the other control. */
    if (c2 == K_ONE) { okind ks = c1; c1 = c2; c2 = ks;
                       uint32_t qs = q1; q1 = q2; q2 = qs; }
    if (c1 == K_ONE) { cx_oracle(e, c2, t, q2, tq, next_q); return; }

    /* both controls Q */
    int t_val = kind_value(t), t_unk = kind_unknown(t);
    uint32_t target = tq;
    if (!kind_is_q(t)) {
        target = materialise_oracle(e, t, next_q);
        t_unk = 0;
    }

    cq_rec r = { CQ_OP_CCX, q1, q2, target, 0.0 };
    e->gate[e->n_gates++] = r;

    e->is_q = 1;
    e->unknown = t_unk || kind_unknown(c1) || kind_unknown(c2);
    e->value = e->unknown ? t_val : (t_val ^ (kind_value(c1) & kind_value(c2)));
}
