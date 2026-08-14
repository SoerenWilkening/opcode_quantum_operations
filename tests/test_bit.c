/* Step 2's gate: M01's tri-valued bit obeys I1.
 *
 * I1 (PRD §2.2) — "for every bit, kind == CQ_BIT_Q XOR the bit is a known
 * constant. No bit is both, no bit is neither."
 *
 * What that costs to assert honestly, and why this file is longer than the
 * module it tests:
 *
 *   "never two"     is the easy half. One discriminant field cannot hold two
 *                   kinds, so the interesting claim is that the *predicates*
 *                   partition — exactly one of is_zero / is_one / is_qubit is
 *                   true for every bit any constructor can produce.
 *   "never neither"  is the half that can rot. A cq_bit_valid() that returned
 *                   1 unconditionally would make the "never two" half green
 *                   and prove nothing, so the invalid encodings are exercised
 *                   too: every out-of-range kind, and a constant carrying a
 *                   qubit index it has no business owning.
 *
 * There is no ctx, no pool and no sink at Step 2 — M01 is Layer 0 with no
 * internal dependencies (plan §3), so every case here is pure value algebra.
 */

#include "bit.h"
#include "support/harness.h"

#include <stdint.h>
#include <stdio.h>

/* Every bit any constructor can produce, plus the boundary qubit indices.
 * Cases below that claim to hold "over all constructors" walk this table. */
typedef struct {
    const char *what;
    cq_bit      b;
} bit_sample;

enum { N_CONSTRUCTIONS = 9 };

static void all_constructions(bit_sample out[N_CONSTRUCTIONS])
{
    const bit_sample t[N_CONSTRUCTIONS] = {
        { "cq_bit_zero()",            cq_bit_zero()            },
        { "cq_bit_one()",             cq_bit_one()             },
        { "cq_bit_const(0)",          cq_bit_const(0)          },
        { "cq_bit_const(1)",          cq_bit_const(1)          },
        { "cq_bit_const(7)",          cq_bit_const(7)          },
        { "cq_bit_const(-1)",         cq_bit_const(-1)         },
        { "cq_bit_qubit(0)",          cq_bit_qubit(0u)         },
        { "cq_bit_qubit(1)",          cq_bit_qubit(1u)         },
        { "cq_bit_qubit(UINT32_MAX)", cq_bit_qubit(UINT32_MAX) },
    };

    for (size_t i = 0; i < N_CONSTRUCTIONS; i++) out[i] = t[i];
}

/* -------------------------------------------------------------------------
 * I1, the two halves.
 * ------------------------------------------------------------------------- */

CQ_TEST(every_construction_is_a_valid_bit)
{
    bit_sample t[N_CONSTRUCTIONS];
    all_constructions(t);

    for (size_t i = 0; i < N_CONSTRUCTIONS; i++) {
        bit_sample s = t[i];
        if (!cq_bit_valid(s.b))
            cq_h_fail(__FILE__, __LINE__, "%s is not a valid bit "
                      "(kind=%u q=%u)", s.what, (unsigned)s.b.kind,
                      (unsigned)s.b.q);
    }
}

CQ_TEST(exactly_one_kind_holds_never_two_never_neither)
{
    bit_sample t[N_CONSTRUCTIONS];
    all_constructions(t);

    for (size_t i = 0; i < N_CONSTRUCTIONS; i++) {
        bit_sample s    = t[i];
        int        hits = cq_bit_is_zero(s.b)
                        + cq_bit_is_one(s.b)
                        + cq_bit_is_qubit(s.b);

        if (hits != 1)
            cq_h_fail(__FILE__, __LINE__,
                      "%s matches %d kinds, want exactly 1 "
                      "(zero=%d one=%d qubit=%d)", s.what, hits,
                      cq_bit_is_zero(s.b), cq_bit_is_one(s.b),
                      cq_bit_is_qubit(s.b));

        /* is_const is the ZERO|ONE half of the partition, so it must be the
         * exact complement of is_qubit — never a third, looser answer. */
        CHECK_EQ(cq_bit_is_const(s.b), !cq_bit_is_qubit(s.b));
    }
}

