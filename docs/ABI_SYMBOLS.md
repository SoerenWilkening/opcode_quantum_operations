# ABI symbol inventory

Every symbol this library defines, and the ABI symbols it must not define. Issue
`opcode_quantum_operations-kmgy.1`. Scope is NORTH_STAR.md.

Sources, read only, nothing copied: CQ_lang `spec/BACKEND_ABI.md` ("ABI") and
`spec/CONSTRUCTION_RULES.md` ("CR"), both at CQ_lang commit `87a5e30e` (2026-11-17), sha256
prefixes `5307f38768f8` and `801e46be6deb`. A reference `ABI:123` or `CR:123` is a line number in
that revision. CR wins over ABI (ABI:4, NORTH_STAR "Goal"). Every disagreement found is in
"Disagreements" below.

## How to read the tables

- `<W>` is a width token. Expand it from the one width list:

  `INT = i1 i6 i7 i8 i16 i17 i24 i31 i32 i48 i64 i80 i100 i128 i200` (NORTH_STAR "Widths"; ABI:77 lists fewer integers; widened 2026-11-17, see C1).

  Core symbols marked **INT+FP** also take `FP = f16 bf16 f32 f64 f80` (NORTH_STAR "Core gates
  accept the fp width tokens as bit patterns"; CR:91 names the five; ABI:77 omits `bf16`, CR
  wins). A row whose width set is not `INT` (or `INT+FP`) says so.
- `<pred>` expands to `eq ne ugt uge ult ule sgt sge slt sle` (ABI:782).
- `h` is a handle: LLVM `i32`, C `uint32_t`. Handle 0 is never returned (ABI:58).
- `<T>` is the LLVM type of a literal or measured value of width `<W>`. C types:

  | `<W>` | LLVM `<T>` | C type, argument | C type, return (`measure`) | note |
  |---|---|---|---|---|
  | i1 | `i1` | `uint8_t` | `int8_t` | low bit only read; return 0 or 1, zero-extended (ABI:88) |
  | i6 | `i6` | `uint8_t` | `int8_t` | read the low 6 bits only; return sign-extended from bit 5 |
  | i7 | `i7` | `uint8_t` | `int8_t` | read the low 7 bits only; return sign-extended from bit 6 |
  | i8 | `i8` | `uint8_t` | `int8_t` | return sign-extended (ABI:88) |
  | i16 | `i16` | `uint16_t` | `int16_t` | return sign-extended (ABI:88) |
  | i17 | `i17` | `uint32_t` | `int32_t` | read the low 17 bits only; return sign-extended |
  | i24 | `i24` | `uint32_t` | `int32_t` | read the low 24 bits only; return sign-extended |
  | i31 | `i31` | `uint32_t` | `int32_t` | read the low 31 bits only; return sign-extended |
  | i32 | `i32` | `uint32_t` | `int32_t` | |
  | i48 | `i48` | `uint64_t` | `int64_t` | read the low 48 bits only; return sign-extended |
  | i64 | `i64` | `uint64_t` | `int64_t` | |
  | i80 | `i80` | `unsigned __int128` | `__int128` | two GPRs; read the low 80 bits only; return sign-extended |
  | i100 | `i100` | `unsigned __int128` | `__int128` | read the low 100 bits only; return sign-extended |
  | i128 | `i128` | `unsigned __int128` | `__int128` | |
  | i200 | `i200` | four `uint64_t` limbs, low first | `cqrt_u256` (32 bytes) | registers and stack as in "Calling convention" below (one more unused `uint64_t` where the literal starts at r9); measure returns through a hidden pointer; bits at and above 200 ignored in arguments, 0 in results |
  | f16 | `half` | `_Float16` | `_Float16` | |
  | bf16 | `bfloat` | `_Float16` | `_Float16` | holds the bits; same register (XMM0) as `bfloat`, and clang at -O0 rounds a `__bf16` parameter through float, losing subnormals and NaN payloads |
  | f32 | `float` | `float` | `float` | |
  | f64 | `double` | `double` | `double` | |
  | f80 | `x86_fp80` | `long double` | `long double` | x86-64 only |

  The C column is ours, not the ABI's: the ABI gives LLVM types only. Checked on this machine
  (x86_64, Apple clang 17, and LLVM 19.1.7 since kmgy.33) by compiling IR calls: `i7` goes in one GPR, `i80` and `i100` in two GPRs like
  `i128` (and comes back in RAX:RDX), `half`/`bfloat` in XMM0, `x86_fp80` on the stack. A C
  `_BitInt(100)` would be passed in memory, so it is not used. The backend reads only the low W
  bits of any narrow integer argument (ABI:87, CR:144), so the unused high bits are garbage.
- `ptr` is C `const char *` (a host string).
- Variant columns: `_unc` means a separate `..._unc` symbol exists, `_controlled` means a separate
  `..._controlled` symbol exists, and "literal shapes" lists the `_hl`/`_lh`/`_ll`/ternary
  spellings. Every variant is its own row in the core tables, because the ABI lists them
  separately.

## Calling convention: reference toolchain and wide literals (2026-11-17, kmgy.33)

