/* tests/test_kernel_cmp_derivation.inc — M16, Step 13. Included by
 * tests/test_kernel_cmp.c; split on the same boundary M16 itself splits on,
 * PRIMITIVES ↔ PREDICATE DERIVATION (plan §3's recorded seam for this module).
 *
 * Everything here is about the seven derived predicates and about what the
 * operand bit-kinds do to the count — not about whether the comparator
 * computes the right bit, which the sweep next door owns. An .inc rather than
 * a second binary because Rule 12 counts .c and .h, and because these cases
 * share the ROWS table and the shape function with the file that includes
 * them; a second suite would need a second copy of both, which is the one
 * thing the table exists to prevent.
 */

/* Runs one kernel into `m` inside an EXISTING context, so two runs share the
 * pool and therefore the qubit indices. Two separately-built contexts would
 * hand `b` different indices in a swapped call, and every record would then
 * differ for a reason that has nothing to do with the construction. */
static void record_into(cq_ctx *ctx, cq_mock *m, cq_kernel_fn k, cq_bit *dst,
                        const cq_bit *a, const cq_bit *b, int W)
{
    cq_sink s = cq_mock_sink(m);
    const cq_sink *save = ctx->sink;

    cq_mock_reset(m);
    ctx->sink = &s;
    k(ctx, dst, a, b, W);
    ctx->sink = save;
}

static cq_kernel_fn primitive_fn(int prim)
{
    if (prim == P_EQ)  return cq_kernel_eq;
    if (prim == P_ULT) return cq_kernel_ult;
    return cq_kernel_slt;
}

/* Gate-for-gate over the first `n`, refusing rather than reading off the end
 * if either stream is short — cq_mock_at aborts on an out-of-range index, and
 * an abort would report as a crashed suite rather than as a failed compare. */
static void streams_agree(const cq_mock *x, const cq_mock *y, size_t n,
                          const char *what)
{
    if (cq_mock_count(x) < n || cq_mock_count(y) < n) {
        cq_h_fail(__FILE__, __LINE__,
                  "%s: stream too short — %zu and %zu gates, need %zu", what,
                  cq_mock_count(x), cq_mock_count(y), n);
        return;
    }

    for (size_t i = 0; i < n; i++) {
        const cq_rec *p = cq_mock_at(x, i), *q = cq_mock_at(y, i);

        if (p->op != q->op || p->a != q->a || p->b != q->b || p->t != q->t) {
            cq_h_fail(__FILE__, __LINE__,
                      "%s: gate %zu differs — (op %d, %u, %u -> %u) vs "
                      "(op %d, %u, %u -> %u)", what, i, (int)p->op, p->a,
                      p->b, p->t, (int)q->op, q->a, q->b, q->t);
            return;
        }
    }
}

/* THE DERIVATION, ASSERTED ON THE GATE STREAM RATHER THAN ON A VALUE. Each of
 * the seven derived predicates is its primitive, called with the operands in
 * the order `lower_icmp!` gives (arith.jl:409-418), plus at most one X in the
 * copy-out. So the two compute halves must be the SAME GATES ON THE SAME
 * QUBITS, gate for gate — a claim a value comparison cannot make, and one a
 * gate count cannot make either, since every predicate is within one X of its
 * sibling and four of them are exactly equal to it.
 *
 * Both runs happen in ONE context on ONE pair of operand registers, and the
 * scratch region goes back to the LIFO free list between them (D4), so the
 * second sandwich acquires the identical indices. The copy-out is deliberately
 * NOT compared: it targets a different `dst` in each run, which is the one
 * place the two legitimately differ. Its LENGTH is compared instead, and that
 * is where `ne`, `ule`, `uge`, `sle` and `sge` part company with their
 * primitives — one gate, the whole of K09.md §5 delta 2.
 *
 * The three primitives compare against themselves here. That is a control, not
 * a waste: it is the row where `got` and `want` must agree in the copy-out
 * length too, so a mistake in this case's own bookkeeping shows up.
 *
 * IT IS THE SOLE DETECTOR FOR A REAL CLASS OF DRIFT, and that was MEASURED
 * rather than argued (Step 13). Give `cq_kernel_ne` the operand swap — `SWAP`
 * instead of `NO_SWAP`, one token — and the kernel is still CORRECT: `a != b`
 * is symmetric, so every value is right, the tuple is identical, the palindrome
 * holds, scratch comes back and the pool is restored. Eleven of the twelve
 * cases in this suite stay green. Only this one goes red, because `ne`'s Phase
 * A now emits `CX(b, diff)` before `CX(a, diff)` and `eq`'s does not — i.e. the
 * module has quietly stopped transcribing `lower_icmp!` and nothing else in
 * the project can tell. That is the shape the 20-mutant battery could not
 * produce, since every mutant it had also moved a value and L1 got there
 * first. */
