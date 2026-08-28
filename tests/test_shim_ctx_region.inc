/* tests/test_shim_ctx_region.inc — the second half of Step 23 landing 1 step 3's
 * suite, split on the seam IMPLEMENTATION_PLAN §3 records for shim/cq_shim_ctx.c
 * itself: `the process CONTEXT <-> the BOUNDARY VOCABULARY`.
 *
 * The `.c` keeps the stateful subject — the process-global cq_ctx, its reset,
 * and installation of the built-in sinks. This file carries what an entry point
 * CALLS on its way in and on its way out: PRD §9's one region bracket, with
 * bd d6m fix (a)'s three arms, and PRD §1's cq_shim_unsupported. Both are
 * stateless and they have different callers, which is why the module wants the
 * same cut.
 *
 * A SIZE CUT WOULD HAVE PUT THE FORK HELPER AND THE SCANNER IN DIFFERENT FILES
 * FROM THE CASES THAT USE THEM. The suite reached 410 of 300 on its first run
 * (Rule 12, `make lint` red), and the seam was taken as recorded rather than
 * wherever the line count happened to fall.
 *
 * Included by tests/test_shim_ctx.c. It is out of check_loc.sh's reach because
 * that script's `find` matches `*.c`, `*.h` and `*.py` only (tools/check_loc.sh:83)
 * — an EXTENSION rule, not a translation-unit rule, which is what makes "move
 * the big table into an .inc" a real escape hatch rather than a rename that
 * dodges the guard.
 */

/* -------------------------------------------------------------------------
 * 3. The one §9 region bracket (bd d6m fix (a)).
 * ------------------------------------------------------------------------- */

typedef struct {
    cq_ctx  *ctx;
    cq_bit  *target;
    int      depth_inside;
    int      ran;
} body_env;

/* One promotable gate. cq_emit_x is the sharpest body available: uncontrolled
 * it is one X, under a promoted region it is one CX, and under a skipped one it
 * is nothing — so the three row-0 rows are distinguishable from the STREAM and
 * not only from a depth counter. */
static void emit_one_x(void *p)
{
    body_env *e = (body_env *)p;
    e->depth_inside = cq_ctrl_depth(e->ctx);
    e->ran = 1;
    cq_emit_x(e->ctx, e->target);
}

/* A body that emits nothing, for the cases whose subject is the bracket's
 * refusals rather than what it wraps. */
static void emit_nothing(void *p) { (void)p; }

/* ARM 1 — BALANCE. Every row-0 kind expressible as a control handle, plus the
 * uncontrolled baseline, asserting depth 0 before and after. Depth counts
 * FRAMES rather than promoting frames (src/controlled.c), so 0 -> 1 -> 0 is a
 * real claim for all four and not only for the promoting pair — and the gate
 * stream is asserted alongside it, because depth alone is 1 for SKIP, OFF and
 * PROMOTE alike and so cannot tell the rows apart. Measured: swapping row 0's
 * ZERO and ONE branches leaves every depth assertion green and turns this case
 * red on the stream. */