CQ_TEST(invalid_encodings_are_rejected)
{
    /* Without this case a cq_bit_valid() that always returned 1 would leave
     * every other case in this file green. */
    for (unsigned k = 0; k < 256u; k++) {
        cq_bit b = { (uint8_t)k, 0u };
        int    want = (k == CQ_BIT_ZERO || k == CQ_BIT_ONE || k == CQ_BIT_Q);

        if (cq_bit_valid(b) != want)
            cq_h_fail(__FILE__, __LINE__,
                      "kind=%u: cq_bit_valid says %d, want %d",
                      k, cq_bit_valid(b), want);
    }

    /* A constant that carries a qubit index is the dangerous invalid: the
     * index is stale by construction (I4 — an all-constant register owns zero
     * qubits), so anything that later read it would be reading a rail owned by
     * someone else. Canonical zero is what makes that unrepresentable. */
    cq_bit stale_zero = { CQ_BIT_ZERO, 7u };
    cq_bit stale_one  = { CQ_BIT_ONE,  7u };
    CHECK(!cq_bit_valid(stale_zero));
    CHECK(!cq_bit_valid(stale_one));
}

/* -------------------------------------------------------------------------
 * What each kind carries.
 * ------------------------------------------------------------------------- */

CQ_TEST(constants_carry_a_value_and_no_qubit)
{
    CHECK_EQ(cq_bit_value(cq_bit_zero()),  0);
    CHECK_EQ(cq_bit_value(cq_bit_one()),   1);
    CHECK_EQ(cq_bit_value(cq_bit_const(0)), 0);
    CHECK_EQ(cq_bit_value(cq_bit_const(1)), 1);

    /* cq_bit_const takes a C truth value, not a bit pattern: any non-zero is
     * ONE. It is reached from CQ_lang literals, where 7 and -1 are ordinary. */
    CHECK_EQ(cq_bit_value(cq_bit_const(7)),  1);
    CHECK_EQ(cq_bit_value(cq_bit_const(-1)), 1);

    CHECK_EQ(cq_bit_zero().q, 0u);
    CHECK_EQ(cq_bit_one().q,  0u);
    CHECK_EQ(cq_bit_const(1).q, 0u);
}

CQ_TEST(qubit_bits_carry_their_index_unchanged)
{
    const uint32_t idx[] = { 0u, 1u, 63u, 0x7fffffffu, UINT32_MAX };

    for (size_t i = 0; i < sizeof idx / sizeof idx[0]; i++) {
        cq_bit b = cq_bit_qubit(idx[i]);

        CHECK(cq_bit_is_qubit(b));
        CHECK(cq_bit_valid(b));
        if (cq_bit_qindex(b) != idx[i])
            cq_h_fail(__FILE__, __LINE__, "cq_bit_qubit(%u) reports index %u",
                      idx[i], cq_bit_qindex(b));
    }
}

CQ_TEST(the_kind_encoding_is_pinned)
{
    /* A constant's kind IS its value (bit.h pins this with a _Static_assert).
     * The numbering is load-bearing for cq_bit_value, so it is asserted here
     * as well — a renumbering must break a test, not just a comment. */
    CHECK_EQ(CQ_BIT_ZERO, 0);
    CHECK_EQ(CQ_BIT_ONE,  1);
    CHECK_EQ(CQ_BIT_Q,    2);

    CHECK_EQ(cq_bit_zero().kind, CQ_BIT_ZERO);
    CHECK_EQ(cq_bit_one().kind,  CQ_BIT_ONE);
    CHECK_EQ(cq_bit_qubit(4u).kind, CQ_BIT_Q);
}

CQ_TEST(the_bit_stays_small_enough_to_hold_no_packed_scalar)
{
    /* I5 — no uint64_t classical, no uint64_t qmask, anywhere. The real
     * enforcement is a grep, but a bit that had grown either one could not
     * still fit in {uint8_t, uint32_t}. This is the cheap runtime tripwire. */
    CHECK(sizeof(cq_bit) <= 8);
    printf("# sizeof(cq_bit) = %zu\n", sizeof(cq_bit));
}

