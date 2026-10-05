/* src/kernels/fcmp_step.c — M36, K18, the STEP MACHINE half of the ORDERED
 * CORE. A SECOND SPLIT, on the seam ROW TABLES <-> STEP MACHINE, taken at
 * implementation because fcmp.c measured 298 of Rule 12's 300 non-blank
 * non-comment lines with both halves in one file — a scheduled split in the
 * sense that it was hit rather than surprised by, but one PRD-v2 §5 did NOT
 * record in advance, so it is named in K18.md §2.0 as D-K18-7.
 *
 * fcmp.c holds the three row tables of fcmp.jl's ordered cores and nothing
 * else; this file turns a row into spans and gates and knows no Julia. The
 * seam is real rather than arithmetic: every cost here is ASKED of another
 * module (cq_fp_class_steps, cq_eq_steps, cq_ult_steps, cq_sub_steps,
 * CQ_MUX_STEPS_PER_BIT) or is upstream's own three-gate bitwise vocabulary,
 * and not one line of it would move if a body table changed.
 *
 * ONE GATE PER SLOT, AND IT IS FORCED (bd ckd.14a, sandwich.h). cq_sandwich
 * runs the reverse pass by re-calling compute(env, s) with the SAME index, so
 * a step undoes itself only if it is an involution. Every branch of emit_row
 * emits exactly one X, CX or CCX, or delegates to a step block that does the
 * same — cq_fp_class_step, cq_eq_step, cq_ult_step, cq_sub_step, cq_mux_step.
 *
 * COMPOSITE KERNELS CALL THE STEP FUNCTION, NEVER THE KERNEL. `cq_kernel_eq`,
 * `cq_kernel_ult` and `cq_kernel_fp_is_nan` are each a whole sandwich, and
 * cq_sandwich refuses nesting in BOTH configurations, so reaching for one from
 * inside this compute half aborts before allocating anything. M12 over M17 is
 * the standing witness; K18 is the fifth consumer of the same rule.
 *
 * I6(a) IS SATISFIED WITH NOTHING LEFT TO CHECK. Every target below is a bit
 * of the caller's region — an inner block's own internals, a `lower_not1!`
 * wire, an `and`/`or` output. The rails `a` and `b`, M31's views over them and
 * the constant spans reach the emitter only through cq_emit_*'s
 * `const cq_bit *` parameters, so a source cannot be materialised here by
 * construction. `dst` is touched exclusively in fcmp_pred.c's copy-out, which
 * the driver runs with the extent deliberately disarmed.
 *
 * THE VIEWS AND CONSTANTS ARE REBUILT PER STEP, ON PURPOSE — fpclass.c's
 * reason, verbatim: a block carrying cached operands would carry state whose
 * initialisation a consumer can forget, and a forgotten bind is a silent wrong
 * circuit rather than a failure.
 *
 * THE PREFIX-OFFSET WALK IS DONE ONCE PER PROGRAM, NOT ONCE PER STEP AND NOT
 * ONCE PER LOOKUP. A row reference resolves to a span, and a span needs the
 * sum of every earlier row's region; doing that per operand would make the
 * step O(n^2) in a program of up to 42 rows driven up to 7,253 times per call.
 * Until 2026-10-05 (bd ifoy, item (e)) the walk ran once per STEP, which still
 * re-asked every sibling module every row's cost on every slot — the cost bd
 * y65c measured as most of the fp suites' time and removed from fpround, fma,
 * ibits and fadd. It is now `map_of`'s memoised prefix map plus a binary
 * search; the map holds layout only (see the note at it), so the rule above
 * about VIEWS AND CONSTANTS is untouched.
 */

#include "kernels/fcmp.h"

#include "emit.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/fpclass.h"
#include "kernels/kernel.h"
#include "kernels/mux.h"

#include <string.h>

enum { W64 = CQ_FP64_W };

/* --- Slot and region arithmetic, ASKED of the owning modules. ------------- */

/* `lower_not1!` is CNOT(w, r) then NOT(r) into a FRESH wire — two slots, one
 * bit (arith.jl:474-478). `lower_and!` is one Toffoli per lane, so one slot
 * and no bit of its own at one lane (:268-272). `lower_or!` is CNOT, CNOT,
 * Toffoli per lane, so three slots and one bit (:274-282). Transcribed from
 * the pinned source because they are emitted directly here and no module
 * publishes a cost for them (PRD-v2 §7.10). */