CQ_TEST(every_control_kind_leaves_the_region_balanced)
{
    static const struct {
        const char *name;
        uint64_t    value, qmask;
        size_t      x, cx;
    } row[] = {
        { "ZERO", 0u, 0u, 0u, 0u },   /* row 0 skips: nothing emitted        */
        { "ONE",  1u, 0u, 1u, 0u },   /* row 0 emits it verbatim: one X      */
        { "Q0",   0u, 1u, 0u, 1u },   /* a wire whose shadow is 0: promoted  */
        { "Q1",   1u, 1u, 0u, 1u }    /* a wire whose shadow is 1: promoted  */
    };

    cq_mock m;
    cq_mock_init(&m);
    cq_sink s = cq_mock_sink(&m);

    for (size_t i = 0; i < sizeof row / sizeof row[0]; i++) {
        fresh();
        cqops_set_sink(&s);
        cq_ctx *ctx = cq_shim_ctx();

        const int32_t ht = cq_bk_reg(ctx, 1u, 0u, 1u);   /* target, already a wire */
        const int32_t hf = flag(ctx, row[i].value, row[i].qmask);

        /* A DECOY WIRE, MINTED LAST AND OTHERWISE UNUSED, SO THE ORDERED-STREAM
         * ASSERTION BELOW IS FALSIFIABLE. With only two live indices, every
         * "promote against the wrong wire" provocation lands either on the
         * target — where M06's coincidence refusal aborts one layer down — or
         * on an unminted index, where the shadow does. Either way the mutant
         * dies from a LIBRARY guard and the stream check is never the detector.
         * One spare live index makes `flag + 1` a legal, distinct wire, so the
         * mutant emits `cx(decoy, target)`: identical kinds, identical totals,
         * and only the operands differ. */
        (void)cq_bk_reg(ctx, 1u, 0u, 1u);
        cq_mock_reset(&m);

        body_env e = { ctx, &cq_reg_bits(&ctx->regs, ht)[0], -1, 0 };
        const uint32_t tq  = cq_bit_qindex(cq_reg_cbits(&ctx->regs, ht)[0]);
        const uint32_t fq  = row[i].qmask
                           ? cq_bit_qindex(cq_reg_cbits(&ctx->regs, hf)[0]) : 0u;
        const cq_pc_snap before = cq_pc_take(ctx);

        CHECK_EQ(cq_ctrl_depth(ctx), 0);
        cq_shim_region(hf, emit_one_x, &e);
        CHECK_EQ(cq_ctrl_depth(ctx), 0);

        CHECK_EQ(e.ran, 1);
        CHECK_EQ(e.depth_inside, 1);

        /* THE ORDERED STREAM, NOT A COUNT. Kinds and totals cannot tell an X on
         * the wrong wire from an X on the right one, and PRD §9's promotion is a
         * statement about OPERANDS. */
        const cq_rec skipped[1] = { CQ_REC_X(0u) };
        const cq_rec verbatim[1] = { CQ_REC_X(tq) };
        const cq_rec promoted[1] = { CQ_REC_CX(fq, tq) };
        CHECK(cq_mock_matches(&m, row[i].x ? verbatim
                                 : (row[i].cx ? promoted : skipped),
                              row[i].x + row[i].cx));

        /* AND THE POOL, which is §9 row 0's other half — "0 gates, 0 qubits" —
         * and which no assertion in this suite made until it was asked for. The
         * target is already a wire and cq_emit_x's promotion allocates no
         * ancilla, so `live` must be unchanged across the bracket for all four
         * rows; a bracket leaking one qubit per controlled call would otherwise
         * pass every case in the file. */
        CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    }

    /* The fifth row — "no region at all" — is the ABSENCE of this call, not a
     * value of its parameter: at the ABI it is a different symbol without a
     * ctrl_flag. Asserted so the four above are not read as the whole space. */
    fresh();
    cqops_set_sink(&s);
    cq_ctx *ctx = cq_shim_ctx();
    const int32_t ht = cq_bk_reg(ctx, 1u, 0u, 1u);
    cq_mock_reset(&m);
    CHECK_EQ(cq_ctrl_depth(ctx), 0);
    cq_emit_x(ctx, &cq_reg_bits(&ctx->regs, ht)[0]);
    CHECK_EQ(cq_ctrl_depth(ctx), 0);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_X), 1);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CX), 0);

    fresh();
    cqops_set_sink(NULL);
    cq_mock_dispose(&m);
}

typedef struct { cq_ctx *ctx; cq_bit *c1, *c2, *t; } ccx_env;

static void emit_one_ccx(void *p)
{
    ccx_env *e = (ccx_env *)p;
    cq_emit_ccx(e->ctx, e->c1, e->c2, e->t);
}

/* THE ONE GATE THAT MAKES THE BRACKET ALLOCATE. PRD §9 promotes a Toffoli into
 * three Toffolis over ONE reusable ancilla, acquired lazily at the first
 * promoted CCX and released at the pop — and arm 1's `cq_emit_x` never reaches
 * that path, so without this case the ancilla's whole lifecycle runs under no
 * assertion at all. `live` must return, and the three-Toffoli shape is what
 * says the promotion happened rather than the gate being emitted verbatim. */