CQ_TEST(each_derived_predicate_is_its_primitives_stream)
{
    static const int WS[] = { 1, 2, 3, 8 };
    cq_mock got, want;

    cq_mock_init(&got);
    cq_mock_init(&want);

    for (int i = 0; i < N_ROWS; i++)
        for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
            const cmp_row *r = &ROWS[i];
            int W = WS[j];
            size_t head = (size_t)n_compute_of(r->prim, W);
            cq_ctx ctx;
            cq_sink boot = cq_mock_sink(&want);

            cq_ctx_init(&ctx, &boot);

            cq_ref_w all = cq_ref_w_ones(W);
            int32_t ha = cq_bk_reg_w(&ctx, (uint32_t)W,
                                     cq_ref_w_make(0x5Au, 0u, W), all);
            int32_t hb = cq_bk_reg_w(&ctx, (uint32_t)W,
                                     cq_ref_w_make(0xA3u, 0u, W), all);
            int32_t h1 = cq_reg_alloc_zero(&ctx.regs, 1u);
            int32_t h2 = cq_reg_alloc_zero(&ctx.regs, 1u);
            const cq_bit *a = cq_reg_cbits(&ctx.regs, ha);
            const cq_bit *b = cq_reg_cbits(&ctx.regs, hb);

            record_into(&ctx, &got, r->spec.kernel,
                        cq_reg_bits(&ctx.regs, h1), a, b, W);

            /* The primitive, with the operands in this row's order. All three
             * primitives invert, so their copy-out is always 2 gates. */
            record_into(&ctx, &want, primitive_fn(r->prim),
                        cq_reg_bits(&ctx.regs, h2),
                        r->swap ? b : a, r->swap ? a : b, W);

            CHECK_EQ(cq_mock_count(&got),  head * 2u + (r->invert ? 2u : 1u));
            CHECK_EQ(cq_mock_count(&want), head * 2u + 2u);
            streams_agree(&got, &want, head, r->spec.name);

            cq_ctx_dispose(&ctx);
        }

    cq_mock_dispose(&got);
    cq_mock_dispose(&want);
}

/* ---- What the operand bit-kinds are still allowed to do. ---------------- */

static uint64_t base_total(const cmp_row *r, int W)
{
    uint64_t x, cx, ccx;

    closed_form(r, W, &x, &cx, &ccx);
    return x + cx + ccx;
}

static int zeros_of(cq_ref_w v, cq_ref_w q, int W)
{
    int n = 0;

    for (int i = 0; i < W; i++)
        if (!cq_ref_w_bit(q, i) && !cq_ref_w_bit(v, i)) n++;
    return n;
}

/* K09.md §3.3.1, and the ⚠ that corrects it. I6(b) puts SCRATCH out of the
 * fold's reach but leaves the OPERANDS in it, so a classical operand bit still
 * folds its own step away — and only a classical ZERO does. A classical ONE
 * turns `CX(c,t)` into `X(t)` and `CCX(ONE,c,t)` into `CX(c,t)`: one gate
 * either way, total unchanged. "Partially classical is strictly cheaper" was
 * written into that document three times before an adversarial pass caught it,
 * and an L5 assertion phrased with `<` would fail on `icmp eq i8 %x, -1`.
 *
 * The coefficient is 4 on whichever operand reaches `lower_ult!` as its `a`:
 * that one is read TWICE per compute half, at Phase C j==0 and j==2. It is 2
 * everywhere else — `eq` and `slt` read both operands exactly once each, in
 * Phase A, and `ult` reads its `b` once. The sandwich doubles both. */
