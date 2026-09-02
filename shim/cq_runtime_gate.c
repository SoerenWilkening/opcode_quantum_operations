/* shim/cq_runtime_gate.c — M26's gate surface, Step 23 landing 1 step 4. The 30
 * `cqrt_*` symbols that emit onto rails somebody else created.
 *
 * Declared in shim/cq_runtime_abi.h, the verified copy of CQ_lang's own header:
 * without it a wrong return or parameter type in a definition compiles clean,
 * links clean, and surfaces at Step 24 as a wrong answer.
 *
 * RULE 12. Budget 200. Split seam, RECORDED BEFORE THE FILE WAS WRITTEN:
 *
 *     the DISCRETE gate primitives <-> the ROTATION families
 *                                          ->  shim/cq_runtime_rotate.c
 *
 * The five discrete symbols — x / cnot / toffoli / x_controlled /
 * cnot_controlled — stay; the 25 rotation symbols move. The 5/25 split is not a
 * size imbalance, it is two ABI shapes over two libcqops layers. The discrete
 * five carry NO width token, act on one-bit rails, and reach M05's
 * cq_emit_x/cx/ccx — Rule 4's classical permutation surface, where the two-bit
 * shadow is EXACT. All 25 rotations are `_i{1,8,16,32,64}`, all carry a
 * `double angle`, all walk every bit of a rail, and all reach M22 — PRD §7's
 * twelve cells, D11's refusal, D12's poison. That last is the sharpest
 * difference: the rotation half is the only part of the whole shim that can
 * make a rail unfreeable, and the only part that can hard-error for a reason
 * the discrete half has no vocabulary for.
 *
 * TRIGGER 240, THE HOUSE THRESHOLD (M06, M07, M20, M22, M27) — measured against
 * Rule 12's 300-line HARD LIMIT and not against this file's budget. See
 * `shim/cq_runtime_rail.c`'s header for the derivation and for the two things a
 * first draft of it got wrong; the short version is that a trigger below the
 * budget fires the day the file lands, which is a two-file plan wearing a
 * trigger's clothes. A budget is an estimate; the 300 is the wall.
 *
 * REJECTED, AND RECORDED SO IT IS NOT RE-PROPOSED: `uncontrolled <-> controlled`
 * is a size cut wearing a subject's clothes. Rule 9 makes the controlled axis an
 * emitter MODE, so there is no controlled machinery to move — the whole of it is
 * one function in a third file — and taking that axis would put `cqrt_x` and
 * `cqrt_x_controlled` in two files with nothing but a `cq_shim_region` call
 * between them.
 *
 * THE ONE-BIT GUARD IS THIS FILE'S OWN. `void cqrt_x(int32_t q)` carries no
 * width token and no width parameter, so its meaning on a wider rail is
 * UNSPECIFIED rather than merely unusual — CQ_lang's header calls the operand
 * handle-typed and says nothing more. Measured over the 247 goldens at CQ_lang
 * `3319975`, 2026-08-23: 4,922 `cqrt_x`, 29,260 `cqrt_cnot` and 53,332
 * `cqrt_toffoli` calls, and all 223,438 of their handle operand slots resolve
 * to a WIDTH-1 rail — 100%, zero unresolved, and zero calls with two equal
 * handle arguments. So refusing costs nothing today, and the alternative —
 * silently taking bit 0 — is a miscompile on the first wider rail anyone hands
 * us. bd 216's checklist asks for this on the two CONTROLLED primitives;
 * extending it to the uncontrolled three is this file's, on the same
 * measurement. Read the figures with their SHA: see cq_runtime_rail.c's header
 * for why that corpus moved twice while these two files were written.
 *
 * THE OPERAND IS A HANDLE AND NOT A WIRE, which is the fact that makes this
 * file possible at all: a raw qubit index has no bit KIND, and the §3 fold
 * table dispatches on nothing else. That also settles PRD §15 D16's capability
 * question for two of the never-minted symbols — `cqrt_x_controlled` IS a CX
 * and `cqrt_cnot_controlled` IS a CCX, so libcqops can serve them correctly,
 * while `cqrt_h` it cannot serve at any point in v1 and is left undefined.
 */