/* -------------------------------------------------------------------------
 * The two operations M01 owns.
 * ------------------------------------------------------------------------- */

CQ_TEST(flipping_a_constant_toggles_it_and_preserves_i1)
{
    /* This is the whole of "X on a constant" (PRD §3, row 1) and of the θ ≡ π
     * classical row (Rule 15): flip the constant, 0 gates, 0 qubits. */
    cq_bit b = cq_bit_zero();

    cq_bit_flip_const(&b);
    CHECK(cq_bit_valid(b));
    CHECK(cq_bit_is_one(b));
    CHECK_EQ(b.q, 0u);

    cq_bit_flip_const(&b);
    CHECK(cq_bit_valid(b));
    CHECK(cq_bit_is_zero(b));
    CHECK_EQ(b.q, 0u);

    /* Two flips are the identity, which is what makes the reverse half of a
     * sandwich cancel on a constant target. */
    cq_bit c = cq_bit_one();
    cq_bit_flip_const(&c);
    cq_bit_flip_const(&c);
    CHECK_EQ(c.kind, cq_bit_one().kind);
    CHECK_EQ(c.q,    cq_bit_one().q);
}

CQ_TEST(coincidence_is_a_property_of_bits_not_of_kinds)
{
    /* PRD §3: the distinctness assert can only ever fire on CQ_BIT_Q operands,
     * and cannot fire on two constants — two constant bits are genuinely
     * independent channels. Two different bits may both be Q and hold
     * different indices; that is an ordinary, legal pair. */
    cq_bit z1 = cq_bit_zero(), z2 = cq_bit_zero();
    cq_bit o1 = cq_bit_one(),  o2 = cq_bit_one();
    cq_bit q3 = cq_bit_qubit(3u), q3b = cq_bit_qubit(3u), q4 = cq_bit_qubit(4u);

    CHECK(!cq_bit_coincident(&z1, &z2));   /* two constants: never coincident */
    CHECK(!cq_bit_coincident(&o1, &o2));
    CHECK(!cq_bit_coincident(&z1, &o1));
    CHECK(!cq_bit_coincident(&z1, &q3));   /* constant vs qubit              */
    CHECK(!cq_bit_coincident(&q3, &q4));   /* distinct rails                 */

    CHECK(cq_bit_coincident(&q3, &q3b));   /* same qubit, two cq_bit objects */
    CHECK(cq_bit_coincident(&q3, &q3));    /* literally the same qubit bit   */

    /* The same CONSTANT object passed twice is NOT coincident, even though it
     * is the same object. PRD §3: the check "can only ever fire on CQ_BIT_Q
     * operands, and cannot fire on two constants". This is not pedantry —
     * CCX(o, o, t) with one constant ONE bit in both control slots is a legal
     * fold to X(t), and Step 6's fold suite aborts on it if a pointer-identity
     * clause creeps back in. That clause is redundant anyway: two Q bits at one
     * address necessarily share an index. */
    CHECK(!cq_bit_coincident(&z1, &z1));
    CHECK(!cq_bit_coincident(&o1, &o1));
}

CQ_TEST_MAIN(
    CQ_CASE(every_construction_is_a_valid_bit),
    CQ_CASE(exactly_one_kind_holds_never_two_never_neither),
    CQ_CASE(invalid_encodings_are_rejected),
    CQ_CASE(constants_carry_a_value_and_no_qubit),
    CQ_CASE(qubit_bits_carry_their_index_unchanged),
    CQ_CASE(the_kind_encoding_is_pinned),
    CQ_CASE(the_bit_stays_small_enough_to_hold_no_packed_scalar),
    CQ_CASE(flipping_a_constant_toggles_it_and_preserves_i1),
    CQ_CASE(coincidence_is_a_property_of_bits_not_of_kinds)
)
