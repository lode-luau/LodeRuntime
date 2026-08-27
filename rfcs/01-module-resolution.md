# RFC: Require and Path Resolution for Lode Modules

## Summary

This RFC defines the path resolution and loading mechanism for Lode modules.
Every Lode module has a fixed Luau entry file named `init.luau`. A module may
also have a compiled implementation loaded from a platform-specific dynamic
library. Both implementations share one module identity and one `require` path.

## Motivation

The path-resolution system must follow the Luau require-by-string semantics.
Compiled implementations must load across operating systems and architectures
without changing the Luau require syntax. The same module directory must also
provide an interface that the Luau language server can analyze.

## Design Details

### 1. The `package.luau` Manifest

The package manager identifies a package through `package.luau`. This is a
Lode-owned manifest written in Luau syntax; it is not a module entrypoint and
must not be loaded by `require`. It contains package identity, dependency
requirements, and an optional compiled implementation contract. It does not
contain CMake flags or link instructions.

The manifest never declares a configurable entrypoint. A package module is a
directory containing `init.luau`.

Example:

```luau
return {
    name = "websocket",
    version = "1.0.0",

    dependencies = {
        task = "1.0.0"
    },

    implementation = {
        artifact = "websocket",
        required = true,
        layout = "libs/{platform}/{architecture}/{configuration}/{artifact}{extension}",
        targets = {
            build = { "windows/x64" },
            release = { "windows/x64" },
        },
    }
}
```

A package without `implementation` is a Luau-only module and still follows
the same `init.luau` convention.

The default artifact layout is:

```text
libs/<platform>/<architecture>/<configuration>/<artifact><extension>
```

The allowed substitutions are `platform`, `architecture`, `configuration`,
`artifact`, and `extension`. Paths must be relative to the package root and
must resolve inside it. The default artifact name is the package name unless
`implementation.artifact` supplies another name.

### 2. The Role of `init.luau`

Every Lode module must include `init.luau` in its module directory. It is the
fixed module entry used by Luau and the Lode language tooling.

For a compiled implementation, `init.luau` is an interface file. It provides
types, documentation, and the exported API shape to the LSP. It may end with a
direct typed return:

```luau
export type Client = {
    close: () -> ()
}

export type WebSocket = {
    connect: (url: string) -> Client
}

return {} :: WebSocket
```

The runtime does not execute this file when `implementation` is present. It
first resolves and validates the artifact. When `required = true` (the
publication default), an unavailable artifact for the current target is a
loading error and the runtime does not fall back to `init.luau`. When
`required = false`, the runtime may fall back to `init.luau` for a target that
has no artifact. A module without `implementation` executes `init.luau`
normally.

The Luau resolver and `luau-lsp` continue to see the module directory and its
fixed `init.luau`. The compiled artifact is a Lode runtime concern and is not
an alternative entry file.

The standard entry point exported by a compiled implementation is
`LodeModuleInit`.

Modules built through the Lode CMake may export `LodeModuleConfig`, a
`const char*` C-linkage function returning the build configuration (for
example, `Debug` or `Release`). When present, the runtime compares it with its
own configuration before calling `LodeModuleInit`; a mismatch aborts loading
instead of risking a CRT/STL ABI failure. Modules that omit it are treated as
unverifiable and remain loadable under the legacy compatibility rule.

They may also export `LodeModuleABI()`. New package publication requires this
symbol and the value must match the runtime ABI identifier.

### 3. Compiled Initialization and Registry Path Injection

When a compiled implementation is loaded and `LodeModuleInit` is called, C++
code may require helper Luau scripts distributed with the package. Immediately
before initialization, the runtime injects the module directory into the Lua
registry under `_LODE_NATIVE_MODULE_PATH`.

During initialization, the resolver treats the compiled code as if it were
running from the module's `init.luau` for path-resolution purposes. This keeps
`./` and `@self` behavior identical to Luau code. The registry value is removed
after `LodeModuleInit` returns.

### 4. Relative Path Resolution (`./`) and Internal Resolution (`@self`)

Lode follows Luau's module-path semantics regarding `init.luau`.

If a module is represented by `Utils/init.luau`, `./` resolves according to
the abstract module represented by the directory, while `@self` resolves
inside that module:

```text
project/
├── Module/
│   └── init.luau
└── Utils/
    ├── init.luau
    ├── a.luau
    └── test/
        └── init.luau
```

From `Utils/init.luau`:

```luau
require("@self/test")
require("@self/a")
```

When C++ code calls `vm.Require`, the same rules apply. In particular,
`vm.Require("@self/utils")` resolves helper code inside the current module.

### 5. Module Results

A module's public contract is one module result. A compiled implementation
returns its API through `LodeModuleInit`; its `init.luau` only describes that
API. Luau-only modules execute `init.luau` and should return one module value.
Lode may preserve multiple VM return values internally for compatibility with
the generic require boundary, but package modules must not use multiple return
values as separate entrypoints.

### 6. Top-Level Yield and Load Timeout

When a Luau-only module's top-level code yields because of an asynchronous
operation, the loader pumps the event loop for up to five seconds. A timeout
or a module that finishes without a result fails loudly. Compiled
implementations do not execute `init.luau`, so this rule applies only to the
Luau implementation path.

Top-level asynchronous work should remain inside functions called after load.