#include "cq_runtime_abi.h"

#include <string.h>

#include "cq_shim_ctx.h"
#include "cq_shim_record.h"
#include "cq_shim_trace.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "reg.h"
#include "rotate.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* A second static with the house prefix, on src/reg_check.c's precedent — the
 * FAIL_REGULAR_EXPRESSION pins discriminate on the MESSAGE, not the module. */
static void cq_gate_die(const char *what, int32_t h, uint32_t n)
{
    fprintf(stderr, "libcqops: FATAL: shim: %s (h%d, %u)\n", what, h, n);
    abort();
}

/* THE ONE-BIT RESOLVE, WITH THE READ/WRITE ASYMMETRY THE REST OF THE SHIM USES.
 * `cq_reg_cbits` admits a MEASURED rail because a control is a READ; `cq_reg_
 * bits` refuses one because a target is a WRITE, which is where measurement's
 * terminality is enforced. Both refuse a tombstone, and both run BEFORE the
 * width comparison so a use-after-free is M07's diagnostic and not a width bug
 * — `cq_reg_width` goes through `cq_reg_slot`, which admits a tombstone on
 * purpose. */
static const char *const GATE_WIDTH =
    "a cqrt_ gate primitive was handed a rail wider than one bit; the ABI "
    "declares these with no width token and every operand CQ_lang emits is i1";

static const cq_bit *gate_ctrl(cq_ctx *ctx, int32_t h)
{
    const cq_bit *b = cq_reg_cbits(&ctx->regs, h);
    const uint32_t w = cq_reg_width(&ctx->regs, h);

    if (w != 1u) cq_gate_die(GATE_WIDTH, h, w);
    return b;
}

static cq_bit *gate_target(cq_ctx *ctx, int32_t h)
{
    cq_bit *b = cq_reg_bits(&ctx->regs, h);
    const uint32_t w = cq_reg_width(&ctx->regs, h);

    if (w != 1u) cq_gate_die(GATE_WIDTH, h, w);
    return b;
}

/* THE ROTATIONS' WIDTH GUARD, AND IT IS UNIFORM WITH THE RAIL SURFACE RATHER
 * THAN SPECIAL. `cqrt_ry_i32`'s width token is a claim about the rail exactly
 * as `cqrt_addc_i32`'s is; the only difference is that M22 walks whatever rail
 * it is given, so a mismatch here rotates the wrong number of lanes instead of
 * computing the wrong value. That is still a caller bug, and an asymmetry
 * ("guarded on the rail surface, unguarded here") would be a rule nobody could
 * state. Measured over the 247 goldens at CQ_lang `3319975`: every
 * `cqrt_ry_i*` and `cqrt_rz_i*` call names a rail of exactly its own width, so
 * the guard is free today.
 *
 * THE `cq_reg_bits` CALL IS NOT A SECOND MEASURED-RAIL REFUSAL AND MUST NOT BE
 * READ AS ONE — M22 makes the same call for the same reason one layer down, so a
 * duplicate here would be the masking-layer trap. What it is for is ORDER: it
 * resolves through the accessor that refuses a tombstone BEFORE `cq_reg_width`
 * is asked, and `cq_reg_width` goes through `cq_reg_slot`, which admits a
 * tombstone on purpose. Without it a freed 32-bit rail handed to `cqrt_ry_i8`
 * would be reported as a width bug rather than a use-after-free. */
static void gate_rot_width(cq_ctx *ctx, int32_t h, uint32_t want)
{
    (void)cq_reg_bits(&ctx->regs, h);
    const uint32_t w = cq_reg_width(&ctx->regs, h);

    if (w != want)
        cq_gate_die("the rail is not the width its cqrt_* symbol names", h, w);
}

