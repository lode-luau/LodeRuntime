# RFC: Lode Generated CI and Enforcement

## Status

Accepted design. The generated workflow command itself is not implemented;
the native build contract it must invoke is now defined by RFC 08.

## Commands

The package manager will provide:

```text
lode ci init
lode ci validate [--source|--artifact]
lode ci update
```

`lode ci init` creates `.github/workflows/lode.yml` and refuses to overwrite an
existing file unless explicitly forced. It does not execute package code.

`lode ci validate --source` validates the development tree: `lode.json`,
`init.luau`, `LICENSE`, native library path declarations, and `CMakeLists.txt`
for native packages. It does not require compiled native libraries.

Dependency and lockfile consistency are separate package-manager validation
work and are not silently claimed by the current command.

`lode ci validate --artifact` validates the publishable tree: the source
contract plus the Debug/Release native libraries, required exports, ABI,
configuration metadata, package-local runtime libraries, and required
third-party notices. The flagless `lode ci validate` form remains an alias for
`--artifact`.

`lode ci update` updates only the generated baseline section. User-owned jobs
and configuration must remain outside that section; silent replacement of the
workflow is forbidden.

## Native workflow

The workflow identifies a native package from `lode.json.libraries`. Its
baseline job performs these operations:

1. Check out the tagged source.
2. Download and checksum the pinned Lode SDK.
3. Provision build dependencies required by the package's CMake project, such
   as OpenSSL. This does not create a Lode manifest dependency.
4. Configure CMake with `CMAKE_PREFIX_PATH` pointing to the SDK.
5. Build Debug and Release for each declared target.
6. Run CTest and the package tests through the matching SDK `lode` runtime.
7. Verify `LodeModuleABI()` and `LodeModuleConfig()` before packaging.
8. Validate every `lode.json.libraries` path.
9. Package only artifacts produced by successful jobs.

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
the manifest, resolves dependencies, checks the package root convention, and
runs the package's declared Luau test entry through `lode`.

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

The first implementation will generate a versioned local workflow template.
The generated baseline will identify its template version, and
`lode ci update` will preserve user-owned sections. A reusable workflow may be
introduced later only after its repository, pinning policy, and required-check
stability are defined.
