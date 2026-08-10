--!strict
-- Scenario: Lode's own modules (filesystem, json, path) and the task
-- timer path — the runtime features that go beyond plain Luau.
local fs = require("@filesystem")
local json = require("@json")
local path = require("@filesystem/path")

local fixturePath = path.resolve("@self", "..", "..", "fixtures", "big.luau")
local scratchPath = path.resolve("@self", "..", "..", "results", "fs_bench_tmp.bin")

local payload = string.rep("lode-benchmark-payload-0123456789", 100)

local jsonDoc = [[{"name":"lode","version":"1.0.0","enabled":true,"tags":["a","b","c"],"nested":{"one":1,"two":2,"three":3},"items":[1,2,3,4,5,6,7,8,9,10],"mixed":[1,"two",3.5,true,null,{"k":false}]}]]

return function(bench: any)
    bench("fs_read_file", function()
        local content = fs.ReadFile(fixturePath)
        if #content == 0 then
            error("fs.ReadFile returned empty content")
        end
    end)

    bench("fs_write_file", function()
        fs.WriteFile(scratchPath, payload)
    end)

    bench("fs_stat", function()
        local info = fs.Stat(fixturePath)
        if not info.isDirectory and not info.isFile then
            error("fs.Stat did not report a valid file")
        end
    end)

    bench("fs_read_dir", function()
        local entries = fs.ReadDirectory("bench")
        if #entries == 0 then
            error("fs.ReadDirectory returned no entries")
        end
    end)

    local parsed = json.parse(jsonDoc)
    bench("json_parse", function()
        json.parse(jsonDoc)
    end)

    bench("json_stringify", function()
        json.stringify(parsed)
    end)

    -- path.join and path.resolve are pure lexical since the weakly_canonical
    -- removals (modules/path/src/path.cpp): neither touches the filesystem,
    -- so resolve costs one current_path syscall plus string normalization.
    bench("path_resolve", function()
        path.resolve("@self", "a", "b", "c.luau")
    end)

    bench("path_join", function()
        path.join("bench", "luau", "run.luau")
    end)
end
