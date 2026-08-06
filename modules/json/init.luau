-- Native JSON Module: Type definitions for Luau LSP & Autocomplete
-- Exposes the Lode::Json parse/stringify functionality to Luau.

export type JSONValue =
    nil
    | boolean
    | number
    | string
    | { [string]: JSONValue }
    | { JSONValue }

export type JsonModule = {
    -- Parses a JSON document into a Lua value. Raises a Luau error on invalid input.
    parse: (text: string, maxDepth: number?, maxNodes: number?) -> JSONValue,
    -- Serializes a Lua value into a JSON document. Raises a Luau error on unsupported values.
    stringify: (value: JSONValue, pretty: boolean?, maxDepth: number?, maxNodes: number?) -> string,
}

export type ModuleMetaInfo = {
    moduleName: string,
    version: string,
}

return {} :: JsonModule, {} :: ModuleMetaInfo
