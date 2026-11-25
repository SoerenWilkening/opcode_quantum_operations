/* Public core entry points of BACKEND_ABI §1 (libcq_backend.a): alloc, free,
 * measure, the i1 logic, copy, xorc and their _controlled forms, the in-place add (§1.15), push/pop.
 * Generated per width from CQ_WIDTHS and CQ_FP_WIDTHS (cq_widths.h).
 *
 * The ABI gives LLVM types; the C types below are chosen so a caller compiled
 * from LLVM IR on x86-64 and a C caller of this header agree:
 *   - iN, N <= 128: the smallest of 8/16/32/64/128-bit integers holding it: one
 *     GPR, two (low, high) above 64 bits, returned in RAX or RAX:RDX. Only the
 *     low N bits of an argument are read. measure returns i1 as 0/1 and every
 *     other width sign-extended from N (the ABI asks this of i8 and i16).
 *   - iN, 128 < N <= 256: four 64-bit limb arguments, low first (LLVM passes
 *     them in four GPRs, and every symbol here has them all in registers);
 *     measure returns a 32-byte struct, which LLVM and C both return through a
 *     hidden pointer. Bits at and above N: ignored in arguments, 0 in results.
 *   - fp tokens: the C type of the LLVM type, as a bit pattern (no conversion). */
#ifndef CQRT_H
#define CQRT_H
#include <stdint.h>
#include "cq_widths.h"

#define CQRT_SIZE_(n) (char (*)[1 + ((n) > 8) + ((n) > 16) + ((n) > 32) + ((n) > 64)])0
/* Type selection by width, so a new width needs no table: unsigned and signed. */
#define CQRT_UINT(n) __typeof__(_Generic(CQRT_SIZE_(n), char (*)[1]: (uint8_t)0, char (*)[2]: (uint16_t)0, \
    char (*)[3]: (uint32_t)0, char (*)[4]: (uint64_t)0, default: (unsigned __int128)0))
#define CQRT_INT(n) __typeof__(_Generic(CQRT_SIZE_(n), char (*)[1]: (int8_t)0, char (*)[2]: (int16_t)0, \
    char (*)[3]: (int32_t)0, char (*)[4]: (int64_t)0, default: (__int128)0))

typedef struct { uint64_t l[4]; } cqrt_u256;   /* a measured iN, 128 < N <= 256 */
#define CQRT_LIMBS_ uint64_t l0, uint64_t l1, uint64_t l2, uint64_t l3

#define CQRT_DECL_(n, LIT, RET) \
    uint32_t cqrt_alloc_##n(LIT); \
    RET cqrt_measure_##n(uint32_t h); \
    void cqrt_copy_##n(uint32_t s, uint32_t d); \
    void cqrt_copy_##n##_controlled(uint32_t c, uint32_t s, uint32_t d); \
    void cqrt_xorc_##n(uint32_t h, LIT); \
    void cqrt_xorc_##n##_controlled(uint32_t c, uint32_t h, LIT);
/* §1.15, integer widths: d += s, d -= s, h += lit in place, mod 2^W; d (h) is any live rail. */
#define CQRT_DECL_ADD_(n, LIT) \
    void cqrt_add_##n(uint32_t s, uint32_t d); \
    void cqrt_sub_##n(uint32_t s, uint32_t d); \
    void cqrt_addc_##n(uint32_t h, LIT);
#define CQRT_DECL_NARROW_(n, bits) CQRT_DECL_(n, CQRT_UINT(bits) lit, CQRT_INT(bits)) CQRT_DECL_ADD_(n, CQRT_UINT(bits) lit)
#define CQRT_DECL_WIDE_(n, bits)   CQRT_DECL_(n, CQRT_LIMBS_, cqrt_u256) CQRT_DECL_ADD_(n, CQRT_LIMBS_)
#define CQRT_DECL_FP_(n, bits, T)  CQRT_DECL_(n, T lit, T)
CQ_WIDTHS_NARROW(CQRT_DECL_NARROW_)
CQ_WIDTHS_WIDE(CQRT_DECL_WIDE_)
CQ_FP_WIDTHS(CQRT_DECL_FP_)

void     cqrt_free(uint32_t h);
void     cqrt_not_i1(uint32_t h);
void     cqrt_and_i1(uint32_t a, uint32_t b, uint32_t t);
void     cqrt_or_i1(uint32_t a, uint32_t b, uint32_t t);
void     cqrt_xor_i1(uint32_t a, uint32_t t);
void     cqrt_push(uint32_t w);
uint32_t cqrt_pop(void);
void     cqrt_invert_begin(void);   /* src/invert.c */
void     cqrt_invert_end(void);

/* Rotations, §1.4-§1.7 (src/rot.c). A is an f64 rail; a quantum A runs the angle ladder
 * (src/ladder.c). */
#define CQRT_DECL_ROT_(n, ...) \
    void cqrt_ry_##n(uint32_t h, double theta); \
    void cqrt_ry_##n##_controlled(uint32_t c, uint32_t h, double theta); \
    void cqrt_ry_##n##_qangle(uint32_t h, uint32_t A); \
    void cqrt_ry_##n##_qangle_controlled(uint32_t c, uint32_t h, uint32_t A);
