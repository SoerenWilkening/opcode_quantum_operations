/* tests/test_kernel_fround_spec.inc — M38b, K25: the L1 ORACLE, the anchor
 * tables and the five `cq_kd_spec`s, shared by test_kernel_fround.c and
 * test_kernel_fround_fadd.c (which carries floor/ceil's circuit sweeps, the
 * two kernels that drive M33's `soft_fadd` program and so cost ~50k slots more
 * per compute half than the other three).
 *
 * THE ORACLE IS THE HOST libm AND SHARES NOTHING WITH THE PORT — EXCEPT ON THE
 * NaN CELLS, WHICH ARE PINNED BY TABLE. `trunc`, `floor`, `ceil`, `rint`,
 * `nearbyint` and `round` are EXACT IEEE operations (the result is an integer
 * value or the input), so the host is a sound oracle wherever IEEE specifies
 * the result; there is no second operand for a compiler to exchange (bd
 * host-fp-operator-commutes-so-the-nan-payload-is-not-an-oracle does not
 * reach a unary function). What the host does NOT pin is the NaN: upstream
 * passes a NaN through QUIETENED with its SIGN AND PAYLOAD KEPT (fround.jl
 * :20-28, :146-149, :243-246, Bennett-r84x / U08), and `ref_fround` answers
 * every NaN row from that literal rule and never calls the host on one.
 *
 * `rint` AND `nearbyint` ARE BOTH ASKED, and must agree: both are
 * `soft_round` (PRD-v2 §7.7) and both read the host's CURRENT rounding mode,
 * so the host probe (bd 9ve.4, cq_fphost_check) is asserted before either is
 * trusted — a host left in FE_UPWARD would make every tie anchor red in the
 * ORACLE rather than in the port.
 *
 * AND THE NAME MAPPING IS ASSERTED HERE, NOT NARRATED: C `round` is the AWAY
 * kernel and C `rint` the EVEN one. A spec table that paired them the other
 * way round keeps every count, palindrome and pool check green.
 */

#include <math.h>

enum { W64 = CQ_FP64_W };

/* ---- L1's oracle. ------------------------------------------------------- */

static int fr_is_nan(uint64_t u)
{
    return ((u >> 52) & 0x7ffu) == 0x7ffu && (u & CQ_F64_MANT_MASK) != 0u;
}

/* NOTHING HERE IS DERIVED FROM src/kernels/fround*.c. */
static uint64_t ref_fround(cq_fround_prog p, uint64_t a)
{
    double x, r = 0.0;

    if (fr_is_nan(a)) return a | CQ_F64_QUIET_BIT;      /* pinned, see above */
    x = cq_fphost_from_bits(a);
    switch (p) {
    case CQ_FROUND_TRUNC: r = trunc(x); break;
    case CQ_FROUND_FLOOR: r = floor(x); break;
    case CQ_FROUND_CEIL:  r = ceil(x);  break;
    case CQ_FROUND_EVEN:
        r = rint(x);
        if (cq_fphost_bits(r) != cq_fphost_bits(nearbyint(x)))
            cq_h_fail(__FILE__, __LINE__, "host rint and nearbyint disagree "
                      "on %016llx", (unsigned long long)a);
        break;
    case CQ_FROUND_AWAY:  r = round(x); break;
    case CQ_FROUND_N_PROG:
    default:
        cq_h_fail(__FILE__, __LINE__, "ref_fround: program %d", (int)p);
    }
    return cq_fphost_bits(r);
}

/* ---- The complete classical table: the generic 15 plus these. ----------- */

#define FR_HALF        0x3fe0000000000000ull  /*  0.5  tie: even 0, away 1  */
#define FR_NHALF       0xbfe0000000000000ull  /* -0.5  -0 / -1, sign kept   */
#define FR_1P5         0x3ff8000000000000ull  /*  1.5  ceil 2 vs trunc 1    */
#define FR_N1P5        0xbff8000000000000ull  /* -1.5  floor -2 vs trunc -1 */
#define FR_2P5         0x4004000000000000ull  /*  2.5  even 2, away 3       */
#define FR_N2P5        0xc004000000000000ull  /* -2.5  even -2, away -3     */
#define FR_3P5         0x400c000000000000ull  /*  3.5  tie up to EVEN 4     */
#define FR_N3P5        0xc00c000000000000ull
#define FR_0P3         0x3fd3333333333333ull  /*  0.3  ceil 1 via fadd(+0,1) */
#define FR_N0P3        0xbfd3333333333333ull  /* -0.3  floor -1, ceil -0    */
#define FR_0P7         0x3fe6666666666666ull  /*  0.7  (0.5,1) -> 1         */
#define FR_2P52        0x4330000000000000ull  /*  2^52 is_integer boundary  */
#define FR_2P52P1      0x4330000000000001ull  /*  2^52+1 already integral   */
#define FR_N2P52P1     0xc330000000000001ull
#define FR_2P52MH      0x432fffffffffffffull  /*  2^52-0.5: tie, CARRY out  */
#define FR_N2P52MH     0xc32fffffffffffffull
#define FR_JUST_HALF   0x3fdfffffffffffffull  /*  0.49999999999999994       */
#define FR_NJUST_HALF  0xbfdfffffffffffffull
#define FR_TWO_M       0x3fffffffffffffffull  /*  1.9999999999999998 carry  */
#define FR_2P53        0x4340000000000000ull
#define FR_1E300       0x7e37e43c8800759cull
#define FR_NSNAN       0xfff0000000000001ull  /* -sNaN -> -qNaN, payload 1  */
#define FR_NQNAN       0xfff8000000000001ull
#define FR_4P5         0x4012000000000000ull
#define FR_N0P75       0xbfe8000000000000ull

