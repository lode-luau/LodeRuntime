export type JSONValue =
    nil
    | boolean
    | number
    | string
    | { [string]: JSONValue }
    | { JSONValue }

--[[
    Native JSON Module.
    Exposes the Lode::Json parse/stringify functionality to Luau.
    
    Usage:
    ```lua
    local json = require("@json")
    
    local tbl = json.parse('{"hello": "world", "score": 42}')
    print(tbl.hello)
    
    local str = json.stringify({ a = 1, b = 2 }, true)
    print(str)
    ```
]]
export type JsonModule = {
    --- Parses a JSON document into a Lua value. Raises a Luau error on invalid input.
    --- @return The parsed Lua value
    parse: (
        --- The JSON string to parse
        text: string,
        --- Optional maximum parsing depth
        maxDepth: number?,
        --- Optional maximum number of nodes
        maxNodes: number?
    ) -> JSONValue,
    
    --- Serializes a Lua value into a JSON document. Raises a Luau error on unsupported values.
    --- @return The JSON string
    stringify: (
        --- The Lua value to serialize
        value: JSONValue,
        --- Optional boolean to enable pretty-printing
        pretty: boolean?,
        --- Optional maximum serialization depth
        maxDepth: number?,
        --- Optional maximum number of nodes
        maxNodes: number?
    ) -> string,
}

export type ModuleMetaInfo = {
    moduleName: string,
    version: string,
}

return {} :: JsonModule, {} :: ModuleMetaInfo
