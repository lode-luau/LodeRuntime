# RFC: Require and Path Resolution for Native Modules

## Summary
This RFC defines the path resolution and loading mechanism for Lode native modules (plugins created using the Lode API). It introduces a structured way to load platform-specific dynamic libraries, establishes how native modules resolve internal paths, and exposes type definitions to align with Luau's standard require semantics.

## Motivation
The primary motivation for this path resolution system is to align correctly with the Luau **require-by-string** RFCs. Native modules require a standard way to be loaded across different platforms (Windows, Linux, macOS) and architectures (x64, arm64, x86) without cluttering the require syntax. 

Furthermore, we need a mechanism to provide type information for the Language Server Protocol (LSP) and to serve as an anchor point for path resolution when native modules themselves require other Luau modules from their C++ initialization code.

## Design Details

### 1. The `lode.json` Manifest
To load a native module, the runtime searches the module's directory (or traverses upwards) for a `lode.json` file. This manifest uses the `libraries` field to specify the relative paths (from the directory containing `lode.json`) to the dynamic libraries (`.dll`, `.so`, `.dylib`). The runtime will automatically select and load the correct library based on the current operating system and architecture.

The path given in `libraries.<platform>.<arch>` is the *base* path. When the loading runtime knows its own build configuration, the loader first looks for the config-aware copy at `libs/<platform>/<arch>/<config>/<name>` (the same path with a `<config>` subdirectory inserted before the file name). It falls back to the base path when no config subdirectory exists, so third-party or prebuilt modules that ship flat library trees keep working unchanged. Loading a config-aware copy guarantees the runtime and the module were compiled with the same CRT/STL build (Debug vs Release), avoiding ABI mismatches across the DLL boundary.

**Example `lode.json`:**
```json
{
  "name": "lib",
  "version": "1.0.0",
  "description": "Lode test lib module",
  "author": "Lode Team",
  "license": "MIT",
  "libraries": {
    "windows": {
      "x64": "libs/windows/x64/lib.dll",
      "arm64": "libs/windows/arm64/lib.dll",
      "x86": "libs/windows/x86/lib.dll"
    },
    "linux": {
      "x64": "libs/linux/x64/lib.so",
      "arm64": "libs/linux/arm64/lib.so"
    },
    "macos": {
      "x64": "libs/macos/x64/lib.dylib",
      "arm64": "libs/macos/arm64/lib.dylib"
    }
  }
}
```

The standard entry point exported by the dynamic library must be `LodeModuleInit`.

Additionally, modules built through the Lode CMake may export `LodeModuleConfig`, a `const char*` C-linkage function returning the build configuration the module was compiled with (e.g. `"Debug"` or `"Release"`). When the loaded module exports it, the runtime compares the value with its own configuration before calling `LodeModuleInit`; a mismatch aborts the load with a named error instead of risking a cross-boundary CRT/STL ABI crash. Modules that omit `LodeModuleConfig` are treated as unverifiable and load as before.

### 2. The Role of `init.luau` in Native Modules
Native modules must include an `init.luau` file alongside the `lode.json` or in the root of the module package. This file serves two critical purposes:

1. **Type Definitions:** It provides type exports and function signatures for the LSP, ensuring developers have autocompletion and type checking when consuming the native module.
2. **Package Root Indicator:** It (along with `lode.json`) marks the root of the package for internal path resolution.

**Important Note:** For modules that contain a `lode.json` with a non-empty `libraries` field, the runtime **always ignores and does not execute** the `init.luau` file, even if the current platform/architecture has no entry in the map (`src/ModuleLoader.cpp:498`). It is strictly used for LSP tooling and as a spatial reference for path resolution; a missing native artifact is reported as a platform error rather than silently falling back to Luau.

### 3. Registry Path Injection for Native Initialization
When a native module is loaded and `LodeModuleInit` is called, the C++ code might need to call `require` to load helper Luau scripts distributed with the plugin. To ensure these `require` calls resolve correctly:

1. Right before calling `LodeModuleInit`, the runtime injects the native module's directory path into the Lua registry under the key `_LODE_NATIVE_MODULE_PATH`.
2. When a `require` call is made during initialization, the runtime checks for this registry key. If present, it treats the "caller" chunkname as `@<native_module_path>/init.luau`. 
3. This effectively tricks the resolver into treating the native initialization code as if it were running inside the module's `init.luau`, ensuring relative paths (e.g., `./utils`) resolve correctly against the module's root directory.
4. After `LodeModuleInit` returns, the runtime removes `_LODE_NATIVE_MODULE_PATH` from the registry to prevent it from leaking into unrelated `require` calls.

### 4. Relative Path Resolution (`./`) vs Internal Resolution (`@self`)

Lode strictly follows standard Luau package resolution semantics regarding `init.luau`. 

If a script is named `init.luau`, the runtime considers the directory *containing* the package folder as the base for relative (`./`) require paths, not the package folder itself. 

**Example Directory Structure:**
```text
project/
├── Module/
│   └── init.luau
├── Utils/
│   ├── init.luau
│   ├── test/
│   │   └── init.luau
│   └── a.luau
└── foo.luau
```

**Relative Resolution (`./`)**
If you are inside `Utils/init.luau` and you write `require("./Module")`, the runtime resolves this from the directory above `Utils/`. It will correctly find `project/Module/init.luau` because `Utils` and `Module` are siblings in the `project/` directory.

**Internal Resolution (`@self`)**
If `Utils/init.luau` needs to require a file *inside* its own package (like `test/init.luau` or `a.luau`), using a relative path like `./a` would fail (because it would look for `project/a.luau`). 

To resolve files inside its own package directory, it must use the `@self` alias (or `self`):
- `require("@self/test")` -> resolves to `Utils/test/init.luau`
- `require("@self/a")` -> resolves to `Utils/a.luau` (the `.luau` extension is automatically appended by the resolver).

#### Native Module Integration with `vm.Require()`
When developing native plugins in C++, you can load other Luau scripts or modules using the `vm.Require()` API. Thanks to the Registry Path Injection mentioned earlier, the runtime treats the C++ dynamic library exactly as if it were an `init.luau` file belonging to the module.

This guarantees that native C++ code uses the exact same resolution rules as standard Luau code. Crucially, if a module is not found, `vm.Require()` directly throws a Lua error—mimicking the exact behavior of a Luau `require()` call and eliminating the need for manual `Result` unwrapping in C++.

**Practical Examples from C++:**
- **`vm.Require("./sibling_module")`**: Resolves to one level *above* the native module's package folder. This is useful when your C++ code needs to load other modules that exist in the broader project workspace.
- **`vm.Require("@self/utils")`**: Uses the internal resolution alias to correctly look *inside* the native module's own folder (the directory containing `lode.json`). This is the recommended way to load helper Luau scripts (`utils/init.luau` or `utils.luau`) that are distributed alongside your native `.dll`/`.so`.

### 5. Multi-Return Support
The Lode require implementation supports multiple return values from modules, storing the resulting tuples in a custom registry cache (`_LODE_MULTI_CACHE`). This allows `require` to properly forward all results from the executed module script.