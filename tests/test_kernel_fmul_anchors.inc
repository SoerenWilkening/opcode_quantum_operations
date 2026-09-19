/* tests/test_kernel_fmul_anchors.inc — PRD-v2 §7.12's anchor list for `fmul`,
 * refined against `soft_fmul`'s own named predicates (K16.md §5.x), as a
 * provider COMPOSING tests/support/fpanchors.c's generic binary one. Included
 * by test_kernel_fmul.c BEFORE the shape function that installs it.
 *
 * WHY A PER-KERNEL PROVIDER AT ALL. fpanchors.h says it outright: the generic
 * table "names no per-kernel special case — the `a_nan` / `a_inf` / `a_zero` /
 * `swap` / `d >= 56` predicates of a particular Julia source are that kernel's
 * own provider's job, composing this one". `soft_fmul` names `a_nan`, `b_nan`,
 * `a_inf`, `b_inf`, `a_zero`, `b_zero` and `msb_at_105`, and across the M32
 * boundary `m_zero`, `subnormal`, `flush_to_zero`, `exp_overflow`,
 * `exp_overflow_after_round`, `round_up` and `mant_overflow`. Each needs an
 * input that makes it TRUE, because each is a branch of the `ifelse` tree that
 * a random draw of 32 reaches with no guarantee — and four of them (the ties,
 * the carries and the flush band) a random draw reaches with probability
 * ~2^-53, which is to say never.
 *
 * NO ARITHMETIC, ONLY LITERALS, for fpanchors.c's reason: a constant computed
 * as `1.0 / 0.0` would be a host probe rather than a table, and this file must
 * read the same on every host. The four constructed rows below were found by
 * a throwaway probe that computes the exact 106-bit product with `__int128`
 * and reads off `grs`, `mant_overflow`, `c1`, `c2` and `shift_sub` — an
 * INDEPENDENT derivation, not a reading of src/kernels/fmul.c — and then
 * written down as the bit patterns it printed. K16.md §5.x states rows 19 and
 * 21-27 as REQUIREMENTS rather than patterns for exactly that reason; these
 * are the patterns that satisfy them, and §6.3's [DERIVED] status for them is
 * discharged.
 *
 * THE DEAD REGION'S ANCHOR IS ROW 21, AND IT IS THE SAME PAIR AS `c1`. Read
 * `fmul.jl:94-102` beside `:126-129`: `prod_lo = pp_ll + cross_lo` IS
 * `sum1 = acc_lo + term2` and `carry_lo = prod_lo < pp_ll` IS `c1`, operator
 * for operator. So "the inputs the dead arm would have selected" are exactly
 * the inputs that make `c1` true, and the pair below drives both. That the two
 * regions are bit-identical computations is WHY the region is dead and is not
 * an argument for deleting it (PRD-v2 §7.6).
 */

/* Values this file needs that fpanchors.h does not name. Everything else comes
 * from there, so a reader checks a row against that file rather than against a
 * second copy of the NaNs and the zeroes. */
#define FM_TWO        0x4000000000000000ull   /* 2.0                        */
#define FM_HALF       0x3fe0000000000000ull   /* 0.5                        */
#define FM_ONE_HALF   0x3ff8000000000000ull   /* 1.5                        */
#define FM_PI         0x400921fb54442d18ull   /* an arbitrary odd normal    */

/* 2^-540 squared is 2^-1080, so `result_exp` is -57 and `shift_sub` is 58 —
 * inside the [56, 60] band `softfloat_common.jl:159-161` says `soft_fmul`
 * exercises in ~0.4% of calls and `soft_fadd` cannot reach at all (K15.md).
 * THIS ROW IS MANDATORY FOR K16 AND ABSENT FROM K15. */
#define FM_2_POW_M540 0x1e30000000000000ull

/* 2^-537 x 2^-538 = 2^-1075, exactly half the smallest subnormal, which RTNE
 * sends to +0 — the verifiable neighbour of the band above. */
#define FM_2_POW_M537 0x1e60000000000000ull
#define FM_2_POW_M538 0x1e50000000000000ull

/* `grs > 4`: a round-up that is NOT a tie. The same pair makes `c2` true, so
 * the second carry of the 106-bit assembly (fmul.jl:133) has an anchor. */
