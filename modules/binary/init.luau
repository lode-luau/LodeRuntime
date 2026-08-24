--!strict

-- SPDX-License-Identifier: MIT

-- BinaryReader / BinaryWriter over Luau buffers.
-- Fixed-width integer reads/writes take an explicit endianness ("le"|"be");
-- floats are little-endian (the buffer library default). Varints are ULEB128
-- and strings are ULEB128 length-prefixed.
--
-- Hot paths carry the @native attribute so the codegen compiles them even
-- under default flags.

export type Endianness = "le" | "be"

export type Reader = {
	buf: buffer,
	pos: number,
	seek: (self: any) -> number,
	remaining: (self: any) -> number,
	u8: (self: any) -> number,
	i8: (self: any) -> number,
	u16: (self: any, e: Endianness?) -> number,
	u32: (self: any, e: Endianness?) -> number,
	f32: (self: any) -> number,
	f64: (self: any) -> number,
	varint: (self: any) -> number,
	readString: (self: any) -> string,
	string: (self: any) -> string,
}

export type Writer = {
	buf: buffer,
	pos: number,
	bytes: (self: any) -> buffer,
	u8: (self: any, v: number) -> Writer,
	u16: (self: any, v: number, e: Endianness?) -> Writer,
	u32: (self: any, v: number, e: Endianness?) -> Writer,
	f32: (self: any, v: number) -> Writer,
	f64: (self: any, v: number) -> Writer,
	varint: (self: any, v: number) -> Writer,
	writeString: (self: any, text: string) -> Writer,
	string: (self: any, text: string) -> Writer,
}

local Reader = {}
Reader.__index = Reader

local Writer = {}
Writer.__index = Writer

@native
local function take(self: any, n: number): number
	local remaining = buffer.len(self.buf) - self.pos
	if remaining < n then
		error("binary.reader: attempt to read " .. n .. " byte(s) with only " .. remaining .. " left", 3)
	end
	local startPos = self.pos
	self.pos += n
	return startPos
end

@native
local function reserve(self: any, extra: number)
	local needed = self.pos + extra
	if needed > buffer.len(self.buf) then
		-- Grow geometrically via create+copy (buffer.resize is not
		-- available in every Luau deployment).
		local newSize = math.max(needed, buffer.len(self.buf) * 2)
		local grown = buffer.create(newSize)
		buffer.copy(grown, 0, self.buf, 0, self.pos)
		self.buf = grown
	end
end

local binary = {}

--- Creates a reader over `buf`. `offset` is the starting byte position
--- (0-based, default 0).
@native
function binary.reader(buf: buffer, offset: number?): Reader
	local self = setmetatable({ buf = buf, pos = math.max(0, math.floor(offset or 0)) }, Reader)
	return self::any
end

--- Creates a growable writer.
@native
function binary.writer(): Writer
	local self = setmetatable({ buf = buffer.create(64), pos = 0 }, Writer)
	return self::any
end

--- Current read cursor position (0-based).
@native
function Reader.seek(self: any): number
	return self.pos
end

--- Bytes left until end of buffer.
@native
function Reader.remaining(self: any): number
	return buffer.len(self.buf) - self.pos
end

@native
function Reader.u8(self: any): number
	return buffer.readu8(self.buf, take(self, 1))
end

@native
function Reader.i8(self: any): number
	local v = buffer.readu8(self.buf, take(self, 1))
	return if v >= 0x80 then v - 0x100 else v
end

--- Reads an unsigned 16-bit integer with explicit endianness (default le).
@native
function Reader.u16(self: any, e: Endianness?): number
	local startPos = take(self, 2)
	local lo = buffer.readu8(self.buf, startPos)
	local hi = buffer.readu8(self.buf, startPos + 1)
	if (e or "le") == "le" then
		return bit32.bor(lo, bit32.lshift(hi, 8))
	end
	return bit32.bor(bit32.lshift(lo, 8), hi)
end

--- Reads an unsigned 32-bit integer with explicit endianness (default le).
@native
function Reader.u32(self: any, e: Endianness?): number
	local startPos = take(self, 4)
	local b0 = buffer.readu8(self.buf, startPos)
	local b1 = buffer.readu8(self.buf, startPos + 1)
	local b2 = buffer.readu8(self.buf, startPos + 2)
	local b3 = buffer.readu8(self.buf, startPos + 3)
	if (e or "le") == "le" then
		return b0 + b1 * 0x100 + b2 * 0x10000 + b3 * 0x1000000
	end
	return b3 + b2 * 0x100 + b1 * 0x10000 + b0 * 0x1000000
