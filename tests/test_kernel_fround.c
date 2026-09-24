/* tests/test_kernel_fround.c — M38b, K25. The rounding-to-integral family at
 * f64: soft_trunc / soft_floor / soft_ceil / soft_round / soft_round_away.
 * PRD-v2 §5, §7; docs/constructions/K25.md.
 *
 * THIS BINARY OWNS the structure of all five programs (census, composition,
 * the trunc-body inlining identity, the slot scan, the span scan, the
 * palindrome, two programs in one region), the classical rows, the L4
 * goldens of all five, and the CIRCUIT sweeps of the three kernels that do not
 * call `soft_fadd`. floor/ceil's circuit sweeps are
 * tests/test_kernel_fround_fadd.c, split so ctest runs the two ~50k-slot
 * kernels in parallel with this one; the oracle and the specs are shared
 * through tests/test_kernel_fround_spec.inc.
 *
 * WIDTH 64 AND NOTHING ELSE, DELIBERATELY: another width is a hard error in
 * both configurations (tests/test_kernel_fround_death.c).
 */

#include "kernels/fround.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/fadd.h"
#include "kernels/fadd_int.h"   /* the scan's UNMAPPED cq_fa_step, see scan.inc */
#include "kernels/fpfield.h"
#include "kernels/fround_int.h"
#include "kernels/mux.h"
#include "kernels/shift_var.h"
#include "reg.h"
#include "scratch.h"
#include "shadow.h"
#include "sink_count.h"

#include "support/bitkinds.h"
#include "support/fpanchors.h"
#include "support/fphost.h"
#include "support/goldens.h"
#include "support/harness.h"
#include "support/kerneldrv.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"
#include "support/refmodel.h"

#include <stdio.h>
#include <string.h>

#include "test_kernel_fround_spec.inc"

/* ---- The host is what the oracle assumes. ------------------------------- */

/* `rint`/`nearbyint` read the CURRENT rounding mode and FTZ/DAZ would corrupt
 * every subnormal row, so the 9ve.4 probe is asserted in-process before any
 * row below trusts the host. */
CQ_TEST(the_host_is_round_to_nearest_before_the_oracle_is_trusted)
{
    char why[CQ_FPHOST_WHY_MAX];

    if (!cq_fphost_check(why, sizeof why))
        cq_h_fail(__FILE__, __LINE__, "host fp probe: %s", why);
    CHECK_EQ(cq_fphost_arms_run(), CQ_FPHOST_ARMS);
}

/* ---- The classical rows against the oracle, on the complete table. ----- */

/* THE ONLY CASES THAT RUN NO CIRCUIT AND STILL CATCH A PORT ERROR. */
CQ_TEST(the_classical_rows_agree_with_the_oracle_on_every_table_row)
{
    int n = fround_table(-1, NULL);

    CHECK_EQ(n, cq_fp_anchors_unary_count() + N_FROUND_SINGLES);
    for (int p = 0; p < CQ_FROUND_N_PROG; p++)
        for (int i = 0; i < n; i++) {
            uint64_t a = 0u, got, want;

            CHECK_EQ(fround_table(i, &a), 1);
            got  = cq_fround_eval((cq_fround_prog)p, a);
            want = ref_fround((cq_fround_prog)p, a);
            if (got != want)
                cq_h_fail(__FILE__, __LINE__, "%s(%016llx) = %016llx, the "
                          "oracle says %016llx", FR_SPECS[p]->name,
                          (unsigned long long)a, (unsigned long long)got,
                          (unsigned long long)want);
        }
    /* And every CIRCUIT anchor is a row of the complete table's kind: the
     * classical body agrees on it too. */
    for (int p = 0; p < CQ_FROUND_N_PROG; p++)
        for (int i = 0; i < 8; i++)
            CHECK_EQ(cq_fround_eval((cq_fround_prog)p, FR_CIRCUIT[p][i]),
                     ref_fround((cq_fround_prog)p, FR_CIRCUIT[p][i]));
}

/* A SEEDED CLASSICAL DIFFERENTIAL, which costs no circuit (§7.12: what is
 * bounded is the gate-emitting replay, not the classical check). Raw 64-bit
 * patterns hit NaNs and huge values; the structured half draws exponents in
 * [1000, 1080] — the band where every arm of every select chain fires — with
 * fractions whose low bits make ties and near-ties. */
