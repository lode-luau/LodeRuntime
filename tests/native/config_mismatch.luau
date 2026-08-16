--!strict
-- ABI/configuration-mismatch regression test (runs through the real runtime
-- path via CTest). config_mismatch_module always reports a build configuration
-- that differs from the running runtime; the loader must refuse to initialize
-- it with a clean, named error instead of loading it and crashing.

local function check(condition: boolean, message: string)
    if not condition then
        error("config_mismatch: " .. message, 2)
    end
end

local ok, err = pcall(require, "./config_mismatch_module")
check(ok == false, "config_mismatch_module must not load, require returned ok=" .. tostring(ok))
local errStr = tostring(err)
check(string.find(errStr, "built for configuration", 1, true) ~= nil,
    "expected a clean configuration-mismatch error, got: " .. errStr)
check(string.find(errStr, "mismatch-fixture-config", 1, true) ~= nil,
    "error must name the module's reported configuration, got: " .. errStr)

print("config_mismatch: loader cleanly refused a mismatched-configuration module")