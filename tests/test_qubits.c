/* Step 4's gate: M03's qubit pool. D2 ceiling, D4 LIFO, I3, peak tracking.
 *
 * The pool is a monotonic counter plus a free list, and its whole job is to
 * hand out indices that are provably |0⟩ (I3) so cq_materialise never has to
 * ask. Two structural facts get pinned here because everything downstream
 * quietly assumes them:
 *
 *   minted == live + free      every index ever minted is in exactly one of
 *                              the two states, so indices are neither leaked
 *                              nor double-counted
 *   peak   == minted           a fresh index is minted ONLY when the free
 *                              list is empty, i.e. only when live has already
 *                              reached minted. So the monotonic counter *is*
 *                              the high-water mark, and the two can never
 *                              disagree — which is what makes the counter
 *                              sink's "peak qubits" line (PRD §8) trustworthy
 *                              without a second mechanism.
 *
 * The fail-loud paths live in test_qubits_death.c: CTest cannot express an
 * abort() in the same binary as ordinary cases.
 */

#include "qubits.h"
#include "shadow.h"
#include "support/harness.h"

#include <stdio.h>

/* Every release in this file goes through the shadow, which is how the pool
 * is actually wired at Step 7 — M03 itself takes the evidence as a parameter
 * and never reads a shadow (see qubits.h on bd ckd.17 — closed, its
 * unresolved half now PRD §15 D15). */
static void release_clean(cq_qubit_pool *p, cq_shadow_table *sh, uint32_t q)
{
    cq_qubits_release(p, q, cq_shadow_known_zero(sh, q));
}

/* The two structural identities, checked after every interesting operation. */
static void check_identities(const cq_qubit_pool *p, const char *where)
{
    if (cq_qubits_minted(p) != cq_qubits_live(p) + cq_qubits_free(p))
        cq_h_fail(__FILE__, __LINE__,
                  "%s: minted=%u but live=%u + free=%u", where,
                  cq_qubits_minted(p), cq_qubits_live(p), cq_qubits_free(p));

    if (cq_qubits_peak(p) != cq_qubits_minted(p))
        cq_h_fail(__FILE__, __LINE__, "%s: peak=%u but minted=%u", where,
                  cq_qubits_peak(p), cq_qubits_minted(p));
}

/* -------------------------------------------------------------------------
 * Minting.
 * ------------------------------------------------------------------------- */

CQ_TEST(a_fresh_pool_is_empty_and_unbounded)
{
    cq_qubit_pool p;
    cq_qubits_init(&p);

    CHECK_EQ(cq_qubits_live(&p), 0u);
    CHECK_EQ(cq_qubits_peak(&p), 0u);
    CHECK_EQ(cq_qubits_minted(&p), 0u);
    CHECK_EQ(cq_qubits_free(&p), 0u);
    CHECK_EQ(cq_qubits_ceiling(&p), 0u);   /* D2: default unbounded */
    check_identities(&p, "fresh");

    cq_qubits_dispose(&p);
}

CQ_TEST(acquire_mints_monotonically_while_the_free_list_is_empty)
{
    cq_qubit_pool p;
    cq_qubits_init(&p);

    /* 200 crosses the pool's growth boundaries (64 -> 128 -> 256), which is
     * the point: a freshly minted index must read as not-free, and in Debug
     * the newly grown tail of the state map is poisoned so a missing
     * initialiser fails here deterministically rather than depending on the
     * allocator handing back zeroed memory. */
    for (uint32_t i = 0; i < 200u; i++) {
        uint32_t q = cq_qubits_acquire(&p);
        if (q != i)
            cq_h_fail(__FILE__, __LINE__, "acquire #%u gave index %u", i, q);
        if (cq_qubits_is_free(&p, q))
            cq_h_fail(__FILE__, __LINE__, "freshly minted q%u reads as free", q);
        CHECK_EQ(cq_qubits_live(&p), i + 1u);
        CHECK_EQ(cq_qubits_minted(&p), i + 1u);
    }
    check_identities(&p, "after 200 acquires");
    cq_qubits_dispose(&p);
}

/* -------------------------------------------------------------------------
 * D4 — LIFO.
 * ------------------------------------------------------------------------- */

