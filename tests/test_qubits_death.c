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
 * reads a shadow itself (qubits.h; bd ckd.17 — closed, its unresolved half
 * now PRD §15 D15). A rotation poisons, poison is
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

/* --- Stranding's own fail-loud paths (PRD §15 D15 §3, bd 06t) ------------
 *
 * D15 §3 asks for "one explicit cq_qubits_strand(pool, q) that pushes nothing,
 * MARKS THE INDEX SO A DOUBLE-STRAND OR A LATER RELEASE IS CAUGHT, and
 * increments a counter". The mark is the whole reason the mechanism is not
 * just "return early": without it, stranding is unobservable and the two
 * errors below are silent. */

/* A second strand of the same index would double-count the leak, and — worse —
 * would mean two callers each believed they owned the qubit. */
static void double_strand(void)
{
    cq_qubit_pool p;
    cq_qubits_init(&p);
    uint32_t q = cq_qubits_acquire(&p);
    cq_qubits_strand(&p, q);

    CQ_EXPECT_ABORT(cq_qubits_strand(&p, q));
}

/* THE ONE THAT MATTERS. A release of a stranded index is the laundering move
 * D15 §3 exists to forbid: the index was stranded precisely because nobody
 * could prove it |0⟩, so putting it on the free list hands a non-|0⟩ qubit to
 * the next cq_materialise — "the one unforgivable bug in this project"
 * (qubits.h, the cq_qubits_release contract). Note the `1`: the caller is
 * ASSERTING it is clean, which is exactly the shape this must refuse, and it
 * is why the check cannot live behind the proven_zero test. */
static void release_of_a_stranded_index(void)
{
    cq_qubit_pool p;
    cq_qubits_init(&p);
    uint32_t q = cq_qubits_acquire(&p);
    cq_qubits_strand(&p, q);

    CQ_EXPECT_ABORT(cq_qubits_release(&p, q, 1));
}

/* Stranding an index that was never minted — the mirror of
 * release_of_an_unminted_index, and the reason both exist is that a strand
 * that silently ignored an out-of-range index would under-count the leak.
 *
 * THIS CASE IS A RELEASE-ONLY DETECTOR, MEASURED. Deleting the range guard in
 * cq_qubits_strand SURVIVES in Debug and is KILLED in Release, which is the
 * inverse of the usual asymmetry and is worth knowing before someone "fixes"
 * it. In Debug the abort still happens, one line lower, from
 * cq_pool_free_flag's CQ_POOL_ASSERT(q < p->minted) — so the case passes for a
 * reason that has nothing to do with the guard it names. In Release that
 * assert compiles out, cq_pool_free_flag reads is_free[q] out of bounds, and
 * the case goes red. The masking layer exists in ONE CONFIGURATION ONLY (the
 * Step 15 finding), so a battery run only in Debug would report this guard as
 * untested. Do not add a Debug-only assert here to "make it symmetric" — that
 * would delete the Release detector. */
static void strand_of_an_unminted_index(void)
{
    cq_qubit_pool p;
    cq_qubits_init(&p);
    cq_qubits_acquire(&p);

    CQ_EXPECT_ABORT(cq_qubits_strand(&p, 7u));
}

/* And a strand of an index sitting on the FREE list. That index is already
 * proven |0⟩ and owned by nobody; stranding it would leak a clean qubit and
 * would make `stranded` a count of something other than the residue. */
static void strand_of_a_freed_index(void)
{
    cq_qubit_pool p;
    cq_qubits_init(&p);
    uint32_t q = cq_qubits_acquire(&p);
    cq_qubits_release(&p, q, 1);

    CQ_EXPECT_ABORT(cq_qubits_strand(&p, q));
}

/* --- Step 26's retirement, PRD §15 D21 (b) -------------------------------- */

/* A retired index has already been disposed of, and the is_free test above
 * cannot catch a second release of one — a retired index never reaches the free
 * list, which is the whole of what retirement is. Without its own guard this
 * would fall through to the proven_zero test and be ACCEPTED, since the caller
 * passes a 1 and the index really was proven clean the first time. */
static void release_of_a_retired_index(void)
{
    cq_qubit_pool p;
    cq_qubits_init(&p);
    cq_qubits_set_recycle(&p, 0);
    uint32_t q = cq_qubits_acquire(&p);
    cq_qubits_release(&p, q, 1);

    CQ_EXPECT_ABORT(cq_qubits_release(&p, q, 1));
}

/* And the mirror. A retired qubit was PROVEN |0⟩; stranding it afterwards would
 * add it to `bd 06t`'s residue and report a clean free as an unproven leak,
 * which is exactly the conflation the separate counters exist to prevent. */
static void strand_of_a_retired_index(void)
{
    cq_qubit_pool p;
    cq_qubits_init(&p);
    cq_qubits_set_recycle(&p, 0);
    uint32_t q = cq_qubits_acquire(&p);
    cq_qubits_release(&p, q, 1);

    CQ_EXPECT_ABORT(cq_qubits_strand(&p, q));
}

/* Turning recycling off is a promise about every index the pool will ever hand
 * out, and indices already on the free list are destined for reuse — so the
 * promise cannot be made retroactively. Same posture as a ceiling set below the
 * live count: refuse where the promise is made, not at some later acquire. */
static void recycling_disabled_with_a_non_empty_free_list(void)
{
    cq_qubit_pool p;
    cq_qubits_init(&p);
    uint32_t q = cq_qubits_acquire(&p);
    cq_qubits_release(&p, q, 1);

    CQ_EXPECT_ABORT(cq_qubits_set_recycle(&p, 0));
}

/* D20's hard ceiling, reached the way the qec sink reaches it: with recycling
 * off the ceiling bounds TOTAL MINTED, so a workload that would fit under D4
 * exhausts it. This is the fifth acquire the passing case stops one short of. */
static void ceiling_exceeded_because_nothing_is_recycled(void)
{
    cq_qubit_pool p;
    cq_qubits_init(&p);
    cq_qubits_set_ceiling(&p, 4u);
    cq_qubits_set_recycle(&p, 0);

    for (uint32_t i = 0; i < 4u; i++) {
        uint32_t q = cq_qubits_acquire(&p);
        cq_qubits_release(&p, q, 1);
    }

    CQ_EXPECT_ABORT((void)cq_qubits_acquire(&p));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(ceiling_exceeded),
    CQ_DEATH_CASE(ceiling_set_below_live),
    CQ_DEATH_CASE(dirty_release),
    CQ_DEATH_CASE(release_of_a_qubit_left_at_one),
    CQ_DEATH_CASE(release_of_an_unminted_index),
    CQ_DEATH_CASE(double_release),
    CQ_DEATH_CASE(double_strand),
    CQ_DEATH_CASE(release_of_a_stranded_index),
    CQ_DEATH_CASE(strand_of_an_unminted_index),
    CQ_DEATH_CASE(strand_of_a_freed_index),
    CQ_DEATH_CASE(release_of_a_retired_index),
    CQ_DEATH_CASE(strand_of_a_retired_index),
    CQ_DEATH_CASE(recycling_disabled_with_a_non_empty_free_list),
    CQ_DEATH_CASE(ceiling_exceeded_because_nothing_is_recycled)
)