CQ_TEST(a_promoted_toffoli_returns_the_regions_shared_ancilla)
{
    cq_mock m;
    cq_mock_init(&m);
    cq_sink s = cq_mock_sink(&m);

    fresh();
    cqops_set_sink(&s);
    cq_ctx *ctx = cq_shim_ctx();

    const int32_t ha = cq_bk_reg(ctx, 1u, 1u, 1u);
    const int32_t hb = cq_bk_reg(ctx, 1u, 1u, 1u);
    const int32_t ht = cq_bk_reg(ctx, 1u, 0u, 1u);
    const int32_t hf = cq_bk_reg(ctx, 1u, 1u, 1u);   /* a promoting flag */

    ccx_env e = { ctx, &cq_reg_bits(&ctx->regs, ha)[0],
                       &cq_reg_bits(&ctx->regs, hb)[0],
                       &cq_reg_bits(&ctx->regs, ht)[0] };

    const cq_pc_snap before = cq_pc_take(ctx);
    cq_mock_reset(&m);

    cq_shim_region(hf, emit_one_ccx, &e);

    CHECK_EQ(cq_ctrl_depth(ctx), 0);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CCX), 3);    /* §9's promotion */
    CHECK_EQ(cq_mock_count(&m), 3);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));      /* the ancilla came back */

    /* Uncontrolled the same gate is ONE Toffoli — the differential that says
     * the three above are a promotion and not three gates the body emitted. */
    cq_mock_reset(&m);
    cq_emit_ccx(ctx, &cq_reg_bits(&ctx->regs, ha)[0],
                     &cq_reg_bits(&ctx->regs, hb)[0],
                     &cq_reg_bits(&ctx->regs, ht)[0]);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CCX), 1);

    fresh();
    cqops_set_sink(NULL);
    cq_mock_dispose(&m);
}

typedef struct {
    cq_ctx  *ctx;
    cq_bit  *target;
    int32_t  flag_handle;
    uint32_t flag_index;
    int      promoting;
    uint32_t wire_index;
    int      index_was_free;
    uint32_t stranded;
    uint32_t reports;
} freeing_env;

/* The body of ARM 2. It does the one thing bd d6m says nothing forbids. */
static void free_the_control_rail_then_emit(void *p)
{
    freeing_env *e = (freeing_env *)p;

    /* THE REGION REALLY IS PROMOTING AT THIS INSTANT, read off M06's own
     * discriminator rather than inferred from the gate that follows. Without
     * this the case would still pass against a bracket that had silently gone
     * row-0 SKIP — no gate, no wire, and "nothing caught it" trivially true
     * because nothing happened. */
    const cq_bit *w = cq_ctrl_wire(&e->ctx->ctrl);
    e->promoting  = (w != NULL);
    e->wire_index = w ? cq_bit_qindex(*w) : 0xFFFFFFFFu;

    cq_reg_free(e->ctx, e->flag_handle, cq_pc_zero_proof_rotation_free);

    e->index_was_free = cq_qubits_is_free(&e->ctx->pool, e->flag_index);
    e->stranded       = cq_qubits_stranded(&e->ctx->pool);
    e->reports        = cq_reg_strand_reports(e->ctx);

    cq_emit_x(e->ctx, e->target);
}

