/* tests/test_kernel_addacc_death.c — M15, Step 15. K8's fail-loud paths.
 *
 * THIS SUITE CARRIES WHAT L5 CARRIES ELSEWHERE. Every other kernel has an
 * all-classical row that folds to zero gates and zero qubits; K8 refuses one
 * instead, and refusing is the whole safety argument for a construction that
 * writes its own addend. So the cases below are not the usual defensive
 * garnish around a kernel — they ARE the level, and each is a miscompile that
 * is otherwise silent (right value, dirty scratch, green L1 and green L4):
 *
 *   - a classical `b[i]` is a gate TARGET, so the fold table materialises it
 *     mid-construction. If the constant was 1 that is an X on the forward pass
 *     that the reverse never emits, because kinds are monotone (D6) and the bit
 *     is already CQ_BIT_Q on the way back. Risk R1, exactly.
 *   - a classical ZERO `x` is read as a CONTROL at block A phase 1 and folds to
 *     nothing, then is materialised as a TARGET at block G phase 1. The reverse
 *     pass emits what the forward folded away — risk R8's antecedent, traced
 *     gate by gate at K11.md §2b.
 *   - `acc` overlapping `b` is two carry chains sharing wires. Both are
 *     TARGETS here, which is why the range test cannot be borrowed from
 *     cq_kernel_check_n: that one is dst-versus-sources and K8 has no dst.
 *
 * NO FAIL_REGULAR_EXPRESSION IS OWED ON ANY OF THESE, and the reason is the
 * question CLAUDE.md says to ask first — which single case goes red if this
 * exact line is deleted? For all seven, nothing beneath M15 speaks. M05's §3
 * distinctness assert compares qubit INDICES, so it is silent on a classical
 * operand and silent on two disjoint registers; M09 is not in the picture,
 * because these calls are not inside a sandwich; and M07 never sees a cq_bit
 * array. Delete cq_addacc_check and the classical cases return a WRONG CIRCUIT
 * with a right value in both configurations, which is the signature this file
 * exists to make loud.
 *
 * Every case is a hard error in BOTH configurations, deliberately: risk R2's
 * whole value is firing during the Step 24 fixture run, which Rule 17 pins
 * under Release, where a Debug-gated assert is simply absent.
 */

#include "kernels/addacc.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "reg.h"
#include "sink_count.h"

#include "support/bitkinds.h"
#include "support/death.h"
#include "support/refmodel.h"

/* A context whose gates go to a counter — these cases abort, so nothing is ever
 * read back, but a sink must exist and the printf default would spew. */
typedef struct {
    cq_ctx     ctx;
    cq_counter cnt;
    cq_sink    sink;
    int32_t    h_acc, h_b, h_x;
    cq_addacc_block k;
} d_fix;

/* `qa`, `qb`, `qx` are the quantum masks, so a case can make exactly one bit
 * classical and leave the rest legal. */
static void d_open(d_fix *f, int W, cq_ref_w qa, cq_ref_w qb, cq_ref_w qx)
{
    cq_count_reset(&f->cnt);
    f->sink = cq_sink_counter(&f->cnt);
    cq_ctx_init(&f->ctx, &f->sink);

    f->h_acc = cq_bk_reg_w(&f->ctx, (uint32_t)W, cq_ref_w_make(3u, 0u, W), qa);
    f->h_b   = cq_bk_reg_w(&f->ctx, (uint32_t)W, cq_ref_w_make(5u, 0u, W), qb);
    f->h_x   = cq_bk_reg_w(&f->ctx, 1u, cq_ref_w_zero(), qx);

    f->k.acc = cq_reg_bits(&f->ctx.regs, f->h_acc);
    f->k.b   = cq_reg_bits(&f->ctx.regs, f->h_b);
    f->k.x   = cq_reg_bits(&f->ctx.regs, f->h_x);
    f->k.W   = W;
}

/* The three kind refusals. Each leaves every OTHER bit quantum, so the case
 * fails for the reason it is named for and not because the whole call is
 * malformed — and each uses a different lane, since an implementation that
 * checked only lane 0 would pass a lane-0-only suite. */
static void classical_acc_bit(void)
{
    d_fix f;
    int W = 8;

    d_open(&f, W, cq_ref_w_andnot(cq_ref_w_ones(W), cq_ref_w_setbit(5)),
           cq_ref_w_ones(W), cq_ref_w_ones(1));
    CQ_EXPECT_ABORT(cq_kernel_addacc(&f.ctx, &f.k));
}

static void classical_addend_bit(void)
{
    d_fix f;
    int W = 8;

    d_open(&f, W, cq_ref_w_ones(W),
           cq_ref_w_andnot(cq_ref_w_ones(W), cq_ref_w_setbit(3)),
           cq_ref_w_ones(1));
    CQ_EXPECT_ABORT(cq_kernel_addacc(&f.ctx, &f.k));
}

/* THE R8 CASE. A ZERO `x` is legal-looking in every way a value test can see:
 * it holds the right value, and K8 would still compute the right sum. What it
 * costs is the forward/reverse symmetry, and only inside a sandwich — so
 * nothing here could catch it after the fact. It is refused at entry instead. */