enum { FC_NOT1_STEPS = 2, FC_AND1_STEPS = 1, FC_OR1_STEPS = 3 };

static int row_steps(const cq_fcmp_row *r)
{
    switch (r->op) {
    case CQ_FCOP_CLASS_NAN: return cq_fp_class_steps(CQ_FP_IS_NAN);
    case CQ_FCOP_EQ:        return cq_eq_steps(W64);
    case CQ_FCOP_ULT:       return cq_ult_steps(W64);
    case CQ_FCOP_SUB:       return cq_sub_steps(W64);
    case CQ_FCOP_MUX:       return CQ_MUX_STEPS_PER_BIT;      /* one bit */
    case CQ_FCOP_NOT1:      return FC_NOT1_STEPS;
    case CQ_FCOP_AND1:      return FC_AND1_STEPS;
    case CQ_FCOP_OR1:       return FC_OR1_STEPS;
    default: break;
    }
    cq_kernel_die("fcmp: unknown op in the program");
    return 0;
}

/* The bits each row owns. `cq_eq_block` budgets `2W - 1` (diff ++ orr) and
 * `cq_ult_block` `3W + 1` (nb ++ carry ++ axnb, carry being W+1 and not W) —
 * both stated as contracts in cmp.h; `cq_sub_block` is `3W` (nb ++ d ++ c,
 * add.h) and `cq_mux_block` is `2` at one bit (r ++ d, mux.h). M31 publishes
 * its own as a function. */
static uint32_t row_region(const cq_fcmp_row *r)
{
    switch (r->op) {
    case CQ_FCOP_CLASS_NAN: return cq_fp_class_region(CQ_FP_IS_NAN);
    case CQ_FCOP_EQ:        return (uint32_t)(2 * W64 - 1);
    case CQ_FCOP_ULT:       return (uint32_t)(3 * W64 + 1);
    case CQ_FCOP_SUB:       return (uint32_t)(3 * W64);
    case CQ_FCOP_MUX:       return 2u;
    default:                return 1u;    /* not1, and, or: one output bit */
    }
}

static void check_program(const cq_fcmp_row *rows, int n)
{
    if (rows == NULL || n <= 0 || n > CQ_FCMP_MAX_ROWS)
        cq_kernel_die("fcmp: the program is empty or longer than "
                      "CQ_FCMP_MAX_ROWS");
}

/* --- The prefix map, built once per program (2026-10-05, bd ifoy (e)). --- */

/* y65c's cq_fpr_map_get one module over, with ONE DIFFERENCE THIS MODULE'S API
 * FORCES. A program here is not a compile-time table with an id: it is rows a
 * CALLER wrote into its own buffer (fcmp_pred.c, fmisc_step.c, the tests), so
 * the key is the PROGRAM TEXT — `n` and every row, compared in full — and
 * never the caller's pointer, which a stack buffer hands to a different
 * program on the next call. The map holds TABLE LAYOUT ONLY: base-RELATIVE
 * region offsets, slot boundaries and the region total — a pure function of
 * the key and of the siblings' published costs at W=64, never an operand, a
 * span, a view or circuit state, and independent of both the step index and
 * `k->off`. Recomputing it at any moment gives the same answer, so
 * cq_sandwich's index reversal cannot desynchronise it. Rule 13's argument is
 * a-395-row-flat-table-needs-the-memoised-prefix-map's. The library is
 * single-threaded, so no entry is shared mid-build.
 *
 * NO EVICTION: CQ_FCMP_MAX_PROGRAMS distinct programs, and the next one is a
 * hard error in both configurations naming this map (fcmp.h says why sixteen
 * is enough). A full table never reuses an entry, so no program can ever be
 * served another's layout, and an entry, once built, never moves.
 *
 * THE KEY HAS ONE DETECTOR, AND IT HAD TO BE WRITTEN: every program
 * cq_fcmp_program builds differs in length from every other, so a key cut to
 * the length, or to the caller's pointer, passed every other case — measured.
 * two_programs_of_one_length_each_get_their_own_layout
 * (tests/test_kernel_fcmp_spans.inc) alternates two hand-built programs of
 * one length through one reused buffer. */
