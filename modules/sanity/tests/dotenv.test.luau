
local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local fs = require("@filesystem")
local sys = require("@sys")
local path = require("@filesystem/path")
local dotenv = require("@dotenv")

describe("dotenv", function()
    it("exposes the documented API surface", function()
        expect(type(dotenv.parse)).toBe("function")
        expect(type(dotenv.load)).toBe("function")
        expect(type(dotenv.apply)).toBe("function")
    end)

    it("parses plain values, comments and export prefixes", function()
        local doc = table.concat({
            "# full line comment",
            "PLAIN=value",
            "export GREETING=hello world",
            "",
            "   # indented comment",
        }, "\n")
        local parsed = dotenv.parse(doc)
        expect(parsed.PLAIN).toBe("value")
        expect(parsed.GREETING).toBe("hello world")
    end)

    it("strips inline comments only for unquoted values", function()
        local doc = table.concat({
            "A=1 # trailing comment",
            'B="keep # this"',
            "C=spaces   # another",
        }, "\n")
        local parsed = dotenv.parse(doc)
        expect(parsed.A).toBe("1")
        expect(parsed.B).toBe("keep # this")
        expect(parsed.C).toBe("spaces")
    end)

    it("supports single quotes and empty values", function()
        local doc = table.concat({
            [[S='single quoted']],
            [[EMPTY=]],
            [[EQ=a=b=c]],
        }, "\n")
        local parsed = dotenv.parse(doc)
        expect(parsed.S).toBe("single quoted")
        expect(parsed.EMPTY).toBe("")
        -- split happens on the first '=' only
        expect(parsed.EQ).toBe("a=b=c")
    end)

    it("handles CRLF documents and skips invalid lines", function()
        local doc = "A=1\r\nno equals sign here\r\n\r\nB=2\r\n=missingkey"
        local parsed = dotenv.parse(doc)
        expect(parsed.A).toBe("1")
        expect(parsed.B).toBe("2")
        expect(parsed["no equals sign here"]).toBe(nil)
    end)

    it("load returns an empty table for missing files", function()
        local loaded = dotenv.load("definitely/missing/.env-file")
        expect(loaded).toEqual({})
    end)

    it("load reads a real file from disk", function()
        local target = path.join(path.resolve("@self"), "dotenv_probe.env")
        pcall(fs.RemoveFile, target)
        fs.WriteFile(target, "LODE_DOTENV_PROBE=works\n# done\n")
        local loaded = dotenv.load(target)
        pcall(fs.RemoveFile, target)
        expect(loaded.LODE_DOTENV_PROBE).toBe("works")
    end)

    it("apply respects the overwrite flag and cleans up", function()
        local key = "LODE_DOTENV_APPLY_PROBE"
        local target = path.join(path.resolve("@self"), "dotenv_apply_probe.env")
        pcall(fs.RemoveFile, target)
        sys.SetEnv(key, "original")
        fs.WriteFile(target, key .. "=from-file\nDOTENV_OTHER=1\n")
        -- overwrite=false keeps the existing value
        dotenv.apply(target, false)
        expect(sys.GetEnv(key)).toBe("original")
        -- overwrite=true replaces it
        local applied = dotenv.apply(target, true)
        expect(sys.GetEnv(key)).toBe("from-file")
        expect(applied).toBe(2)
        -- cleanup
        sys.UnsetEnv(key)
        sys.UnsetEnv("DOTENV_OTHER")
        pcall(fs.RemoveFile, target)
    end)
end)