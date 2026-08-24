--!strict
--!optimize 2

-- Native-call marshaling regression canary (canonical, runs through the real
-- runtime path via CTest as integration_marshal_bench).
--
-- Correctness only: verifies all three native closure paths agree and that
-- the zero-marshaling N-variant delivers multi-value returns. Per-path
-- timing lives in bench/luau/scenarios/marshal_calls.luau, which follows the
-- adaptive methodology of bench/luau/run.luau (run via
-- build/bin/Release/lode.exe bench/luau/run.luau).
local function check(condition: boolean, message: string)
    if not condition then
        error("marshal_bench: " .. message, 2)
    end
end

local marshal = require("./marshal_module")

check(marshal.addLegacy(3, 4) == 7, "addLegacy(3, 4) ~= 7")
check(marshal.addFast(3, 4) == 7, "addFast(3, 4) ~= 7")
local sum, argCount = marshal.addFastN(3, 4)
check(sum == 7, "addFastN(3, 4) sum ~= 7")
check(argCount == 2, "addFastN did not report 2 arguments")
check(select("#", marshal.addFastN(3, 4)) == 2, "addFastN must return two values")

print("marshal_bench: all checks passed")
