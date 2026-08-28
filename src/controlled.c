/* src/controlled.c — M06. PRD §9's promotion, and nothing else.
 *
 * Read src/controlled.h before changing anything here. In particular: row 0 is
 * the reason every zero-cost claim in the PRD survives this axis; the ordering
 * rule (fold on controls first, promote before folding on the target) is the
 * whole correctness of the module; `bd skh` resolves as UNPROMOTED and that is
 * forced by an algebra written out in the header; and the coincidence refusal
 * is v1 declining a well-defined collapse rather than failing to know it.
 */

#include "controlled.h"

#include "ctx.h"
#include "emit.h"
#include "shadow.h"
#include "sink.h"

#include <stdio.h>
#include <stdlib.h>

static void cq_ctrl_die(const char *what)
{
    fprintf(stderr, "libcqops: FATAL: controlled: %s\n", what);
    abort();
}

/* THE SECOND STRUCTURAL `proven_zero` CONSTANT IN src/, and the first since
 * M09's CQ_ZERO_BY_PALINDROME. Like that one it is a literal 1 whose whole
 * content is the argument beside it, and like that one, deleting a premise
 * turns it into a laundering site (PRD §10).
 *
 * IT MAY NOT BE cq_shadow_known_zero, AND THE REASON IS NOT A DETAIL. The
 * shadow's CCX rule is `t.unknown |= a.unknown | b.unknown` with no clearing
 * path, so under a POISONED control wire the promoted block leaves the ancilla
 * at {value 0, unknown 1} and a shadow check would abort the release — in both
 * configurations, on a qubit that is provably |0>. The trap is that on the
 * surface Step 20's own gate exercises, every wire is determinate (nothing
 * rotates in a kernel suite), so a shadow check PASSES there and fails only
 * later, on the corpus. tests/test_controlled_death.c drives the poisoned case
 * on purpose for exactly that reason.
 *
 * THE THREE PREMISES, for both the shared Toffoli ancilla and the nested AND
 * flag. (1) Each is targeted ONLY by an identical PAIR of Toffolis — gates 1
 * and 3 of one promoted block, or the AND at push and its twin at pop — and a
 * CCX is its own inverse, so the wire returns to |0> within the pair. (2)
 * Nothing else can target either: they live in no register and in no scratch
 * region, this file is their only writer, and the coincidence refusal below is
 * what stops a promoted gate reading a control that gate 2 has already flipped
 * — the one way gate 3 could fail to undo gate 1. (3) They come off the pool,
 * so they are born |0> by I3. */
#define CQ_ZERO_BY_CTRL_UNCOMPUTE 1

/* --- The stack. ----------------------------------------------------------- */

void cq_ctrl_stack_init(cq_ctrl_stack *s)
{
    s->f = NULL;
    s->n = 0;
    s->cap = 0;
}

void cq_ctrl_stack_dispose(cq_ctrl_stack *s)
{
    if (s->n != 0)
        cq_ctrl_die("a control region was still open at dispose — every "
                    "cq_ctrl_push owes a cq_ctrl_pop, and the flag qubits of "
                    "an unclosed region are still live");
    free(s->f);
    cq_ctrl_stack_init(s);
}

/* Growable rather than a fixed depth. A cap would be a hidden ceiling on a
 * library entry point, which src/rotate.h already argues is a defect whether or
 * not today's shim can reach it — and here the shim cannot reach depth 2 at
 * all, so a cap would be untestable as well as arbitrary. */
static cq_ctrl_frame *push_slot(cq_ctrl_stack *s)
{
    if (s->n == s->cap) {
        int cap = s->cap ? s->cap * 2 : 4;
        cq_ctrl_frame *f = realloc(s->f, (size_t)cap * sizeof *f);
        if (!f) cq_ctrl_die("out of memory growing the control stack");
        s->f = f;
        s->cap = cap;
    }
    return &s->f[s->n++];
}

int cq_ctrl_depth(const cq_ctx *ctx) { return ctx->ctrl.n; }

/* --- Row 0, and the nested AND. ------------------------------------------- */

