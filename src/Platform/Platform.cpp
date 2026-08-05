// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Platform.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <vector>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace Lode::Platform
{

OS GetOS()
{
#if defined(_WIN32)
    return OS::Windows;
#elif defined(__EMSCRIPTEN__)
    return OS::WASM;
#elif defined(__ANDROID__)
    return OS::Android;
#elif defined(__APPLE__)
    #if TARGET_OS_IPHONE || TARGET_OS_IOS
        return OS::IOS;
    #else
        return OS::MacOS;
    #endif
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__bsdi__)
    return OS::BSD;
#elif defined(__sun) || defined(__sun__) || defined(__SVR4)
    return OS::Solaris;
#elif defined(__HAIKU__)
    return OS::Haiku;
#elif defined(__linux__)
    return OS::Linux;
#else
    return OS::Unknown;
#endif
}

Architecture GetArchitecture()
{
#if defined(__wasm64__)
    return Architecture::wasm64;
#elif defined(__wasm32__) || defined(__EMSCRIPTEN__)
    return Architecture::wasm32;
#elif defined(__x86_64__) || defined(_M_X64)
    return Architecture::x64;
#elif defined(__aarch64__) || defined(_M_ARM64)
    return Architecture::arm64;
#elif defined(__i386__) || defined(_M_IX86)
    return Architecture::x86;
#else
    return Architecture::Unknown;
#endif
}

std::string_view GetOSName()
{
    switch (GetOS())
    {
    case OS::Windows: return "windows";
    case OS::MacOS:   return "macos";
    case OS::IOS:     return "ios";
    case OS::Linux:   return "linux";
    case OS::Android: return "android";
    case OS::BSD:     return "freebsd";
    case OS::Solaris: return "solaris";
    case OS::Haiku:   return "haiku";
    case OS::WASM:    return "wasm";
    default:          return "unknown";
    }
}

std::string_view GetArchitectureName()
{
    switch (GetArchitecture())
    {
    case Architecture::x64:    return "x64";
    case Architecture::arm64:  return "arm64";
    case Architecture::x86:    return "x86";
    case Architecture::wasm32: return "wasm32";
    case Architecture::wasm64: return "wasm64";
    default:                   return "unknown";
    }
}

std::string GetExecutablePath()
{
#if defined(_WIN32)
    wchar_t wBuf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, wBuf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return "";

    std::vector<char> utf8(static_cast<size_t>(len) * 2 + 1);
    int bytes = WideCharToMultiByte(CP_UTF8, 0, wBuf, static_cast<int>(len),
        utf8.data(), static_cast<int>(utf8.size()), nullptr, nullptr);
    if (bytes <= 0)
        return "";

    return std::string(utf8.data(), static_cast<size_t>(bytes));
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0)
        return "";
    return std::string(buf);
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__sun) || defined(__HAIKU__)
    char buf[4096];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return "";
    buf[n] = '\0';
    return std::string(buf);
#else
    return "";
#endif
}

} // namespace Lode::Platform
