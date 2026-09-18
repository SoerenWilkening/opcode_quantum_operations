/* src/kernels/fcmp_pred.c — M36, K18, the PREDICATE TABLE half of PRD-v2 §5's
 * recorded seam ("ORDERED CORE <-> PREDICATE TABLE").
 *
 * Read docs/constructions/K18.md and fcmp.h before changing anything here.
 * fcmp.c holds the three ordered cores and the step machine; this file holds
 * the seven one-line compositions of fcmp.jl (:90, :102, :134, :144, :154,
 * :164, :174, :184), the fourteen-row dispatch of
 * third_party/bennett/src/extract/instructions.jl:7698-7730, the fourteen
 * entry points and risk R9's classical row.
 *
 * THE SEAM WAS TAKEN BECAUSE THE TWO HALVES TOGETHER EXCEED RULE 12, and it
 * was recorded in PRD-v2 §5 before a line was written, which is the whole
 * point of recording a seam in advance.
 *
 * THE SWAP IS RESOLVED IN EXACTLY TWO PLACES, ONE PER MODE, AND A TEST HOLDS
 * THEM TOGETHER. `fcmp()` below exchanges the operand pointers for the circuit
 * and `cq_fcmp_eval` exchanges the two `uint64_t`s for the classical row; both
 * read `cq_fcmp_swapped`, and `the_four_swapped_predicates_are_their_twin_on_
 * exchanged_operands` asserts the two modes agree. A third copy would be a
 * third chance at K9's measured uge-meaning-ule.
 *
 * THE COPY-OUT FOLD IS REFUSED, AND THAT IS A DECISION RATHER THAN AN
 * OVERSIGHT. K18.md §2.5 proposed dropping the trailing `lower_not1!` of `ord`
 * (fcmp.jl:134) and the trailing `1 - x` of `une` (:102) and spelling them as
 * an X at the copy-out, as K9's `cq_kernel_ne` does. It is refused here on the
 * maintainer's 2026-09-18 ruling for D-K18-2: the grain is "one block per
 * OPERATOR occurrence, no reading of what the operator means", and the fold is
 * exactly such a reading. Every predicate's copy-out is therefore ONE CX, the
 * slot scan needs no per-predicate exception, and §2.5's `{ord, une}` is
 * recorded in K18.md §5 as the refused alternative so it is not re-proposed.
 */

#include "kernels/fcmp.h"

#include "emit.h"
#include "kernels/fpclass.h"
#include "kernels/kernel.h"
#include "sandwich.h"

/* --- instructions.jl:7698-7730, as a table. ------------------------------ */

/* FOUR ROWS SWAP AND NONE INVERTS. Every `swap` column below is upstream's own
 * `op1, op2 = op2, op1`; there is no `lower_not1!` arm anywhere in the
 * extractor, because `une`, `ord` and `one` carry their inversion INSIDE the
 * body. Do not carry K9's `lower_icmp!` table across: its swap set is
 * {ugt, ule, sgt, sle} and this one is {ogt, oge, ugt, uge}, and `ule` swaps
 * there and not here. */
static const struct { short callee; short swap; const char *name; }
DISPATCH[CQ_FCMP_N_PRED] = {
    { CQ_FCMP_OEQ, 0, "oeq" },   /* instructions.jl:7700-7701               */
    { CQ_FCMP_OLT, 1, "ogt" },   /* :7706-7708   ogt = olt(b, a)            */
    { CQ_FCMP_OLE, 1, "oge" },   /* :7709-7711   oge = ole(b, a), NOT olt   */
    { CQ_FCMP_OLT, 0, "olt" },   /* :7698-7699                              */
    { CQ_FCMP_OLE, 0, "ole" },   /* :7702-7703                              */
    { CQ_FCMP_ONE, 0, "one" },   /* :7713-7714                              */
    { CQ_FCMP_ORD, 0, "ord" },   /* :7715-7716                              */
    { CQ_FCMP_UNO, 0, "uno" },   /* :7717-7718                              */
    { CQ_FCMP_UEQ, 0, "ueq" },   /* :7719-7720                              */
    { CQ_FCMP_ULT, 1, "ugt" },   /* :7721-7723   ugt = ult(b, a)            */
    { CQ_FCMP_ULE, 1, "uge" },   /* :7724-7726   uge = ule(b, a), NOT ult   */
    { CQ_FCMP_ULT, 0, "ult" },   /* :7727-7728                              */
    { CQ_FCMP_ULE, 0, "ule" },   /* :7729-7730                              */
    { CQ_FCMP_UNE, 0, "une" }    /* :7704-7705                              */
};

