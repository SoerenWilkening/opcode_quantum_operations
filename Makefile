# Convenience wrapper. The build itself is CMake — see CLAUDE.md, "Build & Test".
#
# Tests run under BOTH configurations, always: Debug is where the invariant
# checks and the sanitizers live, Release is what gets its gate counts pinned.
# A count pinned only in Debug is not pinned (Rule 17).

BUILD_DEBUG   ?= build-debug
BUILD_RELEASE ?= build-release

# ctest is SERIAL by default and nobody had tried otherwise until 2026-08-16.
# Measured on this box: Debug 97.6s serial -> 55.0s at -j12, bounded below by
# the longest single binary (test_kernel_cmp). Nothing here shares state
# between test binaries — each builds its own cq_ctx and its own pool, and the
# only files written are per-kernel goldens under an update run — so the
# parallel run is not a weaker run. Override with `make test CTEST_JOBS=1` if
# you are chasing a timing-sensitive failure.
CTEST_JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

.PHONY: all lint configure build test test-debug test-release clean

all: test

lint:
	@tools/check_loc.sh

configure:
	cmake -S . -B $(BUILD_DEBUG)   -DCMAKE_BUILD_TYPE=Debug
	cmake -S . -B $(BUILD_RELEASE) -DCMAKE_BUILD_TYPE=Release

build: configure
	cmake --build $(BUILD_DEBUG)
	cmake --build $(BUILD_RELEASE)

test-debug: configure
	cmake --build $(BUILD_DEBUG)
	ctest --test-dir $(BUILD_DEBUG) -j $(CTEST_JOBS) --output-on-failure

test-release: configure
	cmake --build $(BUILD_RELEASE)
	ctest --test-dir $(BUILD_RELEASE) -j $(CTEST_JOBS) --output-on-failure

test: lint test-debug test-release

clean:
	rm -rf $(BUILD_DEBUG) $(BUILD_RELEASE)
