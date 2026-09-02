/* tests/test_shim_trace.c — `bd 76r`: M26's annotation layer, PRD §15 D21.
 *
 * REGISTERED ONLY WHEN THE BUILD FOUND THE QEC LIBRARY (-DCQOPS_QEC_DIR=), for
 * test_sink_qec.c's reason and one more of its own. The annotation's ONE
 * activation test is `cq_sink_qec_trace()` — the stream M25 opened and handed to
 * `qec_set_trace` — which is NULL under every other sink and in a build without
 * the library, so there is no configuration in which this layer emits a byte
 * that a test could read without a real `libqec.a` behind it. That is the point
 * rather than an inconvenience: it is what keeps M23's printf sink and the qec
 * trace disjoint consumers of two streams, and the last case here is the
 * assertion that it holds.
 *
 * THE ORACLE IS `tests/test_shim_trace_check.inc`, an INDEPENDENT reader of
 * `qec/docs/HOST_LANGUAGE_HANDOFF.md` §§3-6 that includes no shim header. What
 * it checks is the set of rules §9's table calls a FATAL PARSE ERROR — the
 * pipeline aborts citing the line, no JSON, no HTML, no degraded render — so a
 * green case here is "the viewer would not refuse this trace", not "the picture
 * is the one we meant".
 *
 * AND THE PICTURE ITSELF IS PINNED SEPARATELY, by reading the `op begin`
 * payloads back in order. Those two are different claims and the first does not
 * imply the second: a producer that bracketed every operation under one name
 * and omitted every `out=` would be perfectly conformant and would render a row
 * of identical anonymous blocks.
 *
 * THE CONFIG IS `config_demo3.json` AND THE PRECISION IS 0, both for cost. The
 * only route from an all-classical program into a quantum one is a general
 * `Ry` (that is D17's corpus finding, not an accident of this suite: nothing
 * else materialises a bit, because a CX from an untainted source folds), and an
 * `Ry` is a gridsynth walk whose trace grows fast — measured on this box at
 * n_logical = 3: 19,636 lines at precision 0, 390,964 at 1, and 55.9 MILLION at
 * the default 20 against `config.json`. Precision is an accuracy knob for the
 * SYNTHESIS and nothing here reads an angle, so 0 costs the suite nothing.
 *
 * n_logical = 3 IS THE POOL CEILING AND EVERY CASE IS WRITTEN AGAINST IT.
 * Under this sink D2's ceiling is `qec_n_logical` and D21 (b) turns RECYCLING
 * OFF, so the budget is three qubits for the whole case, not three at a time.
 * A case that exceeds it does not fail quietly: `libcqops: FATAL: qubit pool:
 * ceiling exceeded`.
 */

#include "cq_runtime_abi.h"
#include "cq_shim.h"
#include "cq_shim_ctx.h"
#include "cq_shim_trace.h"

#include "sink.h"
#include "sink_qec.h"

#include "support/harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_shim_trace_check.inc"

/* setenv/unsetenv are POSIX rather than C11; fine in a test, which links
 * against a real libc (tests/test_sink.c makes the same call). */

/* THE ORDER OF THESE TWO IS LOAD-BEARING AND IS THE ONE THING A NEW CASE GETS
 * WRONG. `cq_sink_qec_teardown` is what composes the header onto the body, and
 * `cq_shim_ctx_reset` is what DROPS the register map the header is made of — so
 * a reset before the teardown ships a trace with no `#REGISTER` line at all,
 * which is not a wrong picture but a refusal. Compose first, then reset. */
static void trace_begin(const char *path)
{
    cq_sink_qec_teardown();          /* drop any binding a previous case left */
    cq_shim_ctx_reset();
    setenv("CQOPS_SINK", "qec", 1);
    setenv("CQOPS_QEC_CONFIG", CQOPS_QEC_TRACE_CONFIG, 1);
    setenv("CQOPS_QEC_PRECISION", "0", 1);
    setenv("CQOPS_QEC_TRACE", path, 1);
    remove(path);
}

static void trace_finish(void)
{
    cq_sink_qec_teardown();
    cq_shim_ctx_reset();
    unsetenv("CQOPS_QEC_TRACE");
    unsetenv("CQOPS_SINK");
}

/* The one materialisation route (see the header comment). Returns a one-bit
 * rail that owns a qubit. */
static int32_t a_quantum_bit(double theta)
{
    int32_t h = cqrt_alloc_i1(0);
    cqrt_ry_i1(h, theta);
    return h;
}

