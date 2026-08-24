--!strict
-- SPDX-License-Identifier: MIT

-- UUID v4 generation/validation built on top of the crypto CSPRNG.

local crypto = require("@crypto")

local UUID = {}

local HEX = "0123456789abcdef"

@native
local function bytesToUuid(b): string
	local out = table.create(36)
	for i = 1, 16 do
		if i == 5 or i == 7 or i == 9 or i == 11 then
			table.insert(out, "-")
		end
		local byte = buffer.readu8(b, i - 1)
		table.insert(out, HEX:sub((byte // 16) % 16 + 1, (byte // 16) % 16 + 1))
		table.insert(out, HEX:sub(byte % 16 + 1, byte % 16 + 1))
	end
	return table.concat(out)
end

--- Generates a random (version 4) UUID string,
--- e.g. "3b241101-e2bb-4255-8caf-4136c566a962".
function UUID.v4(): string
	local b = crypto.randomBytes(16)
	buffer.writeu8(b, 6, bit32.bor(bit32.band(buffer.readu8(b, 6), 0x0F), 0x40)) -- version 4
	buffer.writeu8(b, 8, bit32.bor(bit32.band(buffer.readu8(b, 8), 0x3F), 0x80)) -- RFC 4122 variant
	return bytesToUuid(b)
end

local PATTERN = "^%x%x%x%x%x%x%x%x%-%x%x%x%x%-4%x%x%x%-[89abAB]%x%x%x%-%x%x%x%x%x%x%x%x%x%x%x%x$"

--- Validates that s looks like a canonical lowercase/uppercase v4 UUID.
function UUID.isV4(s: string): boolean
	if type(s) ~= "string" or #s ~= 36 then
		return false
	end
	return string.match(s:lower(), PATTERN) ~= nil
end

--[[
	UUID Module - RFC 4122 identifier generation and validation.
	Generates random (version 4) UUIDs from the cryptographic CSPRNG and
	validates canonical string form.

	Usage:
	```lua
	local uuid = require("@uuid")

	local id = uuid.v4()
	print(id) --> "3b241101-e2bb-4255-8caf-4136c566a962"

	if uuid.isV4(id) then
		print("valid v4 UUID")
	end
	```
]]
return table.freeze(UUID)