_Static_assert(CQ_FCMP_MAX_PROGRAMS >= CQ_FCMP_N_PRED,
               "the map must hold every predicate's program at once; there "
               "are ten distinct ones and fourteen predicates");

typedef struct {
    cq_fcmp_row rows[CQ_FCMP_MAX_ROWS];       /* the key: the program text   */
    int         n;                            /* 0 = empty: check_program
                                                 refuses n <= 0, so it never
                                                 matches                      */
    uint32_t    rel[CQ_FCMP_MAX_ROWS];        /* RELATIVE to the block's base */
    int         step0[CQ_FCMP_MAX_ROWS + 1];  /* [n] is the slot total        */
    uint32_t    region;
} fc_map;

_Static_assert(sizeof(cq_fcmp_row) == 4 * sizeof(short),
               "map_of compares programs with memcmp, which is a comparison "
               "of the rows only while a row has no padding bytes");

static int same_program(const fc_map *m, const cq_fcmp_row *rows, int n)
{
    return m->n == n && memcmp(m->rows, rows, (size_t)n * sizeof *rows) == 0;
}

static const fc_map *map_of(const cq_fcmp_row *rows, int n)
{
    static fc_map maps[CQ_FCMP_MAX_PROGRAMS];
    static int used = 0, last = 0;
    fc_map *m;
    uint32_t bits = 0u;
    int slots = 0;

    check_program(rows, n);
    if (same_program(&maps[last], rows, n)) return &maps[last];
    for (int j = 0; j < used; j++)
        if (same_program(&maps[j], rows, n)) { last = j; return &maps[j]; }

    if (used >= CQ_FCMP_MAX_PROGRAMS)
        cq_kernel_die("fcmp: the prefix map is full: a program past "
                      "CQ_FCMP_MAX_PROGRAMS distinct ones in one process");
    last = used;
    m = &maps[used++];
    m->n = 0;                    /* an aborted build must never match */
    for (int i = 0; i < n; i++) {
        m->rows[i]  = rows[i];
        m->rel[i]   = bits;
        m->step0[i] = slots;
        bits  += row_region(&rows[i]);
        slots += row_steps(&rows[i]);
    }
    m->step0[n] = slots;
    m->region   = bits;
    m->n        = n;
    return m;
}

uint32_t cq_fcmp_region(const cq_fcmp_row *rows, int n)
{
    return map_of(rows, n)->region;
}

int cq_fcmp_steps(const cq_fcmp_row *rows, int n)
{
    const fc_map *m = map_of(rows, n);

    return m->step0[m->n];
}

/* --- The step machine. --------------------------------------------------- */

/* THE ONE PLACE `k->off` IS APPLIED. The map is base-relative and every span
 * below is reached through this, so a dropped `off` stays a single-line
 * mutation that only a second program in one region sees
 * (two_programs_in_one_region_do_not_collide). */
static uint32_t row_off(const cq_fcmp_block *k, const fc_map *map, int i)
{
    return k->off + map->rel[i];
}

/* `at` is ABSOLUTE inside the region: `row_off` has already added `k->off`,
 * which is the one place this block's base is applied. */
static cq_bit *span(const cq_fcmp_block *k, uint32_t at, uint32_t len)
{
    return cq_scratch_span(k->scr, at, len);
}

/* The region check is the BLOCK's, and it has to be: without it the first
 * out-of-region span aborts in M08 naming the REGION rather than the consumer
 * that mis-sized its offset, and the two-programs-in-one-region shape then has
 * no diagnostic of its own. Hard error in both configurations. */
static void check_region(const cq_fcmp_block *k, const fc_map *m)
{
    if (k->scr == NULL) cq_kernel_die("fcmp: the block has no region");
    if ((uint64_t)k->off + m->region > (uint64_t)cq_scratch_size(k->scr))
        cq_kernel_die("fcmp: the block's region does not fit at its offset");
}

/* Row `i`'s answer, as a one-bit span inside the caller's region. Read through
 * the owning module's accessor wherever there is one: `cq_eq_flag` is
 * `orr[W-2]` in general and `diff[0]` at W == 1, and a consumer spelling that
 * rule inline reads `orr[-1]` at the bottom of the ladder (cmp.h). */
