/* shim/cq_shim_reduce.c — the PORTED reduction. See cq_shim_reduce.h for the
 * pin it is ported from, the five things it keeps exactly, and the five places
 * it deliberately diverges. Every divergence is toward UNPROVEN. */

#include "cq_shim_reduce.h"

#include <string.h>

#include "reg.h"

#define R(i) (1u << (i))
#define MAXW  256u    /* writes considered in one window                     */
#define MAXD   24u    /* recursion bound — see the header's divergence (e)   */

static uint32_t g_depth;
static uint32_t g_declines;

uint32_t cq_reduce_depth_declines(void) { return g_declines; }
void     cq_reduce_reset_stats(void)    { g_declines = 0; g_depth = 0; }

static int reduce(int32_t h, uint32_t lo, uint32_t hi);

/* --- adjoint_matches ----------------------------------------------------- */

/* THE PARITY TEST COMES FIRST AND IS NOT AN OPTIMISATION. The same gate on the
 * COMPLEMENTARY branch of its flag is NOT the adjoint — composing the two is a
 * full, unconditional operation — so a differing parity must REFUSE the pair
 * even though every other field matches. The same vector then ADMITS a
 * commutation in `commutes`, which is why it is frozen at push time. */
static int adjoint_matches(const cq_call_rec *a, const cq_call_rec *b)
{
    const cq_reff *e = cq_rec_effect(a->op);

    if (memcmp(a->xpar, b->xpar, sizeof a->xpar) != 0) return 0;
    if (a->ctrl != b->ctrl) return 0;   /* §9 context — D15 §6(i) */

    if (e->twin) {
        if (b->op != e->twin_of) return 0;
        return a->tag == b->tag &&
               memcmp(a->h, b->h, sizeof a->h) == 0;
    }
    if (a->op != b->op) return 0;
    if (memcmp(a->h, b->h, sizeof a->h) != 0) return 0;

    /* ANGLES COMPARE BITWISE, NEVER WITH `==`, which is this project's standing
     * convention (mock_sink.h) and is a real divergence from upstream: it
     * parses to a float, so it calls (0.0, -0.0) a negation. Here the negation
     * is the SIGN BIT flipped and nothing else, so a NaN never pairs with
     * anything and 0.0 never pairs with -0.0. */
    if (e->neg_angle) return a->angle == (b->angle ^ (uint64_t)1u << 63);
    if (e->neg_imm)   return a->imm   == (uint64_t)(-(int64_t)b->imm);
    if (e->self_adjoint) return a->angle == b->angle && a->imm == b->imm;
    return 0;
}

/* --- commutes ------------------------------------------------------------ */

/* COMPLEMENTARY FLAG BRANCHES AND NOTHING ELSE. An ACCEPT arm: dropping it
 * rejects a correct build rather than admitting a wrong one. */
static int commutes(uint32_t up, const cq_call_rec *u, uint32_t wp, const cq_call_rec *w)
{
    const cq_reff *eu = cq_rec_effect(u->op);
    const cq_reff *ew = cq_rec_effect(w->op);

    for (uint32_t i = 0; i < 3u; i++) {
        if (!(eu->controls & R(i)) || u->h[i] < 0) continue;
        for (uint32_t j = 0; j < 3u; j++) {
            uint32_t lo, hi, n, k;
            uint32_t pos[MAXW];
            if (!(ew->controls & R(j)) || w->h[j] != u->h[i]) continue;
            if (u->xpar[i] == w->xpar[j]) continue;   /* same branch */

            /* NEVER MINTED HERE: the register's write history is not this
             * stream's to know, so the parity claim is backed by nothing.
             * DECLINE rather than assume — upstream's own wording. */
            if (!cq_rec_hist(u->h[i])) continue;

            lo = up < wp ? up : wp;
            hi = up < wp ? wp : up;
            n  = cq_rec_writes_in(u->h[i], lo, hi, pos, MAXW);
            if (n > MAXW) continue;                   /* refuse, never truncate */
            for (k = 0; k < n; k++)
                if (cq_rec_at(pos[k])->op != CQ_ROP_X) break;
            if (k == n) return 1;
        }
    }
    return 0;
}

/* --- pair_operands_unchanged --------------------------------------------- */

