# LodeRuntime [![nightly](https://github.com/lode-luau/LodeRuntime/actions/workflows/nightly.yml/badge.svg)](https://github.com/lode-luau/LodeRuntime/actions/workflows/nightly.yml) [![Latest Nightly Release](https://img.shields.io/github/v/release/lode-luau/LodeRuntime?include_prereleases&label=latest%20nightly)](https://github.com/lode-luau/LodeRuntime/releases)

A fast, modular runtime environment for executing Luau scripts.

### Performance flags

Optional tuning flags are consumed before the script/command and never leak
into `script-arguments`. Without them, behavior is identical to the defaults.

    lode --opt=2 --codegen=all --gc-goal=250 --mem-limit=512 script.luau

| Flag | Meaning |
|---|---|
| `--opt=<0-2>` | Bytecode optimization level override (default 1; 2 enables builtin folding). |
| `--codegen=native\|all\|off` | Native codegen scope: modules marked `--!native` plus functions marked with the `@native` attribute (default), every unmarked function as well, or off. |
| `--gc-goal=<pct>` | GC heap target as a percentage of live data (default 200). |
| `--gc-stepmul=<pct>` | GC work multiplier per incremental step. |
| `--gc-stepsize=<kb>` | Explicit incremental step size in KiB. |
| `--mem-limit=<mb>` | Soft heap limit enforced from the event loop (larger GC steps when exceeded). |

### Running Tests

    ./build/bin/Release/lode modules/sanity/run.luau
    ctest --test-dir build -C Debug --output-on-failure
    # CI (Ninja) uses split trees: build-debug/bin/Debug/lode.exe and
    # build-release/bin/Release/lode.exe — see CONTRIBUTING.md for local
    # Visual Studio workflow.

## Package and CLI quick start

The current package workflow is:

    lode init demo --description "A Luau package"
    lode add owner/repository[@version]
    lode install [--locked] [--dev]
    lode pack --output out/demo.zip .
    lode ci validate --source
    lode ci validate --artifact

Use `lode ci init --lode-version <nightly> --lode-sha256 <sha256>` to generate
an initial workflow. A locked install requires an up-to-date `lode.lock` and
uses the exact locked artifacts; the current installer does not build dependency
sources. The CI runtime/module test matrix covers Windows x64, Linux x64, and
the host architecture on macOS. Nightly runtime, development, and stdlib
archives are published per target; a platform key in a manifest is still only
a claim until its matching CI artifact exists. See [the manifest and ownership RFC](rfcs/05-package-manifest-and-ownership.md), [package resolution and lockfiles](rfcs/06-package-resolution-and-lockfile.md), and [package artifacts](rfcs/09-package-artifacts-and-releases.md).

## Compiled module quick start

A compiled module package typically contains `package.luau`, a type-only `init.luau`,
`CMakeLists.txt`, and a `src/` directory. Its CMake project uses:

    find_package(Lode CONFIG REQUIRED)
    lode_add_module(...)

Configure with the Lode development distribution in `CMAKE_PREFIX_PATH`, then build the
module and run it with the matching Debug or Release `lode` runtime. A module
`init.luau` supplies metadata for loading and typing; it is not executed as
the compiled module implementation. Publishable module artifacts must provide
`LodeModuleInit`, `LodeModuleConfig`, and `LodeModuleABI`; direct legacy
loading may have looser checks. See [the module development and ABI RFC](rfcs/08-native-sdk-and-abi.md), [artifact/release rules](rfcs/09-package-artifacts-and-releases.md), [generated CI](rfcs/10-generated-ci-and-enforcement.md), and [static target validation](rfcs/11-static-native-target-validation.md).

Package installation and module CI behavior are evolving; the linked RFCs are
the normative design documents. The runtime executes trusted code and is not a
sandbox.

## RFCs

* [01 - Module Resolution](rfcs/01-module-resolution.md)
* [02 - Luau Configuration Files](rfcs/02-luau-configuration.md)
* [03 - Native Modules Build System](rfcs/03-native-modules-build.md)
* [04 - C++ API Core](rfcs/04-cpp-api-core.md)
* [05 - Package Manifest and Ownership](rfcs/05-package-manifest-and-ownership.md)
* [06 - Package Resolution and Lockfile](rfcs/06-package-resolution-and-lockfile.md)
* [07 - Package CI and Native Build](rfcs/07-package-ci-and-native-build.md)
* [08 - Module Development and ABI](rfcs/08-native-sdk-and-abi.md)
* [09 - Package Artifacts and Releases](rfcs/09-package-artifacts-and-releases.md)
* [10 - Generated CI and Enforcement](rfcs/10-generated-ci-and-enforcement.md)
* [11 - Static Native Target Validation](rfcs/11-static-native-target-validation.md)

## Building

### Requirements
* CMake 3.20+
* C++20 compatible compiler (MSVC, GCC, or Clang)
* vcpkg in manifest mode

### Build Commands

    # Windows (Visual Studio)
    cmake -B build -G "Visual Studio 18 2026" -DCMAKE_TOOLCHAIN_FILE=C:/tools/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
    cmake --build build --config Release

    # Linux
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
      -DVCPKG_TARGET_TRIPLET=x64-linux
    cmake --build build

    # macOS: use arm64-osx on Apple Silicon or x64-osx on Intel
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
      -DVCPKG_TARGET_TRIPLET=arm64-osx
    cmake --build build

> Change `--config Release` or `-DCMAKE_BUILD_TYPE=Release` to `Debug` for debug builds.

## Usage

    ./build/bin/Release/lode path/to/script.luau
    # With Visual Studio multi-config generator the binary is at
    # build/bin/Release/lode.exe (or build/bin/Debug/lode.exe for Debug).

## Contributing

Please read our [Contributing Guide](CONTRIBUTING.md) and [Code of Conduct](CODE_OF_CONDUCT.md). For security matters, see [SECURITY.md](SECURITY.md). See [CHANGELOG.md](CHANGELOG.md) for notable changes.

## License

This project is available under the terms of the [MIT License](LICENSE).
