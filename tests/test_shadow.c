/* Step 3's gate: M02's per-qubit shadow obeys the PRD §3 update table.
 *
 * The shadow is the whole of our "simulation" — two bits per qubit, no
 * amplitudes, no correlations (Rule 13). Its discipline is one-way:
 *
 *   it MAY say unknown when the truth is determinate  (it forgets
 *       correlations, and CCX with a known-0 control is the standing example)
 *   it may NEVER say determinate when the truth is unknown
 *
 * Both halves are asserted here. The second is the one that would be a silent
 * miscompile: a shadow that claimed determinate-and-wrong would let a rail
 * that is not provably zero pass a Rule 6 free.
 *
 * Operand states. Three would be the obvious set {known-0, known-1, unknown},
 * but that misses a real footgun: `value` is meaningful only when `unknown`
 * is clear, so an entry poisoned *from 1* carries value=1 underneath. If any
 * predicate reads `value` without first checking `unknown`, only U1 catches
 * it. Hence four: K0, K1, U0 (poisoned from 0), U1 (poisoned from 1).
 *
 * Every state is built through the real API — there is no back door that
 * writes an entry directly, because M02 deliberately exposes none (see
 * shadow.h on bd ckd.17).
 */

#include "shadow.h"
#include "support/harness.h"

#include <stdio.h>

typedef enum { SK0 = 0, SK1, SU0, SU1, N_STATES } state;

static const char *state_name(state s)
{
    static const char *n[N_STATES] = { "K0", "K1", "U0", "U1" };
    return n[s];
}

/* Entries are born known-0 (I3: a qubit off the pool is |0⟩), so K0 is the
 * default and the other three are reached by the rules themselves. */
static void put(cq_shadow_table *sh, uint32_t q, state s)
{
    if (s == SK1 || s == SU1) cq_shadow_x(sh, q);
    if (s == SU0 || s == SU1) cq_shadow_rotate(sh, q);
}

static int state_unknown(state s) { return s == SU0 || s == SU1; }
static int state_value  (state s) { return s == SK1 || s == SU1; }

/* Builds a table of `n` qubits, all known-0. */
static void fresh(cq_shadow_table *sh, uint32_t n)
{
    cq_shadow_init(sh);
    cq_shadow_ensure(sh, n);
}

/* -------------------------------------------------------------------------
 * Birth, growth, and the one thing growth must not do.
 * ------------------------------------------------------------------------- */

CQ_TEST(a_fresh_table_is_empty)
{
    cq_shadow_table sh;
    cq_shadow_init(&sh);
    CHECK_EQ(cq_shadow_count(&sh), 0u);
    cq_shadow_dispose(&sh);
}

CQ_TEST(qubits_are_born_known_zero)
{
    /* I3 — a qubit on the free list is |0⟩, so a qubit handed out by the pool
     * has a known-0 shadow. If birth were "unknown", the classical
     * short-circuit (L5) would never fire and every program would allocate. */
    cq_shadow_table sh;
    fresh(&sh, 70u);

    CHECK_EQ(cq_shadow_count(&sh), 70u);
    for (uint32_t q = 0; q < 70u; q++) {
        cq_shadow e = cq_shadow_get(&sh, q);
        if (e.unknown || e.value || !cq_shadow_known_zero(&sh, q))
            cq_h_fail(__FILE__, __LINE__,
                      "q%u born {value=%u unknown=%u}, want known-0",
                      q, (unsigned)e.value, (unsigned)e.unknown);
    }
    cq_shadow_dispose(&sh);
}

CQ_TEST(growth_preserves_every_existing_entry)
{
    /* The miscompile this case exists to catch: a grow path that re-inits the
     * whole array instead of only the new tail would silently un-poison every
     * live qubit, and nothing else in the project would notice — the shadow
     * would simply start claiming determinate where it had lost the truth.
     * Growth crosses several doublings here on purpose. */
    cq_shadow_table sh;
    fresh(&sh, 4u);

    put(&sh, 0u, SK0);
    put(&sh, 1u, SK1);
    put(&sh, 2u, SU0);
    put(&sh, 3u, SU1);

    for (uint32_t n = 5u; n <= 300u; n++) {
        cq_shadow_ensure(&sh, n);

        for (uint32_t q = 0; q < 4u; q++) {
            state    want = (state)q;
            cq_shadow e   = cq_shadow_get(&sh, q);

            if (e.unknown != (uint8_t)state_unknown(want))
                cq_h_fail(__FILE__, __LINE__,
                          "after ensure(%u): q%u unknown=%u, want %d (%s)",
                          n, q, (unsigned)e.unknown, state_unknown(want),
                          state_name(want));
            if (!e.unknown && e.value != (uint8_t)state_value(want))
                cq_h_fail(__FILE__, __LINE__,
                          "after ensure(%u): q%u value=%u, want %d (%s)",
                          n, q, (unsigned)e.value, state_value(want),
                          state_name(want));
        }
        /* and the tail is born known-0, every time */
        CHECK(cq_shadow_known_zero(&sh, n - 1u));
    }
    cq_shadow_dispose(&sh);
}