static int partial_exists(const char *path)
{
    char  buf[512];
    FILE *f;

    snprintf(buf, sizeof buf, "%s.partial", path);
    f = fopen(buf, "r");
    if (f) fclose(f);
    return f != NULL;
}

/* -------------------------------------------------------------------------
 * 1. A representative program, end to end.
 * ------------------------------------------------------------------------- */

/* EVERY LINE KIND THE CONTRACT DEFINES, AND EVERY RULE IT CALLS FATAL, on one
 * program that exercises the rail surface, the gate surface and the template
 * surface at once. The three claims are separate on purpose:
 *
 *   - conformance (the oracle), which is what stops the pipeline aborting;
 *   - the HEADER, which is D21 (a) — three quantum rails get a line and the
 *     all-constant i8 does not, which is I4 and therefore L5 made VISIBLE
 *     rather than a gap in the picture;
 *   - the op sequence, which is the picture itself, including the `_unc`
 *     suffix that is the only thing separating an uncompute from its forward.
 */
CQ_TEST(a_representative_program_produces_a_conformant_annotated_trace)
{
    const char *path = "test_shim_trace_main.out";
    cq_conf c;
    int32_t a, b, s, k;

    trace_begin(path);
    a = a_quantum_bit(0.7);                       /* q0 */
    b = cqrt_alloc_i1(1);
    cqrt_cnot(a, b);                              /* materialises b -> q1 */
    k = cqrt_alloc_i8(0x0f);                      /* stays classical: no line */
    cqrt_xorc_i8(k, 0x33);
    s = cq_shim_bin_qq(CQ_SHIM_OP_XOR, 1, a, b);  /* mints h3 -> q2 */
    cq_shim_bin_qq_unc(CQ_SHIM_OP_XOR, 1, s, a, b);
    cqrt_copy_i1(a, s);
    (void)cqrt_measure_i1(s);
    cqrt_free(k);
    trace_finish();

    cq_conf_scan(path, &c);
    cq_conf_ok(&c, "the representative program");

    /* D21 (a): the header names the rails that own qubits, and only those. */
    CHECK_EQ(c.n_reg, 3);
    CHECK(c.n_gates > 0);
    CHECK(!partial_exists(path));       /* a shipped trace removes its body */

    /* The picture. `out=` on a FORWARD names a handle the shim had not minted
     * when the bracket opened — D7b's copy emits before the mint, so the
     * bracket must open before it — and this is where that prediction is
     * checked against the handle the call actually returned. */
    CHECK_EQ(c.n_ops, 8);
    CHECK_STR_EQ(c.op[0], "name=ry, out=h0");
    CHECK_STR_EQ(c.op[1], "name=cnot, in=h0, out=h1");
    CHECK_STR_EQ(c.op[2], "name=xorc, in=h2, out=h2");
    CHECK_STR_EQ(c.op[3], "name=xor, in=h0|h1, out=h3");
    CHECK_STR_EQ(c.op[4], "name=xor_unc, in=h0|h1, out=h3");
    CHECK_STR_EQ(c.op[5], "name=copy, in=h0, out=h3");
    CHECK_STR_EQ(c.op[6], "name=measure, in=h3, out=h3");
    CHECK_STR_EQ(c.op[7], "name=free, in=h2");
    CHECK_EQ((long long)s, 3);

    /* THE ALLOC EXEMPTION, PINNED AS AN ABSENCE. Three `cqrt_alloc_i<W>` calls
     * ran above and none may have opened a bracket: `cqrt_alloc` cannot reach a
     * `qec_*` call at any value or width (I4 + `cq_reg_alloc_const` taking the
     * table, not the context), so its bracket was an empty OP unit and is gone.
     * A count alone would not say this — restoring the bracket and deleting an
     * unrelated one keeps `n_ops` at 8 — so the NAME is what is searched for,
     * across every payload rather than at a fixed index. */
    {
        int i, seen_alloc = 0;
        for (i = 0; i < c.n_ops; i++)
            if (strncmp(c.op[i], "name=alloc,", 11) == 0) seen_alloc = 1;
        CHECK_EQ(seen_alloc, 0);
    }
}

/* -------------------------------------------------------------------------
 * 2. The refusal row.
 * ------------------------------------------------------------------------- */

