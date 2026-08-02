#pragma once

#include "Lode/Export.hpp"
#include "Lode/State.hpp"
#include "Lode/Table.hpp"
#include "Lode/Metatable.hpp"
#include "Lode/ObjectWrap.hpp"
#include <string>
#include <string_view>
#include <functional>
#include <memory>
#include <unordered_map>
#include <type_traits>
#include <tuple>
#include <utility>
#include <stdexcept>

namespace Lode
{

namespace Detail
{

template <typename T>
struct is_shared_ptr : std::false_type {};

template <typename T>
struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};

template <typename Arg>
struct ValueConverter
{
    using CleanArg = std::decay_t<Arg>;

    static CleanArg FromValue(State* vm, const Value& val, int stackIndex)
    {
        if constexpr (std::is_same_v<CleanArg, Value>)
        {
            return val;
        }
        else if constexpr (std::is_same_v<CleanArg, std::string> || std::is_same_v<CleanArg, std::string_view>)
        {
            return val.AsString();
        }
        else if constexpr (std::is_floating_point_v<CleanArg>)
        {
            return static_cast<CleanArg>(val.AsNumber());
        }
        else if constexpr (std::is_integral_v<CleanArg> && !std::is_same_v<CleanArg, bool>)
        {
            return static_cast<CleanArg>(val.AsInteger());
        }
        else if constexpr (std::is_same_v<CleanArg, bool>)
        {
            return val.AsBoolean();
        }
        else if constexpr (is_shared_ptr<CleanArg>::value)
        {
            using ElementType = typename CleanArg::element_type;
            if (vm)
            {
                auto ptr = ObjectWrap<ElementType>::Unwrap(*vm, stackIndex);
                if (!ptr && vm)
                {
                    vm->RaiseError("Type mismatch: expected userdata instance");
                }
                return ptr;
            }
            return ObjectWrap<ElementType>::Unwrap(val);
        }
        else
        {
            return CleanArg{};
        }
    }
};

template <typename Ret>
struct ValueReturner
{
    template <typename U>
    static Value ToValue(State* vm, U&& result)
    {
        using CleanRet = std::decay_t<U>;
        if constexpr (std::is_same_v<CleanRet, Value>)
        {
            return std::forward<U>(result);
        }
        else if constexpr (std::is_same_v<CleanRet, Table>)
        {
            return Value(result);
        }
        else if constexpr (std::is_same_v<CleanRet, std::string>)
        {
            return Value(result);
        }
        else if constexpr (std::is_floating_point_v<CleanRet> || std::is_integral_v<CleanRet>)
        {
            return Value(static_cast<double>(result));
        }
        else if constexpr (std::is_same_v<CleanRet, bool>)
        {
            return Value(result);
        }
        else
        {
            return Value();
        }
    }
};

template <>
struct ValueReturner<void>
{
    static Value ToValue(State* vm)
    {
        return Value();
    }
};

} // namespace Detail

template <typename T>
class ClassBuilder
{
public:
    using PropertyGetter = std::function<Value(const T& self)>;
    using PropertySetter = std::function<void(T& self, const Value& val)>;

    struct PropertyAccessors
    {
        PropertyGetter getter;
        PropertySetter setter;
    };

    explicit ClassBuilder(State& vm, const std::string& className)
        : vm_(vm), className_(className), metatable_(vm.CreateTable()), methodsTable_(vm.CreateTable())
    {
        auto accessorsMap = std::make_shared<std::unordered_map<std::string, PropertyAccessors>>();
        accessorsMap_ = accessorsMap;

        Table methods = methodsTable_;

        // Phase 1: Dispatcher Metamethods (__index and __newindex)
        metatable_.Set("__index", vm_.CreateFunction([methods, accessorsMap, className](State& vm, const std::vector<Value>& args) -> Value {
            if (args.size() < 2) return Value();

            std::string key = args[1].AsString();

            // 1. Check Methods Table first
            auto methodRes = methods.Get(key);
            if (methodRes.IsOk())
            {
                Value methodVal = methodRes.GetValue();
                if (methodVal.GetType() != ValueType::Nil)
                {
                    return methodVal;
                }
            }

            // 2. Check Property Getters
            auto it = accessorsMap->find(key);
            if (it != accessorsMap->end() && it->second.getter)
            {
                auto instance = ObjectWrap<T>::Unwrap(vm, 1);
                if (!instance)
                {
                    vm.RaiseError("Attempt to access property '" + key + "' on invalid or null " + className + " instance");
                    return Value();
                }
                try
                {
                    return it->second.getter(*instance);
                }
                catch (const std::exception& e)
                {
                    vm.RaiseError("C++ Exception in property getter '" + key + "' of " + className + ": " + e.what());
                    return Value();
                }
                catch (...)
                {
                    vm.RaiseError("Unknown C++ Exception in property getter '" + key + "' of " + className);
                    return Value();
                }
            }

            return Value();
        }));

        metatable_.Set("__newindex", vm_.CreateFunction([accessorsMap, className](State& vm, const std::vector<Value>& args) -> Value {
            if (args.size() < 3) return Value();

            std::string key = args[1].AsString();
            Value val = args[2];

            auto it = accessorsMap->find(key);
            if (it != accessorsMap->end())
            {
                if (!it->second.setter)
                {
                    vm.RaiseError("Property '" + key + "' on " + className + " is read-only");
                    return Value();
                }

                auto instance = ObjectWrap<T>::Unwrap(vm, 1);
                if (!instance)
                {
                    vm.RaiseError("Attempt to modify property '" + key + "' on invalid or null " + className + " instance");
                    return Value();
                }

                try
                {
                    it->second.setter(*instance, val);
                    return Value();
                }
                catch (const std::exception& e)
                {
                    vm.RaiseError("C++ Exception in property setter '" + key + "' of " + className + ": " + e.what());
                    return Value();
                }
                catch (...)
                {
                    vm.RaiseError("Unknown C++ Exception in property setter '" + key + "' of " + className);
                    return Value();
                }
            }

            vm.RaiseError("Property '" + key + "' does not exist on " + className);
            return Value();
        }));

        // Automatic Garbage Collection __gc
        using Holder = std::shared_ptr<T>;
        vm_.SetUserdataGC(metatable_, [](void* ptr) {
            if (ptr) {
                auto* holder = static_cast<Holder*>(ptr);
                holder->~Holder();
            }
        });
    }

