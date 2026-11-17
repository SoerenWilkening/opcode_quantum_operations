/* BACKEND_ABI §2.2 add, sub and §2.7 overflow and saturating ops (kmgy.10), every shape.
 * Declarations: include/cq_template.h. */
#include "cq_template.h"
#include "tpl.h"

#define BIN_(name, n, bits, LIT, V) \
    CQ_TPL_BINARY_(name, CQ_KOP_ARITH_##name, n, bits) \
    CQ_TPL_HL_(name, CQ_KOP_ARITH_##name, n, bits, LIT, V)
#define NONCOMM_(name, n, bits, LIT, V) BIN_(name, n, bits, LIT, V) CQ_TPL_LH_(name, CQ_KOP_ARITH_##name, n, bits, LIT, V)
#define ARITH_(n, bits, LIT, V) \
    BIN_(add, n, bits, LIT, V) BIN_(uadd_overflow, n, bits, LIT, V) BIN_(sadd_overflow, n, bits, LIT, V) \
    BIN_(uadd_sat, n, bits, LIT, V) BIN_(sadd_sat, n, bits, LIT, V) \
    NONCOMM_(sub, n, bits, LIT, V) NONCOMM_(usub_overflow, n, bits, LIT, V) NONCOMM_(ssub_overflow, n, bits, LIT, V) \
    NONCOMM_(usub_sat, n, bits, LIT, V) NONCOMM_(ssub_sat, n, bits, LIT, V)
#define ARITH_NARROW_(n, bits) ARITH_(n, bits, CQ_TPL_LIT_NARROW_, CQ_TPL_V_NARROW_)
#define ARITH_WIDE_(n, bits) ARITH_(n, bits, CQ_TPL_LIT_WIDE_, CQ_TPL_V_WIDE_)
CQ_WIDTHS_NARROW(ARITH_NARROW_)
CQ_WIDTHS_WIDE(ARITH_WIDE_)
