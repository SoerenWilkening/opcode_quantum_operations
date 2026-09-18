/* tests/test_kernel_fpround.c — M32, K23. The four shared softfloat helpers
 * of softfloat_common.jl as exported step blocks at f64. PRD-v2 §5's M32 row,
 * §7.1-7.4, §7.6, §7.12, §7.16; docs/constructions/K23.md.
 *
 * THE SUBJECT OF THIS FILE IS THE SLOT ARITHMETIC, THE SIGNEDNESS AND THE TWO
 * HAND-OFF ENCODINGS — not the comparator, the adder or the barrel, each of
 * which has its own suite next door. PRD-v2 §7.1 names what a kernel of this
 * shape actually gets wrong: a phase boundary off by one, a span aliased to
 * the wrong intermediate, a block fed an operand of the wrong width. M32 has
 * 154 rows and therefore 154 boundaries where K12 has four, and its twelve CLZ
 * stages are one repeating structure, so an off-by-one there is absorbed by
 * the next stage and produces a PLAUSIBLE gate rather than a crash.
 *
 * THERE IS NO RULE 7 KERNEL HERE, so there is no cq_kd_spec, no cq_kd_sweep_at
 * and no cq_kd_measure. Every case below drives a block by hand, the way a
 * consumer's flat step space will — which is also the only way to replay a
 * reverse half against a SHADOW proof rather than under cq_sandwich's literal
 * CQ_ZERO_BY_PALINDROME (bd step-block-fixture-discipline item 4).
 *
 * L1's ORACLE IS tests/support/fpref.c AND NOT src/kernels/fpround_eval.c.
 * PRD-v2 §7.16: M32 has no host operator to differential against, so L1 is a
 * hand-written IEEE 754 reference model written from the standard's rules and
 * never from the Julia. `fpround_eval.c` is the library's OWN Julia
 * transcription for the consumers' R9 rows, and it is CHECKED AGAINST fpref
 * rather than used as an oracle — an oracle sharing shape with the
 * implementation is blind to exactly what that implementation gets wrong.
 *
 * WIDTH 64 AND NOTHING ELSE, DELIBERATELY. PRD-v2 §1 scopes v2 to f64, so
 * there is no ladder to sweep: M32 names no width parameter at all and a
 * second width would be a fiction. That is the one place this suite is thinner
 * than a v1 kernel's, and it is thinner because the surface is.
 */

#include "kernels/fpround.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/mux.h"
#include "kernels/shift_var.h"
#include "reg.h"
#include "scratch.h"
#include "shadow.h"
#include "sink_count.h"

#include "support/bitkinds.h"
#include "support/fpref.h"
#include "support/goldens.h"
#include "support/harness.h"
#include "support/kerneldrv.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { W64 = CQ_FP64_W, N_IN = 3 };

/* The biggest region is `_sf_handle_subnormal`'s, and the fixture also mints
 * three 64-lane inputs and a canary pad. Sized generously and asserted rather
 * than trusted — an under-sized array here would read as a pool failure. */
enum { FPR_NAMED_MAX = 8192 };

/* --- The four blocks, as one table. --------------------------------------- */

typedef struct {
    const char   *name;
    cq_fpround_id id;
    int           n_in;     /* how many of the three inputs the block reads */
} fpr_row;

static const fpr_row ROWS[] = {
    { "norm52",  CQ_FPR_NORM52,  2 },
    { "clz",     CQ_FPR_CLZ,     2 },
    { "subnorm", CQ_FPR_SUBNORM, 3 },
    { "round",   CQ_FPR_ROUND,   3 }
};
enum { N_ROWS = 4 };

_Static_assert(sizeof ROWS / sizeof ROWS[0] == (size_t)N_ROWS,
               "fpround.h declares four blocks");

/* --- The fixture. One region for the inputs, one for the block. ----------- */

/* TWO REGIONS, and the split is the block's own contract: `ops` holds what the
 * caller supplies as CONTROLS and `work` holds what the block writes. Only
 * `work` has to come back to |0>; the inputs carry live values and are nobody's
 * ancilla. Two regions are legal because no sandwich is open — I6(a)'s extent
 * is one contiguous range and is armed by cq_sandwich, which these cases
 * deliberately do not use. */
