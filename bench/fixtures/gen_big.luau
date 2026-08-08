--!strict

-- Regenerates bench/fixtures/big.luau deterministically (no RNG).
-- Usage: build/bin/Release/lode_runtime.exe bench/fixtures/gen_big.luau
--
-- The output path is resolved relative to this script via @self, so the
-- generator works regardless of the current working directory. Uses only
-- Lode's own modules from .config.luau (filesystem, path).

local fs = require("@filesystem")
local path = require("@path")

local N_BLOCKS = 84
local BODY = 18

local lines: { string } = {}

local function push(line: string)
    table.insert(lines, line)
end

-- Deterministic LCG, so the fixture is stable across runs and machines.
local function makeSeeded(n: number): { number }
    local out: { number } = {}
    local s: number = 123456789
    for i = 1, n do
        s = (s * 1103515245 + 12345) % 2147483648
        out[i] = s
    end
    return out
end

local rand = makeSeeded(N_BLOCKS * BODY * 2 + N_BLOCKS + 2)
local rpos = 0
local function nextConst(): number
    rpos += 1
    return rand[rpos] % 1073741824
end

push("--!strict")
push("-- Deterministic generated workload for CLI warm benchmarks (see bench/README.md).")
push("-- Regenerate with: build/bin/Release/lode_runtime.exe bench/fixtures/gen_big.luau")
push("")

push("local function mix(a: number, b: number): number")
push("    local h = (a * 374761393 + b * 668265263) % 2147483647")
push("    h = bit32.bxor(h, bit32.rshift(h, 13)) * 1274126177")
push("    h = bit32.bxor(h, bit32.rshift(h, 16))")
push("    return h % 2147483647")
push("end")
push("")

push("local function mix2(a: number, b: number, c: number): number")
push("    return mix(mix(a, b), c)")
push("end")
push("")

for blk = 0, N_BLOCKS - 1 do
    push(string.format("local function block%d(x: number, y: number): number", blk))
    push("    local acc: number = x + y")
    for i = 1, BODY do
        push(string.format("    acc = mix2(acc, %d, %d)", nextConst(), nextConst()))
        if i % 3 == 0 then
            push("    acc = acc % 2147483647")
        end
    end
    push("    return acc")
    push("end")
    push("")
end

push("local function main(): number")
push("    local total: number = 0")
for blk = 0, N_BLOCKS - 1 do
    local v = nextConst()
    push(string.format("    total = bit32.bxor(total, block%d(%d, %d))", blk, v, v + 1))
end
push("    return total")
push("end")
push("")

push("local function check(n: number): string")
push("    if n % 2 == 0 then")
push("        return \"even\"")
push("    else")
push("        return \"odd\"")
push("    end")
push("end")
push("")

push("local result: number = main()")
push("print(\"fixture ok\", check(result), result % 1000000)")

local target = path.resolve("@self", "big.luau")
fs.writeFile(target, table.concat(lines, "\n") .. "\n")
print("wrote " .. target .. " (" .. #lines .. " lines)")
