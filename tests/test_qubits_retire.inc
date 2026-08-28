/* tests/test_qubits_retire.inc — Step 26's pool half: PRD §15 D21 (b).
 *
 * INCLUDED BY tests/test_qubits.c, on the seam test_qubits_strand.inc already
 * took: the POOL's arithmetic in the .c, a DISPOSITION in an .inc of its own.
 *
 * RETIREMENT IS STRANDING'S MECHANISM UNDER A DIFFERENT FACT, and the cases
 * below are here to keep the two apart. A stranded qubit could not be proven
 * |0⟩; a retired one WAS proven |0⟩ and is dropped anyway, because
 * qec/docs/HOST_LANGUAGE_HANDOFF.md §3 — the contract the trace annotations
 * answer to — says every index belongs to at most one register, and D4's LIFO
 * reuse would hand a freed rail's index to an unrelated rail. Conflating them
 * would make `bd 06t`'s residue report call every ordinary clean free under the
 * qec sink an unproven leak.
 *
 * IT IS A DISPLAY-MODEL CONSTRAINT AND NOT A PHYSICAL ONE: the qec patch really
 * is back at |0⟩ after our free and would happily be reused. Hence a sink-mode
 * toggle set at install, never a change to D4.
 */

CQ_TEST(recycling_is_on_by_default_and_off_makes_a_release_retire)
{
    /* D4 on its OWN pool, so this is a contrast and not a baseline — and
     * because turning recycling off with that release still on the free list is
     * itself a hard error (the promise cannot be made retroactively; the case
     * for it is in test_qubits_death.c). */
    cq_qubit_pool d4;
    cq_qubits_init(&d4);
    CHECK(cq_qubits_recycles(&d4));
    uint32_t a = cq_qubits_acquire(&d4);
    cq_qubits_release(&d4, a, 1);
    CHECK_EQ(cq_qubits_acquire(&d4), a);
    cq_qubits_dispose(&d4);

    cq_qubit_pool p;
    cq_qubits_init(&p);
    cq_qubits_set_recycle(&p, 0);
    CHECK(!cq_qubits_recycles(&p));

    uint32_t b = cq_qubits_acquire(&p);
    CHECK_EQ(cq_qubits_live(&p), 1u);

    cq_qubits_release(&p, b, 1);
    CHECK(cq_qubits_is_retired(&p, b));
    CHECK(!cq_qubits_is_free(&p, b));
    CHECK(!cq_qubits_is_stranded(&p, b));
    CHECK_EQ(cq_qubits_retired(&p), 1u);
    CHECK_EQ(cq_qubits_stranded(&p), 0u);

    /* NOTHING ELSE MOVED: the index is still live because nobody got it back,
     * so both structural identities hold with no arithmetic — the same reason
     * stranding is not a third bucket. */
    CHECK_EQ(cq_qubits_live(&p), 1u);
    CHECK_EQ(cq_qubits_free(&p), 0u);
    check_identities(&p, "after a retire");

    /* And the point of the whole mode: the next acquire is a FRESH index. */
    uint32_t c = cq_qubits_acquire(&p);
    CHECK(c != b);
    CHECK_EQ(c, b + 1u);
    check_identities(&p, "after the fresh mint");

    cq_qubits_dispose(&p);
}

/* THE CLAIM THE MODE EXISTS FOR, stated as the contract's own words: over a
 * whole workload no index is ever handed out twice. A count cannot see this —
 * retire one index and reuse another and every total agrees — so the case
 * records each index and checks the set, which is L2's argument one layer down.
 */
CQ_TEST(with_recycling_off_no_index_is_ever_handed_out_twice)
{
    cq_qubit_pool p;
    cq_qubits_init(&p);
    cq_qubits_set_recycle(&p, 0);

    uint8_t seen[64] = { 0 };
    for (uint32_t round = 0; round < 20u; round++) {
        uint32_t q[3];
        for (int i = 0; i < 3; i++) {
            q[i] = cq_qubits_acquire(&p);
            CHECK(q[i] < 64u);
            if (seen[q[i]])
                cq_h_fail(__FILE__, __LINE__,
                          "index %u handed out twice, in round %u", q[i], round);
            seen[q[i]] = 1;
        }
        /* Released in a different order each round, so a LIFO that slipped
         * through would show up as a reuse rather than as a coincidence. */
        cq_qubits_release(&p, q[round % 3u], 1);
        cq_qubits_release(&p, q[(round + 1u) % 3u], 1);
        cq_qubits_release(&p, q[(round + 2u) % 3u], 1);
        check_identities(&p, "inside the no-reuse workload");
    }

    CHECK_EQ(cq_qubits_retired(&p), 60u);
    CHECK_EQ(cq_qubits_minted(&p), 60u);
    CHECK_EQ(cq_qubits_live(&p), 60u);
    CHECK_EQ(cq_qubits_free(&p), 0u);

    cq_qubits_dispose(&p);
}

/* THE COST OF THE MODE, PINNED SO IT IS NOT DISCOVERED AS A SURPRISE: D2's
 * ceiling now bounds TOTAL MINTED rather than the peak, because `live` never
 * falls. Under D20's Toffoli-free-at-small-width scope that is not what limits
 * anything, but it is a real change in what the ceiling means and the qec sink
 * sets both modes together for that reason.
 *
 * The other half is the one that was checked rather than assumed: the ceiling
 * bounds the INDEX in BOTH modes. `peak == minted` and `live <= ceiling` is
 * enforced at every acquire, so `minted <= ceiling` holds with recycling ON
 * too — which is why the qec sink's index safety is D2's alone and this mode
 * takes no credit for it. */
CQ_TEST(the_ceiling_bounds_total_minted_once_recycling_is_off)
{
    cq_qubit_pool p;
    cq_qubits_init(&p);
    cq_qubits_set_ceiling(&p, 4u);

    /* With D4 on, four qubits serve an unbounded number of acquires — and
     * every index is already below the ceiling. */
    for (uint32_t i = 0; i < 40u; i++) {
        uint32_t q = cq_qubits_acquire(&p);
        CHECK(q < 4u);
        cq_qubits_release(&p, q, 1);
    }
    CHECK_EQ(cq_qubits_minted(&p), 1u);

    cq_qubit_pool r;
    cq_qubits_init(&r);
    cq_qubits_set_ceiling(&r, 4u);
    cq_qubits_set_recycle(&r, 0);

    for (uint32_t i = 0; i < 4u; i++) {
        uint32_t q = cq_qubits_acquire(&r);
        CHECK(q < 4u);
        cq_qubits_release(&r, q, 1);
    }
    /* The fifth would abort — that case is in test_qubits_death.c. */
    CHECK_EQ(cq_qubits_minted(&r), 4u);
    CHECK_EQ(cq_qubits_live(&r), 4u);
    CHECK_EQ(cq_qubits_retired(&r), 4u);

    cq_qubits_dispose(&r);
    cq_qubits_dispose(&p);
}
