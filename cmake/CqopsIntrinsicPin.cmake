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
#
# AMENDED 2026-10-04 (bd n4ut): A THIRD ARM, `cqops_check_opcode_pin` at the
# foot of this file, over opcode_table.yaml and its new record COMMIT.opcode
# (label `opcode-sha256`, matched by neither arm above). The 2,880 quoted from
# §6.1 above is 2,820 since that re-vendor removed `frem`'s 60 symbols.

# THE MESSAGES NAME THE RECORD RATHER THAN SAYING "intrinsic" since bd n4ut
# (2026-10-04) added a third arm over a third record, COMMIT.opcode. Until then
# every message here began "intrinsic pin:" and named COMMIT.intrinsics.
function(_cqops_pin_one _label _src _commit)
    get_filename_component(_rec ${_commit} NAME)
    if(NOT EXISTS ${_src})
        message(FATAL_ERROR
            "yaml pin (${_rec}): ${_src} is missing. gen_shim.py expands it "
            "into the cq_template_* surface the shim satisfies (2,820 symbols "
            "since the bd n4ut re-vendor; 2,880 at PRD-v2 §6.1's vendoring), "
            "and without it the grid cannot be generated. Restore it with the "
            "Refresh command in ${_commit}.")
    endif()

    file(SHA256 ${_src} _have)
    file(STRINGS ${_commit} _prov REGEX "^  ${_label} :")
    if(NOT _prov)
        message(FATAL_ERROR
            "yaml pin (${_rec}): ${_commit} carries no `  ${_label} :` "
            "provenance line")
    endif()
    list(LENGTH _prov _n_prov)
    if(NOT _n_prov EQUAL 1)
        message(FATAL_ERROR
            "yaml pin (${_rec}): ${_commit} carries ${_n_prov} `${_label}` "
            "lines; this arm checks exactly one file. Give the new artefact "
            "its own arm.")
    endif()
    string(REGEX REPLACE "^.*${_label}[ \t]*:[ \t]*" "" _claim "${_prov}")
    string(STRIP "${_claim}" _claim)

    if(NOT _claim STREQUAL _have)
        message(FATAL_ERROR
            "yaml pin (${_rec}): DRIFT. ${_commit} claims ${_label} ${_claim} "
            "but ${_src} hashes to ${_have}. The vendored table and its record "
            "disagree, so nothing in this repository knows which ABI the "
            "generated shim and tests/abi/'s manifests were expanded from. "
            "Re-pin with ${_rec}'s Refresh command and RE-EXTRACT the "
            "matching tests/abi/ manifest from CQ_lang's regenerated header. "
            "Never edit either to make this green.")
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

# THE THIRD ARM, OVER A THIRD RECORD (bd n4ut, 2026-10-04). The maintainer
# ruled "Re-vendor, authorized" and opcode_table.yaml moved 6245117d… ->
# 7307995f… (CQ_lang 61999c4a removed `frem`). Its LIVE record is
# COMMIT.opcode, a new file beside COMMIT for COMMIT.intrinsics's reasons:
# Rule 1 forbids editing COMMIT, and COMMIT's a6a92fe / 6245117d… pin stays a
# true record of the bytes vendored 2026-08-14. COMMIT's own sha line was
# never checked here and must not be — it now describes bytes we no longer
# ship. Until this arm, the opcode yaml's sha was checked only by
# CqopsLinkWitness.cmake's R3 arm against tests/abi/cq_templates_abi.txt;
# that arm still runs, and catches a manifest that was not re-extracted,
# which this one cannot see.
function(cqops_check_opcode_pin)
    set(_dir    ${CMAKE_SOURCE_DIR}/third_party/cq_lang)
    set(_commit ${_dir}/COMMIT.opcode)

    if(NOT EXISTS ${_commit})
        message(FATAL_ERROR
            "opcode pin: ${_commit} is missing. It is the LIVE record of "
            "opcode_table.yaml since bd n4ut, a THIRD file beside COMMIT and "
            "COMMIT.intrinsics rather than an edit to COMMIT, because Rule 1 "
            "forbids writing to third_party/ and COMMIT's pin is history.")
    endif()

    _cqops_pin_one("opcode-sha256" ${_dir}/opcode_table.yaml ${_commit})
endfunction()