/* AN ALL-CLASSICAL PROGRAM IS NOT SHIPPED, AND THAT IS THE PACKAGE RULE RATHER
 * THAN A JUDGEMENT ABOUT VALUE. Handoff §6: the two annotation kinds are a
 * package, and an op bracket with zero `#REGISTER` lines is a fatal parse
 * error — so a trace with brackets and no register is one the viewer would
 * REFUSE, and §1's posture is that such a trace must not be shipped at all.
 * Nothing is withheld by it: a gate needs a materialised bit and a materialised
 * bit is a register, so this trace has no gate lines either.
 *
 * THE `.partial` IS WHAT SURVIVES, deliberately — the body is still on disk for
 * whoever is debugging, under a name no consumer reads. */
CQ_TEST(a_program_whose_rails_all_stay_classical_is_not_shipped)
{
    const char *path = "test_shim_trace_classical.out";
    cq_conf c;
    int32_t a, b;

    trace_begin(path);
    a = cqrt_alloc_i8(5);
    b = cqrt_alloc_i8(3);
    cqrt_xorc_i8(a, 0x0f);
    cqrt_addc_i8(b, 4);
    (void)cq_shim_bin_qq(CQ_SHIM_OP_XOR, 8, a, b);
    cqrt_free(a);
    cq_h_mute(1);                       /* the refusal prints to stderr */
    trace_finish();
    cq_h_mute(0);

    cq_conf_scan(path, &c);
    CHECK(c.missing);                   /* the final name was never created */
    CHECK(partial_exists(path));        /* and the body is still there */
}

/* -------------------------------------------------------------------------
 * 3. The two shapes the header could get wrong.
 * ------------------------------------------------------------------------- */

/* `cqrt_cswap` WITH A CLASSICAL ONE FLAG EXCHANGES TWO RAILS' BIT ARRAYS FOR
 * ZERO GATES, so rail `a`'s qubits become rail `b`'s — and a register map built
 * by UNIONING each rail's index set over time would then have both handles
 * claiming both indices. That is an overlapping `#REGISTER`, which handoff §9
 * makes a fatal parse error rather than a merely wrong picture. The snapshot
 * replaces rather than unions, so the recorded lanes follow the BITS; this case
 * is what says so, and `dup_index` is the counter that would fire. */
CQ_TEST(a_constant_flag_cswap_leaves_the_register_map_a_partition)
{
    const char *path = "test_shim_trace_cswap.out";
    cq_conf c;
    int32_t a, b, f;

    trace_begin(path);
    a = a_quantum_bit(0.4);                       /* q0 */
    b = cqrt_alloc_i1(0);
    cqrt_cnot(a, b);                              /* q1 */
    f = cqrt_alloc_i1(1);                         /* a CLASSICAL ONE flag */
    cqrt_cswap(f, a, b);
    trace_finish();

    cq_conf_scan(path, &c);
    cq_conf_ok(&c, "the constant-flag cswap");
    CHECK_EQ(c.n_reg, 2);
    /* THE FLAG COMES FIRST IN `in=` BECAUSE IT COMES FIRST IN THE ABI —
     * `cqrt_cswap(ctrl, a, b)` — and it is h2 because it is allocated third.
     * The payload is the CALL, not a canonicalised operand set. */
    CHECK_STR_EQ(c.op[c.n_ops - 1], "name=cswap, in=h2|h0|h1, out=h0|h1");
}

/* D7b — TWO SOURCES ALIASING EACH OTHER — IS THE ONE SHAPE WHERE THE PREDICTED
 * OUTPUT HANDLE IS NOT `cq_reg_count`. The shim mints a defensive temporary
 * FIRST and copies into it, so the result rail is one further along; and the
 * copy is a loop of `cq_emit_cx` that runs BEFORE the mint, which is precisely
 * why the bracket cannot wait for the handle to exist. Get the `+ 1` wrong and
 * the trace names a rail that is workspace, or one that does not exist yet.
 *
 * THE TEMPORARY ITSELF STAYS UNREGISTERED (§6 rule 3), which is what "workspace
 * qubits stay unregistered" means here: it is a handle CQ_lang never sees, it
 * is hidden at the algorithm level, and it shows as an extra lane inside this
 * one op. */
