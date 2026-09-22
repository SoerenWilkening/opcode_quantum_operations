# Installing libcqops as CQ's backend

Backend authors should read [the backend implementation
contract](BACKEND_CONTRACT.md). It separates CQ's full static-backend ABI from
libcqops' smaller logical-gate sink API and the concrete QEC adapter interface.

`libcqops` installs as the CMake package `CQBackend`. Its canonical imported
target is `CQBackend::backend`; the existing build-tree alias remains
`cqops::cqops`.

The supported non-interactive installation path is:

```sh
python3 tools/install_cqops.py \
  --prefix /absolute/install/prefix \
  --build-type Release \
  --cq-dir /path/to/CQ_lang
```

The CQ checkout must be at commit
`0707dfd69e88bb76572ce518a10447823e7a5f14`. If `--cq-dir` is omitted, the
installer uses `CQ_LANG_DIR`, then a sibling checkout named `CQ_lang`. It
configures and builds libcqops, runs every local test plus CQ's L7 integration,
the installed-package consumer, and the backend-contract check, installs it,
and validates the installed archive again. The exhaustive, opt-in L6 fixture
audit remains a separate integration gate as documented in `CLAUDE.md`.

To include the optional error-correction sink, point at the QEC repository's
already-built `qec/` directory:

```sh
python3 tools/install_cqops.py \
  --prefix /absolute/install/prefix \
  --build-type Release \
  --cq-dir /path/to/CQ_lang \
  --qec-dir /path/to/C_quantum_error_correction/qec \
  --qec-build build
```

The QEC build must provide `include/qec/qec.h` and `libqec.a`, `libtommath.a`,
and `libcjson.a` under the named QEC build directory. The installer and CMake
configuration both stop if any part is missing. The installed target records
these dependencies and their required include path in static-link order. Those
paths currently refer to the QEC build tree, so that tree must remain available;
copying only the libcqops prefix is not a self-contained QEC deployment.

A CQ application consumes the packages without naming archives or arranging
their order:

```cmake
find_package(CQ CONFIG REQUIRED)
find_package(CQBackend CONFIG REQUIRED)

add_executable(application lowered-program.c)
target_link_libraries(application PRIVATE CQBackend::backend)
```

Set `CMAKE_PREFIX_PATH` to the CQ and libcqops installation prefixes if they
are not under a standard prefix. Do not additionally link `CQ::trace_backend`,
`libcq_templates.a`, or `libcq_runtime.a`; those archives define overlapping
ABI symbols and can silently select trace implementations.

The package installs:

- `lib/libcqops.a`;
- `include/cqops/cqops.h`;
- `lib/cmake/CQBackend/CQBackendConfig.cmake`;
- `lib/cmake/CQBackend/CQBackendConfigVersion.cmake`;
- `lib/cmake/CQBackend/CQBackendTargets.cmake`; and
- `lib/cmake/CQBackend/backend-manifest.json`.