static uint64_t folded_total(const cmp_row *r, int W, int za, int zb)
{
    int first  = r->swap ? zb : za;    /* the one arriving as the primitive's a */
    int second = r->swap ? za : zb;
    int coeff  = (r->prim == P_ULT) ? 4 : 2;

    return base_total(r, W) - (uint64_t)(coeff * first + 2 * second);
}

/* One call at an explicit (value, mask) pair per operand. Returns the gate
 * total; `*minted` is how many qubits the call took from the pool, which is
 * the only thing that can see a sandwich that was entered and tidied away. */
static uint64_t total_of(const cmp_row *r, int W, cq_ref_w va, cq_ref_w qa,
                         cq_ref_w vb, cq_ref_w qb, uint32_t *minted)
{
    cq_ctx ctx;
    cq_counter cnt;
    cq_sink sink;
    cq_kd_shape sh;

    cmp_shape(W, &sh);
    va = cq_ref_w_make(va.lo, va.hi, W);
    vb = cq_ref_w_make(vb.lo, vb.hi, W);

    cq_count_reset(&cnt);
    sink = cq_sink_counter(&cnt);
    cq_ctx_init(&ctx, &sink);

    int32_t ha = cq_bk_reg_w(&ctx, (uint32_t)W, va, qa);
    int32_t hb = cq_bk_reg_w(&ctx, (uint32_t)W, vb, qb);
    int32_t hd = cq_reg_alloc_zero(&ctx.regs, 1u);
    const cq_bit *src[2] = { cq_reg_cbits(&ctx.regs, ha),
                             cq_reg_cbits(&ctx.regs, hb) };

    cq_pc_snap before = cq_pc_take(&ctx);

    /* Building the operands materialises their set bits, which emits X. Reset
     * after that, so what is counted is the KERNEL. */
    cq_count_reset(&cnt);
    r->spec.call(&ctx, cq_reg_bits(&ctx.regs, hd), src, &sh);

    /* L1 on the way past: a count case that never looks at the answer pins the
     * gate profile of whatever circuit happens to be there. */
    CHECK_EQ(cq_pc_value(&ctx, hd), (uint64_t)cq_ref_icmp(r->pred, va, vb, W));

    if (minted) *minted = cq_pc_take(&ctx).minted - before.minted;

    cq_ctx_dispose(&ctx);
    return cq_count_total(&cnt);
}

CQ_TEST(a_classical_zero_operand_bit_folds_by_k09s_own_formula)
{
    /* Four (value, mask) shapes per operand at W=4: all-quantum, all-classical
     * ZERO, all-classical ONE — the row that removes nothing — and a mixed
     * register whose classical half is all ONEs. Crossed both ways, minus the
     * fully-classical corner, which short-circuits before the sandwich and is
     * L5's case rather than this one's. */
    enum { CW = 4, N = 4 };
    static const uint64_t VAL[N]  = { 0xFu, 0x0u, 0xFu, 0x3u };
    static const uint64_t MASK[N] = { 0xFu, 0x0u, 0x0u, 0xCu };

    for (int i = 0; i < N_ROWS; i++)
        for (int p = 0; p < N; p++)
            for (int q = 0; q < N; q++) {
                cq_ref_w va = cq_ref_w_make(VAL[p],  0u, CW);
                cq_ref_w qa = cq_ref_w_make(MASK[p], 0u, CW);
                cq_ref_w vb = cq_ref_w_make(VAL[q],  0u, CW);
                cq_ref_w qb = cq_ref_w_make(MASK[q], 0u, CW);
                uint64_t got, wanted;

                if (MASK[p] == 0u && MASK[q] == 0u) continue;

                got    = total_of(&ROWS[i], CW, va, qa, vb, qb, NULL);
                wanted = folded_total(&ROWS[i], CW, zeros_of(va, qa, CW),
                                      zeros_of(vb, qb, CW));

                if (got != wanted)
                    cq_h_fail(__FILE__, __LINE__,
                              "%s W=%d a=(v %llx, q %llx) b=(v %llx, q %llx): "
                              "%llu gates, K09.md §3.3.1 says %llu",
                              ROWS[i].spec.name, CW,
                              (unsigned long long)VAL[p],
                              (unsigned long long)MASK[p],
                              (unsigned long long)VAL[q],
                              (unsigned long long)MASK[q],
                              (unsigned long long)got,
                              (unsigned long long)wanted);
            }
}

