// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"
#include "Lode/Result.hpp"
#include "Lode/State.hpp"
#include "Lode/Value.hpp"
#include <string>

namespace Lode
{

/**
 * @brief Provides JSON parsing and serialization for native modules.
 *
 * Backed by the nlohmann/json parser already embedded in the runtime, so native
 * modules can share one hardened implementation instead of shipping their own.
 * Parsing enforces a maximum nesting depth and a maximum element count, and
 * serialization is limited the same way, so untrusted or adversarial inputs
 * cannot exhaust the C++ stack or allocate unbounded Lua tables.
 *
 * Arrays are represented as Lua tables with contiguous integer keys starting at
 * 1; objects use string keys. On serialization, a table with no string keys and
 * dense integer keys 1..#t encodes as an array; any other table (including an
 * empty one) encodes as an object with stringified keys.
 */
class LODE_API Json
{
public:
    /** @brief Default maximum nesting depth for both parsing and serialization. */
    static constexpr size_t DefaultMaxDepth = 512;
    /** @brief Default maximum number of JSON elements (objects, arrays, scalars). */
    static constexpr size_t DefaultMaxNodes = 1'000'000;

    /**
     * @brief Parses a JSON document into a Lode::Value tree.
     * @param vm The state to create tables in.
     * @param text The JSON document to parse.
     * @param maxDepth Maximum nesting depth; deeper documents fail.
     * @param maxNodes Maximum element count; larger documents fail.
     * @return The parsed Value, or an Error describing the failure.
     */
    static Result<Value> Parse(State& vm, const std::string& text,
                               size_t maxDepth = DefaultMaxDepth,
                               size_t maxNodes = DefaultMaxNodes);

    /**
     * @brief Serializes a Lode::Value into a JSON document.
     * @param value The value to serialize (nil, bool, number, string, table).
     * @param pretty Whether to indent the output.
     * @param maxDepth Maximum nesting depth of tables; deeper values fail.
     * @param maxNodes Maximum element count; larger values fail.
     * @return The JSON text, or an Error for unsupported or out-of-limit values.
     */
    static Result<std::string> Stringify(const Value& value, bool pretty = false,
                                         size_t maxDepth = DefaultMaxDepth,
                                         size_t maxNodes = DefaultMaxNodes);
};

} // namespace Lode
