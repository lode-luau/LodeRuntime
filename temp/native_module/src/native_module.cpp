#include "Lode/Module.hpp"
#include "Lode/State.hpp"
#include "Lode/Table.hpp"
#include "Lode/Value.hpp"
#include <string>

LODE_MODULE(vm, exports)
{
    // High-level C++ lambda (takes std::string, returns std::string)
    exports.Function("greet", [](const std::string& name) -> std::string {
        std::string targetName = name.empty() ? "Lode User" : name;
        return "Hello from Native C++ DLL! Welcome, " + targetName + "!";
    });

    // High-level C++ lambda (takes double, returns double)
    exports.Function("square", [](double x) -> double {
        return x * x;
    });

    // High-level C++ lambda receiving Lode::State& vm directly in parameters (no dangling reference capture!)
    exports.Function("getSystemInfo", [](Lode::State& vm, const std::vector<Lode::Value>&) -> Lode::Value {
        Lode::Table info = vm.CreateTable();

#if defined(_WIN32)
        info.Set("platform", Lode::Value("Windows"));
#elif defined(__APPLE__)
        info.Set("platform", Lode::Value("macOS"));
#else
        info.Set("platform", Lode::Value("Linux"));
#endif

#if defined(_M_X64) || defined(__x86_64__)
        info.Set("arch", Lode::Value("x64"));
#else
        info.Set("arch", Lode::Value("x86"));
#endif

        return info;
    });
}
