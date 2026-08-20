/* tests/test_kernel_addacc_upstream.inc — included by test_kernel_addacc.c.
 *
 * THE STRUCTURAL HALF. Everything in the parent file is a differential against
 * a reference: values, counts, pool. None of it can see a transcription that is
 * self-consistently wrong, and Cuccaro has two such shapes, both cheap to write
 * by accident:
 *
 *   - MAJ and UMA are each other's phases REVERSED. Emitting a UMA where a MAJ
 *     belongs, or getting a phase order wrong inside one, changes the circuit
 *     while leaving the gate multiset identical — so L4 agrees, and at many
 *     (acc, b) pairs L1 does too.
 *   - the §3.5 optimisation drops a Toffoli in phase 1 and its PARTNER in phase
 *     3 and relocates one into phase 2. Drop the wrong pair and the counts are
 *     still (0, 4W-2, 2W-3).
 *
 * So this file pins the emitted stream gate for gate against literals read off
 * third_party/bennett/src/adder.jl, at the three widths that exercise the three
 * distinct shapes: W=2 (the separate :84-96 branch, where blocks A and G are
 * absent), W=3 (both middle loops degenerate to empty), and W=4 (both run
 * exactly once — the first width where an ascending/descending mix-up in the
 * middle chain is expressible at all).
 *
 * THE EXPECTED STREAM IS BUILT FROM THE REGISTERS' OWN QUBIT INDICES, not from
 * literals like `q0`. What is being pinned is WHICH WIRE each gate touches and
 * in what order; hard-coding 0,1,2... would additionally pin the pool's
 * allocation order, which is D4 free-list policy and explicitly not part of any
 * golden (risk R5).
 */

/* The qubit index behind bit `i` of a register — every bit here is CQ_BIT_Q by
 * construction, since all-quantum is the only mask K8 accepts. */
static uint32_t qi(const cq_ctx *ctx, int32_t h, int i)
{
    return cq_bit_qindex(cq_reg_cbits(&ctx->regs, h)[i]);
}

/* Records the whole construction at width W into `m`, and hands back the three
 * registers so the caller can name wires. */
typedef struct { cq_ctx ctx; int32_t h_acc, h_b, h_x; cq_addacc_block k; } k8_rec;

static void k8_record(k8_rec *r, cq_mock *m, int W, uint64_t va, uint64_t vb)
{
    static cq_sink s;
    cq_ref_w all = cq_ref_w_ones(W);

    s = cq_mock_sink(m);
    cq_mock_reset(m);
    cq_ctx_init(&r->ctx, &s);

    r->h_acc = cq_bk_reg_w(&r->ctx, (uint32_t)W, cq_ref_w_make(va, 0u, W), all);
    r->h_b   = cq_bk_reg_w(&r->ctx, (uint32_t)W, cq_ref_w_make(vb, 0u, W), all);
    r->h_x   = cq_bk_reg_w(&r->ctx, 1u, cq_ref_w_zero(), cq_ref_w_ones(1));

    r->k.acc = cq_reg_bits(&r->ctx.regs, r->h_acc);
    r->k.b   = cq_reg_bits(&r->ctx.regs, r->h_b);
    r->k.x   = cq_reg_bits(&r->ctx.regs, r->h_x);
    r->k.W   = W;

    /* Building the operands materialises their set bits, which emits X. Drop
     * that, so what is compared is the KERNEL. */
    cq_mock_reset(m);
    cq_kernel_addacc(&r->ctx, &r->k);
}

