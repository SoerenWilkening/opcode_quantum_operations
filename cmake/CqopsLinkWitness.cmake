# cmake/CqopsLinkWitness.cmake — Step 23's LINK GATE (bd 216 (E) step 8).
#
# WHAT A LINK GATE HAS TO BE, AND THE FOUR MEASURED WAYS THE OBVIOUS ONE IS
# VACUOUS (bd remember link-gate-static-archive):
#
#  0. `nm -u` ON A STATIC ARCHIVE IS NOT A LINK CHECK. BSD `nm -u` reports
#     undefineds PER ARCHIVE MEMBER, including symbols a sibling member defines,
#     so essentially every non-libc undefined is self-satisfied. Measured on this
#     tree, `nm -u build-release/libcqops.a | grep cq_template` returns only the
#     twelve `file.o:` header lines. The instrument has to be a real LINK.
#
#  1. NO ROW-COUNT ASSERTION. An empty external table compiles clean under this
#     project's exact flags. The partial-parse hazard is concrete: the manifest
#     is TWO-SHAPED — 1830 `int32_t` and 649 `void` — so a generator written from
#     the top of the file emits 1830 rows, links green, and witnesses no `_unc`
#     symbol at all. Hence the `_Static_assert`s below, on BOTH tables.
#
#  2. EXTERNAL LINKAGE IS NECESSARY AND NOT SUFFICIENT — the LINKER can still
#     strip the table, and CQ_lang's own `cq_link_smoke.c` has this hole.
#     Measured with one definition missing: plain link FAILS (teeth);
#     `-Wl,-dead_strip` SUCCEEDS (vacuous); plain `-flto` SUCCEEDS (vacuous);
#     `-flto -Wl,-dead_strip` FAILS. The `-flto` row is the surprising one and
#     testing the two flags together would have hidden it. With a RUNTIME-INDEXED
#     read the witness is RED in all four, which is why that is what is built
#     here rather than a flag policy.
#
#  3. THE nm ARM IS NOT IMPLIED BY THE LINK. The witness enumerates the grid, so
#     it is structurally blind to the OUTWARD direction: a `cq_template_*` we
#     define that is NOT in the grid links green, and at Step 24 that is not a
#     duplicate-symbol error but link-order-dependent SILENT SHADOWING (measured:
#     same two archives, opposite order, both exit 0, answers 7 and -1). And a
#     COUNT IS NOT A SET: renaming one definition keeps the archive at exactly
#     2479 defined `cq_template_*`. `CqopsSymbolSets.cmake` is that arm.
#
#  4. THE GENERATOR CAN SILENTLY NOT EXIST. Every other generator this repo owns
#     is PyYAML-gated and `cmake/CqopsPython.cmake` only WARNS when PyYAML is
#     absent — lose it and you lose the name-set oracle, the R3 sha guard and the
#     link gate in one step. So this is PURE CMake, and the sha guard rides
#     inside it.
#
# WHAT THE WITNESS DOES NOT PROVE: NAME resolution only. C has no mangling, so a
# transposed signature resolves cleanly. Signatures are Step 22's `-include`-the-
# ABI compile check. Never read "the full grid links" as "the full grid is right".
#
# AND ITS SCOPE IS THE GRID `opcode_table.yaml` SPELLS — WHICH IS NOT EVERY
# `cq_template_*` THE CORPUS CALLS (`bd c2o`, closed 2026-08-27). CQ_lang emits a
# THIRD arity shape, `cq_template_*_qql` — `cq_template_fma_f{32,64,80}_qql` and
# the purely-integer `cq_template_fshl_i32_qql[_unc]` — declared in
# `runtime/cq_intrinsic_templates.h`, a different header, and absent from the
# yaml. **libcqops leaves those symbols UNDEFINED and that is CORRECT, not a
# gap**: they are DEFINED in CQ_lang's own `runtime/cq_intrinsic_templates.c` and
# ship in its `cq_templates` archive, exactly like the 401 intrinsic and libm
# symbols PRD §1 calls deliberately not ours. Measured at Step 24 (2026-08-27,
# CQ_lang `134e625`): those two objects define exactly 401 symbols and their
# intersection with this archive's 2850 is EMPTY. So the gate reads "no undefined
# `cq_template_*` FROM THE OPCODE GRID" and never the unqualified form.
#
# TWO DEVIATIONS FROM bd 216's STEP 8 AS WRITTEN, both measured rather than
# chosen:
#
#  * "table 2 over the 180 `cqrt_*` from docs/cqrt_census.txt" IS NOT SATISFIABLE
#    AS WRITTEN. Measured: the census contains 98 distinct `cqrt_*` TOKENS, most
#    of them prefixes, plus a verbatim list of the SIXTY-FIVE v1 symbols — it
#    never spells the 173 expanded names, which is exactly what its Part C says
#    it is about ("where names are actually built"). The source used instead is
#    `shim/cq_runtime_abi.h`, and the independence the census was named for is
#    not lost: `tests/test_runtime_abi.inc` already checks that header's name set
#    against the census BOTH WAYS, so the chain is census -> abi.h -> witness with
#    a test at the first arrow.
#  * The generation is at CONFIGURE time rather than a `-P` script at build time,
#    which is what makes the sha mismatch a hard CONFIGURE error. The manifest,
#    the yaml and the ABI header are added to CMAKE_CONFIGURE_DEPENDS so a re-pin
#    re-runs it.

