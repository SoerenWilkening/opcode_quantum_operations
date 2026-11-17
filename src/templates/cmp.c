/* BACKEND_ABI §2.4 icmp, §2.5 select, §2.7 smax smin umax umin abs (kmgy.14), every shape.
 * Declarations: include/cq_template.h. */
#include "cq_template.h"
#include "tpl.h"

#define BW_(name, kop, n, bits, LIT, V) CQ_TPL_BINARY_(name, kop, n, bits) CQ_TPL_HL_(name, kop, n, bits, LIT, V)
#define ICMP_(p, n, bits, LIT, V) BW_(icmp_##p, CQ_KOP_ICMP_##p, n, bits, LIT, V)
#define MINMAX_(name, n, bits, LIT, V) BW_(name, CQ_KOP_##name, n, bits, LIT, V)
#define SEL_(n, bits, NLIT, LIMBS, s1, s2, sfx) CQ_TPL_TERN_(select, CQ_KOP_SELECT, n, bits, NLIT, LIMBS, q, s1, s2, sfx)
#define CMP_(n, bits, LIT, V, NLIT, LIMBS) \
    ICMP_(eq, n, bits, LIT, V) ICMP_(ne, n, bits, LIT, V) ICMP_(ugt, n, bits, LIT, V) ICMP_(uge, n, bits, LIT, V) \
    ICMP_(ult, n, bits, LIT, V) ICMP_(ule, n, bits, LIT, V) ICMP_(sgt, n, bits, LIT, V) ICMP_(sge, n, bits, LIT, V) \
    ICMP_(slt, n, bits, LIT, V) ICMP_(sle, n, bits, LIT, V) \
    MINMAX_(smax, n, bits, LIT, V) MINMAX_(smin, n, bits, LIT, V) MINMAX_(umax, n, bits, LIT, V) MINMAX_(umin, n, bits, LIT, V) \
    CQ_TPL_UNARY_(abs, CQ_KOP_ABS, n, bits) \
    SEL_(n, bits, NLIT, LIMBS, q, q, ) SEL_(n, bits, NLIT, LIMBS, q, l, _hl) \
    SEL_(n, bits, NLIT, LIMBS, l, q, _lh) SEL_(n, bits, NLIT, LIMBS, l, l, _ll)
#define NARROW_(n, bits) CMP_(n, bits, CQ_TPL_LIT_NARROW_, CQ_TPL_V_NARROW_, CQ_TPL_NLIT_NARROW_, CQ_TPL_LIMBS_NARROW_)
#define WIDE_(n, bits) CMP_(n, bits, CQ_TPL_LIT_WIDE_, CQ_TPL_V_WIDE_, CQ_TPL_NLIT_WIDE_, CQ_TPL_LIMBS_WIDE_)
CQ_WIDTHS_NARROW(NARROW_)
CQ_WIDTHS_WIDE(WIDE_)
