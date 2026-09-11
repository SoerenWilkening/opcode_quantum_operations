# Convenience wrapper. The build itself is CMake — see CLAUDE.md, "Build & Test".
#
# Tests run under BOTH configurations, always: Debug is where the invariant
# checks and the sanitizers live, Release is what gets its gate counts pinned.
# A count pinned only in Debug is not pinned (Rule 17).

BUILD_DEBUG   ?= build-debug
BUILD_RELEASE ?= build-release

# ctest is SERIAL by default and nobody had tried otherwise until 2026-08-16.
# Measured on this box: Debug 35.0s serial -> 25.4s at -j12, bounded below by
# the longest single binary (test_kernel_cmp, ~24s). Nothing here shares state
# between test binaries — each builds its own cq_ctx and its own pool, and the
# only files written are per-kernel goldens under an update run — so the
# parallel run is not a weaker run. Override with `make test CTEST_JOBS=1` if
# you are chasing a timing-sensitive failure.
CTEST_JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

# NINJA WHEN IT IS PRESENT, THE DEFAULT GENERATOR WHEN IT IS NOT — probed, not
# assumed, on cmake/CqopsSanitizers.cmake's precedent. Measured 2026-08-27 on
# this box, interleaved against Make because it spreads 4-5x (bd 97s):
#
#   no-op build (59 targets, nothing to do)   Make 2.2-7.1 s   Ninja 0.07 s
#   one shim .c touched -> relink 57 binaries Make 6.3-6.8 s   Ninja 2.7-3.4 s
#   a 5-mutant battery, both configurations   Make 105-128 s   Ninja 71-87 s
#
# The no-op figure is the one that matters for a mutation battery and for any
# edit-test loop: `make` walks the recursive target graph every time, and Ninja
# does not. It does NOT make the tests faster — `ctest` is unchanged, and 73 of
# the 78 tests a battery selects are single-case death binaries whose cost is
# process startup.
GENERATOR ?= $(shell command -v ninja >/dev/null 2>&1 && echo Ninja)
ifneq ($(GENERATOR),)
CMAKE_GEN := -G "$(GENERATOR)"
endif

# EXPLICIT -j, BECAUSE THE TWO GENERATORS DISAGREE ABOUT THE DEFAULT. Make
# builds serially unless told otherwise; Ninja defaults to CPUs+2, which
# oversubscribes this box's 6 PHYSICAL cores. Passing it keeps the reported
# timings comparable across a generator switch.
BUILD_JOBS ?= 6

.PHONY: all lint shim-check configure build test test-debug test-release clean \
        labreport labreport-data labreport-entry

all: test

lint:
	@tools/check_loc.sh
	@tools/check_cites.sh

# THE SHIM DRIFT GATE (bd kju). The shim is GENERATED from CQ_lang's own
# opcode_table.yaml — never hand-written, never forked — so the symbol grid
# cannot drift from the frozen ABI it must satisfy. `gen_shim.py --check` has
# existed since Step 22 and until now no target, no ctest entry and no workflow
# invoked it, so nothing enforced that.
#
# --check IS the check, not an approximation of one: it re-expands the pinned
# yaml in memory and compares the FULL TEXT of every file it would write against
# shim/generated/*.gen.c, then reports any *.gen.c on disk the grid no longer
# names. That is what a regenerate-into-a-scratch-tree plus `diff -r` reports,
# without the temporary tree — and without the hazard of a regeneration run with
# the wrong --output-dir writing over the tracked files. It writes NOTHING.
#
# IT HARD-FAILS RATHER THAN SKIPPING when the interpreter is missing, unlike the
# two PyYAML-gated ctest suites: cmake/CqopsPython.cmake declines to REGISTER
# those, which makes their absence a visible test-count drop next to a warning.
# A drift gate has no such tell — one that skips is one that is off — so both
# probes below exit non-zero.
PYTHON ?= python3