/* EVERYTHING THE PAIR'S ACTION DEPENDS ON MUST HAVE HELD STILL IN BETWEEN.
 * Three operand classes, and the THIRD is the 2026-08-07 miscompile fix: until
 * upstream added it, `cswap(f,q,rail); ry(q,0.75); cswap(f,q,rail); free(rail)`
 * reported reduces-to-identity at exit 0 while the route-back parked
 * `Ry(0.75)|0>` on the rail it then freed. The CO-WRITTEN slot is checked
 * because `cqrt_cswap` is the one opcode in the model that writes two rails.
 *
 * THE `v == target` SKIP IS AN ACCEPT ARM, NOT AN OPTIMISATION: it is what
 * keeps this check from taking its own conclusion as a premise. */
static int pair_operands_unchanged(const cq_call_rec *c1, uint32_t p1, uint32_t p2,
                                   int32_t target)
{
    const cq_reff *e = cq_rec_effect(c1->op);

    /* (i) declared READS, and (ii) the classical-immediate slots. Divergence
     * (c): the imm slots need no `provably_not_a_rail` evidence here because
     * the ABI DECLARES them classical and the shim consumes them
     * arithmetically. They are still LISTED, so that a future opcode whose
     * immediate is a handle cannot slip through the same door. */
    for (uint32_t i = 0; i < 3u; i++) {
        if (!(e->reads & R(i)) || c1->h[i] < 0) continue;
        if (!cq_rec_hist(c1->h[i])) continue;
        if (!reduce(c1->h[i], p1, p2)) return 0;
    }
    /* The §9 control flag is a read the operand slots do not carry. */
    if (c1->ctrl >= 0 && cq_rec_hist(c1->ctrl) && !reduce(c1->ctrl, p1, p2))
        return 0;

    /* (iii) CO-WRITTEN slots — every write slot that is not `target`. It is
     * `unchanged_over` with ONE extra admission: an intervening write that
     * provably acts on the COMPLEMENTARY branch of a flag this pair also
     * carries is DROPPED before the history is reduced, because two operators
     * supported on complementary blocks of one flag commute exactly.
     *
     * THE SOUNDNESS SPLITS THE WINDOW IN TWO AND ONLY ONE HALF IS CHECKED
     * HERE. (p1, w) is this `commutes` call. (w, p2) is discharged by the READS
     * loop above, which has already demanded the flag be unchanged across the
     * WHOLE window — and that step is why `controls ⊆ reads` is a
     * `_Static_assert` in the record rather than a convention.
     *
     * `v == target` IS AN ACCEPT ARM AND IT HAS NO KILLER AT THE v1 OP SURFACE.
     * Reported as SURVIVED by two battery rounds, and it is recorded here
     * rather than "fixed", on this project's idiom for an equivalent mutant.
     * Upstream kill-tests it with a committed positive fixture and gives two
     * non-redundancy grounds, both of which need a write to the TARGET inside
     * the pair's window that the two checks judge DIFFERENTLY — the caller
     * filters on the interval (w, p2) against the LATER pair member, this path
     * on (p1, w) against the EARLIER one. Reaching that needs a controlled
     * write to the target and a non-`cqrt_x` write to its flag on exactly one
     * side of it, and the only opcode that reaches `co_written_stable` at all
     * is `cqrt_cswap` (every other row's single write slot IS the target),
     * which CQ_lang emits with an `icmp` flag and never nests.
     *
     * IT STAYS BECAUSE ITS FAILURE DIRECTION IS THE SAFE ONE AND ITS ABSENCE
     * WOULD BE UNSOUND LATER. Removing it makes the engine DECLINE more —
     * `pair_operands_unchanged` would take its own conclusion as a premise and
     * reject pairs it should accept — which costs strands, not releases. But
     * upstream's second ground is a real hazard the moment a second two-write
     * opcode is modelled (v2's `qram_store` family is the obvious one), and by
     * then the line would look like dead code. Do not delete it because a
     * battery calls it equivalent; the scope of that verdict is the v1 opcode
     * table, and it is written down here so the next reader knows that. */
    for (uint32_t i = 0; i < 3u; i++) {
        int32_t v = c1->h[i];
        uint32_t pos[MAXW], n, kept = 0;
        if (!(e->writes & R(i)) || v < 0 || v == target) continue;
        if (!cq_rec_hist(v)) continue;
        n = cq_rec_writes_in(v, p1, p2, pos, MAXW);
        if (n > MAXW) return 0;
        for (uint32_t k = 0; k < n; k++)
            if (!commutes(pos[k], cq_rec_at(pos[k]), p1, c1)) kept++;
        if (kept && !reduce(v, p1, p2)) return 0;
    }
    return 1;
}