CQ_TEST(the_gate_stream_is_adder_jl_transcribed)
{
    cq_mock m;
    k8_rec r;

    cq_mock_init(&m);

    /* --- W = 1. NOT upstream: adder.jl:66 sends W<=1 to the OUT-OF-PLACE
     * lower_add!, which is not an accumulator at all. `acc += b mod 2` is one
     * CX in place (K08.md §5 D1). ---------------------------------------- */
    {
        k8_record(&r, &m, 1, 1u, 1u);
        {
            const cq_rec want[] = { CQ_REC_CX(qi(&r.ctx, r.h_b, 0),
                                              qi(&r.ctx, r.h_acc, 0)) };
            CHECK(cq_mock_matches(&m, want, 1u));
        }
        cq_ctx_dispose(&r.ctx);
    }

    /* --- W = 2, adder.jl:84-96, seven gates. The §3.5 note says both the first
     * MAJ's Toffoli and the last UMA's matching Toffoli are dropped here, so NO
     * GATE TARGETS THE ADDEND at this width — asserted separately below. --- */
    {
        k8_record(&r, &m, 2, 2u, 3u);
        {
            uint32_t a0 = qi(&r.ctx, r.h_acc, 0), a1 = qi(&r.ctx, r.h_acc, 1);
            uint32_t b0 = qi(&r.ctx, r.h_b,   0), b1 = qi(&r.ctx, r.h_b,   1);
            uint32_t x  = qi(&r.ctx, r.h_x,   0);
            const cq_rec want[] = {
                CQ_REC_CX (b0, a0),        /* :86  CNOT(a[1], b[1])          */
                CQ_REC_CX (b0, x ),        /* :87  CNOT(a[1], X[1])          */
                CQ_REC_CX (b1, a1),        /* :89  CNOT(a[W], b[W])          */
                CQ_REC_CX (b0, a1),        /* :90  CNOT(a[W-1], b[W])        */
                CQ_REC_CCX(x, a0, a1),     /* :91  Toffoli(X[1], b[1], b[W]) */
                CQ_REC_CX (b0, x ),        /* :93  CNOT(a[1], X[1])          */
                CQ_REC_CX (x,  a0)         /* :94  CNOT(X[1], b[1])          */
            };
            CHECK(cq_mock_matches(&m, want, 7u));
        }
        cq_ctx_dispose(&r.ctx);
    }

    /* --- W = 3, adder.jl:98-145 with both middle loops EMPTY (`2:1` and
     * `1:-1:2`), thirteen gates. ----------------------------------------- */
    {
        k8_record(&r, &m, 3, 5u, 6u);
        {
            uint32_t a0 = qi(&r.ctx, r.h_acc, 0), a1 = qi(&r.ctx, r.h_acc, 1);
            uint32_t a2 = qi(&r.ctx, r.h_acc, 2);
            uint32_t b0 = qi(&r.ctx, r.h_b, 0), b1 = qi(&r.ctx, r.h_b, 1);
            uint32_t b2 = qi(&r.ctx, r.h_b, 2);
            uint32_t x  = qi(&r.ctx, r.h_x, 0);
            const cq_rec want[] = {
                CQ_REC_CX (b0, a0),        /* :102 first MAJ                 */
                CQ_REC_CX (b0, x ),        /* :103                           */
                CQ_REC_CCX(x, a0, b0),     /* :104 TARGETS THE ADDEND        */
                CQ_REC_CX (b1, a1),        /* :116 last MAJ, Toffoli dropped */
                CQ_REC_CX (b1, b0),        /* :117 TARGETS THE ADDEND        */
                CQ_REC_CX (b2, a2),        /* :123 phase 2                   */
                CQ_REC_CX (b1, a2),        /* :124                           */
                CQ_REC_CCX(b0, a1, a2),    /* :125 the RELOCATED Toffoli     */
                CQ_REC_CX (b1, b0),        /* :130 first UMA, Toffoli dropped*/
                CQ_REC_CX (b0, a1),        /* :131                           */
                CQ_REC_CCX(x, a0, b0),     /* :141 last UMA                  */
                CQ_REC_CX (b0, x ),        /* :142                           */
                CQ_REC_CX (x,  a0)         /* :143 the structural no-op      */
            };
            CHECK(cq_mock_matches(&m, want, 13u));
        }
        cq_ctx_dispose(&r.ctx);
    }

    /* --- W = 4: both middle loops run EXACTLY ONCE. This is the first width
     * where the ascending MAJ chain and the descending UMA chain are
     * distinguishable, and the smallest counter-example to a transcription that
     * runs phase 3 upwards. Nineteen gates. ------------------------------ */
    {
        k8_record(&r, &m, 4, 9u, 7u);
        {
            uint32_t a0 = qi(&r.ctx, r.h_acc, 0), a1 = qi(&r.ctx, r.h_acc, 1);
            uint32_t a2 = qi(&r.ctx, r.h_acc, 2), a3 = qi(&r.ctx, r.h_acc, 3);
            uint32_t b0 = qi(&r.ctx, r.h_b, 0), b1 = qi(&r.ctx, r.h_b, 1);
            uint32_t b2 = qi(&r.ctx, r.h_b, 2), b3 = qi(&r.ctx, r.h_b, 3);
            uint32_t x  = qi(&r.ctx, r.h_x, 0);
            const cq_rec want[] = {
                CQ_REC_CX (b0, a0), CQ_REC_CX (b0, x ), CQ_REC_CCX(x, a0, b0),
                CQ_REC_CX (b1, a1), CQ_REC_CX (b1, b0), CQ_REC_CCX(b0, a1, b1),
                CQ_REC_CX (b2, a2), CQ_REC_CX (b2, b1),
                CQ_REC_CX (b3, a3), CQ_REC_CX (b2, a3), CQ_REC_CCX(b1, a2, a3),
                CQ_REC_CX (b2, b1), CQ_REC_CX (b1, a2),
                CQ_REC_CCX(b0, a1, b1), CQ_REC_CX (b1, b0), CQ_REC_CX (b0, a1),
                CQ_REC_CCX(x, a0, b0), CQ_REC_CX (b0, x ), CQ_REC_CX (x,  a0)
            };
            CHECK(cq_mock_matches(&m, want, 19u));
        }
        cq_ctx_dispose(&r.ctx);
    }

    cq_mock_dispose(&m);
}

