# Backend implementation contract

This manual describes how another codebase can provide a production backend for
CQ, and how a physical or error-correction engine can instead sit below
`libcqops`. These are different integration points. Choose one before writing
code.

| Integration point | Implementer supplies | Application links |
|---|---|---|
| CQ static backend | The complete `cqrt_*` and `cq_template_*` C ABI | The provider archive through `CQBackend::backend` |
| libcqops sink | Six gate callbacks: X, CX, CCX, Ry, Rz, and MZ | `libcqops.a` plus the sink implementation |
| Existing QEC adapter | The specific `qec/qec.h` API and its static archives | `libcqops.a`, then QEC and its dependencies |

The first is CQ's interchangeable-backend contract. The other two reuse
libcqops' reversible opcode implementations and replace only the destination of
its logical gate stream.

## 1. CQ static-backend contract

### 1.1 Source of truth

Build against one exact CQ revision or installed CQ release. For the contract
implemented by this repository, that revision is:

```text
0707dfd69e88bb76572ce518a10447823e7a5f14
```

(Amended 2026-10-04, bd 13zl: the contract this repository implements is now
named by its ABI id, not by that revision. See the amendment at the end of this
section.)

The authoritative inputs are supplied by that CQ tree or installation:

```text
include/cq/backend/cq_runtime.h
include/cq/backend/cq_templates.h
include/cq/backend/cq_intrinsic_templates.h
include/cq/backend/cq_libm_templates.h
share/cq/abi/opcode_table.yaml
share/cq/abi/intrinsic_table.yaml
share/cq/abi/libm_table.yaml
bin/cq-backend-contract
```

In a CQ source checkout, the headers are under `runtime/`, the tables and
contract tool are under `tools/`, and the command is
`tools/backend_contract.py`. Do not copy a symbol list or ABI hash into a new
backend by hand. Generate both from the matching CQ revision.

Inspect the current contract with either form:

```sh
/cq/prefix/bin/cq-backend-contract describe

python3 /path/to/CQ_lang/tools/backend_contract.py \
  --root /path/to/CQ_lang describe
```

At commit `0707dfd6`, the required surface is:

- 180 emit-capable `cqrt_*` functions;
- 2,880 `cq_template_*` functions;
- 3,060 required symbols in total.

`cqrt_h` and `cqrt_h_controlled` are declared but deliberately excluded because
the lowering pass cannot emit them. Additional provider-specific public symbols
are allowed.

> **Amended 2026-10-04 (bd 13zl): the contract moved, and libcqops implements
> it as a SUPERSET.** The values above are kept as the record of `0707dfd6`,
> where the ABI id was
> `sha256:f9debf7770db72aa35b65e5a9649f8d3be4475a0dcb99a680ae0fc96527a5ebd`.
>
> - CQ_lang `61999c4a` (2026-09-30, "Reject unsupported native frem and retire
>   trace templates") removed the 60 `cq_template_frem_*` declarations (`frem`
>   at f16, f32, f64 and f80, 15 variants each). Its `opcode_table.yaml` moved
>   from sha256 `6245117d…` to `7307995f…`.
> - From that commit on, `describe` reports 180 `cqrt_*` + 2,820
>   `cq_template_*` = **3,000 required symbols**, ABI id
>   `sha256:e1cc2bbcfb92143c0da24d58d2d7589709b426bf40eeb6489f3f599c8ae20f0d`.
>   `backend-manifest.json` was regenerated with `provider-manifest` at CQ_lang
>   `4266c40d`, the HEAD on 2026-10-04.
> - No `cqrt_*` or `cq_template_*` declaration changed between `61999c4a` and
>   `4266c40d`. Three later commits (`4b54f18a`, `27fc9a62`, `18b3f5ff`) touched
>   `runtime/cq_runtime.h` in comments only. `27fc9a62` is the normative QRAM
>   index text agreed with libcqops as bd b6a, and comments do not enter the
>   ABI id.
> - **libcqops provides 3,060:** its vendored grid stays at
>   `third_party/cq_lang/opcode_table.yaml` sha256 `6245117d…` (2,880
>   templates), so the 60 extra symbols it defines are exactly the
>   `cq_template_frem_*` that CQ_lang no longer declares. Extra symbols are
>   allowed (above), and `check` passes against the archive. Re-vendoring the
>   table (2,880 → 2,820) is a separate, tracked decision.
> - `tools/install_cqops.py` no longer requires CQ_lang HEAD to equal
>   `0707dfd6`. It requires CQ_lang's regenerated provider manifest to equal
>   `backend-manifest.json`, i.e. the same ABI id.