static void classical_ancilla(void)
{
    d_fix f;
    int W = 8;

    d_open(&f, W, cq_ref_w_ones(W), cq_ref_w_ones(W), cq_ref_w_zero());
    CQ_EXPECT_ABORT(cq_kernel_addacc(&f.ctx, &f.k));
}

/* AT W=1 TOO, WHERE NO GATE TOUCHES THE ANCILLA. The precondition is uniform
 * on purpose (K11 carves an x[j] per call at every width), and a W-dependent
 * one is an edge M18 would have to remember. */
static void classical_ancilla_at_width_one(void)
{
    d_fix f;

    d_open(&f, 1, cq_ref_w_ones(1), cq_ref_w_ones(1), cq_ref_w_zero());
    CQ_EXPECT_ABORT(cq_kernel_addacc(&f.ctx, &f.k));
}

/* RANGES, NOT BASE POINTERS. `&acc[2]` is a different pointer from `acc`, so a
 * base-pointer comparison passes and lanes 2..W-1 of the accumulator are lanes
 * 0..W-3 of the addend — the shape a K11 scratch-layout off-by-one takes,
 * since cq_scratch_span hands out sub-arrays of one region. */
static void acc_overlaps_the_addend(void)
{
    d_fix f;
    int W = 8;

    d_open(&f, W, cq_ref_w_ones(W), cq_ref_w_ones(W), cq_ref_w_ones(1));
    f.k.b = &cq_reg_bits(&f.ctx.regs, f.h_acc)[2];
    f.k.W = 4;
    CQ_EXPECT_ABORT(cq_kernel_addacc(&f.ctx, &f.k));
}

/* The ancilla carved out of the accumulator's own storage — the same mistake
 * one lane wide, and the one a `x = &region[2*W]` off-by-one produces. */
static void the_ancilla_is_inside_the_accumulator(void)
{
    d_fix f;
    int W = 8;

    d_open(&f, W, cq_ref_w_ones(W), cq_ref_w_ones(W), cq_ref_w_ones(1));
    f.k.x = &cq_reg_bits(&f.ctx.regs, f.h_acc)[W - 1];
    CQ_EXPECT_ABORT(cq_kernel_addacc(&f.ctx, &f.k));
}

static void a_width_of_zero(void)
{
    d_fix f;

    d_open(&f, 4, cq_ref_w_ones(4), cq_ref_w_ones(4), cq_ref_w_ones(1));
    f.k.W = 0;
    CQ_EXPECT_ABORT(cq_kernel_addacc(&f.ctx, &f.k));
}

/* WHICH LAYER ABORTED, and this case was FILED BY A MUTATION BATTERY rather
 * than written from a checklist. `cq_addacc_check`'s own width guard survived
 * mutation to always-true, because both entry points call `cq_addacc_steps`
 * first and its identical guard fires one layer up — the Step 7/8 finding with
 * the masking copy EARLIER in the call chain rather than later. The guard is
 * still wanted: K11 calls cq_addacc_check directly, once per accumulate, and
 * that path passes through no other check. So this drives it directly, and the
 * two messages were made disjoint so tests/CMakeLists.txt can pin which one
 * spoke. Delete either guard and exactly one of these two cases goes red. */
static void check_called_directly_with_a_zero_width(void)
{
    d_fix f;

    d_open(&f, 4, cq_ref_w_ones(4), cq_ref_w_ones(4), cq_ref_w_ones(1));
    f.k.W = 0;
    CQ_EXPECT_ABORT(cq_addacc_check(&f.k));
}

/* The step index is the caller's arithmetic and K11 will compute it from an
 * outer loop variable, so an off-by-one there is a live hazard rather than a
 * theoretical one. Out of range in both directions. */
static void a_step_index_past_the_end(void)
{
    d_fix f;
    int W = 8;

    d_open(&f, W, cq_ref_w_ones(W), cq_ref_w_ones(W), cq_ref_w_ones(1));
    CQ_EXPECT_ABORT(cq_addacc_step(&f.ctx, &f.k, cq_addacc_steps(W)));
}

static void a_negative_step_index(void)
{
    d_fix f;
    int W = 8;

    d_open(&f, W, cq_ref_w_ones(W), cq_ref_w_ones(W), cq_ref_w_ones(1));
    CQ_EXPECT_ABORT(cq_addacc_step(&f.ctx, &f.k, -1));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(classical_acc_bit),
    CQ_DEATH_CASE(classical_addend_bit),
    CQ_DEATH_CASE(classical_ancilla),
    CQ_DEATH_CASE(classical_ancilla_at_width_one),
    CQ_DEATH_CASE(acc_overlaps_the_addend),
    CQ_DEATH_CASE(the_ancilla_is_inside_the_accumulator),
    CQ_DEATH_CASE(a_width_of_zero),
    CQ_DEATH_CASE(check_called_directly_with_a_zero_width),
    CQ_DEATH_CASE(a_step_index_past_the_end),
    CQ_DEATH_CASE(a_negative_step_index)
)