end

--- Reads a little-endian 32-bit float.
@native
function Reader.f32(self: any): number
	return buffer.readf32(self.buf, take(self, 4))
end

--- Reads a little-endian 64-bit float.
@native
function Reader.f64(self: any): number
	return buffer.readf64(self.buf, take(self, 8))
end

--- Reads an unsigned ULEB128 varint of arbitrary length.
@native
function Reader.varint(self: any): number
	local result = 0
	local shift = 0
	while true do
		local byte = buffer.readu8(self.buf, take(self, 1))
		result += bit32.band(byte, 0x7F) * (2 ^ shift)
		if byte < 0x80 then
			break
		end
		shift += 7
		if shift > 63 then
			error("binary.reader: varint too long", 3)
		end
	end
	return result
end

--- Reads a ULEB128 length-prefixed string.
@native
function Reader.readString(self: any): string
	local len = self:varint()
	local startPos = take(self, len)
	return buffer.readstring(self.buf, startPos, len)
end

--- Alias kept for discoverability; same as readString.
Reader.string = Reader.readString

--- Returns a copy of exactly the bytes written so far.
@native
function Writer.bytes(self: any): buffer
	local snapshot = buffer.create(self.pos)
	buffer.copy(snapshot, 0, self.buf, 0, self.pos)
	return snapshot
end

@native
function Writer.u8(self: any, v: number): Writer
	reserve(self, 1)
	buffer.writeu8(self.buf, self.pos, v)
	self.pos += 1
	return self
end

--- Writes an unsigned 16-bit integer with explicit endianness (default le).
@native
function Writer.u16(self: any, v: number, e: Endianness?): Writer
	reserve(self, 2)
	if (e or "le") == "le" then
		buffer.writeu8(self.buf, self.pos, v % 0x100)
		buffer.writeu8(self.buf, self.pos + 1, v // 0x100 % 0x100)
	else
		buffer.writeu8(self.buf, self.pos, v // 0x100 % 0x100)
		buffer.writeu8(self.buf, self.pos + 1, v % 0x100)
	end
	self.pos += 2
	return self
end

--- Writes an unsigned 32-bit integer with explicit endianness (default le).
@native
function Writer.u32(self: any, v: number, e: Endianness?): Writer
	reserve(self, 4)
	for i = 1, 4 do
		local shift = if (e or "le") == "le" then (i - 1) * 8 else (4 - i) * 8
		buffer.writeu8(self.buf, self.pos + i - 1, v // (2 ^ shift) % 0x100)
	end
	self.pos += 4
	return self
end

--- Writes a little-endian 32-bit float.
@native
function Writer.f32(self: any, v: number): Writer
	reserve(self, 4)
	buffer.writef32(self.buf, self.pos, v)
	self.pos += 4
	return self
end

--- Writes a little-endian 64-bit float.
@native
function Writer.f64(self: any, v: number): Writer
	reserve(self, 8)
	buffer.writef64(self.buf, self.pos, v)
	self.pos += 8
	return self
end

--- Encodes a non-negative integer as ULEB128.
@native
function Writer.varint(self: any, v: number): Writer
	if v < 0 or v ~= math.floor(v) then
		error("binary.writer: varint requires a non-negative integer", 3)
	end
	local value = math.floor(v)
	while true do
		local byte = value % 128
		value = (value - byte) // 128
		if value > 0 then
			byte += 0x80
		end
		self:u8(byte)
		if value == 0 then
			break
		end
	end
	return self
end

--- Writes a string prefixed with its ULEB128 length.
@native
function Writer.writeString(self: any, text: string): Writer
	self:varint(#text)
	reserve(self, #text)
	buffer.writestring(self.buf, self.pos, text)
	self.pos += #text
	return self
end

--- Alias kept for discoverability; same as writeString.
Writer.string = Writer.writeString

--[[
	Binary Module - ByteReader/ByteWriter over Luau buffers with explicit
	endianness ("le" | "be"), ULEB128 varints and ULEB128 length-prefixed
	strings, so peers need no out-of-band framing agreement. Hot paths carry
	the @native attribute for the Luau codegen.

	Usage:
	```lua
	local binary = require("@binary")

	local w = binary.writer()
	w:u32(4000000000, "be"):varint(300):string("payload")

	local r = binary.reader(w:bytes())
	local value = r:u32("be")
	local text = r:string()
	```
]]
local Binary = {
	reader = binary.reader,
	writer = binary.writer,
}
return table.freeze(Binary)