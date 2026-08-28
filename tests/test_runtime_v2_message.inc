/* tests/test_runtime_v2_message.inc — the MESSAGE half of the V1 boundary
 * suite, split on the seam tests/test_runtime_v2.c recorded in its own header
 * BEFORE its cases were written:
 *
 *     the SURFACE (which symbols exist) <-> the MESSAGE (what each one says)
 *
 * TAKEN AT THE HOUSE 240 TRIGGER, on the first measurement: the `.c` landed at
 * 277 of 300, which is 23 from Rule 12's wall. The cut is a subject cut and its
 * discriminator is sharp — the surface half opens FILES (the ABI header, the
 * five shim sources) and names no message at all; this half forks a child per
 * symbol and compares bytes. Nothing here reads a file and nothing there forks.
 *
 * Included by tests/test_runtime_v2.c and inheriting its includes, its bucket
 * strings and its `header_deferred`; out of check_loc.sh's reach by the
 * EXTENSION rule.
 */

/* --- fork and capture ----------------------------------------------------- */

static void child_stderr(void (*fn)(void), char *out, size_t cap, int *aborted)
{
    int fd[2];

    out[0] = '\0';
    *aborted = 0;

    if (pipe(fd) != 0) { cq_h_fail(__FILE__, __LINE__, "pipe failed"); return; }
    fflush(NULL);

    const pid_t pid = fork();
    if (pid < 0) {
        (void)close(fd[0]); (void)close(fd[1]);
        cq_h_fail(__FILE__, __LINE__, "fork failed");
        return;
    }
    if (pid == 0) {
        (void)close(fd[0]);
        (void)dup2(fd[1], STDERR_FILENO);
        (void)close(fd[1]);
        fn();
        _exit(0);              /* fn returned: not an abort, and the child must
                                * not resume the suite */
    }

    (void)close(fd[1]);
    size_t n = 0;
    for (;;) {
        const ssize_t got = read(fd[0], out + n, cap - 1u - n);
        if (got == 0) break;
        if (got < 0) {
            if (errno == EINTR) continue;
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
    *aborted = WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

/* --- the sweep, run once and cached --------------------------------------- */

typedef struct {
    char name[V2_NAME_MAX];
    char reason[256];
    int  aborted;
    int  well_formed;
} v2_said;

static v2_said g_said[CQ_V2_N_THUNKS];
static int     g_swept;

/* `cqops: <symbol> not implemented (<reason>)\n`, split without a scanf so a
 * missing field is a FAILURE rather than a partial parse. */
static void parse_said(const char *msg, v2_said *s)
{
    const char *p = "cqops: ";
    const char *mid = " not implemented (";

    s->well_formed = 0;
    if (strncmp(msg, p, strlen(p)) != 0) return;

    const char *a = msg + strlen(p);
    const char *b = strstr(a, mid);
    if (!b) return;

    const size_t nlen = (size_t)(b - a);
    if (nlen == 0u || nlen >= sizeof s->name) return;
    memcpy(s->name, a, nlen);
    s->name[nlen] = '\0';

    const char *c = b + strlen(mid);
    const char *e = strstr(c, ")\n");
    if (!e || e[2] != '\0') return;

    const size_t rlen = (size_t)(e - c);
    if (rlen >= sizeof s->reason) return;
    memcpy(s->reason, c, rlen);
    s->reason[rlen] = '\0';
    s->well_formed = 1;
}

static void v2_sweep(void)
{
    char msg[512];

    if (g_swept) return;
    for (size_t i = 0; i < CQ_V2_N_THUNKS; i++) {
        int aborted = 0;

        child_stderr(CQ_V2_THUNKS[i], msg, sizeof msg, &aborted);
        g_said[i].aborted = aborted;
        parse_said(msg, &g_said[i]);
    }
    g_swept = 1;
}

/* EVERY ONE OF THE 109 ABORTS, AND NAMES ITSELF. The abort is the cheap half;
 * the name is what a death suite could never have checked, and with 12 macro
 * bodies covering 109 symbols a single wrong literal would ship 4 or 9 wrong
 * messages at once.
 *
 * OBSERVED FAILING (2026-08-27, both configurations): a body rewritten to
 * `return 0;` instead of aborting. */
CQ_TEST(every_deferred_symbol_aborts_with_a_well_formed_message)
{
    v2_sweep();

    for (size_t i = 0; i < CQ_V2_N_THUNKS; i++) {
        if (!g_said[i].aborted)
            cq_h_fail(__FILE__, __LINE__, "thunk %zu did not abort", i);
        if (!g_said[i].well_formed)
            cq_h_fail(__FILE__, __LINE__, "thunk %zu: malformed message", i);
    }
}

/* BOTH DIRECTIONS, over the SET. A name in the messages and not in the header
 * is an invention; a name in the header and not in the messages is a symbol
 * whose body names something else — which is exactly what a stringification
 * slip produces, and it necessarily produces BOTH at once.
 *
 * OBSERVED FAILING (2026-08-27, both configurations) and it is the SOLE
 * detector for its mutant: `"cqrt_rz_f" #W "_controlled_inv"` shortened to
 * `"cqrt_rz_f" #W "_controlled"` — four symbols printing a name that is already
 * another symbol's — turns THIS case red and leaves the other five green. The
 * abort still happens, the message is still well formed, and the bucket is
 * still right, so nothing else in the suite can see it. */
CQ_TEST(the_names_the_bodies_print_are_exactly_the_headers_deferred_set)
{
    char want[128][V2_NAME_MAX];
    const uint32_t nw = header_deferred(want, 128u);

    v2_sweep();

    for (size_t i = 0; i < CQ_V2_N_THUNKS; i++) {
        int seen = 0;
        for (uint32_t j = 0; j < nw; j++)
            if (strcmp(g_said[i].name, want[j]) == 0) { seen = 1; break; }
        if (!seen)
            cq_h_fail(__FILE__, __LINE__, "body printed `%s`, which the ABI "
                      "header does not declare as deferred", g_said[i].name);
    }

    for (uint32_t j = 0; j < nw; j++) {
        int seen = 0;
        for (size_t i = 0; i < CQ_V2_N_THUNKS; i++)
            if (strcmp(g_said[i].name, want[j]) == 0) { seen = 1; break; }
        if (!seen)
            cq_h_fail(__FILE__, __LINE__, "no body printed `%s`", want[j]);
    }
}

/* THE BUCKET IS PART OF THE CONTRACT AND `bd vxk` NAMES THE WAY IT GOES WRONG.
 * The four strings are written out in this file, so a shim that swapped two of
 * them cannot agree with itself into green. The load-bearing row is
 * `cqrt_qram_alloc_f32`: it carries an fp token AND a qram family, and D16
 * assigns it to QRAM because there is no addressable quantum array in libcqops
 * at i1 either — the fp width is not what defers it. */
/* OBSERVED FAILING (2026-08-27, both configurations) on two mutants and SOLE
 * detector for the second: a swapped bucket on the qram alloc body, and an
 * edited reason STRING. */
CQ_TEST(each_body_carries_its_own_buckets_reason)
{
    v2_sweep();

    for (size_t i = 0; i < CQ_V2_N_THUNKS; i++) {
        const char *want = bucket_of(g_said[i].name);

        if (!want) {
            cq_h_fail(__FILE__, __LINE__, "`%s` is not a deferred name",
                      g_said[i].name);
            continue;
        }
        if (strcmp(g_said[i].reason, want) != 0)
            cq_h_fail(__FILE__, __LINE__,
                      "`%s` said `%s`, wanted `%s`",
                      g_said[i].name, g_said[i].reason, want);
    }
}

/* AND THE ONE ROW A FIXTURE-LEVEL TEST WOULD GET WRONG, stated on its own and
 * as LITERAL BYTES rather than through `bucket_of`.
 *
 * WHY IT IS NOT A SECOND COPY OF THE CASE ABOVE, MEASURED RATHER THAN ARGUED
 * (2026-08-27, a PAIRED mutation — the only way to establish that a case which
 * shares a subject with another is not redundant). Move the shim's whole qram
 * bucket into `fp is v2` AND move `bucket_of`'s qram row to match it:
 * `each_body_carries_its_own_buckets_reason` goes GREEN, because the classifier
 * and the shim now agree with each other, and THIS case goes red. A case whose
 * oracle is a classifier can always be neutralised by editing the classifier;
 * one that spells the expected message out cannot.
 *
 * THE FIXTURE-LEVEL TRAP `bd vxk` RECORDS is why the row matters at all: 21 of
 * the 33 corpus fixtures that reach qram before any fp call hit
 * `cqrt_qram_alloc_f{32,64,80}` FIRST — simultaneously qram AND fp — so at the
 * fixture level the same line dies either way and only the reason string
 * changes. This asserts the symbol-level answer directly, and pairs it with an
 * INTEGER-width qram symbol, where fp could never have been the reason.
 *
 * A RIDER ON HOW THAT WAS MEASURED, because the first attempt reported the
 * opposite. Rewriting the shim's qram CALL SITES to `V2_FP` leaves `V2_QRAM`
 * unreferenced, which is `-Wunused-const-variable` and therefore a BUILD
 * FAILURE under `-Werror` — and a hand-run with the build output redirected to
 * /dev/null then runs the PREVIOUS binary and reports the baseline as the
 * mutant's result. Change the CONSTANT's value instead, and never suppress a
 * build's output while mutating. */
CQ_TEST(qram_at_an_fp_width_is_qrams_refusal_and_not_fps)
{
    char msg[512];
    int  aborted = 0;

    child_stderr(t_qa_f32, msg, sizeof msg, &aborted);
    CHECK_EQ(aborted, 1);
    CHECK_STR_EQ(msg, "cqops: cqrt_qram_alloc_f32 not implemented ("
                      V2_R_QRAM ")\n");

    child_stderr(t_qa_i32, msg, sizeof msg, &aborted);
    CHECK_EQ(aborted, 1);
    CHECK_STR_EQ(msg, "cqops: cqrt_qram_alloc_i32 not implemented ("
                      V2_R_QRAM ")\n");
}

