# RFC: Luau Configuration Files Support

## Summary
This RFC details Lode's support for Luau configuration files (`.luaurc` and `.config.luau`). It ensures that Lode fully complies with Luau's official configuration specifications, allowing developers to customize analysis, type-checking, and runtime behavior using standard Luau conventions.

## Motivation
To maintain 100% compatibility with the broader Luau ecosystem, Lode must support how Luau configures its environments. Luau officially supports two types of configuration files:
1. **`.luaurc`**: A JSON-like configuration file.
2. **`.config.luau`**: A newer configuration file format that uses Luau syntax, allowing for better type-checking and autocomplete support inside the configuration itself.

By supporting both natively within Lode's `require` and compiler pipeline, developers can seamlessly port existing Luau projects into Lode without needing to translate or change their configuration setups. Both files will function correctly and exactly as they do in standard Luau environments.

## Design Details

### 1. Supported Formats and Schemas
Lode's `ModuleLoader` seamlessly supports both formats. Below is the blueprint and structure for each file type so users know exactly what keys and options are available.

#### `.config.luau`
The `.config.luau` file must return a table that adheres to the following Luau type signature:

```luau
type LanguageMode = "strict" | "nonstrict" | "nocheck"

type LintWarning =
    "*" | "UnknownGlobal" | "DeprecatedGlobal" | "GlobalUsedAsLocal"
    | "LocalShadow" | "SameLineStatement" | "MultiLineStatement"
    | "LocalUnused" | "FunctionUnused" | "ImportUnused"
    | "BuiltinGlobalWrite" | "PlaceholderRead" | "UnreachableCode"
    | "UnknownType" | "ForRange" | "UnbalancedAssignment"
    | "ImplicitReturn" | "DuplicateLocal" | "FormatString"
    | "TableLiteral" | "UninitializedLocal" | "DuplicateFunction"
    | "DeprecatedApi" | "TableOperations" | "DuplicateCondition"
    | "MisleadingAndOr" | "CommentDirective" | "IntegerParsing"
    | "ComparisonPrecedence" | "RedundantNativeAttribute"

type LuauConfig = {
    languagemode: LanguageMode?,
    lint: { [LintWarning]: boolean }?,
    linterrors: boolean?,
    typeerrors: boolean?,
    globals: { string }?,
    aliases: { [string]: string }?,
}

type Config = {
    luau: LuauConfig?
}
```

**Example `.config.luau`:**
```luau
return {
    luau = {
        languagemode = "nonstrict",
        lint = {
            ["*"] = true,
            LocalUnused = false
        },
        linterrors = true,
        typeerrors = true,
        globals = {"expect"},
        aliases = {
            src = "./src"
        }
    }
}
```

#### `.luaurc`
The `.luaurc` file is a JSON5 format file (meaning it supports comments and trailing commas). It defines the exact same fields as the `.config.luau` format but uses JSON syntax.

**Example `.luaurc`:**
```json
{
    "languageMode": "nonstrict",
    "lint": { 
        "*": true, 
        "LocalUnused": false 
    },
    "lintErrors": true,
    "typeErrors": true,
    "globals": ["expect"],
    "aliases": {
        "src": "./src"
    }
}
```

### 2. Search Semantics
The search semantics for these configuration files mirror Luau's official behavior. When compiling or running a script, Lode will search for configuration files starting from the directory containing the script. All files found in the ancestry chain are evaluated, with configuration files closer to the script overriding those in higher-level directories.

### 3. Resolving Ambiguity
To prevent confusing overrides and maintain strict predictability, Lode explicitly checks for the presence of both files in the same directory. 

If a directory contains both a `.luaurc` and a `.config.luau` file, Lode's `get_config_status` hook will return `CONFIG_AMBIGUOUS`. This will cause the runtime to throw an error, forcing the developer to choose only one configuration format per directory.

### 4. Integration with `luarequire`
Lode implements Luau's internal C API hooks (`luarequire_Configuration`) to provide the configuration file contents directly to the compiler. 
- The `get_config_status` callback checks the filesystem for `.config.luau` and `.luaurc`.
- The `get_config` callback reads the content of the selected file and passes it to the Luau compiler, ensuring that all linting rules, type-checking modes (`strict`, `nonstrict`), and global variables are correctly applied during the bytecode generation phase.
