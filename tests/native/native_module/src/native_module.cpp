#include "Lode/Module.hpp"
#include "Lode/State.hpp"
#include "Lode/Table.hpp"
#include "Lode/Value.hpp"
#include "Lode/Buffer.hpp"
#include "Lode/Task.hpp"
#include "Lode/Metatable.hpp"
#include "Lode/ClassBuilder.hpp"
#include "Lode/Signal.hpp"
#include <cstring>
#include <string>
#include <cmath>
#include <limits>
#include <stdexcept>

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

    Lode::Exports rawExports(vm);
    rawExports.Function("throwRaw", [](Lode::State&, const std::vector<Lode::Value>&) -> Lode::Value {
        throw std::runtime_error("raw export failure");
    });
    auto rawThrow = rawExports.GetExportTable().Get("throwRaw");
    if (rawThrow.IsOk())
        exports.Set("throwRaw", rawThrow.GetValue());

    Lode::Table throwingObject = vm.CreateTable();
    Lode::Metatable throwingMetatable = vm.CreateMetatable();
    throwingMetatable.SetIndexFunction([](Lode::State&, Lode::Value) -> Lode::Value {
        throw std::runtime_error("metatable callback failure");
    });
    throwingObject.SetMetatable(throwingMetatable);
    exports.Set("throwingObject", Lode::Value(throwingObject));

    // --- Test 1: require("@self/utils") ---
    // Loads native_module/utils/init.luau — internal to this package.
    // @self always resolves to the package directory (the folder containing lode.json),
    // so @self/utils finds: native_module/utils/init.luau  (or native_module/utils.luau).
    //
    // vm.Require() raises a Lua error if the module is not found, exactly like
    // a plain require() call from Luau. No Result unwrapping or pcall needed here.
    auto utils = vm.Require("@self/utils").AsTable();

    // --- Test 2: require("./sibling_module") ---
    // Loads tests/native/sibling_module.luau — one level above this package folder.
    // ./X from a native module resolves relative to the parent of the package folder,
    // the same way it does from an init.luau-based package.
    auto sibling = vm.Require("./sibling_module").AsTable();

    // 1. High-level functions (Optimized with Zero-Copy StackArgs)
    exports.Set("greet", vm.CreateFastFunction([utils](Lode::State& vm, Lode::StackArgs args) mutable -> Lode::Value {
        std::string_view name = (args.Size() > 0 && args[0].IsString()) ? args[0].AsStringView() : "Lode User";

        // Delegate to utils.formatGreeting loaded from @self/utils.
        auto result = utils.CallFunctionSingle("formatGreeting", Lode::Value(std::string(name)));
        
        if (result.IsOk())
            return result.GetValue();

        if (result.IsError())
            return Lode::Value("Call error: " + std::string(result.GetError().ErrorMessage()));
        return Lode::Value();
    }));

    exports.Set("square", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        double x = (args.Size() > 0 && args[0].IsNumber()) ? args[0].AsNumber() : 0.0;
        return Lode::Value(x * x);
    }));

    // --- Test 4: Zero-Copy Buffer Manipulation ---
    exports.Set("processBuffer", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 2 || !args[0].IsBuffer() || !args[1].IsString()) {
            vm.RaiseError("Expected (buffer, string) as arguments");
            return Lode::Value();
        }

        // Zero-copy read from Lua stack
        std::span<uint8_t> buf = args[0].AsSpan();
        std::string_view str = args[1].AsStringView();

        size_t copyCount = (buf.size() < str.size()) ? buf.size() : str.size();
        if (copyCount > 0) {
            std::memcpy(buf.data(), str.data(), copyCount);
        }
        
        return Lode::Value(static_cast<double>(copyCount));
    }));

    // Exercise reference transfer through a separate coroutine. This keeps the
    // integration regression on the real runtime path instead of a standalone VM.
    exports.Set("roundTripReference", vm.CreateFunction([](Lode::State& vm, const std::vector<Lode::Value>& args) -> Lode::Value {
        if (args.empty()) return Lode::Value();

        Lode::Value identity = vm.CreateFunction([](Lode::State&, const std::vector<Lode::Value>& values) -> Lode::Value {
            return values.empty() ? Lode::Value() : values[0];
        });
        Lode::Coroutine coroutine(vm, identity);
        auto result = coroutine.Resume({ args[0] });
        if (result.IsError())
        {
            vm.RaiseError(result.GetError().ErrorMessage());
            return Lode::Value();
        }
        return result.GetValue().empty() ? Lode::Value() : result.GetValue()[0];
    }));

    exports.Set("roundTripString", vm.CreateFastFunction([](Lode::State&, Lode::StackArgs args) -> Lode::Value {
        return args.Size() > 0 ? args[0].ToValue() : Lode::Value();
    }));

    exports.Set("roundTripInteger", vm.CreateFastFunction([](Lode::State&, Lode::StackArgs args) -> Lode::Value {
        return args.Size() > 0 && args[0].IsInteger() ? args[0].ToValue() : Lode::Value();
    }));

    exports.Set("makeLargeInteger", vm.CreateFastFunction([](Lode::State&, Lode::StackArgs) -> Lode::Value {
        return Lode::Value(static_cast<int64_t>(0x123456789abcdefLL));
    }));

    exports.Set("makeVector", vm.CreateFastFunction([](Lode::State&, Lode::StackArgs) -> Lode::Value {
        Lode::Vector vector;
        vector.components[0] = 1.25f;
        vector.components[1] = -2.5f;
        vector.components[2] = 3.75f;
        return Lode::Value(vector);
    }));

    exports.Set("roundTripVector", vm.CreateFastFunction([](Lode::State&, Lode::StackArgs args) -> Lode::Value {
        return args.Size() > 0 && args[0].IsVector() ? args[0].ToValue() : Lode::Value();
    }));

    Lode::Table modernObject = vm.CreateTable();
    Lode::Metatable modernMetatable = vm.CreateMetatable();
    modernMetatable.SetIntegerDivide([](Lode::State&, Lode::Value, Lode::Value) {
        return Lode::Value(21.0);
    });
    modernMetatable.SetLength([](Lode::State&, Lode::Value) {
        return Lode::Value(42.0);
    });
    Lode::Value iterator = vm.CreateFunction([](Lode::State&, const std::vector<Lode::Value>& args) {
        int64_t current = args.size() > 1 ? args[1].AsInteger() : 0;
        return current < 3 ? Lode::Value(current + 1) : Lode::Value();
    });
    modernMetatable.SetIter([iterator](Lode::State&, Lode::Value object) {
        return std::vector<Lode::Value>{ iterator, object, Lode::Value() };
    });
    modernObject.SetMetatable(modernMetatable);
    exports.Set("modernObject", Lode::Value(modernObject));

    exports.Set("bufferEndianProbe", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs) -> Lode::Value {
        Lode::Buffer written = vm.CreateBuffer(8).AsBufferObj();
        written.WriteUInt32(0, 0x01020304u);
        written.WriteFloat32(4, 1.0f);

        Lode::Buffer read = vm.CreateBuffer(8).AsBufferObj();
        read.WriteString(0, std::string("\x04\x03\x02\x01\x00\x00\x80\x3f", 8));

        Lode::Table result = vm.CreateTable();
        result.Set("written", Lode::Value(std::string(static_cast<const char*>(written.Data()), written.Size())));
        result.Set("readInt", Lode::Value(static_cast<double>(read.ReadUInt32(0))));
        result.Set("readFloat", Lode::Value(static_cast<double>(read.ReadFloat32(4))));
        return Lode::Value(result);
    }));

    exports.Set("bufferBoundsProbe", vm.CreateFastFunction([](Lode::State&, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() == 0 || !args[0].IsBuffer()) return Lode::Value();
        Lode::Buffer buffer = args[0].ToValue().AsBufferObj();
        return Lode::Value(static_cast<double>(buffer.ReadUInt32(std::numeric_limits<size_t>::max())));
    }));

    exports.Set("bufferOverlapProbe", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs) -> Lode::Value {
        Lode::Buffer buffer = vm.CreateBuffer(4).AsBufferObj();
        buffer.WriteString(0, "ABCD");
        buffer.CopyFrom(1, buffer, 0, 3);
        return Lode::Value(std::string(buffer.ReadString(0, 4)));
    }));

    // Call a table with a __call metamethod through the Value call API.
    exports.Set("callCallable", vm.CreateFunction([](Lode::State& vm, const std::vector<Lode::Value>& args) -> Lode::Value {
        if (args.empty())
        {
            vm.RaiseError("callCallable expects a callable value");
            return Lode::Value();
        }
        Lode::Value callable = args[0];
        std::vector<Lode::Value> passArgs(args.begin() + 1, args.end());
        auto result = callable.CallSingle(vm, passArgs);
        if (result.IsError())
        {
            vm.RaiseError(result.GetError().ErrorMessage());
            return Lode::Value();
        }
        return result.GetValue();
    }));

    // Prove that non-callable values still fail through the Value call API.
    exports.Set("callNonCallable", vm.CreateFunction([](Lode::State& vm, const std::vector<Lode::Value>& args) -> Lode::Value {
        Lode::Value nonCallable = args.empty() ? Lode::Value() : args[0];
        auto result = nonCallable.CallSingle(vm);
        Lode::Table out = vm.CreateTable();
        out.Set("ok", Lode::Value(result.IsOk()));
        out.Set("error", Lode::Value(result.IsError() ? std::string(result.GetError().ErrorMessage()) : std::string()));
        return Lode::Value(out);
    }));

    exports.Set("throwCpp", vm.CreateFastFunction([](Lode::State&, Lode::StackArgs) -> Lode::Value {
        throw std::runtime_error("native callback failure");
    }));

    exports.Set("getSystemInfo", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs) -> Lode::Value {
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

    // --- Test 3: Native Signal Integration ---
    // Creates a Lode::Signal from C++ and exposes only its read-only public
    // proxy to Luau. Luau can Connect/Once/Wait but never Fire.
    auto onTickSignal = Lode::Signal::Create(vm);

    // Public proxy: a frozen table with Connect/Once/Wait (equivalent to
    // modules/signal's Public()), so type(onTick) == "table".
    exports.Set("onTick", onTickSignal->CreatePublic());

    // Fires the signal immediately with the given message. Exposed to Luau so
    // the test can exercise Connect/Once/Disconnect/Wait deterministically.
    exports.Set("fireOnTick", vm.CreateFunction([onTickSignal](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        std::string message = (args.size() > 0 && args[0].IsString()) ? args[0].AsString() : "Ticked from native C++!";
        onTickSignal->Fire({ Lode::Value(message) });
        return Lode::Value();
    }));

    // Schedule a native delay task to fire the signal once after 0.5 seconds.
    Lode::Value delayedFire = vm.CreateFunction([onTickSignal](Lode::State&, const std::vector<Lode::Value>&) -> Lode::Value {
        onTickSignal->Fire({ Lode::Value(std::string("Ticked from native C++ after 0.5s!")) });
        return Lode::Value();
    });
    Lode::Task::Delay(vm, 0.5, delayedFire);

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