The reference for how arguments cross is **LLVM 19.1.7** (Homebrew `llvm@19`,
`/usr/local/opt/llvm@19/bin/clang`): CQ_lang pins exactly it to compile the programs that call this
library (CQ_lang `CMakeLists.txt:1-22`). LLVM 22 is used for ASan builds only; Apple clang is not a
reference. `tests/ir/caller.ll` is compiled by `${CQ_LLVM_PREFIX}/bin/clang` (CMake cache variable,
default `/usr/local/opt/llvm@19`); with another clang the `ir_abi_*` tests run and
`ir_abi_reference` reports Skipped, without clang they are not built. The library itself needs any
C11 compiler with `__int128` (built and tested with Apple clang 17, LLVM 19.1.7, LLVM 22, GCC 15).

Measured on x86-64 with IR calls into a register and stack dump stub, at -O0 and -O2. `r` is the
number of integer argument registers (rdi rsi rdx rcx r8 r9) taken by earlier arguments, the
hidden result pointer included; `double` arguments go in XMM registers and do not count.

- `i200` (four 64-bit parts, low first). r ≤ 2: four registers. r = 3: limbs 0-2 in rcx r8 r9,
  limb 3 on the stack. r = 4: limbs 0-1 in r8 r9, limbs 2-3 on the stack. **r = 5: all four on
  the stack and r9 stays unused, also by every later argument** (they follow on the stack).
  r = 6: all four on the stack. Four `uint64_t` C parameters agree everywhere except r = 5, where
  C puts limb 0 in r9 (so does LLVM 22; Apple clang 17 does what LLVM 19 does).
- `i128` and `i100` (two parts): r ≤ 4: two registers; r = 5: both on the stack, r9 unused by
  later arguments too. C `unsigned __int128` compiled by clang 19 does the same, so C and IR agree
  (LLVM 22 differs: it lets a later argument use r9, and splits `i100`). No symbol has a two-part
  literal starting at r = 5 (the most is `_llq_unc`: `o`, two literals, `k` in r9).
- Narrow integers: one register; bits above W are undefined (`i8 -1` arrives as `0xff`).
- Returns: `i200` through a hidden pointer in rdi (a 32-byte slot; LLVM writes 25 bytes), as the
  C `cqrt_u256`; `i80 i100 i128` in RAX:RDX; `half bfloat float double` in XMM0; `x86_fp80` in
  ST0. All checked by `tests/ir/`.

