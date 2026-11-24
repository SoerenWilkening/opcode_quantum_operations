# Completeness against the ABI

Audit of 2026-11-18 (issue `opcode_quantum_operations-kmgy.23`), at `rework` 8a89d67 plus the
work in progress of that day. Authority: CQ_lang `spec/BACKEND_ABI.md` and
`spec/CONSTRUCTION_RULES.md` at 87a5e30e (unchanged since docs/ABI_SYMBOLS.md was written; `ABI:n` is
a line there). Platform: macOS, x86_64, Apple clang 17, LLVM 19.1.7 for the IR programs.

## The tests

| test | what it checks |
|---|---|
| `abi_symbols` (`tests/abi_symbols.sh`) | nm on `libcq_backend.a` against the list `tests/abi_expected.c` writes from the ABI's rules (each family cites its ABI line; widths from `CQ_WIDTHS` and `CQ_FP_WIDTHS` only, cast pairs and bswap widths derived there, not taken from the implementation's macros): every expected symbol defined; no other public `cqrt_*`/`cq_template_*`; no out-of-scope or retired name (fp templates and casts incl. fneg/fabs/copysign, `_rm`/`_rmq`/`_exc`, clones, the clone table, `__cq_alloc`, `__cq_mem`, `__cq_sig`, the exact CQ.h names, ABI:134-139's retired list); no `__quantum__*` defined, only the seven referenced, all seven referenced; every `cq*` symbol the archive references is defined by it (the link-order property); every expected symbol referenced by some test object. Prints the counts below and the internal globals nothing else references. Shown to fail on a missing, an extra and an out-of-scope name. |
| `abi_link_cq_gates_replay`, `abi_link_cq_abi_count_gates` | `tests/abi_link.ll`, IR calling only ABI symbols (alloc, a template with a literal and its `_unc`, `icmp`, `and_i1`, `ry`, `rz_i1`, a bus store and load, a tape write and a performed effect entry, an invert region, push/pop, measure, free), compiled by LLVM 19.1.7 and linked `abi_link.o libcq_backend.a <seven operations>`: the replay archive, then `tests/abi_link_gates.c`, an independent bit-and-count definition. Exit 0 only if every measured value is right. Skipped with a message without clang. |
| `abi_classical` (`tests/test_abi_classical.c`) | one process: every template family (incl. a literal shape) with its `_unc` and free at all 15 widths, bswap at the multiples of 16, all 315 casts, and every core family (alloc, copy, xorc and `_controlled`, measure, all 16 bus forms at a classical address and a classical address rail, tape write/`_controlled`/`_lit`, a skipped effect) at all 20 width tokens, an invert region, push/pop, a classical-0 abort flag. Asserts 0 gates, 0 qubits touched, 0 qubits taken. |

## Done when

| # | NORTH_STAR item | verified by | not verified |
|---|---|---|---|
| 1 | an ABI-only program links against `libcq_backend.a` plus any definition of the seven operations (one archive replaces the ABI's two, user decision 2026-11-18) | `abi_link_*` (two gate definitions), `abi_symbols` (order property by nm), `templates_layering` (no non-template object references `cq_template_*`), `ir_abi_*` (the calling convention of every core entry point and the literal-carrying templates) | Apple's ld does not enforce archive order, so the one-pass link is only checked by nm; GNU ld untested (kmgy.40). The shared-library form (ABI:111-114) is not built. |
| 2 | every in-scope symbol at every listed width passes its sampled test, forced-quantum and all-classical | all 4532 defined; all 4532 referenced by a sampled test object (`abi_symbols`); the per-family tests run the harness forced-quantum and all-classical | that a reference is a sampled call is not proven per symbol (nm shows a reference, not a call path). Samples are reduced at wide widths (as few as 2 for the barrel shifts and 6 for division at the widest; 200 per cast pair; 100 in test_invert at i200). The quantum-address bus is sampled at i8 i16 i17 i64 only (test_qram), every other width at a classical address. Rotations: the replay backend ignores angles, so rotation controls and the ladder's rotation list are not checked by replay (the ladder's K register is, test_ladder). |
| 3 | all-classical programs emit no gate | `abi_classical`; each sampled test also asserts it per call | the exceptions below, which the ABI requires |
| 4 | quantum-address bus access passes replay against a host byte array at depth O(q) | `qram` (load, store, swap and `_controlled`, q = 1..6, depth formula and linear growth) | widths other than i8 i16 i17 i64; q above 6 (Q, environment `CQ_MAX_QADDR_BITS`, default 22) |
| 5 | every integer operation has its depth table, the shallowest shipped | `depth` (docs/DEPTH.md and kernel_choice.c fresh); every template operation has a section | see "Depth tables" |

Calls that emit gates on classical data, as the ABI requires: `cqrt_ry_<W>[_controlled]`,
`cqrt_ry_<W>_qangle[_controlled]`, `cqrt_rz_i1`, `cqrt_rz_qangle[_controlled]`: rotations are gates
(ABI:161-164), "a classical-valued h gets its qubits here" (ABI:258-259), a classical angle rail
appends Ry(P k/2^N) (ABI:304-305).

## Symbol counts (`abi_symbols`, 2026-11-18)

| group | family | expected | defined | tested |
|---|---|---|---|---|
| runtime | core (alloc, measure, copy, xorc, free, i1 logic, push/pop) | 127 | 127 | 127 |
| runtime | rot (ry, ry_controlled, rz_i1) | 41 | 41 | 41 |
| runtime | ladder (ry_qangle, rz_qangle) | 42 | 42 | 42 |
| runtime | bus | 323 | 323 | 323 |
| runtime | tape and abort | 85 | 85 | 85 |
| runtime | invert | 2 | 2 | 2 |
| templates | arith (add, sub) | 150 | 150 | 150 |
| templates | mul | 60 | 60 | 60 |
| templates | div | 360 | 360 | 360 |
| templates | bitwise | 180 | 180 | 180 |
| templates | shift (shl lshr ashr fshl fshr) | 690 | 690 | 690 |
| templates | icmp | 600 | 600 | 600 |
| templates | select | 120 | 120 | 120 |
| templates | overflow | 420 | 420 | 420 |
| templates | sat | 300 | 300 | 300 |
| templates | minmax (smax smin umax umin abs) | 270 | 270 | 270 |
| templates | count (ctlz cttz ctpop) | 90 | 90 | 90 |
| templates | bits (bswap, bitreverse) | 42 | 42 | 42 |
| templates | cast | 630 | 630 | 630 |
| | total | 4532 | 4532 | 4532 |

No symbol is missing, none extra, none out of scope; no `__quantum__*` defined; all seven referenced.

## Shortfalls

- **Tape rows ruled 2026-11-18** (kmgy.36, closed; line numbers from the ABI working tree of
  that day). Built: `pread`'s quantum offset (ABI:536), `getdelim`'s quantum delimiter
  (ABI:537), an INPUT call that reads a QUANTUM cell, the `scanf` format (ABI:538), and a deferred
  format or `va_list` with QUANTUM cells or a format at an address rail (ABI:613-616). Not ruled,
  no abort: `getline`/`getdelim` with `*lineptr` not NULL gets a fresh host buffer as if it were
  NULL. Not ruled, aborts naming kmgy.43: a deferred v-form's `va_list` pointer that is a quantum
  rail (Capture names only a format at an address rail).
- **Permitted backend limits, failing loudly** (ABI:590-594, the §1.11 Limits bullet, and
  ABI:434-435 for the bus). Each aborts with its own message and has a forked-child test:
  1. `ungetc`/`ungetwc` with a quantum character (test_tape);
  2. a quantum format or `va_list` pointer on an INPUT call (test_tape, one each);
  3. positional `%n$` conversions in a format the backend parses: format capture and the scanf
     rows (test_tape, one each);
  4. a host call with more than 16 stack argument slots (test_tape);
  5. more than Q (environment `CQ_MAX_QADDR_BITS`, default 22) quantum bits in a rail an INPUT row
     enumerates (stream, `pread` offset, `getdelim` delimiter) or a capture enumerates (test_tape);
  6. more than 2^Q cells in the registered objects: the registration aborts, src/qram.c (test_qram,
     test_qram_obj). A bus access has no cap of its own; Q above 24 is refused.
- **Namespace** (kmgy.41): about 190 internal globals named `cq_*`, the prefix CR §1.1 gives to
  CQ.h; four could be static (`cq_angle_window`, `cq_clz`, `cq_count_width`, `cq_popcount`).
- Stale comments in include/cqrt.h say quantum angle rails, quantum addresses and INPUT rows
  abort "until kmgy.18/21/28"; all three are implemented.

## Known limits recorded by earlier work

- Linux untested (kmgy.40); x86-64 only: the host call in src/tape.c is the SysV convention, and
  src/hostmem.c has Mach and Linux paths only (`#error` otherwise). The ABI's target is x86-64 (CR:68).
- The default of Q, `CQ_MAX_QADDR_BITS` = 22, is provisional (CMakeLists.txt).
- Division is the deepest family; a shallower wide-width candidate is open (kmgy.34). Test time:
  kmgy.35. `select` emits bit by bit when its inputs overlap: kmgy.37.
- Sample counts reduced at wide widths (Done-when 2 above).
- The replay backend cannot verify rotation controls or angles beyond the logged list.

## Depth tables (Done-when 5), information

Every template operation has a docs/DEPTH.md section: add sub mul udiv urem sdiv srem, and or xor,
shl lshr ashr fshl fshr, the ten icmp, select, the six overflow and four saturating ops, smax smin
umax umin abs, ctpop ctlz cttz, bswap bitreverse, zext sext trunc. Single-candidate sections:
`bswap`, `bitreverse`, `zext`, `trunc`, `and` (CX wiring or one CCX per bit: depth 1, optimal),
`xor` (depth 2: two terms per result bit with no ancilla) and `or` (depth 3; the De Morgan form,
also depth 3, is noted and not registered). No shallower construction is apparent for any of
them: each result bit takes one (wiring, `and`), two (`xor`) or three (`or`) gates onto it, and
scratch adds layers rather than removing them. Information, not a failure.