/* COINCIDENT HANDLES ARE REFUSED IN BOTH CONFIGURATIONS, for cqrt_cswap's
 * reason one file over: M05's §3 distinctness check is Debug-gated, so in
 * Release a coincident operand reaches the sink as `cx q0 q0` or a Toffoli
 * whose two controls are one physical qubit — a malformed circuit with the
 * right value and no diagnostic. Measured over the 247 goldens at 2026-08-23:
 * zero of the 87,514 calls in these five families have two equal handle
 * arguments (CQ_lang `3319975`). */
static void gate_distinct(int32_t x, int32_t y)
{
    if (x == y)
        cq_gate_die("a cqrt_ gate primitive was handed the same rail twice: a "
                    "coincident operand is a meaningless channel and, in "
                    "Release, a malformed gate with no diagnostic", x, 0u);
}

/* --- D15's certificate: RECORDING the call stream ------------------------ */

/* One record per ENTRY POINT (Rule 13; see cq_runtime_rail.c's copy of this
 * helper for why the flag handle is taken from the parameter and not from the
 * region frame). Duplicated across the two files DELIBERATELY rather than
 * hoisted into cq_shim_record.h: it is four lines, both copies are `static`,
 * and the alternative is a public helper whose only job is to memset a struct —
 * which would put the ABI's operand ORDER one indirection away from the entry
 * point that knows it, and the operand order is exactly what D15 §6(ii) says a
 * write model gets wrong. */
static void rec(cq_rop op, int32_t h0, int32_t h1, int32_t h2,
                double angle, int32_t ctrl)
{
    cq_call_rec c;
    memset(&c, 0, sizeof c);
    c.op   = (uint16_t)op;
    c.h[0] = h0; c.h[1] = h1; c.h[2] = h2; c.h[3] = CQ_REG_NONE;
    c.ctrl = ctrl;
    memcpy(&c.angle, &angle, sizeof c.angle);   /* BITWISE — see the rail copy */
    cq_rec_push(&c);
}

/* --- D21's annotation brackets -------------------------------------------- */

/* ONE BRACKET PER ENTRY POINT, AND EVERY SYMBOL IN THIS FILE WRITES EXACTLY ONE
 * RAIL, so the fifth slot of `cq_trace_op` is spelled once here instead of at
 * eighteen call sites. The uniform rule — bracket the entry point, never the
 * body — is what makes coverage CHECKABLE (cq_shim_trace.h); it is also what
 * keeps the two `_controlled` families out of trouble, since a bracket inside
 * `gate_x_body` would open once per region rather than once per call.
 *
 * COVERAGE HERE IS WIDER THAN THE GATE COUNT SUGGESTS. `cqrt_x` on an
 * all-classical rail emits NOTHING (`cq_emit_x` flips the constant), and a
 * `CQ_BIT_ZERO`-controlled region emits nothing at all — so a bracket is opened
 * for many calls that produce no gate line, which handoff §6 rule 1 explicitly
 * contemplates. The converse is what would be fatal. */
#define CQ_TRACE_OP(name, i0, i1, o0) \
    cq_trace_op((name), (i0), (i1), CQ_REG_NONE, (o0), CQ_REG_NONE)

/* --- The five discrete primitives ---------------------------------------- */

void cqrt_x(int32_t q)
{
    cq_ctx *ctx = cq_shim_ctx();

    CQ_TRACE_OP("x", CQ_REG_NONE, CQ_REG_NONE, q);
    cq_emit_x(ctx, gate_target(ctx, q));
    cq_trace_end();
    rec(CQ_ROP_X, q, CQ_REG_NONE, CQ_REG_NONE, 0.0, CQ_REG_NONE);
}