CQ_TEST(release_then_acquire_reuses_the_last_released_index)
{
    /* D4. Lowest-index-first (Bennett's WireAllocator) would give tighter
     * peak counts; LIFO was chosen for quieter trace diffs, and the choice is
     * only revisited at L4. Pin it so a "tidier" allocator cannot land
     * unnoticed and churn every trace golden. */
    cq_qubit_pool   p;
    cq_shadow_table sh;
    cq_qubits_init(&p);
    cq_shadow_init(&sh);

    for (uint32_t i = 0; i < 5u; i++) { cq_qubits_acquire(&p); }
    cq_shadow_ensure(&sh, 5u);

    release_clean(&p, &sh, 1u);
    release_clean(&p, &sh, 3u);
    release_clean(&p, &sh, 0u);
    check_identities(&p, "after 3 releases");
    CHECK_EQ(cq_qubits_free(&p), 3u);
    CHECK_EQ(cq_qubits_live(&p), 2u);

    CHECK_EQ(cq_qubits_acquire(&p), 0u);   /* last released comes back first */
    CHECK_EQ(cq_qubits_acquire(&p), 3u);
    CHECK_EQ(cq_qubits_acquire(&p), 1u);

    /* free list drained — the next one is freshly minted */
    CHECK_EQ(cq_qubits_free(&p), 0u);
    CHECK_EQ(cq_qubits_acquire(&p), 5u);
    check_identities(&p, "after draining the free list");

    cq_shadow_dispose(&sh);
    cq_qubits_dispose(&p);
}

CQ_TEST(lifo_holds_over_a_longer_interleaved_workload)
{
    cq_qubit_pool   p;
    cq_shadow_table sh;
    cq_qubits_init(&p);
    cq_shadow_init(&sh);

    uint32_t held[64];
    for (uint32_t i = 0; i < 64u; i++) held[i] = cq_qubits_acquire(&p);
    cq_shadow_ensure(&sh, 64u);

    /* release the whole run in order, then drain: strict reverse order out */
    for (uint32_t i = 0; i < 64u; i++) release_clean(&p, &sh, held[i]);
    check_identities(&p, "all released");

    for (uint32_t i = 0; i < 64u; i++) {
        uint32_t want = held[63u - i];
        uint32_t got  = cq_qubits_acquire(&p);
        if (got != want)
            cq_h_fail(__FILE__, __LINE__,
                      "drain #%u gave %u, want %u (LIFO)", i, got, want);
    }
    check_identities(&p, "drained");
    CHECK_EQ(cq_qubits_minted(&p), 64u);   /* nothing new was minted */

    cq_shadow_dispose(&sh);
    cq_qubits_dispose(&p);
}

/* -------------------------------------------------------------------------
 * Peak and the structural identities.
 * ------------------------------------------------------------------------- */

CQ_TEST(peak_is_a_high_water_mark_and_never_falls)
{
    cq_qubit_pool   p;
    cq_shadow_table sh;
    cq_qubits_init(&p);
    cq_shadow_init(&sh);

    for (uint32_t i = 0; i < 10u; i++) cq_qubits_acquire(&p);
    cq_shadow_ensure(&sh, 10u);
    CHECK_EQ(cq_qubits_peak(&p), 10u);

    for (uint32_t q = 0; q < 8u; q++) release_clean(&p, &sh, q);
    CHECK_EQ(cq_qubits_live(&p), 2u);
    CHECK_EQ(cq_qubits_peak(&p), 10u);     /* unmoved by the releases */

    /* reuse stays under the mark, so the mark stays put */
    for (uint32_t i = 0; i < 8u; i++) cq_qubits_acquire(&p);
    CHECK_EQ(cq_qubits_live(&p), 10u);
    CHECK_EQ(cq_qubits_peak(&p), 10u);
    CHECK_EQ(cq_qubits_minted(&p), 10u);

    /* one past the mark moves it, by exactly one */
    cq_qubits_acquire(&p);
    CHECK_EQ(cq_qubits_peak(&p), 11u);
    check_identities(&p, "past the mark");

    cq_shadow_dispose(&sh);
    cq_qubits_dispose(&p);
}