void cq_ctrl_push(cq_ctx *ctx, const cq_bit *ctrl)
{
    /* Both configurations. A push inside a compute half would emit the AND's
     * Toffoli at an index the reverse replay never revisits, so the sandwich
     * would stop cancelling while the recorded stream stayed a palindrome —
     * risk R1 with every detector blind, which is why M22 carries the identical
     * guard for the identical reason. */
    if (ctx->sandwich_depth != 0)
        cq_ctrl_die("cq_ctrl_push inside a sandwich compute half: the axis is "
                    "an emitter mode and no kernel may open a region (Rule 9)");

    /* THE PREVIOUS FRAME IS READ BY VALUE, BEFORE push_slot RUNS, AND THAT IS
     * NOT STYLE. push_slot reallocs the frame array when the stack grows, so a
     * `cq_ctrl_frame *prev` taken before it dangles from the fifth push onward
     * and every row-0 decision below would read freed memory. Nothing in the
     * suite would see it: Debug was UBSan-only when this was written (bd 6wg --
     * the Apple clang ASan runtime SIGILLs before main), and CQ_lang cannot
     * express depth 2 at all, let alone 5. Two scalars copied out is the whole
     * fix, and it makes the hazard unrepresentable rather than merely absent.
     *
     * Debug now selects an ASan-capable compiler (bd 6wg, 2026-08-27) and
     * re-applying the pointer form is reported as a heap-use-after-free -- but
     * only because a case reaching depth 6 exists to run it. The sanitizer needs
     * the test; the test came first. */
    const int    have_prev = ctx->ctrl.n > 0;
    const int    prev_mode = have_prev ? ctx->ctrl.f[ctx->ctrl.n - 1].mode
                                       : CQ_CTRL_OFF;
    const cq_bit prev_wire = have_prev ? ctx->ctrl.f[ctx->ctrl.n - 1].wire
                                       : cq_bit_zero();

    cq_ctrl_frame *f = push_slot(&ctx->ctrl);

    f->owns_wire = 0;
    f->has_anc   = 0;
    f->wire      = cq_bit_zero();
    f->anc       = cq_bit_zero();
    f->and_a     = cq_bit_zero();
    f->and_b     = cq_bit_zero();

    /* Row 0, top to bottom. An outer SKIP swallows everything inside it — a
     * region that does not run cannot contain one that does. */
    if (have_prev && prev_mode == CQ_CTRL_SKIP) { f->mode = CQ_CTRL_SKIP; return; }

    if (cq_bit_is_zero(*ctrl)) { f->mode = CQ_CTRL_SKIP; return; }

    if (cq_bit_is_one(*ctrl)) {
        /* "Emitted uncontrolled, verbatim" means: whatever the enclosing region
         * already was. A ONE control adds no constraint, so it inherits — it
         * does NOT reset an outer quantum control to OFF. */
        f->mode = prev_mode;
        if (have_prev) f->wire = prev_wire;
        return;
    }

    /* CQ_BIT_Q from here. */
    f->mode = CQ_CTRL_PROMOTE;

    if (!have_prev || prev_mode == CQ_CTRL_OFF) { f->wire = *ctrl; return; }

    /* Nested: AND the two flags into one wire so the promotion never sees more
     * than one control. Idempotent when the two ARE one wire — `q & q == q`, and
     * emitting CCX(q, q, anc) instead would be a malformed gate for a program as
     * ordinary as a doubly-nested branch on the same condition. */
    if (cq_bit_coincident(&prev_wire, ctrl)) { f->wire = prev_wire; return; }

    f->wire      = cq_bit_qubit(cq_ctx_fresh_qubit(ctx));
    f->owns_wire = 1;
    f->and_a     = prev_wire;
    f->and_b     = *ctrl;
    cq_emit_ccx_phys(ctx, &f->and_a, &f->and_b, &f->wire);
}

void cq_ctrl_pop(cq_ctx *ctx)
{
    if (ctx->ctrl.n == 0) cq_ctrl_die("cq_ctrl_pop with no region open");

    cq_ctrl_frame *f = &ctx->ctrl.f[--ctx->ctrl.n];

    if (f->has_anc)
        cq_ctx_release_qubit(ctx, cq_bit_qindex(f->anc),
                             CQ_ZERO_BY_CTRL_UNCOMPUTE);

    if (f->owns_wire) {
        /* The identical Toffoli, which is what makes the flag clean. It is
         * emitted with the frame ALREADY POPPED so that it is not promoted
         * against the wire it is uncomputing. */
        cq_emit_ccx_phys(ctx, &f->and_a, &f->and_b, &f->wire);
        cq_ctx_release_qubit(ctx, cq_bit_qindex(f->wire),
                             CQ_ZERO_BY_CTRL_UNCOMPUTE);
    }
}

