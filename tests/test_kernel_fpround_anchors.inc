/* tests/test_kernel_fpround_anchors.inc — K23 §5.10's anchors, PRD-v2 §7.12's
 * "each named predicate gets an anchor that makes it true". Included by
 * test_kernel_fpround.c.
 *
 * AT 32 SAMPLES OVER A 64-BIT VALUE SPACE A UNIFORM DRAW REACHES A TIE, A
 * FLUSH OR A MANTISSA CARRY-OUT WITH PROBABILITY ~0, and every one of those is
 * a branch of the `ifelse` tree. So the anchors are FORCED INTO the draw and
 * sit INSIDE the budget rather than on top of it, exactly as cq_kd_samples()'
 * four corners do for a v1 kernel.
 *
 * ALL SIX CLZ STAGES CAN NEVER FIRE TOGETHER IN EITHER LADDER, so there is no
 * "every need_k true" row here and an anchor list demanding one would list a
 * row that cannot go red. The stages that fire are exactly the binary
 * representation of the shift count `s`, and `s` is bounded by the distance
 * from lane 0 to the target bit: `s` in [0, 52] for `_sf_normalize_to_bit52`
 * and [0, 55] for `_sf_normalize_clz`, while all six together are 63. The
 * largest reachable popcount is FIVE in both, and it is reached at DIFFERENT
 * shifts — `s = 31` is 16+8+4+2+1 and `s = 47` is 32+8+4+2+1 — so both are
 * listed for each ladder rather than assumed to be the same row.
 * `bd clz-stages-all-six-never-fire-so-that-anchor-cannot-go-red`.
 *
 * AN ANCHOR HERE IS NOT A `double`. M32's inputs are a working-format `wr` and
 * an UNBIASED `result_exp`, not an IEEE bit pattern, so §7.12's ±0 / ±Inf /
 * NaN anchors belong to the CONSUMERS and reach M32 only through their select
 * chains. That is K23 §6.5 risk 1 in one sentence, and it is why
 * tests/support/fpanchors.c is not used in this file.
 */

typedef struct { uint64_t v[3]; const char *what; } fpr_anchor;

/* `e` is written as a decimal and converted by fpr_u64, so a negative exponent
 * reads as one rather than as a hex pattern. */
static uint64_t fpr_u64(int64_t v) { uint64_t r; memcpy(&r, &v, sizeof r); return r; }
static int64_t  fpr_i64(uint64_t v) { int64_t r; memcpy(&r, &v, sizeof r); return r; }

/* --- `_sf_normalize_to_bit52` (m, e). ------------------------------------- */

/* The `m` column is a single set bit at the position named, so the shift each
 * row forces is `52 - position` and the stages that fire are that number's
 * bits. Rows 2..8 fire one stage each; rows 9 and 10 are the two five-stage
 * maxima; row 11 is the longest shift the domain allows. */
static const fpr_anchor A_NORM52[] = {
    { { 0u,                        0u, 0u }, "m_zero (tpg0's guard)"          },
    { { UINT64_C(1) << 52,         0u, 0u }, "no stage fires"                 },
    { { UINT64_C(1) << 20,         0u, 0u }, "need32 alone"                   },
    { { UINT64_C(1) << 36,         0u, 0u }, "need16 alone"                   },
    { { UINT64_C(1) << 44,         0u, 0u }, "need8 alone"                    },
    { { UINT64_C(1) << 48,         0u, 0u }, "need4 alone"                    },
    { { UINT64_C(1) << 50,         0u, 0u }, "need2 alone"                    },
    { { UINT64_C(1) << 51,         0u, 0u }, "need1 alone"                    },
    { { UINT64_C(1) << 21,         0u, 0u }, "s = 31, five stages"            },
    { { UINT64_C(1) << 5,          0u, 0u }, "s = 47, five stages"            },
    { { UINT64_C(1),               0u, 0u }, "s = 52, the longest shift"      },
    { { UINT64_C(0x1FFFFFFFFFFFFF), 0u, 0u }, "every lane of the domain set"  },
    { { UINT64_C(1) << 52,         0u, 0u }, "a negative exponent"            }
};

/* --- `_sf_normalize_clz` (wr, result_exp). -------------------------------- */

