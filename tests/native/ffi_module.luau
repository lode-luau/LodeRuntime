--!strict

-- CTest regression for the public PascalCase FFI, Address, and Memory APIs.
local function check(condition: boolean, message: string)
    if not condition then
        error("ffi_module: " .. message, 2)
    end
end

local FFI = require("@ffi")
local sys = require("@sys")
local fixturePath = sys.Args()[1]
local isWindows = sys.GetPlatform():lower():find("windows", 1, true) ~= nil

check(type(fixturePath) == "string" and #fixturePath > 0, "CTest must pass the fixture library path")

local fixture = FFI.Load(fixturePath, [[
    int32_t ffi_fixture_add_i32(int32_t left, int32_t right);
    int32_t ffi_fixture_call_dependency(int32_t value);
    typedef struct { int32_t left; uint16_t right; } FixturePair;
    int32_t ffi_fixture_pair_sum(const FixturePair* pair);
    int32_t ffi_fixture_pair_sum_value(FixturePair pair);
    FixturePair ffi_fixture_pair_make(int32_t left, uint16_t right);
    typedef struct { uint16_t values[3]; int32_t tail; } FixtureArray;
    int32_t ffi_fixture_array_sum(FixtureArray value);
    typedef union { int32_t integer; float decimal; } FixtureUnion;
    int32_t ffi_fixture_union_integer(const FixtureUnion* value);
    int32_t ffi_fixture_invoke_callback(int32_t (*callback)(int32_t), int32_t value);
    double ffi_fixture_invoke_double_callback(double (*callback)(double, double), double left, double right);
    int8_t ffi_fixture_negate_i8(int8_t value);
    uint8_t ffi_fixture_increment_u8(uint8_t value);
    bool ffi_fixture_bool_not(bool value);
    double ffi_fixture_scale_f64(double value, double factor);
    void* ffi_fixture_null(void);
    int32_t ffi_fixture_is_null(const void* pointer);
    uint32_t ffi_fixture_sum_bytes(const uint8_t* data, uint32_t count);
    void* ffi_fixture_allocate(uint32_t size);
    void ffi_fixture_release(void* pointer);
    int32_t ffi_fixture_allocation_count(void);
    uint64_t ffi_fixture_large_u64(void);
    int64_t ffi_fixture_large_i64(void);
    uint64_t ffi_fixture_identity_u64(uint64_t value);
    void ffi_fixture_set_last_error(int32_t code);
]], if isWindows then { SearchDllDirectory = true } else nil)

check(fixture.ffi_fixture_add_i32(-8, 13) == 5, "int32 result mismatch")
check(fixture.ffi_fixture_call_dependency(6) == 42, "dependent library result mismatch")
local pair = FFI.Struct("int32_t left; uint16_t right;")
check(buffer.len(pair) == 8, "struct layout size mismatch")
buffer.writei32(pair, 0, 40)
buffer.writeu16(pair, 4, 2)
check(fixture.ffi_fixture_pair_sum(pair) == 42, "struct pointer result mismatch")
check(fixture.ffi_fixture_pair_sum_value(pair) == 42, "struct value result mismatch")
local madePair = fixture.ffi_fixture_pair_make(39, 3) :: buffer
check(buffer.readi32(madePair, 0) == 39 and buffer.readu16(madePair, 4) == 3,
    "struct return value mismatch")
local array = FFI.Struct("uint16_t values[3]; int32_t tail;")
buffer.writeu16(array, 0, 10)
buffer.writeu16(array, 2, 20)
buffer.writeu16(array, 4, 30)
buffer.writei32(array, 8, 40)
check(fixture.ffi_fixture_array_sum(array) == 100, "array struct value mismatch")
local union = FFI.Union("int32_t integer; float decimal;")
buffer.writei32(union, 0, 42)
check(fixture.ffi_fixture_union_integer(union) == 42, "union pointer result mismatch")
check(fixture.ffi_fixture_negate_i8(-97) == 97, "int8 result mismatch")
check(fixture.ffi_fixture_increment_u8(254) == 255, "uint8 result mismatch")
check(fixture.ffi_fixture_bool_not(true) == 0, "bool result mismatch")
check(fixture.ffi_fixture_scale_f64(1.25, 8.0) == 10.0, "double result mismatch")
check(fixture.ffi_fixture_null() == nil, "NULL pointer must become nil")
check(fixture.ffi_fixture_is_null(nil) == 1, "NULL pointer argument mismatch")
check(not pcall(function() fixture.ffi_fixture_is_null(1) end), "numeric addresses must be rejected")

local callback = FFI.Callback("int32_t increment(int32_t value);", function(value: number): number
    return value + 1
end)
check(fixture.ffi_fixture_invoke_callback(callback, 41) == 42, "callback result mismatch")
callback.Close()

local scale = FFI.Callback("double scale(double left, double right);", function(left: number, right: number): number
    return left * right
end)
check(fixture.ffi_fixture_invoke_double_callback(scale.Pointer, 1.5, 8.0) == 12.0, "double callback result mismatch")
scale.Close()

local bytes = FFI.new(8) :: buffer
check(fixture.ffi_fixture_sum_bytes(bytes, 8) == 0, "raw FFI storage mismatch")

local allocation = fixture.ffi_fixture_allocate(32)
check(allocation ~= nil, "fixture allocation must return a pointer")
check(fixture.ffi_fixture_allocation_count() == 1, "fixture allocation count mismatch")
fixture.ffi_fixture_release(allocation)
check(fixture.ffi_fixture_allocation_count() == 0, "fixture allocation release mismatch")

local largeUnsigned = fixture.ffi_fixture_large_u64() :: buffer
local largeSigned = fixture.ffi_fixture_large_i64() :: buffer
check(type(largeUnsigned) == "buffer" and buffer.len(largeUnsigned) == 8, "uint64 binary result mismatch")
check(type(largeSigned) == "buffer" and buffer.len(largeSigned) == 8, "int64 binary result mismatch")
check(buffer.readu32(largeUnsigned, 0) == 1 and buffer.readu32(largeUnsigned, 4) == 0x200000,
    "uint64 binary contents mismatch")
check(buffer.readu32(fixture.ffi_fixture_identity_u64(largeUnsigned) :: buffer, 0) == 1,
    "uint64 binary argument mismatch")

check(fixture.CallAsync("ffi_fixture_add_i32", 20, 22) == 42, "async result mismatch")
fixture.ffi_fixture_set_last_error(1234)
check(FFI.LastError() == 1234, "LastError mismatch")

print("ffi_module: all checks passed")
