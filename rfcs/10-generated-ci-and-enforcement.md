# RFC: Lode Generated CI and Enforcement

## Status

Accepted design. `lode ci init` generates the first Windows x64 compiled-module
workflow baseline, and `lode ci update` refreshes only its managed block. The
compiled implementation contract it invokes is defined by RFC 08. Additional target
matrices are not implemented yet.

## Commands

The package manager will provide:

```text
lode install [--dev] [--locked]
lode pack [--output <archive>] [package-root]
lode ci init --sdk-version <nightly> --sdk-sha256 <sha256>
lode ci validate [--source|--artifact] [--locked]
lode ci update
```

`lode ci init` requires the exact nightly SDK release tag and SHA-256 checksum
that the generated workflow will download. It refuses to write a workflow when
the pin is missing or malformed, and refuses to overwrite an existing file
unless explicitly forced. It validates package metadata and file presence only;
it does not execute package code. The initial generator accepts Windows x64
compiled implementations and emits a Luau package test workflow. Other
compiled targets are
rejected until their runner and SDK matrix is implemented.

`lode ci validate --source` validates the development tree: `package.luau`,
`init.luau`, `LICENSE`, implementation artifact declarations, and
`CMakeLists.txt` when a compiled implementation is declared. It does not
require built artifacts.

`ci validate` checks package structure and artifacts; it does not install
dependencies. With `--locked`, it additionally verifies that the existing
`lode.lock` exactly describes the validated dependency graph. A dependency-
bearing package with a missing or stale lockfile fails this check rather than
regenerating the file. A package with no dependencies may pass without a
lockfile, as defined by RFC 06. The regular validation path does not download
artifacts or materialize aliases; dependency-bearing CI must still run
`lode install --dev --locked` after validation once the installer is available.

The `--source --locked` form is the CI handoff after that install step. It
replays the exact locked graph, including selected stdlib and GitHub Release
artifacts, without materializing packages or changing `.config.luau`. A
locked artifact may be populated into the global cache when it is missing;
the project graph and lockfile remain unchanged.

The `--artifact --locked` form uses the same exact locked graph, but validates
the publishable package after its Debug/Release artifacts have been built. It
does not resolve a different dependency version merely because the bundled
stdlib catalog has another version. It may populate an exact locked dependency
artifact into the global cache, but it does not materialize packages or change
`.config.luau`.

`lode ci validate --artifact` validates the publishable tree: the source
contract plus the Debug/Release compiled artifacts, required exports, ABI,
configuration metadata, package-local runtime libraries, and required
third-party notices. The flagless `lode ci validate` form remains an alias for
`--artifact`.

`lode ci update` requires an existing workflow containing the exact managed
markers emitted by `lode ci init` and a valid SDK pin. It reuses the existing
pin and updates only the generated baseline section. User-owned jobs and
configuration must remain outside that section; missing or malformed markers
or pins cause the command to fail instead of replacing the workflow silently.

## Compiled implementation workflow

The workflow identifies a compiled implementation from
`package.luau.implementation` and derives its publication matrix exclusively
from `package.luau.implementation.targets.release`. The first implemented
matrix is Windows x64; a declared target without an
implemented runner and SDK matrix makes `lode ci init` or `lode ci update`
fail explicitly rather than generate a workflow that claims support. Its
baseline job performs these operations:

1. Check out the tagged source.
2. Download and checksum the explicitly pinned Lode SDK.
3. Run `lode install --dev --locked` when the package declares dependencies or
   development dependencies. The command may fetch the exact locked artifact,
   but must not resolve new versions or revisions.
4. Provision build dependencies required by the package's CMake project, such
   as OpenSSL. This does not create a Lode manifest dependency.
5. Configure CMake with `CMAKE_PREFIX_PATH` pointing to the SDK.
6. Build Debug and Release for each `implementation.targets.release` target.
7. Run CTest and the package tests through the matching SDK `lode` runtime.
8. Verify `LodeModuleABI()` and `LodeModuleConfig()` before packaging.
9. Validate every published `implementation.targets.release` artifact with
   `lode ci validate --artifact --locked`.
10. Run `lode pack --output out/lode-<name>-<version>-windows-x64.zip .` and
    publish only the archive and checksum produced by successful jobs.

The compiled implementation CMake must use:

```cmake
find_package(Lode CONFIG REQUIRED)
lode_add_module(...)
```

The default package test command runs `tests/run.luau` through the matching
SDK `lode` executable. A maintainer may replace that command in the editable
workflow or package CTest configuration without changing `package.luau`.

The workflow must fail if an `implementation.targets.release` target was not
built and tested. It must not claim support based only on a recognized
identifier or a target listed in `package.luau`. Static compiled targets and iOS runners,
signing, and loading remain outside this implementation.

## Luau-only workflow

Packages without `implementation` do not build compiled artifacts, but the
current Windows baseline still downloads the pinned SDK because its `lode`
executable is the runtime used for validation and tests. Their baseline runs
`lode install --dev --locked` when dependencies are declared, checks the
package root convention, and runs the package's declared Luau test entry
through that runtime.

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
Lode CI / implementation-<platform>-<architecture>
```

The repository's branch protection, not the CLI, enforces these checks.

## Workflow ownership

The first implementation generates a versioned local workflow template. The
generated baseline identifies its template version, and `lode ci update`
preserves user-owned sections outside the managed markers. A reusable workflow
may be introduced later only after its repository, pinning policy, and
required-check stability are defined.
