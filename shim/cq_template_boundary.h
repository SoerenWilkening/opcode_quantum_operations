/* shim/cq_template_boundary.h — the interface between M26's HANDLE BOUNDARY
 * and the per-family `cq_shim_*` entry points, and the second seam taken in
 * `shim/cq_template_impl.c`.
 *
 * THE FIRST SEAM WAS `the opcode DISPATCH TABLE <-> the handle BOUNDARY`
 * (shim/cq_template_dispatch.h, Step 23). THIS ONE IS
 *
 *     the INTEGER opcode surface <-> the fp opcode surface
 *                                          ->  shim/cq_template_fp.c
 *
 * and it was taken on the measurement Rule 12 asks for rather than on taste:
 * `cq_template_impl.c` stood at 288 counted lines of 300, and `fcmp`'s four
 * entry points plus their request builder are ~35 more. A seam reached for at
 * the wall is the surprise refactor Rule 12 forbids, so the cut is recorded in
 * IMPLEMENTATION_PLAN §3's Layer-5 row at the same time as it is taken.
 *
 * WHY THIS CUT AND NOT `the ENTRY POINTS <-> the ORDERED CALL SEQUENCE`. The
 * discriminator this file's sibling uses is WHAT MAKES EACH HALF CHANGE, and by
 * that test the fp surface is a different subject: it grows one family at a
 * time as PRD-v2 §7.15's order of work lands kernels, while the integer surface
 * is frozen with the ABI. A pure size cut down the middle of the entry points
 * would put `cq_shim_icmp_qq` and `cq_shim_fcmp_qq` in one file and the thing
 * they both call in another, which is worse on both counts.
 *
 * WHAT CROSSES THE SEAM IS DELIBERATELY SMALL: the request record, the neutral
 * builder that fills the parts the ABI fixes, and the ordered call sequence.
 * EVERYTHING THAT CAN BE GOT WRONG SILENTLY STAYS ON THE OTHER SIDE — D7a's
 * refusal, D7b's defensive copy and its un-copy, the mint at the RESULT width,
 * the §9 region, D15's twin record and D21's bracket are all inside
 * `cq_tpl_binary` and are reached identically by every family. An fp family
 * that wanted its own copy of any of them would be re-acquiring, in a new file,
 * every trap `cq_template_impl.c`'s header records.
 *
 * SO A FAMILY'S OWN FILE OWES EXACTLY THREE THINGS: its kernel, its display
 * name, and a TAG SPACE DISJOINT FROM EVERY OTHER FAMILY'S. The tag is D15's
 * twin identity — it is what makes a forward and its `_unc` the same operation
 * rather than two calls naming the same rail — so two families sharing a tag
 * would let an `fcmp_oeq_f64_unc` pair with an `icmp_eq_i64` forward and
 * discharge a free that nothing uncomputed. The four tag spaces in use are
 * recorded beside `cq_tpl_req`.
 */
/* A THIRD SEAM WAS TAKEN 2026-09-19 (bead 9ve.36) AND IS RECORDED IN
 * IMPLEMENTATION_PLAN §3's Layer-5 row:
 *
 *     the ARITY-2 ordered call sequence <-> the ARITY-1 ordered call sequence
 *                                               ->  shim/cq_template_unary.c
 *
 * Forced by measurement — `cq_template_impl.c` stood at 288 counted lines of
 * 300 and the arity-1 sequence had to grow a WORKSPACE composition for the
 * cross-domain casts — and right by the discriminator this file's siblings use,
 * WHAT MAKES EACH HALF CHANGE. The arity-2 half changes when a decision about
 * ALIASING or CONTROL changes: it owns D7b's defensive copy and its un-copy,
 * the §9 region and the literal lane, none of which an arity-1 operation has.
 * The arity-1 half changes when a family with a COMPOSITION lands: `fptosi f64
 * -> i8` is M37's kernel at T = 64 into a workspace and then `cq_kernel_trunc`,
 * because upstream emits the narrowing as a second IR instruction.
 *
 * WHAT DOES *NOT* CROSS IS A SECOND COPY OF ANYTHING. D7a's refusal, the width
 * doors, the mint, D15's record and D21's bracket are ONE implementation each,
 * reached by both sequences — which is why the three doors below are exported
 * rather than re-spelled next to the unary sequence.
 */
#ifndef CQ_TEMPLATE_BOUNDARY_H
#define CQ_TEMPLATE_BOUNDARY_H

#include "cq_template_dispatch.h"   /* cq_tpl_cast_fn, for the arity-1 stages */

#include "bit.h"
#include "ctx.h"
#include "kernels/kernel.h"

#include <stdint.h>

/* One binary-or-compare call. `a_h`/`b_h` are CQ_REG_NONE on the lane that
 * carries a literal; `out` is CQ_REG_NONE on a forward symbol, which mints one;
 * `ctrl` is CQ_REG_NONE on an uncontrolled one. */