void cqrt_cnot(int32_t ctrl, int32_t tgt)
{
    cq_ctx *ctx = cq_shim_ctx();

    gate_distinct(ctrl, tgt);
    /* The TARGET is resolved first and held: `gate_ctrl` cannot invalidate it
     * (a register's bits array is its own allocation), and taking them in this
     * order keeps the write-access refusal ahead of the read one. */
    CQ_TRACE_OP("cnot", ctrl, CQ_REG_NONE, tgt);
    cq_bit *t = gate_target(ctx, tgt);
    cq_emit_cx(ctx, gate_ctrl(ctx, ctrl), t);
    cq_trace_end();
    rec(CQ_ROP_CNOT, ctrl, tgt, CQ_REG_NONE, 0.0, CQ_REG_NONE);
}

void cqrt_toffoli(int32_t c1, int32_t c2, int32_t tgt)
{
    cq_ctx *ctx = cq_shim_ctx();

    gate_distinct(c1, c2);
    gate_distinct(c1, tgt);
    gate_distinct(c2, tgt);

    CQ_TRACE_OP("toffoli", c1, c2, tgt);
    cq_bit *t = gate_target(ctx, tgt);
    const cq_bit *a = gate_ctrl(ctx, c1);
    cq_emit_ccx(ctx, a, gate_ctrl(ctx, c2), t);
    cq_trace_end();
    rec(CQ_ROP_TOFFOLI, c1, c2, tgt, 0.0, CQ_REG_NONE);
}

/* RULE 9: THE CONTROLLED AXIS IS AN EMITTER MODE, so these two are the
 * uncontrolled gate inside the one region bracket rather than a hand-rolled CX
 * and CCX. That is not a stylistic preference — writing `cq_emit_cx` here would
 * be right for a CQ_BIT_Q flag and WRONG for both classical rows, which row 0
 * owns: a ZERO flag must emit nothing at all and a ONE flag must emit the
 * UNCONTROLLED gate, and a hand-rolled CX gets the second of those wrong by
 * emitting a real gate controlled off a constant. */
typedef struct { int32_t q; } gate_x_args;
typedef struct { int32_t c, t; } gate_cnot_args;

static void gate_x_body(void *p)
{
    cq_ctx *ctx = cq_shim_ctx();
    cq_emit_x(ctx, gate_target(ctx, ((const gate_x_args *)p)->q));
}

static void gate_cnot_body(void *p)
{
    const gate_cnot_args *a = (const gate_cnot_args *)p;
    cq_ctx *ctx = cq_shim_ctx();
    cq_bit *t = gate_target(ctx, a->t);

    cq_emit_cx(ctx, gate_ctrl(ctx, a->c), t);
}

/* THE BODY ALWAYS RUNS, AND A FIRST DRAFT OF THIS FILE BELIEVED OTHERWISE.
 * `cq_shim_region` pushes the frame and then calls `body(arg)` unconditionally;
 * what PRD §9 row 0 skips is EMISSION, one gate at a time, inside
 * `cq_emit_x/cx/ccx` (and inside M22 for the rotations). So the operand guards
 * belong in the body, where they run on all three rows, and hoisting them out
 * bought nothing — measured: with the hoisted copies deleted, every case in both
 * suites stayed green, because the body's own resolve had always been doing the
 * work. They are gone.
 *
 * The belief was worth killing rather than tidying, because it is the `bd d6m`
 * hazard class: an author who thinks a body is skipped may put a mint or a free
 * inside one. `cqrt_copy_i<W>_controlled` next door never hoisted, and that was
 * the consistent choice all along.
 *
 * `gate_distinct` STAYS IN THE WRAPPER and is not a duplicate: no body checks
 * it, and deleting any of its wrapper calls that a case covers turns that case
 * red. It is also the only guard here that has to run before the region opens,
 * because a flag coinciding with an operand is a statement about the CALL rather
 * than about the gate. */