So a wide literal that would begin at r = 5 has one unused `uint64_t <x>_r9` before its limbs in
the C prototype (a C caller passes 0 there): with r9 taken, C puts the limbs and every later
argument on the stack exactly as LLVM 19 does, whatever compiler builds the library. The
ternary shape macros (`CQ_TPL_TERN_PARAMS_`, `include/cq_template.h`) count r per operand, so this
follows the width list and the shapes. Today it is exactly `cq_template_{fshl,fshr}_i200_qll`,
`_lql` (third operand, r = 1 + 4), `_llq_unc` (second operand, r = 1 + 4) and
`cq_template_select_i200_ll` (second arm, r = 1 + 4; added by kmgy.14). Core, tape `_lit`,
bus, rotations, the binary `_hl`/`_lh` shapes and every other ternary shape start a wide literal
at r ≤ 4 and are unchanged. ABI:1050-1052 ("N/64 registers when all fit, else all on the stack
with the remaining registers skipped") does not match LLVM 19.1.7 for r = 3 and r = 4, which
split, and does not say that later arguments do not use a skipped register.

## Core gates, §1 (all in scope: NORTH_STAR "Core (§1)")

`_controlled` puts the control `c` first (ABI:207, CR:152). There is no core `_unc` symbol except
`cqrt_qram_load_*_unc`: an in-place core gate is an involution and its undo is the same call
(ABI:343-344, ABI:357-358; ABI §4 table ABI:1111-1127 expands no other core op with `_unc`).

| # | name pattern | C signature | ABI § | widths |
|---|---|---|---|---|
| 1 | `cqrt_alloc_<W>` | `uint32_t cqrt_alloc_<W>(<T> lit)` | 1.1 (ABI:212) | INT+FP |
| 2 | `cqrt_free` | `void cqrt_free(uint32_t h)` | 1.2 (ABI:223) | none |
| 3 | `cqrt_measure_<W>` | `<T> cqrt_measure_<W>(uint32_t h)` | 1.3 (ABI:236) | INT+FP (see D3) |
| 4 | `cqrt_ry_<W>` | `void cqrt_ry_<W>(uint32_t h, double theta)` | 1.4 (ABI:250) | INT+FP |
| 5 | `cqrt_ry_<W>_controlled` | `void cqrt_ry_<W>_controlled(uint32_t c, uint32_t h, double theta)` | 1.4 (ABI:250) | INT+FP |
| 6 | `cqrt_rz_i1` | `void cqrt_rz_i1(uint32_t f, double theta)` | 1.6 (ABI:308) | i1 only |
| 7 | `cqrt_not_i1` | `void cqrt_not_i1(uint32_t h)` | 1.8 (ABI:338) | i1 only |
| 8 | `cqrt_and_i1` | `void cqrt_and_i1(uint32_t a, uint32_t b, uint32_t t)` | 1.8 (ABI:339) | i1 only |
| 9 | `cqrt_or_i1` | `void cqrt_or_i1(uint32_t a, uint32_t b, uint32_t t)` | 1.8 (ABI:340) | i1 only |
| 10 | `cqrt_xor_i1` | `void cqrt_xor_i1(uint32_t a, uint32_t t)` | 1.8 (ABI:341) | i1 only |
| 11 | `cqrt_copy_<W>` | `void cqrt_copy_<W>(uint32_t s, uint32_t d)` | 1.9 (ABI:353) | INT+FP |
| 12 | `cqrt_copy_<W>_controlled` | `void cqrt_copy_<W>_controlled(uint32_t c, uint32_t s, uint32_t d)` | 1.9 (ABI:353) | INT+FP |
| 13 | `cqrt_xorc_<W>` | `void cqrt_xorc_<W>(uint32_t h, <T> lit)` | 1.9 (ABI:354) | INT+FP |
| 14 | `cqrt_xorc_<W>_controlled` | `void cqrt_xorc_<W>_controlled(uint32_t c, uint32_t h, <T> lit)` | 1.9 (ABI:354) | INT+FP |
| 15 | `cqrt_push` | `void cqrt_push(uint32_t w)` | 1.12 (ABI:622) | none |
| 16 | `cqrt_pop` | `uint32_t cqrt_pop(void)` | 1.12 (ABI:622) | none |
| 17 | `cqrt_invert_begin` | `void cqrt_invert_begin(void)` | 1.13 (ABI:639) | none |
| 18 | `cqrt_invert_end` | `void cqrt_invert_end(void)` | 1.13 (ABI:639) | none |

## Angle ladder, §1.5 and §1.7 (in scope: NORTH_STAR "Angle ladder")

`A` is always an f64 rail (ABI:264); its width is not in the name. `<W>` is the width of the
rotated rail `h`. CR retires the argument order `(A, h)` (CR:72).

| # | name pattern | C signature | ABI § | widths |
|---|---|---|---|---|
| 19 | `cqrt_ry_<W>_qangle` | `void cqrt_ry_<W>_qangle(uint32_t h, uint32_t A)` | 1.5 (ABI:262) | INT+FP |
| 20 | `cqrt_ry_<W>_qangle_controlled` | `void cqrt_ry_<W>_qangle_controlled(uint32_t c, uint32_t h, uint32_t A)` | 1.5 (ABI:262-263) | INT+FP |
| 21 | `cqrt_rz_qangle` | `void cqrt_rz_qangle(uint32_t A)` | 1.7 (ABI:323) | none |
| 22 | `cqrt_rz_qangle_controlled` | `void cqrt_rz_qangle_controlled(uint32_t c, uint32_t A)` | 1.7 (ABI:323) | none |

## Memory bus, §1.10 (in scope: NORTH_STAR "Memory bus")

`m` is the bus handle, `addr` is a handle to a 64-bit rail in the plain forms and a classical
`uint64_t` in the `_hl` forms (ABI:387-388). Name order of the suffixes is `_hl`, `_controlled`,
`_unc` (ABI:396-397; CQ_lang `examples/` declares `cqrt_qram_load_i1_hl_controlled_unc`). The
`_unc` puts `h` before `c` (ABI:397, CR:179), against the "control is argument 0" rule; see D2.

| # | name pattern | C signature | ABI § | widths |
|---|---|---|---|---|
| 23 | `cqrt_qram_alloc_i8` | `uint32_t cqrt_qram_alloc_i8(uint32_t n)` (n = 0 only) | 1.10 (ABI:394) | i8 only |
| 24 | `cqrt_qram_load_<W>` | `uint32_t cqrt_qram_load_<W>(uint32_t m, uint32_t addr)` | 1.10 (ABI:395) | INT+FP |
| 25 | `cqrt_qram_load_<W>_hl` | `uint32_t cqrt_qram_load_<W>_hl(uint32_t m, uint64_t addr)` | 1.10 (ABI:395) | INT+FP |
| 26 | `cqrt_qram_load_<W>_controlled` | `uint32_t cqrt_qram_load_<W>_controlled(uint32_t c, uint32_t m, uint32_t addr)` | 1.10 (ABI:396) | INT+FP |
| 27 | `cqrt_qram_load_<W>_hl_controlled` | `uint32_t cqrt_qram_load_<W>_hl_controlled(uint32_t c, uint32_t m, uint64_t addr)` | 1.10 (ABI:396) | INT+FP |
| 28 | `cqrt_qram_load_<W>_unc` | `void cqrt_qram_load_<W>_unc(uint32_t h, uint32_t m, uint32_t addr)` | 1.10 (ABI:397) | INT+FP |
| 29 | `cqrt_qram_load_<W>_hl_unc` | `void cqrt_qram_load_<W>_hl_unc(uint32_t h, uint32_t m, uint64_t addr)` | 1.10 (ABI:397) | INT+FP |
| 30 | `cqrt_qram_load_<W>_controlled_unc` | `void cqrt_qram_load_<W>_controlled_unc(uint32_t h, uint32_t c, uint32_t m, uint32_t addr)` | 1.10 (ABI:397) | INT+FP |
| 31 | `cqrt_qram_load_<W>_hl_controlled_unc` | `void cqrt_qram_load_<W>_hl_controlled_unc(uint32_t h, uint32_t c, uint32_t m, uint64_t addr)` | 1.10 (ABI:397) | INT+FP |
| 32 | `cqrt_qram_store_<W>` | `void cqrt_qram_store_<W>(uint32_t m, uint32_t addr, uint32_t v)` | 1.10 (ABI:398) | INT+FP |
| 33 | `cqrt_qram_store_<W>_hl` | `void cqrt_qram_store_<W>_hl(uint32_t m, uint64_t addr, uint32_t v)` | 1.10 (ABI:398) | INT+FP |
| 34 | `cqrt_qram_store_<W>_controlled` | `void cqrt_qram_store_<W>_controlled(uint32_t c, uint32_t m, uint32_t addr, uint32_t v)` | 1.10 (ABI:398) | INT+FP |
| 35 | `cqrt_qram_store_<W>_hl_controlled` | `void cqrt_qram_store_<W>_hl_controlled(uint32_t c, uint32_t m, uint64_t addr, uint32_t v)` | 1.10 (ABI:398) | INT+FP |
| 36 | `cqrt_qram_swap_<W>` | `void cqrt_qram_swap_<W>(uint32_t m, uint32_t addr, uint32_t h)` | 1.10 (ABI:399) | INT+FP |
| 37 | `cqrt_qram_swap_<W>_hl` | `void cqrt_qram_swap_<W>_hl(uint32_t m, uint64_t addr, uint32_t h)` | 1.10 (ABI:399) | INT+FP |
| 38 | `cqrt_qram_swap_<W>_controlled` | `void cqrt_qram_swap_<W>_controlled(uint32_t c, uint32_t m, uint32_t addr, uint32_t h)` | 1.10 (ABI:399) | INT+FP |
| 39 | `cqrt_qram_swap_<W>_hl_controlled` | `void cqrt_qram_swap_<W>_hl_controlled(uint32_t c, uint32_t m, uint64_t addr, uint32_t h)` | 1.10 (ABI:399) | INT+FP |

No store or swap `_unc` exists (ABI:398-399; `cqrt_qram_store_<W>*_unc` is retired, ABI:138).
An access touches store-size(T) cells: `ceil(N/8)` for `iN`, 2 for f16/bf16, 4, 8, 10 for
f32/f64/f80 (ABI:380-386, CR:193-195).

## Tape and abort, §1.11 (in scope: NORTH_STAR "Tape and abort", first version)

| # | name pattern | C signature | ABI § | widths |
|---|---|---|---|---|
| 40 | `cqrt_tape_alloc` | `uint32_t cqrt_tape_alloc(void)` | 1.11 (ABI:595) | none |
| 41 | `cqrt_tape_fmt` | `void cqrt_tape_fmt(uint32_t t, const char *fmt)` | 1.11 (ABI:596) | none |
| 42 | `cqrt_tape_fmt_controlled` | `void cqrt_tape_fmt_controlled(uint32_t c, uint32_t t, const char *fmt)` | 1.11 (ABI:596) | none |
| 43 | `cqrt_tape_effect_controlled` | `void cqrt_tape_effect_controlled(uint32_t c, uint32_t t, const char *name, uint32_t n)` | 1.11 (ABI:597) | none |
| 44 | `cqrt_tape_effect_<W>_controlled` | `uint32_t cqrt_tape_effect_<W>_controlled(uint32_t c, uint32_t t, const char *name, uint32_t n)` | 1.11 (ABI:597) | INT+FP: the callee's result type, a pointer as i64 |
| 45 | `cqrt_tape_write_<W>` | `uint32_t cqrt_tape_write_<W>(uint32_t t, uint32_t h)` | 1.11 (ABI:598) | INT+FP |
| 46 | `cqrt_tape_write_<W>_controlled` | `uint32_t cqrt_tape_write_<W>_controlled(uint32_t c, uint32_t t, uint32_t h)` | 1.11 (ABI:598) | INT+FP |
| 47 | `cqrt_tape_write_<W>_lit` | `void cqrt_tape_write_<W>_lit(uint32_t t, <T> lit)` | 1.11 (ABI:599) | INT+FP |
| 48 | `cqrt_abort_i1` | `void cqrt_abort_i1(uint32_t f)` | 1.11 (ABI:600) | i1 only |

There is no uncontrolled `cqrt_tape_effect` (ABI:597). The five INPUT rows of the host-boundary
table (ABI:514-518) are behaviour, not symbols; NORTH_STAR makes three of them a hard error, overruled 2026-11-17: the ABI wins and they are in scope (C4, kmgy.28).

### Symbols affected by the fp width tokens

Rows 1, 3, 4, 5, 11-14, 19, 20, 24-39, 44-47: every core row whose name has `<W>`. The rows without `<W>` (or fixed at `i1`/`i8`) are unaffected. ABI:1019-1022 confirms the fp
widths reach the backend only through §1 (`alloc`, `copy`, `measure`, `ry` and its forms, tape
writes), and the bus rows carry them by ABI:380-382.

## Integer templates, §2.2-§2.8 (all in scope: NORTH_STAR "Integer templates")

The ABI defines this family by a rule, not a list.

- Grammar, ABI:693 (= CR:117):
  `cq_template_<op>[_<pred>]_<W>[_to_<W2>][_<shape>][_rm|_rmq][_exc][_unc]`.
  `_rm`, `_rmq`, `_exc` are fp-only (ABI:703-704); no integer symbol has them.
- Forward, ABI:697: "`i32 @cq_template_X(args…)`. Takes a fresh `|0⟩` result rail o, sets o ^=
  f(args), returns o."
- `_unc`, ABI:698: "`void @cq_template_X_unc(i32 o, args…)`, with exactly the forward argument
  list and the same literals." ABI:699: "The backend provides `_unc` for **every** forward
  template." So every row below has a `_unc`, and the `_unc` C signature is
  `void cq_template_X_unc(uint32_t o, <forward args>)`.
- `_controlled`: none. ABI:198 "`_controlled` exists for core gates only, never for a template";
  ABI:705. The "controlled arithmetic grid" is retired (ABI:137, CR:70).
- Shapes, ABI:701 (= CR:127-134): "One letter per **data** operand in signature order: `h`/`q` is
  a handle and `l` a literal. Omitted means all handles. Binary ops use `_hl`, or `_lh` for a
  non-commutative op only (a commutative op is swapped to `_hl` …). Compares never take `_lh`; the
  predicate is flipped instead. `select` letters cover the two **arms**, and the condition is
  always a handle. Ternary ops (`fma`, `fshl`, `fshr`) take every mix of `q` and `l`, bare for
  `qqq`; … `fshl` and `fshr` keep every mixed shape. `_ll` is a binary op whose only quantum
  operand is the `_rmq` mode" (so no integer `_ll`, except `select`, whose `ll` names the arms).
- Literal type, ABI:702: "Typed as the LLVM operand … A narrow literal is read as its low W bits."
  `_lh` puts the literal first: `sub_i32_lh(i32 lit, i32 b)`, `shl_i64_lh(i64 lit, i32 k)`
  (ABI:713-714).
- Result width, ABI:700: "W, except compares, `*_overflow` and `is_fpclass` (`i1`), casts (W2)".

The shape sets and signatures below are this rule applied; they are derived, not listed in the
ABI. Commutativity is LLVM's (`add mul and or xor`, `{u,s}add`/`{u,s}mul` overflow, `{u,s}add_sat`,
`smax smin umax umin` are commutative). A literal slot is `<T>` of width W. Ternary shapes: the
six mixes `qql qlq qll lqq lql llq` plus bare; `lll` has no quantum operand and is not a template.

