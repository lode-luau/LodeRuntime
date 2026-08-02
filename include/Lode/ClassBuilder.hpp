#pragma once

#include "Lode/Export.hpp"
#include "Lode/State.hpp"
#include "Lode/Table.hpp"
#include "Lode/Metatable.hpp"
#include "Lode/ObjectWrap.hpp"
#include "lua.h"
#include <string>
#include <functional>
#include <memory>
#include <unordered_map>

namespace Lode
{

template <typename T>
class ClassBuilder
{
public:
    explicit ClassBuilder(State& vm, const std::string& className)
        : vm_(vm), className_(className), metatable_(vm.CreateTable()), methodsTable_(vm.CreateTable())
    {
        lua_State* L = vm.GetLuaState();
        metatable_.Set("__index", Value(methodsTable_));

        // Automatic __gc calling ~T()
        using Holder = std::shared_ptr<T>;
        auto gcClosure = [](lua_State* L) -> int {
            if (lua_isuserdata(L, 1)) {
                auto* holder = static_cast<Holder*>(lua_touserdata(L, 1));
                if (holder) {
                    holder->~Holder();
                }
            }
            return 0;
        };

        metatable_.PushToLuaState(L);
        lua_pushcclosure(L, gcClosure, "__gc", 0);
        lua_setfield(L, -2, "__gc");
        lua_pop(L, 1);
    }

    template <typename... Args>
    ClassBuilder& Constructor()
    {
        Table meta = metatable_;

        methodsTable_.Set("new", vm_.CreateFunction([meta](State& vm, const std::vector<Value>& args) -> Value {
            auto instance = std::make_shared<T>();
            lua_State* L = vm.GetLuaState();
            using Holder = std::shared_ptr<T>;
            void* userMemory = lua_newuserdata(L, sizeof(Holder));
            new (userMemory) Holder(instance);

            meta.PushToLuaState(L);
            lua_setmetatable(L, -2);
            return Value::FromLuaState(L, -1);
        }));

        return *this;
    }

    ClassBuilder& CustomConstructor(std::function<std::shared_ptr<T>(State& vm, const std::vector<Value>& args)> factory)
    {
        Table meta = metatable_;
        methodsTable_.Set("new", vm_.CreateFunction([meta, factory](State& vm, const std::vector<Value>& args) -> Value {
            auto instance = factory(vm, args);
            if (!instance) instance = std::make_shared<T>();
            lua_State* L = vm.GetLuaState();
            using Holder = std::shared_ptr<T>;
            void* userMemory = lua_newuserdata(L, sizeof(Holder));
            new (userMemory) Holder(instance);

            meta.PushToLuaState(L);
            lua_setmetatable(L, -2);
            return Value::FromLuaState(L, -1);
        }));
        return *this;
    }

    ClassBuilder& Method(const std::string& name, const std::function<Value(T& self, State& vm, const std::vector<Value>& args)>& fn)
    {
        methodsTable_.Set(name, vm_.CreateFunction([fn](State& vm, const std::vector<Value>& args) -> Value {
            auto instance = ObjectWrap<T>::Unwrap(vm, 1);
            if (!instance) {
                return Value();
            }
            std::vector<Value> methodArgs;
            if (args.size() > 1) {
                methodArgs.assign(args.begin() + 1, args.end());
            }
            return fn(*instance, vm, methodArgs);
        }));
        return *this;
    }

    ClassBuilder& Property(const std::string& name, std::function<Value(const T& self)> getter, std::function<void(T& self, const Value& val)> setter = nullptr)
    {
        getters_[name] = getter;
        if (setter) {
            setters_[name] = setter;
        }
        return *this;
    }

    ClassBuilder& ToString(std::function<std::string(const T& self)> fn)
    {
        metatable_.Set("__tostring", vm_.CreateFunction([fn](State& vm, const std::vector<Value>& args) -> Value {
            auto instance = ObjectWrap<T>::Unwrap(vm, 1);
            if (!instance) return Value("nullptr");
            return Value(fn(*instance));
        }));
        return *this;
    }

    [[nodiscard]] Table Build() const
    {
        return methodsTable_;
    }

private:
    State& vm_;
    std::string className_;
    Table metatable_;
    Table methodsTable_;
    std::unordered_map<std::string, std::function<Value(const T&)>> getters_;
    std::unordered_map<std::string, std::function<void(T&, const Value&)>> setters_;
};

} // namespace Lode
