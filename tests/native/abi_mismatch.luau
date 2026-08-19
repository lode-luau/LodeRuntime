--!strict

local function check(condition: boolean, message: string)
    if not condition then
        error("abi_mismatch: " .. message, 2)
    end
end

local ok, err = pcall(require, "./abi_mismatch_module")
check(ok == false, "abi_mismatch_module must not load")

local errString = tostring(err)
check(string.find(errString, "was built for ABI", 1, true) ~= nil,
    "expected a clean ABI-mismatch error, got: " .. errString)
check(string.find(errString, "lode-abi-mismatch-fixture", 1, true) ~= nil,
    "error must name the module ABI, got: " .. errString)

print("abi_mismatch: loader cleanly refused an incompatible ABI")