/* ARM 2 — THE CHARACTERISATION, NOT A DEATH TEST. bd d6m: cq_ctrl_push copies
 * the control bit BY VALUE, so the frame holds a qubit INDEX; indices are
 * recycled through the LIFO free list (D4) while handles are not (D5). A region
 * that outlives its control rail therefore promotes against a wire someone else
 * may now own — right shape, wrong wire, and NO DIAGNOSTIC IN EITHER
 * CONFIGURATION. This case pins that state of affairs so the comment at the
 * bracket is load-bearing rather than decorative; the day bd d6m's option (b)
 * or (c) is taken, it becomes a death test and its assertions invert. That
 * conversion is not hypothetical — building option (b) by hand is a provocation
 * this case is required to go red under.
 *
 * IT IS THE DEPTH-1 HALF OF THE HAZARD, and that is now the whole of the shim's
 * exposure: cq_shim_region refuses to open a nested region, because at depth
 * >= 2 the same act leaves the AND flag dirty on the free list rather than
 * merely mis-wired. See the bracket's comment.
 *
 * THE PROOF IS NAMED, and it has to be. Under Step 23's three-valued free a
 * rail only returns its indices to the free list when a cq_zero_proof answers
 * CQ_PROOF_CLEAN per qubit; anything else STRANDS them, which would make
 * "the index is back on the free list" false for a reason that has nothing to
 * do with d6m. cq_pc_zero_proof_rotation_free answers CLEAN here because the
 * flag was materialised from CQ_BIT_ZERO: cq_materialise emits an X only when
 * the constant was 1, so the wire is born with shadow {value 0, unknown 0}. A
 * Q1 flag would be PROVEN DIRTY and a poisoned one UNPROVEN, and both would
 * strand — the case asserts the strand counters are untouched so that a future
 * change of proof cannot quietly turn this into a different test.
 *
 * THE TARGET IS MATERIALISED BEFORE THE FREE, AND THAT ORDERING IS THE CASE.
 * Measured: with the target still a constant at the free, cq_emit_x
 * materialises it INSIDE the region and immediately re-acquires the flag's own
 * index off the LIFO free list, so the emitted gate is `cx(q0, q0)` — which
 * Release passes silently and Debug aborts on one layer down, in
 * cq_shadow_cx's `c != t` assert. That is a different bug in a different layer
 * and it is configuration-dependent. Materialising first gives d6m's witness
 * exactly: `cx(flag, target)` with `flag` sitting on the free list. */
CQ_TEST(a_region_outliving_its_control_rail_is_caught_by_nothing)
{
    cq_mock m;
    cq_mock_init(&m);
    cq_sink s = cq_mock_sink(&m);

    fresh();
    cqops_set_sink(&s);
    cq_ctx *ctx = cq_shim_ctx();

    const int32_t ht = cq_bk_reg(ctx, 1u, 0u, 1u);   /* target wire, FIRST */
    const int32_t hf = cq_bk_reg(ctx, 1u, 0u, 1u);   /* the flag: Q, shadow 0 */

    uint32_t idx[1];
    CHECK_EQ(cq_pc_indices(ctx, hf, idx, 1u), 1);
    const uint32_t target_q = cq_bit_qindex(cq_reg_cbits(&ctx->regs, ht)[0]);

    cq_mock_reset(&m);
    freeing_env e = { ctx, &cq_reg_bits(&ctx->regs, ht)[0], hf, idx[0],
                      0, 0u, -1, 99u, 99u };
    cq_shim_region(hf, free_the_control_rail_then_emit, &e);

    /* It returned. Nothing aborted, in either configuration. */
    CHECK_EQ(cq_ctrl_depth(ctx), 0);

    /* The region was promoting, against this flag, at the moment of the free. */
    CHECK_EQ(e.promoting, 1);
    CHECK_EQ(e.wire_index, idx[0]);

    /* The free was a genuine, proven-clean release — not a strand. */
    CHECK_EQ(e.index_was_free, 1);
    CHECK_EQ(e.stranded, 0);
    CHECK_EQ(e.reports, 0);

    /* And the promoted gate names the index that is now on the free list. */
    const cq_rec want[] = { CQ_REC_CX(idx[0], target_q) };
    CHECK(cq_mock_matches(&m, want, 1));
    CHECK_EQ(cq_qubits_is_free(&ctx->pool, idx[0]), 1);

    fresh();
    cqops_set_sink(NULL);
    cq_mock_dispose(&m);
}

/* --- ARM 3: the location assertion. ---------------------------------------
 *
 * NOT A TOKEN COUNT. One macro can serve five symbols, so counting textual
 * occurrences both false-alarms and goes vacuous the moment someone "fixes" the
 * count. The claim is about LOCATION: the set of files under shim/ that CALL
 * cq_ctrl_push or cq_ctrl_pop is exactly { cq_shim_ctx.c }. Risk R7's
 * structural pin (tests/test_gen_shim.py) is the precedent — it diffs a set of
 * definitions and never counts a token. */

