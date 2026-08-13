--!strict
-- Scenario: ClassBuilder-bound method calls (per-call binding overhead),
-- using the Vector3 class exported by tests/native/native_module.
return function(bench: any, NativeMod: any)
    local Vector3 = NativeMod.Vector3
    if not Vector3 then
        print("class_method_call: skipped (Vector3 not exported)")
        return
    end

    local v = Vector3.new(3, 4, 0)

    bench("class_method_call", function()
        v:length()
    end)
end
