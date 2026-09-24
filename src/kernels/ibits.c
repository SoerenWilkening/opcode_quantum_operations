/* src/kernels/ibits.c — M41, K26, the ROW TABLES. Nothing else lives here.
 *
 * Each builder below is one `if startswith(cname, "llvm.<name>.")` arm of
 * third_party/bennett/src/extract/instructions.jl, transcribed one row per
 * `push!` of an IRBinOp `add`/`or`, an IRICmp or an IRSelect — including
 * ctlz/cttz's seed `add` whose two operands are BOTH constants (an operator
 * occurrence; the §3 fold table elides its constant-controlled gates) — and
 * one VIEW (no row) per `push!` of an `lshr`, `shl` or `and` by a constant,
 * which PRD-v2 §7.3 as amended makes wiring.
 * The step machine (ibits_step.c) turns rows into gates and knows no Julia;
 * the entry points (ibits_kernel.c) know neither. K26.md §2 is the table of
 * which source line became which row.
 *
 * NO CSE AND NO NARROWING (PRD-v2 §7.2). Every row is W lanes because every
 * IR instruction the expansion emits carries width `w` (`_iwidth(ops[1])`),
 * including the 1-bit-valued `bit` and `is_set` intermediates — the IRICmp is
 * at width `w` over `bit` and `iconst(0)`, so the `ne` block is W lanes wide.
 * And the closing `IRBinOp(dest, :add, ssa(prev), iconst(0), w)` of every arm
 * is a real `add` row: it is an operator occurrence, not a rename, however it
 * reads (the comment above it upstream says "Rename" — the IR does not).
 */

#include "kernels/ibits.h"

#include "kernels/kernel.h"

static cq_ibits_opnd view_a(int shr, int keep, int shl)
{
    cq_ibits_opnd o;

    o.src = (short)CQ_IBS_A;
    o.shr = (short)shr; o.keep = (short)keep; o.shl = (short)shl; o.k = 0;
    return o;
}

static cq_ibits_opnd konst(int v, int W)
{
    cq_ibits_opnd o;

    o.src = (short)CQ_IBS_K;
    o.shr = 0; o.keep = (short)W; o.shl = 0; o.k = (short)v;
    return o;
}

static cq_ibits_opnd row_ref(int r, int W)
{
    cq_ibits_opnd o;

    o.src = (short)r;
    o.shr = 0; o.keep = (short)W; o.shl = 0; o.k = 0;
    return o;
}

static int push(cq_ibits_row *out, int n, int op, cq_ibits_opnd s0,
                cq_ibits_opnd s1, cq_ibits_opnd s2)
{
    if (n >= CQ_IBITS_MAX_ROWS)
        cq_kernel_die("ibits: a program outgrew CQ_IBITS_MAX_ROWS");
    out[n].op = (short)op;
    out[n].s[0] = s0; out[n].s[1] = s1; out[n].s[2] = s2;
    return n + 1;
}

/* instructions.jl:4828-4848 — llvm.ctpop. `prev = x & 1` (:4835) is a view;
 * each iteration's `lshr` (:4840) and `and` (:4841) fold into one view and
 * its `add` (:4842) is a row; `dest = prev + 0` (:4846) closes. */
static int ctpop(int W, cq_ibits_row *out)
{
    cq_ibits_opnd prev = view_a(0, 1, 0), z = konst(0, W);
    int n = 0;

    for (int i = 1; i <= W - 1; i++) {
        n = push(out, n, CQ_IBOP_ADD, prev, view_a(i, 1, 0), z);
        prev = row_ref(n - 1, W);
    }
    return push(out, n, CQ_IBOP_ADD, prev, konst(0, W), z);
}

/* instructions.jl:4851-4870 — llvm.ctlz, LSB -> MSB so the highest set bit
 * overwrites last; :4873-4892 — llvm.cttz, the mirror. The seed `W + 0`
 * (:4856, :4878) is a row with two constant sources; per bit, `lshr`/`and`
 * (:4862-4863, :4884-4885) are a view, the `ne` IRICmp (:4864, :4886) and
 * the `select` (:4865, :4887) are rows; `dest = prev + 0` (:4868, :4890). */
