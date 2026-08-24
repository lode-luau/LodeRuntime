--!strict
-- SPDX-License-Identifier: MIT

-- dotenv: parse ".env"/INI-style key=value documents.
--
-- Supported syntax:
--   KEY=value            (trailing inline comments after " #")
--   # full-line comment
--   export KEY=value     (leading "export" is ignored)
--   KEY="quoted value"   (quotes stripped, no escape processing)
--   empty lines ignored

local dotenv = {}

local function trim(s: string): string
	return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function unquote(v: string): string
	local trimmed = trim(v)
	local q = trimmed:sub(1, 1)
	if (q == '"' or q == "'") and #trimmed >= 2 and trimmed:sub(-1) == q then
		return trimmed:sub(2, -2)
	end
	return trimmed
end

--- Parses the given document into a { [key]: value } table.
--- Lines without "=" or with an empty key are skipped silently.
function dotenv.parse(text: string): { [string]: string }
	local result: { [string]: string } = {}
	for rawLine in text:gmatch("[^\r\n]+") do
		local line = trim(rawLine)
		if line ~= "" and line:sub(1, 1) ~= "#" then
			line = line:gsub("^export%s+", "")
			local eq = line:find("=", 1, true)
			if eq and eq > 1 then
				local key = trim(line:sub(1, eq - 1))
				local rest = line:sub(eq + 1)
				-- strip trailing comments only for unquoted values
				if rest:sub(1, 1) ~= '"' and rest:sub(1, 1) ~= "'" then
					local hash = rest:find(" %s-#", 1)
					local hashPos = rest:find("#", 2, true)
					if hashPos then
						rest = rest:sub(1, hashPos - 1)
					end
				end
				if key ~= "" then
					result[key] = unquote(rest)
				end
			end
		end
	end
	return result
end

--- Reads `path` (default ".env") through @filesystem and returns the parsed
--- table. Missing files yield an empty table instead of erroring.
function dotenv.load(path: string?): { [string]: string }
	local fs = require("@filesystem")
	local target = path or ".env"
	local ok, content = pcall(fs.ReadFile, target)
	if not ok then
		return {}
	end
	return dotenv.parse(content)
end

--- Like load(), but also applies every entry to the process environment via
--- @sys.SetEnv. Existing variables are NOT overwritten unless overwrite=true.
function dotenv.apply(path: string?, overwrite: boolean?): number
	local sys = require("@sys")
	local entries = dotenv.load(path)
	local applied = 0
	for key, value in entries do
		if overwrite or sys.GetEnv(key) == nil then
			sys.SetEnv(key, value)
			applied += 1
		end
	end
	return applied
end

--[[
	Dotenv Module - Parses .env / INI-style key=value documents and can
	merge them into the process environment. Supports full-line comments,
	"export" prefixes, single/double quotes and inline comments on unquoted
	values.

	Usage:
	```lua
	local dotenv = require("@dotenv")

	-- Parse only
	local vars = dotenv.parse("HOST=localhost\nPORT=8080")
	print(vars.HOST) --> "localhost"

	-- Load a file (defaults to ".env"; missing files yield {})
	local cfg = dotenv.load(".env")

	-- Load and push into the process env via @sys (without overwriting)
	dotenv.apply(".env", false)
	```
]]
return table.freeze(dotenv)