/* ---- R9: the all-classical short-circuit. ------------------------------- */

/* Not an optimisation. Pre-materialisation is UNCONDITIONAL, so a compare that
 * entered the sandwich here would take 2W-1, 3W+1 or 5W+1 qubits for an
 * operation with no quantum input at all, and L5's "zero gates and zero
 * qubits" would be false (plan §0.2 consequence 2, risk R9). `minted` is the
 * instrument and `live` is not: a sandwich that took its scratch and gave it
 * back leaves `live` exactly where it found it.
 *
 * The expected bits are LITERALS, not cq_ref_icmp's answer, so this case is a
 * third independent statement of the derivation table. 200 and 55 at i8 is the
 * pair worth hand-checking: unsigned 200 > 55, but signed -56 < 55, so the two
 * orderings disagree on every one of the six inequality predicates. */
CQ_TEST(r9_all_classical_operands_never_enter_the_sandwich)
{
    /*                                    eq ne ult ugt ule uge slt sgt sle sge */
    static const uint64_t W_200_55 [N_ROWS] = { 0, 1,  0,  1,  0,  1,  1,  0,  1, 0 };
    static const uint64_t W_200_200[N_ROWS] = { 1, 0,  0,  0,  1,  1,  0,  0,  1, 1 };
    cq_ref_w zero = cq_ref_w_zero();
    cq_ref_w big  = cq_ref_w_make(200u, 0u, 8);
    cq_ref_w mid  = cq_ref_w_make(55u,  0u, 8);

    for (int i = 0; i < N_ROWS; i++) {
        uint32_t minted = 1u;

        CHECK_EQ(total_of(&ROWS[i], 8, big, zero, mid, zero, &minted), 0);
        CHECK_EQ(minted, 0);
        CHECK_EQ(cq_ref_icmp(ROWS[i].pred, big, mid, 8), W_200_55[i]);

        minted = 1u;
        CHECK_EQ(total_of(&ROWS[i], 8, big, zero, big, zero, &minted), 0);
        CHECK_EQ(minted, 0);
        CHECK_EQ(cq_ref_icmp(ROWS[i].pred, big, big, 8), W_200_200[i]);
    }
}

/* THE OTHER SIDE OF THE SAME FOLD, AND NO SWEEP CASE REACHES IT. The driver
 * always mints `dst` all-BIT_ZERO, so every all-classical case above lands on
 * cq_bit_flip_const and emits nothing. With `dst` already on a qubit the same
 * path must emit a REAL X when the predicate is true — which is what makes the
 * short-circuit a correct implementation of `dst[0] ^= f(a,b)` rather than
 * merely a cheap one. Delete that emit and only this case goes red. */
CQ_TEST(the_classical_fold_writes_a_real_gate_into_a_quantum_dst)
{
    cq_ref_w big = cq_ref_w_make(200u, 0u, 8);
    cq_ref_w mid = cq_ref_w_make(55u,  0u, 8);

    for (int i = 0; i < N_ROWS; i++) {
        cq_ctx ctx;
        cq_counter cnt;
        cq_sink sink;
        cq_kd_shape sh;
        uint64_t want = (uint64_t)cq_ref_icmp(ROWS[i].pred, big, mid, 8);

        cmp_shape(8, &sh);
        cq_count_reset(&cnt);
        sink = cq_sink_counter(&cnt);
        cq_ctx_init(&ctx, &sink);

        int32_t ha = cq_bk_reg_w(&ctx, 8u, big, cq_ref_w_zero());
        int32_t hb = cq_bk_reg_w(&ctx, 8u, mid, cq_ref_w_zero());
        /* |0> on one qubit — a flag rail a previous compare could well have
         * left, and exactly the shape Step 21's `_inv` axis will hand us. */
        int32_t hd = cq_bk_reg_w(&ctx, 1u, cq_ref_w_zero(), cq_ref_w_ones(1));
        const cq_bit *src[2] = { cq_reg_cbits(&ctx.regs, ha),
                                 cq_reg_cbits(&ctx.regs, hb) };

        cq_count_reset(&cnt);
        ROWS[i].spec.call(&ctx, cq_reg_bits(&ctx.regs, hd), src, &sh);

        CHECK_EQ(cq_pc_value(&ctx, hd), want);
        CHECK_GATES(cnt.x, cnt.cx, cnt.ccx, want, 0, 0);

        cq_ctx_dispose(&ctx);
    }
}