/* CALL SITES, NOT MENTIONS, and the distinction was measured rather than
 * anticipated: the first spelling of this scan went red on shim/cq_shim.h and
 * shim/gen_bodies.py, both of which DISCUSS the bracket in prose — "what M26
 * does between cq_ctrl_push and cq_ctrl_pop is unconstrained" — and neither of
 * which opens one. A prose mention is not a second bracket, and a rule that
 * forbade the words would forbid exactly the comments bd d6m fix (a) consists
 * of. So the discriminator is the identifier followed by an open paren, with
 * whitespace allowed between, which `cq_ctrl_push (ctx, b)` would otherwise
 * slip through. */
static size_t call_sites(const char *hay, const char *fn)
{
    const size_t len = strlen(fn);
    size_t n = 0;

    for (const char *p = strstr(hay, fn); p; p = strstr(p + 1, fn)) {
        const char *q = p + len;
        while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
        if (*q == '(') n++;
    }
    return n;
}

/* THE INSTRUMENT'S OWN INSTRUMENT. A scanner that always answered 0 would make
 * the location assertion below vacuously green for every file it visits, which
 * is this project's most-recorded failure mode. The fourth case carries the
 * exact prose that made the first spelling go red. */
CQ_TEST(the_token_scanner_can_tell_a_call_from_a_mention)
{
    CHECK_EQ(call_sites("cq_ctrl_push(ctx, b); cq_ctrl_pop(ctx);", "cq_ctrl_push"), 1);
    CHECK_EQ(call_sites("cq_ctrl_push (ctx, b);", "cq_ctrl_push"), 1);
    CHECK_EQ(call_sites("cq_ctrl_push\n    (ctx, b);", "cq_ctrl_push"), 1);
    CHECK_EQ(call_sites("what M26 does between cq_ctrl_push and cq_ctrl_pop is "
                        "unconstrained", "cq_ctrl_push"), 0);
    CHECK_EQ(call_sites("nothing here at all", "cq_ctrl_push"), 0);
    CHECK_EQ(call_sites("f(); f(); f();", "f"), 3);
}

/* Reads a whole file into `buf`; 0 if it could not be opened. Aborts rather
 * than truncating, so a file that outgrew the buffer is a failure and not a
 * silently shorter scan. */
static size_t slurp(const char *path, char *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    const size_t n = fread(buf, 1u, cap - 1u, f);
    const int more = (fgetc(f) != EOF);
    fclose(f);
    if (more) cq_h_fail(__FILE__, __LINE__, "%s does not fit in %zu bytes", path, cap);
    buf[n] = '\0';
    return n + 1u;
}

/* WALKED, NOT LISTED, and that is the difference between an assertion that
 * stays complete and one that quietly stops covering things. A hard-coded file
 * list is correct on the day it is written and silently vacuous for every file
 * Steps 23.4-23.7 add — cq_runtime_rail.c, cq_runtime_gate.c, cq_runtime_v2.c,
 * cq_template_impl.c — because a file nobody listed is a file nobody scanned,
 * and nothing would go red. readdir costs a few lines and covers them the day
 * they land.
 *
 * THE ANTI-VACUITY GUARDS ARE THE OTHER HALF. A walk that failed to open the
 * directory, or that recursed into nothing, would report "zero files call the
 * bracket" — and `n_push == 1` catches exactly that, because the bracket's own
 * file would be missing too. What it does NOT catch is a walk that reached
 * cq_shim_ctx.c and nothing else, so the three `seen_*` flags pin that the walk
 * also reached the contract header and descended into generated/. */
