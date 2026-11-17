/* BACKEND_ABI §2.3 and, or, xor: bare and _hl. Declarations: include/cq_template.h. */
#include "cq_template.h"
#include "tpl.h"

#define BW_(name, kop, n, bits, LIT, V) CQ_TPL_BINARY_(name, kop, n, bits) CQ_TPL_HL_(name, kop, n, bits, LIT, V)
#define BITWISE_(n, bits, LIT, V) \
    BW_(and, CQ_KOP_AND, n, bits, LIT, V) BW_(or, CQ_KOP_OR, n, bits, LIT, V) BW_(xor, CQ_KOP_XOR, n, bits, LIT, V)
#define NARROW_(n, bits) BITWISE_(n, bits, CQ_TPL_LIT_NARROW_, CQ_TPL_V_NARROW_)
#define WIDE_(n, bits) BITWISE_(n, bits, CQ_TPL_LIT_WIDE_, CQ_TPL_V_WIDE_)
CQ_WIDTHS_NARROW(NARROW_)
CQ_WIDTHS_WIDE(WIDE_)
