

-- Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
-- SPDX-License-Identifier: MIT

local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local sys = require("../../sys")
local ffi = require("../../ffi")
local task = require("../../../modules/task")

-- Platform-specific fixtures. The FFI surface itself is cross-platform;
-- only the loaded libraries and symbol names differ per OS.
local platform = sys.GetPlatform():lower()
local isWindows = platform:find("windows", 1, true) ~= nil
local isLinux = platform:find("linux", 1, true) ~= nil
local isMac = not isWindows and not isLinux
local declarationLibrary = isWindows and "kernel32" or (isMac and "libSystem.B.dylib" or "libc.so.6")

describe("ffi", function()
    describe("load", function()
        it("errors on missing library name", function()
            local ok, err = pcall(function()
                ffi.load("@definitely-not-a-real-library-xyz")
            end)
            expect(ok).toBe(false)
            expect(type(err)).toBe("string")
        end)

        it("errors on non-string declarations", function()
            local ok = pcall(function()
                ffi.load("kernel32", 42)
            end)
            expect(ok).toBe(false)
        end)

        it("errors on malformed declarations with line info", function()
            local ok, err = pcall(function()
                ffi.load(declarationLibrary, [[
                    int Broken(void);
                    int ???(int);
                ]])
            end)
            expect(ok).toBe(false)
            expect(type(err)).toBe("string")
        end)

        it("errors on unknown symbols at load time", function()
            local ok, err = pcall(function()
                if isWindows then
                    return ffi.load("kernel32", "int ThisSymbolDoesNotExist12345(void);\n")
                end
                return ffi.load(isMac and "libm.dylib" or "libm.so.6",
                    "int ThisSymbolDoesNotExist12345(void);\n")
            end)
            expect(ok).toBe(false)
            expect(type(err)).toBe("string")
        end)

        it("binds against the current process with ffi.C", function()
            local ok, err = pcall(function()
                return ffi.C("int FfiCurrentProcessSymbolDoesNotExist12345(void);")
            end)
            expect(ok).toBe(false)
            expect(type(err)).toBe("string")
        end)

        it("validates ffi.C declarations", function()
            expect(pcall(ffi.C, 42)).toBe(false)
            expect(pcall(ffi.C, "int MissingCurrentSymbol12345(void);", "extra")).toBe(false)
        end)

        it("rejects invalid load options", function()
            expect(pcall(ffi.load, "kernel32", nil, { searchDllDirectory = "yes" })).toBe(false)
            expect(pcall(ffi.load, "kernel32", nil, { typo = true })).toBe(false)
        end)

        it("errors on too many parameters", function()
            local decl = "int f(" .. table.concat(
                { "int", "int", "int", "int", "int", "int", "int", "int",
                  "int", "int", "int", "int", "int", "int", "int", "int", "int" },
                ", ") .. ");"
            local ok = pcall(ffi.load, isWindows and "kernel32" or (isMac and "libm.dylib" or "libm.so.6"), decl)
            expect(ok).toBe(false)
        end)

        it("rejects unsupported function pointer declarations", function()
            local ok = pcall(ffi.load, isWindows and "kernel32" or (isMac and "libm.dylib" or "libm.so.6"),
                "int (*callback)(int);")
            expect(ok).toBe(false)
        end)

        it("rejects void mixed with other parameters", function()
            local ok = pcall(ffi.load, isWindows and "kernel32" or (isMac and "libm.dylib" or "libm.so.6"),
                "int invalid(void, int);")
            expect(ok).toBe(false)
        end)

        it("supports qualified and signed scalar declarations", function()
            local lib = ffi.load(isWindows and "ucrtbase" or (isMac and "libSystem.B.dylib" or "libc.so.6"), [[
                signed int abs(signed int value);
            ]])
            expect(lib.abs(-17)).toBe(17)
        end)

        it("reports the final source line for truncated declarations", function()
            local ok, err = pcall(ffi.C, "\n\nint Truncated(")
            expect(ok).toBe(false)
            -- macOS may prepend a dyld error when opening the current image;
            -- the parser line detail is asserted on Linux and Windows.
            if not isMac then
                expect(tostring(err):find("line 3", 1, true) ~= nil).toBe(true)
            else
                expect(type(err)).toBe("string")
            end
        end)

        it("rejects structs and unions passed by value", function()
            expect(pcall(ffi.C, "int InvalidStruct(struct Native value);")).toBe(false)
            expect(pcall(ffi.C, "int InvalidUnion(union Native value);")).toBe(false)
        end)

        it("rejects opaque aggregate typedefs passed by value", function()
            local ok, err = pcall(ffi.C, [[
                typedef struct OpaqueHandle OpaqueHandle;
                int InvalidOpaqueValue(OpaqueHandle value);
            ]])
            expect(ok).toBe(false)
            if not isMac then
                expect(tostring(err):find("values by value are not supported", 1, true) ~= nil).toBe(true)
            else
                expect(type(err)).toBe("string")
            end
        end)
    end)

    describe("typed storage", function()
        it("registers named structs and keeps pointer fields opaque", function()
            ffi.cdef([[typedef struct { int32_t x; uint32_t y; void* pointer; } FfiTypedPoint;]])
            local point = ffi.new("FfiTypedPoint")
            point.x = -7
            point.y = 42
            local backing = ffi.new(8)
            point.pointer = ffi.addr(backing)
            expect(point.x).toBe(-7)
            expect(point.y).toBe(42)
            expect(point.pointer ~= nil).toBe(true)
        end)

        it("keeps aliases, unions, sizes, and compatible declarations per State", function()
            ffi.cdef([[typedef uint32_t FfiDword;]])
            ffi.cdef([[typedef union { uint32_t value; uint8_t bytes[4]; } FfiTypedUnion;]])
            ffi.cdef([[typedef union { uint32_t value; uint8_t bytes[4]; } FfiTypedUnion;]])
            expect(ffi.sizeof("FfiDword")).toBe(4)
            expect(ffi.sizeof("FfiTypedUnion")).toBe(4)
            local value = ffi.new("FfiDword")
            expect(buffer.len(value)).toBe(4)
            local union = ffi.new("FfiTypedUnion", { value = 0x12345678 })
            expect(union.value).toBe(0x12345678)
            expect(pcall(ffi.cdef, [[typedef union { uint64_t value; } FfiTypedUnion;]])).toBe(false)
        end)

        it("reads and writes through strict opaque pointers", function()
            local bytes = ffi.new(16)
            local p = ffi.addr(bytes)
            ffi.write(p, "uint32_t", 0x12345678)
            expect(ffi.read(p, "uint32_t")).toBe(0x12345678)
            ffi.writePointer(p, 8, p)
            expect(ffi.readPointer(p, 8) ~= nil).toBe(true)
            expect(pcall(ffi.addr, 123)).toBe(false)
            expect(pcall(ffi.read, bytes, "uint32_t", -1)).toBe(false)
            expect(pcall(ffi.read, bytes, "uint32_t", 1)).toBe(false)
            expect(pcall(ffi.write, bytes, "uint64_t", 1, 12)).toBe(false)
            expect(pcall(ffi.writePointer, bytes, -1, p)).toBe(false)
        end)

        it("posts work on the event loop", function()
            local ran = false
            ffi.post(function()
                ran = true
            end)
            task.wait(0)
            task.wait(0)
            expect(ran).toBe(true)
        end)
    end)

    it("accepts anonymous callback declarations", function()
        local callback = ffi.callback("int32_t(int32_t)", function(value)
            return value
        end)
        expect(callback.pointer ~= nil).toBe(true)
        expect(callback.state().closed).toBe(false)
        callback.close()
        expect(callback.state().closed).toBe(true)
        expect(pcall(ffi.addr, callback)).toBe(false)
    end)

    it("registers and resolves named function-pointer types", function()
        ffi.cdef("typedef int32_t (*FfiUnaryCallback)(int32_t);")
        local callback = ffi.callback("FfiUnaryCallback", function(value)
            return value + 1
        end)
        expect(callback.pointer ~= nil).toBe(true)
        callback.close()
    end)

    if isWindows then
        describe("windows calls", function()
                local k32 = ffi.load("kernel32", [[
                unsigned long GetCurrentProcessId(void);
                unsigned long GetTickCount(void);
                uint64_t GetTickCount64(void);
                int lstrlenA(const char* s);
                int lstrcmpA(const char* left, const char* right);
                void* GetModuleHandleA(const char* name);
                unsigned long GetModuleFileNameW(void* module, wchar_t* filename, unsigned long size);
                void SetLastError(unsigned long error);
                void Sleep(unsigned long milliseconds);
                void* CreateThread(void* attributes, size_t stackSize, uint32_t (*start)(void*), void* parameter, uint32_t flags, uint32_t* threadId);
                uint32_t WaitForSingleObject(void* handle, uint32_t milliseconds);
                int CloseHandle(void* handle);
            ]])
                local ucrt = ffi.load("ucrtbase", [[
                double pow(double x, double y);
                size_t strnlen(const char* s, size_t maxlen);
                float sqrtf(float x);
                int abs(int value);
            ]])

            it("returns the current process id", function()
                expect(k32.GetCurrentProcessId()).toBe(sys.GetPid())
            end)

            it("returns a monotonic tick count", function()
                local t = k32.GetTickCount()
                expect(type(t)).toBe("number")
                expect(t > 0).toBe(true)
            end)

            it("computes integer results from string args", function()
                expect(k32.lstrlenA("hello")).toBe(5)
            end)

            it("handles empty and embedded-NUL strings", function()
                expect(k32.lstrlenA("")).toBe(0)
                expect(k32.lstrlenA("a\0b")).toBe(1)
            end)

            it("converts UTF-16 buffers for Windows APIs", function()
                local wide = ffi.wbuffer("Lode UTF-16 ✓")
                expect(ffi.wstring(wide)).toBe("Lode UTF-16 ✓")
                local path = ffi.new(1024 * 2)
                local length = k32.GetModuleFileNameW(nil, path, 1024)
                expect(length > 0).toBe(true)
                expect(#ffi.wstring(path, length) > 0).toBe(true)
            end)

            it("supports signed integer arguments and results", function()
                expect(ucrt.abs(-42)).toBe(42)
                expect(k32.lstrcmpA("b", "a") > 0).toBe(true)
                expect(k32.lstrcmpA("a", "b") < 0).toBe(true)
            end)

            it("supports 32-bit and 64-bit integer returns", function()
                local ticks32 = k32.GetTickCount()
                local ticks64 = k32.GetTickCount64()
                expect(ticks32 > 0).toBe(true)
                expect(ticks64 >= ticks32).toBe(true)
            end)

            it("supports void and float calls", function()
                k32.Sleep(0)
                expect(ucrt.sqrtf(81)).toBe(9)
            end)

            it("captures the thread last-error code", function()
                k32.SetLastError(1234)
                expect(ffi.lastError()).toBe(1234)
            end)

            it("runs blocking calls off the event loop", function()
                local advanced = false
                task.delay(0.001, function()
                    advanced = true
                end)
                k32.callAsync("Sleep", 25)
                expect(advanced).toBe(true)
            end)

            it("returns values from asynchronous calls", function()
                expect(ucrt.callAsync("pow", 2, 10)).toBe(1024)
            end)

            it("posts foreign-thread callbacks onto the runtime event loop", function()
                local called = false
                local callback = ffi.callback("uint32_t(void*)", function()
                    called = true
                    return 7
                end, { foreignThread = "post" })
                local thread = k32.CreateThread(nil, 0, callback, nil, 0, nil)
                expect(thread ~= nil).toBe(true)
                expect(k32.WaitForSingleObject(thread, 1000)).toBe(0)
                task.wait(0)
                task.wait(0)
                expect(called).toBe(true)
                expect(callback.foreignCalls()).toBe(1)
                expect(callback.state().postedForeignCalls).toBe(1)
                expect(callback.state().rejectedForeignCalls).toBe(0)
                expect(k32.CloseHandle(thread) ~= 0).toBe(true)
                callback.close()
            end)

            it("rejects foreign-thread callbacks without entering Luau", function()
                local called = false
                local callback = ffi.callback("uint32_t(void*)", function()
                    called = true
                    return 7
                end)
                local thread = k32.CreateThread(nil, 0, callback, nil, 0, nil)
                expect(thread ~= nil).toBe(true)
                expect(k32.WaitForSingleObject(thread, 1000)).toBe(0)
                task.wait(0)
                expect(called).toBe(false)
                expect(callback.state().foreignCalls).toBe(1)
                expect(callback.state().postedForeignCalls).toBe(0)
                expect(callback.state().rejectedForeignCalls).toBe(1)
                expect(k32.CloseHandle(thread) ~= 0).toBe(true)
                callback.close()
            end)

            it("validates asynchronous function names and arity", function()
                expect(pcall(k32.callAsync, "notBound")).toBe(false)
                expect(pcall(k32.callAsync, "Sleep")).toBe(false)
            end)

            it("parses comments and whitespace between declarations", function()
                local commented = ffi.load("kernel32", [[
                    // The parser must ignore line comments.
                    unsigned long GetCurrentProcessId ( void );
                    /* Block comments may split declarations too. */
                    unsigned long GetTickCount( void );
                ]])
                expect(commented.GetCurrentProcessId()).toBe(sys.GetPid())
                expect(commented.GetTickCount() > 0).toBe(true)
            end)

            it("treats fixed array parameters as pointers", function()
                local arrays = ffi.load("kernel32", "int lstrlenA(const char text[32]);")
                expect(arrays.lstrlenA("array")).toBe(5)
            end)

            it("resolves simple typedef aliases", function()
                local aliases = ffi.load("kernel32", [[
                    typedef unsigned long DWORD;
                    DWORD GetCurrentProcessId(void);
                ]])
                expect(aliases.GetCurrentProcessId()).toBe(sys.GetPid())
            end)

            it("accepts Windows calling-convention markers on x64", function()
                local conventions = ffi.load("kernel32", [[
                    typedef unsigned long DWORD;
                    DWORD WINAPI GetCurrentProcessId(void);
                ]])
                expect(conventions.GetCurrentProcessId()).toBe(sys.GetPid())
            end)

            it("accepts enum typedefs as 32-bit integer ABI values", function()
                local enums = ffi.load("kernel32", [[
                    typedef enum FixtureStatus {
                        FixtureStatusOk = 0,
                        FixtureStatusFailed = 1 << 4,
                    } FixtureStatus;
                    FixtureStatus GetCurrentProcessId(void);
                ]])
                expect(enums.GetCurrentProcessId()).toBe(sys.GetPid())
            end)

            it("accepts buffers as zero-copy pointer args", function()
                local buf = buffer.fromstring("abcd")
                -- Buffers are not NUL-terminated, so use the bounded variant.
                expect(ucrt.strnlen(buf, 4)).toBe(4)
            end)

            it("returns nil for NULL pointers", function()
                -- A name that is definitely not a loaded module resolves to NULL.
                expect(k32.GetModuleHandleA("definitely-not-loaded-xyz.dll")).toBe(nil)
            end)

            it("returns lightuserdata pointers", function()
                local handle = k32.GetModuleHandleA("kernel32.dll")
                expect(handle ~= nil).toBe(true)
            end)

            it("computes double results", function()
                local r = ucrt.pow(2, 10)
                expect(r).toBe(1024)
            end)

            it("enforces arity", function()
                local ok = pcall(k32.lstrlenA)
                expect(ok).toBe(false)
                local ok2 = pcall(k32.lstrlenA, "a", "b")
                expect(ok2).toBe(false)
            end)

            it("rejects wrong argument types", function()
                local ok = pcall(k32.lstrlenA, {})
                expect(ok).toBe(false)
            end)

            it("keeps bound functions safe after Close", function()
                local temp = ffi.load("kernel32", "unsigned long GetCurrentProcessId(void);")
                temp.Close()
                -- The closure stays callable (the handle remains resident);
                -- this must not crash.
                expect(temp.GetCurrentProcessId()).toBe(sys.GetPid())
            end)

            it("supports the lowercase close alias and multiple bindings", function()
                local temp = ffi.load("kernel32", [[
                    unsigned long GetCurrentProcessId(void);
                    unsigned long GetTickCount(void);
                ]])
                temp.close()
                expect(temp.GetCurrentProcessId()).toBe(sys.GetPid())
                expect(temp.GetTickCount() > 0).toBe(true)
            end)

            it("supports a library handle without declarations", function()
                local temp = ffi.load("kernel32")
                temp.Close()
                expect(type(temp.close)).toBe("function")
            end)
        end)
    elseif isLinux or isMac then
        describe("posix calls", function()
            local libc = ffi.load(isMac and "libSystem.B.dylib" or "libc.so.6", [[
                size_t strlen(const char* s);
                size_t strnlen(const char* s, size_t maxlen);
                int atoi(const char* s);
                void* malloc(size_t size);
                void free(void* ptr);
            ]])
            local libm = ffi.load(isMac and "libSystem.B.dylib" or "libm.so.6", [[
                double pow(double x, double y);
            ]])

            it("computes strlen over strings", function()
                expect(libc.strlen("hello")).toBe(5)
            end)

            it("accepts buffers as zero-copy pointer args", function()
                local buf = buffer.fromstring("abcd")
                -- Buffers are not NUL-terminated, so use the bounded variant.
                expect(libc.strnlen(buf, 4)).toBe(4)
            end)

            it("parses integers from strings", function()
                expect(libc.atoi("42")).toBe(42)
            end)

            it("computes double results", function()
                expect(libm.pow(2, 8)).toBe(256)
            end)

            it("returns pointers and accepts numeric addresses", function()
                local p = libc.malloc(64)
                expect(p ~= nil).toBe(true)
                libc.free(p)
            end)

            it("enforces arity", function()
                local ok = pcall(libc.strlen)
                expect(ok).toBe(false)
            end)

            it("rejects wrong argument types", function()
                local ok = pcall(libc.strlen, {})
                expect(ok).toBe(false)
            end)

            it("supports void calls and signed results", function()
                libc.free(libc.malloc(1))
                expect(libc.atoi("-17")).toBe(-17)
            end)
        end)
    else
        it("has platform coverage", function()
            expect(isWindows or isLinux or isMac).toBe(true)
        end)
    end
end)
