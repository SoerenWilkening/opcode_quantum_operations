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
`0707dfd69e88bb76572ce518a10447823e7a5f14`.

> **Amended 2026-10-04 (bd 13zl).** The installer no longer pins a CQ_lang
> commit. It runs CQ_lang's own `tools/backend_contract.py provider-manifest`
> against the checkout and requires the result to equal this repository's
> `backend-manifest.json`, whose ABI id is
> `sha256:e1cc2bbcfb92143c0da24d58d2d7589709b426bf40eeb6489f3f599c8ae20f0d`
> (CQ_lang `61999c4a` onward; regenerated at `4266c40d`). Any CQ_lang commit
> with that ABI id is accepted, and any other is refused before anything is
> built. This is a superset claim. CQ_lang requires 3,000 symbols (ABI
> `e1cc2bbc…`, `opcode_table.yaml` `7307995f…`), and libcqops provides 3,060
> from its pinned `opcode_table.yaml` `6245117d…` grid. The 60 extra are the
> `cq_template_frem_*` that CQ_lang `61999c4a` removed. See
> [the backend contract](BACKEND_CONTRACT.md) §1.1.
>
> **Amended again 2026-10-04 (bd n4ut).** The superset ended. The pinned
> `opcode_table.yaml` was re-vendored at `7307995f…` (CQ_lang `4266c40d`), and
> libcqops now provides exactly the 3,000 symbols CQ_lang requires: 3,000 =
> 3,000. The ABI id the installer checks is unchanged.

If `--cq-dir` is omitted, the
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
