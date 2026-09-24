/* tests/test_kernel_fmisc_anchors.inc — PRD-v2 §7.12's anchors for K24's five
 * kernels. Included by test_kernel_fmisc.c BEFORE the shape functions.
 *
 * TWO TABLES PER SHAPE, AND THEY HAVE DIFFERENT JOBS (§7.12 at HEAD).
 *
 *   THE CIRCUIT SET — exactly eight rows per shape, each picked because it
 *   separates a row of K24 from its NEIGHBOUR: `fmin` from `fminimum`
 *   (fmin(qNaN, 1) = 1, where the NaN-propagating sibling gives NaN), `fmin`
 *   from `fmax`, the ±0 tie-break in BOTH orders, the both-NaN row's
 *   CANONICAL 0x7FF8… against either payload, an sNaN in EACH position,
 *   copysign(a, b) against copysign(b, a), `fneg` NOT quietening an sNaN,
 *   `fabs` on −0 and on a negative NaN. The sampler replays these eight at
 *   three mask rows inside the fixed 32-case budget; no shape raises it.
 *
 *   THE CLASSICAL TABLE — the generic §7.12 rows (tests/support/fpanchors.c)
 *   plus the circuit set, checked against the oracle through the cheap
 *   classical path only. Adding a row there never adds a circuit.
 *
 * NO ARITHMETIC, ONLY LITERALS, for fpanchors.c's reason: a constant computed
 * as `1.0 / 0.0` would be a host probe, not a table.
 */

#define FM_TWO       0x4000000000000000ull
#define FM_NEG_SNAN  0xfff0000000000001ull   /* −sNaN, payload 1 */

typedef struct { uint64_t a, b; } fm_pair;

/* ---- fmin / fmax: eight ordered pairs, shared by both programs. -------- */

static const fm_pair FMINMAX_CIRCUIT[] = {
    /* `a_nan` fires: the OTHER operand comes back. soft_fminimum would give
     * the canonical NaN here — the row that separates the two families. */
    { CQ_F64_QNAN_A,          CQ_F64_ONE             },
    /* An sNaN in the SECOND position: `b_nan` fires, `a` comes back bit-exact
     * and nothing is quietened, because the NaN is not returned at all. */
    { CQ_F64_ONE,             CQ_F64_SNAN            },
    /* An sNaN in the FIRST position. */
    { CQ_F64_SNAN,            FM_TWO                 },
    /* BOTH NaN, distinguishable payloads: the answer is 0x7FF8000000000000,
     * which is NEITHER payload and is POSITIVE where x86's INDEF is not. */
    { CQ_F64_QNAN_A,          CQ_F64_QNAN_B          },
    /* ±0 in BOTH orders: `both_zero` with `a_neg` false and then true. min
     * returns −0 and max +0 on both rows, which only an order-blind tie-break
     * achieves — `ifelse(a_neg, a, b)` read backwards gets exactly one wrong. */
    { CQ_F64_POS_ZERO,        CQ_F64_NEG_ZERO        },
    { CQ_F64_NEG_ZERO,        CQ_F64_POS_ZERO        },
    /* ONE operand zero and the other not: `both_zero` FALSE with `abs_a == 0`
     * TRUE, and `a < b` — so it also separates fmin from fmax and pins the
     * `ifelse(a_lt_b, a, b)` arm order. MEASURED as the only L1 row that sees
     * an `abs_b` view wired to `a` (K24's battery): `(1, 2)` stood here first
     * and that mutant survived every value case. */
    { CQ_F64_POS_ZERO,        FM_TWO                 },
    /* A NEGATIVE pair with `a > b`: `olt`'s `neg_lt` branch, and the row where
     * a dropped operand swap in fmax's `olt(b, a)` changes the answer. */
    { CQ_F64_NEG(CQ_F64_ONE), CQ_F64_NEG(FM_TWO)     }
};

/* ---- copysign: eight ordered pairs. ------------------------------------ */

static const fm_pair COPYSIGN_CIRCUIT[] = {
    { CQ_F64_ONE,             CQ_F64_NEG_ZERO        },   /* → −1               */
    { CQ_F64_NEG_ZERO,        CQ_F64_ONE             },   /* → +0: the SWAP row */
    { CQ_F64_QNAN_A,          CQ_F64_NEG(CQ_F64_ONE) },   /* payload survives   */
    { CQ_F64_SNAN,            CQ_F64_NEG_INF         },   /* NOT quietened      */
    { CQ_F64_NEG(FM_TWO),     CQ_F64_POS_ZERO        },   /* → +2               */
    { CQ_F64_NEG_INF,         CQ_F64_QNAN_B          },   /* sign from a NaN    */
    { CQ_F64_ONE,             CQ_F64_DEFAULT_NAN     },   /* INDEF is NEGATIVE  */
    { CQ_F64_MIN_SUBNORMAL,   CQ_F64_NEG(CQ_F64_MAX) }
};