static const fpr_anchor A_CLZ[] = {
    { { UINT64_C(1) << 55,          0u, 0u }, "no stage fires"                },
    { { UINT64_C(1) << 23,          0u, 0u }, "need32 alone"                  },
    { { UINT64_C(1) << 39,          0u, 0u }, "need16 alone"                  },
    { { UINT64_C(1) << 47,          0u, 0u }, "need8 alone"                   },
    { { UINT64_C(1) << 51,          0u, 0u }, "need4 alone"                   },
    { { UINT64_C(1) << 53,          0u, 0u }, "need2 alone"                   },
    { { UINT64_C(1) << 54,          0u, 0u }, "need1 alone"                   },
    { { UINT64_C(1) << 24,          0u, 0u }, "s = 31, five stages"           },
    { { UINT64_C(1) << 8,           0u, 0u }, "s = 47, five stages"           },
    { { UINT64_C(1),                0u, 0u }, "s = 55, the longest shift"     },
    { { 0u,                         0u, 0u }, "wr == 0: all six, value stays" },
    { { UINT64_C(0x00FFFFFFFFFFFFFF), 0u, 0u }, "every lane below bit 56"     },
    { { UINT64_C(1) << 55,          0u, 0u }, "a negative exponent"           }
};

/* --- `_sf_handle_subnormal` (wr, result_exp, result_sign). ----------------- */

/* THE CLAMP AT :178 IS VALUE-INERT ON EVERY REACHABLE INPUT, AND THAT IS A
 * FINDING RATHER THAN A GAP. `shift_clamped` reaches `shift_u` only when
 * `subnormal` is true and `flush_to_zero` is false — that is
 * `result_exp <= 0` with `1 - result_exp <= 55`, so `shift_sub` is in [1, 55]
 * and both clamp arms are the identity there. Rows 6 and 8 below make the two
 * arms FIRE, which is what §5.10 row 10 asks for and what the SLOT scan pins;
 * no L1 comparison can see a mis-ordered or mis-signed clamp at THIS site, and
 * `_sf_round_and_pack`'s clamp (rows 10 and 11 of A_ROUND) is where it can.
 * PRD-v2 §7.6's discarded-arm rule applies: still emitted, not a cost to
 * optimise away, and narrowing it would strand the mux that selects it. */
static const fpr_anchor A_SUBNORM[] = {
    { { UINT64_C(0x00FFFFFFFFFFFFFF), 0u, 0u }, "not subnormal: passthrough"  },
    { { UINT64_C(0x00FFFFFFFFFFFFFF), 0u, 1u }, "result_exp == 1, the boundary"},
    { { UINT64_C(0x00FFFFFFFFFFFFFF), 0u, 0u }, "subnormal, shift 1"          },
    { { UINT64_C(0x0080000000000000), 0u, 1u }, "subnormal, shift 2, no sticky"},
    { { UINT64_C(0x0080000000000001), 0u, 0u }, "subnormal, sticky from lane 0"},
    { { UINT64_C(0x00FFFFFFFFFFFFFF), 0u, 0u }, "shift 55, just below the flush"},
    { { UINT64_C(0x00FFFFFFFFFFFFFF), 0u, 1u }, "shift 56: flush_to_zero"     },
    { { UINT64_C(0x00FFFFFFFFFFFFFF), 0u, 0u }, "shift 101: clamp's upper arm"},
    { { UINT64_C(0x0080000000000000), 0u, 1u }, "shift_sub < 0: clamp's lower arm"},
    { { 0u,                           0u, 0u }, "wr == 0, subnormal"          },
    { { UINT64_C(0xDEADBEEFCAFEF00D), 0u, 1u }, "bits above 55 shift in too"  }
};

/* --- `_sf_round_and_pack` (wr, result_exp, result_sign). ------------------ */

/* `FRAC_MASK << 3 | 6` is the carry-out row: `frac` is all ones, G and R are
 * set so `grs = 6 > 4`, and `frac + 1` lands exactly on IMPLICIT. Rows 3, 4
 * and 5 are §1.7's only detectors — a one-bit disagreement about the G/R/S
 * encoding is right on every input that does not round and wrong on the ties. */
static const fpr_anchor A_ROUND[] = {
    { { UINT64_C(0x0080000000000000), 0u, 0u }, "grs = 0, no rounding"        },
    { { UINT64_C(0x0080000000000006), 0u, 0u }, "grs = 6 > 4: round up"       },
    { { UINT64_C(0x0080000000000002), 0u, 0u }, "grs = 2: the round bit alone does NOT round" },
    { { UINT64_C(0x0080000000000004), 0u, 0u }, "TIE to EVEN: rounds DOWN"    },
    { { UINT64_C(0x008000000000000C), 0u, 0u }, "TIE to ODD: rounds UP"       },
    { { UINT64_C(0x0080000000000005), 0u, 0u }, "grs = 5: the STICKY bit alone rounds up" },
    { { (CQ_FP64_FRAC_MASK << 3) | 6u, 0u, 0u }, "mant_overflow: the carry-out bump" },
    { { (CQ_FP64_FRAC_MASK << 3) | 6u, 0u, 0u }, "exp_overflow_after WITHOUT exp_overflow" },
    { { UINT64_C(0x0080000000000000), 0u, 0u }, "exp_overflow at 0x7FF"       },
    { { UINT64_C(0x0080000000000000), 0u, 0u }, "clamp's LOWER arm: a negative exponent" },
    { { UINT64_C(0x0080000000000000), 0u, 0u }, "clamp's UPPER arm above 0x7FE" },
    { { UINT64_C(0x0080000000000000), 0u, 1u }, "result_sign = 1"             },
    { { UINT64_C(0xDEADBEEFCAFEF00D), 0u, 1u }, "a deeply negative exponent"  }
};

