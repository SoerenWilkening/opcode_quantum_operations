/* BACKEND_ABI §2.2 mul and §2.7 umul_overflow, smul_overflow (kmgy.11): bare and _hl.
 * Declarations: include/cq_template.h. */
#include "cq_template.h"
#include "tpl.h"

#define MUL1_(name, kop, n, bits, LIT, V) CQ_TPL_BINARY_(name, kop, n, bits) CQ_TPL_HL_(name, kop, n, bits, LIT, V)
#define MUL_(n, bits, LIT, V) MUL1_(mul, CQ_KOP_MUL, n, bits, LIT, V) \
    MUL1_(umul_overflow, CQ_KOP_UMUL_OVERFLOW, n, bits, LIT, V) MUL1_(smul_overflow, CQ_KOP_SMUL_OVERFLOW, n, bits, LIT, V)
#define NARROW_(n, bits) MUL_(n, bits, CQ_TPL_LIT_NARROW_, CQ_TPL_V_NARROW_)
#define WIDE_(n, bits) MUL_(n, bits, CQ_TPL_LIT_WIDE_, CQ_TPL_V_WIDE_)
CQ_WIDTHS_NARROW(NARROW_)
CQ_WIDTHS_WIDE(WIDE_)
