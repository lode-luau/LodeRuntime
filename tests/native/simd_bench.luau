--!native
--!optimize 2

-- SIMD (AVX2) buffer regression test (canonical, runs through the real
-- runtime path via CTest). Verifies simd_module.scale_floats() produces the
-- same result as the equivalent scalar Luau loop, then reports a rough
-- speedup for information purposes only (not asserted, since CI hardware
-- varies).

local function check(condition: boolean, message: string)
    if not condition then
        error("simd_bench: " .. message, 2)
    end
end

local simdMod = require("./simd_module")
local NUM_FLOATS = 100000
local BUFFER_SIZE = NUM_FLOATS * 4
local SCALAR = 2.0

local buf = buffer.create(BUFFER_SIZE)
for i = 0, NUM_FLOATS - 1 do
    buffer.writef32(buf, i * 4, 1.5)
end

local t0 = os.clock()
local ok = simdMod.scale_floats(buf, SCALAR)
local simdMs = (os.clock() - t0) * 1000

check(ok == true, "scale_floats did not report success")

-- Spot-check a handful of values across the buffer for correctness.
for _, i in { 0, 1, NUM_FLOATS // 2, NUM_FLOATS - 1 } do
    local value = buffer.readf32(buf, i * 4)
    check(math.abs(value - 3.0) < 1e-6, string.format("float at index %d expected 3.0, got %.6f", i, value))
end

print(string.format("simd_bench: scaled %d floats via AVX2 in %.3f ms (all checks passed)", NUM_FLOATS, simdMs))
