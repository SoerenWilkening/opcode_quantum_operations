# cmake/CqopsIntrinsicPin.cmake — the R3 guard for the VENDORED INTRINSIC and
# LIBM TABLES (PRD-v2 §6.1, bead 9ve.24).
#
# `third_party/cq_lang/intrinsic_table.yaml` and `libm_table.yaml` are CQ_lang's
# other two `cq_template_*` sources, vendored 2026-09-19 because §6.1 decided
# that "the complete `cq_template_*` link surface is 2,880 symbols and that is
# the surface a complete shim satisfies". Their COMMIT.intrinsics records the
# sha256 of the bytes that were read.
#
# A PIN NOTHING CHECKS IS A COMMENT — CqopsFreePairingPin.cmake's sentence, and
# this file is that file's shape applied twice. `opcode_table.yaml`'s pin has
# CqopsLinkWitness.cmake's R3 arm behind it and the vendored analysis has its
# own guard; these two get the same treatment, at CONFIGURE time so the failure
# is a hard error rather than a skipped test.
#
# TWO ARMS, AND THAT IS FORCED RATHER THAN TIDY. The template this is modelled
# on FATALs outright when its COMMIT file carries more than one `sha256` line,
# with the message "give the new artefact its own arm" — so a single-armed
# guard over a two-file record is the exact shape it refuses. The two labels in
# COMMIT.intrinsics are deliberately DISTINCT strings (`intrinsic-sha256` and
# `libm-sha256`) so each arm's anchored regex matches exactly one line, and the
# arm asserts that count rather than assuming it.
#
# AND THE ANCHOR IS TWO SPACES AND THE WHOLE LABEL, NOT `sha256`. That record
# also QUOTES CQ_lang's generated banners, which contain the string
# `yaml-sha256:` twice; a loose regex would match those, return three lines,
# and fail the count check on a correct tree. Measured while writing this file.
#
# THREE THINGS THIS GUARD IS DELIBERATELY NOT, inherited verbatim.
#
#  * It is NOT a check against CQ_lang. It compares the vendored bytes against
#    the sha the vendored record claims — both inside this repository. Reaching
#    out to a tree this repo does not own and cannot pin is the failure the
#    vendoring exists to fix. Re-pinning is COMMIT.intrinsics's `Refresh
#    command`, run by a human, deliberately.
#  * It does NOT parse, expand or otherwise read the tables' CONTENT. That is
#    shim/gen_shim.py's job and it is checked by the shim drift gate and by
#    tests/abi/'s two manifests. This adds no dependency and no PyYAML.
#  * It does NOT pattern-match the hash. CqopsLinkWitness.cmake records the
#    measured trap: CMake's regex has no `{n}` repetition, so the obvious
#    `[0-9a-f][0-9a-f]+` matches "a256" out of the word "sha256" itself and
#    reports a drift that is not there. Strip the label, then compare in full.

function(_cqops_pin_one _label _src _commit)
    if(NOT EXISTS ${_src})
        message(FATAL_ERROR
            "intrinsic pin: ${_src} is missing. PRD-v2 §6.1 vendors it so the "
            "shim can satisfy the whole 2,880-symbol cq_template_* surface; "
            "without it gen_shim.py cannot expand the grid it ships. Restore "
            "it with the Refresh command in ${_commit}.")
    endif()

    file(SHA256 ${_src} _have)
    file(STRINGS ${_commit} _prov REGEX "^  ${_label} :")
    if(NOT _prov)
        message(FATAL_ERROR
            "intrinsic pin: ${_commit} carries no `  ${_label} :` provenance "
            "line")
    endif()
    list(LENGTH _prov _n_prov)
    if(NOT _n_prov EQUAL 1)
        message(FATAL_ERROR
            "intrinsic pin: ${_commit} carries ${_n_prov} `${_label}` lines; "
            "this arm checks exactly one file. Give the new artefact its own "
            "arm.")
    endif()
    string(REGEX REPLACE "^.*${_label}[ \t]*:[ \t]*" "" _claim "${_prov}")
    string(STRIP "${_claim}" _claim)

    if(NOT _claim STREQUAL _have)
        message(FATAL_ERROR
            "intrinsic pin: DRIFT. ${_commit} claims ${_label} ${_claim} but "
            "${_src} hashes to ${_have}. The vendored table and its record "
            "disagree, so nothing in this repository knows which ABI the "
            "generated shim and tests/abi/'s manifests were expanded from. "
            "Re-pin with COMMIT.intrinsics's Refresh command and RE-EXTRACT "
            "both manifests from CQ_lang's regenerated headers. Never edit "
            "either to make this green.")
    endif()

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 ${_src} ${_commit})
endfunction()

function(cqops_check_intrinsic_pin)
    set(_dir    ${CMAKE_SOURCE_DIR}/third_party/cq_lang)
    set(_commit ${_dir}/COMMIT.intrinsics)

    if(NOT EXISTS ${_commit})
        message(FATAL_ERROR
            "intrinsic pin: ${_commit} is missing. It is a SECOND record beside "
            "COMMIT rather than an edit to it, because Rule 1 forbids writing "
            "to third_party/ and because the three artefacts pin at three "
            "different revisions.")
    endif()

    _cqops_pin_one("intrinsic-sha256" ${_dir}/intrinsic_table.yaml ${_commit})
    _cqops_pin_one("libm-sha256"      ${_dir}/libm_table.yaml      ${_commit})
endfunction()