function(cqops_add_link_witness)
    set(_manifest ${CMAKE_SOURCE_DIR}/tests/abi/cq_templates_abi.txt)
    set(_yaml     ${CMAKE_SOURCE_DIR}/third_party/cq_lang/opcode_table.yaml)
    set(_abi      ${CMAKE_SOURCE_DIR}/shim/cq_runtime_abi.h)

    # --- R3, INSIDE THE GENERATOR. The manifest's provenance line must equal the
    # sha256 of the yaml we actually pin. CQ_lang is UNPINNED and its HEAD has
    # already moved past third_party/cq_lang/COMMIT while the yaml has not, so
    # the YAML SHA — not the checkout — is what proves this manifest is the
    # expansion of the grid we ship. Re-pinning turns this red LOUDLY instead of
    # silently witnessing the new grid against the old ABI.
    file(SHA256 ${_yaml} _yaml_sha)
    file(STRINGS ${_manifest} _prov REGEX "yaml-sha256")
    if(NOT _prov)
        message(FATAL_ERROR "link witness: ${_manifest} carries no yaml-sha256 provenance line")
    endif()
    # STRIP THE LABEL, DO NOT PATTERN-MATCH THE HASH. CMake's regex has no
    # `{n}` repetition, so the obvious `[0-9a-f][0-9a-f]+` matches "a256" out of
    # the word "yaml-sha256" itself and reports a drift that is not there —
    # observed on the first configure, which is this arm firing for the wrong
    # reason and is worth keeping as a comment rather than as a memory.
    string(REGEX REPLACE "^.*yaml-sha256[^:]*:[ \t]*" "" _claim "${_prov}")
    string(STRIP "${_claim}" _claim)
    if(NOT _claim STREQUAL _yaml_sha)
        message(FATAL_ERROR
            "link witness: R3 DRIFT. ${_manifest} claims yaml-sha256 ${_claim} "
            "but third_party/cq_lang/opcode_table.yaml hashes to ${_yaml_sha}. "
            "Re-pin the yaml FIRST, then re-extract the manifest from CQ_lang's "
            "regenerated header. Never edit either to make this green.")
    endif()

    # --- Table 1: the 2479 grid symbols, DECLARED FROM THE MANIFEST'S OWN BYTES.
    # Emitting CQ_lang's declarations verbatim rather than re-deriving them is
    # what keeps this an independent oracle: a re-expansion here would share
    # every transcription slip shim/gen_shim.py made.
    file(STRINGS ${_manifest} _decls REGEX "^[A-Za-z_].*cq_template_")
    set(_t_decls "")
    set(_t_rows  "")
    set(_t_n 0)
    foreach(_d IN LISTS _decls)
        string(REGEX MATCH "cq_template_[A-Za-z0-9_]+" _n "${_d}")
        string(APPEND _t_decls "${_d}\n")
        string(APPEND _t_rows  "    (cq_link_fn)&${_n},\n")
        math(EXPR _t_n "${_t_n} + 1")
    endforeach()

    # --- Table 2: the cqrt_* the LIBRARY defines. `cqrt_h` and
    # `cqrt_h_controlled` are excluded BY NAME because PRD §15 D16 leaves them
    # deliberately undefined — Rule 4 means libcqops could not serve an `H` at
    # any point, nothing references them, and listing them here would turn D16's
    # decision into a link failure. That is 182 - 2 = 180, which is D16's
    # arithmetic checked rather than restated. (171 until the 2026-09-10
    # re-vendor at CQ_lang `170ede1` widened the ABI to 182 — `bd w9i`. THIS IS
    # A FIFTH POPULATION PIN AND THE BEAD'S RECIPE NAMES ONLY FOUR: it is
    # generated, so it goes red at BUILD time rather than at test time, and it
    # is the only one whose failure names neither the count's owner nor a test.)
    file(STRINGS ${_abi} _cdecls REGEX "^[A-Za-z_].*cqrt_[a-z0-9_]+[ \t]*\\(")
    set(_c_rows "")
    set(_c_n 0)
    foreach(_d IN LISTS _cdecls)
        string(REGEX MATCH "cqrt_[a-z0-9_]+" _n "${_d}")
        if(NOT _n STREQUAL "cqrt_h" AND NOT _n STREQUAL "cqrt_h_controlled")
            string(APPEND _c_rows "    (cq_link_fn)&${_n},\n")
            math(EXPR _c_n "${_c_n} + 1")
        endif()
    endforeach()

    set(_out ${CMAKE_CURRENT_BINARY_DIR}/test_link_smoke.gen.c)
    file(WRITE ${_out}
"/* AUTOGENERATED by cmake/CqopsLinkWitness.cmake — do not edit.
 * Source: tests/abi/cq_templates_abi.txt (${_t_n} rows) and
 *         shim/cq_runtime_abi.h minus PRD 15 D16's two cqrt_h* (${_c_n} rows).
 * yaml-sha256 verified against third_party/cq_lang/opcode_table.yaml at
 * configure time.
 *
 * NAMED *.gen.c AND PLACED UNDER THE BUILD DIRECTORY for two independent
 * reasons: tools/check_loc.sh selects by EXTENSION and exempts *.gen.c, and it
 * only walks the source roots. Measured: the same bytes as
 * tests/test_link_smoke.c FAIL lint at ~5000 lines.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include \"cq_runtime_abi.h\"

typedef void (*cq_link_fn)(void);

${_t_decls}
/* EXTERNAL LINKAGE, NOT static: the compiler MUST emit a definition for an
 * external-linkage object, so the initialiser's relocations reach the object
 * file. Measured against a static archive with one definition missing: an
 * external table fails the link at -O0 and -O2; a `static` one exits 0 at both.
 */
cq_link_fn const cq_link_smoke_templates[] = {
${_t_rows}};

cq_link_fn const cq_link_smoke_cqrt[] = {
${_c_rows}};

#define CQ_LINK_T_ROWS ${_t_n}
#define CQ_LINK_C_ROWS ${_c_n}

_Static_assert(sizeof cq_link_smoke_templates / sizeof *cq_link_smoke_templates
               == CQ_LINK_T_ROWS,
               \"the whole opcode grid, not a prefix of it\");
_Static_assert(sizeof cq_link_smoke_cqrt / sizeof *cq_link_smoke_cqrt
               == CQ_LINK_C_ROWS,
               \"every cqrt_* libcqops defines\");
_Static_assert(CQ_LINK_T_ROWS == 2479, \"PRD 1 and 15 D14: 992 + 603 + 884\");
_Static_assert(CQ_LINK_C_ROWS == 180,  \"PRD 15 D16: 182 minus the two cqrt_h*\");

/* THE READ IS INDEXED BY A RUNTIME VALUE, and that is the whole difference
 * between a witness with teeth and one that is green four ways. With a constant
 * index — or with no read at all — `-Wl,-dead_strip` strips the table and plain
 * `-flto` folds it, and BOTH exit 0 with a definition missing. `argc` is not
 * knowable at compile time, so neither can. Neither flag is in this project's
 * flag set today, which makes the hazard LATENT rather than live — and they are
 * the first two flags anyone reaches for on a 2479-row object. */
int main(int argc, char **argv)
{
    const size_t i = (size_t)(argc - 1) % (size_t)CQ_LINK_T_ROWS;
    const size_t j = (size_t)(argc - 1) % (size_t)CQ_LINK_C_ROWS;

    (void)argv;
    printf(\"cq_link_smoke: %d template rows, %d cqrt rows, probe %llu\\n\",
           CQ_LINK_T_ROWS, CQ_LINK_C_ROWS,
           (unsigned long long)(uintptr_t)cq_link_smoke_templates[i]
           + (unsigned long long)(uintptr_t)cq_link_smoke_cqrt[j]);
    return 0;
}
")

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 ${_manifest} ${_yaml} ${_abi})

    # A TARGET IN `all`, NOT ONLY A REGISTERED TEST. A test can be skipped; a
    # target that fails to build cannot. `add_cqops_test` cannot be used —
    # cmake/CqopsTest.cmake hard-codes the source path under tests/ — so this is
    # a bare add_executable + add_test, on the precedent already in this file.
    add_executable(test_link_smoke ${_out})
    target_link_libraries(test_link_smoke PRIVATE cqops cqops_build_flags)
    target_include_directories(test_link_smoke PRIVATE ${CMAKE_SOURCE_DIR}/shim)
    add_test(NAME test_link_smoke COMMAND test_link_smoke)

    # THE OUTWARD ARM. See cmake/CqopsSymbolSets.cmake: the witness above is
    # structurally blind to a symbol we define that the grid does not name, and
    # a COUNT cannot see a rename. `nm` is found rather than assumed so a
    # toolchain without one fails loudly at configure instead of registering a
    # test that passes by not running.
    find_program(CQOPS_NM nm)
    if(NOT CQOPS_NM)
        message(FATAL_ERROR "link witness: no `nm` — the outward arm of the "
                            "link gate cannot be built, and registering it to "
                            "pass is exactly what bd 216 item 18 forbids")
    endif()
    add_test(NAME test_link_symbol_sets
             COMMAND ${CMAKE_COMMAND}
                     -DARCHIVE=$<TARGET_FILE:cqops>
                     -DMANIFEST=${_manifest}
                     -DABI=${_abi}
                     -DNM=${CQOPS_NM}
                     -P ${CMAKE_SOURCE_DIR}/cmake/CqopsSymbolSets.cmake)
endfunction()