/* ---- fneg and fabs: eight singles each. -------------------------------- */

static const uint64_t FNEG_CIRCUIT[] = {
    CQ_F64_SNAN,              /* → 0xfff0000000000001: NOT quietened       */
    CQ_F64_POS_ZERO,          /* → −0                                       */
    CQ_F64_NEG_ZERO,          /* → +0                                       */
    CQ_F64_DEFAULT_NAN,       /* → 0x7ff8000000000000: a NaN's sign flips   */
    CQ_F64_QNAN_A,
    CQ_F64_POS_INF,
    CQ_F64_ONE,
    CQ_F64_NEG(CQ_F64_MIN_SUBNORMAL)
};

static const uint64_t FABS_CIRCUIT[] = {
    CQ_F64_NEG_ZERO,          /* → +0                                       */
    CQ_F64_DEFAULT_NAN,       /* a NEGATIVE NaN → 0x7ff8000000000000        */
    FM_NEG_SNAN,              /* → 0x7ff0000000000001: NOT quietened        */
    CQ_F64_POS_ZERO,
    CQ_F64_NEG_INF,
    CQ_F64_NEG(CQ_F64_ONE),
    CQ_F64_ONE,
    CQ_F64_NEG(CQ_F64_MAX_SUBNORMAL)
};

#define FM_N(t) ((int)(sizeof (t) / sizeof (t)[0]))

_Static_assert(FM_N(FMINMAX_CIRCUIT) == 8 && FM_N(COPYSIGN_CIRCUIT) == 8
               && FM_N(FNEG_CIRCUIT) == 8 && FM_N(FABS_CIRCUIT) == 8,
               "PRD-v2 §7.12: at most eight circuit anchors per shape, and "
               "each of these eight is named for the neighbour it separates");

/* The two-call contract (kerneldrv.h). IT OVERWRITES, IT DOES NOT FILL: only
 * the operands the shape has are written, so nothing is zero-filled. */
static int fm_pairs(const fm_pair *t, int n, int W, int i, cq_ref_w *v)
{
    if (W != CQ_FP64_W) return 0;
    if (i < 0)          return n;
    if (i >= n || v == NULL) return 0;
    v[0] = cq_ref_w_make(t[i].a, 0u, CQ_FP64_W);
    v[1] = cq_ref_w_make(t[i].b, 0u, CQ_FP64_W);
    return 1;
}

static int fm_singles(const uint64_t *t, int n, int W, int i, cq_ref_w *v)
{
    if (W != CQ_FP64_W) return 0;
    if (i < 0)          return n;
    if (i >= n || v == NULL) return 0;
    v[0] = cq_ref_w_make(t[i], 0u, CQ_FP64_W);
    return 1;
}

static int fminmax_anchors(int W, int i, cq_ref_w *v)
{ return fm_pairs(FMINMAX_CIRCUIT, FM_N(FMINMAX_CIRCUIT), W, i, v); }
static int copysign_anchors(int W, int i, cq_ref_w *v)
{ return fm_pairs(COPYSIGN_CIRCUIT, FM_N(COPYSIGN_CIRCUIT), W, i, v); }
static int fneg_anchors(int W, int i, cq_ref_w *v)
{ return fm_singles(FNEG_CIRCUIT, FM_N(FNEG_CIRCUIT), W, i, v); }
static int fabs_anchors(int W, int i, cq_ref_w *v)
{ return fm_singles(FABS_CIRCUIT, FM_N(FABS_CIRCUIT), W, i, v); }

/* The CLASSICAL table of a binary shape: the generic 25 first, so their
 * indices are stable, then the shape's own circuit set. */
static int fm_full_binary(cq_kd_anchor_fn own, int i, cq_ref_w *v)
{
    int base = cq_fp_anchors_binary_count();

    if (i < 0)    return base + own(CQ_FP64_W, -1, NULL);
    if (i < base) return cq_fp_anchors_binary(CQ_FP64_W, i, v);
    return own(CQ_FP64_W, i - base, v);
}

static int fm_full_unary(cq_kd_anchor_fn own, int i, cq_ref_w *v)
{
    int base = cq_fp_anchors_unary_count();

    if (i < 0)    return base + own(CQ_FP64_W, -1, NULL);
    if (i < base) return cq_fp_anchors_unary(CQ_FP64_W, i, v);
    return own(CQ_FP64_W, i - base, v);
}