static void scan_shim(const char *dir, const char *rel, char *buf, size_t cap,
                      size_t *n_files, size_t *n_push, size_t *n_pop,
                      int *seen_ctx_c, int *seen_shim_h, int *seen_gen_c)
{
    DIR *d = opendir(dir);
    if (!d) {
        cq_h_fail(__FILE__, __LINE__, "cannot open %s", dir);
        return;
    }

    for (const struct dirent *e = readdir(d); e; e = readdir(d)) {
        char path[1024], show[1024];
        struct stat st;

        if (e->d_name[0] == '.') continue;          /* . .. __pycache__ dotfiles */
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        snprintf(show, sizeof show, "%s%s", rel, e->d_name);

        if (stat(path, &st) != 0) {
            cq_h_fail(__FILE__, __LINE__, "cannot stat %s", path);
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            char sub[1024];
            snprintf(sub, sizeof sub, "%s%s/", rel, e->d_name);
            scan_shim(path, sub, buf, cap, n_files, n_push, n_pop,
                      seen_ctx_c, seen_shim_h, seen_gen_c);
            continue;
        }

        const char *dot = strrchr(e->d_name, '.');
        if (!dot) continue;
        if (strcmp(dot, ".c") && strcmp(dot, ".h") &&
            strcmp(dot, ".py") && strcmp(dot, ".inc")) continue;

        if (slurp(path, buf, cap) == 0) {
            cq_h_fail(__FILE__, __LINE__, "cannot read %s", path);
            continue;
        }
        (*n_files)++;
        if (strcmp(show, "cq_shim_ctx.c") == 0) *seen_ctx_c  = 1;
        if (strcmp(show, "cq_shim.h")     == 0) *seen_shim_h = 1;
        if (strstr(show, "generated/") && strstr(show, ".gen.c")) *seen_gen_c = 1;

        const size_t push = call_sites(buf, "cq_ctrl_push");
        const size_t pop  = call_sites(buf, "cq_ctrl_pop");
        const int is_bracket = (strcmp(show, "cq_shim_ctx.c") == 0);

        if (push && !is_bracket)
            cq_h_fail(__FILE__, __LINE__,
                      "shim/%s CALLS cq_ctrl_push; the bracket is cq_shim_ctx.c's "
                      "alone (bd d6m fix (a))", show);
        if (pop && !is_bracket)
            cq_h_fail(__FILE__, __LINE__,
                      "shim/%s CALLS cq_ctrl_pop; the bracket is cq_shim_ctx.c's "
                      "alone (bd d6m fix (a))", show);

        *n_push += (push != 0);
        *n_pop  += (pop != 0);
    }
    closedir(d);
}

CQ_TEST(the_region_bracket_lives_in_exactly_one_shim_file)
{
    static char buf[4u * 1024u * 1024u];
    size_t n_files = 0, n_push = 0, n_pop = 0;
    int seen_ctx_c = 0, seen_shim_h = 0, seen_gen_c = 0;

    scan_shim(CQOPS_SHIM_DIR, "", buf, sizeof buf, &n_files, &n_push, &n_pop,
              &seen_ctx_c, &seen_shim_h, &seen_gen_c);

    CHECK(n_files >= 15);        /* 3 hand-written + 2 generators + 10 *.gen.c */
    CHECK_EQ(seen_ctx_c, 1);     /* the walk reached the bracket's own file    */
    CHECK_EQ(seen_shim_h, 1);    /* ...and the contract header                 */
    CHECK_EQ(seen_gen_c, 1);     /* ...and descended into generated/           */

    CHECK_EQ(n_push, 1);         /* and the walk above says WHICH file         */
    CHECK_EQ(n_pop, 1);
}

/* -------------------------------------------------------------------------
 * 4. The two fail-loud paths, read as BYTES on a named stream.
 *
 * A death case can say only "it aborted". Two things here need more: PRD §1's
 * message is the v1 boundary's user-visible contract, and the shim's own three
 * refusals are otherwise interchangeable — all three print the same
 * `libcqops: FATAL: shim:` prefix, so swapping two of them would survive every
 * exit-code assertion in the file. That is CLAUDE.md's recorded Step 20
 * survivor ("NO TEST READ THE REFUSAL MESSAGE") and the remedy is the same:
 * read the message.
 *
 * READING IT NEEDS A CHILD PROCESS, because both paths are _Noreturn. A ctest
 * PASS_REGULAR_EXPRESSION would not do it either — that property DISPLACES the
 * exit-code check, trading the death test's own contract for a substring
 * (tests/CMakeLists.txt records this) — and it cannot tell stdout from stderr,
 * which is the other half of what is pinned here: every hard error in this
 * project goes to stderr, and stdout is where the default sink writes its
 * trace. Only STDERR_FILENO is dup2'd into the pipe, so a refusal that went to
 * stdout would arrive as an empty string.
 *
 * fflush(NULL) BEFORE THE FORK, AND IT IS NOT HYGIENE. The harness writes its
 * TAP diagnostics to stdout with printf and flushes at the end of a case
 * (tests/support/harness.c); under ctest stdout is a pipe and therefore fully
 * buffered, so a child forked mid-case inherits every line the case has already
 * printed and RE-EMITS them when it aborts. Measured before the flush was
 * added: three `# FAIL` lines for two failed checks.
 * ------------------------------------------------------------------------- */

