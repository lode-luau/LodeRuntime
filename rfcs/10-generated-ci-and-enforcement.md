# RFC: Lode Generated CI and Enforcement

## Status

Accepted design. `lode ci init` generates the first Windows x64 native package
workflow baseline, and `lode ci update` refreshes only its managed block. The
native build contract it invokes is defined by RFC 08. Additional target
matrices are not implemented yet.

## Commands

The package manager will provide:

```text
lode install [--dev] [--locked]
lode ci init
lode ci validate [--source|--artifact] [--locked]
lode ci update
```

`lode ci init` creates `.github/workflows/lode.yml` and refuses to overwrite an
existing file unless explicitly forced. It validates package metadata and file
presence only; it does not execute package code. The initial generator accepts
native Windows x64 packages and emits a pure-Luau test workflow. Other native
targets are rejected until their runner and SDK matrix is implemented.

`lode ci validate --source` validates the development tree: `lode.json`,
`init.luau`, `LICENSE`, native library path declarations, and `CMakeLists.txt`
for native packages. It does not require compiled native libraries.

`ci validate` checks package structure and artifacts; it does not install
dependencies. With `--locked`, it additionally verifies that the existing
`lode.lock` exactly describes the validated dependency graph. A dependency-
bearing package with a missing or stale lockfile fails this check rather than
regenerating the file. A package with no dependencies may pass without a
lockfile, as defined by RFC 06. This check does not download artifacts or
materialize aliases; dependency-bearing CI must still run
`lode install --dev --locked` after validation once the installer is available.

`lode ci validate --artifact` validates the publishable tree: the source
contract plus the Debug/Release native libraries, required exports, ABI,
configuration metadata, package-local runtime libraries, and required
third-party notices. The flagless `lode ci validate` form remains an alias for
`--artifact`.

`lode ci update` requires an existing workflow containing the exact managed
markers emitted by `lode ci init`. It updates only the generated baseline
section. User-owned jobs and configuration must remain outside that section;
missing or malformed markers cause the command to fail instead of replacing
the workflow silently.

## Native workflow

The workflow identifies a native package from `lode.json.libraries`. Its
baseline job performs these operations:

1. Check out the tagged source.
2. Download and checksum the pinned Lode SDK.
3. Run `lode install --dev --locked` when the package declares dependencies or
   development dependencies. The command may fetch the exact locked artifact,
   but must not resolve new versions or revisions.
4. Provision build dependencies required by the package's CMake project, such
   as OpenSSL. This does not create a Lode manifest dependency.
5. Configure CMake with `CMAKE_PREFIX_PATH` pointing to the SDK.
6. Build Debug and Release for each declared target.
7. Run CTest and the package tests through the matching SDK `lode` runtime.
8. Verify `LodeModuleABI()` and `LodeModuleConfig()` before packaging.
9. Validate every `lode.json.libraries` path.
10. Package only artifacts produced by successful jobs.

The native package CMake must use:

```cmake
find_package(Lode CONFIG REQUIRED)
lode_add_native_module(...)
```

The default package test command runs `tests/run.luau` through the matching
SDK `lode` executable. A maintainer may replace that command in the editable
workflow or package CTest configuration without changing `lode.json`.

The workflow must fail if a declared target was not built and tested. It must
not claim support based only on a platform key written in `lode.json`.

## Pure Luau workflow

Pure Luau packages do not download the native SDK. Their baseline validates
the manifest, runs `lode install --dev --locked` when dependencies are declared,
checks the package root convention, and runs the package's declared Luau test
entry through `lode`.

## User customization and enforcement

Maintainers may add jobs, CMake options, system dependencies, documentation
checks, and publication steps. They may not remove the baseline validation
from the required status checks.

The generated file is not a security boundary and its presence is not proof
of compliance. Package publication requires stable required GitHub checks for
the relevant targets and a successful release workflow for the exact tag.

The initial required-check names are:

```text
Lode CI / validate
Lode CI / tests
Lode CI / native-<platform>-<architecture>
```

The repository's branch protection, not the CLI, enforces these checks.

## Workflow ownership

The first implementation generates a versioned local workflow template. The
generated baseline identifies its template version, and `lode ci update`
preserves user-owned sections outside the managed markers. A reusable workflow
may be introduced later only after its repository, pinning policy, and
required-check stability are defined.
