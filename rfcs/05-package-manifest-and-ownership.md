# RFC: Lode Package Manifest and Field Ownership

## Status

Accepted design direction for the `package-manager` branch. Implementation is
not part of this RFC.

## Summary

This RFC defines the canonical `lode.json` contract and assigns each field to
the component that consumes it. The manifest describes a package; it does not
replace CMake, `.config.luau`, the runtime loader, or CI.

The same manifest shape may be used for a project root and for a distributable
package. A project manifest may omit `init.luau` and `libraries`. There is no
`type` field and no configurable `entry` field.

## Package conventions

The package root is identified by `lode.json` and/or `init.luau`, consistent
with the existing module loader. A pure Luau package uses `init.luau` as its
automatic entry module. A native package declares platform libraries in
`libraries`; its `init.luau` provides Luau types and is not executed when a
matching native library is loaded.

The presence of `libraries` is the native-package convention. The absence of
`libraries` does not make a project root invalid: a project manifest may only
declare dependencies.

## Canonical manifest

The initial package-manager contract is:

```json
{
  "name": "signal",
  "version": "1.0.0",
  "description": "Lode signal module (observer pattern with Connect, Once, Wait)",
  "author": "Lode Team",
  "license": "MIT",
  "dependencies": {
    "task": "1.0.0"
  }
}
```

The fields have these meanings:

| Field | Owner | Meaning |
| --- | --- | --- |
| `name` | Package manager and release metadata | Canonical package identity. |
| `version` | Package manager and release metadata | SemVer package version. |
| `description`, `author`, `license` | Package manager and release metadata | Package metadata. |
| `repository` | Package manager and release metadata | Canonical source repository for external packages; runtime-owned stdlib modules omit it. |
| `dependencies` | Package manager | Runtime Lode package requirements. |
| `devDependencies` | Package manager/CI | Test and development requirements. |
| `libraries` | Runtime and package manager | Native artifact map and packaging validation. |

Dependency keys are local aliases only. They are not package identities and do
not select a version globally. Resolution and lockfile identity are defined by
RFC 06; native artifact identity is extended with target and ABI information
as defined by RFC 09.

A native package adds `libraries` to the same manifest:

```json
{
  "name": "http",
  "version": "0.1.0",
  "description": "Lode HTTP client module (Async/LibUV)",
  "author": "Lode Team",
  "license": "MIT",
  "libraries": {
    "windows": {
      "x64": "libs/windows/x64/http.dll",
      "arm64": "libs/windows/arm64/http.dll",
      "x86": "libs/windows/x86/http.dll"
    },
    "linux": {
      "x64": "libs/linux/x64/http.so",
      "arm64": "libs/linux/arm64/http.so"
    },
    "macos": {
      "x64": "libs/macos/x64/http.dylib",
      "arm64": "libs/macos/arm64/http.dylib"
    }
  }
}
```

## Native library paths

`libraries.<platform>.<architecture>` contains a relative base path inside the
package. The runtime may insert its build configuration between the
architecture directory and the file name, for example:

```text
libs/windows/x64/Release/module.dll
libs/windows/x64/Debug/module.dll
libs/windows/x64/module.dll
```

The package manager validates that declared paths remain inside the package and
that the artifact exists when creating a native package. The manifest does not
contain CMake link instructions.

Recognized platform and architecture identifiers follow the existing manifests:

```text
Platforms: windows, linux, android, freebsd, macos, ios, solaris, haiku, wasm
Architectures: x64, arm64, x86, wasm32, wasm64
```

Recognition is not a support or release commitment. A release may claim a
target only when its workflow builds, tests, and publishes that target's
artifact. In particular, iOS is an accepted manifest identifier, but static
native-module loading and iOS deployment have not yet been validated; iOS is
not currently a supported release target.

## Explicit non-goals

The initial manifest does not contain `type`, `entry`, `buildDependencies`,
`nativeDependencies`, or `systemDependencies`. `type` and `entry` duplicate
established conventions. C++ libraries such as OpenSSL remain in CMake and the
CI toolchain. A separate native Lode package dependency will only be added
after such a package and its CMake integration actually exist.

## Runtime behavior

The current runtime consumes the existence of `lode.json`, the `libraries`
map, and the package's `init.luau` convention. It does not currently consume
metadata, versions, `dependencies`, or `devDependencies`.

The package manager resolves dependencies and materializes aliases in
`.config.luau`; the runtime continues to use the Luau configuration and
aliases instead of deriving `require("@name")` from the manifest `name`.
