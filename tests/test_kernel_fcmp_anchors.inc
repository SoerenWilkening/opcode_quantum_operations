/* tests/test_kernel_fcmp_anchors.inc — §7.12's anchor list for `fcmp`, as a
 * provider COMPOSING tests/support/fpanchors.c's generic binary one. Included
 * by test_kernel_fcmp.c BEFORE the shape function that installs it.
 *
 * WHY A PER-KERNEL PROVIDER AT ALL. fpanchors.h says it outright: the generic
 * table "names no per-kernel special case — the `a_nan` / `a_inf` / `a_zero` /
 * `swap` / `d >= 56` predicates of a particular Julia source are that kernel's
 * own provider's job, composing this one". `soft_fcmp_olt` names EIGHT such
 * predicates (`a_nan`, `b_nan`, `either_nan`, `both_zero`, `pos_lt`, `neg_lt`,
 * `diff_sign_lt`, `same_sign`) plus the unnamed `sa == 0` branch selector at
 * fcmp.jl:40, and each needs a pair that makes it TRUE and one that makes it
 * FALSE. The generic table covers the NaN and ±0 rows; everything below is a
 * row it has no reason to carry.
 *
 * IT OVERWRITES, IT DOES NOT FILL (kerneldrv.h). The sampler draws a value per
 * operand first and this provider replaces v[0] and v[1]; a third operand, if
 * a future kernel composed this provider, would stay drawn rather than pinned
 * at zero — the recorded cq_kd_case2 trap.
 *
 * NO ARITHMETIC, ONLY LITERALS, for fpanchors.c's reason: a constant computed
 * as `1.0 / 0.0` would be a host probe rather than a table, and this file must
 * read the same on every host.
 */

/* 2.0 and its negation. The rest come from fpanchors.h, so a reader checks a
 * row against that file rather than against a second copy of ±0 and the NaNs. */
#define FC_TWO  0x4000000000000000ull

/* THE ROWS, EACH NAMING THE fcmp.jl PREDICATE IT DISCRIMINATES. An anchor list
 * whose rows are not traceable to the source is one nobody can audit for an
 * omission, and an omitted anchor fails by not existing. */
static const struct { uint64_t a, b; } FCMP_PAIRS[] = {
    /* `diff_sign_lt = (sa > sb)` (fcmp.jl:36), BOTH WAYS. This is the row that
     * is easiest to invert: the port spells it `ult(sb, sa)` — operands
     * EXCHANGED — and `ult(sa, sb)` is the exact negation, which gets every
     * same-sign pair right and every mixed-sign pair backwards. One order
     * alone passes under the inverted spelling. */
    { CQ_F64_NEG(CQ_F64_ONE), CQ_F64_ONE            },
    { CQ_F64_ONE,             CQ_F64_NEG(CQ_F64_ONE) },

    /* `pos_lt = abs_a < abs_b` (:32), both ways: `same_sign` TRUE with
     * `sa == 0` TRUE, so the inner `ifelse` takes its `t` arm. */
    { CQ_F64_ONE,             FC_TWO                 },
    { FC_TWO,                 CQ_F64_ONE             },

    /* `neg_lt = abs_a > abs_b` (:33), both ways: `same_sign` TRUE with
     * `sa == 0` FALSE, so the inner `ifelse` takes its `f` arm. THE MUX ARM
     * SWAP IS OTHERWISE INVISIBLE — mux(c,t,f) and mux(c,f,t) emit the
     * identical tuple at every width and mask and keep the palindrome — so
     * these two rows and the two above are the only instrument for it. */
    { CQ_F64_NEG(CQ_F64_ONE), CQ_F64_NEG(FC_TWO)     },
    { CQ_F64_NEG(FC_TWO),     CQ_F64_NEG(CQ_F64_ONE) },

    /* TWO BIT-IDENTICAL NaNs: `a == b` TRUE at :76 AND `either_nan` TRUE at
     * :79. It pins the ORDER of those two lines — the override must win, so
     * `oeq` is 0 — and no other row makes both true at once. */
    { CQ_F64_QNAN_A,          CQ_F64_QNAN_A          },

    /* EQUALITY ON AN ORDERED PAIR. It is all that separates `ogt` from `oge`
     * and `ugt` from `uge`: `oge = ole(b,a)` and `ogt = olt(b,a)` differ on
     * exactly the inputs where a == b, and pairing `oge` with `olt` is
     * invisible to every structural check. */
    { CQ_F64_ONE,             CQ_F64_ONE             },

    /* `both_zero` FALSE with `sa != sb` — one operand a signed zero and the
     * other finite, both orders. Separates the `both_zero` override from the
     * `same_sign` branch, which the (±0, ±0) rows of the generic table cannot. */
    { CQ_F64_NEG_ZERO,        CQ_F64_ONE             },
    { CQ_F64_ONE,             CQ_F64_NEG_ZERO        },

    /* A subnormal against +0: an ordinary magnitude here, and `both_zero`
     * FALSE although one side is zero. The NEGATIVE subnormal is the row that
     * separates an exponent view of lanes 52..62 from one that swallowed the
     * sign lane — M31's recorded finding, reached here through `abs_a`. */
    { CQ_F64_MIN_SUBNORMAL,               CQ_F64_POS_ZERO },
    { CQ_F64_NEG(CQ_F64_MIN_SUBNORMAL),   CQ_F64_POS_ZERO }
};

enum { N_FCMP_PAIRS = (int)(sizeof FCMP_PAIRS / sizeof FCMP_PAIRS[0]) };

/* Pinned so a row lost to an editing accident breaks the BUILD rather than
 * quietly shrinking the forced set. */
_Static_assert(N_FCMP_PAIRS == 12,
               "K18.md §5.2's fcmp-specific anchor list is 12 ordered pairs; "
               "adding or removing one is a deliberate act");

/* The two-call contract of kerneldrv.h, with the generic rows first so their
 * indices are stable and a composing provider could offset past both. */
static int fcmp_anchors(int W, int i, cq_ref_w *v)
{
    int base = cq_fp_anchors_binary_count();

    if (W != CQ_FP64_W) return 0;              /* f64 only, as the generic is */
    if (i < 0)          return base + N_FCMP_PAIRS;
    if (i < base)       return cq_fp_anchors_binary(W, i, v);
    if (i >= base + N_FCMP_PAIRS || v == NULL) return 0;

    v[0] = cq_ref_w_make(FCMP_PAIRS[i - base].a, 0u, CQ_FP64_W);
    v[1] = cq_ref_w_make(FCMP_PAIRS[i - base].b, 0u, CQ_FP64_W);
    return 1;
}