Shape → forward C signature (handles `uint32_t`, return `uint32_t` o):

| shape | binary op `(a, b)` | `select` `(c, arm1, arm2)` | ternary `(a, b, k)` |
|---|---|---|---|
| bare | `(uint32_t a, uint32_t b)` | `(uint32_t c, uint32_t a1, uint32_t a2)` | `(uint32_t a, uint32_t b, uint32_t k)` |
| `_hl` | `(uint32_t a, <T> lit)` | `(uint32_t c, uint32_t a1, <T> lit)` | |
| `_lh` | `(<T> lit, uint32_t b)` | `(uint32_t c, <T> lit, uint32_t a2)` | |
| `_ll` | | `(uint32_t c, <T> lit1, <T> lit2)` | |
| `_qql` etc. | | | each `l` slot is `<T>`, each `q` slot `uint32_t` |
| unary | `(uint32_t a)` | | |

| # | op | ABI § | shapes (forward; each also `_unc`) | result | widths | issue |
|---|---|---|---|---|---|---|
| 49 | `add` | 2.2 (ABI:728) | bare, `_hl` | W | INT | kmgy.10 |
| 50 | `sub` | 2.2 (ABI:729) | bare, `_hl`, `_lh` | W | INT | kmgy.10 |
| 51 | `mul` | 2.2 (ABI:730) | bare, `_hl` | W | INT | kmgy.11 |
| 52 | `udiv` | 2.2 (ABI:731) | bare, `_hl`, `_lh` | W | INT (i100, i128: EXTEND, ABI:750) | kmgy.12 |
| 53 | `urem` | 2.2 (ABI:732) | bare, `_hl`, `_lh` | W | INT (same) | kmgy.12 |
| 54 | `sdiv` | 2.2 (ABI:733) | bare, `_hl`, `_lh` | W | INT (same) | kmgy.12 |
| 55 | `srem` | 2.2 (ABI:734) | bare, `_hl`, `_lh` | W | INT (same) | kmgy.12 |
| 56 | `and` | 2.3 (ABI:755) | bare, `_hl` | W | INT | kmgy.13 |
| 57 | `or` | 2.3 (ABI:755) | bare, `_hl` | W | INT | kmgy.13 |
| 58 | `xor` | 2.3 (ABI:755) | bare, `_hl` | W | INT | kmgy.13 |
| 59 | `shl` | 2.3 (ABI:756) | bare, `_hl`, `_lh` | W | INT (rail amount at i7, i100: reduce mod W first, ABI:768-772) | kmgy.13 |
| 60 | `lshr` | 2.3 (ABI:756) | bare, `_hl`, `_lh` | W | INT (same) | kmgy.13 |
| 61 | `ashr` | 2.3 (ABI:756) | bare, `_hl`, `_lh` | W | INT (same) | kmgy.13 |
| 62 | `fshl` | 2.3 (ABI:757) | bare, `_qql`, `_qlq`, `_qll`, `_lqq`, `_lql`, `_llq` | W | INT (same) | kmgy.13 (implicit) |
| 63 | `fshr` | 2.3 (ABI:757) | as `fshl` | W | INT (same) | kmgy.13 (implicit) |
| 64 | `icmp_<pred>` | 2.4 (ABI:782) | bare, `_hl` (never `_lh`) | i1 | INT | kmgy.14 |
| 65 | `select` | 2.5 (ABI:804) | bare, `_hl`, `_lh`, `_ll` (arms) | W | INT | kmgy.14 |
| 66 | `zext` | 2.6 (ABI:820) | unary, `_<W>_to_<W2>`, W < W2 | W2 | pairs of INT | kmgy.15 |
| 67 | `sext` | 2.6 (ABI:820) | unary, `_<W>_to_<W2>`, W < W2 | W2 | pairs of INT | kmgy.15 |
| 68 | `trunc` | 2.6 (ABI:820) | unary, `_<W>_to_<W2>`, W > W2 | W2 | pairs of INT | kmgy.15 |
| 69 | `uadd_overflow` | 2.7 (ABI:849) | bare, `_hl` | i1 | INT | kmgy.10 |
| 70 | `sadd_overflow` | 2.7 (ABI:849) | bare, `_hl` | i1 | INT | kmgy.10 |
| 71 | `usub_overflow` | 2.7 (ABI:849) | bare, `_hl`, `_lh` | i1 | INT | kmgy.10 |
| 72 | `ssub_overflow` | 2.7 (ABI:849) | bare, `_hl`, `_lh` | i1 | INT | kmgy.10 |
| 73 | `umul_overflow` | 2.7 (ABI:849) | bare, `_hl` | i1 | INT | kmgy.10 or .11 (see G1) |
| 74 | `smul_overflow` | 2.7 (ABI:849) | bare, `_hl` | i1 | INT | kmgy.10 or .11 (see G1) |
| 75 | `uadd_sat` | 2.7 (ABI:850) | bare, `_hl` | W | INT | kmgy.10 |
| 76 | `usub_sat` | 2.7 (ABI:850) | bare, `_hl`, `_lh` | W | INT | kmgy.10 |
| 77 | `sadd_sat` | 2.7 (ABI:851) | bare, `_hl` | W | INT | kmgy.10 |
| 78 | `ssub_sat` | 2.7 (ABI:851) | bare, `_hl`, `_lh` | W | INT | kmgy.10 |
| 79 | `smax` | 2.7 (ABI:852) | bare, `_hl` | W | INT | kmgy.14 |
| 80 | `smin` | 2.7 (ABI:852) | bare, `_hl` | W | INT | kmgy.14 |
| 81 | `umax` | 2.7 (ABI:852) | bare, `_hl` | W | INT | kmgy.14 |
| 82 | `umin` | 2.7 (ABI:852) | bare, `_hl` | W | INT | kmgy.14 |
| 83 | `abs` | 2.7 (ABI:853) | unary | W | INT | kmgy.14 |
| 84 | `ctlz` | 2.8 (ABI:877) | unary | W | INT | kmgy.16 |
| 85 | `cttz` | 2.8 (ABI:877) | unary | W | INT | kmgy.16 |
| 86 | `ctpop` | 2.8 (ABI:878) | unary | W | INT | kmgy.16 |
| 87 | `bswap` | 2.8 (ABI:879) | unary | W | **i16 i32 i48 i64 i80 i128 only** ("W a multiple of 16"; list `CQ_WIDTHS_X16`) | kmgy.25 |
| 88 | `bitreverse` | 2.8 (ABI:880) | unary | W | INT | **none** (kmgy.25) |

