/* tests/support/bitkinds.c — plan §2.2. (value, quantum-mask) -> register. */

#include "bitkinds.h"

#include "emit.h"
#include "reg.h"
#include "refmodel.h"

#include <stdio.h>
#include <stdlib.h>

static void cq_bk_die(const char *what, uint64_t a, uint64_t b)
{
    fprintf(stderr, "libcqops: FATAL: bitkinds: %s (%llu, %llu)\n",
            what, (unsigned long long)a, (unsigned long long)b);
    abort();
}

int32_t cq_bk_reg_w(cq_ctx *ctx, uint32_t W, cq_ref_w value, cq_ref_w qmask)
{
    cq_ref_w m = cq_ref_w_ones((int)W);

    /* A mask bit above W would silently do nothing, so a caller who built its
     * mask at the wrong width would see a coverage gap rather than an error. */
    if (!cq_ref_w_is_zero(cq_ref_w_andnot(qmask, m)))
        cq_bk_die("quantum mask has bits above the register width", qmask.lo, W);

    value = cq_ref_w_make(value.lo, value.hi, (int)W);

    int32_t h = cq_reg_alloc_const(&ctx->regs, W, value.lo, value.hi);
    cq_bit *bits = cq_reg_bits(&ctx->regs, h);

    for (uint32_t i = 0; i < W; i++)
        if (cq_ref_w_bit(qmask, (int)i)) cq_materialise(ctx, &bits[i]);

    return h;
}

int32_t cq_bk_reg(cq_ctx *ctx, uint32_t W, uint64_t value, uint64_t qmask)
{
    /* A WRAPPER, NOT A SECOND IMPLEMENTATION — see bitkinds.h. */
    if (W > 64u) cq_bk_die("cq_bk_reg is the one-word form; use cq_bk_reg_w", W, 64u);
    return cq_bk_reg_w(ctx, W, cq_ref_w_make(value, 0u, (int)W),
                       cq_ref_w_make(qmask, 0u, (int)W));
}

/* --- The fixed mask set (bd bz5, plus risk R8's asymmetric witnesses). ---- */

uint32_t cq_bk_fixed_pairs(uint32_t W, cq_bk_pair *out, uint32_t cap)
{
    int      w   = (int)W;
    cq_ref_w all = cq_ref_w_ones(w);
    cq_ref_w alt = cq_ref_w_zero();
    cq_ref_w nalt;
    cq_ref_w lsb = cq_ref_w_setbit(0);
    cq_ref_w msb = cq_ref_w_setbit(w - 1);
    cq_ref_w none = cq_ref_w_zero();
    uint32_t n = 0;

    for (int i = 0; i < w; i += 2) alt = cq_ref_w_or(alt, cq_ref_w_setbit(i));
    nalt = cq_ref_w_andnot(all, alt);

    /* A macro rather than a static table because every entry depends on W.
     * The bound check is inside, so an added row cannot outrun `cap`. */
#define PAIR(qa_, qb_, name_)                                                 \
    do {                                                                      \
        if (n >= cap) cq_bk_die("mask-pair buffer too small", n, cap);         \
        out[n].q[0] = (qa_); out[n].q[1] = (qb_); out[n].name = (name_); n++;  \
    } while (0)

    /* L5 lives here: both operands fully classical must cost 0 gates and 0
     * qubits, and the driver asserts exactly that on this row. */
    PAIR(none, none, "all-classical");
    PAIR(all,  all,  "all-quantum");     /* index 1 — the driver relies on it */
    PAIR(alt,  alt,  "alternating");
    PAIR(nalt, nalt, "alternating-complement");
    PAIR(lsb,  lsb,  "lsb-only");
    PAIR(msb,  msb,  "msb-only");

    /* THE ASYMMETRIC ROWS. Risk R8's measured witness is "a all Q, b all
     * ZERO"; the value sweep supplies the ZERO half, this supplies the kind
     * half. A symmetric mask set cannot express any of these six. */
    PAIR(all,  none, "a-quantum/b-classical");
    PAIR(none, all,  "a-classical/b-quantum");
    PAIR(alt,  nalt, "disjoint-lanes");
    PAIR(nalt, alt,  "disjoint-lanes-swapped");
    PAIR(lsb,  msb,  "lsb/msb");
    PAIR(msb,  lsb,  "msb/lsb");

    /* The one-bit-quantum sweep across all W positions. */
    for (int i = 0; i < w; i++)
        PAIR(cq_ref_w_setbit(i), cq_ref_w_setbit(i), "one-bit-sweep");

#undef PAIR
    return n;
}

/* Clears from every mask the bits a kernel's shape requires to be classical —
 * K4's shift amount is the case: a quantum amount is M12's operand, not M11's,
 * and handing one to M11 is a hard error rather than a test. */
void cq_bk_constrain(cq_bk_pair *p, const cq_ref_w *classical, int n_src)
{
    for (int i = 0; i < n_src && i < 2; i++)
        p->q[i] = cq_ref_w_andnot(p->q[i], classical[i]);
}

/* --- Deterministic sampling. --------------------------------------------- */

void cq_bk_rng_init(cq_bk_rng *r, uint64_t seed)
{
    /* Zero is xorshift's fixed point: it would emit an infinite stream of
     * zeroes and every "random" mask would be all-classical, which is a green
     * run that sampled one case. */
    r->s = seed ? seed : 0x9E3779B97F4A7C15ull;
}

uint64_t cq_bk_rng_next(cq_bk_rng *r)
{
    uint64_t x = r->s;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    r->s = x;
    return x * 0x2545F4914F6CDD1Dull;
}

uint64_t cq_bk_rng_below(cq_bk_rng *r, uint64_t bound)
{
    if (bound == 0u) cq_bk_die("random bound of zero", bound, 0u);
    return cq_bk_rng_next(r) % bound;
}