#define FM_GRS4_A     0x3ffd9c28a9741a02ull
#define FM_GRS4_B     0x3ffd14d4430c32b6ull

/* `c1` (fmul.jl:129) — a real carry out of the low 64 bits — and with it the
 * dead region's `carry_lo` at :97. */
#define FM_C1_A       0x3ffc5561c2d77749ull
#define FM_C1_B       0x3ff4592d0da82be7ull

/* THE TWO TIE ROWS ARE A CONSTRUCTION, NOT A SEARCH. Both operands are odd
 * integers below 2^27, so the exact product is an odd integer of 54 bits: the
 * retained significand is its top 53 and the guard bit is its low one, with
 * NOTHING below — `grs == 4` by construction. Which way it rounds is then bit
 * 1 of that product, i.e. the parity of the retained significand.
 *   67108867 x 134217723 = 9007199321849841, bit 1 clear -> rounds DOWN
 *   67108865 x 134217727 = 9007199321849855, bit 1 set   -> rounds UP       */
#define FM_TIEDN_A    0x419000000c000000ull   /* 67108867.0                 */
#define FM_TIEDN_B    0x419fffffec000000ull   /* 134217723.0                */
#define FM_TIEUP_A    0x4190000004000000ull   /* 67108865.0                 */
#define FM_TIEUP_B    0x419ffffffc000000ull   /* 134217727.0                */

/* `mant_overflow`: (2^27-1)(2^27+1) = 2^54-1, so the retained significand is
 * all ones and the tie rounds up, carrying into the exponent. */
#define FM_MANTOVF_A  0x419ffffffc000000ull   /* 2^27 - 1                   */
#define FM_MANTOVF_B  0x41a0000002000000ull   /* 2^27 + 1                   */

/* The same product scaled by 2^970 so the PRE-round exponent is exactly 0x7FE
 * and the carry pushes it to 0x7FF: `exp_overflow` FALSE and
 * `exp_overflow_after_round` TRUE, which is the only pair of flags in
 * `_sf_round_and_pack` that a plain overflow row cannot separate. */
#define FM_OVFAFT_A   0x5feffffffc000000ull
#define FM_OVFAFT_B   0x5ff0000002000000ull

/* THE ROWS, EACH NAMING THE fmul.jl PREDICATE IT DISCRIMINATES. An anchor list
 * whose rows are not traceable to the source is one nobody can audit for an
 * omission, and an omitted anchor fails by not existing. */