Examples of the spelling: `cq_template_icmp_slt_i64_hl`, `cq_template_select_i8_lh`,
`cq_template_fshl_i32_qll`, `cq_template_zext_i1_to_i32`, `cq_template_sub_i64_hl_unc`
(ABI:707-719; `examples/` in CQ_lang declares `void @cq_template_sub_i64_hl_unc(i32, i32, i64)`).

Notes on widths:

- Casts expand over pairs of `INT`: with the 15 widths, 105 `zext`, 105 `sext`, 105 `trunc` pairs
  (every ordered pair, `include/cq_cast_pairs.h`; the 28 written here first counted the 8 widths
  of the first list, corrected 2026-11-18, kmgy.23). Every corpus pair (ABI:820) lies inside `INT`.
- `i1` is in `INT` for every template; the grammar allows it, and the ops degenerate (a shift by
  `k mod 1` is the identity, `icmp_slt_i1` is signed on one bit). §2.12 asks for `abs_i1`,
  `select_i1` and the `i1` bitwise ops (ABI:1034-1047).
- There is no `neg` and no `not` template. LLVM negation reaches the backend as `sub_<W>_lh`
  with literal 0, and `not` as `xor_<W>_hl` with all ones.
- There is no integer `bitcast`: every `bitcast` the ABI names has a float side (ABI:821, ABI:828).

