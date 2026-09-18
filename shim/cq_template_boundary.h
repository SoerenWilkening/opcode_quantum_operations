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
#ifndef CQ_TEMPLATE_BOUNDARY_H
#define CQ_TEMPLATE_BOUNDARY_H

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
 * THE FOUR TAG SPACES IN USE, and a fifth family must claim a fifth:
 *
 *     0x00000000 | op   * 1024 + w + 1      binary          (cq_template_impl.c)
 *     0x40000000 | pred * 1024 + w + 1      icmp            (cq_template_impl.c)
 *     0x80000000 | kind * 65536 + f*256 + t cast            (cq_template_impl.c)
 *     0xC0000000 | pred * 1024 + w + 1      fcmp            (cq_template_fp.c)
 *
 * The `+ 1` and the width term are not decoration: the width is folded in
 * because the ABI's `_unc` carries it too, and a forward at i32 is not the
 * adjoint of an `_unc` at i64 on the same handles. */
tpl_req cq_tpl_req(int bits, int32_t a_h, int32_t b_h);

/* The ordered call sequence — seven steps, every one of them a place where
 * getting it wrong is silent. `shim/cq_template_impl.c` documents each. */
int32_t cq_tpl_binary(tpl_req r);

#endif /* CQ_TEMPLATE_BOUNDARY_H */