CQ_TEST(an_aliased_template_call_names_the_handle_it_will_mint)
{
    const char *path = "test_shim_trace_alias.out";
    cq_conf c;
    int32_t a, r;

    trace_begin(path);
    a = a_quantum_bit(0.9);                       /* h0, q0 */
    r = cq_shim_bin_qq(CQ_SHIM_OP_XOR, 1, a, a);  /* h1 is the temporary */
    trace_finish();

    cq_conf_scan(path, &c);
    cq_conf_ok(&c, "the aliased template call");
    CHECK_EQ((long long)r, 2);
    CHECK_STR_EQ(c.op[c.n_ops - 1], "name=xor, in=h0|h0, out=h2");
    /* h0 and the result; the temporary h1 is workspace and gets no line. */
    CHECK_EQ(c.n_reg, 2);
}

/* A REGISTER WITH MORE THAN ONE LANE, which nothing above has: every rail in
 * this suite is one bit, because n_logical = 3 is the whole program's budget
 * and an `i8` under a general `Ry` wants eight. `sext i1 -> i2` is the cheapest
 * two-lane rail there is — it replicates the sign bit, so both destination bits
 * are physical copies (I2: copies are never aliases) and the rail costs two
 * qubits against the one it reads.
 *
 * IT PINS TWO THINGS NO OTHER CASE CAN. The `qubits=` SEPARATOR — a one-lane
 * list never writes one, and handoff §3's grammar is `\d+(,\d+)*` with a
 * non-decimal token a fatal parse error. And the ORDER, which §3 says IS the
 * top-to-bottom lane order in an expanded op (`a[0]` = first listed): the lanes
 * are written LSB-first out of the rail's own bit array, so bit 0 renders
 * above bit 1, and a reversed loop would be conformant and wrong. */
CQ_TEST(a_multi_lane_register_lists_its_lanes_in_bit_order)
{
    const char *path = "test_shim_trace_lanes.out";
    cq_conf c;
    int32_t a, w;

    trace_begin(path);
    a = a_quantum_bit(0.2);                            /* h0 -> q0 */
    w = cq_shim_cast(CQ_SHIM_CAST_SEXT, 1, 2, a);      /* h1 -> q1, q2 */
    trace_finish();

    cq_conf_scan(path, &c);
    cq_conf_ok(&c, "the two-lane register");
    CHECK_EQ((long long)w, 1);
    CHECK_EQ(c.n_reg, 2);
    CHECK_STR_EQ(c.op[c.n_ops - 1], "name=sext, in=h0, out=h1");
    CHECK_STR_EQ(c.reg_line[0], "#REGISTER name=h0 type=i1 qubits=0");
    CHECK_STR_EQ(c.reg_line[1], "#REGISTER name=h1 type=i2 qubits=1,2");
}

/* PRD §15 D23: THE KEPT RAIL IS A REGISTER AND THE TOKEN IS NOT. A tape write
 * mints a rail CQ_lang receives back, so it gets a `#REGISTER` line like any
 * other; the tape handle owns zero qubits and gets none (I4 made visible), and
 * `cqrt_tape_alloc` joins `cqrt_alloc_i<W>`'s named static exemption and opens
 * NO bracket — pinned as an ABSENCE, by name, exactly as the alloc exemption
 * is above. The write's payload names the SOURCE (and the flag) in and the
 * kept rail out, and never the token: `in=` would spell it `h<N>`, a lie.
 * Handles are asserted too, because the token consumes a number from the same
 * D5 counter as the rails — `t0`, then `h1` — which is what the goldens show. */
CQ_TEST(a_controlled_tape_write_registers_the_kept_rail_and_names_no_token)
{
    const char *path = "test_shim_trace_tape.out";
    cq_conf c;
    int32_t t, f, a, o;
    int i, seen = 0;

    trace_begin(path);
    t = cqrt_tape_alloc();                              /* h0: a token      */
    f = a_quantum_bit(0.3);                             /* h1 -> q0         */
    a = a_quantum_bit(0.6);                             /* h2 -> q1         */
    o = cqrt_tape_write_i1_controlled(f, t, a);         /* h3 -> q2, KEPT   */
    trace_finish();

    cq_conf_scan(path, &c);
    cq_conf_ok(&c, "the controlled tape write");
    CHECK_EQ((long long)t, 0);
    CHECK_EQ((long long)f, 1);
    CHECK_EQ((long long)a, 2);
    CHECK_EQ((long long)o, 3);
    CHECK_EQ(c.n_reg, 3);                    /* f, a, the kept rail; no t */
    CHECK_STR_EQ(c.reg_line[2], "#REGISTER name=h3 type=i1 qubits=2");
    CHECK_EQ(c.n_ops, 3);
    CHECK_STR_EQ(c.op[2], "name=tape_write_ctrl, in=h1|h2, out=h3");
    for (i = 0; i < c.n_ops; i++)
        if (strncmp(c.op[i], "name=tape_alloc", 15) == 0) seen = 1;
    CHECK_EQ(seen, 0);
}

