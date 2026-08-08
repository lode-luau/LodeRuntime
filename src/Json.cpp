// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Json.hpp"

#include "rapidjson/error/en.h"
#include "rapidjson/memorystream.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/reader.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace Lode
{

namespace
{

using rapidjson::SizeType;

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

// Handles rapidjson parse events and builds Lode values directly, skipping
// the intermediate DOM (and the second conversion pass) that the previous
// nlohmann SAX implementation produced.
struct ValueHandler : rapidjson::BaseReaderHandler<rapidjson::UTF8<>, ValueHandler>
{
    State& vm;
    size_t maxDepth = 0;
    size_t maxNodes = 0;
    size_t depth = 0;
    size_t nodes = 0;
    bool failed = false;
    std::string error;
    Value root;
    std::vector<Table> stack;
    std::vector<bool> isArray;
    std::vector<size_t> indexStack;
    std::vector<std::string> keyStack;
    std::string pendingKey;

    ValueHandler(State& vm_, size_t maxDepth_, size_t maxNodes_) : vm(vm_), maxDepth(maxDepth_), maxNodes(maxNodes_) {}

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

    void Attach(const Value& value)
    {
        if (stack.empty())
        {
            root = value;
            return;
        }
        if (isArray.back())
        {
            stack.back().Set(static_cast<int>(indexStack.back() + 1), value);
            ++indexStack.back();
        }
        else
        {
            stack.back().Set(pendingKey, value);
        }
    }

    bool StartContainer(bool array)
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
        stack.push_back(vm.CreateTable());
        isArray.push_back(array);
        indexStack.push_back(0);
        keyStack.push_back(pendingKey);
        return true;
    }

    bool EndContainer()
    {
        if (failed)
            return false;
        --depth;
        Table child = stack.back();
        stack.pop_back();
        isArray.pop_back();
        indexStack.pop_back();
        std::string childKey = std::move(keyStack.back());
        keyStack.pop_back();
        if (stack.empty())
        {
            root = Value(child);
        }
        else if (isArray.back())
        {
            stack.back().Set(static_cast<int>(indexStack.back() + 1), Value(child));
            ++indexStack.back();
        }
        else
        {
            stack.back().Set(childKey, Value(child));
        }
        return true;
    }

    bool Scalar(const Value& value)
    {
        if (failed)
            return false;
        if (!CountNode())
            return false;
        Attach(value);
        return true;
    }

    bool Null() { return Scalar(Value()); }
    bool Bool(bool b) { return Scalar(Value(b)); }
    bool Int(int i) { return Scalar(Value(static_cast<double>(i))); }
    bool Uint(unsigned u) { return Scalar(Value(static_cast<double>(u))); }
    bool Int64(int64_t i) { return Scalar(Value(static_cast<double>(i))); }
    bool Uint64(uint64_t u) { return Scalar(Value(static_cast<double>(u))); }
    bool Double(double d) { return Scalar(Value(d)); }
    bool String(const Ch* str, SizeType length, bool /*copy*/) { return Scalar(Value(std::string(str, length))); }
    bool Key(const Ch* str, SizeType length, bool /*copy*/)
    {
        if (failed)
            return false;
        if (!CountNode())
            return false;
        pendingKey.assign(str, length);
        return true;
    }
    bool StartObject() { return StartContainer(false); }
    bool StartArray() { return StartContainer(true); }
    bool EndObject(SizeType) { return EndContainer(); }
    bool EndArray(SizeType) { return EndContainer(); }
};

// Serializes a Lode value straight into a rapidjson Writer. No intermediate
// DOM: the previous implementation converted the whole tree into nlohmann
// objects first and only then dumped it.
template <typename Writer>
bool WriteValue(Writer& writer, const Value& value, std::string& error, size_t& nodes,
                size_t& depth, size_t maxNodes, size_t maxDepth)
{
    if (++nodes > maxNodes)
    {
        error = "json exceeds the maximum number of elements";
        return false;
    }
    if (depth >= maxDepth)
    {
        error = "json nesting exceeds the maximum allowed depth";
        return false;
    }
    switch (value.GetType())
    {
        case ValueType::Nil:
            writer.Null();
            return true;
        case ValueType::Boolean:
            writer.Bool(value.AsBoolean());
            return true;
        case ValueType::Integer:
            writer.Int64(value.AsInteger());
            return true;
        case ValueType::Number:
        {
            double number = value.AsNumber();
            if (!std::isfinite(number))
            {
                error = "cannot encode non-finite number";
                return false;
            }
            if (number == std::floor(number) &&
                number >= static_cast<double>(std::numeric_limits<int64_t>::min()) &&
                number <= static_cast<double>(std::numeric_limits<int64_t>::max()))
            {
                writer.Int64(static_cast<int64_t>(number));
            }
            else
            {
                writer.Double(number);
            }
            return true;
        }
        case ValueType::String:
        {
            std::string str = value.AsString();
            writer.String(str.data(), static_cast<SizeType>(str.size()));
            return true;
        }
        case ValueType::Table:
        {
            Table table = value.AsTable();
            size_t size = table.Size();
            std::vector<std::string> keys = table.GetKeys();
            ++depth;
            if (keys.empty() && size > 0)
            {
                writer.StartArray();
                for (size_t i = 1; i <= size; ++i)
                {
                    Result<Value> item = table.Get(static_cast<int>(i));
                    if (item.IsError() || item.GetValue().IsNil())
                    {
                        error = "table with holes cannot be encoded as a json array";
                        return false;
                    }
                    if (!WriteValue(writer, item.GetValue(), error, nodes, depth, maxNodes, maxDepth))
                        return false;
                }
                writer.EndArray();
            }
            else
            {
                writer.StartObject();
                for (size_t i = 1; i <= size; ++i)
                {
                    Result<Value> item = table.Get(static_cast<int>(i));
                    if (item.IsError() || item.GetValue().IsNil())
                    {
                        error = "table with holes cannot be encoded as a json object";
                        return false;
                    }
                    std::string key = std::to_string(i);
                    writer.Key(key.data(), static_cast<SizeType>(key.size()));
                    if (!WriteValue(writer, item.GetValue(), error, nodes, depth, maxNodes, maxDepth))
                        return false;
                }
                for (const auto& key : keys)
                {
                    Result<Value> item = table.Get(key);
                    if (item.IsError() || item.GetValue().IsNil())
                        continue;
                    writer.Key(key.data(), static_cast<SizeType>(key.size()));
                    if (!WriteValue(writer, item.GetValue(), error, nodes, depth, maxNodes, maxDepth))
                        return false;
                }
                writer.EndObject();
            }
            --depth;
            return true;
        }
        default:
            error = "cannot encode value of type " + TypeName(value.GetType());
            return false;
    }
}

} // namespace

