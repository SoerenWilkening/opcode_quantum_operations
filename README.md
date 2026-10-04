# libcqops

`libcqops` is CQ's static C backend for reversible circuit construction. It
emits logical gates through a sink (`X`, `CX`, `CCX`, rotations, and
measurement); it does not store a circuit or run a statevector simulation.

## Requirements

- CMake and a C compiler;
- Python 3; and
- a CQ_lang checkout at the backend-contract commit
  `0707dfd69e88bb76572ce518a10447823e7a5f14`.
  **Amended 2026-10-04 (bd 13zl):** the requirement is now a CQ_lang checkout
  whose backend ABI id equals the one in [`backend-manifest.json`](backend-manifest.json),
  `sha256:e1cc2bbcfb92143c0da24d58d2d7589709b426bf40eeb6489f3f599c8ae20f0d`
  (CQ_lang `61999c4a` onward; regenerated at `4266c40d`), not one commit.
  `tools/install_cqops.py` checks that id through CQ_lang's own
  `tools/backend_contract.py`. The manifest is a superset claim. CQ_lang
  requires 3,000 symbols (ABI `e1cc2bbc…`, `opcode_table.yaml` `7307995f…`).
  libcqops provides 3,060 from its pinned `opcode_table.yaml` `6245117d…`
  grid, and the 60 extra are the `cq_template_frem_*` that CQ_lang `61999c4a`
  removed. See [`docs/BACKEND_CONTRACT.md`](docs/BACKEND_CONTRACT.md) §1.1.

The CQ checkout must already have a configured build directory (normally
`build/`). An optional QEC sink can be enabled with a built QEC checkout; see
[`docs/INSTALLING.md`](docs/INSTALLING.md) for its required libraries.

## Install

From this repository, run the installer with an absolute prefix and the CQ
checkout path:

```sh
python3 tools/install_cqops.py \
  --prefix /absolute/install/prefix \
  --build-type Release \
  --cq-dir /path/to/CQ_lang
```

The installer configures and builds libcqops, runs the local test suite and
package-consumer checks, installs the CMake package, and validates the
installed backend archive. If `--cq-dir` is omitted, it uses `CQ_LANG_DIR` or a
`CQ_lang` sibling directory.

To include the optional QEC sink:

```sh
python3 tools/install_cqops.py \
  --prefix /absolute/install/prefix \
  --build-type Release \
  --cq-dir /path/to/CQ_lang \
  --qec-dir /path/to/C_quantum_error_correction/qec \
  --qec-build build
```

For a manual build, configure the same CMake options, then build and install:

```sh
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/absolute/install/prefix \
  -DCQOPS_CQLANG_DIR=/path/to/CQ_lang
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
cmake --install build-release
```

## Use from CMake

Install CQ_lang and libcqops prefixes, then make both discoverable through
`CMAKE_PREFIX_PATH`:

```cmake
find_package(CQ CONFIG REQUIRED)
find_package(CQBackend CONFIG REQUIRED)

add_executable(application lowered-program.c)
target_link_libraries(application PRIVATE CQBackend::backend)
```

The package target supplies `libcqops.a`, its public headers, and the required
static-link dependencies. Do not additionally link CQ's trace archives
(`CQ::trace_backend`, `libcq_templates.a`, or `libcq_runtime.a`): they define
overlapping ABI symbols.

## Further documentation

- [Detailed installation and QEC instructions](docs/INSTALLING.md)
- [Backend implementation and ABI contract](docs/BACKEND_CONTRACT.md)
- [CQ package installation test](tests/package-consumer/)