typedef struct {
    cq_ctx        ctx;
    cq_scratch    ops, work;
    cq_bit       *in[N_IN];
    cq_fpround_id id;
    uint32_t      pad;              /* the canary below `off`; see spans.inc */
    uint32_t      named[FPR_NAMED_MAX];
    uint32_t      n_named;
    cq_pc_snap    pristine;
} fpr_fx;

static fpr_fx FX;                   /* 32 KB of `named`; not a stack frame */

static uint32_t fpr_region_of(cq_fpround_id id)
{
    switch (id) {
    case CQ_FPR_NORM52:  return cq_norm52_region();
    case CQ_FPR_CLZ:     return cq_clz_region();
    case CQ_FPR_SUBNORM: return cq_subnorm_region();
    default:             return cq_round_region();
    }
}

static int fpr_steps_of(cq_fpround_id id)
{
    switch (id) {
    case CQ_FPR_NORM52:  return cq_norm52_steps();
    case CQ_FPR_CLZ:     return cq_clz_steps();
    case CQ_FPR_SUBNORM: return cq_subnorm_steps();
    default:             return cq_round_steps();
    }
}

/* The four block structs, built from the fixture at a given offset. Rebuilt
 * per call on purpose — fpclass.c's reason, one layer up: a fixture caching a
 * block would carry state whose initialisation a case can forget. */
#define FPR_N52(f, o) { (f)->in[0], (f)->in[1],             &(f)->work, (o) }
#define FPR_CLZ(f, o) { (f)->in[0], (f)->in[1],             &(f)->work, (o) }
#define FPR_SUB(f, o) { (f)->in[0], (f)->in[1], (f)->in[2], &(f)->work, (o) }
#define FPR_RND(f, o) { (f)->in[0], (f)->in[1], (f)->in[2], &(f)->work, (o) }

static void fpr_step_at(fpr_fx *f, uint32_t o, int u)
{
    switch (f->id) {
    case CQ_FPR_NORM52:  { cq_norm52_block  k = FPR_N52(f, o); cq_norm52_step (&f->ctx, &k, u); return; }
    case CQ_FPR_CLZ:     { cq_clz_block     k = FPR_CLZ(f, o); cq_clz_step    (&f->ctx, &k, u); return; }
    case CQ_FPR_SUBNORM: { cq_subnorm_block k = FPR_SUB(f, o); cq_subnorm_step(&f->ctx, &k, u); return; }
    default:             { cq_round_block   k = FPR_RND(f, o); cq_round_step  (&f->ctx, &k, u); return; }
    }
}

/* Output `which`, in fpround.h's declaration order per block. `which == 4` is
 * `_sf_handle_subnormal`'s `flushed_result`, which is a VIEW and is assembled
 * into `buf` by its own accessor (D-K23-8) rather than named in the region. */
static const cq_bit *fpr_out_at(fpr_fx *f, uint32_t o, int which, cq_bit *buf)
{
    switch (f->id) {
    case CQ_FPR_NORM52: { cq_norm52_block k = FPR_N52(f, o);
        return which ? cq_norm52_e(&k) : cq_norm52_m(&k); }
    case CQ_FPR_CLZ: { cq_clz_block k = FPR_CLZ(f, o);
        return which ? cq_clz_exp(&k) : cq_clz_wr(&k); }
    case CQ_FPR_SUBNORM: { cq_subnorm_block k = FPR_SUB(f, o);
        if (which == 0) return cq_subnorm_wr(&k);
        if (which == 1) return cq_subnorm_exp(&k);
        if (which == 2) return cq_subnorm_flag(&k);
        if (which == 3) return cq_subnorm_ftz(&k);
        cq_subnorm_flushed(&k, buf); return buf; }
    default: { cq_round_block k = FPR_RND(f, o);
        if (which == 0) return cq_round_normal(&k);
        if (which == 1) return cq_round_overflow_result(&k);
        if (which == 2) return cq_round_exp_overflow(&k);
        return cq_round_exp_overflow_aft(&k); }
    }
}

/* A span holding `value`, built the one sanctioned way (Rule 5): set the
 * constant, then cq_materialise, which emits the X for a set bit. Those Xs are
 * recorded, so every caller resets its sink afterwards. */