static int pred_ok(cq_fcmp_pred p)
{
    if ((int)p < 0 || (int)p >= (int)CQ_FCMP_N_PRED)
        cq_kernel_die("fcmp: predicate outside [0, CQ_FCMP_N_PRED)");
    return (int)p;
}

cq_fcmp_pred cq_fcmp_callee(cq_fcmp_pred p)
{ return (cq_fcmp_pred)DISPATCH[pred_ok(p)].callee; }

int cq_fcmp_swapped(cq_fcmp_pred p)
{ return DISPATCH[pred_ok(p)].swap; }

const char *cq_fcmp_name(cq_fcmp_pred p)
{ return DISPATCH[pred_ok(p)].name; }

/* --- The ten programs. --------------------------------------------------- */

static int put(cq_fcmp_row *out, int n, int op, int s0, int s1, int s2)
{
    if (n < 0 || n >= CQ_FCMP_MAX_ROWS)
        cq_kernel_die("fcmp: the program outgrew CQ_FCMP_MAX_ROWS");
    out[n].op = (short)op;
    out[n].s0 = (short)s0; out[n].s1 = (short)s1; out[n].s2 = (short)s2;
    return n + 1;
}

static int body(cq_fcmp_body_id id, cq_fcmp_row *out, int n)
{ return n + cq_fcmp_body(id, n, out + n); }

/* Each arm below is one line of fcmp.jl, with the bodies appended in the order
 * the source evaluates them and the combining row last. NO COMMON
 * SUB-EXPRESSION SHARING (PRD-v2 §7.2): `ule` carries THREE independent copies
 * of the NaN test — one in `B_nan`, one in `B_olt`, one in `B_oeq` — and two
 * of `abs_a`/`abs_b`. That is the port, not a defect; §5 delta 1 of K18.md
 * records what it costs and why the alternative is a re-derivation. */
