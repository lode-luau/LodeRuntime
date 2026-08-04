# LodeRuntime

A fast, modular runtime environment for executing Luau scripts.

## RFCs

* [01 - Module Resolution](rfcs/01-module-resolution.md)
* [02 - Luau Configuration Files](rfcs/02-luau-configuration.md)
* [03 - Native Modules Build System](rfcs/03-native-modules-build.md)
* [04 - C++ API Core](rfcs/04-cpp-api-core.md)

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

    ./build/bin/Release/lode_runtime path/to/script.luau

### Running Tests

    ./build/bin/Release/lode_runtime temp/test.luau

## License

This project is available under the terms of the [MIT License](LICENSE).
