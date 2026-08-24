
local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local toml = require("../../../modules/toml")

describe("toml", function()
    it("exposes encode, decode, serialize, deserialize functions", function()
        expect(typeof(toml.encode)).toBe("function")
        expect(typeof(toml.decode)).toBe("function")
        expect(typeof(toml.serialize)).toBe("function")
        expect(typeof(toml.deserialize)).toBe("function")
    end)

    it("serializes and deserializes primitive values", function()
        local data = {
            title = "TOML Example",
            count = 42,
            pi = 3.14159,
            enabled = true,
            disabled = false,
        }

        local encoded = toml.encode(data)
        expect(typeof(encoded)).toBe("string")
        expect(encoded:find('title = "TOML Example"', 1, true) ~= nil).toBe(true)
        expect(encoded:find("count = 42", 1, true) ~= nil).toBe(true)
        expect(encoded:find("enabled = true", 1, true) ~= nil).toBe(true)
        expect(encoded:find("disabled = false", 1, true) ~= nil).toBe(true)

        local decoded = toml.decode(encoded)
        expect(decoded.title).toBe("TOML Example")
        expect(decoded.count).toBe(42)
        expect(decoded.enabled).toBe(true)
        expect(decoded.disabled).toBe(false)
    end)

    it("serializes and deserializes arrays of primitives", function()
        local data = {
            numbers = { 1, 2, 3, 4 },
            tags = { "luau", "runtime", "lode" },
            flags = { true, false, true },
        }

        local encoded = toml.encode(data)
        expect(typeof(encoded)).toBe("string")

        local decoded = toml.decode(encoded)
        expect(typeof(decoded.numbers)).toBe("table")
        expect(#(decoded.numbers :: any)).toBe(4)
        expect((decoded.numbers :: any)[1]).toBe(1)
        expect((decoded.numbers :: any)[4]).toBe(4)
        expect((decoded.tags :: any)[2]).toBe("runtime")
        expect((decoded.flags :: any)[1]).toBe(true)
        expect((decoded.flags :: any)[2]).toBe(false)
    end)

    it("serializes and deserializes nested tables and sections", function()
        local data = {
            server = {
                host = "127.0.0.1",
                port = 8080,
            },
            database = {
                enabled = true,
                connection = {
                    timeout = 30,
                }
            }
        }

        local encoded = toml.encode(data)
        expect(typeof(encoded)).toBe("string")
        expect(encoded:find("%[server%]") ~= nil).toBe(true)

        local decoded = toml.decode(encoded)
        expect((decoded.server :: any).host).toBe("127.0.0.1")
        expect((decoded.server :: any).port).toBe(8080)
        expect((decoded.database :: any).enabled).toBe(true)
        expect(((decoded.database :: any).connection :: any).timeout).toBe(30)
    end)

    it("serializes and deserializes array of tables", function()
        local data = {
            products = {
                { name = "Hammer", sku = 738594937 },
                { name = "Nail", sku = 284758302 },
            }
        }

        local encoded = toml.encode(data)
        expect(typeof(encoded)).toBe("string")
        expect(encoded:find("%[%[products%]%]") ~= nil).toBe(true)

        local decoded = toml.decode(encoded)
        expect(typeof(decoded.products)).toBe("table")
        expect(#(decoded.products :: any)).toBe(2)
        expect((decoded.products :: any)[1].name).toBe("Hammer")
        expect((decoded.products :: any)[1].sku).toBe(738594937)
        expect((decoded.products :: any)[2].name).toBe("Nail")
    end)

    it("escapes special characters in strings", function()
        local data = {
            escaped = 'Line 1\nLine 2\t"Quotes" and \\Backslash\\',
        }

        local encoded = toml.encode(data)
        local decoded = toml.decode(encoded)
        expect(decoded.escaped).toBe('Line 1\nLine 2\t"Quotes" and \\Backslash\\')
    end)
end)
