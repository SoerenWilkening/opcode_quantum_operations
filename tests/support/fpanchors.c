/* tests/support/fpanchors.c — PRD-v2 §7.12's list, as an ordered pair table.
 *
 * The header carries the argument and the constants. This file is the table
 * and the two-call provider contract, and it contains NO arithmetic of any
 * kind: every entry is a pair of literals, so the table is host-independent
 * (§7.4) and a reader can check a row by looking at it.
 *
 * ONE ROW PER §7.12 CLAUSE, AND THE CLAUSE IS NAMED. A table whose rows are
 * not traceable back to the document is a table nobody can audit for an
 * omission, and an omitted anchor is invisible: it fails by not existing.
 */

#include "support/fpanchors.h"

#include <stddef.h>

/* The pairs, in the order §7.12 names them. Both operand POSITIONS and both
 * ORDERS wherever the cell is asymmetric — the first-operand NaN-payload rule,
 * the sNaN/qNaN row and ±0 all are, and a one-order table is blind to an
 * operand swap (K9's `uge`-meaning-`ule`, in a new column). */
static const struct { uint64_t a, b; } FP64_PAIRS[] = {
    /* ±0, both signs, and §7.12's explicitly named `+0 + −0 = +0`. */
    { CQ_F64_POS_ZERO,      CQ_F64_POS_ZERO      },
    { CQ_F64_POS_ZERO,      CQ_F64_NEG_ZERO      },
    { CQ_F64_NEG_ZERO,      CQ_F64_POS_ZERO      },
    { CQ_F64_NEG_ZERO,      CQ_F64_NEG_ZERO      },

    /* ±Inf. The mixed pair is the `Inf − Inf` cell §7.4 pins by table. */
    { CQ_F64_POS_INF,       CQ_F64_POS_INF       },
    { CQ_F64_POS_INF,       CQ_F64_NEG_INF       },
    { CQ_F64_NEG_INF,       CQ_F64_POS_INF       },
    { CQ_F64_NEG_INF,       CQ_F64_NEG_INF       },

    /* Inf against a finite, in each operand position. `0 · Inf` is the other
     * §7.4 cell and is reached by the zero rows above under a multiply. */
    { CQ_F64_POS_INF,       CQ_F64_ONE           },
    { CQ_F64_ONE,           CQ_F64_POS_INF       },

    /* The DEFAULT NaN, in each operand position. */
    { CQ_F64_DEFAULT_NAN,   CQ_F64_ONE           },
    { CQ_F64_ONE,           CQ_F64_DEFAULT_NAN   },

    /* A payload NaN in each position, BOTH ORDERS — the two rows that can see
     * §7.4's "a two-NaN add returns the FIRST operand's payload". */
    { CQ_F64_QNAN_A,        CQ_F64_QNAN_B        },
    { CQ_F64_QNAN_B,        CQ_F64_QNAN_A        },

    /* An sNaN against a qNaN, both orders. QNAN_**B**, never QNAN_A: the rule
     * is "the first operand, quietened if signalling" and quieten(SNAN) IS
     * QNAN_A, so the SNAN/QNAN_A pair prints identical bits under that rule and
     * under the rival "the qNaN wins" reading. That degenerate pair is what
     * made PRD-v2 §7.4's table row wrong (bd 9ve.4); against QNAN_B the two
     * readings give …0001 and …0002, and the two ORDERS together separate
     * "first operand" from "the quiet one". */
    { CQ_F64_SNAN,          CQ_F64_QNAN_B        },
    { CQ_F64_QNAN_B,        CQ_F64_SNAN          },

    /* The subnormal boundary. (max subnormal, min subnormal) sums to EXACTLY
     * the min normal, which is the subnormal→normal carry in round-and-pack. */
    { CQ_F64_MAX_SUBNORMAL, CQ_F64_MIN_SUBNORMAL },
    { CQ_F64_MIN_SUBNORMAL, CQ_F64_MAX_SUBNORMAL },

    /* The smallest normal against each subnormal end — the normal→subnormal
     * borrow, and §7.12's "underflow to a subnormal" as an OPERAND pair. What
     * the pair should PRODUCE is the kernel's own provider's claim; a provider
     * generic over fadd/fsub/fmul/fdiv/fcmp cannot make it. */
    { CQ_F64_MIN_NORMAL,    CQ_F64_MIN_SUBNORMAL },
    { CQ_F64_MIN_NORMAL,    CQ_F64_MAX_SUBNORMAL },

    /* Tie-to-even IN EACH DIRECTION (round bit set, sticky clear). 1.0 has an
     * EVEN trailing significand bit, so `1 + half-ulp` ties DOWN to 1.0;
     * 1+2^-52 has an ODD one, so the same addend ties UP to 1+2^-51. A table
     * with only one of these cannot tell ties-to-even from ties-away, which is
     * §7.7's `soft_round` / `soft_round_away` distinction in another column. */
    { CQ_F64_ONE,           CQ_F64_HALF_ULP_OF_1 },
    { CQ_F64_ONE_PLUS_ULP,  CQ_F64_HALF_ULP_OF_1 },

    /* Overflow to Inf. */
    { CQ_F64_MAX,           CQ_F64_MAX           },

    /* Exact cancellation, at both ends of the range: the sign of a zero
     * produced by cancellation is +0 in round-to-nearest, which is a rule no
     * random draw reaches and one an implementation can get wrong silently. */
    { CQ_F64_MAX,           CQ_F64_NEG(CQ_F64_MAX)           },
    { CQ_F64_MIN_SUBNORMAL, CQ_F64_NEG(CQ_F64_MIN_SUBNORMAL) }
};

