--!native
--!optimize 2

print("=== SIMD AVX2 Buffer Benchmark ===")

local simdMod = require("./simd_module")
local NUM_FLOATS = 1000000 -- 1 Million floats
local BUFFER_SIZE = NUM_FLOATS * 4

-- 1. Create table
local tab = table.create(NUM_FLOATS, 1.5)
-- 2. Create buffer
local buf = buffer.create(BUFFER_SIZE)
buffer.fill(buf, 0, 0, BUFFER_SIZE)
for i = 0, NUM_FLOATS - 1 do
    buffer.writef32(buf, i * 4, 1.5)
end

local SCALAR = 2.0

local function testLuauTable()
    local t = os.clock()
    for i = 1, NUM_FLOATS do
        tab[i] = tab[i] * SCALAR
    end
    return os.clock() - t
end

local function testLuauBuffer()
    local t = os.clock()
    for i = 0, NUM_FLOATS - 1 do
        local offset = i * 4
        local val = buffer.readf32(buf, offset)
        buffer.writef32(buf, offset, val * SCALAR)
    end
    return os.clock() - t
end

local function testSIMDModule()
    local t = os.clock()
    -- Passes the buffer to C++ which scales it instantly via AVX2
    simdMod.scale_floats(buf, SCALAR)
    return os.clock() - t
end

print("Scaling " .. tostring(NUM_FLOATS) .. " floats by " .. tostring(SCALAR) .. "...\n")

local timeTab = testLuauTable()
print(string.format("[Luau Table Loop]   %.5f seconds", timeTab))

local timeBuf = testLuauBuffer()
print(string.format("[Luau Buffer Loop]  %.5f seconds", timeBuf))

local timeSIMD = testSIMDModule()
print(string.format("[C++ SIMD AVX2]     %.5f seconds", timeSIMD))

print(string.format("\nSpeedup (Luau Table vs SIMD AVX2): %.2fx", timeTab / timeSIMD))
print(string.format("Speedup (Luau Buffer vs SIMD AVX2): %.2fx", timeBuf / timeSIMD))
