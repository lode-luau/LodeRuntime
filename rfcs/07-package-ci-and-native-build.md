# RFC: Lode Package CI and Compiled Implementation Ownership

## Status

Accepted design direction for the `package-manager` branch. The compiled
implementation contract is defined by RFC 08 and the first runtime/CMake
pieces are now implemented.

## Summary

The Lode package manager orchestrates package installation, CMake invocation,
testing, and packaging. It does not replace CMake or invent a second C/C++
dependency system.

Every package is a module package. A package may execute its fixed
`init.luau`, or `package.luau` may declare a compiled implementation that the
runtime loads instead. CMake remains the source of truth for sources, include
directories, compiler options, system libraries, and link targets.

This RFC defines ownership and the baseline package lifecycle. The compiled
ABI, artifact layout, and generated CI workflow have dedicated RFCs:

- [08 - Module Development and ABI](08-native-sdk-and-abi.md)
- [09 - Package Artifacts and Releases](09-package-artifacts-and-releases.md)
- [10 - Generated CI and Enforcement](10-generated-ci-and-enforcement.md)

## Ownership boundaries

```text
package.luau
    package identity, requirements, and optional compiled implementation contract

lode.lock
    exact versions, revisions, target artifacts, ABI values, and checksums

.config.luau
    Luau aliases and Luau/LSP configuration

CMakeLists.txt
    compiled implementation sources, find_package, link targets, and output

CI
    toolchains, system libraries, validation, and release artifacts

lode
    Luau execution, module resolution, and optional compiled implementation loading
```

## Dependency ownership

Compiled implementations link to the existing `LodeCore` CMake target. Current
modules such as `tcp` and `websocket` keep their source and include
relationships in CMake. CMake locates OpenSSL or other system libraries when a
module requires them.

If one module calls another through Luau `require`, that is a normal
`dependencies` entry in `package.luau`; it is not a C++ link dependency. A
separately published compiled dependency requires its own exported CMake
target, version policy, artifact layout, and ABI policy.

There is no `nativeDependencies` or `systemDependencies` field. OpenSSL and
other system libraries remain CMake/CI concerns.

## Package-manager build orchestration

The intended package workflow is:

```text
lode install --dev --locked
    ↓
prepare package-local aliases and dependencies
    ↓
prepare or select the Lode development distribution and CMake toolchain
    ↓
cmake -S . -B build [toolchain/options]
    ↓
cmake --build build --config <configuration>
    ↓
run package tests through lode
    ↓
validate package.luau, init.luau, and declared artifacts
    ↓
lode pack
```

`lode install` resolves runtime dependencies. `--dev` includes the root
`devDependencies`; `--locked` forbids changing resolved versions, revisions,
artifacts, or checksums. A consumer normally installs published artifacts; a
source build is an explicit maintainer workflow.

The package manager invokes CMake but does not encode CMake's link
instructions in `package.luau`.

## Package tests

The default package test entry is `tests/run.luau`, executed through the
matching Lode runtime with the package's `.config.luau` active. A package
test loads the root module through its configured alias. Its `init.luau` is
executed for a Luau-only package and is type-only when a compiled
implementation is selected.

The test script and CTest configuration belong to the package's CMake/CTest
files or the user-editable workflow. They are not configurable module
entrypoints in `package.luau`.

Pull request CI must:

1. validate `package.luau` and `init.luau`;
2. install locked dependencies when declared;
3. download and verify the pinned Lode development distribution;
4. prepare the platform toolchain and system libraries;
5. configure and build through the package's CMake project when it has a
   compiled implementation;
6. run tests through `lode`;
7. verify `LodeModuleInit`, `LodeModuleABI()`, and `LodeModuleConfig()` when a
   compiled artifact is declared;
8. verify every artifact target built by the matrix.

The matrix may claim only targets that it actually builds and tests.

## Release CI

Release CI must:

1. require a SemVer Git tag;
2. verify the tag matches `package.luau.version`;
3. build only validated implementation targets;
4. package `package.luau`, `init.luau`, licenses, Luau sources, and produced
   artifacts;
5. generate checksums;
6. publish package metadata mapping target information to each artifact.

The release must run from a clean checkout and publish only artifacts produced
by successful target jobs.

## CI generation and enforcement boundary

The package manager may provide:

```text
lode ci init
lode ci validate --source
lode ci validate --artifact
lode pack --output out/lode-<name>-<version>-<platform>-<architecture>.zip .
lode ci update
```

`lode ci init` generates an editable workflow. It detects whether the package
declares `implementation` and adds the CMake/artifact validation required by
that package. A package without `implementation` receives only Luau package
validation and tests.

The generated workflow may be customized with additional tests, platforms,
cache settings, documentation jobs, and publication steps. Required baseline
checks must be enforced by repository branch protection, required status
checks, or the release gate.
