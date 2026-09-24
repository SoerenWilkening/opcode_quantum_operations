/* tests/test_template_fmisc_rows.inc — the SYMBOL TABLES of
 * tests/test_template_fmisc.c. Every prototype is declared by hand; the
 * compiler-checkable statement of the shapes is the two ABI manifests under
 * tests/abi/, which tests/test_gen_shim.py diffs. */

/* --- the eight UNARY rows through `cq_shim_fun` --------------------------- */

#define FU_ALL(X)                                                             \
    X(fneg, cq_ref_fneg, 1) X(fabs, cq_ref_fabs, 1)                           \
    X(floor, h_floor, 0) X(ceil, h_ceil, 0) X(trunc, h_trunc, 0)              \
    X(round, h_round, 0) X(rint, h_rint, 0) X(nearbyint, h_nbint, 0)

#define FU_DECL(op, ref, sign)                                                \
    int32_t cq_template_##op##_f64(int32_t);                                  \
    void    cq_template_##op##_f64_unc(int32_t, int32_t);
FU_ALL(FU_DECL)

typedef struct {
    const char *name;
    uint64_t  (*ref)(uint64_t);
    int32_t   (*fwd)(int32_t);
    void      (*unc)(int32_t, int32_t);
    int         sign_op;    /* 1: a quiet bit operation, NaN rows included */
} fu_row;

