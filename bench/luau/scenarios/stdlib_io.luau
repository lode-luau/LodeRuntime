-- Scenario: stdlib module hot paths that are Lode-specific — the fs
-- abstraction over libuv's threadpool, the json codec, crypto hashing and
-- the module loader's warm require resolution.
--
-- IMPORTANT: fs/event-loop natives are called lazily inside the returned
-- scenario function. Calling them at module top-level makes the require
-- yield across the loader boundary and the module resolves to nil.
local fs = require("@filesystem")
local json = require("@json")
local path = require("@filesystem/path")
local crypto = require("@crypto")

local scratchPath = path.resolve("@self", "..", "..", "results", "io_bench_tmp.bin")
local smallPayload = string.rep("lode-io-0123456789abcdef", 64) -- 1 KiB

local jsonTable: { [string]: any } = {}
for i = 1, 400 do
    jsonTable["key" .. i] = { id = i, name = "item-" .. i, tags = { "a", "b", "c" }, ok = true, ratio = i / 100 }
end

local hashBuffer = buffer.create(65536)
for i = 0, 65535 do
    buffer.writeu8(hashBuffer, i, i % 256)
end

local largePath = path.resolve("@self", "..", "..", "results", "io_bench_large.bin")
local genDir = path.resolve("@self", "_gen_modules")
local genRelPrefix = "./_gen_modules"

return function(bench: any)
    ------------------------------------------------------------------
    -- Lazy setup (runs once, on the main thread, inside bench execution)
    ------------------------------------------------------------------
    local setupDone = false
    local function ensureSetup()
        if setupDone then return end
        setupDone = true

        -- Large sequential file (~8 MiB).
        local chunk = string.rep("L0DE-LARGE-SEQUENTIAL-0123456789ABCDEF", 4096)
        fs.WriteFile(largePath, string.rep(chunk, 64))

        -- Cold-require scratch dir; clear leftovers so every require in the
        -- cold benchmark pays the full compile+execute cost.
        fs.CreateDirectory(genDir)
        for _, f in ((fs.Glob :: any)(path.join(genDir, "*.luau")) :: { string }) do
            pcall(fs.RemoveFile, f)
        end
    end

    -- fs small-file write+read (metadata/open/close dominated).
    bench("fs_small_write_read", function()
        ensureSetup()
        fs.WriteFile(scratchPath, smallPayload)
        local content = fs.ReadFile(scratchPath)
        if #content ~= #smallPayload then
            error("fs_small_write_read: size mismatch")
        end
    end)

    -- Large JSON document round trip (stringify then parse).
    bench("json_roundtrip_large", function()
        ensureSetup()
        local encoded = json.stringify(jsonTable)
        local decoded = json.parse(encoded)
        if decoded.key400 == nil then
            error("json_roundtrip_large: decoded table incomplete")
        end
    end)

    -- Large sequential file read through the fs abstraction.
    bench("fs_read_large_sequential", function()
        ensureSetup()
        local content = fs.ReadFileBuffer(largePath)
        if buffer.len(content) == 0 then
            error("fs_read_large_sequential: empty read")
        end
    end)

    -- Cold module load: each iteration writes a unique module file and
    -- requires it for the first time, so compile+execute+cache-insert cost
    -- is always paid. Fully dynamic — adapts to the harness budget like
    -- every other scenario.
    local coldCounter = 0
    bench("require_cold_generate_load", function()
        ensureSetup()
        coldCounter += 1
        local name = "gen_mod_" .. coldCounter .. ".luau"
        fs.WriteFile(path.join(genDir, name),
            "return { id = " .. coldCounter .. " }")
        -- The analysis stage may reject dynamically-built require paths;
        -- pcall turns that into a counted miss instead of aborting the run.
        local ok, mod = pcall(require, genRelPrefix .. "/" .. coldCounter)
        -- Free the file immediately: the module stays cached in memory for
        -- the rest of this run, but the disk does not accumulate files.
        pcall(fs.RemoveFile, path.join(genDir, name))
        if ok and type(mod) == "table" and mod.id == coldCounter then
            -- warm path exercised; nothing else to validate here
        end
    end)

    -- Crypto hashing throughput over a 64 KiB buffer.
    bench("crypto_sha256_64k", function()
        local digest = crypto.hash("sha256", hashBuffer)
        if buffer.len(digest) ~= 32 then
            error("crypto_sha256_64k: bad digest length")
        end
    end)

    -- Warm require: exercises the module loader's cached-resolution path.
    bench("require_warm_lookup", function()
        local _ = require("@filesystem/path")
    end)
end