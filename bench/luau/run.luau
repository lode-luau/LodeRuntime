--!strict

-- Benchmark runner for Luau-side scenarios (bench/luau/run.luau).
-- Usage: build/bin/Release/lode.exe bench/luau/run.luau
--
-- Methodology: adaptive timing (google-benchmark min_time model) — a doubling
-- calibration probe estimates per-op cost, the iteration count targets a
-- ~400 ms repeat, and the number of repeats follows a ~2 s per-scenario
-- budget (3-10 repeats, median reported). Total runtime stays bounded even if
-- a scenario becomes orders of magnitude slower or faster.
-- Use Release builds only.

export type BenchFn = (string, () -> ()) -> ()

local kRepeatMs = 400.0
local kBudgetMs = 2000.0
local kMinRepeats = 3
local kMaxRepeats = 10
local kMaxIterations = 1000000000

local function bench(name: string, fn: () -> ())
    -- Calibration: doubling probe until >= 25 ms of sampled time.
    local probeIters = 100
    local probeMs = 0.0
    for _ = 1, 14 do
        local t0 = os.clock()
        for _ = 1, probeIters do
            fn()
        end
        probeMs = (os.clock() - t0) * 1000
        if probeMs >= 25.0 or probeIters >= kMaxIterations then
            break
        end
        probeIters = math.min(probeIters * 10, kMaxIterations)
    end
    probeMs = math.max(probeMs, 0.001)

    local perOpNs = (probeMs * 1e6) / probeIters
    local targetIters = math.floor(math.clamp(kRepeatMs * 1e6 / perOpNs, 1, kMaxIterations))

    -- Warmup: short untimed pass to fill caches.
    for _ = 1, math.max(1, math.floor(targetIters / 10)) do
        fn()
    end

    local repeatMs = (perOpNs * targetIters) / 1e6
    local repeats = math.clamp(math.floor(kBudgetMs / math.max(repeatMs, 1.0)), kMinRepeats, kMaxRepeats)

    local times: { number } = {}
    for _ = 1, repeats do
        local t0 = os.clock()
        for _ = 1, targetIters do
            fn()
        end
        local dt = os.clock() - t0
        table.insert(times, (dt * 1e9) / targetIters)
    end

    table.sort(times)
    local median = times[math.floor(#times / 2)]
    print(string.format("%-32s %12.2f ns/op", name, median))
end

print("=== LodeRuntime Luau benchmarks ===")

local NativeMod = require("../../tests/native/native_module")

local scenarios = {
    require("./scenarios/native_calls"),
    require("./scenarios/class_methods"),
    require("./scenarios/coroutines"),
    require("./scenarios/runtime_modules"),
}

for _, scenario in scenarios do
    scenario(bench, NativeMod)
end

print("=== done ===")
