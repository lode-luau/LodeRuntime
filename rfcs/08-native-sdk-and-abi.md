# RFC: Lode Module SDK and ABI Compatibility

## Status

Accepted design. The first CMake and runtime pieces are implemented in this
repository. Nightly SDK packaging and external-package validation are wired
for Windows x64; additional SDK targets and stable release publication remain
follow-up work.

## Scope

The Lode SDK is the development kit used to compile a module implementation outside
the LodeRuntime source tree. It is not a runtime package, is not declared as a
dependency in `package.luau`, and does not add a configurable entrypoint to
that manifest. Every module uses `init.luau` as its fixed Luau entry and type
interface.

The SDK is consumed through the standard CMake package mechanism:

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

Packages with compiled implementations use the SDK helper instead of copying platform and output
directory detection into every repository:

```cmake
cmake_minimum_required(VERSION 3.20)
project(http LANGUAGES CXX)

find_package(Lode CONFIG REQUIRED)

lode_add_native_module(http
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

`lode_add_native_module()` creates the compiled implementation with no Unix `lib`
prefix and writes it to:

```text
libs/<platform>/<architecture>/<configuration>/<target>.<suffix>
```

The helper does not install OpenSSL, choose system libraries, or execute
package code. Those remain normal CMake and CI responsibilities. The helper
must produce the artifact described by the package's `implementation` table;
it must not create an alternative module entrypoint.

## SDK contents

The SDK is installed with normal CMake package layout:

```text
<prefix>/
├── lode-sdk.json
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
    └── LodeNativeModule.cmake
```

The SDK ships Debug and Release variants together. A package built in Debug
must be tested with the Debug runtime; the same rule applies to Release.

The SDK archive also carries the matching bundled standard-library catalog.
This keeps the SDK's `lode` executable self-contained for `lode install` and
package CI validation. It does not turn standard modules into external
packages; the end-user runtime archive and selective standard-module artifacts
remain the distribution paths described by RFC 09.

The exported Luau targets are included because a compiled module calls Luau's C
API in addition to using the Lode C++ wrappers. The SDK must therefore keep
the LodeCore, Luau, compiler, and CRT/toolchain build compatible.

## ABI contract

The first ABI identifier is the opaque string:

```text
lode-abi-1
```

It is defined by `LODE_ABI_ID` in the public export header. It must not be
assembled by package authors or inferred from the package version.

SDK-built modules export these C symbols before initialization:

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

## SDK version and distribution

The SDK version follows the Lode release version and is published with the
Lode release. A package CI job downloads a platform/toolchain SDK archive,
verifies its SHA-256 checksum, and sets `CMAKE_PREFIX_PATH` to its extracted
prefix. The package manager stores archives by checksum and extracted SDKs in
the global `.lode` cache; no project-local `.lode` directory is used.

> **Implementation note:** `CMakeLists.txt:427` keeps `LodeConfigVersion.cmake`
> at `PROJECT_VERSION` (`1.0.0`) even for nightly `1.0.0-nightly.YYYYMMDD.N`
> builds. CMake treats `1.0.0-nightly` < `1.0.0` (prerelease), so exposing the
> nightly string as the CMake package version would make
> `find_package(Lode 1.0.0)` fail on nightly SDKs. The exact nightly is
> authoritative in the SDK's `VERSION` file and `lode-sdk.json`
> (`nightly.yml:138`, `cmake/lode-sdk.json.in`); the CMake package version
> stays `1.0.0` with `SameMajorVersion` compatibility.

The package manager never places SDK link instructions in `package.luau`.

## Compiled implementation dependency ownership

The following remain CMake/CI concerns:

- OpenSSL;
- platform system libraries;
- compiler-provided runtimes;
- internal source relationships such as the current `tcp` and `HttpTls.cpp`
  relationship;
- the LodeCore/Luau libraries shipped by the SDK.

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
