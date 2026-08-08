--!strict
-- Scenario: Lode's own modules (filesystem, json, path) and the task
-- timer path — the runtime features that go beyond plain Luau.
local fs = require("@filesystem")
local json = require("@json")
local path = require("@path")
local task = require("@task")

local fixturePath = path.resolve("@self", "..", "..", "fixtures", "big.luau")
local scratchPath = path.resolve("@self", "..", "..", "results", "fs_bench_tmp.bin")

local payload = string.rep("lode-benchmark-payload-0123456789", 100)

local jsonDoc = [[{"name":"lode","version":"1.0.0","enabled":true,"tags":["a","b","c"],"nested":{"one":1,"two":2,"three":3},"items":[1,2,3,4,5,6,7,8,9,10],"mixed":[1,"two",3.5,true,null,{"k":false}]}]]

return function(bench: any)
    bench("fs_read_file", function()
        local content = fs.readFile(fixturePath)
        if #content == 0 then
            error("fs.readFile returned empty content")
        end
    end)

    bench("fs_write_file", function()
        fs.writeFile(scratchPath, payload)
    end)

    bench("fs_stat", function()
        local info = fs.stat(fixturePath)
        if not info.isFile then
            error("fs.stat did not report a file")
        end
    end)

    bench("fs_read_dir", function()
        local entries = fs.readDir("bench")
        if #entries == 0 then
            error("fs.readDir returned no entries")
        end
    end)

    local parsed = json.parse(jsonDoc)
    bench("json_parse", function()
        json.parse(jsonDoc)
    end)

    bench("json_stringify", function()
        json.stringify(parsed)
    end)

    -- path.join is pure lexical since the weakly_canonical removal
    -- (modules/path/src/path.cpp); path.resolve still canonicalizes.
    bench("path_resolve", function()
        path.resolve("@self", "a", "b", "c.luau")
    end)

    bench("path_join", function()
        path.join("bench", "luau", "run.luau")
    end)
end
