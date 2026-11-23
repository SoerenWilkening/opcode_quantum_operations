/* The one width list. A new width is one new line here. X(name, bits).
 * CQ_WIDTHS is every integer width in order. It is split only by how an LLVM iN
 * value crosses the x86-64 C ABI: up to 128 bits in one or two registers, wider
 * as 64-bit limbs (include/cqrt.h). A new width goes in the group it belongs to. */
#ifndef CQ_WIDTHS_H
#define CQ_WIDTHS_H
#define CQ_WIDTHS(X) CQ_WIDTHS_NARROW(X) CQ_WIDTHS_WIDE(X)
#define CQ_WIDTHS_NARROW(X) \
    X(i1, 1) \
    X(i3, 3) \
    X(i4, 4) \
    X(i5, 5) \
    X(i6, 6) \
    X(i7, 7) \
    X(i8, 8) \
    X(i13, 13) \
    X(i14, 14) \
    X(i15, 15) \
    X(i16, 16) \
    X(i17, 17) \
    X(i24, 24) \
    X(i28, 28) \
    X(i29, 29) \
    X(i31, 31) \
    X(i32, 32) \
    X(i33, 33) \
    X(i40, 40) \
    X(i43, 43) \
    X(i48, 48) \
    X(i50, 50) \
    X(i56, 56) \
    X(i63, 63) \
    X(i64, 64) \
    X(i80, 80) \
    X(i100, 100) \
    X(i112, 112) \
    X(i128, 128)
#define CQ_WIDTHS_WIDE(X) \
    X(i200, 200)
/* The widths of CQ_WIDTHS that are a multiple of 16 bits, where bswap exists (ABI §2.8).
 * The preprocessor cannot filter by bits % 16, so a new such width is listed here too;
 * tests/test_tpl_bits.c fails if this list and CQ_WIDTHS disagree. */
#define CQ_WIDTHS_X16(X) \
    X(i16, 16) \
    X(i32, 32) \
    X(i48, 48) \
    X(i64, 64) \
    X(i80, 80) \
    X(i112, 112) \
    X(i128, 128)

/* The fp width tokens core gates accept as bit patterns (inventory C7). X(name, bits, C type).
 * bf16 travels as a _Float16 holding its bits: the same register (XMM0), and clang 17 at -O0
 * rounds a __bf16 parameter through float, which loses subnormals and NaN payloads. */
#define CQ_FP_WIDTHS(X) \
    X(f16, 16, _Float16) \
    X(bf16, 16, _Float16) \
    X(f32, 32, float) \
    X(f64, 64, double) \
    X(f80, 80, long double)
#endif
