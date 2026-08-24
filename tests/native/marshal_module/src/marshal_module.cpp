// Native-call marshaling benchmark fixture.
//
// Exposes the same add(a, b) through the three native-closure paths the
// runtime provides, so tests/native/marshal_bench.luau can compare their
// per-call cost through the real runtime path:
//
//   addLegacy  State::CreateFunction      vector<Value> boxing in, Value out
//   addFast    State::CreateFastFunction  StackArgs reads, single boxed Value out
//   addFastN   State::CreateFastFunctionN StackArgs reads + direct stack push,
//                                          no Value construction, multi-return
#include "Lode/Module.hpp"
#include "Lode/StackValue.hpp"
#include "lua.h"

LODE_MODULE(vm)
{
    Lode::Table exports = vm.CreateTable();

    exports.Set("addLegacy", vm.CreateFunction([](Lode::State&, const std::vector<Lode::Value>& args) -> Lode::Value {
        double s = 0.0;
        for (size_t i = 0; i < args.size(); ++i)
            s += args[i].AsNumber();
        return Lode::Value(s);
    }));

    exports.Set("addFast", vm.CreateFastFunction([](Lode::State&, Lode::StackArgs args) -> Lode::Value {
        double s = 0.0;
        for (size_t i = 0; i < args.Size(); ++i)
            s += args[i].AsNumber();
        return Lode::Value(s);
    }));

    exports.Set("addFastN", vm.CreateFastFunctionN([](Lode::State&, Lode::StackArgs args) -> int {
        lua_State* L = args.RawState();
        double s = 0.0;
        const int n = static_cast<int>(args.Size());
        for (int i = 0; i < n; ++i)
            s += args[static_cast<size_t>(i)].AsNumber();
        lua_pushnumber(L, s);
        lua_pushinteger(L, n);
        return 2;
    }));

    return Lode::ModuleReturn(exports);
}