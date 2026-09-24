/* tests/test_template_bitcast_cert.inc — the RECORD, D15's certificate and
 * D14's `_inv` for `bitcast f64 <-> i64` at the ABI edge. Part of
 * tests/test_template_bitcast.c.
 *
 * EVERY FREE-TIME CASE POISONS ITS SOURCE FIRST, through the ABI
 * (`cqrt_alloc_*` for the record history, a general `cqrt_ry_*` for the
 * poison), and carries the negative control that the shadow really has lost
 * the result — on the rotation-free surface the shadow answers first and the
 * certificate is never asked (bd memories
 * the-shadow-masks-every-certificate-defect-off-the-rotation-taint,
 * a-test-minted-rail-has-no-certificate-history). */

/* THE RECORD ITSELF, because a strand count cannot see a MISSING record: an
 * unrecorded write reduces vacuously and RELEASES (bd memory
 * a-dropped-template-record-is-seen-only-by-a-record-walk), so the poisoned
 * round trip below would pass with the record dropped. This walks the call's
 * own slice of the stream: exactly ONE template write, naming
 * `{out, a, NONE, NONE}`, uncontrolled. */
static uint32_t bc_one_write(uint32_t base, uint16_t op, int32_t out, int32_t a)
{
    uint32_t n = 0u, tag = 0u;

    for (uint32_t p = base; p < cq_rec_len(); p++) {
        const cq_call_rec *c = cq_rec_at(p);

        if (c->op != (uint16_t)CQ_ROP_TPL_FWD && c->op != (uint16_t)CQ_ROP_TPL_UNC)
            continue;
        CHECK_EQ((int)c->op, (int)op);
        CHECK_EQ((long long)c->h[0], (long long)out);
        CHECK_EQ((long long)c->h[1], (long long)a);
        CHECK_EQ((long long)c->h[2], (long long)CQ_REG_NONE);
        CHECK_EQ((long long)c->h[3], (long long)CQ_REG_NONE);
        CHECK_EQ((long long)c->ctrl, (long long)CQ_REG_NONE);
        tag = c->tag;
        n++;
    }
    CHECK_EQ(n, 1u);
    return tag;
}

/* ...and the tag: the forward and its `_unc` share one; it is in the INTEGER
 * CAST space (`0x80000000`, cq_template_boundary.h), because that is the door
 * bitcast goes through; it is not `zext`'s at any width pair, because bitcast
 * is its own `cq_shim_cast_kind`; and the two DIRECTIONS share it (see
 * the_two_directions_pair_because_they_are_one_function). */
CQ_TEST(a_bitcast_records_one_write_naming_a_under_its_cast_tag)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t af = cqrt_alloc_f64(f64_of(CQ_F64_SNAN));
    const int32_t ai = cqrt_alloc_i64(-2);
    const int32_t a32 = cqrt_alloc_i32(7);
    uint32_t base, t_f2i, t_f2i_u, t_i2f, t_i2f_u, t_zext;
    int32_t o;

    base = cq_rec_len(); o = cq_template_bitcast_f64_to_i64(af);
    t_f2i = bc_one_write(base, (uint16_t)CQ_ROP_TPL_FWD, o, af);
    base = cq_rec_len(); cq_template_bitcast_f64_to_i64_unc(o, af);
    t_f2i_u = bc_one_write(base, (uint16_t)CQ_ROP_TPL_UNC, o, af);
    base = cq_rec_len(); o = cq_template_bitcast_i64_to_f64(ai);
    t_i2f = bc_one_write(base, (uint16_t)CQ_ROP_TPL_FWD, o, ai);
    base = cq_rec_len(); cq_template_bitcast_i64_to_f64_unc(o, ai);
    t_i2f_u = bc_one_write(base, (uint16_t)CQ_ROP_TPL_UNC, o, ai);
    base = cq_rec_len(); o = cq_shim_cast(CQ_SHIM_CAST_ZEXT, 32, 64, a32);
    t_zext = bc_one_write(base, (uint16_t)CQ_ROP_TPL_FWD, o, a32);

    CHECK_EQ(t_f2i, t_f2i_u);                       /* the twins share one  */
    CHECK_EQ(t_i2f, t_i2f_u);
    CHECK_EQ(t_f2i, t_i2f);                         /* one function, one tag */
    CHECK_EQ(t_f2i & 0xF0000000u, 0x80000000u);     /* the integer cast space */
    CHECK(t_f2i != t_zext);
    CHECK_EQ(cq_mock_count(&m), 0);
    cq_reg_audit(ctx);
    close_with(&m);
}

