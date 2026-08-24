
local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local url = require("@url")

describe("url", function()
    it("encodeComponent keeps unreserved characters intact", function()
        local kept = "ABCZabcz0189-_.~"
        expect(url.encodeComponent(kept)).toBe(kept)
    end)

    it("encodeComponent percent-encodes reserved and non-ASCII bytes", function()
        expect(url.encodeComponent("a b")).toBe("a%20b")
        expect(url.encodeComponent("a/b?c=d&e")).toBe("a%2Fb%3Fc%3Dd%26e")
        expect(url.encodeComponent("&=")).toBe("%26%3D")
    end)

    it("decodeComponent decodes escapes and treats + as space", function()
        expect(url.decodeComponent("a%20b%26c")).toBe("a b&c")
        expect(url.decodeComponent("a+b")).toBe("a b")
        expect(url.decodeComponent("%2F")).toBe("/")
    end)

    it("buildQuery handles scalars, booleans, arrays and skips nil/false", function()
        local q = url.buildQuery({
            text = "a b",
            page = 2,
            flag = true,
            skipped = false,
            absent = nil,
            tags = { "x", "y z" },
        })
        -- A ordem de iteracao de tabelas Luau nao e deterministica:
        -- compare como conjunto de pares ordenado.
        local parts = {}
        for pair in q:gmatch("[^&]+") do
            table.insert(parts, pair)
        end
        table.sort(parts)
        expect(table.concat(parts, "&")).toBe("flag&page=2&tags=x&tags=y%20z&text=a%20b")
    end)

    it("parseQuery splits pairs, decodes and groups repeated keys", function()
        local q = url.parseQuery("q=a%20b%26c&page=2&flag&tag=x&tag=y%20z")
        expect(q.q).toBe("a b&c")
        expect(q.page).toBe("2")
        expect(q.flag).toBe(true)
        expect(q.tag).toEqual({ "x", "y z" })
    end)

    it("parseQuery returns an empty table for empty input", function()
        expect(url.parseQuery("")).toEqual({})
    end)

    it("parses a fully-featured URL", function()
        local r = url.parse("https://user@example.com:8443/a/b?x=1#frag")
        expect(r.scheme).toBe("https")
        expect(r.userinfo).toBe("user")
        expect(r.host).toBe("example.com")
        expect(r.port).toBe(8443)
        expect(r.path).toBe("/a/b")
        expect(r.query).toBe("x=1")
        expect(r.fragment).toBe("frag")
    end)

    it("parses URLs without scheme, port, query or fragment", function()
        local r = url.parse("example.com/path")
        expect(r.scheme).toBe("")
        expect(r.host).toBe("example.com")
        expect(r.path).toBe("/path")
        expect(r.port).toBe(nil)
    end)

    it("keeps non-numeric colons in the host", function()
        local r = url.parse("example.com:notaport")
        expect(r.host).toBe("example.com:notaport")
        expect(r.port).toBe(nil)
    end)

    it("formats a record back into a URL (round-trip)", function()
        local original = "https://user@example.com:8443/a/b?x=1#frag"
        expect(url.format(url.parse(original))).toBe(original)
        local minimal = url.format({ scheme = "", userinfo = "", host = "h", port = nil, path = "/p", query = "", fragment = "" })
        expect(minimal).toBe("h/p")
    end)
end)