    // Default Constructor
    template <typename... Args>
    ClassBuilder& Constructor()
    {
        Table meta = metatable_;
        std::string className = className_;

        methodsTable_.Set("new", vm_.CreateFunction([meta, className](State& vm, const std::vector<Value>& args) -> Value {
            try
            {
                auto instance = std::make_shared<T>();
                using Holder = std::shared_ptr<T>;
                void* userMemory = vm.CreateUserdata(sizeof(Holder));
                new (userMemory) Holder(instance);

                vm.SetUserdataMetatable(-1, meta);
                return vm.GetValue(-1);
            }
            catch (const std::exception& e)
            {
                vm.RaiseError("C++ Exception in constructor of " + className + ": " + e.what());
                return Value();
            }
        }));

        return *this;
    }

    // Custom Factory Constructor
    ClassBuilder& CustomConstructor(std::function<std::shared_ptr<T>(State& vm, const std::vector<Value>& args)> factory)
    {
        Table meta = metatable_;
        std::string className = className_;

        methodsTable_.Set("new", vm_.CreateFunction([meta, factory, className](State& vm, const std::vector<Value>& args) -> Value {
            try
            {
                auto instance = factory(vm, args);
                if (!instance) instance = std::make_shared<T>();
                using Holder = std::shared_ptr<T>;
                void* userMemory = vm.CreateUserdata(sizeof(Holder));
                new (userMemory) Holder(instance);

                vm.SetUserdataMetatable(-1, meta);
                return vm.GetValue(-1);
            }
            catch (const std::exception& e)
            {
                vm.RaiseError("C++ Exception in custom constructor of " + className + ": " + e.what());
                return Value();
            }
        }));
        return *this;
    }

    // Phase 2 & 4: Manual Method binding with zero-allocation slicing & Exception Guards
    ClassBuilder& Method(const std::string& name, const std::function<Value(T& self, State& vm, const std::vector<Value>& args)>& fn)
    {
        std::string className = className_;

        methodsTable_.Set(name, vm_.CreateFunction([fn, name, className](State& vm, const std::vector<Value>& args) -> Value {
            auto instance = ObjectWrap<T>::Unwrap(vm, 1);
            if (!instance)
            {
                vm.RaiseError("Invalid self object passed to method " + className + ":" + name);
                return Value();
            }

            try
            {
                // Zero-allocation argument passing
                if (args.size() <= 1)
                {
                    static const std::vector<Value> emptyArgs;
                    return fn(*instance, vm, emptyArgs);
                }

                std::vector<Value> methodArgs(args.begin() + 1, args.end());
                return fn(*instance, vm, methodArgs);
            }
            catch (const std::exception& e)
            {
                vm.RaiseError("C++ Exception in method " + className + ":" + name + ": " + e.what());
                return Value();
            }
            catch (...)
            {
                vm.RaiseError("Unknown C++ Exception in method " + className + ":" + name);
                return Value();
            }
        }));
        return *this;
    }

    // Phase 3: Automatic Method binding via member function pointer (Non-const & Const)
    template <typename Ret, typename... Args>
    ClassBuilder& Method(const std::string& name, Ret (T::* methodPtr)(Args...))
    {
        std::string className = className_;

        methodsTable_.Set(name, vm_.CreateFunction([methodPtr, name, className](State& vm, const std::vector<Value>& args) -> Value {
            auto instance = ObjectWrap<T>::Unwrap(vm, 1);
            if (!instance)
            {
                vm.RaiseError("Invalid self object passed to method " + className + ":" + name);
                return Value();
            }

            try
            {
                return InvokeMemberFunction<Ret, Args...>(vm, instance.get(), methodPtr, args, std::index_sequence_for<Args...>{});
            }
            catch (const std::exception& e)
            {
                vm.RaiseError("C++ Exception in method " + className + ":" + name + ": " + e.what());
                return Value();
            }
            catch (...)
            {
                vm.RaiseError("Unknown C++ Exception in method " + className + ":" + name);
                return Value();
            }
        }));
        return *this;
    }