/* One poisoned forward -> `_unc` -> free that must RELEASE. */
static void bc_poisoned(const char *what, int32_t a, int32_t (*fwd)(int32_t),
                        void (*unc)(int32_t, int32_t), int want_release)
{
    cq_ctx *ctx = cq_shim_ctx();
    const uint32_t reports = cq_reg_strand_reports(ctx);
    uint32_t strand_before;
    int32_t out;

    strand_before = cq_qubits_stranded(&ctx->pool);
    out = fwd(a);
    CHECK_EQ(cq_reg_disposition(ctx, out, cq_pc_zero_proof_rotation_free),
             CQ_PROOF_UNPROVEN);
    unc(out, a);
    cqrt_free(out);
    if (want_release && cq_qubits_stranded(&ctx->pool) != strand_before)
        cq_h_fail(__FILE__, __LINE__, "the certificate did not discharge a "
                  "poisoned %s round trip — %u qubit(s) stranded", what,
                  cq_qubits_stranded(&ctx->pool) - strand_before);
    if (!want_release) {
        CHECK(cq_qubits_stranded(&ctx->pool) > strand_before);
        CHECK_EQ(cq_reg_strand_reports(ctx), reports + 1u);
    }
}

static int32_t bc_poison_f64(uint64_t v)
{
    const int32_t a = cqrt_alloc_f64(f64_of(v));

    cqrt_ry_f64(a, 0.5);
    return a;
}

static int32_t bc_poison_i64(uint64_t v)
{
    const int32_t a = cqrt_alloc_i64(i64_of(v));

    cqrt_ry_i64(a, 0.5);
    return a;
}

CQ_TEST(a_bitcast_unc_on_a_poisoned_source_is_discharged_by_the_certificate)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    bc_poisoned("bitcast_f64_to_i64", bc_poison_f64(CQ_F64_SNAN),
                cq_template_bitcast_f64_to_i64,
                cq_template_bitcast_f64_to_i64_unc, 1);
    bc_poisoned("bitcast_i64_to_f64", bc_poison_i64(0x8000000000000001ull),
                cq_template_bitcast_i64_to_f64,
                cq_template_bitcast_i64_to_f64_unc, 1);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0u);
    cq_reg_audit(ctx);
    close_with(&m);
}

/* A DECISION, STATED AS A CASE. `bitcast f64 -> i64` and `i64 -> f64` are the
 * SAME function on 64 bits — `out ^= a` — so one kind and one tag serve both,
 * and a forward in one direction is truthfully undone by an `_unc` in the
 * other: pairing them is D15's soundness condition (the two writes XOR the
 * same value from the same unchanged source), not a coincidence of encoding.
 * If the directions are ever given separate kinds this case goes red, and
 * that is the moment to decide it again. */
CQ_TEST(the_two_directions_pair_because_they_are_one_function)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    bc_poisoned("f2i then i2f_unc", bc_poison_f64(CQ_F64_NEG_ZERO),
                cq_template_bitcast_f64_to_i64,
                cq_template_bitcast_i64_to_f64_unc, 1);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0u);
    cq_reg_audit(ctx);
    close_with(&m);
}

/* THE TAG SPACE IS D15's TWIN IDENTITY (bd memory
 * a-template-familys-tag-space-is-d15s-twin-identity). `fneg f64` records
 * `{out, a, NONE, NONE}` too, uncontrolled, with no literal — so only the tag
 * separates a bitcast forward from an `fneg` `_unc` on the same handles, and
 * their XOR leaves the sign bit set. The free must STRAND and report once. */
CQ_TEST(a_bitcast_forward_does_not_pair_with_an_fneg_unc)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    bc_poisoned("f2i then fneg_unc", bc_poison_f64(CQ_F64_ONE),
                cq_template_bitcast_f64_to_i64, cq_template_fneg_f64_unc, 0);
    cq_reg_audit(ctx);
    close_with(&m);
}

/* BOTH `_inv` SYMBOLS ARE D14's ABORT, AND THE SENTENCE IS THE BUCKET'S. A
 * bitcast's inverse EXISTS (it is the other direction), and D14 refuses it on
 * the ground it refuses every landed family's `_inv`: the ABI offers the fresh
 * rail it would mint no `_inv_unc` twin. Read out of a forked child. */
typedef struct { const char *name; int32_t (*fn)(int32_t); } bc_inv;

static void call_inv(void *p)
{
    (void)((const bc_inv *)p)->fn(0);
}

CQ_TEST(both_bitcast_inv_symbols_abort_naming_themselves_and_d14s_reason)
{
    static const bc_inv I[2] = {
        { "cq_template_bitcast_f64_to_i64_inv", cq_template_bitcast_f64_to_i64_inv },
        { "cq_template_bitcast_i64_to_f64_inv", cq_template_bitcast_i64_to_f64_inv },
    };
    for (size_t i = 0; i < 2; i++) {
        char msg[512], want[512];
        const int aborted = cq_child_capture(CQ_CHILD_STDERR, call_inv,
                                             (void *)&I[i], NULL, 0u,
                                             msg, sizeof msg);

        CHECK(aborted);
        snprintf(want, sizeof want, "cqops: %s not implemented (_inv is "
                 "f-inverse, not f; the data family has no uniform definition "
                 "(PRD 15 D14))\n", I[i].name);
        CHECK_STR_EQ(msg, want);
    }
}