/* Runs `fn` in a child with stderr on a pipe, and reports what it wrote and
 * whether it died on SIGABRT. If `fn` RETURNS, the child _exit()s rather than
 * falling back into the parent's case list — a forked test binary that resumed
 * would run every remaining case a second time. */
static void child_stderr(void (*fn)(void *), void *arg,
                         char *out, size_t cap, int *signalled)
{
    int fd[2];

    out[0] = '\0';                    /* never report a previous call's bytes */
    *signalled = 0;

    if (pipe(fd) != 0) { cq_h_fail(__FILE__, __LINE__, "pipe failed"); return; }

    fflush(NULL);

    const pid_t pid = fork();
    if (pid < 0) {
        (void)close(fd[0]);
        (void)close(fd[1]);
        cq_h_fail(__FILE__, __LINE__, "fork failed");
        return;
    }

    if (pid == 0) {
        (void)close(fd[0]);
        (void)dup2(fd[1], STDERR_FILENO);
        (void)close(fd[1]);
        fn(arg);
        _exit(0);                     /* fn returned: not an abort, and the
                                       * child must not resume the suite */
    }

    (void)close(fd[1]);
    size_t n = 0;
    for (;;) {
        const ssize_t got = read(fd[0], out + n, cap - 1u - n);
        if (got == 0) break;                       /* EOF: the child is gone  */
        if (got < 0) {
            if (errno == EINTR) continue;          /* a signal, not an end    */
            cq_h_fail(__FILE__, __LINE__, "read failed: errno %d", errno);
            break;
        }
        n += (size_t)got;
        if (n + 1u >= cap) break;
    }
    out[n] = '\0';
    (void)close(fd[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    *signalled = WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

typedef struct { const char *symbol, *reason; } unsup_args;

static void call_unsupported(void *p)
{
    const unsup_args *a = (const unsup_args *)p;
    cq_shim_unsupported(a->symbol, a->reason);
}

static void unsupported_says(const char *symbol, const char *reason,
                             char *out, size_t cap, int *signalled)
{
    unsup_args a = { symbol, reason };
    child_stderr(call_unsupported, &a, out, cap, signalled);
}

/* THE MESSAGE IS THE CONTRACT, AND UNTIL NOW NOTHING READ THE REAL ONE.
 * tests/test_gen_bodies.py pins these bytes against a stub it writes for
 * itself, so the generated bodies and the Python suite agree with each other
 * and with nothing on disk. This runs the definition libcqops actually ships. */
CQ_TEST(the_v1_boundary_prints_prd_1s_message_and_aborts)
{
    char got[512];
    int  signalled = 0;

    unsupported_says("cq_template_sitofp_i32_to_f64", "fp is v2",
                     got, sizeof got, &signalled);

    CHECK_STR_EQ(got, "cqops: cq_template_sitofp_i32_to_f64 "
                      "not implemented (fp is v2)\n");
    CHECK_EQ(signalled, 1);
}

/* THE REASON IS THE CALLER'S STRING AND IS NOT VALIDATED, and that is a live
 * constraint rather than a style note: there are two buckets in the generated
 * bodies today — PRD §1's fp one and D14's `_inv` one — and bd vxk and bd ck6
 * are expected to add a third from cq_runtime_v2.c for the qram / tape /
 * alloc_handle refusals. Anything written against a fixed set of two would have
 * to be edited to let the third through, which is how a bucket gets swept into
 * the wrong message.
 *
 * WHAT THIS DOES NOT PIN, said plainly: the `_inv` string below is supplied BY
 * THIS TEST, so the case constrains the format and nothing about the 603
 * shipped bodies. That those bodies carry the right reason is
 * tests/test_gen_bodies.py's
 * the_two_abort_reasons_are_distinct_and_neither_lies_about_its_bucket, which
 * compares them against tests/support/shimcheck.py's constant. */
CQ_TEST(the_boundary_carries_whatever_reason_its_caller_names)
{
    char got[512];
    int  signalled = 0;

    unsupported_says("cq_template_add_i32_hl_inv",
                     "_inv is f-inverse, not f; the data family has no uniform "
                     "definition (PRD 15 D14)",
                     got, sizeof got, &signalled);

    CHECK_STR_EQ(got, "cqops: cq_template_add_i32_hl_inv not implemented "
                      "(_inv is f-inverse, not f; the data family has no "
                      "uniform definition (PRD 15 D14))\n");
    CHECK_EQ(signalled, 1);

    unsupported_says("cqrt_qram_alloc_i32", "a third reason", got, sizeof got,
                     &signalled);
    CHECK_STR_EQ(got, "cqops: cqrt_qram_alloc_i32 not implemented "
                      "(a third reason)\n");
    CHECK_EQ(signalled, 1);
}

/* THE NULL SUBSTITUTES ARE EXERCISED RATHER THAN LEFT AS DEFENSIVE CODE.
 * Nothing in the tree passes NULL — all 1,487 generated abort bodies pass
 * string literals — so without this case both ternaries are branches nobody has
 * ever seen taken, which is exactly what CLAUDE.md means by "an assertion
 * nobody has seen fail is an assertion nobody has tested". The placeholder is
 * "?" because src/sink.c's cq_sink_die already writes "-" for a missing detail
 * and a second convention would be worse than a terse one; a louder string was
 * considered and declined on that ground. */
CQ_TEST(a_null_symbol_or_reason_is_still_a_well_formed_refusal)
{
    char got[512];
    int  signalled = 0;

    unsupported_says(NULL, "fp is v2", got, sizeof got, &signalled);
    CHECK_STR_EQ(got, "cqops: ? not implemented (fp is v2)\n");
    CHECK_EQ(signalled, 1);

    unsupported_says("cq_template_and_i32_inv", NULL, got, sizeof got, &signalled);
    CHECK_STR_EQ(got, "cqops: cq_template_and_i32_inv not implemented (?)\n");
    CHECK_EQ(signalled, 1);
}

/* WHICH SHIM GUARD FIRED, POSITIVELY. The three refusals in cq_shim_region all
 * abort with the same prefix, so the death cases — which can only assert "it
 * aborted" plus a NEGATIVE tripwire — cannot tell them apart, and swapping two
 * of their message strings would survive every one of them. Reading the bytes
 * off the child's stderr is what distinguishes them, and it pins the STREAM at
 * the same time: only STDERR_FILENO is dup2'd, so a refusal routed to stdout
 * arrives here as an empty string. */
typedef struct { int32_t h; } region_args;

static void region_with_a_wide_flag(void *p)
{
    cq_ctx *ctx = cq_shim_ctx();
    (void)ctx;
    cq_shim_region(((region_args *)p)->h, emit_nothing, NULL);
}

static void region_nested(void *p)
{
    region_args *a = (region_args *)p;
    cq_ctx *ctx = cq_shim_ctx();
    cq_ctrl_push(ctx, &cq_reg_cbits(&ctx->regs, a->h)[0]);
    cq_shim_region(a->h, emit_nothing, NULL);
}

CQ_TEST(each_region_refusal_names_itself_on_stderr)
{
    char got[512];
    int  signalled = 0;

    fresh();
    cq_ctx *ctx = cq_shim_ctx();
    region_args wide = { cq_reg_alloc_zero(&ctx->regs, 8u) };
    region_args one  = { cq_reg_alloc_zero(&ctx->regs, 1u) };

    child_stderr(region_with_a_wide_flag, &wide, got, sizeof got, &signalled);
    CHECK_EQ(signalled, 1);
    CHECK(strstr(got, "libcqops: FATAL: shim: ") == got);
    CHECK(strstr(got, "not one bit wide") != NULL);
    CHECK(strstr(got, "nests") == NULL);          /* not the nesting refusal  */
    CHECK(strstr(got, "no body") == NULL);        /* nor the body one         */

    child_stderr(region_nested, &one, got, sizeof got, &signalled);
    CHECK_EQ(signalled, 1);
    CHECK(strstr(got, "libcqops: FATAL: shim: ") == got);
    CHECK(strstr(got, "never nests") != NULL);
    CHECK(strstr(got, "one bit wide") == NULL);

    fresh();
}