shim-check:
	@command -v $(PYTHON) >/dev/null 2>&1 || { \
	  echo "shim-check: no '$(PYTHON)' on PATH."; \
	  echo "shim-check: this gate HARD-FAILS rather than skipping — see the Makefile."; \
	  exit 1; }
	@$(PYTHON) -c 'import yaml' >/dev/null 2>&1 || { \
	  echo "shim-check: '$(PYTHON)' cannot import yaml — install PyYAML"; \
	  echo "shim-check: (Debian/Ubuntu: apt-get install python3-yaml; otherwise pip install PyYAML)"; \
	  exit 1; }
	@$(PYTHON) shim/gen_shim.py --check

# A BUILD DIRECTORY REMEMBERS ITS GENERATOR AND CMAKE REFUSES TO CHANGE IT, so
# `-G` on an existing tree of the other kind is a hard error telling you to
# delete it. That is the correct behaviour and this target does not paper over
# it: run `make clean` first when switching.
configure:
	cmake -S . -B $(BUILD_DEBUG)   $(CMAKE_GEN) -DCMAKE_BUILD_TYPE=Debug
	cmake -S . -B $(BUILD_RELEASE) $(CMAKE_GEN) -DCMAKE_BUILD_TYPE=Release

build: configure
	cmake --build $(BUILD_DEBUG)   -j $(BUILD_JOBS)
	cmake --build $(BUILD_RELEASE) -j $(BUILD_JOBS)

test-debug: configure
	cmake --build $(BUILD_DEBUG) -j $(BUILD_JOBS)
	ctest --test-dir $(BUILD_DEBUG) -j $(CTEST_JOBS) --output-on-failure

test-release: configure
	cmake --build $(BUILD_RELEASE) -j $(BUILD_JOBS)
	ctest --test-dir $(BUILD_RELEASE) -j $(CTEST_JOBS) --output-on-failure

test: lint shim-check test-debug test-release

clean:
	rm -rf $(BUILD_DEBUG) $(BUILD_RELEASE)

# ---- The lab report ------------------------------------------------------
#
# An APPEND-ONLY record of what each session did: docs/labreport/, one .tex per
# session, never edited after it lands. The .tex is the artefact and is tracked;
# the PDF is a build product and is gitignored.
#
# pdflatex is PROBED, NOT ASSUMED — the same rule this build already applies to
# ninja, to each sanitizer and to the Debug compiler. A box without TeX should
# be told what is missing, not handed an obscure failure, and must not have
# `make test` broken by a documentation target it cannot run.
LATEX ?= $(shell command -v pdflatex 2>/dev/null)
LABREPORT_DIR := docs/labreport

# Figure data comes out of the goldens on disk and out of probes RUN against the
# built archive. NEVER from a formula typed into a document: `bd j75` is what
# that costs. A missing input is a loud skip naming what is absent.
labreport-data:
	@python3 tools/labreport/gen_data.py || 	  echo "labreport: some figure data is stale — build build-release for the rest"

labreport: labreport-data
ifeq ($(LATEX),)
	@echo "labreport: pdflatex not found — install a TeX distribution"
	@echo "labreport: (macOS: brew install --cask mactex-no-gui; Debian: texlive-latex-recommended texlive-pictures)"
	@echo "labreport: the .tex sources under $(LABREPORT_DIR) are the artefact and are unaffected."
	@exit 1
else
	@cd $(LABREPORT_DIR) && 	  $(LATEX) -interaction=nonstopmode -halt-on-error labreport.tex >/dev/null && 	  $(LATEX) -interaction=nonstopmode -halt-on-error labreport.tex >/dev/null
	@echo "labreport: $(LABREPORT_DIR)/labreport.pdf"
endif

# Start the next entry. Writes the GENERATED header — SHA range, commits, beads,
# ctest counts, LOC, diffstat — and leaves the prose fields blank, because the
# only part worth reading is the part no script can supply. Refuses to overwrite
# an existing entry: a correction is a NEW entry carrying \supersedes.
labreport-entry:
	@python3 tools/labreport/new_entry.py $(if $(TITLE),--title "$(TITLE)")