typedef struct {
    cq_kernel_fn k;
    uint32_t w, wout;
    int32_t  a_h, b_h;
    uint64_t lo, hi;
    int32_t  out, ctrl;
    /* THE TEMPLATE'S OPCODE IDENTITY, for D15's twin match. It is what makes a
     * forward and its `_unc` the SAME operation rather than merely two calls
     * naming the same rail: `cq_shim_bin_qq(ADD,32,a,b)` and
     * `cq_shim_bin_qq_unc(SUB,32,out,a,b)` name identical handles and are not a
     * pair. It is NOT `r.k`: the compare families all share one kernel slot
     * shape and two predicates can collide there, and a function pointer is not
     * a stable identity across a rebuild anyway. */
    uint32_t tag;
    /* THE DISPLAY NAME, for D21's `op begin` payload. It is the DISPATCH half's
     * (cq_template_dispatch.h) for the seam's own reason — a name set that grows
     * with the yaml, not with a decision — and it rides here rather than being
     * re-derived because `cq_tpl_binary` has the opcode only as a kernel pointer
     * by then, and two predicates can share a kernel slot shape. */
    const char *name;
} tpl_req;

/* THE NEUTRAL BASE: everything the ABI fixes for EVERY arity-2 family, and
 * nothing a family owns. `k`, `name`, `tag` and — where the result width is not
 * the operand width — `wout` are the caller's to fill, and leaving any of them
 * as this function sets them is a defect its own family's suite must catch:
 * `k` is NULL, `name` is NULL and `tag` is 0.
 *
 * THE EIGHT TAG SPACES IN USE, and a ninth family must claim a ninth:
 *
 *     0x00000000 | op   * 1024 + w + 1      binary          (cq_template_impl.c)
 *     0x10000000 | fop  * 1024 + w + 1      fp_arith binary (cq_template_fparith.c)
 *     0x20000000 | op   * 1024 + w + 1      fp unary        (cq_template_fparith.c)
 *     0x30000000 | kind * 65536 + f*256 + t fp conversion   (cq_template_fparith.c)
 *     0x40000000 | pred * 1024 + w + 1      icmp            (cq_template_impl.c)
 *     0x50000000 | op   * 1024 + w + 1      fp TERNARY      (cq_template_ternary.c)
 *     0x80000000 | kind * 65536 + f*256 + t cast            (cq_template_unary.c)
 *     0xC0000000 | pred * 1024 + w + 1      fcmp            (cq_template_fp.c)
 *
 * THE TOP TWO BITS WERE EXHAUSTED AT FOUR, so the fp families subdivide rather
 * than claiming a fifth two-bit prefix. That is safe by measurement rather than
 * by intent: the binary space reaches at most `12 * 1024 + 128 + 1 = 12417` and
 * the cast space at most `2 * 65536 + 128 * 256 + 128 = 163968`, both far below
 * `0x10000000`, so the gaps between the old prefixes are wide and empty.
 *
 * RE-CHECKED FOR THE EIGHTH (bead 9ve.24, 2026-09-19), as the sentence this
 * paragraph used to end with required. `0x50000000` is the first free
 * `0x10000000`-granular prefix above `icmp`'s, and the ternary space reaches at
 * most `0 * 1024 + 64 + 1 = 65` because `cq_shim_fma_op` has ONE enumerator and
 * the width is f64 only — five orders of magnitude of headroom below the next
 * prefix. `0x60000000`, `0x70000000`, `0x90000000`, `0xA0000000`, `0xB0000000`,
 * `0xD0000000`, `0xE0000000` and `0xF0000000` remain free. A ninth family must
 * re-check this arithmetic rather than assume it.
 *
 * The `+ 1` and the width term are not decoration: the width is folded in
 * because the ABI's `_unc` carries it too, and a forward at i32 is not the
 * adjoint of an `_unc` at i64 on the same handles. */
tpl_req cq_tpl_req(int bits, int32_t a_h, int32_t b_h);

/* The ordered call sequence — seven steps, every one of them a place where
 * getting it wrong is silent. `shim/cq_template_impl.c` documents each. */
int32_t cq_tpl_binary(tpl_req r);

/* --- the handle boundary's three DOORS, shared across the third seam ------
 *
 * They are exported rather than duplicated because a second copy of a guard is
 * how a deleted one keeps passing (this project's recorded trap, five times
 * over). `cq_tpl_width` validates a width as a RANGE, never a whitelist;
 * `cq_tpl_src` is the READ door and ADMITS a measured rail; `cq_tpl_out` is the
 * WRITE door, REFUSES one, and RETURNS the pointer so that `out` is resolved
 * exactly once on every path — which is what makes the asymmetry structural
 * rather than a convention (swap the doors and the const qualifier refuses to
 * compile). All three carry ONE width message between them. */
