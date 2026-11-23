/* The in-scope ABI symbol list (kmgy.23), written from the ABI text, NOT from include/cqrt.h or
 * include/cq_template.h, so that a family missing from both is still expected. Prints one line per
 * symbol: "<archive> <family> <name>", archive "runtime" (cqrt_*) or "templates" (cq_template_*).
 * Widths come from the width lists only (CQ_WIDTHS, CQ_FP_WIDTHS); the cast pairs and the bswap
 * widths are derived here from CQ_WIDTHS, independently of cq_cast_pairs.h and CQ_WIDTHS_X16.
 * ABI:n is a line of CQ_lang spec/BACKEND_ABI.md at commit 87a5e30e (docs/ABI_SYMBOLS.md). */
#include <stdio.h>
#include "cq_widths.h"

#define I_(n, bits) { #n, bits },
#define F_(n, bits, T) { #n, bits },
static const struct { const char *n; int bits; } INT[] = { CQ_WIDTHS(I_) }, FP[] = { CQ_FP_WIDTHS(F_) };
#define NI (int)(sizeof INT / sizeof *INT)
#define NF (int)(sizeof FP / sizeof *FP)

/* Core families: cqrt_<op>_<W><suffix> for every suffix, at INT and the fp tokens (ABI:72-74, CR:91;
 * NORTH_STAR "Core gates accept the fp width tokens"). "" is the bare name. */
static const struct { const char *fam, *op, *suf[9]; } CORE_W[] = {
    { "core",  "alloc",   { "" } },                                   /* §1.1 ABI:212 */
    { "core",  "measure", { "" } },                                   /* §1.3 ABI:236 (every W, inventory D3) */
    { "core",  "copy",    { "", "_controlled" } },                    /* §1.9 ABI:353 */
    { "core",  "xorc",    { "", "_controlled" } },                    /* §1.9 ABI:354 */
    { "rot",   "ry",      { "", "_controlled" } },                    /* §1.4 ABI:250 */
    { "ladder","ry",      { "_qangle", "_qangle_controlled" } },      /* §1.5 ABI:262-263 */
    /* §1.10 ABI:395-397, table ABI:1120: load _<W>[_hl][_controlled][_unc] */
    { "bus",   "qram_load",  { "", "_hl", "_controlled", "_hl_controlled", "_unc", "_hl_unc",
                               "_controlled_unc", "_hl_controlled_unc" } },
    { "bus",   "qram_store", { "", "_hl", "_controlled", "_hl_controlled" } },   /* ABI:398, ABI:1121 */
    { "bus",   "qram_swap",  { "", "_hl", "_controlled", "_hl_controlled" } },   /* ABI:399, ABI:1121 */
    { "tape",  "tape_effect", { "_controlled" } },                    /* §1.11 ABI:597, ABI:1123 */
    { "tape",  "tape_write",  { "", "_controlled", "_lit" } },        /* ABI:598-599, ABI:1124 */
};
/* Core names with no width token (ABI §4 table, ABI:1111-1127). */
static const struct { const char *fam, *name; } CORE[] = {
    { "core", "free" },                                               /* §1.2 ABI:223 */
    { "core", "not_i1" }, { "core", "and_i1" }, { "core", "or_i1" }, { "core", "xor_i1" },  /* §1.8 ABI:338-341 */
    { "core", "push" }, { "core", "pop" },                            /* §1.12 ABI:622 */
    { "invert", "invert_begin" }, { "invert", "invert_end" },         /* §1.13 ABI:639 */
    { "rot", "rz_i1" },                                               /* §1.6 ABI:308 */
    { "ladder", "rz_qangle" }, { "ladder", "rz_qangle_controlled" },  /* §1.7 ABI:323 */
    { "bus", "qram_alloc_i8" },                                       /* §1.10 ABI:394 */
    { "bus", "qram_object" }, { "bus", "qram_unbound" },                                         /* §1.10 "Objects" (CQ_lang 6055f5b4) */
    { "tape", "tape_alloc" }, { "tape", "tape_fmt" }, { "tape", "tape_fmt_controlled" },   /* ABI:595-596 */
    { "tape", "tape_effect_controlled" },                             /* ABI:597 */
    { "tape", "abort_i1" },                                           /* ABI:600 */
};
/* Integer templates (§2.2-§2.8; D1: ABI:96 says §2.2-§2.7, §2.8 is integer too, ABI:1138) at every
 * INT width: cq_template_<op>_<W>[_<shape>] and its _unc (ABI:698-699: _unc for every forward).
 * Shapes by ABI:701: a binary op takes _hl, and _lh only if it does not commute; compares never _lh;
 * select's letters are its arms (hl lh ll); fshl/fshr every mix of q and l; a unary op is bare. */