void cqrt_x_controlled(int32_t ctrl, int32_t q)
{
    gate_x_args a = { q };

    gate_distinct(ctrl, q);
    CQ_TRACE_OP("x_ctrl", ctrl, CQ_REG_NONE, q);
    cq_shim_region(ctrl, gate_x_body, &a);
    cq_trace_end();
    rec(CQ_ROP_X_CTRL, ctrl, q, CQ_REG_NONE, 0.0, ctrl);
}

void cqrt_cnot_controlled(int32_t ctrl, int32_t target_ctrl, int32_t tgt)
{
    gate_cnot_args a = { target_ctrl, tgt };

    gate_distinct(ctrl, target_ctrl);
    gate_distinct(ctrl, tgt);
    gate_distinct(target_ctrl, tgt);
    CQ_TRACE_OP("cnot_ctrl", ctrl, target_ctrl, tgt);
    cq_shim_region(ctrl, gate_cnot_body, &a);
    cq_trace_end();
    /* `target_ctrl` IS A READ (cq_runtime.h): the word "target" inside its name
     * is the INNER control, and the write is arg 2. D15 §6(ii)'s trap in its
     * second spelling — enumerate from the declaration, never from the name. */
    rec(CQ_ROP_CNOT_CTRL, ctrl, target_ctrl, tgt, 0.0, ctrl);
}

/* --- The rotation families ----------------------------------------------- */

/* NO WIDTH GUARD HERE, AND THE ASYMMETRY WITH THE FIVE ABOVE IS THE POINT.
 * PRD §7 specifies a rotation as applying to EVERY BIT of the register, with
 * the column chosen per bit, so `cqrt_ry_i32` on a 32-bit rail is the whole
 * meaning of the symbol rather than an accident of width — M22 walks the rail
 * and there is nothing to disagree with. The five discrete primitives have no
 * width token at all, which is a different situation: there the ABI states no
 * meaning for a wider rail, so refusing is the only honest answer.
 *
 * M22 resolves the handle itself and owns every guard that matters: the
 * sandwich refusal, D11's three refusal sites, row 0's skip (which has to live
 * in M22 because the general row MATERIALISES before it emits, so an
 * emitter-only skip would take W qubits for a region that does not run), and
 * the tombstone. */
