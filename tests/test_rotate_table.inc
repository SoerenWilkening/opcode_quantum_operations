/* tests/test_rotate_table.inc — PRD §7's table, transcribed as DATA.
 *
 * THIS FILE IS THE SPECIFICATION AND src/rotate.c IS THE IMPLEMENTATION, and
 * they are deliberately written in different shapes so that one cannot be read
 * off the other. §7's table is six rows × two columns; the module walks it as
 * control flow (a switch on M21's class, then a branch on the bit's kind) and
 * this file states it as a flat list of cells with the counts spelled out per
 * bit. A transcription error in either is a disagreement, not a shared answer.
 *
 * EVERY COLUMN IS ENUMERATED FOR EVERY ROW, which is the point rather than
 * thoroughness for its own sake. The mutation the enumeration exists to catch
 * is "the zero-qubit check runs only on the Ry identity rows": the Rz constant
 * column is the one that would then survive materialising, and it is the cell
 * PRD §15 D12's twelve corpus rails depend on.
 *
 * THE ANGLES ARE SPELLED AS MULTIPLES OF π DELIBERATELY, and that is safe here
 * for the reason PRD §15 D10 records: the residual M21 tests is
 * |θ − fl(k·π_double)|, which for θ spelled `k * π_double` is EXACTLY zero at
 * every k. So these land on the lattice at any tolerance down to and including
 * zero, and the cases below that vary the tolerance are testing the module's
 * consultation of it, never the arithmetic of these constants.
 *
 * WHAT IS NOT HERE. The classification itself — which row an angle sits on —
 * is M21's subject and is tested exhaustively in tests/test_angle.c against two
 * independent oracles. This file assumes it and tests the CELL: given the row
 * and the bit's kind, what is emitted, what does the bit become, and what
 * happens to the shadow.
 */

/* π, spelled independently of the library's constant.
 *
 * NOT CQ_ANGLE_PI, and that is the whole point (the Step 18 lesson: an oracle
 * that shares a constant with the code is blind to exactly what that constant
 * gets wrong). `4·atan(1)` is a different computation that must round to the
 * same double, and `the_z_of_a_half_turn_is_rz_at_the_nearest_double_to_pi`
 * compares the emitted
 * angle against THIS one, bitwise. If a future libm makes the two differ, that
 * case goes red and says so — which is the correct outcome, not a nuisance. */
#define RT_PI (4.0 * atan(1.0))

typedef enum { RT_RY = 0, RT_RZ = 1 } rt_axis;

/* One cell pair of §7's table: what the row does to a CONSTANT bit and what it
 * does to a QUBIT bit. Counts are PER BIT, so a W-bit rail multiplies them.
 *
 * THE ANGLE IS SPLIT INTO `lit + pi_mult · π` BECAUSE RT_PI IS NOT A CONSTANT
 * EXPRESSION. That is the price of computing π independently rather than
 * copying the module's literal, and it is worth paying: a table initialised
 * with `4.0 * RT_PI` does not compile at all, and the reflex fix — spell the
 * digits again here — is the exact coupling this file exists to avoid. */
typedef struct {
    const char *name;
    rt_axis     axis;
    double      lit;       /* the angle is `lit + pi_mult * RT_PI`            */
    double      pi_mult;

    /* --- constant column ------------------------------------------------- */
    int c_flip;         /* the bit's value flips                             */
    int c_materialise;  /* the bit becomes CQ_BIT_Q (and then rotates)       */

    /* --- qubit column: gates emitted per already-quantum bit -------------- */
    unsigned q_x, q_ry, q_rz;

    /* --- PRD §15 D12 ----------------------------------------------------- */
    int poisons;        /* does the row leave the shadow `unknown`?          */
} rt_cell;

/* PRD §7's six rows, at sixteen angles chosen so that every row is reached from
 * more than one residue and every collapse is exercised.
 *
 * Read the Rz rows against src/angle.h: cq_angle_rz_row returns IDENTITY only
 * for φ ≡ 0 (mod 4π) and GENERAL for everything else, so φ = 2π (which IS −I)
 * and φ = π (which IS ∓iZ) both take the general row and a QUBIT under them
 * gets a real sink.rz. That collapse is deliberate and PRD §15 D11 is why:
 * folding either would lose a phase that becomes observable under §9's control.
 *
 * `rz_corpus_3_14` is the literal the CQ_lang corpus actually passes, twice,
 * and both occurrences are `cqrt_rz_i32`. It must be GENERAL at every legal
 * tolerance INCLUDING the cap — the Rz column has no half-turn row to fall
 * into — which is the correction PRD §15 D10 carries. */