> **Amended again 2026-10-04 (bd n4ut): the superset ended, and 3,000 =
> 3,000.** The maintainer authorised the re-vendor that the amendment above
> left open. `third_party/cq_lang/opcode_table.yaml` was re-copied from
> CQ_lang `4266c40d` (sha256 `7307995f…`, the table `61999c4a` produced) and
> recorded in a new `third_party/cq_lang/COMMIT.opcode`. `COMMIT`'s
> `6245117d…` pin stays as history.
>
> - libcqops now defines 180 `cqrt_*` + 2,820 `cq_template_*` = **3,000**,
>   exactly the set CQ_lang requires. The 60 `cq_template_frem_*` it
>   provided as extras are gone. Every one of them was a loud abort, so no
>   capability was lost.
> - The ABI id is unchanged (`sha256:e1cc2bbc…`), because it is computed from
>   CQ_lang's inputs and those did not move. `backend-manifest.json` was
>   regenerated with `provider-manifest` at CQ_lang `ad2d4ec6`, whose
>   `runtime/` and `tools/` are identical to `4266c40d`'s, and came out
>   byte-identical.
> - The figures above stay as written: `0707dfd6`'s 2,880 templates and
>   3,060 required, and the superset period's 3,060 provided (bd 13zl to
>   bd n4ut, both 2026-10-04).

### 1.2 Binary requirements

A provider must:

1. Build a C11-compatible static archive.
2. Define every symbol reported in `required_symbols` by the matching contract
   descriptor.
3. Compile its ABI entry points against CQ's matching installed headers.
4. Use every prototype verbatim, including argument order, signedness, floating
   types, and return type.
5. Provide a manifest generated by CQ's contract tool.
6. Export all libraries and link options needed to consume the static archive.

The `int32_t` values in the ABI are opaque handle identifiers. They are not the
payload stored in a quantum register. The width encoded in a function name,
such as `i64` in `cq_template_add_i64`, describes the logical rail. Classical
operands remain ordinary C values with the exact type shown in the header.

A native linker normally checks only symbol names. A wrong function signature
can therefore link successfully and corrupt arguments at runtime. Including the
CQ headers in every ABI implementation translation unit is part of the
contract, not merely a convenience.

### 1.3 Semantic requirements

Passing `cq-backend-contract check` proves that symbols exist. It does not prove
that they implement the right circuit. A production provider must also preserve
CQ's semantic contract:

- A returned handle denotes a newly minted result rail or a documented resource
  token.
- A forward reversible operation preserves its inputs unless its header
  explicitly defines an in-place mutation.
- An `_unc` entry is the documented uncompute operation. Given the same live
  operands and the corresponding result, it must return the result and all
  internal workspace to `|0>` without damaging preserved inputs.
- An `_inv` or controlled entry follows the exact semantics documented for that
  family. Do not infer its meaning from the suffix alone.
- Controls are coherent quantum controls. They must not be measured or converted
  into classical branch decisions.
- Measurement occurs only through the measurement ABI and is terminal where CQ
  documents it as terminal.
- Every internal ancilla is either returned clean to `|0>` before the ABI call
  returns or retained under an explicitly documented ownership rule. A correct
  classical result with dirty scratch is a circuit miscompile.
- Array, tape, and other resource handles must remain distinct from quantum rail
  handles and obey the lifetimes documented in `cq_runtime.h`.

The comments in CQ's four ABI headers are the normative per-family description.
The generated YAML tables define the template surface, not the reversible
construction. Backend authors must provide their own reviewed constructions and
tests for those semantics. An aborting placeholder may satisfy the symbol
checker, but it is not a usable production implementation for any path CQ emits.

### 1.4 Build the archive against CQ's headers

A minimal provider can locate the installed ABI headers without linking CQ's
trace libraries:

```cmake
cmake_minimum_required(VERSION 3.20)
project(mybackend VERSION 1.0.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_path(CQ_BACKEND_ABI_INCLUDE_DIR
          NAMES cq_runtime.h cq_templates.h
          PATH_SUFFIXES include/cq/backend
          REQUIRED)

add_library(mybackend STATIC ${backend_sources})
target_compile_features(mybackend PRIVATE c_std_11)
target_include_directories(mybackend
    PRIVATE "${CQ_BACKEND_ABI_INCLUDE_DIR}"
    PUBLIC  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

# Use PUBLIC for every dependency needed when a consumer links the static
# archive. Preserve dependency order here.
target_link_libraries(mybackend PUBLIC m dependency_a dependency_b)
```