CQ_TEST(ensure_is_monotone_and_never_shrinks)
{
    cq_shadow_table sh;
    fresh(&sh, 8u);
    put(&sh, 7u, SU1);

    cq_shadow_ensure(&sh, 3u);
    CHECK_EQ(cq_shadow_count(&sh), 8u);
    CHECK(cq_shadow_get(&sh, 7u).unknown);

    cq_shadow_ensure(&sh, 8u);
    CHECK_EQ(cq_shadow_count(&sh), 8u);
    CHECK(cq_shadow_get(&sh, 7u).unknown);
    cq_shadow_dispose(&sh);
}

/* -------------------------------------------------------------------------
 * The four §3 rules.
 * ------------------------------------------------------------------------- */

CQ_TEST(rule_x_flips_a_known_value_and_is_a_nop_on_poison)
{
    /* PRD §3: X(t) — `t.unknown ? nop : t.value ^= 1`. */
    cq_shadow_table sh;
    fresh(&sh, N_STATES);

    for (state s = SK0; s < N_STATES; s++) {
        uint32_t q = (uint32_t)s;
        put(&sh, q, s);
        cq_shadow_x(&sh, q);

        cq_shadow e = cq_shadow_get(&sh, q);
        if (e.unknown != (uint8_t)state_unknown(s))
            cq_h_fail(__FILE__, __LINE__, "X on %s changed unknown to %u",
                      state_name(s), (unsigned)e.unknown);

        /* On a poisoned entry the rule is "nop", not "flip a byte nobody
         * reads". The distinction is invisible in v1 — `value` is meaningless
         * under poison — but it stops being invisible the moment ckd.17's
         * un-poisoning write lands and starts trusting that byte. Pin it now,
         * while it costs one line. */
        int want_v = state_unknown(s) ? state_value(s) : !state_value(s);
        if (e.value != (uint8_t)want_v)
            cq_h_fail(__FILE__, __LINE__, "X on %s gave value %u, want %d",
                      state_name(s), (unsigned)e.value, want_v);
    }
    cq_shadow_dispose(&sh);
}

CQ_TEST(rule_cx_matches_the_literal_prd_table)
{
    /* All 16 (c, t) combinations, pinned literally rather than recomputed, so
     * this case is an independent statement of PRD §3 and not a restatement
     * of the implementation. -1 means "unknown, so value is meaningless". */
    static const struct { state c, t; int unknown, value; } table[] = {
        { SK0, SK0, 0,  0 }, { SK0, SK1, 0,  1 },
        { SK0, SU0, 1, -1 }, { SK0, SU1, 1, -1 },
        { SK1, SK0, 0,  1 }, { SK1, SK1, 0,  0 },
        { SK1, SU0, 1, -1 }, { SK1, SU1, 1, -1 },
        { SU0, SK0, 1, -1 }, { SU0, SK1, 1, -1 },
        { SU0, SU0, 1, -1 }, { SU0, SU1, 1, -1 },
        { SU1, SK0, 1, -1 }, { SU1, SK1, 1, -1 },
        { SU1, SU0, 1, -1 }, { SU1, SU1, 1, -1 },
    };

    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) {
        cq_shadow_table sh;
        fresh(&sh, 2u);
        put(&sh, 0u, table[i].c);
        put(&sh, 1u, table[i].t);

        cq_shadow_cx(&sh, 0u, 1u);
        cq_shadow e = cq_shadow_get(&sh, 1u);

        if (e.unknown != (uint8_t)table[i].unknown)
            cq_h_fail(__FILE__, __LINE__, "CX(%s,%s): unknown=%u, want %d",
                      state_name(table[i].c), state_name(table[i].t),
                      (unsigned)e.unknown, table[i].unknown);
        if (table[i].value >= 0 && e.value != (uint8_t)table[i].value)
            cq_h_fail(__FILE__, __LINE__, "CX(%s,%s): value=%u, want %d",
                      state_name(table[i].c), state_name(table[i].t),
                      (unsigned)e.value, table[i].value);

        /* the control is a control: CX must not disturb it */
        cq_shadow c = cq_shadow_get(&sh, 0u);
        CHECK_EQ(c.unknown, (uint8_t)state_unknown(table[i].c));
        cq_shadow_dispose(&sh);
    }
}

