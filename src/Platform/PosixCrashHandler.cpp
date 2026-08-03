// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#if !defined(_WIN32)

#include "CrashHandler.hpp"
#include "Lode/Logger.hpp"
#include <signal.h>
#include <execinfo.h>
#include <dlfcn.h>
#include <cxxabi.h>
#include <unistd.h>
#include <sstream>
#include <iomanip>
#include <cstdlib>

namespace Lode::Platform
{

static const char* GetSignalName(int sig)
{
    switch (sig)
    {
    case SIGSEGV: return "SIGSEGV (Segmentation fault - Invalid memory reference)";
    case SIGABRT: return "SIGABRT (Abort signal)";
    case SIGFPE:  return "SIGFPE (Floating-point exception)";
    case SIGILL:  return "SIGILL (Illegal instruction)";
    case SIGBUS:  return "SIGBUS (Bus error - Bad memory access)";
    case SIGSYS:  return "SIGSYS (Bad system call)";
    default:      return "UNKNOWN_SIGNAL";
    }
}

static void PosixSignalHandler(int sig, siginfo_t* info, void* ucontext)
{
    void* addr = (info && info->si_addr) ? info->si_addr : nullptr;

    std::vector<std::string> stackTrace;

    void* callstack[128];
    int frames = backtrace(callstack, 128);
    char** strs = backtrace_symbols(callstack, frames);

    for (int i = 0; i < frames; ++i)
    {
        std::stringstream frameSs;
        Dl_info dlinfo;
        frameSs << "0x" << std::hex << reinterpret_cast<uintptr_t>(callstack[i]) << std::dec;

        if (dladdr(callstack[i], &dlinfo) && dlinfo.dli_sname)
        {
            int status = -1;
            char* demangled = abi::__cxa_demangle(dlinfo.dli_sname, nullptr, nullptr, &status);
            const char* name = (status == 0 && demangled) ? demangled : dlinfo.dli_sname;

            frameSs << " -> " << name;

            if (dlinfo.dli_fname)
            {
                frameSs << " (" << dlinfo.dli_fname << ")";
            }

            if (demangled)
            {
                free(demangled);
            }
        }
        else if (strs && strs[i])
        {
            frameSs << " -> " << strs[i];
        }
        stackTrace.push_back(frameSs.str());
    }

    if (strs)
    {
        free(strs);
    }

    Logger::EmitCrashReport("POSIX Signal Exception", GetSignalName(sig), addr, stackTrace);

    // Reset signal handler to default and re-raise signal for core dumps
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(sig, &sa, nullptr);
    raise(sig);
}

void CrashHandler::Initialize()
{
    struct sigaction sa;
    sa.sa_sigaction = PosixSignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;

    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGFPE,  &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
    sigaction(SIGSYS,  &sa, nullptr);
}

} // namespace Lode::Platform

#endif