## Counts

- Operations (ABI §4 counting, ABI:1107): 25 core operations, all in scope; 40 of the 66 template
  operations in scope (rows 49-88). 65 in-scope operations of 91.
- Name patterns: 48 core (rows 1-48) and 95 forward template patterns (counting `icmp_<pred>` and
  each cast as one pattern), each with its `_unc`: 190. In total 238 in-scope name patterns.
- Symbols at the 15 integer widths and 5 fp tokens (2026-11-18, kmgy.23): 618 `cqrt_*` (30 per
  width token times 20, plus 18 without one) and 3912 `cq_template_*` (1956 forward, each with its
  `_unc`), 4530 in all. `tests/abi_expected.c` generates the list from the rules above and the
  `abi_symbols` test compares it with the archives (docs/COMPLETENESS.md).

## Out of scope (must not be defined)

`kmgy.23` checks these are absent. Defining a float template would also shadow the compiler's
library ("a symbol it defines itself is taken instead of the library's", ABI:985-986).

| symbols | ABI § | why |
|---|---|---|
| `cq_template_fcmp_<pred>_<fW>*`, `cq_template_fcmps_<pred>_<fW>*` | 2.4 (ABI:783-786) | NORTH_STAR "Not ours": every fp template (§2.12) |
| `cq_template_select_<fW>*` | 2.5 (ABI:813-815) | same (a shim onto `select_i<N>`) |
| `fpext fptrunc sitofp uitofp fptosi fptoui`, `bitcast` (all forms) | 2.6 (ABI:827-829) | fp casts |
| `fadd fsub fmul fdiv frem fneg sqrt fma` | 2.9 | fp templates, `fneg` named in NORTH_STAR |
| `fabs copysign fmax fmin floor ceil trunc rint round is_fpclass` (`trunc` without `_to_`) | 2.10 | fp templates, `fabs`/`copysign` named in NORTH_STAR |
| every `_rm`, `_rmq`, `_exc` form | 2.1, 2.11 | fp only |
| `__cq_q_*` clones | 3 | NORTH_STAR: clones |
| `__cq_clone_register`, `__cq_clone_resolve`, `__cq_clone_count`, `__cq_clone_at` | 1.14 | NORTH_STAR: the clone table |
| `__cq_alloc`, `__cq_mem`, `__cq_sig` | 1.10, CR:222-244 | NORTH_STAR: injection bundle (`__cq_sig` is bundle too, ABI:99) |
| `cq_*`, `__cq_invert_begin/end`, `__cq_register`, `__cq_act_<fn>`, `__cq_k` | 0 (ABI:94, ABI:100) | never reach the backend |