/* The exponent column, written as decimals beside the table above rather than
 * as `fpr_u64` calls inside a static initialiser, which C forbids. */
static const int64_t E_NORM52[]  = { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, -3, -700 };
static const int64_t E_CLZ[]     = { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, -3, -700 };
static const int64_t E_SUBNORM[] = { 5, 1, 0, -1, -20, -54, -55, -100, 100, 0, -7 };
static const int64_t E_ROUND[]   = { 1023, 1023, 1023, 1023, 1023, 1023, 1023,
                                     0x7FE, 0x7FF, -5, 0x900, 1023, -1000 };

static int fpr_anchor_count(cq_fpround_id id)
{
    switch (id) {
    case CQ_FPR_NORM52:  return (int)(sizeof A_NORM52  / sizeof A_NORM52[0]);
    case CQ_FPR_CLZ:     return (int)(sizeof A_CLZ     / sizeof A_CLZ[0]);
    case CQ_FPR_SUBNORM: return (int)(sizeof A_SUBNORM / sizeof A_SUBNORM[0]);
    default:             return (int)(sizeof A_ROUND   / sizeof A_ROUND[0]);
    }
}

/* Each table's exponent column must be exactly as long as its value column; a
 * row added to one and not the other would silently read past the end. */
_Static_assert(sizeof E_NORM52  / sizeof E_NORM52[0]
               == sizeof A_NORM52 / sizeof A_NORM52[0], "norm52 anchors");
_Static_assert(sizeof E_CLZ     / sizeof E_CLZ[0]
               == sizeof A_CLZ / sizeof A_CLZ[0], "clz anchors");
_Static_assert(sizeof E_SUBNORM / sizeof E_SUBNORM[0]
               == sizeof A_SUBNORM / sizeof A_SUBNORM[0], "subnormal anchors");
_Static_assert(sizeof E_ROUND   / sizeof E_ROUND[0]
               == sizeof A_ROUND / sizeof A_ROUND[0], "round anchors");

static const char *fpr_anchor_at(cq_fpround_id id, int i, uint64_t *v)
{
    const fpr_anchor *t;
    const int64_t *e;

    switch (id) {
    case CQ_FPR_NORM52:  t = A_NORM52;  e = E_NORM52;  break;
    case CQ_FPR_CLZ:     t = A_CLZ;     e = E_CLZ;     break;
    case CQ_FPR_SUBNORM: t = A_SUBNORM; e = E_SUBNORM; break;
    default:             t = A_ROUND;   e = E_ROUND;   break;
    }
    v[0] = t[i].v[0];
    v[1] = fpr_u64(e[i]);
    v[2] = t[i].v[2];
    return t[i].what;
}

/* The random half of the budget, drawn per block at that block's DOMAIN.
 * `_sf_normalize_clz` wants a leading 1 at or below bit 55 and
 * `_sf_normalize_to_bit52` no bit above 52 — softfloat_common.jl:46-48's own
 * precondition — and outside those a counting loop and a six-stage binary
 * search over masks that stop at the target bit are genuinely different
 * functions, so a draw there would be testing neither (fpref.h's domains).
 * The other two take any 64-bit `wr`: both the port and the model shift the
 * whole word, and for round-and-pack bits 56-63 are read by neither, which is
 * itself worth exercising. */
static void fpr_draw(cq_fpround_id id, cq_bk_rng *rng, uint64_t *v)
{
    v[0] = cq_bk_rng_next(rng);
    v[1] = fpr_u64((int64_t)(cq_bk_rng_below(rng, 8192u)) - 4096);
    v[2] = cq_bk_rng_next(rng) & UINT64_C(1);

    if (id == CQ_FPR_NORM52) v[0] &= (UINT64_C(1) << 53) - UINT64_C(1);
    if (id == CQ_FPR_CLZ)    v[0] &= UINT64_C(0x00FFFFFFFFFFFFFF);
}

/* One tuple of the budget: anchor `i` while `i` is inside the anchor list,
 * then a draw. The budget is cq_kd_samples() and the anchors sit INSIDE it. */
static const char *fpr_tuple(cq_fpround_id id, int i, cq_bk_rng *rng,
                             uint64_t *v)
{
    if (i < fpr_anchor_count(id)) return fpr_anchor_at(id, i, v);
    fpr_draw(id, rng, v);
    return "draw";
}
