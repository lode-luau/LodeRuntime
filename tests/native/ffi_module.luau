--!strict

-- CTest regression for the ffi module.  All symbols come from the local
-- fixture, so it has the same behavior on glibc, musl, macOS, and Windows.
local function check(condition: boolean, message: string)
    if not condition then
        error("ffi_module: " .. message, 2)
    end
end

local ffi = require("@ffi")
local sys = require("@sys")
local args = sys.Args()
local fixturePath = args[1]
local useDllDirectory = sys.GetPlatform():lower():find("windows", 1, true) ~= nil

check(type(fixturePath) == "string" and #fixturePath > 0,
    "CTest must pass the ffi fixture library path")

local fixture = ffi.load(fixturePath, [[
    typedef struct OpaqueHandle OpaqueHandle;
    int32_t ffi_fixture_add_i32(int32_t left, int32_t right);
    int32_t ffi_fixture_call_dependency(int32_t value);
    typedef struct {
        int32_t left;
        uint16_t right;
    } FixturePair;
    int32_t ffi_fixture_pair_sum(const FixturePair* pair);
    int32_t ffi_fixture_pair_sum_value(FixturePair pair);
    FixturePair ffi_fixture_pair_make(int32_t left, uint16_t right);
    typedef struct {
        uint16_t values[3];
        int32_t tail;
    } FixtureArray;
    int32_t ffi_fixture_array_sum(FixtureArray value);
    typedef struct {
        FixturePair pair;
        int32_t extra;
    } FixtureNested;
    int32_t ffi_fixture_nested_sum(FixtureNested value);
    typedef union {
        int32_t integer;
        float decimal;
    } FixtureUnion;
    int32_t ffi_fixture_union_integer(const FixtureUnion* value);
    const char* ffi_fixture_greeting(void);
    int32_t ffi_fixture_invoke_callback(int32_t (*callback)(int32_t), int32_t value);
    double ffi_fixture_invoke_double_callback(double (*callback)(double, double), double left, double right);
    const char* ffi_fixture_invoke_string_callback(const char* (*callback)(const char*), const char* value);
    int8_t ffi_fixture_negate_i8(int8_t value);
    uint8_t ffi_fixture_increment_u8(uint8_t value);
    int16_t ffi_fixture_negate_i16(int16_t value);
    uint16_t ffi_fixture_increment_u16(uint16_t value);
    bool ffi_fixture_bool_not(bool value);
    double ffi_fixture_scale_f64(double value, double factor);
    void* ffi_fixture_null(void);
    int32_t ffi_fixture_is_null(const OpaqueHandle* pointer);
    uint32_t ffi_fixture_sum_bytes(const uint8_t* data, uint32_t count);
    void* ffi_fixture_allocate(uint32_t size);
    void ffi_fixture_release(void* pointer);
    int32_t ffi_fixture_allocation_count(void);
    uint64_t ffi_fixture_large_u64(void);
    int64_t ffi_fixture_large_i64(void);
    uint64_t ffi_fixture_identity_u64(uint64_t value);
    void ffi_fixture_set_last_error(int32_t code);
]], if useDllDirectory then { searchDllDirectory = true } else nil)

check(fixture.ffi_fixture_add_i32(-8, 13) == 5, "int32 result mismatch")
check(fixture.ffi_fixture_call_dependency(6) == 42, "dependent library result mismatch")
local pairFields = "int32_t left; uint16_t right;"
local pair = ffi.struct(pairFields)
check(buffer.len(pair) == 8, "struct layout size mismatch")
check(ffi.offsetof(pairFields, "left") == 0, "first struct offset mismatch")
check(ffi.offsetof(pairFields, "right") == 4, "aligned struct offset mismatch")
buffer.writei32(pair, ffi.offsetof(pairFields, "left"), 40)
buffer.writeu16(pair, ffi.offsetof(pairFields, "right"), 2)
check(fixture.ffi_fixture_pair_sum(pair) == 42, "struct pointer result mismatch")
check(fixture.ffi_fixture_pair_sum_value(pair) == 42, "struct value result mismatch")
check(fixture.callAsync("ffi_fixture_pair_sum_value", pair) == 42,
    "async struct value result mismatch")
local madePair = fixture.ffi_fixture_pair_make(39, 3) :: buffer
check(buffer.readi32(madePair, 0) == 39 and buffer.readu16(madePair, 4) == 3,
    "struct return value mismatch")
local asyncPair = fixture.callAsync("ffi_fixture_pair_make", 38, 4) :: buffer
check(buffer.readi32(asyncPair, 0) == 38 and buffer.readu16(asyncPair, 4) == 4,
    "async struct return value mismatch")
local arrayFields = "uint16_t values[3]; int32_t tail;"
local arrayValue = ffi.struct(arrayFields)
check(buffer.len(arrayValue) == 12, "array struct layout size mismatch")
check(ffi.offsetof(arrayFields, "values") == 0 and ffi.offsetof(arrayFields, "tail") == 8,
    "array struct offsets mismatch")
