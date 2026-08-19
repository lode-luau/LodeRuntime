# LodeRuntime [![nightly](https://github.com/lode-luau/LodeRuntime/actions/workflows/nightly.yml/badge.svg)](https://github.com/lode-luau/LodeRuntime/actions/workflows/nightly.yml)

A fast, modular runtime environment for executing Luau scripts.

## RFCs

* [01 - Module Resolution](rfcs/01-module-resolution.md)
* [02 - Luau Configuration Files](rfcs/02-luau-configuration.md)
* [03 - Native Modules Build System](rfcs/03-native-modules-build.md)
* [04 - C++ API Core](rfcs/04-cpp-api-core.md)
* [05 - Package Manifest and Ownership](rfcs/05-package-manifest-and-ownership.md)
* [06 - Package Resolution and Lockfile](rfcs/06-package-resolution-and-lockfile.md)
* [07 - Package CI and Native Build](rfcs/07-package-ci-and-native-build.md)
* [08 - Native SDK and ABI](rfcs/08-native-sdk-and-abi.md)
* [09 - Package Artifacts and Releases](rfcs/09-package-artifacts-and-releases.md)
* [10 - Generated CI and Enforcement](rfcs/10-generated-ci-and-enforcement.md)

## Building

### Requirements
* CMake 3.15+
* C++20 compatible compiler (MSVC, GCC, or Clang)

### Build Commands

    # Windows (Visual Studio)
    cmake -B build -G "Visual Studio 17 2022"
    cmake --build build --config Release

    # Linux
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build

> Change `--config Release` or `-DCMAKE_BUILD_TYPE=Release` to `Debug` for debug builds.

## Usage

    ./build/bin/Release/lode path/to/script.luau

### Running Tests

    ./build/bin/Release/lode modules/sanity/run.luau
    ctest --test-dir build -C Release --output-on-failure

## Contributing

Please read our [Contributing Guide](CONTRIBUTING.md) and [Code of Conduct](CODE_OF_CONDUCT.md). For security matters, see [SECURITY.md](SECURITY.md). See [CHANGELOG.md](CHANGELOG.md) for notable changes.

## License

This project is available under the terms of the [MIT License](LICENSE).
