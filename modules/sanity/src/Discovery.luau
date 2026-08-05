--!strict

--[[
    Test discovery for the Sanity framework.

    Recursively walks a directory using the filesystem native module and
    returns the relative paths (forward slashes) of files matching a pattern.
]]

local Deps = require("./Deps")

--- Recursively discovers files matching `pattern` under `dir`.
--- Returns relative paths (with forward slashes) from `dir`, sorted.
local function discover(dir: string, pattern: string): { string }
    local fs = Deps.fs
    local path = fs.path
    local files: { string } = {}

    local function walk(current: string, relPrefix: string)
        local ok, entries = pcall(fs.readDir, current)
        if not ok then
            return
        end

        for _, entry in ipairs(entries) do
            local full = path.join(current, entry)
            local rel = relPrefix .. entry

            local okStat, st = pcall(fs.stat, full)
            if okStat then
                if st.isDirectory then
                    walk(full, rel .. "/")
                elseif st.isFile and string.match(entry, pattern) then
                    table.insert(files, rel)
                end
            end
        end
    end

    walk(dir, "")
    table.sort(files)
    return files
end

return {
    discover = discover,
}