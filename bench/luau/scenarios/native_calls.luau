--!strict
-- Scenario: native call throughput (fast and slow closure paths) using the
-- existing native_module exports from tests/native/native_module.
return function(bench: any, NativeMod: any)
    local square = NativeMod.square
    local roundTripReference = NativeMod.roundTripReference
    local roundTripString = NativeMod.roundTripString

    local sharedTable = { key = "value", nested = { 1, 2, 3 } }
    local sharedString = "a string used for native round-trips"

    bench("native_square_call", function()
        square(3.0)
    end)

    bench("native_roundtrip_ref", function()
        roundTripReference(sharedTable)
    end)

    bench("native_roundtrip_string", function()
        roundTripString(sharedString)
    end)
end