int cq_fcmp_program(cq_fcmp_pred p, cq_fcmp_row *out)
{
    int n = 0, f_nan, f_olt, f_oeq, f_ord, f_ole, f_sub;

    if (out == NULL) cq_kernel_die("fcmp: no output buffer for a program");

    switch (cq_fcmp_callee(p)) {
    case CQ_FCMP_OEQ:                                     /* fcmp.jl:57-82  */
        return body(CQ_FCMP_B_OEQ, out, 0);

    case CQ_FCMP_OLT:                                     /* fcmp.jl:8-47   */
        return body(CQ_FCMP_B_OLT, out, 0);

    case CQ_FCMP_UNO:                                     /* fcmp.jl:144    */
        return body(CQ_FCMP_B_NAN, out, 0);

    case CQ_FCMP_ORD:                                     /* fcmp.jl:134    */
        f_nan = cq_fcmp_body_flag(CQ_FCMP_B_NAN, 0);
        n = body(CQ_FCMP_B_NAN, out, 0);
        return put(out, n, CQ_FCOP_NOT1, f_nan, 0, 0);

    case CQ_FCMP_OLE:                                     /* fcmp.jl:90     */
        f_olt = cq_fcmp_body_flag(CQ_FCMP_B_OLT, 0);
        n     = body(CQ_FCMP_B_OLT, out, 0);
        f_oeq = cq_fcmp_body_flag(CQ_FCMP_B_OEQ, n);
        n     = body(CQ_FCMP_B_OEQ, out, n);
        return put(out, n, CQ_FCOP_OR1, f_olt, f_oeq, 0);

    case CQ_FCMP_UNE:                                     /* fcmp.jl:102    */
        f_oeq = cq_fcmp_body_flag(CQ_FCMP_B_OEQ, 0);
        n     = body(CQ_FCMP_B_OEQ, out, 0);
        /* `UInt64(1) - x` is M14's sub block with the constant 1 as a SOURCE
         * — the maintainer's 2026-09-18 ruling on D-K18-2, against the draft's
         * `lower_not1!`. One block per operator occurrence; the fold table
         * elides most of the sub's gates anyway (PRD-v2 §7.2 as amended). */
        return put(out, n, CQ_FCOP_SUB, CQ_FC_K_ONE, f_oeq, 0);

    case CQ_FCMP_UEQ:                                     /* fcmp.jl:164    */
        f_nan = cq_fcmp_body_flag(CQ_FCMP_B_NAN, 0);
        n     = body(CQ_FCMP_B_NAN, out, 0);
        f_oeq = cq_fcmp_body_flag(CQ_FCMP_B_OEQ, n);
        n     = body(CQ_FCMP_B_OEQ, out, n);
        return put(out, n, CQ_FCOP_OR1, f_nan, f_oeq, 0);

    case CQ_FCMP_ULT:                                     /* fcmp.jl:174    */
        f_nan = cq_fcmp_body_flag(CQ_FCMP_B_NAN, 0);
        n     = body(CQ_FCMP_B_NAN, out, 0);
        f_olt = cq_fcmp_body_flag(CQ_FCMP_B_OLT, n);
        n     = body(CQ_FCMP_B_OLT, out, n);
        return put(out, n, CQ_FCOP_OR1, f_nan, f_olt, 0);

    case CQ_FCMP_ULE:                                     /* fcmp.jl:184    */
        f_nan = cq_fcmp_body_flag(CQ_FCMP_B_NAN, 0);
        n     = body(CQ_FCMP_B_NAN, out, 0);
        f_olt = cq_fcmp_body_flag(CQ_FCMP_B_OLT, n);
        n     = body(CQ_FCMP_B_OLT, out, n);
        f_oeq = cq_fcmp_body_flag(CQ_FCMP_B_OEQ, n);
        n     = body(CQ_FCMP_B_OEQ, out, n);
        /* `uno | ole`, and `ole` is itself `olt | oeq` (:90) — so TWO `or`
         * rows, inner first, exactly as the two source lines nest. */
        f_ole = n;
        n     = put(out, n, CQ_FCOP_OR1, f_olt, f_oeq, 0);
        return put(out, n, CQ_FCOP_OR1, f_nan, f_ole, 0);

    case CQ_FCMP_ONE:                                     /* fcmp.jl:154    */
        f_nan = cq_fcmp_body_flag(CQ_FCMP_B_NAN, 0);
        n     = body(CQ_FCMP_B_NAN, out, 0);
        f_ord = n;
        n     = put(out, n, CQ_FCOP_NOT1, f_nan, 0, 0);    /* ord, :134     */
        f_oeq = cq_fcmp_body_flag(CQ_FCMP_B_OEQ, n);
        n     = body(CQ_FCMP_B_OEQ, out, n);
        f_sub = n;
        n     = put(out, n, CQ_FCOP_SUB, CQ_FC_K_ONE, f_oeq, 0);
        return put(out, n, CQ_FCOP_AND1, f_ord, f_sub, 0);

    default: break;
    }
    /* The four swapped rows resolve to a callee above, so reaching here means
     * cq_fcmp_callee returned something that is not one of the ten exported
     * soft_fcmp_* (softfloat.jl:55-57). */
    cq_kernel_die("fcmp: no program for a predicate that is not a callee");
    return 0;
}

/* --- Risk R9's classical row (PRD-v2 §7.4). ------------------------------ */

/* A C TRANSCRIPTION OF THE SAME JULIA BODIES OVER `uint64_t`, NEVER THE HOST
 * `<` / `==` / `isunordered`. For `fcmp` the motivating hazard is weaker than
 * for `fadd` — IEEE specifies the RESULT of every comparison including every
 * NaN case, so §7.4's five unspecified cells do not reach K18 — but the rule
 * holds for two reasons that survive: this row and the circuit must agree
 * bit-for-bit BY CONSTRUCTION rather than by a shared belief about IEEE, and a
 * `double` comparison in src/ is exactly what acquires -ffast-math or x87
 * excess precision on a host nobody tested. L1's oracle is the host operator
 * and shares nothing with this code, which is what makes a wrong row visible.
 *
 * A `Bool` INTERMEDIATE IS AN `int` AND A `UInt64` ONE IS A `uint64_t` — the
 * same D-K18-1 split the circuit uses for its span widths. */
