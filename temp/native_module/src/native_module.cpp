#include "Lode/Module.hpp"
#include "Lode/State.hpp"
#include "Lode/Table.hpp"
#include "Lode/Value.hpp"
#include "Lode/Task.hpp"
#include "Lode/Metatable.hpp"
#include "Lode/ClassBuilder.hpp"
#include <string>
#include <cmath>

struct Vector3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    Vector3() = default;
    Vector3(double x, double y, double z) : x(x), y(y), z(z) {}

    double Length() const
    {
        return std::sqrt(x * x + y * y + z * z);
    }
};

LODE_MODULE(vm)
{
    Lode::Table exports = vm.CreateTable();

    // --- Test 1: require("@self/utils") ---
    // Loads native_module/utils/init.luau — internal to this package.
    // @self always resolves to the package directory (the folder containing lode.json),
    // so @self/utils finds: native_module/utils/init.luau  (or native_module/utils.luau).
    //
    // vm.Require() raises a Lua error if the module is not found, exactly like
    // a plain require() call from Luau. No Result unwrapping or pcall needed here.
    auto utils = vm.Require("@self/utils").AsTable();

    // --- Test 2: require("./sibling_module") ---
    // Loads temp/sibling_module.luau — one level above this package folder.
    // ./X from a native module resolves relative to the parent of the package folder,
    // the same way it does from an init.luau-based package.
    auto sibling = vm.Require("./sibling_module").AsTable();

    // 1. High-level functions
    exports.Set("greet", vm.CreateFunction([utils](Lode::State& vm, const std::vector<Lode::Value>& args) mutable -> Lode::Value {
        std::string name = (args.size() > 0 && args[0].IsString()) ? args[0].AsString() : "Lode User";

        // Delegate to utils.formatGreeting loaded from @self/utils.
        // Look how clean it is now! We can just call CallFunction without even passing the vm.
        auto result = utils.CallFunction("formatGreeting", { Lode::Value(name) });
        
        if (result.IsOk() && !result.GetValue().empty())
            return result.GetValue()[0];

        // Debug: surface the exact error so we can diagnose it.
        if (result.IsError())
            return Lode::Value("Call error: " + std::string(result.GetError().GetMessage()));

        return Lode::Value("Hello, " + name + "! (utils.formatGreeting returned empty)");
    }));

    exports.Set("square", vm.CreateFunction([](Lode::State& vm, const std::vector<Lode::Value>& args) -> Lode::Value {
        double x = (args.size() > 0 && args[0].IsNumber()) ? args[0].AsNumber() : 0.0;
        return Lode::Value(x * x);
    }));

    exports.Set("getSystemInfo", vm.CreateFunction([](Lode::State& vm, const std::vector<Lode::Value>&) -> Lode::Value {
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
        return Lode::Value(info);
    }));

    // Expose utils and sibling so the Luau caller can verify both were loaded.
    exports.Set("utils", Lode::Value(utils));
    exports.Set("sibling", Lode::Value(sibling));

    // 2. Class Binding: Vector3
    Lode::ClassBuilder<Vector3> vec3Builder(vm, "Vector3");
    vec3Builder.CustomConstructor([](Lode::State&, const std::vector<Lode::Value>& args) -> std::shared_ptr<Vector3> {
        double x = (args.size() > 0 && args[0].IsNumber()) ? args[0].AsNumber() : 0.0;
        double y = (args.size() > 1 && args[1].IsNumber()) ? args[1].AsNumber() : 0.0;
        double z = (args.size() > 2 && args[2].IsNumber()) ? args[2].AsNumber() : 0.0;
        return std::make_shared<Vector3>(x, y, z);
    });

    // Automatic member property bindings (&Vector3::x, &Vector3::y, &Vector3::z)
    vec3Builder.Property("x", &Vector3::x);
    vec3Builder.Property("y", &Vector3::y);
    vec3Builder.Property("z", &Vector3::z);

    // Automatic member method binding (&Vector3::Length)
    vec3Builder.Method("length", &Vector3::Length);

    vec3Builder.ToString([](const Vector3& self) -> std::string {
        return "Vector3(" + std::to_string(self.x) + ", " + std::to_string(self.y) + ", " + std::to_string(self.z) + ")";
    });

    exports.Set("Vector3", vec3Builder.Build());

    // 3. Secondary Metadata Table export (Multiple Return Values)
    Lode::Table metaInfo = vm.CreateTable();
    metaInfo.Set("moduleName", Lode::Value("NativeModule"));
    metaInfo.Set("version", Lode::Value("2.0.0"));

    // Returns multiple values to require: local NativeMod, MetaInfo = require(...)
    return { exports, metaInfo };
}