/* --- The coincidence refusal. --------------------------------------------- */

/* `c1`/`c2` may be NULL; `t` never is.
 *
 * IT WALKS THE WHOLE STACK, NOT THE TOP WIRE, and that is not defensiveness —
 * the narrower version shipped for an hour and was a dirty-ancilla hazard. At
 * depth >= 2 the top frame's wire is a freshly minted AND FLAG, while the bits
 * the region actually depends on are the two the AND was computed from. A gate
 * that TARGETS one of those changes a control of the push Toffoli, so the twin
 * at pop no longer uncomputes the flag — and CQ_ZERO_BY_CTRL_UNCOMPUTE then
 * launders a demonstrably non-|0> qubit onto the free list, which is the one
 * unforgivable bug in this project. Nothing else could see it: the flag belongs
 * to no register, so no L2 set check names it, and its shadow is poisoned
 * exactly when the control wire is.
 *
 * WALKING EACH FRAME'S `wire` ALONE IS NOT ENOUGH. At depth 2 that set is
 * {outer wire, flag} and misses the INNER pushed control, which after the push
 * lives only in `and_b`. `and_a` is by construction the enclosing frame's wire
 * and so is covered twice; it is compared anyway, because relying on that
 * identity would make this guard depend on a fact push happens to maintain.
 *
 * See the header for why the target case and the control case are different
 * facts with the same disposition in v1. */
static void check_one(const cq_bit *w, const cq_bit *c1, const cq_bit *c2,
                      const cq_bit *t)
{
    if (cq_bit_coincident(w, t))
        cq_ctrl_die("a gate inside a controlled region TARGETS a wire the "
                    "region's control depends on: `if (q) q ^= …` is not "
                    "injective, and at depth >= 2 it also stops the AND flag "
                    "from uncomputing, so pop would release it dirty (PRD §9)");

    if ((c1 && cq_bit_coincident(w, c1)) || (c2 && cq_bit_coincident(w, c2)))
        cq_ctrl_die("the control wire is also a control of the gate it "
                    "controls: `q & q == q` makes this well defined and v1 "
                    "REFUSES it rather than collapsing silently — measured 0 "
                    "occurrences in CQ_lang's 239 goldens, arithmetic in "
                    "PRD §9");
}

static void check_operands(const cq_ctx *ctx, const cq_bit *c1,
                           const cq_bit *c2, const cq_bit *t)
{
    for (int i = 0; i < ctx->ctrl.n; i++) {
        const cq_ctrl_frame *f = &ctx->ctrl.f[i];

        if (f->mode != CQ_CTRL_PROMOTE) continue;

        check_one(&f->wire, c1, c2, t);
        if (f->owns_wire) {
            check_one(&f->and_a, c1, c2, t);
            check_one(&f->and_b, c1, c2, t);
        }
    }
}

/* --- §9's promotion table. ------------------------------------------------ */

void cq_ctrl_promote_x(cq_ctx *ctx, cq_bit *t)
{
    const cq_bit *w = cq_ctrl_wire(&ctx->ctrl);

    check_operands(ctx, NULL, NULL, t);
    cq_emit_cx_phys(ctx, w, t);            /* NOT → CNOT */
}

void cq_ctrl_promote_cx(cq_ctx *ctx, const cq_bit *c, cq_bit *t)
{
    const cq_bit *w = cq_ctrl_wire(&ctx->ctrl);

    check_operands(ctx, c, NULL, t);
    cq_emit_ccx_phys(ctx, w, c, t);        /* CNOT → Toffoli */
}

