# RFC: Lode Module Development and ABI Compatibility

## Status

Accepted design. The first CMake and runtime pieces are implemented in this
repository. The nightly Lode development distribution and external-package
validation are wired for Windows x64; the runtime and standard-library CI
matrix also covers Linux x64 and the host architecture on macOS.

## Scope

The Lode development distribution is the development kit used to compile a
module implementation outside the LodeRuntime source tree. It is not a runtime
package, is not declared as a
dependency in `package.luau`, and does not add a configurable entrypoint to
that manifest. Every module uses `init.luau` as its fixed Luau entry and type
interface.

The Lode development distribution is consumed through the standard CMake package mechanism:

```cmake
find_package(Lode CONFIG REQUIRED)
```

The package-facing targets are:

```text
Lode::Core
Lode::Module
```

`Lode::Module` is the target compiled module implementations link to. It carries the public
Lode and Luau include paths, the matching `LodeCore` library, and the build
configuration definition required by the loader.

## CMake contract

Packages with compiled implementations use the Lode module helper instead of copying platform and output
directory detection into every repository:

```cmake
cmake_minimum_required(VERSION 3.20)
project(http LANGUAGES CXX)

find_package(Lode CONFIG REQUIRED)

lode_add_module(http
    SOURCES
        src/Main.cpp
        src/HttpManager.cpp
        src/HttpClient.cpp
        src/HttpServer.cpp
        src/HttpHelpers.cpp
        src/HttpTls.cpp
)

if(WIN32)
    target_link_libraries(http PRIVATE Secur32)
else()
    find_package(OpenSSL REQUIRED)
    target_compile_definitions(http PRIVATE LODE_HAS_OPENSSL_TLS)
    target_link_libraries(http PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()
```

`lode_add_module()` creates the compiled implementation with no Unix `lib`
prefix and writes it to the path described by the package manifest. With no
extra options, it writes:

```text
libs/<platform>/<architecture>/<configuration>/<target>.<suffix>
```

The helper accepts optional `ARTIFACT` and `LAYOUT` arguments when a package
uses a non-default implementation contract:

```cmake
lode_add_module(my_target
    ARTIFACT "my-artifact"
    LAYOUT "runtime/{platform}/{architecture}/{configuration}/{artifact}{extension}"
    SOURCES src/Main.cpp
)
```

These values must exactly match `package.luau.implementation`. The helper does
not install OpenSSL, choose system libraries, or execute package code. Those
remain normal CMake and CI responsibilities; it must not create an alternative
module entrypoint.

## Lode development distribution contents

The development distribution is installed with normal CMake package layout:

```text
<prefix>/
├── share/lode/lode-install.json
├── bin/<configuration>/lode[.exe]
├── bin/<configuration>/LodeCore[.dll]
├── stdlib/.config.luau
├── stdlib/modules/<module>/
├── include/Lode/*.hpp
├── include/Luau/*
├── lib/<configuration>/LodeCore[.lib|.so|.dylib]
├── lib/<configuration>/Luau.*
└── lib/cmake/Lode/
    ├── LodeConfig.cmake
    ├── LodeConfigVersion.cmake
    ├── LodeTargets.cmake
    └── LodeModule.cmake
```

The development distribution ships Debug and Release variants together. A package built in Debug
must be tested with the Debug runtime; the same rule applies to Release.

The development archive also carries the matching bundled standard-library catalog.
This keeps the Lode `lode` executable self-contained for `lode install` and
package CI validation. It does not turn standard modules into external
packages; the end-user runtime archive and aggregate standard-module bundle
remain the distribution paths described by RFC 09.

The exported Luau targets are included because a compiled module calls Luau's C
API in addition to using the Lode C++ wrappers. The development distribution
must therefore keep
the LodeCore, Luau, compiler, and CRT/toolchain build compatible.

## ABI contract

The first ABI identifier is the opaque string:

```text
lode-abi-1
```

It is defined by `LODE_ABI_ID` in the public export header. It must not be
assembled by package authors or inferred from the package version.

Modules built with Lode export these C symbols before initialization:

```cpp
LodeModuleConfig();  // Debug/Release compatibility
LodeModuleABI();     // Lode ABI compatibility
LodeModuleInit(lua_State*); // module initialization
```

The runtime checks `LodeModuleConfig()` and then `LodeModuleABI()` before
calling `LodeModuleInit()`. The ABI function returns `const char*`; no C++
object crosses the DLL boundary.

The current loader still allows a legacy module that has no
`LodeModuleABI()` symbol. The package publication validator must reject such
an artifact. This preserves direct compatibility during the migration without
making unmanaged binaries eligible for new package releases.

The ABI identifier changes when the compiled module contract changes. A package
artifact is compatible only when all of these match:

```text
Lode ABI identifier
platform
architecture
Debug/Release configuration
compiler/toolchain family
CRT/runtime-library policy
```

## Lode version and distribution

The development distribution follows the Lode release version and is published
with the Lode release. A package CI job downloads the platform/toolchain
development archive, verifies its entry in `SHA256SUMS`, and sets
`CMAKE_PREFIX_PATH` to its extracted prefix. The package manager stores archives
by checksum and extracted Lode distributions in
the global `.lode` cache; no project-local `.lode` directory is used.

> **Implementation note:** `CMakeLists.txt:427` keeps `LodeConfigVersion.cmake`
> at `PROJECT_VERSION` (`1.0.0`) even for nightly `1.0.0-nightly.YYYYMMDD.N`
> builds. CMake treats `1.0.0-nightly` < `1.0.0` (prerelease), so exposing the
> nightly string as the CMake package version would make
> `find_package(Lode 1.0.0)` fail on nightly distributions. The exact nightly is
> authoritative in the Lode `VERSION` file and `share/lode/lode-install.json`;
> the CMake package version
> stays `1.0.0` with `SameMajorVersion` compatibility.

The package manager never places development-distribution link instructions in
`package.luau`.

## Compiled implementation dependency ownership

The following remain CMake/CI concerns:

- OpenSSL;
- platform system libraries;
- compiler-provided runtimes;
- internal source relationships such as the current `tcp` and `HttpTls.cpp`
  relationship;
- the LodeCore/Luau libraries shipped by the development distribution.

There is no `lode-tcp-core` package and no `nativeDependencies` or
`systemDependencies` field in the initial manifest contract.

A separately published compiled-module dependency requires its own exported CMake
target, artifact layout, runtime policy, and ABI policy before it can be
introduced.

## OpenSSL runtime policy

OpenSSL is a CMake/CI build dependency, not a Lode package-manager
dependency. Package CI may provision it for `find_package(OpenSSL REQUIRED)`;
the package manager must not install OpenSSL into the user's system and
`package.luau` must not gain a `systemDependencies` field for it.

When a module implementation uses OpenSSL, its published artifact must include
the runtime libraries required by the supported target beside the module
artifact.
Windows uses the package-local DLL directory. Unix-like targets must use a
package-relative loader path such as `$ORIGIN` or `@loader_path` and include
the corresponding shared libraries in the artifact. A target that cannot
deliver a working package-local OpenSSL runtime is not publishable.
