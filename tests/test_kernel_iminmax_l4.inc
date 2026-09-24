/* tests/test_kernel_iminmax_l4.inc — L4: the goldens, the closed forms of
 * K27.md §3, and the scratch high-water mark. Included by
 * test_kernel_iminmax.c. The goldens are the only thing here that reads a
 * file; their no-golden sibling is the composition identity in the slots .inc.
 */

/* K27.md §3, at the all-quantum mask. Every term is a block's measured tuple:
 *   ult (W+1, 3W, 2W)   slt (W+3, 5W, 2W)   mux (0, 3W, W)
 *   sub(0, a) (W+1, 3W, W-1) and slt(a, 0) (W+3, 4W, 2W) — the constant 0
 *   folds W CNOTs of each (and the W-1 `a`-controlled Toffolis of sub).
 * Sandwich = 2 * compute + W copy-out CNOTs. */
static void im_closed_form(int op, int W, uint64_t *x, uint64_t *cx,
                           uint64_t *ccx, uint32_t *region)
{
    uint64_t w = (uint64_t)W;

    switch (op) {
    case CQ_IMM_UMAX: case CQ_IMM_UMIN:
        *x = 2u * w + 2u;  *cx = 13u * w;  *ccx = 6u * w;
        *region = (uint32_t)(5 * W + 1);
        break;
    case CQ_IMM_SMAX: case CQ_IMM_SMIN:
        *x = 2u * w + 6u;  *cx = 17u * w;  *ccx = 6u * w;
        *region = (uint32_t)(7 * W + 1);
        break;
    default:
        *x = 4u * w + 8u;  *cx = 21u * w;  *ccx = 8u * w - 2u;
        *region = (uint32_t)(10 * W + 1);
        break;
    }
}

CQ_TEST(l4_goldens)
{
    cq_gold g;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/iminmax.counts",
                      "M42 kernels/iminmax.c — K27 abs/smin/smax/umin/umax "
                      "(icmp + select, abs adds a sub; one sandwich)",
                      "ALL-QUANTUM on every source (abs has one); abs's "
                      "constant 0 is a CQ_BIT_ZERO source and folds at every "
                      "mask; at ctrl_depth 0",
                      CQOPS_BENNETT_COMMIT))
        return;

    for (int op = 0; op < CQ_IMM_N_OP; op++)
        for (int w = 0; w < IM_NWS; w++) {
            cq_counter fwd, unc;
            uint64_t x, cx, ccx;
            uint32_t region;
            int W = IM_WS[w];

            im_closed_form(op, W, &x, &cx, &ccx, &region);
            cq_kd_measure(IM_SPECS[op], W, &fwd, &unc);

            CHECK_GATES(fwd.x, fwd.cx, fwd.ccx, x, cx, ccx);
            CHECK_GATES(unc.x, unc.cx, unc.ccx, x, cx, ccx);
            CHECK_EQ(fwd.ry + fwd.rz + fwd.mz, 0);
            CHECK_EQ(unc.ry + unc.rz + unc.mz, 0);
            CHECK_EQ(cq_imm_region(op, W), region);

            cq_gold_check(&g, IM_SPECS[op]->name, "forward", W,
                          fwd.x, fwd.cx, fwd.ccx);
            cq_gold_check(&g, IM_SPECS[op]->name, "unc", W,
                          unc.x, unc.cx, unc.ccx);
        }

    CHECK(cq_gold_close(&g));
}

/* K27.md §3.2's evaluated table, as LITERALS: the closed form above generates
 * the goldens' expectations, so on its own it would pin whatever it computes.
 * These are the numbers the DOCUMENT claims, at the ABI widths. */
CQ_TEST(the_closed_forms_in_k27_match_what_is_emitted)
{
    static const struct { int op, W; uint64_t x, cx, ccx; } ROWS[] = {
        { CQ_IMM_UMAX,  8,  18u,  104u,  48u }, { CQ_IMM_UMIN, 64, 130u,  832u, 384u },
        { CQ_IMM_SMAX, 16,  38u,  272u,  96u }, { CQ_IMM_SMIN, 32,  70u,  544u, 192u },
        { CQ_IMM_ABS,   8,  40u,  168u,  62u }, { CQ_IMM_ABS,  64, 264u, 1344u, 510u },
        { CQ_IMM_ABS,   1,  12u,   21u,   6u }
    };

    for (size_t i = 0; i < sizeof ROWS / sizeof ROWS[0]; i++) {
        cq_counter fwd, unc;
        uint64_t x, cx, ccx;
        uint32_t region;

        im_closed_form(ROWS[i].op, ROWS[i].W, &x, &cx, &ccx, &region);
        CHECK_GATES(x, cx, ccx, ROWS[i].x, ROWS[i].cx, ROWS[i].ccx);
        cq_kd_measure(IM_SPECS[ROWS[i].op], ROWS[i].W, &fwd, &unc);
        CHECK_GATES(fwd.x, fwd.cx, fwd.ccx, ROWS[i].x, ROWS[i].cx, ROWS[i].ccx);
        CHECK_GATES(unc.x, unc.cx, unc.ccx, ROWS[i].x, ROWS[i].cx, ROWS[i].ccx);
    }
}

/* dst owns W qubits after the call; the high-water mark is W + the whole
 * region (I6(b) pre-materialises it). L2 looks AFTER the call and cannot see
 * scratch that was taken and tidily released — this is the only instrument
 * for the region's size. */
CQ_TEST(dst_owns_w_qubits_and_the_scratch_comes_back)
{
    for (int op = 0; op < CQ_IMM_N_OP; op++)
        for (int w = 0; w < IM_NWS; w++) {
            uint32_t peak = 0u;
            int W = IM_WS[w];
            uint32_t owned = cq_kd_peak(IM_SPECS[op], W, &peak);

            CHECK_EQ(owned, (uint32_t)W);
            CHECK_EQ(peak, (uint32_t)W + cq_imm_region(op, W));
        }
}
