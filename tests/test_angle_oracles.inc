/* tests/test_angle_oracles.inc — M21's two oracles and the comparison macros.
 *
 * Split out of tests/test_angle.c at Step 18 when that file crossed Rule 12's
 * 300-line guard. The seam is INSTRUMENT against ASSERTION: everything here is
 * a way of finding out what the right answer is, and the .c holds every claim
 * about what the answer must be. That is the same split test_kernel_cmp took
 * (primitives against derivation) and the same one test_kernel_divrem took
 * (the measured blocks against what each half asserts about them).
 *
 * NEITHER ORACLE IS THE MODULE, and the two are not redundant.
 *
 *   `ref_row` reduces theta into [0, 4pi) with fmod and compares against the
 *   four residues PRD section 7 tabulates, where angle.c finds the NEAREST
 *   multiple of pi and reads its index mod 4. It is an independent check of the
 *   PARITY -- the thing a hand-written expectation table cannot check about
 *   itself -- and of NOTHING ELSE, because it shares this file's pi and this
 *   file's window. Recording that limit is the point of this paragraph: an
 *   earlier draft of the module used a window proportional to |theta|, and
 *   ref_row agreed with the bug at every angle, because it shared the bug.
 *
 *   `distance_to_true_multiple_of_pi` is the one that would have caught it. It
 *   carries pi to double-double precision and uses fma() to recover the exact
 *   residual of the product, so it measures the distance to a multiple of TRUE
 *   pi rather than of the double angle.c rounds it to. Those differ by
 *   |theta|*3.9e-17, which is invisible to any oracle built out of plain
 *   doubles and is exactly the term PRD section 7's wording hid.
 *
 * NEITHER MAKES THE OTHER REDUNDANT, and that was measured rather than assumed.
 * Give angle.c a second accepted residue (say at CQ_ANGLE_PI/16) and
 * `the_classification_agrees_with_an_independent_reduction` is the SOLE red
 * case — 514 failed checks, every one of them from the `lattice +/- pi/16`
 * probes and none from the dense or random scans. check_sound_at is in scope for
 * that mutant and does NOT catch it, because the bound it asserts is on the
 * distance to the multiple the row NAMES, and the mutant names a consistent
 * (wrong) one. The two oracles answer different questions: ref_row asks "is this
 * the row section 7 gives that angle", check_sound_at asks "is the angle really
 * that close to the multiple named".
 */

/* π to double-double: PI_HI is the nearest double to π — the same double
 * angle.c uses, and the suite may rely on that — and PI_LO carries the next 53
 * bits of π − PI_HI, giving π to a relative 9.5e-34 (~2^-110).
 *
 * HOW MUCH FINER THAN THE WINDOW, stated as the quantity that is actually
 * bounded: the oracle's own error is ~2^-107·|θ|, and the module admits |θ| only
 * up to tol·π/1.6e-16, so their ratio at the refusal boundary is
 * 1.6e-16 / 2^-107 ≈ 2.6e16 — **sixteen orders of magnitude, and the same
 * sixteen at EVERY legal tolerance**, because both scale with tol. (Do not
 * phrase this against "the tightest window this module admits": tol = 0 is legal
 * and its window is exactly 0, so that ratio does not exist.) */
#define PI_HI 3.14159265358979323846264338327950288
#define PI_LO 1.2246467991473531772e-16
#define REF_PI PI_HI

/* Bitwise, never ==. mock_sink compares angles this way throughout the project
 * for the same reason: 0.0 and −0.0 are equal in C and are different doubles.
 * Note also that CHECK_EQ casts both sides to long long, so it is VACUOUSLY
 * TRUE for every tolerance under 1.0 — it must never be used here. */
static int same_double(double a, double b)
{
    return memcmp(&a, &b, sizeof a) == 0;
}

static const char *cname(cq_angle_class c)
{
    switch (c) {
    case CQ_ANGLE_GENERAL:      return "GENERAL";
    case CQ_ANGLE_IDENTITY:     return "IDENTITY";
    case CQ_ANGLE_NEG_IDENTITY: return "NEG_IDENTITY";
    case CQ_ANGLE_HALF_TURN:    return "HALF_TURN";
    }
    return "(invalid)";
}

/* Angles print with %a as well as %.17g: %a is the only format that round-trips
 * every finite double (M23's reason), and the decimal form is the actionable
 * one. */
