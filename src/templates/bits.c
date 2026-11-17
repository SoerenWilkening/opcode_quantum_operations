/* BACKEND_ABI §2.8 bswap and bitreverse. Declarations: include/cq_template.h. */
#include "cq_template.h"
#include "tpl.h"

#define BSWAP_(n, bits) \
    _Static_assert((bits) % 16 == 0, #n " is not a multiple of 16: remove it from CQ_WIDTHS_X16"); \
    CQ_TPL_UNARY_(bswap, CQ_KOP_BSWAP, n, bits)
#define BITREVERSE_(n, bits) CQ_TPL_UNARY_(bitreverse, CQ_KOP_BITREVERSE, n, bits)
CQ_WIDTHS_X16(BSWAP_)
CQ_WIDTHS(BITREVERSE_)
