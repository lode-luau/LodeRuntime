--!strict
-- Scenario: direct FastFunctionN address and runtime-memory operations.
return function(bench: any)
    local Address = require("../../../modules/address")
    local Memory = require("../../../modules/memory")
    local block = Memory.new(64, { Zeroed = true, Alignment = 16 })
    local pointer = Memory.Address(block)

    bench("address_read_u32", function()
        Address.Read.U32(pointer, 0)
    end)

    bench("address_write_u32", function()
        Address.Write.U32(pointer, 0, 0x12345678)
    end)

    bench("address_add", function()
        Address.Add(pointer, 8)
    end)

    bench("memory_allocate_free", function()
        local temporary = Memory.new(64, { Alignment = 16 })
        Memory.Free(temporary)
    end)

    Memory.Free(block)
end
