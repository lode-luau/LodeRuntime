--!strict
-- SPDX-License-Identifier: MIT

-- CSV encode/decode (RFC 4180 style, single-character separator).
-- decode returns rows as arrays of string fields; the first row is the
-- header only if the caller asks for it via opts.headers.

export type DecodeOptions = {
	--- Field separator (default ","). Use "\t" for TSV.
	sep: string?,
	--- When true, treats the first row as a header and returns an array of
	--- { [header]: value } records instead of positional fields.
	headers: boolean?,
}

local csv = {}

local function needsQuotes(field: string, sep: string): boolean
	return field:find(sep, 1, true) ~= nil
		or field:find('"', 1, true) ~= nil
		or field:find("[\r\n]", 1) ~= nil
end

local function quote(field: string): string
	return '"' .. field:gsub('"', '""') .. '"'
end

--- Encodes a matrix of rows into a CSV document. Numbers and booleans are
--- converted with tostring; anything else must be pre-formatted.
function csv.encode(rows: { { any } }, sep: string?): string
	local s = sep or ","
	local out = {}
	for _, row in ipairs(rows) do
		local fields = {}
		for _, cell in ipairs(row) do
			local text = tostring(cell)
			if needsQuotes(text, s) then
				table.insert(fields, quote(text))
			else
				table.insert(fields, text)
			end
		end
		table.insert(out, table.concat(fields, s))
	end
	return table.concat(out, "\r\n") .. "\r\n"
end

--- Decodes a CSV document. Returns (records, header):
---   default: records is an array of { string } rows;
---   with opts.headers=true: records is an array of { [string]: string }
---   keyed by the first-row headers, and header is returned too.
function csv.decode(text: string, opts: DecodeOptions?): ({ any }, { string }?)
	local sep = (opts and opts.sep) or ","
	local useHeaders = opts and opts.headers

	local rows: { { string } } = {}
	local field: { string } = {}
	local row: { string } = {}
	local i, n = 1, #text
	local inQuotes = false

	while i <= n do
		local ch = text:sub(i, i)
		if inQuotes then
			if ch == '"' then
				if text:sub(i + 1, i + 1) == '"' then
					table.insert(field, '"')
					i += 2
				else
					inQuotes = false
					i += 1
				end
			else
				table.insert(field, ch)
				i += 1
			end
		elseif ch == '"' then
			inQuotes = true
			i += 1
		elseif ch == sep then
			table.insert(row, table.concat(field))
			field = {}
			i += 1
		elseif ch == "\r\n" then
			table.insert(row, table.concat(field))
			field = {}
			table.insert(rows, row)
			row = {}
			i += 2
		elseif ch == "\n" or ch == "\r" then
			table.insert(row, table.concat(field))
			field = {}
			table.insert(rows, row)
			row = {}
			i += if ch == "\r" then 2 else 1
		else
			table.insert(field, ch)
			i += 1
		end
	end

	if #field > 0 or #row > 0 then
		table.insert(row, table.concat(field))
		table.insert(rows, row)
	end

	if useHeaders then
		local header = rows[1] or {}
		local records: { any } = {}
		for r = 2, #rows do
			local record: { [string]: string } = {}
			for c, name in ipairs(header) do
				record[name] = rows[r][c]
			end
			table.insert(records, record)
		end
		return records, header
	end

	return rows, nil
end

--[[
	CSV Module - RFC 4180-style encoding and decoding with quoted fields,
	embedded separators, newlines and escaped quotes. Custom separators
	support TSV and other dialects; the headers option returns keyed records.

	Usage:
	```lua
	local csv = require("@csv")

	local text = csv.encode({ { "name", "age" }, { "Ana", 30 } })
	--> "name,age\r\nAna,30\r\n"

	local rows = csv.decode(text)
	local records, header = csv.decode(text, { headers = true })
	print(header[1], records[1].name)
	```
]]
return table.freeze(csv)
