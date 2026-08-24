
local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local binary = require("@binary")

describe("binary", function()
    it("exposes the documented API surface", function()
        expect(type(binary.reader)).toBe("function")
        expect(type(binary.writer)).toBe("function")
    end)

    it("round-trips u8 and i8 including negative values", function()
        local w = binary.writer()
        w:u8(0):u8(255):u8(127)
        w:u8(0):u8(128):u8(255)
        local r = binary.reader(w:bytes())
        expect(r:u8()).toBe(0)
        expect(r:u8()).toBe(255)
        expect(r:u8()).toBe(127)
        expect(r:i8()).toBe(0)
        expect(r:i8()).toBe(-128)
        expect(r:i8()).toBe(-1)
    end)

    it("round-trips u16 in both endiannesses", function()
        for _, e in ipairs({ "le", "be" }) do
            for _, v in ipairs({ 0, 1, 65500, 65535 }) do
                local w = binary.writer()
                w:u16(v, e)
                local bytes = w:bytes()
                expect(buffer.len(bytes)).toBe(2)
                if e == "le" then
                    expect(buffer.readu8(bytes, 0)).toBe(v % 0x100)
                    expect(buffer.readu8(bytes, 1)).toBe(v // 0x100 % 0x100)
                else
                    expect(buffer.readu8(bytes, 0)).toBe(v // 0x100 % 0x100)
                    expect(buffer.readu8(bytes, 1)).toBe(v % 0x100)
                end
                expect(binary.reader(bytes):u16(e)).toBe(v)
            end
        end
    end)

    it("round-trips u32 in both endiannesses", function()
        for _, e in ipairs({ "le", "be" }) do
            for _, v in ipairs({ 0, 1, 4000000000, 4294967295 }) do
                local w = binary.writer()
                w:u32(v, e)
                expect(buffer.len(w:bytes())).toBe(4)
                expect(binary.reader(w:bytes()):u32(e)).toBe(v)
            end
        end
    end)

    it("round-trips f32 and f64 approximately", function()
        local w = binary.writer()
        w:f32(0.5):f64(3.14159)
        local r = binary.reader(w:bytes())
        expect(r:f32()).toBe(0.5)
        expect(math.abs(r:f64() - 3.14159) < 1e-12).toBe(true)
        -- default byte order is little-endian: 0.5f == 00 00 00 3F
        local le = binary.writer()
        le:f32(0.5)
        expect(buffer.readu8(le:bytes(), 3)).toBe(0x3F)
    end)

    it("encodes varint boundaries correctly", function()
        local values = { 0, 1, 127, 128, 300, 16383, 16384, 2147483647 }
        for _, v in values  do
            local w = binary.writer()
            w:varint(v)
            expect(binary.reader(w:bytes()):varint()).toBe(v)
        end
        -- known encodings
        local one = binary.writer(); one:varint(0)
        expect(buffer.len(one:bytes())).toBe(1)
        local two = binary.writer(); two:varint(128)
        expect(buffer.len(two:bytes())).toBe(2)
        expect(buffer.readu8(two:bytes(), 0)).toBe(0x80)
        expect(buffer.readu8(two:bytes(), 1)).toBe(0x01)
    end)

    it("rejects negative and fractional varints", function()
        expect(pcall(function() binary.writer():varint(-1) end)).toBe(false)
        expect(pcall(function() binary.writer():varint(1.5) end)).toBe(false)
    end)

    it("frames strings with ULEB128 length prefix (UTF-8 safe)", function()
        local text = "olá mundo — çãé"
        local w = binary.writer()
        w:string(text)
        local bytes = w:bytes()
        -- payload é maior que o texto por causa dos bytes do prefixo
        expect(buffer.len(bytes) > #text).toBe(true)
        local r = binary.reader(bytes)
        expect(r:string()).toBe(text)
        expect(r:remaining()).toBe(0)
    end)

    it("grows the writer beyond its initial capacity", function()
        local big = string.rep("x", 500)
        local w = binary.writer()
        w:string(big):string(big):string(big)
        local r = binary.reader(w:bytes())
        expect(r:string()).toBe(big)
        expect(r:string()).toBe(big)
        expect(r:string()).toBe(big)
    end)

    it("supports reader offset and seek/remaining", function()
        local w = binary.writer()
        w:u8(1):u16(2):u32(3)
        local bytes = w:bytes()
        local r = binary.reader(bytes)
        r:u8()
        expect(r:seek()).toBe(1)
        expect(r:remaining()).toBe(buffer.len(bytes) - 1)
        local r2 = binary.reader(bytes, 1)
        expect(r2:u16()).toBe(2)
        expect(r2:remaining()).toBe(4)
    end)

    it("errors on truncated reads instead of returning garbage", function()
        local w = binary.writer()
        w:string("abc")
        local bytes = w:bytes()
        local cut = buffer.create(buffer.len(bytes) - 1)
        buffer.copy(cut, 0, bytes, 0, buffer.len(bytes) - 1)
        expect(pcall(function()
            return binary.reader(cut):string()
        end)).toBe(false)
        expect(pcall(function()
            return binary.reader(buffer.create(1)):u32()
        end)).toBe(false)
    end)

    it("chains writer calls fluently", function()
        local w = binary.writer()
        local returned = w:u8(1):u16(2):string("x")
        expect(returned).toBe(w)
        expect(w:bytes()).toBeType("buffer")
    end)
end)
describe("binary aliases", function()
    it("keeps readString/writeString as documented aliases", function()
        local w = binary.writer()
        expect(type(w.writeString)).toBe("function")
        w:writeString("alias")
        local r = binary.reader(w:bytes())
        expect(type(r.readString)).toBe("function")
        expect(r:readString()).toBe("alias")
    end)
end)