#define FU_ROW(op, ref, sign)                                                 \
    { #op, ref, cq_template_##op##_f64, cq_template_##op##_f64_unc, sign },
static const fu_row FU_ROWS[] = { FU_ALL(FU_ROW) };

#define FU_N (sizeof FU_ROWS / sizeof FU_ROWS[0])

/* The operands. Every tie a ties-even and a ties-away rounding disagree about
 * (±0.5, ±1.5, ±2.5) and the one where they agree (the last below 2^52 with a
 * fraction, which is itself .5); a value whose ceil is NEGATIVE ZERO (-0.3)
 * and whose floor is -1; both zeros, both infinities, a subnormal of each
 * sign, the first integer-only binade (2^52) and a large magnitude. */
static const uint64_t FU_VALS[] = {
    0x3fe0000000000000ull, 0xbfe0000000000000ull,   /*  0.5, -0.5          */
    0x3ff8000000000000ull, 0xbff8000000000000ull,   /*  1.5, -1.5          */
    0x4004000000000000ull, 0xc004000000000000ull,   /*  2.5, -2.5          */
    0x400d99999999999aull, 0xc00d99999999999aull,   /*  3.7, -3.7          */
    0xbfd3333333333333ull, 0x3fd3333333333333ull,   /* -0.3,  0.3          */
    0x432fffffffffffffull,                          /*  2^52 - 0.5         */
    0x4330000000000000ull,                          /*  2^52               */
    0x7e37e43c8800759cull,                          /*  1e300              */
    CQ_F64_POS_ZERO, CQ_F64_NEG_ZERO, CQ_F64_POS_INF, CQ_F64_NEG_INF,
    CQ_F64_MIN_SUBNORMAL, CQ_F64_NEG(CQ_F64_MIN_SUBNORMAL)
};

#define FU_NV (sizeof FU_VALS / sizeof FU_VALS[0])

/* The NaN rows only a SIGN op is judged on: a quiet NaN with a payload, a
 * signalling one, and a negative one — negate and abs flip or clear bit 63 and
 * touch nothing else, signalling-ness included (IEEE 754 §5.5.1). */
static const uint64_t FU_NANS[] = {
    CQ_F64_QNAN_A, CQ_F64_SNAN, 0xfff8000000000123ull
};

#define FU_NNAN (sizeof FU_NANS / sizeof FU_NANS[0])

/* --- the three BINARY rows through `cq_shim_fbin_*` ----------------------- */

int32_t cq_template_fmin_f64(int32_t, int32_t);
int32_t cq_template_fmin_f64_hl(int32_t, double);
void    cq_template_fmin_f64_unc(int32_t, int32_t, int32_t);
void    cq_template_fmin_f64_hl_unc(int32_t, int32_t, double);
int32_t cq_template_fmax_f64(int32_t, int32_t);
int32_t cq_template_fmax_f64_hl(int32_t, double);
void    cq_template_fmax_f64_unc(int32_t, int32_t, int32_t);
void    cq_template_fmax_f64_hl_unc(int32_t, int32_t, double);
int32_t cq_template_copysign_f64(int32_t, int32_t);
int32_t cq_template_copysign_f64_hl(int32_t, double);
int32_t cq_template_copysign_f64_lh(double, int32_t);
void    cq_template_copysign_f64_unc(int32_t, int32_t, int32_t);
void    cq_template_copysign_f64_hl_unc(int32_t, int32_t, double);
void    cq_template_copysign_f64_lh_unc(int32_t, double, int32_t);

/* `fwd_lh`/`unc_lh` are NULL on the two COMMUTATIVE opcodes, which is the
 * ABI's own shape (intrinsic_table.yaml: "Commutative ops carry qq+hl only"),
 * not an omission. */
typedef struct {
    const char *name;
    uint64_t  (*ref)(uint64_t, uint64_t);
    int32_t   (*fwd)(int32_t, int32_t);
    int32_t   (*fwd_hl)(int32_t, double);
    int32_t   (*fwd_lh)(double, int32_t);
    void      (*unc)(int32_t, int32_t, int32_t);
    void      (*unc_hl)(int32_t, int32_t, double);
    void      (*unc_lh)(int32_t, double, int32_t);
} fb_row;

static const fb_row FB_ROWS[] = {
    { "fmin", cq_ref_fmin, cq_template_fmin_f64, cq_template_fmin_f64_hl, NULL,
      cq_template_fmin_f64_unc, cq_template_fmin_f64_hl_unc, NULL },
    { "fmax", cq_ref_fmax, cq_template_fmax_f64, cq_template_fmax_f64_hl, NULL,
      cq_template_fmax_f64_unc, cq_template_fmax_f64_hl_unc, NULL },
    { "copysign", cq_ref_copysign, cq_template_copysign_f64,
      cq_template_copysign_f64_hl, cq_template_copysign_f64_lh,
      cq_template_copysign_f64_unc, cq_template_copysign_f64_hl_unc,
      cq_template_copysign_f64_lh_unc }
};

#define FB_N (sizeof FB_ROWS / sizeof FB_ROWS[0])

/* The PAIRS: both orders of two normals (the order detector for copysign and
 * the min/max detector), ±0 both ways (upstream's tie rule: min -> -0,
 * max -> +0), a NaN on each side and on both (minNum ABSORBS a single NaN and
 * returns the CANONICAL NaN for two), an infinity, and a sign that differs
 * from the magnitude's for copysign. Quiet NaNs only on a literal lane: a
 * `double` parameter is not a promise to carry a signalling payload. */
static const uint64_t FB_PAIRS[][2] = {
    { CQ_F64_ONE, 0xc000000000000000ull },          /*  1.0, -2.0          */
    { 0xc000000000000000ull, CQ_F64_ONE },          /* -2.0,  1.0          */
    { CQ_F64_POS_ZERO, CQ_F64_NEG_ZERO },
    { CQ_F64_NEG_ZERO, CQ_F64_POS_ZERO },
    { CQ_F64_QNAN_A, 0x4008000000000000ull },       /* NaN, 3.0            */
    { 0x4008000000000000ull, CQ_F64_QNAN_B },       /* 3.0, NaN            */
    { CQ_F64_QNAN_A, CQ_F64_QNAN_B },
    { CQ_F64_NEG_INF, 0x4008000000000000ull }
};

#define FB_NP (sizeof FB_PAIRS / sizeof FB_PAIRS[0])
