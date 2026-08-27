# RFC: Lode Module Structure and Build System

## Summary

This RFC defines the standard structure and CMake integration for Lode
modules. Every package is a module package with a fixed `init.luau` entry. A
module may be implemented entirely in Luau or may have a compiled
implementation loaded by the runtime.

## Motivation

Modules must be portable, cross-platform, and consumable by Luau tooling.
Developers need one predictable package layout, one require path, and one CMake
contract for the optional compiled implementation.

## Design Details

### 1. Standard Directory Structure

A module with a compiled implementation follows this layout:

```text
my_module/
├── CMakeLists.txt         # Build configuration
├── package.luau           # Package identity and implementation contract
├── init.luau              # Fixed module entry and LSP interface
├── src/
│   └── main.cpp           # Compiled implementation using the Lode API
└── libs/                  # Generated runtime artifacts
    └── windows/x64/Release/
        └── my_module.dll
```

A Luau-only module needs `package.luau` and `init.luau`; it does not need
`CMakeLists.txt`, `src/`, or `libs/`.

Submodules are ordinary Luau module directories. A directory is a module when
it contains its own `init.luau`; no `modules` map or multiple entrypoint list
is declared in `package.luau`.

### 2. Required Files

- **`init.luau`**: The fixed module entry. For a compiled implementation it
  provides the LSP interface and may end with `return {} :: ModuleType`; the
  runtime does not execute it in that mode.
- **`package.luau`**: The Lode package manifest. It declares package identity,
  dependencies, and the optional compiled implementation artifact.

### 3. Build System (`CMakeLists.txt`)

CMake compiles the optional implementation and remains the source of truth for
sources, include directories, compiler options, system libraries, and link
targets. The build must output the artifact to:

```text
libs/<platform>/<architecture>/<configuration>/<artifact><extension>
```

The Lode SDK helper should expose a module-oriented command:

```cmake
find_package(Lode CONFIG REQUIRED)

lode_add_module(my_module
    SOURCES
        src/main.cpp
)
```

The helper derives platform, architecture, configuration, output name, and
extension consistently with the runtime resolver. Package authors continue to
use normal CMake commands such as `find_package(OpenSSL)` and
`target_link_libraries()` for build dependencies.

### 4. Platform and Architecture

The helper detects the target platform and architecture and writes the result
to the standard layout. A recognized target is not automatically supported:
each target requires a real toolchain build, runtime test, and artifact
validation. iOS static loading remains outside the supported release contract
until it has been implemented and tested.

### 5. Build Configuration and ABI

Modules built through the Lode CMake define `LODE_BUILD_CONFIG_NAME` per
configuration and export `LodeModuleConfig` so the runtime can verify Debug or
Release compatibility. They also export `LodeModuleABI` for the Lode ABI
contract. A third-party implementation without these symbols may remain
loadable for compatibility, but it is not eligible for new package releases.