static int ctlz_cttz(int W, int trailing, cq_ibits_row *out)
{
    cq_ibits_opnd z = konst(0, W);
    int n = push(out, 0, CQ_IBOP_ADD, konst(W, W), z, z);
    int prev = n - 1;

    for (int j = 0; j < W; j++) {
        /* ctlz walks i = 0 .. W-1 and selects iconst(W-1-i) (:4857, :4865);
         * cttz walks i = W-1 .. 0 and selects iconst(i) (:4879, :4887). */
        int i = trailing ? W - 1 - j : j;
        int v = trailing ? i : W - 1 - i;

        n = push(out, n, CQ_IBOP_NE, view_a(i, 1, 0), konst(0, W), z);
        n = push(out, n, CQ_IBOP_MUX, row_ref(n - 1, W), konst(v, W),
                 row_ref(prev, W));
        prev = n - 1;
    }
    return push(out, n, CQ_IBOP_ADD, row_ref(prev, W), konst(0, W), z);
}

/* instructions.jl:4895-4921 — llvm.bitreverse. Bit 0 (:4903-4906) is the
 * view `(x >> 0) & 1 << (W-1)` and emits nothing; each later bit's `lshr`,
 * `and`, `shl` (:4913-4915) are one view and its `or` (:4916) is a row;
 * `dest = prev + 0` (:4919). */
static int bitreverse(int W, cq_ibits_row *out)
{
    cq_ibits_opnd prev = view_a(0, 1, W - 1), z = konst(0, W);
    int n = 0;

    for (int i = 1; i <= W - 1; i++) {
        n = push(out, n, CQ_IBOP_OR, prev, view_a(i, 1, W - 1 - i), z);
        prev = row_ref(n - 1, W);
    }
    return push(out, n, CQ_IBOP_ADD, prev, konst(0, W), z);
}

/* instructions.jl:4923-4946 — llvm.bswap, n_bytes = W ÷ 8 (:4926). Byte 0
 * (:4931-4932) is the view `(x & 255) << (n-1)*8`; each later byte's `lshr`,
 * `and`, `shl` (:4938-4940) are one view and its `or` (:4941) a row;
 * `dest = prev + 0` (:4944). */
static int bswap(int W, cq_ibits_row *out)
{
    int nb = W / 8, n = 0;
    cq_ibits_opnd prev = view_a(0, 8, (nb - 1) * 8), z = konst(0, W);

    for (int b = 1; b <= nb - 1; b++) {
        n = push(out, n, CQ_IBOP_OR, prev, view_a(b * 8, 8, (nb - 1 - b) * 8),
                 z);
        prev = row_ref(n - 1, W);
    }
    return push(out, n, CQ_IBOP_ADD, prev, konst(0, W), z);
}

int cq_ibits_program(cq_ibits_fn f, int W, cq_ibits_row *out)
{
    if (out == NULL) cq_kernel_die("ibits: program into a NULL buffer");
    if (W <= 0 || W > (int)CQ_REG_WIDTH_MAX)
        cq_kernel_die("ibits: width outside [1, CQ_REG_WIDTH_MAX]");

    switch (f) {
    case CQ_IBF_CTPOP:      return ctpop(W, out);
    case CQ_IBF_CTLZ:       return ctlz_cttz(W, 0, out);
    case CQ_IBF_CTTZ:       return ctlz_cttz(W, 1, out);
    case CQ_IBF_BITREVERSE: return bitreverse(W, out);
    case CQ_IBF_BSWAP:
        /* LLVM's verifier rejects bswap on a width that is not a multiple of
         * 16 and the expansion's `w ÷ 8` (:4926) would silently drop the odd
         * byte of a W = 24 register — a hard error, both configurations. */
        if (W % 16 != 0)
            cq_kernel_die("ibits: bswap width is not a multiple of 16");
        return bswap(W, out);
    case CQ_IBF_N:
        break;
    }
    cq_kernel_die("ibits: program of an unknown intrinsic");
    return 0;
}

const char *cq_ibits_name(cq_ibits_fn f)
{
    switch (f) {
    case CQ_IBF_CTPOP:      return "ctpop";
    case CQ_IBF_CTLZ:       return "ctlz";
    case CQ_IBF_CTTZ:       return "cttz";
    case CQ_IBF_BITREVERSE: return "bitreverse";
    case CQ_IBF_BSWAP:      return "bswap";
    case CQ_IBF_N:          break;
    }
    cq_kernel_die("ibits: name of an unknown intrinsic");
    return "";
}
