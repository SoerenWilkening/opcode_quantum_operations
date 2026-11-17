/* BACKEND_ABI §2.2 udiv, urem, sdiv, srem (kmgy.12), every shape. Declarations: include/cq_template.h. */
#include "cq_template.h"
#include "tpl.h"

#define DIV_(name, n, bits, LIT, V) \
    CQ_TPL_BINARY_(name, CQ_KOP_DIV_##name, n, bits) \
    CQ_TPL_HL_(name, CQ_KOP_DIV_##name, n, bits, LIT, V) \
    CQ_TPL_LH_(name, CQ_KOP_DIV_##name, n, bits, LIT, V)
#define DIVS_(n, bits, LIT, V) DIV_(udiv, n, bits, LIT, V) DIV_(urem, n, bits, LIT, V) DIV_(sdiv, n, bits, LIT, V) DIV_(srem, n, bits, LIT, V)
#define DIVS_NARROW_(n, bits) DIVS_(n, bits, CQ_TPL_LIT_NARROW_, CQ_TPL_V_NARROW_)
#define DIVS_WIDE_(n, bits) DIVS_(n, bits, CQ_TPL_LIT_WIDE_, CQ_TPL_V_WIDE_)
CQ_WIDTHS_NARROW(DIVS_NARROW_)
CQ_WIDTHS_WIDE(DIVS_WIDE_)