#define CQ_GATE_ROT(W)                                                        \
    void cqrt_ry_i##W(int32_t handle, double angle)                           \
    {                                                                         \
        cq_ctx *ctx = cq_shim_ctx();                                          \
        gate_rot_width(ctx, handle, W##u);                                    \
        CQ_TRACE_OP("ry", CQ_REG_NONE, CQ_REG_NONE, handle);                  \
        cq_rotate_ry(ctx, handle, angle);                                     \
        cq_trace_end();                                                       \
        rec(CQ_ROP_RY, handle, CQ_REG_NONE, CQ_REG_NONE, angle,               \
            CQ_REG_NONE);                                                     \
    }                                                                         \
    void cqrt_rz_i##W(int32_t handle, double angle)                           \
    {                                                                         \
        cq_ctx *ctx = cq_shim_ctx();                                          \
        gate_rot_width(ctx, handle, W##u);                                    \
        CQ_TRACE_OP("rz", CQ_REG_NONE, CQ_REG_NONE, handle);                  \
        cq_rotate_rz(ctx, handle, angle);                                     \
        cq_trace_end();                                                       \
        rec(CQ_ROP_RZ, handle, CQ_REG_NONE, CQ_REG_NONE, angle,               \
            CQ_REG_NONE);                                                     \
    }

CQ_GATE_ROT(1)
CQ_GATE_ROT(8)
CQ_GATE_ROT(16)
CQ_GATE_ROT(32)
CQ_GATE_ROT(64)

/* `_inv` NEGATES THETA (PRD §15 D14), and on these families that is an EXACT
 * inverse rather than an approximation. `cq_angle_rz_row` collapses
 * NEG_IDENTITY and both half turns into GENERAL, so `bd fna`'s parity split —
 * which exists because negating theta swaps k = 1 and k = 3 — is structurally
 * invisible on the Rz column, and the Ry column's two half turns both REFUSE
 * under a quantum control, so the parity only decides which string the abort
 * prints. Where D11's phases are ever emitted the parity becomes load-bearing
 * and this comment stops being true.
 *
 * THERE IS NO PLAIN `cqrt_ry_<W>_controlled` AT ANY WIDTH, so the Ry axis is
 * five symbols with no forward twin — the prep itself is unconditional and only
 * its inverse is controlled. Do not "complete" the grid: a uniform
 * 9 x {controlled, controlled_inv} cross product mints symbols the frozen ABI
 * does not declare.
 *
 * THE REFUSAL SURFACE IS REAL AND IS NOT ATOMIC. Under a quantum flag, §7's Rz
 * row folds on the CONSTANT column, so M22 emits the full four-gate promotion
 * for every WIRE below the first constant lane and then hard-errors — 4*j
 * gates, characterised in tests/test_runtime_gate.c because nothing in the tree
 * could see it before. By I4 a freshly allocated rail owns zero qubits, so a
 * rail with a constant lane is the ordinary case rather than a corner. Measured
 * demand today: zero controlled rotations of any kind in the 247 goldens. */
typedef struct { int32_t h; double angle; } gate_rot_args;

static void gate_rz_body(void *p)
{
    const gate_rot_args *a = (const gate_rot_args *)p;
    cq_rotate_rz(cq_shim_ctx(), a->h, a->angle);
}

static void gate_ry_body(void *p)
{
    const gate_rot_args *a = (const gate_rot_args *)p;
    cq_rotate_ry(cq_shim_ctx(), a->h, a->angle);
}

#define CQ_GATE_ROT_CTRL(W)                                                   \
    void cqrt_rz_i##W##_controlled(int32_t ctrl, int32_t handle, double angle)\
    {                                                                         \
        gate_rot_args a = { handle, angle };                                  \
        gate_rot_width(cq_shim_ctx(), handle, W##u);                          \
        CQ_TRACE_OP("rz_ctrl", ctrl, CQ_REG_NONE, handle);                    \
        cq_shim_region(ctrl, gate_rz_body, &a);                               \
        cq_trace_end();                                                       \
        rec(CQ_ROP_RZ_CTRL, ctrl, handle, CQ_REG_NONE, angle, ctrl);          \
    }                                                                         \
    void cqrt_rz_i##W##_controlled_inv(int32_t ctrl, int32_t handle,          \
                                       double angle)                          \
    {                                                                         \
        gate_rot_args a = { handle, -angle };                                 \
        gate_rot_width(cq_shim_ctx(), handle, W##u);                          \
        CQ_TRACE_OP("rz_ctrl_inv", ctrl, CQ_REG_NONE, handle);                \
        cq_shim_region(ctrl, gate_rz_body, &a);                               \
        cq_trace_end();                                                       \
        rec(CQ_ROP_RZ_CTRL_INV, ctrl, handle, CQ_REG_NONE, -angle, ctrl);     \
    }                                                                         \
    void cqrt_ry_i##W##_controlled_inv(int32_t ctrl, int32_t handle,          \
                                       double angle)                          \
    {                                                                         \
        gate_rot_args a = { handle, -angle };                                 \
        gate_rot_width(cq_shim_ctx(), handle, W##u);                          \
        CQ_TRACE_OP("ry_ctrl_inv", ctrl, CQ_REG_NONE, handle);                \
        cq_shim_region(ctrl, gate_ry_body, &a);                               \
        cq_trace_end();                                                       \
        rec(CQ_ROP_RY_CTRL_INV, ctrl, handle, CQ_REG_NONE, -angle, ctrl);     \
    }

CQ_GATE_ROT_CTRL(1)
CQ_GATE_ROT_CTRL(8)
CQ_GATE_ROT_CTRL(16)
CQ_GATE_ROT_CTRL(32)
CQ_GATE_ROT_CTRL(64)
