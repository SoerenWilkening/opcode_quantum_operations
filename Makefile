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

.PHONY: all lint configure build test test-debug test-release clean

all: test

lint:
	@tools/check_loc.sh

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

test: lint test-debug test-release

clean:
	rm -rf $(BUILD_DEBUG) $(BUILD_RELEASE)
