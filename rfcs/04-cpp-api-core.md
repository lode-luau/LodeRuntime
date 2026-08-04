# RFC: Core C++ API (`Lode::State`, `Value`, `Table`)

## Summary
This RFC documents the core data types and execution environment wrappers provided by the `LodeRuntime` C++ API (`include/Lode/`). It details how to interact with the Luau Virtual Machine (`Lode::State`), manipulate data without manual stack operations (`Lode::Value`, `Lode::Table`), and bind C++ functions directly to Luau.

## Motivation
Interacting with the standard Lua C API (`lua_State*`, `lua_push*`, `lua_pop*`) is error-prone, verbose, and difficult to maintain. Lode provides a modern, type-safe C++20 layer that abstracts stack manipulations away, allowing developers to write zero-overhead native plugins that feel like standard C++.

## Core Abstractions

### 1. `Lode::State`
The `State` class is the central orchestrator. It wraps the raw `lua_State*` and provides high-level utilities for VM execution.

**Key Responsibilities:**
- **Creating Data:** `vm.CreateTable()`, `vm.CreateBuffer(size)`
- **Binding Functions:** `vm.CreateFastFunction(callback)`
- **Execution:** `vm.Require(path)`, `vm.CallFunction(callable, args)`
- **Threading:** `vm.YieldThread()`, `vm.ResumeThread(args)`

### 2. `Lode::Value`
`Lode::Value` is a type-safe wrapper representing any Luau value (String, Number, Boolean, Table, Function, Userdata, Nil). It completely abstracts stack indexing.

**Usage:**
```cpp
// Creation
Lode::Value strVal("Hello World");
Lode::Value numVal(42.5);
Lode::Value boolVal(true);

// Type Checking
if (val.IsString()) { ... }
if (val.IsNumber()) { ... }

// Extraction
std::string str = strVal.AsString();
std::string_view sv = strVal.AsStringView(); // Zero-copy string access
double num = numVal.AsNumber();
bool b = boolVal.AsBoolean();
```

### 3. `Lode::Table`
A specialized wrapper around Luau tables. It provides `Get` and `Set` semantics that feel like a standard C++ map.

**Usage:**
```cpp
Lode::Table exports = vm.CreateTable();

// Setting fields
exports.Set("version", Lode::Value("1.0.0"));
exports.Set(1, Lode::Value("Array Item"));

// Getting fields
Lode::Value v = exports.Get("version");
```

## Creating Native Functions (`CreateFastFunction`)

Native functions in Lode are implemented using `CreateFastFunction`. Instead of manually pulling arguments from the Lua stack, the callback receives a `Lode::StackArgs` object, which behaves like an array of `Lode::Value`s.

```cpp
exports.Set("greet", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
    // 1. Safely validate and extract arguments
    std::string_view name = "World";
    if (args.Size() > 0 && args[0].IsString()) {
        name = args[0].AsStringView();
    }

    // 2. Perform C++ logic
    std::string result = "Hello, " + std::string(name) + "!";

    // 3. Return a Value natively
    return Lode::Value(result);
}));
```

## Calling Luau from C++ (`vm.Require` and `vm.CallFunction`)
Often, C++ modules distribute helper Luau scripts (`.luau`) inside their package. You can require and execute them directly from C++.

```cpp
// 1. Require a Luau file distributed alongside your native module DLL
// The `@self` prefix ensures it resolves relative to the module's root directory.
Lode::Value factoryFn = vm.Require("@self/utils/formatter");

// 2. Call the loaded function from C++
// The second argument is a std::vector<Lode::Value> containing the function arguments.
Lode::Result<std::vector<Lode::Value>> result = vm.CallFunction(factoryFn, { 
    Lode::Value("Test"), 
    Lode::Value(123) 
});

// 3. Handle the result
if (result.IsOk()) {
    std::vector<Lode::Value> returnedArgs = result.GetValue();
    // Use returnedArgs[0], returnedArgs[1], etc.
}
```

## Building the Module Entry Point
Every dynamic native module must export a standard C-linkage initialization function (`LodeModuleInit`). Lode simplifies this with the `LODE_MODULE(vm)` macro.

```cpp
#include "Lode/Module.hpp"
#include "Lode/State.hpp"
#include "Lode/Table.hpp"

LODE_MODULE(vm)
{
    Lode::Table exports = vm.CreateTable();
    exports.Set("myConst", Lode::Value(100.0));
    
    // The braced initialization allows returning multiple Values back to Luau.
    return { exports };
}
```

## Putting it All Together: A Complete Example

To build a robust native module, you must provide both the C++ implementation (`src/main.cpp`) and a Luau type definition file (`init.luau`) so that the Language Server Protocol (LSP) can provide autocompletion.

### 1. The C++ Implementation (`src/main.cpp`)
```cpp
#include "Lode/Module.hpp"
#include "Lode/State.hpp"
#include "Lode/Table.hpp"
#include "Lode/Value.hpp"
#include <string>

LODE_MODULE(vm)
{
    Lode::Table exports = vm.CreateTable();

    exports.Set("greet", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        std::string_view name = (args.Size() > 0 && args[0].IsString()) ? args[0].AsStringView() : "World";
        return Lode::Value("Hello, " + std::string(name) + "!");
    }));

    exports.Set("square", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        double x = (args.Size() > 0 && args[0].IsNumber()) ? args[0].AsNumber() : 0.0;
        return Lode::Value(x * x);
    }));

    return { exports };
}
```

### 2. The Type Definitions (`init.luau`)
This file is placed at the root of your module folder (next to `lode.json`). When the runtime loads your native DLL, this file is ignored by the VM, but the Luau LSP uses it to type-check scripts that `require` your module.

```luau
-- Type definitions for LSP and Autocomplete
-- The runtime will load the DLL directly, but tools will read this file.

export type NativeModule = {
    -- Returns a greeting string. If no name is provided, defaults to "World".
    greet: (name: string?) -> string,
    
    -- Returns the square of a number.
    square: (x: number) -> number,
}

-- Return an empty table casted to our type so the LSP understands the export shape
return {} :: NativeModule
```
