#include "Platform.hpp"

namespace Lode::Platform
{

OS GetOS()
{
#if defined(_WIN32)
    return OS::Windows;
#elif defined(__APPLE__)
    return OS::MacOS;
#elif defined(__linux__)
    return OS::Linux;
#else
    return OS::Unknown;
#endif
}

Architecture GetArchitecture()
{
#if defined(__x86_64__) || defined(_M_X64)
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
    case OS::Linux:   return "linux";
    default:          return "unknown";
    }
}

std::string_view GetArchitectureName()
{
    switch (GetArchitecture())
    {
    case Architecture::x64:   return "x64";
    case Architecture::arm64: return "arm64";
    case Architecture::x86:   return "x86";
    default:                  return "unknown";
    }
}

} // namespace Lode::Platform
