/* BACKEND_ABI §2.6 zext, sext, trunc. Declarations: include/cq_template.h. */
#include "cq_template.h"
#include "tpl.h"

#define ZEXT_(f, fb, t, tb) CQ_TPL_CAST_(zext, CQ_KOP_ZEXT, f, fb, t, tb)
#define SEXT_(f, fb, t, tb) CQ_TPL_CAST_(sext, CQ_KOP_SEXT, f, fb, t, tb)
#define TRUNC_(f, fb, t, tb) CQ_TPL_CAST_(trunc, CQ_KOP_TRUNC, t, tb, f, fb)   /* the pair read downwards */
CQ_CAST_PAIRS(ZEXT_)
CQ_CAST_PAIRS(SEXT_)
CQ_CAST_PAIRS(TRUNC_)
