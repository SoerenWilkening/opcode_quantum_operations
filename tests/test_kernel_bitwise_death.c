/* tests/test_kernel_bitwise_death.c — M10's fail-loud paths, Step 10.
 *
 * ALL OF THESE ABORT IN BOTH CONFIGURATIONS, and none carries
 * CQ_DEATH_SKIP_WITHOUT_INVARIANTS. D7a in particular must fire in Release
 * for M07's reason one level up (src/reg.h): risk R2's entire value is firing
 * during the Step 24 fixture run, which Rule 17 pins under Release, where a
 * Debug-gated assert is simply absent.
 *
 * WHICH LAYER ABORTED, not merely that something did — the Step 6/7/8 lesson
 * for the fourth time. `dst == a` is guarded twice over: by
 * cq_kernel_check_dst here, and, one level down and only for a QUANTUM lane
 * and only in Debug, by M05's §3 distinctness assert. tests/CMakeLists.txt
 * therefore pins the expected message with FAIL_REGULAR_EXPRESSION, so
 * deleting the kernel-level check turns these red instead of leaving them
 * green on M05's backstop.
 *
 * THE CASE THAT ONLY THIS GUARD CAN CATCH is dst_aliases_a_classical_source:
 * with `a` all-constant there is nothing for M05 to compare, the fold table
 * folds happily, and the kernel returns a WRONG ANSWER IN SILENCE in both
 * configurations. That is the case the guard exists for.
 */

#include "kernels/bitwise.h"

#include "bit.h"
#include "ctx.h"
#include "reg.h"
#include "support/bitkinds.h"
#include "support/death.h"

static cq_sink g_sink;
static cq_ctx  g_ctx;

/* A sink that accepts everything and records nothing: these cases are about
 * the kernel's preconditions, not about what reached the sink. */
static void nx  (void *u, uint32_t q)                        { (void)u; (void)q; }
static void ncx (void *u, uint32_t c, uint32_t t)            { (void)u; (void)c; (void)t; }
static void nccx(void *u, uint32_t a, uint32_t b, uint32_t t){ (void)u; (void)a; (void)b; (void)t; }
static void nry (void *u, uint32_t q, double th)             { (void)u; (void)q; (void)th; }
static void nrz (void *u, uint32_t q, double ph)             { (void)u; (void)q; (void)ph; }
static void nmz (void *u, uint32_t q)                        { (void)u; (void)q; }

static void setup(void)
{
    g_sink.x = nx; g_sink.cx = ncx; g_sink.ccx = nccx;
    g_sink.ry = nry; g_sink.rz = nrz; g_sink.mz = nmz;
    g_sink.user = NULL;
    cq_ctx_init(&g_ctx, &g_sink);
}

/* D7a with a quantum source. M05 would also catch this one, in Debug only —
 * which is exactly why the CMake property names the kernel's message. */
static void dst_aliases_a_quantum_source(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 4u, 0xFu, 0xFu);
    int32_t hb = cq_bk_reg(&g_ctx, 4u, 0x5u, 0xFu);

    cq_bit       *bits = cq_reg_bits (&g_ctx.regs, ha);
    const cq_bit *b    = cq_reg_cbits(&g_ctx.regs, hb);

    CQ_EXPECT_ABORT(cq_kernel_xor(&g_ctx, bits, bits, b, 4));
}

/* THE case with nothing beneath it. `a` is all-constant, so M05's distinctness
 * check — which compares qubit INDICES and cannot fire on constants (PRD §3,
 * src/bit.h) — has nothing to say, in either configuration. Delete
 * cq_kernel_check_dst and this call returns normally, having computed
 * something that is not dst ^= f(a,b). */
static void dst_aliases_a_classical_source(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 4u, 0xFu, 0x0u);
    int32_t hb = cq_bk_reg(&g_ctx, 4u, 0x5u, 0xFu);

    cq_bit       *bits = cq_reg_bits (&g_ctx.regs, ha);
    const cq_bit *b    = cq_reg_cbits(&g_ctx.regs, hb);

    CQ_EXPECT_ABORT(cq_kernel_xor(&g_ctx, bits, bits, b, 4));
}

/* The second source, because `dst == a || dst == b` is two clauses and one of
 * them can be deleted on its own. */
static void dst_aliases_the_second_source(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 4u, 0xFu, 0xFu);
    int32_t hb = cq_bk_reg(&g_ctx, 4u, 0x5u, 0x0u);

    const cq_bit *a    = cq_reg_cbits(&g_ctx.regs, ha);
    cq_bit       *bits = cq_reg_bits (&g_ctx.regs, hb);

    CQ_EXPECT_ABORT(cq_kernel_and(&g_ctx, bits, a, bits, 4));
}