buffer.writeu16(arrayValue, 0, 10)
buffer.writeu16(arrayValue, 2, 20)
buffer.writeu16(arrayValue, 4, 30)
buffer.writei32(arrayValue, 8, 40)
check(fixture.ffi_fixture_array_sum(arrayValue) == 100, "array struct value mismatch")
local nestedFields = "int32_t left; uint16_t right; int32_t extra;"
local nestedValue = ffi.struct(nestedFields)
buffer.writei32(nestedValue, 0, 20)
buffer.writeu16(nestedValue, 4, 2)
buffer.writei32(nestedValue, 8, 20)
check(fixture.ffi_fixture_nested_sum(nestedValue) == 42, "nested struct value mismatch")
local unionValue = ffi.union("int32_t integer; float decimal;")
check(buffer.len(unionValue) == 4, "union layout size mismatch")
buffer.writei32(unionValue, 0, 42)
check(fixture.ffi_fixture_union_integer(unionValue) == 42, "union pointer value mismatch")
local greeting = fixture.ffi_fixture_greeting()
check(ffi.string(greeting) == "ffi fixture", "NUL-terminated string copy mismatch")
check(ffi.string(greeting, 3) == "ffi", "sized string copy mismatch")
check(buffer.tostring(ffi.copy(greeting, 3)) == "ffi", "pointer buffer copy mismatch")
local callback = ffi.callback("int32_t increment(int32_t value);", function(value: number): number
    return value + 1
end)
check(fixture.ffi_fixture_invoke_callback(callback, 41) == 42, "callback result mismatch")
local doubleCallback = ffi.callback("double scale(double left, double right);", function(left: number, right: number): number
    return left * right
end)
check(fixture.ffi_fixture_invoke_double_callback(doubleCallback, 1.5, 8.0) == 12.0,
    "double callback result mismatch")
local callbackText = buffer.fromstring("callback text\0")
local stringCallback = ffi.callback("const char* select_text(const char* input);", function(_: any): any
    return callbackText
end)
local callbackResult = fixture.ffi_fixture_invoke_string_callback(stringCallback, "ignored")
check(ffi.string(callbackResult) == "callback text", "pointer callback result mismatch")
check(fixture.ffi_fixture_negate_i8(-97) == 97, "int8 result mismatch")
check(fixture.ffi_fixture_increment_u8(254) == 255, "uint8 result mismatch")
check(fixture.ffi_fixture_negate_i16(-12345) == 12345, "int16 result mismatch")
check(fixture.ffi_fixture_increment_u16(65534) == 65535, "uint16 result mismatch")
check(fixture.ffi_fixture_bool_not(true) == 0, "bool false result mismatch")
check(fixture.ffi_fixture_bool_not(false) == 1, "bool true result mismatch")
local asyncI8 = fixture.callAsync("ffi_fixture_negate_i8", -64)
check(asyncI8 == 64, "async int8 result mismatch")

check(fixture.ffi_fixture_scale_f64(1.25, 8.0) == 10.0, "double result mismatch")
check(fixture.ffi_fixture_null() == nil, "NULL pointer must become nil")
check(fixture.ffi_fixture_is_null(nil) == 1, "opaque struct pointer must accept NULL")

local bytes = buffer.fromstring("abc")
check(fixture.ffi_fixture_sum_bytes(bytes, 3) == 294, "buffer argument mismatch")
check(fixture.ffi_fixture_sum_bytes(ffi.ptr(bytes, 1), 2) == 197, "pointer offset mismatch")
local memory = ffi.new(4)
ffi.fill(memory, 7, 4)
check(fixture.ffi_fixture_sum_bytes(memory, 4) == 28, "memory fill mismatch")
ffi.move(ffi.ptr(memory, 1), bytes, 3)
check(buffer.tostring(memory) == string.char(7) .. "abc", "overlap-safe memory move mismatch")
check(fixture.callAsync("ffi_fixture_sum_bytes", bytes, 3) == 294,
    "async buffer argument mismatch")
check(fixture.callAsync("ffi_fixture_null") == nil, "async NULL pointer must become nil")

local raw = ffi.new(8)
check(type(raw) == "buffer", "ffi.new must return a buffer")
check(fixture.ffi_fixture_sum_bytes(raw, 8) == 0, "ffi.new memory must start zeroed")
check(ffi.sizeof("int32_t") == 4, "int32_t size mismatch")
check(ffi.sizeof("void*") == ffi.sizeof("uintptr_t"), "pointer size mismatch")
check(buffer.len(ffi.new("uint16_t")) == 2, "typed ffi.new size mismatch")

local allocation = fixture.ffi_fixture_allocate(32)
check(allocation ~= nil, "fixture allocation must return a pointer")
check(fixture.ffi_fixture_allocation_count() == 1, "fixture allocation count mismatch")
fixture.ffi_fixture_release(allocation)
check(fixture.ffi_fixture_allocation_count() == 0, "pointer must be released by its owning library")
fixture.ffi_fixture_set_last_error(1234)
check(ffi.lastError() == 1234, "ffi.lastError did not capture the native error code")
fixture.callAsync("ffi_fixture_set_last_error", 4321)
check(ffi.lastError() == 4321, "ffi.lastError did not receive the async native error code")

local largeUnsigned = fixture.ffi_fixture_large_u64() :: buffer
local largeSigned = fixture.ffi_fixture_large_i64() :: buffer
check(type(largeUnsigned) == "buffer" and buffer.len(largeUnsigned) == 8,
    "unsafe uint64 result must return an 8-byte buffer")
check(type(largeSigned) == "buffer" and buffer.len(largeSigned) == 8,
    "unsafe int64 result must return an 8-byte buffer")
check(buffer.readu32(largeUnsigned, 0) == 1 and buffer.readu32(largeUnsigned, 4) == 0x200000,
    "unsafe uint64 buffer contents mismatch")
check(buffer.readu32(fixture.ffi_fixture_identity_u64(largeUnsigned) :: buffer, 0) == 1,
    "64-bit buffer argument mismatch")

print("ffi_module: all checks passed")