static const cq_bit *flag_of(const cq_fcmp_block *k, const fc_map *map,
                            int i)
{
    const cq_fcmp_row *r;
    uint32_t o;

    /* A 1-BIT OPERAND MUST BE A ROW OF THIS PROGRAM, AND NOTHING BELOW M36
     * KNOWS THAT. `op64` refuses an operand code it does not recognise; this
     * is the mirror refusal for the other direction — a `not1`, `and`, `or` or
     * `mux` handed one of the eight NEGATIVE codes, which would index the row
     * table out of bounds and hand the emitter whatever it read. Release has
     * no other detector at all: the read succeeds, the gate is plausible and
     * the value is wrong. Hard error in both configurations. */
    if (i < 0 || i >= k->n_rows)
        cq_kernel_die("fcmp: a 1-bit operand is not a row of this program");

    r = &k->rows[i];
    o = row_off(k, map, i);

    if (r->op == CQ_FCOP_CLASS_NAN) {
        cq_fp_class_block c;

        c.a = k->a; c.scr = k->scr; c.off = o; c.cls = CQ_FP_IS_NAN;
        return cq_fp_class_flag(&c);
    }
    if (r->op == CQ_FCOP_EQ) {
        cq_eq_block e;

        e.a = NULL; e.b = NULL; e.W = W64;
        e.diff = span(k, o, (uint32_t)W64);
        e.orr  = span(k, o + (uint32_t)W64, (uint32_t)W64 - 1u);
        return cq_eq_flag(&e);
    }
    /* `carry[W]` IS `a >=u b` (cmp.h), and carry starts one vector in. */
    if (r->op == CQ_FCOP_ULT) return span(k, o + (uint32_t)(2 * W64), 1u);
    /* `d` is the difference and lies between `nb` and `c` (add.h). */
    if (r->op == CQ_FCOP_SUB)  return span(k, o + (uint32_t)W64, 1u);
    /* mux `r`, and every bitwise row's single output bit, sit at rel 0. */
    return span(k, o, 1u);
}

/* A 64-lane block operand. Every arm but the first two costs ZERO gates and
 * ZERO qubits: PRD-v2 §7.3 as amended makes an AND or a shift by a
 * compile-time constant WIRING, so `a & ABS_MASK` and `a >> 63` are views and
 * the constants are `const cq_bit` spans. A row reference arrives as the
 * Julia type's width — one wire zero-extended to 64 — which is what `UInt64(1)
 * - soft_fcmp_oeq(a, b)` (fcmp.jl:102) subtracts from. */
static const cq_bit *op64(const cq_fcmp_block *k, const fc_map *map, int s,
                          cq_bit *buf)
{
    switch (s) {
    case CQ_FC_A:      return k->a;
    case CQ_FC_B:      return k->b;
    case CQ_FC_ABS_A:  cq_fp_view(k->a, 0, W64 - 1, buf);  return buf;
    case CQ_FC_ABS_B:  cq_fp_view(k->b, 0, W64 - 1, buf);  return buf;
    case CQ_FC_SIGN_A: cq_fp_view_sign(k->a, buf);         return buf;
    case CQ_FC_SIGN_B: cq_fp_view_sign(k->b, buf);         return buf;
    case CQ_FC_K_ZERO: cq_fp_const_zero(buf);              return buf;
    case CQ_FC_K_ONE:  cq_fp_const(buf, UINT64_C(1));      return buf;
    default: break;
    }
    if (s < 0) cq_kernel_die("fcmp: unknown 64-lane operand code");
    cq_fp_view(flag_of(k, map, s), 0, 1, buf);
    return buf;
}

