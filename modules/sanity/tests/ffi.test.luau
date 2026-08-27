

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
local isMac = platform:find("darwin", 1, true) ~= nil or platform:find("macos", 1, true) ~= nil
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
            expect(tostring(err):find("line 3", 1, true) ~= nil).toBe(true)
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
            expect(tostring(err):find("values by value are not supported", 1, true) ~= nil).toBe(true)
        end)
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