enum { N_FP64_PAIRS = (int)(sizeof FP64_PAIRS / sizeof FP64_PAIRS[0]) };

/* The count is pinned so a row lost to an editing accident breaks the BUILD
 * rather than quietly shrinking the forced set — the same instrument
 * test_kernel_cmp.c uses for icmp's ten predicates, and for the same reason:
 * a missing anchor fails by not existing, and nothing can observe that. */
_Static_assert(N_FP64_PAIRS == 25,
               "PRD-v2 §7.12's binary anchor list is 25 ordered pairs; adding "
               "or removing one is a deliberate act, so update this number and "
               "say why in the bead");

int cq_fp_anchors_binary_count(void) { return N_FP64_PAIRS; }

int cq_fp_anchors_binary(int W, int i, cq_ref_w *v)
{
    /* f64 ONLY. Not an error at another width — a provider rides a whole
     * ladder and simply has nothing to say off f64 — which is what keeps the
     * schedule identical to today's wherever it declares zero. */
    if (W != 64) return 0;

    if (i < 0) return N_FP64_PAIRS;
    if (i >= N_FP64_PAIRS || v == NULL) return 0;

    /* Widths come from the SHAPE, not from here: cq_kd_case masks each operand
     * to sh.w[i] anyway, so writing at 64 and letting a narrower operand mask
     * it would silently truncate a NaN into a normal. The W != 64 refusal
     * above is what makes that unreachable rather than merely unlikely. */
    v[0] = cq_ref_w_make(FP64_PAIRS[i].a, 0u, 64);
    v[1] = cq_ref_w_make(FP64_PAIRS[i].b, 0u, 64);
    return 1;
}

/* --- The SINGLES (bd 9ve.18). ------------------------------------------- */

/* One row per §7.12 clause that names a single value, plus the two NEGATIVE
 * rows the header argues for. Literals only, as above: no arithmetic, host-
 * independent, checkable by looking at it. */
static const uint64_t FP64_SINGLES[] = {
    CQ_F64_POS_ZERO,       CQ_F64_NEG_ZERO,
    CQ_F64_POS_INF,        CQ_F64_NEG_INF,
    CQ_F64_DEFAULT_NAN,
    CQ_F64_QNAN_A,         CQ_F64_QNAN_B,
    CQ_F64_SNAN,
    CQ_F64_MAX_SUBNORMAL,  CQ_F64_MIN_SUBNORMAL,
    CQ_F64_MIN_NORMAL,
    CQ_F64_MAX,
    CQ_F64_ONE,

    /* The two that separate an 11-lane exponent view from a 12-lane one. */
    CQ_F64_NEG(CQ_F64_MIN_SUBNORMAL),
    CQ_F64_NEG(CQ_F64_ONE)
};

enum { N_FP64_SINGLES = (int)(sizeof FP64_SINGLES / sizeof FP64_SINGLES[0]) };

_Static_assert(N_FP64_SINGLES == 15,
               "PRD-v2 §7.12's singles plus the two sign rows; adding or "
               "removing one is a deliberate act, so update this number and "
               "say why in the bead");

int cq_fp_anchors_unary_count(void) { return N_FP64_SINGLES; }

int cq_fp_anchors_unary(int W, int i, cq_ref_w *v)
{
    if (W != 64) return 0;

    if (i < 0) return N_FP64_SINGLES;
    if (i >= N_FP64_SINGLES || v == NULL) return 0;

    v[0] = cq_ref_w_make(FP64_SINGLES[i], 0u, 64);
    return 1;
}

/* Eight values x three mask rows leaves six random cases inside the shared
 * 32-case budget after its mandatory all-classical and all-quantum slots. */
enum { N_FP_REPRESENTATIVES = 8 };

int cq_fp_representative_count(int available)
{
    if (available <= 0) return 0;
    return available < N_FP_REPRESENTATIVES ? available : N_FP_REPRESENTATIVES;
}

static int representative_index(const int *base, int nbase,
                                int available, int i)
{
    const int n = cq_fp_representative_count(available);

    if (i < 0 || i >= n) return -1;
    if (available <= N_FP_REPRESENTATIVES) return i;
    if (i < nbase && base[i] < available) return base[i];
    return available - 1;                /* one kernel-specific tail row */
}

int cq_fp_representative_binary_index(int available, int i)
{
    /* FP64_PAIRS indices: +0, +Inf, default NaN, sNaN, subnormal boundary,
     * tie-to-even, overflow.  The eighth row is the provider's own last row. */
    static const int BASE[] = { 0, 4, 10, 14, 16, 20, 22 };

    return representative_index(BASE, (int)(sizeof BASE / sizeof BASE[0]),
                                available, i);
}

int cq_fp_representative_unary_index(int available, int i)
{
    /* FP64_SINGLES indices: +0, +Inf, default NaN, sNaN, max subnormal,
     * min normal, 1.0.  The eighth row is the provider's own last row. */
    static const int BASE[] = { 0, 2, 4, 7, 8, 10, 12 };

    return representative_index(BASE, (int)(sizeof BASE / sizeof BASE[0]),
                                available, i);
}

int cq_fp_representative_spread_index(int available, int i)
{
    const int n = cq_fp_representative_count(available);

    if (i < 0 || i >= n) return -1;
    if (n == 1) return 0;
    return (i * (available - 1)) / (n - 1);
}