Retired, must not be provided (ABI:134-139, CR:69-80): `cqrt_x`, `cqrt_cnot`, `cqrt_toffoli`,
`cqrt_phase_f64[_controlled]`, `cqrt_measure_<W>_controlled`, `cqrt_rz_<W>`,
`cqrt_ry_*_controlled_inv`, `cqrt_cswap`, `cqrt_tape_write_str_lit`, `cqrt_tape_str_begin/end`,
every `*_inv` template, the controlled arithmetic grid, `cqrt_qram_alloc_host_<W>[_s]`,
`cqrt_qram_realloc_<W>[_unc]`, `cqrt_qram_size`, `cqrt_qram_free`, `cqrt_qram_store_<W>*_unc`,
`cq_template_qrom_load_<W>[_s]`, the `i32`-index `_hl` bus forms, `cqrt_qram_alloc_<W>` for W
other than i8 or n > 0.

## (a) Template grouping: ABI vs issues kmgy.10-16

The ABI groups by section: §2.2 arithmetic `add sub mul udiv sdiv urem srem`; §2.3 bitwise and
shifts `and or xor shl lshr ashr fshl fshr`; §2.4 compare `icmp`; §2.5 `select`; §2.6 casts
`zext sext trunc`; §2.7 overflow, saturating, min/max, abs (15 ops); §2.8 bit counting `ctlz cttz
ctpop bswap bitreverse` (ABI:1131-1138). The issue column above maps each row.

- **G1.** `umul_overflow`, `smul_overflow` are claimed by no issue by name. kmgy.10 says "with
  overflow" next to add/sub; the ABI builds them from the 2W-bit product (ABI:860-862), which is
  kmgy.11's multiplier. Suggest kmgy.11.
- **G2.** `bswap` and `bitreverse` are covered by no issue (kmgy.16 names only ctpop, ctlz,
  cttz). Filed as kmgy.25.
- **G3.** `fshl`, `fshr` are not named in kmgy.13 but are in the ABI's "bitwise and shifts".
  Their rail-amount form needs `icmp_eq` and `select` (kmgy.14) and, at i7/i100, `urem` by a
  literal (kmgy.12) (ABI:773-777); so do `shl lshr ashr` by a rail at i7/i100 (ABI:768-772).
  kmgy.13 therefore depends on kmgy.12 and kmgy.14.
- **G4.** kmgy.10 names `neg`, kmgy.13 names `not`: neither is a symbol (see notes above).
- **G5.** `abs` (kmgy.14) is built from `sub` (kmgy.10), and the saturating ops (kmgy.10) from
  `select` (kmgy.14) (ABI:868-872). A cross dependency, not a gap.
- **G6.** The angle ladder (kmgy.17/18) needs the wide multiplier, adder, a shift or table
  lookup, and a conditional negate (ABI:284-293); the tape INPUT merge needs `icmp_eq_<W>_hl`
  (ABI:610-614).

No re-cut beyond G1 and G2 is needed.

## (b) Link order and archive split

ABI:102-104 names two archives in the order `main.o [lowered libraries (rule 27)] libcq_templates.a
libcq_runtime.a` (a referencing archive before the one it references). User decision 2026-11-18
(kmgy.42): one archive, `libcq_backend.a`, holds both, so the link is
`main.o [lowered libraries] libcq_backend.a <gate backend>` and no order between the two exists. The
layering the split enforced stays a source rule, tested by `templates_layering`:

- `libcq_backend.a`: every `cqrt_*` symbol (rows 1-48) and every `cq_template_*` symbol (rows 49-88
  and their `_unc`).
- Nothing compiled from outside `src/templates/` references a `cq_template_*` symbol. The runtime
  needs integer circuits itself (G6: the angle ladder, the tape INPUT merge), so the integer
  kernels (the gate emitters) are in `src/kernels/` under internal names, and the `cq_template_*`
  entry points in `src/templates/` call them.
- `libcq_gates_replay.a` (the seven `__quantum__qis__*` operations) stays separate and comes
  after `libcq_backend.a`; NORTH_STAR "Done when" 1 allows any other definition there.
- Shared-library builds need no order (ABI:111-114). Either way one process holds one copy of the
  backend (ABI:116-119).

Apple's ld does not enforce archive order, so a link test on macOS passes even when the archive
references a symbol defined later; the `abi_symbols` test checks with `nm` that every `cq*` symbol
`libcq_backend.a` references is defined by it, and `templates_layering` checks the source rule.