CQ_TEST(the_classical_rows_agree_with_the_host_on_a_seeded_sweep)
{
    uint64_t x = 0x9e3779b97f4a7c15ull;
    int bad = 0;

    for (int k = 0; k < 20000 && bad < 5; k++) {
        uint64_t a;

        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        a = x;
        if (k % 2 == 1) {
            uint64_t e = 1000u + (x >> 40) % 81u;
            uint64_t f = (x % 4u == 0u) ? (x & 0xfffff00000000ull)
                                        : (x & CQ_F64_MANT_MASK);
            a = (x & CQ_F64_SIGN_MASK) | (e << 52) | f;
        }
        for (int p = 0; p < CQ_FROUND_N_PROG; p++) {
            uint64_t got = cq_fround_eval((cq_fround_prog)p, a);
            uint64_t want = ref_fround((cq_fround_prog)p, a);

            if (got != want) {
                cq_h_fail(__FILE__, __LINE__, "%s(%016llx) = %016llx, host "
                          "%016llx", FR_SPECS[p]->name, (unsigned long long)a,
                          (unsigned long long)got, (unsigned long long)want);
                bad++;
            }
        }
    }
}

/* ---- §7.7's name mapping, asserted rather than narrated. ---------------- */

/* THE K9 TRAP IN fp DRESS: C `round` is ties-AWAY (soft_round_away) and C
 * `rint`/`nearbyint` are ties-to-EVEN (soft_round). These pins are LITERALS,
 * read off fround.jl's own docstrings (:106-111, :213-218), so they check the
 * oracle as well as the port: both kernels must separate on every tie. */
CQ_TEST(the_name_mapping_separates_on_every_tie)
{
    static const struct { uint64_t a, even, away; } TIES[] = {
        { FR_HALF,  0x0000000000000000ull, 0x3ff0000000000000ull }, /*  0.5 */
        { FR_NHALF, 0x8000000000000000ull, 0xbff0000000000000ull }, /* -0.5 */
        { FR_1P5,   0x4000000000000000ull, 0x4000000000000000ull }, /*  1.5 */
        { FR_2P5,   0x4000000000000000ull, 0x4008000000000000ull }, /*  2.5 */
        { FR_N2P5,  0xc000000000000000ull, 0xc008000000000000ull }, /* -2.5 */
        { FR_3P5,   0x4010000000000000ull, 0x4010000000000000ull }  /*  3.5 */
    };

    for (size_t i = 0; i < sizeof TIES / sizeof TIES[0]; i++) {
        CHECK_EQ(cq_fround_eval(CQ_FROUND_EVEN, TIES[i].a), TIES[i].even);
        CHECK_EQ(cq_fround_eval(CQ_FROUND_AWAY, TIES[i].a), TIES[i].away);
        CHECK_EQ(ref_fround(CQ_FROUND_EVEN, TIES[i].a), TIES[i].even);
        CHECK_EQ(ref_fround(CQ_FROUND_AWAY, TIES[i].a), TIES[i].away);
    }
    /* trunc / floor / ceil on the three rows the brief names. */
    CHECK_EQ(cq_fround_eval(CQ_FROUND_TRUNC, FR_N1P5), 0xbff0000000000000ull);
    CHECK_EQ(cq_fround_eval(CQ_FROUND_FLOOR, FR_N1P5), 0xc000000000000000ull);
    CHECK_EQ(cq_fround_eval(CQ_FROUND_CEIL,  FR_1P5),  0x4000000000000000ull);
    CHECK_EQ(cq_fround_eval(CQ_FROUND_TRUNC, FR_1P5),  0x3ff0000000000000ull);
    CHECK_EQ(cq_fround_eval(CQ_FROUND_FLOOR, FR_N0P3), 0xbff0000000000000ull);
    CHECK_EQ(cq_fround_eval(CQ_FROUND_CEIL,  FR_N0P3), 0x8000000000000000ull);
    CHECK_EQ(cq_fround_eval(CQ_FROUND_CEIL,  FR_0P3),  0x3ff0000000000000ull);
    /* The NaN cells: quietened, sign and payload kept, in every kernel. */
    for (int p = 0; p < CQ_FROUND_N_PROG; p++) {
        CHECK_EQ(cq_fround_eval((cq_fround_prog)p, CQ_F64_SNAN),
                 CQ_F64_SNAN_QUIET);
        CHECK_EQ(cq_fround_eval((cq_fround_prog)p, FR_NSNAN), FR_NQNAN);
        CHECK_EQ(cq_fround_eval((cq_fround_prog)p, CQ_F64_NEG_INF),
                 CQ_F64_NEG_INF);
        CHECK_EQ(cq_fround_eval((cq_fround_prog)p, FR_2P52P1), FR_2P52P1);
    }
    /* The spec table pairs each C name with the right kernel. */
    CHECK(strcmp(FR_SPECS[CQ_FROUND_EVEN]->name, "fround_even") == 0);
    CHECK(strcmp(FR_SPECS[CQ_FROUND_AWAY]->name, "fround_away") == 0);
}

