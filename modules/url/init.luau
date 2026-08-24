--!strict

-- SPDX-License-Identifier: MIT

-- URL utilities: percent-encoding, query strings and URL parsing/formatting.

export type UrlRecord = {
	scheme: string,
	userinfo: string,
	host: string,
	port: number?,
	path: string,
	query: string,
	fragment: string,
}

local url = {}

@native
function url.encodeComponent(text: string): string
	return (text:gsub("[^%w%-_.~]", function(ch)
		return "%" .. string.format("%02X", ch:byte())
	end))
end

--- Decodes percent-escapes; "+" becomes a space. Invalid sequences are
--- passed through as literal characters.
@native
function url.decodeComponent(text: string): string
	local s = text:gsub("+", " ")
	s = s:gsub("%%(%x%x)", function(hex: string)
		return string.char(tonumber(hex, 16) :: number)
	end)
	return s
end

--- Encodes a flat table into an "a=1&b=two" query string. Array values
--- become repeated keys; true becomes a bare key; false/nil are skipped.
function url.buildQuery(params: { [string]: any }): string
	local parts: { string } = {}
	for key, value in params do
		if value == false or value == nil then
			continue
		end
		local k = url.encodeComponent(key)
		if value == true then
			table.insert(parts, k)
		elseif type(value) == "table" then
			for _, item in ipairs(value :: { any }) do
				table.insert(parts, k .. "=" .. url.encodeComponent(tostring(item)))
			end
		else
			table.insert(parts, k .. "=" .. url.encodeComponent(tostring(value)))
		end
	end
	return table.concat(parts, "&")
end

--- Parses "a=1&b=two&flag" into a table. Repeated keys become arrays;
--- bare keys map to true. Values are percent-decoded.
function url.parseQuery(query: string): { [string]: any }
	local result: { [string]: any } = {}
	if query == "" then
		return result
	end
	for pair in query:gmatch("[^&]+") do
		local eq = pair:find("=", 1, true)
		local rawKey: string
		local rawValue: string?
		if eq ~= nil then
			rawKey = pair:sub(1, eq - 1)
			rawValue = pair:sub(eq + 1)
		else
			rawKey = pair
			rawValue = nil
		end
		local key = url.decodeComponent(rawKey)
		if key ~= "" then
			if rawValue == nil then
				result[key] = true
			else
				local decoded = url.decodeComponent(rawValue)
				local current = result[key]
				if current == nil then
					result[key] = decoded
				elseif type(current) == "table" then
					table.insert(current :: { any }, decoded)
				else
					result[key] = { current, decoded }
				end
			end
		end
	end
	return result
end

-- Finds the last occurrence of needle in s, or nil.
local function rfind(s: string, needle: string): number?
	for i = #s - #needle + 1, 1, -1 do
		if s:sub(i, i + #needle - 1) == needle then
			return i
		end
	end
	return nil
end

--- Parses an absolute or authority-less URL into its components. Missing
--- pieces come back as empty strings (port is nil when absent).
function url.parse(text: string): UrlRecord
	local record: UrlRecord = {
		scheme = "", userinfo = "", host = "",
		port = nil, path = "", query = "", fragment = "",
	}
	local rest = text

	local hashPos = rest:find("#", 1, true)
	if hashPos ~= nil then
		record.fragment = rest:sub(hashPos + 1)
		rest = rest:sub(1, hashPos - 1)
	end

	local schemePos = rest:find("://", 1, true)
	if schemePos ~= nil then
		record.scheme = rest:sub(1, schemePos - 1):lower()
		rest = rest:sub(schemePos + 3)
	end

	local pathStart = rest:find("[/?#]")
	if pathStart ~= nil then
		record.path = rest:sub(pathStart)
		rest = rest:sub(1, pathStart - 1)
	end

	local atPos = rfind(rest, "@")
	if atPos ~= nil then
		record.userinfo = rest:sub(1, atPos - 1)
		rest = rest:sub(atPos + 1)
	end

	local colonPos = rfind(rest, ":")
	if colonPos ~= nil and colonPos > 1 then
		local maybePort = tonumber(rest:sub(colonPos + 1))
		if maybePort ~= nil then
			record.port = maybePort
			rest = rest:sub(1, colonPos - 1)
		end
	end
	record.host = rest

	local qPos = record.path:find("?", 1, true)
	if qPos ~= nil then
		record.query = record.path:sub(qPos + 1)
		record.path = record.path:sub(1, qPos - 1)
	end

	return record
end

--- Formats a UrlRecord back into a URL string (inverse of parse).
function url.format(record: UrlRecord): string
	local out = ""
	if record.scheme ~= "" then
		out ..= record.scheme .. "://"
	end
	if record.userinfo ~= "" then
		out ..= record.userinfo .. "@"
	end
	out ..= record.host
	if record.port ~= nil then
		out ..= ":" .. tostring(record.port)
	end
	out ..= record.path
	if record.query ~= "" then
		out ..= "?" .. record.query
	end
	if record.fragment ~= "" then
		out ..= "#" .. record.fragment
	end
	return out
end

--[[
	URL Module - Parsing, formatting and percent-encoding helpers.
	Decomposes scheme, userinfo, host, port, path, query and fragment;
	builds and parses application/x-www-form-urlencoded query strings;
	encodes/decodes components for safe URL interpolation.

	Usage:
	```lua
	local url = require("@url")

	local r = url.parse("https://example.com:8443/search?q=luau&page=2")
	print(r.host, r.port) --> example.com 8443
	print(url.parseQuery(r.query).q) --> "luau"

	local qs = url.buildQuery({ q = "a b", page = 2 })
	--> "q=a%20b&page=2"

	local safe = url.encodeComponent("a/b c") --> "a%2Fb%20c"
	```
]]
return table.freeze(url)