/* i6_ok = false, MADE FALSIFIABLE RATHER THAN ASSERTED IN A COMMENT.
 *
 * The module header, K08.md §2 and K11.md §2b all rest on one claim: at W >= 3
 * K8 writes its own ADDEND, which is why `b` is non-const and why the Debug
 * scratch-extent check is load-bearing here. Every value-level assertion in
 * this suite is blind to it — `b` is restored, so an implementation that never
 * touched it would pass all of them. This watches `b` after every single step.
 *
 * It also pins the other half, which is the part a reader disbelieves: at W = 2
 * the addend is READ-ONLY, because §3.5 removed the only two gates that would
 * have targeted it. A construction that used the general path at W = 2 without
 * eliding blocks A and G would fail this and nothing else.
 */
CQ_TEST(the_addend_really_is_written_during_the_construction)
{
    static const int WS[] = { 2, 3, 4, 8 };

    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
        int W = WS[j], touched = 0;
        k8_fix f;
        cq_ref_w vb = cq_ref_w_make(0xB5u, 0u, W);

        k8_open(&f, W, cq_ref_w_make(0x6Au, 0u, W), vb);

        for (int u = 0; u < cq_addacc_steps(W); u++) {
            cq_addacc_step(&f.ctx, &f.k, u);
            if (!cq_ref_w_eq(cq_pc_value_w(&f.ctx, f.h_b), vb)) touched = 1;
        }

        if (W == 2)
            CHECK_EQ(touched, 0);       /* §3.5 left nothing targeting b   */
        else
            CHECK_EQ(touched, 1);       /* the carry chain lives in b      */

        /* Restored either way — that is the contract, and it is what makes an
         * addend-targeting construction usable at all. */
        CHECK(cq_ref_w_eq(cq_pc_value_w(&f.ctx, f.h_b), vb));
        k8_close(&f);
    }
}

/* THE LAST GATE, AND THE TWO K08.md §4 CLAIMS IT REFUTES.
 *
 * `CX(x -> acc[0])` closes the construction with `x` already returned to zero
 * by the CNOT before it, so it is a structural no-op at every W. K08.md §4
 * (lines 336-369) draws two conclusions from that and both are wrong against
 * this tree, which is why they are executed here rather than argued:
 *
 *   (i)  "the two-bit shadow will NOT say x is |0>; poison is sticky". It says
 *        so exactly. cq_shadow_cx poisons a target only from the CONTROL's own
 *        `unknown` bit (src/shadow.c), and `unknown` has ONE writer,
 *        cq_shadow_rotate, which has no caller in src/ before M22 lands at Step
 *        19. On the rotation-free surface the shadow tracks x precisely — as
 *        tests/support/poolcheck.h says in as many words.
 *   (ii) "the gate survives into 6W-5 only BECAUSE the shadow says x is
 *        unknown; a shadow refinement would drop the golden to 6W-6". The fold
 *        table reads KIND, never shadow (D6 no-demotion, src/emit.h) — a
 *        CQ_BIT_Q control with a known-zero shadow is not folded away, and this
 *        run proves it, since the shadow IS known-zero here and the gate is
 *        emitted anyway. The golden is contingent on x's KIND, i.e. on I6(b)
 *        pre-materialisation — a different and much more consequential
 *        dependency.
 *
 * Applying K08.md §4's stated correction today would make a correct golden
 * wrong. That is the Step 14 K10.md §3.2 finding in a new place, and the reason
 * this case exists at all.
 */