CQ_TEST(rule_ccx_over_all_64_operand_combinations)
{
    /* PRD §3: t.unknown |= a.unknown | b.unknown; if (!t.unknown)
     *         t.value ^= a.value & b.value.
     * Restated from the PRD in terms of the operand *states*, so the oracle
     * never consults the implementation. */
    for (state a = SK0; a < N_STATES; a++)
    for (state b = SK0; b < N_STATES; b++)
    for (state t = SK0; t < N_STATES; t++) {
        cq_shadow_table sh;
        fresh(&sh, 3u);
        put(&sh, 0u, a);
        put(&sh, 1u, b);
        put(&sh, 2u, t);

        cq_shadow_ccx(&sh, 0u, 1u, 2u);
        cq_shadow e = cq_shadow_get(&sh, 2u);

        int want_u = state_unknown(a) || state_unknown(b) || state_unknown(t);
        int want_v = state_value(t) ^ (state_value(a) & state_value(b));

        if (e.unknown != (uint8_t)want_u)
            cq_h_fail(__FILE__, __LINE__, "CCX(%s,%s,%s): unknown=%u, want %d",
                      state_name(a), state_name(b), state_name(t),
                      (unsigned)e.unknown, want_u);
        if (!want_u && e.value != (uint8_t)want_v)
            cq_h_fail(__FILE__, __LINE__, "CCX(%s,%s,%s): value=%u, want %d",
                      state_name(a), state_name(b), state_name(t),
                      (unsigned)e.value, want_v);

        /* both controls survive unchanged */
        CHECK_EQ(cq_shadow_get(&sh, 0u).unknown, (uint8_t)state_unknown(a));
        CHECK_EQ(cq_shadow_get(&sh, 1u).unknown, (uint8_t)state_unknown(b));
        cq_shadow_dispose(&sh);
    }
}

CQ_TEST(rule_rotate_poisons_and_is_idempotent)
{
    /* PRD §3 row 4: Ry/Rz(q,θ) — q.unknown = 1, unless θ is in the classical
     * set. Classifying θ is M21's job (§7) and is not reachable from here;
     * M02 owns only the poisoning half. */
    cq_shadow_table sh;
    fresh(&sh, N_STATES);

    for (state s = SK0; s < N_STATES; s++) {
        uint32_t q = (uint32_t)s;
        put(&sh, q, s);

        cq_shadow_rotate(&sh, q);
        CHECK(cq_shadow_get(&sh, q).unknown);
        CHECK(!cq_shadow_known_zero(&sh, q));

        cq_shadow_rotate(&sh, q);
        CHECK(cq_shadow_get(&sh, q).unknown);
    }
    cq_shadow_dispose(&sh);
}

/* -------------------------------------------------------------------------
 * The one-way discipline.
 * ------------------------------------------------------------------------- */

CQ_TEST(no_rule_can_ever_clear_poison)
{
    /* Poison is sticky, and in v1 nothing un-poisons: the sanctioned
     * un-poisoning write is still an OPEN decision (bd ckd.17), so M02 must
     * expose no way to reach it. Every rule is driven against a poisoned
     * target from every operand state; none may return it to determinate. */
    for (state other = SK0; other < N_STATES; other++)
    for (state tgt   = SU0; tgt   < N_STATES; tgt++) {
        cq_shadow_table sh;
        fresh(&sh, 3u);
        put(&sh, 0u, other);
        put(&sh, 1u, other);
        put(&sh, 2u, tgt);

        cq_shadow_x  (&sh, 2u);
        cq_shadow_cx (&sh, 0u, 2u);
        cq_shadow_ccx(&sh, 0u, 1u, 2u);
        cq_shadow_x  (&sh, 2u);

        if (!cq_shadow_get(&sh, 2u).unknown)
            cq_h_fail(__FILE__, __LINE__,
                      "poison on %s cleared by X/CX/CCX with operand %s",
                      state_name(tgt), state_name(other));
        CHECK(!cq_shadow_known_zero(&sh, 2u));
        cq_shadow_dispose(&sh);
    }
}