static const uint64_t FROUND_SINGLES[] = {
    FR_HALF, FR_NHALF, FR_1P5, FR_N1P5, FR_2P5, FR_N2P5, FR_3P5, FR_N3P5,
    FR_0P3, FR_N0P3, FR_0P7, FR_2P52, FR_2P52P1, FR_N2P52P1, FR_2P52MH,
    FR_N2P52MH, FR_JUST_HALF, FR_NJUST_HALF, FR_TWO_M, FR_2P53, FR_1E300,
    FR_NSNAN, FR_NQNAN, FR_4P5, FR_N0P75
};

enum { N_FROUND_SINGLES = (int)(sizeof FROUND_SINGLES / sizeof FROUND_SINGLES[0]) };

_Static_assert(N_FROUND_SINGLES == 25,
               "K25.md §5's fround-specific classical list is 25 singles");

/* The complete table: every row goes through the CLASSICAL instruments; the
 * circuit replays only the eight below. */
static int fround_table(int i, uint64_t *out)
{
    int base = cq_fp_anchors_unary_count();
    cq_ref_w v[CQ_KD_MAX_SRC];

    if (i < 0) return base + N_FROUND_SINGLES;
    if (i < base) {
        if (cq_fp_anchors_unary(W64, i, v) != 1) return 0;
        *out = v[0].lo;
        return 1;
    }
    if (i >= base + N_FROUND_SINGLES) return 0;
    *out = FROUND_SINGLES[i - base];
    return 1;
}

/* ---- §7.12's EIGHT circuit anchors, per kernel. ------------------------- *
 *
 * EACH LIST IS CHOSEN TO SEPARATE ITS KERNEL FROM ITS NEIGHBOUR, which is
 * §7.7's instruction and the only thing that sees a wrong row of the name
 * mapping: trunc/floor/ceil disagree on -2.5, -0.3, -1.5, 2.5 and 1.5;
 * even/away disagree on every tie (0.5, -0.5, 2.5, -2.5); and every list
 * carries a NaN whose quietening is load-bearing and one row per arm of its
 * own select chain (special, small, integer, normal). */
static const uint64_t FR_CIRCUIT[CQ_FROUND_N_PROG][8] = {
    /* trunc  */ { CQ_F64_SNAN, CQ_F64_NEG_INF, FR_NHALF, FR_2P5, FR_N2P5,
                   FR_N0P3, FR_2P52P1, CQ_F64_MAX_SUBNORMAL },
    /* floor  */ { FR_NSNAN, CQ_F64_NEG_INF, FR_NHALF, FR_2P5, FR_N2P5,
                   FR_N0P3, FR_N1P5, CQ_F64_NEG(CQ_F64_MIN_SUBNORMAL) },
    /* ceil   */ { CQ_F64_SNAN, CQ_F64_POS_INF, FR_HALF, FR_2P5, FR_N2P5,
                   FR_0P3, FR_1P5, CQ_F64_MAX_SUBNORMAL },
    /* even   */ { FR_NSNAN, FR_HALF, FR_NHALF, FR_2P5, FR_N2P5, FR_3P5,
                   FR_JUST_HALF, FR_TWO_M },
    /* away   */ { CQ_F64_SNAN, FR_HALF, FR_NHALF, FR_2P5, FR_N2P5, FR_1P5,
                   FR_JUST_HALF, FR_2P52MH }
};

/* ---- The five specs. ---------------------------------------------------- */

/* UNARY, ONE WIDTH (PRD-v2 §7.11): `n_src` 1 and a `call` adapter, M40's shape
 * exactly — `cq_kd_shape_of` refuses a shape the default path cannot serve. */