CQ_TEST(an_uncontrolled_tape_write_names_the_source_in_and_the_kept_rail_out)
{
    const char *path = "test_shim_trace_tape_plain.out";
    cq_conf c;
    int32_t t, a, o;

    trace_begin(path);
    t = cqrt_tape_alloc();                              /* h0               */
    a = a_quantum_bit(0.6);                             /* h1 -> q0         */
    o = cqrt_tape_write_i1(t, a);                       /* h2 -> q1         */
    trace_finish();

    cq_conf_scan(path, &c);
    cq_conf_ok(&c, "the tape write");
    CHECK_EQ((long long)o, 2);
    CHECK_EQ(c.n_reg, 2);
    CHECK_STR_EQ(c.reg_line[1], "#REGISTER name=h2 type=i1 qubits=1");
    CHECK_STR_EQ(c.op[c.n_ops - 1], "name=tape_write, in=h1, out=h2");
}

/* PRD §15 D24: `cqrt_qram_alloc_<W>` joins the alloc exemption — pinned as an
 * ABSENCE by name — and a load's bracket names the INDEX in and `out` out and
 * NOTHING else: the array is a token (`in=` would spell it h<N>) and the cells
 * and the slot are handles CQ_lang never received (rule 3), so the cell the
 * store wrote has no `#REGISTER` line even though it now owns a qubit, and a
 * store's bracket has no `out=` at all. One cell and a classical index, because
 * the trace config's pool ceiling is three logical qubits: `val`, the cell it
 * is copied into, and `out`. */
CQ_TEST(a_qram_alloc_opens_no_bracket_and_the_cells_are_unregistered)
{
    const char *path = "test_shim_trace_qram.out";
    cq_conf c;
    int32_t a, idx, val, out;
    int i, seen = 0, ops = 0;

    trace_begin(path);
    a   = cqrt_qram_alloc_i1(1);                        /* h0: token; h1 cell */
    idx = cqrt_alloc_i32(0);                            /* h2, classical      */
    val = a_quantum_bit(0.3);                           /* h3 -> q0           */
    cqrt_qram_store_i1(a, idx, val);                    /* cell h1 -> q1; h4 slot */
    out = cqrt_qram_load_i1(a, idx);                    /* h5 -> q2           */
    trace_finish();

    cq_conf_scan(path, &c);
    cq_conf_ok(&c, "the qram program");
    CHECK_EQ((long long)a, 0);
    CHECK_EQ((long long)idx, 2);
    CHECK_EQ((long long)out, 5);
    CHECK_EQ(c.n_reg, 2);                               /* val and out only   */
    for (i = 0; i < c.n_reg; i++)
        if (strstr(c.reg_line[i], "name=h1 ") || strstr(c.reg_line[i], "name=h4 "))
            seen++;
    CHECK_EQ(seen, 0);                                  /* cell, slot: none   */
    for (i = 0; i < c.n_ops; i++) {
        if (strncmp(c.op[i], "name=qram_alloc", 15) == 0) seen++;
        if (strcmp(c.op[i], "name=qram_store, in=h2|h3") == 0) ops++;
        if (strcmp(c.op[i], "name=qram_load, in=h2, out=h5") == 0) ops++;
    }
    CHECK_EQ(seen, 0);
    CHECK_EQ(ops, 2);
}

/* -------------------------------------------------------------------------
 * 4. The oracle's own instrument, and the disjoint-stream claim.
 * ------------------------------------------------------------------------- */

/* AN ASSERTION NOBODY HAS SEEN FAIL IS AN ASSERTION NOBODY HAS TESTED, and
 * every counter in `cq_conf` is one of those on a correct producer. So this
 * case feeds the reader four deliberately broken traces — one per rule that a
 * producer can break — and asserts it CATCHES them. Without it a reader whose
 * `dup_name` search could never match (which is what the first draft shipped)
 * would report every case above as conformant. */