#include "test_kernel_fround_slots.inc"
#include "test_kernel_fround_scan.inc"
#include "test_kernel_fround_spans.inc"

/* ---- L1 + L2 + L3 + L5, the regions and the peak: trunc, even, away. ---- */

CQ_TEST(circuit_ftrunc)      { fr_circuit_suite(CQ_FROUND_TRUNC, FR_N2P5); }
CQ_TEST(circuit_fround_even) { fr_circuit_suite(CQ_FROUND_EVEN,  FR_2P5);  }
CQ_TEST(circuit_fround_away) { fr_circuit_suite(CQ_FROUND_AWAY,  FR_N2P5); }

/* ---- L4: the goldens of all five, at the all-quantum mask. ------------- */

CQ_TEST(l4_goldens)
{
    cq_gold g;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/fround.counts",
                      "M38b kernels/fround*.c — K25 IEEE binary64 round to "
                      "integral (sandwiched): soft_trunc, soft_floor, "
                      "soft_ceil, soft_round, soft_round_away",
                      "ALL-QUANTUM on `a` at W = 64 (f64 only), ctrl_depth 0. "
                      "Views and constant spans sit inside block operands, so "
                      "these sit below twice each compute half's slot count",
                      CQOPS_BENNETT_COMMIT))
        return;

    for (int p = 0; p < CQ_FROUND_N_PROG; p++) {
        cq_counter fwd, unc;

        cq_kd_measure(FR_SPECS[p], W64, &fwd, &unc);
        CHECK_EQ(fwd.ry + fwd.rz + fwd.mz, 0);
        CHECK_EQ(unc.ry + unc.rz + unc.mz, 0);
        cq_gold_check(&g, FR_SPECS[p]->name, "forward", W64,
                      fwd.x, fwd.cx, fwd.ccx);
        cq_gold_check(&g, FR_SPECS[p]->name, "unc", W64,
                      unc.x, unc.cx, unc.ccx);
    }
    CHECK(cq_gold_close(&g));
}

CQ_TEST_MAIN_ARGV(
    CQ_CASE(the_host_is_round_to_nearest_before_the_oracle_is_trusted),
    CQ_CASE(the_programs_are_the_source_line_for_line),
    CQ_CASE(the_bool_operand_scan_reports_rather_than_aborts),
    CQ_CASE(the_table_reading_guards_are_unreachable),
    CQ_CASE(floor_and_ceil_inline_the_trunc_body_row_for_row),
    CQ_CASE(the_blocks_cost_what_their_modules_say_they_cost),
    CQ_CASE(the_programs_are_the_sum_of_their_blocks),
    CQ_CASE(the_classical_rows_agree_with_the_oracle_on_every_table_row),
    CQ_CASE(the_classical_rows_agree_with_the_host_on_a_seeded_sweep),
    CQ_CASE(the_name_mapping_separates_on_every_tie),
    CQ_CASE(the_slot_boundaries_match_an_independent_four_valued_scan),
    CQ_CASE(the_rows_spans_are_pairwise_disjoint),
    CQ_CASE(the_compute_half_is_a_palindrome_at_an_asymmetric_mask),
    CQ_CASE(two_programs_in_one_region_do_not_collide),
    CQ_CASE(circuit_ftrunc),
    CQ_CASE(circuit_fround_even),
    CQ_CASE(circuit_fround_away),
    CQ_CASE(l4_goldens)
)
