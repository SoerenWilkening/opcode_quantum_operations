/* Step 4's other half: M03's five fail-loud paths actually fire.
 *
 * Two of these are named in the plan (D2 ceiling, I3 dirty release). The other
 * three are pool-corruption signatures that would otherwise surface much later
 * as an I2 violation: a double release puts one index on the free list twice,
 * so the next two acquires hand the SAME qubit to two different registers —
 * exactly the aliasing I2 exists to forbid, discovered at Step 24 instead of
 * here.
 */

#include "qubits.h"
#include "shadow.h"
#include "support/death.h"

/* D2 — the ceiling is a promise about the device; exceeding it fails loud. */
static void ceiling_exceeded(void)
{
    cq_qubit_pool p;
    cq_qubits_init(&p);
    cq_qubits_set_ceiling(&p, 3u);

    cq_qubits_acquire(&p);
    cq_qubits_acquire(&p);
    cq_qubits_acquire(&p);      /* exactly at the ceiling: still legal */

    CQ_EXPECT_ABORT(cq_qubits_acquire(&p));
}

/* A ceiling below what is already live cannot be honoured, so it is refused
 * at the point it is set rather than at some later acquire. */
static void ceiling_set_below_live(void)
{
    cq_qubit_pool p;
    cq_qubits_init(&p);
    cq_qubits_acquire(&p);
    cq_qubits_acquire(&p);
    cq_qubits_acquire(&p);

    cq_qubits_set_ceiling(&p, 3u);   /* equal to live: legal */
    CQ_EXPECT_ABORT(cq_qubits_set_ceiling(&p, 2u));
}

/* I3 — a qubit on the free list is |0⟩. The evidence here is the shadow,
 * which is how Step 7 wires it; M03 takes the fact as a parameter and never
 * reads a shadow itself (qubits.h, bd ckd.17). A rotation poisons, poison is
 * sticky, so the qubit is not provably clean and the release must abort. */
static void dirty_release(void)
{
    cq_qubit_pool   p;
    cq_shadow_table sh;
    cq_qubits_init(&p);
    cq_shadow_init(&sh);

    uint32_t q = cq_qubits_acquire(&p);
    cq_shadow_ensure(&sh, 1u);
    cq_shadow_rotate(&sh, q);

    CQ_EXPECT_ABORT(cq_qubits_release(&p, q, cq_shadow_known_zero(&sh, q)));
}

/* The same error reached the other way: a qubit left at |1⟩. Its shadow is
 * determinate, so this is not a poison case — it is the plain "you are about
 * to push a non-zero qubit onto the free list" case, which is what would hand
 * a dirty rail to the next cq_materialise. */
static void release_of_a_qubit_left_at_one(void)
{
    cq_qubit_pool   p;
    cq_shadow_table sh;
    cq_qubits_init(&p);
    cq_shadow_init(&sh);

    uint32_t q = cq_qubits_acquire(&p);
    cq_shadow_ensure(&sh, 1u);
    cq_shadow_x(&sh, q);        /* known 1 — determinate, and not clean */

    CQ_EXPECT_ABORT(cq_qubits_release(&p, q, cq_shadow_known_zero(&sh, q)));
}

/* Releasing an index that was never minted. */
static void release_of_an_unminted_index(void)
{
    cq_qubit_pool p;
    cq_qubits_init(&p);
    cq_qubits_acquire(&p);

    CQ_EXPECT_ABORT(cq_qubits_release(&p, 7u, 1));
}

/* Releasing an index that is already on the free list: the direct route to
 * two live registers owning one qubit (I2). */
static void double_release(void)
{
    cq_qubit_pool p;
    cq_qubits_init(&p);
    uint32_t q = cq_qubits_acquire(&p);
    cq_qubits_release(&p, q, 1);

    CQ_EXPECT_ABORT(cq_qubits_release(&p, q, 1));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(ceiling_exceeded),
    CQ_DEATH_CASE(ceiling_set_below_live),
    CQ_DEATH_CASE(dirty_release),
    CQ_DEATH_CASE(release_of_a_qubit_left_at_one),
    CQ_DEATH_CASE(release_of_an_unminted_index),
    CQ_DEATH_CASE(double_release)
)
