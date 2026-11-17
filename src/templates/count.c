/* BACKEND_ABI §2.8 ctpop, ctlz, cttz (kmgy.16). Declarations: include/cq_template.h. */
#include "cq_template.h"
#include "tpl.h"

#define COUNT_(n, bits) \
    CQ_TPL_UNARY_(ctpop, CQ_KOP_CTPOP, n, bits) CQ_TPL_UNARY_(ctlz, CQ_KOP_CTLZ, n, bits) CQ_TPL_UNARY_(cttz, CQ_KOP_CTTZ, n, bits)
CQ_WIDTHS(COUNT_)