static void fpr_mint(cq_ctx *ctx, cq_bit *v, uint64_t value, int q)
{
    for (int i = 0; i < W64; i++) {
        v[i] = cq_bit_const((int)((value >> (unsigned)i) & UINT64_C(1)));
        if (q) cq_materialise(ctx, &v[i]);
    }
}

static uint32_t fpr_collect(const cq_scratch *scr, uint32_t *out, uint32_t n)
{
    for (uint32_t i = 0; i < scr->n; i++)
        if (cq_bit_is_qubit(scr->bits[i])) {
            if (n >= (uint32_t)FPR_NAMED_MAX)
                cq_h_fail(__FILE__, __LINE__, "FPR_NAMED_MAX is too small");
            else out[n++] = cq_bit_qindex(scr->bits[i]);
        }
    return n;
}

/* `q` is the MASK, one flag per input — risk R8's asymmetric witness is
 * inexpressible with a single mask, and here the asymmetric row is also
 * D-K23-7's: a CONSTANT `result_sign` is `fsqrt.jl:103`'s own call shape and a
 * constant `e` is `flog.jl:269`'s, and each collapses a different set of rows.
 *
 * `pad` is the canary region below the block's own offset. The block is placed
 * at `off = pad`, so a block that dropped `k->off` would run over the pad —
 * which is inert unless something there must not move (bd
 * sub-region-offset-needs-a-canary). The pad is materialised like the rest of
 * the region, so I6(b) holds for it too and the spans case can set it to |1>. */
static void fpr_open(fpr_fx *f, cq_fpround_id id, const uint64_t *v,
                     const int *q, uint32_t pad, const cq_sink *sink)
{
    cq_ctx_init(&f->ctx, sink);
    f->id  = id;
    f->pad = pad;
    f->pristine = cq_pc_take(&f->ctx);       /* taken BEFORE the mint */

    cq_scratch_alloc(&f->ops,  (uint32_t)(N_IN * W64));
    cq_scratch_alloc(&f->work, pad + fpr_region_of(id));

    for (int i = 0; i < N_IN; i++) {
        f->in[i] = cq_scratch_span(&f->ops, (uint32_t)(i * W64), (uint32_t)W64);
        fpr_mint(&f->ctx, f->in[i], v[i], q[i]);
    }
    for (uint32_t i = 0; i < f->work.n; i++) cq_materialise(&f->ctx, &f->work.bits[i]);

    f->n_named = fpr_collect(&f->ops,  f->named, 0u);
    f->n_named = fpr_collect(&f->work, f->named, f->n_named);
}

static void fpr_forward(fpr_fx *f)
{
    int n = fpr_steps_of(f->id);

    for (int u = 0; u < n; u++) fpr_step_at(f, f->pad, u);
}

static void fpr_reverse(fpr_fx *f)
{
    for (int u = fpr_steps_of(f->id) - 1; u >= 0; u--) fpr_step_at(f, f->pad, u);
}

/* THE SHADOW IS THE READER, and every lane of the region is determinate: the
 * surface is rotation-free, so cq_shadow_get is EXACT rather than conservative
 * (poolcheck.h). A lane may also be a CONSTANT — every lane of an assembled
 * view over a classical input is — so both kinds are read. `ok` carries the
 * unknown row out so a poisoned lane fails loudly instead of reading as 0. */
static uint64_t fpr_read(const cq_ctx *ctx, const cq_bit *v, int n, int *ok)
{
    uint64_t r = 0u;

    *ok = 1;
    for (int i = 0; i < n; i++) {
        if (cq_bit_is_qubit(v[i])) {
            cq_shadow sv = cq_shadow_get(&ctx->shadow, cq_bit_qindex(v[i]));

            if (sv.unknown) { *ok = 0; continue; }
            if (sv.value) r |= UINT64_C(1) << (unsigned)i;
        } else if (cq_bit_value(v[i])) {
            r |= UINT64_C(1) << (unsigned)i;
        }
    }
    return r;
}

/* Checked BEFORE releasing: cq_qubits_release aborts on an index not proven
 * |0>, and an abort reports as a crashed binary rather than as a failed case.
 * cq_sandwich's own release runs under CQ_ZERO_BY_PALINDROME, a literal 1,
 * which would launder exactly the fault these cases exist to see. */