CQ_WIDTHS(CQRT_DECL_ROT_)
CQ_FP_WIDTHS(CQRT_DECL_ROT_)
void     cqrt_rz_i1(uint32_t f, double theta);
void     cqrt_rz_qangle(uint32_t A);
void     cqrt_rz_qangle_controlled(uint32_t c, uint32_t A);

/* Memory bus, §1.10 (src/bus.c). addr: a 64-bit rail in the plain forms, an address in the _hl
 * forms. The load _unc takes h before c (ABI:397, inventory D2). A quantum address rail goes through
 * the fan-out QRAM (src/qram.c), over at most 2^Q registered cells (cq_bus.h). */
#define CQRT_DECL_BUS_(n, ...) \
    uint32_t cqrt_qram_load_##n(uint32_t m, uint32_t addr); \
    uint32_t cqrt_qram_load_##n##_hl(uint32_t m, uint64_t addr); \
    uint32_t cqrt_qram_load_##n##_controlled(uint32_t c, uint32_t m, uint32_t addr); \
    uint32_t cqrt_qram_load_##n##_hl_controlled(uint32_t c, uint32_t m, uint64_t addr); \
    void cqrt_qram_load_##n##_unc(uint32_t h, uint32_t m, uint32_t addr); \
    void cqrt_qram_load_##n##_hl_unc(uint32_t h, uint32_t m, uint64_t addr); \
    void cqrt_qram_load_##n##_controlled_unc(uint32_t h, uint32_t c, uint32_t m, uint32_t addr); \
    void cqrt_qram_load_##n##_hl_controlled_unc(uint32_t h, uint32_t c, uint32_t m, uint64_t addr); \
    void cqrt_qram_store_##n(uint32_t m, uint32_t addr, uint32_t v); \
    void cqrt_qram_store_##n##_hl(uint32_t m, uint64_t addr, uint32_t v); \
    void cqrt_qram_store_##n##_controlled(uint32_t c, uint32_t m, uint32_t addr, uint32_t v); \
    void cqrt_qram_store_##n##_hl_controlled(uint32_t c, uint32_t m, uint64_t addr, uint32_t v); \
    void cqrt_qram_swap_##n(uint32_t m, uint32_t addr, uint32_t h); \
    void cqrt_qram_swap_##n##_hl(uint32_t m, uint64_t addr, uint32_t h); \
    void cqrt_qram_swap_##n##_controlled(uint32_t c, uint32_t m, uint32_t addr, uint32_t h); \
    void cqrt_qram_swap_##n##_hl_controlled(uint32_t c, uint32_t m, uint64_t addr, uint32_t h);
CQ_WIDTHS(CQRT_DECL_BUS_)
CQ_FP_WIDTHS(CQRT_DECL_BUS_)
uint32_t cqrt_qram_alloc_i8(uint32_t n);
/* An object [addr, addr + size) for the access bound (src/qram.c): no gate, no qubit; size <= 0 and a
 * range reaching 2^64 register nothing; overlapping registrations merge. */
void     cqrt_qram_object(int64_t addr, int64_t size);
void     cqrt_qram_pointer(uint32_t h);   /* §1.10 "A pointer load": h's load ignores the non-address cells of its bound */
void     cqrt_qram_data(uint32_t h);      /* §1.10 "Data": h is data from now on, whatever it was derived from */
void     cqrt_qram_unbound(uint32_t h);   /* §1.10 "No object": h carries no note from now on */

/* Tape and abort, §1.11 (src/tape.c). name: a host symbol, or NULL for the address in the first
 * field. abort_i1 of a classical 1 does not return. INPUT calls merge under a quantum predicate,
 * buffer address, length, stream, pread offset or getdelim delimiter. The limits ABI §1.11 Limits
 * permits fail loudly (docs/COMPLETENESS.md); a deferred v-form's quantum va_list pointer, which
 * the ABI does not rule, aborts (kmgy.43). */
#define CQRT_DECL_TAPE_(n, LIT) \
    uint32_t cqrt_tape_effect_##n##_controlled(uint32_t c, uint32_t t, const char *name, uint32_t nf); \
    uint32_t cqrt_tape_write_##n(uint32_t t, uint32_t h); \
    uint32_t cqrt_tape_write_##n##_controlled(uint32_t c, uint32_t t, uint32_t h); \
    void cqrt_tape_write_##n##_lit(uint32_t t, LIT);
#define CQRT_DECL_TAPE_NARROW_(n, bits) CQRT_DECL_TAPE_(n, CQRT_UINT(bits) lit)
#define CQRT_DECL_TAPE_WIDE_(n, bits)   CQRT_DECL_TAPE_(n, CQRT_LIMBS_)
#define CQRT_DECL_TAPE_FP_(n, bits, T)  CQRT_DECL_TAPE_(n, T lit)
CQ_WIDTHS_NARROW(CQRT_DECL_TAPE_NARROW_)
CQ_WIDTHS_WIDE(CQRT_DECL_TAPE_WIDE_)
CQ_FP_WIDTHS(CQRT_DECL_TAPE_FP_)
uint32_t cqrt_tape_alloc(void);
void     cqrt_tape_fmt(uint32_t t, const char *fmt);
void     cqrt_tape_fmt_controlled(uint32_t c, uint32_t t, const char *fmt);
void     cqrt_tape_effect_controlled(uint32_t c, uint32_t t, const char *name, uint32_t nf);
void     cqrt_abort_i1(uint32_t f);
#endif
