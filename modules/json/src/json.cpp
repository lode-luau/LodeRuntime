// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Json.hpp"
#include "Lode/Module.hpp"
#include "Lode/State.hpp"
#include "Lode/Table.hpp"
#include "Lode/Value.hpp"
#include <vector>

LODE_MODULE(vm)
{
    Lode::Table exports = vm.CreateTable();

    exports.Set("parse", vm.CreateFunction([](Lode::State& vm, const std::vector<Lode::Value>& args) -> Lode::Value {
        std::string text = (args.size() > 0 && args[0].IsString()) ? args[0].AsString() : std::string();
        size_t maxDepth = args.size() > 1 && args[1].IsNumber() ? static_cast<size_t>(args[1].AsNumber()) : Lode::Json::DefaultMaxDepth;
        size_t maxNodes = args.size() > 2 && args[2].IsNumber() ? static_cast<size_t>(args[2].AsNumber()) : Lode::Json::DefaultMaxNodes;
        Lode::Result<Lode::Value> parsed = Lode::Json::Parse(vm, text, maxDepth, maxNodes);
        if (parsed.IsError())
        {
            vm.RaiseError(parsed.GetError().ErrorMessage());
            return Lode::Value();
        }
        return parsed.GetValue();
    }));

    exports.Set("stringify", vm.CreateFunction([](Lode::State& vm, const std::vector<Lode::Value>& args) -> Lode::Value {
        Lode::Value value = args.empty() ? Lode::Value() : args[0];
        bool pretty = args.size() > 1 && args[1].IsBoolean() && args[1].AsBoolean();
        size_t maxDepth = args.size() > 2 && args[2].IsNumber() ? static_cast<size_t>(args[2].AsNumber()) : Lode::Json::DefaultMaxDepth;
        size_t maxNodes = args.size() > 3 && args[3].IsNumber() ? static_cast<size_t>(args[3].AsNumber()) : Lode::Json::DefaultMaxNodes;
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