void cq_ctrl_promote_ccx(cq_ctx *ctx, const cq_bit *c1, const cq_bit *c2,
                         cq_bit *t)
{
    cq_ctrl_frame *f = &ctx->ctrl.f[ctx->ctrl.n - 1];
    const cq_bit  *w = &f->wire;

    check_operands(ctx, c1, c2, t);

    if (!f->has_anc) {                     /* lazily, exactly like upstream */
        f->anc = cq_bit_qubit(cq_ctx_fresh_qubit(ctx));
        f->has_anc = 1;
    }

    /* Toffoli → three Toffolis and one reusable ancilla, verbatim from
     * third_party/bennett/src/controlled.jl's promote_gate!. THE BLOCK IS A
     * PALINDROME OF SELF-INVERSE GATES, hence an involution — `(ABA)² = I` —
     * which is what lets cq_sandwich keep replaying steps in reverse and keeps
     * cq_mock_is_palindrome green through the promotion. */
    cq_emit_ccx_phys(ctx, w, c1, &f->anc);          /* anc = ctrl ∧ c1     */
    cq_emit_ccx_phys(ctx, &f->anc, c2, t);          /* t ⊕= anc ∧ c2       */
    cq_emit_ccx_phys(ctx, w, c1, &f->anc);          /* uncompute anc       */
}

/* --- §7's rotations under §9. --------------------------------------------- */

/* The two CXs go STRAIGHT TO THE SINK rather than through cq_emit_cx_phys, and
 * the header says why: they cancel, so the composite moves no basis value, and
 * updating the shadow for each in turn would propagate the control wire's
 * poison into a target that provably did not move. Skipping both is exact. */
static void ctrl_rot(cq_ctx *ctx, uint32_t q, double theta,
                     void (*sink_r)(const cq_sink *, uint32_t, double))
{
    const cq_bit *w = cq_ctrl_wire(&ctx->ctrl);
    const cq_bit  t = cq_bit_qubit(q);

    /* THETA GOES OUT BITWISE UNCHANGED when no region is open, and it is passed
     * whole rather than reconstructed from its half for exactly that reason.
     * `theta*0.5 + theta*0.5 == theta` for every NORMAL double — halving is an
     * exponent decrement and y + y is exact — but NOT in the subnormal range,
     * where the halving rounds: 3·DBL_TRUE_MIN round-trips to 4·DBL_TRUE_MIN.
     * Angles are compared bitwise everywhere here (mock_sink.h), and a
     * subnormal θ reaches §7's general row at tol = 0. */
    if (!w) { sink_r(ctx->sink, q, theta); return; }
    check_operands(ctx, NULL, NULL, &t);

    {
        const double half = theta * 0.5;

        sink_r(ctx->sink, q, half);
        cq_sink_cx(ctx->sink, cq_bit_qindex(*w), q);
        sink_r(ctx->sink, q, -half);
        cq_sink_cx(ctx->sink, cq_bit_qindex(*w), q);
    }
}

void cq_ctrl_ry(cq_ctx *ctx, uint32_t q, double theta)
{
    ctrl_rot(ctx, q, theta, cq_sink_ry);
}

void cq_ctrl_rz(cq_ctx *ctx, uint32_t q, double phi)
{
    ctrl_rot(ctx, q, phi, cq_sink_rz);
}

/* --- PRD §15 D11's refusals. ---------------------------------------------- */

void cq_ctrl_refuse_fold_row(const cq_ctx *ctx, const char *row)
{
    if (!cq_ctrl_wire(&ctx->ctrl)) return;

    fprintf(stderr,
            "libcqops: FATAL: controlled: PRD §15 D11 — the §7 row \"%s\" FOLDS, "
            "and a fold is wrong under a quantum control: controlled-(e^{ia}I) "
            "is Rz(a) on the control wire, per bit. v1 refuses rather than "
            "emitting a hand-derived phase it has no instrument to check. The "
            "corrections are tabulated in PRD §15 D11.\n", row);
    abort();
}

void cq_ctrl_refuse_measurement(const cq_ctx *ctx)
{
    const cq_ctrl_frame *t = cq_ctrl_top(&ctx->ctrl);

    /* OFF is legal and covers row 0's CQ_BIT_ONE control, where the region is
     * the uncontrolled one verbatim. */
    if (!t || t->mode == CQ_CTRL_OFF) return;

    cq_ctrl_die("a measurement inside a controlled region: `mz` is not a "
                "unitary, §8's vtable has no conditional entry, and CQ_lang "
                "declares no cqrt_measure_*_controlled at any width");
}
