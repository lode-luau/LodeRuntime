# RFC: Lode Package CI and Native Build Ownership

## Status

Accepted design direction for the `package-manager` branch. The native SDK
contract is defined by RFC 08 and the first runtime/CMake pieces are now
implemented.

## Summary

The Lode package manager orchestrates package installation, CMake invocation,
testing, and packaging. It does not replace CMake and does not invent a second
native dependency system.

For native packages, CMake remains the source of truth for sources, include
directories, compiler options, system libraries, and link targets. CI provides
the compiler, CMake, OpenSSL or other system libraries, and the Lode build
environment.

This RFC defines ownership and the baseline package lifecycle. The native ABI,
published artifact layout, and generated CI workflow have dedicated RFCs:

- [08 - Native SDK and ABI](08-native-sdk-and-abi.md)
- [09 - Package Artifacts and Releases](09-package-artifacts-and-releases.md)
- [10 - Generated CI and Enforcement](10-generated-ci-and-enforcement.md)

## Ownership boundaries

```text
lode.json
    package identity, package requirements, native artifact map

lode.lock
    exact versions, revisions, and checksums

.config.luau
    Luau aliases and Luau/LSP configuration

CMakeLists.txt
    native compilation, find_package, target_link_libraries, output paths

CI
    toolchains, system libraries, validation, release artifacts

lode
    Luau execution and native module loading
```

## Existing native dependencies

The following are real dependencies in the current repository:

- Native modules link to the existing `LodeCore` CMake target.
- `tcp` and `websocket` use their existing CMake source/include relationships.
- CMake locates OpenSSL where the module requires it.
- `sanity` is a Luau module used to run tests through `lode`.

There is no `lode-tcp-core` package. No manifest may refer to that name until a
real package, repository, artifact, and CMake integration have been created.

There is also no `nativeDependencies` or `systemDependencies` field in the
initial manifest contract. OpenSSL remains expressed by CMake and prepared by
the platform-specific CI environment.

## Package-manager build orchestration

The intended native package workflow is:

```text
lode install --dev --locked
    ↓
prepare package-local aliases and development modules
    ↓
prepare or select the Lode build environment
    ↓
cmake -S . -B build [toolchain/options]
    ↓
cmake --build build --config <configuration>
    ↓
run lode-based package tests
    ↓
validate lode.json and declared libraries
    ↓
lode pack
```

`lode install` resolves and installs runtime dependencies. `--dev` includes
the current project's `devDependencies`; `--locked` requires the committed
lockfile and forbids changing its resolved versions, revisions, or checksums.
The package manager may download an exact locked artifact that is not cached,
but it must not perform a new resolution in this mode.

The ownership boundary is fixed: the package manager invokes CMake; it does
not encode CMake's link instructions in `lode.json`.

The normal consumer installation path uses prebuilt release artifacts. A
consumer does not compile a dependency as part of a regular install. Source
builds are a development and package-maintainer workflow and must be explicit.

## Continuous integration

### Package test entry

The default package test entry is `tests/run.luau`. It is executed through
the matching `lode` executable from the SDK, with the package's `.config.luau`
active. A native package test loads the package through its configured alias;
the package's own local test configuration may map that alias to `.`.

The default does not prevent a maintainer from choosing another test script.
That choice belongs in the package CMake/CTest configuration or the
user-editable workflow, not in a new `test`, `entry`, or `type` field in
`lode.json`.

Pull request CI for a package must:

1. validate the manifest;
2. install dependencies with `lode install --dev --locked` when the package
   declares dependencies or development dependencies;
3. download and verify the pinned Lode SDK;
4. prepare the platform toolchain and system libraries;
5. configure with `CMAKE_PREFIX_PATH` and build through the package's CMake
   project;
6. run tests through `lode`;
7. verify the native entry point, `LodeModuleABI()`, and `LodeModuleConfig()`;
8. verify every declared platform artifact that the matrix builds.

The matrix must only claim platforms, architectures, and configurations that
the workflow actually builds and tests. A native package must not publish a
`libraries` entry merely because the platform name is allowed by the manifest
contract.

The initial native validation matrix is expected to cover Debug and Release for
each supported target. The exact target set is package-specific and remains
limited by available runners, toolchains, Lode SDK support, and passing tests.

Release CI must additionally:

1. require a SemVer Git tag;
2. verify the tag matches the manifest version;
3. build only artifacts that were actually validated;
4. package `lode.json`, `init.luau`, licenses, Luau sources, and native
   libraries according to the package layout;
5. generate checksums;
6. publish the package artifact and GitHub Release metadata.

Release CI must run from a clean checkout and must package only artifacts
produced by successful matrix jobs. The release tag uses `vMAJOR.MINOR.PATCH`
and must match the manifest version without the leading `v`.

The main LodeRuntime CI continues to build the bundled modules in the existing
top-level CMake project and runs the canonical sanity and CTest suites. It may
also run a package smoke test once the package CLI exists.

## Native module dependencies between packages

Native code shared by current modules remains an internal CMake concern. It
must not be represented as a package dependency until a separately published
native library has an actual exported CMake target, version policy, artifact
layout, and ABI policy.

If a native module only calls another module through Luau `require`, that is a
normal `dependencies` entry. It is not a C++ link dependency.

## CI generation and enforcement boundary

The package manager may provide:

```text
lode ci init
lode ci validate --source
lode ci validate --artifact
lode pack --output out/lode-<name>-<version>-windows-x64.zip .
lode ci update
```

`lode ci init` generates an editable workflow under
`.github/workflows/lode.yml`. It must not overwrite an existing workflow unless
the user explicitly requests replacement. The generated workflow is a starting
point, not a security boundary.

The workflow detects a native package from the presence of `libraries` and
adds the CMake/native validation required by the package shape. Pure Luau
packages do not receive native build jobs.

The generated file may be customized with additional tests, platforms, cache
settings, documentation jobs, and publication steps. Required baseline checks
must be enforced through repository branch protection, required GitHub status
checks, or the release gate; an editable YAML file alone cannot make CI
mandatory.

The first generated workflow uses a versioned local template. A reusable
workflow can be introduced later only after its repository and pinning policy
are defined.