static const struct { uint64_t a, b; } FMUL_PAIRS[] = {
    /* `(a_inf | b_inf) & (a_zero | b_zero)` at :210 — the INDEF arm. The
     * generic table has the Inf rows and the zero rows and NOT their product,
     * so these three are K16's alone. INDEF is NOT result_sign-signed: all
     * three give 0xFFF8000000000000 whatever the operand signs are, and a
     * port that "tidied" it into `(result_sign << 63) | QNAN` is wrong on
     * exactly half its inputs. */
    { CQ_F64_POS_INF,        CQ_F64_POS_ZERO       },
    { CQ_F64_POS_ZERO,       CQ_F64_NEG_INF        },
    { CQ_F64_NEG_ZERO,       CQ_F64_POS_INF        },

    /* `(a_inf | b_inf) & !(a_zero | b_zero)` at :211, both positions, with a
     * SIGN FLIP so the row also pins `result_sign = sa ^ sb` on the Inf path. */
    { CQ_F64_POS_INF,        CQ_F64_NEG(FM_TWO)    },
    { CQ_F64_NEG(CQ_F64_ONE), CQ_F64_POS_INF       },

    /* `a_zero` alone at :209: a signed zero times an ordinary finite. */
    { CQ_F64_NEG_ZERO,       FM_TWO                },

    /* The whole normal path with no rounding at all — `1.0 * x = x` — and an
     * exact power of two on each side. If these three fail, nothing further
     * down the list means anything. */
    { CQ_F64_ONE,            FM_PI                 },
    { FM_TWO,                FM_TWO                },
    { FM_TWO,                FM_PI                 },

    /* `msb_at_105` TRUE and FALSE — A MATCHED PAIR, and the ONLY instrument
     * for the arm swap at :186. `mux(c,t,f)` and `mux(c,f,t)` emit the
     * identical (X, CX, CCX) tuple at every width and mask, keep the
     * palindrome and leave scratch clean, so neither the slot scan nor L4 nor
     * the span scan can see it; one of these two rows alone passes under the
     * swap, because the swapped mux then agrees on that half of the inputs. */
    { FM_ONE_HALF,           FM_ONE_HALF           },   /* 1.5*1.5 = 2.25   */
    { CQ_F64_ONE,            FM_ONE_HALF           },   /* 1.0*1.5 = 1.5    */

    /* The `_sf_normalize_to_bit52` ladder: a subnormal operand makes it shift,
     * which is the whole reason fmul.jl:51-58 calls it. Both-subnormal
     * underflows to zero. */
    { CQ_F64_MIN_SUBNORMAL,  FM_TWO                },
    { CQ_F64_MAX_SUBNORMAL,  CQ_F64_ONE            },
    { CQ_F64_MIN_SUBNORMAL,  CQ_F64_MIN_SUBNORMAL  },

    /* `m_zero`, the post-Bennett-tpg0 guard at softfloat_common.jl:69-74. A
     * zero operand reaches the ladder and its result is then discarded by the
     * select chain — PRD-v2 §7.6's discarded arm, one helper down. */
    { CQ_F64_POS_ZERO,       FM_PI                 },

    /* `flush_to_zero`: the Bennett-xiqt band, `shift_sub` in [56, 60], and its
     * verifiable neighbour at exactly half the smallest subnormal. */
    { FM_2_POW_M540,         FM_2_POW_M540         },
    { FM_2_POW_M537,         FM_2_POW_M538         },

    /* The normal -> subnormal boundary from above. */
    { CQ_F64_MIN_NORMAL,     FM_HALF               },

    /* `exp_overflow` at :200 — overflow to Inf before rounding — and
     * `exp_overflow_after_round` at :220, which only the carry row reaches. */
    { CQ_F64_MAX,            FM_TWO                },
    { FM_OVFAFT_A,           FM_OVFAFT_B           },

    /* Rounding: `grs > 4`, the two tie directions, and the tie that carries. */
    { FM_GRS4_A,             FM_GRS4_B             },
    { FM_TIEDN_A,            FM_TIEDN_B            },
    { FM_TIEUP_A,            FM_TIEUP_B            },
    { FM_MANTOVF_A,          FM_MANTOVF_B          },

    /* `c1` at :129 — and the dead region's `carry_lo` at :97, which is the
     * same computation (see this file's header). `c2` at :133 is the GRS row
     * above. */
    { FM_C1_A,               FM_C1_B               }
};

enum { N_FMUL_PAIRS = (int)(sizeof FMUL_PAIRS / sizeof FMUL_PAIRS[0]) };

/* Pinned so a row lost to an editing accident breaks the BUILD rather than
 * quietly shrinking the forced set. */
_Static_assert(N_FMUL_PAIRS == 25,
               "K16.md §5.x's fmul-specific anchor list is 25 ordered pairs; "
               "adding or removing one is a deliberate act");

/* The two-call contract of kerneldrv.h, with the generic rows first so their
 * indices are stable and a composing provider could offset past both. IT
 * OVERWRITES, IT DOES NOT FILL: the sampler draws a value per operand first
 * and this provider replaces v[0] and v[1] only. */
static int fmul_anchors(int W, int i, cq_ref_w *v)
{
    int base = cq_fp_anchors_binary_count();

    if (W != CQ_FP64_W) return 0;              /* f64 only, as the generic is */
    if (i < 0)          return base + N_FMUL_PAIRS;
    if (i < base)       return cq_fp_anchors_binary(W, i, v);
    if (i >= base + N_FMUL_PAIRS || v == NULL) return 0;

    v[0] = cq_ref_w_make(FMUL_PAIRS[i - base].a, 0u, CQ_FP64_W);
    v[1] = cq_ref_w_make(FMUL_PAIRS[i - base].b, 0u, CQ_FP64_W);
    return 1;
}
