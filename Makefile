# Convenience wrapper. The build itself is CMake — see CLAUDE.md, "Build & Test".
#
# Tests run under BOTH configurations, always: Debug is where the invariant
# checks and the sanitizers live, Release is what gets its gate counts pinned.
# A count pinned only in Debug is not pinned (Rule 17).

BUILD_DEBUG   ?= build-debug
BUILD_RELEASE ?= build-release

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
	ctest --test-dir $(BUILD_DEBUG) --output-on-failure

test-release: configure
	cmake --build $(BUILD_RELEASE)
	ctest --test-dir $(BUILD_RELEASE) --output-on-failure

test: lint test-debug test-release

clean:
	rm -rf $(BUILD_DEBUG) $(BUILD_RELEASE)