CQ_TEST(the_identities_hold_across_a_scripted_workload)
{
    /* A deterministic acquire/release mix — no rng, so a failure reproduces.
     * Both identities are checked after every single operation. */
    cq_qubit_pool   p;
    cq_shadow_table sh;
    cq_qubits_init(&p);
    cq_shadow_init(&sh);

    uint32_t held[128];
    uint32_t n_held = 0;

    for (uint32_t step = 0; step < 500u; step++) {
        if (n_held < 128u && (step % 3u != 2u)) {
            held[n_held++] = cq_qubits_acquire(&p);
            cq_shadow_ensure(&sh, cq_qubits_minted(&p));
        } else if (n_held > 0u) {
            release_clean(&p, &sh, held[--n_held]);
        }
        check_identities(&p, "scripted workload");
    }

    CHECK_EQ(cq_qubits_live(&p), n_held);
    printf("# scripted workload: minted=%u peak=%u live=%u\n",
           cq_qubits_minted(&p), cq_qubits_peak(&p), cq_qubits_live(&p));

    cq_shadow_dispose(&sh);
    cq_qubits_dispose(&p);
}

CQ_TEST(is_free_tracks_the_free_list_exactly)
{
    cq_qubit_pool   p;
    cq_shadow_table sh;
    cq_qubits_init(&p);
    cq_shadow_init(&sh);

    for (uint32_t i = 0; i < 6u; i++) cq_qubits_acquire(&p);
    cq_shadow_ensure(&sh, 6u);
    for (uint32_t q = 0; q < 6u; q++) CHECK(!cq_qubits_is_free(&p, q));

    release_clean(&p, &sh, 2u);
    release_clean(&p, &sh, 4u);

    for (uint32_t q = 0; q < 6u; q++) {
        int want = (q == 2u || q == 4u);
        if (cq_qubits_is_free(&p, q) != want)
            cq_h_fail(__FILE__, __LINE__, "q%u: is_free=%d, want %d",
                      q, cq_qubits_is_free(&p, q), want);
    }

    CHECK_EQ(cq_qubits_acquire(&p), 4u);
    CHECK(!cq_qubits_is_free(&p, 4u));
    CHECK(cq_qubits_is_free(&p, 2u));

    cq_shadow_dispose(&sh);
    cq_qubits_dispose(&p);
}

/* -------------------------------------------------------------------------
 * D2 — the ceiling.
 * ------------------------------------------------------------------------- */

CQ_TEST(the_ceiling_bounds_live_qubits_not_total_acquires)
{
    /* D2 pins the ceiling to qec_n_logical, which is how many logical qubits
     * exist at once — so reuse must not count against it. A ceiling on total
     * acquires would make any long program fail on a device that could run
     * it. */
    cq_qubit_pool   p;
    cq_shadow_table sh;
    cq_qubits_init(&p);
    cq_shadow_init(&sh);
    cq_qubits_set_ceiling(&p, 4u);
    CHECK_EQ(cq_qubits_ceiling(&p), 4u);

    for (uint32_t i = 0; i < 4u; i++) cq_qubits_acquire(&p);
    cq_shadow_ensure(&sh, 4u);
    CHECK_EQ(cq_qubits_live(&p), 4u);

    /* 40 more acquires, never more than 4 live at once: all legal */
    for (uint32_t i = 0; i < 40u; i++) {
        release_clean(&p, &sh, 3u);
        CHECK_EQ(cq_qubits_acquire(&p), 3u);
    }
    CHECK_EQ(cq_qubits_live(&p), 4u);
    CHECK_EQ(cq_qubits_minted(&p), 4u);
    CHECK_EQ(cq_qubits_peak(&p), 4u);

    cq_shadow_dispose(&sh);
    cq_qubits_dispose(&p);
}

CQ_TEST(a_ceiling_can_be_raised_lowered_and_lifted)
{
    cq_qubit_pool p;
    cq_qubits_init(&p);

    for (uint32_t i = 0; i < 3u; i++) cq_qubits_acquire(&p);

    cq_qubits_set_ceiling(&p, 3u);     /* exactly at live: legal */
    CHECK_EQ(cq_qubits_ceiling(&p), 3u);

    cq_qubits_set_ceiling(&p, 100u);
    CHECK_EQ(cq_qubits_ceiling(&p), 100u);
    cq_qubits_acquire(&p);
    CHECK_EQ(cq_qubits_live(&p), 4u);

    cq_qubits_set_ceiling(&p, 0u);     /* 0 lifts it again */
    CHECK_EQ(cq_qubits_ceiling(&p), 0u);
    for (uint32_t i = 0; i < 50u; i++) cq_qubits_acquire(&p);
    CHECK_EQ(cq_qubits_live(&p), 54u);

    cq_qubits_dispose(&p);
}