CQ_TEST(the_conformance_reader_catches_what_it_is_written_to_catch)
{
    static const struct { const char *body; const char *what; } BAD[] = {
        { "#REGISTER name=h0 type=i1 qubits=0\nCX 0 1\n", "gate outside" },
        { "#REGISTER name=h0 type=i1 qubits=0\n# STAGE: op begin (name=a)\n"
          "# STAGE: op begin (name=b)\n# STAGE: op end\n# STAGE: op end\n", "nested" },
        { "#REGISTER name=h0 type=i1 qubits=0\n"
          "#REGISTER name=h0 type=i1 qubits=1\n", "duplicate name" },
        { "#REGISTER name=h0 type=i1 qubits=0,1\n"
          "#REGISTER name=h1 type=i1 qubits=1\n", "overlapping index" },
    };
    const char *path = "test_shim_trace_bad.out";
    size_t i;

    for (i = 0u; i < sizeof BAD / sizeof *BAD; i++) {
        cq_conf c;
        FILE *f = fopen(path, "w");
        int  caught;

        CHECK(f != NULL);
        if (!f) return;
        fputs(BAD[i].body, f);
        fclose(f);

        cq_conf_scan(path, &c);
        cq_h_mute(1);
        cq_conf_ok(&c, BAD[i].what);
        caught = cq_h_take_failures();
        cq_h_mute(0);
        if (caught == 0)
            cq_h_fail(__FILE__, __LINE__,
                      "the reader accepted a trace with a %s", BAD[i].what);
    }
    remove(path);
}

/* UNDER ANY OTHER SINK THE LAYER IS INERT, AND THIS IS THE HAZARD D21 NAMES AS
 * CONCRETE AND CHEAP TO HIT. M23's printf sink writes `cx q0 q1` — neither a
 * conformant gate line (the library owns those, spelled `CX 0 17`) nor a
 * conformant annotation — so the two sharing one stream would make every trace
 * a fatal parse error. `cq_trace_ops()` counting ZERO after a program that
 * opened a dozen brackets' worth of entry points is the assertion that they
 * cannot: the bracket writer returns before it counts when the stream is NULL.
 *
 * IT IS AN L5-SHAPED ASSERTION AND SO IT IS PAIRED. "Zero brackets" passes just
 * as well against a layer that was never reached at all, which is why the same
 * program is run once under each sink and the qec run is required to be
 * non-zero. */
CQ_TEST(the_annotation_layer_is_inert_under_every_other_sink)
{
    const char *path = "test_shim_trace_inert.out";
    uint32_t under_qec, under_printf;
    int32_t a;

    trace_begin(path);
    a = cqrt_alloc_i8(3);
    cqrt_xorc_i8(a, 1);
    under_qec = cq_trace_ops();
    cq_h_mute(1);                       /* all-classical: the refusal fires */
    trace_finish();
    cq_h_mute(0);
    remove(path);

    cq_shim_ctx_reset();
    setenv("CQOPS_SINK", "counter", 1);
    a = cqrt_alloc_i8(3);
    cqrt_xorc_i8(a, 1);
    under_printf = cq_trace_ops();
    cq_shim_ctx_reset();
    unsetenv("CQOPS_SINK");

    /* ONE, not two: `cqrt_xorc_i8` brackets and `cqrt_alloc_i8` is the named
     * static exemption (cq_shim_trace.h). The pairing still has teeth — the qec
     * arm has to be non-zero for the printf arm's zero to mean anything. */
    CHECK_EQ(under_qec, 1);
    CHECK_EQ(under_printf, 0);
    CHECK_EQ(cq_trace_open(), 0);
}

CQ_TEST_MAIN(
    CQ_CASE(a_representative_program_produces_a_conformant_annotated_trace),
    CQ_CASE(a_program_whose_rails_all_stay_classical_is_not_shipped),
    CQ_CASE(a_constant_flag_cswap_leaves_the_register_map_a_partition),
    CQ_CASE(an_aliased_template_call_names_the_handle_it_will_mint),
    CQ_CASE(a_multi_lane_register_lists_its_lanes_in_bit_order),
    CQ_CASE(a_controlled_tape_write_registers_the_kept_rail_and_names_no_token),
    CQ_CASE(an_uncontrolled_tape_write_names_the_source_in_and_the_kept_rail_out),
    CQ_CASE(a_qram_alloc_opens_no_bracket_and_the_cells_are_unregistered),
    CQ_CASE(the_conformance_reader_catches_what_it_is_written_to_catch),
    CQ_CASE(the_annotation_layer_is_inert_under_every_other_sink)
)
