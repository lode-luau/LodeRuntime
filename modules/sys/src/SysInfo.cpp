// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Sys/SysInfo.hpp"
#include <uv.h>
#include <string>

namespace lodesys
{

void BindSysInfo(Lode::State& vm, Lode::Table& exports)
{
    exports.Set("GetPlatform", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        uv_utsname_t buffer;
        int r = uv_os_uname(&buffer);
        if (r < 0) {
            vm.RaiseError(std::string("sys.GetPlatform error: ") + uv_strerror(r));
            return Lode::Value();
        }
        return Lode::Value(std::string(buffer.sysname));
    }));

    exports.Set("GetArchitecture", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        uv_utsname_t buffer;
        int r = uv_os_uname(&buffer);
        if (r < 0) {
            vm.RaiseError(std::string("sys.GetArchitecture error: ") + uv_strerror(r));
            return Lode::Value();
        }
        return Lode::Value(std::string(buffer.machine));
    }));

    exports.Set("GetRelease", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        uv_utsname_t buffer;
        int r = uv_os_uname(&buffer);
        if (r < 0) {
            vm.RaiseError(std::string("sys.GetRelease error: ") + uv_strerror(r));
            return Lode::Value();
        }
        return Lode::Value(std::string(buffer.release));
    }));

    exports.Set("GetVersion", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        uv_utsname_t buffer;
        int r = uv_os_uname(&buffer);
        if (r < 0) {
            vm.RaiseError(std::string("sys.GetVersion error: ") + uv_strerror(r));
            return Lode::Value();
        }
        return Lode::Value(std::string(buffer.version));
    }));

    exports.Set("GetHostname", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        char buf[256];
        size_t size = sizeof(buf);
        int r = uv_os_gethostname(buf, &size);
        if (r < 0) {
            vm.RaiseError(std::string("sys.GetHostname error: ") + uv_strerror(r));
            return Lode::Value();
        }
        return Lode::Value(std::string(buf, size));
    }));

    exports.Set("GetTotalMemory", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        uint64_t mem = uv_get_total_memory();
        return Lode::Value(static_cast<double>(mem));
    }));

    exports.Set("GetFreeMemory", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        uint64_t mem = uv_get_free_memory();
        return Lode::Value(static_cast<double>(mem));
    }));

    exports.Set("GetUptime", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        double uptime;
        int r = uv_uptime(&uptime);
        if (r < 0) {
            vm.RaiseError(std::string("sys.GetUptime error: ") + uv_strerror(r));
            return Lode::Value();
        }
        return Lode::Value(uptime);
    }));
}

} // namespace lodesys