#define CHECK_ROW(fn, th, want)                                                \
    do {                                                                       \
        double cq_t_ = (th);                                                   \
        cq_angle_class cq_g_ = fn(cq_t_);                                      \
        cq_angle_class cq_w_ = (want);                                         \
        if (cq_g_ != cq_w_)                                                    \
            cq_h_fail(__FILE__, __LINE__, "%s(%a = %.17g): got %s, want %s",   \
                      #fn, cq_t_, cq_t_, cname(cq_g_), cname(cq_w_));          \
    } while (0)

#define CHECK_RY(th, want) CHECK_ROW(cq_angle_ry_row, th, want)
#define CHECK_RZ(ph, want) CHECK_ROW(cq_angle_rz_row, ph, want)

#define CHECK_LATTICE(th, tol, want)                                           \
    do {                                                                       \
        double cq_t_ = (th), cq_tol_ = (tol);                                  \
        cq_angle_class cq_g_ = cq_angle_lattice(cq_t_, cq_tol_);               \
        cq_angle_class cq_w_ = (want);                                         \
        if (cq_g_ != cq_w_)                                                    \
            cq_h_fail(__FILE__, __LINE__,                                      \
                      "lattice(%a = %.17g, tol %g): got %s, want %s",          \
                      cq_t_, cq_t_, cq_tol_, cname(cq_g_), cname(cq_w_));      \
    } while (0)

/* The row §7 gives to θ = k·π, written from the table's own wording rather than
 * from angle.c: "θ ≡ 0 (mod 4π)" is k ≡ 0 (mod 4), "θ ≡ 2π (mod 4π)" is
 * k ≡ 2 (mod 4), "θ ≡ π (mod 2π)" is every odd k. */
static cq_angle_class row_of_index(long k)
{
    long m = ((k % 4) + 4) % 4;
    if (m == 0) return CQ_ANGLE_IDENTITY;
    if (m == 2) return CQ_ANGLE_NEG_IDENTITY;
    return CQ_ANGLE_HALF_TURN;
}

/* ORACLE 1 — parity, by a different reduction. */
static cq_angle_class ref_row(double theta, double tol)
{
    const double turn = 4.0 * REF_PI;
    const double w    = tol * REF_PI;

    double r = fmod(theta, turn);
    if (r < 0.0) r += turn;

    if (fabs(r) <= w || fabs(r - turn) <= w)                  return CQ_ANGLE_IDENTITY;
    if (fabs(r - 2.0 * REF_PI) <= w)                          return CQ_ANGLE_NEG_IDENTITY;
    if (fabs(r - REF_PI) <= w || fabs(r - 3.0 * REF_PI) <= w) return CQ_ANGLE_HALF_TURN;
    return CQ_ANGLE_GENERAL;
}

static void check_against_reference(double theta, const char *what)
{
    cq_angle_class got  = cq_angle_ry_row(theta);
    cq_angle_class want = ref_row(theta, cq_angle_tolerance());
    if (got != want)
        cq_h_fail(__FILE__, __LINE__,
                  "%s: ry_row(%a = %.17g) = %s, independent reduction says %s",
                  what, theta, theta, cname(got), cname(want));
}

/* ORACLE 2 — the distance from θ to the nearest multiple of TRUE π.
 *
 * `theta - p` is exact (the two are within a factor of two, so Sterbenz
 * applies); `fma(k, PI_HI, -p)` recovers the part of the product that rounding
 * threw away, which is the term a plain double computation loses and precisely
 * the one that hides the drift between PI_HI and π. */
static double distance_to_true_multiple_of_pi(double theta, double *k_out)
{
    double k  = round(theta / PI_HI);
    double p  = k * PI_HI;
    double pe = fma(k, PI_HI, -p);
    double d  = ((theta - p) - pe) - k * PI_LO;
    if (k_out) *k_out = k;
    return fabs(d);
}

/* D10's contract, asserted rather than argued: a fold names a multiple of true
 * π that θ really is within 2·tol·π of, and names the right one.
 *
 * IT TAKES A TOLERANCE, AND THAT IS NOT A CONVENIENCE. The bound has two terms
 * — the residual the module measures, and the drift it cannot — and at the
 * MODULE tolerance only the first is reachable: with tol = 1e-12 the refusal
 * caps |θ| at 1.96e4, where the drift is 3e-12 against a bound of 6.3e-12, so a
 * broken CQ_ANGLE_PI_ERROR stays invisible. The drift dominates only near the
 * refusal boundary |θ| ≈ tol·π/1.6e-16, which is large enough to matter only at
 * a loose tolerance. Measured: mutating CQ_ANGLE_PI_ERROR to 5e-17 — which
 * really does violate the contract, by 1.22× at
 * `lattice(0x1.550f7dca5209cp+39, 0x1.8723a1d588a37p-17)` — left this case
 * GREEN until it swept `tol`, and the only thing that went red was a magnitude
 * pin that reads the same for a SAFE tightening as for an unsafe loosening. */
static void check_sound_at(double theta, double tol)
{
    cq_angle_class got = cq_angle_lattice(theta, tol);
    if (got == CQ_ANGLE_GENERAL) return;          /* always sound */

    double k, bound = 2.0 * tol * REF_PI;
    double d = distance_to_true_multiple_of_pi(theta, &k);

    if (!(d <= bound))
        cq_h_fail(__FILE__, __LINE__,
                  "lattice(%a = %.17g, tol %g) = %s, but θ is %.6g rad from the "
                  "nearest multiple of TRUE π (k = %.0f); the bound is %.6g",
                  theta, theta, tol, cname(got), d, k, bound);
    else if (got != row_of_index((long)k))
        cq_h_fail(__FILE__, __LINE__,
                  "lattice(%a = %.17g, tol %g) = %s, but k = %.0f names %s",
                  theta, theta, tol, cname(got), k, cname(row_of_index((long)k)));
}

/* At the module tolerance, which is what the ry_row path uses. */
static void check_sound(double theta)
{
    check_sound_at(theta, cq_angle_tolerance());
}