CQ_TEST(the_last_gate_is_a_no_op_that_is_emitted_anyway)
{
    static const int WS[] = { 2, 3, 4, 8, 16 };

    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
        int W = WS[j], n = cq_addacc_steps(W);
        k8_fix f;
        cq_ref_w va = cq_ref_w_make(0x37u, 0u, W);
        cq_ref_w vb = cq_ref_w_make(0x9Eu, 0u, W);
        cq_ref_w want = cq_ref_w_add(va, vb, W);
        uint32_t qx;
        uint64_t before;

        k8_open(&f, W, va, vb);
        qx = qi(&f.ctx, f.h_x, 0);

        for (int u = 0; u < n - 1; u++) cq_addacc_step(&f.ctx, &f.k, u);

        /* (i) the shadow proves it, and (ii) the sum is already complete. */
        CHECK(cq_shadow_known_zero(&f.ctx.shadow, qx));
        CHECK(cq_ref_w_eq(cq_pc_value_w(&f.ctx, f.h_acc), want));

        before = f.cnt.cx;
        cq_addacc_step(&f.ctx, &f.k, n - 1);

        /* Emitted regardless — the fold table never read that shadow. */
        CHECK_EQ(f.cnt.cx, before + 1u);
        CHECK(cq_ref_w_eq(cq_pc_value_w(&f.ctx, f.h_acc), want));
        CHECK(cq_shadow_known_zero(&f.ctx.shadow, qx));

        k8_close(&f);
    }
}

/* THE ANCILLA IS RECLAIMABLE AFTER A BARE K8 CALL, and this case exists because
 * K08.md §4 says it is not.
 *
 * The document's framing is that M15 has "exactly two honest options" — leak a
 * qubit per call, or write a hand-justified shadow override — and that the
 * dilemma dissolves only because K11 owns the ancilla inside a sandwich, so
 * "there is no standalone K8 call to leak from". At Step 15 there is exactly
 * one: this suite's. And the dilemma is false anyway. cq_reg_free demands a
 * per-qubit proof, and on the rotation-free surface the shadow IS one —
 * cq_shadow_rotate is the only producer of `unknown` and has no caller in src/
 * before M22 (Step 19), so cq_shadow_known_zero is exact rather than
 * conservative there (tests/support/poolcheck.h). The construction really does
 * clear x, and the library really can see it.
 *
 * WHAT THIS DOES NOT CLAIM. The scope is the predicate's name: rotation-free.
 * NARROWED AT STEP 19 BY PRD §15 D12 — it is not "any rotation upstream" but a
 * GENERAL Ry: every Rz is diagonal, and so is the `Z` of the θ≡π row, so a rail
 * that met only those keeps a determinate shadow and stays freeable. What does
 * hard-error on a genuinely clean rail is a rail a general Ry has touched, which
 * is bd ckd.18's shape and is why the proof lives in tests/ and must never move
 * into src/. So K08.md §4 is not wrong in general; it is wrong for Steps 10-17,
 * which is where M15 ships, and it stays wrong for any caller that only folds. */
CQ_TEST(the_ancilla_can_be_freed_after_a_bare_call)
{
    static const int WS[] = { 2, 3, 4, 8, 16 };

    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
        int W = WS[j];
        k8_fix f;
        uint32_t qx;

        k8_open(&f, W, cq_ref_w_make(0x2Du, 0u, W), cq_ref_w_make(0xC1u, 0u, W));
        qx = qi(&f.ctx, f.h_x, 0);

        cq_kernel_addacc(&f.ctx, &f.k);

        /* Named BEFORE the free — afterwards the rail is a tombstone. */
        cq_reg_free(&f.ctx, f.h_x, cq_pc_zero_proof_rotation_free);
        CHECK(cq_pc_indices_are_free(&f.ctx, &qx, 1u));

        k8_close(&f);
    }
}