/* -------------------------------------------------------------------------
 * I3 — the clean-release contract, on its passing side.
 * ------------------------------------------------------------------------- */

CQ_TEST(a_provably_clean_qubit_releases_and_comes_back)
{
    /* The dirty half is a hard error and lives in test_qubits_death.c. This
     * is the half that must NOT abort: a known-0 shadow releases fine, and
     * the index returns to service. */
    cq_qubit_pool   p;
    cq_shadow_table sh;
    cq_qubits_init(&p);
    cq_shadow_init(&sh);

    uint32_t q = cq_qubits_acquire(&p);
    cq_shadow_ensure(&sh, 1u);
    CHECK(cq_shadow_known_zero(&sh, q));

    /* materialise-then-unmaterialise: X twice leaves it known-0 again */
    cq_shadow_x(&sh, q);
    CHECK(!cq_shadow_known_zero(&sh, q));
    cq_shadow_x(&sh, q);
    CHECK(cq_shadow_known_zero(&sh, q));

    release_clean(&p, &sh, q);
    CHECK(cq_qubits_is_free(&p, q));
    CHECK_EQ(cq_qubits_acquire(&p), q);

    cq_shadow_dispose(&sh);
    cq_qubits_dispose(&p);
}

/* The stranding disposition (PRD §15 D15 §3) — split on the seam recorded in
 * the .inc's own header: the POOL's arithmetic against the DISPOSITION at a
 * free. Rule 12 forced the timing; the seam chose the place. */
#include "test_qubits_strand.inc"

/* And Step 26's disposition beside it, on the same seam. */
#include "test_qubits_retire.inc"

CQ_TEST(dispose_returns_the_pool_to_its_initial_state)
{
    cq_qubit_pool p;
    cq_qubits_init(&p);
    for (uint32_t i = 0; i < 20u; i++) cq_qubits_acquire(&p);
    cq_qubits_set_ceiling(&p, 64u);

    cq_qubits_dispose(&p);

    CHECK_EQ(cq_qubits_live(&p), 0u);
    CHECK_EQ(cq_qubits_minted(&p), 0u);
    CHECK_EQ(cq_qubits_peak(&p), 0u);
    CHECK_EQ(cq_qubits_ceiling(&p), 0u);
    CHECK_EQ(cq_qubits_stranded(&p), 0u);
    CHECK_EQ(cq_qubits_retired(&p), 0u);
    CHECK(cq_qubits_recycles(&p));      /* D4 again, not the qec sink's mode */
    check_identities(&p, "after dispose");

    /* usable again without a second init */
    CHECK_EQ(cq_qubits_acquire(&p), 0u);
    cq_qubits_dispose(&p);
}

CQ_TEST_MAIN(
    CQ_CASE(a_fresh_pool_is_empty_and_unbounded),
    CQ_CASE(acquire_mints_monotonically_while_the_free_list_is_empty),
    CQ_CASE(release_then_acquire_reuses_the_last_released_index),
    CQ_CASE(lifo_holds_over_a_longer_interleaved_workload),
    CQ_CASE(peak_is_a_high_water_mark_and_never_falls),
    CQ_CASE(the_identities_hold_across_a_scripted_workload),
    CQ_CASE(is_free_tracks_the_free_list_exactly),
    CQ_CASE(the_ceiling_bounds_live_qubits_not_total_acquires),
    CQ_CASE(a_ceiling_can_be_raised_lowered_and_lifted),
    CQ_CASE(a_provably_clean_qubit_releases_and_comes_back),
    CQ_CASE(a_stranded_qubit_stays_live_and_never_reaches_the_free_list),
    CQ_CASE(stranding_is_not_free_and_is_not_release_measured_against_both),
    CQ_CASE(recycling_is_on_by_default_and_off_makes_a_release_retire),
    CQ_CASE(with_recycling_off_no_index_is_ever_handed_out_twice),
    CQ_CASE(the_ceiling_bounds_total_minted_once_recycling_is_off),
    CQ_CASE(dispose_returns_the_pool_to_its_initial_state)
)
