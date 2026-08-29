-- Manual Win32 FFI homologation. Run on Windows x64 with:
--   build/bin/Debug/lode.exe project-m/homolog/ffi-win32-window.luau
-- This remains outside sanity because it exercises user32's real window and
-- message infrastructure.

local FFI = require("@ffi")
local Address = require("@address")
local Memory = require("@memory")

FFI.CDef([[
    typedef int64_t (CALLBACK *WNDPROC)(void* hwnd, uint32_t message,
        void* wParam, void* lParam);
    typedef struct {
        uint32_t cbSize;
        uint32_t style;
        WNDPROC lpfnWndProc;
        int32_t cbClsExtra;
        int32_t cbWndExtra;
        void* hInstance;
        void* hIcon;
        void* hCursor;
        void* hbrBackground;
        void* lpszMenuName;
        void* lpszClassName;
        void* hIconSm;
    } WNDCLASSEXW;
]])

local kernel32 = FFI.Load("kernel32", [[
    void* GetModuleHandleW(void* moduleName);
]])

local user32 = FFI.Load("user32", [[
    uint16_t RegisterClassExW(WNDCLASSEXW* windowClass);
    int32_t UnregisterClassW(void* className, void* instance);
    void* CreateWindowExW(uint32_t exStyle, void* className, void* title,
        uint32_t style, int32_t x, int32_t y, int32_t width, int32_t height,
        void* parent, void* menu, void* instance, void* parameter);
    int32_t ShowWindow(void* window, int32_t command);
    int32_t UpdateWindow(void* window);
    int32_t DestroyWindow(void* window);
    int32_t PeekMessageW(void* message, void* window, uint32_t minimum,
        uint32_t maximum, uint32_t remove);
    int32_t TranslateMessage(void* message);
    int64_t DispatchMessageW(void* message);
    int64_t DefWindowProcW(void* window, uint32_t message, void* wParam, void* lParam);
    void PostQuitMessage(int32_t exitCode);
    void* BeginPaint(void* window, void* paintStruct);
    int32_t EndPaint(void* window, void* paintStruct);
]])

local className = Address.WBuffer("LodeFfiHomologWindow")
local title = Address.WBuffer("Lode FFI homologation")
local instance = kernel32.GetModuleHandleW(nil)
local callbackCalls = 0
local paintCalls = 0

local windowProc = FFI.Callback("WNDPROC", function(hwnd, message, wParam, lParam)
    callbackCalls += 1
    -- WM_PAINT: BeginPaint and EndPaint must be balanced on the same path.
    if message == 0x000F then
        local paint = Memory.new(128, { Zeroed = true })
        user32.BeginPaint(hwnd, Address.Of(paint))
        paintCalls += 1
        user32.EndPaint(hwnd, Address.Of(paint))
        Memory.Free(paint)
        return 0
    end
    -- WM_DESTROY: signal the message loop and let DefWindowProc handle the
    -- remaining non-client behavior (hit testing, sizing, activation, etc.).
    if message == 0x0002 then
        user32.PostQuitMessage(0)
        return 0
    end
    return user32.DefWindowProcW(hwnd, message, wParam, lParam)
end)

local windowClass = FFI.new("WNDCLASSEXW", {
    cbSize = FFI.SizeOf("WNDCLASSEXW"),
    lpfnWndProc = windowProc,
    hInstance = instance,
    lpszClassName = Address.Of(className),
})

assert(FFI.SizeOf("WNDCLASSEXW") == 80, "unexpected WNDCLASSEXW layout")
assert(FFI.OffsetOf("WNDCLASSEXW", "lpfnWndProc") == 8, "unexpected WNDPROC offset")
assert(user32.RegisterClassExW(windowClass) ~= 0,
    "RegisterClassExW failed: " .. tostring(FFI.LastError()))

local window = user32.CreateWindowExW(
    0, Address.Of(className), Address.Of(title), 0x00CF0000,
    0, 0, 320, 180, nil, nil, instance, nil
)
assert(window ~= nil, "CreateWindowExW failed: " .. tostring(FFI.LastError()))
user32.ShowWindow(window, 5)
user32.UpdateWindow(window)

local message = Memory.new(64, { Zeroed = true })
while user32.PeekMessageW(Address.Of(message), nil, 0, 0, 1) ~= 0 do
    user32.TranslateMessage(Address.Of(message))
    user32.DispatchMessageW(Address.Of(message))
end

assert(user32.DestroyWindow(window) ~= 0, "DestroyWindow failed")
-- Drain WM_DESTROY/WM_NCDESTROY before unregistering the class and closing
-- the callback retained by the window procedure.
while user32.PeekMessageW(Address.Of(message), nil, 0, 0, 1) ~= 0 do
    user32.TranslateMessage(Address.Of(message))
    user32.DispatchMessageW(Address.Of(message))
end

assert(user32.UnregisterClassW(Address.Of(className), instance) ~= 0,
    "UnregisterClassW failed: " .. tostring(FFI.LastError()))
assert(callbackCalls > 0 and paintCalls > 0, "WNDPROC/WM_PAINT was not called")
windowProc.Close()
windowProc.Close()
Memory.Free(message)
print("Win32 FFI homologation passed")