/* ---- A third model, sharing nothing with either of the first two. ------- */

/* cq_ref_icmp branches on sign bits; the kernel biases both operands by
 * 2^(W-1) and runs the unsigned comparator. Neither of those is plain C `<` on
 * a sign-extended 64-bit integer, which is what this crosses them against —
 * through cq_ref_sext, whose own arithmetic is the shift-free
 * `(t ^ sign) - sign` identity, with no bit index and no loop. Three models,
 * three derivations of the same order.
 *
 * FALSIFIABLE, MEASURED: rewriting cq_ref_icmp's `ule` row as `!w_ult(a, b)`
 * turns this case red — and five others with it, including
 * r9_all_classical_operands_never_enter_the_sandwich, whose expectations are
 * hand-written literals. So the three models genuinely cross-check; none of
 * them is decoration, and no two of them share a derivation.
 *
 * Bounded at W <= 64 for the obvious reason, and that costs nothing on the
 * signed surface: above 64 `icmp` ships only at i80, whose entire role is
 * x86_fp80 bit-pattern extraction (opcode_table.yaml:133-135 — i80 carries
 * "EXACTLY the extraction ops (and / icmp / lshr ...)", and i128 declines icmp
 * outright at :124). The two-word path there is exercised by the sweep,
 * against cq_ref_icmp. */
CQ_TEST(the_signed_oracle_agrees_with_plain_c_on_sign_extended_values)
{
    static const int WS[] = { 1, 2, 3, 8, 16, 32, 64 };
    cq_bk_rng rng;

    cq_bk_rng_init(&rng, 0xC0FFEE1Cull);

    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
        int W = WS[j];
        uint64_t m = cq_ref_mask(W);

        for (int s = 0; s < 256; s++) {
            /* The four corners first: 0 and all-ones are where a masking or a
             * sign-extension error lives, and a uniform sample reaches them
             * with probability ~0 at W=64. */
            uint64_t ua = (s < 4) ? ((s & 1) ? m : 0u) : (cq_bk_rng_next(&rng) & m);
            uint64_t ub = (s < 4) ? ((s & 2) ? m : 0u) : (cq_bk_rng_next(&rng) & m);
            cq_ref_w a = cq_ref_w_make(ua, 0u, W);
            cq_ref_w b = cq_ref_w_make(ub, 0u, W);
            int64_t sa = cq_ref_sext(ua, W), sb = cq_ref_sext(ub, W);

            CHECK_EQ(cq_ref_icmp(CQ_ICMP_EQ,  a, b, W), ua == ub);
            CHECK_EQ(cq_ref_icmp(CQ_ICMP_NE,  a, b, W), ua != ub);
            CHECK_EQ(cq_ref_icmp(CQ_ICMP_ULT, a, b, W), ua <  ub);
            CHECK_EQ(cq_ref_icmp(CQ_ICMP_UGT, a, b, W), ua >  ub);
            CHECK_EQ(cq_ref_icmp(CQ_ICMP_ULE, a, b, W), ua <= ub);
            CHECK_EQ(cq_ref_icmp(CQ_ICMP_UGE, a, b, W), ua >= ub);
            CHECK_EQ(cq_ref_icmp(CQ_ICMP_SLT, a, b, W), sa <  sb);
            CHECK_EQ(cq_ref_icmp(CQ_ICMP_SGT, a, b, W), sa >  sb);
            CHECK_EQ(cq_ref_icmp(CQ_ICMP_SLE, a, b, W), sa <= sb);
            CHECK_EQ(cq_ref_icmp(CQ_ICMP_SGE, a, b, W), sa >= sb);
        }
    }
}
