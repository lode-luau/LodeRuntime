// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Sys/SysInfo.hpp"
#include <uv.h>
#include <string>

namespace lodesys
{

static Lode::Value GetUvPath(Lode::State& vm, const char* name,
                             int (*getter)(char*, size_t*))
{
    std::string path(256, '\0');
    size_t size = path.size();
    int r = getter(path.data(), &size);
    if (r == UV_ENOBUFS)
    {
        path.resize(size);
        r = getter(path.data(), &size);
    }
    if (r < 0)
    {
        vm.RaiseError(std::string("sys.") + name + " error: " + uv_strerror(r));
        return Lode::Value();
    }
    return Lode::Value(std::string(path.data(), size));
}

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

    exports.Set("GetAvailableParallelism", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        return Lode::Value(static_cast<double>(uv_available_parallelism()));
    }));

    exports.Set("GetCpuCount", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        uv_cpu_info_t* infos = nullptr;
        int count = 0;
        int r = uv_cpu_info(&infos, &count);
        if (r < 0)
        {
            vm.RaiseError(std::string("sys.GetCpuCount error: ") + uv_strerror(r));
            return Lode::Value();
        }
        uv_free_cpu_info(infos, count);
        return Lode::Value(static_cast<double>(count));
    }));

    exports.Set("GetCpuInfo", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        uv_cpu_info_t* infos = nullptr;
        int count = 0;
        int r = uv_cpu_info(&infos, &count);
        if (r < 0)
        {
            vm.RaiseError(std::string("sys.GetCpuInfo error: ") + uv_strerror(r));
            return Lode::Value();
        }
        Lode::Table result = vm.CreateTable();
        for (int i = 0; i < count; ++i)
        {
            Lode::Table cpu = vm.CreateTable();
            cpu.Set("model", Lode::Value(infos[i].model));
            cpu.Set("speed", Lode::Value(static_cast<double>(infos[i].speed)));
            result.Set(i + 1, Lode::Value(cpu));
        }
        uv_free_cpu_info(infos, count);
        return Lode::Value(result);
    }));

    exports.Set("GetTmpDir", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        return GetUvPath(vm, "GetTmpDir", uv_os_tmpdir);
    }));

    exports.Set("GetHomeDir", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        return GetUvPath(vm, "GetHomeDir", uv_os_homedir);
    }));

    exports.Set("GetLoadAverage", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        double average[3];
        uv_loadavg(average);
        Lode::Table result = vm.CreateTable();
        result.Set(1, Lode::Value(average[0]));
        result.Set(2, Lode::Value(average[1]));
        result.Set(3, Lode::Value(average[2]));
        return Lode::Value(result);
    }));
}

} // namespace lodesys
