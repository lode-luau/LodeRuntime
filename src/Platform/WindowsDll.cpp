#if defined(_WIN32)

#include "Platform.hpp"
#include <windows.h>
#include <vector>

namespace Lode::Platform
{

class WindowsDynamicLibrary : public DynamicLibrary
{
public:
    explicit WindowsDynamicLibrary(HMODULE handle) : handle_(handle) {}

    ~WindowsDynamicLibrary() override
    {
        Close();
    }

    Result<void*> GetSymbol(std::string_view name) const override
    {
        if (!handle_)
            return Error::Platform("Dynamic library handle is invalid");

        std::string symName(name);
        FARPROC proc = GetProcAddress(handle_, symName.c_str());
        if (!proc)
        {
            return Error::Platform("Failed to find symbol: " + symName);
        }
        return reinterpret_cast<void*>(proc);
    }

    void Close() override
    {
        if (handle_)
        {
            FreeLibrary(handle_);
            handle_ = nullptr;
        }
    }

private:
    HMODULE handle_ = nullptr;
};

Result<std::shared_ptr<DynamicLibrary>> DynamicLibrary::Open(std::string_view path)
{
    std::string pathStr(path);
    std::vector<wchar_t> wPath(pathStr.length() + 1);
    MultiByteToWideChar(CP_UTF8, 0, pathStr.c_str(), -1, wPath.data(), static_cast<int>(wPath.size()));

    HMODULE handle = LoadLibraryW(wPath.data());
    if (!handle)
    {
        DWORD err = GetLastError();
        return Error::Platform("Failed to load Windows DLL: " + pathStr + " (Error code: " + std::to_string(err) + ")");
    }

    std::shared_ptr<DynamicLibrary> lib = std::make_shared<WindowsDynamicLibrary>(handle);
    return lib;
}

} // namespace Lode::Platform

#endif
