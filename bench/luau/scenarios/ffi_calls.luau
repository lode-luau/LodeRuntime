--!strict
-- Scenario: cached libffi call plans. Windows-only until the deterministic
-- fixture is made available to the standalone benchmark runner on every OS.
return function(bench: any)
    local sys = require("../../../modules/sys")
    if not sys.GetPlatform():lower():find("windows", 1, true) then
        return
    end

    local ffi = require("../../../modules/ffi")
    local kernel32 = ffi.load("kernel32", [[
        unsigned long GetCurrentProcessId(void);
        int lstrlenA(const char* text);
    ]])

    assert(kernel32.GetCurrentProcessId() > 0, "ffi process-id correctness gate failed")
    assert(kernel32.lstrlenA("benchmark") == 9, "ffi string correctness gate failed")

    bench("ffi_noarg_u32", function()
        kernel32.GetCurrentProcessId()
    end)

    bench("ffi_string_pointer", function()
        kernel32.lstrlenA("benchmark")
    end)
end