CQ_TEST(the_shadow_never_claims_determinate_when_the_truth_is_unknown)
{
    /* The soundness property, stated over CCX because it subsumes the other
     * two. Determinate inputs must give a determinate, CORRECT answer — if
     * they did not, the classical short-circuit (L5) would be unsound rather
     * than merely pessimistic. Any unknown input must give unknown. */
    for (state a = SK0; a < N_STATES; a++)
    for (state b = SK0; b < N_STATES; b++)
    for (state t = SK0; t < N_STATES; t++) {
        cq_shadow_table sh;
        fresh(&sh, 3u);
        put(&sh, 0u, a); put(&sh, 1u, b); put(&sh, 2u, t);
        cq_shadow_ccx(&sh, 0u, 1u, 2u);

        cq_shadow e = cq_shadow_get(&sh, 2u);
        int all_determinate = !state_unknown(a) && !state_unknown(b)
                           && !state_unknown(t);

        if (!e.unknown) {
            /* It claims to know. Then every input must have been known, and
             * the answer must be plain C's. */
            if (!all_determinate)
                cq_h_fail(__FILE__, __LINE__,
                          "CCX(%s,%s,%s) claims determinate from an unknown "
                          "input — the shadow lied", state_name(a),
                          state_name(b), state_name(t));
            else
                CHECK_EQ(e.value,
                         (uint8_t)(state_value(t)
                                   ^ (state_value(a) & state_value(b))));
        } else if (all_determinate) {
            cq_h_fail(__FILE__, __LINE__,
                      "CCX(%s,%s,%s) poisoned a fully determinate operand set",
                      state_name(a), state_name(b), state_name(t));
        }
        cq_shadow_dispose(&sh);
    }
}

CQ_TEST(unknown_poisons_even_where_it_provably_cannot_matter)
{
    /* The allowed direction, made explicit so nobody "fixes" it later. With
     * a known-0 control, a & b is 0 whatever b is, so the truth is that t is
     * untouched and still determinate. The shadow says unknown anyway: it
     * tracks bits, not correlations. This is D6 territory — demoting on
     * shadow precision is out of v1 (Key Prohibitions). */
    cq_shadow_table sh;
    fresh(&sh, 3u);
    put(&sh, 0u, SK0);   /* control a: known 0 — the AND cannot fire */
    put(&sh, 1u, SU1);   /* control b: unknown                       */
    put(&sh, 2u, SK0);   /* target:    known 0                       */

    cq_shadow_ccx(&sh, 0u, 1u, 2u);

    CHECK(cq_shadow_get(&sh, 2u).unknown);
    CHECK(!cq_shadow_known_zero(&sh, 2u));
    printf("# CCX(K0, U1, K0) -> unknown: conservative, and deliberate\n");
    cq_shadow_dispose(&sh);
}

CQ_TEST(known_zero_reads_unknown_before_it_reads_value)
{
    /* The I3 / Rule 6 predicate. U0 is the trap: its value byte is 0, so a
     * known_zero that tested `value == 0` without first testing `unknown`
     * would call a poisoned qubit provably clean — which is exactly the
     * laundering that bd ckd.17 warns about, arrived at by accident. */
    cq_shadow_table sh;
    fresh(&sh, N_STATES);
    for (state s = SK0; s < N_STATES; s++) put(&sh, (uint32_t)s, s);

    CHECK(cq_shadow_known_zero(&sh, SK0));
    CHECK(!cq_shadow_known_zero(&sh, SK1));
    CHECK(!cq_shadow_known_zero(&sh, SU0));   /* value byte is 0 — still not clean */
    CHECK(!cq_shadow_known_zero(&sh, SU1));

    CHECK_EQ(cq_shadow_get(&sh, SU0).value, 0u);   /* the trap really is set */
    CHECK_EQ(cq_shadow_get(&sh, SU0).unknown, 1u);
    cq_shadow_dispose(&sh);
}

CQ_TEST_MAIN(
    CQ_CASE(a_fresh_table_is_empty),
    CQ_CASE(qubits_are_born_known_zero),
    CQ_CASE(growth_preserves_every_existing_entry),
    CQ_CASE(ensure_is_monotone_and_never_shrinks),
    CQ_CASE(rule_x_flips_a_known_value_and_is_a_nop_on_poison),
    CQ_CASE(rule_cx_matches_the_literal_prd_table),
    CQ_CASE(rule_ccx_over_all_64_operand_combinations),
    CQ_CASE(rule_rotate_poisons_and_is_idempotent),
    CQ_CASE(no_rule_can_ever_clear_poison),
    CQ_CASE(the_shadow_never_claims_determinate_when_the_truth_is_unknown),
    CQ_CASE(unknown_poisons_even_where_it_provably_cannot_matter),
    CQ_CASE(known_zero_reads_unknown_before_it_reads_value)
)
