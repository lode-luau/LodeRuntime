#if !defined(_WIN32)

#include "CrashHandler.hpp"
#include <signal.h>
#include <execinfo.h>
#include <dlfcn.h>
#include <cxxabi.h>
#include <unistd.h>
#include <iostream>
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
    std::cerr << "\n=======================================================\n";
    std::cerr << "         [LODERUNTIME CRASH DETECTED]                  \n";
    std::cerr << "=======================================================\n";
    std::cerr << "Signal Received   : " << GetSignalName(sig) << "\n";

    if (info && info->si_addr)
    {
        std::cerr << "Faulting Address  : 0x" << std::hex << reinterpret_cast<uintptr_t>(info->si_addr) << std::dec << "\n";
    }

    std::cerr << "\n--- Stack Trace (backtrace / dladdr) ---\n";

    void* callstack[128];
    int frames = backtrace(callstack, 128);
    char** strs = backtrace_symbols(callstack, frames);

    for (int i = 0; i < frames; ++i)
    {
        Dl_info dlinfo;
        std::cerr << "  [" << i << "] 0x" << std::hex << reinterpret_cast<uintptr_t>(callstack[i]) << std::dec;

        if (dladdr(callstack[i], &dlinfo) && dlinfo.dli_sname)
        {
            int status = -1;
            char* demangled = abi::__cxa_demangle(dlinfo.dli_sname, nullptr, nullptr, &status);
            const char* name = (status == 0 && demangled) ? demangled : dlinfo.dli_sname;

            std::cerr << " -> " << name;

            if (dlinfo.dli_fname)
            {
                std::cerr << " (" << dlinfo.dli_fname << ")";
            }

            if (demangled)
            {
                free(demangled);
            }
        }
        else if (strs && strs[i])
        {
            std::cerr << " -> " << strs[i];
        }
        std::cerr << "\n";
    }

    if (strs)
    {
        free(strs);
    }

    std::cerr << "=======================================================\n\n";

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
