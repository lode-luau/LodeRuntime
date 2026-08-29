// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Json.hpp"
#include "Lode/Module.hpp"
#include "Lode/State.hpp"
#include "Lode/Table.hpp"
#include "Lode/Value.hpp"
#include <cmath>
#include <string>
#include <vector>

namespace
{
    // Converts an optional numeric limit argument into a size_t, validating it
    // as a finite non-negative integer. Without this check a negative number
    // such as -1 silently wraps into a gigantic size_t, disabling the very
    // protection the limit exists to provide.
    size_t ResolveLimit(Lode::State& vm, const Lode::Value& value, size_t fallback, const char* name)
    {
        if (!value.IsNumber())
            return fallback;
        const double n = value.AsNumber();
        if (!(n >= 0.0) || n != std::floor(n)) // rejects NaN, negatives and infinities
        {
            vm.RaiseError(std::string("json: ") + name + " must be a non-negative integer");
            return fallback;
        }
        return static_cast<size_t>(n);
    }
}

LODE_MODULE(vm)
{
    Lode::Table exports = vm.CreateTable();

    exports.Set("parse", vm.CreateFastFunctionNoYield([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        std::string text = (args.Size() > 0 && args[0].IsString()) ? std::string(args[0].AsStringView()) : std::string();
        size_t maxDepth = ResolveLimit(vm, args.Size() > 1 ? args[1].ToValue() : Lode::Value(), Lode::Json::DefaultMaxDepth, "maxDepth");
        size_t maxNodes = ResolveLimit(vm, args.Size() > 2 ? args[2].ToValue() : Lode::Value(), Lode::Json::DefaultMaxNodes, "maxNodes");
        Lode::Result<Lode::Value> parsed = Lode::Json::Parse(vm, text, maxDepth, maxNodes);
        if (parsed.IsError())
        {
            vm.RaiseError(parsed.GetError().ErrorMessage());
            return Lode::Value();
        }
        return parsed.GetValue();
    }));

    exports.Set("stringify", vm.CreateFastFunctionNoYield([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        Lode::Value value = args.Size() > 0 ? args[0].ToValue() : Lode::Value();
        bool pretty = args.Size() > 1 && args[1].IsBoolean() && args[1].AsBoolean();
        size_t maxDepth = ResolveLimit(vm, args.Size() > 2 ? args[2].ToValue() : Lode::Value(), Lode::Json::DefaultMaxDepth, "maxDepth");
        size_t maxNodes = ResolveLimit(vm, args.Size() > 3 ? args[3].ToValue() : Lode::Value(), Lode::Json::DefaultMaxNodes, "maxNodes");
        Lode::Result<std::string> encoded = Lode::Json::Stringify(value, pretty, maxDepth, maxNodes);
        if (encoded.IsError())
        {
            vm.RaiseError(encoded.GetError().ErrorMessage());
            return Lode::Value();
        }
        return Lode::Value(encoded.GetValue());
    }));

    return exports;
}
