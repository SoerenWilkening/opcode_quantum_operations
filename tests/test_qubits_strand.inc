/* tests/test_qubits_strand.inc — M03's STRANDING disposition (PRD §15 D15 §3).
 *
 * SPLIT FROM tests/test_qubits.c AT STEP 23, and the seam is OWNERSHIP rather
 * than size — the file crossed Rule 12's 300 by two lines, which is what
 * forced the split to happen now, but it is not what chose where.
 *
 *   the POOL's own arithmetic  <->  the DISPOSITION at a free
 *
 * Everything left in the .c is about the counter and the free list: minting,
 * D4's LIFO, D2's ceiling, peak, and the two structural identities. All of it
 * was decided at Step 4 and is a property of the data structure.
 *
 * Everything here is about EVIDENCE, and none of it was decided by M03. PRD
 * §15 D15 arrived nineteen steps later and says what happens to a qubit whose
 * rail nobody could prove |0⟩; the pool merely carries out the verdict. The
 * two subjects share a struct and nothing else — which is exactly why the
 * cases below spend their effort on what stranding is NOT (not a release, not
 * a free-list entry, not a third bucket) rather than on the counter.
 *
 * The fail-loud half is tests/test_qubits_death.c, which also records the one
 * measured asymmetry: strand_of_an_unminted_index is a RELEASE-ONLY detector.
 */

/* -------------------------------------------------------------------------
 * Stranding — PRD §15 D15 §3, built at Step 23 (bd 06t).
 *
 * "The qubit is never released, never reaches the free list, and is counted.
 * The program continues." The mechanism is deliberately NOT a third pool
 * bucket: D15 §3 measured that a `retired` state breaks BOTH of qubits.h's
 * identities, and that a retired index reads as a leaked ancilla to
 * cq_pc_live_is_exactly, which runs on every L1 case of every kernel. Simply
 * NOT CALLING release keeps both identities — a stranded index stays LIVE,
 * because it genuinely is: nobody got it back.
 *
 * That last sentence is the whole design and it is the thing to pin, because
 * the plausible wrong implementation (decrement `live`, or push nothing but
 * count it as free) is invisible to every other case in this file.
 * ------------------------------------------------------------------------- */

CQ_TEST(a_stranded_qubit_stays_live_and_never_reaches_the_free_list)
{
    cq_qubit_pool p;
    cq_qubits_init(&p);

    uint32_t a = cq_qubits_acquire(&p);
    uint32_t s = cq_qubits_acquire(&p);
    uint32_t b = cq_qubits_acquire(&p);
    CHECK_EQ(cq_qubits_stranded(&p), 0u);

    cq_qubits_strand(&p, s);

    /* Counted, and counted ONCE. */
    CHECK_EQ(cq_qubits_stranded(&p), 1u);

    /* Never on the free list. This is the clause that keeps I3 absolute: a
     * stranded index is exactly the one we could not prove |0⟩, so the next
     * cq_materialise must never be handed it.
     *
     * BOTH HALVES, AND THE SECOND IS NOT REDUNDANT. `is_free` says the index
     * is not on the list; `is_stranded` says WHICH index leaked. A count plus
     * a negative is exactly the "an assertion that counts is not an assertion
     * that identifies" shape this project has recorded six times — strand the
     * wrong index and every count still agrees. */
    CHECK(!cq_qubits_is_free(&p, s));
    CHECK(cq_qubits_is_stranded(&p, s));
    CHECK(!cq_qubits_is_stranded(&p, a));
    CHECK(!cq_qubits_is_stranded(&p, b));
    CHECK_EQ(cq_qubits_free(&p), 0u);

    /* STILL LIVE. Not "retired", not "free" — live, because nothing returned
     * it. Both identities therefore hold with no arithmetic at all. */
    CHECK_EQ(cq_qubits_live(&p), 3u);
    check_identities(&p, "after a strand");

    /* And it is unreachable: releasing the other two and re-acquiring twice
     * comes back with a and b, never s. */
    cq_qubits_release(&p, b, 1);
    cq_qubits_release(&p, a, 1);
    CHECK_EQ(cq_qubits_acquire(&p), a);
    CHECK_EQ(cq_qubits_acquire(&p), b);
    check_identities(&p, "after reuse around a strand");

    /* A fourth acquire mints fresh rather than reaching the stranded index. */
    CHECK_EQ(cq_qubits_acquire(&p), 3u);
    CHECK_EQ(cq_qubits_stranded(&p), 1u);

    cq_qubits_dispose(&p);
}

CQ_TEST(stranding_is_not_free_and_is_not_release_measured_against_both)
{
    /* The differential the counter alone cannot make. Two pools, the same
     * script, one strand where the other releases — every pool figure that
     * moves is named, so an implementation that quietly routed strand through
     * release would have to disagree with one of them. */
    cq_qubit_pool s, r;
    cq_qubits_init(&s);
    cq_qubits_init(&r);

    for (int i = 0; i < 4; i++) { (void)cq_qubits_acquire(&s); (void)cq_qubits_acquire(&r); }

    cq_qubits_strand(&s, 2u);
    cq_qubits_release(&r, 2u, 1);

    CHECK_EQ(cq_qubits_live(&s), 4u);      /* strand keeps it              */
    CHECK_EQ(cq_qubits_live(&r), 3u);      /* release gives it back        */
    CHECK_EQ(cq_qubits_free(&s), 0u);
    CHECK_EQ(cq_qubits_free(&r), 1u);
    CHECK_EQ(cq_qubits_stranded(&s), 1u);
    CHECK_EQ(cq_qubits_stranded(&r), 0u);
    CHECK(cq_qubits_is_stranded(&s, 2u));
    CHECK(!cq_qubits_is_stranded(&r, 2u));

    /* THE CONFIGURATION TRIPWIRE. The tempting encoding is a third value in
     * the existing `is_free` byte — and it is a Debug/Release SPLIT: the
     * pool's read accessor asserts that byte is 0 or 1 only under
     * CQOPS_DEBUG_INVARIANTS, so a `2` aborts in Debug and reads as TRUE in
     * Release, i.e. a stranded index would report as sitting on the free
     * list. That is behaviour diverging by configuration on the one mechanism
     * whose whole job is I3, which ctx.h forbids by name. This pair of
     * assertions is what makes the two states disjoint rather than ordered. */
    CHECK(!(cq_qubits_is_free(&s, 2u) && cq_qubits_is_stranded(&s, 2u)));
    CHECK(cq_qubits_is_free(&r, 2u));
    CHECK(!cq_qubits_is_stranded(&r, 2u));
    CHECK_EQ(cq_qubits_minted(&s), cq_qubits_minted(&r));
    CHECK_EQ(cq_qubits_peak(&s), cq_qubits_peak(&r));
    check_identities(&s, "stranding pool");
    check_identities(&r, "releasing pool");

    /* The observable consequence: the releasing pool recycles index 2, the
     * stranding pool cannot and mints 4 instead. */
    CHECK_EQ(cq_qubits_acquire(&r), 2u);
    CHECK_EQ(cq_qubits_acquire(&s), 4u);

    cq_qubits_dispose(&s);
    cq_qubits_dispose(&r);
}