    template <typename Ret, typename... Args>
    ClassBuilder& Method(const std::string& name, Ret (T::* methodPtr)(Args...) const)
    {
        std::string className = className_;

        methodsTable_.Set(name, vm_.CreateFunction([methodPtr, name, className](State& vm, const std::vector<Value>& args) -> Value {
            auto instance = ObjectWrap<T>::Unwrap(vm, 1);
            if (!instance)
            {
                vm.RaiseError("Invalid self object passed to method " + className + ":" + name);
                return Value();
            }

            try
            {
                return InvokeConstMemberFunction<Ret, Args...>(vm, instance.get(), methodPtr, args, std::index_sequence_for<Args...>{});
            }
            catch (const std::exception& e)
            {
                vm.RaiseError("C++ Exception in method " + className + ":" + name + ": " + e.what());
                return Value();
            }
            catch (...)
            {
                vm.RaiseError("Unknown C++ Exception in method " + className + ":" + name);
                return Value();
            }
        }));
        return *this;
    }

    // Phase 1: Property with Custom Getters and Setters
    ClassBuilder& Property(const std::string& name, PropertyGetter getter, PropertySetter setter = nullptr)
    {
        (*accessorsMap_)[name] = PropertyAccessors{ getter, setter };
        return *this;
    }

    // Phase 3: Automatic Property binding for direct C++ member variables (e.g. &Vector3::x)
    template <typename FieldType>
    ClassBuilder& Property(const std::string& name, FieldType T::* fieldPtr)
    {
        PropertyGetter getter = [fieldPtr](const T& self) -> Value {
            return Detail::ValueReturner<FieldType>::ToValue(nullptr, self.*fieldPtr);
        };

        PropertySetter setter = [fieldPtr](T& self, const Value& val) {
            self.*fieldPtr = Detail::ValueConverter<FieldType>::FromValue(nullptr, val, -1);
        };

        (*accessorsMap_)[name] = PropertyAccessors{ getter, setter };
        return *this;
    }

    // Phase 4: ToString metamethod binding
    ClassBuilder& ToString(std::function<std::string(const T& self)> fn)
    {
        std::string className = className_;

        metatable_.Set("__tostring", vm_.CreateFunction([fn, className](State& vm, const std::vector<Value>& args) -> Value {
            auto instance = ObjectWrap<T>::Unwrap(vm, 1);
            if (!instance) return Value("nullptr");

            try
            {
                return Value(fn(*instance));
            }
            catch (const std::exception& e)
            {
                vm.RaiseError("C++ Exception in __tostring of " + className + ": " + e.what());
                return Value("nullptr");
            }
        }));
        return *this;
    }

    [[nodiscard]] Table Build() const
    {
        return methodsTable_;
    }

private:
    template <typename Ret, typename... Args, size_t... Is>
    static Value InvokeMemberFunction(State& vm, T* instance, Ret (T::* methodPtr)(Args...), const std::vector<Value>& args, std::index_sequence<Is...>)
    {
        if constexpr (std::is_same_v<Ret, void>)
        {
            (instance->*methodPtr)(Detail::ValueConverter<Args>::FromValue(&vm, Is + 1 < args.size() ? args[Is + 1] : Value(), static_cast<int>(Is + 2))...);
            return Value();
        }
        else
        {
            auto res = (instance->*methodPtr)(Detail::ValueConverter<Args>::FromValue(&vm, Is + 1 < args.size() ? args[Is + 1] : Value(), static_cast<int>(Is + 2))...);
            return Detail::ValueReturner<Ret>::ToValue(&vm, std::move(res));
        }
    }

    template <typename Ret, typename... Args, size_t... Is>
    static Value InvokeConstMemberFunction(State& vm, const T* instance, Ret (T::* methodPtr)(Args...) const, const std::vector<Value>& args, std::index_sequence<Is...>)
    {
        if constexpr (std::is_same_v<Ret, void>)
        {
            (instance->*methodPtr)(Detail::ValueConverter<Args>::FromValue(&vm, Is + 1 < args.size() ? args[Is + 1] : Value(), static_cast<int>(Is + 2))...);
            return Value();
        }
        else
        {
            auto res = (instance->*methodPtr)(Detail::ValueConverter<Args>::FromValue(&vm, Is + 1 < args.size() ? args[Is + 1] : Value(), static_cast<int>(Is + 2))...);
            return Detail::ValueReturner<Ret>::ToValue(&vm, std::move(res));
        }
    }

    State& vm_;
    std::string className_;
    Table metatable_;
    Table methodsTable_;
    std::shared_ptr<std::unordered_map<std::string, PropertyAccessors>> accessorsMap_;
};

} // namespace Lode
