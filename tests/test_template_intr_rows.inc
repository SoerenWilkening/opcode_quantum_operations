/* tests/test_template_intr_rows.inc — the SYMBOL TABLES of
 * tests/test_template_intr.c, in an `.inc` under Rule 12's own escape hatch
 * for large static tables.
 *
 * EVERY SYMBOL IS DECLARED BY HAND, and a wrong prototype is a LINK or a
 * `conflicting types` error rather than a silent mismatch — the compiler-
 * checkable statement of the shapes is tests/abi/cq_intrinsic_templates_abi.txt,
 * which tests/test_gen_shim.py diffs. The X-macros below generate the
 * prototypes AND the width-erasing thunks for the `_hl` literals, whose C type
 * is the width's own (`int8_t` .. `int64_t`), so that one table row can carry
 * any width.
 */

/* --- the six UNARY opcodes (M41 + M42's abs) ------------------------------ */

#define IU_W4(X, op) X(op, 8) X(op, 16) X(op, 32) X(op, 64)
#define IU_ALL(X)                                                             \
    IU_W4(X, ctpop) IU_W4(X, ctlz) IU_W4(X, cttz) IU_W4(X, bitreverse)       \
    IU_W4(X, abs) X(bswap, 16) X(bswap, 32) X(bswap, 64)

#define IU_DECL(op, w)                                                        \
    int32_t cq_template_##op##_i##w(int32_t);                                 \
    void    cq_template_##op##_i##w##_unc(int32_t, int32_t);
IU_ALL(IU_DECL)

typedef struct {
    const char *name;
    int         op;         /* an IU_* code below — the REFERENCE's, not M26's */
    int         W;
    int32_t   (*fwd)(int32_t);
    void      (*unc)(int32_t, int32_t);
} iu_row;

enum { IU_CTPOP, IU_CTLZ, IU_CTTZ, IU_BITREVERSE, IU_BSWAP, IU_ABS };

#define IU_CODE_ctpop      IU_CTPOP
#define IU_CODE_ctlz       IU_CTLZ
#define IU_CODE_cttz       IU_CTTZ
#define IU_CODE_bitreverse IU_BITREVERSE
#define IU_CODE_bswap      IU_BSWAP
#define IU_CODE_abs        IU_ABS