/* --- reduces_to_identity ------------------------------------------------- */

static int reduce(int32_t h, uint32_t lo, uint32_t hi)
{
    uint32_t all[MAXW], stack[MAXW], ns = 0, n;

    /* THE DEPTH BOUND, divergence (e). Upstream refuses to over-decline by
     * budget on principle and memoises instead; the port bounds and COUNTS the
     * decline so it is visible rather than silent. Reaching it answers "not
     * provably", which is the safe direction. */
    if (g_depth >= MAXD) { g_declines++; return 0; }
    g_depth++;

    n = cq_rec_writes_in(h, lo, hi, all, MAXW);
    if (n > MAXW) { g_depth--; return 0; }   /* refuse, never truncate */

    for (uint32_t i = 0; i < n; i++) {
        const cq_call_rec *c = cq_rec_at(all[i]);
        int placed = 0;

        /* TOP-DOWN: nearest unpaired adjoint first.
         *
         * REVERSING IT TO BOTTOM-UP IS A MEASURED EQUIVALENT ON EVERY STREAM IN
         * THE SUITE, and it is recorded rather than "covered". The direction is
         * about DETERMINISM and about reducing nested histories innermost-first,
         * not about the answer: each accepted cancellation is individually a
         * sound operator rewrite, so a different pairing CHOICE can only change
         * which decline you get, never turn a decline into an accept. Two
         * candidates on the stack that both adjoint-match AND both pass the
         * operand check are what would separate the orders, and the v1 opcode
         * table cannot produce one — the fixture would need three writes with
         * identical operands and identical parities where only the middle one
         * has moved sources. Keep the top-down scan: it is upstream's, it is
         * what makes an A B B A reduce innermost-first, and its cost is nil. */
        for (int k = (int)ns - 1; k >= 0; k--) {
            const cq_call_rec *ck = cq_rec_at(stack[(uint32_t)k]);
            uint32_t m;
            int ok = 1;

            if (!adjoint_matches(ck, c)) continue;

            /* Every STILL-UNPAIRED write above k must commute with the pair.
             * Writes below k are irrelevant; already-cancelled ones were
             * replaced by the identity and are gone. */
            for (m = (uint32_t)k + 1u; m < ns; m++)
                if (!commutes(stack[m], cq_rec_at(stack[m]), all[i], c))
                    { ok = 0; break; }
            if (!ok) continue;

            if (!pair_operands_unchanged(ck, stack[(uint32_t)k], all[i], h))
                continue;   /* REJECTED, but keep scanning — never `break` */
            /* `continue` RATHER THAN `break` IS AN ACCEPT ARM WITH NO KILLER AT
             * THE v1 OP SURFACE, reported SURVIVED by two rounds and recorded
             * here on this project's idiom for an equivalent mutant. A deeper
             * adjoint can only be reached if EVERY still-unpaired write above it
             * commutes with the pair, and commutation is limited to
             * complementary flag branches — so the stream that separates the
             * two spellings needs a rejected shallow candidate, a viable deep
             * one, AND the rejected one to sit on the opposite branch of a flag
             * the pair also carries. `cqrt_cswap` is the only opcode that can be
             * rejected on its co-written slot, CQ_lang emits it with an `icmp`
             * flag and never nests, so the shape is not constructible from the
             * entry points and only barely from the record.
             *
             * IT STAYS BECAUSE ITS FAILURE DIRECTION IS THE SAFE ONE and
             * because it is upstream's, whose corpus DOES contain the shape:
             * `break` declines where `continue` accepts, which costs strands
             * rather than releases. Do not "simplify" it on a battery's word. */

            memmove(&stack[k], &stack[k + 1],
                    (ns - (uint32_t)k - 1u) * sizeof stack[0]);
            ns--;
            placed = 1;
            break;          /* GREEDY COMMIT — no backtracking */
        }
        if (!placed) stack[ns++] = all[i];
    }

    g_depth--;
    return ns == 0u;        /* an unpaired write is a DECLINE */
}

int cq_reduce_to_identity(int32_t h, uint32_t lo, uint32_t hi)
{
    /* The tripwire's outermost frame: depth is per top-level query, so a stale
     * non-zero here would mean a previous query unwound abnormally. */
    g_depth = 0;
    return reduce(h, lo, hi);
}
