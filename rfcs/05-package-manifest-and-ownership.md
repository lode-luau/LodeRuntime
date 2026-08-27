# RFC: Lode Package Manifest and Field Ownership

## Status

Accepted design direction for the `package.luau` migration. Implementation is
not part of this RFC.

## Summary

This RFC defines the canonical `package.luau` contract and assigns each field
to the component that consumes it. The manifest describes one Lode package
whose root module always uses `init.luau`; it does not replace CMake,
`.config.luau`, the runtime loader, or CI.

The same manifest shape may be used for a project root and for a distributable
package. There is no `type`, `entry`, or `modules` field.

## Package conventions

The package root is identified by `package.luau` and/or `init.luau`. A package
is a module package: its root module is the directory containing `init.luau`.
Submodules are ordinary child directories containing their own `init.luau`;
they are not listed in the package manifest.

Every package module has exactly one logical Luau entry, `init.luau`. If the
manifest declares `implementation`, the file is the LSP/type interface and is
not executed by the runtime. The runtime loads the matching compiled artifact
instead. If `implementation` is absent, the runtime executes `init.luau`.

## Project initialization

`lode init <name> --description <text> [--version <semver>] [--license <SPDX>]
[project-root]` creates the canonical project files in an absent or empty
directory. `name` and `description` are required; version and license default
to `0.1.0` and `MIT`. The template creates `package.luau`, `init.luau`,
`LICENSE`, and `README.md`. A module that has a compiled implementation also
gets its CMake project and source template.

Host detection may select an initial build target, but it does not create a
cross-platform release promise. A non-empty directory is refused without
overwriting files.

## Canonical manifest

The package manifest is a Luau table and must be statically representable. The
package manager may parse it with Luau tooling, but must not execute arbitrary
package code while resolving dependencies.

```luau
return {
    name = "signal",
    version = "1.0.0",
    description = "Lode signal module",
    author = "Lode Team",
    license = "MIT",

    dependencies = {
        task = "1.0.0"
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
| `implementation` | Runtime, package manager, CMake integration, and CI | Optional compiled implementation contract for the root module. |

Dependency keys are local aliases only. They are not package identities and do
not select a version globally. Resolution and lockfile identity are defined by
RFC 06; a compiled artifact extends that identity with target and ABI
information as defined by RFC 09.

## Compiled implementation contract

The optional `implementation` table does not create a second package type. It
describes the implementation of the package's one root module:

```luau
implementation = {
    artifact = "http",
    required = true,
    layout = "libs/{platform}/{architecture}/{configuration}/{artifact}{extension}",

    targets = {
        build = {
            "windows/x64",
            "linux/x64",
            "macos/arm64"
        },

        release = {
            "windows/x64"
        }
    }
}
```

`artifact` defaults to the package name. `layout` defaults to:

```text
libs/<platform>/<architecture>/<configuration>/<artifact><extension>
```

The allowed substitutions are `platform`, `architecture`, `configuration`,
`artifact`, and `extension`. Paths must be relative to the package root and
must resolve inside it. `required = true` means that a matching artifact must
exist for the current runtime target; the runtime must not fall back to the
type-only `init.luau`.

`targets.build` lists targets that the package can build and test.
`targets.release` lists targets authorized for artifact publication. A release
target is valid only after its implementation has been built, tested, and
validated by CI.

The package has no configurable `entry` field. `init.luau` is always the Luau
entry and type interface, and child modules use the standard directory
`init.luau` convention.

## Recognized targets

The recognized platform and architecture identifiers are:

```text
Platforms: windows, linux, android, freebsd, macos, ios, solaris, haiku, wasm
Architectures: x64, arm64, x86, wasm32, wasm64
```

Recognition alone is not a support claim. Each target requires an available
toolchain, a real build, runtime tests, and artifact validation. iOS remains
outside the release contract until static loading, signing, and packaging are
implemented and tested.

## Explicit non-goals

The package manifest does not contain `type`, `entry`, `modules`,
`buildDependencies`, `nativeDependencies`, or `systemDependencies`. CMake and
the CI toolchain own compiler flags, link instructions, OpenSSL, and system
libraries. The `implementation` table describes runtime artifact selection,
not compilation instructions.

## Runtime and tooling ownership

The runtime uses `package.luau` to determine whether the root module has a
compiled implementation and to resolve its standard artifact path. It always
uses `init.luau` as the Luau module interface and never executes that file when
the required compiled artifact is selected.

The package manager resolves metadata and dependencies and materializes aliases
in `.config.luau`. The runtime and `luau-lsp` use those aliases; neither derives
an alias from the package name without an explicit configuration entry.

The package manifest is not a replacement for `.config.luau`. The latter
remains the Luau compiler/LSP configuration and alias file.