static int sc_either_nan(uint64_t a, uint64_t b)          /* fcmp.jl:117-125 */
{
    uint64_t ea = (a >> CQ_FP64_EXP_LO) & CQ_FP64_EXP_ALL;
    uint64_t eb = (b >> CQ_FP64_EXP_LO) & CQ_FP64_EXP_ALL;
    uint64_t fa = a & CQ_FP64_FRAC_MASK, fb = b & CQ_FP64_FRAC_MASK;
    int a_nan = (ea == CQ_FP64_EXP_ALL) && (fa != UINT64_C(0));
    int b_nan = (eb == CQ_FP64_EXP_ALL) && (fb != UINT64_C(0));

    return a_nan || b_nan;
}

#define SC_ABS_MASK  UINT64_C(0x7FFFFFFFFFFFFFFF)         /* fcmp.jl:10, :58 */

static uint64_t sc_oeq(uint64_t a, uint64_t b)            /* fcmp.jl:57-82   */
{
    uint64_t abs_a = a & SC_ABS_MASK, abs_b = b & SC_ABS_MASK;
    int either_nan = sc_either_nan(a, b);
    int both_zero  = (abs_a == UINT64_C(0)) && (abs_b == UINT64_C(0));
    int result     = (a == b) || both_zero;                /* :76 */

    return (uint64_t)(result && !either_nan);              /* :79 */
}

static uint64_t sc_olt(uint64_t a, uint64_t b)            /* fcmp.jl:8-47    */
{
    uint64_t sa = a >> CQ_FP64_SIGN_LO, sb = b >> CQ_FP64_SIGN_LO;
    uint64_t abs_a = a & SC_ABS_MASK, abs_b = b & SC_ABS_MASK;
    int either_nan   = sc_either_nan(a, b);                /* :22-24 */
    int both_zero    = (abs_a == UINT64_C(0)) && (abs_b == UINT64_C(0));
    int pos_lt       = abs_a <  abs_b;                     /* :32    */
    int neg_lt       = abs_a >  abs_b;                     /* :33    */
    int diff_sign_lt = sa    >  sb;                        /* :36    */
    int same_sign    = sa    == sb;                        /* :38    */
    int result = same_sign ? (sa == UINT64_C(0) ? pos_lt : neg_lt)
                           : diff_sign_lt;                 /* :39-41 */

    return (uint64_t)(result && !both_zero && !either_nan);/* :44    */
}

static uint64_t sc_core(uint64_t a, uint64_t b, cq_fcmp_pred c)
{
    switch (c) {
    case CQ_FCMP_OEQ: return sc_oeq(a, b);
    case CQ_FCMP_OLT: return sc_olt(a, b);
    case CQ_FCMP_OLE: return sc_olt(a, b) | sc_oeq(a, b);                /*:90*/
    case CQ_FCMP_UNO: return (uint64_t)sc_either_nan(a, b);              /*144*/
    case CQ_FCMP_ORD: return (uint64_t)(!sc_either_nan(a, b));           /*134*/
    case CQ_FCMP_UNE: return UINT64_C(1) - sc_oeq(a, b);                 /*102*/
    case CQ_FCMP_UEQ: return (uint64_t)sc_either_nan(a, b) | sc_oeq(a, b);
    case CQ_FCMP_ULT: return (uint64_t)sc_either_nan(a, b) | sc_olt(a, b);
    case CQ_FCMP_ULE: return (uint64_t)sc_either_nan(a, b)
                           | (sc_olt(a, b) | sc_oeq(a, b));              /*184*/
    case CQ_FCMP_ONE: return (uint64_t)(!sc_either_nan(a, b))
                           & (UINT64_C(1) - sc_oeq(a, b));               /*154*/
    default: break;
    }
    cq_kernel_die("fcmp: eval of a predicate that is not a callee");
    return UINT64_C(0);
}

uint64_t cq_fcmp_eval(uint64_t a, uint64_t b, cq_fcmp_pred p)
{
    cq_fcmp_pred c = cq_fcmp_callee(p);

    if (cq_fcmp_swapped(p)) { uint64_t t = a; a = b; b = t; }
    return sc_core(a, b, c);
}

/* --- The fourteen kernels: Bennett-in-the-small over one flat region. ----- */

typedef struct {
    cq_fcmp_block k;
    cq_bit       *dst;
} fcmp_env;

static void fcmp_compute(cq_ctx *ctx, void *env, int s)
{
    cq_fcmp_step(ctx, &((const fcmp_env *)env)->k, s);
}

