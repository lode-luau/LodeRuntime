// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#if defined(_WIN32)

#include "CrashHandler.hpp"
#include "Lode/Logger.hpp"
#include <windows.h>
#include <dbghelp.h>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "dbghelp.lib")

namespace Lode::Platform
{

static const char* GetExceptionCodeString(DWORD code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION (0xC0000005)";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED (0xC000008C)";
    case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT (0x80000003)";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT (0x80000002)";
    case EXCEPTION_FLT_DENORMAL_OPERAND: return "EXCEPTION_FLT_DENORMAL_OPERAND (0xC000008D)";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO (0xC000008E)";
    case EXCEPTION_FLT_INEXACT_RESULT: return "EXCEPTION_FLT_INEXACT_RESULT (0xC000008F)";
    case EXCEPTION_FLT_INVALID_OPERATION: return "EXCEPTION_FLT_INVALID_OPERATION (0xC0000090)";
    case EXCEPTION_FLT_OVERFLOW: return "EXCEPTION_FLT_OVERFLOW (0xC0000091)";
    case EXCEPTION_FLT_STACK_CHECK: return "EXCEPTION_FLT_STACK_CHECK (0xC0000092)";
    case EXCEPTION_FLT_UNDERFLOW: return "EXCEPTION_FLT_UNDERFLOW (0xC0000093)";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION (0xC000001D)";
    case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR (0xC0000006)";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO (0xC0000094)";
    case EXCEPTION_INT_OVERFLOW: return "EXCEPTION_INT_OVERFLOW (0xC0000095)";
    case EXCEPTION_INVALID_DISPOSITION: return "EXCEPTION_INVALID_DISPOSITION (0xC0000026)";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION (0xC0000025)";
    case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION (0xC0000096)";
    case EXCEPTION_SINGLE_STEP: return "EXCEPTION_SINGLE_STEP (0x80000004)";
    case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW (0xC00000FD)";
    default: return "UNKNOWN_CRASH_EXCEPTION";
    }
}

static LONG WINAPI WindowsUnhandledExceptionFilter(PEXCEPTION_POINTERS exceptionInfo)
{
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    SymInitialize(process, NULL, TRUE);

    DWORD code = exceptionInfo->ExceptionRecord->ExceptionCode;
    PVOID addr = exceptionInfo->ExceptionRecord->ExceptionAddress;

    std::string details = "";
    if (code == EXCEPTION_ACCESS_VIOLATION && exceptionInfo->ExceptionRecord->NumberParameters >= 2)
    {
        ULONG_PTR accessType = exceptionInfo->ExceptionRecord->ExceptionInformation[0];
        ULONG_PTR targetAddr = exceptionInfo->ExceptionRecord->ExceptionInformation[1];
        std::stringstream ss;
        ss << "Attempted to " << (accessType == 0 ? "READ" : (accessType == 1 ? "WRITE" : "EXECUTE"))
           << " memory address 0x" << std::hex << targetAddr;
        details = ss.str();
    }

    std::vector<std::string> stackTrace;

    STACKFRAME64 stackFrame = {};
    #if defined(_M_X64) || defined(__x86_64__)
    DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
    stackFrame.AddrPC.Offset = exceptionInfo->ContextRecord->Rip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = exceptionInfo->ContextRecord->Rbp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = exceptionInfo->ContextRecord->Rsp;
    stackFrame.AddrStack.Mode = AddrModeFlat;
    #else
    DWORD machineType = IMAGE_FILE_MACHINE_I386;
    stackFrame.AddrPC.Offset = exceptionInfo->ContextRecord->Eip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = exceptionInfo->ContextRecord->Ebp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = exceptionInfo->ContextRecord->Esp;
    stackFrame.AddrStack.Mode = AddrModeFlat;
    #endif

    CONTEXT contextRecord = *exceptionInfo->ContextRecord;

    while (StackWalk64(
        machineType,
        process,
        thread,
        &stackFrame,
        &contextRecord,
        NULL,
        SymFunctionTableAccess64,
        SymGetModuleBase64,
        NULL))
    {
        if (stackFrame.AddrPC.Offset == 0)
            break;

        DWORD64 displacement = 0;
        char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
        PSYMBOL_INFO symbol = (PSYMBOL_INFO)buffer;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;

        std::stringstream frameSs;
        frameSs << "0x" << std::hex << stackFrame.AddrPC.Offset << std::dec;

        if (SymFromAddr(process, stackFrame.AddrPC.Offset, &displacement, symbol))
        {
            frameSs << " -> " << symbol->Name;

            IMAGEHLP_LINE64 line;
            DWORD lineDisplacement;
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            if (SymGetLineFromAddr64(process, stackFrame.AddrPC.Offset, &lineDisplacement, &line))
            {
                frameSs << " (" << line.FileName << ":" << line.LineNumber << ")";
            }
        }
        stackTrace.push_back(frameSs.str());
    }

    Logger::EmitCrashReport("Native Execution Exception", GetExceptionCodeString(code), addr, stackTrace, details);

    SymCleanup(process);

    return EXCEPTION_EXECUTE_HANDLER;
}

void CrashHandler::Initialize()
{
    SetUnhandledExceptionFilter(WindowsUnhandledExceptionFilter);
}

} // namespace Lode::Platform

#endif
