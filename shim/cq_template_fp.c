/* shim/cq_template_fp.c — M26's fp OPCODE surface (bead 9ve.28, PRD-v2 §1).
 *
 * The fp half of the seam `shim/cq_template_boundary.h` records: the four
 * `cq_shim_fcmp_*` entry points that M28's 56 live `cq_template_fcmp_*_f64*`
 * wrappers call, and nothing else. Read that header before adding a family.
 *
 * WHAT IS *NOT* HERE IS THE POINT OF THE FILE. There is no handle resolution,
 * no D7a refusal, no D7b copy, no mint, no §9 region, no D15 record and no D21
 * bracket — all of that is `cq_tpl_binary`'s, reached identically by the
 * integer surface. An fp family that grew its own copy of any of them would be
 * re-acquiring, one file over, every trap `shim/cq_template_impl.c`'s header
 * records. What this file owns is the three things a family really does own:
 * its kernel, its display name, and its tag space.
 *
 * `fcmp` NEEDS NO NEW EFFECT ROW, AND THAT IS WORTH STATING RATHER THAN LEAVING
 * TO BE REDISCOVERED. D15's `CQ_ROP_TPL_FWD` / `CQ_ROP_TPL_UNC` pair models "a
 * template forward WRITES the rail it mints, with its `_unc` as the declared
 * twin" over operand SLOTS, not over opcodes — `shim/cq_shim_record.c`'s table
 * is `{reads: R(1)|R(2), writes: R(0), twin: TPL_UNC}` — so an fp compare is
 * the same shape as an integer one and reuses both rows verbatim. What a new
 * family must NOT reuse is the TAG, which is the twin's identity; see below.
 *
 * WHY `bits` IS CHECKED HERE AND NOT ONLY IN THE KERNEL. `cq_kernel_fcmp_*`
 * hard-errors on `W != 64` in both configurations, so a wrong width cannot
 * silently compute — but it would abort AFTER the mint, the D7b copy and the
 * trace bracket's open, leaving a viewer an operation it never sees closed.
 * The refusal is therefore hoisted to the entry point, where the claim is about
 * the CALL. The message is deliberately DISJOINT from every other `shim:`
 * string in this directory (tests/CMakeLists.txt's FAIL_REGULAR_EXPRESSION pins
 * discriminate on the MESSAGE, not the module).
 *
 * RULE 12. Budget 120, trigger 100 — well under the house 240 because this file
 * is a per-family index and grows by ~35 counted lines per family (§7.15 has
 * six more to come). The seam when it fires is `the COMPARE family <-> the
 * ARITHMETIC families` -> `shim/cq_template_fparith.c`, which is a subject cut
 * on the same discriminator `cq_template_dispatch.h` uses: a compare's result
 * width is one bit and an arithmetic one's is the operand width, and the
 * arithmetic families carry the `_lh` shape and the §9 controlled axis that
 * compares have on NO axis (opcode_table.yaml's own note).
 */

#include "cq_shim.h"

#include "cq_shim_ctx.h"
#include "cq_template_boundary.h"
#include "cq_template_dispatch.h"

#include "reg.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* A FOURTH static with the house `shim:` prefix, on `src/reg_check.c`'s
 * precedent — two guards spelling the same sentence is how a deleted one keeps
 * passing, so this string appears nowhere else. */
static void cq_fp_die(const char *what, int32_t v)
{
    fprintf(stderr, "libcqops: FATAL: shim: %s (%d)\n", what, v);
    abort();
}

/* PRD-v2 §1 IS f64-ONLY AND THE REFUSAL SAYS SO. It is not "the kernel will
 * catch it": `cq_kernel_fcmp_*` aborts one layer down, after this call has
 * already minted a rail and opened a D21 bracket. */
static uint32_t fcmp_width(int bits)
{
    if (bits != 64)
        cq_fp_die("a cq_template_fcmp_* symbol names a width v2 does not "
                  "implement; PRD-v2 §1 scopes the fp port to f64 and every "
                  "soft_fcmp_* upstream is (UInt64, UInt64)", bits);
    return 64u;
}

/* The fp compare request. `wout` is ONE BIT at a 64-bit operand width —
 * `opcode_table.yaml`'s `result_type: flag_handle`, exactly as `icmp` — and the
 * tag space is `0xC0000000`, the fourth of the four listed in
 * shim/cq_template_boundary.h. Sharing `icmp`'s `0x40000000` would let
 * `cq_template_fcmp_olt_f64_unc` pair with an `icmp_slt_i64` forward and
 * discharge a free that nothing uncomputed, which is D15's certificate reading
 * a twin that never existed. */
static tpl_req fcmp_req(cq_shim_fpred p, int bits, int32_t a_h, int32_t b_h)
{
    tpl_req r = cq_tpl_req((int)fcmp_width(bits), a_h, b_h);

    r.k    = cq_tpl_fcmp_kernel(p);
    r.wout = 1u;
    r.tag  = 0xC0000000u + (uint32_t)p * 1024u + r.w + 1u;
    r.name = cq_tpl_fcmp_name(p);
    return r;
}

int32_t cq_shim_fcmp_qq(cq_shim_fpred pred, int bits, int32_t a_handle,
                        int32_t b_handle)
{
    return cq_tpl_binary(fcmp_req(pred, bits, a_handle, b_handle));
}

int32_t cq_shim_fcmp_hl(cq_shim_fpred pred, int bits, int32_t a_handle,
                        uint64_t lo, uint64_t hi)
{
    tpl_req r = fcmp_req(pred, bits, a_handle, CQ_REG_NONE);

    r.lo = lo; r.hi = hi;
    return cq_tpl_binary(r);
}

void cq_shim_fcmp_qq_unc(cq_shim_fpred pred, int bits, int32_t out_handle,
                         int32_t a_handle, int32_t b_handle)
{
    tpl_req r = fcmp_req(pred, bits, a_handle, b_handle);

    r.out = out_handle;
    (void)cq_tpl_binary(r);
}

void cq_shim_fcmp_hl_unc(cq_shim_fpred pred, int bits, int32_t out_handle,
                         int32_t a_handle, uint64_t lo, uint64_t hi)
{
    tpl_req r = fcmp_req(pred, bits, a_handle, CQ_REG_NONE);

    r.out = out_handle; r.lo = lo; r.hi = hi;
    (void)cq_tpl_binary(r);
}