Include the relevant CQ headers from the ABI entry-point sources:

```c
#include <cq_runtime.h>
#include <cq_templates.h>
#include <cq_intrinsic_templates.h>
#include <cq_libm_templates.h>
```

Do not link `CQ::trace_backend`, `libcq_templates.a`, or `libcq_runtime.a` into
the provider or its tests. Those are CQ's trace implementation of the same ABI.

### 1.5 Generate the provider manifest

Generate the manifest from the same CQ revision used for the headers:

```sh
python3 /path/to/CQ_lang/tools/backend_contract.py \
  --root /path/to/CQ_lang \
  provider-manifest \
  --name mybackend \
  --output backend-manifest.json
```

For an installed CQ toolchain:

```sh
/cq/prefix/bin/cq-backend-contract \
  provider-manifest \
  --name mybackend \
  --output backend-manifest.json
```

Commit the generated manifest with the backend. Regenerate it whenever the CQ
ABI inputs change. Never type or copy `cq_backend_abi` manually.

### 1.6 Export the CMake package

The installed package name is `CQBackend`, and its canonical target is
`CQBackend::backend`:

```cmake
set_target_properties(mybackend PROPERTIES EXPORT_NAME backend)

set(MYBACKEND_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/CQBackend")
configure_package_config_file(
    cmake/CQBackendConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/CQBackendConfig.cmake"
    INSTALL_DESTINATION "${MYBACKEND_CMAKEDIR}")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/CQBackendConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)

install(TARGETS mybackend
        EXPORT CQBackendTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(EXPORT CQBackendTargets
        FILE CQBackendTargets.cmake
        NAMESPACE CQBackend::
        DESTINATION "${MYBACKEND_CMAKEDIR}")
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/CQBackendConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/CQBackendConfigVersion.cmake"
        backend-manifest.json
        DESTINATION "${MYBACKEND_CMAKEDIR}")
```

`cmake/CQBackendConfig.cmake.in` can be as small as:

```cmake
@PACKAGE_INIT@

include("${CMAKE_CURRENT_LIST_DIR}/CQBackendTargets.cmake")
set(CQBackend_PROVIDER_MANIFEST
    "${CMAKE_CURRENT_LIST_DIR}/backend-manifest.json")
check_required_components(CQBackend)
```

If a dependency exports its own CMake package, call `find_dependency()` from the
config file and link its imported target. If the exported backend target names
absolute external archive paths, those files must remain at those paths. To
make a backend prefix independently movable, install the dependencies with the
backend or depend on separately installed relocatable packages.

The target must encode static-library ordering. Consumers must never be asked to
repeat private archive names in a guessed order.

### 1.7 Validate the archive and installed package

Validate the built archive:

```sh
python3 /path/to/CQ_lang/tools/backend_contract.py \
  --root /path/to/CQ_lang \
  check \
  --archive build/libmybackend.a \
  --manifest backend-manifest.json
```

Repeat the check against the installed files:

```sh
/cq/prefix/bin/cq-backend-contract check \
  --archive /backend/prefix/lib/libmybackend.a \
  --manifest /backend/prefix/lib/cmake/CQBackend/backend-manifest.json
```

Then configure a separate consumer directory:

```cmake
cmake_minimum_required(VERSION 3.20)
project(backend_consumer C)

find_package(CQ CONFIG REQUIRED)
find_package(CQBackend CONFIG REQUIRED)

add_executable(application lowered-program.c)
target_link_libraries(application PRIVATE CQBackend::backend)
```

The consumer test must:

1. reference at least one real `cqrt_*` function so the linker extracts an
   archive member;
2. run successfully;
3. produce a linker map;
4. show the provider archive in that map;
5. fail if `libcq_templates.a` or `libcq_runtime.a` appears in that map.

The intended production link is:

```text
lowered-program.o libmybackend.a [provider dependencies]
```

This is invalid:

```text
lowered-program.o libmybackend.a libcq_templates.a libcq_runtime.a
```

With static archives, the invalid form can still link and silently select CQ's
trace bodies according to archive order.

### 1.8 Provider CI checklist

A backend is ready for use only after all of these pass in Debug and Release as
applicable:

- compile every ABI definition against the matching CQ headers;
- reversible forward/uncompute tests for every implemented family;
- clean-ancilla and ownership checks at every ABI boundary;
- controlled-operation tests with both control values and coherent controls;
- `cq-backend-contract check` on the installed archive and manifest;
- a separately configured installed-package consumer;
- a link-map assertion excluding CQ's trace archives;
- at least one real program lowered by CQ and linked only through
  `CQBackend::backend`;
- the CQ integration suites that exercise the backend's supported surface.

The contract tool checks ABI identity and symbol presence. These behavioral
tests are what establish that the symbols form a correct backend.

## 2. Using a different sink below libcqops

If the new codebase already accepts a logical gate stream, it usually should not
reimplement all 3,060 CQ ABI functions (3,000 since CQ_lang `61999c4a`;
amended 2026-10-04, bd 13zl, see §1.1; libcqops itself provides exactly those
3,000 since the bd n4ut re-vendor the same day). Reuse libcqops and provide a
`cq_sink`:

```c
#include <cqops/cqops.h>

static void emit_x(void *user, uint32_t q);
static void emit_cx(void *user, uint32_t control, uint32_t target);
static void emit_ccx(void *user, uint32_t a, uint32_t b, uint32_t target);
static void emit_ry(void *user, uint32_t q, double theta);
static void emit_rz(void *user, uint32_t q, double phi);
static void emit_mz(void *user, uint32_t q);

static cq_sink sink = {
    emit_x, emit_cx, emit_ccx, emit_ry, emit_rz, emit_mz, NULL
};

void install_my_sink(void *backend_context)
{
    sink.user = backend_context;
    cqops_set_sink(&sink);
}
```

The sink is borrowed and must outlive every CQ operation that can reach it.
Angles are raw radians in `double`. The sink owns conversion to its native angle
or synthesis representation. Callback failure cannot be returned through the
current `void` vtable, so a sink must stop loudly rather than silently omit a
gate.

This six-callback interface is the general logical-gate boundary. A sink needing
additional lifecycle or capacity setup should provide that setup in its own
library and perform it before the first lowered CQ operation. Call the explicit
installation function from the host or at the start of the CQ application,
before its first quantum intrinsic. Do not rely on an unreferenced constructor
inside a static archive: the linker may omit that archive member entirely.

## 3. Existing QEC adapter contract

The built-in QEC sink is a specific adapter, not the CQ static-backend ABI. A
QEC-enabled libcqops build currently expects this filesystem layout:

```text
<qec-dir>/include/qec/qec.h
<qec-dir>/<qec-build>/libqec.a
<qec-dir>/<qec-build>/libtommath.a
<qec-dir>/<qec-build>/libcjson.a
```

The archive order is load-bearing:

```text
libcqops.a libm libqec.a libtommath.a libcjson.a
```

The adapter uses `qec_create`, `qec_destroy`, `qec_n_logical`, `qec_set_trace`,
`qec_x`, `qec_cx`, `qec_ccx`, `qec_mz`, `qec_rz`, `qec_h`, `qec_s`, and
`qec_sdg`. Gate functions return zero on success and a nonzero result is fatal.
`qec_rz` accepts an exact rational multiple of pi and a synthesis precision.
libcqops constructs Ry as `Sdg; H; Rz; H; S` because that QEC API has no
`qec_ry` entry.

At runtime the QEC sink requires:

```sh
export CQOPS_SINK=qec
export CQOPS_QEC_CONFIG=/absolute/path/to/qec-config.json
```

`CQOPS_QEC_TRACE` optionally names the physical trace output, and
`CQOPS_QEC_PRECISION` selects an integer precision from 0 through 30. A requested
QEC sink never falls back to another sink.

The installed `CQBackend` package contains all link metadata required by its
consumer, but a QEC-enabled installation currently references the QEC include
directory and archives at their build-tree paths. Therefore:

- it is complete for repeated local linking while that QEC build remains in
  place;
- copying only the libcqops installation prefix to another machine is not a
  self-contained QEC deployment;
- a distributable bundle must also install/package QEC and its two dependency
  archives, or replace the absolute items with relocatable imported targets.

A different QEC implementation can integrate in either of two ways:

1. implement the general six-callback `cq_sink`, which is the preferred narrow
   boundary; or
2. act as a drop-in replacement for the existing QEC adapter by providing the
   exact header API, archive names, return conventions, trace behavior, and
   logical-qubit capacity semantics listed above.

The first option isolates the new engine from the current QEC library's package
layout. The second is appropriate only when API compatibility with that library
is intentional.
