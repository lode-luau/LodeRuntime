
local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local csv = require("@csv")

describe("csv", function()
    it("encodes simple rows with a trailing CRLF", function()
        local text = csv.encode({ { "a", "b" }, { "c", "d" } })
        expect(text).toBe("a,b\r\nc,d\r\n")
    end)

    it("quotes fields containing the separator, quotes or newlines", function()
        local text = csv.encode({ { "x;y", 'say "hi"', "multi\nline", "plain" } }, ";")
        expect(text).toBe('"x;y";"say ""hi""";"multi\nline";plain\r\n')
    end)

    it("supports custom separators such as tab (TSV)", function()
        local text = csv.encode({ { 1, 2 } }, "\t")
        expect(text).toBe("1\t2\r\n")
    end)

    it("converts numbers and booleans with tostring semantics", function()
        local text = csv.encode({ { 1, 2.5, true, false } })
        expect(text).toBe("1,2.5,true,false\r\n")
    end)

    it("decodes LF-only documents including a final row without newline", function()
        local rows = csv.decode("a,b\nc,d")
        expect(#rows).toBe(2)
        expect(rows[1]).toEqual({ "a", "b" })
        expect(rows[2]).toEqual({ "c", "d" })
    end)

    it("decodes CRLF documents", function()
        local rows = csv.decode("a,b\r\nc,d\r\n")
        expect(#rows).toBe(2)
        expect(rows[2]).toEqual({ "c", "d" })
    end)

    it("keeps empty fields between separators", function()
        local rows = csv.decode("a,,c")
        expect(rows[1]).toEqual({ "a", "", "c" })
    end)

    it("honors quoted fields with embedded separator, newline and doubled quotes", function()
        local rows = csv.decode('"x;y","multi\nline","say ""hi"""')
        expect(#rows).toBe(1)
        expect(rows[1][1]).toBe("x;y")
        expect(rows[1][2]).toBe("multi\nline")
        expect(rows[1][3]).toBe('say "hi"')
    end)

    it("headers mode returns keyed records plus the header row", function()
        local records, header = csv.decode("name,age\nAna,30\nBo,25", { headers = true })
        expect(header).toEqual({ "name", "age" })
        expect(#records).toBe(2)
        expect(records[1].name).toBe("Ana")
        expect(records[1].age).toBe("30")
        expect(records[2].name).toBe("Bo")
    end)

    it("round-trips complex data through encode then decode", function()
        local original = {
            { "id", "text", "note" },
            { "1", "comma, inside", "" },
            { "2", 'quote " and \n newline', "tab\there" },
        }
        local decoded = csv.decode(csv.encode(original))
        expect(#decoded).toBe(3)
        expect(decoded[2][2]).toBe("comma, inside")
        expect(decoded[3][2]).toBe('quote " and \n newline')
        expect(decoded[3][3]).toBe("tab\there")
    end)
end)