# cmake/CqopsFreePairingPin.cmake — the R3 guard for the VENDORED ANALYSIS.
#
# `third_party/cq_free_pairing/free_pairing_check.py` is CQ_lang's free-pairing
# reduction, vendored at a pinned revision because PRD-v1.md §15 D15 §2 and
# `bd 06t` both instruct the implementer to PORT it rather than re-derive its
# parity, and Rule 1 requires the thing being ported to be on disk at a pinned
# commit. Its COMMIT file records the sha256 of the bytes that were read.
#
# A PIN NOTHING CHECKS IS A COMMENT. That is the whole reason this file exists:
# `third_party/bennett`'s pin has the golden loader's hard error behind it and
# `third_party/cq_lang`'s yaml has `CqopsLinkWitness.cmake`'s R3 arm behind it,
# and CLAUDE.md records that "the golden loader now hard-errors on a SHA
# mismatch precisely so it cannot happen quietly". The vendored analysis gets
# the same treatment, at CONFIGURE time so the failure is a hard error rather
# than a skipped test.
#
# THREE THINGS THIS GUARD IS DELIBERATELY NOT.
#
#  * It is NOT a check against CQ_lang. It compares the vendored bytes against
#    the sha the vendored COMMIT claims — both inside this repository. Reaching
#    out to /Users/.../CQ_lang would make the configure depend on a tree this
#    repo does not own and cannot pin, which is the failure the vendoring
#    exists to fix. Re-pinning is the `Refresh command` in COMMIT, run by a
#    human, deliberately.
#  * It does NOT build, run, import or test the vendored file. It is reading
#    material (COMMIT says so in as many words). This adds no dependency.
#  * It does NOT pattern-match the hash. `CqopsLinkWitness.cmake` records the
#    measured trap: CMake's regex has no `{n}` repetition, so the obvious
#    `[0-9a-f][0-9a-f]+` matches "a256" out of the word "sha256" itself and
#    reports a drift that is not there. Strip the label, then compare in full.

function(cqops_check_free_pairing_pin)
    set(_dir    ${CMAKE_SOURCE_DIR}/third_party/cq_free_pairing)
    set(_src    ${_dir}/free_pairing_check.py)
    set(_commit ${_dir}/COMMIT)

    if(NOT EXISTS ${_src})
        message(FATAL_ERROR
            "free-pairing pin: ${_src} is missing. PRD §15 D15 §2 requires the "
            "reduction to be PORTED from a pinned copy, not read from CQ_lang. "
            "Restore it with the Refresh command in ${_commit}.")
    endif()
    if(NOT EXISTS ${_commit})
        message(FATAL_ERROR "free-pairing pin: ${_commit} is missing")
    endif()

    file(SHA256 ${_src} _have)
    file(STRINGS ${_commit} _prov REGEX "^  sha256 :")
    if(NOT _prov)
        message(FATAL_ERROR
            "free-pairing pin: ${_commit} carries no `  sha256 :` provenance line")
    endif()
    list(LENGTH _prov _n_prov)
    if(NOT _n_prov EQUAL 1)
        # A second matching line means the COMMIT grew a second vendored file
        # without this guard growing a second arm — the two-shaped-manifest
        # hazard CqopsLinkWitness.cmake records, one directory over.
        message(FATAL_ERROR
            "free-pairing pin: ${_commit} carries ${_n_prov} sha256 lines; this "
            "guard checks exactly one file. Give the new artefact its own arm.")
    endif()
    string(REGEX REPLACE "^.*sha256[ \t]*:[ \t]*" "" _claim "${_prov}")
    string(STRIP "${_claim}" _claim)

    if(NOT _claim STREQUAL _have)
        message(FATAL_ERROR
            "free-pairing pin: DRIFT. ${_commit} claims sha256 ${_claim} but "
            "${_src} hashes to ${_have}. The vendored analysis and its record "
            "disagree, so nothing in this repository knows which bytes the "
            "certificate in shim/ was ported from. Re-pin with COMMIT's Refresh "
            "command and RE-READ the port — a re-pin can invalidate the "
            "transcription itself. Never edit either to make this green.")
    endif()

    # A re-pin must re-run this. Neither file is otherwise an input to anything.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_src} ${_commit})
endfunction()