static void fpr_reclaim(fpr_fx *f, cq_scratch *scr, const char *what)
{
    int clean = 1;

    for (uint32_t i = 0; i < scr->n; i++) {
        if (!cq_bit_is_qubit(scr->bits[i])) continue;
        if (!cq_shadow_known_zero(&f->ctx.shadow, cq_bit_qindex(scr->bits[i]))) {
            cq_h_fail(__FILE__, __LINE__,
                      "fpround %d: %s bit %u (q%u) is not back at |0> after the "
                      "reverse half", (int)f->id, what, i,
                      cq_bit_qindex(scr->bits[i]));
            clean = 0;
        }
    }
    if (!clean) return;

    for (uint32_t i = 0; i < scr->n; i++) {
        if (cq_bit_is_qubit(scr->bits[i]))
            cq_ctx_release_qubit(&f->ctx, cq_bit_qindex(scr->bits[i]), 1);
        scr->bits[i] = cq_bit_zero();
    }
    cq_scratch_dispose(scr);
}

static void fpr_close(fpr_fx *f)
{
    for (uint32_t i = 0; i < f->ops.n;  i++) f->ops.bits[i]  = cq_bit_zero();
    for (uint32_t i = 0; i < f->work.n; i++) f->work.bits[i] = cq_bit_zero();
    cq_scratch_dispose(&f->ops);
    cq_scratch_dispose(&f->work);
    cq_ctx_dispose(&f->ctx);
}

/* L2 AS A SET, SPELLED OVER RAW INDICES. cq_pc_live_is_exactly takes HANDLES,
 * and a block's operands and scratch are owned by no register — so the union
 * form has to be written here. It is the same claim at the same strength: the
 * named indices are distinct by construction (I2, I3), so "every named index
 * is live" PLUS "the live count equals the named count" is set equality. A
 * count on its own is strictly weaker, which is the measured
 * L2-masked-by-L3 finding. */
static void fpr_live_is_exactly(fpr_fx *f, uint32_t n, const char *when)
{
    for (uint32_t i = 0; i < n; i++)
        if (cq_qubits_is_free(&f->ctx.pool, f->named[i]))
            cq_h_fail(__FILE__, __LINE__, "L2 fpround %d %s: q%u is on the "
                      "free list", (int)f->id, when, f->named[i]);

    if (cq_qubits_live(&f->ctx.pool) != n)
        cq_h_fail(__FILE__, __LINE__, "L2 fpround %d %s: %u live, %u named — "
                  "an index is live that nothing owns", (int)f->id, when,
                  cq_qubits_live(&f->ctx.pool), n);
}

#include "test_kernel_fpround_anchors.inc"
#include "test_kernel_fpround_value.inc"
#include "test_kernel_fpround_slots.inc"
#include "test_kernel_fpround_spans.inc"
#include "test_kernel_fpround_golden.inc"

CQ_TEST_MAIN_ARGV(
    CQ_CASE(the_blocks_cost_what_their_modules_say_they_cost),
    CQ_CASE(each_helper_is_the_sum_of_its_rows),
    CQ_CASE(the_slot_boundaries_match_an_independent_four_valued_scan),
    CQ_CASE(the_row_tables_are_well_formed),
    CQ_CASE(every_span_of_every_row_is_pairwise_disjoint),
    CQ_CASE(the_blocks_are_palindromes_and_give_the_pool_back),
    CQ_CASE(two_blocks_in_one_region_do_not_collide),
    CQ_CASE(l1_against_the_ieee_reference_model),
    CQ_CASE(the_classical_row_agrees_with_the_ieee_reference_model),
    CQ_CASE(the_grs_and_bit52_handoff_encodings_are_what_the_source_says),
    CQ_CASE(a_signed_compare_swapped_to_unsigned_goes_red_here),
    CQ_CASE(every_anchor_reaches_its_block_and_makes_its_predicate_true),
    CQ_CASE(l4_goldens),
    CQ_CASE(the_caller_shaped_mask_is_cheaper_and_the_alphabet_is_x_cx_ccx)
)
