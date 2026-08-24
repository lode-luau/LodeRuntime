
local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local uuid = require("@uuid")

describe("uuid", function()
    it("exposes the documented API surface", function()
        expect(type(uuid.v4)).toBe("function")
        expect(type(uuid.isV4)).toBe("function")
    end)

    it("generates canonically formatted v4 UUIDs", function()
        local id = uuid.v4()
        expect(#id).toBe(36)
        -- hyphens only at positions 9, 14, 19, 24
        expect(id:sub(9, 9)).toBe("-")
        expect(id:sub(14, 14)).toBe("-")
        expect(id:sub(19, 19)).toBe("-")
        expect(id:sub(24, 24)).toBe("-")
        expect(id:gsub("-", "")).toBeType("string")
        -- version nibble and variant nibble
        expect(id:sub(15, 15)).toBe("4")
        expect(id:match("[89ab]", 20) ~= nil).toBe(true)
    end)

    it("generates unique values across many calls", function()
        local seen = {}
        for _ = 1, 200 do
            local id = uuid.v4()
            expect(seen[id]).toBe(nil)
            seen[id] = true
        end
    end)

    it("isV4 accepts canonical strings in any case", function()
        local id = uuid.v4()
        expect(uuid.isV4(id)).toBe(true)
        expect(uuid.isV4(id:upper())).toBe(true)
    end)

    it("isV4 rejects malformed inputs", function()
        expect(uuid.isV4("")).toBe(false)
        expect(uuid.isV4("not-a-uuid")).toBe(false)
        expect(uuid.isV4("3b241101-e2bb-4255-8caf-4136c566a96")).toBe(false)
        expect(uuid.isV4("3b241101-e2bb-4255-8caf-4136c566a9620")).toBe(false)
        -- wrong version nibble
        expect(uuid.isV4("3b241101-e2bb-5255-8caf-4136c566a962")).toBe(false)
        -- wrong variant nibble
        expect(uuid.isV4("3b241101-e2bb-4255-ccaf-4136c566a962")).toBe(false)
        -- non-hex character
        expect(uuid.isV4("3b241101-e2bb-4255-8caf-4136c566a96g")).toBe(false)
    end)
end)