static const rt_cell RT_CELLS[] = {
  /* name                    axis    lit   ×π    flip mat   x  ry  rz  poison */
  { "ry_identity_0",         RT_RY,  0.0,  0.0,    0,  0,   0,  0,  0,   0 },
  { "ry_identity_4pi",       RT_RY,  0.0,  4.0,    0,  0,   0,  0,  0,   0 },
  { "ry_identity_neg_4pi",   RT_RY,  0.0, -4.0,    0,  0,   0,  0,  0,   0 },
  { "ry_neg_identity_2pi",   RT_RY,  0.0,  2.0,    0,  0,   0,  0,  0,   0 },
  { "ry_neg_identity_6pi",   RT_RY,  0.0,  6.0,    0,  0,   0,  0,  0,   0 },
  { "ry_half_turn_pi",       RT_RY,  0.0,  1.0,    1,  0,   1,  0,  1,   0 },
  { "ry_half_turn_3pi",      RT_RY,  0.0,  3.0,    1,  0,   1,  0,  1,   0 },
  { "ry_half_turn_neg_pi",   RT_RY,  0.0, -1.0,    1,  0,   1,  0,  1,   0 },
  { "ry_general_half",       RT_RY,  0.5,  0.0,    0,  1,   0,  1,  0,   1 },
  { "ry_general_pi_over_2",  RT_RY,  0.0,  0.5,    0,  1,   0,  1,  0,   1 },
  { "rz_identity_0",         RT_RZ,  0.0,  0.0,    0,  0,   0,  0,  0,   0 },
  { "rz_identity_4pi",       RT_RZ,  0.0,  4.0,    0,  0,   0,  0,  0,   0 },
  { "rz_general_2pi",        RT_RZ,  0.0,  2.0,    0,  0,   0,  0,  1,   0 },
  { "rz_general_pi",         RT_RZ,  0.0,  1.0,    0,  0,   0,  0,  1,   0 },
  { "rz_general_half",       RT_RZ,  0.5,  0.0,    0,  0,   0,  0,  1,   0 },
  { "rz_corpus_3_14",        RT_RZ,  3.14, 0.0,    0,  0,   0,  0,  1,   0 },
};
#define RT_N_CELLS ((int)(sizeof RT_CELLS / sizeof RT_CELLS[0]))

static double rt_angle(const rt_cell *c)
{
    return c->lit + c->pi_mult * RT_PI;
}

/* The module's two entry points behind one call, so a case can sweep the table
 * without branching on the axis at every site. */
static void rt_apply_reg(cq_ctx *ctx, const rt_cell *c, int32_t h)
{
    if (c->axis == RT_RY) cq_rotate_ry(ctx, h, rt_angle(c));
    else                  cq_rotate_rz(ctx, h, rt_angle(c));
}

/* The per-bit entry points, which the register loops are built out of. Kept
 * exercised so that a suite driving only the register form cannot leave them
 * untested — M26 will call the register form, but K-side callers and Step 20
 * reach the per-bit one. */
static void rt_apply_bit(cq_ctx *ctx, const rt_cell *c, cq_bit *b)
{
    if (c->axis == RT_RY) cq_rotate_ry_bit(ctx, b, rt_angle(c));
    else                  cq_rotate_rz_bit(ctx, b, rt_angle(c));
}

/* popcount without a compiler builtin — the project takes no dependency beyond
 * libc and refmodel's cq_ref_w_popcount wants a two-word value this file has no
 * reason to build. */
static uint32_t rt_popcount(uint64_t v)
{
    uint32_t n = 0;
    while (v) { n += (uint32_t)(v & 1u); v >>= 1; }
    return n;
}

/* --- Fixture ------------------------------------------------------------- */

typedef struct { cq_ctx ctx; cq_mock mock; cq_sink sink; } fixture;

static void fx_open(fixture *f)
{
    cq_mock_init(&f->mock);
    f->sink = cq_mock_sink(&f->mock);
    cq_ctx_init(&f->ctx, &f->sink);
}

static void fx_close(fixture *f)
{
    cq_ctx_dispose(&f->ctx);
    cq_mock_dispose(&f->mock);
}

/* The value a register holds, read the way PRD §7's measurement reads it: a
 * constant bit contributes its kind, a qubit its shadow value, and a POISONED
 * qubit contributes 0.
 *
 * This is NOT tests/support/poolcheck.c's cq_pc_value, and the difference is
 * the whole reason it exists here. That reader records a HARNESS FAILURE on an
 * unknown shadow, which is right on the rotation-free surface (Steps 10-17,
 * where `unknown` is unreachable) and wrong from Step 19 on, where a general Ry
 * legitimately poisons. Using it here would make every general-Ry case red for
 * a reason that is the specification. */
static uint64_t rt_value(const cq_ctx *ctx, int32_t h)
{
    const cq_bit *b = cq_reg_cbits(&ctx->regs, h);
    uint32_t w = cq_reg_width(&ctx->regs, h);
    uint64_t v = 0;

    for (uint32_t i = 0; i < w; i++) {
        int bit;
        if (cq_bit_is_qubit(b[i])) {
            cq_shadow s = cq_shadow_get(&ctx->shadow, cq_bit_qindex(b[i]));
            bit = s.unknown ? 0 : (int)s.value;
        } else {
            bit = cq_bit_value(b[i]);
        }
        if (bit) v |= (uint64_t)1 << i;
    }
    return v;
}

/* How many of a rail's bits carry an `unknown` shadow. Zero is the claim D12
 * makes for every row but general Ry. */
static uint32_t rt_poisoned_bits(const cq_ctx *ctx, int32_t h)
{
    const cq_bit *b = cq_reg_cbits(&ctx->regs, h);
    uint32_t w = cq_reg_width(&ctx->regs, h), n = 0;

    for (uint32_t i = 0; i < w; i++)
        if (cq_bit_is_qubit(b[i]) &&
            cq_shadow_get(&ctx->shadow, cq_bit_qindex(b[i])).unknown) n++;
    return n;
}

static uint32_t rt_quantum_bits(const cq_ctx *ctx, int32_t h)
{
    return cq_reg_owned_qubits(&ctx->regs, h);
}
