// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Json.hpp"

#include "nlohmann/json.hpp"

#include <cstdint>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Lode
{

namespace
{

std::string CleanMessage(const std::exception& e)
{
    std::string message = e.what();
    const std::string prefix = "[json.exception.";
    size_t start = message.find(prefix);
    size_t end = start == std::string::npos ? std::string::npos : message.find("] ", start);
    if (start != std::string::npos && end != std::string::npos && end + 2 <= message.size())
        return message.substr(end + 2);
    return message;
}

Value FromJson(State& vm, const nlohmann::json& json, size_t& nodes, size_t maxNodes)
{
    if (++nodes > maxNodes)
        throw std::runtime_error("json exceeds the maximum number of elements");
    switch (json.type())
    {
        case nlohmann::json::value_t::null:
            return Value();
        case nlohmann::json::value_t::boolean:
            return Value(json.get<bool>());
        case nlohmann::json::value_t::number_integer:
            return Value(static_cast<double>(json.get<int64_t>()));
        case nlohmann::json::value_t::number_unsigned:
            return Value(static_cast<double>(json.get<uint64_t>()));
        case nlohmann::json::value_t::number_float:
            return Value(json.get<double>());
        case nlohmann::json::value_t::string:
            return Value(json.get<std::string>());
        case nlohmann::json::value_t::array:
        {
            Table table = vm.CreateTable();
            int index = 1;
            for (const auto& item : json)
                table.Set(index++, FromJson(vm, item, nodes, maxNodes));
            return Value(table);
        }
        case nlohmann::json::value_t::object:
        {
            Table table = vm.CreateTable();
            for (auto it = json.begin(); it != json.end(); ++it)
                table.Set(it.key(), FromJson(vm, it.value(), nodes, maxNodes));
            return Value(table);
        }
        default:
            return Value();
    }
}

std::string TypeName(ValueType type)
{
    switch (type)
    {
        case ValueType::Nil: return "nil";
        case ValueType::Boolean: return "boolean";
        case ValueType::Number: return "number";
        case ValueType::Integer: return "integer";
        case ValueType::Vector: return "vector";
        case ValueType::String: return "string";
        case ValueType::Table: return "table";
        case ValueType::Function: return "function";
        case ValueType::Thread: return "thread";
        case ValueType::Userdata: return "userdata";
        case ValueType::LightUserdata: return "lightuserdata";
        case ValueType::Buffer: return "buffer";
    }
    return "unknown";
}

struct LimitedSax : nlohmann::json::json_sax_t
{
    using json = nlohmann::json;

    size_t maxDepth = 0;
    size_t maxNodes = 0;
    size_t depth = 0;
    size_t nodes = 0;
    bool failed = false;
    std::string error;
    json root;
    std::vector<json> stack;
    std::vector<std::string> keyStack;
    std::string pendingKey;

    LimitedSax(size_t maxDepth_, size_t maxNodes_) : maxDepth(maxDepth_), maxNodes(maxNodes_) {}

    bool CountNode()
    {
        if (++nodes > maxNodes)
        {
            failed = true;
            error = "json exceeds the maximum number of elements";
            return false;
        }
        return true;
    }

    bool StartContainer(json&& container)
    {
        if (failed)
            return false;
        if (depth >= maxDepth)
        {
            failed = true;
            error = "json nesting exceeds the maximum allowed depth";
            return false;
        }
        if (!CountNode())
            return false;
        ++depth;
        stack.push_back(std::move(container));
        keyStack.push_back(pendingKey);
        return true;
    }

    bool EndContainer()
    {
        if (failed)
            return false;
        --depth;
        json child = std::move(stack.back());
        stack.pop_back();
        std::string childKey = std::move(keyStack.back());
        keyStack.pop_back();
        if (stack.empty())
        {
            root = std::move(child);
        }
        else if (stack.back().is_array())
        {
            stack.back().push_back(std::move(child));
        }
        else
        {
            stack.back()[childKey] = std::move(child);
        }
        return true;
    }

    bool HandleValue(json&& value)
    {
        if (failed)
            return false;
        if (!CountNode())
            return false;
        if (stack.empty())
        {
            root = std::move(value);
        }
        else if (stack.back().is_array())
        {
            stack.back().push_back(std::move(value));
        }
        else
        {
            stack.back()[pendingKey] = std::move(value);
        }
        return true;
    }

    bool start_object(std::size_t) override { return StartContainer(json(json::value_t::object)); }
    bool start_array(std::size_t) override { return StartContainer(json(json::value_t::array)); }
    bool end_object() override { return EndContainer(); }
    bool end_array() override { return EndContainer(); }
    bool key(string_t& val) override
    {
        if (failed)
            return false;
        if (!CountNode())
            return false;
        pendingKey = val;
        return true;
    }
    bool null() override { return HandleValue(json(nullptr)); }
    bool boolean(bool val) override { return HandleValue(json(val)); }
    bool number_integer(number_integer_t val) override { return HandleValue(json(val)); }
    bool number_unsigned(number_unsigned_t val) override { return HandleValue(json(val)); }
    bool number_float(number_float_t val, const string_t&) override { return HandleValue(json(val)); }
    bool string(string_t& val) override { return HandleValue(json(val)); }
    bool binary(binary_t&) override
    {
        failed = true;
        error = "json binary values are not supported";
        return false;
    }
    bool parse_error(std::size_t position, const std::string& token, const json::exception& ex) override
    {
        failed = true;
        error = CleanMessage(ex);
        (void)position;
        (void)token;
        return false;
    }
};

void ToJson(const Value& value, nlohmann::json& out, std::string& error, size_t& nodes,
            size_t& depth, size_t maxNodes, size_t maxDepth);

// Converts the 1..size indexed range of a table, failing when a slot is
// missing so the result is a dense array/object without holes.
template <typename Sink>
bool ConvertIndexed(const Table& table, size_t size, const char* holeError, std::string& error,
                    size_t& nodes, size_t& depth, size_t maxNodes, size_t maxDepth, Sink&& sink)
{
    for (size_t i = 1; i <= size; ++i)
    {
        Result<Value> item = table.Get(static_cast<int>(i));
        if (item.IsError() || item.GetValue().IsNil())
        {
            error = holeError;
            return false;
        }
        nlohmann::json converted;
        ToJson(item.GetValue(), converted, error, nodes, depth, maxNodes, maxDepth);
        if (!error.empty())
            return false;
        sink(i, std::move(converted));
    }
    return true;
}

void ToJson(const Value& value, nlohmann::json& out, std::string& error, size_t& nodes,
            size_t& depth, size_t maxNodes, size_t maxDepth)
{
    if (++nodes > maxNodes)
    {
        error = "json exceeds the maximum number of elements";
        return;
    }
    if (depth >= maxDepth)
    {
        error = "json nesting exceeds the maximum allowed depth";
        return;
    }
    switch (value.GetType())
    {
        case ValueType::Nil:
            out = nullptr;
            break;
        case ValueType::Boolean:
            out = value.AsBoolean();
            break;
        case ValueType::Integer:
            out = value.AsInteger();
            break;
        case ValueType::Number:
        {
            double number = value.AsNumber();
            if (!std::isfinite(number))
            {
                error = "cannot encode non-finite number";
                return;
            }
            if (number == std::floor(number) &&
                number >= static_cast<double>(std::numeric_limits<int64_t>::min()) &&
                number <= static_cast<double>(std::numeric_limits<int64_t>::max()))
            {
                out = static_cast<int64_t>(number);
            }
            else
            {
                out = number;
            }
            break;
        }
        case ValueType::String:
            out = value.AsString();
            break;
        case ValueType::Table:
        {
            Table table = value.AsTable();
            size_t size = table.Size();
            std::vector<std::string> keys = table.GetKeys();
            ++depth;
            if (keys.empty() && size > 0)
            {
                nlohmann::json array = nlohmann::json::array();
                bool ok = ConvertIndexed(table, size, "table with holes cannot be encoded as a json array",
                                         error, nodes, depth, maxNodes, maxDepth,
                                         [&](size_t, nlohmann::json&& converted) { array.push_back(std::move(converted)); });
                if (!ok) return;
                out = std::move(array);
            }
            else
            {
                nlohmann::json object = nlohmann::json::object();
                bool ok = ConvertIndexed(table, size, "table with holes cannot be encoded as a json object",
                                         error, nodes, depth, maxNodes, maxDepth,
                                         [&](size_t i, nlohmann::json&& converted) { object[std::to_string(i)] = std::move(converted); });
                if (!ok) return;
                for (const auto& key : keys)
                {
                    Result<Value> item = table.Get(key);
                    if (item.IsError() || item.GetValue().IsNil())
                        continue;
                    nlohmann::json converted;
                    ToJson(item.GetValue(), converted, error, nodes, depth, maxNodes, maxDepth);
                    if (!error.empty())
                        return;
                    object[key] = std::move(converted);
                }
                out = std::move(object);
            }
            --depth;
            break;
        }
        default:
            error = "cannot encode value of type " + TypeName(value.GetType());
            break;
    }
}

} // namespace

Result<Value> Json::Parse(State& vm, const std::string& text, size_t maxDepth, size_t maxNodes)
{
    try
    {
        LimitedSax sax(maxDepth, maxNodes);
        if (!nlohmann::json::sax_parse(text, &sax))
        {
            if (sax.failed)
                return Error::Runtime(sax.error);
            return Error::Runtime("json parse error");
        }
        if (sax.failed)
            return Error::Runtime(sax.error);
        size_t nodes = 0;
        return Result<Value>(FromJson(vm, sax.root, nodes, maxNodes));
    }
    catch (const std::exception& e)
    {
        return Error::Runtime(CleanMessage(e));
    }
}

Result<std::string> Json::Stringify(const Value& value, bool pretty, size_t maxDepth, size_t maxNodes)
{
    nlohmann::json document;
    std::string error;
    size_t nodes = 0;
    size_t depth = 0;
    ToJson(value, document, error, nodes, depth, maxNodes, maxDepth);
    if (!error.empty())
        return Error::Runtime(error);
    return Result<std::string>(document.dump(pretty ? 4 : -1));
}

} // namespace Lode
