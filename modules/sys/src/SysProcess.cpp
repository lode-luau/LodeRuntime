// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Sys/SysProcess.hpp"
#include <uv.h>
#include <string>
#include <cstdlib>

namespace lodesys
{

std::string GuessHandleType(int fd)
{
    uv_handle_type t = uv_guess_handle(static_cast<uv_file>(fd));
    switch (t) {
        case UV_TTY: return "tty";
        case UV_NAMED_PIPE: return "pipe";
        case UV_FILE: return "file";
        case UV_TCP: return "tcp";
        case UV_UDP: return "udp";
        default: return "unknown";
    }
}

void BindSysProcess(Lode::State& vm, Lode::Table& exports)
{
    exports.Set("GetEnv", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 1 || !args[0].IsString()) {
            vm.RaiseError("sys.GetEnv: expected string argument");
            return Lode::Value();
        }
        std::string key = args[0].AsString();
        char buf[4096];
        size_t size = sizeof(buf);
        int r = uv_os_getenv(key.c_str(), buf, &size);
        if (r == UV_ENOENT) {
            return Lode::Value(); // return nil
        } else if (r < 0) {
            vm.RaiseError(std::string("sys.GetEnv error: ") + uv_strerror(r));
            return Lode::Value();
        }
        return Lode::Value(std::string(buf, size));
    }));

    exports.Set("SetEnv", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 2 || !args[0].IsString() || !args[1].IsString()) {
            vm.RaiseError("sys.SetEnv: expected (string, string) arguments");
            return Lode::Value();
        }
        std::string key = args[0].AsString();
        std::string val = args[1].AsString();
        int r = uv_os_setenv(key.c_str(), val.c_str());
        if (r < 0) {
            vm.RaiseError(std::string("sys.SetEnv error: ") + uv_strerror(r));
        }
        return Lode::Value();
    }));

    exports.Set("GetCwd", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        char buf[4096];
        size_t size = sizeof(buf);
        int r = uv_cwd(buf, &size);
        if (r < 0) {
            vm.RaiseError(std::string("sys.GetCwd error: ") + uv_strerror(r));
            return Lode::Value();
        }
        return Lode::Value(std::string(buf, size));
    }));

    exports.Set("Chdir", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 1 || !args[0].IsString()) {
            vm.RaiseError("sys.Chdir: expected string argument");
            return Lode::Value();
        }
        std::string dir = args[0].AsString();
        int r = uv_chdir(dir.c_str());
        if (r < 0) {
            vm.RaiseError(std::string("sys.Chdir error: ") + uv_strerror(r));
        }
        return Lode::Value();
    }));

    exports.Set("exit", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        int code = 0;
        if (args.Size() > 0 && args[0].IsNumber()) {
            code = static_cast<int>(args[0].AsNumber());
        }
        std::exit(code);
        return Lode::Value();
    }));

    exports.Set("GuessHandleType", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 1 || !args[0].IsNumber()) {
            vm.RaiseError("sys.GuessHandleType: expected number argument");
            return Lode::Value();
        }
        int fd = static_cast<int>(args[0].AsNumber());
        return Lode::Value(GuessHandleType(fd));
    }));
}

} // namespace lodesys