/* W <= 0. Not a hypothetical: `int W` is PRD §4's signature verbatim, so a
 * signed width really can arrive, and every kernel's loop would simply not
 * run — returning an untouched dst and no error at all. */
static void a_width_of_zero(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 4u, 0xFu, 0xFu);
    int32_t hb = cq_bk_reg(&g_ctx, 4u, 0x5u, 0xFu);
    int32_t hd = cq_reg_alloc_zero(&g_ctx.regs, 4u);

    CQ_EXPECT_ABORT(cq_kernel_or(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                 cq_reg_cbits(&g_ctx.regs, ha),
                                 cq_reg_cbits(&g_ctx.regs, hb), 0));
}

/* THE CASE THE BASE-POINTER CHECK COULD NOT SEE, and the reason the guard now
 * compares RANGES. `dst` and `a` are different pointers into ONE register, so
 * `dst != a` and `dst != b` both hold — yet lanes 2-3 of dst are lanes 0-1 of
 * a. Measured before the fix, in both configurations: returned normally, wrong
 * answer, no diagnostic, because M05's distinctness check compares qubit
 * indices and every bit here is a constant. `cq_scratch_span` (src/scratch.h)
 * exists precisely to hand kernels sub-arrays of one region, and
 * kernels/bitwise.h says K9 and K12 will call these kernels that way — so this
 * is the sanctioned calling shape, not a contrived one. */
static void dst_overlaps_a_source_without_sharing_a_base(void)
{
    setup();
    int32_t hr = cq_bk_reg(&g_ctx, 8u, 0xFFu, 0x00u);
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x5u,  0x0Fu);

    cq_bit       *r = cq_reg_bits (&g_ctx.regs, hr);
    const cq_bit *b = cq_reg_cbits(&g_ctx.regs, hb);

    /* A ONE-ELEMENT overlap: dst = r[0..3], a = r[3..6]. Deliberately minimal,
     * because a two-element overlap does not discriminate — measured, an
     * off-by-one that shortened cq_kernel_overlap2's range by a single element
     * still caught the two-element case and survived the whole suite. */
    CQ_EXPECT_ABORT(cq_kernel_xor(&g_ctx, &r[0], &r[3], b, 4));
}

/* D7b AT THE KERNEL BOUNDARY. It is LEGAL at the handle boundary — 599
 * occurrences in the corpus, 10 on v1's integer surface — and the remedy is
 * M26's defensive cqrt_copy (bd -493). That remedy is exactly what guarantees
 * a kernel never sees it, so a kernel that does is looking at a missing copy.
 * Measured before this guard existed: Debug aborted from M05 with a message
 * naming the fold table rather than the alias, and RELEASE returned normally
 * having emitted `ccx q0 q0 q2` — a Toffoli whose two controls are one
 * physical qubit — straight to the sink. Right value, malformed circuit, no
 * diagnostic. The FAIL_REGULAR_EXPRESSION on this case is what pins that the
 * kernel now speaks first. */
static void the_two_sources_are_the_same_register(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 4u, 0xFu, 0xFu);
    int32_t hd = cq_reg_alloc_zero(&g_ctx.regs, 4u);

    const cq_bit *a = cq_reg_cbits(&g_ctx.regs, ha);

    CQ_EXPECT_ABORT(cq_kernel_or(&g_ctx, cq_reg_bits(&g_ctx.regs, hd), a, a, 4));
}

static void a_negative_width(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 4u, 0xFu, 0xFu);
    int32_t hb = cq_bk_reg(&g_ctx, 4u, 0x5u, 0xFu);
    int32_t hd = cq_reg_alloc_zero(&g_ctx.regs, 4u);

    CQ_EXPECT_ABORT(cq_kernel_and(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                  cq_reg_cbits(&g_ctx.regs, ha),
                                  cq_reg_cbits(&g_ctx.regs, hb), -1));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(dst_aliases_a_quantum_source),
    CQ_DEATH_CASE(dst_aliases_a_classical_source),
    CQ_DEATH_CASE(dst_aliases_the_second_source),
    CQ_DEATH_CASE(dst_overlaps_a_source_without_sharing_a_base),
    CQ_DEATH_CASE(the_two_sources_are_the_same_register),
    CQ_DEATH_CASE(a_width_of_zero),
    CQ_DEATH_CASE(a_negative_width)
)