#define BIN_C   { "", "_hl" }
#define BIN_N   { "", "_hl", "_lh" }
static const struct { const char *fam, *op, *shape[8]; } TPL[] = {
    { "arith", "add", BIN_C }, { "arith", "sub", BIN_N },             /* §2.2 ABI:728-729 */
    { "mul", "mul", BIN_C },                                          /* ABI:730 */
    { "div", "udiv", BIN_N }, { "div", "urem", BIN_N },               /* ABI:731-734 */
    { "div", "sdiv", BIN_N }, { "div", "srem", BIN_N },
    { "bitwise", "and", BIN_C }, { "bitwise", "or", BIN_C }, { "bitwise", "xor", BIN_C },  /* §2.3 ABI:755 */
    { "shift", "shl", BIN_N }, { "shift", "lshr", BIN_N }, { "shift", "ashr", BIN_N },     /* ABI:756 */
    { "shift", "fshl", { "", "_qql", "_qlq", "_qll", "_lqq", "_lql", "_llq" } },           /* ABI:757, 701 */
    { "shift", "fshr", { "", "_qql", "_qlq", "_qll", "_lqq", "_lql", "_llq" } },
    /* §2.4 ABI:782: pred in eq ne ugt uge ult ule sgt sge slt sle */
    { "icmp", "icmp_eq", BIN_C }, { "icmp", "icmp_ne", BIN_C }, { "icmp", "icmp_ugt", BIN_C },
    { "icmp", "icmp_uge", BIN_C }, { "icmp", "icmp_ult", BIN_C }, { "icmp", "icmp_ule", BIN_C },
    { "icmp", "icmp_sgt", BIN_C }, { "icmp", "icmp_sge", BIN_C }, { "icmp", "icmp_slt", BIN_C },
    { "icmp", "icmp_sle", BIN_C },
    { "select", "select", { "", "_hl", "_lh", "_ll" } },              /* §2.5 ABI:804 */
    /* §2.7 ABI:849-853; add and mul commute, sub does not */
    { "overflow", "uadd_overflow", BIN_C }, { "overflow", "sadd_overflow", BIN_C },
    { "overflow", "usub_overflow", BIN_N }, { "overflow", "ssub_overflow", BIN_N },
    { "overflow", "umul_overflow", BIN_C }, { "overflow", "smul_overflow", BIN_C },
    { "sat", "uadd_sat", BIN_C }, { "sat", "sadd_sat", BIN_C },
    { "sat", "usub_sat", BIN_N }, { "sat", "ssub_sat", BIN_N },
    { "minmax", "smax", BIN_C }, { "minmax", "smin", BIN_C }, { "minmax", "umax", BIN_C },
    { "minmax", "umin", BIN_C }, { "minmax", "abs", { "" } },
    /* §2.8 ABI:877-880 (bswap below: W a multiple of 16) */
    { "count", "ctlz", { "" } }, { "count", "cttz", { "" } }, { "count", "ctpop", { "" } },
    { "bits", "bitreverse", { "" } },
};

static void tpl(const char *fam, const char *name) {
    printf("templates %s cq_template_%s\ntemplates %s cq_template_%s_unc\n", fam, name, fam, name);
}

int main(void) {
    char b[160];
    for (size_t f = 0; f < sizeof CORE_W / sizeof *CORE_W; f++)
        for (int w = 0; w < NI + NF; w++)
            for (int s = 0; CORE_W[f].suf[s]; s++)
                printf("runtime %s cqrt_%s_%s%s\n", CORE_W[f].fam, CORE_W[f].op,
                       w < NI ? INT[w].n : FP[w - NI].n, CORE_W[f].suf[s]);
    for (size_t f = 0; f < sizeof CORE / sizeof *CORE; f++) printf("runtime %s cqrt_%s\n", CORE[f].fam, CORE[f].name);
    for (size_t f = 0; f < sizeof TPL / sizeof *TPL; f++)
        for (int w = 0; w < NI; w++)
            for (int s = 0; TPL[f].shape[s]; s++) {
                snprintf(b, sizeof b, "%s_%s%s", TPL[f].op, INT[w].n, TPL[f].shape[s]);
                tpl(TPL[f].fam, b);
            }
    for (int w = 0; w < NI; w++)            /* §2.8 ABI:879 bswap: W a multiple of 16 */
        if (INT[w].bits % 16 == 0) { snprintf(b, sizeof b, "bswap_%s", INT[w].n); tpl("bits", b); }
    for (int i = 0; i < NI; i++)            /* §2.6 ABI:820, ABI:700 "<from>_to_<to>": zext, sext widen, trunc narrows */
        for (int j = 0; j < NI; j++) {
            const char *op = INT[i].bits < INT[j].bits ? NULL : INT[i].bits > INT[j].bits ? "trunc" : "";
            if (!op) {
                snprintf(b, sizeof b, "zext_%s_to_%s", INT[i].n, INT[j].n); tpl("cast", b);
                snprintf(b, sizeof b, "sext_%s_to_%s", INT[i].n, INT[j].n); tpl("cast", b);
            } else if (*op) { snprintf(b, sizeof b, "trunc_%s_to_%s", INT[i].n, INT[j].n); tpl("cast", b); }
        }
    return 0;
}