#define IU_ROW(op, w)                                                         \
    { #op "_i" #w, IU_CODE_##op, w,                                           \
      cq_template_##op##_i##w, cq_template_##op##_i##w##_unc },
static const iu_row IU_ROWS[] = { IU_ALL(IU_ROW) };

#define IU_N (sizeof IU_ROWS / sizeof IU_ROWS[0])

/* --- the four min/max (M42) ----------------------------------------------- */

#define MM_W(X, op) X(op, 8, int8_t) X(op, 16, int16_t) X(op, 32, int32_t)   \
                    X(op, 64, int64_t)
#define MM_ALL(X) MM_W(X, smin) MM_W(X, smax) MM_W(X, umin) MM_W(X, umax)

/* The `(T)b` narrowing is EXPLICIT, which is what -Wconversion asks; it is the
 * same modular conversion CQ_lang's own call site performs on an `iN`
 * constant, and M26 masks the literal to `bits` in any case. */
#define MM_DECL(op, w, T)                                                     \
    int32_t cq_template_##op##_i##w(int32_t, int32_t);                        \
    int32_t cq_template_##op##_i##w##_hl(int32_t, T);                         \
    void    cq_template_##op##_i##w##_unc(int32_t, int32_t, int32_t);         \
    void    cq_template_##op##_i##w##_hl_unc(int32_t, int32_t, T);            \
    static int32_t mm_hl_##op##_i##w(int32_t a, uint64_t b)                   \
    { return cq_template_##op##_i##w##_hl(a, (T)b); }                         \
    static void mm_hlu_##op##_i##w(int32_t o, int32_t a, uint64_t b)          \
    { cq_template_##op##_i##w##_hl_unc(o, a, (T)b); }
MM_ALL(MM_DECL)

typedef struct {
    const char *name;
    int         op;         /* an MM_* code below */
    int         W;
    int32_t   (*fwd)(int32_t, int32_t);
    int32_t   (*fwd_hl)(int32_t, uint64_t);
    void      (*unc)(int32_t, int32_t, int32_t);
    void      (*unc_hl)(int32_t, int32_t, uint64_t);
} mm_row;

enum { MM_SMIN, MM_SMAX, MM_UMIN, MM_UMAX };

#define MM_CODE_smin MM_SMIN
#define MM_CODE_smax MM_SMAX
#define MM_CODE_umin MM_UMIN
#define MM_CODE_umax MM_UMAX

#define MM_ROW(op, w, T)                                                      \
    { #op "_i" #w, MM_CODE_##op, w,                                           \
      cq_template_##op##_i##w, mm_hl_##op##_i##w,                             \
      cq_template_##op##_i##w##_unc, mm_hlu_##op##_i##w },
static const mm_row MM_ROWS[] = { MM_ALL(MM_ROW) };

#define MM_N (sizeof MM_ROWS / sizeof MM_ROWS[0])

/* --- the rows that ABORT, each with the sentence that must be TRUE of it -- */

/* The seven class-1 `_inv` symbols (bswap i16..i64, bitreverse i8..i64) take
 * D14's reason; the funnel shifts take bead 9ve.29.3's; the saturating four
 * take K27's. One symbol per SHAPE the yaml gives each opcode, so a reason
 * that is right for `_hl` and wrong for `_qql` has somewhere to go red. The
 * strings are TRANSCRIBED here, a third copy after gen_shim.py and
 * tests/support/shimcheck.py, so no two of the three can agree by sharing. */
#define IN_D14 "_inv is f-inverse, not f; the data family has no uniform definition (PRD 15 D14)"
#define IN_FSH "fshl/fshr are REFUSED, not unported: Bennett's expansion "      \
    "(instructions.jl:4947-4996) is not LLVM's funnel shift at a shift "       \
    "congruent to 0 mod W, so porting it is a known miscompile and "           \
    "correcting it is a re-derivation (bead 9ve.29.3, K26.md section 6)"
#define IN_SAT "the saturating add/sub intrinsics have NO construction in the " \
    "pinned Bennett snapshot, so Rule 1 forbids writing one (K27.md, Not "     \
    "ported)"

int32_t cq_template_bswap_i16_inv(int32_t);
int32_t cq_template_bswap_i64_inv(int32_t);
int32_t cq_template_bitreverse_i8_inv(int32_t);
int32_t cq_template_bitreverse_i32_inv(int32_t);
int32_t cq_template_fshl_i8_hl(int32_t, int8_t);
int32_t cq_template_fshl_i16_hl_inv(int32_t, int16_t);
void    cq_template_fshr_i32_hl_unc(int32_t, int32_t, int32_t);
int32_t cq_template_fshr_i64_qql(int32_t, int32_t, int64_t);
void    cq_template_fshl_i32_qql_unc(int32_t, int32_t, int32_t, int32_t);
int32_t cq_template_uadd_sat_i8(int32_t, int32_t);
int32_t cq_template_sadd_sat_i16_hl(int32_t, int16_t);
int32_t cq_template_usub_sat_i32_lh(int32_t, int32_t);
void    cq_template_ssub_sat_i64_lh_unc(int32_t, int64_t, int32_t);

#define IN_ABORTS(X)                                                          \
    X(bswap_i16_inv,        (0),          IN_D14)                             \
    X(bswap_i64_inv,        (0),          IN_D14)                             \
    X(bitreverse_i8_inv,    (0),          IN_D14)                             \
    X(bitreverse_i32_inv,   (0),          IN_D14)                             \
    X(fshl_i8_hl,           (0, 3),       IN_FSH)                             \
    X(fshl_i16_hl_inv,      (0, 3),       IN_FSH)                             \
    X(fshr_i32_hl_unc,      (0, 0, 3),    IN_FSH)                             \
    X(fshr_i64_qql,         (0, 0, 3),    IN_FSH)                             \
    X(fshl_i32_qql_unc,     (0, 0, 0, 3), IN_FSH)                             \
    X(uadd_sat_i8,          (0, 0),       IN_SAT)                             \
    X(sadd_sat_i16_hl,      (0, 3),       IN_SAT)                             \
    X(usub_sat_i32_lh,      (3, 0),       IN_SAT)                             \
    X(ssub_sat_i64_lh_unc,  (0, 3, 0),    IN_SAT)

#define IN_THUNK(sym, args, why)                                              \
    static void in_call_##sym(void *p) { (void)p; (void)cq_template_##sym args; }
IN_ABORTS(IN_THUNK)

#define IN_ENTRY(sym, args, why) { #sym, in_call_##sym, why },
static const struct {
    const char *sym;
    void (*fn)(void *);
    const char *why;
} IN_ABORT_ROWS[] = { IN_ABORTS(IN_ENTRY) };

#define IN_N_ABORTS (sizeof IN_ABORT_ROWS / sizeof IN_ABORT_ROWS[0])