static void emit_row(cq_ctx *ctx, const cq_fcmp_block *k, const fc_map *map,
                     int i, int u)
{
    const cq_fcmp_row *r = &k->rows[i];
    cq_bit v0[CQ_FP64_W], v1[CQ_FP64_W];
    uint32_t o = row_off(k, map, i);

    switch (r->op) {
    case CQ_FCOP_CLASS_NAN: {
        cq_fp_class_block c;

        c.a = op64(k, map, r->s0, v0);
        c.scr = k->scr; c.off = o; c.cls = CQ_FP_IS_NAN;
        cq_fp_class_step(ctx, &c, u);
        return; }
    case CQ_FCOP_EQ: {
        cq_eq_block e;

        e.a = op64(k, map, r->s0, v0); e.b = op64(k, map, r->s1, v1);
        e.diff = span(k, o, (uint32_t)W64);
        e.orr  = span(k, o + (uint32_t)W64, (uint32_t)W64 - 1u);
        e.W = W64;
        cq_eq_step(ctx, &e, u);
        return; }
    case CQ_FCOP_ULT: {
        cq_ult_block c;

        c.a = op64(k, map, r->s0, v0); c.b = op64(k, map, r->s1, v1);
        c.nb    = span(k, o,                            (uint32_t)W64);
        c.carry = span(k, o + (uint32_t)W64,            (uint32_t)W64 + 1u);
        c.axnb  = span(k, o + (uint32_t)(2 * W64 + 1),  (uint32_t)W64);
        c.W = W64;
        cq_ult_step(ctx, &c, u);
        return; }
    case CQ_FCOP_SUB: {
        cq_sub_block s;

        s.a = op64(k, map, r->s0, v0); s.b = op64(k, map, r->s1, v1);
        s.nb = span(k, o,                          (uint32_t)W64);
        s.d  = span(k, o + (uint32_t)W64,          (uint32_t)W64);
        s.c  = span(k, o + (uint32_t)(2 * W64),    (uint32_t)W64);
        s.W = W64;
        cq_sub_step(ctx, &s, u);
        return; }
    case CQ_FCOP_MUX: {
        cq_mux_block m;

        m.cond = flag_of(k, map, r->s0);
        m.t    = flag_of(k, map, r->s1);
        m.f    = flag_of(k, map, r->s2);
        m.r    = span(k, o,      1u);
        m.d    = span(k, o + 1u, 1u);
        cq_mux_step(ctx, &m, u);
        return; }
    case CQ_FCOP_NOT1:
        /* arith.jl:476, in order: CNOT(w[1], r[1]) then NOT(r[1]). */
        if (u == 0) cq_emit_cx(ctx, flag_of(k, map, r->s0), span(k, o, 1u));
        else        cq_emit_x (ctx,                         span(k, o, 1u));
        return;
    case CQ_FCOP_AND1:
        /* arith.jl:270's one Toffoli per lane, at one lane. */
        cq_emit_ccx(ctx, flag_of(k, map, r->s0), flag_of(k, map, r->s1),
                    span(k, o, 1u));
        return;
    default:
        /* arith.jl:276-278, in order: CNOT(a), CNOT(b), Toffoli(a, b). */
        if (u == 0)      cq_emit_cx(ctx, flag_of(k, map, r->s0), span(k, o, 1u));
        else if (u == 1) cq_emit_cx(ctx, flag_of(k, map, r->s1), span(k, o, 1u));
        else             cq_emit_ccx(ctx, flag_of(k, map, r->s0),
                                     flag_of(k, map, r->s1), span(k, o, 1u));
        return;
    }
}

/* A BINARY SEARCH OVER THE MEMOISED MAP for the LAST row whose `step0 <= u`.
 * Every op here owns at least one slot, so `step0` is strictly increasing
 * today; the last-match spelling is still the one fpround and fma use, so a
 * zero-slot row added later cannot land the search on a zero-width row. The
 * fall-off refusal is the old linear walk's, at the same place — the slot is
 * past the end of the row the search found — and keeps its message, which
 * tests/CMakeLists.txt pins absent from step_index_past_the_end so that case
 * stays a claim about the range check above it. */
void cq_fcmp_step(cq_ctx *ctx, const cq_fcmp_block *k, int u)
{
    const fc_map *map = map_of(k->rows, k->n_rows);
    int lo = 0, hi = map->n - 1, i = 0;

    if (u < 0 || u >= map->step0[map->n])
        cq_kernel_die("fcmp: step index outside [0, cq_fcmp_steps(rows, n))");
    check_region(k, map);

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;

        if (map->step0[mid] <= u) { i = mid; lo = mid + 1; }
        else                      { hi = mid - 1; }
    }
    if (u >= map->step0[i + 1])
        cq_kernel_die("fcmp: the step dispatch fell off the end of the program");
    emit_row(ctx, k, map, i, u - map->step0[i]);
}

const cq_bit *cq_fcmp_flag(const cq_fcmp_block *k)
{
    const fc_map *map = map_of(k->rows, k->n_rows);

    check_region(k, map);
    return flag_of(k, map, k->n_rows - 1);
}
