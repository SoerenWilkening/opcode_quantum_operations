/* BACKEND_ABI §2.3 shl, lshr, ashr (bare, _hl, _lh) and fshl, fshr (bare and the six mixed
 * shapes). Declarations: include/cq_template.h. */
#include "cq_template.h"
#include "tpl.h"

#define SH_(name, kop, n, bits, LIT, V) \
    CQ_TPL_BINARY_(name, kop, n, bits) CQ_TPL_HL_(name, kop, n, bits, LIT, V) CQ_TPL_LH_(name, kop, n, bits, LIT, V)
#define SHIFTS_(n, bits, LIT, V, NLIT, LIMBS) \
    SH_(shl, CQ_KOP_SHL, n, bits, LIT, V) SH_(lshr, CQ_KOP_LSHR, n, bits, LIT, V) SH_(ashr, CQ_KOP_ASHR, n, bits, LIT, V) \
    CQ_TPL_TERN_SHAPES(CQ_TPL_TERN_, fshl, CQ_KOP_FSHL, n, bits, NLIT, LIMBS) \
    CQ_TPL_TERN_SHAPES(CQ_TPL_TERN_, fshr, CQ_KOP_FSHR, n, bits, NLIT, LIMBS)
#define NARROW_(n, bits) SHIFTS_(n, bits, CQ_TPL_LIT_NARROW_, CQ_TPL_V_NARROW_, CQ_TPL_NLIT_NARROW_, CQ_TPL_LIMBS_NARROW_)
#define WIDE_(n, bits) SHIFTS_(n, bits, CQ_TPL_LIT_WIDE_, CQ_TPL_V_WIDE_, CQ_TPL_NLIT_WIDE_, CQ_TPL_LIMBS_WIDE_)
CQ_WIDTHS_NARROW(NARROW_)
CQ_WIDTHS_WIDE(WIDE_)
