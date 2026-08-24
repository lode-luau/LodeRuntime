--!strict
-- Scenario: native-call marshaling paths (legacy vector<Value> boxing vs
-- StackArgs fast path vs zero-marshaling N-variant) using the
-- marshal_module fixture from tests/native.
return function(bench: any)
    local marshal = require("../../../tests/native/marshal_module")

    -- Correctness gate: all three paths must agree, including multi-value
    -- returns on the N variant, before anything is timed.
    local sum, argCount = marshal.addFastN(3, 4)
    assert(marshal.addLegacy(3, 4) == 7, "addLegacy(3, 4) ~= 7")
    assert(marshal.addFast(3, 4) == 7, "addFast(3, 4) ~= 7")
    assert(sum == 7 and argCount == 2, "addFastN did not return (7, 2)")

    local addLegacy = marshal.addLegacy
    local addFast = marshal.addFast
    local addFastN = marshal.addFastN

    bench("marshal_legacy_vector", function()
        addLegacy(3, 4)
    end)

    bench("marshal_fast_single_value", function()
        addFast(3, 4)
    end)

    bench("marshal_fastn_direct_push", function()
        addFastN(3, 4)
    end)
end