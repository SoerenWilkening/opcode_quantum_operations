# cmake/CqopsSymbolSets.cmake — the OUTWARD half of Step 23's link gate, run as
# a `cmake -P` script test. See cmake/CqopsLinkWitness.cmake for the inward half.
#
# WHY THIS EXISTS AND WHY DELETING IT AS REDUNDANT IS THE GREEN-SUITE-WRONG-
# LIBRARY CASE. The witness ENUMERATES the grid, so it is structurally blind in
# exactly one direction: a `cq_template_*` that libcqops defines and that is NOT
# in the grid links perfectly. Measured (bd remember link-gate-static-archive):
# adding one out-of-grid definition — `cq_template_lrint_f64_to_i64`, a real
# libm_table.yaml name — leaves the link GREEN, and at Step 24 the consequence
# is NOT a duplicate-symbol error but link-order-dependent SILENT SHADOWING:
# the same two archives in opposite orders both exit 0 and return 7 and -1.
#
# AND A COUNT IS NOT A SET. Renaming one definition keeps the archive at exactly
# 2479 defined `cq_template_*` symbols and at exactly 180 `cqrt_*` (171 until the
# 2026-09-10 re-vendor of the ABI at CQ_lang `170ede1` — `bd w9i` — which is
# 182 declared minus the two `cqrt_h*` PRD §15 D16 leaves undefined); only the
# set difference sees it. Both directions are asserted, and each failure NAMES
# the symbol rather than reporting a total.
#
# `nm -g` IS THE RIGHT TOOL HERE AND `nm -u` IS NOT. `-u` on a static archive
# reports undefineds per archive MEMBER, including symbols a sibling member
# defines, so it is red on a perfectly linkable library; `-g` with a definition
# type letter is a statement about what the archive DEFINES.
#
# Inputs: ARCHIVE, MANIFEST, ABI, NM.

if(NOT EXISTS "${ARCHIVE}")
    message(FATAL_ERROR "symbol sets: no archive at ${ARCHIVE}")
endif()

execute_process(COMMAND ${NM} -g ${ARCHIVE}
                OUTPUT_VARIABLE _nm ERROR_VARIABLE _nmerr RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "symbol sets: ${NM} -g failed: ${_nmerr}")
endif()

# A DEFINITION, not a reference: T/D/B/S/R and their local-case spellings. The
# leading underscore is Mach-O's and is absent on ELF, so it is optional here
# rather than assumed either way.
string(REPLACE "\n" ";" _lines "${_nm}")
set(_have_t "")
set(_have_c "")
foreach(_l IN LISTS _lines)
    if(_l MATCHES "^[0-9a-fA-F]+ +[TDBSRtdbsr] +_?(cq_template_[A-Za-z0-9_]+)$")
        list(APPEND _have_t "${CMAKE_MATCH_1}")
    elseif(_l MATCHES "^[0-9a-fA-F]+ +[TDBSRtdbsr] +_?(cqrt_[A-Za-z0-9_]+)$")
        list(APPEND _have_c "${CMAKE_MATCH_1}")
    endif()
endforeach()

file(STRINGS ${MANIFEST} _decls REGEX "^[A-Za-z_].*cq_template_")
set(_want_t "")
foreach(_d IN LISTS _decls)
    string(REGEX MATCH "cq_template_[A-Za-z0-9_]+" _n "${_d}")
    list(APPEND _want_t "${_n}")
endforeach()

file(STRINGS ${ABI} _cdecls REGEX "^[A-Za-z_].*cqrt_[a-z0-9_]+[ \t]*\\(")
set(_want_c "")
foreach(_d IN LISTS _cdecls)
    string(REGEX MATCH "cqrt_[a-z0-9_]+" _n "${_d}")
    if(NOT _n STREQUAL "cqrt_h" AND NOT _n STREQUAL "cqrt_h_controlled")
        list(APPEND _want_c "${_n}")
    endif()
endforeach()

function(cq_set_diff label have want)
    set(_a ${${have}})
    set(_b ${${want}})
    list(SORT _a)
    list(REMOVE_DUPLICATES _a)
    list(SORT _b)
    list(REMOVE_DUPLICATES _b)

    set(_extra ${_a})
    list(REMOVE_ITEM _extra ${_b})
    set(_missing ${_b})
    list(REMOVE_ITEM _missing ${_a})

    if(_extra)
        list(GET _extra 0 _one)
        list(LENGTH _extra _k)
        message(FATAL_ERROR
            "${label}: libcqops DEFINES ${_k} symbol(s) that are not in the "
            "frozen grid — first is `${_one}`. At Step 24 that is not a "
            "duplicate-symbol error, it is link-order-dependent silent "
            "shadowing: whichever archive comes first satisfies the reference.")
    endif()
    if(_missing)
        list(GET _missing 0 _one)
        list(LENGTH _missing _k)
        message(FATAL_ERROR
            "${label}: ${_k} symbol(s) of the frozen grid are NOT defined by "
            "libcqops — first is `${_one}`.")
    endif()
    list(LENGTH _a _n)
    message(STATUS "${label}: ${_n} symbols, set-identical both directions")
endfunction()

cq_set_diff("cq_template_*" _have_t _want_t)
cq_set_diff("cqrt_*"       _have_c _want_c)