uint32_t cq_tpl_width(int bits);
void     cq_tpl_src(cq_ctx *ctx, int32_t h, uint32_t w);
cq_bit  *cq_tpl_out(cq_ctx *ctx, int32_t h, uint32_t w);

/* --- the ARITY-1 ordered call sequence ------------------------------------
 *
 * ONE STAGE IS `fn(ctx, dst, src, f, t)` PLUS THE TAG ITS RECORD CARRIES. A
 * plain arity-1 operation is one OUTER stage reading `a` and writing `out`; a
 * composed one puts an INNER stage in front of it, from `a` into a 64-bit
 * WORKSPACE rail, and the outer stage then reads the workspace.
 *
 * THE WORKSPACE IS D7b's TEMPORARY IN A SECOND DRESS and is handled the same
 * way: minted through `cq_reg_alloc_zero`, RECORDED at birth so the certificate
 * has a history for it, written by a FWD/UNC twin pair so the reduction can
 * cancel them, and freed through `cq_shim_free_proof` — the same door every
 * other rail goes through. */
typedef struct {
    cq_tpl_cast_fn fn;      /* NULL on `inner` when there is no workspace   */
    uint32_t f, t;          /* the widths `fn` is CALLED at, in bits        */
    uint32_t tag;           /* D15's twin identity for THIS stage's record  */
} tpl_ustage;

typedef struct {
    tpl_ustage inner, outer;
    uint32_t   w, wout;     /* the ABI's operand and result widths, in bits */
    int32_t    a_h, out;    /* `out` is CQ_REG_NONE on a forward symbol     */
    const char *name;       /* D21's `op begin` payload                     */
} tpl_ureq;

int32_t cq_tpl_unary(tpl_ureq r);

/* D7b's RECORD, exported rather than copied. `cq_tpl_binary` and
 * `cq_tpl_ternary` both push a `CQ_ROP_COPY` for each half of a defensive
 * copy, and the argument order is the ABI's (`cqrt_copy_<W>(src, dst)`) rather
 * than `cq_reg_xor_into`'s, which is the reverse. A second spelling of that in
 * the ternary file would be a second chance to get the order backwards — and a
 * backwards COPY record still reduces, just against the wrong rail. */
void cq_tpl_rec_copy(int32_t src, int32_t dst);

/* --- the ARITY-3 ordered call sequence ------------------------------------
 *
 * ONE FAMILY REACHES IT — `fma`, `intrinsic_table.yaml`'s only `arity:
 * ternary` opcode (PRD-v2 §6.1's vendoring, bead 9ve.24). Four shapes, because
 * two of the three operands may arrive as literals: `qqq` (the BARE base
 * symbol), `qql`, `qlq` and `qll`, each with an `_unc` twin. There is NO
 * `_controlled` axis and no `_inv` anywhere in that table's `fma` row, so this
 * sequence opens no §9 region and owes no D14 abort.
 *
 * TWO LITERAL LANES IS WHY THIS IS NOT `tpl_req` WITH A THIRD HANDLE.
 * `tpl_req` carries ONE `(lo, hi)` pair, which `qll` needs two of. Each lane
 * gets its own pair below and its own `cq_bit` buffer at the call site; a
 * single shared buffer would make `fma(a, 2.0, 3.0)` compute `fma(a, 3.0, 3.0)`
 * — right shape, right gate count, wrong value, and only L1 sees it.
 *
 * D7b HAS SIX PAIRS AT THIS ARITY AND `cq_reg_sources_alias` ANSWERS A BARE
 * 0/1. Three sources can collide in four distinguishable ways (a==b, a==c,
 * b==c, all three) needing ONE or TWO temporaries, so this sequence does its
 * own pairwise scan; the binary door's trick of adding that predicate's 0/1 to
 * `cq_reg_count` for the D21 prediction is arity-2's and does not carry. */
typedef struct {
    cq_tpl_fma_fn k;         /* the kernel; NULL from no builder here       */
    uint32_t w;              /* operand width == result width, in bits      */
    int32_t  a_h, b_h, c_h;  /* CQ_REG_NONE on a lane carrying a literal    */
    uint64_t b_lo, b_hi;     /* lane b's classical operand, two-word LE     */
    uint64_t c_lo, c_hi;     /* lane c's                                     */
    int32_t  out;            /* CQ_REG_NONE on a forward symbol             */
    uint32_t tag;            /* D15's twin identity — NOT the kernel        */
    const char *name;        /* D21's `op begin` payload                    */
} tpl3_req;

int32_t cq_tpl_ternary(tpl3_req r);

#endif /* CQ_TEMPLATE_BOUNDARY_H */