## Disagreements between ABI and CR

No symbol name or C-level argument list differs between the two. What differs or is
inconsistent:

> **Resolved in the ABI, 2026-11-18** (working tree of CQ_lang `spec/`, not yet committed there):
> D1-D5 below, the wide-literal paragraph (it now gives the LLVM 19.1.7 table and names that
> version normative), the single archive `libcq_backend.a`, the ladder and classical rotation
> angles, P up to a global phase, and the resource limits. The notes are kept as the record of
> what was found; line numbers refer to the older revision.

- **D1.** ABI:96 says the backend's integer templates are §2.2-§2.7, but §2.8 (bit counting) is
  integer too, is not on the compiler's list (ABI:97), and is in §4 (ABI:1138). CR does not split
  by section. Read as §2.2-§2.8, as NORTH_STAR does.
- **D2.** CR:152 "Control flag is always argument 0 of a `_controlled` form", but CR:179 and
  ABI:397 both give `qram_load_*_controlled_unc(h, c, m, addr)`, and CQ_lang's examples declare
  it that way. Both documents agree on the explicit signature; the rows above follow it.
- **D3.** ABI:236 lists the return types of `cqrt_measure_<W>` as `i1, i8, i16, i32, i64, float,
  double, x86_fp80`: no `half`, `bfloat`, `i7`, `i100`, `i128`. CR:157 gives no list and CR:91
  makes every width legal. CR wins: `measure` exists at every `<W>`, typed as in the type table.
- **D4.** ABI:208 says "`_unc` of an in-place core gate takes the same arguments (CR §1.3)". CR
  §1.3 says nothing like that and defines no core `_unc` symbol other than the load's. Read as:
  the undo is the same call, no `_unc` symbol (rows 1-48 are complete).
- **D5.** ABI:77 lists the corpus fp widths without `bf16`; CR:91 and ABI:73 include it. Taken as
  in scope for the core gates.

## (c) Where NORTH_STAR contradicts the ABI or leaves it open

- **C1. Widths.** NORTH_STAR fixes `INT`; ABI:77-78 and CR:91 say any `N ≥ 1` is legal. The
  compiler's float library, which the driver links before the backend (ABI:984-985), needs from
  the backend `cqrt_alloc_<W>`/`cqrt_copy_<W>` at f16..f80 and at `i24 i48 i80 i200`, plus integer
  templates at `i6 i17 i24 i31 i48 i80 i200` (`and_hl i6`, `bitreverse i6`, `select_i80`,
  `shl_i200_hl`, `trunc_i64_to_i17`, …; ABI:1017-1052). With `INT` alone a program using
  floats does not link. `i200` literals also need the N > 128 argument passing of ABI:1050-1052.
  Filed as kmgy.26. **Resolved 2026-11-17:** `INT` now includes `i6 i17 i24 i31 i48 i80 i200`
  (include/cq_widths.h); kept here as the record of why.
- **C2. P as rz.** ABI:309-310 "It is not Rz … Implement exactly P." NORTH_STAR emits P as `rz`.
  The difference is a global phase as long as the gate is never under a control, which holds for
  the backend's own emission (§1.6, §1.7 bracket with CCX). A later stage that fuses the bracket
  (ABI:315-319) would have to know it sees Rz, not P.
  **Outcome 2026-11-17:** P is emitted as rz (kept).
- **C3. cq_invert.** NORTH_STAR: ABI calls in an open region are "recorded, not executed, and
  replayed in reverse". ABI:640-653: allocations take effect at their call (the host needs the
  handle), a `free` is performed after the reversed list, and a measurement, bus store or
  effect entry flushes the gates recorded before it forward and is performed in place. So not
  every call can be recorded; NORTH_STAR does not say which are.
  **Outcome 2026-11-17:** `cq_invert` follows the ABI (ABI:640-653); the ABI wins.
- **C4. Tape INPUT rows.** NORTH_STAR makes the INPUT rows that merge under a quantum predicate,
  length or stream a hard error; the ABI requires them (ABI:516-518) and CR:11 has no decline
  path. The INPUT row with a quantum buffer ADDRESS (ABI:515) is named neither as first-version
  work nor as a hard error. **Outcome 2026-11-17:** the ABI wins; the INPUT rows are in scope (issue kmgy.28).
- **C5. Quantum-address cap.** `docs/QRAM.md` makes more than the configured number of quantum
  address bits a hard error. The ABI has no such error: R is any superset the backend can prove
  (ABI:414-419). A deviation, like C4.
  **Outcome 2026-11-17:** the cap stays as our build-time resource limit; the decoder is fan-out.
- **C6. Section range.** NORTH_STAR's §2.2-§2.8 vs ABI:96's §2.2-§2.7 (D1).
- **C7. Width tokens for core gates.** NORTH_STAR says core gates take the fp tokens but does not
  list them, and the one width list (kmgy.2's X-macro) is integer only. Core rows marked INT+FP
  need a second list `f16 bf16 f32 f64 f80`. `f80` needs x86-64 (`long double` is x87); this
  machine is x86_64, but NORTH_STAR names no target while CR:68 fixes x86-64.
- **C8. kmgy.7.** Says "`cqrt_ry_<W>`, `rz_i1` and their controlled forms for a classical angle
  rail". `rz_i1` has no controlled form, and "k computed on the host" belongs to the classical
  `A` of `ry_qangle`/`rz_qangle` (ABI:304-305), not to `ry_<W>(h, θ)`, whose θ is a plain double.