#define FR_SPEC(P, NAME, KFN)                                                  \
    static int anchors_##NAME(int W, int i, cq_ref_w *v)                       \
    {                                                                          \
        if (W != W64) return 0;                                                \
        if (i < 0) return 8;                                                   \
        if (i >= 8 || v == NULL) return 0;                                     \
        v[0] = cq_ref_w_make(FR_CIRCUIT[P][i], 0u, W64);                       \
        return 1;                                                              \
    }                                                                          \
    static void shape_##NAME(int W, cq_kd_shape *out)                          \
    {                                                                          \
        cq_kd_default_shape(W, out);                                           \
        out->n_src = 1; out->w[0] = W64; out->w_dst = W64;                     \
        out->anchors = anchors_##NAME;                                         \
    }                                                                          \
    static void call_##NAME(cq_ctx *ctx, cq_bit *dst,                          \
                            const cq_bit *const *src, const cq_kd_shape *sh)   \
    {                                                                          \
        (void)sh;                                                              \
        KFN(ctx, dst, src[0], W64);                                            \
    }                                                                          \
    static cq_ref_w refn_##NAME(const cq_ref_w *s, const cq_kd_shape *sh)      \
    {                                                                          \
        (void)sh;                                                              \
        return cq_ref_w_make(ref_fround(P, s[0].lo), 0u, W64);                 \
    }                                                                          \
    static const cq_kd_spec SPEC_##NAME = { #NAME, NULL, NULL, shape_##NAME,   \
                                            call_##NAME, refn_##NAME };

FR_SPEC(CQ_FROUND_TRUNC, ftrunc,      cq_kernel_ftrunc)
FR_SPEC(CQ_FROUND_FLOOR, ffloor,      cq_kernel_ffloor)
FR_SPEC(CQ_FROUND_CEIL,  fceil,       cq_kernel_fceil)
FR_SPEC(CQ_FROUND_EVEN,  fround_even, cq_kernel_fround_even)
FR_SPEC(CQ_FROUND_AWAY,  fround_away, cq_kernel_fround_away)

static const cq_kd_spec *const FR_SPECS[CQ_FROUND_N_PROG] = {
    &SPEC_ftrunc, &SPEC_ffloor, &SPEC_fceil, &SPEC_fround_even,
    &SPEC_fround_away
};

/* Every circuit anchor of kernel `p` is a row of the complete table, so the
 * classical instruments cover every value the circuit replays. */
static void fr_circuit_anchors_are_table_rows(cq_fround_prog p)
{
    int n = fround_table(-1, NULL);

    for (int i = 0; i < 8; i++) {
        int found = 0;

        for (int j = 0; j < n && !found; j++) {
            uint64_t a = 0u;

            CHECK_EQ(fround_table(j, &a), 1);
            found = (a == FR_CIRCUIT[p][i]);
        }
        if (!found)
            cq_h_fail(__FILE__, __LINE__, "%s circuit anchor %d (%016llx) is "
                      "not a row of the complete table", FR_SPECS[p]->name, i,
                      (unsigned long long)FR_CIRCUIT[p][i]);
    }
}

/* The four shared hand-driven rows of the `controlled` case, per kernel: the
 * ALL-CLASSICAL one (the only fixture that sees a controlled region silently
 * made unconditional by the R9 short-circuit) and the ALL-QUANTUM one (the
 * mask L4 pins). */
static const cq_kd_spec *g_fr_ctrl_spec;
static uint64_t          g_fr_ctrl_value;

static void fr_two_rows(void)
{
    cq_bk_pair m;
    cq_ref_w v[CQ_KD_MAX_SRC];

    memset(v, 0, sizeof v);
    v[0] = cq_ref_w_make(g_fr_ctrl_value, 0u, W64);

    m.q[0] = cq_ref_w_zero();  m.q[1] = cq_ref_w_zero();
    m.name = "all-classical";
    cq_kd_case(g_fr_ctrl_spec, W64, v, &m);

    m.q[0] = cq_ref_w_ones(W64); m.q[1] = cq_ref_w_ones(W64);
    m.name = "all-quantum";
    cq_kd_case(g_fr_ctrl_spec, W64, v, &m);
}

/* L1 + L2 + L3 + L5 through the shared driver, then Step 20's four regions
 * and §9's promotion identity, then the peak — one kernel. */
static void fr_circuit_suite(cq_fround_prog p, uint64_t ctrl_value)
{
    uint32_t peak = 0u, owned;
    uint64_t reached;

    fr_circuit_anchors_are_table_rows(p);
    cq_kd_sweep_at(FR_SPECS[p], W64);

    g_fr_ctrl_spec = FR_SPECS[p];
    g_fr_ctrl_value = ctrl_value;
    cq_kd_for_each_region(FR_SPECS[p]->name, fr_two_rows);
    reached = cq_kd_check_promotion(FR_SPECS[p], W64);
    CHECK(reached > 0u);                       /* not vacuous */

    /* dst owns 64 qubits and the scratch comes back; FLAT means the peak is
     * the whole program's region plus dst (PRD §15 D9). */
    owned = cq_kd_peak(FR_SPECS[p], W64, &peak);
    CHECK_EQ(owned, (uint32_t)W64);
    CHECK_EQ(peak, cq_fround_region(p) + (uint32_t)W64);
}