/* The "^=" of Rule 7's contract, and the only place `dst` is written on the
 * sandwich path — which is why the driver runs it with the I6 extent DISARMED.
 * ONE CX and no X: the flag already holds the predicate rather than its
 * negation, because every `==`, `<` and `>` occurrence had its polarity
 * settled inside the program by upstream's own lower_not1!. */
static void fcmp_copyout(cq_ctx *ctx, void *env, int s)
{
    const fcmp_env *e = (const fcmp_env *)env;

    (void)s;
    cq_emit_cx(ctx, cq_fcmp_flag(&e->k), &e->dst[0]);
}

static void fcmp(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, const cq_bit *b,
                 int W, cq_fcmp_pred p)
{
    cq_fcmp_row rows[CQ_FCMP_MAX_ROWS];
    const cq_bit *src[2];
    int w[2], n;
    cq_scratch scr;
    fcmp_env e;

    /* PRD-v2 §1 scopes v2 to f64 and every soft_fcmp_* is (UInt64, UInt64), so
     * another width is a fiction rather than an unimplemented case. */
    if (W != CQ_FP64_W)
        cq_kernel_die("fcmp: W must be 64 — v2 is f64 only and soft_fcmp_* "
                      "takes two UInt64");

    /* `dst` is ONE bit while the operands are 64, so the arity-2 wrapper would
     * size every range with one width (kernel.h). */
    src[0] = a; src[1] = b; w[0] = W; w[1] = W;
    cq_kernel_check_n(dst, 1, src, w, 2);

    /* RISK R9, AND IT IS NOT AN OPTIMISATION (plan §0.2 consequence 2).
     * Pre-materialisation is unconditional, so without this a fully classical
     * `ule` would take 3,032 qubits for an operation with no quantum input at
     * all. The value comes from the SAME Julia bodies the circuit runs, and
     * `cq_fcmp_eval` resolves the swap itself — so the operands go in here in
     * the CALLER's order, unswapped. */
    if (cq_bits_all_const(a, W) && cq_bits_all_const(b, W)) {
        if (cq_fcmp_eval(cq_fp_pack(a), cq_fp_pack(b), p))
            cq_emit_x(ctx, &dst[0]);
        return;
    }

    /* instructions.jl's `op1, op2 = op2, op1`: an argument-order change at the
     * entry point, never a circuit. Four rows of fourteen. */
    if (cq_fcmp_swapped(p)) { const cq_bit *t = a; a = b; b = t; }

    n = cq_fcmp_program(p, rows);

    /* ONE CONTIGUOUS REGION, carved into named sub-arrays, because emit.c's
     * I6(a) check is a pointer RANGE test over cq_bit addresses — a kernel
     * that allocated two regions would put half its targets outside it. */
    cq_scratch_alloc(&scr, cq_fcmp_region(rows, n));
    e.k.a = a; e.k.b = b; e.k.scr = &scr; e.k.off = 0u;
    e.k.rows = rows; e.k.n_rows = n;
    e.dst = dst;

    cq_sandwich(ctx, &scr, fcmp_compute, cq_fcmp_steps(rows, n),
                fcmp_copyout, 1, &e);
    cq_scratch_dispose(&scr);
}

#define CQ_FCMP_ENTRY(lo, UP)                                                  \
    void cq_kernel_fcmp_##lo(cq_ctx *c, cq_bit *d, const cq_bit *a,            \
                             const cq_bit *b, int W)                           \
    { fcmp(c, d, a, b, W, CQ_FCMP_##UP); }

CQ_FCMP_ENTRY(oeq, OEQ) CQ_FCMP_ENTRY(ogt, OGT) CQ_FCMP_ENTRY(oge, OGE)
CQ_FCMP_ENTRY(olt, OLT) CQ_FCMP_ENTRY(ole, OLE) CQ_FCMP_ENTRY(one, ONE)
CQ_FCMP_ENTRY(ord, ORD) CQ_FCMP_ENTRY(uno, UNO) CQ_FCMP_ENTRY(ueq, UEQ)
CQ_FCMP_ENTRY(ugt, UGT) CQ_FCMP_ENTRY(uge, UGE) CQ_FCMP_ENTRY(ult, ULT)
CQ_FCMP_ENTRY(ule, ULE) CQ_FCMP_ENTRY(une, UNE)