Result<Value> Json::Parse(State& vm, const std::string& text, size_t maxDepth, size_t maxNodes)
{
    ValueHandler handler(vm, maxDepth, maxNodes);
    rapidjson::MemoryStream stream(text.data(), text.size());
    rapidjson::Reader reader;
    rapidjson::ParseResult result =
        reader.Parse<rapidjson::kParseDefaultFlags | rapidjson::kParseValidateEncodingFlag>(stream, handler);
    if (handler.failed)
        return Error::Runtime(handler.error);
    if (result.IsError())
        return Error::Runtime(rapidjson::GetParseError_En(result.Code()));
    return Result<Value>(std::move(handler.root));
}

Result<std::string> Json::Stringify(const Value& value, bool pretty, size_t maxDepth, size_t maxNodes)
{
    rapidjson::StringBuffer buffer;
    std::string error;
    size_t nodes = 0;
    size_t depth = 0;
    if (pretty)
    {
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        writer.SetIndent(' ', 4);
        if (!WriteValue(writer, value, error, nodes, depth, maxNodes, maxDepth))
            return Error::Runtime(error);
    }
    else
    {
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        if (!WriteValue(writer, value, error, nodes, depth, maxNodes, maxDepth))
            return Error::Runtime(error);
    }
    return Result<std::string>(std::string(buffer.GetString(), buffer.GetSize()));
}

} // namespace Lode
